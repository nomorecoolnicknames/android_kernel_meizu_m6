// SPDX-License-Identifier: GPL-2.0
/*
 * forge_m681_marker.c - persistent DRAM stage markers for m681 (MT6755) bring-up.
 *
 * m681 attempt3: ported from kernel-nokia-5.1-m681/init/forge_m681_marker.c
 * (the surviving vgdn sibling) into the native Meizu 3.18.35 99degree tree.
 *
 * Background
 * ----------
 * On MT6755 the kernel dies in early boot before the MTK ram_console driver
 * registers a console.  Anything emitted with pr_emerg() in start_kernel()
 * before that lives only in the early printk ring buffer, which is wiped by the
 * WDT reset.  We need a few breadcrumb bytes that survive a WDT.
 *
 * Target: physical 0x46100000 (size 0x1000).  This lies inside the DTB
 * reserved-memory node "@46000000" (reg <0 0x46000000 0 0x400000>) in
 * arch/arm64/boot/dts/mt6755.dtsi, so it is reserved from the kernel allocator
 * and is the vgdn-proven WDT-surviving marker location.  We do NOT use
 * 0x44400000 (clean ram_console; overwritten by recovery) or SPM 0x10006000
 * (SPM driver ioremap conflict).
 *
 * Write offsets are within the FIRST 512 BYTES of the region.  Multiple guarded
 * slots are written so a reader can cross-check signature/raw/inverted guards
 * and ignore any stale value.
 *
 * Recovery inspection (requires CONFIG_DEVMEM=y kernel = TWRP devmem build):
 *   adb shell devmem 0x46100100      -> rolling stage (0xF681xx)
 *   adb shell devmem 0x46100104      -> last reached stage (raw u32)
 *   adb shell devmem 0x46100108      -> 0x46524745 ('FRGE') if our kernel ran
 *   adb shell dd if=/dev/mem of=/sdcard/dram-head.bin bs=4096 skip=287489 count=1
 *
 * COLD-SAFE channel: each marker ALSO emits pr_emerg("[FORGE_M681] stage 0xNN")
 * so if the kernel reaches printk + ram_console the stage is in the normal log
 * that MTK kedump flushes to the expdb partition (FLASH, survives cold/BROM).
 *
 * Timing
 * ------
 * forge_m681_marker_early_init() must run AFTER early_ioremap_init() (stage
 * A04 in setup_arch).  Before that forge_m681_mark() is a silent no-op.  After
 * mm_init() the marker switches to a permanent ioremap() so the late_initcall
 * "early ioremap leak" check stays quiet.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <asm/early_ioremap.h>

#include "forge_m681_marker.h"

/* m681 v27: primary readout moved 0x46100000 -> 0x444f0000 (minirdump reserved
 * node, mt6755.dtsi "minirdump-reserved-memory@444f0000", size 0x10000). RATIONALE
 * (FACT): 0x46100000 lies INSIDE lk-reserved (0x46000000+0x400000) so the warm
 * WDT->recovery path RE-RUNS lk and overwrites it with lk rodata/strings before
 * we can read it (confirmed v26: read back lk "[LK]jump to K64" strings, not our
 * mark). minirdump@0x444f0000 is reserved by BOTH our kernel and TWRP and is
 * written by NOBODY except the minirdump driver on an actual crash-dump (we hang,
 * not crash), so a forge mark written here survives lk-rerun + TWRP recovery +
 * the of_platform_populate probe phase. Read from TWRP:
 *   dd if=/dev/mem bs=512 skip=2238336 (=0x444f0000/512). */
#define FORGE_MARKER_PHYS_BASE	0x444f0000UL
#define FORGE_MARKER_REGION_SIZE	0x1000UL	/* one page for the header */

/*
 * Secondary (TWRP-readable) marker region. attempt #5 (2026-06-17):
 * 0x46100000 (lk-reserved) is overwritten by TWRP's recovery ramdisk
 * (ramdisk_addr 0x45000000) on the warm WDT->recovery path, so it reads back as
 * TWRP runtime data and the stage is lost. The pstore reserved-memory node
 * @0x44410000 (size 0xe0000) sits BELOW the recovery ramdisk, is >= the DRAM
 * base (so STRICT_DEVMEM permits `dd if=/dev/mem` readback from TWRP), and was
 * observed all-zero / preserved across a TWRP boot. We dual-write here; read the
 * last stage after a hang with:  dd if=/dev/mem bs=4 skip=0x44410104/4 count=1
 * (stage @0x44410104, sig 'FRGE' @0x44410108).
 */
/* m681 v26: moved off 0x44410000 (the pstore region, which the pstore driver
 * overwrites during of_platform_populate -> marker reads went valid=False/garbage
 * in the driver-probe phase) to a dedicated no-map reserved node at 0x44500000
 * (see forge-marker-reserved-memory@44500000 in mt6755.dtsi). Read from TWRP:
 * dd if=/dev/mem bs=512 skip=2238464 (=0x44500000/512). */
/* m681 v27: secondary independent survivor moved 0x44410000(pstore) -> 0x44800000
 * (preloader reserved node, "preloader-reserved-memory@44800000", size 0x100000).
 * pstore@0x44410000 is overwritten by the pstore driver DURING of_platform_populate
 * -> useless precisely in the probe-phase where we now hang. preloader region is
 * written only by the preloader (pre-lk) then static, reserved by both kernels,
 * so it is a second readout that also survives the probe phase + recovery fallback.
 * Read from TWRP:  dd if=/dev/mem bs=512 skip=2244608 (=0x44800000/512). */
#define FORGE_MARKER2_PHYS_BASE	0x44800000UL

/*
 * m681 bring-up v56: MTK toprgu watchdog (0x10007000) kick channel.
 *
 * Root cause of the ~95s boot death (4-agent RE consensus, 2026-06-19): the WDT
 * is armed at postcore_initcall (mtk_wdt_probe, level 2) with a 30s timeout and
 * kicked once, but the kicker kthread only starts at late_initcall (level 7).
 * Every level 4/5/6 initcall therefore runs with NO kick, so the HW dog fires
 * mid-boot at whatever initcall happened to be running (v55 marker was
 * xfrm6_tunnel_init = device_initcall entry 471/494, 95% through level 6) --
 * this is a watchdog cut of a PROGRESSING boot, not a genuine hang.
 *
 * forge_m681_wdt_kick() is called once per initcall from do_one_initcall() to
 * reload the counter, letting a progressing boot reach /init while still letting
 * a genuinely-wedged initcall (never returns -> never kicks) trip the dog after
 * ~30s so the device still warm-resets to TWRP and the marker localises it.
 */
#define FORGE_WDT_PHYS_BASE	0x10007000UL
#define FORGE_WDT_REGION_SIZE	0x100UL
#define FORGE_WDT_RESTART_OFF	0x08	/* MTK_WDT_RESTART */
#define FORGE_WDT_RESTART_KEY	0x1971U	/* MTK_WDT_RESTART_KEY */
#define FORGE_WDT_MODE_OFF	0x00	/* MTK_WDT_MODE */
#define FORGE_WDT_LENGTH_OFF	0x04	/* MTK_WDT_LENGTH */
#define FORGE_WDT_MODE_KEY	0x22000000U
#define FORGE_WDT_MODE_ENABLE	0x00000001U
#define FORGE_WDT_MODE_EXTEN	0x00000004U
#define FORGE_WDT_MODE_AUTO_RESTART 0x00000010U
#define FORGE_WDT_LENGTH_KEY	0x00000008U
#define FORGE_WDT_TIMEOUT_SEC	30U

/* Offsets inside the first 512 bytes (never zeroed by ram_console memset_io,
 * which starts at off_linux >= 512). */
#define FORGE_MARKER_LEGACY_OFF	0x100
#define FORGE_MARKER_SHADOW_OFF	0x180
#define FORGE_SLOT_ROLLING_OFF	0x00	/* 0xF6810000 | stage */
#define FORGE_SLOT_STAGE_OFF	0x04	/* raw stage */
#define FORGE_SLOT_SIG_OFF	0x08	/* magic signature 'FRGE' */
#define FORGE_SLOT_AUX_OFF	0x0C	/* auxiliary u32 payload */
#define FORGE_SLOT_SIG_INV_OFF	0x10	/* inverted signature guard */
#define FORGE_SLOT_AUX_INV_OFF	0x14	/* inverted auxiliary guard */
#define FORGE_SLOT_STAGE_INV_OFF	0x18	/* inverted raw stage guard */
#define FORGE_SLOT_ROLLING_INV_OFF	0x1C	/* inverted rolling guard */

#define FORGE_MARKER_MAGIC	0xF6810000U	/* 'F6810000' | stage */
#define FORGE_SIGNATURE		0x46524745U	/* ASCII 'FRGE' */

/*
 * forge_spm_base is exported so the marker engine is reachable from the inline
 * call sites in init/main.c and arch/arm64/kernel/setup.c.  It is updated twice
 * during boot: first by the early_ioremap path, then by the permanent ioremap
 * path.  Until the first update it is NULL and forge_m681_mark() is a no-op.
 */
void __iomem *forge_spm_base;
EXPORT_SYMBOL(forge_spm_base);

/* Secondary marker base (pstore @0x44410000); TWRP-readable. */
void __iomem *forge_spm_base2;
EXPORT_SYMBOL(forge_spm_base2);

/* m681 v56: ioremap of the MTK toprgu watchdog block (see forge_m681_wdt_kick). */
static void __iomem *forge_wdt_base;

/*
 * m681 v57: the driver's own DT-mapped (of_iomap) toprgu base, set in
 * mtk_wdt_probe() at postcore_initcall (level 2).  This is the PROVEN-functional
 * mapping (the WDT is demonstrably armed via it).  Our private start_kernel
 * ioremap() of 0x10007000 (forge_wdt_base) may return NULL/non-functional that
 * early for SoC register space, so prefer toprgu_base once it is live.
 */
extern void __iomem *toprgu_base;

/* v57: per-initcall kick counter, mirrored into the TWRP-readable marker region
 * at offset 0x40 (count) and 0x44 (which bases were non-NULL) so a post-reset
 * readback proves whether the kick ran and how far the boot progressed. */
static u32 forge_wdt_kick_count;

/* Diagnostic offsets in the marker page (free; real slots start at 0x100). */
#define FORGE_DIAG_KICKCNT_OFF	0x40
#define FORGE_DIAG_KICKFLAGS_OFF	0x44

static const unsigned long forge_m681_slot_offsets[] = {
	FORGE_MARKER_LEGACY_OFF,
	0x120, 0x140, 0x160, FORGE_MARKER_SHADOW_OFF,
	0x1a0, 0x1c0, 0x1e0,
};

static void forge_m681_write_slot(void __iomem *base, unsigned long off,
				  u8 stage, u32 aux)
{
	u32 raw = (u32)stage;
	u32 rolling = FORGE_MARKER_MAGIC | raw;

	/*
	 * Guard words come before the final rolling marker.  A reader trusts a
	 * slot only when signature, rolling, raw stage, and inverted guards agree.
	 */
	writel(FORGE_SIGNATURE, base + off + FORGE_SLOT_SIG_OFF);
	writel(aux, base + off + FORGE_SLOT_AUX_OFF);
	writel(~FORGE_SIGNATURE, base + off + FORGE_SLOT_SIG_INV_OFF);
	writel(~aux, base + off + FORGE_SLOT_AUX_INV_OFF);
	writel(raw, base + off + FORGE_SLOT_STAGE_OFF);
	writel(~raw, base + off + FORGE_SLOT_STAGE_INV_OFF);
	writel(rolling, base + off + FORGE_SLOT_ROLLING_OFF);
	writel(~rolling, base + off + FORGE_SLOT_ROLLING_INV_OFF);
}

void forge_m681_mark_aux(u8 stage, u32 aux)
{
	unsigned int i;

	/* m681 v12: write the marker slots FIRST and DO NOT pr_emerg in the hot
	 * path.  Previously this did pr_emerg() BEFORE the writels, so if printk
	 * ever deadlocks/blocks (console_sem, logbuf, an undrained console) the
	 * mark would (a) block the boot thread and (b) hide the true furthest
	 * stage (the next mark hangs in pr_emerg before recording).  v11 showed a
	 * deterministic stop at 0xD7 with NO exception and NO IRQ (count=0) and
	 * 0xC1 absent -- exactly what a pr_emerg hang on the NEXT mark looks like.
	 * Pure writel makes the marker truthful and cannot block boot. */
	if (forge_spm_base)
		for (i = 0; i < ARRAY_SIZE(forge_m681_slot_offsets); i++)
			forge_m681_write_slot(forge_spm_base,
					      forge_m681_slot_offsets[i], stage, aux);

	if (forge_spm_base2)
		for (i = 0; i < ARRAY_SIZE(forge_m681_slot_offsets); i++)
			forge_m681_write_slot(forge_spm_base2,
					      forge_m681_slot_offsets[i], stage, aux);
}
EXPORT_SYMBOL(forge_m681_mark_aux);

void forge_m681_mark(u8 stage)
{
	forge_m681_mark_aux(stage, 0);
}
EXPORT_SYMBOL(forge_m681_mark);

/* m681: record a fatal-fault snapshot from die(). PC -> slots as stage 0xFE
 * (aux=pc low32); LR and ESR -> fixed diag offsets 0x48/0x4c of the marker
 * page so all three survive the reset and are readable from TWRP. */
void forge_m681_mark_fault(u32 pc, u32 lr, u32 esr)
{
	forge_m681_mark_aux(0xFE, pc);
	if (forge_spm_base) {
		writel(lr,  forge_spm_base  + 0x48);
		writel(esr, forge_spm_base  + 0x4c);
	}
	if (forge_spm_base2) {
		writel(lr,  forge_spm_base2 + 0x48);
		writel(esr, forge_spm_base2 + 0x4c);
	}
}
EXPORT_SYMBOL(forge_m681_mark_fault);

/*
 * m681 v10: universal fault/panic net.  die() (0xFE) only catches faults that
 * route through it; bad_mode->panic and direct panic() bypass it.  These hooks
 * snapshot the FIRST fatal event from ANY sink (do_mem_abort unhandled=0xFD,
 * bad_mode=0xFC, panic=0xFB) so we learn whether the 0xD7->reset is a die-able
 * fault, a bad_mode/panic, or (if NOTHING latches) a true IRQ-off hang.
 *
 * Raw slot writes ONLY -- no pr_emerg: we may be in a wedged/lock-held context
 * where printk would deadlock or re-fault.  A one-shot latch keeps the ROOT
 * event (first to fire) instead of a downstream panic overwriting it.
 *
 * diag layout for the latched snapshot:
 *   slot @0x100/@0x180 stage = sink id, aux = PC low32
 *   0x48 = PC low32   0x4c = ESR   0x50 = fault addr/aux2   0x54 = sink id
 */
static int forge_fault_latched;

void forge_m681_fault_snap(u8 stage, u32 pc, u32 esr, u32 addr)
{
	if (forge_fault_latched)
		return;
	forge_fault_latched = 1;

	if (forge_spm_base) {
		forge_m681_write_slot(forge_spm_base, FORGE_MARKER_LEGACY_OFF, stage, pc);
		forge_m681_write_slot(forge_spm_base, FORGE_MARKER_SHADOW_OFF, stage, pc);
		writel(pc,    forge_spm_base + 0x48);
		writel(esr,   forge_spm_base + 0x4c);
		writel(addr,  forge_spm_base + 0x50);
		writel(stage, forge_spm_base + 0x54);
	}
	if (forge_spm_base2) {
		forge_m681_write_slot(forge_spm_base2, FORGE_MARKER_LEGACY_OFF, stage, pc);
		forge_m681_write_slot(forge_spm_base2, FORGE_MARKER_SHADOW_OFF, stage, pc);
		writel(pc,    forge_spm_base2 + 0x48);
		writel(esr,   forge_spm_base2 + 0x4c);
		writel(addr,  forge_spm_base2 + 0x50);
		writel(stage, forge_spm_base2 + 0x54);
	}
}
EXPORT_SYMBOL(forge_m681_fault_snap);

/*
 * m681 v11: per-IRQ trace from the EL1 IRQ entry (gic_handle_irq).  Records a
 * free-running count, the last hwirq, and the INTERRUPTED pc into diag offsets
 * 0x58/0x5c/0x60.  Purpose: the 0xD7->silent-reset is not a CPU exception
 * (v10 proved no die/bad_mode/abort/panic), so the leading hypothesis is an
 * IRQ STORM (a board peripheral asserting an interrupt that no loaded driver
 * clears yet -- a classic MTK bring-up failure, and board/DTB-dependent which
 * matches m6==config-but-boots).  If post-reset count is enormous and the
 * interrupted pc sits in the 0xD7 window -> storm CONFIRMED + the exact irq.
 * No printk; writel only; no-op until the marker region is mapped.
 */
static u32 forge_irq_count;

void forge_m681_irq_trace(u32 irqnr, u32 pc)
{
	forge_irq_count++;
	if (forge_spm_base2) {
		writel(forge_irq_count, forge_spm_base2 + 0x58);
		writel(irqnr,           forge_spm_base2 + 0x5c);
		writel(pc,              forge_spm_base2 + 0x60);
	}
	if (forge_spm_base) {
		writel(forge_irq_count, forge_spm_base + 0x58);
		writel(irqnr,           forge_spm_base + 0x5c);
		writel(pc,              forge_spm_base + 0x60);
	}
}
EXPORT_SYMBOL(forge_m681_irq_trace);

/*
 * forge_m681_wdt_arm - explicitly arm the MTK toprgu hardware watchdog.
 *
 * mtk_wdt_probe is in the v38 platform denylist (never runs), so nobody
 * arms the HW WDT.  The forge kick channel only PETS an already-armed
 * WDT; if it was never armed, petting does nothing and a kernel hang
 * never triggers a WDT reset (v44 first flash: hung forever, required
 * manual hard reset, SRAM marker bit-rotted).
 *
 * This writes WDT_LENGTH (30s timeout) and WDT_MODE (enable + ext reset
 * + auto restart + key) using the same register values as the driver's
 * mtk_wdt_set_timeout() + mtk_wdt_mode_config().  Called once from
 * forge_m681_marker_late_init() in start_kernel, before any initcalls.
 */
static void forge_m681_wdt_arm(void)
{
	void __iomem *b = forge_wdt_base;
	u32 timeout;

	if (!b)
		return;

	timeout = (FORGE_WDT_TIMEOUT_SEC * (1 << 6)) << 5;
	writel(timeout | FORGE_WDT_LENGTH_KEY, b + FORGE_WDT_LENGTH_OFF);

	writel(FORGE_WDT_MODE_KEY | FORGE_WDT_MODE_ENABLE |
	       FORGE_WDT_MODE_EXTEN | FORGE_WDT_MODE_AUTO_RESTART,
	       b + FORGE_WDT_MODE_OFF);

	writel(FORGE_WDT_RESTART_KEY, b + FORGE_WDT_RESTART_OFF);

	pr_emerg("[FORGE_M681] WDT armed: %us timeout, mode=0x%x length=0x%x\n",
		 FORGE_WDT_TIMEOUT_SEC,
		 FORGE_WDT_MODE_KEY | FORGE_WDT_MODE_ENABLE |
		 FORGE_WDT_MODE_EXTEN | FORGE_WDT_MODE_AUTO_RESTART,
		 timeout | FORGE_WDT_LENGTH_KEY);
}

/*
 * forge_m681_wdt_kick - pet the MTK toprgu watchdog from do_one_initcall().
 *
 * No-op until forge_m681_marker_late_init() maps the toprgu block (which happens
 * in start_kernel(), before any do_initcalls() runs).  Writing the restart key
 * reloads the WDT counter (same effect as the driver's mtk_wdt_restart()), which
 * keeps a progressing boot alive across the kicker-less level 4/5/6 initcall
 * window without masking a real per-initcall hang.
 */
void forge_m681_wdt_kick(void)
{
	void __iomem *b = toprgu_base ? toprgu_base : forge_wdt_base;
	u32 flags;

	forge_wdt_kick_count++;
	if (b)
		writel(FORGE_WDT_RESTART_KEY, b + FORGE_WDT_RESTART_OFF);

	/* TWRP-readable breadcrumb: how many initcalls kicked, and which base
	 * was live (bit0=our ioremap, bit4=driver toprgu_base). */
	flags = 0xC0DE0000U | (toprgu_base ? 0x10 : 0) | (forge_wdt_base ? 0x01 : 0);
	if (forge_spm_base2) {
		writel(forge_wdt_kick_count, forge_spm_base2 + FORGE_DIAG_KICKCNT_OFF);
		writel(flags, forge_spm_base2 + FORGE_DIAG_KICKFLAGS_OFF);
	}
	if (forge_spm_base) {
		writel(forge_wdt_kick_count, forge_spm_base + FORGE_DIAG_KICKCNT_OFF);
		writel(flags, forge_spm_base + FORGE_DIAG_KICKFLAGS_OFF);
	}
}
EXPORT_SYMBOL(forge_m681_wdt_kick);

/*
 * m681 v34: dedicated, non-overwritable "current initcall fn" tracker.
 *
 * The rolling slots only ever hold the LAST mark, and the inner platform-probe
 * marks (0xC0 attempt / 0xCD denylist-skip) clobber the per-initcall 0xE0 aux.
 * So after a hang inside an initcall that itself probes platform drivers, the
 * marker names the last PROBE, not the wedged INITCALL.  These fixed diag
 * offsets are written ONLY here, so they always name the initcall we entered
 * last (and whether it returned).  fn is mirrored to four offsets because the
 * preloader survivor region (0x44800000) bit-rots single copies; the reader
 * OR-reconstructs the true fn.  seq is the do_one_initcall ordinal.
 *
 * diag layout:  0x64/0x70/0x74/0x78 = current fn (OR these)   0x68 = seq
 *               0x6c = last fn that RETURNED (entered != returned => wedged)
 */
static u32 forge_initcall_seq;

static void forge_init_write(void __iomem *b, u32 fn)
{
	writel(fn, b + 0x64);
	writel(fn, b + 0x70);
	writel(fn, b + 0x74);
	writel(fn, b + 0x78);
	writel(forge_initcall_seq, b + 0x68);
}

void forge_m681_set_initcall(u32 fn)
{
	forge_initcall_seq++;
	if (forge_spm_base)
		forge_init_write(forge_spm_base, fn);
	if (forge_spm_base2)
		forge_init_write(forge_spm_base2, fn);
}
EXPORT_SYMBOL(forge_m681_set_initcall);

void forge_m681_set_initcall_done(u32 fn)
{
	if (forge_spm_base)
		writel(fn, forge_spm_base + 0x6c);
	if (forge_spm_base2)
		writel(fn, forge_spm_base2 + 0x6c);
}
EXPORT_SYMBOL(forge_m681_set_initcall_done);

/*
 * m681 v35: of_platform_populate node tracker.  arm64_device_init wedges inside
 * of_platform_populate AFTER mtk_wdt's probe was denied, with NO subsequent
 * 0xC0 (so the hang is in of-core device CREATION, not a driver probe).  This
 * marks every node reaching of_platform_device_create_pdata so the post-reset
 * field names the exact wedged node.
 *
 * diag layout:  0x80/0x84/0x88/0x8c = DFS node counter (OR the 4 copies)
 *               0x90..0x9c = 16 ASCII bytes of full_name (copy A)
 *               0xa0..0xac = same 16 ASCII bytes (copy B; reader ORs A|B)
 */
static u32 forge_ofnode_seq;

static void forge_ofnode_write(void __iomem *b, const u32 *w)
{
	writel(forge_ofnode_seq, b + 0x80);
	writel(forge_ofnode_seq, b + 0x84);
	writel(forge_ofnode_seq, b + 0x88);
	writel(forge_ofnode_seq, b + 0x8c);
	writel(w[0], b + 0x90); writel(w[1], b + 0x94);
	writel(w[2], b + 0x98); writel(w[3], b + 0x9c);
	writel(w[0], b + 0xa0); writel(w[1], b + 0xa4);
	writel(w[2], b + 0xa8); writel(w[3], b + 0xac);
}

void forge_m681_mark_ofnode(const char *name)
{
	const char *s = name ? name : "";
	u8 buf[16];
	u32 w[4];
	int i;

	forge_ofnode_seq++;
	for (i = 0; i < 16; i++) {
		buf[i] = *s ? (u8)*s : 0;
		if (*s)
			s++;
	}
	for (i = 0; i < 4; i++)
		w[i] = buf[i*4] | (buf[i*4+1] << 8) |
		       (buf[i*4+2] << 16) | (buf[i*4+3] << 24);

	if (forge_spm_base)
		forge_ofnode_write(forge_spm_base, w);
	if (forge_spm_base2)
		forge_ofnode_write(forge_spm_base2, w);
}
EXPORT_SYMBOL(forge_m681_mark_ofnode);

/*
 * Called from arch/arm64/kernel/setup.c right after early_ioremap_init().
 * Maps the marker region via the fixmap so we can mark stages from A04 onward,
 * well before the SPM/ram_console drivers register.
 */
void __init forge_m681_marker_early_init(void)
{
	if (forge_spm_base || forge_spm_base2)
		return;	/* already mapped */

	forge_spm_base = early_ioremap(FORGE_MARKER_PHYS_BASE,
				       FORGE_MARKER_REGION_SIZE);
	if (!forge_spm_base)
		pr_emerg("[FORGE_M681] marker early_ioremap(0x%lx) FAILED\n",
			 FORGE_MARKER_PHYS_BASE);

	forge_spm_base2 = early_ioremap(FORGE_MARKER2_PHYS_BASE,
					FORGE_MARKER_REGION_SIZE);
	if (!forge_spm_base2)
		pr_emerg("[FORGE_M681] marker2 early_ioremap(0x%lx) FAILED\n",
			 FORGE_MARKER2_PHYS_BASE);

	pr_emerg("[FORGE_M681] marker early base=%p base2=%p\n",
		 forge_spm_base, forge_spm_base2);

	/* sentinel so we know the marker engine itself is alive */
	forge_m681_mark(FORGE_STAGE_MARKER_EARLY_INIT);
}

/*
 * Called from init/main.c after mm_init() when vmalloc and the regular
 * ioremap() are available.  We swap the fixmap mapping for a permanent
 * ioremap() so the late_initcall leak check stays quiet and the mapping
 * survives the early_ioremap_reset() flag flip.
 */
void __init forge_m681_marker_late_init(void)
{
	void __iomem *nb;

	nb = ioremap(FORGE_MARKER_PHYS_BASE, FORGE_MARKER_REGION_SIZE);
	if (nb)
		forge_spm_base = nb;
	else
		pr_emerg("[FORGE_M681] marker late ioremap(0x%lx) FAILED, keeping early\n",
			 FORGE_MARKER_PHYS_BASE);

	nb = ioremap(FORGE_MARKER2_PHYS_BASE, FORGE_MARKER_REGION_SIZE);
	if (nb)
		forge_spm_base2 = nb;
	else
		pr_emerg("[FORGE_M681] marker2 late ioremap(0x%lx) FAILED, keeping early\n",
			 FORGE_MARKER2_PHYS_BASE);

	/* m681 v56: map the toprgu watchdog so do_one_initcall() can kick it
	 * across the level 4/5/6 window where no kicker kthread runs yet. */
	forge_wdt_base = ioremap(FORGE_WDT_PHYS_BASE, FORGE_WDT_REGION_SIZE);
	if (!forge_wdt_base)
		pr_emerg("[FORGE_M681] wdt ioremap(0x%lx) FAILED\n",
			 FORGE_WDT_PHYS_BASE);

	pr_emerg("[FORGE_M681] marker late base=%p base2=%p wdt=%p\n",
		 forge_spm_base, forge_spm_base2, forge_wdt_base);
	forge_m681_mark(FORGE_STAGE_MARKER_LATE_INIT);

	/* m681: forge_m681_wdt_arm() DISABLED — direct writel to toprgu
	 * 0x10007000 killed boot before marker init (v44b: kick_count=0,
	 * 18/2048 non-zero bytes). Preloader/lk already arms HW WDT before
	 * kernel entry; forge_m681_wdt_kick() pets it via RESTART key. */
	/* forge_m681_wdt_arm(); */

	/* early_ioremap_reset() already retired the fixmap path before this
	 * point; the stale early mappings are abandoned, not iounmapped. */
}

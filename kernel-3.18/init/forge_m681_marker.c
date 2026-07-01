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
#include <linux/notifier.h>	/* m681 v48: atomic_notifier_chain_register + panic_notifier_list */
#include <linux/notifier.h>	/* (kept; kernel.h pulls it but be explicit for the forge reader) */
#include <asm/early_ioremap.h>
#include <linux/console.h>	/* m681 v78: forge console -> survivable DRAM log */
#include <linux/string.h>
#include <linux/hrtimer.h>	/* m681 v86: irq-context WDT kicker (kthread never scheduled) */
#include <linux/ktime.h>	/* m681 v86: ns_to_ktime / NSEC_PER_SEC */

#include "forge_m681_marker.h"

/*
 * m681 v78: forge console.  Routes EVERY kernel printk (including userspace
 * init's /dev/kmsg writes) into a ring buffer in the preloader-reserved DRAM
 * region @0x44801000 (just above the forge marker @0x44800000).  That region
 * survives a WDT warm-reset AND the subsequent TWRP boot (unlike the normal
 * ram_console @0x44400000, which TWRP overwrites).  So after the boot kernel
 * hangs in userspace and the armed WDT warm-resets back to TWRP, we read this
 * buffer from /dev/mem and see exactly how far init got and where it stuck.
 * Layout @0x44801000: u32 head, u32 magic 'FLOG', then the text ring.
 */
/* The log ring lives INSIDE the already-working forge marker page
 * (forge_spm_base2 @0x44800000, mapped via the marker's working ioremap).  The
 * preloader region has NO "no-map" so a fresh ioremap of 0x44801000 is rejected
 * as RAM -> NULL.  The marker slots end at 0x200, so 0x200..0xFFF (~3.5 KB) is
 * free.  Read from TWRP at phys 0x44800200.  Layout: u32 head, u32 'FLOG', ring.
 */
#define FORGE_LOG_OFF	0x200U				/* within the marker page */
#define FORGE_LOG_END	0x1000U				/* m681 v97: REVERTED v96's 0x8000 — ioremap(32KB) of the reserved RAM region aliases the linear map with mismatched attrs (device vs cacheable) -> early die (0xFE) in start_kernel. One page only. */
#define FORGE_LOG_HDR	8U				/* head + magic */
#define FORGE_LOG_RING	(FORGE_LOG_END - FORGE_LOG_OFF - FORGE_LOG_HDR)
#define FORGE_LOG_MAGIC	0x464C4F47U			/* 'FLOG' */
static u32 forge_log_head;

static void forge_console_write(struct console *con, const char *s,
				unsigned int count)
{
	void __iomem *b = forge_spm_base2;
	unsigned int i;

	if (!b)
		return;
	for (i = 0; i < count; i++) {
		writeb(s[i], b + FORGE_LOG_OFF + FORGE_LOG_HDR +
		       (forge_log_head % FORGE_LOG_RING));
		forge_log_head++;
	}
	writel(forge_log_head, b + FORGE_LOG_OFF + 0);
	writel(FORGE_LOG_MAGIC, b + FORGE_LOG_OFF + 4);
}

static struct console forge_console = {
	.name	= "forgelog",
	.write	= forge_console_write,
	.flags	= CON_PRINTBUFFER | CON_ENABLED | CON_ANYTIME,
	.index	= -1,
};

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
#define FORGE_MARKER2_MAP_SIZE	0x8000UL	/* m681 v96: late ioremap maps 32KB for the enlarged forge console (within the 1MB preloader-reserved region) */

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
#define FORGE_WDT_MODE_IRQ	0x00000008U
#define FORGE_WDT_MODE_DUAL_MODE 0x00000040U
#define FORGE_WDT_MODE_AUTO_RESTART 0x00000010U
#define FORGE_WDT_LENGTH_KEY	0x00000008U
#define FORGE_WDT_TIMEOUT_SEC	30U

/* m681 v47: manual SWRST backstop — see forge_m681_wdt_kick(). */
#define FORGE_WDT_SWRST_OFF	0x14U
#define FORGE_WDT_SWRST_KEY	0x1209U
/* 3.18.140 m6-graft has ~600 module initcalls; threshold is set well above that
 * so natural initcall flow never trips it, but a pathological loop that keeps
 * calling do_one_initcall (recursive initcall loop, broken module_init chain,
 * kthread that re-enters do_one_initcall) cannot keep the WDT pet forever. */
#define FORGE_WDT_FORCE_SWRESET_THRESHOLD 4000U

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

/* m681 v119: GPT2 free-run counter (phys 0x10008000, counter at +0x28) for the
 * "does 0x10008028 actually count?" probe in forge_m681_wdt_kick (runs every
 * initcall -> guaranteed to execute before any frozen-timer hang). */
static void __iomem *forge_gpt_base;

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
/* m681 v50: initcall-level phase tracking diag offsets. */
#define FORGE_DIAG_LEVEL_ENTER_BASE	0xB0
#define FORGE_DIAG_LEVEL_DONE_BASE	0xC0
#define FORGE_DIAG_LASTGOOD_SEQ_OFF	0xD0
#define FORGE_DIAG_LASTGOOD_SEQ_INV_OFF	0xD4
#define FORGE_DIAG_BOOTPHASE_OFF	0xD8
#define FORGE_DIAG_WDTK_HEARTBEAT_OFF	0xFC	/* m681 v87: kicker heartbeat — moved off 0xDC which COLLIDED with LEVEL_DONE_BASE(0xC0)+7*4=0xDC (level tracker clobbered it -> false heartbeat=0 in v81-v86) */
#define FORGE_DIAG_WDTK_INIT_OFF	0xE0	/* m681 v84: proof kicker initcall ran (A11E armed) */
#define FORGE_DIAG_EMMC_CTR_BASE	0xE4	/* m681 v87: eMMC checkpoint counters slot[0..5]=C7,C8,C9,CA,C3,C5 (0xE4..0xF8); free zone 0xE4..0xFC, before marker slots @0x100 */

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

/* m681 v87: increment a TWRP-readable checkpoint COUNTER in the clean diag zone
 * (0xE4..0xF8).  Unlike the 8-deep rolling stage slots (which only keep the
 * last marks), these counters reveal HOW MANY times a checkpoint executed —
 * distinguishing "stuck at C7 (count 1, never C8)" from "C7->CA retry loop
 * cycling N times".  read-modify-write; bit-rot may perturb low bits but the
 * magnitude (1 vs hundreds) is the signal. */
void forge_m681_bump(unsigned int slot)
{
	u32 off = FORGE_DIAG_EMMC_CTR_BASE + (slot & 0x7u) * 4u;

	if (forge_spm_base)
		writel(readl(forge_spm_base + off) + 1u, forge_spm_base + off);
	if (forge_spm_base2)
		writel(readl(forge_spm_base2 + off) + 1u, forge_spm_base2 + off);
}
EXPORT_SYMBOL(forge_m681_bump);

/* m681 v95: write a raw u32 to a TWRP-readable diag word (clean zone, off masked
 * to the page).  Unlike the wrapping forge console, this survives reliably for a
 * value captured deep in an async kworker (e.g. the VEMC pwrap readback). */
void forge_m681_diag(unsigned int off, u32 val)
{
	off &= 0x3FCu;
	if (forge_spm_base2)
		writel(val, forge_spm_base2 + off);
	if (forge_spm_base)
		writel(val, forge_spm_base + off);
}
EXPORT_SYMBOL(forge_m681_diag);

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
 * forge_m681_wdt_arm - arm the MTK toprgu HW watchdog into single-mode
 * hw-reset.  Called exactly once, from forge_m681_marker_late_init()
 * (post mm_init, before any initcalls), using the post-mm_init ioremap
 * of FORGE_WDT_PHYS_BASE — the path that l681 M18 proved live (kick_count
 * =676, kickflags=0xC0DE0011 = both base mappings valid).
 *
 * v44 WDT was in platform.c denylist and NOT armed → no reset path on hang.
 * v44b tried to arm it by direct writel from start_kernel (pre-mm_init) —
 * BROKE boot (kick_count=0): that mapping path is non-functional for SoC
 * register space that early.  v45 tried instead to let mtk_wdt_probe run
 * and apply mode_config there — but FACT (v46 handoff §0): probe calls
 * request_irq at mtk_wdt.c:769 BEFORE the v45 single-mode mode_config at
 * line 815 is reached, and on the graft tree that request_irq path appears
 * to wedge; WDT is left in preloader dual-mode+IRQ and AXI bus-hang later
 * cannot deliver the IRQ → no SWRST, dead device (battery pull = cold
 * reset = marker wiped).
 *
 * v47 root fix: leave mtk_wdt_probe in the denylist (it doesn't trust the
 * GIC request_irq path), and arm the WDT ourselves via the l681-proven post-
 * mm_init ioremap, in MODE read-modify-write ONLY — NO LENGTH reset.
 *
 * NO-LENGTH-WRITE is the critical v44b lesson: writing LENGTH resets the
 * preloader-running counter; if the WDT was already counting down, the reset
 * can corrupt the state (subsequent pet may not latch, watchdog might expire
 * immediately or never).  Instead READ the current MODE the preloader left,
 * clear ONLY DUAL_MODE (0x40) and IRQ (0x08), set KEY|ENABLE|EXTEN|
 * AUTO_RESTART, write back once.  Preloader's 30s LENGTH survives untouched
 * and is the timeout we want.  Then a single RESTART_KEY pet so any pre-boot
 * timeout count is reset to the full 30s window.
 *
 * Readback snapshot at 0xE6/0xE7 (mode/length) lets a post-reset marker
 * decode PROVE the armed state from recovery — the only evidence we had
 * before was the driver's pr_debug (invisible pre-console).
 */
static void forge_m681_wdt_arm(void)
{
	void __iomem *b = forge_wdt_base;
	u32 mode;

	if (!b)
		return;

	mode = readl(b + FORGE_WDT_MODE_OFF);
	/* Clear DUAL_MODE + IRQ (preloader arms dual-mode+IRQ).  Keep ENABLE
	 * (already set by preloader — we re-assert defensively below).  Do NOT
	 * touch LENGTH (FORGE_WDT_LENGTH_OFF): preloader set a 30s timeout and
	 * resetting it mid-count is the v44b root cause. */
	mode &= ~(FORGE_WDT_MODE_DUAL_MODE | FORGE_WDT_MODE_IRQ);
	mode |= FORGE_WDT_MODE_KEY | FORGE_WDT_MODE_ENABLE |
		FORGE_WDT_MODE_EXTEN | FORGE_WDT_MODE_AUTO_RESTART;
	writel(mode, b + FORGE_WDT_MODE_OFF);

	/* pet once: reload LENGTH counter to the full 30s window */
	writel(FORGE_WDT_RESTART_KEY, b + FORGE_WDT_RESTART_OFF);

	/* snapshot armed state into the forge SRAM marker (rolling-stage
	 * channel); a post-reset readback from recovery decodes aux -> the
	 * raw MODE/LENGTH we just wrote, proving arm-before-hang. */
	forge_m681_mark_aux(0xE6, readl(b + FORGE_WDT_MODE_OFF));
	forge_m681_mark_aux(0xE7, readl(b + FORGE_WDT_LENGTH_OFF));

	pr_emerg("[FORGE_M681] v47 WDT armed single-mode-hwreset (RMW, no LENGTH reset): MODE=0x%x LENGTH=0x%x\n",
		 readl(b + FORGE_WDT_MODE_OFF),
		 readl(b + FORGE_WDT_LENGTH_OFF));
}

/*
 * forge_m681_wdt_disarm - DISABLE the MTK toprgu HW watchdog.
 *
 * m681 v59: forge_m681_wdt_kick() only pets the WDT once per do_one_initcall().
 * When do_initcalls() finishes the kicks stop, so the armed 30s HW watchdog
 * fires ~30s into userspace and resets before adbd/USB-gadget can come up
 * (mtk_wdt driver is denylisted, so nothing in userspace pets it).  Call this
 * right after the last initcall (POST_BASIC_SETUP, init/main.c) so userspace
 * runs watchdog-free and adb can enumerate.  Writes MODE with the KEY but the
 * ENABLE bit cleared -> watchdog disabled.  Trade-off: a userspace hang no
 * longer auto-resets (boot=TWRP harbor + manual/mtkclient recovery covers it).
 */
void forge_m681_wdt_disarm(void)
{
	void __iomem *b = forge_wdt_base ? forge_wdt_base : toprgu_base;

	if (!b)
		return;
	/* KEY only, ENABLE/EXTEN/AUTO_RESTART cleared -> WDT off. */
	writel(FORGE_WDT_MODE_KEY, b + FORGE_WDT_MODE_OFF);
	forge_m681_mark_aux(0xE5, readl(b + FORGE_WDT_MODE_OFF));
	pr_emerg("[FORGE_M681] v59 WDT DISARMED for userspace: MODE=0x%x\n",
		 readl(b + FORGE_WDT_MODE_OFF));
}
EXPORT_SYMBOL(forge_m681_wdt_disarm);

/*
 * m681 v120: jiffies-TICK-driven self-rearming WDT kicker. The forge per-initcall
 * kick (forge_m681_wdt_kick) only fires while do_initcalls() runs; once the kernel
 * hands to userspace the kicks stop and the ~30s HW dog warm-resets ~30s in =
 * the bootloop. Prior userspace kickers failed on this graft: a kthread is never
 * cleanly scheduled and an hrtimer never fires (frozen ktime). BUT the timer WHEEL
 * (timer_list) is serviced by the GPT jiffies tick, which WORKS here -> a
 * self-rearming timer_list keeps the dog petted through userspace WITHOUT relying
 * on the frozen arch timer. This is NOT a disarm: on a genuine HARD hang (tick
 * stops / IRQs off) the timer stops firing and the dog still warm-resets, so the
 * DRAM marker lifeline is preserved ([[feedback_never_disarm_wdt]]). Kick every
 * 8s (dog timeout ~30s). Lazily armed on the first forge_m681_wdt_kick (i.e. the
 * first initcall, after time_init() so the tick is live). */
static struct timer_list forge_wdt_ticker;
static int forge_wdt_ticker_armed;

static void __maybe_unused forge_wdt_ticker_fn(unsigned long data)
{
	void __iomem *b = toprgu_base ? toprgu_base : forge_wdt_base;

	if (b)
		writel(FORGE_WDT_RESTART_KEY, b + FORGE_WDT_RESTART_OFF);
	/* breadcrumb: ticker heartbeat into the kicker-init diag slot (0xE0). */
	if (forge_spm_base2)
		writel(0x71C0E000u | (forge_wdt_kick_count & 0xFFFFu),
		       forge_spm_base2 + FORGE_DIAG_WDTK_INIT_OFF);
	mod_timer(&forge_wdt_ticker, jiffies + msecs_to_jiffies(8000));
}

static void __maybe_unused forge_wdt_ticker_start(void)
{
	if (forge_wdt_ticker_armed)
		return;
	forge_wdt_ticker_armed = 1;
	setup_timer(&forge_wdt_ticker, forge_wdt_ticker_fn, 0);
	mod_timer(&forge_wdt_ticker, jiffies + msecs_to_jiffies(8000));
	pr_emerg("[FORGE_M681] v120 timer_list WDT ticker armed (jiffies tick, 8s)\n");
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

	/* v122 REMOVED: forge_wdt_ticker_start() — with the timer now FIXED (cpuxgpt
	 * enabled, jiffies/hrtimers live), the tick-driven ticker actually FIRES and
	 * keeps the dog kicked even during a HANG, which DEFEATS the marker-preserving
	 * warm reset -> a hung boot hard-locks and needs a battery pull (the v122
	 * mistake). Leave the dog UNKICKED past initcalls so a hang warm-resets to the
	 * TWRP harbor (markers survive, no battery pull). [[feedback_never_disarm_wdt]]
	 * Proper userspace WDT mgmt = re-enable stock mtk_wdt later, now that the
	 * timer works. */

	/* m681 v47: manual SWRST backstop (handoff §4 option 1C).  If the
	 * initcall path is recursing oddly (kick_count climbing past the total
	 * realistic initcall count of ~600 for 3.18.140 m6-graft, threshold
	 * well above that) while system_state is still pre-RUNNING, the chip
	 * must NOT be left alive on WDT pets from a broken/looping do_one_initcall
	 * caller.  Write the MTK_WDT_SWRST key directly — same hardware reset
	 * path as wdt_arch_reset(): a chip-wide warm reset that PRESERVES the
	 * preloader reserved SRAM marker @0x44800000 (verified since l681 M1).
	 * A 0xEA mark right before the SWRST records in the next-recovery
	 * marker that we forced the reset (vs the natural 30s self-arm timeout
	 * which would leave the last-initcall aux as the wedged fn).  DIY only:
	 * the natural 30s self-arm expiry handles the common hang case; this is
	 * a guarantee for the rare "kicks keep coming but boot never progresses"
	 * failure mode that a single WDT would otherwise pet forever. */
	if (system_state < SYSTEM_RUNNING &&
	    forge_wdt_kick_count >= FORGE_WDT_FORCE_SWRESET_THRESHOLD && b) {
		forge_m681_mark(0xEA);
		writel(FORGE_WDT_SWRST_KEY, b + FORGE_WDT_SWRST_OFF);
	}

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

	/* m681 v119: GPT2-counts probe. Runs every initcall (so it executes well
	 * before any frozen-timer hang during device_initcalls). Latches the first
	 * GPT2 reading, then on every later kick writes the delta to diag 0xDC,
	 * sentinel 0xD2 in the high byte (proves it ran), bit23 = "counter moved",
	 * low 22 bits = delta. Decisive answer to "does 0x10008028 actually count?"
	 * — if frozen, the whole GPT2-as-clocksource (TIMERFIX) approach is dead and
	 * only the firmware path (Flyme preloader/LK) can unfreeze the real timer. */
	if (forge_gpt_base) {
		static u32 g0;
		static int gset;
		u32 gn = readl(forge_gpt_base + 0x28);
		u32 pk;
		if (!gset) { g0 = gn; gset = 1; }
		pk = 0xD2000000u | (((gn - g0) != 0) ? (1u << 23) : 0u) |
		     ((gn - g0) & 0x3FFFFFu);
		if (forge_spm_base2)
			writel(pk, forge_spm_base2 + 0xDC);
		if (forge_spm_base)
			writel(pk, forge_spm_base + 0xDC);
	}
}
EXPORT_SYMBOL(forge_m681_wdt_kick);

/*
 * m681 v86: userspace-surviving WDT kicker via HRTIMER (was a kthread v81-v85).
 *
 * FACT (v82-v85): the kthread variant was CREATED ok (arm-proof@0xE0 = A11E)
 * but its loop body NEVER ran (heartbeat@0xDC stayed 0).  On this SMP-disabled,
 * HPS-skipped single-CPU graft the kicker kthread never got a timeslice, so
 * the armed 30s HW-WDT still guillotined userspace before adbd/eMMC could
 * settle (boot looped on the dog).  An hrtimer fires from the timer-interrupt
 * path, INDEPENDENT of thread scheduling, so it pets the dog even when no
 * thread yields.  Safety property PRESERVED: a genuine AXI/APB bus wedge stalls
 * the CPU on the un-acked MMIO access and takes NO interrupts, so the hrtimer
 * also stops firing -> dog no longer petted -> ~30s warm-reset to TWRP, exactly
 * as before.  Pets the dog DIRECTLY (not via forge_m681_wdt_kick) to avoid that
 * path's pre-RUNNING SWRST backstop.  heartbeat -> diag @0xDC (proves the timer
 * actually fires); arm-proof -> diag @0xE0 (A11E).
 * Rollback: delete the core_initcall(forge_wdt_hrtimer_init) line.
 */
static struct hrtimer forge_wdt_hrtimer;
static u32 forge_wdt_beat;
#define FORGE_WDT_KICK_NS	(5ULL * NSEC_PER_SEC)	/* 5s, ~6x margin under 30s WDT */

static enum hrtimer_restart forge_wdt_hrtimer_fn(struct hrtimer *t)
{
	void __iomem *b = toprgu_base ? toprgu_base : forge_wdt_base;

	if (b)
		writel(FORGE_WDT_RESTART_KEY, b + FORGE_WDT_RESTART_OFF);
	forge_wdt_beat++;
	if (forge_spm_base2)
		writel(0xC0DE0000U | (forge_wdt_beat & 0xFFFFU),
		       forge_spm_base2 + FORGE_DIAG_WDTK_HEARTBEAT_OFF);
	if (forge_spm_base)
		writel(0xC0DE0000U | (forge_wdt_beat & 0xFFFFU),
		       forge_spm_base + FORGE_DIAG_WDTK_HEARTBEAT_OFF);
	hrtimer_forward_now(t, ns_to_ktime(FORGE_WDT_KICK_NS));
	return HRTIMER_RESTART;
}

static int __init forge_wdt_hrtimer_init(void)
{
	/* v123 DISABLED: do NOT arm the hrtimer WDT kicker. It was added (v86) as a
	 * frozen-timer workaround, but the arch timer is NOW FIXED (cpuxgpt enabled),
	 * so this CLOCK_MONOTONIC hrtimer actually FIRES every 5s and keeps the dog
	 * kicked through userspace AND during a HANG -> defeats the marker-preserving
	 * warm reset -> hung boot hard-locks -> battery pull (the v122/v123 mistake;
	 * THIS hrtimer, not just the timer_list ticker, was the 2nd kicker the user
	 * caught). Leave the dog UNKICKED past initcalls so a hang warm-resets to the
	 * TWRP harbor (markers survive, no battery pull). [[m6graft_timer_FIXED]],
	 * [[feedback_never_disarm_wdt]]. (void) the fn/struct to dodge -Werror. */
	/* m681 v184: RE-ENABLE the hrtimer WDT kicker. The display controller now
	 * boots (fb0 up, v183) but surfaceflinger cascades (no EGL/Mali yet) so the
	 * boot never completes -> userspace watchdog never kicks -> the armed HW-WDT
	 * (MODE=0x15) guillotines the boot at ~16s. User goal: "start must hold
	 * long" so the kernel stays alive long enough to (a) be stable and (b) be
	 * inspected LIVE (read /dev/mali, dmesg) to bring up the Mali GPU. Pets the
	 * dog every 5s from the timer-IRQ path. Tradeoff (accepted): a SOFT hang no
	 * longer warm-resets to TWRP; a genuine bus wedge still does (CPU stalled ->
	 * hrtimer stops). [[feedback_never_disarm_wdt]] [[m6graft_timer_FIXED]] */
	hrtimer_init(&forge_wdt_hrtimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	forge_wdt_hrtimer.function = forge_wdt_hrtimer_fn;
	hrtimer_start(&forge_wdt_hrtimer, ns_to_ktime(FORGE_WDT_KICK_NS),
		      HRTIMER_MODE_REL);
	pr_emerg("[FORGE_M681] v184 wdt hrtimer kicker RE-ENABLED (5s pet -> start holds long for Mali bring-up)\n");
	return 0;
}
core_initcall(forge_wdt_hrtimer_init);

/*
 * m681 v169: SPM power-domain state dump (FACT-gather, reliable channel).
 *
 * Both recovery-side forensic channels are dead on this setup (FACTs, v169):
 *   - DRAM marker 0x444f0000 is CLOBBERED by TWRP (reads back ELF magic
 *     7f 45 4c 46 after a wedge+recovery dwell), so post-mortem marker reads
 *     from recovery are garbage.
 *   - /dev/mem from TWRP cannot read SPM IO (STRICT_DEVMEM): dd of 0x10006180
 *     returns empty.
 * So the ONLY trustworthy channel is forge_klog on p4, which needs the build
 * to BOOT to late_initcall. This dump runs as a late_initcall in a *booting*
 * (display-denied) build and prints the DIS MTCMOS power state, settling the
 * refuted "DIS power-domain never comes up" theory with a FACT: mt_scpsys_init
 * (CLK_OF_DECLARE "mediatek,mt6755-scpsys") UNCONDITIONALLY powers DIS on at
 * of_clk_init (clk-mt6755-pg.c:2110), long before any deny gate, so a build
 * that boots at all has already powered DIS. This confirms the actual state.
 */
static int __init forge_spm_dump(void)
{
	void __iomem *spm = ioremap(0x10006000UL, 0x1000);
	u32 cfg, sta, sta2, dis, mfg, isp, mm;

	if (!spm) {
		pr_emerg("[FORGE_M681] v169 spm_dump: ioremap(0x10006000) FAILED\n");
		return 0;
	}
	cfg  = readl(spm + 0x000);	/* POWERON_CONFIG_EN */
	sta  = readl(spm + 0x180);	/* PWR_STATUS      */
	sta2 = readl(spm + 0x184);	/* PWR_STATUS_2ND  */
	dis  = readl(spm + 0x30c);	/* DIS_PWR_CON     */
	mm   = readl(spm + 0x308);	/* (MM_PWR_CON neighbor, informational) */
	mfg  = readl(spm + 0x214);	/* MFG_PWR_CON (informational) */
	isp  = readl(spm + 0x238);	/* ISP_PWR_CON (informational) */
	pr_emerg("[FORGE_M681] v169 SPM cfg=%08x PWR_STATUS=%08x/2ND=%08x\n",
		 cfg, sta, sta2);
	pr_emerg("[FORGE_M681] v169 DIS_PWR_CON=%08x dis_on(sta b3)=%d sram_ack(b12)=%d\n",
		 dis, !!((sta & (1u<<3)) && (sta2 & (1u<<3))),
		 !!(dis & (1u<<12)));
	pr_emerg("[FORGE_M681] v169 neigh MM(0x308)=%08x MFG(0x214)=%08x ISP(0x238)=%08x\n",
		 mm, mfg, isp);
	iounmap(spm);
	return 0;
}
late_initcall(forge_spm_dump);

/*
 * m681 v48: panic-notifier direct-SWRST recovery safety net.
 *
 * FACT (5-cycle v44-v47 handoff §0): the toprgu HW timer-WDT expired-time
 * SWRST does NOT happen reliably on mt6755 graft — preloader bin-string
 * `"WDT does not trigger reboot"` and the v45/v46/v47 self-arm / SWRST-
 * backstop changes never produced a warm return.  The ONLY recovery path
 * that has demonstrably worked on m681 graft so far is the **direct
 * SWRST_KEY write** that v43's mtu3d BUG_ON(1) -> panic -> wdt_arch_reset()
 * exercised.  v48 (a) restores that exact SWRST-write path by registering
 * on the kernel panic_notifier chain, so ANY panic (BUG_ON, OOPS, MCE, OOM)
 * fires it instead of relying on machine_restart plumbing; v48 (b) keeps
 * the v47 self-arm + manual SWRST backstop as belt-and-suspenders.
 *
 * The notifier is atomic-context safe: it issues only a single mmio writel
 * to toprgu+0x14 with the MTK_WDT_SWRST_KEY (0x1209) — same constant the
 * driver's wdt_arch_reset() uses — preceded by a forge_m681_mark(0xF1)
 * so the next-recovery marker decodes show "0xF1: paniked-and-forced-reset".
 * A one-shot latch keeps the FIRST panic from being shadowed by a later
 * call from atomic_notifier_call_chain iterating down several registered
 * notifiers (us + others).  pr_emerg is best-effort; printk in panic is
 * safe (logbuf lock held).
 *
 * Rollback: comment out the atomic_notifier_chain_register line in the
 * late_init function below.  No behaviour change on a progressing boot
 * because panic "doesn't happen" on a healthy boot — pointless to disable.
 */
static int forge_panic_latched;

static int forge_m681_panic_handler(struct notifier_block *this,
				    unsigned long ev, void *ptr)
{
	void __iomem *b = toprgu_base ? toprgu_base : forge_wdt_base;

	if (forge_panic_latched)
		return NOTIFY_DONE;
	forge_panic_latched = 1;

	forge_m681_mark(0xF1);
	pr_emerg("[FORGE_M681] panic rv48: writing SWRST_KEY to toprgu+0x14 to force warm reset\n");

	if (b)
		writel(FORGE_WDT_SWRST_KEY, b + FORGE_WDT_SWRST_OFF);

	/* If the SWRST_KEY write worked we never return; if it didn't (toprgu
	 * unmapped, write blocked), at least the marker stuck and the next
	 * reset attempt (manual SWRST backstop in forge_m681_wdt_kick, or
	 * physical battery pull) carries the 0xF1 evidence. */
	return NOTIFY_DONE;
}

static struct notifier_block forge_m681_panic_nb = {
	.notifier_call = forge_m681_panic_handler,
	.priority = INT_MAX,	/* run last so other panic handlers log first */
};

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

u32 forge_m681_get_initcall_seq(void)
{
	return forge_initcall_seq;
}
EXPORT_SYMBOL(forge_m681_get_initcall_seq);

void forge_m681_set_initcall_done(u32 fn)
{
	/* m681 v50: mirror last-good seq (0xD0) + guard (0xD4) so a reader
	 * gets the seq of the last initcall that returned without needing
	 * to cross-reference 0x68 (current seq) vs 0x6c (last done fn). */
	u32 seq = forge_initcall_seq;
	if (forge_spm_base) {
		writel(fn, forge_spm_base + 0x6c);
		writel(seq, forge_spm_base + FORGE_DIAG_LASTGOOD_SEQ_OFF);
		writel(~seq, forge_spm_base + FORGE_DIAG_LASTGOOD_SEQ_INV_OFF);
	}
	if (forge_spm_base2) {
		writel(fn, forge_spm_base2 + 0x6c);
		writel(seq, forge_spm_base2 + FORGE_DIAG_LASTGOOD_SEQ_OFF);
		writel(~seq, forge_spm_base2 + FORGE_DIAG_LASTGOOD_SEQ_INV_OFF);
	}
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
 * m681 v50: initcall-level phase markers + per-level completion counters.
 *
 * do_initcall_level() calls mark_level_enter(level) before the level's
 * initcall loop and mark_level_done(level) after it.  The rolling-stage
 * channel records 0xE8/0xE9 (aux=level) so a post-reset marker decode
 * immediately names the LEVEL the boot wall sits in — early/core/postcore/
 * arch/subsys/fs/device/late — complementing the per-initcall fn tracker
 * (0x64/0x6c) that names the exact initcall.
 *
 * Per-level initcall completion counts are kept in diag offsets
 *   0xB0+level*4 (enter count, incremented on each enter)
 *   0xC0+level*4 (done count, incremented on each done)
 * so a reader can tell whether the wall is at the START of a level
 * (enter>0, done=0) or MIDDLE (done>0, enter>done+1).
 *
 * Additionally, the "last-good seq" (seq of the last initcall that
 * returned) is mirrored to diag 0xD0 so the reader doesn't need to
 * cross-reference 0x68 (current seq) with 0x6c (last done fn) to
 * compute it.
 *
 * diag layout (v50 additions):
 *   0xB0..0xB7 = per-level enter count (u8 each, 8 levels packed)
 *   0xC0..0xC7 = per-level done count  (u8 each, 8 levels packed)
 *   0xD0       = last-good initcall seq (u32, written in set_initcall_done)
 *   0xD4       = last-good initcall seq guard (~seq)
 *   0xD8       = boot-phase owner: current initcall level (u32)
 */
static u8 forge_level_enter_count[8];
static u8 forge_level_done_count[8];

static void forge_level_write_counts(void __iomem *b)
{
	int i;
	for (i = 0; i < 8; i++) {
		writel(forge_level_enter_count[i],
		       b + FORGE_DIAG_LEVEL_ENTER_BASE + i * 4);
		writel(forge_level_done_count[i],
		       b + FORGE_DIAG_LEVEL_DONE_BASE + i * 4);
	}
}

void forge_m681_mark_level_enter(int level)
{
	if (level < 0 || level >= 8)
		return;
	forge_level_enter_count[level]++;
	forge_m681_mark_aux(FORGE_STAGE_INITCALL_LEVEL_ENTER, (u32)level);
	if (forge_spm_base) {
		writel((u32)level, forge_spm_base + FORGE_DIAG_BOOTPHASE_OFF);
		forge_level_write_counts(forge_spm_base);
	}
	if (forge_spm_base2) {
		writel((u32)level, forge_spm_base2 + FORGE_DIAG_BOOTPHASE_OFF);
		forge_level_write_counts(forge_spm_base2);
	}
}
EXPORT_SYMBOL(forge_m681_mark_level_enter);

void forge_m681_mark_level_done(int level)
{
	if (level < 0 || level >= 8)
		return;
	forge_level_done_count[level]++;
	forge_m681_mark_aux(FORGE_STAGE_INITCALL_LEVEL_DONE, (u32)level);
	if (forge_spm_base)
		forge_level_write_counts(forge_spm_base);
	if (forge_spm_base2)
		forge_level_write_counts(forge_spm_base2);
}
EXPORT_SYMBOL(forge_m681_mark_level_done);

/* m681 v51: direct SWRST_KEY write for belt-and-suspenders recovery.
 * Uses the same toprgu_base/forge_wdt_base fallback as the kick path. */
void forge_m681_wdt_swrst(void)
{
	void __iomem *b = toprgu_base ? toprgu_base : forge_wdt_base;
	if (b)
		writel(FORGE_WDT_SWRST_KEY, b + FORGE_WDT_SWRST_OFF);
}
EXPORT_SYMBOL(forge_m681_wdt_swrst);

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
 * m681 v121: NATIVE timer fix — enable the MTK cpuxgpt (the ARM architected
 * system counter) via the secure SMC, exactly as stock setup_syscnt() would if
 * the calls weren't commented out (mt_gpt.c:497-499). The donor-graft kernel
 * never enables cpuxgpt, so CNTVCT_EL0 is frozen. The cpuxgpt CTL is in
 * write-protected MCUSYS (0x10200000): direct EL1 MMIO writes are rejected (v91),
 * but writes routed through SMC MTK_SIP_KERNEL_MCUSYS_WRITE (0x82000201) are
 * serviced by EL3/ATF (the device runs the m681's OWN MT6755 ATF, which stock
 * Flyme uses with a working CNTVCT). Sequence (= __cpuxgpt_set_clk(CLK_DIV2) +
 * __cpuxgpt_enable() from mtk_cpuxgpt_mt6755.c): write INDEX_CTL(0) then CTL with
 * clk=13MHz/DIV2 and EN bit0. Reads of CTL (0x10200670) are plain MMIO (only
 * writes are protected). Verifies by reading CNTVCT before/after — a nonzero
 * delta proves the system counter now free-runs. Result -> diag 0x90 (sentinel
 * 0xCC | CNTVCT-delta-nonzero<<23 | low bits) and 0xA4 (final CTL readback). */
static noinline int forge_smc(u64 fid, u64 a0, u64 a1, u64 a2)
{
	register u64 r0 __asm__("x0") = fid;
	register u64 r1 __asm__("x1") = a0;
	register u64 r2 __asm__("x2") = a1;
	register u64 r3 __asm__("x3") = a2;

	asm volatile ("smc    #0\n" : "+r"(r0) : "r"(r1), "r"(r2), "r"(r3));
	return (int)r0;
}

static inline u64 forge_read_cntvct(void)
{
	u64 v;

	asm volatile ("mrs %0, cntvct_el0" : "=r"(v));
	return v;
}

#define FORGE_SMC_MCUSYS_WRITE	0x82000201ULL
#define FORGE_CPUXGPT_INDEX_PHY	0x10200674ULL	/* MCUSYS + 0x674 */
#define FORGE_CPUXGPT_CTL_PHY	0x10200670ULL	/* MCUSYS + 0x670 */
#define FORGE_CPUXGPT_IDX_CTL	0x000U		/* INDEX_CTL_REG */
#define FORGE_CPUXGPT_EN	0x01U		/* EN_CPUXGPT */
#define FORGE_CPUXGPT_DIV2	(0x2U << 8)	/* CLK_DIV2 = 13MHz/2 */
#define FORGE_CPUXGPT_DIV_MASK	(~(0x7U << 8))	/* CLK_DIV_MASK */

static void forge_cpuxgpt_wr(u32 addr, u32 val)
{
	forge_smc(FORGE_SMC_MCUSYS_WRITE, addr, val, 0);
}

void __init forge_enable_cpuxgpt(void)
{
	void __iomem *mcusys = ioremap(0x10200000UL, 0x1000);
	u64 c1, c2;
	u32 ctl0 = 0, ctlf = 0, tmp;
	volatile int spin;

	c1 = forge_read_cntvct();

	/* set cpuxgpt clk = 13MHz / DIV2 (read-modify-write the CTL) */
	forge_cpuxgpt_wr(FORGE_CPUXGPT_INDEX_PHY, FORGE_CPUXGPT_IDX_CTL);
	if (mcusys)
		ctl0 = readl(mcusys + 0x670);
	tmp = (ctl0 & FORGE_CPUXGPT_DIV_MASK) | FORGE_CPUXGPT_DIV2;
	forge_cpuxgpt_wr(FORGE_CPUXGPT_INDEX_PHY, FORGE_CPUXGPT_IDX_CTL);
	forge_cpuxgpt_wr(FORGE_CPUXGPT_CTL_PHY, tmp);

	/* set EN_CPUXGPT -> system counter free-runs -> CNTVCT ticks */
	forge_cpuxgpt_wr(FORGE_CPUXGPT_INDEX_PHY, FORGE_CPUXGPT_IDX_CTL);
	if (mcusys)
		tmp = readl(mcusys + 0x670);
	tmp |= FORGE_CPUXGPT_EN;
	forge_cpuxgpt_wr(FORGE_CPUXGPT_INDEX_PHY, FORGE_CPUXGPT_IDX_CTL);
	forge_cpuxgpt_wr(FORGE_CPUXGPT_CTL_PHY, tmp);

	for (spin = 0; spin < 3000000; spin++)
		cpu_relax();
	c2 = forge_read_cntvct();

	forge_cpuxgpt_wr(FORGE_CPUXGPT_INDEX_PHY, FORGE_CPUXGPT_IDX_CTL);
	if (mcusys) {
		ctlf = readl(mcusys + 0x670);
		iounmap(mcusys);
	}

	/* 0x90 = sentinel 0xCC | (CNTVCT moved)<<23 | low22 of the CNTVCT delta.
	 * 0xA4 = (initial CTL << 16) | final CTL & 0xFFFF (bit0 should be 1 = EN). */
	if (forge_spm_base2) {
		writel(0xCC000000u | (((c2 - c1) != 0) ? (1u << 23) : 0u) |
		       ((u32)(c2 - c1) & 0x3FFFFFu), forge_spm_base2 + 0x90);
		writel(((ctl0 & 0xFFFFu) << 16) | (ctlf & 0xFFFFu),
		       forge_spm_base2 + 0xA4);
	}
	pr_emerg("[FORGE_M681] v121 cpuxgpt: CTL 0x%x->0x%x, CNTVCT %llu->%llu (d=%llu) %s\n",
		 ctl0, ctlf, c1, c2, c2 - c1,
		 (c2 != c1) ? "RUNNING!" : "still frozen");
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

	nb = ioremap(FORGE_MARKER2_PHYS_BASE, FORGE_MARKER_REGION_SIZE);	/* m681 v97: back to one page (v96 32KB ioremap aliased linear map -> early die) */
	if (nb)
		forge_spm_base2 = nb;
	else
		pr_emerg("[FORGE_M681] marker2 late ioremap(0x%lx) FAILED, keeping early\n",
			 FORGE_MARKER2_PHYS_BASE);

	/* m681 v215: WIPE the survivor log ring + stamp a UNIQUE per-build signature
	 * BEFORE register_console() replays the boot printk into it. This guarantees a
	 * captured log is provably from THIS v215 boot, not a stale earlier build or the
	 * user's 4.4 kernel left in the same DRAM. Read phys 0x448001F0 from TWRP: if it
	 * reads 0xF681022A the ring (0x44800200 + head bytes) is the clean v229 trace.
	 * v229: TEE swap Microtrust->Trustonic (MobiCore) to match m681 stock t-base so
	 * the native keystore.mt6755.so keymaster reaches the secure world. */
	if (forge_spm_base2) {
		memset_io(forge_spm_base2 + FORGE_LOG_OFF, 0,
			  FORGE_LOG_END - FORGE_LOG_OFF);
		forge_log_head = 0;
		writel(0xF681022AU, forge_spm_base2 + 0x1F0);
		writel(0x0, forge_spm_base2 + FORGE_LOG_OFF + 0);     /* head = 0 */
		writel(FORGE_LOG_MAGIC, forge_spm_base2 + FORGE_LOG_OFF + 4);
	}

	/* m681 v121: NATIVE timer fix — enable cpuxgpt via the secure SMC NOW, before
	 * time_init()/arch_counter_register() reads CNTVCT as the clocksource. Done
	 * after forge_spm_base2 is mapped so the verification diag (0x90/0xA4) lands. */
	forge_enable_cpuxgpt();

	/* m681 v79: register the forge console.  It logs into the marker page
	 * (forge_spm_base2 @0x44800000 + 0x200), already mapped above, so all
	 * printk -- including userspace init's /dev/kmsg writes -- lands in
	 * TWRP-surviving DRAM.  Read from TWRP at phys 0x44800200. */
	if (forge_spm_base2) {
		register_console(&forge_console);
		pr_emerg("[FORGE_M681] forge console registered, log@0x%lx\n",
			 FORGE_MARKER2_PHYS_BASE + FORGE_LOG_OFF);
	}

	/* m681 v56: map the toprgu watchdog so do_one_initcall() can kick it
	 * across the level 4/5/6 window where no kicker kthread runs yet. */
	forge_wdt_base = ioremap(FORGE_WDT_PHYS_BASE, FORGE_WDT_REGION_SIZE);
	if (!forge_wdt_base)
		pr_emerg("[FORGE_M681] wdt ioremap(0x%lx) FAILED\n",
			 FORGE_WDT_PHYS_BASE);

	/* m681 v119: map the GPT block so forge_m681_wdt_kick can sample the GPT2
	 * free-run counter (0x10008028) and answer "does it count?". */
	forge_gpt_base = ioremap(0x10008000UL, 0x100);

	/* m681 v91: PROPER CNTVCT fix attempt — directly enable the cpuxgpt system
	 * counter (MCUSYS 0x10200000 + CTL 0x670, bit0 = EN_CPUXGPT).  The ARM
	 * architected counter that feeds get_cycles()/ktime is started by
	 * enable_cpuxgpt(), which the m6-graft never calls (timer DT node binds the
	 * generic mtk_timer.c via "mediatek,mt6577-timer" instead of the mt6755
	 * apxgpt driver that runs setup_syscnt()).  v89's SMC enable did NOT take;
	 * try a DIRECT write here (works iff MCUSYS is not ATF write-protected on
	 * this graft).  If it sticks, CNTVCT ticks -> ktime advances -> udelay AND
	 * hrtimers become accurate and heartbeat@0xFC goes >0.  The __delay()
	 * cpu_relax fallback (arch/arm64/lib/delay.c) stays as backstop if dropped. */
	{
		void __iomem *cx = ioremap(0x10200000UL, 0x1000);

		if (cx) {
			u32 ctl = readl(cx + 0x670);

			writel(ctl | 0x1U, cx + 0x670);
			pr_emerg("[FORGE_M681] v91 cpuxgpt CTL 0x%x -> 0x%x (direct EN_CPUXGPT)\n",
				 ctl, readl(cx + 0x670));
			iounmap(cx);
		} else {
			pr_emerg("[FORGE_M681] v91 cpuxgpt ioremap(0x10200000) FAILED\n");
		}
	}

	pr_emerg("[FORGE_M681] marker late base=%p base2=%p wdt=%p\n",
		 forge_spm_base, forge_spm_base2, forge_wdt_base);
	forge_m681_mark(FORGE_STAGE_MARKER_LATE_INIT);

	/* m681 v47: arm the toprgu HW watchdog ourselves using the just-mapped
	 * post-mm_init forge_wdt_base, applying MODE read-modify-write ONLY
	 * (no LENGTH reset — the v44b killer).  This restores the warm-reboot
	 * safety net that v44 lost (mtu3d BUG_ON panic path disabled) and that
	 * v45 could not recover through mtk_wdt_probe (request_irq wedges before
	 * the v45 single-mode mode_config is reached).  See forge_m681_wdt_arm()
	 * header for the full root-cause chain.  forge_wdt_base ioremap above is
	 * the l681-M18-proven live mapping path; do not call before it succeeds. */
	forge_m681_wdt_arm();

	/* m681 v48: register the panic-notifier SWRST safety net AFTER arming
	 * the WDT to guarantee the marker engine + toprgu mapping is live
	 * before any panic handler can fire.  See forge_m681_panic_handler()
	 * header for rationale.  Uses the SAME forge_wdt_base mapping the
	 * arm routine just established (the l681-M18-proven post-mm_init
	 * ioremap path); toprgu_base from the (denied) driver probe is NULL. */
	atomic_notifier_chain_register(&panic_notifier_list,
				       &forge_m681_panic_nb);
	pr_emerg("[FORGE_M681] v48 panic-SWRST recovery net registered\n");

	/* early_ioremap_reset() already retired the fixmap path before this
	 * point; the stale early mappings are abandoned, not iounmapped. */
}

/*
 * m681 v118: TIMER-FIX verification (late_initcall — guaranteed reached if the
 * kernel completes its initcalls, and runs BEFORE userspace can clobber the
 * marker page). Measures whether the GPT2 free-run counter (phys 0x10008028)
 * counts and whether ktime now ADVANCES (i.e. TIMERFIX-C1/C2 made the apxgpt
 * clocksource win over the frozen arch_sys_counter). Result -> diag 0xDC, packed
 * with a 0xD2 sentinel in the high byte so the read is unambiguous:
 *   bits31..24 = 0xD2  (sentinel: this late_initcall ran)
 *   bit23      = 1 if 0x10008028 delta != 0  (GPT2 counter COUNTS)
 *   bit22      = 1 if ktime delta != 0        (ktime ADVANCES => fix worked)
 *   bits21..0  = low 22 bits of the ktime delta in ns (magnitude)
 */
static int __init forge_timer_verify(void)
{
	void __iomem *g = ioremap(0x10008000UL, 0x100);
	u64 k1, k2;
	u32 c1 = 0, c2 = 0, dk, packed;
	volatile int spin;

	k1 = ktime_to_ns(ktime_get());
	if (g)
		c1 = readl(g + 0x28);
	for (spin = 0; spin < 3000000; spin++)
		cpu_relax();
	k2 = ktime_to_ns(ktime_get());
	if (g) {
		c2 = readl(g + 0x28);
		iounmap(g);
	}
	dk = (u32)(k2 - k1);
	packed = 0xD2000000u
		| ((c2 != c1) ? (1u << 23) : 0u)
		| ((dk != 0)  ? (1u << 22) : 0u)
		| (dk & 0x3FFFFFu);
	forge_m681_diag(0xDCu, packed);
	pr_emerg("[FORGE_M681] v118 timer_verify: GPT2 %u->%u (d=%u) ktime d=%u ns packed=0x%x\n",
		 c1, c2, c2 - c1, dk, packed);
	return 0;
}
late_initcall(forge_timer_verify);

/*
 * m681 v126: PERSISTENT kernel-log dump to eMMC (survives warm-reset AND battery
 * pull / TWRP, unlike the small DRAM forge console). A kthread periodically writes
 * the whole printk log buffer (log_buf_addr_get/len) to the expdb partition
 * (/dev/block/mmcblk0p4 — MTK exception-debug, safe to overwrite for bring-up),
 * prefixed with an 'FLOG' magic + length so it's findable from TWRP:
 *   adb shell "dd if=/dev/block/mmcblk0p4 bs=1M count=2 of=/sdcard/klog.bin" ; pull ; strings.
 * Retries filp_open until eMMC enumerates. Boot stalls (e.g. zygote) are captured
 * up to the last 2s flush. Remove for shipping. */
#include <linux/kthread.h>
#include <linux/fs.h>
#include <linux/delay.h>
#include <linux/printk.h>

static int forge_log_dump_fn(void *arg)
{
	const char *path = "/dev/block/mmcblk0p4";	/* expdb */
	char hdr[16];
	while (!kthread_should_stop()) {
		struct file *f;
		char *lb = log_buf_addr_get();
		u32 ll = log_buf_len_get();
		loff_t pos = 0;

		msleep(400);	/* v152: faster flush to capture the final ~1s before reset */
		f = filp_open(path, O_RDWR | O_LARGEFILE, 0);
		if (IS_ERR(f))
			continue;	/* eMMC not ready yet -> retry */
		memcpy(hdr, "FLOGm681", 8);
		memcpy(hdr + 8, &ll, 4);
		memset(hdr + 12, 0, 4);
		kernel_write(f, hdr, sizeof(hdr), pos);
		pos = sizeof(hdr);
		if (lb && ll)
			kernel_write(f, lb, ll, pos);
		vfs_fsync(f, 0);
		filp_close(f, NULL);
	}
	return 0;
}

static int __init forge_log_dump_init(void)
{
	kthread_run(forge_log_dump_fn, NULL, "forge_klog");
	pr_emerg("[FORGE_M681] v126 eMMC klog dumper started (-> expdb/mmcblk0p4)\n");
	return 0;
}
late_initcall(forge_log_dump_init);

/*
 * m681 v171: SYNCHRONOUS eMMC probe marker — the only post-mortem channel that
 * survives a display BUS-HANG wedge on this device.
 *
 * Why: both DRAM marker regions are 100% owned by TWRP in recovery (PROVEN: the
 * post-wedge read of 0x444f0000 is byte-identical across DIFFERENT wedge builds
 * — seq=6208, names "%%Sb-h$bc"/"mepB-h$pa" — i.e. it is TWRP's own footprint,
 * not our kernel's marks), and SPM IO is unreadable via /dev/mem (STRICT_DEVMEM).
 * forge_klog (async kthread) only flushes at late_initcall, which a pre-
 * late_initcall display wedge never reaches. So we write a tiny record
 * SYNCHRONOUSLY (filp_open + kernel_write + fsync) to a FIXED offset (3MB) of
 * expdb/p4 — far past the klog ring dump (offset 0) and past the 2MB the test
 * harness zeroes — right before each DISPLAY driver's ->probe (called from
 * really_probe, drivers/base/dd.c). After a wedge+recovery, read it back:
 *   dd if=/dev/block/mmcblk0p4 bs=1 skip=$((0x300000)) count=2048 | strings
 * The LAST "FEMK <seq> <name>" record = the driver whose probe hung.
 *
 * Filtered to display-family driver names so non-display probes (thousands) pay
 * only a strstr loop, never eMMC IO. eMMC is core and enumerates long before the
 * display stack probes, so filp_open succeeds by then (if it ever fails we just
 * skip — meaning the wedge would be earlier than eMMC, which display is not).
 */
static u32 forge_emmc_seq;
void forge_m681_emmc_mark(const char *tag)
{
	static const char * const disp_kw[] = {
		"disp", "DISP", "dsi", "mtkfb", "lcm", "smi", "m4u", "M4U",
		"mali", "kbase", "ged", "ddp", "mdp", "ovl", "rdma", "wdma",
		NULL };
	struct file *f;
	char buf[64];
	int n, i, hit = 0;
	loff_t pos;

	if (!tag || !*tag)
		return;
	for (i = 0; disp_kw[i]; i++)
		if (strstr(tag, disp_kw[i])) { hit = 1; break; }
	if (!hit)
		return;

	f = filp_open("/dev/block/mmcblk0p4", O_RDWR | O_LARGEFILE, 0);
	if (IS_ERR(f))
		return;			/* eMMC not up yet -> skip */
	pos = (loff_t)0x300000 + (loff_t)((forge_emmc_seq & 31u) * 64u);
	n = snprintf(buf, sizeof(buf), "FEMK %05u %s", forge_emmc_seq, tag);
	if (n < 0)
		n = 0;
	if (n > (int)sizeof(buf))
		n = sizeof(buf);
	if (n < (int)sizeof(buf))
		memset(buf + n, 0, sizeof(buf) - n);
	forge_emmc_seq++;
	kernel_write(f, buf, sizeof(buf), pos);
	vfs_fsync(f, 0);
	filp_close(f, NULL);
}
EXPORT_SYMBOL(forge_m681_emmc_mark);

/*
 * m681 v152: death-time probe. expdb shows the boot reaching zygote and then the
 * kernel log goes quiet -> ambiguous: did the SoC reset, or did the kernel just
 * stop printing (framework logs to logcat, not dmesg)? A kernel timer fires in
 * softirq every 500ms and stamps a monotonic ktime ms + the CPU it ran on. The
 * timer keeps firing until the kernel is truly wedged/reset, so the LAST "HB ms="
 * in expdb pins the exact instant + CPU where execution stops. Correlate against
 * BOOTPROF ms (same ktime scale). Remove for shipping.
 */
static struct timer_list forge_hb_timer;
static void forge_hb_fn(unsigned long data)
{
	u32 ms = (u32)(ktime_to_ns(ktime_get()) / 1000000);

	pr_emerg("[FORGE_M681] HB ms=%u cpu=%d\n", ms, raw_smp_processor_id());
	mod_timer(&forge_hb_timer, jiffies + msecs_to_jiffies(500));
}
static int __init forge_hb_init(void)
{
	init_timer(&forge_hb_timer);
	forge_hb_timer.function = forge_hb_fn;
	forge_hb_timer.data = 0;
	forge_hb_timer.expires = jiffies + msecs_to_jiffies(500);
	add_timer(&forge_hb_timer);
	pr_emerg("[FORGE_M681] v152 heartbeat timer armed\n");
	return 0;
}
late_initcall(forge_hb_init);

/*
 * m681 v155: EXPLICITLY online secondary CPUs. On this platform boot-time
 * smp_init does NOT bring up secondaries; the MTK HPS governor was the only
 * caller of cpu_up(N>0), and HPS is disabled (hps_init returns early, v76).
 * So removing the _cpu_up guard alone changed nothing -- nobody called cpu_up.
 * Drive it ourselves from late_initcall_sync (after SMP is fully set up, before
 * userspace/zygote). STEP 1: only CPU1 (the _cpu_up gate still caps at >=2).
 * Logs the cpu masks + cpu_up() return so one boot tells us exactly where it
 * stands: present(1)=0 -> smp_prepare_cpus/cpu_prepare skipped it; cpu_up<0 ->
 * the mt-boot/PSCI/spm_mtcmos power path failed (and which errno). On success,
 * heartbeat shows cpu=1 and the box survives past the ~11s wdk-starvation reset.
 */
#include <linux/cpu.h>
static int __init forge_bringup_secondary(void)
{
	unsigned int cpu;
	int ret;

	pr_emerg("[FORGE_M681] v159 pre-bringup: possible=%u present=%u online=%u\n",
		 num_possible_cpus(), num_present_cpus(), num_online_cpus());
	/* v159: bring up ALL secondaries CPU1..7. Cluster0 (1-3) proven in v158.
	 * Cluster1 big cores (4-7) are a separate power domain (bypass_cl1_armpll=1)
	 * -- untested; each cpu_up is logged BEFORE and AFTER so if a big core wedges
	 * the bus, the last "cpu_up(N) ->" line (no matching return) pinpoints it.
	 * dbgregs CoreSight panic fixed (mt_dbg.c NULL-guard). */
	for (cpu = 1; cpu <= 7; cpu++) {
		pr_emerg("[FORGE_M681] v159 cpu_up(%u) ->\n", cpu);
		ret = cpu_up(cpu);
		pr_emerg("[FORGE_M681] v159 cpu_up(%u)=%d online_now=%u\n",
			 cpu, ret, num_online_cpus());
	}
	return 0;
}
late_initcall_sync(forge_bringup_secondary);

/*
 * m681 v164: SELF-HEALING display bring-up gate. Re-enabling the whole display
 * stack (M4U/SMI/Mali/DDP/DSI/mtkfb) at once can wedge the boot. To make that
 * SAFE without a manual fastboot/recovery, a DRAM flag at forge_spm_base2+0x1A0
 * (survives a WDT warm reset; do NOT fastboot-reboot, that wipes it) records the
 * attempt across the reset:
 *   OKAY / cleared -> this boot ATTEMPTS the display (flag := TRY1)
 *   TRY1           -> previous boot set TRY1 and never cleared it = it WEDGED
 *                     -> this boot SKIPS the display (flag := HUNG), reaches adb
 *   HUNG           -> sticky skip (stay on adb) until the flag is cleared
 * forge_disp_mark_ok() (late_initcall) clears the flag to OKAY only if we
 * ATTEMPTED and survived to late init. So: 1 wedged boot, then adb boots with
 * the display skipped and the wedge captured in expdb. Retry from adb:
 *   busybox devmem 0x448001A0 32 0   (then reboot)
 */
/*
 * v165: the gate flag MOVED from DRAM (0x44800000 = preloader-reserved region)
 * to the WDT NONRST_REG2 register (toprgu 0x10007024). The DRAM region is
 * CLOBBERED by the display's own memory buffers the moment the display is
 * enabled (that's why the v164 flag read back as 0xe28100c0 garbage and logs
 * turned to mush -- NOT fastboot, NOT M4U-scrambles-DRAM). NONRST_REG2 is a
 * register in the always-on toprgu block: it SURVIVES a WDT warm reset (that's
 * its purpose) and the display cannot touch it. The active mt6755 wdt driver
 * only READS it, so it is ours. Retry the display from adb:
 *   busybox devmem 0x10007024 32 0   (clear -> next boot ATTEMPTS), then reboot.
 */
#define FORGE_WDT_NONRST2_OFF	0x24
#define FORGE_DISP_ATTEMPT	0xD15B0001u	/* attempting display this boot */
#define FORGE_DISP_SKIP		0xD15B0002u	/* last boot wedged -> skip (sticky) */
#define FORGE_DISP_DONE		0xD15BD09Eu	/* attempted and survived to late init */
int forge_skip_display;
EXPORT_SYMBOL(forge_skip_display);
static void __iomem *forge_disp_wdt(void)
{
	return forge_wdt_base ? forge_wdt_base : toprgu_base;
}
/*
 * v167: the persistent-flag auto-recovery is SHELVED -- it can't read the WDT
 * reliably this early (of_platform runs in setup_arch, before a usable toprgu
 * mapping) and the flag doesn't survive the preloader reset path. The wedging
 * display component is instead found by BINARY SEARCH over flashes, using the
 * operator's screen + adb as the oracle: whatever is listed in the gated
 * forge_display_deny[] (of/platform.c) stays denied, the rest probes. So this
 * always returns "deny the gated set". (forge_disp_decided / the NONRST2 flag
 * defines / forge_disp_wdt() are retained for forge_disp_mark_ok and a future
 * reliable-store harness.) */
int forge_should_skip_display(void)
{
	return 1;
}
EXPORT_SYMBOL(forge_should_skip_display);
static int __init forge_disp_mark_ok(void)
{
	void __iomem *w = forge_disp_wdt();

	if (!forge_skip_display && w)
		writel(FORGE_DISP_DONE, w + FORGE_WDT_NONRST2_OFF);
	pr_emerg("[FORGE_M681] display gate(NONRST2): late-init reached, skip=%d\n",
		 forge_skip_display);
	return 0;
}
late_initcall(forge_disp_mark_ok);

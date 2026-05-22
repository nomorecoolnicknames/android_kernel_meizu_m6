# Meizu M6 Source Kernel Bring-up State

## 2026-05-21 source display resumes from stock-prebuilt proof

FACT: New stock-good reference capture is `/home/n8n/forge-work/debug/0b13c8c6-d194-431f-a397-f852e3aae7d9/25e8d559-1fc0-4bca-a512-30f52f1e4687/browser-bootdiag-1779358816135.tar`, sha256 `b9e741dadb4cda6c9e83e482a1d962c1b0d21f70dca1b07372254e0cfcc0c9b9`, extracted at `/tmp/m6_1779358816135`. Runtime identity is `78c034cde8`, `ro.forge.meizu.kernel=stock-7.1.2.0G-prebuilt-3.18.35`, and `Linux 3.18.35+`. The local stock-prebuilt boot reference is `/srv/forge/android/export/meizu_m6_artifacts/20260521-stock-parity-78c034cde8/boot.img` sha256 `cd959b6db468d06578d82bbb853aa783852ede441eae918abc8ccba1308a9565`, but the capture lacks raw boot partition so exact flashed boot hash is not proven.

FACT: The stock-good capture boots Android with `sys.boot_completed=1`, visible/usable non-inverted display, `ro.sf.hwrotation=0`, SurfaceFlinger `Built-in Screen` `720x1280`, `orient=0`, `flips=10375`, `powerMode=2`, `isDisplayOn=1`, active HWC layers, and high interrupt counts for `mtk_cmdq`, `ovl0`, `rdma0`, `dsi0`, `ovl0_2l`, and Mali. The compared grep set has no source-kernel-style repeated `Frame didn't finished`, `[OVL-IN-0] fence didn't signal`, `timeline-primary`, or RDMA/WDMA EOF stall.

INFERENCE: Stock-screen factory evidence is now sufficient for the source-kernel display parity loop. Do not spend the next step on rizin for screen. Use rizin only if direct source comparison and runtime oracle checks cannot recover a stock-only LCM/CMDQ/DDP detail.

HYPOTHESIS: The source-kernel display blocker remains DSI/LCM/CMDQ/DDP clock/event/route parity, not rotation, OMX, or Android userspace. The next source-kernel patch cycle must be display-only and judged against `1779358816135`: same `720x1280`/rotation, HWC active, high display IRQs, and no RDMA/WDMA EOF stall.

Current next source-kernel artifact to test: `/srv/forge/android/export/meizu_m6_artifacts/20260521-source-rollback-ddp-ili-on-78c034cde8/boot-source-rollback-ddp-ili-on-78c034cde8.img`, sha256 `0da568e18dd38d43651b7166aba2c8bd24410161925e2dfde2ccd71f01ef1166`. It repacks the rollback/DDP/ILI source kernel payload `Image-rollback-ddp-ili.gz-dtb` sha256 `e89a440542f5458183b5705a563b986b044b0589eceda97de7c009dd033bc025` with the proven `78c034cde8` stock-parity ramdisk sha256 `9f281a9df3e766bf8cd9a31bd38468fc21b78cb026e98d34f8783d11a655186f`, stock-parity board `1554686824`, and stock-parity boot geometry. Matching `System.map.rollback-ddp-ili` sha256 is `973ab65d19dc453db57d6abca91d739739584b6f815782e5a59bf8887c6625cf`; matching `kernel-rollback-ddp-ili.config` sha256 is `ffc2f683116d20eb9c0e7d9d98011da2810a223ac353b91fd21158215f2126cc`. Build notes: `/srv/forge/android/export/meizu_m6_artifacts/20260521-source-rollback-ddp-ili-on-78c034cde8/BUILD_NOTES.md`. This is a display diagnostic source-kernel boot image, not a final display fix.

Verification commands for that source artifact:

```bash
sha256sum -c /srv/forge/android/export/meizu_m6_artifacts/20260521-source-rollback-ddp-ili-on-78c034cde8/SHA256SUMS
gzip -cd /srv/forge/android/export/meizu_m6_artifacts/source-kernel-manual-20260520/Image-rollback-ddp-ili.gz-dtb 2>/dev/null | strings | grep -E 'M6 DDP clk|ili9881p_hd_dsi_txd'
grep -n -E 'ro.forge.meizu.kernel|Linux version|ro.sf.hwrotation|sys.boot_completed' <next-source-capture>/mtp/adb/getprop.txt <next-source-capture>/mtp/adb/proc_version.txt
grep -n -E 'Built-in Screen|powerMode=2|isDisplayOn=1|flips=|orient=|HWC|720x1280|VSYNC' <next-source-capture>/mtp/adb/surfaceflinger.txt
cat <next-source-capture>/mtp/adb/interrupts_focus.txt | grep -E 'mtk_cmdq|ovl0|rdma0|dsi0|mali'
grep -R -n -E 'M6 DDP clk|MMSYS_CG_CON0|DISP_DL_VALID_0|DISP_DL_READY_0|CMDQ_EVENT_DISP_(RDMA0|WDMA0)_EOF|Frame didn.t finished|OVL-IN-0|timeline-primary|present_fence_w|Built-in Screen|SetPowerMode' <next-source-capture>/mtp/adb
```

## 2026-05-20 source display DDP clock bundle

FACT: Kernel tree `/srv/forge/android/kernel-meizu_M6-N-ex6-linux-3.18.140` is on branch `work/m6-source-display-manual-20260520`; base HEAD before this bundle was `62cf59f982e8874a52b09fae498d6493b53cd1ff`.

FACT: Pre-existing dirty `primary_display.c` diff was preserved at `/srv/forge/android/export/meizu_m6_artifacts/source-kernel-manual-20260520/preexisting-primary-display-dirty.diff`, sha256 `8559b3ef29b2b1ed01d08c07d91dab0404e0129090a49a40f06bc4cf9a8f2654`.

FACT: Stock-prebuilt good reference capture is `/home/n8n/forge-work/debug/0b13c8c6-d194-431f-a397-f852e3aae7d9/39a7ee88-7f94-4d03-8075-662200b4d40c/browser-bootdiag-1779306644499.tar`, sha256 `1b0c4d46d7be7d8f4b6afad7dc5f7fc457160992c2cbf3a7e54364bbcf9b50e4`. It runs stock kernel `3.18.35+`; SurfaceFlinger flips high; VSYNC is enabled; interrupt counts are high for `mtk_cmdq`, `ovl0`, `rdma0`, and `dsi0`; no repeated GED/timeline/fence stall was found in the compared grep set.

FACT: Source-kernel bad reference capture is `/home/n8n/forge-work/debug/0b13c8c6-d194-431f-a397-f852e3aae7d9/af4d0e0e-694e-44c4-9d08-b702021d45c4/browser-bootdiag-1779220403277.tar`, sha256 `104bd56bf51724765d53655c4d804f0c5f2f34fbf81c19b7120321a9292a0d41`. It runs source kernel `3.18.140`; SurfaceFlinger sees the display, but VSYNC is disabled, flips stop near 7, GED/timeline stalls repeat, and interrupt counts are near zero: `mtk_cmdq=2/0`, `ovl0=1/0`, `rdma0=3/1`, `dsi0=0/0`.

FACT: Source bad dmesg shows `MMSYS_CG_CON0=0xfffefffc`, `DISP_DL_VALID_0=0`, `DISP_DL_READY_0=0`, repeated waits on `CMDQ_EVENT_DISP_RDMA0_EOF` and `CMDQ_EVENT_DISP_WDMA0_EOF`, token value 0, caller `OverlayEngine_0`.

INFERENCE: The current display frontier is not OMX or rotation. The earliest proven display delta is that source kernel never produces real OVL/RDMA/DSI/CMDQ progress while stock does, and the source display clock gates remain mostly gated at the DDP dump point.

PATCH HISTORY, BOOT-UNBLOCK, 2026-05-20: enable real M6 DDP engine clocks and remove the first-wait fake bypass.

Hypothesis: the source kernel display path deadlocks at RDMA/WDMA EOF because the DDP modules in the primary path are initialized while their MMSYS clock gates remain disabled. Re-enabling the normal per-module clocks for OVL, RDMA, WDMA, UFOE, COLOR, AAL, GAMMA, and DITHER should allow real EOF/IRQ progress; removing the first video pre-arm wait skip prevents a fake-ready marker from hiding the same hardware stall.

Evidence: stock-prebuilt capture above has high CMDQ/OVL/RDMA/DSI interrupt activity and no repeated fence stall; source capture above has near-zero display interrupts, `MMSYS_CG_CON0=0xfffefffc`, `DISP_DL_VALID_0=0`, `DISP_DL_READY_0=0`, and repeated `OverlayEngine_0` waits on RDMA0/WDMA0 EOF token 0. Build verification produced `/srv/forge/android/export/meizu_m6_artifacts/source-kernel-manual-20260520/boot-source-ddp-clk.img` sha256 `e279e5e684029e6c6b540573641f64445fb1617a4b3360d99dde37458151cdad`, kernel payload sha256 `cfcee965db6596958940cbcfa2d13c40abe38c8853c8c97eae2ffcae4e5867f4`, `System.map` sha256 `a2cbd240f43772ff2bb185ddc7aa8f5e50ee62afd9b70cc836f98cbf3a33c6c6`, and `kernel.config` sha256 `bc272726035c1a2eca9422e9bc230cf54f8295648a8865a98faba046ab01619e`.

Files changed: `kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_ovl.c` enables/disables OVL clocks and logs CG state; `ddp_rdma.c` enables/disables RDMA clocks and logs CG state; `ddp_wdma.c` enables/disables WDMA clocks and logs CG state; `ddp_ufoe.c` enables/disables UFOE clock and logs CG state; `common/color20/ddp_color.c`, `common/aal20/ddp_aal.c`, `common/corr10/ddp_gamma.c`, and `common/corr10/ddp_dither.c` re-enable the corresponding post-processing clocks on MT6755 and log CG state; `primary_display.c` removes the first pre-arm frame-done wait skip and leaves the normal video frame-done wait active.

Why each file changed: these modules are the primary DDP path and post-processing blocks whose clocks are reflected in `MMSYS_CG_CON0` and whose EOF/IRQ progress is absent in the source capture. `primary_display.c` owned the previous fake-ready skip; it must not mask the DDP clock test.

Expected next marker: next verified capture from `boot-source-ddp-clk.img` contains `M6 DDP clk:` lines, no `skip first pre-arm frame-done wait`, lower or cleared `MMSYS_CG_CON0` gate bits for the primary path, increased `mtk_cmdq`/`ovl0`/`rdma0`/`dsi0` interrupts, and no repeated first-frontier `Frame didn't finished` / `[OVL-IN-0] fence didn't signal` / `OverlayEngine_0` RDMA0/WDMA0 EOF token-0 stall. If it still fails, the next blocker must be earlier or more specific than the same clock-gated EOF stall.

Rollback condition: revert this bundle if the verified next capture regresses before SurfaceFlinger/DisplayManager built-in screen ON, or if the new artifact still shows the identical near-zero display IRQs plus `MMSYS_CG_CON0=0xfffefffc` with the `M6 DDP clk:` markers present.

Verification commands:

```bash
sha256sum /srv/forge/android/export/meizu_m6_artifacts/source-kernel-manual-20260520/boot-source-ddp-clk.img /srv/forge/android/export/meizu_m6_artifacts/source-kernel-manual-20260520/Image.gz-dtb /srv/forge/android/export/meizu_m6_artifacts/source-kernel-manual-20260520/System.map /srv/forge/android/export/meizu_m6_artifacts/source-kernel-manual-20260520/kernel.config
python3 - <<'PY'
import struct, hashlib
boot='/srv/forge/android/export/meizu_m6_artifacts/source-kernel-manual-20260520/boot-source-ddp-clk.img'
with open(boot,'rb') as f: data=f.read()
ksize,kaddr,rsize,raddr,ssize,saddr,taddr,pagesize,dtsize,osver=struct.unpack_from('<10I', data, 8)
print(hashlib.sha256(data).hexdigest(), ksize, rsize, pagesize, hex(kaddr), hex(raddr), hex(taddr))
PY
gzip -cd /srv/forge/android/export/meizu_m6_artifacts/source-kernel-manual-20260520/Image.gz-dtb 2>/dev/null | strings | grep -E 'M6 DDP clk|skip first pre-arm|M6 clean MTK diag'
grep -n -E 'M6 DDP clk|MMSYS_CG_CON0|DISP_DL_VALID_0|DISP_DL_READY_0|CMDQ_EVENT_DISP_(RDMA0|WDMA0)_EOF|Frame didn.t finished|OVL-IN-0|OverlayEngine_0|Built-in Screen|SetPowerMode|vsync' <next capture log files>
cat <next capture>/mtp/adb/proc_interrupts.txt | grep -E 'mtk_cmdq|ovl0|rdma0|dsi0'
```


## 2026-05-21 rollback build guard

FACT: Commit `08c93d345d8` reverted the stock-LCM source-kernel bundle after runtime regression was reported: display died and the kernel died. No newer `browser-bootdiag-*.tar` was present locally at rollback time, so this is a regression rollback, not root-cause proof.

FACT: Rebuilding the reverted tree failed at link with `undefined reference to ili9881p_hd_dsi_txd_lcm_drv` from `mt65xx_lcm_list.c`, because the old ILI9881P list entry was unconditional while the current generated config does not compile that LCM object.

PATCH HISTORY, BOOT-UNBLOCK, 2026-05-21: guard the optional ILI9881P LCM list entry so the rollback/diagnostic source kernel can build without reintroducing the stock-LCM bundle.

Hypothesis: the rollback tree is build-blocked only by an optional LCM table entry being referenced when its driver is not compiled. Guarding the extern and table entry with `ILI9881P_HD_DSI_TXD` restores build determinism without changing runtime panel selection.

Evidence: `/srv/forge/android/export/meizu_m6_artifacts/source-kernel-manual-20260520/source-kernel-rollback-rebuild.log` ends with `undefined reference to ili9881p_hd_dsi_txd_lcm_drv`; after guarding the entry, `/srv/forge/android/export/meizu_m6_artifacts/source-kernel-manual-20260520/source-kernel-rollback-rebuild2.log` completes `Image.gz-dtb`. Built rollback artifact is `/srv/forge/android/export/meizu_m6_artifacts/source-kernel-manual-20260520/boot-source-rollback-ddp-ili.img`, sha256 `bc280498ec794c96dbe596e8d917fdf7ef654ce2d39a658fe9a210eb5b264a3f`; kernel payload sha256 `e89a440542f5458183b5705a563b986b044b0589eceda97de7c009dd033bc025`; System.map sha256 `973ab65d19dc453db57d6abca91d739739584b6f815782e5a59bf8887c6625cf`; kernel config sha256 `ffc2f683116d20eb9c0e7d9d98011da2810a223ac353b91fd21158215f2126cc`.

Files changed: `kernel-3.18/drivers/misc/mediatek/lcm/mt65xx_lcm_list.c` guards the optional ILI9881P extern and list entry; `BRINGUP_STATE.md` records this build-guard checkpoint.

Expected next marker: a fresh capture from `boot-source-rollback-ddp-ili.img` should identify source kernel `3.18.140`, include `M6 DDP clk:` markers, and either reproduce the earlier display frontier without kernel death or expose a fresh earlier panic.

Rollback condition: revert this guard only if the ILI9881P driver is intentionally compiled into this config and the guard hides a required active panel path.

Verification commands: `git diff --check`; `make -C /srv/forge/android/kernel-meizu_M6-N-ex6-linux-3.18.140/kernel-3.18 O=/srv/forge/work/m6-source-kernel-manual-20260520/out ARCH=arm64 CROSS_COMPILE=/srv/forge/android/rom-meizu_M6-lineage-cm-14.1/prebuilts/gcc/linux-x86/aarch64/aarch64-linux-android-4.9/bin/aarch64-linux-android- -j8 Image.gz-dtb`; `sha256sum /srv/forge/android/export/meizu_m6_artifacts/source-kernel-manual-20260520/boot-source-rollback-ddp-ili.img /srv/forge/android/export/meizu_m6_artifacts/source-kernel-manual-20260520/Image-rollback-ddp-ili.gz-dtb /srv/forge/android/export/meizu_m6_artifacts/source-kernel-manual-20260520/System.map.rollback-ddp-ili /srv/forge/android/export/meizu_m6_artifacts/source-kernel-manual-20260520/kernel-rollback-ddp-ili.config`.

## 2026-05-21 source boot failure 1779361010015: stale LCM config

FACT: Recovery capture `/home/n8n/forge-work/debug/0b13c8c6-d194-431f-a397-f852e3aae7d9/551aed01-9901-43b0-8492-4e51cb05f958/browser-bootdiag-1779361010015.tar` sha256 `eaa682e19672196c80a51624a71b5cd4f90d93d80cac67bdae7a654ccbfcf6f2` is fresh enough for the failed source-kernel boot: recovery userspace reports `ro.boot.mode=recovery`, `ro.twrp.version=3.2.3-by uznaikaz`, and recovery `/proc/version` is stock `Linux 3.18.35+`, while pstore/last_kmsg records the previous failed kernel as `Linux 3.18.140 #5 Thu May 21 02:11:45 CDT 2026`.

FACT: Earliest display/kernel failure in pstore is at 1.582298s: `[DISP]FATAL ERROR!!!LCM Driver defined in kernel(nt35695_fhd_dsi_cmd_truly_nt50358_720p_drv) is different with LK(ili9881p_hd_dsi_txd)`, then `plcm is null`, then `ASSERT FAILED .../ddp_manager.c, 1174`, then `PC is at dpmgr_path_get_last_config+0x84/0x88`. The call trace goes through `primary_frame_cfg_input`, `mtkfb_probe`, and `mtkfb_init`.

FACT: The bad tested kernel artifact used stale generated config: `/srv/forge/work/m6-source-kernel-manual-20260520/out/.config` and exported `kernel-rollback-ddp-ili.config` had `CONFIG_CUSTOM_KERNEL_LCM="nt35695_fhd_dsi_cmd_truly_nt50358_720p"`, despite the source defconfig currently saying `CONFIG_CUSTOM_KERNEL_LCM="ili9881p_hd_dsi_txd"`.

INFERENCE: User observation is correct: display is lost before the kernel panic because `disp_lcm_probe()` rejects the LK-selected panel name, leaves `plcm` null, and the framebuffer/DDP init path later crashes on that null display path. This capture does not yet test the DDP clock frontier.

REJECTED: Rizin is not the next required step for this specific failure. The factory screen identity needed for this boot is already in the log: LK passes `ili9881p_hd_dsi_txd`, and the source tree already has that LCM driver and defconfig. Reverse only if the corrected ILI artifact passes this mismatch and then exposes a stock-only unknown panel/DDP detail.

PATCH HISTORY, BOOT-UNBLOCK, 2026-05-21: rebuild source boot after applying `meizu_m6_defconfig`.

Hypothesis: the failed source boot was produced from a stale out-tree `.config` left behind by the reverted stock-LCM experiment. Regenerating `.config` from `meizu_m6_defconfig` before building should compile `ili9881p_hd_dsi_txd`, remove the fatal LK/kernel LCM mismatch, and expose the next real display frontier.

Evidence: capture and pstore lines above; build log `/srv/forge/android/export/meizu_m6_artifacts/source-kernel-manual-20260520/source-kernel-ili-defconfig-rebuild.log` shows `meizu_m6_defconfig`, then `.config` lines `CONFIG_MTK_LCM=y`, `CONFIG_CUSTOM_KERNEL_LCM="ili9881p_hd_dsi_txd"`, `# CONFIG_MTK_LCM_DEVICE_TREE_SUPPORT is not set`, then a successful `Image.gz-dtb` build. The new kernel strings contain `ili9881p_hd_dsi_txd` and `M6 DDP clk`, and do not contain `nt35695_fhd_dsi_cmd_truly_nt50358_720p_drv`.

Files changed: no kernel source change; artifact-only rebuild plus this state update. The active source files already had the correct M6 defconfig and guarded ILI LCM list entry.

Why each file changed: state file records the proven regression and replaces the stale test artifact route with a defconfig-clean artifact; source was not changed because the proven defect was build output identity, not source content.

Expected next marker: next capture from the corrected artifact must not contain `FATAL ERROR!!!LCM Driver defined in kernel(nt35695_fhd_dsi_cmd_truly_nt50358_720p_drv) is different with LK(ili9881p_hd_dsi_txd)`, `plcm is null`, or the same `ddp_manager.c, 1174` panic. It should either continue into ILI LCM init/resume and DDP clock logs, or reveal the next earlier source-kernel blocker.

Rollback condition: reject this artifact only if verified flashed boot hash `3d6695685ad847b10cb915dcc200dab0ddbe8b63db8a1eb9e5ba1dd889fde51c` still produces the exact nt35695-vs-ili mismatch. If a later display failure appears without this mismatch, continue from the new earliest line rather than reverting to the stale config.

Corrected source artifact: `/srv/forge/android/export/meizu_m6_artifacts/20260521-source-ili-defconfig-on-78c034cde8/boot-source-ili-defconfig-on-78c034cde8.img` sha256 `3d6695685ad847b10cb915dcc200dab0ddbe8b63db8a1eb9e5ba1dd889fde51c`. Kernel payload `Image-ili-defconfig.gz-dtb` sha256 `625bed885e37d7477729ea237bbca171e060871b53dfb423baa340ec95c0fead`; `System.map.ili-defconfig` sha256 `5c58cb6fdcb458666ea8108ac90257d04704792e6dbdddd4355ff63328b66729`; `kernel-ili-defconfig.config` sha256 `bc272726035c1a2eca9422e9bc230cf54f8295648a8865a98faba046ab01619e`; `vmlinux-ili-defconfig` sha256 `14d5ed845baed7a8728bab19d1241bd24fcdcdae65a3f43e997a932d55f0ee8d`; stock-parity ramdisk sha256 `9f281a9df3e766bf8cd9a31bd38468fc21b78cb026e98d34f8783d11a655186f`; rebuild log sha256 `ab8cfaeb71f83335f7cb15745c29a1685bbeef665be171878b1334049a96f231`. Boot header: kernel addr `0x40080000`, ramdisk addr `0x45000000`, tags addr `0x44000000`, page size `2048`, board `1554686824`, cmdline `bootopt=64S3,32N2,64N2 androidboot.selinux=permissive buildvariant=userdebug`, OS version `7.1.2 / 2021-06-01`.

Verification commands:

```bash
sha256sum -c /srv/forge/android/export/meizu_m6_artifacts/20260521-source-ili-defconfig-on-78c034cde8/SHA256SUMS
grep -n -E 'CONFIG_CUSTOM_KERNEL_LCM|CONFIG_MTK_LCM_DEVICE_TREE_SUPPORT|CONFIG_MTK_LCM=' /srv/forge/android/export/meizu_m6_artifacts/20260521-source-ili-defconfig-on-78c034cde8/kernel-ili-defconfig.config
gzip -cd /srv/forge/android/export/meizu_m6_artifacts/20260521-source-ili-defconfig-on-78c034cde8/Image-ili-defconfig.gz-dtb 2>/dev/null | strings | grep -E 'ili9881p_hd_dsi_txd|M6 DDP clk'
gzip -cd /srv/forge/android/export/meizu_m6_artifacts/20260521-source-ili-defconfig-on-78c034cde8/Image-ili-defconfig.gz-dtb 2>/dev/null | strings | grep -F 'nt35695_fhd_dsi_cmd_truly_nt50358_720p_drv' || true
grep -R -n -E 'FATAL ERROR!!!LCM|plcm is null|ddp_manager.c, 1174|ili9881p_hd_dsi_txd|M6 DDP clk|Built-in Screen|SetPowerMode|Frame didn.t finished|CMDQ_EVENT_DISP_(RDMA0|WDMA0)_EOF' <next-capture>/mtp/adb <next-capture>/mtp/adb-files
```

## 2026-05-22 source display diag for valid/ready zero

FACT: Kernel display path is stalling on CMDQ RDMA0 EOF timeout, and the previous diagnostic capture states `MMSYS_CG_CON0` clock gating was resolved but `DISP_DL_VALID_0` remains 0.
FACT: If `DISP_DL_VALID_0` is 0, the internal DDP data path is physically not outputting valid data from OVL0/COLOR0 to downstream modules.

PATCH HISTORY, BOOT-UNBLOCK, 2026-05-22: explicitly release MMSYS hardware resets, enable SMI LARB0 test mode, and insert a register dump on EOF timeout.
Hypothesis: `DISP_DL_VALID_0` is 0 because either the display modules are held in reset (`MMSYS_SW0_RST_B`), OVL0 is stalling on `SMI_LARB0` fetch, or the `MOUT`/`SEL` routing is disconnected. Releasing resets and forcing the test mode should clear hardware stalls, and dumping the exact `MMSYS_CONFIG` registers upon timeout will isolate the broken link if it still stalls.
Evidence: previous log identified `DISP_DL_VALID_0=0` with `MMSYS_CG_CON0=0xfffefffc`.
Files changed: `kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_path.c` explicitly writes `0xFFFFFFFF` to `MMSYS_SW0_RST_B`, `SW1_RST_B`, and `LCM_RST_B`, and writes 1 to `MMSYS_MISC_FLD_SMI_LARB0_TEST_MODE`. `kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_manager.c` adds `M6 clean MTK diag: VALID_0=...` to dump pipeline state upon `DISP_PATH_EVENT_FRAME_DONE` wait timeout.
Expected next marker: Next log will either boot without RDMA0 EOF timeout (if reset/SMI fix worked), or the dmesg will contain `M6 clean MTK diag: VALID_0=` with the exact routing register values (`OVL0_MOUT`, `COLOR0_SEL`, `DITHER_MOUT`, `RDMA0_SOUT`, `SW0_RST`, `MMSYS_CG`).
Rollback condition: Revert the reset and SMI test mode changes if they cause immediate memory controller panic.
Verification commands:
```bash
sha256sum -c /srv/forge/android/export/meizu_m6_artifacts/source-kernel-manual-20260520/SHA256SUMS.diag-valid
grep -E 'M6 clean MTK diag: VALID_0=' <next-capture>/mtp/adb/dmesg.txt
```


## 2026-05-22 source display no-ADB regression after diag-valid

FACT: Recovery evidence archive `/home/n8n/forge-work/debug/0b13c8c6-d194-431f-a397-f852e3aae7d9/faedc765-6ef0-4896-aebd-759033f3c400/browser-debug-evidence-1779441875959.tar` has sha256 `697dec96c1e3c786a12a7762b55ea64c475a163818c0bb2d132199dcfb1365b9`. It identifies the recovery kernel as stock `Linux version 3.18.35+`, boot mode `recovery`, bootreason `wdt_by_pass_pwk`, and timestamp `Fri May 22 19:23:28 UTC 2026`.

FACT: The same archive did not capture the failed `/boot` partition hash. It contains recovery/logo/expdb/para/rstinfo hashes only, so exact flashed boot identity for this no-ADB failure is not proven from the capture itself.

FACT: The only failed-kernel signal in `pstore_dump.txt` and `/proc/last_kmsg` is `ram console header, hw_status: 2, fiq step 32` plus `SPM_SW_RSV5(0xfffffffe) 1pll init(val0=0x417000)` at `0.467288`. No source-kernel display log, LCM log, regular dmesg line, or `M6 clean MTK diag` line survived in the archive.

FACT: The suspected artifact `/srv/forge/android/export/meizu_m6_artifacts/source-kernel-manual-20260520/boot-source-diag-valid-0.img` has sha256 `34eb150e3c3bd165cb72973a4f52ed34b68a972b9bf4f074577b2f6ab887d7a5`; matching payload `/srv/forge/android/export/meizu_m6_artifacts/source-kernel-manual-20260520/Image-diag-valid.gz-dtb` has sha256 `9d9ab9d5e08f5dd438188026386e98ff8be1fabf97267310cca2dde8501ec384`; matching `System.map.diag-valid` has sha256 `58cb3b4eefa0f4919cb4c0a9910d62c96e38fc8c92ec59eae24efb7b3baa9405`.

INFERENCE: The no-ADB regression happened before the display timeout diagnostic could run. The strongest local code delta with boot-kill potential was the uncommitted `ddp_path_init()` behavior change: writes to `MMSYS_SW0_RST_B`, `MMSYS_SW1_RST_B`, `MMSYS_LCM_RST_B`, and `MMSYS_MISC_FLD_SMI_LARB0_TEST_MODE` before normal display top-clock power sequencing.

REJECTED: Treating the reset/SMI test-mode writes as a BOOT-UNBLOCK fix is rejected. They were broad behavioral register writes, not backed by entry/exit markers, and the next evidence regressed to WDT/no ADB. They are removed from the worktree before the next build. The read-only `dpmgr_wait_event_timeout()` dump remains useful and is kept.

PATCH HISTORY, DIAGNOSTIC, 2026-05-22: remove unsafe DDP path init writes from the next source-kernel test and keep only the read-only timeout dump.
Hypothesis: The diag-valid no-ADB regression was caused by touching MMSYS reset and SMI test-mode registers at `ddp_path_init()` before the normal `ddp_path_top_clock_on()`/module init flow. Returning `ddp_path_init()` to pointer table initialization should restore the previous source-kernel boot/ADB frontier, while the timeout dump in `ddp_manager.c` can still report the first DDP route/VALID/READY failure if RDMA EOF remains.
Evidence: capture and hashes above; source inspection shows `ddp_drv.c` calls `ddp_path_init()` immediately after register `of_iomap()` and before IRQ request, while `ddp_manager.c` later owns `ddp_path_top_clock_on()`. The current retained code path is read-only and emits `M6 clean MTK diag: VALID_0=...` only after a display path wait timeout.
Files changed: `kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_path.c` has the uncommitted MMSYS reset/SMI writes removed from the worktree; `kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_manager.c` keeps the read-only timeout register dump; `BRINGUP_STATE.md` records this rejection and next verification route.
Why each file changed: `ddp_path.c` must not mutate reset/test-mode state before the proven display clock/init sequence; `ddp_manager.c` is the earliest available safe timeout point for observing whether `DISP_DL_VALID_0`, `DISP_DL_READY_0`, and route registers remain wrong after the boot reaches userspace again.
Expected next marker: a verified next capture should at minimum return to source kernel `3.18.140` with ADB/bootdiag or a richer pstore than the `SPM_SW_RSV5 1pll init` line. If it reaches the previous display frontier, dmesg should contain `M6 clean MTK diag: VALID_0=...` on RDMA EOF timeout.
Rollback condition: If a verified flashed boot hash from the rebuilt artifact still dies with only the same early WDT/`SPM_SW_RSV5 1pll init` signal, stop treating the removed DDP writes as sufficient and inspect non-display kernel deltas plus add earlier forge markers before another display behavior change.
Verification commands:
```bash
sha256sum <rebuilt-boot.img> <rebuilt-Image.gz-dtb> <rebuilt-System.map> <rebuilt-.config>
gzip -cd <rebuilt-Image.gz-dtb> 2>/dev/null | strings | grep -E 'ili9881p_hd_dsi_txd|M6 clean MTK diag|M6 DDP clk'
gzip -cd <rebuilt-Image.gz-dtb> 2>/dev/null | strings | grep -E 'MMSYS_SW0_RST_B|SMI_LARB0_TEST_MODE' || true
grep -R -n -E 'SPM_SW_RSV5|M6 clean MTK diag: VALID_0=|FATAL ERROR!!!LCM|plcm is null|CMDQ_EVENT_DISP_(RDMA0|WDMA0)_EOF|M6 DDP clk|Built-in Screen|SetPowerMode' <next-capture>/evidence <next-capture>/mtp
```

## 2026-05-22 source readonly DDP diagnostic artifact

PATCH HISTORY, DIAGNOSTIC, 2026-05-22: keep only read-only DDP timeout diagnostics after rejecting unsafe reset/SMI writes.

Hypothesis: the last no-ADB regression was caused by broad MMSYS reset/SMI test-mode writes in `ddp_path_init()`, not by the read-only timeout dump itself. Building the ILI defconfig tree with only `dpmgr_wait_event_timeout()` register logging should return to the prior source-kernel boot/ADB frontier and, if the RDMA/WDMA EOF stall remains, emit the exact VALID/READY/route registers needed for the next evidence-backed display patch.

Evidence: recovery capture `/home/n8n/forge-work/debug/0b13c8c6-d194-431f-a397-f852e3aae7d9/faedc765-6ef0-4896-aebd-759033f3c400/browser-debug-evidence-1779441875959.tar` had only `SPM_SW_RSV5(0xfffffffe) 1pll init` and no DDP timeout marker after the unsafe reset/SMI experiment. Source inspection showed `ddp_path_init()` runs before normal top-clock/module init, while this retained patch only logs after an already-observed display wait timeout.

Files changed: `kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_manager.c` logs `DISP_DL_VALID_0`, `DISP_DL_READY_0`, OVL/COLOR/DITHER/RDMA/UFOE route registers, SW0 reset state, and `MMSYS_CG_CON0` only when `dpmgr_wait_event_timeout()` times out. `BRINGUP_STATE.md` records the build and test route.

Why each file changed: `ddp_manager.c` owns the proven RDMA/WDMA EOF timeout point and can observe the route without mutating display hardware state; the state file is the required checkpoint for the next agent/test capture.

Expected next marker: a verified recovery capture hashes `/dev/block/mmcblk0p21` as `3071aa69097dffe6c3e3668d8ea4cda4ae3cb5135bddac497bd324d4655b6cbc`. If Android reaches the previous display frontier, dmesg/logcat contains `M6 clean MTK diag: VALID_0=` plus `M6 DDP clk:` lines. If it still dies before ADB, pstore/last_kmsg must at least prove whether the failure is the same early WDT/SPM-only case.

Rollback condition: revert this diagnostic if the verified flashed boot hash above still dies with only the same early WDT/`SPM_SW_RSV5 1pll init` signal and no display markers; in that case add earlier boot-stage markers before touching display registers again.

Verification commands:

```bash
sha256sum -c /srv/forge/android/export/meizu_m6_artifacts/20260522-source-readonly-ddp-diag-on-78c034cde8/SHA256SUMS
zip -T /srv/forge/android/export/meizu_m6_artifacts/20260522-source-readonly-ddp-diag-on-78c034cde8/m6-source-readonly-ddp-diag-bootonly-20260522-signed.zip
grep -n -E 'CONFIG_CUSTOM_KERNEL_LCM|CONFIG_MTK_LCM_DEVICE_TREE_SUPPORT|CONFIG_MTK_LCM=' /srv/forge/android/export/meizu_m6_artifacts/20260522-source-readonly-ddp-diag-on-78c034cde8/kernel-readonly-ddp-diag.config
gzip -cd /srv/forge/android/export/meizu_m6_artifacts/20260522-source-readonly-ddp-diag-on-78c034cde8/Image-readonly-ddp-diag.gz-dtb 2>/dev/null | strings | grep -E 'ili9881p_hd_dsi_txd|M6 clean MTK diag|M6 DDP clk'
grep -R -n -E 'SPM_SW_RSV5|M6 clean MTK diag: VALID_0=|FATAL ERROR!!!LCM|plcm is null|CMDQ_EVENT_DISP_(RDMA0|WDMA0)_EOF|M6 DDP clk|Built-in Screen|SetPowerMode' <next-capture>/evidence <next-capture>/mtp
```

Artifacts: boot image `/srv/forge/android/export/meizu_m6_artifacts/20260522-source-readonly-ddp-diag-on-78c034cde8/boot-source-readonly-ddp-diag-on-78c034cde8.img` sha256 `3071aa69097dffe6c3e3668d8ea4cda4ae3cb5135bddac497bd324d4655b6cbc`; signed recovery zip `/srv/forge/android/export/meizu_m6_artifacts/20260522-source-readonly-ddp-diag-on-78c034cde8/m6-source-readonly-ddp-diag-bootonly-20260522-signed.zip` sha256 `8447100fc1cdb2fb964611905673c993a6806b3a8da41f95b2853d99108ca4df`; Build Station build `a930069d-ed6c-450c-b460-6cb6c4a28444`, boot artifact `f85ad5b3-7886-43a6-bdb2-85b5ff015e49`, flashable artifact `11491810-168b-4e5b-b143-f778d506e1b8`.

## 2026-05-22 flashable v2 extractfix

FACT: The first signed flashable zip `m6-source-readonly-ddp-diag-bootonly-20260522-signed.zip` used a new minimal installer that extracted `boot.img` with `unzip -p` and only checked `unzip`, `/sbin/busybox`, and `/tmp/busybox`. User reported recovery error `cannot extract boot img`.

FACT: The known-working hotfix package `/srv/forge/android/export/meizu_m6_artifacts/20260522-m6-14.1-9ed1d8cc-flashable-hotfix/m6-14.1-9ed1d8cc-flashable-hotfix.zip` uses `unzip -o "$ZIP" "$SRC" -d "$TMP"` plus fallbacks for `unzip`, `/sbin/unzip`, `busybox`, and `/sbin/busybox`.

INFERENCE: The failure is installer compatibility, not kernel payload identity. The v2 package keeps the exact same boot image sha256 `3071aa69097dffe6c3e3668d8ea4cda4ae3cb5135bddac497bd324d4655b6cbc` and only changes recovery extraction logic to match the working hotfix style.

Artifacts: fixed signed zip `/srv/forge/android/export/meizu_m6_artifacts/20260522-source-readonly-ddp-diag-on-78c034cde8/m6-source-readonly-ddp-diag-bootonly-20260522-v2-extractfix-signed.zip` sha256 `8636a0e532eb64b7af34ea515ee961da8b7c5e927ad7b52f816aa3ef17244d71`; Build Station artifact `5d80020e-22c3-4f42-9cbf-da8e13c659dc`; old flashable artifact `11491810-168b-4e5b-b143-f778d506e1b8` is marked superseded in DB metadata.

Verification commands:
```bash
zip -T /srv/forge/android/export/meizu_m6_artifacts/20260522-source-readonly-ddp-diag-on-78c034cde8/m6-source-readonly-ddp-diag-bootonly-20260522-v2-extractfix-signed.zip
unzip -p /srv/forge/android/export/meizu_m6_artifacts/20260522-source-readonly-ddp-diag-on-78c034cde8/m6-source-readonly-ddp-diag-bootonly-20260522-v2-extractfix-signed.zip boot.img | sha256sum
docker exec android-forge-postgres-1 psql -U forge -d forge -c "select id, sha256, size_bytes, metadata->>'filename' from artifacts where id='5d80020e-22c3-4f42-9cbf-da8e13c659dc';"
```


## 2026-05-22 fresh source-kernel LOS15 capture 6b174034

FACT: Fresh capture `/home/n8n/forge-work/debug/0b13c8c6-d194-431f-a397-f852e3aae7d9/878abca6-e70a-4ae0-b5fb-2ce407070669/browser-debug-evidence-1779450514270.tar` has sha256 `f19fb1e1c9b49c09209d4e16f4963b7f793c7bbd928b76bb950983f04b3b7448`; Build Station test `6b174034-9baa-47af-8bd1-70e5f89f12c4` recorded `boot_state=adb_online`, `boot_completed=false`, `adb_reconnected=true`.

FACT: Captured `/dev/block/mmcblk0p21` is the source readonly DDP diagnostic boot image, not stale 14.1/stock-kernel evidence. Full partition sha256 is `7db305d13cad844187ac93326a8a6de6c064f33926c1efd9ee5e6cb47a960c07`; trimming to the Android boot image length `9922560` matches `/srv/forge/android/export/meizu_m6_artifacts/20260522-source-readonly-ddp-diag-on-78c034cde8/boot-source-readonly-ddp-diag-on-78c034cde8.img` sha256 `3071aa69097dffe6c3e3668d8ea4cda4ae3cb5135bddac497bd324d4655b6cbc`. It does not match the new LOS15-ramdisk boot image sha256 `9bbb03365e2be34fdd9fa53e4c87e1e9a586ea6c2b8c446c80bc992729c04a89`, which had not been tested yet.

FACT: Runtime identity is `Linux version 3.18.140 ... #8 SMP PREEMPT Fri May 22 06:12:33 CDT 2026`, `ro.build.version.release=8.1.0`, `ro.lineage.version=15.1-UNOFFICIAL-meizu_m6`. The stale property `ro.forge.meizu.kernel=stock-7.1.2.0G-prebuilt-3.18.35` is rejected as ramdisk/system metadata, not flashed-kernel truth.

FACT: Android userspace does not complete boot. `sys.boot_completed` is empty; `init.svc.surfaceflinger` is empty; `init.svc.logd=restarting`; `init.svc.keystore=restarting`; `init.svc.mediacodec` is unstable; `ril-daemon=stopped`. Pstore/last_kmsg repeats `Service 'zygote_secondary' ... exited with status 1`, `Service 'zygote' is being killed`, `Service 'logd' ... exited with status 1`, `Service 'keystore' ... exited with status 255`, plus permissive SELinux `unlabeled` accesses under `/system/vendor`.

FACT: Display is registered but not lit. `/proc/fb` shows `0 mtkfb`; `/sys/class/graphics/fb0` reports `U:720x1280p-0`, bpp `32`, virtual `736,3840`, stride `2944`; lcd backlight brightness is `0/255`; display IRQ counts are near-zero (`mtk_cmdq 0/0/1`, `ovl0 1/0/0`, `rdma0 1/2/3`, `dsi0 0/1/0`). Live `dmesg` and `logcat` are not useful because `dmesg.txt` is effectively empty and `logd` is crashing.

REJECTED: Treating the current scrcpy exception as display proof is rejected. Scrcpy fails after `IServiceManager.getService(...) on a null object reference` while zygote/logd/keystore are looping and `servicemanager`/SurfaceFlinger are not stably observable. That is framework/service-manager instability first, not proof of a specific DSI panel layer.

INFERENCE: The earliest proven blocker for this capture is boot-image/ramdisk mismatch against LOS15 userspace. The source kernel itself reaches ADB and registers fb0, but the tested boot image used the older stock-parity/14.1-style ramdisk (`ramdisk-from-stock-78c034cde8.img` sha256 `9f281a9df3e766bf8cd9a31bd38468fc21b78cb026e98d34f8783d11a655186f`) with an Oreo 8.1 system. That mismatch is a stronger explanation for logd/keystore/zygote instability than another display register edit.

HYPOTHESIS: Repacking the same verified source kernel payload (`Image-readonly-ddp-diag.gz-dtb` sha256 `ec204ae9c47312a31f2910e00db71e65dfd3e0d48ace0367ad1803bafc7fb0c9`) with the LOS15 boot ramdisk (`ramdisk-los15.img` sha256 `564c126547097d7b1e69bedfb4d7d16a2813e5d09abad01c2e72562572ed6dcb`) should remove the Oreo ramdisk/init mismatch and either reach stable SurfaceFlinger/logcat or expose the next kernel display frontier with usable logs.

PATCH HISTORY, BOOT-UNBLOCK, 2026-05-22: repackage the source readonly DDP diagnostic kernel with the LOS15 ramdisk and register it as a clean Build Station build.

Hypothesis: The tested source-kernel boot image advanced to ADB but left Oreo userspace in a restart loop because it combined the 3.18.140 source kernel with the previous stock-parity/14.1 ramdisk. Reusing the same kernel payload with the LOS15 ramdisk should make init services coherent enough to debug the remaining black display as display, not as framework collapse.

Evidence: capture/test/hash facts above; base LOS15 boot `/srv/forge/android/rom-lineage-15.1-meizu_m6-experimental/out/target/product/meizu_m6/boot.img` sha256 `895c2d0dc2e3caafbd5815c666cc6e657c54435332840ec1e77d76c9385da590`; extracted LOS15 ramdisk sha256 `564c126547097d7b1e69bedfb4d7d16a2813e5d09abad01c2e72562572ed6dcb`; tested old source boot sha256 `3071aa69097dffe6c3e3668d8ea4cda4ae3cb5135bddac497bd324d4655b6cbc` is exactly what capture boot partition contains.

Files changed: no kernel source changed; `/srv/forge/android/export/meizu_m6_artifacts/20260522-source-readonly-ddp-diag-los15-ramdisk/boot-source-readonly-ddp-diag-los15-ramdisk.img` is the new boot image; `m6-source-readonly-ddp-diag-los15-ramdisk-v1-extractfix-signed.zip` is the recovery flashable package using the proven v2 `extract_one` installer; Build Station DB now has build `e614a1ad-6d74-4e3e-893e-b6ab80035d08`, boot artifact `978d26fe-d7dc-473e-84bc-e8bfce2a4470`, and flashable artifact `4253894b-8615-4293-bf54-9f964f77d840`; this state file records the patch cycle.

Why each file changed: the boot image changes only the ramdisk while preserving the source kernel payload and boot addresses; the zip package makes the image flashable from recovery without the prior `cannot extract boot.img` installer failure; the DB build prevents future debug identity checks from comparing the next capture against the old stock-ramdisk artifact.

Expected next marker: next capture must show boot partition trimmed sha256 `9bbb03365e2be34fdd9fa53e4c87e1e9a586ea6c2b8c446c80bc992729c04a89`. If the hypothesis is correct, `logd` and `keystore` should stop persistent restart loops and either `init.svc.surfaceflinger` should become visible/running or logcat/dmesg should contain the next display failure. If the physical panel remains black with stable userspace, continue at display IRQ/backlight/DDP diagnostic frontier.

Rollback condition: If a verified capture with boot sha256 `9bbb03365e2be34fdd9fa53e4c87e1e9a586ea6c2b8c446c80bc992729c04a89` still shows the same early zygote/logd/keystore loop and no useful logcat, reject ramdisk mismatch as sufficient and inspect LOS15 vendor/system ABI plus 64/32 zygote and linker/service crashes before another DDP behavior patch. If the new image regresses before ADB, compare the LOS15 ramdisk init/fstab/SELinux triggers against the old boot and add earlier boot markers rather than editing display registers blind.

Artifacts: boot image `/srv/forge/android/export/meizu_m6_artifacts/20260522-source-readonly-ddp-diag-los15-ramdisk/boot-source-readonly-ddp-diag-los15-ramdisk.img` sha256 `9bbb03365e2be34fdd9fa53e4c87e1e9a586ea6c2b8c446c80bc992729c04a89`, size `8794112`, kernel size `7546828`, ramdisk size `1245137`, page `2048`, kernel addr `0x40080000`, ramdisk addr `0x45000000`, tags addr `0x44000000`, board `1554686824`, OS version/patch encoded from `8.1.0 / 2021-10-05`. Signed zip `/srv/forge/android/export/meizu_m6_artifacts/20260522-source-readonly-ddp-diag-los15-ramdisk/m6-source-readonly-ddp-diag-los15-ramdisk-v1-extractfix-signed.zip` sha256 `d18022fe5ec3eff86262bd5ab5b3089ee21e4b905d0f0bb911592ddc9c3296f9`, size `8704895`; embedded `boot.img` sha256 is `9bbb03365e2be34fdd9fa53e4c87e1e9a586ea6c2b8c446c80bc992729c04a89`.

Verification commands:

```bash
sha256sum -c /srv/forge/android/export/meizu_m6_artifacts/20260522-source-readonly-ddp-diag-los15-ramdisk/SHA256SUMS
sha256sum -c /srv/forge/android/export/meizu_m6_artifacts/20260522-source-readonly-ddp-diag-los15-ramdisk/ZIP_SHA256SUMS
zip -T /srv/forge/android/export/meizu_m6_artifacts/20260522-source-readonly-ddp-diag-los15-ramdisk/m6-source-readonly-ddp-diag-los15-ramdisk-v1-extractfix-signed.zip
unzip -p /srv/forge/android/export/meizu_m6_artifacts/20260522-source-readonly-ddp-diag-los15-ramdisk/m6-source-readonly-ddp-diag-los15-ramdisk-v1-extractfix-signed.zip boot.img | sha256sum
# Next capture identity:
python3 - <<'PY2'
from pathlib import Path
import hashlib
cap = Path('<next-capture>/evidence/adb/mtk/partitions/boot-partition-16m.img').read_bytes()
expected = Path('/srv/forge/android/export/meizu_m6_artifacts/20260522-source-readonly-ddp-diag-los15-ramdisk/boot-source-readonly-ddp-diag-los15-ramdisk.img').read_bytes()
print(hashlib.sha256(cap[:len(expected)]).hexdigest())
print(cap[:len(expected)] == expected)
PY2
grep -R -n -E "init.svc.(surfaceflinger|logd|keystore|zygote|zygote_secondary)|sys.boot_completed|M6 clean MTK diag: VALID_0=|CMDQ_EVENT_DISP_(RDMA0|WDMA0)_EOF|/sys/class/leds/lcd-backlight/brightness" <next-capture>/evidence
```


## 2026-05-22 LOS15 ramdisk ADB fix boot image

FACT: User reported the first LOS15-ramdisk repack broke ADB. No fresh capture for that boot is available in this section, so the exact runtime failure is not proven from logs.

FACT: The first LOS15 ramdisk used `service adbd /system/bin/adbd`, did not write `/sys/devices/platform/11270000.usb3/musb-hdrc/cmode 1` in the generic `adb` path, and left `ro.secure=1`. The previous ADB-working stock-parity ramdisk used `/sbin/adbd`, forced `cmode 1`, and set `sys.usb.config adb` during `on boot`.

FACT: The Oreo/LOS15 build output contains a static `/system/bin/adbd` (`ELF 64-bit LSB executable, ARM aarch64, statically linked`), so it can be embedded as `/sbin/adbd` without pulling old 14.1 adbd into the new ramdisk.

HYPOTHESIS: ADB broke because the LOS15 ramdisk starts adbd from `/system/bin/adbd` and does not force the legacy `android_usb` gadget into device mode early enough for this 3.18 MTK kernel. Embedding the Oreo static adbd as `/sbin/adbd`, forcing `sys.usb.config=adb`, and restoring `cmode 1` should keep the Oreo ramdisk while recovering the known-good USB/ADB path.

PATCH HISTORY, BOOT-UNBLOCK, 2026-05-22: rebuild boot.img only with source kernel + LOS15 ramdisk + early legacy ADB wiring.

Hypothesis: The source kernel payload is still the wanted new kernel and should not be changed for this cycle. The broken part is the LOS15 ramdisk's ADB startup wiring against the legacy non-configfs MTK USB gadget.

Evidence: previous capture `6b174034-9baa-47af-8bd1-70e5f89f12c4` proved the source kernel payload boots to ADB when the old ramdisk owns USB; local ramdisk diff proves the LOS15 ramdisk changed adbd path and removed early `cmode 1`/`sys.usb.config adb`. Kernel config for this artifact has `CONFIG_USB_G_ANDROID=y`, `CONFIG_USB_F_FS=y`, and `# CONFIG_USB_CONFIGFS is not set`, so the non-configfs `/sys/class/android_usb/android0` path remains the relevant boot-time USB path.

Files changed: no kernel source changed; generated artifact `/srv/forge/android/export/meizu_m6_artifacts/20260522-source-readonly-ddp-diag-los15-adbfix/boot-source-readonly-ddp-diag-los15-adbfix.img`; generated ramdisk `/srv/forge/android/export/meizu_m6_artifacts/20260522-source-readonly-ddp-diag-los15-adbfix/ramdisk-los15-adbfix.img`; Build Station DB build `adb0660b-044c-4ae4-b7c5-166f1f920700`, boot artifact `cc277cd4-f113-43e9-b827-33229255c2d9`.

Why each file changed: `init.usb.rc` inside the generated ramdisk now uses `/sbin/adbd`, writes `cmode 1` during `on boot` and the `sys.usb.config=adb` trigger, and sets `sys.usb.config adb`; `default.prop` inside the generated ramdisk sets `ro.secure=0`, `ro.adb.secure=0`, `persist.sys.usb.config=adb`, and `sys.usb.config=adb`; `/sbin/adbd` is copied from the LOS15 system output, not from the old 14.1 ramdisk.

Expected next marker: after flashing only this boot image, `adb devices` should return. A fresh capture must show trimmed boot sha256 `7fb6f9105626a05ead9c4fcda4d3788a88368fe717054711c4f80e591ba1fef6`; runtime props should include `init.svc.adbd=running`, `sys.usb.config=adb`, `sys.usb.state=adb`, and `sys.usb.configfs=0`.

Rollback condition: If verified boot sha256 `7fb6f9105626a05ead9c4fcda4d3788a88368fe717054711c4f80e591ba1fef6` still has no ADB, capture recovery pstore plus USB sysfs if possible and inspect kernel gadget/cmode logs before changing display. If ADB returns but framework still loops, continue from logd/keystore/zygote/system ABI evidence.

Artifacts: boot image `/srv/forge/android/export/meizu_m6_artifacts/20260522-source-readonly-ddp-diag-los15-adbfix/boot-source-readonly-ddp-diag-los15-adbfix.img` sha256 `7fb6f9105626a05ead9c4fcda4d3788a88368fe717054711c4f80e591ba1fef6`, size `9445376`; ramdisk sha256 `90ae94dcce79d107fc9888596f3be80430067782195c7c419f1eab383fec3e19`; kernel payload sha256 `ec204ae9c47312a31f2910e00db71e65dfd3e0d48ace0367ad1803bafc7fb0c9`; page `2048`, kernel addr `0x40080000`, ramdisk addr `0x45000000`, tags addr `0x44000000`, OS version/patch `8.1.0 / 2021-10-05`.

Verification commands:

```bash
sha256sum -c /srv/forge/android/export/meizu_m6_artifacts/20260522-source-readonly-ddp-diag-los15-adbfix/SHA256SUMS
gzip -dc /srv/forge/android/export/meizu_m6_artifacts/20260522-source-readonly-ddp-diag-los15-adbfix/ramdisk-los15-adbfix.img | cpio -it --quiet | grep -E '^(sbin/adbd|init.usb.rc|default.prop)$'
# Next capture identity check:
python3 - <<'PY2'
from pathlib import Path
import hashlib
cap = Path('<next-capture>/evidence/adb/mtk/partitions/boot-partition-16m.img').read_bytes()
expected = Path('/srv/forge/android/export/meizu_m6_artifacts/20260522-source-readonly-ddp-diag-los15-adbfix/boot-source-readonly-ddp-diag-los15-adbfix.img').read_bytes()
print(hashlib.sha256(cap[:len(expected)]).hexdigest())
print(cap[:len(expected)] == expected)
PY2
grep -R -n -E 'init.svc.adbd|sys.usb.(config|state|configfs)|Service .adbd|android_usb|musb|cmode' <next-capture>/evidence
```

## 2026-05-22 recovery capture 1779453120821: adbfix still has broken init sequencing

FACT: Recovery capture `/home/n8n/forge-work/debug/0b13c8c6-d194-431f-a397-f852e3aae7d9/b6376b9b-3379-46cc-8caf-30b91f85aa36/browser-debug-evidence-1779453120821.tar` has archive sha256 `d040959630a9fcdac0a201a89fa77b2a6ea312047e93d13809c37ae0059aa371` and was extracted locally at `/tmp/m6-1779453120821`.

FACT: The recovery-side identity is stock recovery kernel `Linux version 3.18.35+ ... Sat Nov 18 16:44:53 CST 2017`, `bootmode=recovery`, `bootreason=power_key`. The Android-failed signal is pstore/last_kmsg only.

FACT: The capture could not hash the boot partition image identity: `mtk/boot_candidate_rejected.txt` says `/dev/block/mmcblk0p21` is missing Android boot magic at offset 0 or 512. Therefore the exact flashed boot image is not proven from this capture.

FACT: Pstore/last_kmsg shows the source kernel reaches init and runs for at least 43 seconds. USB gadget initializes at `3.792704` with `android_usb gadget: android_usb ready`.

FACT: The ramdisk content shown by pstore is still the broken LOS15 adbfix init sequence, not the post-analysis initfix-v2 sequence: `/init.rc` runs `mount_all /fstab.mt6735` and fails; `/system/etc/init` is parsed before `/system` is mounted; `init.project.rc` imports `init.mt6755.usb.rc` a second time; the second FunctionFS mount frees ffs and fails; `meizu-detect` still points to `/sbin/sh`; `logd`, `servicemanager`, `hwservicemanager`, and `vndservicemanager` are not found when started.

INFERENCE: ADB is absent because init never reaches a coherent Android service graph. Kernel USB is present, but userspace starts with wrong fs/mount/import ordering and duplicate USB init, so `adbd` is not observed as started before the boot hangs.

REJECTED: Treating this as a kernel USB regression is rejected for this capture. The kernel has `android_usb ready`; the earlier proven failure is userspace init sequencing. Also, boot image identity was not captured, so any kernel-level conclusion would be weaker than the init log evidence.

HYPOTHESIS: A boot image with the same source kernel payload but a repaired LOS15 ramdisk should recover the ADB path or at least move the next capture past `mount_all /fstab.mt6735` and duplicate FunctionFS failure.

PATCH HISTORY, BOOT-UNBLOCK, 2026-05-22: rebuild boot.img only with source kernel + LOS15 ramdisk init sequencing fix + early legacy ADB.

Hypothesis: The previous ADB-fix boot kept the source 3.18.140 kernel but left LOS15 init broken by a wrong m2note `/fstab.mt6735` block, duplicate `init.mt6755.usb.rc` import, and `/sbin/sh` service path. Removing those blockers and forcing early legacy `android_usb` ADB should let the same kernel expose ADB or the next framework/display frontier.

Evidence: Capture `1779453120821` pstore lines show `mount_all /fstab.mt6735`, duplicate `init.mt6755.usb.rc`, failed duplicate FunctionFS mount, missing `/system/etc/init` before system mount, `Service logd not found`, `Service servicemanager not found`, `Service hwservicemanager not found`, `Service vndservicemanager not found`, and `cannot find '/sbin/sh', disabling 'meizu-detect'`. Kernel-side USB shows `android_usb gadget: android_usb ready`.

Files changed: no kernel source changed for this patch cycle; generated ramdisk `/srv/forge/android/export/meizu_m6_artifacts/20260522-source-readonly-ddp-diag-los15-initfix-v2/ramdisk-los15-initfix-v2.img`; generated boot image `/srv/forge/android/export/meizu_m6_artifacts/20260522-source-readonly-ddp-diag-los15-initfix-v2/boot-source-readonly-ddp-diag-los15-initfix-v2.img`; Build Station build `f2cb41b1-aa8f-4247-b947-e9cb6da87d66`, boot artifact `f647f136-ec07-41e0-bffc-2c75fd4f4f3f`.

Why each file changed: `init.rc` no longer carries the wrong m2note `/fstab.mt6735` manual mount block; `init.project.rc` no longer imports `init.mt6755.usb.rc` a second time; `init.meizu_mt675x.rc` starts `meizu-detect` with `/system/bin/sh`; `init.mt6755.usb.rc` adds early non-configfs `/sys/class/android_usb/android0` ADB setup and starts `adbd`; the boot image preserves the source kernel payload sha256 `ec204ae9c47312a31f2910e00db71e65dfd3e0d48ace0367ad1803bafc7fb0c9`.

Expected next marker: next capture must either hash/trim the flashed boot to sha256 `34a365473cef73f08246f97b195e6815d436f3712b1be0be4e24eb5ea1bfc89f`, or at least pstore must no longer contain `mount_all /fstab.mt6735`, duplicate `Parsing file init.mt6755.usb.rc`, or `cannot find '/sbin/sh', disabling 'meizu-detect'`. Desired ADB markers are `starting service 'adbd'`, `init.svc.adbd=running`, and `sys.usb.state=adb`.

Rollback condition: If a verified capture with boot sha256 `34a365473cef73f08246f97b195e6815d436f3712b1be0be4e24eb5ea1bfc89f` still shows the same broken init markers, reject this artifact as not actually flashed or inspect ramdisk packing. If those markers are gone but ADB is still absent, move to USB gadget/adbd SELinux/property trigger evidence instead of editing display.

Artifacts: boot image `/srv/forge/android/export/meizu_m6_artifacts/20260522-source-readonly-ddp-diag-los15-initfix-v2/boot-source-readonly-ddp-diag-los15-initfix-v2.img` sha256 `34a365473cef73f08246f97b195e6815d436f3712b1be0be4e24eb5ea1bfc89f`, size `9447424`; ramdisk sha256 `51292bd1c0382528ed463e3f1771e73e687e0085982370b146ad73f9a4fbfea5`, size `1896976`; kernel payload sha256 `ec204ae9c47312a31f2910e00db71e65dfd3e0d48ace0367ad1803bafc7fb0c9`; boot header magic `ANDROID!`, page `2048`, kernel addr `0x40080000`, ramdisk addr `0x45000000`, tags addr `0x44000000`, OS version/patch encoded from `8.1.0 / 2021-10-05`.

Verification commands:

```bash
sha256sum /srv/forge/android/export/meizu_m6_artifacts/20260522-source-readonly-ddp-diag-los15-initfix-v2/boot-source-readonly-ddp-diag-los15-initfix-v2.img
adb devices
grep -R -n -E 'mount_all /fstab.mt6735|Parsing file init.mt6755.usb.rc|ffs_data_put|cannot find .*/sbin/sh|starting service .adbd.|sys.usb.state|android_usb gadget' <next-capture>/evidence
```

## 2026-05-22 LOS15 source-kernel binder device boot image

FACT: Live mini capture `/srv/forge/android/export/meizu_m6_artifacts/20260522-source-readonly-ddp-diag-los15-initfix-v2/m6nOREO.tar.gz` has sha256 `8e70c82679b25f8cd9dcd2d38be5b9a17ed4286833483058dba51f88b066ae65`. Runtime reached Android 8.1 ADB with `init.svc.adbd=running`, but `surfaceflinger=stopped`, `zygote=stopped`, `zygote_secondary=stopped`, and `hwservicemanager` was unstable.

FACT: The same capture's logcat repeatedly contains `Opening '/dev/hwbinder' failed: No such file or directory`, `Opening '/dev/vndbinder' failed: No such file or directory`, and `hwservicemanager: Failed to aquire binder FD. Aborting...`. It also contains the later display symptom `SurfaceFlinger: ERROR: failed to open framebuffer (Invalid argument), aborting`.

FACT: The LOS15 initfix-v2 ramdisk already labels and permissions `/dev/binder`, `/dev/hwbinder`, and `/dev/vndbinder` in `ueventd.rc`; the running source kernel only registered the legacy `/dev/binder` misc device.

INFERENCE: The earliest proven framework blocker is missing kernel binder device nodes for Oreo HIDL/vendor binder. Fixing `/dev/hwbinder` and `/dev/vndbinder` should stabilize `hwservicemanager` and HAL registration before the display framebuffer error becomes the next frontier.

PATCH HISTORY, BOOT-UNBLOCK, 2026-05-22: add per-device binder contexts for `/dev/binder`, `/dev/hwbinder`, and `/dev/vndbinder`, rebuild source kernel, and repack with the known ADB-working LOS15 initfix-v2 ramdisk.

Hypothesis: Android 8.1 userspace is stalled before stable framework/service startup because the 3.18.140 MTK binder driver exposes only `/dev/binder`. Registering `hwbinder` and `vndbinder` as additional binder misc devices with separate context-manager state should let `hwservicemanager`, `vndservicemanager`, and HIDL clients acquire binder FDs instead of aborting.

Evidence: capture/log facts above; ramdisk `/srv/forge/android/export/meizu_m6_artifacts/20260522-source-readonly-ddp-diag-los15-initfix-v2/ramdisk-los15-initfix-v2.img` sha256 `51292bd1c0382528ed463e3f1771e73e687e0085982370b146ad73f9a4fbfea5` has `/dev/hwbinder` and `/dev/vndbinder` ueventd entries; source kernel build log `/srv/forge/android/export/meizu_m6_artifacts/20260522-los15-source-kernel-binder-devices-v1/source-kernel-binder-devices-v1-build.log` completes with `CAT arch/arm64/boot/Image.gz-dtb`.

Files changed: `kernel-3.18/drivers/staging/android/binder.c` adds `struct binder_device`, per-device `struct binder_context`, per-open context selection, context-manager state per binder device, and registration of `binder`, `hwbinder`, and `vndbinder`. `BRINGUP_STATE.md` records this patch cycle. Existing dirty display/DDP files are not part of this binder fix.

Why each file changed: `binder.c` owns the missing kernel device nodes and must keep separate context managers so `/dev/binder`, `/dev/hwbinder`, and `/dev/vndbinder` do not fight for servicemanager state. The state file is required so the next capture can verify artifact identity before reading logs.

Expected next marker: next capture must hash/trim flashed boot to sha256 `aec3c4dfe13766964c021d3cb7a6f97333c02585bfa57c3916707ee7f8875e7d`. If the hypothesis is correct, `/dev/hwbinder` and `/dev/vndbinder` exist, `Opening '/dev/hwbinder' failed` disappears, `hwservicemanager` no longer loops on missing binder FD, and the next remaining blocker is likely the existing `SurfaceFlinger: ERROR: failed to open framebuffer (Invalid argument)` display path.

Rollback condition: If a verified capture with boot sha256 `aec3c4dfe13766964c021d3cb7a6f97333c02585bfa57c3916707ee7f8875e7d` still has no `/dev/hwbinder` or `/dev/vndbinder`, inspect binder init/registration and ramdisk uevent timing before touching display. If binder devices exist but `hwservicemanager` fails differently, keep the binder patch and move to the new first error. If boot regresses before ADB, revert only this binder patch and compare `binder.o` plus initcall logs.

Artifacts: boot image `/srv/forge/android/export/meizu_m6_artifacts/20260522-los15-source-kernel-binder-devices-v1/boot-los15-source-kernel-binder-devices-v1.img` sha256 `aec3c4dfe13766964c021d3cb7a6f97333c02585bfa57c3916707ee7f8875e7d`, size `9449472`; kernel payload `/srv/forge/android/export/meizu_m6_artifacts/20260522-los15-source-kernel-binder-devices-v1/Image-binder-devices-v1.gz-dtb` sha256 `cefa9edcad1864163297093941fbb65651191ff91a175627041893e0310f2112`; System.map sha256 `73665c343bd841234db4266a71898809c0a647b5d57271e69991e005e1b66f92`; config sha256 `bc272726035c1a2eca9422e9bc230cf54f8295648a8865a98faba046ab01619e`. Build Station build `05e1de44-4a62-4430-aaf3-b1148d93450a`, boot artifact `19301203-dc01-43c9-9dff-4628b5dc3839`, log artifact `4934ac18-ae34-4365-aef1-37eed05fbfb5`.

Verification commands:

```bash
sha256sum -c /srv/forge/android/export/meizu_m6_artifacts/20260522-los15-source-kernel-binder-devices-v1/SHA256SUMS
abootimg -i /srv/forge/android/export/meizu_m6_artifacts/20260522-los15-source-kernel-binder-devices-v1/boot-los15-source-kernel-binder-devices-v1.img
strings /home/n8n/forge-work/rom-b969cd04-32e2-4375-aaa9-c9d3b8655524/source-kernel/drivers/staging/android/binder.o | grep -E '^binder$|^hwbinder$|^vndbinder$|failed to register binder device'
# Next capture identity:
python3 - <<'PY2'
from pathlib import Path
import hashlib
cap = Path('<next-capture>/evidence/adb/mtk/partitions/boot-partition-16m.img').read_bytes()
expected = Path('/srv/forge/android/export/meizu_m6_artifacts/20260522-los15-source-kernel-binder-devices-v1/boot-los15-source-kernel-binder-devices-v1.img').read_bytes()
print(hashlib.sha256(cap[:len(expected)]).hexdigest())
print(cap[:len(expected)] == expected)
PY2
grep -R -n -E '/dev/(hw|vnd)?binder|Opening .*/dev/hwbinder|Opening .*/dev/vndbinder|hwservicemanager: Failed to aquire binder FD|init.svc.hwservicemanager|SurfaceFlinger: ERROR: failed to open framebuffer' <next-capture>/evidence
```

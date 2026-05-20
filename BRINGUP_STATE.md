# Meizu M6 Source Kernel Bring-up State

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


## 2026-05-20 source stock-LCM/panel-bias bundle

FACT: Fresh stock-prebuilt reference capture is `/home/n8n/forge-work/debug/0b13c8c6-d194-431f-a397-f852e3aae7d9/cb0db6c7-b0c0-46bf-a331-4aaec9c83128/browser-bootdiag-1779314012165.tar`, sha256 `49b1bf699361b6b83f03945e0bd2c201ae9d5f619726ee5f898284a12f777bb7`. It runs stock kernel `Linux version 3.18.35+ (flyme@Mz-Builder-l10) #1 SMP PREEMPT Mon Apr 8 09:46:18 CST 2019` with the current LOS 14.1 userspace.

FACT: In that fresh stock-prebuilt capture, SurfaceFlinger reports `Built-in Screen` at `720x1280`, `powerMode=2`, `VSYNC state: enabled`, and `flips=3648`; display IRQs are active (`ovl0`, `rdma0`, `dsi0`, plus `ovl0_2l`/`ovl1_2l`).

FACT: Stock kernel payload `/srv/forge/android/export/meizu_m6_artifacts/prebuilt-stockdiag-54a94dfb-20260520/kernel-stock-7.1.2.0G` contains hardware strings `nt35695_fhd_dsi_cmd_truly_nt50358_drv`, `atag,videolfb-lcmname`, `lcd_bias_enp0_gpio`, `lcd_bias_enp1_gpio`, `lcd_bias_enn0_gpio`, and `lcd_bias_enn1_gpio`. The previous source build selected `CONFIG_CUSTOM_KERNEL_LCM="ili9881p_hd_dsi_txd"` and had only ENP GPIO states wired through DTS.

INFERENCE: The source-kernel display failure cannot be fixed by HWC/userspace alone while the source kernel binds the wrong LCM identity and does not expose the same panel-bias GPIO state set as the stock kernel. This is a display component-initialization bundle tied to the same OVL/GED/timeline frontier as the DDP clock bundle, not a separate audio/modem/storage change.

REJECTED: `ro.sf.hwrotation=180` in the current LOS-prebuilt capture is not hardware truth for this kernel bundle. The stock firmware fact remains `ro.sf.hwrotation=0`; rotation stays out of the source-kernel fix until runtime A/B proves it.

PATCH HISTORY, PROPER-FIX, 2026-05-20: align source kernel with stock M6 LCM identity and panel bias/reset GPIO states.

Hypothesis: the source kernel stalls the display path because it initializes the DDP path with an M2/M5-style `ili9881p_hd_dsi_txd` panel identity and incomplete bias GPIO wiring, while the working stock M6 kernel advertises `nt35695_fhd_dsi_cmd_truly_nt50358_drv` and separate ENP/ENN pinctrl states. Binding the stock LCM name, compiling the matching 720p NT35695 driver, and routing reset/bias through DTS pinctrl should let the existing DDP clock fix drive the real panel instead of waiting forever on OVL/RDMA/DSI fences.

Evidence: fresh stock-prebuilt capture above has working SurfaceFlinger/VSYNC/flips and active display IRQs. Stock kernel strings contain `nt35695_fhd_dsi_cmd_truly_nt50358_drv` plus `lcd_bias_enp0/1` and `lcd_bias_enn0/1`. Source bad captures selected `ili9881p_hd_dsi_txd` and showed repeated GED/timeline/OVL fence stalls. Build verification produced `/srv/forge/android/export/meizu_m6_artifacts/source-kernel-manual-20260520/boot-source-stock-lcm.img` sha256 `8c9a18523f99b266a44f301111b7297f2737d803bb39af2e9812a44cd508fdb5`, kernel payload sha256 `7845e8d552bbdcf0060c867c6db5f1c8c528bf3dc7a508fa8d6ebb93e056aa5b`, `System.map` sha256 `27d1579144c6edca0e6c6973c7c2b39e41605ecfcd1128dc48bb6980542f6aa6`, and `kernel.config` sha256 `ffc2f683116d20eb9c0e7d9d98011da2810a223ac353b91fd21158215f2126cc`.

Files changed: `arch/arm64/boot/dts/meizu_m6.dts` changes the LCM atag name to the stock M6 name and adds ENP/ENN pinctrl states; `arch/arm64/configs/meizu_m6_defconfig` and `meizu_m6_debug_defconfig` select `nt35695_fhd_dsi_cmd_truly_nt50358_720p`; `drivers/misc/mediatek/lcm/mt65xx_lcm_list.c` stops unconditionally referencing the old ILI9881P driver when it is not compiled; `drivers/misc/mediatek/lcm/nt35695_fhd_dsi_cmd_truly_nt50358_720p/...c` exports the stock runtime name; `drivers/misc/mediatek/video/mt6755/ddp_dsi.c` routes ENP, ENN, and reset through DTS pinctrl; `disp_dts_gpio.c` and `disp_dts_gpio.h` add explicit ENP0/ENP1/ENN0/ENN1 states.

Why each file changed: the DTS atag and LCM driver name must match the stock kernel hardware identity; the defconfigs and LCM list must compile and register exactly one intended panel path; the DSI utility hooks are where LCM drivers request reset and bias changes; the DTS GPIO enum/name table must match the new pinctrl names or `disp_dts_gpio_select_state()` cannot select them.

Expected next marker: verified capture from `boot-source-stock-lcm.img` shows source kernel `3.18.140`, `nt35695_fhd_dsi_cmd_truly_nt50358_drv` selected or at least no `ili9881p lcm_get_params` active path, `M6 DDP clk:` lines present, active `mtk_cmdq`/`ovl0`/`rdma0`/`dsi0` interrupts, SurfaceFlinger `Built-in Screen` ON with increasing flips, and no repeated first-frontier `Frame didn't finished` / `[OVL-IN-0] fence didn't signal` / `OverlayEngine_0` EOF token-0 stall. If display advances, diagnose the next earliest blocker separately.

Rollback condition: revert this bundle if the verified next capture regresses before SurfaceFlinger/DisplayManager built-in screen ON, shows panel/bias GPIO selection failures before DDP start, or proves the stock M6 kernel actually binds a different panel path than `nt35695_fhd_dsi_cmd_truly_nt50358_drv`.

Verification commands:

```bash
sha256sum /srv/forge/android/export/meizu_m6_artifacts/source-kernel-manual-20260520/boot-source-stock-lcm.img /srv/forge/android/export/meizu_m6_artifacts/source-kernel-manual-20260520/Image-stock-lcm.gz-dtb /srv/forge/android/export/meizu_m6_artifacts/source-kernel-manual-20260520/System.map.stock-lcm /srv/forge/android/export/meizu_m6_artifacts/source-kernel-manual-20260520/kernel-stock-lcm.config
gzip -cd /srv/forge/android/export/meizu_m6_artifacts/source-kernel-manual-20260520/Image-stock-lcm.gz-dtb 2>/dev/null | strings | grep -E 'nt35695_fhd_dsi_cmd_truly_nt50358_drv|ili9881p_hd_dsi_txd|M6 DDP clk|lcd_bias_enn|lcd_bias_enp'
grep -n -E 'nt35695|ili9881|M6 DDP clk|MMSYS_CG_CON0|DISP_DL_VALID_0|DISP_DL_READY_0|CMDQ_EVENT_DISP_(RDMA0|WDMA0)_EOF|Frame didn.t finished|OVL-IN-0|OverlayEngine_0|Built-in Screen|SetPowerMode|vsync' <next capture log files>
cat <next capture>/mtp/adb/proc_interrupts.txt <next capture>/mtp/adb-files/cache/bootdiag/*/proc/interrupts.txt 2>/dev/null | grep -E 'mtk_cmdq|ovl0|rdma0|dsi0'
```

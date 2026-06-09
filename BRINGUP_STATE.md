# Meizu M6 Source Kernel Bring-up State

## 2026-06-09 #80 DSI C2V debugfs parser fix

PATCH HISTORY, **DIAGNOSTIC**, 2026-06-09: fix the `m6_dsi_c2v_switch`
debugfs command prefix length so the #79 low-level DSI C2V switch probe can
actually execute. The previous parser compared `m6_dsi_c2v_switch:` with length
`19`, but the literal including the colon is 18 bytes, so any value-bearing
command such as `m6_dsi_c2v_switch:0x03:1500` failed the prefix check before
`sscanf()` and before `primary_display_m6_dsi_c2v_switch()` could run. This
patch does not change DSI timing, panel commands, route, clocks, MIPITX state,
PQ/HWC/OVL/RDMA behavior, or boot-time display sequencing.

Hypothesis: FACT: #79/r2 boot image
`01ec11277d92fa319f36a4b955d51476c5c1cf8fed22758eb580ae223233c4c2` was
verified running on `/dev/block/platform/mtk-msdc.0/by-name/boot` in capture
`/srv/forge/android/meizu_m6/captures/20260609-0928-m6-dsi-c2v-switch-probe-dsi0-r2-root-postflash`.
FACT: the same capture ran as `uid=0(root)` and the follow-up
`m6_display_truth_window:after-c2v-switch` marker appeared, proving debugfs was
writable. FACT: that dmesg contains no `M6 DSI c2v_switch`,
`switch-lcm-enter`, or `switch-dsi-enter` markers, so the #79 C2V path did not
execute. FACT: source inspection shows the `strncmp()` length was one byte too
long. HYPOTHESIS: correcting the prefix length is sufficient to make the
existing #79 diagnostic path execute; the next capture can then answer whether
`DDP_SWITCH_DSI_MODE` toggles C2V/VM command state while `line` remains zero.

Evidence:
- Failed #79 runtime capture:
  `/srv/forge/android/meizu_m6/captures/20260609-0928-m6-dsi-c2v-switch-probe-dsi0-r2-root-postflash`.
- `identity-before-c2v-root.txt`: root ADB, `sys.boot_completed=1`, boot sha256
  matches #79/r2 artifact.
- `run-c2v-command.txt`: no shell permission error, unlike the earlier failed
  shell-user capture.
- `dmesg-after-c2v.txt`: only the later truth-window marker appears; #79 C2V
  entry/exit markers are absent.
- `disp_debug.c`: `m6_dsi_c2v_switch:` literal length is 18, while the parser
  used `strncmp(..., 19)`.

Files changed:
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/disp_debug.c`: fixes the
  `m6_dsi_c2v_switch:` prefix length from 19 to 18.
- `BRINGUP_STATE.md`: records why the #79 runtime result was invalid and what
  the corrected rerun must prove.

Why each file changed: `disp_debug.c` owns the runtime debugfs command parser;
without this one-byte parser fix the already-built DSI switch instrumentation is
unreachable. This state file keeps later agents from mistaking the #79/r2
capture for a negative C2V hardware result.

Expected next marker: after flashing this parser fix, rerun
`m6_dsi_c2v_switch:0x03:1500`. Dmesg must show `M6 DSI c2v_switch`,
`c2v-switch-before-stop`, `switch-lcm-enter`, `switch-dsi-enter`,
`switch-dsi-exit`, `c2v-switch-after-dsi`, and
`c2v-switch-restart-after-write`. Only then interpret `line`, `word`,
VM command, MIPITX, and physical LCD state.

Rollback condition: revert only if the corrected parser dispatch destabilizes
debugfs writes or the C2V command path hangs before producing the entry marker.
If the command executes and the panel remains black, keep the parser fix and
move the hardware diagnosis to the next proven low layer.

Verification commands:

```bash
A='adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5'
$A root
$A wait-for-device
$A shell 'dmesg -C'
$A shell 'echo m6_dsi_c2v_switch:0x03:1500 > /d/mtkfb; sleep 1; echo m6_display_truth_window:after-c2v-switch > /d/mtkfb'
$A shell 'dmesg | grep -E "M6 DSI c2v_switch|switch-lcm|switch-dsi|c2v-switch|M6 DSI state_decode|word=|line=|MIPITX|backlight" | tail -420'
```

## 2026-06-09 #79 DSI C2V switch-bit probe

PATCH HISTORY, **DIAGNOSTIC**, 2026-06-09: add a bounded M6-only debugfs probe
for the low-level DDP/DSI command-to-video path:
`m6_dsi_c2v_switch:<value>[:hold_ms]`. Unlike #78, this does not only issue a
plain LCM DCS write. It builds the same `LCM_DSI_MODE_SWITCH_CMD` shape used by
the MTK switch path, forces `cmd_if=LCM_INTERFACE_DSI0` because this M6 LCM does
not populate `params->lcm_cmd_if`, stops video, calls `DDP_SWITCH_LCM_MODE`,
then calls `DDP_SWITCH_DSI_MODE` so `ddp_dsi_switch_mode()` can set
`DSI_MODE_CTRL.C2V_SWITCH_ON`, program the VM command packet, assert
`DSI_START=2`, flush CMDQ, restart the trigger loop/path, and dump live DSI,
MIPITX, and backlight truth around each boundary. It does not change boot-time
LCM timing, porch values, PLL, lane count, PQ/HWC/OVL/RDMA routing, or fake any
ready/fence state.

Hypothesis: FACT: #78 runtime capture
`/srv/forge/android/meizu_m6/captures/20260609-0643-m6-lcm-mode-ctrl-bb03-probe-afterboot`
booted #78, reached `sys.boot_completed=1`, and ran
`m6_lcm_mode_ctrl:0x03:1500`. FACT: #78 showed DSI host in video data period
with MIPITX lanes/PLL stable and moving word counts, but `line=0` persisted
before and after the plain `0xBB=0x03` write. FACT: #78 DCS readback for `0xBB`
stayed `00` before/after/hold, so a simple read/write/read DCS probe did not
prove that the panel entered HS-video acceptance. FACT: source inspection shows
the generic C2V path also toggles `DSI_MODE_CTRL.C2V_SWITCH_ON`, writes the VM
packet at `DSI_REG + 0x200`, writes `DSI_START=2`, and flushes CMDQ in
`ddp_dsi_switch_mode()`. HYPOTHESIS: the missing low-level C2V switch bit/VM
packet path may be the earliest remaining layer between the already-working
RDMA route and the physical-lit-black panel; if it runs and `line` still stays
zero, this branch closes and the next target moves below DSI host switching into
MIPITX lane/drive/settle/polarity or stock LK hidden PHY side effects.

Evidence:
- #79 final artifact:
  `/srv/forge/android/export/meizu_m6_artifacts/20260609-0848-m6-dsi-c2v-switch-probe-dsi0-r2-bootonly`.
- #79 boot image sha256:
  `01ec11277d92fa319f36a4b955d51476c5c1cf8fed22758eb580ae223233c4c2`.
- #79 `Image.gz-dtb` sha256:
  `043c74c74974a59d41f908425b7eab84a3d49e03b9202b4b25bf32342a58db8f`.
- #79 `System.map` sha256:
  `400d1378884a456ec429f3e3412f03116a9afed6756875d1be3e3001a871c68b`.
- #79 `kernel.config` sha256:
  `698b6764b989ef0bab75c0e6d6c291706e6a4a8d527d6a59347ad8c966d1fdd1`.
- #79 artifact verification: `sha256sum -c SHA256SUMS` passed,
  `abootimg -x` unpacked successfully, `cmp Image.gz-dtb verify-unpack/zImage`
  passed, `cmp initrd.img verify-unpack/initrd.img` passed, and gzip-expanded
  marker strings include `m6_dsi_c2v_switch`, `M6 DSI c2v_switch`,
  `switch-lcm-enter`, `switch-dsi-enter`, and `c2v-switch-*`.
- #78 capture facts:
  `/srv/forge/android/meizu_m6/captures/20260609-0643-m6-lcm-mode-ctrl-bb03-probe-afterboot`.
  The simple `0xBB=0x03` probe produced `mode-ctrl-restart-after-write` and
  `after-bb03` snapshots with nonzero `word` but `line=0`, backlight still 255,
  and a nonblack screencap unchanged from before the probe.

Files changed:
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_dsi.c`: logs entry/exit
  of the existing `DDP_SWITCH_LCM_MODE` and `DDP_SWITCH_DSI_MODE` paths plus
  live DSI snapshots around the low-level switch.
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/primary_display.c`: adds
  `primary_display_m6_dsi_c2v_switch()` to stop video, run the MTK C2V switch
  path with `cmd_if=LCM_INTERFACE_DSI0`, rebuild/restart the trigger path, and
  dump post-switch truth.
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/primary_display.h`: exposes
  the M6 debug wrapper to debugfs.
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/disp_debug.c`: adds the
  `m6_dsi_c2v_switch:<value>[:hold_ms]` debugfs command and help text.
- `BRINGUP_STATE.md`: records the #78 result, #79 purpose, evidence, expected
  markers, rollback condition, and verification commands.

Why each file changed: DSI owns the actual hardware switch bit/VM packet path;
primary display owns safe stop/restart and trigger-loop recovery; debugfs is the
existing runtime injection surface; this state file preserves why the work moved
below plain DCS writes after #78.

Expected next marker: after flashing #79, run
`m6_dsi_c2v_switch:0x03:1500`. Dmesg should show
`M6 DSI c2v_switch`, `switch-lcm-enter`, `switch-dsi-enter`,
`c2v-switch-after-dsi`, `c2v-switch-restart-after-write`, and
`c2v-switch-hold-end`. If `switch-dsi-enter/exit` runs, `cmd_if=1`, C2V video
state/VM packet markers appear, but DSI `line` remains zero and the physical LCD
stays black, close the C2V-switch branch and target MIPITX/panel electrical or
stock LK PHY side effects next. If `switch_dsi_mode ret` fails or `cmd_if` is
not DSI0, fix that path before moving lower.

Rollback condition: revert this checkpoint if the debugfs command destabilizes
ADB/SurfaceFlinger/RDMA/backlight, fails before `switch-dsi-enter`, leaves the
display path unable to restart, or produces new CMDQ/DSI timeouts that prevent
comparison with #78/#77.

Verification commands:

```bash
A='adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5'
$A root
$A wait-for-device
$A shell 'svc power stayon true; settings put system screen_off_timeout 2147483647; input keyevent 224; settings put system screen_brightness 255; echo 255 > /sys/class/leds/lcd-backlight/brightness'
$A shell 'echo m6_dsi_c2v_switch:0x03:1500 > /d/mtkfb; sleep 1; echo m6_display_truth_window:after-c2v-switch > /d/mtkfb'
$A shell 'dmesg | grep -E "M6 DSI c2v_switch|switch-lcm|switch-dsi|c2v-switch|M6 DSI state_decode|word=|line=|MIPITX|backlight" | tail -320'
$A shell 'cat /d/mtkfb | head -180'
```

## 2026-06-09 #78 ILI9881P mode-control C2V probe

PATCH HISTORY, **ISOLATION / DIAGNOSTIC**, 2026-06-09: add a bounded M6-only
debugfs probe for the ILI9881P `0xBB` mode-control register. The probe stops
the DSI video path, selects page 0, reads `0xBB`, writes a requested value
(`0x03` is the C2V/video value already encoded by this LCM driver's
`lcm_switch_mode()`), reads it back before and after a bounded hold, restarts
the video path, and dumps DSI/MIPITX/backlight truth around each boundary. It
does not enable global dynamic mode switching, does not change boot-time DSI
timing, does not touch PQ/HWC/OVL/RDMA routing, and does not fake readiness.

Hypothesis: FACT: #77 verified boot image
`5eafeb82fbaae920688db1ea4e8508775dc411915c1499c55496fae1255d3684` reaches
`sys.boot_completed=1`, backlight 255, nonblack screencap, SurfaceFlinger ON,
and RDMA0 transfer near 61 fps while the physical LCD stays lit black. FACT:
#77 retained DSI markers show MIPITX lane/PLL state stable and DSI host started,
but delayed video samples still show `line=0`. FACT: the current LCM driver
already defines `0xBB=0x03` as the command-to-video value in `lcm_switch_mode()`,
but normal bring-up has `switch_mode_enable=0`, so that C2V command is not sent
through the generic switch path. HYPOTHESIS: the panel may be left in an
ILI9881P command/GRAM-side state after LK/Linux handoff or after Linux DCS
windows; a controlled `0xBB=0x03` write before restarting video should prove
whether panel HS-video acceptance changes before moving lower into PHY/lane
electrical state.

Evidence:
- #78 artifact:
  `/srv/forge/android/export/meizu_m6_artifacts/20260609-0638-m6-lcm-mode-ctrl-c2v-probe-bootonly`.
- #78 boot image sha256:
  `693e0b0029741c2f83b1a8d50f1f3ebb2f3a78a34a528f9f59b05258876d0263`.
- #78 `Image.gz-dtb` sha256:
  `3dec3305277181edbbc69f3fbf15421a050b65b2b66e1edeec251264ce096cdd`.
- #78 `System.map` sha256:
  `d743d414fff1abd070a02207091be5ac3450983f72b7eee28b74083a9fbe936e`.
- #78 `kernel.config` sha256:
  `698b6764b989ef0bab75c0e6d6c291706e6a4a8d527d6a59347ad8c966d1fdd1`.
- #78 artifact verification: `sha256sum -c SHA256SUMS` passed,
  `abootimg -x` unpacked successfully, `cmp Image.gz-dtb verify-unpack/zImage`
  passed, `cmp initrd.img verify-unpack/initrd.img` passed, and gzip-expanded
  marker strings include `m6_lcm_mode_ctrl`, `M6 LCM mode_ctrl`,
  `mode_ctrl_probe`, `mode-ctrl-*`, `M6V`, and `M6W`.
- #77 postflash capture:
  `/srv/forge/android/meizu_m6/captures/20260609-0600-m6-dsi-retained-video-window-r2-postflash`.
- #77 second-reboot capture:
  `/srv/forge/android/meizu_m6/captures/20260609-0600-m6-dsi-retained-video-window-r2-after-second-reboot`.
- #77 artifact:
  `/srv/forge/android/export/meizu_m6_artifacts/20260609-0542-m6-dsi-retained-video-window-r2-bootonly`.
- #77 runtime facts: `sys.boot_completed=1`, `mActualBacklight=255`, 720x1280
  screencap nonblack, SurfaceFlinger built-in display ON with flips, and
  `debugfs-mtkfb.txt` reports `PathMode:DIRECT_LINK` with RDMA0 transferring.
- #77 retained DSI facts: `M6V/M6W` keep MIPITX lane/top/PLL state stable,
  `HSA=0x38` after CMDQ flush/start, `after-1vsync` and `after-500ms` decode
  still report `word=0 line=0`, and the first later timeout shows nonzero word
  state with `line=0`.

Files changed:
- `kernel-3.18/drivers/misc/mediatek/lcm/ili9881p_hd_dsi_txd/ili9881p_hd_dsi_txd.c`:
  adds `lcm_m6_diag_mode_ctrl_probe()` to select page 0 and read/write/read
  DCS `0xBB`.
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/primary_display.c`: adds
  `primary_display_m6_lcm_mode_ctrl()` wrapper using the existing safe
  stop-video / LP-DCS / restart-video pattern plus DSI/MIPITX/backlight dumps.
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/primary_display.h`: exposes
  the M6 debug wrapper to debugfs.
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/disp_debug.c`: adds the
  `m6_lcm_mode_ctrl:<value>[:hold_ms]` debugfs command.
- `BRINGUP_STATE.md`: records the probe purpose, evidence, expected markers,
  rollback condition, and verification commands.

Why each file changed: the LCM driver owns panel-private DCS writes/reads; the
primary-display wrapper owns safe DSI video stop/restart sequencing; debugfs is
the existing runtime injection surface; this state file preserves the exact
question so later agents do not re-open PQ/HWC/OVL/RDMA after #77.

Expected next marker: after flashing #78, run `m6_lcm_mode_ctrl:0x03:1500`
while the panel is lit black. Dmesg should show `M6 LCM mode_ctrl_probe` before
and after write readbacks, `mode-ctrl-before-stop`, `mode-ctrl-stop-video-write`,
`mode-ctrl-after-probe`, and `mode-ctrl-restart-after-write` DSI snapshots. If
`0xBB=0x03` latches but DSI `line` remains zero and the physical LCD remains
black, close this branch and move below panel mode-control into MIPITX lane
polarity/swap/drive/settle or stock LK hidden PHY side effects. If `0xBB`
does not latch or video line state changes, keep the branch open and make the
next patch target that exact earliest failure.

Rollback condition: revert this checkpoint if the debugfs command destabilizes
ADB/SurfaceFlinger/RDMA/backlight, fails before logging the read/write/read
sequence, or if stop/restart behavior introduces new DSI/CMDQ timeouts that
prevent comparison with #77.

Verification commands:

```bash
A='adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5'
$A root
$A wait-for-device
$A shell 'svc power stayon true; settings put system screen_off_timeout 2147483647; input keyevent 224; settings put system screen_brightness 255; echo 255 > /sys/class/leds/lcd-backlight/brightness'
$A shell 'echo m6_lcm_mode_ctrl:0x03:1500 > /d/mtkfb; sleep 1; echo m6_display_truth_window:after-bb03 > /d/mtkfb'
$A shell 'dmesg | grep -E "M6 LCM mode_ctrl|mode_ctrl_probe|mode-ctrl-|M6V|M6W|M6 DSI state_decode|word=|line=|MIPITX|backlight" | tail -260'
$A shell 'cat /d/mtkfb | head -180'
```

## 2026-06-09 #77 retained DSI video/PHY window

PATCH HISTORY, **DIAGNOSTIC**, 2026-06-09: add bounded retained `M6V/M6W`
DSI host and MIPITX snapshots around the first VM command setup, DSI takeover,
`DSI_Start()`, and the delayed 1-vsync / 500ms video-state probes. Reduce the
previous retained `M6D/M6R/M6G/M6X` limits so early SRAM is not overwritten by
late DPMGR/RDMA churn. This patch does not change DSI timing, VM command
payloads, MIPITX/PHY programming, panel init/reset, PQ, OVL/RDMA behavior,
HWC, backlight, charging, ramdisk, or boot cmdline.

Hypothesis: FACT: #75 boot image
`f3cf4406fbe2a9d454d22b14e754327100a4a1618cf9ec620ec703766844a8ea` was
verified flashed/running and reached `sys.boot_completed=1`. FACT: #75 second
reboot retained markers show RDMA0 counters moving through a full frame at the
physical-black frontier (`M6G07` has RDMA OUT line `1280`) while the same
retained window reports DSI `START=1` and `STATE9=0`. FACT: user observation
after #75 reports the panel is still lit black, possibly becoming black
slightly later than before. INFERENCE: route, mutex, and RDMA startup are not
the earliest remaining blocker; the frontier is now DSI video-start / MIPITX
PHY / panel HS-video acceptance. HYPOTHESIS: retained `M6V/M6W` values before
and after VM setup, `DSI_START`, and the delayed video samples will identify
whether DSI never leaves the initial video FSM state, loses VM payload/state,
or has a live host but bad MIPITX lane/PLL state.

Evidence:
- #77 artifact:
  `/srv/forge/android/export/meizu_m6_artifacts/20260609-0542-m6-dsi-retained-video-window-r2-bootonly`.
- #77 boot image sha256:
  `5eafeb82fbaae920688db1ea4e8508775dc411915c1499c55496fae1255d3684`.
- #77 `Image.gz-dtb` sha256:
  `fbf0d1770699ba5530d9f044c7aefed83f9f4735441bc544e5d8a89c054dafc7`.
- #77 `System.map` sha256:
  `132259ad0d26b2b637189ad15e86ed4a682e2947ab74ebdeb1ad4969513a07dc`.
- #77 `kernel.config` sha256:
  `698b6764b989ef0bab75c0e6d6c291706e6a4a8d527d6a59347ad8c966d1fdd1`.
- #77 kernel string:
  `3.18.140 #77 SMP PREEMPT Tue Jun 9 05:40:03 CDT 2026`.
- Artifact verification: `sha256sum -c SHA256SUMS` passed, `abootimg -x`
  unpacked successfully, `cmp Image.gz-dtb verify-unpack/zImage` passed,
  `cmp initrd.img verify-unpack/initrd.img` passed, and gzip-expanded marker
  strings include `M6V`, `M6W`, `M6D`, `M6R`, and `M6G`.
- #75 capture:
  `/srv/forge/android/meizu_m6/captures/20260609-0520-m6-ddp-rdma-sram-trace-after-second-reboot`.
- #75 retained lines: `proc-last_kmsg.txt` shows `M6R01..M6R11`,
  `M6G01..M6G08`, and `M6F fill/trig/const` before the first timeout. The key
  frontier line is `M6G07 cfg-begin ... R=101 716/1278 0/1280 D=1/0`.
- #75 runtime dump: `debugfs-mtkfb.txt` reports `PathMode:DIRECT_LINK`,
  `video mode + CMDQ Enabled`, and `RDMA0 Transfer 3748` at about 62 fps.

Files changed:
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_dsi.c`: adds retained
  `M6V/M6W` snapshots with DSI `START/STA/INTSTA/MODE/TXRX/PS`, H timing, VM
  command/payload, state6-9, and MIPITX lane/top/PLL/power/debug registers.
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_rdma.c`: lowers retained
  `M6R` count after #75 proved the early RDMA path.
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_manager.c`: lowers
  retained `M6G` count after #75 proved the early DPMGR/RDMA path.
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_path.c`: lowers retained
  `M6X` count after #75 proved initial path/mutex programming.
- `BRINGUP_STATE.md`: records the #75 verdict, #77 diagnostic patch, expected
  markers, rollback condition, and verification commands.

Why each file changed: `ddp_dsi.c` owns the DSI host and MIPITX state that
remains ambiguous after #75. `ddp_rdma.c`, `ddp_manager.c`, and `ddp_path.c`
already answered their #75 question and now only need enough early markers to
preserve correlation with the new DSI window. No behavior-changing writes are
introduced.

Expected next marker: the next verified #77 capture should show `M6V/M6W`
around `vm-set-entry`, `vm-set-after`, `config-before-vmcmd-enqueue`,
`config-after-vmcmd-enqueue`, `start-before`, `start-after`, `after-1vsync`,
and `after-500ms`, with `M6F` still present. If RDMA again reaches a full
frame while `M6V` state9 remains zero and MIPITX lane/PLL state is stable,
the next patch should target DSI video-start edge / HS acceptance. If MIPITX
PLL/lane/debug state changes or drops between `start-after` and delayed
samples, debug the PHY/lane handoff first. If VM command/payload differs
before and after CMDQ flush, debug VM command sequencing.

Rollback condition: revert or reduce #77 if ADB, `sys.boot_completed=1`,
charging, pstore/last_kmsg retention, or physical bootlogo timing regresses,
or if retained SRAM still overflows before `M6V/M6W` answer the DSI/PHY
question.

Verification commands:

```bash
A='adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5'
$A root
$A shell 'uname -a; getprop sys.boot_completed; getprop init.svc.bootanim; dumpsys battery | grep -E "status|level"; sha256sum /dev/block/platform/mtk-msdc.0/by-name/boot'
$A shell 'cat /proc/last_kmsg | grep -E "M6V|M6W|M6D|M6R|M6G|M6X|M6F|M6 DDP timeout|HSA/HBP|word=|line=" | head -360'
$A shell 'dmesg | grep -E "M6V|M6W|M6D|M6R|M6G|M6X|M6F|M6 DDP timeout|HSA/HBP|word=|line=" | head -360'
$A shell 'cat /d/mtkfb | head -180'
$A reboot
$A wait-for-device
$A root
$A shell 'cat /proc/last_kmsg | grep -E "M6V|M6W|M6D|M6R|M6G|M6X|M6F|M6 DDP timeout|HSA/HBP|word=|line=" | head -420'
```

## 2026-06-09 #75 retained DDP/RDMA/mutex startup markers

PATCH HISTORY, **DIAGNOSTIC**, 2026-06-09: add bounded retained SRAM markers
for the M6 primary direct-link DDP route, mutex enable path, DPMGR
config/start/trigger/flush sequence, and RDMA0 config/start state. This patch
does not change DSI timing, panel reset/init tables, PQ bypass behavior, OVL
layer config, RDMA mode, DDP route selection, CMDQ waits/fences, backlight,
charging, ramdisk, or boot cmdline.

Hypothesis: FACT: #74 boot image
`c5e890821a5862d71b087e2b3120613628c4e2525d38c8b08fc1866a9f31e029` is
verified flashed/running and reaches `sys.boot_completed=1`. FACT: #74 second
reboot retained early markers prove `M6D02 config-force-first` ran, `HSA`
becomes `0x38` by `M6D14/M6D15`, and `M6F` framebuffer fill/trigger markers
execute at the 3-4s physical-black frontier. FACT: the first retained timeout
still has `rdma_eof=0`, `mutex_eof=1`, route `VALID=0x0 READY=0x4000937a`,
RDMA0 `GLOBAL=0x101 SIZE=720x1280 IN=0/0 OUT=0/0`, and DSI `line=0`, but #74
RDMA-specific strings were present in the image and absent from retained logs
because they only used `DISPERR()`. INFERENCE: #74 falsifies first DSI
HSA/timing skip as the sole root cause, but it does not prove whether RDMA0,
mutex, or trigger order is already correct before the bootlogo disappears.
HYPOTHESIS: a retained `M6R/M6X/M6G` window around the early 2.3-3.9s handoff
will classify the next frontier as DDP route/mutex, RDMA config/start, upstream
OVL input, or downstream DSI/panel HS-video acceptance.

Evidence:
- #75 artifact:
  `/srv/forge/android/export/meizu_m6_artifacts/20260609-0507-m6-ddp-rdma-sram-trace-bootonly`.
- #75 boot image sha256:
  `f3cf4406fbe2a9d454d22b14e754327100a4a1618cf9ec620ec703766844a8ea`.
- #75 `Image.gz-dtb` sha256:
  `0b875b79a33219fd227d633196e4728be439d42693b718c143e690c969e90e5b`.
- #75 `System.map` sha256:
  `faf2d80aa5c2239309b990f1096bb99c74bb1265876e7f88373035bbe91830ed`.
- #75 `kernel.config` sha256:
  `698b6764b989ef0bab75c0e6d6c291706e6a4a8d527d6a59347ad8c966d1fdd1`.
- #75 kernel string:
  `3.18.140 #75 SMP PREEMPT Tue Jun 9 05:05:52 CDT 2026`.
- Artifact verification: `sha256sum -c SHA256SUMS` passed, `abootimg -x`
  unpacked successfully, `cmp Image.gz-dtb verify-unpack/zImage` passed,
  `cmp initrd.img verify-unpack/initrd.img` passed, and gzip-expanded marker
  strings include `M6R`, `M6X`, `M6G`, `M6 RDMA diag`,
  `M6 DDP sram path`, and `M6 DPMGR sram`.
- #74 capture:
  `/srv/forge/android/meizu_m6/captures/20260609-0437-m6-dsi-force-first-rdma-trace-after-second-reboot`.
- #74 retained lines: `proc-last_kmsg.txt` shows `M6D02 config-force-first`,
  `M6D14/M6D15` with `H=38/124`, `M6F fill/trig/const`, and the timeout
  snapshot with RDMA0 `IN=0/0 OUT=0/0`, route `VALID=0`, DSI HSA `0x38`, and
  DSI `line=0`.

Files changed:
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_rdma.c`: mirrors RDMA0
  config/start/config_l state into retained `M6R` SRAM markers and keeps the
  longer `DISPERR()` dump.
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_path.c`: adds `M6X`
  retained markers around primary route connect, mutex set, and mutex enable.
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_manager.c`: adds `M6G`
  retained markers around primary config/start/trigger/flush and RDMA0/DSI0
  trigger boundaries.
- `BRINGUP_STATE.md`: records this diagnostic patch, evidence, expected
  markers, rollback condition, and verification commands.

Why each file changed: `ddp_rdma.c` owns the RDMA0 state that timed out with
zero counters in #74, but its prior markers were not retained. `ddp_path.c`
owns route selector and mutex programming, the exact layer where #74 sampled
`VALID=0`. `ddp_manager.c` owns the high-level config/start/trigger/flush order
that bridges OVL/RDMA/DSI and is closest to the human-observed 3-4s transition.
The patch is read-only instrumentation in all three files.

Expected next marker: the next verified #75 second-reboot capture should show
`M6R`, `M6X`, and `M6G` before or near `M6F` and before the first display
timeout. If route and mutex are correct but RDMA `IN/OUT` stays `0`, inspect
OVL layer/input generation next. If RDMA counters move while DSI `STATE9` line
stays `0`, inspect DSI/MIPITX/panel HS-video acceptance next. If `M6G` shows
trigger/mutex ordering missing before `M6F`, patch that earliest ordering gap.

Rollback condition: revert if #75 regresses ADB, `sys.boot_completed=1`,
charging status, pstore/last_kmsg retention, or if the additional markers
overflow SRAM and hide existing `M6D/M6F` breadcrumbs. Revert or reduce the
markers if #75 proves config/start/mutex/trigger order is already correct and
the remaining frontier is below DSI host or upstream OVL input.

Verification commands:

```bash
A='adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5'
$A root
$A shell 'uname -a; getprop sys.boot_completed; getprop init.svc.bootanim; dumpsys battery | grep -E "status|level"; sha256sum /dev/block/platform/mtk-msdc.0/by-name/boot'
$A shell 'cat /proc/last_kmsg | grep -E "M6R|M6X|M6G|M6D|M6F|M6 RDMA diag|M6 DDP sram path|M6 DPMGR sram|M6 DDP timeout|HSA/HBP|word=|line=" | head -320'
$A shell 'dmesg | grep -E "M6R|M6X|M6G|M6D|M6F|M6 RDMA diag|M6 DDP sram path|M6 DPMGR sram|M6 DDP timeout|HSA/HBP|word=|line=" | head -320'
$A reboot
$A wait-for-device
$A root
$A shell 'cat /proc/last_kmsg | grep -E "M6R|M6X|M6G|M6D|M6F|M6 RDMA diag|M6 DDP sram path|M6 DPMGR sram|M6 DDP timeout|HSA/HBP|word=|line=" | head -360'
```

## 2026-06-09 #74 force first DSI config / RDMA start trace

PATCH HISTORY, **DIAGNOSTIC/ISOLATION**, 2026-06-09: replay the first Linux
DSI PHY/TXRX/timing/VM programming path once when LK left MIPITX enabled and
`PMaster_enable == 0`, and add bounded RDMA0 config/start markers that sample
route, mutex, RDMA counters, and direct-link state. This patch does not seed or
fake RDMA EOF, does not skip waits/fences, does not alter OVL/PQ/HWC content,
does not change panel init tables, and does not reset the panel. The behavior
change is limited to the first DSI host reprogramming pass that #73 skipped.

Hypothesis: FACT: #73 boot image
`e842ab565cb9cfbceea02ced6fc2621ce1771b36d96748a7441a1210bfcfc24f` is
flashed and running as kernel `3.18.140 #73`, reaches
`sys.boot_completed=1`, and keeps charging status `6 Charging`. FACT: the #73
second-reboot capture preserves early handoff breadcrumbs at `2.286s-3.285s`:
`M6D01..M6D10` stay in DSI video mode with `H=0/124`, LCM init is skipped
because `force=0 inited=1`, and the framebuffer white/const markers run at
`3.251s-3.285s`. FACT: the first retained video timeout has
`rdma_eof=0`, `mutex_eof=1`, route `VALID=0x0 READY=0x4000937a`, RDMA0
`GLOBAL=0x101` but `IN=0/0 OUT=0/0`, DSI `STATE7=Video data period`, DSI
`word=546 line=0`, and live `DSI_HSA_WC=0x0` while the source formula for
HSA=20 RGB888 expects `0x38`. INFERENCE: `ddp_dsi_config()` can skip
`DSI_PHY_clk_setting()`, `DSI_Config_VDO_Timing()`, and `DSI_Set_VM_CMD()`
when `MIPITX_IsEnabled()` returns true during the first Linux takeover, so
the kernel may inherit an LK video state without committing Linux's host timing
or VM command setup before the physical bootlogo disappears. HYPOTHESIS:
forcing exactly one Linux DSI reprogramming pass at that boundary will either
make `HSA=0x38`/line counters/physical output move, or will falsify the DSI
timing-skip branch and leave RDMA direct-link counters/route markers as the
next proven frontier.

Evidence:
- #74 artifact:
  `/srv/forge/android/export/meizu_m6_artifacts/20260609-0427-m6-dsi-force-first-rdma-trace-bootonly`.
- #74 boot image sha256:
  `c5e890821a5862d71b087e2b3120613628c4e2525d38c8b08fc1866a9f31e029`.
- #74 `Image.gz-dtb` sha256:
  `454ecafe06d7bb4dbceddefbc1a75cb6c9b9f0eb3036cd0ebf20e3e86de45dc1`.
- #74 `System.map` sha256:
  `f832ed76a6544a261a3b3173f5fd0b8ae29b8bbba890a79cf6fead0ce30a5587`.
- #74 `kernel.config` sha256:
  `698b6764b989ef0bab75c0e6d6c291706e6a4a8d527d6a59347ad8c966d1fdd1`.
- #74 kernel string:
  `3.18.140 #74 SMP PREEMPT Tue Jun 9 04:26:09 CDT 2026`.
- Artifact verification: `sha256sum -c SHA256SUMS` passed, `abootimg -x`
  unpacked successfully, `cmp Image.gz-dtb verify-unpack/zImage` passed,
  `cmp initrd.img verify-unpack/initrd.img` passed, and gzip-expanded marker
  strings include `config-force-first`, `timing-before-enqueue`,
  `timing-after-enqueue`, `M6 RDMA diag`, and `M6 RDMA cfg_input`.
- #73 artifact:
  `/srv/forge/android/export/meizu_m6_artifacts/20260609-0332-m6-pstore-retention-flood-cap-bootonly`.
- #73 boot image sha256:
  `e842ab565cb9cfbceea02ced6fc2621ce1771b36d96748a7441a1210bfcfc24f`.
- #73 `System.map` sha256:
  `db5ba3640bd231a8a6e7eb0cc6300d3b02f0c93dbed38dedc05bf1ded474fd2d`.
- #73 capture:
  `/srv/forge/android/meizu_m6/captures/20260609-0349-m6-pstore-retention-flood-cap-after-second-reboot`.
- `identity-status.txt` reports kernel `3.18.140 #73`, boot hash
  `e842ab565cb9cfbceea02ced6fc2621ce1771b36d96748a7441a1210bfcfc24f`,
  `sys.boot_completed=1`, `bootanim=stopped`, battery `6`, and `Charging`.
- `proc-last_kmsg-after-second-reboot.txt` lines around the compact marker
  tail show `M6D01..M6D10` with `H=0/124`, `M6L01 enter`, `M6L02 skip-power`,
  `M6L03 skip-init`, and `M6F fill/trig/const`.
- `proc-last_kmsg-after-second-reboot.txt` first timeout shows
  `rdma_eof=0 mutex_eof=1`, RDMA0 `IN=0/0 OUT=0/0`, `route VALID=0x0
  READY=0x4000937a`, live DSI `HSA/HBP/HFP=0x0/0x124/0x120`, and
  `word=546 line=0`.
- Source audit: `DSI_Config_VDO_Timing()` computes HSA bytes as
  `horizontal_sync_active * bpp - 4`, so the M6 panel's `20 * 3 - 4` should
  become aligned `0x38`. `ddp_dsi_config()` skips the force-config block when
  `mipitx_enabled && PMaster_enable == 0 && !dsi_force_config`.

Files changed:
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_dsi.c`: adds the
  one-shot first-takeover DSI replay when LK-enabled MIPITX would skip Linux
  timing/VM setup, plus compact SRAM timing breadcrumbs.
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_rdma.c`: adds bounded
  read-only RDMA0 config/start markers with route, mutex, RDMA counters, size,
  FIFO, direct-link selectors, and clock-gate state.
- `BRINGUP_STATE.md`: records this patch category, evidence, expected markers,
  rollback condition, and verification commands.

Why each file changed: `ddp_dsi.c` owns the exact first-takeover branch that
the #73 compact markers entered with `HSA=0`; the one-shot replay tests that
branch without panel reset or broad subsystem disable. `ddp_rdma.c` owns the
direct-link engine whose first timeout has `GLOBAL=0x101` but zero IN/OUT
counters; its markers are placed before/after real config and start calls so
the next capture can distinguish an uncommitted RDMA config from DSI
backpressure or route readiness failure.

Expected next marker: the next verified boot should show
`M6 DSI mipitx-decision[config-force-first]`, `M6D timing-before-enqueue`,
`M6D timing-after-enqueue`, and `M6 RDMA diag[config-*]` /
`M6 RDMA diag[start-*]` before the first `FRAME_DONE` timeout. If the DSI skip
branch was the culprit, live DSI HSA should become `0x38`, `STATE9` line
should advance from `0`, DSI IRQ/VM_DONE behavior should change, and the
physical panel should show bootlogo or at least flicker. If not, RDMA markers
should say whether RDMA was configured as direct-link before start and whether
route ready/counters changed before the first timeout.

Rollback condition: revert if the first DSI replay regresses ADB,
`sys.boot_completed=1`, charging status, bootlogo timing, DSI BIST latch, DCS
stock-page reads, or if the next capture shows `HSA=0x38` committed with no
physical/counter/IRQ delta, proving the force path only adds risk without
moving the frontier.

Verification commands:

```bash
A='adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5'
$A root
$A shell 'uname -a; getprop sys.boot_completed; sha256sum /dev/block/platform/mtk-msdc.0/by-name/boot'
$A shell 'cat /proc/last_kmsg | grep -E "M6 DSI mipitx-decision\\[config-force-first\\]|M6D|M6 RDMA diag|M6 DPMGR event flow|M6 DDP timeout|HSA/HBP|word=|line=" | head -260'
$A shell 'dmesg | grep -E "M6 DSI mipitx-decision\\[config-force-first\\]|M6D|M6 RDMA diag|M6 DPMGR event flow|M6 DDP timeout|HSA/HBP|word=|line=" | head -260'
$A shell 'cat /proc/interrupts | grep -E "dsi0|rdma0|mutex|ovl"'
$A shell 'cat /d/mtkfb | head -120'
```

## 2026-06-09 #72 pstore retention / display flood cap

PATCH HISTORY, **DIAGNOSTIC**, 2026-06-09: reduce late M6 display diagnostic
flood enough for early handoff breadcrumbs to survive in `dmesg`,
`/proc/last_kmsg`, and pstore, and duplicate compact `M6D`/`M6L` breadcrumbs
to normal display printk in addition to `aee_sram_printk()`. This patch does
not change display registers, DDP routing, DSI timing, LCM command tables,
OVL/RDMA/HWC/PQ behavior, backlight, or boot image ramdisk/cmdline.

Hypothesis: FACT: #72 boot image
`9388695ce7bd572d3c5f6a24393b98be4aa787bdf09bc544c897c910fd015431` boots,
reaches `sys.boot_completed=1`, preserves nonblack `720x1280` screencaps, and
does not regress charging status. FACT: after a second #72 reboot,
`/proc/last_kmsg` and pstore are fresh enough to show #72-era display logs, but
they start around kernel timestamp `217s` and contain late `M6 OVL irq diag`
flood instead of the human-observed 3-4s bootlogo drop boundary. HYPOTHESIS:
the early `M6P`/`M6D`/`M6L`/`M6F` breadcrumbs are being overwritten by late
diagnostic flood, so reducing late flood and emitting compact breadcrumbs into
the normal printk channel should preserve the earliest takeover sequence in the
next capture.

Evidence:
- #73 pstore-retention artifact:
  `/srv/forge/android/export/meizu_m6_artifacts/20260609-0332-m6-pstore-retention-flood-cap-bootonly`.
- #73 boot image sha256:
  `e842ab565cb9cfbceea02ced6fc2621ce1771b36d96748a7441a1210bfcfc24f`.
- #73 `Image.gz-dtb` sha256:
  `7ca7d1f43e7c73f09edaf6d613baabecd6619e8a853ec5852e5810fa7b9b52d4`.
- #73 `System.map` sha256:
  `db5ba3640bd231a8a6e7eb0cc6300d3b02f0c93dbed38dedc05bf1ded474fd2d`.
- #73 kernel string:
  `3.18.140 #73 SMP PREEMPT Tue Jun 9 03:31:47 CDT 2026`.
- Artifact verification: `sha256sum -c SHA256SUMS` passed, `abootimg -x`
  unpacked successfully, `cmp Image.gz-dtb verify-unpack/zImage` passed,
  `cmp initrd.img verify-unpack/initrd.img` passed, and gzip-expanded marker
  strings include `M6D`, `M6P`, `M6L`, `M6F`, `M6 OVL irq diag`, and
  `M6 DDP irq diag`.
- #72 first boot capture:
  `/srv/forge/android/meizu_m6/captures/20260609-0303-m6-ramconsole-breadcrumbs-firstboot`.
- #72 second boot capture:
  `/srv/forge/android/meizu_m6/captures/20260609-0306-m6-ramconsole-breadcrumbs-after-second-reboot`.
- #72 identity: `identity-status.txt` reports kernel
  `3.18.140 #72 SMP PREEMPT Tue Jun 9 02:56:30 CDT 2026`,
  `sys.boot_completed=1`, `bootanim=stopped`, battery `6`, `Charging`, and
  matching boot-partition sha256
  `9388695ce7bd572d3c5f6a24393b98be4aa787bdf09bc544c897c910fd015431`.
- `bootdiag-latest/proc/last_kmsg.txt` starts with late
  `M6 OVL irq diag[67]` at `[217.701142]` and continues with repeated
  OVL diagnostic lines, proving the retained log window is dominated by late
  display flood rather than the 3-4s physical drop.
- `pstore-pmsg-ramoops-after-second-reboot.txt`, pstore console, and
  `/proc/last_kmsg` contain no `M6P`, `M6D`, `M6L`, or `M6F` compact lines.

Files changed:
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_irq.c`: caps M6 IRQ
  diagnostic sampling to the first 8 events, removing the periodic late tail.
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_manager.c`: caps M6
  DPMGR event-flow sampling to the first 8 events, removing the periodic late
  tail.
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_dsi.c`: duplicates the
  compact `M6D` DSI snapshot to `DISPERR()` as well as `aee_sram_printk()`.
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/disp_lcm.c`: duplicates the
  compact `M6L` LCM breadcrumb to `DISPERR()` as well as `aee_sram_printk()`.
- `BRINGUP_STATE.md`: records the #72 capture verdict and retention patch.

Why each file changed: `ddp_irq.c` and `ddp_manager.c` are the proven late
flood sources that evict the early timeline from pstore/last_kmsg. `ddp_dsi.c`
and `disp_lcm.c` own the compact early DSI/LCM breadcrumbs that need to survive
through the normal printk-backed evidence path. The change is diagnostic-only:
it only changes log volume and log routing.

Expected next marker: the next verified boot and second-reboot capture should
show `M6D`, `M6L`, existing `M6P`, and `M6F` lines in current dmesg and/or
previous-boot pstore/`/proc/last_kmsg`, with the retained log window beginning
near early display handoff instead of around 217s. If the physical panel still
goes black, those lines should bracket whether the drop is before LCM init skip,
during DSI config/start skip, or after DSI HS start.

Rollback condition: revert if the cap hides the only remaining late OVL/RDMA
failure evidence, if compact breadcrumbs still do not survive after a clean
second reboot, or if #73 regresses `sys.boot_completed=1`, ADB root, pstore,
charging status, or physical display timing.

Verification commands:

```bash
A='adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5'
$A root
$A shell 'uname -a; getprop sys.boot_completed; sha256sum /dev/block/platform/mtk-msdc.0/by-name/boot'
$A shell 'dmesg | grep -E "M6P|M6D|M6L|M6F|M6 OVL irq diag|M6 DDP irq diag" | head -160'
$A reboot
$A wait-for-device
$A root
$A shell 'cat /proc/last_kmsg | grep -E "M6P|M6D|M6L|M6F|M6 OVL irq diag|M6 DDP irq diag" | head -200'
$A shell 'cat /sys/fs/pstore/console-ramoops | grep -E "M6P|M6D|M6L|M6F|M6 OVL irq diag|M6 DDP irq diag" | head -200'
```

## 2026-06-09 #71 ram_console display breadcrumbs

PATCH HISTORY, **DIAGNOSTIC**, 2026-06-09: duplicate the shortest display
handoff breadcrumbs into MTK `ram_console` through `aee_sram_printk()`. This
patch does not change display registers, DSI timing, panel command tables,
DDP routing, HWC/PQ/OVL/RDMA behavior, backlight, or boot image ramdisk/cmdline.

Hypothesis: FACT: boot image
`75a1436a1e34bd0b4904e0f3405f3bf295bf6769aeee07a33f6085acde35aeab` boots as
kernel `#71`, reaches `sys.boot_completed=1`, and preserves nonblack
`720x1280` screencaps. FACT: the #71 isolation capture proves runtime HSA
override writes `DSI_HSA_WC=0x38`, page5 `0x2A` write/read probes accept both
`0x14` and `0x18`, and white DSI BIST latches `BIST_CON=0x200446`. FACT:
latest bootdiag starts at kernel timestamp ~36.7s while the human-observed
physical bootlogo loss happens at ~3-4s, so normal dmesg/bootdiag misses the
frontier. HYPOTHESIS: the earliest failing boundary is still the LK-to-Linux
display takeover, but the current evidence channel loses the exact early
marker sequence before ADB/bootdiag can preserve it.

Evidence:
- #71 isolation capture:
  `/srv/forge/android/meizu_m6/captures/20260609-0237-m6-hsa-page5-isolation-postflash`.
- #71 artifact directory:
  `/srv/forge/android/export/meizu_m6_artifacts/20260609-0224-m6-dsi-hsa-page5-isolation-bootonly`.
- #71 boot image sha256:
  `75a1436a1e34bd0b4904e0f3405f3bf295bf6769aeee07a33f6085acde35aeab`.
- #71 capture identity reports kernel
  `3.18.140 #71 SMP PREEMPT Tue Jun 9 02:27:26 CDT 2026`,
  `sys.boot_completed=1`, `bootanim=stopped`, battery `6`, `Charging`, and
  matching boot-partition sha256.
- `dmesg-hsa-page5-isolation.txt` reports `M6 DSI hsa_wc: after-write ... live=0x38/...`,
  `M6 LCM page5_2a_probe ... write=14 read=14 ...`, later
  `write=18 read=18 ...`, and `M6 DSI bist_profile ... BIST_CON=0x200446`.
- Latest bootdiag
  `/cache/bootdiag/run-20260609-063652-1512/cmd/dmesg.txt` starts at
  `[36.717694]`; this is after the physical 3-4s bootlogo drop.
- Generated `.config` has `CONFIG_MTK_RAM_CONSOLE=y`,
  `CONFIG_MTK_RAM_CONSOLE_SIZE=0xc00`, and
  `CONFIG_MTK_RAM_CONSOLE_ADDR=0x0011D000`.
- Source audit shows `aee_sram_printk()` is exported by
  `drivers/misc/mediatek/aee/common/aee-common.c` and writes through
  `ram_console_write()`.
- #72 breadcrumb artifact:
  `/srv/forge/android/export/meizu_m6_artifacts/20260609-0258-m6-ramconsole-display-breadcrumbs-bootonly`.
- #72 boot image sha256:
  `9388695ce7bd572d3c5f6a24393b98be4aa787bdf09bc544c897c910fd015431`.
- #72 `Image.gz-dtb` sha256:
  `75dd801075706839f272019db84489e6a4175d9a9efb00b751b6ecba2e03fe3c`.
- #72 `System.map` sha256:
  `133c09d1a2746fea5f1a07c3551eb9f577538420e6d5a518caaaae550e07ba27`.
- #72 kernel string:
  `3.18.140 #72 SMP PREEMPT Tue Jun 9 02:56:30 CDT 2026`.
- Artifact verification: `sha256sum -c SHA256SUMS` passed, `abootimg -x`
  unpacked successfully, `cmp Image.gz-dtb verify-unpack/zImage` passed,
  `cmp initrd.img verify-unpack/initrd.img` passed, and gzip-expanded marker
  strings include `M6D`, `M6P`, `M6L`, and `M6F`.

Files changed:
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_dsi.c`: adds bounded
  `M6Dxx` ram_console lines for DSI config skip, takeover, config-done, and
  start-after-HS snapshots.
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/primary_display.c`: adds
  bounded `M6Pxx` primary takeover breadcrumbs.
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/disp_lcm.c`: adds bounded
  `M6Lxx` LCM init/call/skip breadcrumbs.
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/mtkfb.c`: adds `M6F`
  early framebuffer white/trigger/const marker breadcrumbs.
- `BRINGUP_STATE.md`: records this diagnostic patch and the #71 capture
  verdict.

Why each file changed: `ddp_dsi.c`, `primary_display.c`, `disp_lcm.c`, and
`mtkfb.c` are the exact LK-to-Linux handoff and early marker boundaries that can
occur before ADB and before bootdiag's dmesg capture. `ram_console` is small, so
the patch intentionally writes compact one-line breadcrumbs rather than full
register dumps.

Expected next marker: after flashing the next boot image and rebooting once more,
`/sys/fs/pstore/console-ramoops` or the latest bootdiag `pstore/console-ramoops`
should contain compact `M6P`, `M6D`, `M6L`, and `M6F` lines from the boot that
just lost the physical bootlogo. Those lines should bracket whether the visual
drop is before framebuffer white marker, during DSI config/start skip, around
`disp_lcm_init()`, or after DSI HS start.

Rollback condition: revert this diagnostic patch if the new ram_console writes
regress `sys.boot_completed=1`, flood/evict all useful ram_console content,
break AEE/pstore capture, change physical display timing, or cause a boot loop.

Verification commands:

```bash
A='adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5'
$A root
$A shell 'sha256sum /dev/block/platform/mtk-msdc.0/by-name/boot; uname -a; getprop sys.boot_completed'
$A reboot
$A wait-for-device
$A root
$A shell 'cat /sys/fs/pstore/console-ramoops | grep -E "M6P|M6D|M6L|M6F"'
$A shell 'BOOTDIAG=$(ls -td /cache/bootdiag/run-* 2>/dev/null | head -1); grep -E "M6P|M6D|M6L|M6F" "$BOOTDIAG/pstore/console-ramoops"'
```

## 2026-06-09 #70 BIST sweep / panel-private drift isolation

PATCH HISTORY, **DIAGNOSTIC/ISOLATION**, 2026-06-09: add DSI timing/VM_CMD
markers plus debugfs-only manual isolation controls for DSI `HSA_WC` and
ILI9881P page5 command `0x2A`. The default boot path is not changed; the new
controls only run when invoked through `/d/mtkfb`.

Hypothesis: FACT: boot image
`4fb4ad2a4648e5220630ddd56f8ca2741483b5b2f2d1c9aee10bea2ca97ce41f` is flashed
and running as kernel `#70`. FACT: DSI BIST profiles 0-5 latch expected
`BIST_CON` values and DSI/MIPITX stay powered in video mode. FACT: stock LK and
current source write ILI9881P page5 `0x2A=0x14`, while the live panel reads back
`0x18` after Linux/reinit/stock-pages reads. INFERENCE: if all BIST windows were
physically black, HWC/PQ/OVL/RDMA source content are rejected as the earliest
physical-black frontier. HYPOTHESIS: the remaining frontier is DSI host output,
MIPITX analog/lane state, or ILI9881P HS-video acceptance after Linux takeover,
with page5 `0x2A` drift and DSI timing/VM_CMD state as narrow candidates.

Evidence:
- BIST profile sweep capture:
  `/srv/forge/android/meizu_m6/captures/20260609-0206-m6-bist-profile-sweep-post-black-report`.
- Post-BIST stock-pages/truth capture:
  `/srv/forge/android/meizu_m6/captures/20260609-0208-m6-post-bist-stockpages-truth`.
- Capture verdicts:
  `/srv/forge/android/meizu_m6/captures/20260609-0206-m6-bist-profile-sweep-post-black-report/CAPTURE_VERDICT.md`
  and
  `/srv/forge/android/meizu_m6/captures/20260609-0208-m6-post-bist-stockpages-truth/CAPTURE_VERDICT.md`.
- Stock LK init decode:
  `/srv/forge/android/meizu_m6/captures/20260530-stock-lk-boot-reverse-inputs/lk-ili9881p-init-table-decode.txt`,
  entry 12 writes page5 command `0x2A` data `0x14`.
- Current LCM source:
  `kernel-3.18/drivers/misc/mediatek/lcm/ili9881p_hd_dsi_txd/ili9881p_hd_dsi_txd.c`,
  `init_setting[]` writes page5 command `0x2A` data `0x14`.
- Live stock-pages read:
  `dmesg-post-bist-stockpages-truth.txt` reports
  `M6 LCM stock_pages[1] page=5 name=page5_2a cmd=0x2a len=1 read=18 ...`.

Files changed:
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_dsi.c`: logs computed
  DSI video timing bytes before/after enqueue, logs bounded VM_CMD set/enable
  transitions, and adds `dsi_m6_force_hsa_wc()` for manual `HSA_WC` isolation.
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_dsi.h`: exports the
  manual `HSA_WC` helper for debugfs.
- `kernel-3.18/drivers/misc/mediatek/lcm/ili9881p_hd_dsi_txd/ili9881p_hd_dsi_txd.c`:
  adds a page5 `0x2A` write/read/hold/read probe.
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/primary_display.c`: wraps the
  page5 probe with the same stop-video, LP-command, restart-video flow used by
  stock-page reads.
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/primary_display.h`: exports
  the page5 probe wrapper.
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/disp_debug.c`: adds
  `m6_dsi_hsa_wc:<value>[:hold_ms]` and
  `m6_lcm_page5_2a:<value>[:hold_ms]` commands.
- `BRINGUP_STATE.md`: records the evidence, patch category, expected markers,
  rollback condition, and verification commands.

Why each file changed: `ddp_dsi.c` owns the DSI timing, VM command, and host
register frontier identified by #70. The LCM driver owns the narrow
panel-private page5 register that drifted from the stock/source table. Primary
display owns the safe transition from video mode to LP command reads/writes and
back. `disp_debug.c` is the existing manual diagnostic command surface. The
state file prevents this isolation from being mistaken for a proper fix before
fresh physical evidence is collected.

Expected next marker: `/d/mtkfb` should accept `m6_dsi_hsa_wc:<value>[:hold_ms]`
and `m6_lcm_page5_2a:<value>[:hold_ms]`, logging before/write/after/hold-end
DSI snapshots and page5 readback. Physical observation should answer whether
forcing HSA `0x38`, restoring page5 `0x14`, or writing observed `0x18` causes
any flash/flicker/image.

Rollback condition: revert the next diagnostic/isolation patch if it changes the
default boot path, regresses `sys.boot_completed=1`, breaks BIST/profile
commands, prevents stock private page reads, loses ADB, or causes persistent
panel state corruption after a reboot.

Verification commands:

```bash
A='adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5'
$A root
$A shell 'printf "m6_dsi_hsa_wc:0x38:5000\n" > /d/mtkfb'
$A shell 'printf "m6_lcm_page5_2a:0x14:5000\n" > /d/mtkfb'
$A shell 'printf "m6_lcm_page5_2a:0x18:5000\n" > /d/mtkfb'
$A shell 'printf "m6_dsi_bist_profile:0:0x00ffffff:5000\n" > /d/mtkfb'
$A shell 'dmesg | grep -E "M6 DSI hsa_wc|M6 LCM page5_2a|M6 DSI snapshot|M6 DSI bist_profile" | tail -360'
```

## 2026-06-09 DSI takeover / MIPITX block diagnostic

PATCH HISTORY, **DIAGNOSTIC**, 2026-06-09: add read-only takeover markers
around the LK-inited panel -> Linux `dpmgr_path_config()` / `dpmgr_path_start()`
/ config-CMDQ-submit / `disp_lcm_init()` boundary, plus a bounded raw MIPITX
block dump (`0x000..0x0dc`) at DSI takeover points. This patch does not change
DSI timing, MIPITX/DSI register writes, LCM reset, panel init command tables,
backlight, DDP route, PQ, OVL, RDMA, HWC, fences, wait tokens, or boot image
ramdisk/cmdline.

Hypothesis: FACT: boot image
`019281e6fd9688c3868ff75397185a91871157f5da2580f26ee7ab3148d5c782` is currently
flashed and read back from `/dev/block/platform/mtk-msdc.0/by-name/boot`. FACT:
the #69 root captures prove Android userspace is boot-completed, screencap is
nonblack `720x1280`, DSI/MIPITX are powered, LP DCS stock-page reads work, and
manual full-BIST latches `BIST_CON=0x200446 self_pat=1 bist_en=1 lane=4`, while
the human still reports a lit physical black panel and bootlogo disappearance
around `mtkfb_probe` / Linux display handoff. FACT: a forced runtime
`m6_lcm_reinit:1` ran the full `lcm_init()` sequence and restarted the path but
did not recover physical output. INFERENCE: normal HWC/FB/PQ/OVL content is not
the earliest frontier, and a simple missing LCM reinit is rejected. HYPOTHESIS:
the remaining frontier is the first Linux takeover of an LK-inited ILI9881P
panel: DSI host/video/CMDQ/MIPITX state may look active while a hidden host-side
or PHY-side detail makes the panel stop accepting HS video.

Evidence:
- Current root capture:
  `/srv/forge/android/meizu_m6/captures/20260609-0107-m6-hs-window-parser-diag-root`.
- Runtime forced-reinit capture:
  `/srv/forge/android/meizu_m6/captures/20260609-0115-m6-lcm-reinit-force-root`.
- Capture identity: `identity-status.txt` reports
  `Linux localhost 3.18.140 #69 SMP PREEMPT Tue Jun 9 01:01:54 CDT 2026
  aarch64`, `sys.boot_completed=1`, root ADB, battery `6`, `Charging`.
- Artifact/readback identity: `device-boot-partition-sha256.txt` reports
  `019281e6fd9688c3868ff75397185a91871157f5da2580f26ee7ab3148d5c782`.
- `screencap-before.png` and `screencap-after.png` in the reinit capture are
  valid `720x1280` PNGs while the physical panel stayed lit black by human
  report.
- Reinit dmesg contains `M6 LCM debug reinit: start force=1`, full
  `M6 LCM init start/end`, `lcm init ret=0`, `start path end busy=0`, and
  BIST latch markers after reinit.
- `/proc/interrupts` in the reinit capture shows `mutex`, `ovl0`, and `rdma0`
  counters advancing while `dsi0` remains `0`; prior sidecar review classifies
  DSI IRQ zero as observation-only for this video path.
- Stock reverse inputs:
  `/srv/forge/android/meizu_m6/captures/20260530-stock-lk-boot-reverse-inputs/`.
- New build log:
  `/srv/forge/android/meizu_m6/kernel-meizu_M6-N-ex6-linux-3.18.140/build-m6-dsi-takeover-mipitx-block-diag-20260609.log`.
- New artifact directory:
  `/srv/forge/android/export/meizu_m6_artifacts/20260609-0141-m6-dsi-takeover-mipitx-block-diag-bootonly`.
- Built boot image sha256:
  `4fb4ad2a4648e5220630ddd56f8ca2741483b5b2f2d1c9aee10bea2ca97ce41f`.
- Built `Image.gz-dtb` sha256:
  `264c1ee492f63dfcc4d4ab5159db483d9b667d1f252563a2b7069d14cf24fef7`.
- Built `System.map` sha256:
  `4b4f609e166c43813c7de4d1bf838e7ecf632cd2d303dd652276c63bbf0ef310`.
- Built `kernel.config` sha256:
  `698b6764b989ef0bab75c0e6d6c291706e6a4a8d527d6a59347ad8c966d1fdd1`.
- Artifact verification: `sha256sum -c SHA256SUMS`, `abootimg -x`, kernel
  `cmp`, ramdisk `cmp`, and gzip marker-string checks passed.

Files changed:
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_dsi.c`: adds
  `dsi_m6_dump_takeover()`, raw MIPITX `0x000..0x0dc` block dumping, and
  takeover markers around VM-CMD enqueue/config-done/start-after-HS points.
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_dsi.h`: exports the
  takeover dump helper for primary-display markers.
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/primary_display.c`: logs the
  primary-display takeover boundary around path config/start, config-CMDQ
  submit, and `disp_lcm_init()`, preserving the existing DCS status reads.
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/disp_lcm.c`: logs whether
  `disp_lcm_init(force=0)` skips or calls `init_power()` / `init()` when LK
  reports the panel already initialized.
- `BRINGUP_STATE.md`: records the reinit verdict, patch category, artifact
  identity, expected markers, rollback condition, and verification commands.

Why each file changed: `ddp_dsi.c` owns the DSI host/MIPITX state that remains
ambiguous after #69. `primary_display.c` owns the earliest Linux takeover
boundary matching the physical bootlogo drop. `disp_lcm.c` is the narrow place
to prove from logs whether normal boot skipped or called panel reset/init. The
header is required for the cross-file read-only dump call. The state file keeps
the negative reinit result and prevents retesting HWC/PQ/normal framebuffer
layers as the first frontier without contradictory fresh evidence.

Expected next marker: after flashing
`boot-m6-dsi-takeover-mipitx-block-diag-20260609.img`, bootdiag/dmesg should
show `M6 primary takeover[primary-before-path-config]`,
`primary-after-path-config`, `primary-before-path-start`,
`primary-after-path-start`, `primary-before-cmdq-flush`,
`primary-after-cmdq-flush-submit`, `primary-before-disp-lcm-init`,
`primary-after-disp-lcm-init`, plus `M6 DSI mipitx_block[...]` raw dumps and
`M6 LCM disp_lcm_init: skip init ...` or `call init ...`. If the physical
bootlogo disappears between two adjacent takeover markers, the next patch
should isolate that exact DSI/DDP sub-boundary. If all takeover markers precede
or follow the visual drop, use the raw MIPITX block and stock-LK reverse to pick
the next host/PHY parity target.

Rollback condition: revert this diagnostic patch if it prevents boot, regresses
`sys.boot_completed=1`, causes no-ADB/offline beyond the already observed cable
instability, floods bootdiag enough to hide takeover timing, changes DCS/BIST
availability compared with #69, or if a fresh capture proves the raw MIPITX
block reads fault/hang on this SoC.

Verification commands:

```bash
ART=/srv/forge/android/export/meizu_m6_artifacts/20260609-0141-m6-dsi-takeover-mipitx-block-diag-bootonly
(cd "$ART" && sha256sum -c SHA256SUMS)
cmp "$ART/Image.gz-dtb" "$ART/verify-unpack/zImage"
cmp "$ART/initrd.img" "$ART/verify-unpack/initrd.img"
A='adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5'
$A root
$A shell 'sha256sum /dev/block/platform/mtk-msdc.0/by-name/boot; uname -a; getprop sys.boot_completed'
$A shell 'dmesg | grep -E "M6 primary takeover|M6 DSI takeover|M6 DSI mipitx_block|M6 LCM disp_lcm_init|config-before-vmcmd|config-after-vmcmd|start-after-hs" | tail -260'
$A shell 'BOOTDIAG=$(ls -d /cache/bootdiag/run-* 2>/dev/null | tail -1); grep -E "M6 primary takeover|M6 DSI takeover|M6 DSI mipitx_block|M6 LCM disp_lcm_init|mtkfb_probe|mtkfb_init" "$BOOTDIAG/cmd/dmesg.txt"'
```

## 2026-06-09 HS-window parser diagnostic follow-up

PATCH HISTORY, **DIAGNOSTIC**, 2026-06-09: replace the `/d/mtkfb`
`m6_dsi_hs_window:<tag>[:hold_ms]` parser with explicit `tag[:hold_ms]`
splitting. This patch does not change display timing, DSI/MIPITX registers,
panel commands, BIST behavior, DDP routing, PQ, OVL, RDMA, HWC, fences, or wait
behavior. It only makes the already-added bounded HS-window sampler callable on
this kernel.

Hypothesis: FACT: boot image
`82c75e72903b20dbee3447487050d8d034b9e60df07196bf32a661b5fedb558a` was flashed
and read back from `/dev/block/platform/mtk-msdc.0/by-name/boot` with the same
sha256. FACT: the user still reports the physical bootlogo disappears at 3-4 s
and the lit panel remains black. FACT: root capture proves Android is booted,
SurfaceFlinger has nonblack `720x1280` content, DSI full-BIST latches
`BIST_CON=0x200446 self_pat=1 bist_en=1 lane=4`, and DSI/PHY/MIPITX snapshots
remain powered. FACT: the same capture also proves both `m6_dsi_hs_window`
commands failed with `error to parse cmd ...`, so the new HS-window sampler did
not run. HYPOTHESIS: the `%[^:]` `sscanf()` scanset used by this kernel parser is
not reliable here; a manual parser is needed before drawing conclusions about
HS-window state evolution.

Evidence:
- Fresh postflash capture:
  `/srv/forge/android/meizu_m6/captures/20260609-0048-m6-dsi-irq-hs-window-after-flash`.
- Root follow-up capture:
  `/srv/forge/android/meizu_m6/captures/20260609-0054-m6-dsi-hs-window-root`.
- Capture identity: `identity-status-root.txt` reports kernel
  `3.18.140 #68 SMP PREEMPT Tue Jun 9 00:20:35 CDT 2026`, root ADB,
  `sys.boot_completed=1`, `bootanim=stopped`, battery `6`, `Charging`, USB online.
- Artifact/readback identity: `device-boot-partition-sha256-root.txt` and
  `flashed_boot_sha256.txt` both report
  `82c75e72903b20dbee3447487050d8d034b9e60df07196bf32a661b5fedb558a`.
- `display-baseline-root.txt`: `mutex`, `ovl0`, and `rdma0` IRQ counters
  advance while `dsi0` is `0`.
- `dumpsys-SurfaceFlinger-root-after.txt`: built-in screen is ON, HWC present,
  flips advance, and framebuffer layers are `720x1280`.
- `screencap-root-after.png`: valid nonblack `720x1280` PNG.
- `dmesg-root-after.txt`: `m6_dsi_bist_full:0x00ff00` latches
  `BIST_PATTERN=0xff00`, `BIST_CON=0x200446`, `self_pat=1`, `bist_en=1`, and
  after disable returns `BIST_CON=0x0`.
- `dmesg-root-after.txt`: `error to parse cmd m6_dsi_hs_window:ui_root:5000`
  and `error to parse cmd m6_dsi_hs_window:bist_green_root:5000`.
- New parser-only build log:
  `/srv/forge/android/meizu_m6/kernel-meizu_M6-N-ex6-linux-3.18.140/build-m6-hs-window-parser-diag-20260609.log`.
- New parser-only artifact directory:
  `/srv/forge/android/export/meizu_m6_artifacts/20260609-0103-m6-hs-window-parser-diag-bootonly`.
- Built boot image sha256:
  `019281e6fd9688c3868ff75397185a91871157f5da2580f26ee7ab3148d5c782`.
- Built `Image.gz-dtb` sha256:
  `e63a65e68813d31b7ca0da1fe19bfc7be17547c5e52d7a8c7ff207c2fd949e34`.
- Built `System.map` sha256:
  `9f2d6d60c569672b9a236785cb886d7c0b49cd46d153067091456038e67dc35a`.
- Built `kernel.config` sha256:
  `698b6764b989ef0bab75c0e6d6c291706e6a4a8d527d6a59347ad8c966d1fdd1`.
- Artifact verification: `sha256sum -c SHA256SUMS`, boot unpack, kernel
  `cmp`, and ramdisk `cmp` passed.

Files changed:
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/disp_debug.c`: manually parse
  `m6_dsi_hs_window` arguments so the bounded sampler can run from `/d/mtkfb`.
- `BRINGUP_STATE.md`: records the verified root capture, the parser failure, and
  the expected follow-up markers.

Why each file changed: `disp_debug.c` owns the `/d/mtkfb` command path that
failed in the fresh root capture; changing only this parser preserves the exact
DSI/PHY/BIST behavior under test. The state file records why this is a
diagnostic patch and prevents misreading the missing `hs_window` markers as a
DSI/PHY result.

Expected next marker: after flashing the next boot-only artifact, root
`printf "m6_dsi_hs_window:ui_root:5000\n" > /d/mtkfb` should print
`M6 DSI hs_window[ui_root]` plus `M6 DSI snapshot[hs-window-ui_root-...]`.
During a latched `m6_dsi_bist_full:0x00ff00` window, the same sampler should show
whether DSI `INTSTA`, `STATE7/8/9`, `VM_CMD`, and MIPITX lane state evolve while
physical output remains black.

Rollback condition: revert this parser-only diagnostic patch if `/d/mtkfb`
command handling regresses, if `m6_dsi_bist_full` stops latching/clearing, if
boot no longer reaches `sys.boot_completed=1`, or if the patch produces log
flooding beyond the bounded sampler.

Verification commands:

```bash
A='adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5'
$A root
$A shell 'printf "m6_dsi_hs_window:ui_root:5000\n" > /d/mtkfb'
$A shell 'printf "m6_dsi_bist_full:0x00ff00\n" > /d/mtkfb'
$A shell 'printf "m6_dsi_hs_window:bist_green_root:5000\n" > /d/mtkfb'
$A shell 'printf "m6_dsi_bist_full:0\n" > /d/mtkfb'
$A shell 'dmesg | grep -E "M6 DSI hs_window|M6 DSI snapshot\\[hs-window|BIST_CON|error to parse cmd m6_dsi_hs_window|M6 DSI irq_decode" | tail -260'
$A shell 'cat /proc/interrupts | grep -E "dsi|rdma|ovl|mutex"'
```

## 2026-06-09 DSI IRQ/HS-video window diagnostic

PATCH HISTORY, **DIAGNOSTIC**, 2026-06-09: add read-only decode markers for
DSI `START`, `STA`, `INTEN`, and `INTSTA`, bounded internal DSI IRQ markers,
`dsi_enable_irq()` frame-done enable markers, and a manual `/d/mtkfb`
`m6_dsi_hs_window:<tag>[:hold_ms]` sampler. This patch does not change DSI
timing, MIPITX registers, panel commands, BIST behavior, DDP route, PQ, OVL,
RDMA, HWC, fences, or wait-token behavior.

Hypothesis: FACT: #67 proved boot-completed Android userspace, nonblack
screencap, real OVL buffers, active RDMA0-to-DSI0 route, LP DCS stock-page
reads, and latched DSI full-BIST while the physical display remains lit-black.
FACT: `/proc/interrupts` still showed `dsi0 = 0` while `mutex`, `ovl0`, and
`rdma0` counters advanced, but LP DCS reads still emitted internal DSI
read/IRQ markers. HYPOTHESIS: the remaining observable frontier is whether the
DSI host ever produces normal VM/frame/IRQ progress during HS video windows,
or whether only LP command reads trigger internal IRQs while the HS-video/PHY
path stays electrically or panel-side invisible.

Evidence:
- Prior runtime verdict capture:
  `/srv/forge/android/meizu_m6/captures/20260608-2358-m6-rdma-eof-dsi-window-diag-mtkfb-debugfs-711HEBSR277K5`.
- Prior verified boot sha256:
  `7c51e2ded765f003191390e3aec5d811ba0c1ebeabf46b7bbed6b3e465fb7a77`.
- New build log:
  `/srv/forge/android/meizu_m6/kernel-meizu_M6-N-ex6-linux-3.18.140/build-m6-dsi-irq-hs-window-diag-20260609.log`.
- New artifact directory:
  `/srv/forge/android/export/meizu_m6_artifacts/20260609-0000-m6-dsi-irq-hs-window-diag-bootonly`.
- Built boot image sha256:
  `82c75e72903b20dbee3447487050d8d034b9e60df07196bf32a661b5fedb558a`.
- Built `Image.gz-dtb` sha256:
  `b801985a1bdb578b2a13b5d465c79c031bc1ab02b980102199272ca32cb23e97`.
- Built `System.map` sha256:
  `f3393d3f1926a46bb3250d3a63634f75c4005f12cea728f77609fae95d63b5e5`.
- Built `kernel.config` sha256:
  `698b6764b989ef0bab75c0e6d6c291706e6a4a8d527d6a59347ad8c966d1fdd1`.
- Built `initrd.img` sha256:
  `7de975b4485324f4a76eb44fa4cc61472e829421e8d27e31f50d80b984cb4a4f`.
- Build log sha256:
  `1631e9aaae2fec32f370590dfdfd3d5f05c03329a42c990fd784165a62a58e06`.
- Artifact verification: `sha256sum -c SHA256SUMS`, boot unpack, kernel
  `cmp`, ramdisk `cmp`, and marker-string checks passed.

Files changed:
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_dsi.c`: decodes DSI
  interrupt/start bits in every M6 snapshot, logs bounded internal IRQ state,
  logs `dsi_enable_irq()` frame-done toggles, and implements
  `dsi_m6_dump_hs_window()`.
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/disp_debug.c`: exposes the
  manual `/d/mtkfb` `m6_dsi_hs_window:<tag>[:hold_ms]` sampler.
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_dsi.h`: exports the
  sampler prototype for the debug command.
- `BRINGUP_STATE.md`: records category, evidence, artifact identity, expected
  markers, rollback condition, and verification commands.

Why each file changed: `ddp_dsi.c` owns the exact DSI host state and IRQ
callback boundary that is still ambiguous after #67. `disp_debug.c` is the
existing `/d/mtkfb` command processor used by the last successful capture, so
it is the narrowest way to trigger a bounded HS-video observation window from
ADB. `ddp_dsi.h` is required for the cross-file debugfs call.

Expected next marker: the next fresh boot should contain `M6 DSI irq_decode`
lines next to all `M6 DSI snapshot[...]` markers. Manual debugfs should print
`M6 DSI hs_window[ui]` and `M6 DSI snapshot[hs-window-ui-...]` samples at
0/17/34/51/68/85/102/119/136/250/500/1000/2000/5000 ms when requested. During
`m6_dsi_bist_full:0xff0000`, the `bist_red` HS window should show whether
`INTSTA` VM/frame/period bits and `STATE7/8/9` evolve while BIST is latched.
Internal DSI IRQ markers should distinguish LP DCS-read IRQ activity from
normal HS-video activity.

Rollback condition: revert this diagnostic patch if it prevents boot, regresses
`sys.boot_completed=1`, floods logs enough to hide early display state, changes
normal DSI/BIST register values compared with #67, breaks `stock_pages` DCS
reads, or makes the device lose ADB/reboot during the bounded sampler.

Verification commands:

```bash
A='adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5'
$A shell 'uname -a; getprop sys.boot_completed; getprop init.svc.bootanim'
$A shell 'printf "m6_dsi_hs_window:ui:5000\n" > /d/mtkfb'
$A shell 'printf "m6_dsi_bist_full:0xff0000\n" > /d/mtkfb'
$A shell 'printf "m6_dsi_hs_window:bist_red:5000\n" > /d/mtkfb'
$A shell 'printf "m6_dsi_bist_full:0\n" > /d/mtkfb'
$A shell 'dmesg | grep -E "M6 DSI (hs_window|irq_decode|irq\\[internal\\]|irq_enable|snapshot\\[hs-window|snapshot\\[bist-full)"'
$A shell 'cat /proc/interrupts | grep -E "dsi|rdma|ovl|mutex"'
```

## 2026-06-08 #67 runtime verdict: DSI BIST latches below Android content

STATE UPDATE, 2026-06-08: `7ed8d3a9379` was flashed and verified on
`711HEBSR277K5`. This update records runtime evidence only; it does not add a
new behavior patch.

Evidence:
- Commit: `7ed8d3a9379 display: add M6 RDMA EOF DSI window diagnostics`.
- Flashed artifact:
  `/srv/forge/android/export/meizu_m6_artifacts/20260608-2343-m6-rdma-eof-dsi-window-diag-bootonly`.
- Built boot image sha256:
  `7c51e2ded765f003191390e3aec5d811ba0c1ebeabf46b7bbed6b3e465fb7a77`.
- Flash/readback capture:
  `/srv/forge/android/meizu_m6/captures/20260608-2348-m6-rdma-eof-dsi-window-diag-flash-711HEBSR277K5`.
- Late postboot capture:
  `/srv/forge/android/meizu_m6/captures/20260608-2355-m6-rdma-eof-dsi-window-diag-late-711HEBSR277K5`.
- Corrected debugfs capture using `/d/mtkfb`:
  `/srv/forge/android/meizu_m6/captures/20260608-2358-m6-rdma-eof-dsi-window-diag-mtkfb-debugfs-711HEBSR277K5`.

Facts:
- `boot-after.img` from the flash capture matches the built boot image sha256
  `7c51e2ded765f003191390e3aec5d811ba0c1ebeabf46b7bbed6b3e465fb7a77`.
- The flashed kernel is `Linux localhost 3.18.140 #67 SMP PREEMPT Mon Jun 8
  23:40:37 CDT 2026 aarch64`.
- Late postboot has `sys.boot_completed=1`, `bootanim=stopped`, and a valid
  nonblack `720x1280` screencap. This keeps Android composition, HWC visible
  output, and framebuffer content out of the earliest physical-black frontier.
- `/d/dispsys` is the wrong command node for M6 debugfs helpers; the earlier
  `parse command error` lines in the ring buffer are from that failed path.
  The same commands written to `/d/mtkfb` run successfully.
- `m6_display_truth_window:post67root_mtkfb` shows two enabled OVL layers with
  real buffers, `bypass_pq=1`, active DSI0 destination, and nonzero backlight
  cache: `lcd-backlight ... bl=10 duty=21`.
- `m6_display_route_probe:dump` shows the normal route active enough for
  OVL/RDMA/DSI: route `VALID=0x4000937a`, RDMA0 enabled at `720x1280`, and
  DSI0 in video mode.
- `/proc/interrupts` counters rise during the debugfs window for `mutex`,
  `ovl0`, and `rdma0`; the `dsi0` GIC line stays at zero in that proc view.
  The LP DCS read path still emits internal DSI read/IRQ markers, so the
  zero proc counter is not evidence that DSI command transport is dead.
- `m6_dsi_dcs_status:stock_pages` reads real panel register values from ILI
  pages and returns to DSI video mode; examples include page-2 gamma bytes
  through `0x7e` and `M6 LCM stock_pages: read end`.
- `m6_dsi_bist_full:0xff0000` latches in DSI registers:
  `BIST_PATTERN=0xff0000`, `BIST_CON=0x200446`, `self_pat=1`, `bist_en=1`,
  `fix=1`, `lane=4`; 500 ms later the same latch remains active.
- During and after BIST, DSI snapshots remain in HS video state with
  `MODE=0x3`, `TXRX=0x1003c`, `PS=0x30870`, `STATE7=Video data period`, and
  MIPITX lanes/PLL matching the current stock-parity baseline.
- Charger evidence in the same boot is healthy enough for continued testing:
  `chrdet=1`, VBUS around `4346-4380 mV`, and `bq2415x` current programming
  returns `ret=0`.

Inference:
- The current evidence puts the physical-lit-black frontier below Android
  composition, PQ/HWC, framebuffer content, OVL layer programming, and normal
  RDMA activity.
- Because LP DCS reads work and DSI BIST latches while physical visibility has
  historically stayed black, the remaining display frontier is DSI host output
  to physical pixels: panel HS-video acceptance, MIPITX electrical/lane/timing
  parity, panel LED/electrical routing, or a stock-LK-only panel/PHY side
  effect that Linux still does not replay.
- Do not reopen PQ or generic OVL/HWC hypotheses unless a fresh capture
  contradicts this verdict.

Next diagnostic direction:
- Compare stock LK hidden DSI/MIPITX/panel side effects against the Linux
  `DSI_Start()` and `ili9881p_hd_dsi_txd` init/resume paths.
- If adding another patch, keep it **DIAGNOSTIC** and focus on the exact
  DSI-start/panel-HS boundary: DSI IRQ enable/status, VM_DONE/FRAME_DONE,
  lane FSM, panel page/status before and after `0x11`/`0x29`, and physical
  BIST visibility windows.

## 2026-06-08 RDMA EOF first-wait DSI/MIPITX diagnostics

PATCH HISTORY, **DIAGNOSTIC**, 2026-06-08: add bounded read-only full
DSI/MIPITX snapshots at the first primary video trigger-loop RDMA EOF wait,
the first primary video dpmgr wait, and the first primary video wait timeout.
This patch does not change CMDQ waits, event tokens, DDP route registers,
RDMA/OVL/DSI configuration, MIPITX registers, panel commands, PQ, HWC, or
userspace policy.

Hypothesis: FACT: #66 proves Android userspace, OVL constant-white content,
MUTEX/RDMA enable, DSI video mode, MIPITX lane/PLL state, and DSI BIST latch
are all observable in one boot. FACT: the remaining normal-pipeline failure is
`CMDQ_EVENT_DISP_RDMA0_EOF`/`CMDQ_EVENT_MUTEX0_STREAM_EOF` staying unset while
route VALID remains programmed and READY drops. HYPOTHESIS: the next capture
must show the full DSI/MIPITX state at the exact first RDMA EOF wait and first
timeout so we can decide whether RDMA EOF is blocked by upstream DDP readiness,
DSI video-state acceptance, or a later event-propagation/IRQ gap.

Evidence:
- Prior runtime capture:
  `/srv/forge/android/meizu_m6/captures/20260608-2305-m6-dsi-disable-lk-handoff-skip-postboot-711HEBSR277K5`.
- Prior verified boot sha256:
  `6f221b28db7e34715585c0342a60fa9ba8a2ce3a6c994599f490bd8ff9239aaf`.
- Prior runtime markers show `sys.boot_completed=1`, `bootanim=stopped`,
  `BIST_CON=0x200446`, `STATE7=Video data period`, `rdma_eof=0`,
  `mutex_eof=0`, and route READY dropping while VALID stays `0x4000937a`.
- New build log:
  `/srv/forge/android/meizu_m6/kernel-meizu_M6-N-ex6-linux-3.18.140/build-m6-rdma-eof-dsi-window-diag-20260608.log`.
- New artifact directory:
  `/srv/forge/android/export/meizu_m6_artifacts/20260608-2343-m6-rdma-eof-dsi-window-diag-bootonly`.
- Built boot image sha256:
  `7c51e2ded765f003191390e3aec5d811ba0c1ebeabf46b7bbed6b3e465fb7a77`.
- Built `Image.gz-dtb` sha256:
  `3d00dfc0e213f63a8b274080336544fc6e3bbf32b61636e5851b7635ba9c6e37`.
- Built `System.map` sha256:
  `2614d292e8f3bcf7caf073f4478f4af02ff0b676a23f5ef08a7b554639e295d4`.
- Built `kernel.config` sha256:
  `698b6764b989ef0bab75c0e6d6c291706e6a4a8d527d6a59347ad8c966d1fdd1`.
- Built `initrd.img` sha256:
  `7de975b4485324f4a76eb44fa4cc61472e829421e8d27e31f50d80b984cb4a4f`.
- Build log sha256:
  `90940fa0026a06cfdaf9f95967099da315e43b120ced9e430707a1be249827d4`.
- Artifact verification: `sha256sum -c SHA256SUMS`, boot unpack, kernel
  `cmp`, ramdisk `cmp`, and marker-string checks passed.

Files changed:
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/primary_display.c`: logs
  full `dsi_m6_dump_live("trigger-before-rdma-eof-wait")` once before the
  trigger loop appends the RDMA0 EOF and MUTEX0 stream EOF waits.
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_manager.c`: logs full
  `dsi_m6_dump_live("dpmgr-first-video-wait")` and
  `dsi_m6_dump_live("dpmgr-first-video-timeout")` once for primary video
  frame/vsync waits.
- `BRINGUP_STATE.md`: records the diagnostic purpose, evidence, expected
  markers, rollback condition, and verification commands.

Why each file changed: `primary_display.c` owns the CMDQ trigger-loop wait
construction that currently waits for real RDMA0 EOF/MUTEX0 EOF. `ddp_manager.c`
owns the wait/timeout path that proves whether those events actually arrive.
Both locations already dump route/RDMA/OVL state; adding the existing full DSI
snapshot there closes the host-side evidence gap without mutating hardware.

Expected next marker: the next fresh boot dmesg should contain
`M6 DSI snapshot[trigger-before-rdma-eof-wait]`,
`M6 DSI snapshot[dpmgr-first-video-wait]`, and, if the current failure
persists, `M6 DSI snapshot[dpmgr-first-video-timeout]`, with MIPITX lane/PLL
lines adjacent to the existing RDMA EOF/READY dumps. If DSI stays in video data
period across all three markers while RDMA EOF remains zero, inspect upstream
DDP READY/MUTEX/IRQ propagation next. If DSI leaves video mode or MIPITX lane
state changes between first wait and timeout, pivot below RDMA to DSI host/PHY
state parity.

Rollback condition: revert this diagnostic patch if it prevents boot,
regresses `sys.boot_completed=1`, floods logs enough to hide the first wait,
breaks stock-pages/BIST debugfs, or changes the RDMA/DSI state compared to the
#66 baseline.

Verification commands:

```bash
rg -n 'trigger-before-rdma-eof-wait|dpmgr-first-video-wait|dpmgr-first-video-timeout|RDMA0_EOF|rdma_eof=0|READY=' \
  /srv/forge/android/meizu_m6/captures/<fresh-capture>/dmesg*.txt \
  /srv/forge/android/meizu_m6/captures/<fresh-capture>/live-debugfs-dmesg.txt
rg -n 'MIPITX lanes|STATE7=|BIST_CON|M6 DPMGR event flow|M6 DDP timeout' \
  /srv/forge/android/meizu_m6/captures/<fresh-capture>/dmesg*.txt \
  /srv/forge/android/meizu_m6/captures/<fresh-capture>/live-debugfs-dmesg.txt
```

## 2026-06-08 DSI LK-handoff skip removal and #66 verdict

PATCH HISTORY, **PROPER-FIX + DIAGNOSTIC**, 2026-06-08: disable the old
M6-only `M6_LK_HANDOFF_SKIP_FIRST_DSI_CONFIG` isolation in `ddp_dsi.c`.
The previous #65 live root capture proved that this isolation left DSI in CMD
mode after `m6_dsi_dcs_status:stock_pages` stopped and restarted the video
path. The #66 patch keeps normal Linux-owned DSI config/start enabled.

Hypothesis: FACT: #65 booted and showed normal Android userspace, active
OVL/RDMA/MUTEX, and DSI video mode before manual debugfs intervention. FACT:
after `stock_pages` stopped the video path, `ddp_dsi_start()` printed
`M6 DSI lk-handoff[start]: skip first DSI start`, and the subsequent window
snapshot stayed at `DSI MODE=0x0`, `START=0x1`, with RDMA counters idle.
HYPOTHESIS: the LK-handoff skip was a stale isolation patch that could preserve
a bad DSI state after any stop/restart and could also interfere with the early
Linux handoff where the physical bootlogo disappears. Removing it should make
DSI restart behavior observable and kernel-owned, without changing LCM command
tables, panel GPIOs, PQ, OVL content, or MIPITX parameters.

Evidence:
- Prior harmful-skip capture:
  `/srv/forge/android/meizu_m6/captures/20260608-2306-m6-dsi-pipe-live-truth-stockpages-root-711HEBSR277K5`.
- New build log:
  `/srv/forge/android/meizu_m6/kernel-meizu_M6-N-ex6-linux-3.18.140/build-m6-dsi-disable-lk-handoff-skip-20260608.log`.
- New artifact directory:
  `/srv/forge/android/export/meizu_m6_artifacts/20260608-2257-m6-dsi-disable-lk-handoff-skip-bootonly`.
- Built boot image sha256:
  `6f221b28db7e34715585c0342a60fa9ba8a2ce3a6c994599f490bd8ff9239aaf`.
- Built `Image.gz-dtb` sha256:
  `6034e11cdaefebbfb6bf31b17701ec457eb8f725f194d12b381533ea9745b6a0`.
- Built `System.map` sha256:
  `d27a5b770790fe2c5db613562dcfaae04336dad1bc058c4c397c908b09ce86c1`.
- Built `kernel.config` sha256:
  `698b6764b989ef0bab75c0e6d6c291706e6a4a8d527d6a59347ad8c966d1fdd1`.
- Artifact verification: `sha256sum -c SHA256SUMS`, boot unpack, kernel
  `cmp`, and ramdisk `cmp` passed.
- Flash/readback capture:
  `/srv/forge/android/meizu_m6/captures/20260608-2301-m6-dsi-disable-lk-handoff-skip-flash-711HEBSR277K5`.
- Flash readback sha256 matched the local boot image:
  `6f221b28db7e34715585c0342a60fa9ba8a2ce3a6c994599f490bd8ff9239aaf`.
- Runtime capture:
  `/srv/forge/android/meizu_m6/captures/20260608-2305-m6-dsi-disable-lk-handoff-skip-postboot-711HEBSR277K5`.
- Runtime kernel identity:
  `Linux localhost 3.18.140 #66 SMP PREEMPT Mon Jun 8 22:51:50 CDT 2026 aarch64`.
- Runtime userspace: `sys.boot_completed=1`, `bootanim=stopped`, root ADB
  works, and `m6-screen.png` is a valid 720x1280 RGBA PNG.
- Runtime before live debugfs:
  `/proc/m6_mtkfb_early_diag` shows
  `const_ovl_l0 source=1 larc=1 clr=0xffffffff`, `live_pipe valid=1`,
  `live_rdma valid=1`, and `live_dsi ... mode=0x3`.
- Runtime live debugfs: `live-debugfs-dmesg.txt` contains no
  `M6 DSI lk-handoff[...]` skip line. During `stock_pages`, DSI enters CMD
  mode for DCS reads; after that, BIST is entered from video mode:
  `M6 DSI snapshot[bist-full-pre] ... MODE=0x3`,
  `M6 DSI snapshot[bist-full-post] ... MODE=0x3`,
  `BIST_PATTERN=0xff0000 BIST_CON=0x200446`, and
  `STATE7=0x2020/Video data period`.
- Runtime IRQ proof: `/proc/interrupts` shows `dsi0` at zero before live
  debugfs and nonzero after the live sequence (`62` and `81` on visible CPU
  columns in this capture).
- External 4PDA sanity check:
  `https://4pda.to/forum/index.php?showtopic=583114` search results reinforce
  that PQ/MiraVision is AAL/display processing, while LCM bring-up still hinges
  on stock `lcm_get_params`, DSI timing, LK state, GPIO, and panel command
  parity.

Files changed:
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_dsi.c`: disables the
  stale M6 LK-handoff skip isolation so Linux DSI config/start paths run.
- `BRINGUP_STATE.md`: records artifact identity, runtime verdict, expected
  next marker, rollback condition, and verification commands.
- Capture-local `CAPTURE_VERDICT.md`: records the #66 runtime verdict next to
  the exact evidence bundle.

Why each file changed: `ddp_dsi.c` owns the DSI config/start path that the #65
capture proved was being skipped after a stop/restart. The state files preserve
the tested artifact identity and prevent future agents from treating this
fixed skip as an open physical-display root cause.

Expected next marker: if the physical panel remains black, do not retest OVL
content, SurfaceFlinger, or normal HWC composition. The next marker set should
compare stock LK host-side DSI/MIPITX writes against #66 live state and log any
hidden differences in MIPITX lane drive/settle/ULPS/non-continuous clock,
DSI PHY timing, VM/PS/TXRX, and panel-private post-init state before applying
behavior changes.

Rollback condition: revert only if a fresh capture proves this change causes
no-ADB boot failure, DSI config/start deadlock, loss of stock-pages DCS reads,
loss of BIST latch, or regression from Android `sys.boot_completed=1`.

Verification commands:

```bash
ART=/srv/forge/android/export/meizu_m6_artifacts/20260608-2257-m6-dsi-disable-lk-handoff-skip-bootonly
(cd "$ART" && sha256sum -c SHA256SUMS)
cmp "$ART/Image.gz-dtb" "$ART/verify-unpack/zImage"
cmp "$ART/initrd.img" "$ART/verify-unpack/initrd.img"
CAP=/srv/forge/android/meizu_m6/captures/20260608-2301-m6-dsi-disable-lk-handoff-skip-flash-711HEBSR277K5
sha256sum "$CAP/boot-after.img" "$ART/boot-m6-dsi-disable-lk-handoff-skip-20260608.img"
POST=/srv/forge/android/meizu_m6/captures/20260608-2305-m6-dsi-disable-lk-handoff-skip-postboot-711HEBSR277K5
rg -n 'lk-handoff|bist-full-(pre|post)|MODE=0x3|BIST_CON=0x200446|live_dsi|const_ovl_l0' \
  "$POST/live-debugfs-dmesg.txt" "$POST/m6_mtkfb_early_diag-before-live.txt"
rg -n 'dsi0' "$POST/proc-interrupts-before-live.txt" "$POST/proc-interrupts-after-live.txt"
```

## 2026-06-08 post-#66 remaining display frontiers

FACT: #66 proves userspace, HWC object creation, OVL constant-white content,
MUTEX route programming, RDMA enable/counters, DSI video mode, MIPITX lanes/PLL,
and DSI BIST latch are all observable in the same fresh boot. FACT:
`live-debugfs-dmesg.txt` still shows `CMDQ_EVENT_DISP_RDMA0_EOF` token value
`0`, VSYNC timeouts with `rdma_eof=0 mutex_eof=0`, and route READY dropping
from `0x40009000`/`0x0`/`0x300` while VALID stays `0x4000937a`. INFERENCE:
the normal digital frame-retirement frontier is now RDMA0 EOF / DDP READY
propagation, not framebuffer content, PQ, normal Android composition, or
LCM command reads.

HYPOTHESIS: if the human observes no physical red/white/other visible output
while the DSI BIST markers are latched for a bounded visible window, physical
black is below RDMA/OVL and at DSI host output, MIPITX analog/lane mapping, or
panel HS-video acceptance. If BIST becomes physically visible, return to the
RDMA EOF/DDP READY frontier for the normal pipeline and do not patch MIPITX.

Next DIAGNOSTIC patch should add read-only snapshots at the first trigger and
first RDMA EOF timeout: route VALID/READY, mutex INTSTA/MOD/SOF, RDMA
INTSTA/GLOBAL/counters, OVL STA/INTSTA/FLOW/ROI/layer enable, DSI STATE7,
and MIPITX lane/PLL state. Roll back if boot completion, stock-pages recovery,
DSI BIST latch, or root ADB regresses.

## 2026-06-08 OVL constant-white handoff isolation

PATCH HISTORY, **ISOLATION + DIAGNOSTIC**, 2026-06-08: preserve the existing
early framebuffer `0xff` marker, force one marker-backed scanout trigger, then
perform one additional first-handoff OVL constant-white scanout using
`DISP_BUFFER_ALPHA` plus an M6-only magic key that writes
`OVL_L0_CLR=0xffffffff`. The patch also adds delayed `mtkfb` proof prints so
the next post-boot capture can prove the early action even when the early dmesg
ring has already wrapped. This patch does not change panel DCS init, DSI timing,
MIPITX PLL, panel reset GPIOs, DDP route, PQ, HWC, Android composition policy,
or backlight policy.

Hypothesis: FACT: prior verified captures show the physical LK bootlogo
disappears at the same timeline as the first Linux `mtkfb_set_par()` /
OVL handoff, while Android later reaches `sys.boot_completed=1` and screencap
contains a nonblack 720x1280 RGBA frame. FACT: the simple framebuffer marker
plus forced trigger produced no physical flicker according to the human, but
the runtime marker was not captured because postboot dmesg started too late.
FACT: source audit shows MTK `DISP_BUFFER_ALPHA` maps to
`OVL_LAYER_SOURCE_RESERVED`, but `ddp_ovl.c` normally writes the reserved-layer
color as `0xff000000`, i.e. opaque black. HYPOTHESIS: a constant-white OVL layer
at the first Linux handoff is the narrowest test that bypasses framebuffer
memory, M4U, and userspace content; if `source=reserved`, `LARC=1`, and
`clr=0xffffffff` are proven while the physical panel stays black, the frontier
moves below OVL content to OVL output/RDMA/MUTEX/DSI HS stream/panel HS
acceptance.

Evidence:
- Prior capture:
  `/srv/forge/android/meizu_m6/captures/20260608-1856-m6-lk-handoff-pstore-711HEBSR277K5`.
- Prior bootdiag shows first Linux OVL handoff at `3.501528` with
  `PA=0x9f370000`, `BGRA8888`, and `720x1280`, matching the human-reported
  3-4 second bootlogo drop.
- Prior human observation after
  `boot-m6-mtkfb-white-trigger-20260608.img`: physical bootlogo still
  disappears at about 3.5 seconds and no white flicker is visible.
- Source audit result: `DISP_BUFFER_ALPHA` alone is not a white marker because
  `ddp_ovl.c` wrote `DISP_REG_OVL_L0_CLR` as `0xff000000`.
- Read-only rizin/LK audit result: stock LK contains DDP/OVL/RDMA/DSI route
  writes for `OVL0 -> ... -> RDMA0 -> DSI0`, with OVL register targets around
  `0x1400802c/30/34/38/3c/44/8f40/c8` and route registers
  `0x1400006c/70/74/7c`; no targeted stock-LK display clear/disable before
  kernel jump was found.
- Build log:
  `/srv/forge/android/meizu_m6/kernel-meizu_M6-N-ex6-linux-3.18.140/build-m6-ovl-const-white-20260608.log`,
  ending in `CAT arch/arm64/boot/Image.gz-dtb`.
- Export artifact directory:
  `/srv/forge/android/export/meizu_m6_artifacts/20260608-2133-m6-ovl-const-white-bootonly`.
- Built boot image sha256:
  `834a20f74a79cc59f96c5e380bc07f5f0cd42dc0b9b80b79eda39ea5e8313e7a`.
- Built `Image.gz-dtb` sha256:
  `54d069d950e901dc18dcfcfc24910e76ef9bd66422ed52f5d92de0418df2da10`.
- Built `System.map` sha256:
  `74d7f91751f3c09cdfd9cadee445ec128afb12498b2054206316197a11df5f56`.
- Built `kernel.config` sha256:
  `698b6764b989ef0bab75c0e6d6c291706e6a4a8d527d6a59347ad8c966d1fdd1`.
- Built `initrd.img` sha256:
  `7de975b4485324f4a76eb44fa4cc61472e829421e8d27e31f50d80b984cb4a4f`.
- Build log sha256:
  `17a3b3687d71157a9d20fa5f53e77a4f53beeb59b2b446345ca8249df66205a0`.
- `sha256sum -c SHA256SUMS` passed in the export directory.
- `cmp Image.gz-dtb verify-unpack-zImage` and
  `cmp initrd.img verify-unpack-initrd.img` passed.
- Marker string check against the exact `Image.gz-dtb` includes
  `M6 mtkfb early-diag`, `M6 mtkfb const-white-trigger`,
  `M6 mtkfb fb-marker-trigger`, `M6 mtkfb fb-marker`, and the extended
  `M6 OVL diag cfg` line with `larc` and `clr`.
- Flash attempt capture:
  `/srv/forge/android/meizu_m6/captures/20260608-2136-m6-ovl-const-white-flash-711HEBSR277K5`.
  The flash did not start because reverse ADB ports `15037` and `15038` were
  not listening; local `adb -P 5037 devices` only showed non-M6 serial
  `30785d1a`. There is no runtime verdict for this artifact yet.

Files changed:
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/mtkfb.c`: adds delayed
  proof state/reporting for the early framebuffer marker and performs one
  marker-backed constant-white OVL config/trigger after the first FB scanout
  trigger.
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_ovl.c`: adds an
  M6-only magic-key check for reserved-source layers so the diagnostic layer
  writes `OVL_L0_CLR=0xffffffff`, and extends OVL diagnostics with `larc` and
  `clr`.
- `BRINGUP_STATE.md`: records the hypothesis, evidence, build identity,
  expected observation, rollback condition, and verification commands.

Why each file changed: `mtkfb.c` owns the first Linux framebuffer/OVL handoff
that matches the physical bootlogo disappearance. `ddp_ovl.c` is the only
place that programs the constant-color register for reserved OVL layers, and
the stock black constant color would otherwise make a constant-layer test
indistinguishable from the current black-screen symptom. The state file
preserves the artifact identity and makes clear that runtime flash/capture is
still pending.

Expected next marker: after flashing
`boot-m6-ovl-const-white-20260608.img`, postboot dmesg should contain delayed
`M6 mtkfb early-diag[...]` with `filled=1`, `fb_trigger=1`,
`const_attempt=1`, and `const_trigger=1`, plus `M6 mtkfb const-white-trigger`
and an `M6 OVL diag cfg` line for the tagged layer with `source=1`, `larc=1`,
`key=0/0x6d3657`, and `clr=0xffffffff`. If the physical LCD turns white, the
next fix should focus on framebuffer/M4U/content/format. If those proof markers
are present and the physical LCD stays black without flicker, stop patching
framebuffer content and move below OVL content to RDMA/MUTEX/DSI HS stream and
MIPITX/panel HS acceptance.

Rollback condition: revert this isolation patch if it prevents boot, breaks
`sys.boot_completed=1`, fails to produce the constant-white proof markers,
changes ordinary non-tagged dim layers, triggers a display/CMDQ deadlock, or
if the physical constant-white marker becomes visible and the next work pivots
to content/M4U instead.

Verification commands:

```bash
ART=/srv/forge/android/export/meizu_m6_artifacts/20260608-2133-m6-ovl-const-white-bootonly
(cd "$ART" && sha256sum -c SHA256SUMS)
cmp "$ART/Image.gz-dtb" "$ART/verify-unpack-zImage"
cmp "$ART/initrd.img" "$ART/verify-unpack-initrd.img"
gzip -cd "$ART/Image.gz-dtb" 2>/tmp/m6-ovl-const-white-gzip.err | strings \
  | rg 'M6 mtkfb early-diag|M6 mtkfb const-white-trigger|M6 OVL diag cfg|clr=0x|fb-marker'
A='adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5'
$A reboot recovery
$A push "$ART/boot-m6-ovl-const-white-20260608.img" /tmp/m6-boot.img
$A shell 'dd if=/tmp/m6-boot.img of=/dev/block/platform/mtk-msdc.0/11230000.msdc0/by-name/boot bs=1048576 conv=fsync; sync'
$A shell 'dd if=/dev/block/platform/mtk-msdc.0/11230000.msdc0/by-name/boot of=/tmp/boot-after.img bs=1048576 count=16; sha256sum /tmp/boot-after.img /tmp/m6-boot.img'
$A reboot
$A wait-for-device
$A shell 'sleep 20; dmesg | grep -E "M6 mtkfb early-diag|M6 mtkfb const-white-trigger|M6 OVL diag cfg.*(clr=0xffffffff|key=0/0x6d3657)|mtkfb_probe|mtkfb_init"'
```

## 2026-06-08 mtkfb early framebuffer white-marker forced trigger

PATCH HISTORY, **ISOLATION + DIAGNOSTIC**, 2026-06-08: keep the existing
early Linux framebuffer `0xff` marker, and force exactly one blocking
`primary_display_trigger(1, NULL, 0)` after the first marker-backed
`mtkfb_set_par()` config. This patch does not change DSI timing, MIPITX
registers, LCM commands, panel reset, DDP route, PQ, HWC, Android composition,
backlight policy, or later SurfaceFlinger buffers.

Hypothesis: FACT: source review shows `mtkfb_fbinfo_init()` calls
`mtkfb_set_par()` but does not call `init_framebuffer()`, so the white marker
should still be present when the first OVL input is configured. FACT: source
review also shows `primary_display_config_input_multiple()` reaches
`primary_frame_cfg_input()` / `_config_ovl_input()` but does not itself call
`primary_display_trigger()`. HYPOTHESIS: the previous early white-marker image
could leave the marker in framebuffer memory without forcing the first OVL
config through the active video path soon enough for a human-visible test; a
single bounded blocking trigger after the marker-backed `mtkfb_set_par()` makes
the negative physical result meaningful.

Evidence:
- Source file:
  `kernel-3.18/drivers/misc/mediatek/video/mt6755/mtkfb.c`.
- Prior capture:
  `/srv/forge/android/meizu_m6/captures/20260608-1856-m6-lk-handoff-pstore-711HEBSR277K5`.
- Prior bootdiag shows first Linux OVL handoff at `3.501528` with
  `PA=0x9f370000`, `BGRA8888`, and `720x1280`, matching the human-reported
  3-4 second bootlogo drop.
- Build log:
  `/srv/forge/android/meizu_m6/kernel-meizu_M6-N-ex6-linux-3.18.140/build-m6-mtkfb-white-trigger-20260608.log`,
  ending in `CAT arch/arm64/boot/Image.gz-dtb`.
- Export artifact directory:
  `/srv/forge/android/export/meizu_m6_artifacts/20260608-1959-m6-mtkfb-white-trigger-bootonly`.
- Built boot image sha256:
  `f119d8dfe6420e05e4f292d8ac6c942358a793b814f87cb473867b022db1444a`.
- Built `Image.gz-dtb` sha256:
  `b8be6edb74f0df8e467f3b0d3e25e29675ae7fb8c5d18b5abe98dfecf9fba918`.
- Built `System.map` sha256:
  `e7ea065ac9eb893262bdb405edee8bcd3a2ed61de49f107b4be28263ed0c98d7`.
- Built `kernel.config` sha256:
  `698b6764b989ef0bab75c0e6d6c291706e6a4a8d527d6a59347ad8c966d1fdd1`.
- Build log sha256:
  `4ca6ce111139affba908efb42d1beb5c0473f4080b11d1f38eed2f8319b736e0`.
- `sha256sum -c SHA256SUMS` passed in the export directory.
- `cmp Image.gz-dtb verify-unpack/zImage` and
  `cmp initrd.img verify-unpack/initrd.img` passed.
- Marker string check against the exact `Image.gz-dtb` includes
  `M6 mtkfb fb-marker`, `M6 mtkfb fb-marker-trigger`,
  `pre-fbinfo-set-par`, and the LK-handoff markers.

Files changed:
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/mtkfb.c`: adds a pending
  flag set by `m6_mtkfb_fill_early_marker()` and performs one blocking
  `primary_display_trigger()` after the first marker-backed `mtkfb_set_par()`,
  logging `cfg_ret`, `trigger_ret`, physical base, format, pitch, and size.
- `BRINGUP_STATE.md`: records the hypothesis, evidence, artifact identity,
  expected observation, rollback condition, and verification commands.

Why each file changed: `mtkfb.c` owns the first Linux framebuffer takeover that
matches the physical bootlogo disappearance. The forced trigger makes the
existing white marker a real scanout test rather than only a memory-write
test. The state file preserves the artifact identity and stop conditions.

Expected next marker: after flashing
`boot-m6-mtkfb-white-trigger-20260608.img`, bootdiag dmesg should contain
`M6 mtkfb fb-marker[pre-fbinfo-set-par]` followed by
`M6 mtkfb fb-marker-trigger` and the first `M6 OVL handoff[1:pre-dpmgr]`.
If the physical LCD turns white/light, the next fix should focus on
framebuffer/M4U/content/format/handoff. If it stays black with those markers
present and boot readback matching this image, the next diagnostic should be
OVL constant-color or a lower OVL/RDMA/DSI handoff proof, not another
framebuffer fill.

Rollback condition: revert this isolation patch after the visual test, or
immediately if it prevents ADB boot, breaks `sys.boot_completed=1`, changes
boot image identity unexpectedly, triggers a display/CMDQ deadlock before
`M6 mtkfb fb-marker-trigger`, or makes the device unusable.

Verification commands:

```bash
ART=/srv/forge/android/export/meizu_m6_artifacts/20260608-1959-m6-mtkfb-white-trigger-bootonly
(cd "$ART" && sha256sum -c SHA256SUMS)
cmp "$ART/Image.gz-dtb" "$ART/verify-unpack/zImage"
cmp "$ART/initrd.img" "$ART/verify-unpack/initrd.img"
gzip -cd "$ART/Image.gz-dtb" 2>/tmp/m6-mtkfb-white-trigger-gzip.err | strings \
  | rg 'M6 mtkfb fb-marker|M6 mtkfb fb-marker-trigger|pre-fbinfo-set-par'
A='adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5'
$A shell 'uname -a; getprop sys.boot_completed; sha256sum /dev/block/platform/mtk-msdc.0/11230000.msdc0/by-name/boot'
$A shell 'BOOTDIAG=$(ls -d /cache/bootdiag/run-* 2>/dev/null | tail -1); grep -E "M6 mtkfb fb-marker|M6 mtkfb fb-marker-trigger|M6 OVL handoff\\[1:pre-dpmgr\\]|mtkfb_probe|mtkfb_init" "$BOOTDIAG/cmd/dmesg.txt"'
```

## 2026-06-08 mtkfb early framebuffer white-marker isolation

PATCH HISTORY, **DIAGNOSTIC**, 2026-06-08: fill the Linux framebuffer mapping
with `0xff` before the first `mtkfb_fbinfo_init()` / `mtkfb_set_par()` handoff.
This patch does not change DSI timing, MIPITX registers, LCM commands, panel
reset, DDP route, PQ, HWC, Android composition, backlight policy, or later
SurfaceFlinger buffers.

Hypothesis: FACT: the LK-handoff skip image proved that first Linux DSI
config/start was skipped at `1.633820` / `1.635620` seconds while DSI/MIPITX
stayed in video data period. FACT: the same fresh bootdiag shows the next
strong physical-timing boundary at `3.501528` seconds:
`mtkfb_probe -> mtkfb_fbinfo_init -> mtkfb_set_par ->
primary_display_config_input_multiple`, which programs OVL0 from the Linux
framebuffer at `PA=0x9f370000`, `BGRA8888`, pitch `2944`. FACT: the human
reports the LK bootlogo disappears at about 3-4 seconds. HYPOTHESIS: Linux may
be replacing the visible LK bootlogo with a black/empty framebuffer layer at
the `mtkfb_set_par()` takeover rather than losing the DSI physical link at the
earlier DSI config/start boundary.

Evidence:
- Pstore/bootdiag capture:
  `/srv/forge/android/meizu_m6/captures/20260608-1856-m6-lk-handoff-pstore-711HEBSR277K5`.
- In that capture, `bootdiag/cmd__dmesg.txt` contains
  `M6 DSI lk-handoff[config]` at `1.633820`,
  `M6 DSI lk-handoff[start]` at `1.635620`, and the first mtkfb framebuffer
  takeover at `3.501528`.
- The same dmesg shows `M6 OVL handoff[1:pre-dpmgr]` with
  `input phy=000000009f370000`, `base=ffffff8000500000`, `pitch_px=736`,
  `src_wh=720/1280`, and `ovl_fmt=BGRA8888/0xc00809`.
- Build log:
  `/srv/forge/android/meizu_m6/kernel-meizu_M6-N-ex6-linux-3.18.140/build-m6-mtkfb-early-white-marker-20260608.log`,
  ending in `CAT arch/arm64/boot/Image.gz-dtb`.
- Export artifact directory:
  `/srv/forge/android/export/meizu_m6_artifacts/20260608-2045-m6-mtkfb-early-white-marker-bootonly`.
- Built boot image sha256:
  `5dc85549fc47961e132e525207d4bf9c65c5cc1efeb7a58eaece2ff66a666906`.
- Built `Image.gz-dtb` sha256:
  `193fdc13b17b75823819b69c1fd61af410e518e713e559307fbaeb09f1183acf`.
- Built `System.map` sha256:
  `da38c60880c902715d3cda188a7da0c5e4de91fd52bb71309c5980fee734abaa`.
- Build log sha256:
  `8fc659db49ecc1342aeccc2f3fdced0566c8b1145dfc08af70d682e38b7931b7`.
- `sha256sum -c SHA256SUMS` passed in the export directory.
- `cmp Image.gz-dtb verify-unpack/zImage` and
  `cmp initrd.img verify-unpack/initrd.img` passed.
- Marker string check against the exact `Image.gz-dtb` includes
  `M6 mtkfb fb-marker` and `pre-fbinfo-set-par`.

Files changed:
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/mtkfb.c`: adds
  `m6_mtkfb_fill_early_marker()` and calls it before `mtkfb_fbinfo_init()` to
  fill the mapped framebuffer with visible `0xff` bytes and log sample words.
- `BRINGUP_STATE.md`: records the hypothesis, evidence, artifact identity,
  expected observation, rollback condition, and verification commands.

Why each file changed: `mtkfb.c` owns the first Linux framebuffer takeover that
matches the 3-4 second physical bootlogo drop. Filling exactly that framebuffer
before it is configured into OVL is the narrowest diagnostic for separating
black FB content from lower DDP/DSI/panel failure.

Expected next marker: after flashing
`boot-m6-mtkfb-early-white-marker-20260608.img`, bootdiag dmesg should contain
`M6 mtkfb fb-marker[pre-fbinfo-set-par]` before the first `M6 OVL
handoff[1:pre-dpmgr]`. Human-visible observation is decisive: if the physical
LCD turns white/light when the LK bootlogo disappears, the DSI physical path is
able to display Linux OVL content and the next fix should focus on framebuffer
content/format/initial handoff. If the LCD stays black despite the marker log
and OVL pointing at the filled framebuffer, move below content to OVL/RDMA/DSI
takeover, output mux, or panel stream acceptance after LK.

Rollback condition: revert this diagnostic patch after the visual test, or
immediately if it prevents ADB boot, breaks `sys.boot_completed=1`, changes
boot image identity unexpectedly, corrupts framebuffer memory outside
`MTK_FB_SIZEV`, or hides a lower DSI/DDP failure by leaving the device unusable.

Verification commands:

```bash
ART=/srv/forge/android/export/meizu_m6_artifacts/20260608-2045-m6-mtkfb-early-white-marker-bootonly
(cd "$ART" && sha256sum -c SHA256SUMS)
cmp "$ART/Image.gz-dtb" "$ART/verify-unpack/zImage"
cmp "$ART/initrd.img" "$ART/verify-unpack/initrd.img"
gzip -cd "$ART/Image.gz-dtb" 2>/tmp/m6-mtkfb-marker-gzip.err | strings \
  | rg 'M6 mtkfb fb-marker|pre-fbinfo-set-par'
A='adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5'
$A shell 'uname -a; getprop sys.boot_completed; sha256sum /dev/block/platform/mtk-msdc.0/11230000.msdc0/by-name/boot'
$A shell 'BOOTDIAG=$(ls -d /cache/bootdiag/run-* 2>/dev/null | tail -1); grep -E "M6 mtkfb fb-marker|M6 OVL handoff\\[1:pre-dpmgr\\]|mtkfb_probe|mtkfb_init" "$BOOTDIAG/cmd/dmesg.txt"'
```

## 2026-06-08 LK handoff DSI config/start skip isolation

PATCH HISTORY, **ISOLATION + DIAGNOSTIC**, 2026-06-08: skip the first
Linux-side DSI config/start when LK already left the panel and MIPITX enabled.
This patch is intentionally narrow: it does not change the LCM init table,
panel reset GPIOs, DDP route, OVL/RDMA/HWC/PQ, BIST controls, PLL math, porch
values, lane count, backlight, or userspace composition.

Hypothesis: FACT: the human reports that the physical bootlogo is visible and
then disappears around 3-4 seconds, before ADB. FACT: the current post-boot
captures prove Android composition is nonblack in screencap, backlight is
nonzero, DSI/MIPITX lanes are active, public LP DCS reads still work, and DSI
BIST profiles latch without physical flicker. FACT: `/proc/bootprof` from the
fresh capture places `mtkfb_probe` at about `4376.877624 ms` and `mtkfb_init`
at about `4392.804855 ms`, matching the observed bootlogo drop. HYPOTHESIS:
the earliest destructive boundary is Linux display handoff reprogramming DSI
or starting the DSI path over the working LK state; preserving LK DSI state on
the first config/start should keep the bootlogo alive if that boundary is the
culprit.

Evidence:
- Fresh bootlogo-drop capture:
  `/srv/forge/android/meizu_m6/captures/20260608-1952-m6-bootlogo-drop-711HEBSR277K5`.
- `bootprof-cmdline.txt` in that capture shows `mtkfb_probe` at
  `4376.877624 ms` and `mtkfb_init` at `4392.804855 ms`.
- Boot image during that capture matched sha256
  `ca4063ce29f4d1bca3f23dd1fa38e343092b2852f6063d00a44b2beb026bd69a`.
- Post-boot screencap in that capture is valid/nonblack while the physical
  display is black.
- Earlier route/content verdict:
  `/srv/forge/android/meizu_m6/captures/20260608-1759-m6-display-route-probe-711HEBSR277K5/CAPTURE_VERDICT.md`.
- Earlier BIST profile sweep verdict:
  `/srv/forge/android/meizu_m6/captures/20260608-1915-m6-dsi-bist-profile-sweep-711HEBSR277K5/CAPTURE_VERDICT.md`.
- Build log:
  `/srv/forge/android/meizu_m6/kernel-meizu_M6-N-ex6-linux-3.18.140/build-m6-dsi-lk-handoff-skip-20260608.log`,
  ending in `CAT arch/arm64/boot/Image.gz-dtb`.
- Export artifact directory:
  `/srv/forge/android/export/meizu_m6_artifacts/20260608-2005-m6-dsi-lk-handoff-skip-bootonly`.
- Built boot image sha256:
  `6f8233c04041aec537df8a5c0f68f41c6bf9e75878b9c4ade62c04c91f44f8e9`.
- Built `Image.gz-dtb` sha256:
  `e53c33a59b2beaa661908f56c44b99632af1d3d654548c328f2933a913d24d56`.
- Built `System.map` sha256:
  `01a3f7ac3a8f8448efd9bb7fc1e18475851207e34c204d28bc90f8acdfa6271e`.
- `sha256sum -c SHA256SUMS` passed in the export directory.
- `cmp Image.gz-dtb verify-unpack/zImage` and
  `cmp initrd.img verify-unpack/initrd.img` passed.
- Marker string check against the exact `Image.gz-dtb` includes
  `M6 DSI lk-handoff[config]`, `lk-handoff-config-skip`,
  `M6 DSI lk-handoff[start]`, and `lk-handoff-start-skip`.

Files changed:
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_dsi.c`: adds
  `M6_LK_HANDOFF_SKIP_FIRST_DSI_CONFIG` and skips the first DSI config/start
  when `PMaster_enable == 0` and `dsi_force_config == 0`, logging snapshots at
  both skip points.
- `BRINGUP_STATE.md`: records the handoff hypothesis, artifact identity,
  expected observation, rollback condition, and verification commands.

Why each file changed: `ddp_dsi.c` owns the Linux DSI config/start boundary
that can overwrite a panel state proven working in LK. Skipping exactly that
first boundary is the narrowest non-destructive isolation for the new human
observation; the state file preserves the evidence chain and rollback rule.

Expected next marker: after flashing
`boot-m6-dsi-lk-handoff-skip-20260608.img`, dmesg should contain
`M6 DSI lk-handoff[config]` and/or `M6 DSI lk-handoff[start]` before Android
boot completion. The human-visible observation is decisive: if the LK bootlogo
does not disappear at 3-4 seconds, re-enable DSI config/start pieces one at a
time to find the destructive register group. If the bootlogo still disappears,
move earlier than DSI config/start to `dpmgr_path_init`, display power/reset,
mutex/module reset, or primary-display path initialization.

Rollback condition: revert this isolation patch if it prevents ADB boot,
breaks `sys.boot_completed=1`, leaves Android composition disabled, regresses
nonblack screencap, triggers DSI/CMDQ timeouts, or if the bootlogo still
disappears before the skip markers can prove DSI config/start is the earliest
destructive boundary.

Verification commands:

```bash
ART=/srv/forge/android/export/meizu_m6_artifacts/20260608-2005-m6-dsi-lk-handoff-skip-bootonly
(cd "$ART" && sha256sum -c SHA256SUMS)
cmp "$ART/Image.gz-dtb" "$ART/verify-unpack/zImage"
cmp "$ART/initrd.img" "$ART/verify-unpack/initrd.img"
gzip -cd "$ART/Image.gz-dtb" 2>/tmp/m6-dsi-lk-handoff-gzip.err | strings \
  | rg 'M6 DSI lk-handoff|lk-handoff-config-skip|lk-handoff-start-skip'
A='adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5'
$A shell 'uname -a; getprop sys.boot_completed; dmesg | grep -E "M6 DSI lk-handoff|lk-handoff-(config|start)-skip|primary_display_init begin|mtkfb_probe"'
```

## 2026-06-08 DSI PHY LK-delay parity isolation

PATCH HISTORY, **ISOLATION + DIAGNOSTIC**, 2026-06-08: change only the MIPITX
analog settle waits in `DSI_PHY_clk_setting()` to LK-like values and log the
MIPITX state around each wait. This patch does not change panel init commands,
PLL math, porch values, lane count, PHY map, DSI mode, DDP route, PQ, HWC,
OVL, RDMA, or BIST register profiles.

Hypothesis: FACT: the profile sweep below proves alternate DSI BIST profile
selection does not make the physical LCD flicker, while Android composition,
DSI video/BIST registers, lane/FSM decode, nonblack screencap, and LP DCS
reads remain alive. FACT: stock LK reverse found a concrete low-level delta in
the MIPITX/DSI PHY setup sequence: LK waits about `0x1e` after BG enable,
`0x14` after PLL enable, and `0xc8` after PCW_CHG plus pad-low release, while
Linux used `mdelay(1)` at the comparable settle points. HYPOTHESIS: the panel
may accept LP reads but reject the HS video stream because Linux starts HS
traffic before the MIPITX analog/PLL/pad path has settled the way stock LK
does.

Evidence:
- Profile-sweep capture verdict:
  `/srv/forge/android/meizu_m6/captures/20260608-1915-m6-dsi-bist-profile-sweep-711HEBSR277K5/CAPTURE_VERDICT.md`.
- Stock reverse notes:
  `/srv/forge/android/meizu_m6/captures/20260530-stock-lk-boot-reverse-inputs/m6_lowlevel_dsi_mipitx_parity_20260608.md`.
- Source lines patched: `DSI_PHY_clk_setting()` in
  `kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_dsi.c`.
- Build log:
  `/srv/forge/android/meizu_m6/kernel-meizu_M6-N-ex6-linux-3.18.140/build-m6-dsi-phy-lk-delay-20260608.log`,
  ending in `CAT arch/arm64/boot/Image.gz-dtb`.
- Export artifact directory:
  `/srv/forge/android/export/meizu_m6_artifacts/20260608-1935-m6-dsi-phy-lk-delay-bootonly`.
- Built boot image sha256:
  `ca4063ce29f4d1bca3f23dd1fa38e343092b2852f6063d00a44b2beb026bd69a`.
- Built `Image.gz-dtb` sha256:
  `d66c958fa35337cd7e6451b162f64a3866de13f7b4582690b075291083bb584a`.
- Built `System.map` sha256:
  `c1faceed5504ab1d4c34596af3bc2622c50bace0aab0d48e188cf3c725f64fb7`.
- Build log sha256:
  `b1f18536defb436b539e99fc652bdf02dcc004a25e2e6594f4d6362b10ce8c7a`.
- `sha256sum -c SHA256SUMS` passed in the export directory.
- `cmp Image.gz-dtb verify-unpack/zImage` and
  `cmp initrd.img verify-unpack/initrd.img` passed.
- Marker string check against the exact `Image.gz-dtb` includes
  `M6 DSI physeq[%s]: lk-delay`, `phy-clk-bg-settle`,
  `phy-clk-pll-en-settle`, and `phy-clk-pcw-pad-settle`.

Files changed:
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_dsi.c`: adds
  LK-delay constants and `M6 DSI physeq[...]` markers, replaces the BG settle
  wait with 30 ms, the PLL-enable settle wait with 20 ms, and the PCW/pad-low
  settle wait with 200 ms.
- `BRINGUP_STATE.md`: records hypothesis, evidence, expected next markers,
  rollback condition, and verification commands.
- `captures/20260608-1915-m6-dsi-bist-profile-sweep-711HEBSR277K5/CAPTURE_VERDICT.md`:
  records the profile sweep result that justified moving below BIST profile
  selection.

Why each file changed: `ddp_dsi.c` owns the MIPITX analog/PLL/pad enable
sequence and is the narrow owner for this LK-parity isolation. The state and
capture verdict files preserve the evidence chain so future agents do not
reopen PQ/HWC/RDMA/BIST-profile hypotheses without contradictory fresh logs.

Expected next marker: after flashing
`boot-m6-dsi-phy-lk-delay-20260608.img`, boot dmesg should contain
`M6 DSI physeq[phy-clk-bg-settle]`, `phy-clk-pll-en-settle`, and
`phy-clk-pcw-pad-settle` begin/end pairs with the same final PLL/lane/PHY map
as the prior image, followed by `sys.boot_completed=1`. If physical LCD shows
anything, keep this patch and narrow the exact wait. If physical LCD stays
black while markers prove LK-like waits ran and DCS/BIST still work, move to
stock-hidden MIPITX register side effects, lane electrical polarity/swap not
visible in `PHY_SEL`, or panel-side HS acceptance.

Rollback condition: revert this isolation patch if it prevents ADB boot,
breaks `sys.boot_completed=1`, changes final lane/PLL values unexpectedly,
regresses nonblack screencap, breaks LP DCS reads, or creates new DSI/CMDQ
timeouts before producing all three `physeq` markers.

Verification commands:

```bash
ART=/srv/forge/android/export/meizu_m6_artifacts/20260608-1935-m6-dsi-phy-lk-delay-bootonly
(cd "$ART" && sha256sum -c SHA256SUMS)
cmp "$ART/Image.gz-dtb" "$ART/verify-unpack/zImage"
cmp "$ART/initrd.img" "$ART/verify-unpack/initrd.img"
gzip -cd "$ART/Image.gz-dtb" 2>/tmp/m6-dsi-phy-lk-delay-gzip.err | strings \
  | rg 'M6 DSI physeq\[|phy-clk-bg-settle|phy-clk-pll-en-settle|phy-clk-pcw-pad-settle'
A='adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5'
$A shell 'uname -a; getprop sys.boot_completed; dmesg | grep -E "M6 DSI physeq\\[|M6 DSI phydecode\\[phy-clk|M6 DSI rtcal\\[phy-clk-after|M6 DSI snapshot\\[start-after"'
```

## 2026-06-08 DSI BIST profile / lane-FSM diagnostic

PATCH HISTORY, **ISOLATION + DIAGNOSTIC**, 2026-06-08: add manual
`m6_dsi_bist_profile:<profile>:<rgb>[:hold_ms]` debugfs control and richer
DSI state decode. The command is manual-only, writes only DSI BIST registers,
holds the selected pattern for a bounded time, logs forced DSI/MIPITX/FSM
snapshots, then disables BIST before returning. It does not change boot-time
panel init, DSI timing, PLL, lanes, DDP route, PQ, HWC, OVL, RDMA, backlight,
or public/private DCS tables.

Hypothesis: FACT: verified capture
`/srv/forge/android/meizu_m6/captures/20260608-1816-m6-dsi-bist-physical-marker-711HEBSR277K5`
shows `m6_dsi_bist_full` setting `BIST_CON=0x200446`, `self_pat=1`,
`bist_en=1`, `fix=1`, `lane=4`, active DSI video period, and active MIPITX
lanes, but the human reported no physical flicker. FACT: stock reverse says
visible DSI params, visible init table, and ordinary runtime DSI state mostly
match, leaving BIST/HS-video/PHY acceptance as the open layer. HYPOTHESIS: the
current `BIST_CON=0x200446` profile may not be the physical self-test profile
on this SoC; toggling `BIST_MODE`, `BIST_HS_FREE`, and legacy `SELF_PAT_MODE`
profiles can distinguish "wrong BIST profile" from "panel/PHY still invisible
even when alternate BIST modes are driven".

Evidence:
- Current physical-BIST capture:
  `/srv/forge/android/meizu_m6/captures/20260608-1816-m6-dsi-bist-physical-marker-711HEBSR277K5`.
- Route/content capture verdict:
  `/srv/forge/android/meizu_m6/captures/20260608-1759-m6-display-route-probe-711HEBSR277K5/CAPTURE_VERDICT.md`.
- Stock reverse:
  `/srv/forge/android/meizu_m6/captures/20260530-stock-lk-boot-reverse-inputs/lk_display_reverse.md`.
- Low-level parity note:
  `/srv/forge/android/meizu_m6/captures/20260530-stock-lk-boot-reverse-inputs/m6_lowlevel_dsi_mipitx_parity_20260608.md`.
- Build log:
  `/srv/forge/android/meizu_m6/kernel-meizu_M6-N-ex6-linux-3.18.140/build-m6-dsi-bist-profile-diag-20260608.log`,
  ending in `CAT arch/arm64/boot/Image.gz-dtb`.
- Export artifact directory:
  `/srv/forge/android/export/meizu_m6_artifacts/20260608-1905-m6-dsi-bist-profile-bootonly`.
- Built boot image sha256:
  `466cfde97f6b9e69d33456e472e41a0a74c526d1a73a1fd85f5ec5d48176995c`.
- Built `Image.gz-dtb` sha256:
  `c4f469b2caf4d0cef420e8ef100efb6674fadc5b6dcf5f36a9052388392e5f36`.
- Built `System.map` sha256:
  `0b096ac5e465b05f3e0fb726f012624a0ded229785fa3cc445f11795ce16f2a1`.
- Built `kernel.config` sha256:
  `698b6764b989ef0bab75c0e6d6c291706e6a4a8d527d6a59347ad8c966d1fdd1`.
- Build log sha256:
  `ef9419b354919c7e27a8cfe27778260c3569f9da478b9ca9b9131a4afb742f28`.
- `sha256sum -c SHA256SUMS` passed in the export directory. `cmp Image.gz-dtb
  verify-unpack/zImage` and `cmp initrd.img verify-unpack/initrd.img` passed.
- Marker string check against the exact `Image.gz-dtb` includes
  `m6_dsi_bist_profile`, `M6 DSI bist_profile`, `M6 DSI state_decode`, and
  `M6 DSI phydecode[%s]: dbg_out=0x%x apb_async=0x%x`.

Files changed:
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_dsi.c`: adds BIST
  profile construction, bounded manual hold/disable, and DBG0-5 / STATE6-9
  decode markers.
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_dsi.h`: exports the
  profile-test helper to debugfs.
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/disp_debug.c`: exposes the
  `m6_dsi_bist_profile` command and help text.
- `BRINGUP_STATE.md`: records the category, evidence, expected markers,
  rollback, and verification commands.

Why each file changed: `ddp_dsi.c` owns BIST_CON, DSI state registers, and
MIPITX register snapshots, so it is the narrow owner for testing alternative
BIST profiles and decoding lane FSM state. `disp_debug.c` is the existing
manual root-triggered display command surface. The state file is the durable
anti-repeat record required for the next capture.

Expected next marker: after flashing this diagnostic boot, a manual sweep like
profiles `0..5` should print `M6 DSI bist_profile: begin`, the raw BIST_CON
value, repeated `M6 DSI state_decode[...]` lines, and
`bist-profile-post-disable` for each profile. If one profile physically
flickers, the previous `m6_dsi_bist_full` result is demoted and the next patch
should use that profile as the physical-path sentinel. If no profile flickers
while state decode shows HS/video/lane state remains active, do not reopen
PQ/HWC/RDMA/OVL; continue with stock-hidden PHY side effects or panel-side
electrical acceptance.

Rollback condition: revert this isolation patch if the profile command hangs
debugfs, fails to auto-disable BIST, leaves the panel in a persistent test
pattern, regresses `sys.boot_completed=1`, breaks the previously working
nonblack screencap, or makes DSI/DCS reads time out after a profile sweep.

Verification commands:

```bash
cd /srv/forge/android/meizu_m6/kernel-meizu_M6-N-ex6-linux-3.18.140
git diff --check -- \
  kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_dsi.c \
  kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_dsi.h \
  kernel-3.18/drivers/misc/mediatek/video/mt6755/disp_debug.c \
  BRINGUP_STATE.md
env CCACHE_DIR=/srv/forge/android/ccache make -C kernel-3.18 \
  O=/srv/forge/work/m6-source-kernel-manual-20260520/out \
  ARCH=arm64 \
  CROSS_COMPILE=/srv/forge/android/meizu_m6/rom-meizu_M6-lineage-cm-14.1/prebuilts/gcc/linux-x86/aarch64/aarch64-linux-android-4.9/bin/aarch64-linux-android- \
  -j8 Image.gz-dtb
A='adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5'
$A shell 'dmesg -C; svc power stayon true; input keyevent 224; settings put system screen_brightness 255; echo 255 > /sys/class/leds/lcd-backlight/brightness; for p in 0 1 2 3 4 5; do echo m6_dsi_bist_profile:$p:0x00ff0000:2500 > /d/mtkfb; sleep 1; done; dmesg' \
  | rg 'M6 DSI bist_profile|M6 DSI state_decode|BIST_CON|bist-profile-post-disable'
```

Runtime capture result, 2026-06-08:

- Capture:
  `/srv/forge/android/meizu_m6/captures/20260608-1915-m6-dsi-bist-profile-sweep-711HEBSR277K5`.
- Capture verdict:
  `/srv/forge/android/meizu_m6/captures/20260608-1915-m6-dsi-bist-profile-sweep-711HEBSR277K5/CAPTURE_VERDICT.md`.
- FACT: `sha256sum -c SHA256SUMS` passes inside the capture directory.
- FACT: the capture ran on boot image
  `466cfde97f6b9e69d33456e472e41a0a74c526d1a73a1fd85f5ec5d48176995c`,
  matching the diagnostic artifact above.
- FACT: profiles 0, 1, 2, 3, and 5 latched `bist_en=1` with `BIST_CON`
  values `0x200446`, `0x200447`, `0x200456`, `0x200457`, and `0x200417`;
  profile 4 was the legacy/self-pattern variant `0x200040` with `bist_en=0`.
- FACT: active profile markers show DSI lane/FSM state in video/HS operation
  with moving word counters; each profile auto-disable returned `BIST_CON=0x0`.
- FACT: post-sweep screencap stayed valid 720x1280 RGBA and nonblack
  (`identify` mean around `0.296` per RGB channel), so logical Android content
  is alive after the test.
- FACT: post-sweep `m6_dsi_dcs_status:stock_pages` completed with
  `M6 LCM stock_pages: read end`, so the LP DCS/control path still works.
- FACT: human observation during the sweep: no physical flicker at all.

INFERENCE: alternate DSI BIST profile selection is not the missing piece. The
earliest open display frontier is now below Android composition, DDP routing,
OVL/RDMA scanout, DSI BIST register programming, and public LP DCS reads.
Continue with LK-like MIPITX/DSI PHY analog settle parity or another
stock-hidden MIPITX/panel-side HS acceptance test. Do not reopen PQ, HWC,
RDMA, OVL, backlight, page5_2a, public DCS, or route validity unless a fresh
capture contradicts the facts above.

## 2026-06-08 Display route-probe diagnostic

PATCH HISTORY, **DIAGNOSTIC**, 2026-06-08: add manual
`m6_display_route_probe[:dump|trigger|rekick|mask]` debugfs instrumentation to
verify whether the nonzero-brightness black image is actually reaching the
OVL/RDMA/DSI scanout route. Default `trigger` runs one normal non-blocking
primary trigger under the primary display lock and snapshots OVL request, DDP
route/RDMA, DSI, and backlight before/after. `dump` is read-only. `rekick`
also rebuilds/restarts the existing trigger loop and is manual-only isolation.

Hypothesis: FACT: capture
`/srv/forge/android/meizu_m6/captures/20260608-173253-m6-dsi-bist-route-kernelonly-711HEBSR277K5`
was collected from verified boot image
`f1f4291b385277cf7a7e51ddd5b96c9b31e5446878213d89f291daf94f9bb9a1`, Android
had `sys.boot_completed=1`, and the backlight cache was nonzero/high. FACT:
DSI self-pattern programming reached `BIST_CON=0x200446 self_pat=1 bist_en=1
fix=1 lane=4`, but the ordinary route stayed `VALID=0x0`, RDMA counters stayed
`0/0`, and OVL0 kept reporting abnormal SOF. HYPOTHESIS: the remaining gap is
whether the real UI buffer is latched and scanned after userspace or
stock-pages/BIST interactions; manual trigger/rekick probes should prove
whether route valid/counters can recover or whether the frontier remains below
OVL/RDMA trigger.

Evidence:
- Runtime capture:
  `/srv/forge/android/meizu_m6/captures/20260608-173253-m6-dsi-bist-route-kernelonly-711HEBSR277K5`.
- `identity-before-bist.txt`: `sys.boot_completed=1`, kernel
  `Linux localhost 3.18.140 #55 SMP PREEMPT Mon Jun 8 13:18:20 CDT 2026`,
  and boot partition sha256
  `f1f4291b385277cf7a7e51ddd5b96c9b31e5446878213d89f291daf94f9bb9a1`.
- `key-bist-route-lines.txt`: repeated `M6 DDP timeout[...] route VALID=0x0
  READY=0x4000937a` in pre-bist, red, green, blue, post-bist, and
  post-stock-pages windows.
- `key-bist-route-lines.txt`: BIST red/green/blue snapshots with
  `BIST_CON=0x200446 self_pat=1 bist_en=1 fix=1 lane=4`.
- `dmesg-after-bist.txt`: continuous `IRQ: ovl0 abnormal SOF!`.
- Build log:
  `/srv/forge/android/meizu_m6/kernel-meizu_M6-N-ex6-linux-3.18.140/build-m6-display-route-probe-diag-20260608.log`,
  ending in `CAT arch/arm64/boot/Image.gz-dtb`.
- Export artifact directory:
  `/srv/forge/android/export/meizu_m6_artifacts/20260608-1750-m6-display-route-probe-bootonly`.
- Built `Image.gz-dtb` sha256:
  `1c6a4326aab25144680dfab20aba8b994a57ff026057373ada21aa089445e5e4`.
- Built `System.map` sha256:
  `11c2e08002a57da6b1b461f243f4aaca0e03ae1a5a6d9b4a8642591863770940`.
- Built `kernel.config` sha256:
  `698b6764b989ef0bab75c0e6d6c291706e6a4a8d527d6a59347ad8c966d1fdd1`.
- Built boot-only artifact sha256:
  `e26a2b991cefba405604d8a184010dfba05216c146dd34702a97d56e97610ae6`.
- `sha256sum -c SHA256SUMS` passed in the export directory. `cmp Image.gz-dtb
  verify-unpack/zImage` and `cmp initrd.img verify-unpack/initrd.img` passed.
- Marker string check includes `m6_display_route_probe` and
  `M6 DISPLAY route_probe[%s]` begin/trigger/rekick/end anchors.

Files changed:
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/disp_debug.c`: adds
  `m6_display_route_probe` debugfs parsing and help text.
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/primary_display.c`: trims
  manual diagnostic tags and dumps OVL/DDP/DSI before/after a manual trigger
  or trigger-loop rekick.
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/primary_display.h`: exports
  the route-probe helper.
- `BRINGUP_STATE.md`: records the evidence, expected markers, rollback, and
  verification commands.

Why each file changed: `disp_debug.c` is the existing manual display command
surface. `primary_display.c` owns the primary path lock, trigger helper,
trigger loop, OVL request dump, and DDP/DSI snapshot calls, so it can answer
the route question without boot-time behavior changes. `primary_display.h`
keeps the debugfs call typed. This state file is the M6 handoff record.

Expected next marker: after flashing, run `dump`, `trigger`, then only if
needed `rekick`. If frame route is healthy, `*-after` must show route valid
bits and RDMA in/out counters advancing. If `trigger` and `rekick` leave
`VALID=0x0`, RDMA `0/0`, and abnormal SOF unchanged while DSI BIST remains
programmable, the current frontier is OVL/RDMA trigger/scanout rather than
brightness or HWC composition.

Rollback condition: revert if the manual command deadlocks the primary display
lock, regresses boot to ADB/SurfaceFlinger, floods logs beyond parseability, or
if `rekick` destabilizes the video trigger loop. Do not promote this patch to
`PROPER-FIX`; it is evidence only.

Verification commands:

```bash
cd /srv/forge/android/meizu_m6/kernel-meizu_M6-N-ex6-linux-3.18.140
git diff --check
env CCACHE_DIR=/srv/forge/android/ccache make -C kernel-3.18 \
  O=/srv/forge/work/m6-source-kernel-manual-20260520/out \
  ARCH=arm64 \
  CROSS_COMPILE=/srv/forge/android/meizu_m6/rom-meizu_M6-lineage-cm-14.1/prebuilts/gcc/linux-x86/aarch64/aarch64-linux-android-4.9/bin/aarch64-linux-android- \
  -j8 Image.gz-dtb
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell 'echo "m6_display_route_probe:dump" > /d/mtkfb'
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell 'echo "m6_display_route_probe:trigger" > /d/mtkfb'
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell 'echo "m6_display_route_probe:rekick" > /d/mtkfb'
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell dmesg | rg 'M6 DISPLAY route_probe|M6 DDP timeout|M6 DISPLAY truth|M6 DSI snapshot|abnormal SOF'
```

Runtime capture result, 2026-06-08:

- Capture verdict:
  `/srv/forge/android/meizu_m6/captures/20260608-1759-m6-display-route-probe-711HEBSR277K5/CAPTURE_VERDICT.md`.
- Capture files:
  `/srv/forge/android/meizu_m6/captures/20260608-1759-m6-display-route-probe-711HEBSR277K5`.
- FACT: `identity-before-route-probe.txt` shows kernel
  `Linux localhost 3.18.140 #56 SMP PREEMPT Mon Jun 8 17:46:20 CDT 2026`,
  `sys.boot_completed=1`, and boot partition sha256
  `e26a2b991cefba405604d8a184010dfba05216c146dd34702a97d56e97610ae6`.
- FACT: the boot partition hash matches exported artifact
  `/srv/forge/android/export/meizu_m6_artifacts/20260608-1750-m6-display-route-probe-bootonly/boot-m6-display-route-probe-20260608.img`.
- FACT: `m6_display_route_probe:dump` and `:trigger` returned without timeout.
- FACT: fresh route snapshots show `VALID=0x4000937a`, OVL0 enabled, two
  enabled OVL layers, clocks ungated, and RDMA0 enabled at `720x1280`.
- FACT: RDMA counters move in every route window: examples include
  `dump-before IN=198/367 OUT=350/363`, `dump-after IN=598/468 OUT=34/465`,
  `trigger-before IN=699/1121 OUT=138/1118`, and
  `trigger-after IN=368/410 OUT=528/406`.
- FACT: DSI is in video mode and reports `STATE7=0x2020/Video data period`,
  `MODE=0x3`, `TXRX=0x1003c`, `PS=0x30870`, and active MIPITX lanes
  `0x603/0x601/0x601/0x601/0x601`.
- FACT: no `abnormal SOF` line is present in fresh `dmesg-after-dump.txt` or
  `dmesg-after-trigger.txt`.
- FACT: backlight remains nonzero: `lcd-backlight ... bl=10 duty=21`.
- INFERENCE: the current fresh boot is not failing before OVL/RDMA scanout;
  the frame route is active enough to feed RDMA and DSI video packets.
- INFERENCE: `m6_display_route_probe:rekick` was intentionally not run because
  route valid bits and RDMA counters were already active.
- HYPOTHESIS: if the physical LCD is still lit-black, the next frontier is
  below the normal OVL/RDMA route: panel HS-video acceptance, panel command
  state/gamma/page programming, or actual UI buffer content being black before
  OVL. Next diagnostic should be a forced visible OVL solid layer or
  writeback/screencap-safe content proof plus panel command-state replay, not
  another PQ or route-valid patch.
- Note: `m6_dsi_dcs_status:route-good` emitted only its command line here.
  Source review shows that helper is rate-limited by static `dump_count >= 2`
  and skips reads while DSI is in video mode, so missing DCS readback is not
  evidence that panel DCS is unreadable.

## 2026-06-08 Display truth-window diagnostic boot

PATCH HISTORY, **DIAGNOSTIC**, 2026-06-08: add a read-only manual
`m6_display_truth_window[:tag]` debugfs entry that captures one bounded
cross-layer display snapshot while Android is showing known UI. This is not a
display fix and does not change DDP route, PQ behavior, panel timing, DSI
mode, or MIPITX programming.

Hypothesis: FACT: stock LK reverse/parity work has already matched the visible
ILI9881P panel identity, DCS init table, geometry, 4-lane DSI parameters,
timing, PLL, packet size, PS byte, TPS/reset order, and LP DCS readability.
FACT: current Linux can boot far enough for scrcpy/UI evidence, but the
physical LCD remains lit-black. HYPOTHESIS: the remaining evidence gap is
between logical composition and physical photons: a single fresh truth window
must correlate OVL requested layers, live DDP route/valid/ready, mutex/RDMA
counters, SMI/LARB, DSI host state, MIPITX raw/decode, panel/backlight DCS,
and LED backlight state before any further behavior patch is justified.

Evidence:
- Stock low-level parity note:
  `/srv/forge/android/meizu_m6/captures/20260530-stock-lk-boot-reverse-inputs/m6_lowlevel_dsi_mipitx_parity_20260608.md`.
- Build log:
  `/srv/forge/android/meizu_m6/kernel-meizu_M6-N-ex6-linux-3.18.140/build-m6-display-truth-window-diag-20260608.log`,
  ending in `CAT arch/arm64/boot/Image.gz-dtb`.
- Export artifact directory:
  `/srv/forge/android/export/meizu_m6_artifacts/20260608-1320-m6-display-truth-window-bootonly`.
- `Image.gz-dtb` marker string check contains
  `m6_display_truth_window`, `M6 DISPLAY truth[%s][window]`,
  `M6 DISPLAY truth[%s][ddp-route]`, `M6 DISPLAY truth[%s][dsi-host]`,
  `M6 DISPLAY truth[%s][mipitx]`, `M6 DISPLAY truth[%s][backlight]`,
  `stock-pages-before-stop`, and `stock-pages-restart-after-read`.
- Built `Image.gz-dtb` sha256:
  `e5bdbab9749d8762ec9c94698a557d43f80a0dc7b35b8e7a46aee0f25f46e668`.
- Built `System.map` sha256:
  `9a8146f2664f808b26f5d4effb8ce0fa80d70b2fe99d7791d0af89fb1b01010d`.
- Built `kernel.config` sha256:
  `698b6764b989ef0bab75c0e6d6c291706e6a4a8d527d6a59347ad8c966d1fdd1`.
- Boot-only artifact sha256:
  `f1f4291b385277cf7a7e51ddd5b96c9b31e5446878213d89f291daf94f9bb9a1`.
- `abootimg -i` reports boot image size `16777216`, page size `2048`,
  boot name `1552631950`, kernel address `0x40080000`, ramdisk address
  `0x45000000`, tags address `0x44000000`, and unchanged cmdline
  `bootopt=64S3,32N2,64N2 androidboot.selinux=permissive binder.devices=binder,hwbinder,vndbinder buildvariant=userdebug`.
- `sha256sum -c SHA256SUMS` passed in the artifact directory.
  `cmp Image.gz-dtb verify-unpack/zImage` and
  `cmp initrd.img verify-unpack/initrd.img` passed.

Files changed:
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/disp_debug.c`: adds the
  manual `m6_display_truth_window[:tag]` debugfs entry and help text.
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/primary_display.c`: adds
  the locked truth-window orchestrator and requested OVL layer snapshot dump;
  also wraps stock-pages stop/read/restart phases with DSI live snapshots.
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/primary_display.h`: exports
  the truth-window entry for debugfs.
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_manager.c`: exposes the
  existing primary DDP route/RDMA/OVL/SMI/MUTEX dump through a read-only truth
  wrapper.
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_manager.h`: declares
  the DDP truth wrapper.
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_dsi.c`: adds
  `M6 DISPLAY truth` anchors around DSI host snapshots and a raw MIPITX
  lane-map/LP/HS/PLL decode line.
- `kernel-3.18/drivers/misc/mediatek/leds/mt6755/leds.c`: adds a bounded
  LED/backlight truth dump for the active `lcd-backlight` DTS entry and
  cached brightness/PWM values.
- `kernel-3.18/drivers/misc/mediatek/leds/mt6755/leds_hal.h`: declares the
  LED truth helper.
- `kernel-3.18/drivers/misc/mediatek/lcm/ili9881p_hd_dsi_txd/ili9881p_hd_dsi_txd.c`:
  adds a panel-side `0x51` backlight truth marker when the existing bounded
  backlight log fires.
- `BRINGUP_STATE.md`: records the diagnostic evidence, expected markers,
  rollback condition, and verification commands.

Why each file changed: `disp_debug.c` is the existing M6 display debugfs
command surface. `primary_display.c` owns the primary path lock, session state,
video-mode state, and OVL snapshot correlation point. `ddp_manager.c` already
had the deepest live DDP route/MUTEX/RDMA/OVL/SMI register dump, so the patch
reuses it instead of duplicating register tables. `ddp_dsi.c` owns DSI host and
MIPITX register access. `leds.c` owns the actual `lcd-backlight` LED/PWM/LCM
dispatch state, while the ILI9881P LCM file owns the panel `0x51` DCS write.

Expected next marker: after flashing
`boot-m6-display-truth-window-20260608.img`, boot Android to a known non-black
scrcpy UI and run:

```bash
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell 'echo "m6_display_truth_window:ui-visible" > /d/mtkfb'
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell 'echo "m6_dsi_dcs_status:stock_pages" > /d/mtkfb'
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell 'echo "m6_display_truth_window:after-stock-pages" > /d/mtkfb'
```

Fresh dmesg should contain the full `M6 DISPLAY truth[...]` set for
`window`, `ovl-request`, `ddp-route`, `dsi-host`, `mipitx`, `backlight`, and
`backlight-write`. If OVL/RDMA counters advance and DSI/MIPITX/LP reads look
stock-like while the physical LCD remains black, stop route/PQ guessing and
escalate to the first proven lower layer from the capture: MIPITX lane/analog
parity, panel HS-video acceptance, or optical/backlight hardware.

Rollback condition: revert this diagnostic if the verified boot regresses
before ADB/SurfaceFlinger, if `m6_display_truth_window` deadlocks the primary
path lock, if the manual command floods logs enough to hide the capture, or if
stock-pages stop/restart phase snapshots destabilize video mode. Do not promote
this patch to `PROPER-FIX`; it is evidence only.

Verification commands:

```bash
cd /srv/forge/android/meizu_m6/kernel-meizu_M6-N-ex6-linux-3.18.140
git diff --check
env CCACHE_DIR=/srv/forge/android/ccache make -C kernel-3.18 \
  O=/srv/forge/work/m6-source-kernel-manual-20260520/out \
  ARCH=arm64 \
  CROSS_COMPILE=/srv/forge/android/meizu_m6/rom-meizu_M6-lineage-cm-14.1/prebuilts/gcc/linux-x86/aarch64/aarch64-linux-android-4.9/bin/aarch64-linux-android- \
  -j8 Image.gz-dtb
sha256sum /srv/forge/work/m6-source-kernel-manual-20260520/out/arch/arm64/boot/Image.gz-dtb \
  /srv/forge/work/m6-source-kernel-manual-20260520/out/System.map \
  /srv/forge/work/m6-source-kernel-manual-20260520/out/.config \
  build-m6-display-truth-window-diag-20260608.log
gzip -cd /srv/forge/work/m6-source-kernel-manual-20260520/out/arch/arm64/boot/Image.gz-dtb 2>/tmp/m6-display-truth-window-gzip.err | \
  strings | grep -E 'm6_display_truth_window|M6 DISPLAY truth|stock-pages-before-stop|stock-pages-restart-after-read'
cd /srv/forge/android/export/meizu_m6_artifacts/20260608-1320-m6-display-truth-window-bootonly
sha256sum -c SHA256SUMS
cmp Image.gz-dtb verify-unpack/zImage
cmp initrd.img verify-unpack/initrd.img
abootimg -i boot-m6-display-truth-window-20260608.img
```

Runtime capture result, 2026-06-08:

- Capture verdict:
  `/srv/forge/android/meizu_m6/captures/20260608-132702-m6-display-truth-window-711HEBSR277K5/CAPTURE_VERDICT.md`.
- Capture files:
  `/srv/forge/android/meizu_m6/captures/20260608-132702-m6-display-truth-window-711HEBSR277K5`.
- Valid flashed boot identity:
  `identity-after-truth.txt` sha256
  `97eb215b005b1e2332f0431ed41c14e051e251b816ed384817f5e153b4bfd61a`;
  boot partition and `/data/local/tmp/boot-m6-display-truth-window-20260608.img`
  both read back as
  `f1f4291b385277cf7a7e51ddd5b96c9b31e5446878213d89f291daf94f9bb9a1`.
- Capture hashes:
  `truth-command-output.txt`
  `4476248bdc818e614e19d535bfcf6fdb546cac9981cc939ba5f2dda559a561dc`;
  `key-display-truth-lines.txt`
  `05c2c43e86da6cf925de496f4ce42201db9a82ea9ba77bfc8468b72844a017e2`;
  `dmesg-after-truth.txt`
  `bec9e20e19b1138b05ea94521b87aa5c8fee836df2e72f595bf206dd382179dd`;
  `logcat-after-truth.txt`
  `02e9709c13401a3ca7c33fdcb1bb7e426d7e142cdfd089c2de26da67264734a6`;
  `screen-after-truth.png`
  `3a572da79c87e5946cddabe013cd69d61aed85c97a13139a8d03da028feb2680`.
- Flash note: direct `adb push boot.img /dev/block/.../boot` appeared to write
  but did not persist after reboot. The valid persistent path for this capture
  was `adb push` to `/data/local/tmp`, then Android `dd` to the boot block
  without `conv=fsync`; toybox rejected `conv=fsync` with
  `dd: conv option disabled`.
- FACT: Android booted with `sys.boot_completed=1`.
- FACT: logical framebuffer content is not black. `screen-after-truth.png` is
  720x1280 and ImageMagick reports red/green/blue channel means around
  75/75/76 with min 0 and max 255.
- FACT: OVL/RDMA/DDP/DSI/MIPITX are active in the truth window. OVL has L0
  RGBA8888 and L1 PRGBA8888 720x1280-ish layers; RDMA0 is enabled with
  `GLOBAL=0x101`, size `720x1280`, and moving in/out counters; direct
  RDMA-to-DSI bits show `rdma_sout=1/1`, `rdma_dsi=1/1`, `dsi_in=1/1`;
  DSI samples include `STATE7=.../Video data period`.
- FACT: DSI/MIPITX visible state is stock-like: `MODE=0x3`, `TXRX=0x1003c`,
  `PS=0x30870`, `VSA/VBP/VFP/VACT=0x14/0x18/0x40/0x500`,
  `PHY_SYNCON=0xb8`, `VM_CMD=0xa511521`, lanes
  `0x603/0x601/0x601/0x601/0x601`, lane map `0/1/2/3/4/0`, PLL on,
  ISO off, and power ack set.
- FACT: LP DCS reads work. `stock_pages` returns page5 sentinels including
  `page5_2a=0x18`, `page5_54=0x28`, `page5_55=0x25`, and `page5_1a=0x50`.
- FACT: the suspicious panel/backlight state is public DCS `0x51=0x00`,
  public DCS `0x53=0x00`, LED cached `lcd-backlight bl=10 duty=21`, and no
  fresh `M6 DISPLAY truth[backlight-write][panel]` marker in this capture.
- INFERENCE: PQ, HWC/SF logical composition, ordinary OVL/RDMA scanout, and
  normal DSI/MIPITX host state are demoted as first suspects for the physical
  black LCD. Do not start the next cycle with another broad PQ/route patch
  unless fresh evidence contradicts this capture.
- Next branch: `DIAGNOSTIC` first, optionally manual `ISOLATION`, for a narrow
  backlight/panel command-state timeline. Add a debugfs-only forced replay
  path for `0x53`/`0x51`, before/after DCS readbacks, LED/PWM/PMIC snapshots,
  and brightness 0/10/255 windows. If forced writes/readbacks are correct and
  physical BIST/UI stay black while the logical screencap remains non-black,
  escalate to panel HS-video acceptance or optical hardware rather than
  HWC/PQ/RDMA.
- Expected next marker: stable `truth[ui-visible]` tags without the shell
  newline suffix, plus lines proving before/after values for DCS `0x51`,
  `0x53`, `0x55`, LED/PWM/PMIC state, and optional RGB BIST observation
  windows.
- Rollback condition for the next replay patch: revert if it mutates boot-time
  panel state without manual invocation, breaks LP DCS reads, regresses
  `sys.boot_completed=1`, or destabilizes video after the debugfs command
  returns.

## 2026-06-08 MSDC2 CMD-state diagnostic compile fix

PATCH HISTORY, **DIAGNOSTIC**, 2026-06-08: fix the build break in the bounded
MSDC2 CMD-state diagnostic helper by providing the local `base` pointer required
by the legacy `MSDC_READ32()` macro in `m6_msdc2_trace_power()`. This preserves
the Wi-Fi/MSDC diagnostic patch as a separate checkpoint before the display
truth-window work; it does not change display behavior.

Hypothesis: FACT: the build log
`build-m6-msdc2-cmd-state-diag-20260608.log` failed in
`drivers/mmc/host/mediatek/mt6755/sd.c` because `MSDC_READ32(MSDC_INT)`
expanded to a macro that expects a local variable named `base`. HYPOTHESIS:
adding the local `void __iomem *base = host->base` setup in the power trace
helper should restore the diagnostic build without altering runtime behavior
outside the existing bounded MSDC2 trace path.

Evidence:
- Failed build log:
  `/srv/forge/android/meizu_m6/kernel-meizu_M6-N-ex6-linux-3.18.140/build-m6-msdc2-cmd-state-diag-20260608.log`.
- Passing retry build log:
  `/srv/forge/android/meizu_m6/kernel-meizu_M6-N-ex6-linux-3.18.140/build-m6-msdc2-cmd-state-diag-20260608-retry1.log`.
- Built `Image.gz-dtb` sha256:
  `5991c9fb549fdd4ffcffad98ec0e6c6593517a1510402aa932c9a8a7b5c4e08f`.
- Built `System.map` sha256:
  `26bab674e1d74a385a535ccb03c998cb8a7855000d2a85eec1909ee5dd43c931`.

Files changed:
- `kernel-3.18/drivers/mmc/host/mediatek/mt6755/sd.c`: adds the local `base`
  pointer for `m6_msdc2_trace_power()` and keeps the existing diagnostic-only
  trace helper compileable.
- `BRINGUP_STATE.md`: records this as a separate non-display checkpoint so the
  next display patch does not mix Wi-Fi/MSDC edits.

Why each file changed: `sd.c` owns the bounded MSDC2 command/power diagnostic
markers that the previous Wi-Fi patch added. The state file is the canonical
M6 kernel-tree handoff record.

Expected next marker: a rebuilt diagnostic image should contain the existing
`M6 MSDC2 state` markers and no longer fail at compile time.

Rollback condition: revert this checkpoint if the MSDC2 diagnostic helper
changes SDIO power sequencing, produces excessive hot-path logs beyond the
existing budget, or regresses boot/storage/Wi-Fi enumeration.

Verification commands:

```sh
cd /srv/forge/android/meizu_m6/kernel-meizu_M6-N-ex6-linux-3.18.140
git diff --check
rg -n 'm6_msdc2_trace_power|M6 MSDC2 state' kernel-3.18/drivers/mmc/host/mediatek/mt6755/sd.c
env CCACHE_DIR=/srv/forge/android/ccache make -C kernel-3.18 \
  O=/srv/forge/work/m6-source-kernel-manual-20260520/out \
  ARCH=arm64 \
  CROSS_COMPILE=/srv/forge/android/meizu_m6/rom-meizu_M6-lineage-cm-14.1/prebuilts/gcc/linux-x86/aarch64/aarch64-linux-android-4.9/bin/aarch64-linux-android- \
  -j8 Image.gz-dtb
```

## 2026-06-08 WMT GPIO/IRQ markers and MSDC2 CMD5 pad frontier

PATCH HISTORY, **PROPER-FIX / DIAGNOSTIC**, 2026-06-08: guard the active
combo-SDIO IRQ path against the real invalid sentinel `0xffffffff`, add bounded
WMT GPIO / WMT detect / CMB SDIO markers, and dump MSDC2 pad registers in the
CMD5 window. This is a proper fix for the proven invalid-IRQ test and a
diagnostic frontier advance; it does not claim Wi-Fi is complete.

Hypothesis: FACT: the previous `#52` boot proved `vcn33_wifi` and `vcn18` are
enabled at 3.3 V / 1.8 V when WMT asks MSDC2 to enumerate, but CMD5 still
returns `ocr=0x0`. FACT: the same boot showed `wifi_irq=4294967295`
(`0xffffffff`) while the legacy code only rejected `0x0fffffff`, so invalid
IRQ state could still flow into enable/disable/wake decisions. FACT: the active
DTB has `consys@18070000` and `wifi@180f0000`, but no
`mediatek,connectivity-combo` node. HYPOTHESIS: correcting the invalid IRQ
guard and adding dense markers around WMT GPIO, WMT detect, CMB SDIO, and MSDC2
pads would prove whether the next failure is bad EINT plumbing, missing combo
GPIO/reset sequencing, or SDIO electrical/card-response state before Wi-Fi HAL.
INFERENCE after flashing `#53`: the invalid IRQ is now correctly treated as
invalid (`valid=0`, wake ret `-19`) and no request is made for the bogus
`0xffffffff` IRQ. The WMT GPIO / WMT detect marker strings are present in the
payload, but no runtime marker fires, so that parser is not on the active boot
path. The earliest open Wi-Fi blocker remains below HAL and netdev:
MSDC2 repeatedly enters the CMD5 window with rails on, pad registers dumped,
and still gets `CMD5 ocr=0x0`; no live `wlan0` exists despite
`wlan.driver.status=ok`.

Evidence:
- Build log:
  `/srv/forge/android/meizu_m6/kernel-meizu_M6-N-ex6-linux-3.18.140/build-m6-wmt-gpio-irq-pad-diag-20260608.log`.
- Artifact:
  `/srv/forge/android/export/meizu_m6_artifacts/20260608-1137-m6-wmt-gpio-irq-pad-diag-bootonly/boot-m6-wmt-gpio-irq-pad-diag-20260608.img`,
  sha256 `3d41f0b3c1358f2cd9528a2faceb81c6cd9068a186075d290cbbd479eaa18337`.
- Matching `Image.gz-dtb` sha256:
  `499ad4dc4122ebc980213d28cc18807443f336081dd54f6c4b209e30c0eb33ee`.
- Matching `System.map` sha256:
  `7ce598852aa7ed7c657cf4cb46bf352555d5f5190f14728376c0e9235ccc2483`.
- Matching `vmlinux` sha256:
  `aa2849fa5872a354c8d3241fb2ef985b6b60e65bb45618a27f62410547321d12`.
- Matching `meizu_m6.dtb` sha256:
  `bdcecd02d7e3ea4ffcd558c88f1a17107ffbd067c9ccc6c908b8d3e2a29a03b8`.
- `abootimg` verification in the artifact directory proves unpacked `zImage`,
  `initrd.img`, and `bootimg.cfg` match the artifact inputs; marker string
  check found 13 marker strings including `M6 CMB SDIO request_eirq`,
  `M6 WMT GPIO`, `M6 WMT detect`, and `M6 MSDC2 pad dump before CMD5`.
- Flash identity:
  `/srv/forge/android/meizu_m6/captures/20260608-1139-m6-wmt-gpio-irq-pad-diag-after-flash-711HEBSR277K5/retry1-postflash-readback-sha256.txt`
  proves the boot partition readback matches the local boot image. The first
  flash attempt in the same capture intentionally remains recorded as a
  mismatch caused by Android `dd` rejecting `conv=fsync`; retry1 is the valid
  flashed identity.
- Runtime identity:
  `/srv/forge/android/meizu_m6/captures/20260608-1139-m6-wmt-gpio-irq-pad-diag-after-flash-711HEBSR277K5/identity-and-props-after-reboot.txt`
  shows `Linux localhost 3.18.140 #53 SMP PREEMPT Mon Jun 8 11:36:58 CDT 2026
  aarch64`, `sys.boot_completed=1`, `init.svc.bootanim=stopped`,
  `service.wcn.driver.ready=yes`, and `wlan.driver.status=ok`.
- Runtime netdev proof:
  `/srv/forge/android/meizu_m6/captures/20260608-1139-m6-wmt-gpio-irq-pad-diag-after-flash-711HEBSR277K5/runtime-components-state.txt`
  shows no `wlan0` in `ip link` or `/sys/class/net`; the property is not proof
  of a working Wi-Fi device.
- Runtime failure markers:
  `M6 CMB board_sdio_ctrl ... wifi_irq=4294967295 valid=0`,
  `M6 CMB board_sdio_ctrl wake ... ret=-19`,
  `M6 MSDC2 rail vmmc/vcn33_wifi ... status=3300000`,
  `M6 MSDC2 rail vqmmc/vcn18 ... status=1800000`,
  `M6 MSDC2 pad dump before CMD5 window power=1`,
  `MSDC2 IES ... =0xff`, `MSDC2 SMT ... =0x38`,
  `MSDC2 TDSEL ... =0x0`, `MSDC2 RDSEL0 ... =0x0`,
  `MSDC2 PULL ... =0x11611660`, `MSDC2 PULL ... =0x1`,
  `M6 MMC2 attach_sdio CMD5 probe err=0 ocr=0x0`,
  `mtk-msdc 11250000.msdc2: no support for card's volts`,
  and `hif_sdio_stp_on:M6 SDIO no supported func probed`.
- Init/HAL context: current ramdisk/source paths include the bridge
  `service.wcn.driver.ready -> setprop wlan.driver.status ok ->
  write /dev/wmtWifi "1"`. The fresh log shows that write returns `-1` because
  the kernel still has no SDIO function, while Wi-Fi HAL reports
  `Failed to write wlan fw path param: I/O error` and
  `Failed to start HAL for client mode`.

Files changed:
- `kernel-3.18/drivers/misc/mediatek/connectivity/common/common_detect/mtk_wcn_stub_alps.c`:
  defines the real invalid Wi-Fi IRQ sentinel, centralizes validity testing,
  skips bogus request/enable/disable/wake work for `0xffffffff`, and logs CMB
  SDIO request/enable/disable/wake state.
- `kernel-3.18/drivers/misc/mediatek/connectivity/common/common_detect/wmt_gpio.c`:
  adds read-only dumps for key combo GPIO IDs, pinctrl state pointers, missing
  node state, and parsed GPIO values.
- `kernel-3.18/drivers/misc/mediatek/connectivity/common/common_detect/wmt_detect_pwr.c`:
  adds read-only WMT detect power/reset GPIO entry/exit/read/write markers.
- `kernel-3.18/drivers/mmc/host/mediatek/mt6755/msdc_io.c`: dumps MSDC2 pad
  registers immediately before each CMD5 attempt.
- `BRINGUP_STATE.md`: records artifact identity, flash identity, runtime
  evidence, closed invalid-IRQ behavior, and the next CMD5/power/reset frontier.

Why each file changed: `mtk_wcn_stub_alps.c` owns the active board-SDIO control
path proven by the `mtk_wmtd` logs, so it is the correct place to guard
`wifi_irq` and prove wake/eirq decisions. `wmt_gpio.c` and `wmt_detect_pwr.c`
were instrumented because the active DTB lacks `connectivity-combo` and stock
truth may still require a board reset/PMU path; the fresh capture proves those
helpers are not currently executing. `msdc_io.c` owns the CMD5-adjacent pad
state and is the earliest confirmed failing boundary after rails are enabled.
The state file is the canonical M6 handoff record.

Expected next marker: the next patch should prove or implement the missing
combo-chip reset/enable/pad sequence before CMD5. A successful proper fix must
move from `CMD5 ocr=0x0` to a nonzero SDIO OCR, a registered SDIO function, or
a more specific command/CRC/timeout error. Good next markers are: explicit
CONSYS/Wi-Fi reset or PMU GPIO number/value/mode before `board_sdio_ctrl(on)`,
MSDC2 DAT/CMD line sampled state before CMD5, and a stock-derived LK/kernel
sequence comparison for SDIO2 pad/power/reset.

Rollback condition: revert this patch if boot/ADB, WMT chip-id detection,
MSDC2 PM callback entry, regulator state, suspend/resume, or display boot
state regresses. Do not revert solely because Wi-Fi still fails; this patch
closed the bogus IRQ handling and proved the remaining failure is earlier than
HAL/netdev.

Verification commands:

```sh
cd /srv/forge/android/meizu_m6/kernel-meizu_M6-N-ex6-linux-3.18.140
sha256sum -c /srv/forge/android/export/meizu_m6_artifacts/20260608-1137-m6-wmt-gpio-irq-pad-diag-bootonly/SHA256SUMS
CAP=/srv/forge/android/meizu_m6/captures/20260608-1139-m6-wmt-gpio-irq-pad-diag-after-flash-711HEBSR277K5
cmp /srv/forge/android/export/meizu_m6_artifacts/20260608-1137-m6-wmt-gpio-irq-pad-diag-bootonly/boot-m6-wmt-gpio-irq-pad-diag-20260608.img "$CAP/postflash-boot-readback-wmt-gpio-irq-pad-diag-retry1-16m.img"
rg -n 'Linux localhost 3.18.140 #53|M6 CMB board_sdio_ctrl|wifi_irq=4294967295 valid=0|M6 MSDC2 pad dump|M6 MMC2 attach_sdio CMD5|no support for card.s volts|M6 SDIO no supported func|Failed to write wlan fw path|Failed to start HAL|wlan.driver.status' "$CAP"/*.txt
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell 'uname -a; getprop sys.boot_completed; getprop service.wcn.driver.ready; getprop wlan.driver.status; ip link show wlan0 2>/dev/null || true; ls -l /sys/class/net'
```

## 2026-06-08 MSDC2 VCN rail power-enable and CMD5 reset/pinctrl frontier

PATCH HISTORY, **PROPER-FIX / DIAGNOSTIC**, 2026-06-08: make the active MSDC2
SDIO power path explicitly operate the Wi-Fi `vmmc` and `vqmmc` regulator
handles, prove their voltage/enable state in the CMD5 window, and record the
new Wi-Fi/BT frontier. This is a real board-power wiring fix and a diagnostic
frontier advance; it does not claim WLAN is complete.

Hypothesis: FACT: the previous `#51` boot proved that the compiled DTB binds
MSDC2 to `vcn33_wifi` and `vcn18`, but the host id 2 `msdc_sdio_power()` branch
only logged those handles and did not perform regulator enable/voltage work.
FACT: WMT/CONSYS reaches chip id `0x00000326` and calls the MSDC2 PM callback,
but CMD5 still returns `ocr=0x0` and no `wlan0` is created. HYPOTHESIS: if the
missing SDIO-card response was caused by the MSDC2 branch leaving the Wi-Fi
rails inactive or unverified, enabling/proving `vcn33_wifi` at 3.3 V and
`vcn18` at 1.8 V before CMD5 would move enumeration past zero OCR. INFERENCE
after flashing `#52`: both rails are present and enabled at the expected
voltages when CMD5 runs, while OCR remains zero. The earliest remaining
evidence-backed Wi-Fi/BT blocker is therefore SDIO physical enumeration:
combo reset/enable GPIO, pinctrl pull/drive, SDIO clock/transaction timing,
card-detect/non-removable rescan policy, or stock WMT/LK sequencing before
CMD5. Do not chase Wi-Fi firmware, NVRAM, HAL, or Android networking until
CMD5 returns a nonzero SDIO OCR or a lower-level command/reset marker.

Evidence:
- Source patch build log:
  `/srv/forge/android/meizu_m6/kernel-meizu_M6-N-ex6-linux-3.18.140/build-m6-msdc2-vcn-power-enable-20260608.log`.
- New artifact:
  `/srv/forge/android/export/meizu_m6_artifacts/20260608-1039-m6-msdc2-vcn-power-enable-bootonly/boot-m6-msdc2-vcn-power-enable-20260608.img`,
  sha256 `a04b88e3cf6234eb5f602478046c12313ce6f6d5933c4b727504fa87a4099ad1`.
- New `Image.gz-dtb` sha256:
  `c49862cf58541aa7477a980f126fed6a851c9045e3a40bb94a0919a02c4757a3`.
- Matching compiled `meizu_m6.dtb` sha256:
  `bdcecd02d7e3ea4ffcd558c88f1a17107ffbd067c9ccc6c908b8d3e2a29a03b8`.
- Matching `System.map` sha256:
  `03538ab097cfe4ade6d261e945d8db9d00f5918fb8d09db1b09bf62f553511fe`.
- Matching `vmlinux` sha256:
  `4e87302a17da3e908994423f8f57658824a4a5ffd1ec0748532dff56b4db1318`.
- Matching `kernel.config` sha256:
  `698b6764b989ef0bab75c0e6d6c291706e6a4a8d527d6a59347ad8c966d1fdd1`.
- Marker strings verified in the matching `vmlinux`: `M6 MSDC2 rail %s on=%u`,
  `vmmc/vcn33_wifi`, `vqmmc/vcn18`, and `M6 MSDC2 sdio_power`.
- Flash identity: the boot partition readback in
  `/srv/forge/android/meizu_m6/captures/20260608-1039-m6-msdc2-vcn-power-enable-after-flash-711HEBSR277K5/postflash-readback-sha256.txt`
  matches the local boot image sha256
  `a04b88e3cf6234eb5f602478046c12313ce6f6d5933c4b727504fa87a4099ad1`.
- Runtime identity: the phone rebooted with
  `Linux localhost 3.18.140 #52 SMP PREEMPT Mon Jun 8 10:52:44 CDT 2026
  aarch64`.
- Android boot state: delayed status capture
  `/srv/forge/android/meizu_m6/captures/20260608-1039-m6-msdc2-vcn-power-enable-after-flash-711HEBSR277K5/delayed-status-and-dmesg-tail.txt`
  shows `sys.boot_completed=1`, `init.svc.bootanim=stopped`,
  `service.wcn.driver.ready=yes`, and later `wlan.driver.status=unloaded`.
- Rail proof markers from the same capture/logcat:
  `M6 MSDC2 sdio_power on=1 ... g_io=1800000 g_flash=3300000`,
  `M6 MSDC2 rail vmmc/vcn33_wifi on=1 target_uv=3300000 before_en=1
  before_uv=3300000 set_ret=0 en_ret=0 dis_ret=0 after_en=1
  after_uv=3300000 status=3300000`, and
  `M6 MSDC2 rail vqmmc/vcn18 on=1 target_uv=1800000 before_en=1
  before_uv=1800000 set_ret=0 en_ret=0 dis_ret=0 after_en=1
  after_uv=1800000 status=1800000`.
- Still-open frontier markers: `M6 MMC2 attach_sdio CMD5 probe err=0 ocr=0x0`,
  `mtk-msdc 11250000.msdc2: no support for card's volts`,
  `M6 MMC2 attach_sdio err=-22 ocr=0x0 rocr=0x0 funcs=0`,
  `hif_sdio_stp_on:M6 SDIO no supported func probed`, and no live `wlan0`.
- Capture-local report:
  `/srv/forge/android/meizu_m6/captures/20260608-1039-m6-msdc2-vcn-power-enable-after-flash-711HEBSR277K5/wifi-msdc2-vcn-power-enable-result.md`.

Files changed:
- `kernel-3.18/drivers/mmc/host/mediatek/mt6755/msdc_io.c`: adds a bounded
  M6 MSDC2 regulator helper and uses it in host id 2 `msdc_sdio_power()` to
  prove/enable `vcn33_wifi` and `vcn18`; reapplies the 1.8 V tdsel/rdsel/drive
  selection while the SDIO card is powered.
- `BRINGUP_STATE.md`: records artifact identity, readback identity, runtime
  markers, the closed rail-enable blocker, and the next SDIO enumeration
  frontier.
- `captures/20260608-1039-m6-msdc2-vcn-power-enable-after-flash-711HEBSR277K5/wifi-msdc2-vcn-power-enable-result.md`:
  capture-local verdict for the flashed `#52` boot.

Why each file changed: `msdc_io.c` owns the legacy MTK host power callback that
WMT invokes through the fixed MSDC2 PM callback. The helper is local to the M6
diagnostic path and uses the regulator handles proven by the previous DT patch,
so it avoids a broad fake-ready or userspace workaround. This state file is the
canonical M6 handoff record for the kernel tree. The capture-local report keeps
the runtime verdict next to the logs used to derive it.

Expected next marker: the next Wi-Fi/BT diagnostic patch should show the
board-level reset/enable and pinctrl state before CMD5: combo/Wi-Fi reset GPIO
number, direction, value, pull, mode, SDIO pin mode/pull/drive, host clock and
command result around CMD5, plus whether WMT toggles any stock GPIO before
`mtk_wcn_cmb_sdio_on()`. A successful proper fix should move from
`CMD5 ocr=0x0` to a nonzero SDIO OCR and then into SDIO function registration
or a more specific command/CRC/timeout error.

Rollback condition: revert this patch if boot/ADB, eMMC/mmc0, external
storage/mmc1, WMT chip power-on, regulator init, suspend/resume, or battery
state regresses, or if stock-source evidence proves `vcn33_wifi`/`vcn18` must
not be controlled from the MSDC2 power callback. Do not revert solely because
Wi-Fi still fails at `CMD5 ocr=0x0`; the fresh capture proves that this is the
new frontier after the rail-enable blocker is closed.

Verification commands:

```sh
cd /srv/forge/android/meizu_m6/kernel-meizu_M6-N-ex6-linux-3.18.140
git show --stat --oneline HEAD
sha256sum /srv/forge/android/export/meizu_m6_artifacts/20260608-1039-m6-msdc2-vcn-power-enable-bootonly/boot-m6-msdc2-vcn-power-enable-20260608.img
sha256sum -c /srv/forge/android/export/meizu_m6_artifacts/20260608-1039-m6-msdc2-vcn-power-enable-bootonly/SHA256SUMS
CAP=/srv/forge/android/meizu_m6/captures/20260608-1039-m6-msdc2-vcn-power-enable-after-flash-711HEBSR277K5
cmp /srv/forge/android/export/meizu_m6_artifacts/20260608-1039-m6-msdc2-vcn-power-enable-bootonly/boot-m6-msdc2-vcn-power-enable-20260608.img "$CAP/postflash-boot-readback-msdc2-vcn-power-enable-16m.img"
rg -n 'Linux localhost 3.18.140 #52|M6 MSDC2 rail|M6 MSDC2 sdio_power|M6 MMC2 attach_sdio CMD5|no support for card.s volts|M6 SDIO no supported func|sys.boot_completed|wlan.driver.status' "$CAP"/*.txt
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell 'getprop sys.boot_completed; getprop init.svc.bootanim; getprop service.wcn.driver.ready; getprop wlan.driver.status; ip link show wlan0 2>/dev/null || true'
```

## 2026-06-08 MSDC2 VCN rail DT binding and CMD5 power frontier

PATCH HISTORY, **PROPER-FIX / DIAGNOSTIC**, 2026-06-08: bind MSDC2's SDIO
`vmmc` and `vqmmc` supplies in the active compiled DTB so the host no longer
parses the Wi-Fi SDIO slot with missing regulator handles. This is a proper DT
wiring fix for the board contract and a diagnostic frontier advance for Wi-Fi:
it does not claim WLAN is complete.

Hypothesis: FACT: the previous `#49` boot closed the WMT/MSDC2 callback gap and
showed `M6 CMB SDIO on invoking pm cb=... evt=272`, but CMD5 still returned
`ocr=0x0` and Android had no `wlan0`. FACT: the source `mmc2` node had
`host_function = <MSDC_SDIO>` but no `vmmc-supply` or `vqmmc-supply`, while the
M6 PMIC DTS already exposes `mt_pmic_vcn33_wifi_ldo_reg` and
`mt_pmic_vcn18_ldo_reg`. HYPOTHESIS: MSDC2 was reaching the SDIO attach path
without the board's Wi-Fi SDIO power rails attached to the MMC host, so CMD5
could not see a powered card. INFERENCE after flashing `#51`: the supply handles
are now present, but `msdc_sdio_power()` still only logs host id 2 and does not
enable those regulators, leaving the next earliest blocker inside the MSDC2
power-on path rather than Wi-Fi firmware, NVRAM, HAL, or userspace.

Evidence:
- Previous callback-fix capture:
  `/srv/forge/android/meizu_m6/captures/20260608-0951-m6-msdc2-cfg-gate-fix-after-flash-711HEBSR277K5`.
- New artifact:
  `/srv/forge/android/export/meizu_m6_artifacts/20260608-1019-m6-msdc2-vcn-rails-only-bootonly/boot-m6-msdc2-vcn-rails-only-20260608.img`,
  sha256 `20156ac4e0bc1bb2b1f81d804fb15abe18a15c26d0447ed6cd2aff4d2a581dfa`.
- New `Image.gz-dtb` sha256:
  `3b60016635fafe2e1349312e4645588c5bd890cca0e051ae2bacf6d883f91022`.
- Matching compiled `meizu_m6.dtb` sha256:
  `bdcecd02d7e3ea4ffcd558c88f1a17107ffbd067c9ccc6c908b8d3e2a29a03b8`.
- Matching `System.map` sha256:
  `30fb32c0c7896562744a56b2e4377b6031516413c25c67325eda6a3cf3f35a64`.
- Matching `vmlinux` sha256:
  `5cfb45efe6b206fa4ea250757810e8ed246db1b53a5693865c71d7679b2e3ea4`.
- Matching `kernel.config` sha256:
  `698b6764b989ef0bab75c0e6d6c291706e6a4a8d527d6a59347ad8c966d1fdd1`.
- DTB verification:
  `/srv/forge/android/export/meizu_m6_artifacts/20260608-1019-m6-msdc2-vcn-rails-only-bootonly/msdc2-compiled-dtb-snippet.txt`
  shows `msdc2@11250000` with `vmmc-supply=<0x10>` and
  `vqmmc-supply=<0x11>`, while `cmd_edge`, `rdata_edge`, and `wdata_edge`
  remain `MSDC_SMPL_FALLING`. The earlier rising-edge idea was abandoned before
  flashing because Q-ex2's reference `mmc2_register_setting_default` also uses
  falling edges.
- Flash identity: the boot partition readback in
  `/srv/forge/android/meizu_m6/captures/20260608-1019-m6-msdc2-vcn-rails-only-after-flash-711HEBSR277K5/postflash-readback-sha256.txt`
  matches the local boot image sha256
  `20156ac4e0bc1bb2b1f81d804fb15abe18a15c26d0447ed6cd2aff4d2a581dfa`.
- Runtime identity: the phone rebooted with
  `Linux localhost 3.18.140 #51 SMP PREEMPT Mon Jun 8 10:23:54 CDT 2026
  aarch64`.
- Closed DT-supply sub-blocker marker: `M6 MSDC2 sdio_power on=1
  vmmc=ffffffc079ddcc00 vqmmc=ffffffc079ddcc80 g_io=0 g_flash=0`.
- Still-open frontier markers: `M6 MMC2 attach_sdio CMD5 probe err=0 ocr=0x0`,
  `mtk-msdc 11250000.msdc2: no support for card's volts`,
  `M6 MMC2 attach_sdio err=-22 ocr=0x0 rocr=0x0 funcs=0`,
  `hif_sdio_stp_on:M6 SDIO no supported func probed`, and
  `Device "wlan0" does not exist.`
- Power-side sanity marker: WMT/CONSYS still reads chip id `0x00000326`, so the
  current failure is not a total CONSYS power-off condition.

Files changed:
- `kernel-3.18/arch/arm64/boot/dts/cust_mt6755_msdc.dtsi`: adds MSDC2
  `vmmc-supply = <&mt_pmic_vcn33_wifi_ldo_reg>` and
  `vqmmc-supply = <&mt_pmic_vcn18_ldo_reg>`.
- `BRINGUP_STATE.md`: records artifact identity, readback identity, the DTB
  proof, the abandoned timing-edge branch, and the remaining
  `msdc_sdio_power()` frontier.

Why each file changed: the DTS file owns the board-level MMC2 supply binding
that `msdc_of_parse()` consumes into `host->mmc->supply`. Without this binding,
driver-local power work could not safely tell whether it was operating on the
real Wi-Fi rails or dummy/missing regulators. This state file is the canonical
M6 handoff record and must carry the evidence chain for the next patch cycle.

Expected next marker: after a targeted MSDC2 power-path patch, the next capture
should show `msdc_sdio_power(on=1)` enabling or proving the already-enabled
state of `vcn33_wifi` and `vcn18`, then CMD5 should either return a nonzero SDIO
OCR or move to a more specific command/CRC/timeout/reset marker. If OCR remains
`0x0` with both rails proven on at the moment of CMD5, pivot to SDIO reset,
pinctrl drive/pull, clock, `ocr_avail`, non-removable/card-detect policy, and
stock LK/kernel power sequencing before touching Wi-Fi firmware or NVRAM.

Rollback condition: revert this DT binding if boot/ADB, eMMC/mmc0, external
storage/mmc1, WMT chip power-on, or regulator init regresses, or if stock-source
evidence proves these are not the M6 Wi-Fi SDIO rails. Do not revert solely
because WLAN still fails at CMD5; that is the expected next frontier.

Verification commands:

```sh
cd /srv/forge/android/meizu_m6/kernel-meizu_M6-N-ex6-linux-3.18.140
kernel-3.18/scripts/dtc/dtc -I dtb -O dts -o /tmp/m6-msdc2.dts /srv/forge/android/export/meizu_m6_artifacts/20260608-1019-m6-msdc2-vcn-rails-only-bootonly/meizu_m6.dtb
rg -n 'msdc2@11250000|vmmc-supply|vqmmc-supply|cmd_edge|rdata_edge|wdata_edge' /tmp/m6-msdc2.dts
sha256sum /srv/forge/android/export/meizu_m6_artifacts/20260608-1019-m6-msdc2-vcn-rails-only-bootonly/boot-m6-msdc2-vcn-rails-only-20260608.img
CAP=/srv/forge/android/meizu_m6/captures/20260608-1019-m6-msdc2-vcn-rails-only-after-flash-711HEBSR277K5
rg -n 'Linux localhost 3.18.140 #51|M6 MSDC2 sdio_power|M6 MMC2 attach_sdio CMD5|no support for card.s volts|M6 SDIO no supported func|Device "wlan0" does not exist|chipId=0x00000326' "$CAP"/*.txt
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell 'getprop sys.boot_completed; getprop service.wcn.driver.ready; ip link show wlan0 2>/dev/null || true'
```

## 2026-06-08 MSDC2 callback compile-gate fix and CMD5 frontier

PATCH HISTORY, **PROPER-FIX**, 2026-06-08: widen the MSDC2 SDIO callback
compile gate so the active M6 combo configuration wires `mt_sdio_ops[2]` into
the MSDC2 host even when `CONFIG_MTK_COMBO_COMM` is disabled. This is not a
Wi-Fi/BT complete fix; it closes the proven `cb=NULL` WMT/MSDC2 integration
blocker and exposes the next SDIO electrical/enumeration frontier.

Hypothesis: FACT: the booted DT has `mtk-msdc.0/msdc2@11250000` enabled and
runtime sysfs shows `mmc2` bound to that host, but the previous `#48` kernel
did not contain the `M6 MSDC2 SDIO callbacks` marker string. FACT: the active
out-dir `.config` has `CONFIG_MTK_COMBO=y`, `CONFIG_MTK_COMBO_WIFI=y`, and
`CONFIG_MTK_COMBO_BT=y`, while `CONFIG_MTK_COMBO_COMM` is disabled. INFERENCE:
the donor compile gate made `CFG_DEV_MSDC2` depend on the wrong combo symbol,
so the MSDC2 host existed but never installed the WMT PM/EIRQ callbacks.

Evidence:
- Previous runtime capture:
  `/srv/forge/android/meizu_m6/captures/20260608-0920-m6-msdc2-hif-diag-after-flash-711HEBSR277K5`.
- Live DT/sysfs capture:
  `/srv/forge/android/meizu_m6/captures/20260608-live-dt-msdc2-711HEBSR277K5`.
- New artifact:
  `/srv/forge/android/export/meizu_m6_artifacts/20260608-0951-m6-msdc2-cfg-gate-fix-bootonly/boot-m6-msdc2-cfg-gate-fix-20260608.img`,
  sha256 `f533f1eb027a1f54c6bec3241e43874de0dc326f59e646014414ed9520d157ff`.
- New `Image.gz-dtb` sha256:
  `ca7833c5a966a0bb96c9c4bff6da1e017f5a9c6cf69869f156ea0decb843f939`.
- Matching `System.map` sha256:
  `30fb32c0c7896562744a56b2e4377b6031516413c25c67325eda6a3cf3f35a64`.
- Matching `vmlinux` sha256:
  `5185470268f901d0bcc1aa6fb2fda23e2e796b60aa7d171d38718ef59faed09f`.
- Matching `kernel.config` sha256:
  `698b6764b989ef0bab75c0e6d6c291706e6a4a8d527d6a59347ad8c966d1fdd1`.
- Postflash capture:
  `/srv/forge/android/meizu_m6/captures/20260608-0951-m6-msdc2-cfg-gate-fix-after-flash-711HEBSR277K5`.
- Flash identity: the first `dd` attempt did not write because toybox rejected
  `conv=fsync`; the retry without `conv`, followed by `sync`, produced a full
  16 MiB boot readback matching the new image sha256
  `f533f1eb027a1f54c6bec3241e43874de0dc326f59e646014414ed9520d157ff`.
- Runtime result: the phone rebooted to Android with
  `Linux localhost 3.18.140 #49 SMP PREEMPT Mon Jun 8 09:50:11 CDT 2026
  aarch64` and later reached `sys.boot_completed=yes`.
- Closed blocker markers: `M6 CMB board_sdio_ctrl port=2 on=1
  cb=ffffffc0008a5b00 data=ffffffc079858fc0`,
  `M6 CMB SDIO on invoking pm cb=ffffffc0008a5b00 ... evt=272`, and
  `M6 MSDC2 WMT resume: scheduling SDIO rescan`.
- New frontier markers: `M6 MMC2 attach_sdio CMD5 probe err=0 ocr=0x0`,
  `mtk-msdc 11250000.msdc2: no support for card's volts`, and
  `mmc2: error -22 whilst initialising SDIO card`. `wlan0` is still absent.

Files changed:
- `kernel-3.18/drivers/mmc/host/mediatek/mt6755/mt_sd.h`: widens the
  `CFG_DEV_MSDC2` gate from `CONFIG_MTK_COMBO_COMM` only to the active combo
  family symbols `CONFIG_MTK_COMBO`, `CONFIG_MTK_COMBO_WIFI`, and
  `CONFIG_MTK_COMBO_BT`.
- `BRINGUP_STATE.md`: records artifact identity, flash/readback identity, closed
  WMT callback blocker, and the next CMD5/OCR frontier.

Why each file changed: `mt_sd.h` owns the compile-time gate that decides
whether `msdc_io.c` can assign `request_sdio_eirq`, `enable_sdio_eirq`,
`disable_sdio_eirq`, and `register_pm` for MSDC2. The active kernel config
does build the combo/Wi-Fi/BT stack, but it does not enable the narrower donor
`CONFIG_MTK_COMBO_COMM` symbol, so the old gate was inconsistent with the
compiled WMT/HIF stack. This state file is the canonical M6 source-kernel
handoff record for the resulting artifact and runtime boundary.

Expected next marker: after the next SDIO power/pinctrl/voltage diagnostic or
fix, CMD5 should return a nonzero SDIO OCR and move from `mmc2: error -22` to
either `M6 SDIO probed-list add`/function enable markers or a more specific
command/CRC/timeout/power marker. If CMD5 remains `ocr=0x0`, collect stock/DTS
parity for MSDC2 `ocr_avail`, vmmc/vqmmc, pinctrl drive/pull, clock source,
reset/CONSYS power sequencing, and card-detect/non-removable policy before
changing higher Wi-Fi, BT, firmware, or NVRAM layers.

Rollback condition: revert this fix if it regresses boot/ADB, eMMC/mmc0,
external storage/mmc1, WMT chip power-on, or causes MSDC2 to lose the host/IRQ
that existed before. Do not revert solely because Wi-Fi/BT still fail at the
new CMD5/OCR frontier; that is expected until the SDIO electrical/enumeration
path is fixed.

Verification commands:

```sh
cd /srv/forge/android/meizu_m6/kernel-meizu_M6-N-ex6-linux-3.18.140
strings /srv/forge/work/m6-source-kernel-manual-20260520/out/vmlinux | rg 'M6 MSDC2 SDIO callbacks|M6 CMB SDIO register_pm'
sha256sum /srv/forge/android/export/meizu_m6_artifacts/20260608-0951-m6-msdc2-cfg-gate-fix-bootonly/boot-m6-msdc2-cfg-gate-fix-20260608.img
CAP=/srv/forge/android/meizu_m6/captures/20260608-0951-m6-msdc2-cfg-gate-fix-after-flash-711HEBSR277K5
rg -n 'M6 CMB board_sdio_ctrl|M6 CMB SDIO on invoking pm cb|M6 MSDC2 WMT resume|M6 MMC2 attach_sdio CMD5|no support for card.s volts|mmc2: error -22|M6 SDIO probed-list add' "$CAP/dmesg.after_reboot_early.txt" "$CAP/logcat.after_reboot_early.txt"
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell 'getprop sys.boot_completed; getprop service.wcn.driver.ready; ip link show wlan0 2>/dev/null || true'
```

## 2026-06-08 display history audit

STATE-ONLY AUDIT, 2026-06-08: display bring-up history was reread against the
fresh event-flow capture. The current conclusion is that PQ/HWC/RDMA/logical
composition, high-level LCM params, boot-time force-reinit, TPS bus/bias/reset,
and generic DCS/backlight command paths should not be repeated as the next
first-frontier patches.

Detailed audit:
`/srv/forge/android/meizu_m6/captures/20260608-105454-m6-display-event-flow-diag-711HEBSR277K5/DISPLAY_HISTORY_AUDIT.md`.

INFERENCE: if the panel is still physically black during DSI full-BIST, the
remaining display frontier is panel-side HS video / optical output acceptance:
MIPI lane electrical mapping or polarity, PHY drive/settle hidden state,
private ILI9881P page state, or an LK-only DSI/PHY/panel side effect. Reuse
existing `m6_dsi_snapshot`, `m6_dsi_dcs_status:stock_pages`, and
`m6_dsi_bist_full:<rgb>` diagnostics before writing new behavior patches.

## 2026-06-08 display/WMT subagent audit and MSDC2-HIF diagnostic patch

PATCH HISTORY, **DIAGNOSTIC**, 2026-06-08: add bounded WMT/MSDC2/HIF SDIO
markers for the next boot-only Wi-Fi/BT capture and record the display
subagent frontier as capture-only. This patch intentionally does not change
display timing, panel init, DSI/MIPITX writes, SDIO power sequencing, callback
control flow, or WMT return values.

Hypothesis: FACT: safe-iomap boot identity is proven by matching flashed boot
and Android readback sha256
`22497e9bc4b25a6fadab5410fbf4e5be2dac97592652cb98c61e723391641bfa` in
`/srv/forge/android/meizu_m6/captures/20260608-2321-m6-rtcal-safe-iomap-after-flash-711HEBSR277K5/sha256sums.txt`;
the same capture reached `sys.boot_completed=1` on kernel `3.18.140 #47`.
FACT: live DSI evidence in that capture proves RT-cal parity, active MIPITX
PLL/lane state, nonblack screencap, and full-BIST RGB latch/clear. INFERENCE:
without a physical LCD report, display can only advance by comparing stock
LK/DSI/MIPITX/panel hidden side effects and by rerunning the existing
`m6_dsi_snapshot`, `m6_dsi_dcs_status:stock_pages`, and `m6_dsi_bist_full`
capture commands. FACT: WMT reads CONSYS chip `0x00000326`, then reports
`SDIO_HW plat_on slot=2 ret=0`, but runtime logs show `M6 CMB board_sdio_ctrl
port=2 on=1 cb=(null) data=(null)` followed by `M6 SDIO no supported func
probed` and `SDIO_FUNC ... ret=-8`; Bluetooth fails downstream with `STP Not
Ready`. HYPOTHESIS: the earliest Wi-Fi/BT blocker is MSDC2/WMT host
integration: MSDC2 SDIO either does not probe/register `request_sdio_eirq` and
`register_pm`, or registration happens but the SDIO function still never
enumerates after WMT power-on.

Evidence:
- Current safe-iomap capture:
  `/srv/forge/android/meizu_m6/captures/20260608-2321-m6-rtcal-safe-iomap-after-flash-711HEBSR277K5`.
- Current boot artifact:
  `/srv/forge/android/export/meizu_m6_artifacts/20260608-0815-m6-dsi-rtcal-safe-iomap-diag-bootonly/boot-m6-dsi-rtcal-safe-iomap-diag-20260608.img`.
- Matching System.map:
  `/srv/forge/android/export/meizu_m6_artifacts/20260608-0815-m6-dsi-rtcal-safe-iomap-diag-bootonly/System.map`,
  sha256 `c1e7052cf273e2b37dba5d4ee4e28cf8e0dc316e3c020a25da119c8abc7f9bd4`.
- Display subagent conclusion: do not patch PQ/HWC/RDMA/OVL, generic DCS,
  backlight, reset/bias, timing/PLL, or `page5_2a` without a fresh mismatch;
  BIST is controller proof, not physical photon proof.
- WMT/MSDC2 subagent conclusion: do not chase BT HAL, Wi-Fi firmware, STP ID
  tables, or unsupported function IDs until a real SDIO function is present or
  the MSDC2 callback registration gap is closed.

Files changed:
- `kernel-3.18/drivers/misc/mediatek/connectivity/common/common_detect/mtk_wcn_stub_alps.c`:
  logs SDIO EIRQ DT lookup, parsed IRQ/request result, PM callback storage, and
  PM callback invocation/return.
- `kernel-3.18/drivers/mmc/host/mediatek/mt6755/msdc_io.c`: logs MSDC2
  DT-init/of-parse entry, DT availability, `CFG_DEV_MSDC2` compile gate,
  `host_function`, supplies, and resulting SDIO callback pointers.
- `kernel-3.18/drivers/mmc/host/mediatek/mt6755/sd.c`: logs MSDC2 probe entry,
  `mmc_alloc_host`/`msdc_dt_init` failures, and the exact calls into
  `request_sdio_eirq` and `register_pm`.
- `kernel-3.18/drivers/misc/mediatek/connectivity/common/common_main/linux/hif_sdio.c`:
  logs HIF client table registration, driver registration, real `sdio_func`
  probe-list insertion, function enable result, and block-size setup result.
- `BRINGUP_STATE.md`: records the two-subagent conclusion, patch category,
  evidence, expected markers, rollback condition, and next verification.

Why each file changed: `mtk_wcn_stub_alps.c` is where WMT sees `cb=NULL` while
still returning SDIO_HW success. `msdc_io.c` and `sd.c` own the DT-derived
MSDC2 host setup that should install the PM/EIRQ callbacks. `hif_sdio.c` owns
the client/probed-list split that distinguishes "MSDC2 never enumerated" from
"enumerated but unsupported/unregistered". The display finding is state-only
because existing DSI debugfs already dumps the needed controller/MIPITX/BIST
state; without physical verification, a display behavior patch would be
speculative.

Expected next marker: in the next boot capture, WMT should show one of these
branches:
1. `M6 MSDC2 of_parse ... cfg_dev_msdc2=1`, `M6 MSDC2 register_pm call`, and
   `M6 CMB SDIO register_pm stored cb=<non-null>` before WMT SDIO on.
2. No MSDC2 probe/of-parse/register markers, proving the DT/platform driver
   bind is earlier than WMT.
3. Callback registration is present and `M6 CMB SDIO on invoking pm cb` fires,
   but no `M6 SDIO probed-list add` appears, moving the blocker to SDIO
   electrical/power/pinctrl/command enumeration.
4. A real `M6 SDIO probed-list add` appears, moving the blocker above SDIO
   enumeration into STP/Wi-Fi client registration or firmware.

For display, the next non-visual capture should contain `M6 DSI rtcal`,
`M6 DSI snapshot`, `phydecode`, `m6_dsi_dcs_status:stock_pages`, RGB
`m6_dsi_bist_full`, `/d/mtkfb`, SurfaceFlinger dump, and screencap from the
same verified boot. Do not draw a physical-visibility conclusion without a
human/camera report.

Rollback condition: revert this diagnostic patch if it regresses ADB boot,
storage/eMMC/SD stability, WMT power-on, or produces log spam that prevents
normal capture. Revert any future display behavior patch if it changes DSI
timing/route behavior without a fresh stock/runtime mismatch.

Verification commands:

```sh
CAP=/srv/forge/android/meizu_m6/captures/<new-safe-iomap-msdc2-hif-capture>
rg -n 'M6 MSDC2|M6 CMB SDIO|M6 SDIO|HIF-SDIO|SDIO_FUNC|WMT SDIO' "$CAP/dmesg.txt"
rg -n 'M6 CMB SDIO register_pm|M6 MSDC2 probe|M6 MSDC2 of_parse|M6 SDIO probed-list add|M6 SDIO probed\[' "$CAP/dmesg.txt"
rg -n 'M6 DSI rtcal|M6 DSI snapshot|phydecode|m6_dsi_bist_full|stock_pages|BIST_CON|BIST_PATTERN' "$CAP/dmesg.txt" "$CAP"/*.txt
```

Build verification result, 2026-06-08: `Image.gz-dtb` built successfully with
the existing M6 out-dir `/srv/forge/work/m6-source-kernel-manual-20260520/out`
and boot-only artifact was packed from the verified safe-iomap ramdisk/config.
Artifact directory:
`/srv/forge/android/export/meizu_m6_artifacts/20260608-0909-m6-msdc2-hif-diag-bootonly`.
Boot image:
`boot-m6-msdc2-hif-diag-20260608.img`, sha256
`08837a1615800e0da47c7d863f64cb09e2cdce2f64f6962bf0175377ffdda531`.
`Image.gz-dtb` sha256
`54cc766eaf95e3ac01c1678cc965dbf0561530222ca22333edfac41c1610c5a7`;
`System.map` sha256
`d74595d92e7c726113539ac87060c709b5e8597dfda2b031cbf4bee438a190bc`;
`vmlinux` sha256
`c860f08d189bbdd29a4c0983932232dc00aa7dd5eaf9df0b28c2513c9f2c5019`;
`kernel.config` sha256
`698b6764b989ef0bab75c0e6d6c291706e6a4a8d527d6a59347ad8c966d1fdd1`.
`abootimg -x` verification passed: unpacked `zImage`, `initrd.img`, and
`bootimg.cfg` match the artifact inputs.

FLASH/CAPTURE RESULT, 2026-06-08: the boot-only artifact
`/srv/forge/android/export/meizu_m6_artifacts/20260608-0909-m6-msdc2-hif-diag-bootonly/boot-m6-msdc2-hif-diag-20260608.img`
was flashed to `/dev/block/platform/mtk-msdc.0/11230000.msdc0/by-name/boot`
on device `711HEBSR277K5`. A full 16 MiB postflash readback
`postflash-boot-readback-msdc2-hif-diag-16m.img` matched the local boot image
byte-for-byte, sha256
`08837a1615800e0da47c7d863f64cb09e2cdce2f64f6962bf0175377ffdda531`.
The phone rebooted to Android with `sys.boot_completed=1` and kernel
`3.18.140 #48 SMP PREEMPT Mon Jun 8 09:08:26 CDT 2026`.

Runtime capture:
`/srv/forge/android/meizu_m6/captures/20260608-0920-m6-msdc2-hif-diag-after-flash-711HEBSR277K5`.
FACT: Wi-Fi/BT still fails at the SDIO host boundary. WMT repeatedly reads
CONSYS chip `0x00000326`, calls `SDIO_HW` for slot 2, and `board_sdio_ctrl()`
still logs `cb=(null) data=(null)`, followed by `M6 SDIO no supported func
probed` and `WMT turn on WIFI fail`. No `M6 MSDC2 probe`,
`M6 MSDC2 of_parse`, `M6 MSDC2 register_pm`, or
`M6 CMB SDIO register_pm` marker appears. `/sys/bus/mmc/devices` shows only
`mmc0:0001` eMMC (`QE63MB`) and no SDIO function. INFERENCE: the current
runtime blocker is earlier than WMT callback invocation: the active kernel/DTB
is not binding/probing MSDC2 SDIO as assumed, or the donor-derived source/DTS
does not represent the actually active hardware contract. Treat source/DTS as
suspect until confirmed by the booted DTB and runtime probe markers.

Root display refresh capture:
`/srv/forge/android/meizu_m6/captures/20260608-0935-m6-display-lowlevel-root-refresh-711HEBSR277K5`.
FACT: the same kernel `#48` still proves the low-level display boundary below
SurfaceFlinger/HWC and below ordinary DDP/RDMA/DSI-controller state. The root
capture shows backlight sysfs write to `255`, LCM backlight callback
`request=255 dcs51=0xff`, SurfaceFlinger Built-in Screen `powerMode=2`,
visible HWC layers plus `FB TARGET`, and DisplayManager state `ON`. DSI
markers show RT-cal parity `raw_valid=1 raw=0x6666699` with LK/live/saved
RT values `0x6/0x6/0x6/0x6/0x6`, MIPITX lanes
`0x603/0x601/0x601/0x601/0x601`, active PLL/power state, and stable
VM/timing snapshots. `m6_dsi_dcs_status:stock_pages` reads private panel pages
and again reports `page5_2a=0x18`. RGB full-BIST latches controller self-test
state (`BIST_CON=0x200446`, `self_pat=1`, `bist_en=1`, `fix=1`, `lane=4`)
and disable clears `BIST_CON=0x0`. ATA/DCS reads still return panel identity
`display_id=15 20 00`, `power_mode=9c`, and `pixel_format=07`.

INFERENCE: if the physical panel remains lit black during these BIST windows,
do not reopen PQ, HWC, RDMA/OVL, generic backlight, reset/bias/TPS, or the
visible panel init table as the first frontier. The remaining display problem
is below or beside controller-visible state: stock-only MIPITX/DSI/panel side
effects, lane electrical polarity/mapping/drive/settle, private ILI9881P
acceptance state, or board-level panel optical/LED behavior that Linux cannot
observe through DCS/BIST registers. Stock boot/LK/runtime evidence wins over
the donor source tree whenever they disagree.

## 2026-06-08 DSI PHY RT-cal / VM-payload diagnostic

PATCH HISTORY, **DIAGNOSTIC**, 2026-06-08: add bounded DSI markers for the
stock-LK-derived RT calibration frontier, MIPITX enabled/skip decisions,
VM command payload registers, and `clk_lp_per_line` timing state. This is not
a display behavior change and must not be treated as a fix.

Hypothesis: FACT: the latest page5 trace capture
`/srv/forge/android/meizu_m6/captures/20260608-0736-m6-page5-2a-trace-after-flash-711HEBSR277K5`
proved `page5_2a=0x18` is a post-`0x29 display on` / panel-side transition,
not a static init-table value to patch. FACT: userspace composition, HWC/SF,
RDMA event flow, BIST latch, public DCS reads, and high-level LCM params were
already proved alive or non-primary by the history audit. FACT: the stock LK
reverse found `fcn.46013b40` reading physical `0x10206190` and decoding RT
codes as `C=(v>>16)&0xf`, `D3=(v>>8)&0xf`, `D2=(v>>12)&0xf`,
`D1=(v>>20)&0xf`, `D0=(v>>24)&0xf`, with zero fallback to `8`; Linux instead
reuses saved MIPITX lane register RT fields in `DSI_PHY_clk_setting()`.
HYPOTHESIS: the remaining black-display frontier may be below Android
composition/PQ/RDMA/OVL/backlight/page5: MIPI PHY RT calibration source,
PHY setup skip when `MIPITX_IsEnabled()` is true, VM command payload parity,
or LP-per-line timing parity versus stock LK.

Evidence:
- Stock reverse inputs:
  `/srv/forge/android/meizu_m6/captures/20260530-stock-lk-boot-reverse-inputs/lk_display_reverse.md`.
- Latest page5 closure capture:
  `/srv/forge/android/meizu_m6/captures/20260608-0736-m6-page5-2a-trace-after-flash-711HEBSR277K5/CAPTURE_VERDICT.md`.
- Failed unsafe diagnostic artifact, flashed and read back:
  `/srv/forge/android/export/meizu_m6_artifacts/20260608-0750-m6-dsi-rtcal-vmpayload-diag-bootonly/boot-m6-dsi-rtcal-vmpayload-diag-20260608.img`,
  boot sha256 `aeb43ded75fa92dcd688c176fdc4892fd790ee5fecb67170061bfc64319399ea`.
  The first 16 MiB of `boot-readback-after-flash.img` were byte-identical to
  the boot image; `boot-readback-after-flash-trimmed.img` has the same sha256.
- Runtime result for that unsafe diagnostic: after `adb reboot`,
  `711HEBSR277K5` did not reappear on ADB during a 120-iteration / 10-minute
  wait. `adb -H 127.0.0.1 -P 15038 devices -l` still showed the neighboring
  `810BBMM22D7S` device and `ss -ltnp` still showed `127.0.0.1:15038`, so this
  is a failed M6 diagnostic boot / no-ADB result, not a tunnel outage.
- Safer rebuilt artifact waiting for the next recovery/ADB window:
  `/srv/forge/android/export/meizu_m6_artifacts/20260608-0815-m6-dsi-rtcal-safe-iomap-diag-bootonly/boot-m6-dsi-rtcal-safe-iomap-diag-20260608.img`,
  boot sha256 `22497e9bc4b25a6fadab5410fbf4e5be2dac97592652cb98c61e723391641bfa`;
  `Image.gz-dtb` sha256 `bbc0bbbc67868e2f9822890adfb2512165ce701272f1dde30f286a056fe3b7e0`;
  `System.map` sha256 `c1e7052cf273e2b37dba5d4ee4e28cf8e0dc316e3c020a25da119c8abc7f9bd4`;
  `vmlinux` sha256 `6425941c3cedbb2a0550e2346b12fe73c34a42838f2dd3c941f24fda05f57a95`;
  `kernel.config` sha256 `698b6764b989ef0bab75c0e6d6c291706e6a4a8d527d6a59347ad8c966d1fdd1`;
  `verify-unpack/kernel` matches the `Image.gz-dtb` sha256.

Files changed:
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_dsi.c`: adds bounded
  `M6 DSI rtcal[...]`, `M6 DSI mipitx-decision[...]`,
  `M6 DSI snapshot[...] VM_PAYLOAD=...`, and `M6 DSI lp_per_line[...]`
  markers. The initial direct virtual read of `0xF0206190` was replaced after
  the failed boot by a one-time `ioremap_nocache(0x10206190, 4)` path that
  prints `raw_valid=0/1`.

Why each file changed: `ddp_dsi.c` owns DSI PHY programming, VM command setup,
MIPITX enabled checks, and LP-per-line timing, so this is the narrow owner for
the stock-LK parity evidence. The safer `ioremap_nocache()` form avoids the
direct unmapped-virtual alias risk while preserving the same physical stock
truth read when the mapping succeeds.

Expected next marker: after flashing the safe-iomap boot, dmesg should contain
`M6 DSI rtcal[...] phys10206190 raw_valid=...`, `M6 DSI
mipitx-decision[init]`, `M6 DSI mipitx-decision[config]`,
`M6 DSI snapshot[...] VM_PAYLOAD=...`, and `M6 DSI lp_per_line[config]`.
If `raw_valid=1`, compare decoded LK RT values against `live_rt` and
`saved_rt`. If `raw_valid=0` but the device boots, continue with live/saved RT,
VM payload, and `MIPITX_IsEnabled()` skip-path evidence.

Rollback condition: if the safe-iomap artifact also fails to reach ADB or
regresses previously working `sys.boot_completed=1`, BIST latch, or valid
screencap/HWC composition, revert the RT-cal physical read entirely and keep
only already-proven mapped MIPITX/DSI register snapshots before attempting a
proper RT behavior patch.

Verification commands:

```sh
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 devices -l
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell getprop sys.boot_completed
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell dmesg \
  | rg 'M6 DSI rtcal|mipitx-decision|VM_PAYLOAD|lp_per_line|M6 DSI snapshot|page5_2a_trace|m6_dsi_bist_full'
sha256sum \
  /srv/forge/android/export/meizu_m6_artifacts/20260608-0815-m6-dsi-rtcal-safe-iomap-diag-bootonly/boot-m6-dsi-rtcal-safe-iomap-diag-20260608.img \
  /srv/forge/android/export/meizu_m6_artifacts/20260608-0815-m6-dsi-rtcal-safe-iomap-diag-bootonly/Image.gz-dtb \
  /srv/forge/android/export/meizu_m6_artifacts/20260608-0815-m6-dsi-rtcal-safe-iomap-diag-bootonly/System.map
```

CAPTURE RESULT, 2026-06-08: the safe-iomap DSI RT-cal diagnostic boot was
flashed over the previously failed unsafe RT-cal image and reached Android.

FACT: recovery preflash capture:
`/srv/forge/android/meizu_m6/captures/20260608-2318-m6-rtcal-failedboot-recovery-preflash-711HEBSR277K5`.
The preflash boot readback
`/srv/forge/android/export/meizu_m6_artifacts/20260608-0815-m6-dsi-rtcal-safe-iomap-diag-bootonly/boot-readback-before-safe-flash.img`
hashed to
`aeb43ded75fa92dcd688c176fdc4892fd790ee5fecb67170061bfc64319399ea`,
proving the failed unsafe RT-cal image was still installed before this flash.

FACT: flashed boot artifact:
`/srv/forge/android/export/meizu_m6_artifacts/20260608-0815-m6-dsi-rtcal-safe-iomap-diag-bootonly/boot-m6-dsi-rtcal-safe-iomap-diag-20260608.img`,
sha256
`22497e9bc4b25a6fadab5410fbf4e5be2dac97592652cb98c61e723391641bfa`.
Post-flash recovery and Android boot readbacks both match that sha256:
`boot-readback-after-safe-flash.img` and
`boot-readback-current-safe-android.img`.

FACT: after reboot, M6 serial `711HEBSR277K5` reached ADB `device`,
`sys.boot_completed=1`, `ro.bootmode=normal`, Android `8.1.0`, and kernel
`Linux localhost 3.18.140 #47 SMP PREEMPT Mon Jun 8 07:55:32 CDT 2026 aarch64`.
Post-flash capture:
`/srv/forge/android/meizu_m6/captures/20260608-2321-m6-rtcal-safe-iomap-after-flash-711HEBSR277K5`.
`screencap-after-live-debugfs.png` is a valid 720x1280 PNG with nonblack RGB
range `min=0 max=255 mean~75/75/77`, sha256
`c44d212f0cf696569c03d98245f886705db9dbd1900800ff5f60bd2fcad3e6a1`.

FACT: the live debugfs pass in that capture proves the safe-iomap RT-cal
markers are present and the full-BIST path still works. `dmesg-live-dsi-debugfs.txt`
contains `M6 DSI rtcal[...] phys10206190 raw_valid=1 raw=0x6666699` and decodes
LK/live/saved RT values as `0x6/0x6/0x6/0x6/0x6`. DSI/MIPITX snapshots show
`pll en=1`, `pwr_on=1`, `iso=0`, `ack=1`, `phy_map d0/d1/d2/d3/c/lprx=0/1/2/3/4/0`,
and stable lane registers `0x603/0x601/0x601/0x601/0x601`.

FACT: full-BIST RGB windows latch the stronger controller self-test bits:
red `BIST_PATTERN=0xff0000`, green `BIST_PATTERN=0xff00`, blue
`BIST_PATTERN=0xff`; each sets `BIST_CON=0x200446`, `self_pat=1`, `bist_en=1`,
`fix=1`, `lane=4`, `timing=0x20`, and DSI `STATE7` samples in video data
period. `m6_dsi_bist_full:0` clears the path back to `BIST_CON=0x0`,
`self_pat=0`, `bist_en=0`.

FACT: other live display evidence remains internally healthy: the backlight
path writes `request=255 dcs51=0xff`; `/d/mtkfb` reports primary display alive,
LCM `ili9881p_hd_dsi_txd`, 720x1280 DSI video mode + CMDQ, DIRECT_LINK, and
RDMA0 transfer around 60 fps. `m6_dsi_dcs_status:stock_pages` still reads
private ILI9881P pages; page5 register `0x2a` remains `0x18`.

INFERENCE: if the physical LCD is still lit black during these verified full
BIST windows, the next display frontier is not PQ/HWC/RDMA/public-DCS/backlight.
It is below or beside the controller-visible path: stock LK hidden DSI/MIPITX
side effects, panel-side HS video acceptance, lane electrical polarity/drive,
or private page state that Linux still does not reproduce. Keep the safe-iomap
boot as the current recoverable diagnostic baseline; do not reflash the unsafe
`aeb43...` RT-cal image.

FACT: non-display blockers visible in the same boot are still live. WMT/SDIO
fails before function enumeration with repeated `hif_sdio_stp_on:M6 SDIO no
supported func probed` and `SDIO_FUNC ctrl func=0 on=1 ret=-8`; Bluetooth is
downstream of that. Charger markers are alive with USB type 1 and successful
500 mA input/charge-current writes. RIL diagnostic markers run and parse the
resident MD image, but modem state still needs a separate post-flash capture
verdict.

Expected next marker: a stock reverse / parity patch should compare stock LK
DSI/MIPITX side effects against the safe-iomap runtime values above, especially
MIPITX lane/polarity/drive/settle, VM payload, page5 transitions, and any
post-`0x29` private writes. For Wi-Fi/BT, the next proof is an MSDC2/SDIO trace
that reaches a real function under `/sys/bus/sdio/devices` or proves the power
/ IRQ / pinctrl boundary that prevents enumeration.

Rollback condition: revert only if a later patch loses this baseline:
safe-iomap boot readback match, `sys.boot_completed=1`, nonblack screencap,
DSI RT-cal markers, full-BIST latch/clear, or `/d/mtkfb` RDMA0 transfer. Do
not revert this state-only record.

## 2026-06-08 integrated stock-pages/full-BIST display diagnostic boot

PATCH HISTORY, **ISOLATION + DIAGNOSTIC**, 2026-06-08: integrate the pending
DSI full-BIST debugfs command with a safe manual `m6_dsi_dcs_status:stock_pages`
path that stops video mode, reads selected ILI9881P public/private page
registers, restores page 0, then restarts the display path. This is not a
display fix. It does not change boot-time LCM params, PQ policy, DDP route,
HWC, userspace composition, charger policy, camera behavior, RIL behavior, or
WMT behavior. It only adds explicit post-boot trigger points for the next
capture.

Hypothesis: FACT: the display history audit above rejects repeating PQ, HWC,
RDMA event-flow, forced boot-time reinit, TPS/reset/bias, high-level panel
params, and generic DCS/backlight as the next first frontier. FACT: the fresh
current-boot BIST capture
`/srv/forge/android/meizu_m6/captures/20260608-1215-m6-currentboot-stockpages-fullbist-711HEBSR277K5`
proved DSI full-BIST register latch on the current boot but also proved the
running image did not contain a working `m6_dsi_dcs_status:stock_pages`
debugfs hook. INFERENCE: the next useful split is not another logical display
patch; it is whether the panel's private ILI9881P page state after LK handoff
matches the stock-derived expectations while the DSI controller can emit a
latched self-pattern. HYPOTHESIS: a private page sentinel, MIPI PHY/lane hidden
side effect, or LK-only panel state remains mismatched even though LP DCS reads,
backlight, DSI START, and controller BIST state look alive.

Evidence:
- History audit:
  `/srv/forge/android/meizu_m6/captures/20260608-105454-m6-display-event-flow-diag-711HEBSR277K5/DISPLAY_HISTORY_AUDIT.md`.
- Current-boot prepatch capture:
  `/srv/forge/android/meizu_m6/captures/20260608-1215-m6-currentboot-stockpages-fullbist-711HEBSR277K5`.
- FACT: that capture showed full-BIST latch values including
  `BIST_CON=0x200446`, `BIST_PATTERN=0xff0000`, `self_pat=1`, `bist_en=1`,
  `fix=1`, `lane=4`, DSI `START=0x10001`, `MODE=0x3`, `TXRX=0x1003c`, and
  active MIPITX lane/PLL state; after disable it returned to `BIST_CON=0x0`.
- FACT: the same capture showed public LP DCS reads alive:
  `display_id=15 20 00`, `display_status=80 03 06 00`, `power_mode=9c`,
  `pixel_format=07`, and ID registers `15/20/00`.
- FACT: string checks against the exact newly built `Image.gz-dtb` prove the
  new image contains `m6_dsi_dcs_status`, `M6 LCM stock_pages`, `M6_CAM`,
  `M6_FLASH`, `M6_RIL_DIAG`, and `M6_CHG` markers. The `gzip -cd` warning
  about trailing garbage is expected for `Image.gz-dtb`.
- Built artifact:
  `/srv/forge/android/export/meizu_m6_artifacts/20260608-0645-m6-stockpages-fullbist-integrated-bootonly/boot-m6-stockpages-fullbist-integrated-20260608.img`.
- Artifact sha256 identities:
  `boot-m6-stockpages-fullbist-integrated-20260608.img`
  `729952aa18667868e8662437ec8348d0c3fe5f9fecaa35daf11e5f4a67ee74a0`;
  `Image.gz-dtb`
  `92e1bc42f0bf37169f4160678dde8c1e3e89bd037d187868fff775a76b0bf64e`;
  `System.map`
  `e028702c9391b0111eff38fea2249e83709db9edbeb57cf5db35f3e7c9045426`;
  `vmlinux`
  `d656fb80d1d7d70cfa621ea1ac779455962181828fb66c4dad6d225600b620c5`;
  `kernel.config`
  `698b6764b989ef0bab75c0e6d6c291706e6a4a8d527d6a59347ad8c966d1fdd1`;
  ramdisk `initrd.img`
  `7de975b4485324f4a76eb44fa4cc61472e829421e8d27e31f50d80b984cb4a4f`.
- Pack verification: `sha256sum -c SHA256SUMS` passed in the artifact
  directory. `abootimg -i` reports 16 MiB boot image, page size 2048,
  kernel address `0x40080000`, ramdisk address `0x45000000`, tags
  `0x44000000`, name `1552631950`, and the existing userdebug/permissive
  cmdline.

Files changed:
- `kernel-3.18/drivers/misc/mediatek/lcm/ili9881p_hd_dsi_txd/ili9881p_hd_dsi_txd.c`:
  adds bounded `M6 LCM stock_pages[...]` private/public register reads and
  page restore.
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/primary_display.c`: adds a
  manual path wrapper that stops video mode before stock-page reads and
  restarts/triggers the path after the reads.
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/primary_display.h`: declares
  the wrapper for the debugfs command parser.
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/disp_debug.c`: exposes
  `m6_dsi_dcs_status[:stock_pages]` and fixes the parser to tolerate the
  newline written by `echo`.
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_dsi.c` and
  `ddp_dsi.h`: include the previously pending manual full-BIST command that
  is used for the same capture window.
- `BRINGUP_STATE.md`: records the patch category, artifact identity, expected
  markers, rollback condition, and verification commands.

Why each file changed: the LCM driver is the only local owner of ILI9881P page
selection/read helpers. `primary_display.c` owns the safe video-mode stop/start
boundary already used by manual ATA paths. `disp_debug.c` is the existing
bounded root-triggered command surface for M6 display isolation. `ddp_dsi.c`
owns DSI self-pattern/BIST registers. The state file is the required durable
identity and anti-repeat record.

Expected next marker: after flashing this boot image and collecting a fresh
capture, dmesg must show `M6 LCM stock_pages: stop video path begin`,
`M6 LCM stock_pages[...] begin`, per-register `M6 LCM stock_pages[...] page=...`
lines, and `M6 LCM stock_pages[...] end reset_page=0`, followed by a latched
`m6_dsi_bist_full:0x00ff0000` window and a clean disable back to
`BIST_CON=0x0`. If the physical panel remains black during the BIST window, do
not reopen PQ/HWC/RDMA. Compare the private page values and MIPITX/DSI snapshot
against stock LK hidden side effects.

Rollback condition: revert this diagnostic if the verified flashed image fails
to boot/ADB, if `m6_dsi_dcs_status:stock_pages` hangs the display path before
printing the end marker, if DCS reads regress from the previous public
`display_id/status/power_mode` values, if BIST no longer latches, or if the
manual page-select/read path causes new sustained DSI/CMDQ/ESD failures after
the trigger window.

Verification commands:

```bash
cd /srv/forge/android/export/meizu_m6_artifacts/20260608-0645-m6-stockpages-fullbist-integrated-bootonly
sha256sum -c SHA256SUMS
abootimg -i boot-m6-stockpages-fullbist-integrated-20260608.img
gzip -cd Image.gz-dtb 2>/tmp/m6-stockpages-fullbist-gzip.err | strings | rg 'm6_dsi_dcs_status|M6 LCM stock_pages|M6_CAM|M6_FLASH|M6_RIL_DIAG|M6_CHG'

A='adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5'
CAP=/srv/forge/android/meizu_m6/captures/<next-m6-stockpages-fullbist-capture>
$A exec-out 'dd if=/dev/block/platform/mtk-msdc.0/11230000.msdc0/by-name/boot bs=4096 count=4096 2>/dev/null' > "$CAP/boot-readback.img"
sha256sum "$CAP/boot-readback.img" boot-m6-stockpages-fullbist-integrated-20260608.img
$A shell 'dmesg -C; svc power stayon true; settings put system screen_off_timeout 2147483647; input keyevent 224; settings put system screen_brightness 255; echo 255 > /sys/class/leds/lcd-backlight/brightness; echo m6_dsi_snapshot > /d/mtkfb; echo m6_dsi_dcs_status:stock_pages > /d/mtkfb; echo m6_dsi_bist_full:0x00ff0000 > /d/mtkfb; sleep 3; echo m6_dsi_snapshot > /d/mtkfb; echo m6_dsi_bist_full:0 > /d/mtkfb; echo ata > /d/mtkfb; sleep 1; dmesg' > "$CAP/dmesg-stockpages-bist-ata.txt"
rg -n 'M6 LCM stock_pages|M6 DSI snapshot|BIST_CON|BIST_PATTERN|self_pat|M6 LCM ATA|display_status|power_mode|M6_CAM|M6_FLASH|M6_RIL_DIAG|M6_CHG' "$CAP"
```

CAPTURE RESULT, 2026-06-08: the integrated diagnostic boot was flashed and
captured successfully.

FACT: root-trigger capture:
`/srv/forge/android/meizu_m6/captures/20260608-064936-m6-stockpages-fullbist-integrated-root-711HEBSR277K5`.
Capture verdict:
`/srv/forge/android/meizu_m6/captures/20260608-064936-m6-stockpages-fullbist-integrated-root-711HEBSR277K5/CAPTURE_VERDICT.md`.

FACT: boot readback in the capture matches the flashed artifact sha256
`729952aa18667868e8662437ec8348d0c3fe5f9fecaa35daf11e5f4a67ee74a0`; running
kernel is `#44 SMP PREEMPT Mon Jun 8 06:42:43 CDT 2026`, and Android reached
`sys.boot_completed=1`.

FACT: display markers closed most private page-state checks but left one
specific mismatch open. `m6_dsi_dcs_status:stock_pages` printed 67 reads and
reset page 0. Page1/page6/page2 and most page5 values match the current
`ili9881p_hd_dsi_txd` init table, but page5 register `0x2a` reads `0x18` while
the current source and stock LK decode write `0x14`. Full-BIST latched red with
`BIST_CON=0x200446`, `self_pat=1`, `bist_en=1`, `fix=1`, `lane=4`; disable
returned to `BIST_CON=0x0`. Public DCS still reads `display_id=15 20 00`,
`display_status=80 03 06 00`, `power_mode=9c`, `pixel_format=07`.

INFERENCE: if the physical LCD stayed black during this verified BIST window,
the next display work should move to stock reverse / register parity for hidden
DSI/MIPITX side effects and the single `page5_2a` sentinel. Do not repeat
PQ/HWC/RDMA, public DCS, TPS/reset/bias, or boot-time forced LCM reinit patches
without a new mismatch. Do not convert `page5_2a` from `0x14` to `0x18` as a
PROPER-FIX yet: stock LK evidence still says the init write is `0x14`.

FACT: the same boot captured current non-display blockers:
- Charger is active: `chrdet:1`, `VChr` around 4.3-4.4 V, `CHR_Type 1`, and
  `[M6_CHG] set_input_current` / `set_chargecurrent` return `0`.
- Wi-Fi fails at WMT/SDIO: `wlan.driver.status=unloaded`, Wi-Fi HAL reports
  `Failed to write wlan fw path param: I/O error`, and kernel markers show
  `M6 CMB SDIO on/off port=2 cb=(null) data=(null) wifi_irq=4294967295`.
- Bluetooth is downstream of WMT/STP: `STP Not Ready`, `wmt_lib_put_act_op
  ... result:-3`, and `BT_open: WMT turn on BT fail!`.
- RIL uses resident MD image path: `M6_RIL_DIAG start run_env_ready=1` and
  `bypass_hdr ret=0 ... size=0xf93fd0`; MD1 still transitions to `exception`.
- Camera provider remains zero-device; `CHECK_SENSOR_ID` reaches
  `s5k4h8mipiraw` on socket 2 / bus 2 and fails at
  `i2c send fail bus=2 client=bus2 adapter=1 addr=0x5a reg=0x6f12 ret=-22`.

CAPTURE RESULT, 2026-06-08 live `page5_2a` reinit/BIST follow-up:

FACT: live capture:
`/srv/forge/android/meizu_m6/captures/20260608-070144-m6-page5-2a-live-reinit-bist-711HEBSR277K5`.
Boot readback sha256 again matches the flashed integrated artifact:
`729952aa18667868e8662437ec8348d0c3fe5f9fecaa35daf11e5f4a67ee74a0`.

FACT: baseline `stock_pages[2]` reads `page5_2a=0x18`. A manual
`m6_lcm_reinit:1` then runs a full Linux LCM init, logs
`M6 LCM table[init] idx=12 cmd=0x2a count=1 p=14 00 00 00 force=1`, and ends
`ret=0`. Immediately after that, `stock_pages[3]` still reads
`page5_2a=0x18`. During the later full-BIST window, `stock_pages[4]` again
reads `page5_2a=0x18`, while BIST remains latched at `BIST_CON=0x200446` and
returns to `BIST_CON=0x0` after disable.

INFERENCE: `page5_2a=0x18` is not only stale LK handoff state; it survives a
verified Linux reinit that writes `0x14`. The next non-repeating display patch
should be DIAGNOSTIC: trace immediate readback after init-table entry 12 and
after later page5/page0/video-start boundaries, and compare stock LK/stock
kernel for hidden post-init DSI/MIPITX or page writes. A behavior change to
`0x18` needs fresh evidence that stock also writes or requires `0x18`, or an
isolation patch with an explicit rollback, not a PROPER-FIX claim.

PATCH HISTORY, **DIAGNOSTIC**, 2026-06-08: add bounded `M6 LCM
page5_2a_trace[...]` read-after-write markers inside the stock `init_setting[]`
push path. This does not change the init table, page5 `0x2a` write value,
timings, reset/bias policy, DDP route, PQ, RDMA, DSI PHY, or userspace display
policy.

Hypothesis: FACT: the live capture above proves Linux writes page5 `0x2a=0x14`
during manual reinit, but the later private readback still returns `0x18`.
FACT: stock LK decode also writes `cmd=0x2a data=14`. HYPOTHESIS: the register
either reads as `0x18` immediately after the write, changes later in the init
cluster, changes when returning to page0/display-on/video-start, or is being
affected by a hidden stock/LK side effect outside the visible table.

Evidence:
- Live capture:
  `/srv/forge/android/meizu_m6/captures/20260608-070144-m6-page5-2a-live-reinit-bist-711HEBSR277K5`.
- Source line: current `init_setting[]` writes page5 `0x2A` with data `0x14`.
- Stock LK decode:
  `/srv/forge/android/meizu_m6/captures/20260530-stock-lk-boot-reverse-inputs/lk-ili9881p-init-table-decode.txt`
  line `12 off=0x5f794 cmd=0x2a count=1 data=14`.

Files changed:
- `kernel-3.18/drivers/misc/mediatek/lcm/ili9881p_hd_dsi_txd/ili9881p_hd_dsi_txd.c`:
  logs page5 `0x2a` readback after init-table entry 12, after the page5
  cluster, after page0 select, and after display-on.
- `BRINGUP_STATE.md`: records the evidence, expected markers, rollback, and
  verification commands.

Why each file changed: the LCM push-table path is the only place that can prove
the immediate write/read timeline without changing behavior. The state file is
the required durable anti-repeat record.

Expected next marker: after reinit or boot init, dmesg should contain
`M6 LCM page5_2a_trace[init] idx=12 phase=after-page5-2a-write ...` and the
later `after-page5-cluster`, `after-page0-select`, and `after-display-on`
phases. If idx 12 already reads `0x18`, focus on register semantics/panel
variant. If idx 12 reads `0x14` and a later phase reads `0x18`, inspect the
intervening command or boundary. If all phases read `0x14` but post-init
`stock_pages` reads `0x18`, move to DSI video-start/hidden MIPITX side effects.

Rollback condition: revert this diagnostic after one capture localizes the
transition, or immediately if the read-after-write probes regress boot,
SurfaceFlinger, DCS public reads, BIST latch, or make `m6_lcm_reinit:1` hang.

Verification commands:

```bash
cd /srv/forge/android/meizu_m6/kernel-meizu_M6-N-ex6-linux-3.18.140
git diff --check -- \
  kernel-3.18/drivers/misc/mediatek/lcm/ili9881p_hd_dsi_txd/ili9881p_hd_dsi_txd.c \
  BRINGUP_STATE.md
env CCACHE_DIR=/srv/forge/android/ccache make -C kernel-3.18 \
  O=/srv/forge/work/m6-source-kernel-manual-20260520/out \
  ARCH=arm64 \
  CROSS_COMPILE=/srv/forge/android/meizu_m6/rom-lineage-15.1-meizu_m6-experimental/prebuilts/gcc/linux-x86/aarch64/aarch64-linux-android-4.9/bin/aarch64-linux-android- \
  -j8 Image.gz-dtb
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell \
  'dmesg -C; echo m6_lcm_reinit:1 > /d/mtkfb; sleep 2; echo m6_dsi_dcs_status:stock_pages > /d/mtkfb; sleep 1; dmesg' \
  | rg 'page5_2a_trace|page5_2a|M6 LCM table\\[init\\] idx=12|M6 LCM debug reinit: end'
```

CAPTURE RESULT, 2026-06-08 page5 trace after-flash:

FACT: capture:
`/srv/forge/android/meizu_m6/captures/20260608-0736-m6-page5-2a-trace-after-flash-711HEBSR277K5`.
Verdict:
`/srv/forge/android/meizu_m6/captures/20260608-0736-m6-page5-2a-trace-after-flash-711HEBSR277K5/CAPTURE_VERDICT.md`.

FACT: flashed artifact and post-reboot boot readback match sha256
`5f88e7ea7cdd4c043fefc0deb3803e623cd73b8648a624582ced54ffa55a3d5b`. Running
kernel is `#45 SMP PREEMPT Mon Jun 8 07:10:37 CDT 2026`, and Android reached
`sys.boot_completed=1`.

FACT: the diagnostic localized the page5 `0x2a` transition. Manual
`m6_lcm_reinit:1` logs the source write
`M6 LCM table[init] idx=12 cmd=0x2a count=1 p=14 00 00 00 force=1`.
Immediate trace reads:
- `idx=12 phase=after-page5-2a-write ... read=14`;
- `idx=17 phase=after-page5-cluster ... read=14`;
- `idx=65 phase=after-page0-select ... read=14`;
- `idx=69 phase=after-display-on ... read=18`.
Later `stock_pages[1]` reads `page5_2a=18`.

INFERENCE: `page5_2a=0x18` is a post-`0x29 display on` / panel-side state
transition, not a missing Linux table write. Do not patch the visible stock
table from `0x14` to `0x18` as a fix.

FACT: logical composition is non-black on the same boot. `settings-screencap.png`
is a valid `720x1280` RGBA PNG showing Settings; pixel stats show
`nonblack_pct=100.0000`, `max=255`. A later bounded screencap attempt timed out
with exit `124` and produced a zero-byte timeout PNG, which remains a runtime
symptom but does not invalidate the non-black UI screencap.

FACT: DSI full-BIST still latches and clears:
`BIST_PATTERN=0xff0000`, `BIST_CON=0x200446`, `self_pat=1`, `bist_en=1`,
`fix=1`, `lane=4`, `timing=0x20`; after disable `BIST_CON=0x0`.

INFERENCE: if the physical LCD is still lit black during this boot/BIST window,
the remaining display frontier is not PQ, HWC, RDMA event-flow, OVL/M4U,
generic backlight, high-level LCM params, TPS/reset/bias, or page5 `0x2a`.
Continue below Android composition and below visible LCM init: stock LK/boot DSI
core reverse for MIPITX/PLL/lane/VM_CMD side effects, panel HS electrical
acceptance, or another panel-side private state not present in the visible
init table.

## 2026-06-08 RIL/MD1 CCCI image-path diagnostic patch

PATCH HISTORY, **DIAGNOSTIC**, 2026-06-08: add bounded `M6_RIL_DIAG`
markers to the CCCI CLDMA start/image-load path. This patch does not change
RIL service start, `/dev/radio` symlink creation, SELinux policy, modem boot
commands, firmware selection, image contents, or MD reset behavior.

Hypothesis: FACT: verified capture
`/srv/forge/android/meizu_m6/captures/20260608-105454-m6-display-event-flow-diag-711HEBSR277K5`
uses boot readback sha256
`f8c7fd598b2bd9bfcb0ac417ed96324df15c9e78b2ee00ba6394d8faf215abf6`; matching
`System.map` is
`/srv/forge/android/export/meizu_m6_artifacts/20260608-105112-m6-display-event-flow-diag-bootonly/System.map`
with sha256
`7da2c0be0c23d2f6f74970845b96ae0c247861c77bf492a62091755e0c6aaf85`.
FACT: RIL and phone framework are alive, but MD1 still reaches exception.
INFERENCE: `/dev/radio/pttynoti` is currently a downstream mux symptom, not the
first proven blocker. HYPOTHESIS: the next decisive split is whether CCCI is
booting from LK/radio-partition resident modem image or from Linux
`request_firmware()`, and which postfix/image/header values precede the RF
assert.

Evidence:
- `getprop.txt:55` `init.svc.ccci_mdinit=running`;
  `getprop.txt:91` `init.svc.ril-daemon=running`;
  `getprop.txt:126` `mtk.md1.status=exception`;
  `getprop.txt:197-198` `rild.libargs=-d /dev/ttyC0`, `rild.libpath=mtk-ril.so`;
  `getprop.txt:425` `service.nvram_init=Ready`.
- `service-list.txt:4` publishes `phone`, so the previous phone-service death
  is not the current blocker in this capture.
- `logcat-all.txt:880-893` RIL expects `/dev/radio/pttynoti` and gets ENOENT;
  `logcat-all.txt:1772-1806` `gsm0710muxd` uses `/dev/ttyC0` and reads
  `+EIND: 64`; `logcat-all.txt:1571,2823` time out waiting for `+EIND: 128`.
- `dmesg.txt:936-939` first decisive MD assert is
  `common/modem/el1/el1d/el1d_rf_error_check.c:161 para0=1 para1=7 para2=6`.
  `dmesg.txt:10070-10073` later `cc_irq.c:1043 para0=858936144` is a
  subsequent exception after reset, not the earliest MD failure.
- Source evidence: `init.modem.rc:51-59,68` creates `/dev/radio` and starts
  `gsm0710muxd`; `ueventd.mt6755.rc:167-168` assigns `/dev/ccci*` and
  `/dev/ttyC*` to `radio:radio`; `file_contexts:52-63,80` labels CCCI and
  `/dev/radio`; `mtk_vendor_daemons.te:124-139,174-177` grants CCCI/mux/RIL
  access. This does not prove a userspace label/init PROPER-FIX.

Files changed:
- `kernel-3.18/drivers/misc/mediatek/eccci/modem_cldma.c`: logs
  `run_env_ready`, CCCI settings/postfix, loaded/bypass MD header fields, DSP,
  and ARMv7 image fields at `md_cd_start()`.
- `kernel-3.18/drivers/misc/mediatek/ccci_util/ccci_util_lib_load_img.c`:
  logs every `request_firmware()` attempt, failure fallback, and success
  image name/size in `ccci_load_firmware()`.
- `BRINGUP_STATE.md`: records this RIL/MD1 diagnostic patch summary.

Why each file changed: `modem_cldma.c` owns the branch between resident modem
environment and Linux image loading; `ccci_util_lib_load_img.c` owns image name
construction and fallback. The capture does not prove a ROM init/sepolicy
PROPER-FIX, so the bounded DIAGNOSTIC markers are the lowest-risk next patch.

Expected next marker: next dmesg/logcat must contain `M6_RIL_DIAG start`.
If it shows `run_env_ready=1` plus `M6_RIL_DIAG bypass_hdr`, focus next on
radio-partition modem image/SBP/NVRAM/RF data. If it shows `run_env_ready=0`
plus `firmware_request_fail`, the next PROPER-FIX candidate is firmware
packaging/`firmware_class.path`. If it shows `firmware_request_ok` and the same
RF assert, focus next on image content, AP/MD header mismatch, SBP, or NVRAM.

Rollback condition: revert this diagnostic patch after one capture identifies
the CCCI image path, or immediately if it changes MD boot timing, prevents
`sys.boot_completed=1`, stops `rild`, or introduces a new earlier CCCI failure
before the expected `M6_RIL_DIAG` markers.

Verification commands:

```bash
cd /srv/forge/android/meizu_m6/kernel-meizu_M6-N-ex6-linux-3.18.140
git diff --check -- kernel-3.18/drivers/misc/mediatek/eccci/modem_cldma.c kernel-3.18/drivers/misc/mediatek/ccci_util/ccci_util_lib_load_img.c
CCACHE_DIR=/srv/forge/android/ccache make -C kernel-3.18 O=/srv/forge/work/m6-source-kernel-manual-20260520/out ARCH=arm64 CROSS_COMPILE=/srv/forge/android/meizu_m6/rom-lineage-15.1-meizu_m6-experimental/prebuilts/gcc/linux-x86/aarch64/aarch64-linux-android-4.9/bin/aarch64-linux-android- -j8 Image.gz-dtb

cd /srv/forge/android/meizu_m6
CAP=/srv/forge/android/meizu_m6/captures/<next-m6-ril-md1-capture>
sha256sum "$CAP/boot-readback.img" /srv/forge/android/export/meizu_m6_artifacts/<matching-artifact>/System.map
rg -n "M6_RIL_DIAG|el1d_rf_error_check|cc_irq.c|pttynoti|Wait \\+EIND|mtk.md1.status|rild|phone:" "$CAP/dmesg.txt" "$CAP/logcat-all.txt" "$CAP/getprop.txt" "$CAP/service-list.txt"
```

## 2026-06-08 full-ROM camera zero-device and torch diagnostic

PATCH HISTORY, DIAGNOSTIC, 2026-06-08: add bounded post-boot-actionable
camera/provider and torch markers for the fresh full-ROM zero-camera capture.
This patch does not change the stock-backed sensor list, camera power table,
GPIO/regulator sequencing, flashlight enable logic, device nodes, ROM
packaging, or sepolicy.

Hypothesis: FACT: fresh full-ROM capture
`/srv/forge/android/meizu_m6/captures/20260608-053522-full-rom-integrated-711HEBSR277K5`
uses boot sha256
`55dac1693482f41010a4bfd306b993f3f8435a61edb3f34f18fb796703149b7f`; the
boot readback matches the local full-ROM boot artifact and Android reaches
`sys.boot_completed=1`. FACT: camera provider `legacy/0` loads and
`media.camera` is registered, but `dumpsys media.camera` reports zero devices.
FACT: the MTK HAL searches sensor driver IDs `10000..10003` and `20000..20003`;
each path returns `Err-ctrlCode (I/O error)`, then `sensor ID mismatch`, then
`Error No sensor found`. FACT: `/dev/kd_camera_hw`, `/dev/kd_camera_hw_bus2`,
and `/dev/kd_camera_flashlight` exist with `system:camera` ownership and
`mtk_camera_device` label. FACT: the booted kernel config already selects the
M6 sensor list and AW3643 flashlight, and the exact integrated vmlinux contains
prior `[M6_CAM]` strings, but this fresh dmesg/logcat capture contains no
`[M6_CAM]` or `[M6_FLASH]` marker lines. INFERENCE: current evidence proves the
provider's zero-device result at the kernel ioctl/check-alive boundary, but does
not prove a wrong sensor table, GPIO/regulator, device node, sepolicy, or AW3643
identity mismatch. HYPOTHESIS: either boot-time printk was evicted before the
capture, or the next actionable failure is inside compat/ioctl ->
`SENSOR_FEATURE_CHECK_SENSOR_ID` -> I2C/power. Add diagnostic markers so a
post-boot provider restart/camera open and a torch toggle produce fresh,
grepable kernel evidence.

Evidence:
- Capture identity: `artifact-identity-local.txt:3` lists boot sha256
  `55dac1693482f41010a4bfd306b993f3f8435a61edb3f34f18fb796703149b7f`;
  `boot-readback-vs-local.txt:1` is `match`; `kernel-version.txt:3` reports
  `Linux localhost 3.18.140 #36 SMP PREEMPT Mon Jun 8 04:49:15 CDT 2026`.
- Boot/provider facts: `getprop.txt:52` has
  `init.svc.camera-provider-2-4=running`, `getprop.txt:342` has
  `ro.hardware.camera=mt6750`, and `getprop.txt:430` has
  `sys.boot_completed=1`.
- Camera registration facts: `lshal.txt:9` lists
  `android.hardware.camera.provider@2.4::ICameraProvider/legacy/0`;
  `dumpsys-media-camera.txt:4-5` reports zero camera devices and
  `dumpsys-media-camera.txt:13` reports provider static info with zero devices.
- Sensor search facts: `logcat-all-threadtime.txt:302-307` starts
  `impSearchSensor` and sets driver ID `10000`; lines `330`, `337`, `343`,
  `357`, `363`, `371`, `377`, `383`, and `389` report `Err-ctrlCode (I/O error)`;
  lines `341`, `347`, `361`, `367`, `375`, `381`, `387`, and `393` report
  `sensor ID mismatch`; lines `395`, `499`, `810`, and `917` report
  `Error No sensor found`; lines `396`, `500`, `811`, and `918` end with
  `SENSOR search end: 0x0`; lines `965-966` report provider `legacy/0` ready
  with zero camera devices.
- Device node facts: `devnodes-radio-camera-wmt.txt:51-53` show
  `/dev/kd_camera_flashlight`, `/dev/kd_camera_hw`, and
  `/dev/kd_camera_hw_bus2` as `crw-rw---- system camera
  u:object_r:mtk_camera_device:s0`.
- Kernel source/config facts:
  `/srv/forge/android/export/meizu_m6_artifacts/20260608-m6-integrated-camera-sdio-ril-sensors-bootonly-050053/kernel.config:1180`
  selects `ov13855_mipi_raw s5k3l8_mipi_raw hi846_mipi_raw
  hi846_mipi_raw_holi s5k4h8_mipi_raw`; lines `1190-1191` enable MTK
  flashlight and `leds_AW3643`; line `1204` enables MTK imgsensor; line `1496`
  enables MTK LEDs. Matching `System.map` contains `kdSetDriver`,
  `kdCISModulePowerOn`, `flashlight_init`, and
  `leds_AW3643_flashlight_init`.
- Negative evidence: `rg -n "M6_CAM|M6_FLASH"` over the fresh dmesg/logcat
  returned no matches; a camera/flash-specific AVC search returned no matches.
  Fresh dmesg contains no AW3643/flashlight runtime evidence beyond unrelated
  `select_vdpm_vol board_gpio54` messages.

Files changed:
- `kernel-3.18/drivers/misc/mediatek/imgsensor/src/mt6755/kd_sensorlist.c`:
  adds diagnostic `[M6_CAM]` markers around compat ioctl entry/exit,
  `kd_MultiSensorFeatureControl()` `SENSOR_FEATURE_CHECK_SENSOR_ID`, check-alive
  power/probe/result/power-off, and failing I2C send/recv paths.
- `kernel-3.18/drivers/misc/mediatek/flashlight/src/mt6755/kd_flashlightlist.c`:
  adds diagnostic `[M6_FLASH]` markers around flashlight core probe, device
  creation, ioctl entry, bad index/part, part resolution, and ioctl exit.
- `kernel-3.18/drivers/misc/mediatek/flashlight/src/mt6755/leds-AW3643/leds_strobe.c`:
  adds diagnostic `[M6_FLASH]` markers around AW3643 I2C probe, present/absent
  identification, absent-path enable attempts, and `SET_DUTY`/`SET_ONOFF`
  ioctl handling.
- `BRINGUP_STATE.md`: records this evidence-backed camera/torch diagnostic
  patch summary and next-capture expectations.

Why each file changed: `kd_sensorlist.c` owns the kernel side of the MTK HAL
sensor search path that currently returns `-EIO`/zero devices, so it is the
least invasive place to prove whether the provider reaches compat ioctl,
driver selection, check-alive, sensor-ID, I2C, or power failure. `kd_flashlightlist.c`
owns `/dev/kd_camera_flashlight` registration and generic torch ioctl routing,
so it proves whether the node is only present or is actually opened/routed at
runtime. `leds_strobe.c` owns AW3643 detection and enable/on-off handling, so
it proves whether torch failure is absent hardware, missing ioctl traffic, or
downstream AW3643 enable behavior. The state file is updated because the kernel
rules require the patch summary, evidence, expected marker, rollback condition,
and verification commands to live in the closest device state file.

Expected next marker: after flashing a boot image built from
`Image.gz-dtb` sha256
`af8447f9e688c5618cdf8ed2c443aacde341f81c87e904a4420f49fd0769e084` with
`System.map` sha256
`b63cf52cf55f95ad72ca3eae6982d32cabbb7e1f222686d2017c087dbf43d209`,
restart camera provider or open a camera app after clearing dmesg. The next
capture should show `[M6_CAM] compat ioctl entry` and/or `[M6_CAM] ioctl
SET_DRIVER`, then `[M6_CAM] kdSetDriver`, `[M6_CAM] check_alive`, and either
`[M6_CAM] feature CHECK_SENSOR_ID ret ... sensorID!=0xffffffff` or a concrete
`[M6_CAM] i2c send/recv fail` or `[M6_CAM] power on/off fail` marker. For torch,
after toggling torch or using a known flashlight test binary, dmesg should show
`[M6_FLASH] core probe`, `[M6_FLASH] aw3643_i2c_probe present/absent`,
`[M6_FLASH] ioctl entry/resolved/exit`, and `[M6_FLASH] aw3643_ioctl
SET_DUTY/SET_ONOFF`. If no markers appear after explicit post-boot provider
restart and torch trigger, fix capture/trigger method or device-node path before
behavior changes.

Rollback condition: revert this diagnostic if logging floods boot enough to
evict earlier evidence, regresses boot/provider startup/ADB, changes camera or
torch behavior without an explanatory marker, or if a post-boot triggered
capture still produces no markers despite `strings vmlinux` proving they are in
the exact flashed kernel.

Verification commands:

```bash
cd /srv/forge/android/meizu_m6/kernel-meizu_M6-N-ex6-linux-3.18.140
git diff --check -- \
  kernel-3.18/drivers/misc/mediatek/imgsensor/src/mt6755/kd_sensorlist.c \
  kernel-3.18/drivers/misc/mediatek/flashlight/src/mt6755/kd_flashlightlist.c \
  kernel-3.18/drivers/misc/mediatek/flashlight/src/mt6755/leds-AW3643/leds_strobe.c \
  BRINGUP_STATE.md
env CCACHE_DIR=/srv/forge/android/ccache make -C kernel-3.18 \
  O=/srv/forge/work/m6-source-kernel-manual-20260520/out \
  ARCH=arm64 \
  CROSS_COMPILE=/srv/forge/android/meizu_m6/rom-meizu_M6-lineage-cm-14.1/prebuilts/gcc/linux-x86/aarch64/aarch64-linux-android-4.9/bin/aarch64-linux-android- \
  -j8 Image.gz-dtb
strings /srv/forge/work/m6-source-kernel-manual-20260520/out/vmlinux | \
  rg "M6_CAM|M6_FLASH"
sha256sum \
  /srv/forge/work/m6-source-kernel-manual-20260520/out/arch/arm64/boot/Image.gz-dtb \
  /srv/forge/work/m6-source-kernel-manual-20260520/out/System.map \
  /srv/forge/work/m6-source-kernel-manual-20260520/out/vmlinux
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell \
  'dmesg -C; stop camera-provider-2-4; start camera-provider-2-4; sleep 5; dmesg | grep -E "M6_CAM|M6_FLASH|CHECK_SENSOR_ID|kdSetDriver|aw3643|flashlight" | tail -240'
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell \
  'ls -lZ /dev/kd_camera_hw /dev/kd_camera_hw_bus2 /dev/kd_camera_flashlight; dumpsys media.camera | head -40'
```

For torch, use the UI tile or a known flashlight test binary, then immediately
collect `dmesg | grep -E "M6_FLASH|aw3643|flashlight" | tail -240`. If
`/proc/driver/flash_lightness` exists on that boot, it can be used only as an
optional trigger after confirming the node exists.

## 2026-06-08 display event-flow diagnostic capture result

CAPTURE HISTORY, 2026-06-08: the display event-flow diagnostic boot-only image
was flashed to serial `711HEBSR277K5` and captured under
`/srv/forge/android/meizu_m6/captures/20260608-105454-m6-display-event-flow-diag-711HEBSR277K5`.

FACT: boot partition readback matches the flashed boot artifact:
`f8c7fd598b2bd9bfcb0ac417ed96324df15c9e78b2ee00ba6394d8faf215abf6`.
Matching decode files are in
`/srv/forge/android/export/meizu_m6_artifacts/20260608-105112-m6-display-event-flow-diag-bootonly/`;
`System.map` sha256 is
`7da2c0be0c23d2f6f74970845b96ae0c247861c77bf492a62091755e0c6aaf85`.

FACT: Android reaches `sys.boot_completed=1`, SurfaceFlinger is running,
bootanimation is stopped, and `screencap.png` is a valid nonblank `720x1280`
PNG. SurfaceFlinger reports a built-in `720x1280` screen, `powerMode=2`,
`flips=1583`, HWC layers, and Mali-T860 GLES.

FACT: the new `M6 DPMGR event flow[...]` markers show `FRAME_DONE`
`irq-prewake` and `irq-postwake` events with RDMA0 counters moving. The new
`M6 DDP irq diag[...]` markers show repeated RDMA0 and mutex IRQ samples with
RDMA0 `GLOBAL=0x101`, `SIZE=720x1280`, nonzero in/out counters, and DSI0
`START=0x10001`. Route `ready` remains `0x0` in the sampled states and some
samples temporarily show `route valid=0x0 ready=0x0`.

INFERENCE: the current display blocker is not userspace composition, HWC
bring-up, screencap timeout, missing RDMA IRQ, or missing DPMGR wake. If the
physical panel remains black, the next frontier is below the logical
composition path: DSI video-stream acceptance, panel state/TE, backlight
handoff, or route-ready/DSI handoff state that does not prevent screenshots.

HYPOTHESIS: the panel can be powered and the logical framebuffer can compose,
but the physical panel is not accepting or showing the HS video stream. The
next display patch should be DIAGNOSTIC only: log DSI/MIPITX lane state,
TE/vsync state, DCS status/readback, backlight write path, and route-ready
transitions around the first post-SF frames.

Detailed report:
`/srv/forge/android/meizu_m6/captures/20260608-105454-m6-display-event-flow-diag-711HEBSR277K5/CAPTURE_ANALYSIS.md`.

## 2026-06-08 RDMA0 / DSI0 / mutex IRQ diagnostic

PATCH HISTORY, DIAGNOSTIC, 2026-06-08: add bounded error-level display IRQ
and DPMGR event-flow markers for the primary RDMA0, DSI0, mutex0, and
waitqueue/CMDQ-token path. This patch does not change routing, PQ bypass
state, layer configuration, RDMA timings, DSI mode, CMDQ tokens, waitqueue
state, or event mapping.

Hypothesis: FACT: the verified capture
`/srv/forge/android/meizu_m6/captures/20260608-050606-m6-integrated-camera-sdio-bootonly-711HEBSR277K5`
uses boot sha256
`e414f275df9a82ee5bcfa4d00d4168f6c3fe3eae7d705cd6a2dbfebbb1db344c` and
kernel `System.map` sha256
`6d78f9ad194d15f8b2c67035652165e0550ef1e9d5303f0a8cfbfe7ab5123f4e`.
FACT: OVL diagnostics show `bypass_pq=1` and nonzero scanout layers, but
dmesg/debugfs show `RDMA0 underflow`, `ovl0 frame underflow`, `wait VSYNC
timeout`, and RDMA0 counters often `IN=0/0 OUT=0/0` at timeout. HYPOTHESIS:
the next unclosed display boundary is whether RDMA0 emits start/done/target
line after the first underflow, whether dpmgr maps RDMA0 DONE into the
`DISP_PATH_EVENT_IF_VSYNC` waitqueue, and whether CMDQ `RDMA0_EOF` /
`MUTEX0_STREAM_EOF` token state changes around that wake.

Evidence:
- Capture report:
  `/srv/forge/android/meizu_m6/captures/20260608-050606-m6-integrated-camera-sdio-bootonly-711HEBSR277K5/CAPTURE_ANALYSIS.md`.
- Fresh debugfs lines include repeated `wait VSYNC timeout on scenario
  primary_disp`, RDMA0 `GLOBAL=0x101 SIZE=720x1280`, OVL0 enabled with valid
  layer address/pitch, DSI0 `INTSTA=0x80000790`, and mutex `M0_SOF=0x41`.
- Fresh dmesg lines include `IRQ: RDMA0 underflow!`, `IRQ: ovl0 frame
  underflow!`, `IRQ: ovl0 hw reset done`, and OVL IRQ diagnostics with
  `direct=1 bypass_pq=1`.

Files changed:
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_irq.c`: adds sampled
  `M6 DDP irq diag[...]` dumps for RDMA0, DSI0, and mutex0 interrupts, including
  route valid/ready, mutex INTEN/INTSTA/MOD/SOF, RDMA0 counters/FIFO/global
  state, OVL0 IRQ state, DSI0 INTSTA/mode/state debug, and RDMA IRQ counters.
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_manager.c`: adds sampled
  `M6 DPMGR event flow[...]` dumps before primary wait calls, on wait timeout,
  and before/after RDMA0 DONE wakes the matching DPMGR waitqueue, including
  mapped irq bit, waitqueue timestamp, route state, RDMA counters, DSI status,
  and CMDQ `RDMA0_EOF` / `MUTEX0_STREAM_EOF` token reads.

Why this file changed: the fresh display failure occurs inside IRQ/event
completion after OVL has configured layers. `ddp_irq.c` is the owner that sees
RDMA underflow, DSI IRQ status, and mutex IRQ status before the wait path times
out; adding read-only markers there proves the next branch without changing
display behavior.
`ddp_manager.c` is the owner that maps display IRQ bits to path events and
wakes `DISP_PATH_EVENT_IF_VSYNC`; adding read-only markers there proves whether
the IRQ reaches dpmgr but fails to propagate to CMDQ/present wait state.

Expected next marker: a fresh boot with this diagnostic should show
`M6 DDP irq diag[*][rdma0]`, `M6 DDP irq diag[*][dsi0]`, and/or
`M6 DDP irq diag[*][mutex0]`, plus `M6 DPMGR event flow[*][wait-timeout-pre]`
and either `M6 DPMGR event flow[*][irq-prewake]` / `[irq-postwake]` or
`[wait-timeout-expired]` before the first `wait VSYNC timeout`. If RDMA0 DONE
IRQ fires and dpmgr wakes the event while CMDQ tokens stay zero, the next patch
belongs in CMDQ token set/clear ordering or event binding. If RDMA0 DONE never
fires despite RDMA transfer counters rising, the next patch belongs in RDMA IRQ
status/enable or reset/restart. If dpmgr wakes correctly and tokens advance but
present still hangs, the next frontier is HWC/fence/timeline.

Rollback condition: revert this diagnostic if the added logging floods early
boot enough to destabilize ADB/log capture or if it changes timing such that
the display failure disappears without a causal marker.

Verification commands:

```bash
cd /srv/forge/android/meizu_m6/kernel-meizu_M6-N-ex6-linux-3.18.140
git diff --check -- \
  kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_irq.c \
  kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_manager.c \
  BRINGUP_STATE.md
env CCACHE_DIR=/srv/forge/android/ccache make -C kernel-3.18 \
  O=/srv/forge/work/m6-source-kernel-manual-20260520/out \
  ARCH=arm64 \
  CROSS_COMPILE=/srv/forge/android/meizu_m6/rom-meizu_M6-lineage-cm-14.1/prebuilts/gcc/linux-x86/aarch64/aarch64-linux-android-4.9/bin/aarch64-linux-android- \
  -j8 Image.gz-dtb
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell \
  'dmesg | grep -E "M6 DDP irq diag|M6 DPMGR event flow|RDMA0 underflow|wait VSYNC timeout" | tail -240'
```

## 2026-06-08 integrated boot-only runtime capture

CAPTURE HISTORY, 2026-06-08: the integrated camera/SDIO/RIL/sensors
source-kernel boot-only artifact was flashed to serial `711HEBSR277K5` and
captured under
`/srv/forge/android/meizu_m6/captures/20260608-050606-m6-integrated-camera-sdio-bootonly-711HEBSR277K5`.

FACT: `artifact-identity.txt` verifies the boot partition sha256 as
`e414f275df9a82ee5bcfa4d00d4168f6c3fe3eae7d705cd6a2dbfebbb1db344c`, with
kernel `Image.gz-dtb` sha256
`6a54d915f6f0ef55256191f0c5edd25af8fb98c818a0312ffb6f186f0e98df43` and
matching `System.map` sha256
`6d78f9ad194d15f8b2c67035652165e0550ef1e9d5303f0a8cfbfe7ab5123f4e`.

FACT: Android reaches `sys.boot_completed=1` and USB charging is alive, but the
display pipeline remains black/hung (`screencap_timeout_or_error=124`), SDIO
function enumeration is absent, camera provider reports zero devices, and MD1
ends in `mtk.md1.status=exception`.

Display evidence: OVL diagnostics show `bypass_pq=1`; OVL is enabled with
nonzero layer addresses, but dmesg/debugfs show `RDMA0 underflow`, `ovl0 frame
underflow`, `wait VSYNC timeout`, and RDMA0 counters often `IN=0/0 OUT=0/0` at
timeout. INFERENCE: the next display frontier is RDMA reset/restart, mutex
completion event, and DSI/vsync signaling, not a renewed broad PQ disable.

Connectivity evidence: `/sys/bus/sdio/devices` is empty, WMT reads chip
`0x00000326`, then `hif_sdio_stp_on` reports no supported SDIO function and
WMT SDIO_FUNC returns `-8`. INFERENCE: the next kernel frontier is proving
MSDC2 rescan and `mmc_attach_sdio` behavior after WMT power-on.

Camera evidence: camera provider loads and scans IDs `10000..10003` and
`20000..20003`, then reports zero devices. No `[M6_CAM]` kernel markers are
visible in this capture, so the next camera kernel task is to repair or deepen
marker plumbing before changing sensor behavior again.

Detailed report:
`/srv/forge/android/meizu_m6/captures/20260608-050606-m6-integrated-camera-sdio-bootonly-711HEBSR277K5/CAPTURE_ANALYSIS.md`.

## 2026-06-08 integrated camera and SDIO checkpoint

PATCH HISTORY, PROPER-FIX + DIAGNOSTIC, 2026-06-08: integrate the current
fresh-log worker results into a buildable source-kernel checkpoint. The grouped
checkpoint covers camera sensor-list selection, bounded camera diagnostics, and
MSDC2/SDIO WMT-resume rescan instrumentation/fix. It does not claim display,
Wi-Fi, Bluetooth, camera, or torch are runtime-fixed until a flashed capture
proves the next markers.

Hypothesis: FACT: the verified all-layer capture
`/srv/forge/android/meizu_m6/captures/20260608-040416-m6-all-layer-evidence-711HEBSR277K5`
uses boot sha256 `1e82b12cfbbd857acec419d0314b5a037be1103694a559facbc486668b6e0c92`.
FACT: the camera provider loads but registers zero devices after
`KDCAMERAHWIOC_X_SET_DRIVER` returns `-EIO`; the old `.config` selected stale
`imx278_mipi_raw ov8856jsl_mipi_raw` while the M6 source camera table and power
table contain `ov13855`, `s5k3l8`, `hi846`, `hi846_holi`, and `s5k4h8`. FACT:
Wi-Fi/BT fail above absent SDIO/STP bring-up (`wificond` has no interface and
BT cannot open `/dev/stpbt`). INFERENCE: the next source-kernel step must build
the stock-backed sensor list and schedule a real MSDC2 SDIO rescan after WMT
resume, while preserving dense diagnostics for the next runtime capture.

Evidence:
- Capture:
  `/srv/forge/android/meizu_m6/captures/20260608-040416-m6-all-layer-evidence-711HEBSR277K5`.
- Camera evidence: MTK provider loads, then reports `0 camera devices`; dmesg
  markers show empty-name/null-init `kdSetDriver()` paths before any real
  sensor-ID/I2C frontier.
- Connectivity evidence: no SDIO function exists under `/sys/bus/sdio/devices`
  in the fresh capture; Wi-Fi HAL cannot change firmware mode; BT loops on
  `/dev/stpbt` and the HIDL close path crashes after partial init.
- Build evidence: `build-m6-integrated-subagent-fixes-20260608.log` first
  proved the regenerated `.config` selects
  `ov13855_mipi_raw s5k3l8_mipi_raw hi846_mipi_raw hi846_mipi_raw_holi s5k4h8_mipi_raw`
  and failed on a source-level missing prototype for `s4AF_ReadReg_I2C`.
  `build-m6-integrated-subagent-fixes-20260608-retry1.log` then completed
  `Image.gz-dtb` after making that prototype unconditional in the two sensor
  headers whose source files call the exported helper unconditionally.

Files changed:
- `kernel-3.18/arch/arm64/configs/meizu_m6_defconfig`: selects the M6 camera
  sensor list proven by the active source tables.
- `kernel-3.18/arch/arm64/configs/meizu_m6_debug_defconfig`: mirrors the same
  sensor list for debug builds.
- `kernel-3.18/drivers/misc/mediatek/imgsensor/src/mt6755/ov13855_mipi_raw/ov13855mipiraw_Sensor.h`:
  exposes the already exported `s4AF_ReadReg_I2C()` prototype outside the
  Huawei dev-flag config because the sensor source calls it unconditionally.
- `kernel-3.18/drivers/misc/mediatek/imgsensor/src/mt6755/s5k3l8_mipi_raw/s5k3l8mipiraw_Sensor.h`:
  applies the same build fix for the next compiled sensor with the same
  unconditional helper call.
- `kernel-3.18/drivers/misc/mediatek/imgsensor/src/mt6755/kd_sensorlist.c`:
  adds bounded `[M6_CAM]` markers around driver-index selection, null init,
  selected sensor name/function, and ioctl return paths.
- `kernel-3.18/drivers/misc/mediatek/imgsensor/src/mt6755/camera_hw/kd_camera_hw.c`:
  keeps bounded camera power markers and fixes the existing dirty marker
  structure so the next sensor-ID/power frontier is visible.
- `kernel-3.18/drivers/mmc/host/mediatek/mt6755/sd.c`: on MSDC2
  WMT `PM_EVENT_USER_RESUME`, clears `rescan_entered` and schedules a bounded
  SDIO rescan, with probe/add_host/pm markers for the next Wi-Fi/BT capture.
- `BRINGUP_STATE.md`: records this grouped checkpoint and verification.

Why each file changed: the defconfig lines decide which sensor objects/macros
exist; the two headers unblock the exact compiled source selected by that list
without changing runtime branch behavior; the camera diagnostic files mark the
earliest proven zero-device boundary; and `sd.c` is the narrow kernel owner of
the missing SDIO rediscovery after WMT brings connectivity power up.

Expected next marker: after this exact `Image.gz-dtb` is packed into boot and
flashed, camera dmesg should show named `ov13855` / `s5k3l8` / `hi846` sensor
init paths instead of empty-name/null-init `kdSetDriver` entries; if camera
still fails, the log should move to sensor-ID, I2C bus, or power-step evidence.
Connectivity dmesg should show `M6 MSDC2 WMT resume` and a real SDIO rescan;
if Wi-Fi/BT still fail, the next failure should be after SDIO function
enumeration or a clearer WMT/STP marker.

Rollback condition: revert the camera defconfig/header pieces if the exact
rebuilt boot image still shows null init for indices `0..3`, if a named sensor
compile/runtime path regresses provider startup earlier than ready-with-zero
devices, or if a stock source comparison disproves the selected sensor set.
Revert the SDIO rescan piece if it regresses boot, storage, ADB stability, or
causes repeated unsafe rescans without SDIO progress.

Verification result, 2026-06-08:
- `git diff --check` passed for this scoped kernel patch set.
- Build command regenerated `.config` from `meizu_m6_defconfig`, then built
  `Image.gz-dtb` with `O=/srv/forge/work/m6-source-kernel-manual-20260520/out`.
- Build log:
  `build-m6-integrated-subagent-fixes-20260608-retry1.log`.
- `Image.gz-dtb` sha256:
  `6a54d915f6f0ef55256191f0c5edd25af8fb98c818a0312ffb6f186f0e98df43`.
- `System.map` sha256:
  `6d78f9ad194d15f8b2c67035652165e0550ef1e9d5303f0a8cfbfe7ab5123f4e`.
- `vmlinux` sha256:
  `39fbdf367cfe223ce6d55748ae76640391a3f4247a1e063fc62a866ff74d706d`.

## 2026-06-08 camera sensor defconfig list fix

PATCH HISTORY, PROPER-FIX, 2026-06-08: replace the stale M6 image-sensor
defconfig list with the sensor folders already present in the active MT6755
sensor init list and camera power table.

Hypothesis: FACT: the verified all-layer capture
`/srv/forge/android/meizu_m6/captures/20260608-040416-m6-all-layer-evidence-711HEBSR277K5`
uses boot sha256 `1e82b12cfbbd857acec419d0314b5a037be1103694a559facbc486668b6e0c92`.
FACT: camera provider loads the MTK module but reports zero devices. FACT:
kernel camera markers show `kdSetDriver()` receives indices `0..3` with empty
driver names and null init functions, returning `-EIO`, before real sensor ID
probing. FACT: `kd_sensorlist.h` and `camera_hw/kd_camera_hw.c` already carry
guarded entries/power sequences for `ov13855`, `s5k3l8`, `hi846`,
`hi846_holi`, and `s5k4h8`, while the defconfigs still selected stale
`imx278_mipi_raw ov8856jsl_mipi_raw`. INFERENCE: the zero-camera failure is
caused by building the wrong sensor objects/macros for this M6 camera table.

Evidence:
- Capture:
  `/srv/forge/android/meizu_m6/captures/20260608-040416-m6-all-layer-evidence-711HEBSR277K5`.
- `logcat-all-threadtime.txt`: MTK camera module loads, then provider
  `legacy/0` is ready with `0 camera devices`.
- `dmesg.txt`: `[M6_CAM] kdSetDriver ... drvIdx=0..3 name=` / null init /
  `ret=-5`, proving the HAL's generic sensor mismatch line is above real I2C
  ID readback.
- Source checks: `Makefile.custom` maps
  `CONFIG_CUSTOM_KERNEL_IMGSENSOR` tokens to sensor macros; `mt6755/Makefile`
  builds those folders; `kd_sensorlist.h` and `kd_camera_hw.c` already list the
  replacement candidates.

Files changed:
- `kernel-3.18/arch/arm64/configs/meizu_m6_defconfig`: sets
  `CONFIG_CUSTOM_KERNEL_IMGSENSOR` to
  `ov13855_mipi_raw s5k3l8_mipi_raw hi846_mipi_raw hi846_mipi_raw_holi s5k4h8_mipi_raw`.
- `kernel-3.18/arch/arm64/configs/meizu_m6_debug_defconfig`: mirrors the same
  sensor list for debug builds.

Why each file changed: these defconfigs select which sensor driver objects and
preprocessor symbols exist. The active source camera table and power table
already match the new list, so no sensor-table or GPIO rewrite is needed before
testing the proper compiled set.

Expected next marker: build output should print the new image-sensor platform
list. Runtime dmesg should stop showing empty-name/null-init `kdSetDriver`
for indices `0..3` and instead show selected sensor names/functions, followed
by camera power markers and either real sensor ID success or a concrete I2C /
power-ID failure. If a camera device registers, test flashlight/torch after
camera enumeration moves.

Rollback condition: revert these two defconfig lines if a full kernel build
fails on the new sensor objects, if the exact rebuilt boot image still shows
null init for indices `0..3`, or if camera provider regresses from ready-with-0
devices into an earlier crash/hang.

Verification commands:

```bash
cd /srv/forge/android/meizu_m6/kernel-meizu_M6-N-ex6-linux-3.18.140
git diff --check -- \
  kernel-3.18/arch/arm64/configs/meizu_m6_defconfig \
  kernel-3.18/arch/arm64/configs/meizu_m6_debug_defconfig \
  BRINGUP_STATE.md
env CCACHE_DIR=/srv/forge/android/ccache make -C kernel-3.18 \
  O=/srv/forge/work/m6-source-kernel-manual-20260520/out \
  ARCH=arm64 \
  CROSS_COMPILE=/srv/forge/android/meizu_m6/rom-meizu_M6-lineage-cm-14.1/prebuilts/gcc/linux-x86/aarch64/aarch64-linux-android-4.9/bin/aarch64-linux-android- \
  -j8 Image.gz-dtb
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell \
  'dmesg | grep -E "M6_CAM|kdSetDriver|Camera|sensor ID|KDCAMERAHWIOC" | tail -220'
```

## 2026-06-08 camera zero-device diagnostic

PATCH HISTORY, DIAGNOSTIC, 2026-06-08: add bounded M6 camera markers around
the MTK sensor driver selection and camera power sequence. This does not change
the sensor table, GPIO/regulator settings, flashlight behavior, device nodes,
or policy. It only makes the next camera-provider boot prove whether the HAL's
`KDCAMERAHWIOC_X_SET_DRIVER` `-EIO` comes from the compiled sensor list, a null
sensor init/function, an out-of-range driver index, or a later power-list path.

Hypothesis: FACT: the verified boot image for capture
`/srv/forge/android/meizu_m6/captures/20260608-040416-m6-all-layer-evidence-711HEBSR277K5`
matches local artifact
`/srv/forge/android/export/meizu_m6_artifacts/20260608-m6-dsi-bist-full-isolation/boot-m6-dsi-bist-full-isolation.img`
with sha256
`1e82b12cfbbd857acec419d0314b5a037be1103694a559facbc486668b6e0c92`.
FACT: the camera provider and MTK HAL load, but HAL sensor search repeatedly
gets `KDCAMERAHWIOC_X_SET_DRIVER` `I/O error` and ends with zero detected
camera IDs. FACT at diagnostic-build time: the active
`/srv/forge/work/m6-source-kernel-manual-20260520/out/.config` still selected
`imx278_mipi_raw ov8856jsl_mipi_raw` and `leds_AW3643`, and the matching
diagnostic `System.map` contains `IMX278_MIPI_RAW_SensorInit`,
`OV8856JSLMIPIRAW_SensorInit`, `flashlight_init`, and
`leds_AW3643_flashlight_init`. FACT after a concurrent update: the current
source defconfigs now carry the separate PROPER-FIX entry above this section
and select `ov13855/s5k3l8/hi846/hi846_holi/s5k4h8`; the diagnostic build
result below does not verify that defconfig fix because the out `.config` was
not regenerated. HYPOTHESIS for this diagnostic layer: if a boot still uses the
old compiled list or any other bad list, the new markers will prove the exact
`kdSetDriver()` failure branch before power/I2C behavior is guessed.

Evidence:
- `service-list.txt`: `media.camera` is registered.
- `logcat-all-threadtime.txt:6651-6652`: provider loads
  `android.hardware.camera.provider@2.4-impl.so` and
  `/vendor/lib64/hw/camera.mt6750.so`.
- `logcat-all-threadtime.txt:6680-6750`: first `impSearchSensor` pass starts,
  every attempted `set sensor driver id` returns
  `ERROR:KDCAMERAHWIOC_X_SET_DRIVER` / `Err-ctrlCode (I/O error)`, then
  `Error No sensor found!!` and
  `SENSOR search end: 0x0 /[0xffffff][255]/...`.
- `logcat-all-threadtime.txt:6791`: `Loaded "MediaTek Camera Module"`.
- `logcat-all-threadtime.txt:6800-6874`: second pass repeats the same zero-ID
  result.
- `logcat-all-threadtime.txt:6918-6919`: provider `legacy/0` is ready with
  `0 camera devices`.
- `dev-nodes.txt`: `/dev/kd_camera_hw`, `/dev/kd_camera_hw_bus2`, and
  `/dev/kd_camera_flashlight` exist as `system:camera` with
  `u:object_r:mtk_camera_device:s0`; `/dev/flashlight*` does not exist.
- `sysfs/leds.txt`: `/sys/class/leds/flashlight` exists with `brightness=0`
  and `max_brightness=255`. No torch enable/open failure is present in this
  capture.

Files changed:
- `kernel-3.18/drivers/misc/mediatek/imgsensor/src/mt6755/kd_sensorlist.c`:
  emits bounded `[M6_CAM]` markers for the first four compiled list slots,
  decoded HAL driver indices, selected sensor ID/name/init function, null init,
  null sensor function, and out-of-range index.
- `kernel-3.18/drivers/misc/mediatek/imgsensor/src/mt6755/camera_hw/kd_camera_hw.c`:
  preserves existing `[M6_CAM]` power markers and fixes their power-off branch
  structure so they compile without changing power sequencing.
- `BRINGUP_STATE.md`: records this diagnostic evidence and rollback contract.

Why each file changed: `kd_sensorlist.c` owns the exact ioctl path returning
`-EIO` to the HAL. `kd_camera_hw.c` owns the next power boundary if
`kdSetDriver()` succeeds, and the existing dirty markers needed structural
repair before build verification. No flashlight file changed because the
capture proves flashlight node/class presence but no torch command failure.

Expected next marker: next boot/camera-provider log should show `[M6_CAM]
list[0..3]`, `[M6_CAM] kdSetDriver enter raw0/raw1`, `[M6_CAM] selected
invoke=... drvIdx=... id/name/init`, then either a specific null/out-of-range
`-EIO`, an enabled sensor name/function followed by sensor ID mismatch/I2C
failure, or `[M6_CAM] power on/off ...` with the exact matched power-list step
and failing type/voltage. If provider registers a camera, record the detected
sensor ID/name and then test torch separately.

Rollback condition: revert this diagnostic patch if it breaks the kernel
build, prevents camera provider startup, changes detected sensor behavior
without a corresponding marker explanation, floods hot paths beyond one
bounded sensor-search sequence, or if a fresh capture proves a stock-backed
proper fix should replace the markers.

Verification result, 2026-06-08: `git diff --check` passed for the three
touched files. `make -C kernel-3.18 O=/srv/forge/work/m6-source-kernel-manual-20260520/out
ARCH=arm64 ... -j8 Image.gz-dtb` completed successfully. Built hashes:
`Image.gz-dtb` =
`fedb53b7f08faf6fc16fce2e271eed023ae0246e4eae6a4aa0d05eff7274ed5f`,
`System.map` =
`3796d588f46ea5f7a49e0c8a7c6262d526b3354d513c79874b0e55196170724c`,
`vmlinux` =
`908284ca9b818b49416f2c770c612a8037c406746cb79d6ced2e56a9dc9050a2`.
This build used the pre-existing out `.config`, so it verifies marker
compilation only, not the concurrent defconfig PROPER-FIX. `vmlinux` contains
the `[M6_CAM]` marker strings; the matching `System.map` contains
`kdSetDriver`, `kdCISModulePowerOn`, `IMX278_MIPI_RAW_SensorInit`,
`OV8856JSLMIPIRAW_SensorInit`, `flashlight_init`, and
`leds_AW3643_flashlight_init`.

Verification commands:

```bash
cd /srv/forge/android/meizu_m6/kernel-meizu_M6-N-ex6-linux-3.18.140
git diff --check -- \
  kernel-3.18/drivers/misc/mediatek/imgsensor/src/mt6755/kd_sensorlist.c \
  kernel-3.18/drivers/misc/mediatek/imgsensor/src/mt6755/camera_hw/kd_camera_hw.c \
  BRINGUP_STATE.md
env CCACHE_DIR=/srv/forge/android/ccache make -C kernel-3.18 \
  O=/srv/forge/work/m6-source-kernel-manual-20260520/out \
  ARCH=arm64 \
  CROSS_COMPILE=/srv/forge/android/meizu_m6/rom-meizu_M6-lineage-cm-14.1/prebuilts/gcc/linux-x86/aarch64/aarch64-linux-android-4.9/bin/aarch64-linux-android- \
  -j8 Image.gz-dtb
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell \
  'logcat -b all -d -v threadtime | grep -E "M6_CAM|CameraProvider|impSearchSensor|KDCAMERAHWIOC|No sensor|camera devices"'
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell \
  'ls -lZ /dev/kd_camera_hw /dev/kd_camera_hw_bus2 /dev/kd_camera_flashlight; ls -l /sys/class/leds/flashlight; cat /sys/class/leds/flashlight/{brightness,max_brightness}'
```

## 2026-06-08 DSI BIST-full physical-path isolation

PATCH HISTORY, ISOLATION + DIAGNOSTIC, 2026-06-08: add a root-triggered
`m6_dsi_bist_full:<color>` debugfs command that enables the DSI controller's
real `BIST_ENABLE` bit in addition to the existing self-pattern bit. This is
not a normal display-path fix and does not change boot-time DDP, OVL, panel
init, fences, or SurfaceFlinger behavior.

Hypothesis: FACT: the previous `dsipattern` command wrote `BIST_PATTERN` and
set `SELF_PAT_MODE`, but the fresh capture showed `BIST_ENABLE=0`. FACT:
stock/LK/current source parity checks show the active `ili9881p_hd_dsi_txd`
panel table already matches the visible stock surface: 720x1280, 4 lanes,
RGB888/24-bit packet, PLL 230, porch timings, ESD `0x0a -> 0x9c`, and the
72-entry init table. HYPOTHESIS: a full DSI BIST pattern with `BIST_ENABLE=1`,
`BIST_FIX_PATTERN=1`, `SELF_PAT_MODE=1`, `BIST_LANE_NUM=4`, and
`BIST_TIMING=0x20` separates controller-side video/BIST generation from
panel/PHY/lane electrical acceptance. If physical red is visible during the
test window, the panel/lane path can display controller-generated pixels and
the frontier returns to RDMA/DDP-to-DSI stream handoff. If physical red is not
visible while these markers are set and MIPITX stays powered, stop chasing
OVL/fences/HWC and move to panel electrical state, lane mapping, or compiled
DTB/MIPITX parity.

Evidence:
- Stock reverse agent result, 2026-06-08: LK `ili9881p_hd_dsi_txd` table and
  current source match the visible LCM/DSI init surface; do not change panel
  name, init table, PLL, porch, lane count, or RGB format without new evidence.
- Built artifact:
  `/srv/forge/android/export/meizu_m6_artifacts/20260608-m6-dsi-bist-full-isolation/boot-m6-dsi-bist-full-isolation.img`,
  sha256
  `1e82b12cfbbd857acec419d0314b5a037be1103694a559facbc486668b6e0c92`,
  size `8876032`.
- Built `Image.gz-dtb` sha256
  `cde3eb997c7a4698b3419cedf7552cc56bc04079ed9ac698f98c28b496f55186`;
  matching `System.map` sha256
  `4cd52859037b67487340428a1161cb0579daf3b935577d280b5d63a08f864dbb`;
  `vmlinux` sha256
  `dbf891943084070e5bcfbd96d738bbe37188e1eb5727aa923616351ac22206b5`.
- Build log:
  `/srv/forge/android/meizu_m6/kernel-meizu_M6-N-ex6-linux-3.18.140/build-m6-dsi-bist-full-isolation-20260608.log`,
  sha256 `97bc04eb6a0a4bdcf76bb51bee9f08be0b7fcdf1b750a18cad3dbf0b7d2e90b6`.
- Android-root flash verification: `/dev/block/mmcblk0p21` was written from
  `/data/local/tmp/boot-m6-dsi-bist-full-isolation.img`; device-side and
  pulled readback both matched boot sha256
  `1e82b12cfbbd857acec419d0314b5a037be1103694a559facbc486668b6e0c92`.
  The direct `adb exec-out dd | sha256sum` path produced a mismatched hash and
  is not trusted for this capture; use device-side sha256 or pulled readback.
- Invalid first capture:
  `/srv/forge/android/meizu_m6/captures/20260608-034920-m6-dsi-bist-full-isolation-711HEBSR277K5`.
  FACT: after reboot, adbd was not root and `/d/mtkfb` writes failed with
  `Permission denied`; this capture is superseded.
- Valid root capture:
  `/srv/forge/android/meizu_m6/captures/20260608-035010-m6-dsi-bist-full-root-711HEBSR277K5`.
  FACT: Android reached `sys.boot_completed=1`, `bootanim=stopped`,
  SurfaceFlinger running, `input` found, and logical `screencap.png` remained
  720x1280 RGBA.
- Valid BIST-full markers:
  `M6 DSI snapshot[bist-full-post]` shows
  `BIST_PATTERN=0xff0000`, `BIST_CON=0x200446`, `self_pat=1`,
  `bist_en=1`, `fix=1`, `lane=4`, `timing=0x20`,
  `STATE7=0x2020/Video data period`; after 500 ms the same BIST bits remain
  set and MIPITX lanes/PLL remain powered. `bist-full-post-disable` shows
  `BIST_CON=0x0`, `self_pat=0`, `bist_en=0`.

Files changed:
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_dsi.c`: adds
  `DSI_M6_BIST_Full_Test()` with bounded pre/post/after-500ms/disable
  snapshots and explicit BIST fields.
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_dsi.h`: exports the new
  diagnostic helper for the debug parser.
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/disp_debug.c`: adds the
  `/d/mtkfb` command `m6_dsi_bist_full:<color>`.
- `BRINGUP_STATE.md`: records evidence, artifact identity, expected marker,
  rollback condition, and verification commands.

Why each file changed: `ddp_dsi.c` owns the DSI BIST register programming and
already contains the bounded M6 DSI snapshot decoder. `disp_debug.c` is the
existing root-controlled MTKFB command surface used for display isolation
without changing normal boot behavior. `ddp_dsi.h` is required only for the
local helper declaration.

Expected next marker: if the human observes red during
`echo m6_dsi_bist_full:0x00ff0000 > /d/mtkfb`, record physical BIST visible
and move to DDP/RDMA-to-DSI handoff. If the panel remains physically black
while valid markers show `bist_en=1`, move to passive panel electrical and DTB
parity diagnostics: reset GPIO, ENP/ENN/TPS65132 readback, MIPITX lane
mapping/electrical registers, compiled DTB `dsi0`/`mipi_tx0`/pinctrl/backlight
nodes. Do not repeat `m6_lcm_reinit:1`; it is proven harmful in booted Android.

Rollback condition: revert this patch if the command causes ADB/runtime
instability, SurfaceFlinger or input regression, persistent BIST mode after
disable, new fence timeouts, or display underflow/M4U churn not present before
the BIST-full command. This patch is diagnostic only and should not ship in a
release ROM unless debugfs diagnostics are intentionally kept.

Verification commands:

```bash
cd /srv/forge/android/meizu_m6/kernel-meizu_M6-N-ex6-linux-3.18.140
git diff --check -- kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_dsi.c \
  kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_dsi.h \
  kernel-3.18/drivers/misc/mediatek/video/mt6755/disp_debug.c BRINGUP_STATE.md
env CCACHE_DIR=/srv/forge/android/ccache make -C kernel-3.18 \
  O=/srv/forge/work/m6-source-kernel-manual-20260520/out \
  ARCH=arm64 \
  CROSS_COMPILE=/srv/forge/android/meizu_m6/rom-meizu_M6-lineage-cm-14.1/prebuilts/gcc/linux-x86/aarch64/aarch64-linux-android-4.9/bin/aarch64-linux-android- \
  -j8 Image.gz-dtb
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 root
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell 'echo m6_dsi_bist_full:0x00ff0000 > /d/mtkfb; sleep 10; echo m6_dsi_bist_full:0 > /d/mtkfb'
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell 'dmesg | grep -E "M6 DSI snapshot\\[bist-full|M6 LCM backlight|M4Ufault|underflow|not complete until EOF" | tail -220'
```

## 2026-06-08 OVL bounds-default and stale-layer clear isolation

PATCH HISTORY, ISOLATION + DIAGNOSTIC, 2026-06-08: the full ROM boot reached
late Android userspace but the physical panel stayed black and ADB later went
offline. The recovery capture verified that the boot partition still contained
the intended full-ROM boot image, then showed OVL0 layer-transition faults with
the bounds profile still at `0`. This patch promotes the already-tested M6
OVL bounds profile to the default and tightens the inactive-layer clear path
for stale OVL0 layers during direct-link/PQ-bypass composition. This is still
an isolation patch, not a proper display fix.

Hypothesis: FACT: recovery readback in
`/srv/forge/android/meizu_m6/captures/20260608-025410-m6-fullrom-c81boot-after-offline-recovery-711HEBSR277K5/boot-readback-recovery.sha256`
is `c81c52bba8c04707a92c0157917a53a2f72432225b24cf2955d64e4e6ee4d797`,
matching the full-ROM boot image embedded in
`/srv/forge/android/export/meizu_m6_artifacts/20260608-m6-full-rom-c81boot/lineage-15.1-20260608-UNOFFICIAL-meizu_m6-c81boot.zip`
sha256 `04f05e467a101d31a8cdfd323f655863041fdb7459d492d0174204841ce313e2`.
FACT: the target pstore/last_kmsg in the same capture shows backlight DCS
`0x51` writes, nonzero OVL buffer samples, then
`M6 OVL cpu preclear[2]: old_src=0xf enabled=0x7 stale=0x8`, followed by
OVL0 frame underflow, `L3 not complete until EOF`, and an IRQ snapshot where
`src=0x7`, `scan=0x0->0xf`, and `L3 live en=0` while `bounds=0` for active
layers. FACT from the previous profile scan in this file: runtime-selected
`m6_ovl_bounds_profile:1` removed the short-stimulus OVL/RDMA/M4U fault
markers while profile `0` reproduced them. HYPOTHESIS: the full-ROM black
runtime is still exercising the unfixed profile-0 OVL prefetch/stale-layer
transition path; starting with profile `1` and clearing stale disabled layer
registers plus the stale layer EOF IRQ should remove the proven OVL0
transition fault or move the frontier to DSI/panel-side evidence.

Evidence:
- Fresh capture:
  `/srv/forge/android/meizu_m6/captures/20260608-025410-m6-fullrom-c81boot-after-offline-recovery-711HEBSR277K5`.
- Capture evidence file:
  `display-runtime-evidence.txt`, especially the backlight lines, nonzero
  `M6 OVL m4u sample[...]` lines, and the OVL IRQ block at lines 59-74.
- Previous bounds-profile evidence in this file:
  `2026-06-07 OVL bounds profile end-prefetch isolation`.
- Built artifact:
  `/srv/forge/android/export/meizu_m6_artifacts/20260608-m6-ovl-default1-staleclear/boot-m6-ovl-default1-staleclear.img`,
  sha256
  `24937c5ced2aefae4d165d6ba49d30d8fb3d65df44e4577d72607a78d2b9f1f0`,
  size `8876032`.
- Built `Image.gz-dtb` sha256
  `498f012c8ca0cf4134677a0d3efc2bb4526d0afd26a297602ac2e1a571c609da`;
  matching `System.map` sha256
  `da7b8f87acba6e21c3a78f19093054ae150ce68955920893dfe48bb0daed081b`;
  `vmlinux` sha256
  `31e65991655757dd80fe33181c7e2c101dea09a7341f1b75cc2caf50ae446a14`.
- Build log:
  `/srv/forge/android/meizu_m6/kernel-meizu_M6-N-ex6-linux-3.18.140/build-m6-ovl-default1-staleclear-20260608.log`,
  sha256 `445797badc8f28d2af5a795969ab5e1c2dd578bc5bb9f801ebf34f6e7b357fa4`.
- Recovery flash verification: `/dev/block/mmcblk0p21` sized readback matched
  the new boot sha256
  `24937c5ced2aefae4d165d6ba49d30d8fb3d65df44e4577d72607a78d2b9f1f0`
  before rebooting Android.
- Live capture:
  `/srv/forge/android/meizu_m6/captures/20260608-031634-m6-ovl-default1-staleclear-live-711HEBSR277K5`.
  FACT: Android reached `sys.boot_completed=1`, `bootanim=stopped`,
  SurfaceFlinger and `input` were running, and `screencap.png` is non-black
  720x1280 RGBA. FACT: this clean logical display result moves the physical
  black-screen frontier below Android UI/HWC composition.
- Root OVL stimulus capture:
  `/srv/forge/android/meizu_m6/captures/20260608-032225-m6-ovl-default1-stimulus-root-711HEBSR277K5`.
  FACT: after explicitly writing `m6_ovl_bounds_profile:1`, fresh dmesg
  showed twelve `M6 OVL bounds profile apply[...] dst_h=1280->1279` markers
  and no fresh `M4Ufault`, `frame underflow`, `not complete until EOF`, or
  `RDMA0 underflow` in that stimulus window.
- DSI BIST capture:
  `/srv/forge/android/meizu_m6/captures/20260608-032427-m6-dsi-bist-pattern-root-711HEBSR277K5`.
  FACT: writing `dsipattern:0x00ff0000` toggled DSI registers to
  `BIST_PATTERN=0xff0000`, `BIST_CON=0x200040`, `self_pat=1`; MIPITX lane
  LDO/PLL state stayed powered. This proves the DSI BIST register path is
  writable, but it does not prove physical pixels without human observation.
- LCM reinit capture:
  `/srv/forge/android/meizu_m6/captures/20260608-032504-m6-lcm-reinit-root-711HEBSR277K5`.
  FACT: `m6_lcm_reinit:1` completed the 72-entry init table and returned
  `ret=0`, then poisoned the runtime with display fence timeouts and
  `service check input` temporarily reported `not found`. INFERENCE: manual
  LCM reinit is a harmful isolation probe in the current booted Android state,
  not a fix path to automate.
- Clean post-reboot capture:
  `/srv/forge/android/meizu_m6/captures/20260608-032840-m6-clean-post-reboot-root-711HEBSR277K5`.
  FACT: after rebooting away from the harmful reinit state, Android again
  reached `sys.boot_completed=1`, `bootanim=stopped`, SurfaceFlinger running,
  `input` found, and non-empty 720x1280 `screencap.png`. Remaining non-display
  blockers include Bluetooth enable timeout/crash and `emdlogger1` SIGABRT.
- Live bounds-profile rerun:
  `/srv/forge/android/meizu_m6/captures/20260608-042729-m6-live-bounds0-input-ab-711HEBSR277K5`.
  FACT: `m6_ovl_bounds_profile:0` returned `ret=0`, but the attempted Android
  `input` stimulus processes were killed and the post-probe OVL snapshot still
  showed `last cfg seq=1827`, `bounds=1`, live L0 `720x1279`, `valid=0x0`,
  `ready=0x4000937a`, and RDMA `in=0/0 out=0/0`. INFERENCE: this capture only
  proves the runtime setter changed the future profile value; it does not test
  profile-0 behavior because no new OVL MEM layer config occurred after the
  switch. Capture-local verdict:
  `/srv/forge/android/meizu_m6/captures/20260608-042729-m6-live-bounds0-input-ab-711HEBSR277K5/runtime-bounds0-result.md`.

Files changed:
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_ovl.c`: defaults
  `m6_ovl_bounds_profile_id` to `1`, adds a bounded inactive-layer register
  clear helper, applies it to stale disabled OVL0 layers in the direct-link /
  PQ-bypass path, and clears stale layer EOF IRQ bits with the same inverted
  `INTSTA` write pattern used by the IRQ handler.
- `BRINGUP_STATE.md`: records patch category, evidence, expected marker,
  rollback condition, and verification commands.

Why each file changed: `ddp_ovl.c` owns the exact OVL0 layer source/size/address
programming and already contains the tested bounds profile and stale-layer
preclear marker that fired immediately before the fresh `L3 not complete`
event. No DSI, panel, PQ, fence, or userspace behavior is changed by this patch.

Expected next marker: a fresh runtime stimulus with explicit
`m6_ovl_bounds_profile:1` should continue to show
`M6 OVL bounds profile apply[...] profile=1` without fresh OVL/RDMA/M4U
transition faults. The remaining physical black-screen frontier is below
logical framebuffer composition: DSI/panel stream acceptance, MIPI lane
mapping/timing, panel command state, or LK-to-kernel panel handoff. Do not
reuse `m6_lcm_reinit:1` as an automatic fix; it is currently a harmful
isolation probe.

Rollback condition: revert this patch if the device regresses before ADB,
SurfaceFlinger, backlight DCS `0x51`, or if `bounds=1` causes visible
composition cropping beyond the known one-line isolation, new M4U faults, or
new OVL/RDMA underflows that were not present with the verified profile-1
runtime scan.

Verification commands:

```bash
cd /srv/forge/android/meizu_m6/kernel-meizu_M6-N-ex6-linux-3.18.140
git diff --check -- kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_ovl.c BRINGUP_STATE.md
env CCACHE_DIR=/srv/forge/android/ccache make -C kernel-3.18 \
  O=/srv/forge/work/m6-source-kernel-manual-20260520/out \
  ARCH=arm64 \
  CROSS_COMPILE=/srv/forge/android/meizu_m6/rom-meizu_M6-lineage-cm-14.1/prebuilts/gcc/linux-x86/aarch64/aarch64-linux-android-4.9/bin/aarch64-linux-android- \
  -j8 Image.gz-dtb
gzip -cd /srv/forge/work/m6-source-kernel-manual-20260520/out/arch/arm64/boot/Image.gz-dtb 2>/tmp/m6-ovl-default1-gzip.err | strings | grep -E 'M6 OVL stale clear|M6 OVL bounds profile apply'
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell 'dmesg | grep -E "M6 OVL stale clear|M6 OVL bounds profile apply|M6 OVL irq diag|not complete until EOF|frame underflow|M4Ufault|M6 OVL m4u sample|M6 LCM backlight" | tail -360'
```

## 2026-06-07 OVL bounds profile end-prefetch isolation

PATCH HISTORY, ISOLATION + DIAGNOSTIC, 2026-06-07: after the per-layer IRQ
snapshot proved at least one DISP_OVL0 fault occurs exactly at the previous
full-screen layer `pitch_end`, this patch adds a runtime-selectable
`m6_ovl_bounds_profile:[0|1]` command and extends M4U handoff sampling to all
OVL MEM layers. Profile `0` preserves current behavior. Profile `1` programs
OVL0 normal MEM layers with hardware height `dst_h - 1` while keeping the
userspace request visible in logs; this is an isolation probe, not a proper
fix.

Hypothesis: FACT from
`/srv/forge/android/meizu_m6/captures/20260607-ovl-per-layer-irq-snapshot-flash-711HEBSR277K5/ui-stimulus/dmesg.txt`
shows `M6 OVL irq diag[6]` L2 full-screen `addr=0x2e00000` and
`pitch_end=0x3184000`, then `diag[7]` reuses L2 at `720x48`, and the same
stimulus later reports `M4Ufault: port=DISP_OVL0, mva=0x3184000`. FACT:
ION/MM and FB heap `phys()` paths call `m4u_alloc_mva_sg(..., buffer->size,
...)`, so a guard page cannot be assumed at the MVA end. HYPOTHESIS: OVL0 is
prefetching one burst/line past the programmed visible end during fast
full-screen-to-small-layer transitions; cropping the programmed height by one
line should move or remove the exact `pitch_end` M4U fault if this is the
active frontier.

Evidence:
- Current baseline diagnostic commit: `576f1551632` (`diag(m6-display): dump
  ovl layer state on irq`).
- Current tested boot artifact:
  `/srv/forge/android/export/meizu_m6_artifacts/20260607-m6-ovl-per-layer-irq-snapshot/boot-m6-ovl-per-layer-irq-snapshot.img`,
  sha256 `2e0b8822af14c28487b45b47fd5465e4b297b0e6f0b2e9ebe144156b628bd70d`.
- Matching tested `System.map` sha256
  `3655b4d591202bec68261fdacde26bf6b1f8d58b7ee3e8c9e1254208202c353a`.
- Baseline capture:
  `/srv/forge/android/meizu_m6/captures/20260607-ovl-per-layer-irq-snapshot-flash-711HEBSR277K5`.
- Source FACT: `kernel-3.18/drivers/staging/android/ion/mtk/ion_mm_heap.c`
  and `ion_fb_heap.c` allocate display MVA through `m4u_alloc_mva_sg` with
  `buffer->size`, not with an explicit display guard page.
- Built isolation artifact:
  `/srv/forge/android/export/meizu_m6_artifacts/20260607-m6-ovl-bounds-profile-isolation/boot-m6-ovl-bounds-profile-isolation.img`,
  sha256 `0b0762968f67496ee951c5283fed27475c7e4df790f353aa7a4e3795b313af7a`,
  size `8871936`.
- Built `Image.gz-dtb` sha256
  `8473a3c61b8e37ab1061c05dabc1e58135955943c4e298a7f6a1bcf12725a1f5`;
  matching `System.map` sha256
  `f22e5e70a73ab7b2ebbd17e354315d26cae7c34738ab7ec6267df9ad112a7170`;
  `vmlinux` sha256
  `6612e51bdff344b80a19584971c79ce236b85910782487eace01da6f5c300a3e`.
- Build log:
  `/srv/forge/android/meizu_m6/kernel-meizu_M6-N-ex6-linux-3.18.140/build-m6-ovl-bounds-profile-isolation-20260607.log`,
  sha256 `66ce2911e983ded0d48bb151c23b2fe6beca88bc0e9bf851e683870d87059253`.
- Flash/capture directory:
  `/srv/forge/android/meizu_m6/captures/20260607-ovl-bounds-profile-isolation-flash-711HEBSR277K5`.
  The boot partition was written as `/dev/block/mmcblk0p21`; pre-reboot and
  postboot sized readbacks both matched the local boot image, and the device
  reached `sys.boot_completed=1` with SurfaceFlinger/SystemUI running.
- Profile scan result:
  `/srv/forge/android/meizu_m6/captures/20260607-ovl-bounds-profile-isolation-flash-711HEBSR277K5/profile-scan-ui`.
  Profile `0` reproduced the active display frontier in a short UI stimulus:
  `M6 OVL irq diag=88`, `RDMA0 underflow=1`, `frame underflow=8`, and
  `not complete until EOF=8`. Profile `1` in the same short stimulus showed
  `M6 OVL bounds profile apply=96`, `M6 OVL irq diag=0`, `RDMA0 underflow=0`,
  `frame underflow=0`, `not complete until EOF=0`, and no `M4Ufault`.
- Extended profile `1` result:
  `/srv/forge/android/meizu_m6/captures/20260607-ovl-bounds-profile-isolation-flash-711HEBSR277K5/profile-scan-ui/profile-1-extended`.
  Twelve mixed HOME/swipe/menu windows showed `M4Ufault=0`, `M6 M4U=0`,
  `M6 OVL fault corr=0`, `M6 OVL bounds profile apply=96`,
  `M6 OVL irq diag=0`, `M6 OVL m4u sample=18`, `RDMA0 underflow=0`,
  `frame underflow=0`, and `not complete until EOF=0`.

Files changed:
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_ovl.c`: adds the M6
  bounds profile state, applies profile `1` only to OVL0 direct-link/PQ-bypass
  normal MEM layers, logs `dst_h -> hw_h`, and carries programmed height into
  the diagnostic end-address calculator.
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_ovl.h`: exposes
  `ovl_m6_set_bounds_profile()` and adds `hw_dst_h` / `bounds_profile` to the
  last-config snapshot.
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/disp_debug.c`: adds the
  `/d/mtkfb` command parser and help text for `m6_ovl_bounds_profile`.
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_irq.c`: prints
  requested `dst_h`, programmed `hw_h`, and bounds profile in IRQ snapshots.
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/primary_display.c`: expands
  M4U sample logging from only layer 0 to all normal MEM OVL layers and prints
  `layer_end`, `real_end`, `end_gap`, and `exact_end`.
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_drv.c`: adds bounded
  `M6 OVL fault corr[...]` lines in the DISP_OVL0 M4U fault callback, linking
  fault MVA, M4U valid end, live OVL0 registers, and the latest requested OVL0
  layer snapshot at fault time.
- `BRINGUP_STATE.md`: records patch category, evidence, expected marker,
  rollback condition, and verification commands.

Why each file changed: `ddp_ovl.c` is the only place that programs OVL layer
`SRC_SIZE` and calculates the effective read window. `disp_debug.c` is the
existing runtime command path used for M6 display isolation. `ddp_irq.c`,
`primary_display.c`, and `ddp_drv.c` provide before/after/fault-time evidence
needed to tell whether profile `1` changed the actual fault boundary instead
of hiding a userspace configuration bug.

Expected next marker: with profile `0`, fresh UI stimulus should reproduce
M4U faults at an L1/L2 `pitch_end` or show an exact `M6 OVL m4u sample` end
boundary. With profile `1`, `M6 OVL bounds profile apply[...]` should appear,
IRQ snapshots should show `hw_h=dst_h-1 bounds=1`, and the decisive marker is
whether `M6 OVL fault corr[...]` shows `fault_minus_end=0` at M4U valid end
while current live/requested layer ends have already moved, and whether
`M4Ufault` disappears, moves down by exactly one pitch, or persists at the old
full-screen `pitch_end`.

Rollback condition: revert this patch if profile `0` no longer reproduces the
baseline, if profile `1` regresses boot/ADB/SurfaceFlinger/RDMA transfer, if
the one-line crop visibly breaks otherwise working screencap composition, or if
faults continue at unchanged old `pitch_end` values after `hw_h=dst_h-1` is
proven active.

Verification commands:

```bash
cd /srv/forge/android/meizu_m6/kernel-meizu_M6-N-ex6-linux-3.18.140
git diff --check -- kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_ovl.c \
  kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_ovl.h \
  kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_irq.c \
  kernel-3.18/drivers/misc/mediatek/video/mt6755/disp_debug.c \
  kernel-3.18/drivers/misc/mediatek/video/mt6755/primary_display.c \
  kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_drv.c BRINGUP_STATE.md
env CCACHE_DIR=/srv/forge/android/ccache make -C kernel-3.18 \
  O=/srv/forge/work/m6-source-kernel-manual-20260520/out \
  ARCH=arm64 \
  CROSS_COMPILE=/srv/forge/android/meizu_m6/rom-meizu_M6-lineage-cm-14.1/prebuilts/gcc/linux-x86/aarch64/aarch64-linux-android-4.9/bin/aarch64-linux-android- \
  -j8 Image.gz-dtb
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell 'echo m6_ovl_bounds_profile:0 > /d/mtkfb; dmesg -c >/dev/null; input keyevent 3; input swipe 360 1050 360 250 250; sleep 3; dmesg | grep -E "M6 OVL bounds|M6 OVL m4u sample|M6 OVL fault corr|M6 OVL irq diag|M4Ufault|M6 M4U|RDMA0 underflow|frame underflow|not complete until EOF" | tail -320'
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell 'echo m6_ovl_bounds_profile:1 > /d/mtkfb; dmesg -c >/dev/null; input keyevent 3; input swipe 360 1050 360 250 250; sleep 3; dmesg | grep -E "M6 OVL bounds|M6 OVL m4u sample|M6 OVL fault corr|M6 OVL irq diag|M4Ufault|M6 M4U|RDMA0 underflow|frame underflow|not complete until EOF" | tail -360'
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell 'echo m6_ovl_bounds_profile:0 > /d/mtkfb'
```

## 2026-06-07 OVL per-layer IRQ snapshot diagnostic

PATCH HISTORY, DIAGNOSTIC, 2026-06-07: after the OVL GREQ profiles proved the
threshold variable is not a fix, this patch keeps behavior unchanged and adds
per-layer evidence at the OVL0 underflow boundary.

Hypothesis: FACT from
`/srv/forge/android/meizu_m6/captures/20260607-ovl-greq-profile-isolation-flash-711HEBSR277K5/profile-rescan-ui`
shows profiles `1..3` apply to OVL registers but still produce `RDMA0
underflow`, `OVL0 frame underflow`, EOF-not-complete bits, and DISP_OVL0
M4U faults. FACT: the old IRQ dump printed only L0 registers while fault lines
and `intsta` bits implicate higher layers too. HYPOTHESIS: the next frontier is
whether L1/L2/L3 live registers or the last requested OVL0 config point to a
specific out-of-range MVA, wrong pitch/size, or CMDQ/CPU config skew.

Evidence:
- Current tested boot artifact:
  `/srv/forge/android/export/meizu_m6_artifacts/20260607-m6-ovl-greq-profile-isolation/boot-m6-ovl-greq-profile-isolation-pgup.img`,
  sha256 `94eba1038b48ce215ac548206b93f4bb22a87bbe44f973343bff23e72eaf1bbb`.
- Matching tested `System.map` sha256
  `886a499003704742cff76aecf33f319ef79f0f0cd2d2cc4451479cd3030029e5`.
- Profile rescan FACT examples:
  `profile-rescan-ui/profile-0/dmesg.txt` shows `intsta=0x74`, `src=0xf`,
  `flow=0xf8c02`, and only an L0 decode before later `M4Ufault:
  port=DISP_OVL0, mva=0x4484000, layer=1`;
  `profile-rescan-ui/profile-1/dmesg.txt` shows profile `1` register values
  plus repeated M4U faults at `0x6484000`, `0x6c84000`, and `0x6084000`;
  `profile-rescan-ui/profile-2/dmesg.txt` still faults after profile `2` at
  `0x7884000`; profile `3` still faults at `0x9c84000`.
- Built diagnostic artifact:
  `/srv/forge/android/export/meizu_m6_artifacts/20260607-m6-ovl-per-layer-irq-snapshot/boot-m6-ovl-per-layer-irq-snapshot.img`,
  sha256 `2e0b8822af14c28487b45b47fd5465e4b297b0e6f0b2e9ebe144156b628bd70d`,
  size `8867840`.
- Built `Image.gz-dtb` sha256
  `cfa11890c78abe7892162f7b4dbf88a78c4e8a920d0319ec07ce6dbcaae378b3`.
- Built `System.map` sha256
  `3655b4d591202bec68261fdacde26bf6b1f8d58b7ee3e8c9e1254208202c353a`.
- Built `vmlinux` sha256
  `d4b9a157386556d83bf5d7c48dcf7f02615c5103cd658f481cc5b9ae447361b7`.
- Built `.config` sha256
  `bc272726035c1a2eca9422e9bc230cf54f8295648a8865a98faba046ab01619e`.
- Build log:
  `/srv/forge/android/meizu_m6/kernel-meizu_M6-N-ex6-linux-3.18.140/build-m6-ovl-per-layer-irq-snapshot-20260607.log`,
  sha256 `78b36df0b79375caf3badc32fd7fcfe884b673cd825764666f5ec54137a6e78f`.
- Flash/capture directory:
  `/srv/forge/android/meizu_m6/captures/20260607-ovl-per-layer-irq-snapshot-flash-711HEBSR277K5`.
- FACT: boot partition readback matched the local boot image before and after
  reboot, `sys.boot_completed=1` reached at loop `24`, bootanim stopped,
  SurfaceFlinger/SystemUI ran, `/d/mtkfb` reported `LCM
  Driver=[ili9881p_hd_dsi_txd]`, `PathMode:DIRECT_LINK`, `RDMA0 Transfer`
  about 61.71 fps, and `DISP_OPT_BYPASS_PQ=1`.
- FACT: `ui-stimulus/dmesg.txt` shows the added L0..L3 snapshots at OVL0 IRQ
  boundary. The decisive sample is `M6 OVL irq diag[7]`: L2 was live/requested
  as `addr=0x2e00000`, `size=720x48`, `pitch=0xb40`, `pitch_end=0x2e21c00`,
  while the previous full-screen L2 sample `diag[6]` had `addr=0x2e00000` and
  `pitch_end=0x3184000`. The same stimulus later faulted at `M4Ufault:
  port=DISP_OVL0, mva=0x3184000`, exactly the previous full-screen L2
  `pitch_end`.
- INFERENCE: the GREQ/PQ variable remains rejected for this frontier; the next
  evidence-backed display test should isolate OVL/M4U end-prefetch or mapping
  coverage at layer-size transitions, not add more RDMA EOF/CMDQ wait changes.

Files changed:
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_ovl.h`: defines the M6
  OVL0 last-config snapshot ABI shared between OVL config and IRQ diagnostic
  code.
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_ovl.c`: records the
  latest OVL0 direct-link/PQ-bypass config snapshot, including requested
  addr/final/visible_last/pitch_end for L0..L3.
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_irq.c`: expands bounded
  `M6 OVL irq diag` from L0-only to L0..L3 live registers plus the matching
  last requested layer config.
- `BRINGUP_STATE.md`: records patch category, evidence, expected marker,
  rollback condition, and verification commands.

Why each file changed: `ddp_ovl.c` owns requested OVL layer configuration, while
`ddp_irq.c` is where the failing underflow/EOF bits are observed. Printing both
views at the same sampled IRQ boundary is the minimum evidence needed before a
behavioral OVL/M4U/pitch fix.

Expected next marker: after flashing the rebuilt boot and stimulating UI, dmesg
should show `M6 OVL irq diag[*]: L0..L3 live ...` and `L0..L3 req seq=...`.
The decisive marker is the first layer whose live `addr/size/pitch/rdma_dbg`
or requested `visible_last/pitch_end` brackets the M4U fault MVA or contradicts
the enabled layer mask.

Rollback condition: revert this diagnostic if ADB/boot/SF regress, if log
volume makes captures unusable, or if the IRQ snapshot itself perturbs OVL
timing enough to remove the underflow without explaining it.

Verification commands:

```bash
cd /srv/forge/android/meizu_m6/kernel-meizu_M6-N-ex6-linux-3.18.140
git diff --check -- kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_ovl.c \
  kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_ovl.h \
  kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_irq.c BRINGUP_STATE.md
env CCACHE_DIR=/srv/forge/android/ccache make -C kernel-3.18 \
  O=/srv/forge/work/m6-source-kernel-manual-20260520/out \
  ARCH=arm64 \
  CROSS_COMPILE=/srv/forge/android/meizu_m6/rom-meizu_M6-lineage-cm-14.1/prebuilts/gcc/linux-x86/aarch64/aarch64-linux-android-4.9/bin/aarch64-linux-android- \
  -j8 Image.gz-dtb
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell 'dmesg -c >/dev/null; input keyevent 3; input swipe 360 1050 360 250 250; sleep 3; dmesg | grep -E "M6 OVL irq diag|M4Ufault|RDMA0 underflow|frame underflow|not complete until EOF" | tail -260'
```

## 2026-06-07 OVL GREQ profile isolation flashed

PATCH HISTORY, ISOLATION, 2026-06-07: after the DSI HS-video diagnostic boot
proved Android scanout remains alive while OVL/RDMA underflows and DISP_OVL0
M4U end-prefetch faults continue, this patch adds a runtime-selectable
`m6_ovl_greq_profile:[0|1|2|3]` debugfs command in `/d/mtkfb`. Profile `0`
preserves the current register values; profiles `1..3` rewrite only OVL0
RDMA/GREQ/urgent/buf-low golden-setting fields for live underflow isolation.
No default runtime behavior changes unless a profile is explicitly selected.

Hypothesis: FACT from the fresh postboot capture shows `sys.boot_completed=1`,
SurfaceFlinger/HWC are live, the screencap is nonblack, RDMA0 transfers at
about 62 fps, DSI/MIPITX self-pattern registers toggle, and `DISP_OPT_BYPASS_PQ`
is already `1`. FACT from the same capture shows repeated `RDMA0 underflow`,
`ovl0 frame underflow`, `ovl0 -Lx not complete until EOF`, and DISP_OVL0 M4U
faults exactly at the end of RGBA layer MVAs. HYPOTHESIS: one remaining
hardware frontier was whether MTK OVL RDMA/GREQ/urgent thresholds starve scanout
under this ROM/HWC layer mix. The debugfs profiles make that variable testable
without baking a new default into the kernel.

Evidence:
- Built artifact:
  `/srv/forge/android/export/meizu_m6_artifacts/20260607-m6-ovl-greq-profile-isolation/boot-m6-ovl-greq-profile-isolation-pgup.img`,
  sha256 `94eba1038b48ce215ac548206b93f4bb22a87bbe44f973343bff23e72eaf1bbb`,
  size `8867840`.
- Built `Image.gz-dtb` sha256
  `4d95a164545e3ae2dc27c6727ca1c9ad2469fc01180e944d38d21c9fe121d612`.
- Built `System.map` sha256
  `886a499003704742cff76aecf33f319ef79f0f0cd2d2cc4451479cd3030029e5`.
- Built `vmlinux` sha256
  `7864ba17a0ed9151fd29bbf9f42920a1cf7017df8ca79e924cab65e67641bef3`.
- Built `.config` sha256
  `bc272726035c1a2eca9422e9bc230cf54f8295648a8865a98faba046ab01619e`.
- Build log:
  `/srv/forge/android/meizu_m6/kernel-meizu_M6-N-ex6-linux-3.18.140/build-m6-ovl-greq-profile-isolation-20260607.log`,
  sha256 `6553aa5b20e5141ad8a273708c6acde3788704ea3534fe7289c012a902fac0e9`.
- Boot-image layout/ramdisk reused the previous verified DSI diagnostic boot.
  The new kernel was one 2048-byte page larger, so the artifact-local
  `bootimg-0x875000.cfg` increases `bootsize` from `0x874800` to `0x875000`;
  boot partition readback matched the local artifact before and after reboot.
- Flash/capture directory:
  `/srv/forge/android/meizu_m6/captures/20260607-ovl-greq-profile-isolation-flash-711HEBSR277K5`.
- Postboot FACT: `sys.boot_completed=1`, bootanim stopped, SurfaceFlinger
  running, SystemUI running, SELinux permissive, battery `6` and `Charging`.
  Postboot boot-partition prefix sha256 is
  `94eba1038b48ce215ac548206b93f4bb22a87bbe44f973343bff23e72eaf1bbb` and
  `postboot-prefix-cmp-ok`.
- Postboot FACT: `/d/mtkfb` still reports
  `LCM Driver=[ili9881p_hd_dsi_txd]`, `PathMode:DIRECT_LINK`,
  `RDMA0 Transfer` about 62 fps, and `DISP_OPT_BYPASS_PQ=1`.
- Profile scan FACT: in
  `profile-rescan-ui/profile-0/dmesg.txt`, profile `0` under UI stimulus still
  reports 29 underflow markers, 42 `M6 OVL irq diag` lines, and 6 M4U/end
  fault markers.
- Profile scan FACT: in
  `profile-rescan-ui/profile-1/dmesg.txt`, profile `1` successfully changes
  OVL registers to `ovl_greq=0x30ff5555`, `ovl_urg=0x13ff5555`,
  `flush_ultra=1`, `urg_th=0x3ff`, `buflow=0x20010`, but still reports 36
  underflow markers, 54 `M6 OVL irq diag` lines, and 6 M4U/end fault markers.
- Profile scan FACT: in
  `profile-rescan-ui/profile-2/dmesg.txt`, profile `2` successfully changes
  OVL registers to `layer=7/7/7/7`, `ovl_greq=0x30ff7777`,
  `buflow=0x40020`, but still reports 22 underflow markers, 39
  `M6 OVL irq diag` lines, and 2 M4U/end fault markers.
- Profile scan FACT: in
  `profile-rescan-ui/profile-3/dmesg.txt`, profile `3` successfully changes
  OVL registers to `layer=3/3/3/3`, `ostd=0x40`, `gmc=0x8080`, but still
  reports 26 underflow markers, 12 capped `M6 OVL irq diag` lines, and 2
  M4U/end fault markers.

Files changed:
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_ovl.c`: factors OVL
  RDMA/GREQ golden-setting constants into variables, adds profiles `0..3`, and
  adds `ovl_m6_set_greq_profile()` to apply OVL0 CPU registers on demand.
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_ovl.h`: exports the M6
  profile setter to the debug command parser.
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/disp_debug.c`: adds
  `m6_ovl_greq_profile:[0|1|2|3]` to `/d/mtkfb` help and parser.
- `BRINGUP_STATE.md`: records the patch category, artifact identity, flash
  identity, profile scan verdict, expected next marker, rollback condition, and
  verification commands.

Why each file changed:
- `ddp_ovl.c` owns OVL golden-setting writes and the live OVL0 registers that
  the underflow diagnostics dump. The fresh capture proves OVL/RDMA faults
  continue after PQ bypass and live DSI, so the patch isolates only the OVL
  RDMA/GREQ threshold variable.
- `ddp_ovl.h` is required because the existing `/d/mtkfb` parser lives in
  `disp_debug.c`.
- `disp_debug.c` is the existing bounded debug command path for display bringup
  commands such as `m6_lcm_reinit`; using it avoids a new userspace ABI.
- `BRINGUP_STATE.md` is the selected device-local state file for this tree.

Result and next marker: FACT: profiles `1..3` apply and are observable in the
OVL irq diag decode, but none closes the OVL/RDMA underflow or M4U end-fault
frontier under UI stimulus. INFERENCE: do not promote any GREQ profile to a
default/proper fix. The next patch should move to the earlier proven register
state inside the same frontier: why `flow=0xf8c02`/`sta=0x1e` blank/zero-in-out
cycles and stale/multi-layer `OVL0_SRC` transitions recur under HWC UI
composition even with DSI active, RDMA transfer live, and PQ bypassed.

Rollback condition: revert this isolation patch if `/d/mtkfb` parsing regresses,
profile `0` no longer reproduces the previous OVL defaults
(`ovl_greq=0x10ff5555`, `ovl_urg=0x5555`, `buflow=0x0`), Android boot/screencap
regresses, or the debug command causes high log volume outside explicit profile
testing.

Verification commands:

```bash
cd /srv/forge/android/meizu_m6/kernel-meizu_M6-N-ex6-linux-3.18.140
git diff --check -- kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_ovl.c \
  kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_ovl.h \
  kernel-3.18/drivers/misc/mediatek/video/mt6755/disp_debug.c BRINGUP_STATE.md
env CCACHE_DIR=/srv/forge/android/ccache make -C kernel-3.18 \
  O=/srv/forge/work/m6-source-kernel-manual-20260520/out \
  ARCH=arm64 \
  CROSS_COMPILE=/srv/forge/android/meizu_m6/rom-meizu_M6-lineage-cm-14.1/prebuilts/gcc/linux-x86/aarch64/aarch64-linux-android-4.9/bin/aarch64-linux-android- \
  -j8 Image.gz-dtb
sha256sum /srv/forge/android/export/meizu_m6_artifacts/20260607-m6-ovl-greq-profile-isolation/boot-m6-ovl-greq-profile-isolation-pgup.img \
  /srv/forge/work/m6-source-kernel-manual-20260520/out/arch/arm64/boot/Image.gz-dtb \
  /srv/forge/work/m6-source-kernel-manual-20260520/out/System.map
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell 'echo m6_ovl_greq_profile:0 > /d/mtkfb; dmesg -c >/dev/null; input keyevent 3; input swipe 360 1050 360 250 250; sleep 3; dmesg | grep -E "M6 OVL greq profile|M6 OVL irq diag|M4Ufault|M6 M4U|RDMA0 underflow|frame underflow|L2 not complete" | tail -120'
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell 'echo m6_ovl_greq_profile:2 > /d/mtkfb; dmesg -c >/dev/null; input keyevent 3; input swipe 360 1050 360 250 250; sleep 3; dmesg | grep -E "M6 OVL greq profile|M6 OVL irq diag|M4Ufault|M6 M4U|RDMA0 underflow|frame underflow|L2 not complete" | tail -120'
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell 'echo m6_ovl_greq_profile:0 > /d/mtkfb'
```

## 2026-06-07 DSI HS video diagnostic patch built

PATCH HISTORY, DIAGNOSTIC, 2026-06-07: task 4 of
`/srv/forge/android/meizu_m6/.ai-factory/plans/m6-physical-black-stock-reverse-display-plan.md`
adds bounded DSI HS-video boundary markers and delayed post-start snapshots in
`ddp_dsi.c`. This patch is read-only instrumentation; it does not change panel
timing, PLL, lane count, init table, TPS/reset behavior, PQ, OVL/M4U, or fence
logic.

Hypothesis: current source and stock evidence agree on panel identity, primary
DSI params, direct-link route, PQ bypass, RDMA transfer, and backlight command
path, but the current physical-black capture lacks live DSI/MIPITX HS-video
state after RDMA feeds `dsi0`. The earliest remaining evidence gap is whether
DSI video mode, DSI FSM, MIPITX PLL/lane state, and VM command state remain
healthy after `DSI_START`.

Evidence:
- Fresh physical-black identity capture:
  `/srv/forge/android/meizu_m6/captures/20260607-222849-m6-physical-black-identity-dsi-frontier-711HEBSR277K5`.
- Stock LK reverse report:
  `/srv/forge/android/meizu_m6/captures/20260530-stock-lk-boot-reverse-inputs/lk_display_reverse.md`.
- Stock/current display parity report:
  `/srv/forge/android/meizu_m6/captures/20260530-stock-lk-boot-reverse-inputs/stock_display_parity_matrix.md`.
- Build log:
  `/srv/forge/android/meizu_m6/kernel-meizu_M6-N-ex6-linux-3.18.140/build-m6-dsi-hs-video-diag-20260607.log`,
  sha256 `a3bfdebb5691ca03152c8fc51a1e5e8e80c6e2b20d19ebe86859055e12494e8f`.
- Built `Image.gz-dtb` sha256
  `b5016b9b3872206c0cfd687b59be21db90e5ed0d3359c70c70030db1dd86087e`.
- Built `System.map` sha256
  `e7e5ffe959cc8d3bf7fcb3850f43f03d4133163717f67277e534b35e4f765784`.
- Built `vmlinux` sha256
  `66f8ecd3a6b651bc345ea2703b68d2db38bac2d6e86a8f8a827b7be3e90896db`.
- Built `.config` sha256
  `bc272726035c1a2eca9422e9bc230cf54f8295648a8865a98faba046ab01619e`.
- Packed boot artifact:
  `/srv/forge/android/export/meizu_m6_artifacts/20260607-m6-dsi-hs-video-diag/boot-m6-dsi-hs-video-diag.img`,
  sha256 `2552752194ea41a93aadad584e8d42f842ce5ecccd40f2f9e1574fe2a491440d`.
- Unpack verification confirmed artifact kernel `zImage` hash matches the new
  `Image.gz-dtb`, and artifact ramdisk hash matches the current boot ramdisk.
- `strings` on the built image finds `M6 DSI HS video[%s]`,
  `after-1vsync`, `after-500ms`, `start-before`, `start-after`, and
  `config-done`.

Files changed:
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_dsi.c`: adds
  workqueue/jiffies includes, a bounded `M6 DSI HS video[...]` marker wrapper,
  delayed work snapshots after one vsync and after 500 ms, and routes
  `config-done` / `DSI_Start()` diagnostics through that wrapper.
- `BRINGUP_STATE.md`: records Task 3 parity and this Task 4 diagnostic patch
  summary, artifact identity, expected next markers, rollback condition, and
  verification commands.

Why each file changed:
- `ddp_dsi.c` owns DSI config/start, live DSI registers, and MIPITX register
  access. The fresh capture proves DDP/RDMA/backlight are alive but lacks the
  post-start DSI/MIPITX state, so the patch adds read-only markers exactly at
  config, start-before, start-after, and delayed post-start boundaries.
- `BRINGUP_STATE.md` is the selected device-local durable state file for this
  kernel tree and must mirror patch evidence and rollback rules.

Expected next marker:
- Healthy transport should show `M6 DSI HS video[config-done]`,
  `[start-before]`, `[start-after]`, `[after-1vsync]`, and `[after-500ms]`
  with video mode active, expected PLL/lane/packet/timing values, DSI start on,
  MIPITX PLL/lane state not off/ULPS, and DSI FSM/counters changing after the
  delayed snapshots.
- Failing transport should show RDMA/DDP still active while DSI/MIPITX state is
  static, off, ULPS-like, timeouted, or inconsistent with stock runtime DSI
  state (`START=0x1`, `MODE=0x3`, `PS=0x30870`, `PHY_LCCON=0x1`,
  `STATE_DBG=0x40010/0x1080010/0x1010001/0x8100810`).

Rollback condition: revert this diagnostic patch if the rebuilt boot image
regresses ADB, `sys.boot_completed`, SurfaceFlinger flips, RDMA transfer,
current backlight/DCS command path, or causes log volume high enough to make the
device unusable or the capture unparsable.

Verification commands:

```bash
cd /srv/forge/android/meizu_m6/kernel-meizu_M6-N-ex6-linux-3.18.140
git diff --check -- kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_dsi.c BRINGUP_STATE.md
env CCACHE_DIR=/srv/forge/android/ccache make -C /srv/forge/android/meizu_m6/kernel-meizu_M6-N-ex6-linux-3.18.140/kernel-3.18 \
  O=/srv/forge/work/m6-source-kernel-manual-20260520/out \
  ARCH=arm64 \
  CROSS_COMPILE=/srv/forge/android/meizu_m6/rom-meizu_M6-lineage-cm-14.1/prebuilts/gcc/linux-x86/aarch64/aarch64-linux-android-4.9/bin/aarch64-linux-android- \
  -j8 Image.gz-dtb
sha256sum /srv/forge/android/export/meizu_m6_artifacts/20260607-m6-dsi-hs-video-diag/boot-m6-dsi-hs-video-diag.img \
  /srv/forge/work/m6-source-kernel-manual-20260520/out/arch/arm64/boot/Image.gz-dtb \
  /srv/forge/work/m6-source-kernel-manual-20260520/out/System.map
gzip -cd /srv/forge/work/m6-source-kernel-manual-20260520/out/arch/arm64/boot/Image.gz-dtb 2>/tmp/m6-dsi-hs-diag-gzip.err | \
  strings | grep -E 'M6 DSI HS video|after-1vsync|after-500ms|start-before|start-after|config-done'
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell 'dmesg | grep -E "M6 DSI HS video|M6 DSI snapshot|M6 DSI phydecode|M6 LCM backlight|RDMA0 Transfer|M4Ufault" | tail -400'
```

## 2026-06-07 stock boot/kernel display parity checkpoint

STATE / EVIDENCE CHECKPOINT, 2026-06-07: task 3 of
`/srv/forge/android/meizu_m6/.ai-factory/plans/m6-physical-black-stock-reverse-display-plan.md`
unpacked the stock and current boot images and compared LK/source/runtime
display facts. Report path:
`/srv/forge/android/meizu_m6/captures/20260530-stock-lk-boot-reverse-inputs/stock_display_parity_matrix.md`.

FACT: stock boot image
`/srv/forge/android/export/meizu_m6_artifacts/20260530-m6-stock-lcm-parity-72entry/boot-m6-stock-lcm-parity-72entry.img`
has sha256 `7be765bd7aa180ace519db0ba4fe6a9c27b6f27b3138364a3ec4fa8246b0e0f2`,
page size `2048`, kernel load address `0x40080000`, ramdisk load address
`0x45000000`, tags address `0x44000000`, extracted `zImage` sha256
`90ef931093dc8e01a2068016ab0f966f465802f41b3bfab32d60aefb69a33bfe`,
and gzip-decoded `Image` sha256
`238ee31caf81968a805250bc07344ce5bac574cad95e248be5fba2554856b4da`.

FACT: current physical-black boot image
`/srv/forge/android/meizu_m6/rom-lineage-15.1-meizu_m6-experimental/out/target/product/meizu_m6/boot.img`
has sha256 `a201f25f92317a6157bd7ca855ada4edb1bf7d4f9d7545b510067a6c99b4f9b4`,
the same bootimg page/load-address layout, extracted `zImage` sha256
`50820adf50d1d1d720f873de73dad2d613d18aa439bb978ba609544b78256b92`,
and gzip-decoded `Image` sha256
`6a8d15645202a916dd9c75da69d3d641583d527ff19f35e420883bf548620f40`.

FACT: stock LK, stock runtime, current source, and current runtime agree on
panel driver name `ili9881p_hd_dsi_txd`, resolution `720x1280`, four DSI
lanes, packet size `256`, primary porch values `20/24/64/1280` and
`20/80/100/720`, PLL `230`, direct-link DDP route, `DISP_OPT_BYPASS_PQ=1`,
RDMA transfer, and backlight command path. The trailing DTB bytes in
`Image.gz-dtb` were not decoded in this task; compiled DTB parity remains task
8.

INFERENCE: the next evidence gap is live DSI HS video/MIPITX state after RDMA
feeds `dsi0`. Do not change timing, PLL, init table, TPS, or OVL/M4U behavior
before collecting the Task 4 markers.

NEXT ACTION: add bounded `DIAGNOSTIC` markers named
`M6 DSI HS video[config-done]`, `[start-before]`, `[start-after]`,
`[after-1vsync]`, and `[after-500ms]` in `ddp_dsi.c`, then build and capture.

## 2026-06-07 stock LK display reverse checkpoint

STATE / EVIDENCE CHECKPOINT, 2026-06-07: task 2 of
`/srv/forge/android/meizu_m6/.ai-factory/plans/m6-physical-black-stock-reverse-display-plan.md`
reversed the stock LK display handoff enough to constrain the next kernel
patch. Report path:
`/srv/forge/android/meizu_m6/captures/20260530-stock-lk-boot-reverse-inputs/lk_display_reverse.md`.

FACT: `lk.img` sha256 is
`b32d7ae68c918195632faf730a5fd6fc0136e090c100f4fe6eddfba4c56746bc`;
`lk2.img` sha256 is
`7f2597d35ce8297145d27e51258c5d03d4044bb7085b2ba55e90a8907fa84708`.
The two images differ only at offsets 11187-11190. Runtime VA mapping is
`fileoff - 0x200 + 0x46000000`; rizin disassembly used map base
`0x45fffe00`.

FACT: stock LK contains `ili9881p_hd_dsi_txd`, and the fresh current runtime
capture reports the same active LCM driver in `/proc/mtkfb`. Stock LK
`get_params` writes the same primary current-source display params: 720x1280,
four lanes, packet size 256, vertical `20/24/64/1280`, horizontal
`20/80/100/720`, RGB888 PS, and PLL 230. Stock LK `lcm_init` pushes a 0x48
entry table starting at file offset `0x5f434`; current source already carries
that stock Flyme LK/kernel `init_setting[]`.

INFERENCE: do not start the next patch by changing panel name, DSI
lane/porch/PLL, or the init table. The next evidence-backed boundary is DSI HS
video/MIPITX state after RDMA feeds `dsi0`.

NEXT ACTION: implement a bounded `DIAGNOSTIC` DSI HS video marker patch in
`ddp_dsi.c`, then rebuild/flash/capture before any behavior change.

## 2026-06-07 fresh physical-black identity gate

STATE / EVIDENCE CHECKPOINT, 2026-06-07: task 1 of
`/srv/forge/android/meizu_m6/.ai-factory/plans/m6-physical-black-stock-reverse-display-plan.md`
captured a fresh physical-black display identity bundle after clearing logs and
rebooting `711HEBSR277K5`.

FACT: capture path is
`/srv/forge/android/meizu_m6/captures/20260607-222849-m6-physical-black-identity-dsi-frontier-711HEBSR277K5`.
The boot partition readback is a 16 MiB image with sha256
`ef61d1a64284fbe295743ec712d8dc132cdf9defcf240edf2a4de98e295301bd`.
Its prefix matches local ROM `boot.img` size `8865792`, sha256
`a201f25f92317a6157bd7ca855ada4edb1bf7d4f9d7545b510067a6c99b4f9b4`.
Matching local context recorded `Image.gz-dtb` sha256
`50820adf50d1d1d720f873de73dad2d613d18aa439bb978ba609544b78256b92`,
`System.map` sha256
`dc78cb63c233f1672fbca66c929f5016e13b3b8c1950bc7e828518b553cc4207`,
and `.config` sha256
`bc272726035c1a2eca9422e9bc230cf54f8295648a8865a98faba046ab01619e`.

FACT: fresh runtime state is Android-booted and composition-live:
`sys.boot_completed=1`, SurfaceFlinger running, non-black 720x1280
`screencap.png` sha256
`5f066250bdc29b81ed32e9692926991c0219b0b080994eb8ea789b530696d4bd`,
SurfaceFlinger `powerMode=2`, `isDisplayOn=1`, `flips=1738`, and Android
display state `mActualBacklight=255`.

FACT: kernel display path remains direct and active:
`LCM Driver=[ili9881p_hd_dsi_txd]`, `PathMode:DIRECT_LINK`,
`RDMA0 Transfer` about `62.0 fps`, `DISP_OPT_BYPASS_PQ=1`, and DDP path cfg
walks through `dsi0`. No `M6 DSI HS video[...]`, `MIPITX`, `DSI_START`, or
`STATE_DBG` marker set exists in this boot beyond the route handoff to `dsi0`.

INFERENCE: the next implementation frontier remains DSI HS video
acceptance/timing/lane/mode, not PQ, generic SurfaceFlinger/scrcpy,
backlight-off, or DDP route construction. OVL/M4U end faults still recur at
`delta=0x0`, but remain a secondary branch unless future evidence correlates
them with physical black.

NEXT ACTION: reverse stock LK/boot display handoff from
`captures/20260530-stock-lk-boot-reverse-inputs/lk.img` and `lk2.img`, then
add bounded `DIAGNOSTIC` DSI HS markers in `ddp_dsi.c` before any behavior
patch.

## 2026-06-07 post-GUI-shim physical display frontier: OVL/M4U

STATE / EVIDENCE CHECKPOINT, 2026-06-07: after the ROM-side targeted GUI
linker shim was live-pushed, Android framebuffer output and scrcpy/screencap
must be treated separately from the physical LCD.

FACT: live `adb` check on `711HEBSR277K5` showed `sys.boot_completed=1`,
`surfaceflinger=running`, and SystemUI pid `1030`. `dumpsys SurfaceFlinger`
showed real active buffers for Launcher, ImageWallpaper, and StatusBar.
Fresh `/tmp/m6-live-screencap.png` was 720x1280, sha256
`d24617e6b9ad7e9c04dac50181c8dcffdefafb2934c20d4f8a96b6c5d4c5ef1c`,
with mean brightness about `0.20`; it was not an all-black frame.

FACT: the same post-fix capture
`/srv/forge/android/meizu_m6/captures/20260607-162257-m6-targeted-gui-shim-linker-reboot`
still contains lower-level display anomalies. `dmesg.txt` reports
`M4Ufault: port=DISP_OVL0` at `0x1984000` and `0x3184000`, while the matching
M6 marker reports `valid=0x1600000 size=0x384000 end=0x1984000` and
`valid=0x2e00000 size=0x384000 end=0x3184000`. `0x384000` is exactly
720x1280x4 bytes, so both faults are exactly at the end of an RGBA layer MVA.

FACT: the same capture reports OVL underflows and `L2 not complete until EOF`.
The OVL register dump has ROI `0x50002d0`, source size `0x50002d0`, pitch
`0x10001680`, and address examples such as `0x1e00000`, matching a 720x1280
direct-link scanout path. `mtkfb` still reports `DISP_OPT_BYPASS_PQ=1`.

FACT: source comparison with `android_kernel_collection_mt6750-P-ex2` and
`android_kernel_collection_mt6750-Q-ex2` shows the MTK OVL span formula is the
same donor behavior: `(cfg->dst_h - 1) * cfg->src_pitch + dst_w * Bpp`.
Those donors also carry the generic display translation-fault bypass for
faults within `+SZ_4K` of a valid MVA end. The current M6 `M4UM6` line is a
diagnostic marker on that path, not evidence that PQ or Android composition is
the active blocker.

INFERENCE: if the human still sees a physically black LCD while the live
screencap is non-black, do not reopen SurfaceFlinger, scrcpy, or PQ. The next
kernel frontier is OVL/M4U/SMI/DSI physical scanout: determine whether the OVL
end fault is a harmless hardware prefetch that is already bypassed, or whether
it correlates with the underflow/L2 EOF and prevents physical visibility.

NEXT ACTION: collect a fresh physical-black capture after forcing brightness
255 and waking the screen, then compare live `dmesg` before/after screen
transitions for new `M4Ufault`, `M4UM6`, `OVL irq`, `frame underflow`, and
`L2 not complete until EOF`. Only patch OVL/M4U behavior if the fresh capture
proves the fault recurs with the physical black state. Do not classify a
larger `+4K` bypass or fake fence/EOF release as a proper fix.

## 2026-06-06 charger power_supply Android export diagnostics

PATCH HISTORY, PROPER-FIX + DIAGNOSTIC, 2026-06-06: expose Android-standard
battery voltage/temperature units from the MTK fuel-gauge driver and add
bounded charger/SOC-ready markers.

Hypothesis: the charger hardware path is alive, but Android userspace sees
stale default battery state because stock `fuelgauged` is not running and the
kernel's standard `power_supply` attributes are incomplete/wrong for Android
healthd. Once `fuelgauged` can set SOC-ready, Android should read standard
`voltage_now` in microvolts and `temp` in tenths of a degree C instead of
falling into zero-valued or MTK-private attributes.

Evidence:
- Fresh capture:
  `/srv/forge/android/meizu_m6/captures/20260606-101042-m6-nondisplay-runtime-711HEBSR277K5`.
- Kernel logs in that capture repeatedly show charger hardware present and
  active: `vbus` about 4.3 V, `chrdet:1`, `CHR_Type 1`, `CC mode charge`, and
  `bq2415x_charging: enable charger successfully`.
- The same capture and live readback show Android battery defaults:
  `dumpsys battery` reports USB/AC false, status `4`, level `50`, voltage `0`,
  and temperature `0`; live `/sys/class/power_supply/battery/uevent` reports
  `POWER_SUPPLY_BATT_VOL=4214` but `POWER_SUPPLY_VOLTAGE_NOW=0` and
  `POWER_SUPPLY_BATT_TEMP=0`.
- `BatteryMonitor.cpp` chooses `voltage_now` before the MTK-private `batt_vol`
  fallback and divides it by 1000, so `voltage_now` must be microvolts.
- `kernel-3.18/include/linux/power_supply.h` defines both
  `POWER_SUPPLY_PROP_VOLTAGE_NOW` and `POWER_SUPPLY_PROP_TEMP`.
- Build verification completed:
  `env CCACHE_DIR=/srv/forge/android/ccache make -C .../kernel-3.18
  O=/srv/forge/work/m6-source-kernel-manual-20260520/out ARCH=arm64
  CROSS_COMPILE=...aarch64-linux-android- -j8 Image.gz-dtb`.
- Built artifact hashes:
  `Image.gz-dtb` sha256
  `4b66c82e160577af9b82baa6ad8d15c8910292eb09aa0b837cdc482e7d1b07e2`;
  `System.map` sha256
  `aa08f558b28e5a5b1792f434d3b9700e820810de60bd4800642f32e310aef21a`;
  `vmlinux` sha256
  `88adc0f35b7a9e40714b3105a30fca65ad0fcf010b23de0caf9ec840005c7bf7`.
- `strings` on the built `Image.gz-dtb` finds
  `[M6_CHG] soc_ready update ...` and `[M6_CHG] wait_soc_ready ...`.

Files changed:
- `kernel-3.18/drivers/power/mediatek/battery_common_fg_20.c`: adds standard
  `POWER_SUPPLY_PROP_TEMP`, returns `voltage_now` in microvolts with a fallback
  to the MTK battery mV field, and adds rate-limited `[M6_CHG]` markers around
  the `g_battery_soc_ready` gate and published AC/USB/battery state.

Why each file changed: this is the compiled M6 MTK fuel-gauge driver that owns
the `battery`, `usb`, and `ac` `power_supply` nodes. The fresh capture proves
hardware charging is active below this layer while Android sees stale exported
state above it.

Expected next marker: with the rebuilt kernel and rebuilt system image flashed,
fresh dmesg should show either `[M6_CHG] wait_soc_ready ...` if `fuelgauged`
still cannot initialize, or `[M6_CHG] soc_ready update ...` with nonzero
voltage/temp and the correct USB/AC online value if the daemon unlocks the
SOC-ready path. `dumpsys battery` should then stop showing voltage `0` and
temperature `0`.

Rollback condition: revert this kernel change if a fresh capture with the
rebuilt image shows malformed `power_supply` sysfs files, healthd parse
failures caused by these attributes, or a charger regression where bq2415x no
longer enables charging.

Verification commands:

```bash
cd /srv/forge/android/meizu_m6/kernel-meizu_M6-N-ex6-linux-3.18.140
git diff --check -- kernel-3.18/drivers/power/mediatek/battery_common_fg_20.c BRINGUP_STATE.md
env CCACHE_DIR=/srv/forge/android/ccache make -C /srv/forge/android/meizu_m6/kernel-meizu_M6-N-ex6-linux-3.18.140/kernel-3.18 \
  O=/srv/forge/work/m6-source-kernel-manual-20260520/out \
  ARCH=arm64 \
  CROSS_COMPILE=/srv/forge/android/meizu_m6/rom-meizu_M6-lineage-cm-14.1/prebuilts/gcc/linux-x86/aarch64/aarch64-linux-android-4.9/bin/aarch64-linux-android- \
  -j8 Image.gz-dtb
sha256sum /srv/forge/work/m6-source-kernel-manual-20260520/out/arch/arm64/boot/Image.gz-dtb /srv/forge/work/m6-source-kernel-manual-20260520/out/System.map
gzip -cd /srv/forge/work/m6-source-kernel-manual-20260520/out/arch/arm64/boot/Image.gz-dtb 2>/tmp/m6-chg-gzip.err | strings | grep -E 'M6_CHG|soc_ready|wait_soc_ready'
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell 'dmesg | grep M6_CHG | tail -40; dumpsys battery; for d in /sys/class/power_supply/*; do echo ===$d===; cat $d/uevent; done'
```

## 2026-06-06 display awake capture after mediaserver hotfix

STATE / EVIDENCE CHECKPOINT, 2026-06-06: after the live
`/system/etc/init/mediaserver.rc` hotfix, the phone was rebooted, forced awake
with brightness 255, and a fresh read-only display capture was taken from
`711HEBSR277K5`.

FACT: capture
`/srv/forge/android/meizu_m6/captures/20260606-1356-m6-display-awake-after-mediahotfix-711HEBSR277K5`
shows `sys.boot_completed=1`, `mWakefulness=Awake`, display power state `ON`,
global display state `ON`, power request `policy=BRIGHT`, and
`screenBrightness=255`.

FACT: Android composition remains live. `surfaceflinger.txt` shows the built-in
screen at `720x1280`, `powerMode=2`, `isDisplayOn=1`, `flips=1190`, refresh
about `57.66 fps`, HWC present but disabled, and GLES framebuffer-target
composition. `screencap-display-awake.png` is a valid 720x1280 Android UI PNG,
sha256 `b79d4e141ef2248e4df52606c8f70a76b6a2347ddb30b2decc3bb0310b06021f`.

FACT: kernel display path and clocks are alive. `mtkfb-key-strings.txt` shows
`LCM Driver=[ili9881p_hd_dsi_txd]`, `State=Alive`, `PathMode:DIRECT_LINK`,
`RDMA0 Transfer` about `61.20 fps`, and `DISP_OPT_BYPASS_PQ=1`.
`clk-summary-display.txt` shows DSI engine/digital, OVL0, RDMA0, COLOR,
DITHER, SMI common/LARB0, and `infra_disppwm` clocks enabled/prepared in the
awake state.

FACT: backlight command plumbing is alive in the current kernel. `dmesg.txt`
shows resume at about `332s`, DSI power-on, DSI start snapshots, `[PWM]
backlight is on (1023)`, and LCM DCS backlight command `0x51` with
`dcs51=0xff` for requested brightness 255.

INFERENCE: if the human still sees a physically black LCD for this capture,
the current frontier is below Android composition, PQ, DDP direct-link
construction, RDMA transfer, DSI clock gating, display PWM clock enable, and
LCM DCS 0x51 command submission. Remaining evidence-backed candidates are
panel LED electrical route/readback, DSI HS video acceptance, or panel
timing/mode parity with stock/Q reference.

NEXT ACTION: do not re-open PQ or generic SurfaceFlinger/scrcpy for physical
black. The already-built diagnostic boot remains the next clean flash candidate:
`/srv/forge/android/export/meizu_m6_artifacts/20260606-m6-physical-black-led-dcs-pwmguard-diag/boot-m6-physical-black-led-dcs-pwmguard-diag.img`,
sha256 `0e9ba01b34559b03026818e7c9dec02f283d77ab174c75614b9b9c95c261e78d`.
It should add LED route, DCS `0x51/0x53/0x55`, GPIO/TPS, and guarded PWM
readbacks. Do not flash it without explicit human `шей` confirmation.

## 2026-06-06 scrcpy-visible physical-black awake capture

STATE / EVIDENCE CHECKPOINT, 2026-06-06: the user reported that scrcpy shows
the rendered Android image while the physical LCD remains black. A fresh
read-only awake capture was collected from `711HEBSR277K5`.

FACT: capture
`/srv/forge/android/meizu_m6/captures/20260606-093239-m6-live-awake-physical-black-711HEBSR277K5`
was taken after forcing `screen_off_timeout=2147483647`,
`screen_brightness=255`, `svc power stayon true`, and wake key input.
`getprop` in that capture shows `sys.boot_completed=1`,
`init.svc.surfaceflinger=running`, and `ro.boot.bootreason=wdt_by_pass_pwk`.

FACT: Android display policy and SurfaceFlinger consider the panel awake and
compositing. `dumpsys-power.txt` shows `mWakefulness=Awake` and
`Display Power: state=ON`; `dumpsys-display.txt` shows
`mGlobalDisplayState=ON`, built-in screen state `ON`, and
`screenBrightness=255`; `dumpsys-window-policy.txt` shows
`mScreenOnEarly=true mScreenOnFully=true`; `surfaceflinger.txt` shows
`powerMode=2`, `isDisplayOn=1`, `layerStack=0`, `flips=1310`, and `numLayers=3`.

FACT: framebuffer/userspace content is not all-black. `screencap.png` is a
valid `720 x 1280` PNG with sha256
`8241c2547409cbd98a36bb19ed90dcf77894afbe5fe6f57b74cdb507cd8add9f`.
The first 16 KiB of `/dev/graphics/fb0` were captured as `fb0-head.bin`,
sha256 `3d558540e8c59a9e6915aaa700c8d714c8927167920bf0bcf84c4ece2589f368`.

FACT: kernel display path is alive while the physical panel is still black.
`mtkfb-after-ata.txt` shows `LCM Driver=[ili9881p_hd_dsi_txd]`,
`State=Alive`, `PathMode:DIRECT_LINK`, `RDMA0 Transfer` about `61.06 fps`,
and `DISP_OPT_BYPASS_PQ=1`.

FACT: DSI LP command/read path is healthy in the same awake capture.
The ATA-triggered DCS sweep returns panel identity and status:
`display_id=15 20 00`, `display_status=80 03 06 00`,
`power_mode=9c`, `pixel_format=07`, `id1=15`, `id2=20`, and `id3=00`.
`0x2a` and `0x2b` still read `00 00 00 00`, matching prior captures where
those address-window reads were not useful as the primary health signal.

FACT: userspace brightness request reaches the LCM backlight callback.
`backlight-sysfs.txt` shows `255/255`. `dmesg.txt` shows
`M6 LCM backlight ... request=170 dcs51=0xaa` and later
`M6 LCM backlight ... request=255 dcs51=0xff`. It also shows TPS65132 bias
writes returning `ret=2` for registers `0x00` and `0x01`.

INFERENCE: this capture again rejects framebuffer content, SurfaceFlinger,
HWC power policy, generic sleep/doze state, PQ, and DSI LP DCS command/read
transport as the first physical-black frontier. The next evidence-backed
frontier is the real physical visibility path: LED/backlight electrical route,
DDP PWM module mapping, panel bias/reset state after resume, or MIPI HS video
acceptance/timing/lane state.

NEXT ACTION: the built but not-yet-flashed diagnostic boot remains the correct
next physical-display test:
`/srv/forge/android/export/meizu_m6_artifacts/20260606-m6-physical-black-led-dcs-pwmguard-diag/boot-m6-physical-black-led-dcs-pwmguard-diag.img`,
sha256 `0e9ba01b34559b03026818e7c9dec02f283d77ab174c75614b9b9c95c261e78d`.
It adds LED route, DCS `0x51/0x53/0x55`, GPIO/TPS, and PWM debugfs guard
readbacks. Do not flash it without explicit human `шей` confirmation.

Rollback condition: none for this checkpoint; it is a read-only capture
summary. If a later verified capture shows physical image with the same boot
state, this physical-black conclusion can be retired.

## 2026-06-05 DSI sleep/clock diagnostic capture result

STATE / EVIDENCE CHECKPOINT, 2026-06-05: the DSI sleep/clock diagnostic boot
was flashed, readback-verified, and captured on `711HEBSR277K5`.

FACT: boot-only artifact
`/srv/forge/android/export/meizu_m6_artifacts/20260605-m6-dsi-sleep-clock-diag-boot/boot-m6-dsi-sleep-clock-diag.img`
was written to the boot partition and verified in preflash capture
`/srv/forge/android/meizu_m6/captures/20260605-203006-m6-preflash-dsi-sleep-clock-diagboot-711HEBSR277K5`;
local and remote readback sha256 both equal
`e4c317d025440efa5e04f158b255b4b127ce6e5c0848a149ccecefbbab6eabbd`.
The postboot capture is
`/srv/forge/android/meizu_m6/captures/20260605-203136-m6-postboot-dsi-sleep-clock-diagboot-711HEBSR277K5`;
its boot readback again matches `e4c317d025440efa5e04f158b255b4b127ce6e5c0848a149ccecefbbab6eabbd`.

FACT: the postboot capture was taken early enough that `boot-state.txt` still
had `sys.boot_completed=` and `bootanim=running`, but live follow-up under
`live-after-bootcompleted/` shows `sys.boot_completed=1`, `bootanim=stopped`,
`surfaceflinger=running`, and `input/window/activity` services present.
`screenrecord-720x1280.mp4` is non-empty (`6065` bytes), and the late
`screencap-keepawake.png` shows the Android lockscreen.

FACT: DSI/panel reads are healthy while the primary display is ALIVE. In
`ata-sweep-1.txt` / `mtkfb-after-ata-1-reg_dsi.txt`, DSI reads run with
`power=1 ulps=0 dsi_e/p=1/1 dig_e/p=1/1`, wait return `ret=200`, and decoded
packets. Panel responses include display ID `15 20 00`, display status
`80 03 06 00`, power mode `9c`, pixel format `07`, ID1 `15`, ID2 `20`, and
ID3 `00`. `0x2a` and `0x2b` return four bytes but still report
`00 00 00 00`, so address-window reads are not useful as the primary health
signal on this panel.

FACT: DSI read timeouts happen after userspace blanks the framebuffer. At
`74.664386`, `surfaceflinger` calls `M6 mtkfb blank: mode=4`; the primary path
then logs `suspend-begin`, `suspend-after-lcm`, `power-off-entry`, DSI clock
disable, `ALIVE -> SLEPT`, `ulps=1`, and DSI engine/digital clocks
`0/1`. ATA/DCS reads after that state show `power=0 ulps=1`, register reads
stuck at `0x30870`, `DSI Read Fail`, and sentinel `a5` data. These timeouts
are sleep-state evidence, not proof of a broken DSI transport while awake.

FACT: live screen-off evidence matched the kernel suspend: before wake,
`mAwake=false`, `mScreenOnFully=false`, backlight brightness `0`, and
`screencap-before-wake.png` was all black. After `input keyevent 224`,
brightness `180`, and `screen_off_timeout=2147483647`, the kernel logs
`SLEPT -> ALIVE`, DSI clocks return to `1/1`, backlight is `180/255`, and
`screencap-keepawake.png` shows the lockscreen.

INFERENCE: PQ is no longer the active display frontier. The panel, DSI read
transport, SurfaceFlinger, framebuffer, and backlight control are all working
when the display is awake. If the human still sees a physically black panel
while `screencap-keepawake.png` shows the lockscreen and backlight reads
`180`, the next frontier is physical video visibility/backlight wiring or a
panel video-mode issue, not DCS read timeout. If the human sees the lockscreen,
the display bring-up frontier should move to normal ROM polish and persistent
runtime defaults rather than more PQ/DSI-read patches.

Runtime keep-awake commands used after the capture:

```bash
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell 'settings put system screen_off_timeout 2147483647; svc power stayon true; input keyevent 224; settings put system screen_brightness 180'
```

FLASH TOOLING NOTE: Android userspace on this build uses toolbox `dd`; it does
not accept `conv=fsync` or `bs=4M`. Large `adb exec-in dd ...` also produced a
partial boot write. The reliable boot write path is: `adb push` boot image to
`/data/local/tmp`, verify remote sha256, remote `dd if=/data/local/tmp/... of=/dev/block/mmcblk0p21 bs=4096`,
`sync`, then host readback with `bs=4096` and trim/hash to the boot image size.

## 2026-06-05 DSI sleep/clock frontier diagnostic

PATCH HISTORY, DIAGNOSTIC, 2026-06-05: trace the exact boundary where the
display path enters sleep/ULPS and drops DSI clocks before DCS/ATA reads.

Hypothesis: the current black-panel frontier is not PQ, HWC, framebuffer
content, or media userspace. Fresh captures show Android boot complete,
SurfaceFlinger and screenrecord alive, `DISP_OPT_BYPASS_PQ=1`, and the LCM
driver selected, while every DCS sweep read returns timeout/sentinel data after
the primary display reports `State=Sleep`, `is_mipi_enterulps()` blocks
debugfs register dumps, and DSI engine/digital clock enable counts are zero.
The next capture must prove who requests blank/suspend and at which boundary
DSI power/clock state changes relative to ATA reads.

Evidence: current boot identity was verified by trimmed readback in
`/srv/forge/android/meizu_m6/captures/20260605-identity-current-boot-711HEBSR277K5/boot-identity-sha256.txt`;
the first `8863744` bytes of the boot partition match
`/srv/forge/android/export/meizu_m6_artifacts/20260605-m6-sourcebuilt-softenc-latinime-system-flash/boot-m6-dcs-read-sweep-diag.img`
sha256 `6dca836c3e854890f0ce28cb5ebb83af8e12e601144064ae7dbee70c1873fcc6`.
Fresh DCS sweep capture
`/srv/forge/android/meizu_m6/captures/20260605-192639-m6-display-dcs-sweep-current-711HEBSR277K5`
shows `DSI Read Fail: dsi wait read ready timeout`, wrapper `ret=0`, and
`read_count=0` / `a5 a5 a5 a5` sentinel data for `0x04`, `0x09`, `0x0a`,
`0x0b`, `0x0c`, `0x0d`, `0x2a`, `0x2b`, `0xda`, `0xdb`, and `0xdc`.
Live display capture
`/srv/forge/android/meizu_m6/captures/20260605-194925-m6-live-dsi-mtcmos-frontier-711HEBSR277K5`
shows `sys.boot_completed=1`, `surfaceflinger=running`,
`Service input: found`, `Service media.codec: found`, DSI engine/digital
`clk_enable_count=0`, debugfs `idlemgr disable mtcmos now, all the regs may
0x00000000`, `LCM Driver=[ili9881p_hd_dsi_txd]`, `State=Sleep`,
`RDMA0 Transfer ... 60.77 fps`, and `DISP_OPT_BYPASS_PQ Value: [1]`.

Files changed: `ddp_clkmgr.h` exposes read-only display clock count helpers.
`ddp_clkmgr.c` implements those helpers with `__clk_get_enable_count()` and
`__clk_get_prepare_count()`. `ddp_dsi.c` adds bounded `M6 DSI clkstate[...]`
markers around DCS read start/wait/timeout and DSI power on/off. `mtkfb.c`
logs `M6 mtkfb blank` at fb blank requests. `primary_display.c` adds bounded
`M6 primary state` and `M6 primary power[...]` markers around state changes,
suspend, and resume.

Why each file changed: clock counts must be read at the display-driver
boundary that owns the clocks, not inferred from stale debugfs text. `ddp_dsi.c`
owns both the DCS read timeout and DSI power transition points, so it can
correlate `s_isDsiPowerOn`, ULPS state, DSI registers, MMSYS route/mutex
registers, and clock counts. `mtkfb.c` owns the fb blank entrypoint that can
explain why the primary display becomes slept even though userspace is alive.
`primary_display.c` owns the state machine and suspend/resume sequencing that
turns display path activity into `DISP_SLEPT`. All edits are bounded log-only
diagnostics with no new waits, resets, register writes, fake-ready path, or
PQ/display bypass behavior.

Expected next marker: after flashing
`/srv/forge/android/export/meizu_m6_artifacts/20260605-m6-dsi-sleep-clock-diag-boot/boot-m6-dsi-sleep-clock-diag.img`,
the postboot capture should contain `M6 mtkfb blank`, `M6 primary state`,
`M6 primary power[...]`, and `M6 DSI clkstate[...]` lines before and after
the repeated `/d/mtkfb` `ata` sweeps. If the blank/state transition appears
before ATA, the next fix should target the earliest proven blank/suspend
request or policy. If DSI clocks are enabled during read but DCS still times
out, continue into BTA/read-ready/panel command-mode sequencing.

Rollback condition: revert this diagnostic if the verified boot image regresses
before ADB/SurfaceFlinger, if markers flood beyond their bounded counters, or
if pstore/last_kmsg proves the new read-only markers trigger a reset/panic.
Do not promote this patch to a proper fix; it is evidence only.

Verification commands:

```bash
cd /srv/forge/android/meizu_m6/kernel-meizu_M6-N-ex6-linux-3.18.140
git diff --check -- kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_clkmgr.h kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_clkmgr.c kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_dsi.c kernel-3.18/drivers/misc/mediatek/video/mt6755/primary_display.c kernel-3.18/drivers/misc/mediatek/video/mt6755/mtkfb.c BRINGUP_STATE.md
env CCACHE_DIR=/srv/forge/android/ccache make -C /srv/forge/android/meizu_m6/kernel-meizu_M6-N-ex6-linux-3.18.140/kernel-3.18 \
  O=/srv/forge/work/m6-source-kernel-manual-20260520/out \
  ARCH=arm64 \
  CROSS_COMPILE=/srv/forge/android/meizu_m6/rom-meizu_M6-lineage-cm-14.1/prebuilts/gcc/linux-x86/aarch64/aarch64-linux-android-4.9/bin/aarch64-linux-android- \
  -j8 Image.gz-dtb
cd /srv/forge/android/export/meizu_m6_artifacts/20260605-m6-dsi-sleep-clock-diag-boot
sha256sum -c SHA256SUMS
WAIT_SECONDS=21600 POLL_SECONDS=5 ./m6_wait_capture_flash_dsi_sleep_clock_diag_autoport.sh
CAP=/srv/forge/android/meizu_m6/captures/<postboot-dir>
rg -n 'M6 mtkfb blank|M6 primary power|M6 primary state|M6 DSI clkstate|M6 LCM ATA|M6 DSI wrapper read|M6 DSI core read|DSI Read Fail|idlemgr disable mtcmos|State=Sleep' "$CAP"
```

Build/artifact result: branch `work/m6-rdma0-disp-decpq-20260531` built
successfully in `/srv/forge/work/m6-source-kernel-manual-20260520/out`.
The exported boot-only diagnostic artifact is
`/srv/forge/android/export/meizu_m6_artifacts/20260605-m6-dsi-sleep-clock-diag-boot/boot-m6-dsi-sleep-clock-diag.img`,
sha256 `e4c317d025440efa5e04f158b255b4b127ce6e5c0848a149ccecefbbab6eabbd`,
size `8863744`. Kernel payload `Image.gz-dtb` sha256 is
`57e3e7c876c1794a4cdf612b2bb9bd6b246e05a57b4109e747e152e2cc5ebb3f`;
matching `System.map` sha256 is
`e04b21200794b3051053ea197ae8831806a340caa5d0d81bf1fed75003512ff2`;
matching `kernel.config` sha256 is
`bc272726035c1a2eca9422e9bc230cf54f8295648a8865a98faba046ab01619e`.
`abootimg -i` preserved page size `2048`, boot name `1552631950`, addresses
`0x40080000/0x45000000/0x44000000`, and cmdline `bootopt=64S3,32N2,64N2
androidboot.selinux=permissive binder.devices=binder,hwbinder,vndbinder`.

## 2026-06-05 Display frontier audit before source-built flash

STATE / EVIDENCE CHECKPOINT, 2026-06-05: read-only audit of the latest
verified ADB-good display capture before the source-built softenc/LatinIME
system flash.

Evidence:
- Capture:
  `/srv/forge/android/meizu_m6/captures/20260604-182046-m6-tps-bus0-reinit-711HEBSR277K5`.
- Boot artifact identity is verified: capture `boot-hashes.txt` matches
  `/srv/forge/android/export/meizu_m6_artifacts/20260604-m6-tps-bus0-dts-removefix/boot-m6-tps-bus0-dts-removefix.img`
  and readback sha256 `c88c634e6006ca59b71f48c6c870e66dca23aab9b821d0f673e2026dbd669485`.
- Runtime display userspace is alive in that capture: `sys.boot_completed=1`,
  `surfaceflinger=running`, built-in screen `720x1280`, `powerMode=2`, and
  framebuffer flips are present.
- Framebuffer/composition is not a black-buffer problem: `screencap-after.png`
  exists and SurfaceFlinger reports GLES/Mali plus `HWC_FRAMEBUFFER_TARGET`.
- Display kernel path still fails at panel/read acceptance: TPS bus0 writes
  return `ret=2`, LCM reinit ends `ret=0`, DSI BIST registers toggle
  `self_pat=1`, but ATA still logs
  `M6 LCM ATA expected=00 b4 02 1c read=00 00 00 00 ret=0`.

INFERENCE: the first still-open display frontier remains DSI DCS/BTA read
response or panel command acceptance. The latest evidence does not support
another PQ, TPS bus, SurfaceFlinger, HWC, or framebuffer-content patch before
the already-built DCS-read sweep diagnostic is flashed and captured.

Expected next marker: the active source-built watcher should flash
`/srv/forge/android/export/meizu_m6_artifacts/20260605-m6-sourcebuilt-softenc-latinime-system-flash/boot-m6-dcs-read-sweep-diag.img`
and collect a postboot capture containing `M6 LCM ATA dcs[...]`,
`M6 DSI wrapper read`, `M6 DSI core read wait`, and
`M6 DSI core read packet` lines.

Rollback condition: do not rollback this conclusion solely because the
physical panel remains black; rollback only if the fresh verified DCS sweep
capture contradicts the frontier by proving an earlier boot/display regression
or by showing the DSI/panel read boundary is already healthy.

## 2026-06-05 DCS sweep sidecar audit

STATE / READ-ONLY AUDIT, 2026-06-05: independent display sidecar reviewed the
current physical-display frontier while the source-built flash watcher was
still polling with no visible `711HEBSR277K5`.

FACT: `/srv/forge/android/export/display_bringup_refs` is not present in this
workspace, so the next display decision must use local source, `AGENTS_DISPLAY`,
this state file, and the next verified capture. FACT: the active package is
`/srv/forge/android/export/meizu_m6_artifacts/20260605-m6-sourcebuilt-softenc-latinime-system-flash`,
with boot sha256 `6dca836c3e854890f0ce28cb5ebb83af8e12e601144064ae7dbee70c1873fcc6`
and system sha256 `26ab41d792d93b95266c226449ff36d8ed8652fbf6ff321de27fc09cd1f52378`.

INFERENCE: the next capture question is whether verified boot `6dca836c...`
and system `26ab41d...` reach ADB/SurfaceFlinger and whether the DCS sweep
proves one of four states: general BTA/read transport failure, valid packet
headers with zero RX payload, selective `0x2A`/`0x2B` failure with other panel
registers nonzero, or healthy panel reads with failure moving to HS video /
lane / timing / BIST visibility.

Branching for the next postboot capture:

- Missing `M6 LCM ATA dcs[...]` and DSI wrapper/core read logs: the capture did
  not exercise `echo ata > /d/mtkfb`, or boot/runtime regressed before the
  display diagnostic.
- `DSI Read Fail`, wait `ret<=0`, ACK/error packet, or unrecognized packet:
  investigate DSI command mode, BTA, and LP read readiness.
- Valid packet headers and `read_count>0`, but all registers zero or unchanged:
  inspect DSI RX FIFO / payload copy path and panel read state.
- `0x04`, `0x0A`, or `0xDA`-`0xDC` nonzero while `0x2A`/`0x2B` stay zero:
  panel responds; focus on init page/window/address-state, not PQ/TPS.
- DCS reads healthy but physical panel remains black: move frontier to HS video
  acceptance, lane/timing, or BIST visibility; do not branch back to
  framebuffer/SF/HWC/PQ without contradictory evidence.

Commands after the source-built helper prints `POSTBOOT_CAPTURE=...`:

```bash
CAP=/srv/forge/android/meizu_m6/captures/<postboot-dir>
cat "$CAP/boot-readback-sha256.txt" "$CAP/flash-readback.txt" 2>/dev/null
rg -n 'M6 LCM ATA dcs|M6 LCM ATA expected|M6 DSI wrapper read|M6 DSI core read wait|M6 DSI core read packet|DSI Read Fail|packet_type|read_count' \
  "$CAP"/ata-dsi-markers.txt "$CAP"/display-marker-tail.txt "$CAP"/dmesg.txt 2>/dev/null
rg -n 'sys.boot_completed|init.svc.surfaceflinger|Built-in Screen|powerMode=2|isDisplayOn=1|HWC_FRAMEBUFFER_TARGET|dsi0|rdma0|ovl0' \
  "$CAP"/boot-state.txt "$CAP"/surfaceflinger.txt "$CAP"/dumpsys-display.txt "$CAP"/interrupts-display.txt 2>/dev/null
rg -n 'screenrecord|scrcpy|OMX.google.h264|SoftVideoEncoderOMXComponent|M6 softenc isolation|media.codec' \
  "$CAP"/sourcebuilt-runtime-analysis.txt "$CAP"/screenrecord-720x1280.txt "$CAP"/media-screen-marker-tail.txt 2>/dev/null
```

## 2026-06-04 OVL/M4U endpoint correlation diagnostic

PATCH HISTORY, DIAGNOSTIC, 2026-06-04: correlate OVL endpoint math with the
display M4U translation-fault bypass path.

Hypothesis: the latest display capture proves a real OVL0 layer handoff, but
the visible black screen is not yet explained by the display M4U fault alone.
The fault appears exactly at the first address after the allocated/visible OVL
layer span, and the existing M4U ISR already treats display faults within the
next 4 KiB as bypassable. The next boot must prove whether the OVL programmed
address/span and the M4U bypass event are the same endpoint event before
promoting any MVA-size, OVL pitch, or M4U behavior change.

Evidence: capture
`/srv/forge/android/meizu_m6/captures/20260604-181456-m6-tps-bus0-dts-removefix-711HEBSR277K5`
shows OVL0 using `phy=0x1200000`, 720x1280 RGBA8888, pitch 2880 bytes, and
layer size `0x384000`. The same capture later logs `M4Ufault: port=DISP_OVL0,
mva=0x1984000` and `M4Ubypass disp TF, valid mva=0x1600000, size=0x384000,
mva_end=0x1984000`. Source inspection confirms `ddp_ovl.c` writes the
non-secure OVL layer address without a size parameter, while `m4u_hw.c`
queries `fault_mva - 1`, computes `valid_mva_end = valid_mva + valid_size`,
and bypasses display TF only when `fault_mva < valid_mva_end + SZ_4K`.

Files changed: `kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_ovl.c`
extends the existing bounded M6 OVL diagnostic with `visible_span`,
`visible_last`, `next`, `pitch_span`, and `pitch_end` endpoint fields.
`kernel-3.18/drivers/misc/mediatek/m4u/mt6755/m4u_hw.c` adds a bounded
`M6 M4U disp tf bypass[...]` marker inside the already-existing display TF
bypass branch.

Why each file changed: `ddp_ovl.c` owns the OVL layer config values needed to
derive the final programmed address and the exact endpoint a display prefetch
could touch. `m4u_hw.c` owns the translation-fault evidence and can report the
faulting port, fault MVA, valid MVA range, delta, layer, write flag, and raw
fault id at the moment the driver decides to bypass the display TF. Both edits
are bounded log-only diagnostics; they add no new register writes, waits,
reset policy, route changes, or fake-success behavior.

Expected next marker: after flashing
`/srv/forge/android/export/meizu_m6_artifacts/20260604-m6-ovl-m4u-endpoint-diag/boot-m6-ovl-m4u-endpoint-diag.img`
and collecting a fresh boot, dmesg should contain paired `M6 OVL diag end[...]`
and `M6 M4U disp tf bypass[...]` lines. If the M4U `fault` equals the OVL
`next` or `pitch_end` for the active layer, treat the TF as a likely
prefetch/guard-page symptom and continue down the display pipeline
(MUTEX/RDMA/DSI/panel state) instead of changing M4U mappings. If the fault is
outside the logged OVL endpoint, investigate the layer address/size/MVA owner
before making behavior changes.

Rollback condition: revert this diagnostic if it causes no-ADB/no-boot with a
verified matching boot partition readback, if the markers flood logs past the
bounded counters, or if the next capture proves the display M4U fault is
unrelated to the OVL endpoint and earlier display hardware state is the real
frontier. Do not promote this diagnostic to a proper fix; it is evidence only.

Verification commands:

```bash
cd /srv/forge/android/meizu_m6/kernel-meizu_M6-N-ex6-linux-3.18.140
git diff --check -- kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_ovl.c kernel-3.18/drivers/misc/mediatek/m4u/mt6755/m4u_hw.c BRINGUP_STATE.md
rg -n 'M6 OVL diag end|M6 M4U disp tf bypass' kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_ovl.c kernel-3.18/drivers/misc/mediatek/m4u/mt6755/m4u_hw.c
env CCACHE_DIR=/srv/forge/android/ccache make -C /srv/forge/android/meizu_m6/kernel-meizu_M6-N-ex6-linux-3.18.140/kernel-3.18 \
  O=/srv/forge/work/m6-source-kernel-manual-20260520/out \
  ARCH=arm64 \
  CROSS_COMPILE=/srv/forge/android/meizu_m6/rom-meizu_M6-lineage-cm-14.1/prebuilts/gcc/linux-x86/aarch64/aarch64-linux-android-4.9/bin/aarch64-linux-android- \
  -j8 Image.gz-dtb
cd /srv/forge/android/export/meizu_m6_artifacts/20260604-m6-ovl-m4u-endpoint-diag
sha256sum -c SHA256SUMS
bash -n m6_wait_capture_flash_clean_runtime_diag.sh
ADB_PORT=15039 ./m6_wait_capture_flash_clean_runtime_diag.sh
```

Build/artifact result: branch `work/m6-rdma0-disp-decpq-20260531` built
successfully in `/srv/forge/work/m6-source-kernel-manual-20260520/out`.
The exported diagnostic artifact is
`/srv/forge/android/export/meizu_m6_artifacts/20260604-m6-ovl-m4u-endpoint-diag/boot-m6-ovl-m4u-endpoint-diag.img`,
sha256 `04819714adc193c603a7a9d574911b5e6bb125ea188c7a89e2f0c4627860ad1b`,
size `8863744`. Kernel payload `Image.gz-dtb` sha256 is
`4446ff5113ffd71f6d46228c346260257c5c32c05c3cb9c726385d3b65c1d8a3`;
matching `System.map` sha256 is
`03629ecab433a19c5ef62bd0ed57e2aa7c2f35e7b79f24086128ff7969c98b1e`;
matching `config` sha256 is
`bc272726035c1a2eca9422e9bc230cf54f8295648a8865a98faba046ab01619e`.
`abootimg -i` preserved page size `2048`, boot name `1552631950`, addresses
`0x40080000/0x45000000/0x44000000`, and cmdline `bootopt=64S3,32N2,64N2
androidboot.selinux=permissive binder.devices=binder,hwbinder,vndbinder`.

FACT: At artifact creation/checkpoint time, `127.0.0.1:15039` had no listener,
so this artifact is not yet flashed/readback-verified. The old watcher for the
bounded DSI artifact must be stopped before the next reverse tunnel appears,
then replaced with the helper from this artifact directory.

Recommended watcher:

```bash
tmux kill-session -t m6-dsi-diag-flash-20260605
tmux new-session -d -s m6-ovl-m4u-endpoint-flash-20260605 'cd /srv/forge/android/export/meizu_m6_artifacts/20260604-m6-ovl-m4u-endpoint-diag && ADB_PORT=15039 WAIT_SECONDS=7200 POLL_SECONDS=5 ./m6_wait_capture_flash_clean_runtime_diag.sh 2>&1 | tee /srv/forge/android/meizu_m6/captures/m6-ovl-m4u-endpoint-flash-20260605-watch.log'
```

## 2026-06-04 bounded DSI core read summary diagnostic

PATCH HISTORY, DIAGNOSTIC, 2026-06-04: add bounded core DCS-read summary
markers after the wrapper-level read-count artifact.

Hypothesis: the current display frontier is inside the DSI read/packet path,
not PQ, TPS bias, or reset sequencing. The latest ADB-good TPS/bus0 capture
shows reinit completing, TPS writes returning `ret=2` on adapter 0, init
reaching `0x11`/`0x29`, RDMA/BIST activity present, and ATA still reading
`00 00 00 00`. The safe wrapper artifact only proves the wrapper return count
and final buffer; if that shows `read_count=0` or sentinel data, the next
question is whether `DSI_dcs_read_lcm_reg_v2()` receives a read-ready wait,
which packet type is decoded, and what RX registers contain before copying
into the LCM buffer.

Evidence: current ADB-good capture
`/srv/forge/android/meizu_m6/captures/20260604-182046-m6-tps-bus0-reinit-711HEBSR277K5`
has matching boot/readback sha256
`c88c634e6006ca59b71f48c6c870e66dca23aab9b821d0f673e2026dbd669485`; its
`dmesg-focus-after.txt` logs `M6 LCM init ... tps reg0 ret=2`, `tps reg1
ret=2`, init table completion, `M6 LCM debug reinit: end ret=0`, and then
`M6 LCM ATA expected=00 b4 02 1c read=00 00 00 00 ret=0`. Display-side audit
confirmed `DISP_OPT_BYPASS_PQ=1`, so PQ is already isolated.

Files changed: `kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_dsi.c`
adds two bounded `M6 DSI core read ...` log lines for the first 32 core reads:
one immediately after the read-ready wait, and one after packet decoding or an
unrecognized packet return. It does not add `DSI_DumpRegisters()` calls, new
waits, reset-policy changes, or extra DSI transactions.

Why each file changed: `ddp_dsi.c` owns the low-level DCS read path used by
the ILI ATA/readback test. The previous heavy core diagnostic was too risky
and caused a no-ADB regression before pstore was captured. This patch keeps
the next evidence to plain state logging: wait return, INTSTA/TRIG/START/CMDQ,
RX0..RX3, mode/busy, decoded packet type, returned count, retry count, and the
first four copied bytes.

Expected next marker: after rebuilding/flashing this diagnostic boot image and
running `echo ata > /d/mtkfb`, dmesg should contain paired
`M6 DSI wrapper read ...`, `M6 DSI core read wait ...`, `M6 DSI core read
packet ...`, and `M6 LCM ATA ... read_count=...` lines. If `wait ret` is 0 or
negative, the next branch is read-ready/interrupt/command-mode state. If the
packet type is ACK/error `0x02` or unrecognized, inspect DSI command/read
sequencing. If packet type and count are valid but bytes are zero, move to
panel-side command/page/read sequencing.

Rollback condition: revert this diagnostic if the rebuilt image fails before
ADB/boot_completed, if pstore shows DSI read diagnostics triggering a reset
loop, or if the log volume disrupts normal display/runtime capture. Do not
promote it to a proper fix; it is evidence only.

Verification commands:

```bash
cd /srv/forge/android/meizu_m6/kernel-meizu_M6-N-ex6-linux-3.18.140
git diff --check -- kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_dsi.c BRINGUP_STATE.md
rg -n 'M6 DSI core read wait|M6 DSI core read packet|DSI_dcs_read_lcm_reg_v2_wrapper_DSI0' kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_dsi.c
# build exact target after current ROM build finishes:
# ./<existing kernel build command for this tree>
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell 'echo ata > /d/mtkfb; dmesg | grep -E "M6 DSI wrapper read|M6 DSI core read|M6 LCM ATA|DSI Read Fail" | tail -120'
```

Build/artifact result: branch `work/m6-rdma0-disp-decpq-20260531` built
successfully in
`/srv/forge/work/m6-source-kernel-manual-20260520/out` with:

```bash
make -C /srv/forge/android/meizu_m6/kernel-meizu_M6-N-ex6-linux-3.18.140/kernel-3.18 \
  O=/srv/forge/work/m6-source-kernel-manual-20260520/out \
  ARCH=arm64 \
  CROSS_COMPILE=/srv/forge/android/meizu_m6/rom-meizu_M6-lineage-cm-14.1/prebuilts/gcc/linux-x86/aarch64/aarch64-linux-android-4.9/bin/aarch64-linux-android- \
  -j8 Image.gz-dtb
```

The exported diagnostic artifact is
`/srv/forge/android/export/meizu_m6_artifacts/20260604-m6-dsi-core-read-bounded/boot-m6-dsi-core-read-bounded.img`,
sha256 `5f011eaea1e04fc4a4101d1a129b7acb5545ec3333a8b93311761e6928e1c4a2`,
size `8863744`. Kernel payload `Image.gz-dtb` sha256 is
`2d7c0214147502ce5fb1d0d237c798ded49433018ac6e6a7ec2fe0afaa47bdfa`;
matching `System.map` sha256 is
`8bbcfa1e4b99b13b3355e652c44599bcb34c032f54d4b837a90493acffc7941c`;
matching `config` sha256 is
`bc272726035c1a2eca9422e9bc230cf54f8295648a8865a98faba046ab01619e`;
the reused ADB-working ramdisk sha256 is
`e82c6695614132e8759b9ee96ee5b9e9efdaf8df96d1ef0c32c5dae8b5e16332`.
`build-verify.log` sha256 is
`87c5d4f75d8282bf76bb3dfbe5ed966c6185a908d25fb66ae6039062be1fca7d`.
`sha256sum -c SHA256SUMS` passed.

`abootimg -i` preserved the safe-wrapper boot geometry: page size `2048`, boot
name `1552631950`, addresses `0x40080000/0x45000000/0x44000000`, and cmdline
`bootopt=64S3,32N2,64N2 androidboot.selinux=permissive
binder.devices=binder,hwbinder,vndbinder`. Marker extraction from the final
payload confirms `M6 DSI core read wait`, `M6 DSI core read packet`,
`M6 DSI wrapper read`, `M6 LCM ATA expected`, `M6 DDP timeout`, and
`DISP_OPT_BYPASS_PQ`.

FACT: At artifact creation time, `127.0.0.1:15039` was free and
`127.0.0.1:15038` still had only the old reverse listener. The M6 serial
`711HEBSR277K5` was not visible through a valid ADB server, so the artifact is
not yet flashed/readback-verified. Flash only after a fresh reverse tunnel is
visible, and capture boot partition hash before interpreting the new DSI
markers.

Next flash/capture command once `711HEBSR277K5` is visible:

```bash
ADB_PORT=15039 /srv/forge/android/export/meizu_m6_artifacts/20260604-223750-m6-los15-runtime-clean-systemimage/m6_clean_runtime_wait_capture_flash.sh --flash
adb -H 127.0.0.1 -P 15039 -s 711HEBSR277K5 push /srv/forge/android/export/meizu_m6_artifacts/20260604-m6-dsi-core-read-bounded/boot-m6-dsi-core-read-bounded.img /dev/block/platform/mtk-msdc.0/by-name/boot
adb -H 127.0.0.1 -P 15039 -s 711HEBSR277K5 shell sync
adb -H 127.0.0.1 -P 15039 -s 711HEBSR277K5 reboot
```

FACT: A safer one-shot helper now exists at
`/srv/forge/android/export/meizu_m6_artifacts/20260604-m6-dsi-core-read-bounded/m6_wait_capture_flash_clean_runtime_diag.sh`,
sha256 `6a8cd94a4992ee841a323b274893c441f0cc09a2f73e668ef41dfed5663a63ae`.
It waits for a real listener on `15039` before using ADB, captures preflash
runtime/display evidence, writes `system-clean-no-video-le.raw.img` and
`boot-m6-dsi-core-read-bounded.img` without an intermediate reboot, verifies
readback hashes for both partitions, reboots, and captures postboot
runtime/display/ATA markers. `bash -n` passed; `WAIT_SECONDS=0` correctly
exits without flashing when `15039` has no listener. Prefer this helper over
the four manual commands above for the next device cycle.

One-shot command:

```bash
ADB_PORT=15039 /srv/forge/android/export/meizu_m6_artifacts/20260604-m6-dsi-core-read-bounded/m6_wait_capture_flash_clean_runtime_diag.sh
```

## 2026-06-04 DSI read diagnostic no-ADB rollback

FACT: Commit `e944550ed2f` is the last committed checkpoint before this DSI
read diagnostic iteration. The follow-up diagnostic artifact
`/srv/forge/android/export/meizu_m6_artifacts/20260604-m6-dsi-dcs-read-diag/boot-m6-dsi-dcs-read-diag.img`
has sha256 `2ff635df9865ed108f7da0eadb6528d2a4800b1f906cae3a8727fb378e5f2f52`.
It was flashed to `/dev/block/platform/mtk-msdc.0/by-name/boot` with
`adb push` because large `adb exec-in dd`/`cat` writes over the current tunnel
left a mismatching tail. Clean readback
`/srv/forge/android/export/meizu_m6_artifacts/20260604-m6-dsi-dcs-read-diag/boot-readback-adbpush-clean.img`
matched `2ff635df9865ed108f7da0eadb6528d2a4800b1f906cae3a8727fb378e5f2f52`.

FACT: After reboot from the verified `2ff635df...` boot image, ADB did not
return for 180 poll iterations in
`/srv/forge/android/export/meizu_m6_artifacts/20260604-m6-dsi-dcs-read-diag/reboot-wait-20260604-184517.txt`.
The follow-up watcher is
`/srv/forge/android/export/meizu_m6_artifacts/20260604-m6-dsi-dcs-read-diag/wait-capture-after-noadb-*.txt`.
No pstore/last_kmsg evidence has been captured yet, so the exact failing stage
is still unknown.

INFERENCE: The failed `2ff635df...` artifact is not safe to keep as the next
display test image until pstore proves whether the no-ADB regression is caused
by the heavy core `DSI_dcs_read_lcm_reg_v2()` register-snapshot diagnostic or
by an unrelated boot/runtime issue. The previous committed TPS/bus0 artifact
`c88c634e6006ca59b71f48c6c870e66dca23aab9b821d0f673e2026dbd669485`
had returned ADB and boot_completed before this test.

PATCH HISTORY, DIAGNOSTIC, 2026-06-04: reduce DSI read instrumentation to
wrapper-level read-count logging after the verified no-ADB regression.

Hypothesis: the broad core DCS read diagnostic in `2ff635df...` has too much
blast radius for the next boot test because it adds extra register reads and
`dsi_m6_dump_snapshot()` calls inside the low-level DCS read function. Keeping
only bounded wrapper begin/end logs plus the ILI ATA `read_count` marker should
preserve the DCS return-value evidence needed for manual `ata` tests while
removing the highest-risk diagnostic path.

Evidence: verified boot readback for `2ff635df...` matched before reboot, then
ADB did not return for 180 polls. Source inspection shows the active
`ili9881p_hd_dsi_txd` driver exposes `.compare_id` and `.ata_check`, and the
heavy diagnostic patched the shared core `DSI_dcs_read_lcm_reg_v2()` function.
The safer artifact is
`/srv/forge/android/export/meizu_m6_artifacts/20260604-m6-dsi-readcount-wrapper-safe/boot-m6-dsi-readcount-wrapper-safe.img`
sha256 `7961fab28e48b615288ad7b17348d373ab61bbca2bdaf05a34bfd80c3d69b157`;
matching `Image.gz-dtb` sha256 is
`1567c98d61cba22d883e994d71bd55d10806b671b4ae98bf290b56ae4de22e5f`,
`System.map` sha256 is
`38aa046bfce096907de3e6554103ce5c83e595629a6d0a07c963c6cc4de01d55`, and
`config` sha256 is
`bc272726035c1a2eca9422e9bc230cf54f8295648a8865a98faba046ab01619e`.

Files changed: `kernel-3.18/drivers/misc/mediatek/lcm/ili9881p_hd_dsi_txd/ili9881p_hd_dsi_txd.c`
initializes the ATA read buffer and logs `read_count`; `kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_dsi.c`
initializes a DSI read compare counter and adds bounded wrapper begin/end logs,
without extra core DSI register snapshots.

Why each file changed: the LCM file owns the manual ATA readback marker that
distinguishes zero-filled buffers from an actual zero-length read; the DSI file
owns the wrapper that can report `ret` and returned bytes without changing the
core read state machine or adding new register-dump calls.

Expected next marker: after flashing `7961fab...`, Android should return to
the prior ADB/boot_completed frontier. Manual `echo ata > /d/mtkfb` should log
`M6 DSI wrapper read begin`, `M6 DSI wrapper read end`, and `M6 LCM ATA
... read_count=...`. If the panel still returns zeros, the next diagnostic
frontier is DSI command/read response rather than TPS bias or safe reinit.

Rollback condition: if `7961fab...` also causes no-ADB with a verified boot
readback, stop testing DSI read diagnostics and restore the previous committed
`c88c634e...` TPS/bus0 artifact before adding earlier boot-stage markers or
reading pstore.

Verification commands:

```bash
cd /srv/forge/android/export/meizu_m6_artifacts/20260604-m6-dsi-readcount-wrapper-safe && sha256sum -c SHA256SUMS
(gzip -cd /srv/forge/android/export/meizu_m6_artifacts/20260604-m6-dsi-readcount-wrapper-safe/Image.gz-dtb 2>/dev/null || true) | strings | grep -E 'M6 DSI wrapper read|M6 DSI read\[|M6 LCM ATA expected'
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 push /srv/forge/android/export/meizu_m6_artifacts/20260604-m6-dsi-readcount-wrapper-safe/boot-m6-dsi-readcount-wrapper-safe.img /dev/block/platform/mtk-msdc.0/by-name/boot
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell sync
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 reboot
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell 'echo ata > /d/mtkfb; dmesg | grep -E "M6 DSI wrapper read|M6 LCM ATA|DSI Read Fail" | tail -80'
```

FACT: As of the next continuation check, `adb -H 127.0.0.1 -P 15038 devices -l`
showed no attached devices. Recovery flashable boot-only packages were prepared
under
`/srv/forge/android/export/meizu_m6_artifacts/20260604-m6-recovery-bootonly-packages/`
for the next physical recovery window:
`restore-c88c/m6-restore-c88c-tps-bus0-bootonly-20260604-unsigned.zip` sha256
`c3010370c0c4b32a1e1488e8205ac104f5e6ab65c58793ee0e483b726b950e3a`, embedded
boot sha256 `c88c634e6006ca59b71f48c6c870e66dca23aab9b821d0f673e2026dbd669485`;
and `safe-7961/m6-dsi-readcount-wrapper-safe-bootonly-20260604-unsigned.zip`
sha256 `21a45d662d0fe3f2ac425a11acbf7c725c5f0d0b93e8e5577b914fdcada2a993`,
embedded boot sha256
`7961fab28e48b615288ad7b17348d373ab61bbca2bdaf05a34bfd80c3d69b157`.
Package manifest:
`/srv/forge/android/export/meizu_m6_artifacts/20260604-m6-recovery-bootonly-packages/MANIFEST.md`.
The reusable wait/capture script
`/srv/forge/android/export/meizu_m6_artifacts/20260604-m6-recovery-bootonly-packages/m6_wait_capture_then_flash.sh`
has sha256 `e3e2541b7bc815b71c03fa9f1bdb64b4ca9152ccf50a67f282e6e4c579f9a165`;
`bash -n` passed, and `WAIT_SECONDS=0` correctly exits without flashing when
the device is absent. The script now captures display/runtime screen markers
in addition to pstore and boot readback: `logcat-tail.txt`,
`surfaceflinger.txt`, `dumpsys-display.txt`, `dumpsys-window.txt`,
`service-list.txt`, `framebuffer-backlight.txt`, `interrupts-display.txt`,
`debugfs-display.txt`, and `screencap.png`.

FACT: Signed recovery boot-only packages were added with Temurin 8 and LOS15
testkeys. Signer inputs were
`/srv/forge/android/meizu_m6/rom-lineage-15.1-meizu_m6-experimental/out/host/linux-x86/framework/signapk.jar`
sha256 `ad6f19d58421f413d1178a3a4e7f8bd684a73f2587b0a571fc2710a5eebeb259`,
`testkey.x509.pem` sha256
`a4384ba815b9499a5ce349b4e33c1755278873fe2eac150a068823f526e6dbde`, and
`testkey.pk8` sha256
`495675d32e89a149d5abe191f4e9c0e218b9068714e9b53a7c91e164a0741a23`.
Signed restore zip:
`/srv/forge/android/export/meizu_m6_artifacts/20260604-m6-recovery-bootonly-packages/restore-c88c/m6-restore-c88c-tps-bus0-bootonly-20260604-signed.zip`
sha256 `a371a6ebfa74cec7506541a204214b887ae9cb16c0a42c2de2c5e3d1ec0ab67b`;
embedded boot sha256 remains
`c88c634e6006ca59b71f48c6c870e66dca23aab9b821d0f673e2026dbd669485`.
Signed safe zip:
`/srv/forge/android/export/meizu_m6_artifacts/20260604-m6-recovery-bootonly-packages/safe-7961/m6-dsi-readcount-wrapper-safe-bootonly-20260604-signed.zip`
sha256 `34c464e65ab7b44452d08bc64f20f0f4733f152256cf75734769f5fa13300cc8`;
embedded boot sha256 remains
`7961fab28e48b615288ad7b17348d373ab61bbca2bdaf05a34bfd80c3d69b157`.
`zip -T` passed for both signed packages.

FACT: After the user reported the phone was returned, the remote ADB server at
`127.0.0.1:15038` answered `host-features`, but
`adb -H 127.0.0.1 -P 15038 devices -l` still listed no devices and
`adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 get-state` returned
`error: device '711HEBSR277K5' not found`. No new capture was possible from
that empty device list.

INFERENCE: Once `7961fab...` is tested, the next display branch depends on the
new `read_count` marker. If `read_count=0` or the buffer stays at the sentinel
`a5 a5 a5 a5`, the next patch should be a bounded DIAGNOSTIC inside
`DSI_dcs_read_lcm_reg_v2()` for the first few ATA reads only: wait return,
`MODE`, `START`, `STA`, `INTEN`, `INTSTA`, `CMDQ_SIZE`, `CMDQ0/1`, RX words,
packet type, and receive count. If `read_count>0` but data is still
`00 00 00 00`, the next branch should be a controlled multi-DCS read after
manual reinit for `0x2A`, `0x2B`, `0x0A`, `0x0B`, `0x0C`, `0x0D`, `0xDA`,
`0xDB`, and `0xDC`, because TPS bus0 and manual reinit were already verified
in `/srv/forge/android/meizu_m6/captures/20260604-182046-m6-tps-bus0-reinit-711HEBSR277K5/dmesg-focus-after.txt`.

FACT: Current non-display runtime blockers to re-check after ADB returns are
recorded in the ROM state and captures, not in this kernel tree. Evidence
entry points:
`/srv/forge/android/meizu_m6/rom-lineage-15.1-meizu_m6-experimental/BRINGUP_STATE.md`,
`/srv/forge/android/meizu_m6/docs/2026-06-01_m6_pure64_scrcpy_non_display_runtime_fixes.md`,
`/srv/forge/android/meizu_m6/captures/20260603-1130-m6-runtime-unblock-minimal-android-711HEBSR277K5/`,
and
`/srv/forge/android/meizu_m6/captures/20260604-181456-m6-tps-bus0-dts-removefix-711HEBSR277K5/`.
Sidecar audit found `boot_completed=1`, `surfaceflinger=running`,
`zygote=running`, `input` published, and storage cleanup no longer the main
blocker. Remaining non-display re-checks are `media.codec` availability for
scrcpy video, `webview_zygote32` restarting despite pure64 properties, and
RIL/`conn_launcher` crashes.

Runtime re-check commands after `711HEBSR277K5` is visible:

```bash
A='adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5'
$A shell 'getprop sys.boot_completed; getprop dev.bootcomplete; getprop ro.zygote; getprop ro.product.cpu.abilist32; getprop persist.media.treble_omx'
$A shell 'getprop init.svc.surfaceflinger; getprop init.svc.zygote; getprop init.svc.webview_zygote32; getprop init.svc.ril-daemon; getprop init.svc.conn_launcher'
$A shell 'service check input; service list | grep -E "input|window|activity|SurfaceFlinger|display|media.codec|media.player|media.extractor"'
$A shell 'dumpsys media.codec 2>&1 | head -80'
$A shell 'df -h /data /cache; du -sk /data/core /data/media /data/media/0 2>/dev/null'
$A logcat -d | grep -E 'webview_zygote32|abilist32|media.codec|Failed to initialize video/avc|rild|mtk-ril|libril|conn_launcher' | tail -200
```

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


## 2026-05-22 LOS15 source-kernel binder SG transaction boot image

FACT: Debug run `371b9f40` / capture linked to build `05e1de44-4a62-4430-aaf3-b1148d93450a` verified the flashed `binder-devices-v1` boot image by trimmed boot hash `aec3c4dfe13766964c021d3cb7a6f97333c02585bfa57c3916707ee7f8875e7d`. ADB was online on Android 8.1, `/dev/hwbinder` and `/dev/vndbinder` existed, and `Opening '/dev/hwbinder' failed` was gone.

FACT: The same fresh LOS15 capture still had `sys.boot_completed=false` and framework/HAL loops. The earliest new binder-specific failure in logcat was `binder: unknown command 1078485777`, where `1078485777 == 0x40486311 == BC_TRANSACTION_SG`, followed by HIDL service registration failures such as allocator/cas returning `-2147483648`. Later symptoms included `SurfaceFlinger: ERROR: failed to open framebuffer (Invalid argument), aborting`.

INFERENCE: The previous binder-device patch moved the frontier from missing `/dev/hwbinder`/`/dev/vndbinder` to Oreo scatter-gather binder protocol support. Android 8.1 userspace now reaches HIDL binder clients, but the 3.18 binder driver rejects `BC_TRANSACTION_SG`, so HAL/service startup cannot stabilize.

PATCH HISTORY, BOOT-UNBLOCK, 2026-05-22: add Oreo binder scatter-gather transaction support and repack with the known ADB-working LOS15 initfix-v2 ramdisk.

Hypothesis: Android 8.1 userspace is blocked because the legacy 3.18 binder driver lacks `BC_TRANSACTION_SG` / `BC_REPLY_SG` and the matching `binder_transaction_data_sg` object-buffer handling. Backporting the Oreo binder SG command surface should remove the `unknown command 1078485777` loop and let HIDL services progress to the next real framework or display blocker.

Evidence: verified prior boot image sha256 `aec3c4dfe13766964c021d3cb7a6f97333c02585bfa57c3916707ee7f8875e7d`; debug run `371b9f40` fresh runtime identity matched build `05e1de44-4a62-4430-aaf3-b1148d93450a`; logcat contained `binder: unknown command 1078485777` after hwbinder/vndbinder devices were present. Local build log `/srv/forge/android/export/meizu_m6_artifacts/20260522-los15-source-kernel-binder-sg-v1/source-kernel-binder-sg-v1-build.log` completes with `CAT arch/arm64/boot/Image.gz-dtb`.

Files changed: `kernel-3.18/drivers/staging/android/binder.c` adds SG transaction command handling, extra object-buffer allocation/copy, `BINDER_TYPE_PTR` and `BINDER_TYPE_FDA` object validation, parent fixups, FD-array translation/release, and stats/string coverage. `kernel-3.18/drivers/staging/android/uapi/binder.h` adds the Oreo UAPI constants and `struct binder_transaction_data_sg`. `BRINGUP_STATE.md` records this patch cycle. Existing dirty DDP/display files are not part of this binder fix.

Why each file changed: `binder.c` owns the kernel protocol decoder that rejected command `0x40486311`; it must understand SG transactions before userspace HIDL clients can register services. `binder.h` must expose the exact command and object types used by Android 8.1 userspace so kernel and userspace layouts match. The state file is required so the next capture can verify artifact identity before reading logs.

Expected next marker: next capture must hash/trim flashed boot to sha256 `ba85181c66d2c6540d2b982d52dee90ba4b58a26ffd54181a30395089ac5bf9c`. If the hypothesis is correct, `binder: unknown command 1078485777` disappears and allocator/cas/hwservicemanager move past the `-2147483648` registration failure. If framework then still loops, the next likely proven frontier is the existing framebuffer open `EINVAL` from SurfaceFlinger/display.

Rollback condition: If a verified capture with boot sha256 `ba85181c66d2c6540d2b982d52dee90ba4b58a26ffd54181a30395089ac5bf9c` regresses before ADB/init, revert only this SG patch and compare binder init logs. If `unknown command 1078485777` remains, reject the artifact identity or inspect the built `binder.o` command table before touching display. If the command disappears but display still fails, keep the binder patch and move to framebuffer/DDP evidence.

Artifacts: boot image `/srv/forge/android/export/meizu_m6_artifacts/20260522-los15-source-kernel-binder-sg-v1/boot-los15-source-kernel-binder-sg-v1.img` sha256 `ba85181c66d2c6540d2b982d52dee90ba4b58a26ffd54181a30395089ac5bf9c`, size `9469952`; kernel payload `/srv/forge/android/export/meizu_m6_artifacts/20260522-los15-source-kernel-binder-sg-v1/Image-binder-sg-v1.gz-dtb` sha256 `a85f3ae7636187ca65eefcd259b89d76a72ed007ad150ebe6ee720293ffd4368`; System.map sha256 `fd7d0ceef1faaba8f48c87c1f6ea9dd7f53920181e01d8c9bc6ab7623e5fb348`; config sha256 `bc272726035c1a2eca9422e9bc230cf54f8295648a8865a98faba046ab01619e`; ramdisk sha256 `51292bd1c0382528ed463e3f1771e73e687e0085982370b146ad73f9a4fbfea5`.

Verification commands:

```bash
sha256sum -c /srv/forge/android/export/meizu_m6_artifacts/20260522-los15-source-kernel-binder-sg-v1/SHA256SUMS
abootimg -i /srv/forge/android/export/meizu_m6_artifacts/20260522-los15-source-kernel-binder-sg-v1/boot-los15-source-kernel-binder-sg-v1.img
# Next capture identity:
python3 - <<'PY2'
from pathlib import Path
import hashlib
cap = Path('<next-capture>/evidence/adb/mtk/partitions/boot-partition-16m.img').read_bytes()
expected = Path('/srv/forge/android/export/meizu_m6_artifacts/20260522-los15-source-kernel-binder-sg-v1/boot-los15-source-kernel-binder-sg-v1.img').read_bytes()
print(hashlib.sha256(cap[:len(expected)]).hexdigest())
print(cap[:len(expected)] == expected)
PY2
grep -R -n -E 'binder: unknown command 1078485777|BC_TRANSACTION_SG|Unable to register allocator service|Error while registering cas service|hwservicemanager|SurfaceFlinger: ERROR: failed to open framebuffer|sys.boot_completed' <next-capture>/evidence
```


## 2026-05-22 LOS15 binder SG verified; next boot uses default gralloc framebuffer fallback

FACT: Fresh debug evidence `/home/n8n/forge-work/debug/0b13c8c6-d194-431f-a397-f852e3aae7d9/870a1516-3aa5-43fc-8c1f-504749c3085f/browser-debug-evidence-1779460071340.tar` has sha256 `fd54ea11af528e20a522284cffd366b63aa10267749f8b4357c109810d18f3e2`. The captured boot partition trimmed to the tested image matches `/srv/forge/android/export/meizu_m6_artifacts/20260522-los15-source-kernel-binder-sg-v1/boot-los15-source-kernel-binder-sg-v1.img` sha256 `ba85181c66d2c6540d2b982d52dee90ba4b58a26ffd54181a30395089ac5bf9c`.

FACT: Runtime is Android 8.1 / SDK 27 with ADB online, `init.svc.adbd=running`, `init.svc.hwservicemanager=running`, `init.svc.vndservicemanager=running`, `hwservicemanager.ready=true`, `init.svc.surfaceflinger=restarting`, and `sys.boot_completed` empty. The previous `binder: unknown command 1078485777` and HIDL allocator `-2147483648` blocker is absent from this capture.

FACT: The earliest remaining system-start blocker is SurfaceFlinger display init. Logcat shows `/vendor/lib64/hw/hwcomposer.mt6750.so` fails to load because it cannot locate symbol `_ZN7android11BufferQueue17createBufferQueueEPNS_2spINS_22IGraphicBufferProducerEEEPNS1_INS_22IGraphicBufferConsumerEEERKNS1_INS_19IGraphicBufferAllocEEE`, then SurfaceFlinger reports `hwcomposer module not found` and aborts on `ERROR: failed to open framebuffer (Invalid argument)`. Kernel fb0 exists as `mtkfb`, mode `720x1280`, `bits_per_pixel=32`, virtual size `736,3840`, but display IRQs remain near zero.

INFERENCE: The binder SG patch succeeded and should stay. The next boot-unblock should avoid the incompatible stock MTK HWC HAL and force the AOSP framebuffer/gralloc path so zygote/system_server can stop being killed by SurfaceFlinger restarts. This is a userspace HAL selection test; it is not a kernel display proper-fix.

PATCH HISTORY, BOOT-UNBLOCK, 2026-05-22: repack the verified binder-SG kernel with LOS15 initfix-v2 ramdisk forcing AOSP gralloc/default framebuffer fallback and disabling the stock MTK HWC module.

Hypothesis: Android 8.1 cannot complete boot because the stock `hwcomposer.mt6750.so` is ABI-incompatible with Oreo SurfaceFlinger and aborts before a stable display service exists. Forcing `ro.hardware.gralloc=default`, `ro.hardware.hwcomposer=none`, and `debug.sf.disable_hwc=1` in the ramdisk should bypass the bad HWC and make SurfaceFlinger use the AOSP fbdev/gralloc path. If fbdev still returns `EINVAL`, the next blocker is kernel framebuffer var/ioctl compatibility rather than HWC linkage.

Evidence: verified capture facts above; source code in `hardware/libhardware/hardware.c` checks `ro.hardware.<class>` before platform variants, so ramdisk properties can redirect module selection without rebuilding system. The new image keeps the exact binder-SG kernel payload sha256 `a85f3ae7636187ca65eefcd259b89d76a72ed007ad150ebe6ee720293ffd4368` and changes only ramdisk properties.

Files changed: no kernel source changed for this artifact-only cycle; generated ramdisk `/srv/forge/android/export/meizu_m6_artifacts/20260522-los15-source-kernel-binder-sg-gfxfb-v1/ramdisk-los15-initfix-v2-gfxfb.img`; generated boot image `/srv/forge/android/export/meizu_m6_artifacts/20260522-los15-source-kernel-binder-sg-gfxfb-v1/boot-los15-source-kernel-binder-sg-gfxfb-v1.img`; `BRINGUP_STATE.md` records this next route.

Why each file changed: ramdisk `default.prop` now selects `gralloc.default`, avoids the incompatible `hwcomposer.mt6750`, and keeps the ADB/initfix-v2 properties; boot image packages those properties with the already verified binder-SG kernel. The state file preserves the identity and next verification requirements.

Expected next marker: next capture must hash/trim flashed boot to sha256 `82ef5de38bcd5b7f52feec93d902183aec04b540ac60781f2039634454d9cf98`. If the hypothesis is correct, logcat no longer contains the `BufferQueue::createBufferQueue` HWC dlopen failure and SurfaceFlinger no longer restarts. If it still aborts with framebuffer `EINVAL`, inspect `FBIOPUT_VSCREENINFO` / `mtkfb_check_var` / fbdev var screeninfo next.

Rollback condition: If verified boot sha256 `82ef5de38bcd5b7f52feec93d902183aec04b540ac60781f2039634454d9cf98` regresses before ADB or binder/HIDL service stability, revert to `ba85181c...` and treat the ramdisk HAL selection as bad. If only HWC is bypassed but fbdev still fails, keep the binder SG source patch and move to framebuffer ioctl/kernel compatibility.

Artifacts: boot image `/srv/forge/android/export/meizu_m6_artifacts/20260522-los15-source-kernel-binder-sg-gfxfb-v1/boot-los15-source-kernel-binder-sg-gfxfb-v1.img` sha256 `82ef5de38bcd5b7f52feec93d902183aec04b540ac60781f2039634454d9cf98`, size `9467904`; ramdisk sha256 `dc95b46764acd2814c6de56099eef4c426156eb9e33789a112b2aeede7b484e7`; kernel payload sha256 `a85f3ae7636187ca65eefcd259b89d76a72ed007ad150ebe6ee720293ffd4368`; System.map sha256 `fd7d0ceef1faaba8f48c87c1f6ea9dd7f53920181e01d8c9bc6ab7623e5fb348`; config sha256 `bc272726035c1a2eca9422e9bc230cf54f8295648a8865a98faba046ab01619e`.

Verification commands:

```bash
sha256sum -c /srv/forge/android/export/meizu_m6_artifacts/20260522-los15-source-kernel-binder-sg-gfxfb-v1/SHA256SUMS
python3 - <<'PY2'
from pathlib import Path
import hashlib
cap = Path('<next-capture>/evidence/adb/mtk/partitions/boot-partition-16m.img').read_bytes()
expected = Path('/srv/forge/android/export/meizu_m6_artifacts/20260522-los15-source-kernel-binder-sg-gfxfb-v1/boot-los15-source-kernel-binder-sg-gfxfb-v1.img').read_bytes()
print(hashlib.sha256(cap[:len(expected)]).hexdigest())
print(cap[:len(expected)] == expected)
PY2
grep -R -n -E 'BufferQueue17createBufferQueue|hwcomposer.mt6750|hwcomposer module not found|failed to open framebuffer|SurfaceFlinger is starting|init.svc.surfaceflinger|sys.boot_completed|FBIOPUT|mtkfb_check_var' <next-capture>/evidence
```

## 2026-05-23 LOS15 c8531b6f display UFOE direct-route boot image

FACT: Fresh debug evidence `/home/n8n/forge-work/debug/0b13c8c6-d194-431f-a397-f852e3aae7d9/f17594a9-3fde-4bd1-a7f2-7ee4cf28b3bf/browser-debug-evidence-1779548908639.tar` reaches Android ADB with `surfaceflinger`, `bootanim`, `audioserver`, and `zygote` running, but `sys.boot_completed` is empty. The capture does not include a raw boot partition image, so exact flashed boot hash is not proven from the archive.

FACT: The runtime strongly matches build `8d802cc7` plus insecure ADB boot patch: local expected boot `/srv/forge/android/export/meizu_m6_artifacts/20260523-los15-8d802cc7-adb-insecure-bootpatch/boot-adb-insecure-8d802cc7.img` has sha256 `2d84ac1196728c25ebab8c62ccabb566c240b94f49d0fa4449a4f1f8352b546f`, and its kernel payload contains the current M6 DDP diagnostic strings.

FACT: Display is the earliest current boot frontier. `/proc/fb` reports `mtkfb` with `720x1280`, but display IRQs are near zero and dmesg repeats `CMDQ_EVENT_DISP_RDMA0_EOF` token 0 with `DISP_DL_VALID_0=0` and `DISP_DL_READY_0=0`. The decisive route dump is `M6 clean MTK diag: VALID_0=0x0 READY_0=0x0 OVL0_MOUT=0x1 COLOR0_SEL=0x1 DITHER_MOUT=0x1 RDMA0_SOUT=0x0 UFOE_SEL=0x0 SW0_RST=0xffffffff MMSYS_CG=0xfe706bfc`; CMDQ dump also shows `DISP_UFOE_MOUT_EN=0x1`, `DISP_UFOE_SEL_IN=0`, `DSI0_SEL_IN=0`, and `DISP_RDMA0_SOUT_SEL_IN=0`.

FACT: The active panel is `ili9881p_hd_dsi_txd`; its LCM driver does not set `params->dsi.ufoe_enable`, so the runtime panel config has UFOE disabled.

INFERENCE: The display path is routed through an inactive UFOE stage despite the active M6 panel not enabling UFOE compression. This leaves the DITHER/RDMA/UFOE/DSI selectors inconsistent and prevents RDMA0 EOF from ever completing.

PATCH HISTORY, BOOT-UNBLOCK/PROPER-FIX, 2026-05-23: register UFOE as route-only for diagnostics and remove UFOE from the primary display scenarios when the active DSI panel has `ufoe_enable=0`.

Hypothesis: The source kernel display path stalls because the static MT6755 DDP scenario includes `DISP_MODULE_UFOE` even when the M6 ILI9881P panel disables UFOE. Keeping a route-only UFOE driver for safe dumps while removing UFOE from the active primary scenarios should make `ddp_connect_path_l()` route RDMA0 directly to DSI0, allowing the first video frame fence to receive `CMDQ_EVENT_DISP_RDMA0_EOF` instead of timing out forever.

Evidence: capture `browser-debug-evidence-1779548908639.tar` facts above; matching System.map for the new artifact is `/srv/forge/android/export/meizu_m6_artifacts/20260523-los15-sourcekernel-ufoe-direct-route/System.map-ufoe-direct-route` sha256 `93fafebbb74f7449f2a0610e97d725ba1fac1855afe3634eb87f662dd61281b8`. Build verification produced `Image-ufoe-direct-route.gz-dtb` sha256 `f0c383dbee0cd667dfad89762b3aa0208b5be62bc2891f97dbe9b46da620c250` and the string `M6 DDP ufoe route: panel ufoe_enable=0; route RDMA0 directly to DSI0`.

Files changed: `kernel-3.18/drivers/misc/mediatek/video/mt6755/Makefile` builds `ddp_ufoe.o`; `ddp_info.c` and `ddp_info.h` register `ddp_driver_ufoe`; `ddp_ufoe.c` makes the UFOE driver route-only when present and logs route state without programming compression for disabled panels; `primary_display.c` removes `DISP_MODULE_UFOE` from `DDP_SCENARIO_PRIMARY_DISP`, `DDP_SCENARIO_PRIMARY_RDMA0_COLOR0_DISP`, `DDP_SCENARIO_PRIMARY_ALL`, and `DDP_SCENARIO_DITHER_1TO2` when the active DSI panel reports `ufoe_enable=0`; `BRINGUP_STATE.md` records this patch cycle.

Why each file changed: the Makefile/info files are required because `DISP_MODULE_UFOE` was in the active DDP scenario table but had no driver. `ddp_ufoe.c` must not start or configure compression when the panel disables it, but it must provide init/config/dump hooks so route state is observable. `primary_display.c` is the earliest point where `LCM_PARAMS` is available before CMDQ/DDP path setup, so it owns the panel-specific scenario correction.

Expected next marker: next capture from boot sha256 `6205461913e69f81ddbd5b9691f761739774db7c58a553373784b971ce58b1a9` should contain `M6 DDP ufoe route: panel ufoe_enable=0; route RDMA0 directly to DSI0`; the DDP timeout dump should either disappear or show `RDMA0_SOUT`/`DSI0_SEL` connected to RDMA0 instead of UFOE. Success criteria are no repeated `CMDQ_EVENT_DISP_RDMA0_EOF` token-0 wait, rising `rdma0`/`dsi0` IRQ counts, and boot moving to the known audio HAL blocker or to `sys.boot_completed=1`.

Rollback condition: revert this patch if a verified capture with boot sha256 `6205461913e69f81ddbd5b9691f761739774db7c58a553373784b971ce58b1a9` regresses before ADB/SurfaceFlinger, or if it logs the direct-route message but the route dump still shows `RDMA0_SOUT=0x0`, `DSI0_SEL_IN=0`, and the identical RDMA0 EOF stall with no route change.

Artifacts: boot image `/srv/forge/android/export/meizu_m6_artifacts/20260523-los15-sourcekernel-ufoe-direct-route/boot-los15-sourcekernel-ufoe-direct-route.img` sha256 `6205461913e69f81ddbd5b9691f761739774db7c58a553373784b971ce58b1a9`, size `8820736`; signed boot-only recovery zip `/srv/forge/android/export/meizu_m6_artifacts/20260523-los15-sourcekernel-ufoe-direct-route/m6-los15-sourcekernel-ufoe-direct-route-bootonly-signed.zip` sha256 `d58ad45d8ff23f850e99f84076c28c9429998fe3dcf05009cd7e548287dcfeb8`; kernel payload sha256 `f0c383dbee0cd667dfad89762b3aa0208b5be62bc2891f97dbe9b46da620c250`; System.map sha256 `93fafebbb74f7449f2a0610e97d725ba1fac1855afe3634eb87f662dd61281b8`; config sha256 `bc272726035c1a2eca9422e9bc230cf54f8295648a8865a98faba046ab01619e`; ramdisk sha256 `db778817422d17c3340b8dc06dc76ab560cc62bc085ca123945e3f01317d17e8`.

Verification commands:

```bash
sha256sum -c /srv/forge/android/export/meizu_m6_artifacts/20260523-los15-sourcekernel-ufoe-direct-route/SHA256SUMS
/usr/bin/zip -T /srv/forge/android/export/meizu_m6_artifacts/20260523-los15-sourcekernel-ufoe-direct-route/m6-los15-sourcekernel-ufoe-direct-route-bootonly-signed.zip
unzip -p /srv/forge/android/export/meizu_m6_artifacts/20260523-los15-sourcekernel-ufoe-direct-route/m6-los15-sourcekernel-ufoe-direct-route-bootonly-signed.zip boot.img | sha256sum
gzip -cd /srv/forge/android/export/meizu_m6_artifacts/20260523-los15-sourcekernel-ufoe-direct-route/Image-ufoe-direct-route.gz-dtb 2>/dev/null | strings | grep -E 'M6 DDP ufoe route|M6 clean MTK diag|M6 DDP ufoe'
python3 - <<'PY2'
from pathlib import Path
import hashlib
cap = Path('<next-capture>/evidence/adb/mtk/partitions/boot-partition-16m.img').read_bytes()
expected = Path('/srv/forge/android/export/meizu_m6_artifacts/20260523-los15-sourcekernel-ufoe-direct-route/boot-los15-sourcekernel-ufoe-direct-route.img').read_bytes()
print(hashlib.sha256(cap[:len(expected)]).hexdigest())
print(cap[:len(expected)] == expected)
PY2
grep -R -n -E 'M6 DDP ufoe route|M6 clean MTK diag|DISP_DL_VALID_0|DISP_DL_READY_0|CMDQ_EVENT_DISP_RDMA0_EOF|DISP_UFOE|DSI0_SEL|RDMA0_SOUT|Built-in Screen|sys.boot_completed|android.hardware.audio@2.0-service|Error retrieving audio properties from HAL' <next-capture>/evidence <next-capture>/mtp
```

## 2026-05-23 LOS15 source-kernel SMI LARB0 display boot image

FACT: Fresh debug evidence `/home/n8n/forge-work/debug/0b13c8c6-d194-431f-a397-f852e3aae7d9/8c743bf4-015f-485c-95e5-761d193d4a93/browser-debug-evidence-1779572584002.tar` has sha256 `8c299101f7a71f8474de18a17026874df6159729a166b27921eda9e6e330cef9` and reaches Android 8.1 with ADB online, `surfaceflinger`/`audioserver` running, `/proc/fb` reporting `mtkfb`, fb0 `720x1280`, brightness `102`, Built-in Screen visible to SurfaceFlinger, Mali EGL/GLES loaded, and HWC enabled. `sys.boot_completed` is empty.

FACT: The same capture does not include a raw boot partition image, so the flashed boot hash is not proven. The prior expected artifact remains `/srv/forge/android/export/meizu_m6_artifacts/20260523-los15-sourcekernel-ufoe-direct-route/boot-los15-sourcekernel-ufoe-direct-route.img` sha256 `6205461913e69f81ddbd5b9691f761739774db7c58a553373784b971ce58b1a9`; conclusions from this capture are identity-gated until the next capture includes a trimmed boot hash.

FACT: Display advanced beyond the UFOE route blocker: the new route dump shows `RDMA0_SOUT=0x2`, while `DISP_DL_VALID_0=0`, `DISP_DL_READY_0=0`, and repeated `CMDQ_EVENT_DISP_RDMA0_EOF` token-0 waits remain. The display IRQ counters are still near zero (`cmdq` around `1/1/1`, `rdma0` around `0/3/3`, `dsi0` around `0/1/0`), SurfaceFlinger reports VSYNC disabled, and nearby DEVAPC logs reference `SMI_LARB1`, `SMI_LARB2`, and `SMI_LARB3` addresses including `0x16010000`, `0x150012fc`, and `0x17001014/0xa8`.

INFERENCE: The direct DSI route is now applied, but the first RDMA0 frame still cannot retire because display memory path enable/order or diagnostic access around SMI/LARB is inconsistent. With CCF enabled (`# CONFIG_MTK_CLKMGR is not set`), the DDP first-call path must enable `DISP_MTCMOS_CLK`, `DISP0_SMI_COMMON`, and display `DISP0_SMI_LARB0` in order before DDP activity. CMDQ timeout dumps must also stay display-scoped so unrelated LARB1/2/3 DEVAPC reads do not hide the display LARB0 state.

PATCH HISTORY, BOOT-UNBLOCK, 2026-05-23: enable M6 display SMI clocks on the first CCF DDP clock-on path and keep timeout SMI diagnostics display-scoped.

Hypothesis: The M6 source kernel stalls at RDMA0 EOF after the UFOE route fix because the CCF DDP top-clock path leaves display MTCMOS/SMI common/LARB0 disabled on the first call, so OVL/RDMA/WDMA memory traffic cannot reach a valid display LARB0 transaction. Enabling those clocks in order on every DDP top-clock-on call should let RDMA0 reach EOF; narrowing CMDQ SMI hang detection to `SMI_DBG_DISPSYS` and dumping read-only LARB0 status on timeouts should confirm whether remaining failures are in the display LARB0 path without triggering unrelated LARB1/2/3 DEVAPC noise.

Evidence: capture `browser-debug-evidence-1779572584002.tar` facts above; display pack `/tmp/m6_1779572584002_display_pack.txt`; build output `/srv/forge/work/m6-source-kernel-manual-20260520/out/arch/arm64/boot/Image.gz-dtb` sha256 `7fc44d716c7a132b7d96fb0bf1e0401dc3c1b7d800eb353234f4a4883e65bfac`; matching System.map `/srv/forge/work/m6-source-kernel-manual-20260520/out/System.map` sha256 `1b0f5187082238210eba7602b94fb7921c939add83269ffcd0c5272f18cff559`; `.config` has `CONFIG_ARCH_MT6755=y`, `# CONFIG_MTK_CLKMGR is not set`, `CONFIG_CUSTOM_KERNEL_LCM="ili9881p_hd_dsi_txd"`, `CONFIG_MTK_SMI_EXT=y`, and `# CONFIG_MTK_SMI_VARIANT is not set`. The built kernel contains `M6 DDP SMI clk: mtcmos/common/larb0 enabled in order`, `M6 CMDQ SMI diag: display-only LARB mask`, `M6 SMI diag: LARB0_STA=...`, and the prior `M6 DDP ufoe route: panel ufoe_enable=0; route RDMA0 directly to DSI0` marker.

Files changed: `kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_path.c` enables MTCMOS, SMI common, and LARB0 in order in the CCF DDP top-clock-on path, including the first call. `kernel-3.18/drivers/misc/mediatek/cmdq/v2/cmdq_virtual.c` limits CMDQ SMI hang detection to `SMI_DBG_DISPSYS`. `kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_manager.c` adds a read-only LARB0 status/MMU/GREQ dump beside the existing RDMA EOF timeout dump. `BRINGUP_STATE.md` records this patch cycle.

Why each file changed: `ddp_path.c` owns the DDP top-clock sequence and is the earliest common point before OVL/RDMA/WDMA display traffic; the generated `.config` proves the CCF branch is active for this build. `cmdq_virtual.c` owns the SMI hang detector called from CMDQ error handling; display timeouts should not probe unrelated multimedia larbs while LARB1/2/3 DEVAPC violations are the current noise frontier. `ddp_manager.c` owns the DDP event wait timeout path where `CMDQ_EVENT_DISP_RDMA0_EOF` is observed, so it is the narrow place to snapshot display LARB0 state without changing fences or skipping waits. The state file is required by the kernel-tree contract.

Expected next marker: next capture must first prove the flashed boot by hashing the captured raw boot partition trimmed to `/srv/forge/android/export/meizu_m6_artifacts/20260523-los15-sourcekernel-smi-larb0-v1/boot-los15-sourcekernel-smi-larb0-v1.img` sha256 `822fc4b81ac068040b064b298ce2176bccb57bde5b129d151f075ca4f0bfc731`. If the hypothesis is correct, dmesg contains `M6 DDP SMI clk: mtcmos/common/larb0 enabled in order`, the RDMA EOF timeout either disappears or the `M6 SMI diag: LARB0_STA=...` line shows the real display LARB0/MMU/GREQ state, `rdma0`/`dsi0`/`cmdq` IRQ counters rise, VSYNC/flips advance, and `DISP_DL_VALID_0`/`DISP_DL_READY_0` are no longer both zero.

Rollback condition: Revert this patch if a verified capture with boot sha256 `822fc4b81ac068040b064b298ce2176bccb57bde5b129d151f075ca4f0bfc731` regresses before ADB/SurfaceFlinger, if the new SMI-clock marker appears but `MMSYS_CG`/LARB0 state proves the clock sequence is harmful, or if RDMA0 EOF behavior is identical and the LARB0 dump proves clocks were already valid before the timeout. If the only change is that unrelated LARB1/2/3 DEVAPC noise disappears while display still times out, keep the diagnostic scoping and use the LARB0 dump as the next display evidence.

Artifacts: boot image `/srv/forge/android/export/meizu_m6_artifacts/20260523-los15-sourcekernel-smi-larb0-v1/boot-los15-sourcekernel-smi-larb0-v1.img` sha256 `822fc4b81ac068040b064b298ce2176bccb57bde5b129d151f075ca4f0bfc731`, size `8820736`; signed boot-only recovery zip `/srv/forge/android/export/meizu_m6_artifacts/20260523-los15-sourcekernel-smi-larb0-v1/m6-los15-sourcekernel-smi-larb0-v1-bootonly-signed.zip` sha256 `db190991c036be23435d8960d71b8f40ba0e49e8f0c6d8a44e2c4de3e8bf49ab`; kernel payload sha256 `7fc44d716c7a132b7d96fb0bf1e0401dc3c1b7d800eb353234f4a4883e65bfac`; System.map sha256 `1b0f5187082238210eba7602b94fb7921c939add83269ffcd0c5272f18cff559`; config sha256 `bc272726035c1a2eca9422e9bc230cf54f8295648a8865a98faba046ab01619e`; vmlinux sha256 `03e2867182f013991fda03f4e2d4c34dcbafad29929a9ef58742288cffe76305`; ramdisk sha256 `db778817422d17c3340b8dc06dc76ab560cc62bc085ca123945e3f01317d17e8`.

Verification commands:

```bash
sha256sum -c /srv/forge/android/export/meizu_m6_artifacts/20260523-los15-sourcekernel-smi-larb0-v1/SHA256SUMS
/usr/bin/zip -T /srv/forge/android/export/meizu_m6_artifacts/20260523-los15-sourcekernel-smi-larb0-v1/m6-los15-sourcekernel-smi-larb0-v1-bootonly-signed.zip
unzip -p /srv/forge/android/export/meizu_m6_artifacts/20260523-los15-sourcekernel-smi-larb0-v1/m6-los15-sourcekernel-smi-larb0-v1-bootonly-signed.zip boot.img | sha256sum
gzip -cd /srv/forge/android/export/meizu_m6_artifacts/20260523-los15-sourcekernel-smi-larb0-v1/Image-smi-larb0-v1.gz-dtb 2>/dev/null | strings | grep -E 'M6 DDP SMI clk|M6 CMDQ SMI diag|M6 SMI diag|M6 DDP ufoe route'
python3 - <<'PY2'
from pathlib import Path
import hashlib
cap = Path('<next-capture>/evidence/adb/mtk/partitions/boot-partition-16m.img').read_bytes()
expected = Path('/srv/forge/android/export/meizu_m6_artifacts/20260523-los15-sourcekernel-smi-larb0-v1/boot-los15-sourcekernel-smi-larb0-v1.img').read_bytes()
print(hashlib.sha256(cap[:len(expected)]).hexdigest())
print(cap[:len(expected)] == expected)
PY2
grep -R -n -E 'M6 DDP SMI clk|M6 CMDQ SMI diag|M6 SMI diag|DISP_DL_VALID_0|DISP_DL_READY_0|CMDQ_EVENT_DISP_RDMA0_EOF|DEVAPC.*SMI_LARB|RDMA0_SOUT|DSI0_SEL|Built-in Screen|VSYNC|sys.boot_completed|AudioFlinger::RecordThread::readInputParameters_l' <next-capture>/evidence <next-capture>/mtp
```


## 2026-05-24 LOS15 source-kernel M4U/OVL0_MOUT display boot image

FACT: Fresh capture `/tmp/m6_71799c1a_latest` was collected at `2026-05-24T06:18:44.095Z` and reaches Android 8.1 ADB with `surfaceflinger`, `bootanim`, `audioserver`, `zygote`, and `hwservicemanager` running, but `sys.boot_completed` is empty. Runtime kernel identity is `Linux version 3.18.140 ... #18 SMP PREEMPT Sun May 24 05:19:37 UTC 2026`, `ro.lineage.version=15.1-20260524-UNOFFICIAL-meizu_m6`, `ro.adb.secure=0`, and `ro.debuggable=1`.

FACT: The same capture does not prove raw boot identity: `/tmp/m6_71799c1a_latest/evidence/adb/mtk/partitions/boot-hash.txt` contains `sha256sum: /dev/block/mmcblk0p21: Permission denied`, `md5sum: /dev/block/mmcblk0p21: Permission denied`, and `no_hash_tool`. The next capture must include root/recovery raw boot dump or the captured boot partition trimmed to the expected image length.

FACT: Display is registered in userspace but hardware scanout is still stalled. `surfaceflinger.txt` shows `Built-in Screen` `720x1280`, `flips=6`, `powerMode=2`, HWC present/enabled, Mali EGL/GLES loaded, and `VSYNC state: disabled`. `/proc/fb` reports `0 mtkfb`; fb0 is `720x1280`, bpp `32`, stride `2944`; backlight brightness is `102/255`. Display IRQs remain near zero: `mtk_cmdq 0/0`, `ovl0 0/1`, `rdma0 3/1`, `dsi0 0/0`.

FACT: Dmesg in the same capture repeats `CMDQ_EVENT_DISP_RDMA0_EOF` token 0 and `wait VSYNC timeout on scenario primary_disp`. The read-only display dump shows `VALID_0=0x0 READY_0=0x0 OVL0_MOUT=0x1 COLOR0_SEL=0x1 DITHER_MOUT=0x1 RDMA0_SOUT=0x2 UFOE_SEL=0x0 SW0_RST=0xffffffff MMSYS_CG=0xfe706bfc` and `LARB0_STA=0x0 LARB0_MMU=0x0/0x0/0x0/0x0 LARB0_GREQ=0x0`. CMDQ dump confirms `DSI0_SEL_IN=0x00000001`, `DISP_RDMA0_SOUT_SEL_IN=0x00000002`, `DISP_DL_VALID_0=0`, and `DISP_DL_READY_0=0`.

INFERENCE: The UFOE direct-route fix is active, but `MMSYS_CG=0xfe706bfc` still leaves the OVL0 MOUT gate bit disabled while the path depends on `OVL0_MOUT=0x1`. The CCF branch is active (`# CONFIG_MTK_CLKMGR is not set`), so the display driver must request and enable the exact M4U/dispsys SMI and OVL0_MOUT clock handles exposed by `clk-mt6755.c`/DTS, not only the coarse SMI common/LARB0 handles.

PATCH HISTORY, BOOT-UNBLOCK, 2026-05-24: enable the exact M6 display SMI M4U/dispsys clock handles and OVL0 MOUT gate in the CCF display path.

Hypothesis: the source kernel reaches SurfaceFlinger/HWC but RDMA0 never produces EOF because the path route is programmed while OVL0_MOUT and the display-specific SMI child gates are not requested/enabled through CCF. Adding the exact DT clock names and enabling SMI common M4U/dispsys, LARB0 M4U/dispsys, and OVL0_MOUT before display traffic should let the OVL0->COLOR0->DITHER->RDMA0->DSI0 route produce valid/ready data and real RDMA0/DSI0 IRQs.

Evidence: capture `/tmp/m6_71799c1a_latest` facts above; generated `.config` has `CONFIG_ARCH_MT6755=y`, `CONFIG_CUSTOM_KERNEL_LCM="ili9881p_hd_dsi_txd"`, `# CONFIG_MTK_CLKMGR is not set`, `CONFIG_MTK_SMI_EXT=y`, and `# CONFIG_MTK_SMI_VARIANT is not set`. Build log `/srv/forge/android/export/meizu_m6_artifacts/20260524-los15-sourcekernel-m4u-ovl0mout-v1/source-kernel-m4u-ovl0mout-v1-build.log` completes `CAT arch/arm64/boot/Image.gz-dtb`; the built kernel contains `M6 DDP SMI clk: mtcmos/common+m4u+dispsys/larb0+m4u+dispsys/ovl0_mout enabled in order` plus the existing DDP route/SMI diagnostic strings.

Files changed: `kernel-3.18/arch/arm64/boot/dts/mt6755.dtsi` adds the missing display SMI M4U/dispsys and OVL0_MOUT clocks to the display node. `kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_clkmgr.h` adds clock IDs for the new handles. `kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_drv.c` keeps `disp_clk_name[]` aligned with the enum/DTS order and prepares the added SMI handles at probe. `kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_path.c` enables/disables SMI common, SMI LARB0, their M4U/dispsys child handles, and OVL0_MOUT in order. `kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_ovl.c` enables OVL0_MOUT with OVL0 and disables it before OVL0 shutdown.

Why each file changed: the DTS/enum/name table are one contract for `devm_clk_get()`; changing only one would shift indexes or leave a CCF handle unresolved. `ddp_path.c` owns the common top-clock sequence before OVL/RDMA/CMDQ display traffic. `ddp_ovl.c` owns the OVL0 module clock boundary and keeps the MOUT gate refcounted with the producer module. No fake-ready, fence-skip, GED, or backlight wait bypass is part of this patch.

Expected next marker: next capture first proves the flashed boot by hashing raw boot trimmed to `/srv/forge/android/export/meizu_m6_artifacts/20260524-los15-sourcekernel-m4u-ovl0mout-v1/boot-los15-sourcekernel-m4u-ovl0mout-v1.img` sha256 `96c1a927136a56c58a73c824305243df9c34a3ae5b019501f2f704e7283fd734`. If the hypothesis is correct, dmesg contains the new `common+m4u+dispsys/larb0+m4u+dispsys/ovl0_mout` marker, `MMSYS_CG` has OVL0_MOUT bit 25 cleared, `DISP_DL_VALID_0`/`DISP_DL_READY_0` no longer both stay zero, `CMDQ_EVENT_DISP_RDMA0_EOF` token-0 waits stop repeating, display IRQ counters rise, VSYNC/flips advance, and the boot moves to the known audioserver capture-advertising crash or to `sys.boot_completed=1`.

Rollback condition: revert this patch if a verified capture with boot sha256 `96c1a927136a56c58a73c824305243df9c34a3ae5b019501f2f704e7283fd734` regresses before ADB/SurfaceFlinger, if the new marker appears but OVL0_MOUT bit 25 is still gated due unresolved CCF handles, or if RDMA0 EOF behavior is identical and the route dump proves OVL0_MOUT and SMI child gates are already valid before timeout.

Artifacts: boot image `/srv/forge/android/export/meizu_m6_artifacts/20260524-los15-sourcekernel-m4u-ovl0mout-v1/boot-los15-sourcekernel-m4u-ovl0mout-v1.img` sha256 `96c1a927136a56c58a73c824305243df9c34a3ae5b019501f2f704e7283fd734`, size `8820736`; signed boot-only recovery zip `/srv/forge/android/export/meizu_m6_artifacts/20260524-los15-sourcekernel-m4u-ovl0mout-v1/m6-los15-sourcekernel-m4u-ovl0mout-v1-bootonly-signed.zip` sha256 `da538333b8bc8b6f4499a92274e4dae91a7e1f9b0a3b68c35c7dc7320c5f6491`; kernel payload sha256 `9388585af48d7b3a084e0a3b6ae29fc4c5c61572a86cee67fa8ab7ee30251fde`; System.map sha256 `8c5fe80276455d2d193e1cb137a2abf9859c01c4f0a404d57c1473f532e35488`; config sha256 `bc272726035c1a2eca9422e9bc230cf54f8295648a8865a98faba046ab01619e`; vmlinux sha256 `fba181ac9f9561e729e55c6dc890fa3e720cae6a1b235be0bd02e4d40548e4f3`; ramdisk sha256 `8069edc1162af5a9dd3fbebe4428008d91cce41dbaaf3ebdc9eabdf1d8adb6b6`.

Verification commands:

```bash
ART=/srv/forge/android/export/meizu_m6_artifacts/20260524-los15-sourcekernel-m4u-ovl0mout-v1
(cd "$ART" && sha256sum -c SHA256SUMS && sha256sum -c ZIP_SHA256SUMS)
/usr/bin/zip -T "$ART/m6-los15-sourcekernel-m4u-ovl0mout-v1-bootonly-signed.zip"
unzip -p "$ART/m6-los15-sourcekernel-m4u-ovl0mout-v1-bootonly-signed.zip" boot.img | sha256sum
gzip -cd "$ART/Image-m4u-ovl0mout-v1.gz-dtb" 2>/dev/null | strings | grep -E 'M6 DDP SMI clk|M6 clean MTK diag|M6 SMI diag|M6 DDP ufoe route|ili9881p_hd_dsi_txd'
python3 - <<'PY2'
from pathlib import Path
import hashlib
cap = Path('<next-capture>/evidence/adb/mtk/partitions/boot-partition-16m.img').read_bytes()
expected = Path('/srv/forge/android/export/meizu_m6_artifacts/20260524-los15-sourcekernel-m4u-ovl0mout-v1/boot-los15-sourcekernel-m4u-ovl0mout-v1.img').read_bytes()
print(hashlib.sha256(cap[:len(expected)]).hexdigest())
print(cap[:len(expected)] == expected)
PY2
grep -R -n -E 'M6 DDP SMI clk|M6 clean MTK diag|M6 SMI diag|MMSYS_CG|DISP_DL_VALID_0|DISP_DL_READY_0|CMDQ_EVENT_DISP_RDMA0_EOF|RDMA0_SOUT|DSI0_SEL|Built-in Screen|VSYNC|flips=|sys.boot_completed|AudioFlinger::RecordThread::readInputParameters_l' <next-capture>/evidence <next-capture>/mtp
```

## 2026-05-25 LOS15 3b345ff9 closeall combined debug artifact

FACT: The current combined debug artifact was built from `/srv/forge/android/rom-lineage-15.1-meizu_m6-experimental` using the existing output tree `out-m6-71799c1a-closeall`, not a new clean out directory. The final flashable zip is `/srv/forge/android/rom-lineage-15.1-meizu_m6-experimental/out-m6-71799c1a-closeall/target/product/meizu_m6/lineage-15.1-20260525-UNOFFICIAL-meizu_m6.zip` sha256 `7b8bb69383f4415ea028748ffef80cbfc7b6f8cb580cd5873d9cd6fd6db5d8fd`. Export bundle is `/srv/forge/android/export/meizu_m6_artifacts/20260525-los15-3b345ff9-closeall-1650dfcaf3` and `sha256sum -c SHA256SUMS` plus `zip -T` pass.

FACT: Artifact identity files: boot image `boot-3b345ff9-closeall.img` sha256 `a0f4e75736155ef62458ec45e9032164b1dcd311e74459e6d1d9356dfe89a08e`; kernel payload `Image-3b345ff9-closeall.gz-dtb` sha256 `08ab81a0376fa9943af4a6fb628bf21e3d90ceed5cb29c0df2c1a6052cc00dbd`; `System.map-3b345ff9-closeall` sha256 `b6628dd3054df8f5b9d113fc5950035faacdad9ed39372f18b95d64285340c72`; kernel config sha256 `bc272726035c1a2eca9422e9bc230cf54f8295648a8865a98faba046ab01619e`; target_files `lineage_meizu_m6-target_files-1650dfcaf3.zip` sha256 `17741e4788fb0b674907c39c8e9dee44d42f853f3bf1f7c0a9c9b221d71227b4`.

FACT: Final OTA updater-script writes nested MTK paths again: `/dev/block/platform/mtk-msdc.0/11230000.msdc0/by-name/system` for mount/update and `/dev/block/platform/mtk-msdc.0/11230000.msdc0/by-name/boot` for boot extraction. `BOOT/RAMDISK/default.prop` has `ro.secure=0`, `ro.debuggable=1`, and `ro.adb.secure=0`. Target-files ramdisk creates `/dev/block/platform/mtk-msdc.0/by-name` as a legacy alias to the nested path without replacing the real nested path. Target-files listing has no device sensors HAL service/impl, no `sensors.mt6750`, no `android.hardware.sensor.*` permission XML, and no soundtrigger service/impl entries; framework soundtrigger libraries remain as platform libraries.

FACT: The previous `71799c1a` capture still did not prove raw flashed boot identity because boot hash read from `/dev/block/mmcblk0p21` returned `Permission denied`. Runtime nonetheless showed the live display frontier: `BootAnimation`, SurfaceFlinger built-in screen `720x1280`, HWC/Mali active, backlight `102`, but `VSYNC disabled`, flips around 6, display IRQs near zero, and repeated `CMDQ_EVENT_DISP_RDMA0_EOF` token 0 with `DISP_DL_VALID_0=0`, `DISP_DL_READY_0=0`, and `MMSYS_CG=0xfe706bfc`.

PATCH HISTORY, DIAGNOSTIC/BOOT-UNBLOCK, 2026-05-25: combined M6 closeall artifact for display-first runtime capture.

Hypothesis: The current source kernel is no longer blocked by a missing fb device or HWC load; it is blocked in the first real primary display frame path where RDMA0/MUTEX0 never reach EOF. The next artifact must avoid fake EOF signaling and log the exact DDP/CMDQ/RDMA/OVL/DSI state before and after the first real wait while unrelated first-boot loops are reduced enough to keep capture quality usable.

Evidence: The `71799c1a` capture facts above; final built kernel strings contain `M6 DDP timeout[...]` and `M6 video CMDQ: keep real RDMA0/MUTEX0 frame-done waits; no EOF token seeding`. Final built kernel strings do not contain the old `skip pre-first-config` boot-unblock marker. The final ROM payload verification facts above prove installer, debug ADB, and sensors/soundtrigger isolation are present in the artifact to be flashed.

Files changed: `kernel-3.18/drivers/misc/mediatek/video/mt6755/primary_display.c` removes first-config frame-done wait skipping and EOF token seeding, holds present fences until real VSYNC, and keeps backlight commands behind real RDMA0 EOF waits. `kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_manager.c` adds early and timeout read-only dumps for route, MMSYS gates, mutex, RDMA0 memory/counters/FIFO, OVL0 layer config/address/size/pitch, SMI/LARB0, and DSI0 core/PHY debug state. `kernel-3.18/arch/arm64/boot/dts/meizu_m6.dts` disables duplicate OF-created I2C clients for gsensor, bq24157, and msensor that were colliding with legacy MTK board registration. `kernel-3.18/drivers/input/touchscreen/mediatek/ft5x0x/ft5x0x_driver.c` and `kernel-3.18/drivers/input/touchscreen/mediatek/mtk_tpd.c` propagate `-EPROBE_DEFER` for `vtouch` instead of continuing after deferred regulator setup. ROM `init.mt6755.rc` restores nested by-name usage and legacy aliases, disables `msensord` first-boot start, and keeps insecure ADB. ROM product/vendor makefiles and the M6 handheld core permissions stop advertising broken sensors/soundtrigger HAL endpoints. ROM `system.prop` no longer publishes `ro.hardware.sensors=mt6750`.

Why each file changed: `primary_display.c` and `ddp_manager.c` are on the proven display frontier and either remove fake progress or add read-only state needed for the next capture. The OVL/RDMA/DSI additions are observation-only and target the current `DISP_DL_VALID_0=0`, `DISP_DL_READY_0=0`, near-zero IRQ frontier. The DTS/I2C/touch edits address capture-proven I2C transfer failures and deferred power setup without stubbing drivers. The ROM init and product edits address the user's proven install-path regression and the first-boot runtime loops that were hiding display/audio progress; they are boot-unblock/isolation changes, not claims that sensors or soundtrigger are fixed.

Expected next marker: The next capture must include a raw boot partition dump or recovery/root hash matching boot sha256 `a0f4e75736155ef62458ec45e9032164b1dcd311e74459e6d1d9356dfe89a08e`. If display advances, `CMDQ_EVENT_DISP_RDMA0_EOF` token-0 waits stop repeating, `rdma0`/`dsi0`/`cmdq` IRQ counts rise, SurfaceFlinger VSYNC enables, flips rise, and the panel becomes visible. If it still stalls, dmesg should contain the new `M6 DDP timeout[...]` dumps showing whether RDMA0 starts, OVL0 produces pixels, mutex config is armed, DSI0 starts, and which MMSYS gates remain set. Runtime should not show sensors/soundtrigger restart loops from removed HAL advertising, and install must not fail on missing non-nested by-name paths.

Rollback condition: Revert the display behavior part if a verified boot hash regresses before ADB/SurfaceFlinger or if real EOF waits disappear from logs. Revert the expanded read-only dump only if it makes logs unusably noisy or causes a register-access fault. Revert the sensors/soundtrigger isolation only after real kernel input/IIO nodes, HAL domains, and service registration are proven. Revert the installer/by-name change only if recovery proves the device lacks the nested `11230000.msdc0/by-name` path, which conflicts with all current runtime captures.

Verification commands:

```bash
ART=/srv/forge/android/export/meizu_m6_artifacts/20260525-los15-3b345ff9-closeall-1650dfcaf3
(cd "$ART" && sha256sum -c SHA256SUMS)
/usr/bin/zip -T "$ART/lineage-15.1-20260525-UNOFFICIAL-meizu_m6.zip"
unzip -p "$ART/lineage-15.1-20260525-UNOFFICIAL-meizu_m6.zip" META-INF/com/google/android/updater-script | grep -n '11230000.msdc0/by-name'
unzip -l "$ART/lineage_meizu_m6-target_files-1650dfcaf3.zip" | grep -Ei 'sensors\.mt6750|android\.hardware\.sensors@1\.0-service\.mtk|android\.hardware\.sensors@1\.0-impl\.mtk|android\.hardware\.sensor\.|android\.hardware\.soundtrigger@2\.0-impl|soundtrigger@2\.0-service|sound_trigger' || true
gzip -cd "$ART/Image-3b345ff9-closeall.gz-dtb" 2>/dev/null | strings | grep -E 'M6 DDP timeout|M6 video CMDQ|M6 DDP SMI clk|ili9881p_hd_dsi_txd'
# Next capture identity gate:
python3 - <<'PY2'
from pathlib import Path
import hashlib
cap = Path('<next-capture>/evidence/adb/mtk/partitions/boot-partition-16m.img').read_bytes()
expected = Path('/srv/forge/android/export/meizu_m6_artifacts/20260525-los15-3b345ff9-closeall-1650dfcaf3/boot-3b345ff9-closeall.img').read_bytes()
print(hashlib.sha256(cap[:len(expected)]).hexdigest())
print(cap[:len(expected)] == expected)
PY2
grep -R -n -E 'M6 DDP timeout|M6 video CMDQ|CMDQ_EVENT_DISP_RDMA0_EOF|DISP_DL_VALID|DISP_DL_READY|MMSYS_CG|MEM_CON|ovl0 ROI|PHY_LCCON|RDMA0_SOUT|DSI0_SEL|Built-in Screen|VSYNC|flips=|sys.boot_completed|msensord|soundtrigger|i2c.*xfer fail' <next-capture>/evidence <next-capture>/mtp
```

## 2026-05-25 9196ea7c display prep without rebuild

FACT: The latest ROM debug capture `9196ea7c-7dfc-4768-b470-99dcc1411bdc` verified the current LOS15 boot image and shows SurfaceFlinger/HWC/fb0 alive but physical scanout stalled: `surfaceflinger.txt` reports Built-in Screen `720x1280`, `flips=7`, `powerMode=2`, `isDisplayOn=1`, `VSYNC state: disabled`, and only BootAnimation; `interrupts_focus.txt` has near-zero `mtk_cmdq`, `ovl0`, `rdma0`, and zero `dsi0`; logcat repeats `GED Frame didn't finished in 1000 ms` and `timeline-primary`/`disp-S10000-L0-3` fence waits.

FACT: The user corrected the workflow: do not start with a manual rebuild, and display must be handled. No build was launched for this update.

PATCH HISTORY, DIAGNOSTIC, 2026-05-25: expand M6 display timeout dump and remove pre-frame backlight bypass from the planned source state.

Hypothesis: the current display frontier is below SurfaceFlinger/HWC and above or inside the first real OVL0/RDMA0/DSI0 frame path. The next useful capture must tell whether OVL0 has enabled layers and valid fetch addresses, whether RDMA0 is in memory/direct mode and counting pixels, whether MUTEX0/SOF is armed, whether DSI0 has started/PHY state, and whether SMI/LARB0 is blocking display fetch. A pre-frame direct backlight command is not needed for this question and risks another behavior variable.

Evidence: `9196ea7c` display facts above; prior `71799c1a` facts show the same frontier with `DISP_DL_VALID_0=0`, `DISP_DL_READY_0=0`, and `MMSYS_CG=0xfe706bfc`. Current display guardrail requires hardware-state inspection, not another CMDQ wait/fence/fake-ready patch.

Files changed: `ddp_manager.c` now logs RDMA0 `MEM_CON`, memory start, pitch, target line, FIFO config; OVL0 ROI/datapath and L0-L3 config/size/address/pitch; SMI LARB0 status/MMU/GREQ; and DSI0 PHY/debug registers in the existing first-wait/timeout dump. `primary_display.c` keeps real RDMA0/MUTEX0 waits and present-fence hold, but removes the pre-frame direct-DSI backlight bypass so backlight remains behind real RDMA0 EOF.

Why each file changed: `ddp_manager.c` owns the proven wait-timeout observation point and can read hardware state without mutating registers. `primary_display.c` owned prior fake-ready/bypass behavior; this update keeps the no-fake-EOF stance while avoiding an extra pre-frame DSI command path.

Expected next marker: next verified capture contains `M6 DDP timeout[...]` lines with `MEM_CON`, `MEM_START`, `ovl0 ROI`, `L0/L1/L2/L3`, `LARB0_STA`, and `PHY_LCCON`. The dump should narrow the stall to OVL fetch/layer config, RDMA start/counters/mode, MUTEX/SOF, DSI start/PHY, SMI/LARB, or clock/reset state.

Rollback condition: revert this diagnostic if a verified next artifact regresses before ADB/SurfaceFlinger or if the added read-only register accesses fault. Revisit present-fence hold separately if userspace stops producing useful captures, but do not restore fake fence release as a fix.

Verification commands: `git diff --check -- kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_manager.c kernel-3.18/drivers/misc/mediatek/video/mt6755/primary_display.c BRINGUP_STATE.md`; after rebuild/capture, grep `M6 DDP timeout|MEM_CON|MEM_START|ovl0 ROI|LARB0_STA|PHY_LCCON|CMDQ_EVENT_DISP_RDMA0_EOF|Built-in Screen|VSYNC|flips=` in the verified capture.

## 2026-05-26 LOS15 018b10cf / debug 1779747393021 identity-display-audio frontier

FACT: Build `018b10cf-7b13-46a5-b032-df2b1176ffee` is M6 recipe `7fad8270-05be-4798-ae73-2ca46b7ede7c`, device `0b13c8c6-d194-431f-a397-f852e3aae7d9`, target `rom-meizu_M6-lineage-15.1-stockkernel-experimental`, status `success/DONE`. ROM zip SHA256 is `a989497d7124dbb9837138279e1549144ee01344b8b36c249f17f285d4be92a2`; debug capture archive `browser-debug-evidence-1779747393021.tar` SHA256 is `8590fe40039b0e2232d16bf8d0530ecf4c71eff2428ca1ad8c6540f485d35ed7`.

FACT: Capture path is `/home/n8n/forge-work/m6-1779747393021/intake`. Runtime identity is Meizu M6: `ro.product.device=meizu_m6`, `ro.product.name=lineage_meizu_m6`, fingerprint `meizu/lineage_meizu_m6/meizu_m6:8.1.0/OPM7.181205.001/ed271c6377:eng/test-keys`, boot mode `normal`, and kernel `Linux version 3.18.140 (root@f90a8e173c9c) ... #22 SMP PREEMPT Mon May 25 17:34:59 UTC 2026`.

FACT: Boot artifact identity is matched by trimmed Android boot image, not by full padded partition. Raw `/dev/block/mmcblk0p21` SHA256 is `da77b5b0018ed03bde00b636f195f56e5f5f0d3e9138a9105717f5c947c0c3bd`; trimmed Android boot SHA256 is `c628110ea0d691ba44b7c5cfb3f7592d23a9cc4ab9ee3aec059bc018e9322166`, matching local `/tmp/m6_out_probe/boot.img` and current ROM out `out/target/product/meizu_m6/boot.img`.

FACT: Display has advanced beyond the earlier no-fb/HWC frontier. `/proc/fb` is `0 mtkfb`; fb0 name is `mtkfb`, mode `U:720x1280p-0`, bpp `32`, virtual `736,3840`, stride `2944`; SurfaceFlinger reports Built-in Screen `720x1280`, `flips=7`, `powerMode=2`, `isDisplayOn=1`, HWC present/enabled, Mali-T860 EGL/GLES, BootAnimation layer, and framebuffer target. VSYNC is still disabled, so display scanout/jank remains a follow-up, but this capture's first crash loop is not missing framebuffer registration.

FACT: Current primary runtime blocker in this capture is the audio speech HAL loop. `logcat_crash.txt` repeatedly shows `/system/vendor/lib/hw/audio.primary.mt6750.so` frames `SpeechParamParser::InitSpeechNetwork()+376`, `SpeechDriverLAD`, `SpeechDriverFactory`, `AudioALSASpeechPhoneCallController`, `AudioALSAHardware`, and `createAudioHardware`, followed by `Fatal signal 11` in `android.hardware.audio@2.0-service` and `Abort message: 'HAL server crashed, need to restart'` in `audioserver`. `/proc/asound/cards` reports `0 [mtsndcard]: mt-snd-card`, so this is not the old missing-ALSA-card frontier.

INFERENCE: The next M6 patch cycle should move from display bring-up to a single audio speech contract bundle: vendor `audio_param`/NVRAM contents and permissions, `/dev/ccci_aud`/modem readiness, audio policy/device XML, and `audio.primary.mt6750.so` dependency/ABI coherence. Do not patch display in the same cycle unless a fresh capture proves display has become the earliest blocker again.

PATCH HISTORY, PRODUCT-FIX, 2026-05-26: Build Station classifier and triage now recognize M6 `SpeechParamParser::InitSpeechNetwork` as `android_audio_vendor_speech_network_crash` and route it to `vendor_blobs`. Tests `tests/test_log_classifier.py tests/test_log_triage.py` pass `132 passed`. This is product routing only; no kernel behavior changed in this state entry.

Expected next marker: next Build Station triage of `1779747393021` or equivalent M6 capture should select `android_audio_vendor_speech_network_crash`, `component=android.audio`, `patch_root=vendor_blobs`; a real audio patch should stop repeated `SpeechParamParser::InitSpeechNetwork` SIGSEGV and either publish audio policy normally or expose the next concrete speech/CCCI/audio_param error.

Rollback condition: revert the classifier/product routing only if a verified M6 capture with matching boot identity lacks the speech-network crash and instead has an earlier kernel display/init/storage blocker; do not restore default-HAL masking as a clean fix.

Verification commands:

```bash
cd /home/n8n/build-station/apps/api
uv run pytest tests/test_log_classifier.py tests/test_log_triage.py -q
uv run python - <<'PY2'
from pathlib import Path
from app.services.log_triage import triage_log
p = Path('/home/n8n/forge-work/m6-1779747393021/intake/evidence/adb/logcat_crash.txt')
pack = triage_log(p.read_text(errors='ignore'), source_kind='logcat')
print(pack['frontier'])
print(pack['issues'][0]['error_type'], pack['issues'][0]['component'], pack['issues'][0]['patch_root'])
PY2
grep -n -E 'ro.product.device|ro.product.name|ro.build.fingerprint|ro.bootmode' /home/n8n/forge-work/m6-1779747393021/intake/evidence/adb/getprop.txt
grep -n -E 'Built-in Screen|powerMode=2|isDisplayOn=1|BootAnimation|VSYNC state|HWC|Mali-T860' /home/n8n/forge-work/m6-1779747393021/intake/evidence/adb/surfaceflinger.txt
grep -n -E 'SpeechParamParser|SpeechDriverFactory|AudioALSASpeechPhoneCallController|createAudioHardware|HAL server crashed|Fatal signal' /home/n8n/forge-work/m6-1779747393021/intake/evidence/adb/logcat_crash.txt | head -80
```

## 2026-05-26 fa21056a display clock-hold patch

FACT: Debug run `fa21056a-7024-4d07-a432-ffb5b29cda50` / capture `/home/n8n/forge-work/debug/0b13c8c6-d194-431f-a397-f852e3aae7d9/85c22568-0c02-4692-bf4f-f57fc12474a4/browser-debug-evidence-1779788484672.tar` was verified against ROM build `260f8ddf-229b-40c3-8719-37e2d761fbe4`; local, ZIP, and trimmed captured `boot.img` sha256 all match `654a8d90daa52a2a1b99c67f4be3efb7b909c94132aacf7975a95a8094c7990a`.

FACT: The capture reaches Android 8.1 userspace with SurfaceFlinger running and fb0 registered, but the physical display remains black. Dmesg lines around 1401/2015/2366/2836 show direct route registers already set (`OVL0_MOUT=0x1`, `COLOR0_SEL=0x1`, `DITHER_MOUT=0x1`, `RDMA0_SOUT=0x2`, `DSI0_SEL=0x1`) while `DISP_DL_VALID_0=0`, `DISP_DL_READY_0=0`, repeated `CMDQ_EVENT_DISP_RDMA0_EOF` token value is 0, and `MMSYS_CG` is `0xfc706bfc`/`0xfc702bfc` with gated primary scanout bits `larb0=1`, `ovl0=1`, `color=1`, `rdma0=1`.

FACT: The same dumps show RDMA0 enabled but not counting pixels (`GLOBAL=0x101`, `IN=0/1280`, `OUT=0/1280`, `MEM_CON=0`, `MEM_START=0`), OVL0 enabled with layer addresses, mutex0 armed, and DSI0 started. This keeps the frontier below HWC/fences and inside DDP clock/module state, not a new CMDQ wait-token or fake EOF problem.

PATCH HISTORY, BOOT-UNBLOCK, 2026-05-26: re-enable primary DDP scanout clocks before the first real video wait.

Hypothesis: The current M6 display path is configured but the primary scanout modules are still clock-gated when the first `FRAME_DONE`/`IF_VSYNC` wait begins. Re-running the normal DDP top-clock and per-module `power_on` sequence once for the primary display path, only when the captured gated bits are present, should let real RDMA0 EOF/VSYNC progress happen without fake token seeding or wait skipping.

Evidence: `fa21056a` dmesg lines around 1401-1411, 2015-2025, 2366-2390, and 2836-2846 show valid route registers, started DSI0, enabled OVL0/RDMA0, repeated RDMA0 EOF waits, and primary scanout CG bits still gated. The display bring-up guardrail says after the direct route is already `OVL0 -> COLOR0 -> DITHER -> RDMA0 -> DSI0`, do not patch waits/fences/CMDQ token seeding again; move to hardware state and clocks.

Files changed: `kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_manager.c` adds `dpmgr_m6_hold_primary_video_clocks()` and calls it before the first primary `FRAME_DONE`/`IF_VSYNC` wait; the existing first-wait/timeout dump remains read-only and now reports before/after clock-hold state with `M6 DDP clock hold[...]` markers.

Why each file changed: `ddp_manager.c` owns `dpmgr_wait_event_timeout()` and has access to the active primary display path handle, scenario module list, and existing `module_power_on()`/`ddp_path_top_clock_on()` helpers. The patch uses the normal driver power-on path for modules proven gated by the capture and does not change LCM init, DSI commands, fences, CMDQ waits, or EOF token values.

Expected next marker: next verified build kernel strings contain `M6 DDP clock hold`; next capture shows `M6 DDP clock hold[FRAME_DONE|VSYNC]` before the first timeout with lower/cleared `MMSYS_CG` bits for LARB0/OVL0/COLOR0/RDMA0. If the hypothesis is right, `CMDQ_EVENT_DISP_RDMA0_EOF` token-0 repeats stop or reduce, `rdma0`/`dsi0` IRQ counts rise, `DISP_DL_VALID_0`/`READY_0` become non-zero, SurfaceFlinger VSYNC enables, flips increase, and the panel lights.

Rollback condition: revert this clock-hold patch if a verified next build regresses before ADB/SurfaceFlinger/fb0, if the `M6 DDP clock hold` marker appears but the same CG bits remain gated with identical RDMA0 counters, or if power-on sequencing causes a new DSI/SMI/clock crash before the previous RDMA0 EOF frontier.

Verification commands:

```bash
# Build/source checks
cd /srv/forge/android/kernel-meizu_M6-N-ex6-linux-3.18.140
git diff --check -- kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_manager.c BRINGUP_STATE.md
grep -n 'M6 DDP clock hold' kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_manager.c
# After ROM/kernel rebuild:
gzip -cd /srv/forge/android/rom-lineage-15.1-meizu_m6-experimental/out/target/product/meizu_m6/kernel 2>/dev/null | strings | grep 'M6 DDP clock hold' || true
grep -R -n -E 'M6 DDP clock hold|M6 DDP timeout|MMSYS_CG gated bits|CMDQ_EVENT_DISP_RDMA0_EOF|DISP_DL_VALID|DISP_DL_READY|rdma0 INTEN|dsi0 START|VSYNC|flips=' <next-capture>/evidence <next-capture>/mtp
```

## 2026-05-26 fa21056a clock-hold rebuild verified

FACT: The M6 ROM rebuild containing the clock-hold kernel patch completed in the previous workspace `/home/n8n/forge-work/rom-workspaces/lineage-lineage-15.1-meizu_m6-885bc4c815`, container `fff7cb3dbfb5` / `codex-m6-prevworkspace-20260526111426`, with build log `/home/n8n/forge-work/rom-m6-prevdir-fa21056a-20260526/build.log` ending in `Package Complete` and `build completed successfully (04:12:21 (hh:mm:ss))`. Build log sha256 is `90daae0abe6ce0bc587ae1fb9ea2ff339182cc590f68edb69063df0c9e900e8f`.

FACT: Flashable ROM is `/home/n8n/forge-work/rom-workspaces/lineage-lineage-15.1-meizu_m6-885bc4c815/out/target/product/meizu_m6/lineage-15.1-20260526-UNOFFICIAL-meizu_m6.zip`, sha256 `68dba98f19adca29447a640782c5d5ab416e8ec4561eb8ac61b4e43472f84ddf`; `/usr/bin/zip -T` passes. `boot.img` sha256 is `9f3b5a4c021bb9f62dc731b749275d07609115e3d03c6b384eaf139ecee07a11`, and the ZIP-embedded `boot.img` sha256 is the same.

FACT: Kernel payload `/home/n8n/forge-work/rom-workspaces/lineage-lineage-15.1-meizu_m6-885bc4c815/out/target/product/meizu_m6/kernel` sha256 is `e4670296680c27a7645ab562ddd9998b0707ea8db27bd1d25427d20009772392`. Matching source-kernel identity files are `/srv/forge/android/kernel-meizu_M6-N-ex6-linux-3.18.140/kernel-3.18/out/forge-meizu_m6_defconfig/System.map` sha256 `f739351a28afaac56178a192cfb914d8e1b9757b6654f0113c501c774309b56a` and `.config` sha256 `bc272726035c1a2eca9422e9bc230cf54f8295648a8865a98faba046ab01619e`.

FACT: Decompressing the built kernel payload and grepping strings finds the expected marker `M6 DDP clock hold[%s]: re-enable primary scanout clocks CG=0x%x mask=0x%x`, proving the rebuilt artifact contains the clock-hold patch.

Expected next marker: after flashing this exact ROM, the next capture should show `M6 DDP clock hold[FRAME_DONE|VSYNC]` before the first `M6 DDP timeout[...]` dump. If the hypothesis is correct, `MMSYS_CG` should clear at least the primary scanout gated bits, RDMA0/DISPLAY IRQ counters should rise, and the black screen should move to a newer visible log frontier or light the panel.

Verification commands used:

```bash
/usr/bin/zip -T /home/n8n/forge-work/rom-workspaces/lineage-lineage-15.1-meizu_m6-885bc4c815/out/target/product/meizu_m6/lineage-15.1-20260526-UNOFFICIAL-meizu_m6.zip
sha256sum /home/n8n/forge-work/rom-workspaces/lineage-lineage-15.1-meizu_m6-885bc4c815/out/target/product/meizu_m6/lineage-15.1-20260526-UNOFFICIAL-meizu_m6.zip /home/n8n/forge-work/rom-workspaces/lineage-lineage-15.1-meizu_m6-885bc4c815/out/target/product/meizu_m6/boot.img /home/n8n/forge-work/rom-workspaces/lineage-lineage-15.1-meizu_m6-885bc4c815/out/target/product/meizu_m6/kernel /srv/forge/android/kernel-meizu_M6-N-ex6-linux-3.18.140/kernel-3.18/out/forge-meizu_m6_defconfig/System.map /srv/forge/android/kernel-meizu_M6-N-ex6-linux-3.18.140/kernel-3.18/out/forge-meizu_m6_defconfig/.config /home/n8n/forge-work/rom-m6-prevdir-fa21056a-20260526/build.log
unzip -p /home/n8n/forge-work/rom-workspaces/lineage-lineage-15.1-meizu_m6-885bc4c815/out/target/product/meizu_m6/lineage-15.1-20260526-UNOFFICIAL-meizu_m6.zip boot.img | sha256sum
gzip -cd /home/n8n/forge-work/rom-workspaces/lineage-lineage-15.1-meizu_m6-885bc4c815/out/target/product/meizu_m6/kernel 2>/dev/null | strings | grep 'M6 DDP clock hold'
grep -n -E 'Package Complete|build completed successfully|FAILED:|ninja: error|No space left|error:' /home/n8n/forge-work/rom-m6-prevdir-fa21056a-20260526/build.log | tail -30
```

## 2026-05-27 f8285f8e trigger-loop display evidence gap

FACT: Latest debug run `f8285f8e-0f7d-4cc3-9e32-31ae5ac15f86` captured boot
partition trim sha256 `9f3b5a4c021bb9f62dc731b749275d07609115e3d03c6b384eaf139ecee07a11`,
matching the clock-hold ROM boot image from the previous section. The capture
contains `M6 DDP ufoe route: panel ufoe_enable=0; route RDMA0 directly to DSI0`
and then `M6 video CMDQ: trigger loop waits real RDMA0_EOF/MUTEX0_STREAM_EOF`,
but it does not contain `M6 DDP clock hold` or `M6 DDP timeout[...]`.

INFERENCE: The current evidence reaches the trigger-loop wait before the
`dpmgr_wait_event_timeout()` diagnostic path, so the existing timeout dump is
too late for this run. The next artifact needs a read-only snapshot at the
trigger-loop wait site before any new behavior change.

PATCH HISTORY, DIAGNOSTIC, 2026-05-27: add a read-only trigger-loop display
state dump.

Hypothesis: The source kernel is still stalled at the first video trigger loop,
but the missing layer data is route/clock/mutex/RDMA/OVL/DSI state at the
moment the CMDQ trigger loop queues the real RDMA0/MUTEX0 waits.

Evidence: `docs/run_reports/2026-05-27_m6_f8285f8e_display_run_final.json`
selects `kernel.display.rdma`; the fresh dmesg evidence line is
`[DISP]ERROR:M6 video CMDQ: trigger loop waits real RDMA0_EOF/MUTEX0_STREAM_EOF`.

Files changed: `kernel-3.18/drivers/misc/mediatek/video/mt6755/primary_display.c`
adds `primary_m6_dump_trigger_loop_state("before-wait")` under the existing
one-shot trigger-loop diagnostic guard.

Why each file changed: `primary_display.c` owns `_cmdq_build_trigger_loop()`,
which is the earliest proven line in the latest capture. The new dump only
reads registers and does not change DSI commands, clocks, fences, waits, or
CMDQ event tokens.

Expected next marker: next verified build kernel strings contain
`M6 trigger dump[%s]`; next capture contains `M6 trigger dump[before-wait]`
lines for `VALID/READY`, `MMSYS_CG gated bits`, `mutex`, `rdma0`, `ovl0`, and
`dsi0`, before the RDMA0 EOF wait line.

Rollback condition: revert this diagnostic if the verified next artifact
regresses before the existing `M6 video CMDQ` marker or if a read-only register
access faults before ADB/SurfaceFlinger.

Verification commands:

```bash
grep -n 'M6 trigger dump' kernel-3.18/drivers/misc/mediatek/video/mt6755/primary_display.c
gzip -cd <rebuilt-kernel> 2>/dev/null | strings | grep 'M6 trigger dump'
grep -R -n -E 'M6 trigger dump|M6 video CMDQ|CMDQ_EVENT_DISP_RDMA0_EOF|M6 DDP clock hold|M6 DDP timeout' <next-capture>/evidence <next-capture>/mtp
```

## 2026-05-27 8ed94f7b trigger-loop clock hold

FACT: Debug run `8ed94f7b-b3c9-4f85-a034-5e1e5ee504bd` captured manual boot
artifact `/home/n8n/forge-work/artifacts/meizu_m6/20260527-m6-trigger-dump-manual/boot-mkbootimg.img`
with sha256 `bf5d9326591d1d9cf3dea93b153aee93b18bbaa60a49942d83a4f7334a3683ff`.
The runtime identity pack reports `ro.product.device=meizu_m6`; `/proc/fb`
contains `0 mtkfb`.

FACT: The BOOT-UNBLOCK rebuild produced kernel payload
`/home/n8n/forge-work/kernel-builds/m6-8ed94f7b-display/out/arch/arm64/boot/Image.gz-dtb`
with sha256 `91d9b14962052619d7e8b9d4394ae840d0990d68707557060ed8204650561d62`.
The manual boot image is
`/home/n8n/forge-work/artifacts/meizu_m6/20260527-m6-trigger-clock-hold-manual/boot.img`
with sha256 `810b58028f7c8d2eb383600677d11dd98a9e426ab0f81720490c3b9112109710`;
Build Station artifact id is `fb05707b-aace-469a-b784-71d77011dec6`.

FACT: The new trigger-loop dump is present in
`docs/run_reports/2026-05-27_m6_8ed94f7b_debug_text.txt`: before the real
RDMA0/MUTEX0 wait, route registers are set, but `READY=0x0` and
`MMSYS_CG=0xfc706bfc` with `larb0=1 ovl0=1 rdma0=1 color=1`. RDMA0 is enabled
and counting partially (`IN=662/768 OUT=68/765`), DSI0 is started, and OVL0
registers are readable. This keeps the frontier inside primary DDP scanout
clocks/RDMA, not in HWC or userspace.

PATCH HISTORY, BOOT-UNBLOCK, 2026-05-27: re-run normal primary DDP path power-on
immediately before the first video trigger-loop frame wait when scanout clocks
are still gated.

Hypothesis: The earlier clock-hold in `dpmgr_wait_event_timeout()` runs too
late for the current boot path. The first CMDQ trigger loop queues real
RDMA0/MUTEX0 waits while primary scanout clocks are already gated, so the
frame never completes. Re-running the normal `dpmgr_path_power_on()` sequence
once at this exact site should ungate the active DDP modules without fake EOF
tokens, wait skipping, or direct broad CG register writes.

Evidence: `8ed94f7b` lines 3487-3494 show the first trigger-loop wait and the
before-wait dump: `READY=0x0`, gated `larb0/ovl0/rdma0/color`, RDMA output
stopping at `68/765`, and DSI0 already started. The generated `.config` has
`# CONFIG_MTK_CLKMGR is not set`, so this tree uses the CCF `ddp_clk_*` path
which `dpmgr_path_power_on()` already drives per module.

Files changed: `kernel-3.18/drivers/misc/mediatek/video/mt6755/primary_display.c`
adds a one-shot `primary_m6_hold_trigger_loop_clocks()` call between the
before-clock dump and the real RDMA0/MUTEX0 waits.

Why each file changed: `_cmdq_build_trigger_loop()` is the earliest proven
display blocker in the latest capture. The patch calls existing path power-on
plumbing for the active primary display path only if the captured scanout CG
bits are set; it does not change LCM init, DSI commands, CMDQ events, fences,
or EOF waits.

Expected next marker: next capture shows `M6 trigger clock hold[before-wait]`
followed by `M6 trigger dump[before-wait]` with lower/cleared
`MMSYS_CG` bits for LARB0/OVL0/RDMA0/COLOR0. If the hypothesis is right,
RDMA0 EOF/MUTEX0 stream EOF should progress and the panel should either light
or advance to a newer display frontier.

Rollback condition: revert this patch if a verified next artifact regresses
before `M6 video CMDQ`, if the hold marker appears but the same CG bits remain
gated with identical RDMA counters, or if it causes a new DSI/SMI/clock crash
before the previous RDMA0 EOF frontier.

Verification commands:

```bash
grep -n -E 'M6 trigger clock hold|M6 trigger dump' kernel-3.18/drivers/misc/mediatek/video/mt6755/primary_display.c
gzip -cd <rebuilt-kernel> 2>/dev/null | strings | grep -E 'M6 trigger clock hold|M6 trigger dump'
grep -R -n -E 'M6 trigger clock hold|M6 trigger dump|MMSYS_CG gated bits|CMDQ_EVENT_DISP_RDMA0_EOF|MUTEX0_STREAM_EOF|rdma0 INTEN|dsi0 START' <next-capture>/evidence <next-capture>/mtp
```

## 2026-05-27 976594e3 trigger-loop clock hold result

FACT: Debug run `976594e3-67e8-472c-8a82-3bfb32ec13e4` captured target Android from manual trigger-clock boot `/home/n8n/forge-work/artifacts/meizu_m6/20260527-m6-trigger-clock-hold-manual/boot.img`, sha256 `810b58028f7c8d2eb383600677d11dd98a9e426ab0f81720490c3b9112109710`. The local boot image kernel payload sha256 is `91d9b14962052619d7e8b9d4394ae840d0990d68707557060ed8204650561d62`; runtime `/proc/version` reports `Linux version 3.18.140 ... Wed May 27 17:37:42 CDT 2026`.

FACT: Fresh dmesg has the expected display markers from commit `80489cbc54d`: `M6 video CMDQ: trigger loop waits real RDMA0_EOF/MUTEX0_STREAM_EOF`, `M6 trigger dump[before-clock-hold]`, and `M6 trigger clock hold[before-wait]`. Before the hold, `MMSYS_CG=0xfc706bfc` with `larb0=1 ovl0=1 rdma0=1 color=1`. After `dpmgr_path_power_on()`, `M6 trigger clock hold[before-wait]: after path_power_on CG=0xfc706bfc`; the following dump still reports `larb0=1 ovl0=1 rdma0=1 color=1`.

FACT: The same dump shows the direct route still set (`OVL0_MOUT=0x1`, `COLOR0_SEL=0x1`, `DITHER_MOUT=0x1`, `RDMA0_SOUT=0x2`, `DSI0_SEL=0x1`), `mutex EN=0x1 MOD=0x5f280 SOF=0x41`, RDMA0 enabled (`GLOBAL=0x101`, `SIZE=720x1280`), DSI0 started (`START=0x1`, `MODE=0x3`, `PHY_LCCON=0x1`). The frontier remains below HWC/SurfaceFlinger and inside DDP/clock/SMI/RDMA.

INFERENCE: The trigger-loop `dpmgr_path_power_on()` call did not ungate the primary scanout clocks. The next display patch must not fake EOF, skip waits, seed CMDQ tokens, or repeat the same hold. The next kernel hypothesis should inspect lower CCF/DDP clock enable state and SMI/LARB power-domain state for LARB0/OVL0/COLOR0/RDMA0.

BLOCKER: The same captured boot ramdisk still had stale M6 storage/m2note contamination (`Build Station m2note`, `/fstab.mt6735`, flat `mtk-msdc.0/by-name` paths). A clean ramdisk repack now exists at `/home/n8n/forge-work/artifacts/meizu_m6/20260527-m6-trigger-clock-hold-ramdiskfix/boot.img`, sha256 `9300bd7d878e9761b0cc3a56842108423c856ecab6af1d3cd3d7da36b494e10d`, Build Station artifact `d6cb5087-c535-4947-b673-11cd7ee915bb`, with the same kernel payload sha256 `91d9b14962052619d7e8b9d4394ae840d0990d68707557060ed8204650561d62`. Flash and recapture that clean boot before adding another display behavior patch.

Expected next marker: with the clean-ramdisk boot flashed, the next capture should still show `M6 trigger clock hold[before-wait]` if the display frontier is unchanged, but without the m2note/fstab noise. If `MMSYS_CG` remains `0xfc706bfc` and the same gated bits remain after the hold, patch lower CCF/DDP/SMI/LARB clock-domain instrumentation/enable paths.

Verification commands:

```bash
sha256sum /home/n8n/forge-work/artifacts/meizu_m6/20260527-m6-trigger-clock-hold-manual/boot.img /home/n8n/forge-work/artifacts/meizu_m6/20260527-m6-trigger-clock-hold-ramdiskfix/boot.img /home/n8n/forge-work/artifacts/meizu_m6/20260527-m6-trigger-clock-hold-ramdiskfix/unpack/zImage
grep -n -E 'M6 trigger clock hold|M6 trigger dump|MMSYS_CG gated bits|RDMA0_EOF|MUTEX0_STREAM_EOF' /home/n8n/build-station/docs/run_reports/2026-05-27_m6_976594e3_debug_text.txt
grep -R -n -E 'Build Station m2note|fstab\.mt6735|mtk-msdc\.0/by-name/(system|userdata|nvdata|protect1|protect2)|mtk\.msdc\.0/by-name' /home/n8n/forge-work/artifacts/meizu_m6/20260527-m6-trigger-clock-hold-ramdiskfix/verify/ramdisk
grep -R -n -E '11230000\.msdc0/by-name/(system|userdata|nvdata|protect1|protect2)' /home/n8n/forge-work/artifacts/meizu_m6/20260527-m6-trigger-clock-hold-ramdiskfix/verify/ramdisk/init.mt6755.rc /home/n8n/forge-work/artifacts/meizu_m6/20260527-m6-trigger-clock-hold-ramdiskfix/verify/ramdisk/fstab.mt6755
```

## 2026-05-28 cgdecode kernel with clean M6 ramdisk

FACT: Artifact directory `/home/n8n/forge-work/artifacts/meizu_m6/20260528-m6-cgdecode-diagnostic/` is not identical to the prior trigger-clock boot. Its `boot.img` sha256 is `e19e854ecc81e0277967f054abb609034626936d816dbc08f86d9590faf4e7f4`; unpacked kernel payload sha256 is `a2b7ac8b5bd3af357f18acbb3b0679c049d89bf85614cf973deacc25b7cd5bf6`, different from trigger-clock `91d9b14962052619d7e8b9d4394ae840d0990d68707557060ed8204650561d62`.

FACT: The cgdecode boot's ramdisk sha256 is `36cb0e637d7fab95afc7c4d941bb5aeef542197c881ea4026b9f9f26c3a33b20`, exactly matching the stale manual trigger-clock ramdisk and not the clean M6 ramdisk `a599580be2c2f0a7c12afa6a160a94e532b9dffaf1e56368593d801881f1e72e`.

FACT: Clean combined display diagnostic boot is `/srv/forge/android/export/meizu_m6_artifacts/20260528-m6-cgdecode-clean-ramdisk/boot.img`, sha256 `db072d18b932c5e76c6f61a18491c46531a5a9ba9db1defa14f782ca6dd3a42d`. Unpadded `boot-mkbootimg.img` sha256 is `7f934825c394cd31770e836a24373766a9ef965979779103baf20f363831b5e4`. It combines cgdecode kernel payload sha256 `a2b7ac8b5bd3af357f18acbb3b0679c049d89bf85614cf973deacc25b7cd5bf6` with clean ramdisk sha256 `a599580be2c2f0a7c12afa6a160a94e532b9dffaf1e56368593d801881f1e72e`.

FACT: Extracted clean combined ramdisk has no `Build Station m2note`, no `fstab.mt6735`, no flat `/dev/block/platform/mtk-msdc.0/by-name/{system,userdata,nvdata,protect1,protect2}`, and no typo `/dev/block/platform/mtk.msdc.0/by-name`; it retains nested `11230000.msdc0/by-name` paths for system/userdata/protect1/protect2/nvdata.

Expected next marker: flash `/srv/forge/android/export/meizu_m6_artifacts/20260528-m6-cgdecode-clean-ramdisk/boot.img`, then capture. Judge display from cgdecode markers only after identity proves boot sha256 `db072d18b932c5e76c6f61a18491c46531a5a9ba9db1defa14f782ca6dd3a42d` or kernel payload sha256 `a2b7ac8b5bd3af357f18acbb3b0679c049d89bf85614cf973deacc25b7cd5bf6`; ramdisk evidence should stay clean of m2note/fstab noise.

Verification commands:

```bash
sha256sum /home/n8n/forge-work/artifacts/meizu_m6/20260527-m6-trigger-clock-hold-manual/boot.img /home/n8n/forge-work/artifacts/meizu_m6/20260527-m6-trigger-clock-hold-ramdiskfix/boot.img /home/n8n/forge-work/artifacts/meizu_m6/20260528-m6-cgdecode-diagnostic/boot.img
abootimg -x /home/n8n/forge-work/artifacts/meizu_m6/20260528-m6-cgdecode-diagnostic/boot.img
sha256sum zImage initrd.img
sha256sum /srv/forge/android/export/meizu_m6_artifacts/20260528-m6-cgdecode-clean-ramdisk/boot.img /srv/forge/android/export/meizu_m6_artifacts/20260528-m6-cgdecode-clean-ramdisk/boot-mkbootimg.img /srv/forge/android/export/meizu_m6_artifacts/20260528-m6-cgdecode-clean-ramdisk/Image-cgdecode.gz-dtb /srv/forge/android/export/meizu_m6_artifacts/20260528-m6-cgdecode-clean-ramdisk/clean-initrd.img
grep -RIn -E 'Build Station m2note|fstab\.mt6735|mtk-msdc\.0/by-name/(system|userdata|nvdata|protect1|protect2)|mtk\.msdc\.0/by-name' /tmp/m6_cgdecode_clean_verify/ramdisk
grep -RIn -E '11230000\.msdc0/by-name/(system|userdata|nvdata|protect1|protect2)' /tmp/m6_cgdecode_clean_verify/ramdisk/init.mt6755.rc /tmp/m6_cgdecode_clean_verify/ramdisk/fstab.mt6755
```

## 2026-05-28 6652033c CG decode correction

FACT: Debug run `6652033c-31da-45c2-bb7e-d014ca5772f8` uses the manual
trigger-clock boot family. Runtime `/proc/version` reports
`Linux version 3.18.140 ... Wed May 27 17:37:42 CDT 2026`; preflight stored
capture evidence at
`/home/n8n/build-station/docs/run_reports/preflight_6652033c-31da-45c2-bb7e-d014ca5772f8/display_debug.md`.

FACT: The previous M6 markers decoded `MMSYS_CG=0xfc706bfc` with MT6755
bit positions from an old table. The actual CCF gate table in
`drivers/clk/mediatek/clk-mt6755.c` maps LARB0/OVL0/RDMA0/COLOR/DITHER/
OVL0_MOUT to CG_CON0 bits 1/10/12/15/19/25, and DSI engine/digital to
CG_CON1 bits 0/1. Under that map `0xfc706bfc/0xffffffc0` has those primary
scanout clocks ungated.

PATCH HISTORY, PROPER-FIX, 2026-05-28: correct MT6755 display CG bit decode
and stop the false trigger-loop clock-hold condition.

Hypothesis: The current display frontier is RDMA0 frame-done generation, not
gated primary clocks. The wrong CG bit map made the debug pipeline and kernel
markers report `larb0=1 ovl0=1 rdma0=1 color=1` even though the CCF table says
those gates are clear, which caused repeated clock-hold attempts on the wrong
blocker.

Evidence: `docs/run_reports/2026-05-28_6652033c_debug_text.txt` lines around
1631-1649 show `M6 trigger dump`, RDMA0/OVL0/DSI0 started, and the old wrong
gated-bit labels. Lines around 12370 and later show repeated
`CMDQ_EVENT_DISP_RDMA0_EOF` token value 0. `clk-mt6755.c` defines the actual
gate bits: LARB0 bit 1, OVL0 bit 10, RDMA0 bit 12, COLOR bit 15, DITHER bit
19, OVL0_MOUT bit 25, DSI engine/digital in CG_CON1 bits 0/1.

Files changed: `kernel-3.18/drivers/misc/mediatek/video/mt6755/primary_display.c`
corrects trigger-loop CG decode, logs CG_CON1, and uses the correct MT6755
scanout mask for the one-shot hold guard. `kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_manager.c`
corrects timeout CG decode, logs CG_CON1, and uses the same corrected mask.
`BRINGUP_STATE.md` records the evidence and next capture condition.

Why each file changed: both files own the diagnostic markers that misled the
display pipeline. The mask must match the CCF gate table so the kernel no
longer treats unrelated set bits in `MMSYS_CG_CON0` as proven display clock
gates.

Expected next marker: next capture from a rebuilt boot shows `MMSYS_CG=.../...`
and `MMSYS_CG gated bits larb0=0 ovl0=0 rdma0=0 color=0 dither=0 dsi_engine=0
dsi_digital=0` if the clock state is unchanged. The remaining blocker should
stay at real `CMDQ_EVENT_DISP_RDMA0_EOF` / frame-done generation with corrected
RDMA/OVL/DSI state, not a clock-gate label.

Rollback condition: revert this correction only if a verified next capture
with matching boot identity proves the corrected bit map contradicts
`clk-mt6755.c`, or if the new artifact regresses before the existing
`M6 video CMDQ` trigger-loop marker.

Verification commands:

```bash
cd /srv/forge/android/meizu_m6/kernel-meizu_M6-N-ex6-linux-3.18.140
grep -n -E 'M6 trigger dump|M6 trigger clock hold' kernel-3.18/drivers/misc/mediatek/video/mt6755/primary_display.c
grep -n -E 'M6 DDP timeout|M6_PRIMARY_SCANOUT_CG_MASK' kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_manager.c
grep -n -E 'MM_DISP0_(SMI_LARB0|DISP_OVL0|DISP_RDMA0|DISP_COLOR|DISP_DITHER|DISP_OVL0_MOUT)|MM_DISP1_DSI' kernel-3.18/drivers/clk/mediatek/clk-mt6755.c
grep -R -n -E 'M6 trigger dump|MMSYS_CG gated bits|CMDQ_EVENT_DISP_RDMA0_EOF|RDMA0_EOF|M6 DDP timeout' <next-capture>/evidence <next-capture>/mtp
```

## 2026-05-28 CG decode diagnostic boot artifact

FACT: Manual clean-out kernel build completed at
`/home/n8n/forge-work/kernel-builds/m6-6652033c-cgfix/out`. The matching
artifacts are:
`Image-cgdecode.gz-dtb` sha256
`a2b7ac8b5bd3af357f18acbb3b0679c049d89bf85614cf973deacc25b7cd5bf6`,
`System.map.cgdecode` sha256
`2ce165540536b024d868ef947cfe7eac59944a546796a66ea0cde0113ff3cb3d`,
and `kernel-cgdecode.config` sha256
`bc272726035c1a2eca9422e9bc230cf54f8295648a8865a98faba046ab01619e`.

FACT: The rebuilt boot image is
`/home/n8n/forge-work/artifacts/meizu_m6/20260528-m6-cgdecode-diagnostic/boot.img`,
sha256 `e19e854ecc81e0277967f054abb609034626936d816dbc08f86d9590faf4e7f4`,
Build Station artifact `917b63aa-7431-48f7-8f89-d55a7a5ac7aa`. It repacks
the CG decode kernel with the clean ramdisk base from
`20260527-m6-trigger-clock-hold-ramdiskfix`. Boot header: size `16777216`,
page `2048`, board `1552631950`, kernel addr `0x40080000`, ramdisk addr
`0x45000000`, tags addr `0x44000000`, kernel size `7572285`, ramdisk size
`1258507`, cmdline `bootopt=64S3,32N2,64N2 androidboot.selinux=permissive
binder.devices=binder,hwbinder,vndbinder buildvariant=eng`.

Expected next marker: next capture must match boot sha256
`e19e854ecc81e0277967f054abb609034626936d816dbc08f86d9590faf4e7f4`, show
`MMSYS_CG=.../...`, and decode the primary scanout gates from the corrected
MT6755 CCF bit map. If display remains black, continue from RDMA0 EOF/frame
done state with the corrected `rdma0`/`ovl0`/`dsi0` dumps instead of clock-gate
labels.

Verification commands:

```bash
sha256sum -c /home/n8n/forge-work/artifacts/meizu_m6/20260528-m6-cgdecode-diagnostic/SHA256SUMS
abootimg -i /home/n8n/forge-work/artifacts/meizu_m6/20260528-m6-cgdecode-diagnostic/boot.img
gzip -cd /home/n8n/forge-work/artifacts/meizu_m6/20260528-m6-cgdecode-diagnostic/Image-cgdecode.gz-dtb 2>/dev/null | strings | grep -E 'M6 trigger dump|M6 DDP timeout|MMSYS_CG=0x%x/%x'
grep -R -n -E 'MMSYS_CG=|MMSYS_CG gated bits|CMDQ_EVENT_DISP_RDMA0_EOF|RDMA0_EOF|M6 DDP timeout|M6 trigger dump' <next-capture>/evidence <next-capture>/mtp
```

## 2026-05-30 PQ bypass isolation

PATCH HISTORY, ISOLATION, 2026-05-30: bypass MTK PQ modules while the
verified M6 display path stalls below framebuffer memory.

Hypothesis: M6 is not failing in SurfaceFlinger or framebuffer writes. The
active primary DDP mutex includes the PQ tail (`COLOR0`, `CCORR`, `AAL`,
`GAMMA`, `DITHER`), and fresh dumpreg evidence shows those modules not
advancing a real 720x1280 frame (`COLOR0` pixel count 0; `CCORR`/`AAL`/
`GAMMA`/`DITHER` counters at `0x10001`). Relaying PQ through the existing MTK
PQ bypass path may let the OVL/RDMA/DSI route produce physical scanout, matching
the neighboring-device symptom where disabling PQ made the image appear.

Evidence: flashed boot identity for the current black-screen artifact is
sha256 `8067ef307baaac03f3432ecf23b40324a3d81b8ead6e97592dfa6fd4c87f9bf9`
from `/srv/forge/android/meizu_m6/captures/20260530-m6-after-f8cf9c69-711HEBSR277K5`.
The framebuffer marker capture
`/srv/forge/android/meizu_m6/captures/20260530-m6-screenmarkers-f8cf9c69-711HEBSR277K5`
proves `/dev/graphics/fb0` accepts marker writes and reads back the exact marker
sha256 `3d35dd022db31a15a06b1126ff3d6a66e6989b74517f56100dcfe715e9dfa52b`,
while the panel stays physically black. PQ investigation capture
`/srv/forge/android/meizu_m6/captures/20260530-m6-pq-investigation-f8cf9c69-711HEBSR277K5`
shows `Option [22][DISP_OPT_BYPASS_PQ] Value [0]`, `M0_MOD=0x1df280`
(`ovl0`, `rdma0`, `color0`, `ccorr`, `aal`, `gamma`, `dither`, `pwm0`,
`ovl0_2l`, `ovl1_2l`), and dumpreg counters with PQ blocks stuck at the
1x1-style frontier instead of 720x1280 progress.

Files changed: `kernel-3.18/drivers/misc/mediatek/video/mt6755/disp_helper.c`
sets `DISP_OPT_BYPASS_PQ` to 1 during display helper init. `BRINGUP_STATE.md`
records the isolation hypothesis, evidence, rollback, and next-capture checks.

Why each file changed: `ddp_manager.c` already calls each PQ module's
`bypass(module, 1)` path when `DISP_OPT_BYPASS_PQ` is enabled, so this uses the
stock MTK relay/bypass mechanism instead of patching waits, fences, CMDQ tokens,
or unrelated DDP routing. The state file is the per-device bring-up journal for
this kernel tree.

Expected next marker: after flashing the rebuilt boot image, kernel logs should
show `Set Option 22(DISP_OPT_BYPASS_PQ) from (0) to (1)` and
`After set Option 22(DISP_OPT_BYPASS_PQ) is (1)`. A useful positive result is
physical image, rising `rdma0`/`dsi0` IRQ counts, non-zero DDP valid/ready, or
PQ relay bits visible in dumpreg (`COLOR_CFG_MAIN` relay bit, AAL/GAMMA/DITHER
relay config) with frame counters advancing beyond the current 1x1 frontier.

Rollback condition: revert this isolation patch if a verified next artifact
with `DISP_OPT_BYPASS_PQ=1` still has the same physical black screen, the same
`VALID=0x0 READY=0x0`, the same stagnant `RDMA0 Transfer`/IRQ state, and the
same 1x1 PQ counter frontier, or if bypassing PQ regresses boot before fb0,
SurfaceFlinger, or bootanimation are present.

Verification commands:

```bash
cd /srv/forge/android/meizu_m6/kernel-meizu_M6-N-ex6-linux-3.18.140
grep -n 'DISP_OPT_BYPASS_PQ' kernel-3.18/drivers/misc/mediatek/video/mt6755/disp_helper.c
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell dmesg | grep -E 'DISP_OPT_BYPASS_PQ|Set Option 22|RDMA0_EOF|M6 DDP timeout|MMSYS_CG|M0_MOD'
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell cat /proc/interrupts | grep -E 'mtk_cmdq|ovl0|rdma0|dsi0|aal'
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell 'echo dump_reg > /d/mtkfb || echo dump_reg > /sys/kernel/debug/mtkfb'
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell 'cat /d/mtkfb 2>/dev/null || cat /sys/kernel/debug/mtkfb 2>/dev/null' | grep -E 'DISP_OPT_BYPASS_PQ|M0_MOD|COLOR_CFG_MAIN|DISP_AAL_CFG|GAMMA_CFG|DITHER_CFG|RDMA0'
```

Artifact build note: Build Station API build
`d4e48bf6-982d-4175-86b7-fd48adab72a7` was started with options file
`/home/n8n/build-station/docs/run_reports/2026-05-30-m6-pq-bypass-build-options.json`
but cancelled at `PREPARE_KERNEL` because the root-owned reused
`out/forge-meizu_m6_defconfig` stalled in `scripts/kconfig/conf` for multiple
minutes before compilation. The same source state was built in a clean manual
out-dir with the registered Build Station toolchain:
`/home/n8n/forge-work/kernel-builds/m6-pq-bypass-20260530/out`.

FACT: clean build output
`/home/n8n/forge-work/kernel-builds/m6-pq-bypass-20260530/out/arch/arm64/boot/Image.gz-dtb`
has sha256 `d21d981d18328b03f04b30dc3fa8da24c455134318c678d3c320f6f86ec1a121`.
Matching `System.map` sha256 is
`2ce165540536b024d868ef947cfe7eac59944a546796a66ea0cde0113ff3cb3d`; matching
`.config` sha256 is `bc272726035c1a2eca9422e9bc230cf54f8295648a8865a98faba046ab01619e`.

FACT: repacked test boot is
`/srv/forge/android/export/meizu_m6_artifacts/20260530-m6-pq-bypass-isolation/boot-pq-bypass-mkbootimg-abs.img`,
sha256 `585e8a710a5b6ee2c96e6d995d2879d9fa63dca8321c9a4f15c9fcdb41c1f739`.
It reuses the verified current LOS15 boot base
`/srv/forge/android/meizu_m6/rom-lineage-15.1-meizu_m6-experimental/out/target/product/meizu_m6/boot.img`
sha256 `8067ef307baaac03f3432ecf23b40324a3d81b8ead6e97592dfa6fd4c87f9bf9` and
ramdisk sha256 `b897743629941f917796605abb7c20f3cb3ee9c2e60a12faf8d9fb8de0279a81`.
`abootimg -i` reports page size 2048, board `1552631950`, kernel address
`0x40080000`, ramdisk address `0x45000000`, tags address `0x44000000`, and the
same cmdline as the verified f8cf9c69 boot.

## 2026-05-30 repeated display pokes after PQ bypass

PATCH HISTORY, DIAGNOSTIC, 2026-05-30: collect repeated runtime display pokes,
screen markers, DSI pattern tests, LCM reset/ATA checks, and a clean reboot
baseline against the PQ-bypass artifact.

Hypothesis: the M6 black screen is no longer best explained by PQ, userspace
composition, framebuffer memory, or backlight alone. With `DISP_OPT_BYPASS_PQ=1`
verified in the flashed artifact, the display stack can still compose boot
animation frames and accept framebuffer writes, but physical panel output stays
black. The next frontier is LCM/DSI video or panel state: DSI command reads
return a packet but the 0x2A ATA payload is wrong/zero, and hardware BIST
patterns remain invisible even with the backlight forced on.

Evidence: the active boot prefix hash matches the local PQ-bypass test image:
`/srv/forge/android/export/meizu_m6_artifacts/20260530-m6-pq-bypass-isolation/boot-pq-bypass-mkbootimg-abs.img`
sha256 `585e8a710a5b6ee2c96e6d995d2879d9fa63dca8321c9a4f15c9fcdb41c1f739`;
`dd if=<boot-partition> bs=8876032 count=1 | sha256sum` returned the same
hash in the clean reboot capture.

Capture
`/srv/forge/android/meizu_m6/captures/20260530-141613-m6-repeat-display-pokes-585e8a-711HEBSR277K5`
forced wake and `lcd-backlight` to 255, ran red/green/blue/white DSI patterns,
three suspend/resume cycles, pattern off, an fb blank cycle, and a direct fb
marker write. The user still reported only black. FACT: final SurfaceFlinger
had the built-in display on (`powerMode=2`, `isDisplayOn=1`) with
`BootAnimation` present and `flips=590`. FACT: `screencap-final.png` is a valid
720x1280 PNG (10008 bytes, non-empty extrema), so userspace composition exists.
FACT: the fb marker was overwritten by the active display stack instead of
staying static, which means the framebuffer path is not simply dead memory.
FACT: DSI pattern tests were still physically invisible. FACT: the final DDP
timeout still shows `primary_rdma0_color0_disp`, `rdma0 GLOBAL=0x101
SIZE=720x1280`, and `dsi0 START=0x1 MODE=0x1 TXRX=0x1003c PS=0x30870`.

Capture
`/srv/forge/android/meizu_m6/captures/20260530-142249-m6-lcm-reset-ata-pokes-585e8a-711HEBSR277K5`
ran `ata`, `lcm0_reset + dsipattern`, `suspend + lcm0_reset + resume`,
`primary_reset`, `esd_recovery`, and pattern off. FACT: the ATA/DCS read path
does not hang or hit a read-ready timeout, but the payload is wrong/zero:
`DSI_RX_STA=0x00000a40`, `DSI_CMDQ_DATA0=0x00043700`,
`DSI_CMDQ_DATA1=0x002a0604`, `DSI_RX_DATA0=0x3300041c`,
`DSI_RX_DATA1=0x00000000`, packet type `0x1c`, long packet size `4`. The
current `ili9881p_hd_dsi_txd` ATA check expects the written 0x2A window bytes,
so this is evidence of a live-but-wrong panel command state rather than a fixed
scanout path. FACT: `esd_recovery` is not a fix on this artifact; it increments
the recovery counter and later saturates CMDQ with `There too many DISP
(198/200) tasks cannot acquire thread` and repeated
`CMDQ_EVENT_DISP_RDMA0_EOF`.

Capture
`/srv/forge/android/meizu_m6/captures/20260530-142713-m6-clean-reboot-after-pokes-585e8a-711HEBSR277K5`
cleanly rebooted the same artifact after the invasive pokes. FACT:
`/proc/version` reports the expected May 30 PQ-bypass kernel, boot prefix hash
matches `585e8a710a5b6ee2c96e6d995d2879d9fa63dca8321c9a4f15c9fcdb41c1f739`,
and `DISP_OPT_BYPASS_PQ=1` remains active. FACT: immediately after clean boot
`/sys/class/leds/lcd-backlight/brightness` was 0, but previous captures forced
255 and still had no visible pixels, so boot-time brightness 0 is not sufficient
as root cause. FACT: clean boot returned to the same lower display failure
shape: `M6 trigger dump[before-clock-hold]`, later `VALID=0x0 READY=0x0`,
`M0_MOD=0x1df280`, `rdma0 GLOBAL=0x101 SIZE=720x1280`,
`dsi0 START=0x1 STA=0x20`, low display IRQ counts, and SurfaceFlinger showing
the boot animation display object.

Capture
`/srv/forge/android/meizu_m6/captures/20260530-144231-m6-more-pokes-585e8a-711HEBSR277K5`
repeated the runtime test with wake/backlight forced to 255, red/green/blue/
white/gray DSI BIST patterns, three fb blank/unblank cycles, and three ATA
reads. FACT: the final `screencap` hung and produced a zero-byte PNG, but the
post-kill SurfaceFlinger dump still had the built-in 720x1280 display on with
`BootAnimation`, `powerMode=2`, `isDisplayOn=1`, HWC enabled, and `flips=5`.
FACT: DSI IRQs changed from `0 0 1 0` to `1 10 7 4` across the pokes, while
OVL/RDMA IRQ counters did not advance, so DSI commands are reaching the DSI
block but normal scanout is still stuck. FACT: all three ATA reads had the same
expected window bytes `0x0,0xb4,0x2,0x1c` and failed with the same DSI RX shape
seen earlier (`DSI_RX_DATA0=0x3300041c`, `DSI_RX_DATA1=0x00000000`).
FACT: `mtkfb-post-kill.txt` still reports `LCM Driver=[ili9881p_hd_dsi_txd]`,
`PathMode:DECOUPLE`, `RDMA0 Transfer=3`, `DISP_OPT_BYPASS_PQ=1`, and timeout
dumps with `VALID=0x0 READY=0x0`, `rdma0 GLOBAL=0x101 SIZE=720x1280`, and
`dsi0 START=0x1 MODE=0x1 TXRX=0x1003c PS=0x30870`.

FACT: stock Flyme reverse inputs were captured read-only from the live device
into `/srv/forge/android/meizu_m6/captures/20260530-stock-lk-boot-reverse-inputs`.
The key hashes are `lk.img`
sha256 `b32d7ae68c918195632faf730a5fd6fc0136e090c100f4fe6eddfba4c56746bc`,
`lk2.img` sha256
`7f2597d35ce8297145d27e51258c5d03d4044bb7085b2ba55e90a8907fa84708`, and
`logo.img` sha256
`dc832354260bd18240d8f626aa55bd2ff60879bfb76c32845e76317780967b24`.
FACT: stock Flyme boot/kernel inputs also exist under
`/srv/forge/android/meizu_m6/rom-lineage-15.1-meizu_m6-experimental/device/meizu/meizu_m6/prebuilt/stock-7.1.2.0G`.
LK strings include `ili9881p_hd_dsi_txd`, `ili9881c_hd_dsi_txd`,
`s6d7aa6_hd720_dsi_vdo_hlt`, `tps65132`, and `videolfb-lcmname`; the stock
kernel strings also expose `atag,videolfb-lcmname`, `atag,videolfb-islcm_inited`,
LCM bias/reset GPIO names, and an `nt35695_fhd_dsi_cmd_truly_nt50358_drv`
string. HYPOTHESIS: the next evidence-backed patch should come from stock
LK/boot parity for the selected panel, reset/bias sequence, DSI timing, or
init table; do not guess PLL/porches/panel driver solely from the current
source file.

Files changed: no kernel behavior changed in this diagnostic entry. This
`BRINGUP_STATE.md` section records the fresh evidence and rejects the stale
"PQ-only" path.

Why each file changed: the state file is the per-device bring-up journal for
the active M6 kernel tree. The captures prove the next patch must instrument or
fix LCM/DSI/panel state, not add more wait/fence/CMDQ token patches.

Expected next marker: the next diagnostic kernel should print the exact
`ili9881p_hd_dsi_txd` ATA expected bytes, read bytes, and return value when
`echo ata > /d/mtkfb` is run. A positive fix later should make at least one of
these change: visible DSI pattern, visible boot animation, valid 0x2A ATA
readback, DSI frame-done/IRQ progress, or nonzero scanout counters without the
current RDMA0 EOF timeout loop.

Rollback condition: do not keep using `esd_recovery` as a recovery step if it
again saturates CMDQ tasks or prevents fresh display captures. Revert the
planned ATA logging patch only if it changes panel behavior, blocks fb0/SF
startup, or makes the 0x2A ATA command hang instead of returning a packet.

Verification commands:

```bash
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell 'input keyevent KEYCODE_WAKEUP; echo 255 > /sys/class/leds/lcd-backlight/brightness'
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell 'echo ata > /d/mtkfb; cat /d/mtkfb' | grep -E 'ATA|DSI_RX|DSI_CMDQ|M6_DIAG'
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell 'echo dsipattern:0xff0000 > /d/mtkfb; sleep 1; echo dsipattern:0xffffff > /d/mtkfb; sleep 1; echo dsipattern:0x0 > /d/mtkfb'
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell cat /proc/interrupts | grep -E 'mtk_cmdq|ovl0|rdma0|dsi0|aal'
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell dmesg | grep -E 'M6_DIAG|M6 DDP timeout|RDMA0_EOF|primary_rdma0|DSI_RX|CMDQ_EVENT_DISP_RDMA0_EOF'
```

## 2026-05-30 stock Flyme LCM init parity

PATCH HISTORY, PROPER-FIX, 2026-05-30: restore the active
`ili9881p_hd_dsi_txd` init table, reset settle, and TPS65132 bias value to
stock Flyme LK/kernel values.

Hypothesis: the remaining black screen is a panel-init mismatch, not PQ,
SurfaceFlinger, framebuffer memory, or backlight. The current source driver
uses a heavily adapted `init_setting[]` that differs from both stock Flyme LK
and stock Flyme kernel for the same `ili9881p_hd_dsi_txd` driver. Because DSI
BIST patterns are invisible, ATA reads return a live but wrong 0x2A payload,
and RDMA0 EOF persists with userspace composition alive, the panel is likely in
the wrong command/video state before scanout starts.

Evidence: active failing artifact is still PQ-bypass boot
`/srv/forge/android/export/meizu_m6_artifacts/20260530-m6-pq-bypass-isolation/boot-pq-bypass-mkbootimg-abs.img`
sha256 `585e8a710a5b6ee2c96e6d995d2879d9fa63dca8321c9a4f15c9fcdb41c1f739`.
Fresh capture
`/srv/forge/android/meizu_m6/captures/20260530-144231-m6-more-pokes-585e8a-711HEBSR277K5`
shows DSI IRQs advancing under command pokes while OVL/RDMA IRQs do not advance,
three repeated ATA failures with expected bytes `00 b4 02 1c`, and
`DISP_OPT_BYPASS_PQ=1`. Stock Flyme LK
`/srv/forge/android/meizu_m6/captures/20260530-stock-lk-boot-reverse-inputs/lk.img`
sha256 `b32d7ae68c918195632faf730a5fd6fc0136e090c100f4fe6eddfba4c56746bc`
contains the `ili9881p_hd_dsi_txd` LCM driver and a 201-entry init table at LK
file offset `0x5b2d4`. That table is `14472` bytes and has sha256
`4fb6a6aaffb188dd2cfda5fba1b4d3eceba3279df8bbf6669d59bfbdb56caa8e`.
The read-only reverse sidecar found the same table in the stock Flyme kernel at
decompressed offset `0x1122cf0`; stock `boot.img` sha256 is
`8c0f2a4886b3b681f8275192e07fd529c62e7f37538e58f81f6466301db99915`, and stock
kernel sha256 is
`1d99d0a8dd3ca483192e01589cc567883ccf6af57d825be2db3a9a30a5f8e362`.
Rebuilt artifact directory:
`/srv/forge/android/export/meizu_m6_artifacts/20260530-m6-stock-flyme-lcm-parity/`.
Boot image `boot-stock-flyme-lcm-parity.img` sha256
`63072892444f43997a9bf2d57a5ae869486dd434d5b3321edadd8076cdf4f3e8`,
md5 `f0c3ba5d230cfb504ce64eff363ada9e`, size `8876032`. Kernel payload
`Image.gz-dtb` sha256
`0e222fdd97fe403c92ddccc76a2c80aaa4d983652724d019fe131465d0b65de7`,
md5 `b375a9c7a75fc0d639f1430b59a219f8`. `System.map` sha256
`51afd81bbac958e8c4cb60daa7f48ef9bd3648a5d42eab1786846c4f18714bab`;
kernel config sha256
`bc272726035c1a2eca9422e9bc230cf54f8295648a8865a98faba046ab01619e`.

Files changed: `kernel-3.18/drivers/misc/mediatek/lcm/ili9881p_hd_dsi_txd/ili9881p_hd_dsi_txd.c`
keeps the previous source-port init table under `#if 0`, activates the exact
stock Flyme LK/kernel table, changes TPS65132 register 0/1 bias writes from
`0x0e` to stock `0x0f`, and changes the reset-high settle from the previous
120 ms experiment to the stock `1/2/6 ms` reset sequence. `BRINGUP_STATE.md`
records the evidence and rollback condition.

Why each file changed: the LCM file owns the selected panel command sequence,
power bias writes, and reset timing. Stock source wins over donor/adapted source
for panel register programming, so the active table and reset/bias sequence now
match the hardware baseline. The state file is the per-device bring-up journal
for this kernel tree.

Expected next marker: after flashing the rebuilt boot image, the panel should
either show LK/boot animation pixels or at least change the lower-level markers:
DSI BIST patterns visible, `echo ata > /d/mtkfb` returns the expected
`00 b4 02 1c` readback, RDMA/DSI frame IRQs increase beyond the current stuck
counts, or the `VALID=0 READY=0` RDMA0 EOF timeout shape changes.

Rollback condition: revert this patch if a verified rebuilt artifact with the
stock table still has a physically black screen, still fails ATA with the same
wrong DSI RX packet, and still shows unchanged OVL/RDMA/DSI counters and RDMA0
EOF timeout shape. Also revert if the device regresses before fb0,
SurfaceFlinger, or ADB are available.

Verification commands:

```bash
cd /srv/forge/android/meizu_m6/kernel-meizu_M6-N-ex6-linux-3.18.140
git diff --check -- kernel-3.18/drivers/misc/mediatek/lcm/ili9881p_hd_dsi_txd/ili9881p_hd_dsi_txd.c BRINGUP_STATE.md
sha256sum /srv/forge/android/export/meizu_m6_artifacts/20260530-m6-stock-flyme-lcm-parity/boot-stock-flyme-lcm-parity.img /srv/forge/android/export/meizu_m6_artifacts/20260530-m6-stock-flyme-lcm-parity/Image.gz-dtb /srv/forge/android/export/meizu_m6_artifacts/20260530-m6-stock-flyme-lcm-parity/System.map /srv/forge/android/export/meizu_m6_artifacts/20260530-m6-stock-flyme-lcm-parity/config
python3 - <<'PY'
from pathlib import Path
from hashlib import sha256
p=Path('/srv/forge/android/meizu_m6/captures/20260530-stock-lk-boot-reverse-inputs/lk.img')
d=p.read_bytes()[0x5b2d4:0x5b2d4 + 201*72]
print(len(d), sha256(d).hexdigest())
PY
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell 'echo ata > /d/mtkfb; cat /d/mtkfb' | grep -E 'ATA|DSI_RX|DSI_CMDQ|ili9881p'
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell cat /proc/interrupts | grep -E 'mtk_cmdq|ovl0|rdma0|dsi0|aal'
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell dmesg | grep -E 'ili9881p|tps6132|M6 DDP timeout|RDMA0_EOF|DSI_RX'
```

## 2026-05-30 charger DTS bind fix after stock LCM test

FACT: Flashed stock-LCM-parity boot identity was verified by boot-partition
prefix hash. Capture
`/srv/forge/android/meizu_m6/captures/20260530-m6-stock-lcm-postboot-630728-711HEBSR277K5`
matches local boot image
`/srv/forge/android/export/meizu_m6_artifacts/20260530-m6-stock-flyme-lcm-parity/boot-stock-flyme-lcm-parity.img`
sha256 `63072892444f43997a9bf2d57a5ae869486dd434d5b3321edadd8076cdf4f3e8`.

FACT: Stock Flyme LCM parity did not fix the physical black screen. The same
capture still shows SurfaceFlinger with a 720x1280 built-in display,
`BootAnimation`, `powerMode=2`, and `flips=5`, while DDP remains stuck below
scanout: `CMDQ_EVENT_DISP_RDMA0_EOF` token 0, `M0_MOD=0x1df280`,
`rdma0 GLOBAL=0x101 SIZE=720x1280`, `dsi0 START=0x1 STA=0x20`, and
`DISP_OPT_BYPASS_PQ=1`. Runtime marker pokes forced lcd-backlight to 255 and
ran red/green/blue/white DSI patterns without a reported visible image.

FACT: The same boot is battery-critical. Capture
`/srv/forge/android/meizu_m6/captures/20260530-m6-charger-live-630728-711HEBSR277K5`
records `capacity=1`, `status=Not charging`, `usb online=0`, and
`ac online=0` in `power_supply.txt`. Its `dmesg.txt` repeatedly reports
`is_chr_det]vbus:4433-4450 chrdet:1`, proving VBUS detection while the Linux
power-supply state still reports no charging.

FACT: Runtime I2C device `1-006a` is instantiated from the generated
`swithing_charger@6a` node, not the BQ24157 node: `name=swithing_charger`,
`modalias=i2c:swithing_charger`, `OF_COMPATIBLE_0=mediatek,swithing_charger`,
and no driver symlink is present. The built DTB from the active build out-dir
contains `swithing_charger@6a { compatible = "mediatek,swithing_charger";
status = "okay"; }` and a second `bq24157@6a` node. The source BQ24157 driver
is compiled via `drivers/misc/mediatek/power/mt6755/Makefile` and matches
`ti,bq2415x`, `ti,bq24157`, and `bq24157`, so the current failure is DT binding
selection, not absence of the driver object.

PATCH HISTORY, PROPER-FIX, 2026-05-30: disable the generated
`swithing_charger@6a` placeholder and enable the stock BQ24157 node on i2c1.

Hypothesis: the device is not charging because DrvGen/DCT emits a generic
`mediatek,swithing_charger` child at the real charger address `i2c1-006a`.
That placeholder wins runtime instantiation and has no bound driver, while the
real stock/Q-tree BQ24157 node is disabled. Disabling the placeholder overlay
and enabling `bq24157@6a` should bind `/sys/bus/i2c/drivers/bq2415x` to
`1-006a` and let the charger driver report USB/AC charging state before the
1% battery shuts the device down.

Evidence: capture paths and runtime/DTB facts above; donor source trees
`kernel-meizu_M6-Q-ex2-3.18.119` and `kernel-meizu_M6-N-ex6` both keep
`bq24157@6a` enabled with the same `ti,bq2415x` properties for this board.

Files changed: `kernel-3.18/arch/arm64/boot/dts/meizu_m6.dts` overrides the
generated `swithing_charger@6a` node to `disabled` and keeps the stock
`bq24157@6a` node enabled; `BRINGUP_STATE.md` records the evidence, expected
marker, and rollback.

Why each file changed: `meizu_m6.dts` includes generated `cust.dtsi` before
the board overrides, so the board DTS is the narrowest place to override the
wrong generated node and select the stock charger node without editing
generated output or changing charger driver code.

Expected next marker: rebuilt DTB should show `swithing_charger@6a` disabled
and `bq24157@6a` okay. After flashing, `/sys/bus/i2c/devices/1-006a/name`
should be `bq24157` or `bq2415x`, `/sys/bus/i2c/devices/1-006a/driver` should
point to `.../drivers/bq2415x`, and `power_supply.txt` should stop reporting
the contradictory `chrdet:1` plus `Not charging`/`online=0` state.

Rollback condition: revert this DTS override if a verified rebuilt artifact
still instantiates `swithing_charger@6a`, or if BQ24157 binds but immediately
fails DT parsing, panics, or regresses USB/ADB before userspace capture.

Verification commands:

```bash
cd /srv/forge/android/meizu_m6/kernel-meizu_M6-N-ex6-linux-3.18.140
dtc -I dtb -O dts /home/n8n/forge-work/kernel-builds/m6-pq-bypass-20260530/out/arch/arm64/boot/dts/meizu_m6.dtb | grep -A5 -B2 -E 'swithing_charger@6a|bq24157@6a'
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell 'cat /sys/bus/i2c/devices/1-006a/name; cat /sys/bus/i2c/devices/1-006a/modalias; readlink /sys/bus/i2c/devices/1-006a/driver || true'
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell 'for p in /sys/class/power_supply/*; do echo ===$p===; cat $p/type 2>/dev/null; cat $p/online 2>/dev/null; cat $p/status 2>/dev/null; cat $p/capacity 2>/dev/null; done'
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell dmesg | grep -E 'bq2415|bq24157|swithing_charger|is_chr_det|charger'
```

Build/flash result: `Image.gz-dtb` rebuilt successfully in
`/home/n8n/forge-work/kernel-builds/m6-pq-bypass-20260530/out` with sha256
`e5191b15866aacefb82e928f0365e68f345f60af323354f50c3ada1fd7f620f1`.
Compiled DTB verification shows `swithing_charger@6a status = "disabled"` and
`bq24157@6a status = "okay"`. Repacked boot image
`/srv/forge/android/export/meizu_m6_artifacts/20260530-m6-bq24157-dts-bind-fix/boot-m6-bq24157-dts-bind-fix.img`
has sha256 `58914607637ad0d0ef93a4dab079c9f4d7b2d310408dd355dab855dfa13d7e82`
and size `8876032`.

FACT: The boot partition first `8876032` bytes were written and read back as
sha256 `58914607637ad0d0ef93a4dab079c9f4d7b2d310408dd355dab855dfa13d7e82`.
After `adb reboot`, the device did not return to ADB within the wait window and
is absent from `adb devices`, `fastboot devices`, and local `lsusb`. Because the
pre-flash live capture had `capacity=1` and `status=Not charging`, the next
required human step is physical charging/reconnecting before judging this boot
artifact. Do not infer a kernel boot regression from this absence until USB or
recovery/pstore is visible again.

PATCH HISTORY, ISOLATION, 2026-05-30: switch the active ili9881p LCM driver to
the true stock Flyme command/timing profile extracted from the stock kernel.

Hypothesis: the active source LCM profile is not the stock hardware profile for
this M6 panel. The current source table had a longer non-stock initialization
cluster and SYNC_PULSE video timings, while the stock Flyme kernel contains a
72-entry `ili9881p_hd_dsi_txd` initialization cluster, a 4-entry suspend
cluster, burst video mode, different porches, 230 MHz PLL, physical size
68000x121000 um, and an `0xF2 == 0x10` compare-id probe after selecting page 6.
If the panel stays black because DSI is streaming after an incompatible LCM
setup, using the stock LCM profile should either make the panel visible or move
the next capture to a different, more specific DSI/RDMA marker.

Evidence: latest verified live display capture
`/srv/forge/android/meizu_m6/captures/20260530-183857-m6-live-after-replug-589146-711HEBSR277K5`
used boot image sha256 prefix `589146...` and still showed a black panel even
though fb marker writes/readback worked, SurfaceFlinger saw 720x1280, backlight
was 255, DDP direct route was `OVL0 -> COLOR0 -> DITHER -> RDMA0 -> DSI0`,
`DISP_OPT_BYPASS_PQ=1`, and the persistent timeout remained
`CMDQ_EVENT_DISP_RDMA0_EOF`. Stock binary extraction from
`/tmp/m6_stock_7.1.2.0G_kernel.Image` found the true init table at offset
`0x11270f8`, size `72*72`, sha256
`6c9bcff880a3b65c19859b8cf506cf0d750055945d040a982fa9016eb4525a41`; and
the suspend table at offset `0x1128538`, size `4*72`, sha256
`4b556fdd2357b10854ce8f81868005459d83f39c4cb9b7559903c2f1d6e7d727`.

Files changed:

- `kernel-3.18/drivers/misc/mediatek/lcm/ili9881p_hd_dsi_txd/ili9881p_hd_dsi_txd.c`
  replaces the source init/suspend command tables with the stock 72-entry and
  4-entry clusters; changes physical size, DSI mode/timings/PLL, ESD enable,
  compare-id probe, and C2V switch mode to match the stock profile.
- `BRINGUP_STATE.md` records the evidence, artifact identity, next marker, and
  rollback condition.

Why each file changed: the LCM driver owns the panel command table and DSI
mode/timing contract, so it is the narrowest kernel-side isolation point for
stock display parity. The state file is updated because this is a bootable
display isolation checkpoint and must be reusable by the next agent.

Expected next marker: after flashing
`/srv/forge/android/export/meizu_m6_artifacts/20260530-m6-stock-lcm-parity-72entry/boot-m6-stock-lcm-parity-72entry.img`,
the boot partition readback should match sha256
`7be765bd7aa180ace519db0ba4fe6a9c27b6f27b3138364a3ec4fa8246b0e0f2`.
If the compare-id path is called, dmesg should include
`ili9881p_hd_dsi_txd_f2_id=0x00000010`. A successful display result is visible
boot animation or framebuffer markers on panel. If still black, collect the
same DDP/DSI/RDMA timeout markers and compare whether the timeout remains
`CMDQ_EVENT_DISP_RDMA0_EOF`, whether DSI `STA`, RDMA IRQ/frame-done counters,
or ESD/TE markers changed.

Rollback condition: revert this LCM parity patch if a verified flash regresses
before fb0/SurfaceFlinger, introduces repeated DCS/ESD panel timeouts, prevents
ADB capture compared with the `589146...` charger/display baseline, or if
stock LK/boot reverse evidence proves this is not the stock M6 ili9881p panel
profile for serial `711HEBSR277K5`.

Verification commands:

```bash
cd /srv/forge/android/meizu_m6/kernel-meizu_M6-N-ex6-linux-3.18.140
sha256sum -c /srv/forge/android/export/meizu_m6_artifacts/20260530-m6-stock-lcm-parity-72entry/SHA256SUMS
adb -H 127.0.0.1 -P 15038 devices -l
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 push /srv/forge/android/export/meizu_m6_artifacts/20260530-m6-stock-lcm-parity-72entry/boot-m6-stock-lcm-parity-72entry.img /data/local/tmp/boot.img
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell 'su -c "dd if=/data/local/tmp/boot.img of=/dev/block/platform/mtk-msdc.0/11230000.msdc0/by-name/boot bs=1048576 conv=fsync"'
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 exec-out su -c 'dd if=/dev/block/platform/mtk-msdc.0/11230000.msdc0/by-name/boot bs=8876032 count=1 2>/dev/null' | sha256sum
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 reboot
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 wait-for-device
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell dmesg | grep -E 'ili9881p_hd_dsi_txd|f2_id|CMDQ_EVENT_DISP_RDMA0_EOF|M6 DDP timeout|dsi0|rdma0'
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell 'cat /proc/interrupts; cat /sys/class/leds/lcd-backlight/brightness 2>/dev/null'
```

Build/artifact result: branch `work/m6-stock-lcm-parity-20260530` built
successfully in
`/home/n8n/forge-work/kernel-builds/m6-stock-lcm-parity-20260530/out`.
Repacked artifact
`/srv/forge/android/export/meizu_m6_artifacts/20260530-m6-stock-lcm-parity-72entry/boot-m6-stock-lcm-parity-72entry.img`
has sha256 `7be765bd7aa180ace519db0ba4fe6a9c27b6f27b3138364a3ec4fa8246b0e0f2`
and size `8876032`; `Image.gz-dtb` sha256 is
`90ef931093dc8e01a2068016ab0f966f465802f41b3bfab32d60aefb69a33bfe`; the
matching `System.map` sha256 is
`113cf396e1a0bc07d6fa21291d2108f787527d7f1b66b582174e02b71b1601ad`.
`sha256sum -c SHA256SUMS` passed. The image is not flashed yet because
`711HEBSR277K5` is not currently visible on the ADB tunnel; visible devices are
nx549j `30785d1a`, m2note `810BBMM22D7S`, and m681 `91HEBNL163XD`.

Runtime result: `boot-m6-stock-lcm-parity-72entry.img` was flashed from
recovery on serial `711HEBSR277K5` and verified by boot partition readback in
`/srv/forge/android/meizu_m6/captures/20260531-013053-m6-stock72-preflash-711HEBSR277K5`.
The runtime capture
`/srv/forge/android/meizu_m6/captures/20260531-013528-m6-stock72-runtime-7be765-711HEBSR277K5`
verified boot sha256
`7be765bd7aa180ace519db0ba4fe6a9c27b6f27b3138364a3ec4fa8246b0e0f2`.
The panel was still not proven visible: DSI/LCM stock parity is present, but
`M6 DDP timeout[VSYNC]` persists, route is still direct, `DISP_OPT_BYPASS_PQ=1`,
`M0_MOD=0x1df280`, `RDMA0 underflow` appears at boot, and the timeout dump
still shows `OVL SRC=0x9` with L0+L3 enabled. `fb0-before.raw` and
`fb0-after-marker.raw` matched each other, not the marker raw, because
SurfaceFlinger/BootAnimation was continuously compositing; this means fb raw
marker persistence is no longer a reliable standalone success marker in this
boot state.

PATCH HISTORY, ISOLATION, 2026-05-31: narrow primary display mutex membership
under the M6 direct-route/PQ-bypass path.

Hypothesis: after stock LCM parity, the earliest remaining display blocker is
not the LCM table or PQ module programming itself. The active route is already
`OVL0 -> COLOR0 -> DITHER -> RDMA0 -> DSI0`, clocks are ungated, and
`DISP_OPT_BYPASS_PQ=1`, but mutex0 still waits on non-routed `OVL0_2L` /
`OVL1_2L` and bypassed PQ blocks `CCORR` / `AAL` / `GAMMA`. Narrowing only the
mutex mask, while leaving path lists, clocks, and module power sequencing
unchanged, should test whether the frame is blocked by over-broad mutex
membership rather than by the physical DSI stream.

Evidence: runtime capture
`/srv/forge/android/meizu_m6/captures/20260531-013528-m6-stock72-runtime-7be765-711HEBSR277K5`
shows verified boot sha256 `7be765...`, LCM driver `ili9881p_hd_dsi_txd`, direct
route registers (`OVL0_MOUT=0x1 COLOR0_SEL=0x1 DITHER_MOUT=0x1
RDMA0_SOUT=0x2 DSI0_SEL=0x1`), `DISP_OPT_BYPASS_PQ=1`, ungated scanout clocks,
`RDMA0 underflow`, `M6 DDP timeout[VSYNC]`, and `mutex ... M0_MOD=0x1df280`.
The decoded `M0_MOD` includes bits for OVL0, OVL0_2L, OVL1_2L, RDMA0, COLOR0,
CCORR, AAL, GAMMA, DITHER, and PWM0 even though the active route does not use
the 2L overlay engines and PQ blocks are explicitly bypassed.

Files changed:

- `kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_path.c` adds an M6
  direct-route mutex isolation helper and clears the mutex bits for OVL0_2L,
  OVL1_2L, CCORR, AAL, and GAMMA only when setting `DDP_SCENARIO_PRIMARY_DISP`
  while `DISP_OPT_BYPASS_PQ` is enabled. The path list and power/connect
  sequence are left intact.
- `BRINGUP_STATE.md` records the stock72 runtime result, this isolation patch,
  artifact identity, expected marker, rollback condition, and verification
  commands.

Why each file changed: `ddp_path.c` is where `M0_MOD` is generated from the
scenario module list. Clearing the mask after the normal mutex setup isolates
the suspected wait condition without disabling modules globally, removing them
from path/power sequencing, or changing DSI/LCM. The state file records the
evidence and next capture contract for rollback.

Expected next marker: after flashing
`/srv/forge/android/export/meizu_m6_artifacts/20260531-m6-mutex-route-isolation-51280/boot-m6-mutex-route-isolation-51280.img`,
the boot partition readback should match sha256
`a9c09d5848640febe04a42d6b2c6acd99ea774df1fab04827ba07b1d79b35517`.
Early dmesg should include `M6 DDP mutex isolate` and the timeout dump should
show `M0_MOD=0x51280` instead of `0x1df280`. A successful or useful runtime
change is visible image, moving RDMA0/frame-done/IRQ counters, disappearance of
`RDMA0 underflow`/`VSYNC` timeout, or a new earlier DSI/RDMA/OVL marker.

Rollback condition: revert this isolation if the readback-verified artifact
does not change `M0_MOD`, regresses before ADB/fb0/SurfaceFlinger compared with
the stock72 baseline, or if `M0_MOD=0x51280` appears but RDMA0 underflow,
VSYNC timeout, DSI state, and black-screen behavior are unchanged.

Verification commands:

```bash
cd /srv/forge/android/meizu_m6/kernel-meizu_M6-N-ex6-linux-3.18.140
sha256sum -c /srv/forge/android/export/meizu_m6_artifacts/20260531-m6-mutex-route-isolation-51280/SHA256SUMS
adb -H 127.0.0.1 -P 15038 devices -l
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 push /srv/forge/android/export/meizu_m6_artifacts/20260531-m6-mutex-route-isolation-51280/boot-m6-mutex-route-isolation-51280.img /data/local/tmp/boot.img
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell 'dd if=/data/local/tmp/boot.img of=/dev/block/platform/mtk-msdc.0/11230000.msdc0/by-name/boot bs=1048576 conv=fsync; sync'
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 exec-out 'dd if=/dev/block/platform/mtk-msdc.0/11230000.msdc0/by-name/boot bs=8876032 count=1 2>/dev/null' | sha256sum
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 reboot
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 wait-for-device
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell 'cat /d/mtkfb 2>&1; dmesg | grep -E "M6 DDP mutex isolate|M0_MOD|RDMA0 underflow|M6 DDP timeout|DISP_OPT_BYPASS_PQ|rdma0|dsi0"'
```

Build/artifact result: branch `work/m6-mutex-route-isolation-20260531` built
successfully in
`/home/n8n/forge-work/kernel-builds/m6-mutex-route-isolation-20260531/out`.
Repacked artifact
`/srv/forge/android/export/meizu_m6_artifacts/20260531-m6-mutex-route-isolation-51280/boot-m6-mutex-route-isolation-51280.img`
has sha256 `a9c09d5848640febe04a42d6b2c6acd99ea774df1fab04827ba07b1d79b35517`
and size `8876032`; `Image.gz-dtb` sha256 is
`98982e6c28dd75545302943c2b8ae82ea97754be9d15849d2e9525c0f89d0733`; the
matching `System.map` sha256 is
`e300615b7aa47f11edb508e63fc3f094c1021d20f374c5bf5f17f0bdb3f57c33`.
`sha256sum -c SHA256SUMS` passed and the kernel strings include
`M6 DDP mutex isolate`.

Runtime result: `boot-m6-mutex-route-isolation-51280.img` was flashed on serial
`711HEBSR277K5` and read back as sha256
`a9c09d5848640febe04a42d6b2c6acd99ea774df1fab04827ba07b1d79b35517`. Fresh
capture
`/srv/forge/android/meizu_m6/captures/20260531-083137-m6-mutex51280-runtime-a9c09d-711HEBSR277K5`
shows root ADB, `surfaceflinger=running`, boot animation exit `0`, boot mode
`normal`, SurfaceFlinger `Built-in Screen` `720x1280`, `powerMode=2`,
`isDisplayOn=1`, `flips=7`, and `VSYNC state: disabled`. User-visible result
remained a black physical panel. `fb0-after-marker.raw` matched the known BGRA
marker sha256 `4a8b635eff9c04bb4b6ceda435c3db3e3045f7200403bc49f6d52b10f1ebf7d5`,
so framebuffer writes persist in memory, but `screencap` produced empty files.
`/proc/interrupts` still had `dsi0` at zero before and after marker writes, and
dmesg was flooded by `IRQ: ovl0 frame underflow`, `L0 not complete until EOF`,
`L3 not complete until EOF`, `abnormal SOF`, and `hw reset done`.

FACT: The previous isolation log only proved the `DDP_SCENARIO_PRIMARY_DISP`
mutex path was filtered. Runtime also switches a handle from `primary_all` to
`primary_rdma0_color0_disp`, whose module list is `RDMA0`, `COLOR0`, `CCORR`,
`AAL`, `GAMMA`, `DITHER`, `UFOE`, `PWM0`, `DSI0`. Because the old helper did
not apply to `DDP_SCENARIO_PRIMARY_RDMA0_COLOR0_DISP`, the active scanout path
could still keep bypassed PQ modules in its mutex membership after the handle
switch.

PATCH HISTORY, ISOLATION, 2026-05-31: extend M6 direct-route/PQ mutex isolation
to the active RDMA0-COLOR0 display scenario.

Hypothesis: PQ is already bypassed as engines (`DISP_OPT_BYPASS_PQ=1`), but
the active `primary_rdma0_color0_disp` mutex path can still wait on PQ blocks
(`CCORR`, `AAL`, `GAMMA`) because the previous clear only covered
`PRIMARY_DISP`. Extending the same narrow mutex clear to
`PRIMARY_RDMA0_COLOR0_DISP` tests whether the remaining black screen is caused
by the active scanout path synchronizing on bypassed PQ modules after the route
switch. The patch also logs the queued value separately from the immediate
register read because a CMDQ-backed `DISP_REG_MASK(handle, ...)` can make the
post-write `DISP_REG_GET()` stale.

Evidence: the readback-verified `a9c09d...` capture above still has a black
panel with SurfaceFlinger ON, framebuffer marker memory persistence, dsi0 IRQ
count zero, VSYNC disabled, and OVL underflow/abnormal-SOF flooding. The source
scenario table for `PRIMARY_RDMA0_COLOR0_DISP` still contains `CCORR`, `AAL`,
and `GAMMA`, while the previous helper only returned true for
`DDP_SCENARIO_PRIMARY_DISP`.

Files changed:

- `kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_path.c` applies the M6
  direct-route/PQ mutex clear to both `DDP_SCENARIO_PRIMARY_DISP` and
  `DDP_SCENARIO_PRIMARY_RDMA0_COLOR0_DISP`, and logs `queued=`.
- `BRINGUP_STATE.md` records the verified `a9c09d...` runtime result, this
  patch, artifact identity, expected marker, rollback condition, and
  verification commands.

Why each file changed: `ddp_path.c` owns the scenario-to-mutex module mask and
is the narrowest place to isolate mutex membership without removing modules
from path lists, power sequencing, or DSI/LCM state. The state file is required
so the next capture can judge this isolation by artifact hash and log markers.

Expected next marker: after flashing
`/srv/forge/android/export/meizu_m6_artifacts/20260531-m6-rdma-color-mutex-isolation-51200/boot-m6-rdma-color-mutex-isolation-51200.img`,
the boot partition readback should match sha256
`69ea4dc9fc45761ead74c9935bb0af3b7be01da3e321bc36463845a5b45dafce`.
Early dmesg should include `M6 DDP mutex isolate` for both `primary_disp` and
`primary_rdma0_color0_disp`; the active RDMA0-COLOR0 path should report
`queued=0x51200` or a final timeout/debugfs mutex mask consistent with
`RDMA0|COLOR0|DITHER|UFOE|PWM0|DSI0`. A positive or useful result is visible
image, rising DSI/RDMA frame progress, VSYNC enabled, reduced OVL underflow
flooding, or a new earlier DSI/OVL/SMI marker.

Rollback condition: if the readback-verified artifact still has black physical
panel, `dsi0` IRQ zero, VSYNC disabled, OVL underflow/abnormal-SOF flooding,
and a correct queued/final mask for `primary_rdma0_color0_disp`, stop blaming
PQ/mutex membership and pivot to OVL layer fetch/config, SMI/LARB, or DSI video
acceptance rather than expanding PQ bypass further.

Verification commands:

```bash
cd /srv/forge/android/meizu_m6/kernel-meizu_M6-N-ex6-linux-3.18.140
sha256sum -c /srv/forge/android/export/meizu_m6_artifacts/20260531-m6-rdma-color-mutex-isolation-51200/SHA256SUMS
adb -H 127.0.0.1 -P 15038 devices -l
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 push /srv/forge/android/export/meizu_m6_artifacts/20260531-m6-rdma-color-mutex-isolation-51200/boot-m6-rdma-color-mutex-isolation-51200.img /data/local/tmp/boot.img
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell 'dd if=/data/local/tmp/boot.img of=/dev/block/platform/mtk-msdc.0/11230000.msdc0/by-name/boot bs=1048576; sync'
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 exec-out 'dd if=/dev/block/platform/mtk-msdc.0/11230000.msdc0/by-name/boot bs=8876032 count=1 2>/dev/null' | sha256sum
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 reboot
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 wait-for-device
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell 'cat /d/mtkfb 2>&1; dmesg | grep -E "M6 DDP mutex isolate|primary_rdma0_color0_disp|M0_MOD|queued=0x51200|RDMA0 underflow|M6 DDP timeout|DISP_OPT_BYPASS_PQ|ovl0|rdma0|dsi0"'
```

Build/artifact result: branch `work/m6-rdma-color-mutex-isolation-20260531`
built successfully in
`/home/n8n/forge-work/kernel-builds/m6-rdma-color-mutex-isolation-20260531/out`
using the previous verified `.config` from the `a9c09d...` build to avoid the
unrelated clean-debug `CONFIG_KGDB_KDB` compile blocker. Repacked artifact
`/srv/forge/android/export/meizu_m6_artifacts/20260531-m6-rdma-color-mutex-isolation-51200/boot-m6-rdma-color-mutex-isolation-51200.img`
has sha256 `69ea4dc9fc45761ead74c9935bb0af3b7be01da3e321bc36463845a5b45dafce`
and size `8876032`; `Image.gz-dtb` sha256 is
`1d036e8e1574eef7963434e98b75cfff4df24b7bd88101c4b5fff20d0ebd1d79`; the
matching `System.map` sha256 is
`bb2b188e17ba68ec76bf0b5c0d57f2aca2325e74958bc79155b8cf8f44ed3459`; the
matching `config` sha256 is
`bc272726035c1a2eca9422e9bc230cf54f8295648a8865a98faba046ab01619e`.
`sha256sum -c SHA256SUMS` passed, `abootimg -i` preserved the previous boot
geometry/header, and kernel strings include `M6 DDP mutex isolate` with
`queued=`.

Runtime result: `boot-m6-rdma-color-mutex-isolation-51200.img` was flashed on
serial `711HEBSR277K5` and read back as sha256
`69ea4dc9fc45761ead74c9935bb0af3b7be01da3e321bc36463845a5b45dafce`. Fresh
capture
`/srv/forge/android/meizu_m6/captures/20260531-111210-m6-rdma-color51200-runtime-69ea4d-711HEBSR277K5`
shows root ADB, `surfaceflinger=running`, `service.bootanim.exit=0`, boot mode
`normal`, kernel compile time `Sun May 31 10:48:16 CDT 2026`, SurfaceFlinger
`Built-in Screen` `720x1280`, `powerMode=2`, `isDisplayOn=1`, `flips=17`, and
`VSYNC state: disabled`. The physical panel stayed black. `fb0-after-marker.raw`
matched the known BGRA marker sha256
`4a8b635eff9c04bb4b6ceda435c3db3e3045f7200403bc49f6d52b10f1ebf7d5`, but both
`screencap` files were empty. `/proc/interrupts` showed OVL0 and RDMA0 IRQs
rising after the marker write, while `dsi0` stayed at zero.

FACT: the active runtime path is no longer a PQ/mutex membership failure.
`dmesg-before-marker.txt` contains `M6 DDP mutex isolate:
scenario=primary_rdma0_color0_disp mutex=0 MOD 0x51280 queued=0x51280
now=0x51280 clear=0x18e000`. In this tree's module map, `0x51280` is
`OVL0|RDMA0|COLOR0|DITHER|PWM0`; the PQ blocks `CCORR`, `AAL`, and `GAMMA`
are bits `0x2000`, `0x4000`, and `0x8000` and are absent from the active mask.

REJECTED: expanding PQ/mutex clearing further. The `69ea4d...` artifact proves
that removing PQ bits from the active RDMA0-COLOR0 mutex path does not recover
visible scanout, DSI IRQs, VSYNC, or screencap output. Do not remove `COLOR0`,
`DITHER`, `PWM0`, OVL0, RDMA0, or DSI route state as a PQ follow-up.

FACT: the current timeout frontier is OVL/RDMA/DSI hardware state. The same
capture logs `PathMode:DECOUPLE`, `RDMA0 Transfer = 3`, `DSI_EXT_TE = 0`,
`DISP_OPT_BYPASS_PQ=1`, `VALID=0x0`, `RDMA0 IN=0/0 OUT=0/0`, `dsi0 START=0x1
STA=0x20 INTSTA=0x80000790 MODE=0x3`, and persistent `IRQ: ovl0 frame
underflow`, `L0 not complete until EOF`, `L3 not complete until EOF`,
`abnormal SOF`, `hw reset done` flooding. The timeout dump shows OVL0 L0 and
L3 enabled with `CON=0x27ff`, full-screen `SIZE=0x50002d0`, addresses
`0x9f707fbf` and `0x9fa8bfff`, and pitches `0x7ff0b80` and `0x7ff0b40`.

INFERENCE: the next useful patch is read-only OVL/RDMA diagnostics at layer
config time and timeout time, not another PQ bypass. The capture proves
framebuffer memory can be written, but OVL0 does not complete L0/L3 fetch before
EOF and RDMA0 does not see frame progress. The diagnostic should record the
OVL input config that produced the final registers, including layer id, enable,
format, source/destination rect, pitch, address/MVA, security/cache state, and
the decoded final OVL/RDMA/DSI registers near each timeout.

Expected next marker: a DIAGNOSTIC boot should still boot to root ADB and print
bounded `M6 OVL diag` lines for the active L0/L3 config before the first
underflow or timeout. The capture should make it possible to decide whether the
bad state is malformed layer configuration, invalid MVA/SMI fetch, or a
downstream RDMA/DSI acceptance issue.

Rollback condition: if the diagnostic materially changes timing, floods dmesg
so much that the original timeout lines are lost, or prevents root ADB, revert
the diagnostic and collect a smaller register-only snapshot.

PATCH HISTORY, DIAGNOSTIC, 2026-05-31: add bounded OVL layer config and timeout
decode logging.

Hypothesis: the active black-screen frontier is not PQ/mutex membership because
the readback-verified `69ea4d...` artifact removed PQ bits from the active
mutex mask while the physical panel stayed black, `dsi0` IRQs stayed at zero,
VSYNC stayed disabled, and OVL0 continued to report L0/L3 underflow/EOF
failure. The next evidence gap is whether OVL0 is being programmed with a bad
layer address, pitch, format, source rectangle, or downstream RDMA/DSI state.
This patch only records the computed OVL layer configuration and decoded
timeout registers so the next capture can distinguish malformed layer input
from SMI/MVA fetch failure or downstream DSI acceptance failure.

Evidence: capture
`/srv/forge/android/meizu_m6/captures/20260531-111210-m6-rdma-color51200-runtime-69ea4d-711HEBSR277K5`
is readback-verified against boot sha256
`69ea4dc9fc45761ead74c9935bb0af3b7be01da3e321bc36463845a5b45dafce`; matching
`System.map` is
`/srv/forge/android/export/meizu_m6_artifacts/20260531-m6-rdma-color-mutex-isolation-51200/System.map`
sha256 `bb2b188e17ba68ec76bf0b5c0d57f2aca2325e74958bc79155b8cf8f44ed3459`.
The capture logs `M6 DDP mutex isolate:
scenario=primary_rdma0_color0_disp ... MOD 0x51280 queued=0x51280
now=0x51280 clear=0x18e000`, `VALID=0x0`, `RDMA0 IN=0/0 OUT=0/0`, `dsi0
START=0x1 STA=0x20 INTSTA=0x80000790 MODE=0x3`, and repeated `IRQ: ovl0 frame
underflow`, `L0 not complete until EOF`, `L3 not complete until EOF`, and
`abnormal SOF`.

Files changed:

- `kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_ovl.c` adds bounded
  `M6 OVL diag cfg[...]` logging for OVL0 layer config and records the final
  address expression before programming the existing address register.
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_manager.c` adds
  read-only OVL0 L0-L3 decode lines to the existing `M6 DDP timeout[...]`
  dump.
- `BRINGUP_STATE.md` records this patch, artifact identity, expected marker,
  rollback condition, and verification commands.

Why each file changed: `ddp_ovl.c` owns the conversion from userspace/HWC layer
input to OVL0 registers, which is the first point that can explain bad full
screen L0/L3 fetch state. `ddp_manager.c` owns the proven timeout point and can
decode the live OVL/RDMA register state without writing hardware. The state
file is the required checkpoint so the next capture is judged by artifact hash
and marker lines, not by stale logs.

Expected next marker: after flashing
`/srv/forge/android/export/meizu_m6_artifacts/20260531-m6-ovl-layer-diag/boot-m6-ovl-layer-diag.img`,
the boot partition readback should match sha256
`5bb8b2875edd2385cb40ac50e44aaacfbee4c31e38f3900e94d6c8663e776cf4`.
Fresh dmesg should include bounded `M6 OVL diag cfg[...]` lines before or near
the first OVL underflow, plus timeout lines containing `ovl0 L%u decode`. A
useful result is either a visible image or enough OVL/RDMA state to classify
the next patch as layer config, MVA/SMI fetch, or downstream DSI video
acceptance.

Rollback condition: revert this diagnostic if a readback-verified flash of
`5bb8b287...` regresses before root ADB/fb0/SurfaceFlinger, loses the original
OVL/RDMA timeout markers due to log flood, or materially changes the failing
state without producing the bounded `M6 OVL diag cfg` evidence.

Verification commands:

```bash
cd /srv/forge/android/meizu_m6/kernel-meizu_M6-N-ex6-linux-3.18.140
sha256sum -c /srv/forge/android/export/meizu_m6_artifacts/20260531-m6-ovl-layer-diag/SHA256SUMS
(gzip -cd /srv/forge/android/export/meizu_m6_artifacts/20260531-m6-ovl-layer-diag/Image.gz-dtb 2>/dev/null || true) | strings | grep -E 'M6 OVL diag cfg|M6 DDP timeout'
adb -H 127.0.0.1 -P 15038 devices -l
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 push /srv/forge/android/export/meizu_m6_artifacts/20260531-m6-ovl-layer-diag/boot-m6-ovl-layer-diag.img /data/local/tmp/boot.img
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell 'dd if=/data/local/tmp/boot.img of=/dev/block/platform/mtk-msdc.0/11230000.msdc0/by-name/boot bs=1048576; sync'
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 exec-out 'dd if=/dev/block/platform/mtk-msdc.0/11230000.msdc0/by-name/boot bs=8876032 count=1 2>/dev/null' | sha256sum
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 reboot
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 wait-for-device
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell 'dmesg | grep -E "M6 OVL diag cfg|ovl0 L[0-3] decode|M6 DDP timeout|IRQ: ovl0 frame underflow|not complete until EOF|abnormal SOF|RDMA0|dsi0"'
```

Build/artifact result: branch `work/m6-ovl-layer-diag-20260531` built
successfully in
`/home/n8n/forge-work/kernel-builds/m6-ovl-layer-diag-20260531/out`.
Repacked artifact
`/srv/forge/android/export/meizu_m6_artifacts/20260531-m6-ovl-layer-diag/boot-m6-ovl-layer-diag.img`
has sha256 `5bb8b2875edd2385cb40ac50e44aaacfbee4c31e38f3900e94d6c8663e776cf4`
and size `8876032`; `Image.gz-dtb` sha256 is
`f57c7e4f5e3ec673e6b978a025c5735cbcf27fabc3663e0d15552859260e595c`; the
matching `System.map` sha256 is
`7ac771be7630d3167b917a2fefa75f31187a10d431f9f3ecc9747dae69672a04`; the
matching `config` sha256 is
`bc272726035c1a2eca9422e9bc230cf54f8295648a8865a98faba046ab01619e`.
`sha256sum -c SHA256SUMS` passed, `abootimg -i` preserved the previous boot
geometry/header, and kernel strings include `M6 OVL diag cfg` and
`M6 DDP timeout`.

Runtime result: `boot-m6-ovl-layer-diag.img` was flashed on serial
`711HEBSR277K5` and read back as sha256
`5bb8b2875edd2385cb40ac50e44aaacfbee4c31e38f3900e94d6c8663e776cf4`. Fresh
capture
`/srv/forge/android/meizu_m6/captures/20260531-115907-m6-ovl-layerdiag-runtime-5bb8b2-711HEBSR277K5`
shows root ADB, `surfaceflinger=running`, `service.bootanim.exit=0`, boot mode
`normal`, SurfaceFlinger `Built-in Screen` `720x1280`, `powerMode=2`,
`isDisplayOn=1`, and `flips=34`. The framebuffer marker persisted in memory:
`fb0-after-marker.raw` sha256 is
`4a8b635eff9c04bb4b6ceda435c3db3e3045f7200403bc49f6d52b10f1ebf7d5`, matching
the marker source; both `screencap` outputs were empty after the adb screencap
processes were killed. `/proc/interrupts` showed OVL0 and RDMA0 IRQs rising
after the marker write while `dsi0` stayed zero.

FACT: the new timeout dump proves the main interface route is still dependent
on OVL/PQ-class hardware after the system reports `PathMode:DECOUPLE`. The
capture logs `modify handle ... from primary_all to primary_rdma0_color0_disp`,
then later times out in `primary_rdma0_color0_disp` with `M0_MOD=0x51280`,
`OVL0_MOUT=0x2`, `COLOR0_SEL=0x1`, `DITHER_MOUT=0x1`, `RDMA0_SOUT=0x2`, and
`DSI0_SEL=0x1`. `0x51280` includes `OVL0|RDMA0|COLOR0|DITHER|PWM0`; it no
longer includes `CCORR|AAL|GAMMA`, but it still makes the interface wait on
OVL0. The same timeout has `RDMA0 MEM_START=0x0`, `IN=0/0`, `OUT=0/0`, and
`ovl0 SRC=0x9` with L0/L3 underflow. This is not a clean decoupled
`RDMA0 -> DSI0` scanout path.

INFERENCE: with `DISP_OPT_BYPASS_PQ=1` on this M6 runtime, the decoupled
interface path should use the existing `DDP_SCENARIO_PRIMARY_RDMA0_DISP`
instead of `DDP_SCENARIO_PRIMARY_RDMA0_COLOR0_DISP`. That removes
`COLOR0/DITHER/PQ` and OVL0 from the display mutex and lets the OVL path remain
only in the separate OVL-to-WDMA producer path. This is narrower than deleting
drivers or globally disabling engines because it changes only the scenario
chosen during the DL-to-DC/RDMA switch when PQ bypass is already active.

Expected next marker: a readback-verified boot of the next artifact should log
`M6 DDP decouple route: PQ bypass active, use primary_rdma0_disp`, then
`modify handle ... to primary_rdma0_disp`. If a VSYNC timeout remains, it should
say `primary_rdma0_disp`; mutex0 should no longer include OVL0/COLOR0/DITHER
(`M0_MOD` should be consistent with `RDMA0|PWM0`, normally `0x40200`), and
`RDMA0 MEM_START` should be nonzero with RDMA input/output counters either
moving or exposing a new downstream DSI marker.

Rollback condition: revert the next patch if the readback-verified artifact
regresses before root ADB/SurfaceFlinger/fb0, does not switch the handle to
`primary_rdma0_disp`, or switches to `primary_rdma0_disp` but still has black
panel, zero `dsi0` IRQs, no RDMA memory address/counter progress, and no new
lower-level DSI/RDMA marker compared with the `5bb8b287...` capture.

PATCH HISTORY, BOOT-UNBLOCK, 2026-05-31: use RDMA0-DISP for M6 decouple when
PQ bypass is active.

Hypothesis: the latest readback-verified diagnostic capture shows that the
decoupled display handle is named `primary_rdma0_color0_disp`, but the final
hardware state still couples interface scanout to OVL0/COLOR0/DITHER and
OVL0 L0/L3 underflow. Because this tree already sets `DISP_OPT_BYPASS_PQ=1`,
the direct RDMA interface path should use the existing
`DDP_SCENARIO_PRIMARY_RDMA0_DISP` instead of the COLOR/PQ scenario. That should
remove OVL0 and post-processing blocks from the display mutex and allow RDMA0
to scan out the decouple memory buffer directly to DSI0.

Evidence: capture
`/srv/forge/android/meizu_m6/captures/20260531-115907-m6-ovl-layerdiag-runtime-5bb8b2-711HEBSR277K5`
matches boot sha256
`5bb8b2875edd2385cb40ac50e44aaacfbee4c31e38f3900e94d6c8663e776cf4`; matching
`System.map` is
`/srv/forge/android/export/meizu_m6_artifacts/20260531-m6-ovl-layer-diag/System.map`
sha256 `7ac771be7630d3167b917a2fefa75f31187a10d431f9f3ecc9747dae69672a04`.
The capture shows `PathMode:DECOUPLE`, `modify handle ... from primary_all to
primary_rdma0_color0_disp`, timeout `M0_MOD=0x51280`, route
`OVL0_MOUT=0x2 COLOR0_SEL=0x1 DITHER_MOUT=0x1 RDMA0_SOUT=0x2 DSI0_SEL=0x1`,
`RDMA0 MEM_START=0x0 IN=0/0 OUT=0/0`, and `ovl0 SRC=0x9` with L0/L3 enabled
and underflowing.

Files changed:

- `kernel-3.18/drivers/misc/mediatek/video/mt6755/primary_display.c` adds a
  small M6 scenario selector for decouple/RDMA mode, uses
  `DDP_SCENARIO_PRIMARY_RDMA0_DISP` when `DISP_OPT_BYPASS_PQ` is active, and
  disconnects that scenario on resume alongside the existing RDMA0-COLOR path.
- `BRINGUP_STATE.md` records the diagnostic runtime result, this patch,
  artifact identity, expected marker, rollback condition, and verification
  commands.

Why each file changed: `primary_display.c` owns the DL-to-DC and DL-to-RDMA
scenario switch that produced the proven bad `primary_rdma0_color0_disp`
runtime state. Selecting an existing RDMA0-only scenario is the narrowest
change that removes PQ/COLOR/DITHER and OVL0 from the display-side mutex
without disabling OVL2MEM, DSI, LCM, clocks, or userspace composition.

Expected next marker: after flashing
`/srv/forge/android/export/meizu_m6_artifacts/20260531-m6-rdma0-disp-decpq/boot-m6-rdma0-disp-decpq.img`,
the boot partition readback should match sha256
`ee061f6b0896302704406e30f26dde2fbd77f10f6bd0f934623887a620a1e4f7`.
Fresh dmesg/debugfs should include `M6 DDP decouple route: PQ bypass active,
use primary_rdma0_disp` and `modify handle ... to primary_rdma0_disp`. A useful
positive result is visible image, nonzero DSI IRQs, VSYNC enabled, RDMA0
memory address/counter progress, or a new lower-level DSI/RDMA marker. If
timeout remains, `M0_MOD` should be consistent with `RDMA0|PWM0` rather than
`OVL0|RDMA0|COLOR0|DITHER|PWM0`.

Rollback condition: revert this patch if a readback-verified flash of
`ee061f6b...` regresses before root ADB/fb0/SurfaceFlinger, fails to switch to
`primary_rdma0_disp`, or switches there but still shows black panel, zero DSI
IRQs, no RDMA memory/counter progress, and no new downstream evidence.

Verification commands:

```bash
cd /srv/forge/android/meizu_m6/kernel-meizu_M6-N-ex6-linux-3.18.140
sha256sum -c /srv/forge/android/export/meizu_m6_artifacts/20260531-m6-rdma0-disp-decpq/SHA256SUMS
(gzip -cd /srv/forge/android/export/meizu_m6_artifacts/20260531-m6-rdma0-disp-decpq/Image.gz-dtb 2>/dev/null || true) | strings | grep -E 'M6 DDP decouple route|M6 DDP timeout|M6 OVL diag cfg'
adb -H 127.0.0.1 -P 15038 devices -l
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 push /srv/forge/android/export/meizu_m6_artifacts/20260531-m6-rdma0-disp-decpq/boot-m6-rdma0-disp-decpq.img /data/local/tmp/boot.img
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell 'dd if=/data/local/tmp/boot.img of=/dev/block/platform/mtk-msdc.0/11230000.msdc0/by-name/boot bs=1048576; sync'
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 exec-out 'dd if=/dev/block/platform/mtk-msdc.0/11230000.msdc0/by-name/boot bs=8876032 count=1 2>/dev/null' | sha256sum
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 reboot
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 wait-for-device
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell 'cat /d/mtkfb 2>&1; dmesg | grep -E "M6 DDP decouple route|primary_rdma0_disp|primary_rdma0_color0_disp|M0_MOD|M6 DDP timeout|RDMA0|dsi0|ovl0|VSYNC"'
```

Build/artifact result: branch `work/m6-rdma0-disp-decpq-20260531` built
successfully in
`/home/n8n/forge-work/kernel-builds/m6-rdma0-disp-decpq-20260531/out`.
Repacked artifact
`/srv/forge/android/export/meizu_m6_artifacts/20260531-m6-rdma0-disp-decpq/boot-m6-rdma0-disp-decpq.img`
has sha256 `ee061f6b0896302704406e30f26dde2fbd77f10f6bd0f934623887a620a1e4f7`
and size `8876032`; `Image.gz-dtb` sha256 is
`a4fe4daef29dbc9970ccfb34772d0fd9abb58e90f34cba8e048b6965deeaf8ed`; the
matching `System.map` sha256 is
`452397c4c39e72f6cb4296af5086e3476a1fc2514e1bd648a9e43e5d27bc7b78`; the
matching `config` sha256 is
`bc272726035c1a2eca9422e9bc230cf54f8295648a8865a98faba046ab01619e`.
`sha256sum -c SHA256SUMS` passed, `abootimg -i` preserved the previous boot
geometry/header, and kernel strings include `M6 DDP decouple route`,
`M6 OVL diag cfg`, and `M6 DDP timeout`.

Runtime result: `boot-m6-rdma0-disp-decpq.img` was flashed on serial
`711HEBSR277K5` and read back as sha256
`ee061f6b0896302704406e30f26dde2fbd77f10f6bd0f934623887a620a1e4f7` before
and after reboot. Fresh capture
`/srv/forge/android/meizu_m6/captures/20260531-124448-m6-rdma0-disp-decpq-runtime-ee061f-711HEBSR277K5`
shows root ADB, `surfaceflinger=running`, `init.svc.bootanim=running`,
`service.bootanim.exit=0`, SurfaceFlinger `Built-in Screen` `720x1280`,
`powerMode=2`, `isDisplayOn=1`, and `flips=10`. The framebuffer marker was
written through `/cache` and read back from `/dev/graphics/fb0` as sha256
`4a8b635eff9c04bb4b6ceda435c3db3e3045f7200403bc49f6d52b10f1ebf7d5`, but
the physical panel remained black and `screencap` produced an empty file.

FACT: the software scenario changed to `primary_rdma0_disp`, but the hardware
route and mutex did not. The capture logs `wait VSYNC timeout on scenario
primary_rdma0_disp` with `M0_MOD=0x51280`, `OVL0_MOUT=0x2`,
`COLOR0_SEL=0x1`, `DITHER_MOUT=0x1`, `RDMA0_SOUT=0x2`, `DSI0_SEL=0x1`,
`RDMA0 MEM_START=0x0 IN=0/0 OUT=0/0`, and `dsi0` IRQ count `0`. OVL0/RDMA0
IRQs increased across the marker write, while DSI0 stayed zero. This means the
previous patch selected the right scenario in software but did not get the
route/mutex command stream applied to hardware before the old path stalled.

INFERENCE: the route/mutex update is queued behind the existing
`_cmdq_insert_wait_frame_done_token_mira()` in the DL-to-DC/RDMA switch path.
Because the old OVL/COLOR/DITHER path is already stuck and never signals the
old frame-done token, the CMDQ packet that should change mutex0 to
`RDMA0|PWM0` and connect `RDMA0 -> DSI0` does not execute. The next patch must
skip that old frame-done wait only for the already-active M6 PQ-bypass
`primary_rdma0_disp` route.

PATCH HISTORY, BOOT-UNBLOCK, 2026-05-31: skip stale frame-done wait before the
M6 PQ-bypass RDMA0-DISP route update.

Hypothesis: with `DISP_OPT_BYPASS_PQ=1`, the DL-to-DC and DL-to-RDMA path
switches choose `DDP_SCENARIO_PRIMARY_RDMA0_DISP`, but they enqueue the
hardware route/mutex changes after a wait token belonging to the old
OVL/COLOR/DITHER path. Skipping that old wait token only for
`primary_rdma0_disp` should let CMDQ apply the existing route/mutex update and
RDMA memory config, so the next timeout, if any, reflects the true RDMA0-to-DSI
frontier instead of stale OVL/PQ hardware state.

Evidence: capture
`/srv/forge/android/meizu_m6/captures/20260531-124448-m6-rdma0-disp-decpq-runtime-ee061f-711HEBSR277K5`
matches boot sha256
`ee061f6b0896302704406e30f26dde2fbd77f10f6bd0f934623887a620a1e4f7`; matching
`System.map` is
`/srv/forge/android/export/meizu_m6_artifacts/20260531-m6-rdma0-disp-decpq/System.map`
sha256 `452397c4c39e72f6cb4296af5086e3476a1fc2514e1bd648a9e43e5d27bc7b78`.
The capture shows the timeout on `primary_rdma0_disp` while route/mutex
registers still match the old OVL/COLOR/DITHER path and `RDMA0 MEM_START=0`.

Files changed:

- `kernel-3.18/drivers/misc/mediatek/video/mt6755/primary_display.c` skips
  `_cmdq_insert_wait_frame_done_token_mira()` in `_DL_switch_to_DC_fast()` and
  `DL_switch_to_rdma_mode()` only when the selected new scenario is
  `DDP_SCENARIO_PRIMARY_RDMA0_DISP`, and logs the decision.
- `BRINGUP_STATE.md` records the verified runtime failure, hypothesis,
  rollback condition, and next verification route.

Why each file changed: `primary_display.c` owns the exact two switch paths that
select `primary_rdma0_disp` and queue the route/mutex update behind the old
frame-done token. The state file is required so the next agent judges the patch
against the readback-verified `ee061f...` runtime evidence rather than stale
logs.

Expected next marker: the next boot image should contain `M6 DDP decouple
route: skip old frame-done wait before primary_rdma0_disp`. A useful positive
result is visible image, nonzero DSI0 IRQs, VSYNC enabled, RDMA0
`MEM_START`/IN/OUT progress, or a new DSI/RDMA timeout marker. If VSYNC still
times out on `primary_rdma0_disp`, mutex0 should no longer be `0x51280`; it
should be consistent with the RDMA0/PWM0 path, and the route registers should
no longer describe `OVL0 -> COLOR0 -> DITHER -> RDMA0`.

Rollback condition: revert this patch if a readback-verified flash regresses
before root ADB/fb0/SurfaceFlinger, if the skip log appears but mutex/route
remain identical to the `ee061f...` capture, or if the panel remains black
with zero DSI0 IRQs and no new RDMA memory/counter or downstream DSI evidence.

Verification commands:

```bash
cd /srv/forge/android/meizu_m6/kernel-meizu_M6-N-ex6-linux-3.18.140
sha256sum -c <next-artifact-dir>/SHA256SUMS
(gzip -cd <next-artifact-dir>/Image.gz-dtb 2>/dev/null || true) | strings | grep -E 'skip old frame-done wait|M6 DDP decouple route|M6 DDP timeout'
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 push <next-artifact-dir>/boot-*.img /cache/boot.img
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 exec-out 'dd if=/cache/boot.img bs=<boot-size> count=1 2>/dev/null' | sha256sum
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell 'dd if=/cache/boot.img of=/dev/block/platform/mtk-msdc.0/11230000.msdc0/by-name/boot bs=1048576 conv=fsync; sync'
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 exec-out 'dd if=/dev/block/platform/mtk-msdc.0/11230000.msdc0/by-name/boot bs=<boot-size> count=1 2>/dev/null' | sha256sum
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 reboot
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 wait-for-device
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell 'cat /d/mtkfb 2>&1; dmesg | grep -E "skip old frame-done wait|primary_rdma0_disp|M0_MOD|M6 DDP timeout|RDMA0 MEM|dsi0|VSYNC|OVL0_MOUT|COLOR0_SEL|DITHER_MOUT|RDMA0_SOUT|DSI0_SEL"'
```

Build/artifact result: branch `work/m6-rdma0-disp-decpq-20260531` built
successfully in
`/home/n8n/forge-work/kernel-builds/m6-rdma0-disp-skipwait-20260531/out`.
Repacked artifact
`/srv/forge/android/export/meizu_m6_artifacts/20260531-m6-rdma0-disp-skipwait/boot-m6-rdma0-disp-skipwait.img`
has sha256 `c28afc0f5beaa5c492cd02629039d37e52f0e4f9f5e223d0626a38398c2d56e2`
and size `8876032`; `Image.gz-dtb` sha256 is
`4a3db86924c6b01550bd54fc9c9cc26a128c2398e5d118830d255112f4d82109`; the
matching `System.map` sha256 is
`a3e8727de0cdacffafc79ea7154a68f6eeef839e1afc4cd03911c1332b1cdf14`; the
matching `config` sha256 is
`bc272726035c1a2eca9422e9bc230cf54f8295648a8865a98faba046ab01619e`.
`sha256sum -c SHA256SUMS` passed, `abootimg -i` preserved the previous boot
geometry/header, and kernel strings include `M6 DDP decouple route: skip old
frame-done wait before primary_rdma0_disp`, `M6 DDP rdma mode: skip old
frame-done wait before primary_rdma0_disp`, `M6 OVL diag cfg`, and
`M6 DDP timeout`.

Runtime result: `boot-m6-rdma0-disp-skipwait.img` was flashed on serial
`711HEBSR277K5` only after the raw boot readback matched sha256
`c28afc0f5beaa5c492cd02629039d37e52f0e4f9f5e223d0626a38398c2d56e2`.
Fresh capture
`/srv/forge/android/meizu_m6/captures/20260531-130951-m6-rdma0-disp-skipwait-runtime-c28afc-711HEBSR277K5`
matches that boot hash and `System.map`
`/srv/forge/android/export/meizu_m6_artifacts/20260531-m6-rdma0-disp-skipwait/System.map`
sha256 `a3e8727de0cdacffafc79ea7154a68f6eeef839e1afc4cd03911c1332b1cdf14`.
Android reached root ADB, SurfaceFlinger, bootanimation, fb0 `720x1280`,
powerMode `2`, and `isDisplayOn=1`; the framebuffer marker read back from
`/dev/graphics/fb0` as sha256
`4a8b635eff9c04bb4b6ceda435c3db3e3045f7200403bc49f6d52b10f1ebf7d5`.
The physical panel remained black and `screencap` stayed empty.

FACT: the patch did not change the final display hardware state. The capture
still times out on `primary_rdma0_disp` with `M0_MOD=0x51280`,
`OVL0_MOUT=0x2`, `COLOR0_SEL=0x1`, `DITHER_MOUT=0x1`, `RDMA0_SOUT=0x2`,
`DSI0_SEL=0x1`, `RDMA0 MEM_START=0x0`, `IN=0/0`, `OUT=0/0`, and `dsi0` IRQs
remain zero. CMDQ thread dumps show thread 4 stuck on
`CMDQ_EVENT_DISP_WDMA0_EOF` and thread 7/thread 0 stuck on
`CMDQ_EVENT_DISP_RDMA0_EOF`, all token values zero. The mtkfb ring shows
`dl_to_dc capture:Flush wait wdma sof`, then
`modify handle ... from primary_all to primary_rdma0_disp`, then
`primary display is DECOUPLE mode now`; it does not show the new skip log in
the captured mtkfb/debugfs window.

INFERENCE: the software scenario transition completes, but the CMDQ path still
does not make the route/mutex/RDMA memory writes visible in hardware. The
earliest useful next test is not fake EOF seeding; it is to apply the already
selected M6 PQ-bypass `primary_rdma0_disp` route/mutex and first RDMA memory
config with CPU writes, then check whether `M0_MOD`, `RDMA0 MEM_START`, RDMA
counters, DSI IRQs, or a downstream DSI marker advances.

PATCH HISTORY, BOOT-UNBLOCK, 2026-05-31: CPU-apply the M6 PQ-bypass RDMA0-DISP
route and first RDMA config.

Hypothesis: the M6 PQ-bypass decouple switch selects `primary_rdma0_disp`, but
the necessary route/mutex and first RDMA memory config remain trapped behind
CMDQ tasks that are already waiting on zero-valued RDMA0/WDMA0 EOF events.
Applying only that selected route/mutex and first RDMA config with CPU register
writes should turn the stale `M0_MOD=0x51280` / `RDMA0 MEM_START=0` state into
a real `RDMA0 -> DSI0` memory scanout attempt without faking any EOF token.

Evidence: capture and hashes in the runtime result above. The decisive lines
are `wait VSYNC timeout on scenario primary_rdma0_disp`,
`M0_MOD=0x51280`, `RDMA0 MEM_START=0x0`, `dsi0 START=0x1`,
`dsi0` IRQ count zero, and CMDQ waits on `CMDQ_EVENT_DISP_WDMA0_EOF` and
`CMDQ_EVENT_DISP_RDMA0_EOF` with token value zero.

Files changed:

- `kernel-3.18/drivers/misc/mediatek/video/mt6755/primary_display.c` uses
  CPU writes, only when `DISP_OPT_BYPASS_PQ` already selected
  `DDP_SCENARIO_PRIMARY_RDMA0_DISP`, for `dpmgr_modify_path()` and the first
  RDMA memory config; it also leaves the post-switch config handle free of the
  stale RDMA0 EOF wait and logs the applied route/RDMA registers.
- `BRINGUP_STATE.md` records the readback-verified `c28afc` runtime result,
  the hypothesis, rollback condition, and verification route.

Why each file changed: `primary_display.c` owns the exact DL-to-DC transition
and decouple RDMA update that the fresh capture proves are not reaching
hardware through CMDQ. The patch does not seed EOF tokens, disable WDMA/RDMA,
or bypass DSI/LCM; it only changes the write transport for the selected
PQ-bypass RDMA0-DISP topology so the next capture can prove whether the
hardware path itself advances.

Expected next marker: the next kernel strings contain `M6 DDP decouple route:
CPU apply primary_rdma0_disp route/mutex`, `M6 DDP decouple route: CPU route`,
and `M6 DDP decouple rdma: MEM_START=0, CPU apply first RDMA config without
stale wait`. A useful positive result is visible image, nonzero DSI0 IRQs,
VSYNC enabled, `M0_MOD` consistent with RDMA0/PWM0 instead of `0x51280`,
nonzero `RDMA0 MEM_START`, or a new lower-level DSI/RDMA timeout marker.

Rollback condition: revert this patch if a readback-verified flash regresses
before root ADB/fb0/SurfaceFlinger, if the CPU-route logs appear but the next
timeout still shows `M0_MOD=0x51280` and `RDMA0 MEM_START=0`, or if it lights
no panel and exposes no new RDMA/DSI marker beyond the `c28afc` capture.

Verification commands:

```bash
sha256sum -c <next-artifact-dir>/SHA256SUMS
(gzip -cd <next-artifact-dir>/Image.gz-dtb 2>/dev/null || true) | strings | grep -E 'CPU apply primary_rdma0_disp|CPU route|CPU RDMA MEM|M6 DDP timeout'
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 push <next-artifact-dir>/boot-*.img /cache/boot.img
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 exec-out 'dd if=/cache/boot.img bs=<boot-size> count=1 2>/dev/null' | sha256sum
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell 'dd if=/cache/boot.img of=/dev/block/platform/mtk-msdc.0/11230000.msdc0/by-name/boot bs=1048576; sync'
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 exec-out 'dd if=/dev/block/platform/mtk-msdc.0/11230000.msdc0/by-name/boot bs=<boot-size> count=1 2>/dev/null' | sha256sum
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 reboot
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 wait-for-device
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell 'cat /d/mtkfb 2>&1; dmesg | grep -E "CPU apply primary_rdma0_disp|CPU route|CPU RDMA MEM|primary_rdma0_disp|M0_MOD|M6 DDP timeout|RDMA0 MEM|dsi0|VSYNC|RDMA0_SOUT|DSI0_SEL"'
```

Build/artifact result: branch `work/m6-rdma0-disp-decpq-20260531` built
successfully in
`/home/n8n/forge-work/kernel-builds/m6-rdma0-disp-cpuroute-20260531/out`.
Build log is
`/srv/forge/android/export/meizu_m6_artifacts/20260531-m6-rdma0-disp-cpuroute/build.log`
sha256 `0641c0998d9abdde57b84fda0d4fa532806d318fc4e1514308fa44933c477b4c`.
Repacked artifact
`/srv/forge/android/export/meizu_m6_artifacts/20260531-m6-rdma0-disp-cpuroute/boot-m6-rdma0-disp-cpuroute.img`
has sha256 `37ecde8c1a4abdfb4506106fa058c3ee7287562b5f17779518193f3daa43c9e8`
and size `8876032`; `Image.gz-dtb` sha256 is
`60bce88db9664b28c514ef75b898e0faf2c89201ff96e84e2cb5cb6463f6d435`; the
matching `System.map` sha256 is
`57d37db4b147280b7aa12b936b0e8ab1ba3aa2b2d42313d4499eefd76df20727`; the
matching `config` sha256 is
`bc272726035c1a2eca9422e9bc230cf54f8295648a8865a98faba046ab01619e`.
`sha256sum -c SHA256SUMS` passed, `abootimg -i` preserved the previous boot
geometry/header, and kernel strings include `M6 DDP decouple route: CPU apply
primary_rdma0_disp route/mutex`, `M6 DDP decouple route: CPU route`,
`M6 DDP decouple rdma: MEM_START=0, CPU apply first RDMA config without stale
wait`, `M6 DDP decouple rdma: CPU RDMA MEM`, and `M6 DDP timeout`.

Runtime result: `boot-m6-rdma0-disp-cpuroute.img` was flashed on serial
`711HEBSR277K5` only after the raw boot readback matched sha256
`37ecde8c1a4abdfb4506106fa058c3ee7287562b5f17779518193f3daa43c9e8`.
Fresh capture
`/srv/forge/android/meizu_m6/captures/20260531-134313-m6-rdma0-disp-cpuroute-runtime-37ecde-711HEBSR277K5`
matches that boot hash and was produced by kernel
`Linux version 3.18.140 ... #1 SMP PREEMPT Sun May 31 13:30:04 CDT 2026`.
Android reached root ADB, SurfaceFlinger, bootanimation, display state `ON`,
`powerMode=2`, `isDisplayOn=1`, and SurfaceFlinger `flips=61`. Backlight was
raised from `102` to `255`. The three framebuffer markers
`marker-rgb-stripes.raw`, `marker-checker.raw`, and `marker-white.raw` all
read back from `/dev/graphics/fb0` with matching sha256 values, and
`screencap` produced 720x1280 PNGs. The physical panel still stayed black.

FACT: the CPU route patch executed and advanced one register snapshot, but the
system later reverted to the old direct-link path. `dmesg-after-markers.txt`
shows `M6 DDP decouple route: CPU apply primary_rdma0_disp route/mutex` and
`CPU route M0_MOD=0x40200 M0_SOF=0x41 RDMA_MEM=0x400000/0x870
RDMA_SOUT=0x2 DSI0_SEL=0x1` at `356.256s`. It does not show the
`M6 DDP decouple rdma: CPU RDMA MEM` marker. At `398.095s` the display stack
logs `primary display will switch from DECOUPLE to DIRECT_LINK`, then
`modify handle ... from primary_rdma0_disp to primary_disp`; final timeout
snapshots are back to `M0_MOD=0x51280`, `RDMA0 MEM_START=0`, and DSI0 IRQs
remain zero even though OVL0/RDMA0 IRQ counts rise across marker writes.

INFERENCE: the previous patch proved CPU route writes can reach hardware, but
the `MEM_START == 0` guard suppressed the CPU RDMA update after route config
had already written a placeholder RDMA address. Then `smart_ovl` switched the
session back to DIRECT_LINK, recreating the stale OVL/COLOR/DITHER path. The
next minimal test should keep the active PQ-bypass experiment in DECOUPLE and
CPU-apply each RDMA config while `primary_rdma0_disp` remains selected.

PATCH HISTORY, BOOT-UNBLOCK, 2026-05-31: pin the M6 PQ-bypass decouple route
and CPU-apply every RDMA update.

Hypothesis: with `DISP_OPT_BYPASS_PQ=1`, the selected experiment is
`primary_rdma0_disp`, but the direct-link smart-OVL fallback restores the
broken PQ/OVL route before scanout can prove the RDMA0-to-DSI frontier. Also,
the first CPU route config makes `RDMA_MEM` nonzero before
`_decouple_update_rdma_config_nolock()`, so the previous `MEM_START == 0`
condition prevents the CPU RDMA config from running. Keeping DECOUPLE pinned
for PQ-bypass and CPU-applying every RDMA update should produce persistent
`M0_MOD=0x40200`, visible `CPU RDMA MEM`, and either an image or a lower
DSI/RDMA failure marker.

Evidence: capture and hashes in the runtime result above. Decisive markers are
the CPU route line with `M0_MOD=0x40200`, absence of `CPU RDMA MEM`, the later
`DECOUPLE to DIRECT_LINK` switch, final `M0_MOD=0x51280`, final
`RDMA0 MEM_START=0`, and DSI0 IRQ counts staying zero across all three
framebuffer marker writes.

Files changed:

- `kernel-3.18/drivers/misc/mediatek/video/mt6755/primary_display.c` removes
  the `MEM_START == 0` gate from the M6 PQ-bypass RDMA CPU path, so every
  selected `primary_rdma0_disp` RDMA update avoids the stale frame-done wait
  and logs `CPU RDMA MEM`; it also blocks `smart_ovl` from switching back to
  DIRECT_LINK while `DISP_OPT_BYPASS_PQ` is active.
- `BRINGUP_STATE.md` records the readback-verified `37ecde...` runtime result,
  the new hypothesis, rollback condition, and verification route.

Why each file changed: `primary_display.c` owns both decisions proven by the
fresh capture: whether the decouple RDMA update uses CMDQ or CPU transport,
and whether smart OVL reverts the experiment to direct-link. The state file is
the required durable link from this patch to the exact capture and artifact
identity.

Expected next marker: the next boot image should contain `M6 DDP smart ovl:
keep DECOUPLE while PQ bypass RDMA0-DISP is active`, `M6 DDP decouple rdma:
CPU apply RDMA config without stale wait`, and `M6 DDP decouple rdma: CPU RDMA
MEM`. A useful positive result is physical image, nonzero DSI0 IRQs,
persistent `PathMode:DECOUPLE`, persistent `M0_MOD=0x40200`, nonzero RDMA0
MEM/IN/OUT progress, or a new DSI/RDMA timeout marker after the CPU RDMA log.

Rollback condition: revert this patch if a readback-verified flash regresses
before root ADB/fb0/SurfaceFlinger, if keeping DECOUPLE causes a bootloop or
stuck adbd, if `CPU RDMA MEM` appears but the next timeout still immediately
returns to `M0_MOD=0x51280`, or if the panel remains black with no new RDMA/DSI
marker beyond the `37ecde...` capture.

Verification commands:

```bash
sha256sum -c <next-artifact-dir>/SHA256SUMS
(gzip -cd <next-artifact-dir>/Image.gz-dtb 2>/dev/null || true) | strings | grep -E 'keep DECOUPLE|CPU apply RDMA config|CPU RDMA MEM|CPU route|M6 DDP timeout'
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 push <next-artifact-dir>/boot-*.img /cache/boot.img
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 exec-out 'dd if=/cache/boot.img bs=<boot-size> count=1 2>/dev/null' | sha256sum
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell 'dd if=/cache/boot.img of=/dev/block/platform/mtk-msdc.0/11230000.msdc0/by-name/boot bs=1048576; sync'
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 exec-out 'dd if=/dev/block/platform/mtk-msdc.0/11230000.msdc0/by-name/boot bs=<boot-size> count=1 2>/dev/null' | sha256sum
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 reboot
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 wait-for-device
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell 'cat /d/mtkfb 2>&1; dmesg | grep -E "keep DECOUPLE|CPU apply RDMA config|CPU RDMA MEM|CPU route|primary_rdma0_disp|M0_MOD|RDMA0 MEM|dsi0|VSYNC|DIRECT_LINK"'
```

Build/artifact result: branch `work/m6-rdma0-disp-decpq-20260531` built
successfully in
`/home/n8n/forge-work/kernel-builds/m6-rdma0-disp-pin-decouple-20260531/out`.
Build log is
`/srv/forge/android/export/meizu_m6_artifacts/20260531-m6-rdma0-disp-pin-decouple/build.log`
sha256 `48c15423b49ceedf414fb5c4a7b5b032876bc6dd44f5b0def02ed4e429abab46`.
Repacked artifact
`/srv/forge/android/export/meizu_m6_artifacts/20260531-m6-rdma0-disp-pin-decouple/boot-m6-rdma0-disp-pin-decouple.img`
has sha256 `df317057c3137015a9bf33d1907a7d89b00be6d17d2c0a021b6542a2711bd5f6`
and size `8876032`; `Image.gz-dtb` sha256 is
`0c7144ab488e95ad5646f4c5f526ffe2a617717111310af39af2e28ba614a3ce`; the
matching `System.map` sha256 is
`3eeed87d0b6fed021d4cde1b85e10be7aea82751569d54850dd2cc928d63ea46`; the
matching `config` sha256 is
`bc272726035c1a2eca9422e9bc230cf54f8295648a8865a98faba046ab01619e`.
`sha256sum -c SHA256SUMS` passed, `abootimg -i` preserved the previous boot
geometry/header, and kernel strings include `M6 DDP smart ovl: keep DECOUPLE
while PQ bypass RDMA0-DISP is active`, `M6 DDP decouple rdma: CPU apply RDMA
config without stale wait`, `M6 DDP decouple rdma: CPU RDMA MEM`, `M6 DDP
decouple route: CPU route`, and `M6 DDP timeout`.

Runtime result: `boot-m6-rdma0-disp-pin-decouple.img` was flashed on serial
`711HEBSR277K5`; readback
`/srv/forge/android/meizu_m6/captures/20260531-140843-m6-rdma0-disp-pin-decouple-runtime-df3170-711HEBSR277K5/boot-readback.img`
matches the local artifact sha256
`df317057c3137015a9bf33d1907a7d89b00be6d17d2c0a021b6542a2711bd5f6`.

FACT: Fresh runtime capture
`/srv/forge/android/meizu_m6/captures/20260531-140843-m6-rdma0-disp-pin-decouple-runtime-df3170-711HEBSR277K5`
reaches Android userspace with SurfaceFlinger running. `live-dumpsys-SurfaceFlinger.txt`
shows Built-in Screen `720x1280`, `powerMode=2`, `numLayers=1`, and
`flips=4892`. `live-debugfs-mtkfb.txt` shows `PathMode:DECOUPLE`,
`DISP_OPT_BYPASS_PQ=1`, `RDMA0 Transfer=5585` at about `61.37fps`, repeated
`M6 DDP decouple rdma: CPU RDMA MEM=0x100000/0x870`, `0x400000/0x870`, and
`0x700000/0x870`, and repeated `M6 DDP smart ovl: keep DECOUPLE while PQ bypass
RDMA0-DISP is active`. The same file shows `DSI_EXT_TE=0`; `live-proc-interrupts.txt`
shows `dsi0` IRQs still zero.

FACT: Screen marker writes round-tripped through framebuffer and screencap in
the same capture. The marker raw files and pulled fb0 raw files match their
recorded sha256 files, and screencaps are non-empty PNGs. This proves userspace
composition and framebuffer memory writes are real for this artifact; it does
not prove physical panel visibility.

INFERENCE: The active display frontier moved past PQ, mutex membership, stale
OVL/PQ waits, and RDMA0 memory scanout scheduling. The remaining display
frontier is downstream of RDMA memory scanout and inside DSI/PHY/panel video
acceptance or a power/low-battery interaction, because RDMA0 transfer counts
rise while DSI0 IRQs remain zero and the user still reports a black physical
panel.

Read-only DSI dump attempt: capture
`/srv/forge/android/meizu_m6/captures/20260531-141838-m6-df3170-readonly-ddp-dsi-dump-711HEBSR277K5`
was started from the same verified artifact. The debugfs `/d/dispsys`
commands echoed `dump_path:*` / `dump_reg:*` request strings but did not yield a
usable decoded DSI register dump before the device rebooted. Do not treat this
as a display conclusion.

FACT: Post-reset capture
`/srv/forge/android/meizu_m6/captures/20260531-142209-m6-df3170-after-dumpreg15-reset-711HEBSR277K5`
again read back boot sha256
`df317057c3137015a9bf33d1907a7d89b00be6d17d2c0a021b6542a2711bd5f6`. Its
`proc-last_kmsg.txt` proves the reset cause was low battery/DLPT, not a DSI
dump conclusion: `[DLPT_POWER_OFF_EN] SOC=0 to power off , cnt=4`, battery
line `AvgVbat 3474`, `bat_vol 3326`, `VChr 4424`, `CHR_Type 1`, `SOC 0:0:0`,
`healthd: battery l=0 ... st=2 ... chg=u`, `bq2415x_charging: enable charger
successfully`, and `reboot: Restarting system with command 'DLPT reboot
system'`.

FACT: After the DLPT reset, the same post-reset capture is not a clean display
test. `mtkfb.txt` reports `PathMode:DIRECT_LINK`, `RDMA0 Transfer=4`,
`DISP_OPT_BYPASS_PQ=1`; `dumpsys-SurfaceFlinger.txt` has `flips=0` and
`numLayers=0`; `interrupts.txt` still shows `dsi0` IRQs zero. Current ADB
listing after this state update does not show serial `711HEBSR277K5`, only
`nx549j` and `m681`.

FACT: The active kernel config for this artifact uses the MT6755 power tree
with `CONFIG_MTK_PMIC_CHIP_MT6353=y`, `CONFIG_MTK_SMART_BATTERY=y`, and
`CONFIG_MTK_PMIC_CHIP_MT6335` unset. The matching DLPT power-off check is in
`kernel-3.18/drivers/misc/mediatek/power/mt6755/pmic.c:3321`, not in the
`mt6335/pmic_throttling_dlpt.c` copy. The charger path is the in-tree
`bq24157` driver, with DTS node `bq24157@6a` compatible
`ti,bq2415x`, `ti,bq24157`, and `bq24157`; runtime logs confirm `bq2415x
1-006a`.

INFERENCE: The immediate blocker for further flash/capture cycles is charging
stability at SOC 0. A kernel boot-unblock that ignores DLPT poweroff is possible
only as an explicit power ISOLATION/BOOT-UNBLOCK patch and carries brownout/FS
risk at `bat_vol` around 3.3V. Prefer the runtime debug knobs first when M6
returns to ADB: set low-battery/DLPT stop flags, keep USB connected, and wait
for battery capacity to rise before another display flash cycle.

Safe next verification commands:

```bash
adb -H 127.0.0.1 -P 15038 devices -l
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell 'date; for d in /sys/class/power_supply/*; do echo $d; for f in status capacity voltage_now current_now online present health temp; do [ -e "$d/$f" ] && printf "%s=" "$f" && cat "$d/$f"; done; done'
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell 'find /sys /d /proc -name "*dlpt*" -o -name "*low_battery*" 2>/dev/null | sort'
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell 'for f in /sys/devices/platform/*/dlpt_stop /sys/devices/platform/*/low_battery_protect_stop /d/*/dlpt_stop /d/*/low_battery_protect_stop; do [ -e "$f" ] && echo "$f=$(cat "$f" 2>/dev/null)"; done'
```

Next display action after battery is stable: add a DIAGNOSTIC-only DSI/PHY/panel
snapshot at the safe DSI start/timeout callsites. Do not patch CMDQ wait tokens
or expand PQ/mutex bypass again unless a fresh verified capture regresses to
that earlier frontier.

PATCH HISTORY, DIAGNOSTIC, 2026-05-31: add read-only M6 DSI/MIPITX snapshots.

Hypothesis: the verified `df317057...` pin-decouple artifact advanced past PQ,
mutex membership, stale wait handling, and RDMA memory scanout, but the physical
panel remains black because the failure is now downstream of RDMA0 scanout in
DSI/PHY/panel video acceptance. A read-only snapshot at DSI init/config/start
call sites should show whether DSI0 reaches video mode, HS clock enable,
valid timing/packet-size programming, and enabled MIPITX PLL/lane state without
changing display behavior.

Evidence: runtime capture
`/srv/forge/android/meizu_m6/captures/20260531-140843-m6-rdma0-disp-pin-decouple-runtime-df3170-711HEBSR277K5`
uses boot readback sha256
`df317057c3137015a9bf33d1907a7d89b00be6d17d2c0a021b6542a2711bd5f6`.
`live-debugfs-mtkfb.txt` shows `PathMode:DECOUPLE`, `DISP_OPT_BYPASS_PQ=1`,
`RDMA0 Transfer=5585`, and repeated `M6 DDP decouple rdma: CPU RDMA MEM=...`.
`live-dumpsys-SurfaceFlinger.txt` shows Built-in Screen `720x1280`,
`powerMode=2`, `numLayers=1`, and `flips=4892`; fb markers and screencaps
round-tripped. `live-proc-interrupts.txt` still shows `dsi0` IRQs zero, and the
user still reports a black physical panel. Post-reset capture
`/srv/forge/android/meizu_m6/captures/20260531-142209-m6-df3170-after-dumpreg15-reset-711HEBSR277K5`
proves the later reset was low-battery/DLPT, so it is not display evidence.

Files changed:
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_dsi.c` adds read-only
  `M6 DSI snapshot[...]` dumps for DSI core, video timing, PHY timing, state
  debug, and MIPITX PLL/lane/control registers, rate-limited at init/config/
  start/trigger paths.
- `BRINGUP_STATE.md` records the hypothesis, artifact identity, expected next
  markers, rollback condition, and verification route.

Why each file changed: `ddp_dsi.c` owns the DSI core and MIPITX state that is
now the earliest unresolved display frontier after the verified RDMA0-DECOUPLE
runtime result. The patch deliberately uses only `INREG32` and `DISPERR`, with
no register writes beyond the pre-existing DSI start path. The state file is
the required durable link from this DIAGNOSTIC patch to the exact verified
artifact and capture chain.

Expected next marker: the next capture from this artifact should show
`M6 DSI snapshot[init-after]`, `M6 DSI snapshot[config-done]`,
`M6 DSI snapshot[start-after-hs]`, and `M6 DSI snapshot[start-before/start-after]`
lines in dmesg. A useful positive result is a physical image or nonzero `dsi0`
IRQs. A useful negative result is a stable black-panel boot with snapshot values
showing which of DSI `START/STA/MODE/TXRX/PS/VM_CMD/STATE_DBG`, `PHY_LCCON`,
or MIPITX PLL/lane registers failed to enter the expected video/HS state.

Rollback condition: revert this patch if a readback-verified flash regresses
before root ADB/fb0/SurfaceFlinger, if the log volume destabilizes boot or
causes DLPT resets earlier than the prior `df317057...` artifact, or if the
snapshot proves DSI/PHY state is already healthy and the frontier moves to LCM
DCS/panel power acceptance.

Build/artifact result: branch `work/m6-rdma0-disp-decpq-20260531` built
successfully in
`/home/n8n/forge-work/kernel-builds/m6-dsi-snapshot-20260531/out`.
Repacked artifact
`/srv/forge/android/export/meizu_m6_artifacts/20260531-m6-dsi-snapshot-readonly/boot-m6-dsi-snapshot-readonly.img`
has sha256 `7504b88f18b7ed5510361ba76f602ca3ea780786ca506c0be6054f7d08ca74c4`
and size `8876032`. Kernel payload `Image.gz-dtb` sha256 is
`1a3b5e05ab30aae042b2090e5936975d322d558a1aa09eea09bacb18cada3b32`;
matching `System.map` sha256 is
`76bb0c6ea6a3ed1484e23e52bdfe682931f005d40bd14db72ba7318d0559284a`;
matching `config` sha256 is
`bc272726035c1a2eca9422e9bc230cf54f8295648a8865a98faba046ab01619e`;
`build-verify.log` sha256 is
`3e2c975c3ec7c5f9a8a786db0d1e9b7e85c26abc116fac179d8ff9f60157c8a6`.
`sha256sum -c SHA256SUMS` passed, `abootimg -i` preserved page size `2048`,
boot name `1552631950`, addresses `0x40080000/0x45000000/0x44000000`, and the
eng permissive cmdline. Kernel strings include all `M6 DSI snapshot` lines plus
the prior `M6 DDP decouple rdma` and `M6 DDP smart ovl` markers.

FACT: At artifact creation time, M6 serial `711HEBSR277K5` was not visible via
`adb -H 127.0.0.1 -P 15038 devices -l`; only `nx549j` and `m681` were visible.
Do not flash without `-s 711HEBSR277K5`, and wait for battery/ADB stability
before the next device cycle.

Verification commands:

```bash
sha256sum -c /srv/forge/android/export/meizu_m6_artifacts/20260531-m6-dsi-snapshot-readonly/SHA256SUMS
(gzip -cd /srv/forge/android/export/meizu_m6_artifacts/20260531-m6-dsi-snapshot-readonly/Image.gz-dtb 2>/dev/null || true) | strings | grep -E 'M6 DSI snapshot|M6 DDP smart ovl|M6 DDP decouple rdma'
adb -H 127.0.0.1 -P 15038 devices -l
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell 'date; for d in /sys/class/power_supply/*; do echo $d; for f in status capacity voltage_now current_now online present health temp; do [ -e "$d/$f" ] && printf "%s=" "$f" && cat "$d/$f"; done; done'
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 push /srv/forge/android/export/meizu_m6_artifacts/20260531-m6-dsi-snapshot-readonly/boot-m6-dsi-snapshot-readonly.img /cache/boot.img
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 exec-out 'dd if=/cache/boot.img bs=8876032 count=1 2>/dev/null' | sha256sum
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell 'dd if=/cache/boot.img of=/dev/block/platform/mtk-msdc.0/11230000.msdc0/by-name/boot bs=1048576; sync'
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 exec-out 'dd if=/dev/block/platform/mtk-msdc.0/11230000.msdc0/by-name/boot bs=8876032 count=1 2>/dev/null' | sha256sum
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 reboot
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 wait-for-device
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell 'dmesg | grep -E "M6 DSI snapshot|M6 DDP decouple|M6 DDP smart|dsi0|DSI|MIPITX|DLPT|battery|healthd"; cat /d/mtkfb 2>&1; cat /proc/interrupts | grep -E "dsi|disp|rdma"'
```

## 2026-05-31 DSI live snapshot after RDMA progress

PATCH HISTORY, DIAGNOSTIC, 2026-05-31: rate-limit M6 RDMA/PQ log spam and add
a live DSI snapshot at the CPU RDMA update frontier.

Hypothesis: the read-only DSI snapshot artifact `7504b88f...` did not preserve
the most useful DSI state in the ring buffer because the M6 CPU RDMA and smart
OVL diagnostics printed on nearly every frame. Rate-limiting those diagnostics
and taking a DSI/MIPITX snapshot from the sampled CPU RDMA path should preserve
the downstream DSI state for the same black-panel runtime without changing the
display programming sequence.

Evidence: verified flash/readback of
`/srv/forge/android/export/meizu_m6_artifacts/20260531-m6-dsi-snapshot-readonly/boot-m6-dsi-snapshot-readonly.img`
sha256 `7504b88f18b7ed5510361ba76f602ca3ea780786ca506c0be6054f7d08ca74c4`
produced capture
`/srv/forge/android/meizu_m6/captures/20260531-171132-m6-dsi-snapshot-runtime-7504b88f-711HEBSR277K5`.
That capture booted Android with SurfaceFlinger and bootanimation running,
`PathMode:DECOUPLE`, `DISP_OPT_BYPASS_PQ=1`, `RDMA0 Transfer=7858`, and
fb0 marker raw readbacks matching `white`, `bars`, `scan`, and `black`, but
the physical panel remained black and the preserved dmesg did not contain the
needed `M6 DSI snapshot` lines. The rebuilt artifact
`/srv/forge/android/export/meizu_m6_artifacts/20260531-m6-dsi-live-snapshot-ratelimit/boot-m6-dsi-live-snapshot-ratelimit.img`
sha256 `6fbe672c17498d01a9a706df2ed4baa14dabfe3cdf2b554abf7597df55d6e8df`
was flashed and read back successfully on serial `711HEBSR277K5`; capture
`/srv/forge/android/meizu_m6/captures/20260531-173320-m6-dsi-live-snapshot-ratelimit-runtime-6fbe672c-711HEBSR277K5`
shows Android 8.1, SurfaceFlinger running, BootAnimation layer, Built-in
Screen `720x1280`, `powerMode=2`, `flips=7227`, `PathMode:DECOUPLE`,
`LCM Driver=[ili9881p_hd_dsi_txd]`, `RDMA0 Transfer=7583`, and
`DISP_OPT_BYPASS_PQ=1`. The same capture preserves live DSI snapshots with
`START=0x10001`, `STA=0x20`, `MODE=0x3`, `PS=0x30870`, video timing
`VSA/VBP/VFP/VACT=0x14/0x18/0x40/0x500`,
`STATE7=0x2020/Video data period` on sampled frames, and MIPITX lane/PLL state
`lanes=0x603/0x601/0x601/0x601/0x601` and
`pll=0x9/0x1/0x46c4ec4e/0x0/0x1/0x20/0x101`. Backlight is not the current
frontier: live sysfs reports `/sys/class/leds/lcd-backlight/brightness=204`
and dmesg contains `disp_pwm_set_backlight_cmdq(id=0x1, level_1024=818)` plus
`backlight is on`. `dsi0` IRQs remain zero, and fb0 marker writes still read
back correctly while screencap remains black.

Files changed:
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_dsi.h` declares the
  live M6 DSI snapshot helper for other display files.
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_dsi.c` exposes
  `dsi_m6_dump_live()` as a read-only wrapper around the existing DSI/MIPITX
  snapshot code.
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/primary_display.c` samples
  the M6 CPU RDMA and smart OVL diagnostic logs and calls the live DSI snapshot
  only on sampled CPU RDMA updates.
- `BRINGUP_STATE.md` records this tested diagnostic checkpoint and the next
  evidence gap.

Why each file changed: `ddp_dsi.*` owns the already-added read-only DSI/MIPITX
snapshot code; exporting a wrapper lets the current RDMA frontier observe DSI
state without adding register writes. `primary_display.c` is where the noisy
per-frame M6 CPU RDMA/PQ logs and the sampled RDMA memory update occur; sampling
there preserves dmesg capacity and ties the DSI snapshot to frames that really
advance RDMA memory scanout.

Expected next marker: the next display diagnostic should read panel DCS status
after LCM init/resume or during the stable black-panel runtime. If DCS reports
sleep/display-off or command read failure, the frontier moves to panel command
path/LCM init acceptance. If DCS reports display-on while DSI video state stays
active, the frontier moves to HS video acceptance/timing or panel-specific TE/
mode expectations.

Rollback condition: revert this diagnostic if a verified flash regresses before
root ADB/fb0/SurfaceFlinger, if the rate-limited snapshot still causes log
pressure or battery/DLPT resets earlier than the prior artifact, or if a later
proper fix makes the live snapshot obsolete and the tree needs to shed debug
noise.

Build/artifact result: build directory
`/home/n8n/forge-work/kernel-builds/m6-dsi-live-snapshot-ratelimit-20260531/out`.
Artifact directory
`/srv/forge/android/export/meizu_m6_artifacts/20260531-m6-dsi-live-snapshot-ratelimit`;
`boot-m6-dsi-live-snapshot-ratelimit.img` sha256
`6fbe672c17498d01a9a706df2ed4baa14dabfe3cdf2b554abf7597df55d6e8df`;
`Image.gz-dtb` sha256
`519554f4e9809872ee4f35fcc7648c9e156257533fad27c632df524fa5b0cd73`;
`System.map` sha256
`c8fae0517cf9ec6b45234cbc6184be52c96888add2b01eb89280aab4eff79e66`;
`config` sha256
`bc272726035c1a2eca9422e9bc230cf54f8295648a8865a98faba046ab01619e`;
`build.log` sha256
`e0eec942bb6a9aff42c218b7f03631623168d6b9723f3088567c34019d12340f`.

Verification commands:

```bash
sha256sum -c /srv/forge/android/export/meizu_m6_artifacts/20260531-m6-dsi-live-snapshot-ratelimit/SHA256SUMS
(gzip -cd /srv/forge/android/export/meizu_m6_artifacts/20260531-m6-dsi-live-snapshot-ratelimit/Image.gz-dtb 2>/dev/null || true) | strings | grep -E 'M6 DSI snapshot|cpu-rdma-live|CPU apply RDMA config|CPU RDMA MEM|keep DECOUPLE'
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 exec-out 'dd if=/dev/block/platform/mtk-msdc.0/11230000.msdc0/by-name/boot bs=8876032 count=1 2>/dev/null' | sha256sum
grep -R -n -E 'M6 DSI snapshot\\[cpu-rdma-live\\]|STATE7=|MIPITX|PathMode:DECOUPLE|RDMA0 Transfer|Built-in Screen|powerMode=2|flips=|backlight is on|disp_pwm_set_backlight|dsi0' /srv/forge/android/meizu_m6/captures/20260531-173320-m6-dsi-live-snapshot-ratelimit-runtime-6fbe672c-711HEBSR277K5
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell 'cat /sys/class/leds/lcd-backlight/brightness; cat /d/mtkfb 2>&1; cat /proc/interrupts | grep -E "dsi|disp|rdma"; dmesg | grep -E "M6 DSI snapshot|backlight|disp_pwm|ili9881p"'
```

## 2026-05-31 DCS status window probe

PATCH HISTORY, DIAGNOSTIC, 2026-05-31: add a no-reset DCS status probe around
the first primary path configuration window and record the M681 format/PQ lesson
against the current M6 evidence.

Hypothesis: after the verified RDMA0 decouple/PQ bypass and live DSI snapshot
artifacts, the unresolved physical black-panel frontier could still be either
panel-side DCS state or an earlier OVL/RDMA input handoff problem. A safe DCS
status probe must first prove whether the primary-display init path ever offers
a command-mode window. If both hooks are already in DSI video mode, the probe
must skip reads without resetting DSI and the next diagnostic should move toward
the OVL/RDMA layer configuration path instead of repeating DSI/PQ loops.

Evidence: artifact
`/srv/forge/android/export/meizu_m6_artifacts/20260531-m6-dcs-status-pathwindow/boot-m6-dcs-status-pathwindow.img`
was flashed to serial `711HEBSR277K5`; local boot image and device boot
readback both have sha256
`74a60198455d778c15804c9499e41c70392c5ce695b916f64ef52223c48cbe17`.
Matching payload identities are `Image.gz-dtb`
`13bd94bab5921e10e7f3bc01bf07bd39b8b7748a38920380ad3932918cf76e6e`,
`System.map`
`791ecd3d331e98e75da1d5c18bbf4e375a72f73fb2cc92f55d26e76bde719461`, and
`config` `bc272726035c1a2eca9422e9bc230cf54f8295648a8865a98faba046ab01619e`.
Fresh capture path:
`/srv/forge/android/meizu_m6/captures/20260531-185548-m6-dcs-status-pathwindow-runtime-74a60198-711HEBSR277K5`.
The latest `/cache/bootdiag` run inside that capture,
`cache-bootdiag/run-20260601-095001-319/cmd/dmesg.txt`, shows
`M6 DCS status[primary-before-path-config]: begin` followed by every requested
DCS command being skipped with `video-mode=3 START=0x1 STA=0x440
INTSTA=0x80000790`; it then shows the same skip result for
`M6 DCS status[primary-after-path-config]: begin`. Therefore this diagnostic did
not issue a DCS read and did not call `DSI_Reset`. The same verified capture
keeps SurfaceFlinger/bootanimation alive (`Built-in Screen` 720x1280,
`powerMode=2`, flips advancing, BootAnimation layer), has a valid 720x1280
`screen/screencap.png`, and reports backlight sysfs brightness `102/255`, but
physical output is still treated as black unless the user reports otherwise.
The active display failure remains in DDP runtime evidence:
`present_fence_w` VSYNC timeouts, OVL underflows/abnormal SOF, RDMA0 active, DSI
in video mode, and OVL0 L0/L3 programmed as BGRA8888 at addresses
`0x9f707fbf` / `0x9fa8bfff`.

M681 carry-forward check: the report
`/srv/forge/android/m681/docs/run_reports/2026-05-31_m681_display_route_state_matrix_audit.md`
records that the later proven M681 physical-display fix was not PQ and not TXD
readback, but a high-level display format handoff where
`DISP_FORMAT_PRGBA8888` (`0x1304`) reached the kernel mapper. For this M6 tree,
`kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_color_format.c` already
maps `DISP_FORMAT_PRGBA8888` to `UFMT_PRGBA8888`, and the fresh M6 capture has
no `Invalid color format` or `0x1304` marker. INFERENCE: M681 still warns us
not to over-focus on DSI/PQ, but the exact M681 `0x1304` missing-map failure is
not the current M6 blocker.

Files changed:
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_dsi.h` declares
  `dsi_m6_dump_dcs_status()` for the primary and LCM display code.
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_dsi.c` adds the
  no-reset DCS status reader and video-mode skip logging for DCS commands
  `0x0a`, `0x0b`, `0x0c`, `0x0d`, `0xda`, `0xdb`, and `0xdc`.
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/disp_lcm.c` calls the probe
  from LCM init if that DSI init path executes.
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/primary_display.c` calls the
  probe before and after the first primary path config/start window.
- `BRINGUP_STATE.md` records the verified artifact, capture, conclusion, M681
  comparison, and next diagnostic route.

Why each file changed: `ddp_dsi.c` owns DSI command packets and is the only
right place to add a DCS reader that can check command/video mode before
touching registers. `ddp_dsi.h` is needed to expose the read-only diagnostic to
nearby display stages. `disp_lcm.c` and `primary_display.c` are the two narrow
places that can prove whether a command-mode DCS window exists during LCM init
or first primary path setup. The state file is required to bind the tested
diagnostic to exact artifact and capture evidence.

Expected next marker: the next diagnostic should log the userspace-to-kernel
OVL input handoff before OVL register programming: session/layer id, enable,
format, pitch, source crop, destination rectangle, ion/acquire fence fields,
MVA/address/offset, and the resulting `OVL_CONFIG_STRUCT`. A useful positive
result is proving that HWC/userspace already passes a bad MVA, offset, pitch, or
layer enable pattern. A useful negative result is proving the input config is
normal before OVL programming, moving the frontier to OVL register packing,
mutex/module route, or SMI/MMU behavior.

Rollback condition: revert this diagnostic if a verified flash regresses before
root ADB/fb0/SurfaceFlinger, if the new DCS probe causes DSI reset or command
timeout side effects, or if a later controlled command-mode DCS probe replaces
this safer skipped-read window. Do not treat this patch as a display fix.

Verification commands:

```bash
sha256sum -c /srv/forge/android/export/meizu_m6_artifacts/20260531-m6-dcs-status-pathwindow/SHA256SUMS
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 exec-out 'dd if=/dev/block/platform/mtk-msdc.0/11230000.msdc0/by-name/boot bs=8876032 count=1 2>/dev/null' | sha256sum
grep -R -n -E 'M6 DCS status|dcs-status-after|M6 DSI snapshot\\[start-after-hs\\]|M6 DDP timeout\\[VSYNC\\]|Built-in Screen|BootAnimation|brightness|Invalid color format|0x1304' /srv/forge/android/meizu_m6/captures/20260531-185548-m6-dcs-status-pathwindow-runtime-74a60198-711HEBSR277K5
grep -R -n -E 'DISP_FORMAT_PRGBA8888|UFMT_PRGBA8888|Invalid color format|0x1304' kernel-3.18/drivers/misc/mediatek/video/mt6755 /srv/forge/android/meizu_m6/captures/20260531-185548-m6-dcs-status-pathwindow-runtime-74a60198-711HEBSR277K5
```

## 2026-06-01 OVL0_2L mutex membership probe

PATCH HISTORY, ISOLATION/DIAGNOSTIC, 2026-06-01: keep the active
`OVL0_2L` bit in mutex0 while preserving the current M6 PQ-bypass direct route,
and add bounded primary input / OVL handoff logging across the userspace-to-DDP
frontier.

Hypothesis: the previous M6 PQ-bypass mutex isolation removed too much from
mutex0. Fresh OVL evidence showed the first primary layer was programmed on
`OVL0_2L`, so clearing `OVL0_2L` from mutex0 could make the display path wait on
`OVL0/RDMA0/DSI0` while the real active layer was outside the mutex. Keeping
`OVL0_2L` in mutex0 should close that exact mismatch. If the physical display
remains black, the next proven blocker is no longer "active OVL layer absent
from mutex" but a route/handoff mismatch between the active `OVL0_2L` layer and
the routed `OVL0 -> OVL0_VIRTUAL -> COLOR0 -> DITHER -> RDMA0 -> DSI0` path.

Evidence: artifact
`/srv/forge/android/export/meizu_m6_artifacts/20260531-m6-ovl0-2l-mutex/boot-m6-ovl0-2l-mutex.img`
was flashed to serial `711HEBSR277K5`; local artifact and device boot readback
both have sha256
`47b5df7f847b03ff22a409d7e97474d14817993a588b591996a61dafc52f9753`.
Matching payload identities are `Image.gz-dtb`
`9971baa16b54f339ca30273f7cb9d1aa0a12c97b24057e11f0d1a30f2cd71d34`,
`System.map`
`251523311790f144f3e813f93ad2e5cee604a4f3260bb170ecd283c01dc7724d`,
`config` `bc272726035c1a2eca9422e9bc230cf54f8295648a8865a98faba046ab01619e`,
and `initrd.img`
`b897743629941f917796605abb7c20f3cb3ee9c2e60a12faf8d9fb8de0279a81`.
Fresh capture path:
`/srv/forge/android/meizu_m6/captures/20260601-010700-m6-ovl0-2l-mutex-runtime-47b5df7f-711HEBSR277K5`.
The latest fresh bootdiag run in that capture is
`cache-bootdiag/run-20260601-110422-317`, manifest dated
`Mon Jun 1 11:04:23 GMT 2026`, cmdline serial `711HEBSR277K5`.

The kernel log proves normal userspace input into display: logcat kernel lines
3313-3314 show primary layer 0 enabled from `OverlayEngine_0` with
`fmt=PBGRA/0x1404`, `phy=0xa00000`, `pitch_px=736`, `720x1280`. Lines
3341-3342 show the handoff to `OVL_CONFIG_STRUCT` as
`ovl_fmt=PBGRA8888/0xc00c2d`, `addr=0xa00000`, `pitch_bytes=2944`. Lines
3357-3358 prove the active hardware programming is now
`M6 OVL diag cfg[1]: mod=OVL0_2L L0 global=0 en=1 ... addr=0xa00000`.
Lines 3362-3366 prove the mutex change applied:
`M0_MOD=0xd1280` includes `OVL0_2L`, but the frame still times out with
`route VALID=0x0 READY=0x4000930a`, `rdma0 ... IN=0/0 OUT=0/0`, and
`MEM_CON=0x0 MEM_START=0x0`. Lines 3367-3373 still show routed `OVL0` contains
stale/garbage-looking BGRA8888 L0/L3 addresses `0x9f707fbf` and `0x9fa8bfff`.
Lines 3375-3376 show DSI remains in video mode and alive
(`MODE=0x3`, `TXRX=0x1003c`, `PS=0x30870`). SurfaceFlinger remains logically
alive: `adb/surfaceflinger.txt:43-44` reports a 720x1280 built-in display,
`flips=5118`, `powerMode=2`, and BootAnimation is the active layer. Screencap
is still a valid 720x1280 PNG. The physical LCD remains treated as black unless
the user reports otherwise.

Files changed:
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/mtk_disp_mgr.c` adds bounded
  primary `disp_input_config` logging before display config preprocessing hands
  buffers to primary display.
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/primary_display.c` adds
  bounded logging of the converted `OVL_CONFIG_STRUCT` immediately before
  `dpmgr_path_config()`.
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_ovl.c` widens the OVL
  layer-config diagnostic to identify which OVL module receives each active
  layer.
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_path.c` keeps
  `OVL0_2L` in the M6 PQ-bypass mutex while still clearing inactive `OVL1_2L`
  and PQ modules `CCORR/AAL/GAMMA`.
- `BRINGUP_STATE.md` records the artifact, capture, conclusion, and next
  rollback/verification contract.

Why each file changed: `mtk_disp_mgr.c` owns the userspace/session input layer
before it is converted, so it is the narrowest place to prove whether Android
hands a sane buffer, format, pitch, and rectangle into the display driver.
`primary_display.c` owns the conversion from `disp_input_config` to
`OVL_CONFIG_STRUCT`, so it proves whether a sane input turns into a sane OVL
handoff. `ddp_ovl.c` owns final OVL register programming, so adding the module
name is required to distinguish routed `OVL0` from active `OVL0_2L`.
`ddp_path.c` owns mutex membership and is the only file needed to test the
specific evidence-backed mismatch: active `OVL0_2L` layer versus a clear mask
that previously removed `OVL0_2L`.

Expected next marker: a follow-up route/layer patch should make the first
active primary layer land on routed `OVL0` or otherwise connect the active OVL
engine into the path. A useful positive marker is `M6 OVL diag cfg` for the
enabled layer on `mod=OVL0`, with mutex/path timeout no longer showing
`rdma0 IN=0/0 OUT=0/0` at the first userspace frame. A useful negative marker is
unchanged `OVL0_2L` activity with `RDMA0 IN/OUT=0/0`, which keeps the frontier
at route/module connection rather than panel DCS, PQ format mapping, or DSI
timing.

Rollback condition: revert this checkpoint if a verified flash regresses before
root ADB, SurfaceFlinger, or the first primary frame, or if a later stock-route
patch proves `OVL0_2L` must be excluded from mutex0 because it is intentionally
inactive in the final route. Do not treat this patch as a physical display fix:
it closes one mutex-membership hypothesis and exposes the next route/handoff
blocker.

Verification commands:

```bash
sha256sum -c /srv/forge/android/export/meizu_m6_artifacts/20260531-m6-ovl0-2l-mutex/SHA256SUMS
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 exec-out 'dd if=/dev/block/platform/mtk-msdc.0/11230000.msdc0/by-name/boot bs=8876032 count=1 2>/dev/null' | sha256sum
grep -R -n -E 'M6 OVL input|M6 OVL handoff|M6 OVL diag cfg|M6 DDP timeout\[VSYNC\]|M0_MOD=0xd1280|rdma0 .*IN=0/0 OUT=0/0|Built-in Screen|BootAnimation|Invalid color format|0x1304' /srv/forge/android/meizu_m6/captures/20260601-010700-m6-ovl0-2l-mutex-runtime-47b5df7f-711HEBSR277K5
grep -R -n -E 'module_list_scenario|module_can_connect|OVL0_2L|OVL0_VIRTUAL|ddp_m6_primary_direct_mutex_clear_mask' kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_path.c kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_ovl.c
```

## 2026-06-01 OVL0_2L mutex and handoff proof

PATCH HISTORY, DIAGNOSTIC / ISOLATION, 2026-06-01: log the primary userspace
input-to-OVL handoff, log the actual OVL module receiving the layer, and stop
clearing `OVL0_2L` from the primary display mutex while PQ bypass is active.

Hypothesis: the prior DCS-window artifact proved DSI was already in video mode
and did not expose a safe command read window. The next earliest display
failure could be in the OVL/RDMA handoff. Because the primary route list starts
with `OVL0_2L` but the path connection tables route scanout from `OVL0` through
`OVL1_2L/OVL0_VIRTUAL` toward COLOR/DITHER/RDMA0/DSI0, the active layer might
be programmed into an OVL module that was either missing from mutex or not
connected to RDMA. This checkpoint tests the mutex half of that hypothesis and
records enough handoff evidence to decide the next route patch.

Evidence: artifact
`/srv/forge/android/export/meizu_m6_artifacts/20260531-m6-ovl0-2l-mutex/boot-m6-ovl0-2l-mutex.img`
was flashed to serial `711HEBSR277K5`; local boot image and boot partition
readback both have sha256
`47b5df7f847b03ff22a409d7e97474d14817993a588b591996a61dafc52f9753`.
Matching payload identities are `Image.gz-dtb`
`9971baa16b54f339ca30273f7cb9d1aa0a12c97b24057e11f0d1a30f2cd71d34`,
`System.map`
`251523311790f144f3e813f93ad2e5cee604a4f3260bb170ecd283c01dc7724d`,
and config `bc272726035c1a2eca9422e9bc230cf54f8295648a8865a98faba046ab01619e`.
Fresh capture path:
`/srv/forge/android/meizu_m6/captures/20260601-010700-m6-ovl0-2l-mutex-runtime-47b5df7f-711HEBSR277K5`.
Its latest bootdiag run
`cache-bootdiag/run-20260601-110422-317/cmd/logcat_kernel.txt` shows the active
layer programmed as `M6 OVL diag cfg[1]: mod=OVL0_2L L0 global=0 en=1
source=0 fmt=PBGRA8888/0xc00c2d ... addr=0xa00000 ... dst_xywh=0/0/720/1280
pitch=2944`. The same first timeout shows `M0_MOD=0xd1280`, proving the mutex
now contains `OVL0_2L` in addition to the previous primary route modules.
However the panel is still treated as black, `route VALID=0x0`, `RDMA0
IN=0/0 OUT=0/0`, and RDMA memory mode is not active at that first marker. The
same marker shows OVL0 still has stale-looking enabled layers at unaligned
addresses `0x9f707fbf` and `0x9fa8bfff`, while the clean current layer is on
`OVL0_2L`. Therefore the mutex omission is closed as a necessary but
insufficient fix; the next earliest evidence-backed blocker is layer placement
versus the connected route.

Files changed:
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/mtk_disp_mgr.c` logs primary
  `disp_input_config` fields after input preprocessing.
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/primary_display.c` logs the
  converted `OVL_CONFIG_STRUCT` just before `dpmgr_path_config()`.
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_ovl.c` logs which OVL
  hardware module receives each enabled layer.
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_path.c` keeps
  `OVL0_2L` in mutex while still clearing inactive `OVL1_2L` and PQ modules
  `CCORR/AAL/GAMMA` under `DISP_OPT_BYPASS_PQ`.
- `BRINGUP_STATE.md` records the verified artifact, capture, conclusion, and
  next route hypothesis.

Why each file changed: `mtk_disp_mgr.c` is the point where userspace primary
layer configs are normalized and MVA/offset/pitch evidence appears.
`primary_display.c` owns the conversion from `disp_input_config` to
`OVL_CONFIG_STRUCT`, which proves whether the format and address are sane before
hardware programming. `ddp_ovl.c` owns the final per-module OVL register
programming and proves which hardware block consumes global layer 0.
`ddp_path.c` owns mutex composition; keeping `OVL0_2L` in mutex was the narrow
test for the proven active module. The state file binds the patch to exact
artifact and capture evidence.

Expected next marker: after a route/layer-placement patch, the first fresh
timeout should no longer show the active current layer only on disconnected
`OVL0_2L`. A positive marker is `M6 OVL diag cfg... mod=OVL0 ... addr=0xa00000`
or a stock-equivalent connected `OVL0 -> OVL0_2L -> OVL0_VIRTUAL` route, with
`RDMA0 IN/OUT` counters nonzero and `route VALID` no longer `0x0`. If that
still leaves a black panel, the frontier moves to OVL output path or SMI/MMU
state with fresh nonzero RDMA input evidence.

Rollback condition: revert this checkpoint if a verified flash regresses before
root ADB/SurfaceFlinger/fb0, if keeping `OVL0_2L` in mutex makes the first
timeout earlier or noisier without producing handoff evidence, or after the
next route patch proves `OVL0_2L` should be unused and absent from the mutex.

Verification commands:

```bash
sha256sum -c /srv/forge/android/export/meizu_m6_artifacts/20260531-m6-ovl0-2l-mutex/SHA256SUMS
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 exec-out 'dd if=/dev/block/platform/mtk-msdc.0/11230000.msdc0/by-name/boot bs=8876032 count=1 2>/dev/null' | sha256sum
grep -R -n -E 'M6 OVL input|M6 OVL handoff|M6 OVL diag cfg|M6 DDP timeout\\[VSYNC\\]|M0_MOD=0xd1280|RDMA0 IN=0/0|route VALID=0x0|Built-in Screen|BootAnimation|brightness' /srv/forge/android/meizu_m6/captures/20260601-010700-m6-ovl0-2l-mutex-runtime-47b5df7f-711HEBSR277K5
grep -R -n -E 'DISP_MODULE_OVL0_2L|module_can_connect|mout_map|sel_out_map|sel_in_map|ovl_config_l' kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_path.c kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_ovl.c /srv/forge/android/meizu_m6/kernel-meizu_M6-Q-ex2-3.18.119/drivers/misc/mediatek/video/mt6757/dispsys/ddp_path.c
```

## 2026-06-01 Primary OVL0 route plus deep OVL/fence diagnostics

PATCH HISTORY, BOOT-UNBLOCK + DIAGNOSTIC, 2026-06-01: force the primary
scenario family back to routed `OVL0 -> OVL0_VIRTUAL -> COLOR/DITHER/RDMA0`
instead of placing the first active primary layer on `OVL0_2L`, and add dense
bounded markers around DDP path config order, `ovl_layer_scanned`, OVL0/OVL0_2L
register state, and present/OVL fence timelines.

Hypothesis: the previous mutex checkpoint proved `OVL0_2L` was no longer
missing from mutex0, but the active layer still lived on a module that the
primary route could not connect to RDMA0. Because `module_can_connect` marks
`OVL0_2L` as non-connectable and the path selection tables route primary scanout
through `OVL0` / `OVL0_VIRTUAL`, the earliest evidence-backed route fix is to
make primary scenario configuration start at `OVL0`. If the panel remains black
after that route fix, the next capture must prove whether the layer really lands
on `OVL0`, whether RDMA0 receives nonzero input, and whether present fences are
blocked only because IF_VSYNC never arrives.

Evidence: the route artifact
`/srv/forge/android/export/meizu_m6_artifacts/20260601-m6-primary-ovl0-route/boot-m6-primary-ovl0-route.img`
was flashed to serial `711HEBSR277K5`; `flash-711HEBSR277K5.txt` records
matching local and readback sha256
`d1828f179721b5cde761b43f6cc53250aca10f99dafb7d6f4b65e60cb0b31bf8`. Its
kernel payload `Image.gz-dtb` is
`9e8866e66fb343561904d3a5021d92e83ac39dfdacf24af9b694db025db09224`. The
currently attached device (`adb -H 127.0.0.1 -P 15038 -s 0123456789ABCDEF`)
has boot partition sha256
`13a2569ad4bb5d89b4e1ad603f3582abfbb23845f728f056bc9ab8ccd08e1651`; readback
capture `/srv/forge/android/meizu_m6/captures/20260601-current-boot-readback-0123456789ABCDEF`
unpacks to zImage sha256
`9e8866e66fb343561904d3a5021d92e83ac39dfdacf24af9b694db025db09224` and
ramdisk sha256
`bf3b959f70d8dbfae8d39c69d55cd93a6d1637663a2804177346e163a29de056`, matching
the route kernel plus the pure64/hwremap ramdisk. Fresh runtime capture
`/srv/forge/android/meizu_m6/captures/20260601-pure64-hwremap-0123456789ABCDEF/live-awake-170931`
shows `sys.boot_completed=1`, SurfaceFlinger running, bootanim stopped, built-in
720x1280 display in SurfaceFlinger, `DISP_OPT_BYPASS_PQ=1`, RDMA0 transfer
counters around 60fps, repeated OVL0 underflow/reset, and a stuck present fence
line `GED : fence disp-S10000-L0-15775 0`. That runtime was later tainted by an
HWC-disable/SF-restart probe, so the new diagnostics are required before a
proper-fix conclusion.

Files changed:
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_path.c` removes
  `OVL0_2L`/`OVL1_2L` from primary scanout scenario lists so global layer 0 is
  assigned to routed `OVL0`.
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_manager.c` logs primary
  path config order, pre/post `ovl_layer_scanned`, OVL0/OVL0_2L SRC state, RDMA
  counters, and timeout snapshots for both OVL engines.
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_ovl.c` logs the chosen
  first global layer, module layer count, final scan mask, enabled local layer
  mask, and key OVL state around `ovl_config_l()`.
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/primary_display.c` logs
  present fence updates/timeouts with timeline value, route/RDMA/OVL state, and
  OVL fence release values per layer.
- `BRINGUP_STATE.md` records the route/kernel/ramdisk identity and the next
  marker contract.
- `/srv/forge/android/meizu_m6/AGENTS.md` records the local M6 marker policy:
  dense bounded debug markers are preferred over guessing when evidence is
  missing.

Why each file changed: `ddp_path.c` is the only path table that decides whether
primary scanout starts on connected `OVL0` or non-connectable `OVL0_2L`.
`ddp_manager.c` owns path config sequencing and timeout state, so it proves
whether the route patch is actually used at runtime. `ddp_ovl.c` owns global
layer-to-local-layer assignment via `ovl_layer_scanned`, which is the exact
branch that must prove or falsify the placement hypothesis. `primary_display.c`
owns present fence update/release and first-frame callbacks, so it proves
whether fences are blocked by missing VSYNC/RDMA progress rather than by an
unrelated userspace fence issue. `AGENTS.md` captures the working rule requested
by the user so later agents keep adding cheap evidence markers instead of
guessing.

Expected next marker: after rebuild and flash of this exact source, a clean
capture should include `M6 DDP path cfg[...]` followed by `M6 OVL scan[...]` and
`M6 OVL diag cfg[...]` showing global layer 0 on `mod=OVL0`, not `OVL0_2L`.
The first timeout must include OVL0 and OVL0_2L decoded state, RDMA0 IN/OUT
counters, and `M6 display: IF_VSYNC timeout[...]` with present timeline value.
Positive route evidence is `OVL0_SRC` enabling the live layer, `OVL0_2L_SRC=0`,
and nonzero RDMA0 input/out counters. Negative route evidence is unchanged
`OVL0_2L` activity or `OVL0_SRC=0` after path config, which keeps the frontier
inside DDP path/layer assignment.

Rollback condition: revert the route part if a verified flash regresses before
root ADB/SurfaceFlinger or if new markers show stock-equivalent working primary
scanout must include `OVL0_2L` in the connected route. Revert the diagnostic
part if log volume makes the device unusable or if a later capture proves the
OVL/fence frontier and narrower markers can replace this broad instrumentation.
Do not treat this patch as a physical display fix until the user reports image
or a clean capture proves RDMA/DSI scanout with advancing fences.

Verification commands:

```bash
sha256sum /srv/forge/android/meizu_m6/captures/20260601-current-boot-readback-0123456789ABCDEF/current-boot.img /srv/forge/android/meizu_m6/captures/20260601-current-boot-readback-0123456789ABCDEF/zImage /srv/forge/android/meizu_m6/captures/20260601-current-boot-readback-0123456789ABCDEF/initrd.img
sha256sum /srv/forge/android/export/meizu_m6_artifacts/20260601-m6-primary-ovl0-route/Image.gz-dtb /srv/forge/android/export/meizu_m6_artifacts/20260601-m6-pure64-hwremap-bootpatch/pure64-hwremap-ramdisk.img
adb -H 127.0.0.1 -P 15038 -s 0123456789ABCDEF exec-out 'dd if=/dev/block/platform/mtk-msdc.0/11230000.msdc0/by-name/boot bs=8876032 count=1 2>/dev/null' | sha256sum
grep -R -n -E 'M6 DDP path cfg|M6 OVL scan|M6 OVL input|M6 OVL handoff|M6 OVL diag cfg|M6 DDP timeout\\[VSYNC\\]|ovl0_2l|M6 present fence update|M6 display: IF_VSYNC timeout|M6 OVL fence release|GED : fence' /srv/forge/android/meizu_m6/captures/<next-clean-capture>
grep -R -n -E 'DISP_MODULE_OVL0_2L|module_can_connect|module_list_scenario|M6 DDP path cfg|M6 OVL scan|M6 display: IF_VSYNC timeout' kernel-3.18/drivers/misc/mediatek/video/mt6755
```

## 2026-06-02 keep PQ-bypass route in DIRECT_LINK for OVL0 proof

PATCH HISTORY, ISOLATION, 2026-06-02: stop Smart OVL from pinning the M6
PQ-bypass display path in RDMA0-DISP DECOUPLE, and restore DIRECT_LINK if the
runtime has already switched there.

Hypothesis: the `Primary OVL0 route plus deep OVL/fence diagnostics` kernel
payload is already present in the current working boot image, but Smart OVL
still moves the runtime into the older PQ-bypass `primary_rdma0_disp` decouple
experiment before the direct OVL0 route can produce clean markers. Keeping
PQ-bypass display in DIRECT_LINK should make the next boot exercise the routed
`OVL0 -> OVL0_VIRTUAL -> COLOR/DITHER -> RDMA0 -> DSI0` path and expose the
first OVL0/RDMA0/DSI0 failure without the CPU-RDMA decouple path masking it.

Evidence: current runtime capture
`/srv/forge/android/meizu_m6/captures/20260602-2058-m6-current-runtime-markers-711HEBSR277K5`
read back the boot partition trimmed to 8863744 bytes with sha256
`eae3bc563a067540588eacb69e78e8e33943279cc40ba3cfef5c2b4cfb9eb836`.
The unpacked kernel payload in
`/srv/forge/android/export/meizu_m6_artifacts/20260602-m6-runtime-unblock-minimal-boot/kernel`
has sha256 `9e8866e66fb343561904d3a5021d92e83ac39dfdacf24af9b694db025db09224`,
matching
`/srv/forge/android/export/meizu_m6_artifacts/20260601-m6-primary-ovl0-route/Image.gz-dtb`.
The same capture showed Android booted (`sys.boot_completed=1`,
SurfaceFlinger running), `PathMode:DECOUPLE`, `DISP_OPT_BYPASS_PQ=1`,
`DISP_OPT_SMART_OVL=1`, `RDMA0 Transfer` around 60 fps, repeated
`M6 DDP decouple rdma: CPU RDMA MEM=...`, repeated
`M6 DSI snapshot[cpu-rdma-live]`, and `dsi0` IRQ counters still zero while
`mutex`, `ovl0`, and `rdma0` counters rose. Full-stride fb0 marker writes in
`screen-markers-fullstride/` proved raw framebuffer writes/readbacks were real
for black, white, and bars, but physical LCD remained treated as black and
`dsi0` IRQs stayed zero.

Live isolation evidence: after
`echo helper:DISP_OPT_SMART_OVL,0 > /sys/kernel/debug/mtkfb` and
`echo switch_mode:1 > /sys/kernel/debug/mtkfb`, capture-local directory
`live-switch-directlink/` showed `PathMode:DIRECT_LINK` and
`DISP_OPT_SMART_OVL=0`; Android and SurfaceFlinger stayed alive. The same
direct-link probe produced dense `IRQ: ovl0 frame underflow`, `hw reset done`,
`L0/L1 not complete until EOF`, and `abnormal SOF` messages, while `dsi0`
IRQs remained zero. This proves direct-link can be entered live, but the next
boot must gather clean path-config and OVL scan markers before the underflow
spam consumes the ring buffer.

Files changed:
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/primary_display.c` changes
  `smart_ovl_try_switch_mode_nolock()` so PQ-bypass DIRECT_LINK does not switch
  into DECOUPLE, and PQ-bypass DECOUPLE is switched back to DIRECT_LINK.

Why each file changed: `primary_display.c` owns the Smart OVL DL/DC switching
decision that live evidence proved can force the already-built OVL0 route
kernel back into the older CPU-RDMA decouple path. The patch does not touch
DSI, CMDQ wait tokens, fence release policy, panel init, or PQ register
programming; it only isolates the route mode so the existing OVL0 diagnostics
can run on a clean boot.

Expected next marker: a boot from the next image should include
`M6 DDP smart ovl: hold DIRECT_LINK while PQ bypass isolates direct OVL0 route`
or `M6 DDP smart ovl: restore DIRECT_LINK; PQ bypass RDMA0-DISP decouple is
diagnostic-only`, then `PathMode:DIRECT_LINK`, `M6 DDP path cfg[...]`,
`M6 OVL scan[...]`, and `M6 OVL diag cfg[...]` showing whether the live layer
lands on `mod=OVL0`. A positive route marker is `OVL0_SRC` enabled with
`OVL0_2L_SRC=0` and nonzero RDMA0 input/output counters. A negative marker is
unchanged OVL0 underflow with decoded sane layer addresses but no RDMA0 input,
which moves the frontier to OVL0 fetch/SMI/M4U or OVL output handoff rather
than PQ or RDMA0-DISP decouple.

Rollback condition: revert this isolation if a readback-verified boot regresses
before root ADB, `sys.boot_completed`, SurfaceFlinger, or fb0, or if a clean
boot proves that the physical display only lights when Smart OVL enters
RDMA0-DISP DECOUPLE. Re-enable the decouple path only as a diagnostic branch,
not as a display fix, if DIRECT_LINK remains black with no additional markers.

Verification commands:

```bash
sha256sum /srv/forge/android/export/meizu_m6_artifacts/<next>/boot.img /srv/forge/android/export/meizu_m6_artifacts/<next>/Image.gz-dtb
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 exec-out 'dd if=/dev/block/platform/mtk-msdc.0/11230000.msdc0/by-name/boot bs=<next-size> count=1 2>/dev/null' | sha256sum
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell 'cat /sys/kernel/debug/mtkfb 2>&1 | strings | egrep "PathMode|DISP_OPT_BYPASS_PQ|DISP_OPT_SMART_OVL|RDMA0 Transfer"; cat /proc/interrupts | egrep "mutex|ovl|rdma|dsi"'
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell 'dmesg | grep -E "hold DIRECT_LINK|restore DIRECT_LINK|M6 DDP path cfg|M6 OVL scan|M6 OVL diag cfg|M6 DDP timeout|M6 display: IF_VSYNC|IRQ: ovl0|dsi0|RDMA0"'
```

## 2026-06-03 OVL0 CPU layer mirror result

PATCH HISTORY, BOOT-UNBLOCK + DIAGNOSTIC, 2026-06-03: CPU-mirror active OVL0
layer programming for the M6 DIRECT_LINK/PQ-bypass path and keep the bounded
direct-route/OVL/fence diagnostics from the previous checkpoint.

Hypothesis: the clean DIRECT_LINK/PQ-bypass capture had moved past PQ, Smart
OVL decouple, route selection, clocks, and mutex membership, but the timeout
still showed stale or late OVL0 layer registers while the queued frame config
looked sane. In this tree, the active OVL layer config can remain only in CMDQ
while the first video wait is already observing stale hardware registers; for
the already-isolated M6 `OVL0 -> COLOR0 -> DITHER -> RDMA0 -> DSI0` route,
mirroring only the active non-secure OVL0 layer config with CPU writes should
prove whether stale OVL0 register programming is the remaining frontier.

Evidence: artifact
`/srv/forge/android/export/meizu_m6_artifacts/20260603-m6-ovl0-cpu-layer-mirror/boot-m6-ovl0-cpu-layer-mirror.img`
was built from branch `work/m6-rdma0-disp-decpq-20260531` and flashed to serial
`711HEBSR277K5`; local artifact and boot readback in capture
`/srv/forge/android/meizu_m6/captures/20260603-072431-m6-ovl0-cpu-layer-mirror-711HEBSR277K5/boot-hashes.txt`
both have sha256
`ad349802dda643c83998b7af3c363751da4918311658aa6ec24f06901fc1b9be`.
Matching payload identities in
`/srv/forge/android/export/meizu_m6_artifacts/20260603-m6-ovl0-cpu-layer-mirror/SHA256SUMS`
are `Image.gz-dtb`
`f0cf06c019b4d211bb27eb2e39c0d7e11cb75726884ad96bbfeff3e799f0244e`,
`System.map`
`5635678c5dc04f676c61159fb13c45ae5cc8941ccf7dfb878b298168ad04e5e5`,
config `bc272726035c1a2eca9422e9bc230cf54f8295648a8865a98faba046ab01619e`,
and ramdisk `e82c6695614132e8759b9ee96ee5b9e9efdaf8df96d1ef0c32c5dae8b5e16332`.

FACT: the verified boot reaches Android userspace. `boot-wait.txt` shows
`sys.boot_completed=1` by 07:24:43 with SurfaceFlinger and bootanim running;
`/proc/fb` reports `0 mtkfb`; fb0 is `U:720x1280p-0`; DisplayManager and
SurfaceFlinger report Built-in Screen 720x1280, rotation 0, state ON,
`powerMode=2`, `isDisplayOn=1`, and `flips=36`. `debugfs-mtkfb.txt` reports
`PathMode:DIRECT_LINK`, `LCM Driver=[ili9881p_hd_dsi_txd]`,
`DISP_OPT_BYPASS_PQ=1`, `RDMA0 Transfer=10`, and `DSI_EXT_TE=0`; `screencap.png`
is 0 bytes because `screencap -p` did not complete during the capture window.

FACT: the CPU mirror marker fired and changed the key observation. Dmesg lines
122-126 show `M6 OVL diag cfg[46]` for `mod=OVL0 L0` at `addr=0x1600000`,
`pitch=2880`, `fmt=RGBA8888`, then `M6 OVL cpu layer mirror[23]`, then the
same OVL0 layer config again. The first VSYNC timeout now decodes active OVL0
L0 hardware as `en=1`, `fmt=RGBA8888`, `addr=0x1600000`, `pitch=2880`,
`wh=720/1280`, `SRC=0x1`, while `OVL0_2L` is disabled and the stale L3 register
image is not enabled.

FACT: the display is still black/stalled after the mirror. The same timeout
shows route `VALID=0x3a READY=0x40009300`, `M0_MOD=0x51280`,
`M0_SOF=0x41`, clocks not gated for the primary path, `rdma0 GLOBAL=0x101
SIZE=720x1280 IN=0/0 OUT=0/0`, and `dsi0 START=0x1 STA=0x20`. IRQ deltas show
OVL0/RDMA0 interrupts rising while `dsi0` and `ovl0_2l` remain zero. Logcat
continues to show `GED Frame didn't finished in 1000 ms`, `[OVL-IN-0] fence
... didn't signal`, and `[WKR] Timed out waiting for Dispatcher_0`; dmesg keeps
`CMDQ_EVENT_DISP_RDMA0_EOF` token value 0 and repeated `IRQ: ovl0 -L0 not
complete until EOF` / `frame underflow`.

INFERENCE: stale active OVL0 register programming is closed as the primary
explanation. The frontier moved one layer lower: OVL0 has a sane enabled L0
register set, but OVL0 still does not produce a completed frame into RDMA0
(`IN=0/0 OUT=0/0`) and DSI0 never interrupts. The next evidence-backed work
should instrument OVL0 fetch/output state, SMI/M4U/GMC/FIFO state, and route
VALID/READY immediately around trigger/underflow, not CMDQ wait tokens, fences,
PQ bypass, Smart OVL, or panel DCS.

Files changed:
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_path.c` keeps the primary
  scanout scenarios routed through connected `OVL0` instead of assigning the
  first live layer to `OVL0_2L`.
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_manager.c` logs primary
  path config order, route/mutex/RDMA/DSI/OVL timeout state, and decodes both
  OVL0 and OVL0_2L layers.
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_ovl.c` logs OVL scan
  decisions, clears stale inactive OVL0 layer enables, and mirrors the active
  non-secure OVL0 layer config with CPU writes only under M6 DIRECT_LINK +
  `DISP_OPT_BYPASS_PQ`.
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/primary_display.c` keeps the
  M6 PQ-bypass path in DIRECT_LINK for this OVL0 proof and logs present/OVL
  fence timeout state.
- `BRINGUP_STATE.md` records the artifact identity, capture, conclusion, and
  next diagnostic boundary.

Why each file changed: `ddp_path.c` and `primary_display.c` isolate the proven
direct OVL0 route so old PQ/RDMA0-DISP decouple paths do not mask the failure.
`ddp_manager.c` owns the timeout snapshots needed to prove route/mutex/RDMA/DSI
state from the exact flashed image. `ddp_ovl.c` owns both layer assignment and
hardware layer programming, so the CPU mirror is the narrowest test for stale
OVL0 registers without touching waits, fences, CMDQ tokens, DSI, panel init, or
PQ registers. The state file is the required durable evidence record.

Expected next marker: a follow-up DIAGNOSTIC build should keep the same
DIRECT_LINK route and add read-only markers that answer whether OVL0 is failing
at memory fetch (`RDMA0_DBG`, `GMC`, `FIFO`, M4U/SMI state), at OVL output
handoff (`FLOW_CTRL_DBG`, `ADDCON_DBG`, `DATAPATH_CON`, abnormal SOF/underflow
reason), or at route consumption by RDMA0 (`VALID/READY`, `RDMA_IN/OUT`,
mutex SOF/MOD immediately after trigger and at underflow). A useful positive
marker is RDMA0 `IN/OUT` becoming nonzero or a decoded OVL0 fetch fault that
explains why it remains zero.

Rollback condition: revert the CPU mirror if a verified boot regresses before
root ADB, `sys.boot_completed`, SurfaceFlinger/fb0, or if a later capture proves
CPU mirroring active OVL0 config creates a worse hardware state than pure CMDQ
with the same direct route. Do not revert it merely because the panel is still
black; this capture proves it closed the stale-L0-register question while
exposing the OVL0 fetch/output frontier.

Verification commands:

```bash
sha256sum -c /srv/forge/android/export/meizu_m6_artifacts/20260603-m6-ovl0-cpu-layer-mirror/SHA256SUMS
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 exec-out 'dd if=/dev/block/platform/mtk-msdc.0/11230000.msdc0/by-name/boot bs=8863744 count=1 2>/dev/null' | sha256sum
grep -R -n -E 'M6 OVL cpu layer mirror|M6 OVL diag cfg|M6 DDP timeout\\[VSYNC\\]: (route|mutex|rdma0|ovl0|ovl0_2l|dsi0)|CMDQ_EVENT_DISP_RDMA0_EOF|IRQ: ovl0' /srv/forge/android/meizu_m6/captures/20260603-072431-m6-ovl0-cpu-layer-mirror-711HEBSR277K5
grep -R -n -E 'boot_completed=1|Built-in Screen|PathMode:DIRECT_LINK|DISP_OPT_BYPASS_PQ|RDMA0 Transfer|dsi0' /srv/forge/android/meizu_m6/captures/20260603-072431-m6-ovl0-cpu-layer-mirror-711HEBSR277K5
```

## 2026-06-03 OVL0 fetch/output IRQ diagnostics

PATCH HISTORY, DIAGNOSTIC, 2026-06-03: add read-only OVL0 IRQ and trigger-loop
snapshots for the first post-mirror fetch/output frontier.

Hypothesis: after `boot-m6-ovl0-cpu-layer-mirror.img`, OVL0 L0 is now
programmed with the expected 720x1280 RGBA layer, but the engine still reports
frame underflow, L0 not complete until EOF, abnormal SOF, and RDMA0 `IN=0/0
OUT=0/0`. The next missing evidence is whether OVL0 is starving on memory fetch
or failing to hand valid pixels to the downstream route. Bounded read-only
markers in the OVL0 IRQ handler and just before the trigger loop wait should
capture the same failure at the exact hardware boundary without changing waits,
tokens, fences, route, PQ, DSI, or panel state.

Evidence: verified capture
`/srv/forge/android/meizu_m6/captures/20260603-072431-m6-ovl0-cpu-layer-mirror-711HEBSR277K5`
matches boot sha256
`ad349802dda643c83998b7af3c363751da4918311658aa6ec24f06901fc1b9be` and
matching `System.map`
`/srv/forge/android/export/meizu_m6_artifacts/20260603-m6-ovl0-cpu-layer-mirror/System.map`
sha256 `5635678c5dc04f676c61159fb13c45ae5cc8941ccf7dfb878b298168ad04e5e5`.
Dmesg lines 122-126 show `M6 OVL cpu layer mirror[23]` for `OVL0 L0` at
`addr=0x1600000`, `pitch=2880`, `fmt=RGBA8888`. Dmesg lines 140-159 show
direct route registers, mutex `M0_MOD=0x51280`, RDMA0 `IN=0/0 OUT=0/0`, OVL0
`SRC=0x1`, and OVL0 L0 decoded as enabled with the expected address/pitch/size.
Dmesg and logcat still show repeated `CMDQ_EVENT_DISP_RDMA0_EOF`,
`IRQ: ovl0 -L0 not complete until EOF`, `IRQ: ovl0 frame underflow`, and HWC
`[OVL-IN-0]` fence timeouts. Interrupt snapshots show OVL0/RDMA0 increasing and
DSI0 staying zero.

Files changed:
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_irq.c` adds bounded
  `M6 OVL irq diag[...]` snapshots on OVL0 underflow/not-complete/abnormal-SOF
  IRQs, including OVL flow/addcon/SMI/GREQ, L0 RDMA/GMC/FIFO/debug, route
  valid/ready, mutex, and RDMA0 counters.
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/primary_display.c` extends
  the existing `M6 trigger dump[...]` with RDMA0 FIFO/GMC/pitch/stall state and
  OVL0 fetch/GREQ/FIFO/debug state before the trigger wait.
- `BRINGUP_STATE.md` records the diagnostic hypothesis, evidence, expected next
  marker, rollback condition, and verification commands.

Why each file changed: `ddp_irq.c` is the earliest code path that observes the
actual OVL0 underflow/not-complete/abnormal-SOF IRQ reported by the current
capture. `primary_display.c` already owns the M6 trigger-loop diagnostic and can
show pre-wait OVL/RDMA fetch state before the IRQ storm. The state file keeps
the diagnostic marker contract synchronized with the code.

Expected next marker: the next verified boot should emit `M6 OVL irq diag[...]`
near the first OVL0 underflow/not-complete IRQ and expanded `M6 trigger
dump[before-wait]` lines. A useful result is one of: OVL0 RDMA debug shows SMI
GREQ/busy or FIFO starvation while RDMA0 input remains zero; OVL0 output valid
state is present but RDMA0 does not consume it; or trigger-time state already
differs from timeout-time state enough to identify ordering. If the markers
show healthy OVL0 fetch/output but RDMA0 still has zero input, the frontier
moves to route consumption/RDMA0 input gating rather than OVL memory fetch.

Rollback condition: revert this diagnostic if a verified boot regresses before
root ADB/SurfaceFlinger/fb0 or if the extra IRQ logging makes the device
unusable or hides earlier evidence. Do not revert for continued black display
alone; this patch is observation-only.

Verification commands:

```bash
git diff --check
make -C /srv/forge/android/meizu_m6/kernel-meizu_M6-N-ex6-linux-3.18.140/kernel-3.18 O=/home/n8n/forge-work/kernel-builds/m6-directlink-smartovl-20260602/out ARCH=arm64 CROSS_COMPILE=/srv/forge/android/meizu_m6/rom-meizu_M6-lineage-cm-14.1/prebuilts/gcc/linux-x86/aarch64/aarch64-linux-android-4.9/bin/aarch64-linux-android- -j4 Image.gz-dtb
grep -R -n -E 'M6 OVL irq diag|M6 trigger dump\\[before-wait\\]: (rdma0 fetch|ovl0 fetch)|M6 DDP timeout\\[VSYNC\\]|CMDQ_EVENT_DISP_RDMA0_EOF|IRQ: ovl0' <next-capture>
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell 'dmesg | grep -E "M6 OVL irq diag|M6 trigger dump\\[before-wait\\]: (rdma0 fetch|ovl0 fetch)|RDMA0_EOF|IRQ: ovl0"'
```

Test result: artifact
`/srv/forge/android/export/meizu_m6_artifacts/20260603-m6-ovl0-fetch-irqdiag/boot-m6-ovl0-fetch-irqdiag.img`
was flashed to serial `711HEBSR277K5`; local image and boot readback in
`/srv/forge/android/meizu_m6/captures/20260603-075401-m6-ovl0-fetch-irqdiag-711HEBSR277K5/boot-hashes.txt`
both have sha256
`2b6507360c1fa13d20a0abe6de00c13fa07ae397f151ebfbe68c517e7479df74`.
Matching build outputs in
`/srv/forge/android/export/meizu_m6_artifacts/20260603-m6-ovl0-fetch-irqdiag/SHA256SUMS`
are `Image.gz-dtb`
`82bcd8f380904eadae9eaa0eb2664fa35597fa7af14af1d918201b5b3d919478`,
`System.map`
`e57bb41f643cab71e93c2e2f17f44aa0589b92913bfd9eb5c726280e83dbe426`,
config `bc272726035c1a2eca9422e9bc230cf54f8295648a8865a98faba046ab01619e`,
and ramdisk `e82c6695614132e8759b9ee96ee5b9e9efdaf8df96d1ef0c32c5dae8b5e16332`.

FACT: the verified boot reaches Android userspace again. `boot-wait.txt` shows
`sys.boot_completed=1` by `2026-06-03T07:54:21-05:00`, with SurfaceFlinger,
bootanimation, and the `input` service present. `dumpsys-sf.txt` reports
Built-in Screen and HWC BootAnimation plus HWC framebuffer target layers.
`mtkfb-debugfs.txt` reports `PathMode:DIRECT_LINK`, `RDMA0 Transfer=10`, and
`DISP_OPT_BYPASS_PQ=1`; `screencap -p` still times out and the captured fb0
head is all zero for the first 262144 bytes.

FACT: the new IRQ marker fired at the live failure boundary. `dmesg.txt` lines
2719-2720 show `M6 OVL irq diag[5120]` with `intsta=0x2034`, OVL0
`sta=0x1d`, `en=0x1`, `src=0x1`, `flow=0x8071020`, `addcon=0x400c`,
`smi=0x2`, `greq=0x10ff5555`, route `valid=0x3a ready=0x40009300`,
mutex `0x51280/0x41`, RDMA0 `rdma=0x101 in=0/0 out=0/0`, and L0
`con=0x10020ff`, `size=0x50002d0`, `addr=0x1600000`, `pitch=0x10000b40`,
`rdma_ctrl=0x880001`, `gmc=0xffff`, `fifo=0x900000`, `buflow=0x0`,
`rdma_dbg=0x20000001`.

FACT: the timeout path remains the same direct OVL0 route. `dmesg.txt` lines
191-223 and repeated later snapshots show route `OVL0 -> COLOR0 -> DITHER ->
RDMA0 -> DSI0`, `M0_MOD=0x51280`, `M0_SOF=0x41`, OVL0 L0 decoded as enabled
RGBA8888 at MVA `0x01600000`, RDMA0 `IN=0/0 OUT=0/0`, and DSI0
`START=1 STA=0x20`. Interrupt snapshots show OVL0 and RDMA0 counters increase
between `interrupts-before.txt` and `interrupts-after.txt`, while `dsi0`
remains zero.

INFERENCE: OVL0 layer programming, direct route selection, PQ bypass isolation,
and Android service startup are now closed as the first blocker. The remaining
display frontier is below the HWC handoff but before RDMA0 consumes pixels:
either the HWC/ion MVA content is blank or unmapped for OVL, OVL0 is not
actually fetching useful data despite a sane register set, or RDMA0 is not
accepting OVL output from the route. The next diagnostic must sample the
enabled OVL0 HWC/MVA buffer through M4U at handoff and log query/map/content
state; do not patch CMDQ EOF waits, fence release, token seeding, PQ, Smart OVL,
or panel DCS from this evidence.

## 2026-06-03 OVL0 HWC/M4U buffer content sampler

PATCH HISTORY, DIAGNOSTIC, 2026-06-03: sample the active OVL0 HWC buffer through
M4U at the pre-DPMGR handoff.

Hypothesis: the verified OVL0 IRQ diagnostic closed stale layer registers and
showed a sane direct OVL0 route, but RDMA0 still reports `IN=0/0 OUT=0/0` and
DSI0 never interrupts. The next missing fact is whether the active HWC/ion MVA
given to OVL0 contains real pixels and can be mapped by M4U from the kernel.
If M4U query/map fails or a strided sample is all zero, the frontier moves to
HWC/gralloc/ion/M4U buffer production or mapping. If the sample is nonzero
while OVL0 still underflows and RDMA0 input remains zero, the frontier stays in
OVL fetch/output or RDMA route consumption.

Evidence: verified test capture
`/srv/forge/android/meizu_m6/captures/20260603-075401-m6-ovl0-fetch-irqdiag-711HEBSR277K5`
matches boot/readback sha256
`2b6507360c1fa13d20a0abe6de00c13fa07ae397f151ebfbe68c517e7479df74` and
matching `System.map`
`/srv/forge/android/export/meizu_m6_artifacts/20260603-m6-ovl0-fetch-irqdiag/System.map`
sha256 `e57bb41f643cab71e93c2e2f17f44aa0589b92913bfd9eb5c726280e83dbe426`.
Dmesg lines 2719-2720 show `M6 OVL irq diag[5120]` with OVL0 L0
`addr=0x1600000`, `size=0x50002d0`, `pitch=0x10000b40`, `rdma_ctrl=0x880001`,
`gmc=0xffff`, `fifo=0x900000`, route `valid=0x3a ready=0x40009300`, mutex
`0x51280/0x41`, and RDMA0 `in=0/0 out=0/0`. `boot-wait.txt` proves
`sys.boot_completed=1`, SurfaceFlinger, bootanimation, and input service are
alive. `dumpsys-sf.txt` shows BootAnimation as an HWC layer and
`HWC_FRAMEBUFFER_TARGET`; `screencap -p` still times out and fb0 head is zero.

Follow-up FACT: verified capture
`/srv/forge/android/meizu_m6/captures/20260603-082037-m6-ovl0-m4u-sample-711HEBSR277K5`
matches local/readback boot sha256
`0d305200808cb22c1a8a42e5d0e6149a910b7c5434c2a48dd525ef574be4da4e`, and the
tested kernel strings contain `M6 OVL m4u sample[...]`. The live dmesg ring
starts at `91.472421s`, while the first visible OVL handoff in debugfs is
`M6 OVL handoff[45:pre-dpmgr]`; no `M6 OVL m4u sample[...]` line survives in
dmesg/debugfs/logcat. This means the first-8-only sampler cadence was too
sparse for the capture window, not that the buffer content question is closed.
The diagnostic now samples the first 96 eligible OVL0 handoffs and then every
256th handoff.

Test result FACT: wide-sampler artifact
`/srv/forge/android/export/meizu_m6_artifacts/20260603-m6-ovl0-m4u-sample-wide/boot-m6-ovl0-m4u-sample-wide.img`
sha256 `8095627bcbffcecc55ddf9c4b5bca06c245d4d3b28861cf34b2ad7e1116c225a`
was flashed only to serial `711HEBSR277K5`; readback
`/srv/forge/android/export/meizu_m6_artifacts/20260603-m6-ovl0-m4u-sample-wide/readback-boot-m6-ovl0-m4u-sample-wide.img`
matches the same sha256 with `cmp_exit=0`. Fresh capture
`/srv/forge/android/meizu_m6/captures/20260603-084232-m6-ovl0-m4u-sample-wide-711HEBSR277K5`
matches local/readback boot sha256 and reaches `sys.boot_completed=1` at
`2026-06-03T08:42:38-05:00` with SurfaceFlinger, bootanimation, input, and a
720x1280 `Built-in Screen` in state `ON`. The direct route/PQ state remains
`PathMode:DIRECT_LINK`, `DISP_OPT_BYPASS_PQ=1`, and `RDMA0 Transfer=10`.

Test result FACT: the sampler proves M4U query/map succeeds for the active OVL0
layer but the sampled framebuffer content is zero. `dmesg.txt:551` reports
`M6 OVL m4u sample[21:pre-dpmgr]` for `mva=0x1600000`, `layer=0x384000`,
`real=0x1600000/0x384000`, `map=0x384000/0x384000`, `words=921600`,
`samples=256`, `nonzero=0`, `xor=0x0`, `sum=0x0`, first eight words all
`00000000`, format `RGBA8888/0xc00a08`, 720x1280. `dmesg.txt:2046` reports
the same all-zero result for sample `[23:pre-dpmgr]`; `mtkfb-debugfs.txt:425`
reports the same all-zero result for sample `[31:pre-dpmgr]`.

INFERENCE: this closes "OVL0 is handed an unmappable/bad MVA" as the current
frontier for the sampled layer. The unresolved earliest display boundary is now
above or beside OVL fetch: HWC/gralloc/ION/cache/producer may be handing OVL0 a
valid allocated buffer that contains zeros, or the kernel-side M4U mapping may
be observing stale CPU-visible contents while the display engine still sees a
different cache state. It is not evidence for another PQ/CMDQ-wait/token-seed
patch.

HYPOTHESIS: SurfaceFlinger/HWC is presenting a framebuffer target or overlay
buffer before real pixels are written or flushed to the MVA consumed by OVL0.
The next patch cycle should inspect HWC/gralloc/ION/cache sync and, if needed,
add bounded producer-side markers around HWC set/prepare, gralloc allocation,
ion import/map, and display cache flush paths.

Files changed:
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/primary_display.c` adds
  `M6 OVL m4u sample[...]`, a bounded read-only sampler for active non-secure
  OVL0 memory layers in DIRECT_LINK + `DISP_OPT_BYPASS_PQ`, with a wider
  first-96-call window so the marker survives the current late dmesg capture.
- `BRINGUP_STATE.md` records the diagnostic hypothesis, evidence, expected
  marker, rollback condition, and verification commands.

Why each file changed: `primary_display.c` owns `_config_ovl_input()` and
already logs the `M6 OVL handoff[...]` marker immediately before DPMGR consumes
the layer. Sampling the exact OVL0 MVA at that boundary is the narrowest way to
prove whether the buffer is present/nonzero before changing any display
hardware behavior. The state file keeps the marker contract synchronized with
the code.

Expected next marker: the next capture or patch should show HWC/gralloc/ION
producer state for the same MVA `0x1600000`: allocation/import owner, usage
flags, cache flush/sync calls, and whether the userspace framebuffer target has
nonzero content before OVL0 handoff. If producer-side evidence shows nonzero
content and explicit cache sync while the kernel sampler still sees zeros,
reopen M4U/cache aliasing. If producer-side evidence also shows zeros, debug
SurfaceFlinger/HWC composition input rather than DDP hardware.

Rollback condition: revert this diagnostic if a verified boot regresses before
root ADB/SurfaceFlinger/fb0, if M4U kernel mapping destabilizes the device, or
if the extra logs hide earlier display evidence. Do not revert solely for
continued black display; this patch is observation-only.

Verification commands:

```bash
git diff --check
make -C /srv/forge/android/meizu_m6/kernel-meizu_M6-N-ex6-linux-3.18.140/kernel-3.18 O=/home/n8n/forge-work/kernel-builds/m6-directlink-smartovl-20260602/out ARCH=arm64 CROSS_COMPILE=/srv/forge/android/meizu_m6/rom-meizu_M6-lineage-cm-14.1/prebuilts/gcc/linux-x86/aarch64/aarch64-linux-android-4.9/bin/aarch64-linux-android- -j4 Image.gz-dtb
grep -R -n -E 'M6 OVL m4u sample|M6 OVL handoff|M6 OVL irq diag|M6 DDP timeout\\[VSYNC\\]: (route|mutex|rdma0|ovl0|dsi0)|CMDQ_EVENT_DISP_RDMA0_EOF' <next-capture>
grep -R -n -E 'hwc|HWC|gralloc|ion|ION|GraphicBuffer|Framebuffer|FB target|cache|flush|sync' <next-capture>
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell 'dmesg | grep -E "M6 OVL m4u sample|M6 OVL handoff|M6 OVL irq diag|RDMA0_EOF"'
```

## 2026-06-03 OVL/RDMA route bit and GREQ decode diagnostics

PATCH HISTORY, DIAGNOSTIC, 2026-06-03: decode direct-route `VALID/READY` bits
and distinguish OVL RDMA GREQ config from real SMI LARB0 GREQ state.

Hypothesis: after the live HWC-off SurfaceFlinger isolation, the HWC overlay
producer is no longer the first display frontier. SurfaceFlinger can force
GLES/client composition and OVL0 can receive a different framebuffer-target
MVA, sometimes with sparse nonzero sampled content, but RDMA0 still reports
`IN=0/0 OUT=0/0`. The next missing fact is which direct-route handoff bit is
valid/not-ready or ready/not-valid, and whether the repeated `0x10ff5555`
marker is only the OVL RDMA golden-setting register rather than an SMI grant
failure.

Evidence: live HWC-off capture
`/srv/forge/android/meizu_m6/captures/20260603-101011-m6-hwc-off-sf-live-711HEBSR277K5`
keeps the verified kernel artifact
`/srv/forge/android/export/meizu_m6_artifacts/20260603-m6-ovl0-m4u-sample-wide/boot-m6-ovl0-m4u-sample-wide.img`
sha256 `8095627bcbffcecc55ddf9c4b5bca06c245d4d3b28861cf34b2ad7e1116c225a`,
while replacing only userspace `libsurfaceflinger.so`. The capture has
`debug.sf.disable_hwc=1`; `dumpsys SurfaceFlinger` shows `BootAnimation#0` as
`GLES` plus an `HWC_FRAMEBUFFER_TARGET`; `M6 OVL m4u sample[37:pre-dpmgr]`
for `mva=0x1e00000` reports `samples=256`, `nonzero=8`, `first_nz=184721`,
and `sum=0x7f8000000`; the same runtime still has `PathMode:DIRECT_LINK`,
`DISP_OPT_BYPASS_PQ=1`, OVL underflow/not-complete IRQs, and RDMA0
`IN=0/0 OUT=0/0`. Timeout markers show `VALID=0x3a`, `READY=0x40009300`,
`LARB0_GREQ=0x0`, all relevant display clock gates ungated, and OVL IRQ
markers show `greq=0x10ff5555` from `DISP_REG_OVL_RDMA_GREQ_NUM`. Source
inspection of `ddp_ovl.c` confirms `0x10ff5555` is the configured OVL golden
setting for layer GREQ counts/OSTD/pre-ultra flush, not the
`DISP_REG_CONFIG_SMI_LARB0_GREQ` not-grant register.

INFERENCE: an SMI LARB0 not-grant condition is not proven by the current
`greq=0x10ff5555` marker. The useful next evidence is route handshake decode
and an unambiguous label for `ovl_greq` versus `larb0_greq`.

Files changed:
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_irq.c` renames the OVL
  IRQ `greq` field to `ovl_greq`, adds `larb0_greq`, and decodes
  `DISP_REG_OVL_RDMA_GREQ_NUM` / `DISP_REG_OVL_RDMA_GREQ_URG_NUM` into layer
  GREQ, OSTD, flush, and urgency fields.
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_manager.c` adds a
  compact direct-route `VALID/READY` bit line for OVL0->COLOR,
  DITHER->RDMA0, RDMA0->DSI0, and DSI0 input bits beside the existing timeout
  dump.
- `BRINGUP_STATE.md` records the corrected interpretation and expected
  markers.

Why each file changed: `ddp_irq.c` owns the OVL0 underflow/not-complete IRQ
boundary where the ambiguous `greq=0x10ff5555` string currently appears.
`ddp_manager.c` owns the VSYNC/frame-done timeout dump where RDMA0 still has
zero input despite direct-route registers looking configured. Both changes are
read-only markers and do not touch route selection, CMDQ events, fences, PQ,
DSI, panel init, or clock enable behavior.

Expected next marker: the next verified boot should emit `M6 OVL irq diag[...]`
with `ovl_greq=...`, `larb0_greq=...`, and `ovl_greq decode ...`; VSYNC
timeout dumps should emit `M6 DDP timeout[VSYNC]: direct bits v/r ...`. A
useful result is either a specific direct-route bit that stays `valid=0` before
RDMA0, or proof that route bits are healthy while RDMA0 still has zero input,
which moves the frontier to RDMA0 input gating or DSI video acceptance.

Rollback condition: revert this diagnostic if it regresses before root
ADB/SurfaceFlinger/fb0, if the added logging hides earlier OVL/RDMA evidence,
or if build/runtime proves the register reads are unsafe on this path. Do not
revert solely for continued black display; this patch is observation-only.

Verification commands:

```bash
git diff --check
make -C /srv/forge/android/meizu_m6/kernel-meizu_M6-N-ex6-linux-3.18.140/kernel-3.18 O=/home/n8n/forge-work/kernel-builds/m6-directlink-smartovl-20260602/out ARCH=arm64 CROSS_COMPILE=/srv/forge/android/meizu_m6/rom-meizu_M6-lineage-cm-14.1/prebuilts/gcc/linux-x86/aarch64/aarch64-linux-android-4.9/bin/aarch64-linux-android- -j4 Image.gz-dtb
grep -R -n -E 'M6 OVL irq diag.*(ovl_greq|larb0_greq|ovl_greq decode)|M6 DDP timeout\\[VSYNC\\]: direct bits|M6 OVL m4u sample|RDMA0_EOF' <next-capture>
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell 'dmesg | grep -E "M6 OVL irq diag.*(ovl_greq|larb0_greq|ovl_greq decode)|M6 DDP timeout\\[VSYNC\\]: direct bits|RDMA0_EOF" | tail -120'
```

Test result FACT: route/GREQ decode artifact
`/srv/forge/android/export/meizu_m6_artifacts/20260603-m6-ovl-rdma-greq-route-decode/boot-m6-ovl-rdma-greq-route-decode.img`
sha256 `bc1a588cc3f4887ab210af75eb48894ae9ba855cfb9af30bfe7a6f24b4cc43be`
was flashed only to serial `711HEBSR277K5`; readback
`/srv/forge/android/export/meizu_m6_artifacts/20260603-m6-ovl-rdma-greq-route-decode/readback-boot-m6-ovl-rdma-greq-route-decode.img`
matches the same sha256 with `cmp_exit=0`. Matching `System.map` is
`/srv/forge/android/export/meizu_m6_artifacts/20260603-m6-ovl-rdma-greq-route-decode/System.map`
sha256 `3ea46eaee9f7e6adf5ee967f6879d46b2e88789e2ee6f524bdf71bfdac37878c`.
Fresh captures
`/srv/forge/android/meizu_m6/captures/20260603-103810-m6-ovl-rdma-greq-route-decode-711HEBSR277K5`
and
`/srv/forge/android/meizu_m6/captures/20260603-104050-m6-hwc-off-ovl-rdma-greq-route-decode-711HEBSR277K5`
both reach `sys.boot_completed=1`.

Test result FACT: HWC-active and HWC-off captures both keep the same failing
hardware boundary. Runtime has `PathMode:DIRECT_LINK`, `DISP_OPT_BYPASS_PQ=1`,
OVL underflow/not-complete IRQs, RDMA0 `IN=0/0 OUT=0/0`, and route
`VALID=0x3a READY=0x40009300`. `M6 OVL irq diag[...]` now distinguishes
`ovl_greq=0x10ff5555` from `larb0_greq=0x0`; decoded `ovl_greq` reports layer
GREQ `5/5/5/5`, OSTD `0xff`, and pre-ultra flush set. This rejects the
previous SMI LARB0 not-grant interpretation for this marker.

INFERENCE: `VALID=0x3a` means bits 1, 3, 4, and 5 are valid while bits 6, 7,
8, 9, 12, 15, and 30 are not valid. `READY=0x40009300` means the downstream
tail beginning at DITHER/RDMA0/DSI0 is ready. The proven direct-link hole is
between `CCORR->AAL` and `DITHER`. Source inspection shows the active M6
`DISP_OPT_BYPASS_PQ` isolation clears `CCORR`, `AAL`, and `GAMMA` from the
mutex even though the scenario route still contains
`COLOR0 -> CCORR -> AAL -> GAMMA -> DITHER`. That behavior can prevent
`AAL->GAMMA` / `GAMMA->DITHER` VALID propagation before RDMA0.

## 2026-06-03 PQ bridge mutex pass-through fix

PATCH HISTORY, BOOT-UNBLOCK, 2026-06-03: keep the required CCORR/AAL/GAMMA
bridge modules in the primary direct-link mutex while `DISP_OPT_BYPASS_PQ` is
enabled.

Hypothesis: the display stack reaches Android, SurfaceFlinger, HWC-off GLES
composition, OVL0 layer programming, and the direct OVL0 path, but RDMA0 still
never receives input because the M6 PQ bypass isolation removes physical bridge
modules from mutex membership. PQ bypass should disable picture processing
effects, not remove the CCORR/AAL/GAMMA hardware path that connects COLOR0 to
DITHER. Keeping these modules in the mutex should allow VALID to propagate
through bits 6/7 to DITHER/RDMA0 and advance the frontier from RDMA0 input
starvation to either DSI/panel output or producer content.

Evidence: verified artifact and captures from the preceding route/GREQ
diagnostic are listed above. The decisive marker is `VALID=0x3a` and
`READY=0x40009300` in both HWC-active and HWC-off captures, with RDMA0
`IN=0/0 OUT=0/0` and `larb0_greq=0x0`. The route bit map in `ddp_dump.h`
defines bit 4 as `COLOR__CCORR`, bit 5 as `CCORR__AAL`, bit 6 as
`AAL__GAMMA`, bit 7 as `GAMMA__DITHER`, bit 8 as `DITHER__DITHER_MOUT`,
bit 9 as `DITHER_MOUT0__RDMA0`, bit 12 as `RDMA0__RDMA0_SOUT`, bit 15 as
`RDMA0_SOUT2__DSI0_SIN1`, and bit 30 as `DIS0_SEL__DSI0`. The source path in
`ddp_path.c` had been clearing `DISP_MODULE_CCORR`, `DISP_MODULE_AAL`, and
`DISP_MODULE_GAMMA` from the mutex under `DISP_OPT_BYPASS_PQ`.

Files changed:
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_path.c` changes the M6
  direct-link PQ isolation mask so it clears only stale `OVL1_2L` and keeps
  `CCORR`, `AAL`, and `GAMMA` in the mutex; the log marker now says
  `M6 DDP mutex isolate: keep PQ bridge ...`.
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_manager.c` expands the
  direct-route marker to print bits 0, 1, 3, 4, 5, 6, 7, 8, 9, 12, 15, and 30
  so the next capture can prove exactly where VALID/READY stops.
- `BRINGUP_STATE.md` records the behavior fix, evidence, expected marker,
  rollback condition, and verification commands.

Why each file changed: `ddp_path.c` owns mutex module programming for the
primary display scenarios and contains the proven M6 isolation mask. The path
still routes through the PQ bridge modules, so keeping them in mutex is the
minimum behavior change tied to the route-bit evidence. `ddp_manager.c` owns
the timeout register dump and must show the previously omitted bridge bits that
falsify or confirm the fix.

Expected next marker: the next verified boot should log
`M6 DDP mutex isolate: keep PQ bridge ... clear=0x...` with only the `OVL1_2L`
mask cleared. Mutex `M0_MOD` should retain the CCORR/AAL/GAMMA bits and the
direct route marker should show `aal_gamma`, `gamma_dither`, `dither_out`, and
`dither_rdma` as valid/ready or move the failure to a later named bit. RDMA0
should begin incrementing `IN/OUT` or the capture should prove a later DSI/panel
or producer-content boundary.

Rollback condition: revert this patch if a verified boot regresses before root
ADB/SurfaceFlinger/fb0, if the mutex now includes the PQ bridge but route bits
or RDMA0 counters prove no progress and a stock/donor source comparison shows
these modules must be excluded in this mode, or if the new markers prove the
first failing boundary is earlier than the PQ bridge. Do not revert solely for
continued black screen until the route bits and RDMA0 counters are compared.

Verification commands:

```bash
git diff --check
make -C /srv/forge/android/meizu_m6/kernel-meizu_M6-N-ex6-linux-3.18.140/kernel-3.18 O=/home/n8n/forge-work/kernel-builds/m6-directlink-smartovl-20260602/out ARCH=arm64 CROSS_COMPILE=/srv/forge/android/meizu_m6/rom-meizu_M6-lineage-cm-14.1/prebuilts/gcc/linux-x86/aarch64/aarch64-linux-android-4.9/bin/aarch64-linux-android- -j4 Image.gz-dtb
grep -R -n -E 'M6 DDP mutex isolate: keep PQ bridge|M6 DDP timeout\\[VSYNC\\]: direct bits|M6 OVL irq diag.*(ovl_greq|larb0_greq)|RDMA0_EOF|M6 OVL m4u sample' <next-capture>
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell 'dmesg | grep -E "M6 DDP mutex isolate: keep PQ bridge|M6 DDP timeout\\[VSYNC\\]: direct bits|M6 OVL irq diag.*(ovl_greq|larb0_greq)|RDMA0_EOF" | tail -160'
```

Test result FACT: PQ bridge mutex pass-through artifact
`/srv/forge/android/export/meizu_m6_artifacts/20260603-m6-pq-bridge-mutex-pass-through/boot-m6-pq-bridge-mutex-pass-through.img`
sha256 `e428c9b8c50a0f8d72ad1fc0bb9a70f62611c77599fa197bbe818624ccc4214a`
was flashed only to serial `711HEBSR277K5`; readback
`/srv/forge/android/export/meizu_m6_artifacts/20260603-m6-pq-bridge-mutex-pass-through/readback-boot-m6-pq-bridge-mutex-pass-through.img`
matches the same sha256 with `cmp_exit=0`. Matching `Image.gz-dtb` is
`/srv/forge/android/export/meizu_m6_artifacts/20260603-m6-pq-bridge-mutex-pass-through/Image.gz-dtb`
sha256 `8810425c67d22825dbfb7de25759615f10467283deb5beb44e8f4def42d40213`;
matching `System.map` is
`/srv/forge/android/export/meizu_m6_artifacts/20260603-m6-pq-bridge-mutex-pass-through/System.map`
sha256 `02e44abdd7c1b0194c5c6d5589a33f88d5cc96b9f590f2885906a42af9c9dce3`.
The fresh capture is
`/srv/forge/android/meizu_m6/captures/20260603-110328-m6-pq-bridge-mutex-pass-through-711HEBSR277K5`;
it reaches `sys.boot_completed=1` and `init.svc.bootanim=stopped`.

Test result FACT: the direct-link starvation moved forward. The capture has
`M6 DDP mutex isolate: keep PQ bridge scenario=primary_disp mutex=0 MOD
0x5f280 queued=0x5f280 now=0x5f280 clear=0x100000`, proving the bridge modules
remained in mutex membership and only stale `OVL1_2L` was cleared. Runtime
still reports `PathMode:DIRECT_LINK` and `DISP_OPT_BYPASS_PQ=1`, but RDMA0 now
transfers: `RDMA0 Transfer` count `8528`, `Primary Path Trigger` count `1214`,
and route state includes `VALID=0x4000937a` with nonzero RDMA `IN/OUT` counters
such as `IN=608/992 OUT=42/989`.

Test result FACT: Android's internal composition is no longer black. Capture
file
`/srv/forge/android/meizu_m6/captures/20260603-110328-m6-pq-bridge-mutex-pass-through-711HEBSR277K5/screen.png`
is a valid `720x1280` PNG sha256
`9071e79080656e8aed1793500cf7e86f4c699b612d2b26bf7cea73c43ed4491b`,
showing the lock screen wallpaper, status bar, notifications, and charging
state. `dumpsys SurfaceFlinger` shows the built-in screen at `720x1280`,
`powerMode=2`, `isDisplayOn=1`, HWC enabled, and layers including
`ImageWallpaper#0`, `StatusBar#0`, and `HWC_FRAMEBUFFER_TARGET`. `fb0-head.raw`
is still all zero, but that is not decisive now because composition is through
HWC/overlay rather than a linear fb0 scanout.

Test result FACT: live backlight state was low after boot and was raised for
visual confirmation. Before the live write,
`/sys/class/leds/lcd-backlight/brightness=10` while max brightness is `255` and
Display Power is ON. The live command sequence in
`/srv/forge/android/meizu_m6/captures/20260603-110328-m6-pq-bridge-mutex-pass-through-711HEBSR277K5/live-brightness-255.txt`
sets manual brightness and writes `255` to the LCD backlight sysfs node; the
node then reads back `255`.

INFERENCE: the PQ bridge mutex patch closes the previously proven
OVL/RDMA direct-link starvation. If the physical LCD is still black after the
brightness-255 live write, the next frontier is DSI/panel/backlight electrical
output or panel command acceptance, not SurfaceFlinger, HWC producer content,
OVL MVA content, PQ bridge mutex membership, or RDMA0 transfer.

Expected next marker: human visual confirmation after the brightness-255 live
write. If the panel is still physically black, run a DSI BIST/color-pattern
visual test and add a bounded DSI/MIPITX register or DCS-readback diagnostic
around `ddp_dsi_start`, `ddp_dsi_config`, panel init, and backlight command
paths.

Verification commands:

```bash
grep -R -n -E 'M6 DDP mutex isolate: keep PQ bridge|PathMode:DIRECT_LINK|DISP_OPT_BYPASS_PQ|RDMA0 Transfer|VALID=0x4000937a|lcm_setbacklight|backlight' /srv/forge/android/meizu_m6/captures/20260603-110328-m6-pq-bridge-mutex-pass-through-711HEBSR277K5
sha256sum /srv/forge/android/export/meizu_m6_artifacts/20260603-m6-pq-bridge-mutex-pass-through/boot-m6-pq-bridge-mutex-pass-through.img /srv/forge/android/export/meizu_m6_artifacts/20260603-m6-pq-bridge-mutex-pass-through/readback-boot-m6-pq-bridge-mutex-pass-through.img /srv/forge/android/meizu_m6/captures/20260603-110328-m6-pq-bridge-mutex-pass-through-711HEBSR277K5/screen.png
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell 'cat /sys/class/leds/lcd-backlight/brightness; dumpsys SurfaceFlinger | grep -E "powerMode|isDisplayOn|Display 0|Built-in Screen"; dumpsys window displays | grep -E "DisplayFrames|mDisplayId|cur=|app="'
```

## 2026-06-03 DSI BIST register snapshot diagnostic

PATCH HISTORY, DIAGNOSTIC, 2026-06-03: log bounded DSI BIST, DSI state, and
MIPITX state snapshots around the existing `/d/mtkfb` `dsipattern` command.

Hypothesis: after the PQ bridge mutex fix, Android composition and RDMA0
transfer are live. If the physical LCD remains black, the next hardware
boundary is whether DSI0 actually enters self-pattern mode and whether the DSI
PHY/lane state is sane while backlight is at `255`. The existing
`echo dsipattern:0x00ff0000 > /d/mtkfb` command logs only that the parser
accepted the command; it does not prove `DSI_BIST_CON.SELF_PAT_MODE`,
`DSI_BIST_PATTERN`, DSI state machines, or MIPITX lanes after the write.

Evidence: the live DSI BIST probe
`/srv/forge/android/meizu_m6/captures/20260603-111135-m6-dsi-bist-red-711HEBSR277K5`
ran against serial `711HEBSR277K5` with the verified PQ bridge artifact still
booted. The capture records `dsipattern:0x00ff0000` and
`enable dsi pattern: 0x00ff0000` in the `mtkfb` debug buffer, RDMA0 transfer
continuing near 60.9 fps, `PathMode:DIRECT_LINK`, and backlight `255`. It does
not expose the BIST register state, so it cannot distinguish "test pattern
accepted and transmitted but panel does not show it" from "debugfs command
accepted but BIST bit did not stick or DSI/PHY is in the wrong state".

Files changed:
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_dsi.c` extends the
  existing `M6 DSI snapshot[...]` marker with `DSI_BIST_PATTERN`,
  `DSI_BIST_CON`, decoded `SELF_PAT_MODE`, BIST mode/enable/fixed-pattern lane
  fields, checksum/debug-select registers, and bounded pre/post snapshots
  around `DSI_BIST_Pattern_Test()` enable and disable paths.
- `BRINGUP_STATE.md` records the diagnostic hypothesis, evidence, expected
  marker, rollback condition, and verification commands.

Why each file changed: `ddp_dsi.c` owns both the DSI self-pattern register
write and the existing M6 DSI snapshot helper, so this is the narrowest
read-only way to prove the hardware state produced by the already-available
debugfs command. No DSI timing, panel init command, mutex, route, RDMA, OVL, or
backlight behavior is changed.

Expected next marker: after flashing the diagnostic artifact and running
`echo dsipattern:0x00ff0000 > /d/mtkfb`, dmesg should contain
`M6 DSI snapshot[bist-pre-enable]` followed by
`M6 DSI snapshot[bist-post-enable]` with `BIST_PATTERN=0xff0000` or
`0x00ff0000` and `self_pat=1`. After `echo dsipattern:0 > /d/mtkfb`, dmesg
should contain `bist-pre-disable` and `bist-post-disable` with `self_pat=0`.
If post-enable proves `self_pat=1` while the user sees no solid color, move to
panel command acceptance, MIPI lane/electrical state, reset/power GPIO, and
backlight wiring. If `self_pat` does not stick, debug DSI register writes,
clock gating, or DSI reset/state instead.

Rollback condition: revert this diagnostic if it regresses before root
ADB/SurfaceFlinger/fb0, if the added snapshots flood logs enough to hide
earlier markers, or if register reads prove unsafe. Do not revert solely for a
continued physical black screen; this patch is observation-only.

Verification commands:

```bash
git diff --check
make -C /srv/forge/android/meizu_m6/kernel-meizu_M6-N-ex6-linux-3.18.140/kernel-3.18 O=/home/n8n/forge-work/kernel-builds/m6-directlink-smartovl-20260602/out ARCH=arm64 CROSS_COMPILE=/srv/forge/android/meizu_m6/rom-meizu_M6-lineage-cm-14.1/prebuilts/gcc/linux-x86/aarch64/aarch64-linux-android-4.9/bin/aarch64-linux-android- -j4 Image.gz-dtb
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell 'echo 255 > /sys/class/leds/lcd-backlight/brightness; echo dsipattern:0x00ff0000 > /d/mtkfb; sleep 8; echo dsipattern:0 > /d/mtkfb; dmesg | grep -E "M6 DSI snapshot\\[bist-(pre|post)-(enable|disable)\\]|BIST_PATTERN|self_pat" | tail -120'
```

Test result FACT: DSI BIST snapshot artifact
`/srv/forge/android/export/meizu_m6_artifacts/20260603-m6-dsi-bist-snapshot/boot-m6-dsi-bist-snapshot.img`
sha256 `559006b2d8f888fc75cd7fb9e60834117ceb12b014a168bcde955575b48d0e63`
was flashed only to serial `711HEBSR277K5`; readback
`/srv/forge/android/export/meizu_m6_artifacts/20260603-m6-dsi-bist-snapshot/readback-boot-m6-dsi-bist-snapshot.img`
matches the same sha256 with `cmp_exit=0`. Matching `Image.gz-dtb` is
`/srv/forge/android/export/meizu_m6_artifacts/20260603-m6-dsi-bist-snapshot/Image.gz-dtb`
sha256 `526882e51fe11d69076ca613b238804c1927755c5a20876e7643750b48848e72`;
matching `System.map` is
`/srv/forge/android/export/meizu_m6_artifacts/20260603-m6-dsi-bist-snapshot/System.map`
sha256 `6c2d823989a0195867904049b2d92e350e809145592cb12c6e7bbcb659362f37`.
The flashed boot reaches `sys.boot_completed=1` with `init.svc.bootanim=stopped`
in `/srv/forge/android/export/meizu_m6_artifacts/20260603-m6-dsi-bist-snapshot/reboot-wait.txt`.

Test result FACT: fresh BIST capture
`/srv/forge/android/meizu_m6/captures/20260603-112512-m6-dsi-bist-snapshot-red-711HEBSR277K5`
confirms the existing `/d/mtkfb` pattern command changes DSI0 registers.
Before enable, `BIST_PATTERN=0x0`, `BIST_CON=0x200000`, and `self_pat=0`.
After `echo dsipattern:0x00ff0000 > /d/mtkfb`, `bist-post-enable` reports
`BIST_PATTERN=0xff0000`, `BIST_CON=0x200040`, and `self_pat=1` while DSI0 stays
in video mode (`MODE=0x3`), `PHY_LCCON=0x1`, `MIPITX lanes=0x603/0x601/0x601/0x601/0x601`,
and `PathMode:DIRECT_LINK` with `RDMA0 Transfer` count `5154` at about
`61.35` fps. Before disable the same BIST state remains set; after
`echo dsipattern:0 > /d/mtkfb`, `bist-post-disable` reports `BIST_CON=0x0` and
`self_pat=0`.

Test result FACT: the display stack still has valid internal output after the
BIST test. `screen-after-bist-snapshot.png` is a valid `720x1280` PNG sha256
`14ca584f83ddb7fa6d8d0139941930bc59b901b00440e795e7071264c01d725f`; pre-state
had `powerMode=2`, `sys.boot_completed=1`, `init.svc.bootanim=stopped`, and
backlight was raised from `10` to `255` for the BIST probe.

INFERENCE: DSI0 register writes, video-mode timing, MIPITX PLL/lane register
state, RDMA0 transfer, and Android composition are all live enough for the DSI
self-pattern bit to stick. If the user did not see a solid red panel during
the BIST window, the remaining physical black-screen frontier is panel
acceptance or board-level output: panel init command sequence, reset/power GPIO
polarity/timing, MIPI lane/electrical mapping, or backlight enable path. The
next useful evidence is not more OVL/RDMA/PQ; it is stock-vs-current LCM init
sequence comparison and targeted panel status/readback or reset/power markers.

Expected next marker: user visual report for the 8-second red BIST window. If
red was visible, resume normal UI/output tuning. If red was not visible, add
bounded markers around `ili9881p_hd_dsi_txd` power/reset/init command order and
compare that sequence against stock Flyme LK/kernel, then test a reset/power
timing or command-sequence correction.

Verification commands:

```bash
grep -R -n -E 'M6 DSI snapshot\\[bist-(pre|post)-(enable|disable)\\]|BIST_PATTERN|self_pat|RDMA0 Transfer|PathMode:DIRECT_LINK' /srv/forge/android/meizu_m6/captures/20260603-112512-m6-dsi-bist-snapshot-red-711HEBSR277K5
sha256sum /srv/forge/android/export/meizu_m6_artifacts/20260603-m6-dsi-bist-snapshot/boot-m6-dsi-bist-snapshot.img /srv/forge/android/export/meizu_m6_artifacts/20260603-m6-dsi-bist-snapshot/readback-boot-m6-dsi-bist-snapshot.img /srv/forge/android/meizu_m6/captures/20260603-112512-m6-dsi-bist-snapshot-red-711HEBSR277K5/screen-after-bist-snapshot.png
```

## 2026-06-03 LCM init sequence markers

PATCH HISTORY, DIAGNOSTIC, 2026-06-03: add bounded markers around the active
`ili9881p_hd_dsi_txd` LCM power, reset, init table, TPS65132 I2C, ATA, and
backlight paths.

Hypothesis: the PQ bridge mutex fix and DSI BIST snapshot prove Android
composition, RDMA0 transfer, DSI video mode, DSI self-pattern register writes,
and MIPITX register state are live. If the physical LCD remains black and the
red BIST window is not visible, the next earliest unproven boundary is whether
the selected LCM driver actually performs the stock-like power/reset/init
sequence, whether the TPS65132 bias writes succeed, whether `0x11`/`0x29` and
other key panel commands are emitted, whether the backlight command is sent
with the expected byte, and whether ATA readback can prove panel command
acceptance.

Evidence: previous verified capture
`/srv/forge/android/meizu_m6/captures/20260603-112512-m6-dsi-bist-snapshot-red-711HEBSR277K5`
shows `sys.boot_completed=1`, `PathMode:DIRECT_LINK`, RDMA0 transfer near
61 fps, DSI `MODE=0x3`, `PHY_LCCON=0x1`, MIPITX lane registers sane, and
`bist-post-enable` with `BIST_PATTERN=0xff0000`, `BIST_CON=0x200040`, and
`self_pat=1`. That capture does not prove the LCM driver's power/reset/init
boundaries or panel command readback. The new built artifact is
`/srv/forge/android/export/meizu_m6_artifacts/20260603-m6-lcm-sequence-markers/boot-m6-lcm-sequence-markers.img`
sha256 `849625d6b854dd6e469df756017bf1655768df32f4787826441226f6d37dcdf4`;
matching `Image.gz-dtb` sha256
`029efaf557635b587ad68317a852c07f2dac5c3c42a0b999778db2a178626beb`,
matching `System.map` sha256
`cdd5d1a510cf71a530c652a2ecce6d34f465fcb80a32805a05acfa6dcf421dec`,
and matching `vmlinux` sha256
`f05176c5b6cbc19008cceed408bc6ccc812943c0110752cb751bc9e2d75cfafc`.
`marker-strings.txt` contains `M6 LCM init`, `M6 LCM tps65132 write`,
`M6 LCM push_table`, `M6 LCM table[...]`, `M6 LCM ATA`, and
`M6 LCM backlight` markers.

Files changed:
- `kernel-3.18/drivers/misc/mediatek/lcm/ili9881p_hd_dsi_txd/ili9881p_hd_dsi_txd.c`
  logs the selected panel's `lcm_get_params`, power hooks, TPS65132 probe and
  write results, reset/bias/init sequence, key init-table commands, suspend and
  resume boundaries, ATA expected/read bytes, and `0x51` backlight command byte.
- `BRINGUP_STATE.md` records the diagnostic hypothesis, evidence, expected
  next markers, rollback condition, and verification commands.

Why each file changed: the LCM file owns the selected panel's power/reset/init
sequence and exposes the lowest-risk read-only evidence for the remaining
physical black-screen frontier. The patch does not change DSI timings, panel
commands, reset delays, TPS65132 values, ESD flags, route state, PQ policy,
RDMA/OVL programming, CMDQ waits, or userspace behavior. The state file is the
required durable handoff for this DIAGNOSTIC patch.

Expected next marker: after flashing only serial `711HEBSR277K5`, fresh dmesg
should contain `M6 LCM params`, `M6 LCM init start`, `M6 LCM tps65132 probe`,
`M6 LCM init ... tps reg0 ret=2`, `M6 LCM init ... tps reg1 ret=2`, the reset
steps, `M6 LCM push_table start tag=init`, key table markers for `0xff`,
`0x11`, and `0x29`, `M6 LCM backlight`, and an `M6 LCM ATA expected=...`
readback line after running the ATA probe. If the markers are absent, the
selected LCM path is not executing. If TPS writes fail or the client is NULL,
debug bias I2C/probe/GPIO. If init and backlight markers execute but ATA fails
and BIST remains invisible, move to panel command acceptance, reset timing,
lane/electrical mapping, or a targeted behavior patch backed by the marker
gap.

Rollback condition: revert this diagnostic if the verified boot regresses
before root ADB, SurfaceFlinger, or stable RDMA0 transfer; if log volume hides
earlier boot markers; or if the read probes are proven unsafe. Do not revert
solely because the physical panel remains black, since this patch is
observation-only.

Verification commands:

```bash
git diff --check
make -C /srv/forge/android/meizu_m6/kernel-meizu_M6-N-ex6-linux-3.18.140/kernel-3.18 O=/home/n8n/forge-work/kernel-builds/m6-directlink-smartovl-20260602/out ARCH=arm64 CROSS_COMPILE=/srv/forge/android/meizu_m6/rom-meizu_M6-lineage-cm-14.1/prebuilts/gcc/linux-x86/aarch64/aarch64-linux-android-4.9/bin/aarch64-linux-android- -j4 Image.gz-dtb
sha256sum /srv/forge/android/export/meizu_m6_artifacts/20260603-m6-lcm-sequence-markers/boot-m6-lcm-sequence-markers.img /srv/forge/android/export/meizu_m6_artifacts/20260603-m6-lcm-sequence-markers/Image.gz-dtb /srv/forge/android/export/meizu_m6_artifacts/20260603-m6-lcm-sequence-markers/System.map
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell 'dmesg | grep -E "M6 LCM|tps65132|ili9881p|M6 DSI snapshot|BIST_PATTERN|self_pat" | tail -240'
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell 'echo ata > /d/mtkfb; cat /d/mtkfb; dmesg | grep -E "M6 LCM ATA|ATA|M6 LCM" | tail -120'
```

Test result FACT: LCM marker artifact
`/srv/forge/android/export/meizu_m6_artifacts/20260603-m6-lcm-sequence-markers/boot-m6-lcm-sequence-markers.img`
sha256 `849625d6b854dd6e469df756017bf1655768df32f4787826441226f6d37dcdf4`
was flashed only to serial `711HEBSR277K5`; readback
`/srv/forge/android/export/meizu_m6_artifacts/20260603-m6-lcm-sequence-markers/readback-boot-m6-lcm-sequence-markers.img`
matches the same sha256 with `cmp_exit=0`. The boot reaches
`sys.boot_completed=1` with `init.svc.bootanim=stopped` in
`/srv/forge/android/export/meizu_m6_artifacts/20260603-m6-lcm-sequence-markers/reboot-wait.txt`.

Test result FACT: fresh capture
`/srv/forge/android/meizu_m6/captures/20260603-115019-m6-lcm-sequence-markers-711HEBSR277K5`
shows Android display stack still alive: `LCM Driver=[ili9881p_hd_dsi_txd]`,
`PathMode:DIRECT_LINK`, `RDMA0 Transfer` count `5576` at about `61.27` fps,
`sys.boot_completed=1`, `DisplayDevice: Built-in Screen` at `720x1280`,
`powerMode=2`, `isDisplayOn=1`, HWC layers for `ImageWallpaper#0`,
`StatusBar#0`, and `HWC_FRAMEBUFFER_TARGET`. `screen-pre.png` and
`screen-after-bist.png` are valid `720x1280` PNGs with identical sha256
`ca236178134d435564a8de811b1415b71d60b9b0af56f1bf5736a2f8a5a66d6c`.

Test result FACT: the LCM driver does not run Linux-side init during normal
boot when LK reports the panel already initialized. The fresh dmesg contains
`M6 LCM backlight` and `M6 LCM ATA`, but no `M6 LCM init start`, no
`M6 LCM init seq`, no TPS65132 probe/write markers, and no
`M6 LCM push_table start tag=init`. Source audit confirms
`primary_display_init()` receives `is_lcm_inited=1`, `disp_lcm_probe()` stores
`plcm->is_inited=true`, and the existing `disp_lcm_init(pgc->plcm, 0)` path
does not call `lcm_drv->init_power()` or `lcm_drv->init()` when
`disp_lcm_is_inited(plcm)` is true.

Test result FACT: panel command readback currently fails. The live ATA probe
logs `M6 LCM ATA expected=00 b4 02 1c read=00 00 00 00 ret=0`. The DSI BIST
red-window command still sets DSI self-pattern state:
`bist-post-enable` has `BIST_PATTERN=0xff0000`, `BIST_CON=0x200040`, and
`self_pat=1`; `bist-post-disable` has `BIST_CON=0x0` and `self_pat=0`.

INFERENCE: the prior stock Flyme LCM command-table restoration was not a real
runtime test of the Linux LCM init sequence on normal boot, because the Linux
driver skipped `lcm_init()` after trusting LK's `is_lcm_inited=1`. The earliest
evidence-backed display frontier is now to force the selected M6 panel through
the existing Linux `disp_lcm_init(force=1)` path even when LK claims it is
already initialized, then retrigger the video path like the existing non-LK
branch does.

## 2026-06-03 force Linux LCM reinit on M6 boot

PATCH HISTORY, BOOT-UNBLOCK + DIAGNOSTIC, 2026-06-03: force the selected
`ili9881p_hd_dsi_txd` panel through Linux-side LCM init on normal boot when LK
reports it initialized, and bound the hot-path backlight diagnostic logs.

Hypothesis: the panel remains physically black because Linux trusts LK's
`is_lcm_inited=1` handoff and therefore never executes the restored stock-like
`ili9881p_hd_dsi_txd` Linux init table, TPS65132 bias writes, or reset sequence
on normal boot. For this panel only, running the existing force-init branch
should exercise the real Linux LCM init path, produce the missing TPS/reset/init
markers, and either wake the panel or expose the next earliest command/power
failure. The previous unbounded backlight markers are diagnostic noise and can
evict the early init markers from dmesg, so they must be bounded before the next
capture.

Evidence: capture
`/srv/forge/android/meizu_m6/captures/20260603-115019-m6-lcm-sequence-markers-711HEBSR277K5`
shows normal boot with live Android composition, RDMA0, DSI BIST, and DCS
backlight command, but no Linux-side `M6 LCM init`/TPS/init-table markers and
ATA readback `00 00 00 00`. Source lines in
`kernel-3.18/drivers/misc/mediatek/video/mt6755/primary_display.c` show
`disp_lcm_init(pgc->plcm, 0)` when `is_lcm_inited` is true; source lines in
`kernel-3.18/drivers/misc/mediatek/video/mt6755/disp_lcm.c` show that
`force=0` skips `lcm_drv->init_power()` and `lcm_drv->init()` when
`plcm->is_inited` is already true. New built artifact
`/srv/forge/android/export/meizu_m6_artifacts/20260603-m6-linux-lcm-reinit/boot-m6-linux-lcm-reinit.img`
has sha256 `a5909870fffa8f79ce48e148abd185778a05525b1989eb01f87363d8252f4b60`;
matching `Image.gz-dtb` sha256
`6c26f781b4917227d8ef349f8a870e142b23fadb819ca31b8a8e06f389af1ad2`,
matching `System.map` sha256
`830bc29c8333e2cf515d2504b304188fa25efe1c50d81be0b15bb239f075d025`,
and matching `vmlinux` sha256
`c42fcf811eb07815ed7e36033cdbf5e25f5ad5feea1dc44ba622b13f2ef59daf`.

Files changed:
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/primary_display.c` detects
  the exact selected LK/kernel panel name `ili9881p_hd_dsi_txd` when
  `is_lcm_inited=1`, logs `M6 LCM reinit`, runs `disp_lcm_init(..., 1)`, and
  uses the existing video-mode retrigger branch.
- `kernel-3.18/drivers/misc/mediatek/lcm/ili9881p_hd_dsi_txd/ili9881p_hd_dsi_txd.c`
  bounds backlight diagnostic logging to the first few calls, extremes, and
  large deltas while still sending every `0x51` command unchanged.
- `BRINGUP_STATE.md` records the previous capture result, hypothesis, evidence,
  expected next marker, rollback condition, and verification commands.

Why each file changed: `primary_display.c` is the point where the LK handoff
`is_lcm_inited` decision is made and where a force-init branch already exists
for the non-LK case, so this is the narrowest behavior change that can make the
Linux LCM table actually execute. The LCM file change is diagnostic hygiene for
the same capture cycle; it does not change panel commands or backlight levels.
The state file is the required durable handoff.

Expected next marker: fresh boot dmesg should show `M6 LCM reinit`, then
`M6 LCM init_power`, `M6 LCM init start`, TPS65132 probe/write markers,
reset/bias steps, `M6 LCM push_table start tag=init`, key init-table commands
including `0xff`, `0x11`, and `0x29`, then `dpmgr_path_trigger`/RDMA0 transfer
and bounded backlight markers. If the physical panel becomes visible, keep this
as the first real display unblock and remove excess diagnostics later. If the
panel remains black but ATA starts returning expected bytes, move to video
stream/timing/backlight validation. If TPS writes fail or init markers stop at
a specific boundary, patch that earliest boundary next. If boot regresses before
ADB, collect pstore/last_kmsg and revert this patch.

Rollback condition: revert if verified boot regresses before ADB,
SurfaceFlinger, or RDMA0 transfer; if forced Linux init causes a new kernel
panic, DSI timeout loop, or no-device state; or if the panel was visible before
the patch and becomes black after it. Do not revert solely because the panel
remains black while the new markers identify an earlier failing boundary.

Verification commands:

```bash
git diff --check
make -C /srv/forge/android/meizu_m6/kernel-meizu_M6-N-ex6-linux-3.18.140/kernel-3.18 O=/home/n8n/forge-work/kernel-builds/m6-directlink-smartovl-20260602/out ARCH=arm64 CROSS_COMPILE=/srv/forge/android/meizu_m6/rom-meizu_M6-lineage-cm-14.1/prebuilts/gcc/linux-x86/aarch64/aarch64-linux-android-4.9/bin/aarch64-linux-android- -j4 Image.gz-dtb
sha256sum /srv/forge/android/export/meizu_m6_artifacts/20260603-m6-linux-lcm-reinit/boot-m6-linux-lcm-reinit.img /srv/forge/android/export/meizu_m6_artifacts/20260603-m6-linux-lcm-reinit/Image.gz-dtb /srv/forge/android/export/meizu_m6_artifacts/20260603-m6-linux-lcm-reinit/System.map
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell 'dmesg | grep -E "M6 LCM reinit|M6 LCM init|M6 LCM table\\[init\\]|tps65132|M6 LCM ATA|BIST_PATTERN|self_pat|RDMA0 Transfer|PathMode" | tail -260'
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell 'echo ata > /d/mtkfb; cat /d/mtkfb'
```

Runtime result FACT: boot image
`/srv/forge/android/export/meizu_m6_artifacts/20260603-m6-linux-lcm-reinit/boot-m6-linux-lcm-reinit.img`
sha256 `a5909870fffa8f79ce48e148abd185778a05525b1989eb01f87363d8252f4b60`
was flashed only to serial `711HEBSR277K5`; readback
`/srv/forge/android/export/meizu_m6_artifacts/20260603-m6-linux-lcm-reinit/readback-boot-m6-linux-lcm-reinit.img`
matched with `cmp_exit=0`. Reboot wait
`/srv/forge/android/export/meizu_m6_artifacts/20260603-m6-linux-lcm-reinit/reboot-wait.txt`
records 240 consecutive polls from `2026-06-03T12:03:19-05:00` through
`2026-06-03T12:12:57-05:00` with `state=none`, empty
`sys.boot_completed`, empty `init.svc.bootanim`, and empty
`init.svc.surfaceflinger`. ADB then showed only sibling device
`91HEBNL163XD`, not target `711HEBSR277K5`.

INFERENCE: boot-time `disp_lcm_init(force=1)` for `ili9881p_hd_dsi_txd`
is too early or unsafe in the primary display init path. It satisfies the
rollback condition for the boot-time force-reinit patch. The failure proves
that Linux-side panel init must be tested after a known-good ADB boot or via a
more staged path, not forced during early display bring-up.

## 2026-06-03 M6 debugfs LCM reinit command

PATCH HISTORY, BOOT-UNBLOCK + DIAGNOSTIC, 2026-06-03: restore the LK-trusting
boot path for the selected M6 panel and add an explicit `/d/mtkfb`
`m6_lcm_reinit:[0|1]` diagnostic command to run Linux-side LCM init only after
ADB/userspace is available.

Hypothesis: the no-ADB regression came from forcing the full LCM reset/bias/init
sequence during `primary_display_init()` while the display path and LK handoff
state are still fragile. Restoring the pre-regression boot branch should recover
the previously verified Android/ADB boot, while a post-boot debugfs command lets
the next capture exercise `disp_lcm_init(force=1)` with dense LCM markers and
without losing the ability to collect logs if it hangs or fails.

Evidence: the previous fresh LCM-marker capture
`/srv/forge/android/meizu_m6/captures/20260603-115019-m6-lcm-sequence-markers-711HEBSR277K5`
booted Android with live SurfaceFlinger/RDMA0/DSI BIST but showed that normal
boot skipped Linux-side `M6 LCM init` markers when LK reported the panel
initialized. The immediately following force-reinit image
`/srv/forge/android/export/meizu_m6_artifacts/20260603-m6-linux-lcm-reinit/boot-m6-linux-lcm-reinit.img`
was verified flashed/read back, then never returned ADB across 240 polls in
`reboot-wait.txt`. New safe-debug artifact
`/srv/forge/android/export/meizu_m6_artifacts/20260603-m6-lcm-debugfs-reinit/boot-m6-lcm-debugfs-reinit.img`
has sha256 `fed6f77cc420087a2b2a15f5781a3e4c82ce8cd864fd26491cfe3bf994279eb3`;
matching `Image.gz-dtb` sha256 is
`1f086cb37dd1dc0e95748ac7e11e33c7ba46480474198e5772962d9323f09017`;
matching `System.map` sha256 is
`26c2da87acff905d12f46b453da3b4ddffbc2207cbecb88094e22124ff399cd7`;
matching `vmlinux` sha256 is
`461703ee0367da30086be96cb4425259e51025f647f2d176623684d437262aa0`;
matching ramdisk sha256 is
`e82c6695614132e8759b9ee96ee5b9e9efdaf8df96d1ef0c32c5dae8b5e16332`.
The built marker strings contain `m6_lcm_reinit`, `M6 LCM debug reinit`, and
`M6 LCM handoff`, and no longer contain the old boot-time
`force Linux init table` string.

Files changed:
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/primary_display.c` removes
  boot-time forced LCM reinit for `ili9881p_hd_dsi_txd`, leaves a handoff
  marker, and adds `primary_display_m6_lcm_reinit()` for post-boot manual
  force init plus video-path retrigger.
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/primary_display.h` declares
  the new debug helper for `disp_debug.c`.
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/disp_debug.c` adds
  `/d/mtkfb` command parsing for `m6_lcm_reinit:[0|1]`.
- `BRINGUP_STATE.md` records the no-ADB regression, artifact identity, expected
  next markers, rollback condition, and verification commands.

Why each file changed: `primary_display.c` owns the LK handoff decision and is
the only safe place to call `disp_lcm_init()` while holding the primary path
lock and optionally retriggering the video path. `primary_display.h` is needed
for a typed cross-file call. `disp_debug.c` is the existing `/d/mtkfb` command
surface already used for `ata`, `dsipattern`, `resume`, and panel reset tests.
The state file is the durable handoff required for this M6 cycle.

Expected next marker: flash
`/srv/forge/android/export/meizu_m6_artifacts/20260603-m6-lcm-debugfs-reinit/boot-m6-lcm-debugfs-reinit.img`
to serial `711HEBSR277K5` only, verify readback hash
`fed6f77cc420087a2b2a15f5781a3e4c82ce8cd864fd26491cfe3bf994279eb3`, and
confirm Android returns to `sys.boot_completed=1`. Baseline dmesg should show
`M6 LCM handoff` but should not show `M6 LCM debug reinit` until the command is
manually issued. Then run `echo m6_lcm_reinit:1 > /d/mtkfb`; fresh dmesg should
show `M6 LCM debug reinit: start`, `M6 LCM init_power`, `M6 LCM init start`,
TPS65132 markers if the bias client is present, reset/init-table markers, and
`M6 LCM debug reinit: end ret=...`. Follow immediately with ATA and red DSI
BIST markers.

Rollback condition: revert this patch if the safe-debug image regresses before
ADB or loses the previously verified SurfaceFlinger/RDMA0/BIST behavior without
even issuing `m6_lcm_reinit`. If only the manual command hangs or fails after
ADB boot, keep the boot-path restoration and narrow the next patch to the
earliest marker inside the manual LCM init sequence.

Verification commands:

```bash
git diff --check
make -C /srv/forge/android/meizu_m6/kernel-meizu_M6-N-ex6-linux-3.18.140/kernel-3.18 O=/home/n8n/forge-work/kernel-builds/m6-directlink-smartovl-20260602/out ARCH=arm64 CROSS_COMPILE=/srv/forge/android/meizu_m6/rom-meizu_M6-lineage-cm-14.1/prebuilts/gcc/linux-x86/aarch64/aarch64-linux-android-4.9/bin/aarch64-linux-android- -j4 Image.gz-dtb
sha256sum /srv/forge/android/export/meizu_m6_artifacts/20260603-m6-lcm-debugfs-reinit/boot-m6-lcm-debugfs-reinit.img /srv/forge/android/export/meizu_m6_artifacts/20260603-m6-lcm-debugfs-reinit/Image.gz-dtb /srv/forge/android/export/meizu_m6_artifacts/20260603-m6-lcm-debugfs-reinit/System.map /srv/forge/android/export/meizu_m6_artifacts/20260603-m6-lcm-debugfs-reinit/ramdisk.img
(gzip -cd /srv/forge/android/export/meizu_m6_artifacts/20260603-m6-lcm-debugfs-reinit/Image.gz-dtb 2>/dev/null || true) | strings | grep -E 'm6_lcm_reinit|M6 LCM debug reinit|M6 LCM handoff|force Linux init table|run disp_lcm_init'
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 push /srv/forge/android/export/meizu_m6_artifacts/20260603-m6-lcm-debugfs-reinit/boot-m6-lcm-debugfs-reinit.img /data/local/tmp/boot.img
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell 'dd if=/data/local/tmp/boot.img of=/dev/block/platform/mtk-msdc.0/11230000.msdc0/by-name/boot bs=4M conv=fsync; sync; reboot'
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell 'dmesg | grep -E "M6 LCM handoff|M6 LCM debug reinit|M6 LCM init|M6 LCM table\\[init\\]|tps65132|M6 LCM ATA|BIST_PATTERN|self_pat|RDMA0 Transfer|PathMode" | tail -260'
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell 'echo m6_lcm_reinit:1 > /d/mtkfb; sleep 2; echo ata > /d/mtkfb; cat /d/mtkfb'
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell 'echo 255 > /sys/class/leds/lcd-backlight/brightness; echo dsipattern:0x00ff0000 > /d/mtkfb; sleep 8; echo dsipattern:0 > /d/mtkfb'
```

Runtime result FACT, 2026-06-04: the earlier unsafe manual force-init path was
reworked into a bounded stop/reset/init/start sequence and tested with boot
image
`/srv/forge/android/export/meizu_m6_artifacts/20260604-m6-lcm-safe-reinit-tabletiming/boot-m6-lcm-safe-reinit-tabletiming.img`
sha256 `d1c479fbb91e880016d6edf7c13a613276a4435d7bc6e1dbdd9caf8f20e2f547`.
Readback matched the local image. Capture
`/srv/forge/android/meizu_m6/captures/20260604-174621-m6-safe-reinit-tabletiming-711HEBSR277K5`
showed `sys.boot_completed=1`, SurfaceFlinger running, a non-black
screencap, `m6_lcm_reinit:1` returning `ret=0`, and no OVL underflow /
abnormal SOF after the reinit sequence. The same capture proved the active
TPS65132 DT client was wrong: runtime sysfs had `4-003e` under
`11011000.i2c`, and LCM reinit logged TPS writes timing out/failing on adapter
4.

Runtime result FACT, 2026-06-04: live bus sweep capture
`/srv/forge/android/meizu_m6/captures/20260604-175603-m6-tps65132-bus-sweep-711HEBSR277K5`
proved the bias chip responds on Linux adapter 0. The pstore file
`pstore-after-return/pstore/console-ramoops` shows the generated adapter-3
test failing (`ret=-22`) at lines 468-477, then a manually instantiated
adapter-0 client at line 2177, and successful writes
`ret=2 ... adapter=0` at lines 2213 and 2216. Deleting that test client then
hit a NULL dereference in `tps65132_remove` after the driver recursively called
`i2c_unregister_device()` from its `.remove` path.

PATCH HISTORY, GROUPED M6 DISPLAY/TPS CHECKPOINT, 2026-06-04: safe post-boot
LCM reinit, full LCM table timing diagnostics, TPS65132 bus0 binding, and
TPS65132 remove fix.

Hypothesis: the M6 panel path had two independent proven blockers. First, the
post-boot `m6_lcm_reinit` command was useful but too unsafe when it ran full
LCM reset/init while the trigger loop and DDP path were active, causing OVL
underflow / abnormal SOF or earlier crashes. Second, the TPS65132 bias client
was generated on the wrong I2C bus even though stock/source code and live
probing prove the hardware is on adapter 0. A safe stop/reset/init/start
sequence plus a bus0 DT override should make Linux-side reset/bias/init
observable and stable after Android is already up.

Evidence: the safe-reinit artifact and capture listed above closed the
post-boot reinit stability problem. The bus sweep pstore listed above closed
the TPS bus location and remove-crash problems. The tested bus0 artifact
`/srv/forge/android/export/meizu_m6_artifacts/20260604-m6-tps-bus0-dts-removefix/boot-m6-tps-bus0-dts-removefix.img`
has sha256
`c88c634e6006ca59b71f48c6c870e66dca23aab9b821d0f673e2026dbd669485`;
readback in
`/srv/forge/android/meizu_m6/captures/20260604-182046-m6-tps-bus0-reinit-711HEBSR277K5`
matches that hash with `cmp_exit=0`. Decompiled DTB in the artifact has the
active `i2c_lcd_bias@3e` under `i2c@11007000`; runtime capture
`/srv/forge/android/meizu_m6/captures/20260604-181456-m6-tps-bus0-dts-removefix-711HEBSR277K5/sysfs/i2c.txt`
shows `/sys/bus/i2c/devices/0-003e i2c_lcd_bias`. The controlled reinit
capture `20260604-182046-m6-tps-bus0-reinit-711HEBSR277K5` logs
`M6 LCM tps65132 write ... ret=2 ... adapter=0`, completes
`M6 LCM debug reinit: end ret=0`, and keeps SurfaceFlinger/FB/RDMA alive.

Files changed:
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/mtkfb.c`
  (**PROPER-FIX**) registers the LCM pinctrl platform driver before `mtkfb`
  and makes pinctrl state selection checked/logged instead of dereferencing
  missing state pointers.
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/primary_display.c`
  (**BOOT-UNBLOCK + DIAGNOSTIC**) makes `primary_display_m6_lcm_reinit()`
  stop the CMDQ trigger loop, wait/stop/reset the path, run `disp_lcm_init`,
  restart the path, retrigger video mode, and restart the trigger loop with
  boundary markers.
- `kernel-3.18/drivers/misc/mediatek/video/mt6755/disp_debug.c`
  (**PROPER-FIX + DIAGNOSTIC**) fixes the `m6_lcm_reinit` parser length and
  wraps DSI BIST enable with the same manual display lock used by disable.
- `kernel-3.18/drivers/misc/mediatek/lcm/ili9881p_hd_dsi_txd/ili9881p_hd_dsi_txd.c`
  (**DIAGNOSTIC + PROPER-FIX**) logs every init-table entry with elapsed time,
  logs TPS client replacement, and removes the recursive unregister from
  `tps65132_remove`.
- `kernel-3.18/arch/arm64/boot/dts/meizu_m6.dts` (**PROPER-FIX**) disables
  stale generated `i2c_lcd_bias@3e` nodes under the wrong buses and binds the
  bias device under `&i2c0`.
- `BRINGUP_STATE.md` records the required evidence and rollback/verification
  route for this grouped checkpoint.

Why each file changed: `mtkfb.c` owned the missing-pinctrl crash boundary seen
while exercising Linux LCM init. `primary_display.c` and `disp_debug.c` own the
manual debugfs reinit/BIST control surface. The LCM driver owns the bias client
and init-table observability. `meizu_m6.dts` is the compiled DT source that
selects which Linux adapter probes `mediatek,i2c_lcd_bias`; the live bus sweep
proved adapter 0 is the hardware truth and adapter 3/4 are wrong for this
panel bias chip.

Expected next marker: with boot sha256
`c88c634e6006ca59b71f48c6c870e66dca23aab9b821d0f673e2026dbd669485`, Android
should boot to `sys.boot_completed=1`, runtime sysfs should expose
`/sys/bus/i2c/devices/0-003e`, and `echo m6_lcm_reinit:1 > /d/mtkfb` should
log TPS writes `ret=2 adapter=0`, init table entries through `0x11` and `0x29`,
`M6 LCM debug reinit: end ret=0`, no transfer timeout, and no
`tps65132_remove` crash. If the physical panel remains black, the next display
frontier is not TPS bias; it is DSI command/readback or video-stream/panel
acceptance.

Current next blocker FACT: after the bus0 fix, controlled capture
`/srv/forge/android/meizu_m6/captures/20260604-182046-m6-tps-bus0-reinit-711HEBSR277K5`
still logs `M6 LCM ATA expected=00 b4 02 1c read=00 00 00 00 ret=0` after the
successful reset/bias/init sequence. DSI BIST registers toggle
`BIST_PATTERN=0xff0000`, `BIST_CON=0x200040`, `self_pat=1`, and `dsi0`
interrupts advance from all-zero to nonzero (`73/1/4/2` in the after sample),
while SurfaceFlinger still reports a `720x1280` built-in screen,
`powerMode=2`, `isDisplayOn=1`, HWC target, and flips. INFERENCE: TPS bias and
post-boot reinit sequencing are no longer the earliest known blockers. The next
patch should add DSI/PHY/DCS read/write diagnostics around ATA and init command
submission, and should use physical BIST visibility as the branch point:
visible BIST means scanout/input path; invisible BIST means DSI/PHY/panel
output.

Rollback condition: revert the DTS bus0 override if a verified boot of this
exact artifact binds `i2c_lcd_bias` somewhere other than adapter 0 or regresses
before ADB/SurfaceFlinger. Revert the reinit sequencing only if the verified
artifact regresses before issuing `m6_lcm_reinit` or if the command causes a
new panic/no-ADB state. Revert the `tps65132_remove` change only if a normal
driver unbind path proves it must unregister the already-removing client, which
would contradict the Linux I2C driver model and the observed recursive
remove crash.

Verification commands:

```bash
git diff --check
sha256sum -c /srv/forge/android/export/meizu_m6_artifacts/20260604-m6-tps-bus0-dts-removefix/SHA256SUMS
grep -n -E 'i2c_lcd_bias|11007000|11011000' /srv/forge/android/export/meizu_m6_artifacts/20260604-m6-tps-bus0-dts-removefix/meizu_m6.dts.decompiled
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 exec-out 'dd if=/dev/block/mmcblk0p21 bs=2048 count=4328 2>/dev/null' > /tmp/m6-readback.img
head -c "$(stat -c%s /srv/forge/android/export/meizu_m6_artifacts/20260604-m6-tps-bus0-dts-removefix/boot-m6-tps-bus0-dts-removefix.img)" /tmp/m6-readback.img | sha256sum
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell 'ls -l /sys/bus/i2c/devices/0-003e; readlink -f /sys/bus/i2c/devices/0-003e'
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell 'echo 255 > /sys/class/leds/lcd-backlight/brightness; echo m6_lcm_reinit:1 > /d/mtkfb; sleep 2; echo ata > /d/mtkfb; cat /d/mtkfb'
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell 'dmesg | grep -E "M6 LCM debug reinit|M6 LCM tps65132 write|M6 LCM ATA|bist-post|transfer timeout|transfer error" | tail -220'
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell 'cat /proc/interrupts | grep -E "mtk_cmdq|ovl0|rdma0|dsi0|mali"; dumpsys SurfaceFlinger | grep -E "Built-in Screen|powerMode|isDisplayOn|flips="'
```

## 2026-06-05 Guarded clean-system + DSI-core capture watcher

Operational FACT: local tmux watcher `m6-dsi-diag-flash-20260605` is running
from
`/srv/forge/android/export/meizu_m6_artifacts/20260604-m6-dsi-core-read-bounded`
with:

```bash
ADB_PORT=15039 WAIT_SECONDS=7200 POLL_SECONDS=5 ./m6_wait_capture_flash_clean_runtime_diag.sh
```

Watcher log:
`/srv/forge/android/meizu_m6/captures/m6-dsi-diag-flash-20260605-watch.log`.

Current status at watcher start: `15039` was not listening, so the helper only
logged `port=15039 no-listener` and did not flash anything. `15038` is not an
M6 endpoint in this session; it is occupied by the separate nx549j watcher and
returns ADB protocol fault for M6 use. The helper is guarded by both the port
listener check and `adb -s 711HEBSR277K5 get-state == device` before it captures
preflash evidence or writes partitions.

Intended next evidence: when the user's reverse tunnel
`ssh -N -o ExitOnForwardFailure=yes -R 127.0.0.1:15039:127.0.0.1:5037 n8n@100.87.104.62`
appears and serial `711HEBSR277K5` is `device`, the helper should capture
preflash state, write clean runtime `system-clean-no-video-le.raw.img`, write
`boot-m6-dsi-core-read-bounded.img`, verify readback SHA256 for both boot and
system, reboot, then capture postboot `M6 DSI core read` / `M6 LCM ATA` /
display state.

Rollback/stop condition: stop the tmux session if a different serial appears
on `15039`, if the helper reports a readback hash mismatch, or if the user wants
to flash a different artifact set. Stop command:

```bash
tmux kill-session -t m6-dsi-diag-flash-20260605
```

## 2026-06-05 DCS read sweep diagnostic + guarded watcher

Patch category: **DIAGNOSTIC**.

Supersedes the older `m6-dsi-diag-flash-20260605` /
`m6-ovl-m4u-endpoint-flash-20260605` waiting runs. Those sessions never saw
`127.0.0.1:15039` and did not flash. 2026-06-05T01:08-05:00 update: the
original `m6-dcs-read-sweep-flash-20260605` watcher was also stopped before it
ever saw `15039`, because the paired clean system image was updated with the
LatinIME optional-JNI cleanup. The current active watcher is:

```bash
tmux ls
# m6-dcs-read-sweep-latinime-flash-20260605
tail -f /srv/forge/android/meizu_m6/captures/m6-dcs-read-sweep-latinime-flash-20260605-watch.log
```

Artifact:
`/srv/forge/android/export/meizu_m6_artifacts/20260605-m6-dcs-read-sweep-latinime-system-flash`.

Paired clean system image:
`/srv/forge/android/export/meizu_m6_artifacts/20260605-m6-los15-clean-system-latinime-jni/system-clean-no-video-le-latinime-jni.raw.img`
sha256 `593fef6bbb111c214b22b0e7da05f0344842221ff4cec96a9cb97b94af7d8ac3`.
This raw image keeps the clean codec state (`media_codecs_google_video.xml`,
no `media_codecs_google_video_le.xml`) and contains LatinIME APK sha256
`5db90ae9ecaee7f6616b769f0596d4d667886b5c537e12bf1bf9dc2b9dce00d2` from
ROM checkpoint `8f61e1f`.

Important hashes:

- boot image:
  `6dca836c3e854890f0ce28cb5ebb83af8e12e601144064ae7dbee70c1873fcc6`
  `boot-m6-dcs-read-sweep-diag.img`
- `Image.gz-dtb`:
  `7a85e7daaf82e2736d551ddada0c1b96752aa422b6659beb22ab261d2b0af219`
- `System.map`:
  `73fba0150a47f65dc741984bb7b5fc31e4be5ff3c6e4ab67101aa2432ce287e7`
- helper:
  `c9721021443c4cb45d0a35f38ea81b3378e11dc237e608f3d87c19204547fad8`
  `m6_wait_capture_flash_clean_runtime_diag.sh`
  The postboot capture path now also runs a 3-second
  `screenrecord --time-limit 3 --size 720x1280` smoke test and saves
  `screenrecord-720x1280.txt`, `screenrecord-720x1280.mp4` when created,
  `screenrecord-pull.txt`, `media-screen-marker-tail.txt`, and
  `surfaceflinger-latency.txt`.

Hypothesis: current evidence places the physical-black-screen frontier after
SurfaceFlinger/HWC/RDMA/TPS/reinit and at DSI command/read response or panel
acceptance. The prior verified capture had nonblack screencap, active display
IRQs, backlight 255, successful TPS writes on adapter 0, successful manual LCM
reset/init through `0x11` and `0x29`, and DSI BIST registers toggling, but ATA
still read `00 00 00 00`. Stock LK contains the `ili9881p_hd_dsi_txd` candidate
used by this kernel, so the next proof must distinguish "panel responds to
other DCS/status/ID reads but not 0x2A" from "DSI BTA/read payload is generally
dead" and from "wrong page/state/variant after init".

Evidence:

- Verified runtime capture:
  `/srv/forge/android/meizu_m6/captures/20260604-182046-m6-tps-bus0-reinit-711HEBSR277K5`
  with boot readback matching
  `/srv/forge/android/export/meizu_m6_artifacts/20260604-m6-tps-bus0-dts-removefix/boot-m6-tps-bus0-dts-removefix.img`.
- That capture had `sys.boot_completed=1`, SurfaceFlinger running, nonblack
  `720x1280` screencap, `DISP_OPT_BYPASS_PQ=1`, active `mtk_cmdq` / `ovl0` /
  `rdma0` / `dsi0` interrupts, TPS writes `ret=2 adapter=0`, and
  `M6 LCM debug reinit: end ret=0`.
- The same capture logged ATA failure after that successful path:
  `M6 LCM ATA expected=00 b4 02 1c read=00 00 00 00`.
- The DSI core read path returned a valid DCS long-read packet header in the
  older ATA dump (`packet_type 0x1c`, long packet size 4), but payload copied
  from RX data was zeros.
- Stock LK raw strings in
  `/srv/forge/android/meizu_m6/captures/20260530-stock-lk-boot-reverse-inputs/lk.img`
  include `ili9881p_hd_dsi_txd`, `ili9881c_hd_dsi_txd`, and
  `s6d7aa6_hd720_dsi_vdo_hlt`; current defconfig/DTS select
  `ili9881p_hd_dsi_txd`, matching one stock candidate but not proving panel
  acceptance.

Files changed:

- `kernel-3.18/drivers/misc/mediatek/lcm/ili9881p_hd_dsi_txd/ili9881p_hd_dsi_txd.c`
  adds bounded post-ATA read-only DCS diagnostics. The helper reads `0x04`,
  `0x09`, `0x0A`, `0x0B`, `0x0C`, `0x0D`, `0x2A`, `0x2B`, `0xDA`, `0xDB`,
  and `0xDC`, logs `M6 LCM ATA dcs[%u] ... read_count=%u`, and stops after
  eight ATA invocations.
- `BRINGUP_STATE.md` records the diagnostic patch, artifact identity, watcher,
  expected next markers, and rollback route.

Why each file changed: the LCM driver owns the `echo ata > /d/mtkfb` panel
readback path, and DSI read wrappers already emit core packet diagnostics. A
single bounded read sweep there gives the next capture enough information to
separate DSI BTA/read transport failure from a narrower 0x2A/window/page-state
problem without changing panel init behavior. The state file changed because
this is a flashable diagnostic checkpoint with a live guarded watcher.

Expected next marker: after the watcher flashes boot sha256
`6dca836c3e854890f0ce28cb5ebb83af8e12e601144064ae7dbee70c1873fcc6`, the
postboot capture should include `M6 LCM ATA dcs[1]` lines for the listed DCS
registers plus the existing `M6 DSI wrapper read`, `M6 DSI core read wait`,
`M6 DSI core read packet`, `M6 LCM ATA expected`, `M6 OVL diag end`, and
`M6 M4U disp tf bypass` markers. If all DCS reads return valid packet headers
with zero payload, inspect DSI RX payload extraction / lane/panel state. If ID
or power/status registers return nonzero but `0x2A` stays zero, focus on
init-table page/window sequencing. If reads time out or ACK/error, focus on
DSI BTA/LP timing or panel reset/power acceptance.

Rollback condition: revert this diagnostic if the verified artifact regresses
before ADB/SurfaceFlinger or if the added read sweep causes repeated DSI reset
or timeout that was absent from the previous verified boot. Otherwise keep it
until one fresh capture classifies the DSI/panel read boundary.

Verification commands:

```bash
git diff --check
env CCACHE_DIR=/srv/forge/android/ccache make -C /srv/forge/android/meizu_m6/kernel-meizu_M6-N-ex6-linux-3.18.140/kernel-3.18 \
  O=/srv/forge/work/m6-source-kernel-manual-20260520/out \
  ARCH=arm64 \
  CROSS_COMPILE=/srv/forge/android/meizu_m6/rom-meizu_M6-lineage-cm-14.1/prebuilts/gcc/linux-x86/aarch64/aarch64-linux-android-4.9/bin/aarch64-linux-android- \
  -j8 Image.gz-dtb
cd /srv/forge/android/export/meizu_m6_artifacts/20260605-m6-dcs-read-sweep-latinime-system-flash
sha256sum -c SHA256SUMS
bash -n m6_wait_capture_flash_clean_runtime_diag.sh
ADB_PORT=15039 WAIT_SECONDS=7200 POLL_SECONDS=5 ./m6_wait_capture_flash_clean_runtime_diag.sh
```

Transport status at creation time: `127.0.0.1:15039` had no listener, so the
new watcher only logged `port=15039 no-listener` and did not flash. The helper
is guarded by both the port listener check and `adb -s 711HEBSR277K5 get-state
== device`.

Runtime sidecar blocker snapshot, not part of this display patch:

- `sys.boot_completed`, `input`, and `clipboard` are closed in current captures.
- Current non-display blockers to revalidate on the next fresh boot are
  H.264/scrcpy media encoder fence timeout and pure64 `webview_zygote32`
  restart/noise. The LatinIME `libjni_latinimegoogle.so` alias/loading noise
  has a source checkpoint and is included in the paired 2026-06-05 system raw;
  the next boot must verify the logcat marker is gone.

## 2026-06-06 DCS sleep-out/display-on packet isolation

Patch category: **ISOLATION + DIAGNOSTIC**.

Hypothesis: FACT: current boot partition readback in
`/srv/forge/android/meizu_m6/captures/20260606-043843-m6-live-scrcpy-ok-physical-black`
matches
`/srv/forge/android/export/meizu_m6_artifacts/20260605-m6-dsi-sleep-clock-diag-boot/boot-m6-dsi-sleep-clock-diag.img`
sha256 `e4c317d025440efa5e04f158b255b4b127ce6e5c0848a149ccecefbbab6eabbd`.
FACT: Android and composition are alive in that capture: `sys.boot_completed=1`,
`Display Power: state=ON`, `mScreenState=ON`, `mScreenBrightness=180`,
SurfaceFlinger built-in display `720x1280` has `powerMode=2`, `isDisplayOn=1`,
and RDMA0 transfer is about 60 fps. FACT: physical LCD is still black per user
observation while scrcpy shows the rendered image.

Evidence: FACT: DCS transport is alive after the manual Linux reinit because
`0x04` / `0xDA` / `0xDB` read `15 20`, but the same fresh dmesg shows bad panel
power state after reinit: `M6 LCM ATA dcs[6] name=display_status ... read=00 01
02 00`, `name=power_mode ... read=08`, and `name=pixel_format ... read=07`.
Earlier LK-handoff captures had `power_mode=9c` with the same selected
`ili9881p_hd_dsi_txd` panel. FACT: source audit shows the active Linux init
tail sends `0x11` and `0x29` through `dsi_set_cmdq_V22()` with `count=1`, which
the MTK DSI core encodes as DCS short packet with one parameter (`0x15`) instead
of the zero-parameter DCS short packet (`0x05`). HYPOTHESIS: the stock raw table
conversion preserved a padding zero byte as a payload, so Linux's force-init
never actually issues valid zero-parameter Sleep Out / Display On packets for
this panel.

Files changed:

- `kernel-3.18/drivers/misc/mediatek/lcm/ili9881p_hd_dsi_txd/ili9881p_hd_dsi_txd.c`
  changes only the active init tail entries for `0x11` and `0x29` from
  `count=1, payload 0x00` to `count=0`.
- `BRINGUP_STATE.md` records the capture identity, evidence, expected next
  marker, rollback condition, and verification commands.

Why each file changed: the LCM driver owns the selected panel command sequence.
`0x11` and `0x29` are the narrowest behavior boundary that directly explains
the observed bad DCS `power_mode=08` after otherwise successful reset, TPS bias,
init table submission, backlight command, and live DCS ID reads. The state file
is the required durable handoff for this isolation checkpoint.

Expected next marker: after flashing the rebuilt boot image, Android should
still reach `sys.boot_completed=1`. Running `echo m6_lcm_reinit:1 > /d/mtkfb`
followed by `echo ata > /d/mtkfb` should log init table entries
`cmd=0x11 count=0` and `cmd=0x29 count=0`. If the hypothesis is right, DCS
`power_mode` should move away from `08` toward the earlier healthy `9c`,
`display_status` should no longer be `00 01 02 00`, and the physical panel may
light or show the DSI BIST/Android frame.

Rollback condition: revert this isolation if the verified boot regresses before
ADB/SurfaceFlinger, if DCS reads start timing out where the current artifact
returns valid `15 20` ID bytes, or if `m6_lcm_reinit:1` still leaves
`power_mode=08` and no physical image while no other marker changes.

Verification commands:

```bash
git diff --check
env CCACHE_DIR=/srv/forge/android/ccache make -C /srv/forge/android/meizu_m6/kernel-meizu_M6-N-ex6-linux-3.18.140/kernel-3.18 \
  O=/srv/forge/work/m6-source-kernel-manual-20260520/out \
  ARCH=arm64 \
  CROSS_COMPILE=/srv/forge/android/meizu_m6/rom-meizu_M6-lineage-cm-14.1/prebuilts/gcc/linux-x86/aarch64/aarch64-linux-android-4.9/bin/aarch64-linux-android- \
  -j8 Image.gz-dtb
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell 'echo m6_lcm_reinit:1 > /d/mtkfb; sleep 2; echo ata > /d/mtkfb; dmesg | grep -E "M6 LCM table\\[init\\].*cmd=0x(11|29)|M6 LCM ATA dcs|M6 DSI wrapper read end" | tail -160'
```

Runtime result: **rejected and rolled back**. The rebuilt boot image
`/srv/forge/android/export/meizu_m6_artifacts/20260606-m6-dcs-zero-param-sleepout/boot-m6-dcs-zero-param-sleepout.img`
sha256 `6b04421eb1cd118588dbab13c05a47d9ba856298af7e0efbc253edaf721979fd`
was flashed to serial `711HEBSR277K5`, read back before reboot, and read back
again after Android boot; both readbacks matched byte-for-byte. Capture:
`/srv/forge/android/meizu_m6/captures/20260606-044921-m6-zero-param-sleepout-flash`.
FACT: Android still booted (`sys.boot_completed=1`, SurfaceFlinger running,
bootanim stopped). FACT: the init-table markers prove the changed commands
were sent: `M6 LCM table[init] idx=67 cmd=0x11 count=0` and
`idx=69 cmd=0x29 count=0`. FACT: after `m6_lcm_reinit:1`, every DCS/ATA read
timed out with `read_count=0` and sentinel `a5 a5 a5 a5`, including
`display_id`, `display_status`, `power_mode`, `pixel_format`, and ID registers.
This is a regression from the prior verified boot, where DCS ID reads returned
`15 20` and only panel power/status were bad. The source change was therefore
manually reverted to the stock/preserved `count=1, payload 0x00` entries. Next
work should treat `0x11/0x29` zero-parameter conversion as rejected for this
MTK video-mode path unless stock LK proves a different packet encoding at the
DSI controller level.

## 2026-06-06 awake DCS and DSI BIST screen markers

Patch category: **DIAGNOSTIC**. No kernel behavior change was kept in this
checkpoint; this section records live-device evidence after rolling back the
rejected zero-parameter DCS isolation.

Hypothesis: the black physical LCD report must be split into two states. FACT:
after the rejected zero-param boot was rolled back, the device was flashed back
to
`/srv/forge/android/export/meizu_m6_artifacts/20260605-m6-dsi-sleep-clock-diag-boot/boot-m6-dsi-sleep-clock-diag.img`
sha256 `e4c317d025440efa5e04f158b255b4b127ce6e5c0848a149ccecefbbab6eabbd`.
Capture:
`/srv/forge/android/meizu_m6/captures/20260606-rollback-from-zero-param-to-dsi-sleep-clock-diag`.
The pre-reboot and postboot boot readbacks matched byte-for-byte, and Android
booted as kernel `#16 SMP PREEMPT Fri Jun 5 20:02:39 CDT 2026`. FACT: normal
Android idle/DOZE can make the physical display black by design: at
`20260606-wake-screen-after-rollback/before-wake-power.txt`, `Display Power:
state=OFF`, `mGlobalDisplayState=OFF`, `mScreenState=OFF`, and backlight
brightness `0`. The matching dmesg shows `M6 mtkfb blank: mode=4`, LCM suspend,
bias `ENN/ENP` off, DSI clocks off, and display state `SLEPT`.

Evidence: after `svc power stayon true`, long `screen_off_timeout`, and
`input keyevent 224`, the same boot resumed cleanly. Capture:
`/srv/forge/android/meizu_m6/captures/20260606-wake-screen-after-rollback`.
FACT: `after-wake-power.txt` shows `Display Power: state=ON`,
`mGlobalDisplayState=ON`, `mScreenState=ON`, `mScreenBrightness=180`, backlight
brightness `180`, and `ata_flag=1`. FACT: `/d/mtkfb` reports `State=Alive`,
`PathMode:DIRECT_LINK`, `DISP_OPT_BYPASS_PQ=1`, and RDMA0 transfer around 61
fps. FACT: the normal resume path ran `lcm_init seq=2` and the DCS status after
awake `echo ata > /d/mtkfb` moved to the known healthy panel state:
`display_id=15 20 00`, `display_status=80 03 06 00`, `power_mode=9c`,
`pixel_format=07`, `id1=15`, `id2=20`, `id3=00`. This means the current stock
`count=1` Linux resume path can bring the panel out of sleep at the DCS level.

Screen-marker evidence: with display still ON/Alive, `dsipattern` was run for
solid red, green, and blue. The kernel markers prove the DSI self-pattern path
was enabled in video mode:

- red: `enable dsi pattern: 0x00ff0000`, then
  `M6 DSI snapshot[bist-post-enable] ... BIST_PATTERN=0xff0000
  BIST_CON=0x200040 self_pat=1 ... STATE7=0x2020/Video data period`;
- green: `enable dsi pattern: 0x0000ff00`, then `BIST_PATTERN=0xff00
  BIST_CON=0x200040 self_pat=1`;
- blue: `enable dsi pattern: 0x000000ff`, then `BIST_PATTERN=0xff
  BIST_CON=0x200040 self_pat=1`;
- off: `dsipattern:0x00000000` cleared `BIST_CON=0x0` while DSI remained in
  video data period.

Why this matters: scrcpy/SurfaceFlinger is no longer the decisive display
frontier. In the awake state, Android composition, RDMA transfer, DSI video
mode, DCS status, backlight sysfs, and DSI BIST register enable are all
internally consistent. If the human sees the DSI BIST colors, the remaining bug
is above or at source-buffer/composition routing. If the physical LCD stays
black even during verified DSI BIST with `power_mode=9c`, the next frontier is
below the DSI controller's self-pattern register write: panel-side power/reset,
lane mapping, MIPI TX electrical state, timing polarity/ranges, or a hidden
stock LK power/PHY side effect.

Expected next marker: keep the device awake during physical display tests with
`svc power stayon true`, `settings put system screen_off_timeout 2147483647`,
and `input keyevent 224`. A positive visual result is any visible solid color
during one of the `dsipattern` windows. A negative visual result is a fully
black physical LCD while the saved logs show `Display Power: state=ON`,
`power_mode=9c`, `BIST_CON=0x200040 self_pat=1`, and DSI `STATE7` in video data
period.

Rollback condition: none for source code; the zero-param code was already
rolled back and the BIST was disabled after the live test. If future captures
show the display went black only after `mScreenState=OFF` or `State=Sleep`,
classify that as Android power policy/idle, not a panel bring-up regression.

Verification commands:

```bash
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell 'svc power stayon true; settings put system screen_off_timeout 2147483647; input keyevent 224'
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell 'echo ata > /d/mtkfb; sleep 1; dmesg | grep -E "M6 LCM ATA dcs|M6 DSI wrapper read end" | tail -120'
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell 'echo dsipattern:0x00ff0000 > /d/mtkfb; sleep 8; echo dsipattern:0x0000ff00 > /d/mtkfb; sleep 8; echo dsipattern:0x000000ff > /d/mtkfb; sleep 8; echo dsipattern:0x00000000 > /d/mtkfb'
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell 'dmesg | grep -E "M6 DSI snapshot\\[bist|enable dsi pattern|BIST_PATTERN|BIST_CON" | tail -160'
```

## 2026-06-06 physical-black GPIO/TPS readback diagnostic

Patch category: **DIAGNOSTIC**. No register, GPIO, pinctrl, DCS, timing, route,
or power behavior is changed by this patch.

Hypothesis: FACT: fresh live capture
`/srv/forge/android/meizu_m6/captures/20260606-live-physical-black-dsi-bist2`
shows Android boot complete, SurfaceFlinger running, `LCM Driver=[ili9881p_hd_dsi_txd]`,
`State=Alive`, `PathMode:DIRECT_LINK`, `DISP_OPT_BYPASS_PQ=1`, and RDMA0
transfer at about 60.82 fps. FACT: the same capture shows healthy DCS reads:
`display_id=15 20 00`, `display_status=80 03 06 00`, `power_mode=9c`,
`pixel_format=07`, `id1=15`, `id2=20`, and `id3=00`. FACT: DSI self-pattern
write markers for white/red/green/blue set `self_pat=1` and report DSI video
period, but the physical LCD remains black per human observation. HYPOTHESIS:
the open frontier is no longer userspace, PQ, RDMA, or DCS command transport;
it is panel-side physical visibility, most likely one of bias/reset/backlight
enable state, TPS65132 applied voltage state, MIPI lane electrical/timing
state, or an LK-only side effect missing from Linux reinit.

Evidence: current source routes M6 LCM bias/reset through
`lcm_pinctl_gpio_output()` in `mtkfb.c`, mapping VSP to GPIO17, VSN to GPIO90,
and reset to GPIO158. The current LCM backlight path is DTS `led_mode=<4>`
(`MT65XX_LED_MODE_CUST_LCM`) and sends DCS `0x51`; live sysfs brightness `255`
therefore proves the DCS brightness request, not a separate LED rail. Live
`/d/gpio` only exposes GPIO12 and GPIO101, with GPIO12 low, so it cannot prove
the actual VSP/VSN/RST state selected by the LCM pinctrl path. TPS writes
currently log only `i2c_master_send ret=2`, which proves transfer completion
but not readback of reg0/reg1.

Files changed:

- `kernel-3.18/drivers/misc/mediatek/video/mt6755/mtkfb.c` adds bounded
  read-only GPIO state markers after LCM pinctrl probe/select for GPIO17,
  GPIO90, GPIO158, GPIO12, and GPIO101.
- `kernel-3.18/drivers/misc/mediatek/lcm/ili9881p_hd_dsi_txd/ili9881p_hd_dsi_txd.c`
  adds TPS65132 reg0/reg1 readback markers after the existing bias writes.
- `BRINGUP_STATE.md` records the diagnostic evidence, expected markers,
  rollback condition, and verification commands.

Why each file changed: `mtkfb.c` owns the M6-specific pinctrl helper actually
called by `set_gpio_lcd_enp()`, `set_gpio_lcd_enn()`, and `SET_RESET_PIN()`,
so it is the lowest point that can correlate requested VSP/VSN/RST states with
actual MTK GPIO mode/dir/out/in reads. The active LCM driver owns TPS65132
write sequencing, so it can read reg0/reg1 immediately after writes without
guessing from userspace debugfs.

Build/artifact result: `Image.gz-dtb` built successfully from
`/srv/forge/work/m6-source-kernel-manual-20260520/out` using the documented
`-j8 Image.gz-dtb` command. Boot-only artifact:
`/srv/forge/android/export/meizu_m6_artifacts/20260606-m6-physical-black-gpio-tps-diag/boot-m6-physical-black-gpio-tps-diag.img`.
Artifact sha256 identities:

- `boot-m6-physical-black-gpio-tps-diag.img`:
  `c5689acac835038709943e8e3ccee77b03c5cb24b1bd2af14c7cda7b18edd09c`
- `Image.gz-dtb`:
  `41d589c7deed8dbe72cbea7a1f2b25da334ae2cb8822b94857cc418c975385f9`
- `System.map`:
  `12af68250308681197d89b92ca24116bf7ad3e86759d43167b10437a2aab2129`
- `kernel.config`:
  `bc272726035c1a2eca9422e9bc230cf54f8295648a8865a98faba046ab01619e`
- `verify-unpack/zImage`:
  `41d589c7deed8dbe72cbea7a1f2b25da334ae2cb8822b94857cc418c975385f9`

`abootimg -i` reports unchanged boot geometry: page size `2048`, boot name
`1552631950`, kernel address `0x40080000`, ramdisk address `0x45000000`, tags
address `0x44000000`, and cmdline
`bootopt=64S3,32N2,64N2 androidboot.selinux=permissive binder.devices=binder,hwbinder,vndbinder`.
`sha256sum -c SHA256SUMS` passed and `cmp Image.gz-dtb verify-unpack/zImage`
passed inside the artifact directory.

Current device readback before flashing the diagnostic: capture
`/srv/forge/android/meizu_m6/captures/20260606-075201-m6-current-boot-readback-711HEBSR277K5`
read `/dev/block/platform/mtk-msdc.0/11230000.msdc0/by-name/boot` and trimmed
the readback to `8863744` bytes. FACT: device boot partition sha256 is still
`e4c317d025440efa5e04f158b255b4b127ce6e5c0848a149ccecefbbab6eabbd`
(`20260605-m6-dsi-sleep-clock-diag-boot`), not the new diagnostic
`c5689acac835038709943e8e3ccee77b03c5cb24b1bd2af14c7cda7b18edd09c`.
Therefore `M6 gpio[...]` and TPS readback markers are not expected on the
currently running kernel until this boot-only artifact is flashed.

Expected next marker: after flashing the rebuilt boot and running
`m6_lcm_reinit:1`, dmesg should contain `M6 gpio[...]` lines for
`vsp-pullhigh`, `vsn-pullhigh`, and `rst-pullhigh`, plus
`M6 LCM tps65132 read addr=0x00 ret=15` and `addr=0x01 ret=15` if the bias IC
readback matches the programmed `0x0f`. If VSP/VSN/RST or TPS readback is
wrong while DCS `power_mode=9c` remains healthy, the next patch should target
the proven pinctrl/TPS boundary. If all physical power/reset evidence is
healthy and the screen is still black during BIST, continue below DSI controller
self-pattern toward MIPI TX electrical/timing/lane parity with stock LK.

Rollback condition: revert this diagnostic if the verified boot regresses
before ADB/SurfaceFlinger, if GPIO read markers flood logs beyond the LCM
init/resume/reinit path, or if TPS readback causes an I2C failure that was not
present with write-only TPS diagnostics. Do not promote this patch to a fix;
it is evidence only.

Verification commands:

```bash
cd /srv/forge/android/meizu_m6/kernel-meizu_M6-N-ex6-linux-3.18.140
git diff --check -- kernel-3.18/drivers/misc/mediatek/video/mt6755/mtkfb.c kernel-3.18/drivers/misc/mediatek/lcm/ili9881p_hd_dsi_txd/ili9881p_hd_dsi_txd.c BRINGUP_STATE.md
env CCACHE_DIR=/srv/forge/android/ccache make -C /srv/forge/android/meizu_m6/kernel-meizu_M6-N-ex6-linux-3.18.140/kernel-3.18 \
  O=/srv/forge/work/m6-source-kernel-manual-20260520/out \
  ARCH=arm64 \
  CROSS_COMPILE=/srv/forge/android/meizu_m6/rom-meizu_M6-lineage-cm-14.1/prebuilts/gcc/linux-x86/aarch64/aarch64-linux-android-4.9/bin/aarch64-linux-android- \
  -j8 Image.gz-dtb
cd /srv/forge/android/export/meizu_m6_artifacts/20260606-m6-physical-black-gpio-tps-diag
sha256sum -c SHA256SUMS
cmp Image.gz-dtb verify-unpack/zImage
abootimg -i boot-m6-physical-black-gpio-tps-diag.img
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell 'echo m6_lcm_reinit:1 > /d/mtkfb; sleep 2; echo ata > /d/mtkfb; dmesg | grep -E "M6 gpio\\[|M6 LCM tps65132 read|M6 LCM ATA dcs|M6 DSI snapshot\\[bist" | tail -220'
```

## 2026-06-06 scrcpy-image / physical-black live split

Patch category: **DIAGNOSTIC / STATE-ONLY**. No source or device boot was
changed in this checkpoint.

FACT: the user reported that scrcpy shows an image while the physical LCD is
black. Fresh live capture:
`/srv/forge/android/meizu_m6/captures/20260606-080219-m6-scrcpy-image-physical-black-live`.
FACT: the capture's clean boot readback
`current-boot-readback-8863744.img` is sha256
`e4c317d025440efa5e04f158b255b4b127ce6e5c0848a149ccecefbbab6eabbd`, matching
the old `/srv/forge/android/export/meizu_m6_artifacts/20260605-m6-dsi-sleep-clock-diag-boot`
boot and not the pending GPIO/TPS diagnostic boot sha256
`c5689acac835038709943e8e3ccee77b03c5cb24b1bd2af14c7cda7b18edd09c`.
Therefore `M6 gpio[...]` and `M6 LCM tps65132 read` markers are still not
expected on the running device.

FACT: SurfaceFlinger still has a built-in 720x1280 display, `isDisplayOn=1`,
refresh `57.820002 fps`, GLES `Mali-T860`, launcher/statusbar/wallpaper
layers, and an `HWC_FRAMEBUFFER_TARGET` in
`surfaceflinger-compact.txt`. FACT: `/d/mtkfb` in the same capture reports
`LCM Driver=[ili9881p_hd_dsi_txd]`, `State=Alive`, `PathMode:DIRECT_LINK`,
`DISP_OPT_BYPASS_PQ=1`, and RDMA0 transfer around `60.72 fps`. FACT: direct
read of `/dev/graphics/fb0` into `fb0-head-4m.raw` returned 4 MiB with
sha256 `508e5c1af3d87986fc1485478cffce84bcbbe898e2604c78de087e1effb2fd50`;
`fb0-head-sample-words.txt` begins with repeated `ff00ff00`, so framebuffer
memory is not all black/zero. FACT: `ata-after-user-physical-black.txt` shows
DCS command transport remains healthy: `display_id=15 20 00`,
`display_status=80 03 06 00`, `power_mode=9c`, `pixel_format=07`, `id1=15`,
`id2=20`, and `id3=00`.

INFERENCE: the current physical-black symptom is not explained by empty
SurfaceFlinger composition, empty framebuffer memory, PQ, RDMA inactivity, or
dead DCS LP command transport. HYPOTHESIS: the next physical frontier remains
panel-side visibility: VSP/VSN/RST pin state, TPS65132 reg0/reg1 readback,
backlight/LED routing, or MIPI TX/HS lane/electrical/timing state. This is
exactly what the already-built GPIO/TPS diagnostic boot is designed to split.

FACT: this same capture also proves a separate runtime blocker. The first
system_server PID `676` completed normal boot enough to launch Trebuchet, but
`logcat-events-boot-tail.txt` and `logcat-watchdog-blockers-tail.txt` show a
Trebuchet `TIME_TICK` ANR at `09:05:10`, followed by
`*** WATCHDOG KILLING SYSTEM PROCESS` at `09:06:41`. FACT: after zygote
started system_server PID `3478`, `logcat-boot-timeline-tail.txt` stops after
`SystemServer: WaitForDisplay`; DisplayManager added the built-in display with
`state UNKNOWN`, but no later `Display device changed state: ON` is present.
FACT: current `dumpsys power` reports `mBootCompleted=false`,
`mSystemReady=false`, `mDisplayReady=false`, and `Display Power: state=UNKNOWN`;
`init.svc.bootanim=running` while `sys.boot_completed=1`, so
`sys.boot_completed=1` is stale/misleading after the system_server restart.

FACT: `anr/anr_2026-06-06-09-06-38-695` identifies the watchdog lock chain:
`android.anim` thread 26 holds `WindowHashMap` while blocked in
`SurfaceComposerClient::createSurface()` via binder to SurfaceFlinger;
`Binder:676_A` thread 89 holds `ActivityManagerService` while waiting for the
same `WindowHashMap` in `WindowManagerService.continueSurfaceLayout()` during
`ActivityManagerService.handleAppDiedLocked()` / `appDiedLocked()`. That in
turn blocks ActivityManager, android.ui, android.fg, and android.display
threads on the AMS monitor. Current binder state still shows SurfaceFlinger
PID `410` with active incoming transactions from the restarted system_server
and bootanimation.

INFERENCE: there are two active fronts, and they should not be conflated:
physical LCD black despite live FB/DCS evidence, and framework/SF watchdog
after the first userspace boot. HYPOTHESIS: the watchdog class is a
SurfaceFlinger/binder/createSurface stall or WMS/AMS lock-order hazard exposed
by an app death during display/window relayout; a separate runtime patch may be
needed even after the physical panel is fixed. A subagent was assigned the
read-only runtime/SF source audit so the main display loop can continue on the
panel frontier.

Expected next marker: after explicit human flash confirmation, flash
`/srv/forge/android/export/meizu_m6_artifacts/20260606-m6-physical-black-gpio-tps-diag/boot-m6-physical-black-gpio-tps-diag.img`
and collect `M6 gpio[...]` plus `M6 LCM tps65132 read` markers. If GPIO17/90/158
or TPS reg0/reg1 readback is wrong, patch that proven boundary. If GPIO/TPS are
correct and physical LCD is still black while FB/DCS remain healthy, move to
MIPI TX/HS lane/electrical/timing parity and stock LK handoff/backlight-side
effects. For runtime, inspect the subagent's SF/watchdog findings before
editing framework or vendor display userspace.

Rollback condition: none for this state-only checkpoint. Do not use
`sys.boot_completed=1` alone as a success criterion for this boot after the
observed system_server watchdog/restart.

Verification commands:

```bash
cd /srv/forge/android/meizu_m6/captures/20260606-080219-m6-scrcpy-image-physical-black-live
sha256sum current-boot-readback-8863744.img fb0-head-4m.raw
grep -E "power_mode|display_id|display_status|pixel_format|id[123]" ata-after-user-physical-black.txt
grep -E "WATCHDOG KILLING|WaitForDisplay|Display device added|Display device changed state" logcat-boot-timeline-tail.txt logcat-watchdog-blockers-tail.txt
grep -n "WindowHashMap\\|SurfaceComposerClient::createSurface\\|handleAppDiedLocked\\|appDiedLocked" anr/anr_2026-06-06-09-06-38-695
cd /srv/forge/android/export/meizu_m6_artifacts/20260606-m6-physical-black-gpio-tps-diag
sha256sum -c SHA256SUMS
# Flash only after explicit human confirmation:
# WAIT_SECONDS=7200 POLL_SECONDS=5 ./m6_wait_capture_flash_physical_black_gpio_tps_diag.sh
```

## 2026-06-06 physical-black LED/DCS route diagnostic

Patch category: **DIAGNOSTIC**. No LED mode, PWM, PMIC, GPIO, DSI timing,
panel command, or boot behavior is changed by this patch.

Hypothesis: FACT: live capture
`/srv/forge/android/meizu_m6/captures/20260606-085612-m6-live-backlight-probe-711HEBSR277K5`
shows Linux can call the active `ili9881p_hd_dsi_txd` backlight hook and send
DCS brightness command `0x51` with `dcs51=0xff` after a sysfs brightness write
to `255`; the physical LCD still remained black by human observation. FACT:
current M6 DTS routes `lcd-backlight` through `led_mode=<4>`, which is
`MT65XX_LED_MODE_CUST_LCM`, so the normal Linux brightness path bypasses the
PWM and PMIC backlight helpers unless another path calls them. HYPOTHESIS: the
next capture must prove whether the active Linux path is only CUST_LCM/DCS or
whether an unexpected PWM/PMIC branch is used, and must prove whether the panel
retains DCS `0x51`/`0x53`/`0x55` values after the reinit/ATA probe. Do not
change `led_mode` to PWM/PMIC until this read-only split is captured.

Evidence: `dmesg-backlight-probe-tail.txt` in the live capture contains
`lcm_setbacklight_cmdq ... level = 255` followed by
`M6 LCM backlight ... request=255 dcs51=0xff min=20 count=19 delta=235`.
The earlier scrcpy/physical-black capture
`/srv/forge/android/meizu_m6/captures/20260606-080219-m6-scrcpy-image-physical-black-live`
shows SurfaceFlinger composition, framebuffer content, RDMA transfer, PQ bypass,
and healthy DCS ID/status reads. A read-only side audit of stock `lk.img` found
strings for `enable backlight after show bootlogo!`, `backlight_set_pwm`,
`brightness_set_pwm`, PMIC backlight text, and the active `ili9881p_hd_dsi_txd`
LCM strings, but raw disassembly was not reliable enough to prove that stock M6
actually takes a PWM/PMIC branch. Strings alone are not evidence to change DTS.

Files changed:

- `kernel-3.18/drivers/misc/mediatek/lcm/ili9881p_hd_dsi_txd/ili9881p_hd_dsi_txd.c`
  extends the existing bounded ATA read sweep with DCS `0x51` as `brightness`,
  `0x53` as `ctrl_display`, and `0x55` as `cabc`.
- `kernel-3.18/drivers/misc/mediatek/leds/mt6755/leds.c` adds bounded
  `lcd-backlight` markers around class-direct/AAL and CUST PWM/LCM/BLS-PWM route
  decisions.
- `kernel-3.18/drivers/misc/mediatek/leds/leds_drv.c` adds bounded
  `lcd-backlight` markers around the common LED wrapper and high-resolution
  backlight entrypoints.
- `BRINGUP_STATE.md` records the live evidence, side-audit result, artifact
  identity, expected markers, rollback condition, and verification commands.

Why each file changed: the active LCM driver is the only safe place to read
panel DCS register state using the already-working ATA/debugfs path. The
MT6755 LED HAL file owns the route decision that distinguishes CUST_LCM from
CUST_PWM/CUST_BLS_PWM at runtime. The common LED wrapper owns the sysfs and
high-resolution backlight entrypoints before they reach the MT6755 LED HAL, so
both files are needed to prove whether a brightness write was transformed or
routed away before the LCM hook.

Build/artifact result: `Image.gz-dtb` built successfully from
`/srv/forge/work/m6-source-kernel-manual-20260520/out` using the documented
`-j8 Image.gz-dtb` command. Boot-only artifact:
`/srv/forge/android/export/meizu_m6_artifacts/20260606-m6-physical-black-led-dcs-diag/boot-m6-physical-black-led-dcs-diag.img`.
Artifact sha256 identities:

- `boot-m6-physical-black-led-dcs-diag.img`:
  `1eee7865c1c5b09e111c22f269a043fa2447c1e490cebdd3d5c5c7a0e3c41e9e`
- `Image.gz-dtb`:
  `a7fa6ccf3ad5c74277664232f70e39bb9bbf6ab44ba16195846e77fda93b1b5b`
- `System.map`:
  `b6bf684dec6084773ba4203fbf5b8884ba6a1c29e2aa05a5de42c882a2c5a384`
- `kernel.config`:
  `bc272726035c1a2eca9422e9bc230cf54f8295648a8865a98faba046ab01619e`
- `ramdisk.img`:
  `e82c6695614132e8759b9ee96ee5b9e9efdaf8df96d1ef0c32c5dae8b5e16332`
- `SHA256SUMS`:
  `201c855828dd1b61a34b6ed300ada27dd344ff30f777d56740901c4f72145931`
- `m6_wait_capture_flash_physical_black_led_dcs_diag.sh`:
  `1cfca6f5d87c2d75c95c0db10bf742ee875b54cb58f6fa43dc8df0385ce98849`

`abootimg -i` reports unchanged boot geometry: page size `2048`, boot name
`1552631950`, kernel address `0x40080000`, ramdisk address `0x45000000`, tags
address `0x44000000`, and cmdline
`bootopt=64S3,32N2,64N2 androidboot.selinux=permissive binder.devices=binder,hwbinder,vndbinder`.
`sha256sum -c SHA256SUMS` passed, `cmp Image.gz-dtb verify-unpack/zImage`
passed, `cmp ramdisk.img verify-unpack/ramdisk.img` passed, and `bash -n`
passed for the flash/capture helper.

Expected next marker: after explicit human flash confirmation, flash
`boot-m6-physical-black-led-dcs-diag.img` and run the helper capture. The
postboot capture should contain `M6 LED path[class-direct]` and
`M6 LED path[cust-lcm]` with `mode=4` if Linux still routes through CUST_LCM.
If `M6 LED path[cust-pwm]`, `disp_pwm_set_backlight`, `mt_backlight_set_pwm`,
or `mt_brightness_set_pmic` appears, that is the first evidence that a PWM/PMIC
path is active. After `m6_lcm_reinit:1` plus `ata`, dmesg should contain
`M6 LCM ATA dcs[...] name=brightness cmd=0x51`, `name=ctrl_display cmd=0x53`,
and `name=cabc cmd=0x55`. If `brightness` reads back `ff` and GPIO/TPS markers
are healthy while the physical LCD remains black, continue to MIPI TX/HS
lane/electrical/timing parity or an LK-only backlight enable side effect.

Rollback condition: revert this diagnostic if the verified boot regresses before
ADB/SurfaceFlinger, if the LED markers flood logs beyond bounded brightness
transitions, or if the added DCS `0x51`/`0x53`/`0x55` reads introduce DSI read
timeouts/panel resets not present in the previous ATA sweep. Do not promote this
patch to a fix; it is evidence only.

Verification commands:

```bash
cd /srv/forge/android/meizu_m6/kernel-meizu_M6-N-ex6-linux-3.18.140
git diff --check -- kernel-3.18/drivers/misc/mediatek/lcm/ili9881p_hd_dsi_txd/ili9881p_hd_dsi_txd.c kernel-3.18/drivers/misc/mediatek/leds/mt6755/leds.c kernel-3.18/drivers/misc/mediatek/leds/leds_drv.c BRINGUP_STATE.md
env CCACHE_DIR=/srv/forge/android/ccache make -C /srv/forge/android/meizu_m6/kernel-meizu_M6-N-ex6-linux-3.18.140/kernel-3.18 \
  O=/srv/forge/work/m6-source-kernel-manual-20260520/out \
  ARCH=arm64 \
  CROSS_COMPILE=/srv/forge/android/meizu_m6/rom-meizu_M6-lineage-cm-14.1/prebuilts/gcc/linux-x86/aarch64/aarch64-linux-android-4.9/bin/aarch64-linux-android- \
  -j8 Image.gz-dtb
cd /srv/forge/android/export/meizu_m6_artifacts/20260606-m6-physical-black-led-dcs-diag
sha256sum -c SHA256SUMS
cmp Image.gz-dtb verify-unpack/zImage
cmp ramdisk.img verify-unpack/ramdisk.img
bash -n m6_wait_capture_flash_physical_black_led_dcs_diag.sh
gzip -cd Image.gz-dtb 2>/tmp/m6-led-dcs-gzip.err | strings | grep -E 'M6 LED path|M6 LED drv path|brightness|ctrl_display|cabc|M6 gpio\[|M6 LCM tps65132 read'
# Flash only after explicit human confirmation:
# WAIT_SECONDS=7200 POLL_SECONDS=5 ./m6_wait_capture_flash_physical_black_led_dcs_diag.sh
```

## 2026-06-06 guarded PWM dump diagnostic refresh

Patch category: **DIAGNOSTIC**. No LED mode, PWM enable, PMIC setting, panel
command, GPIO, DSI timing, or boot behavior is changed by this patch. This
refresh supersedes the previous `led-dcs` artifact as the preferred next flash
because it keeps the same LED/DCS markers and adds a guard for unsafe PWM debug
dumps.

Hypothesis: FACT: read-only capture
`/srv/forge/android/meizu_m6/captures/20260606-091630-m6-currentboot-pwm-reg-dump-711HEBSR277K5`
attempted `/d/dispsys dump_reg:13` on the then-running old boot and the device
disappeared. FACT: follow-up capture
`/srv/forge/android/meizu_m6/captures/20260606-091932-m6-after-dispsys-pwm-dump-disconnect-711HEBSR277K5`
shows `ro.boot.bootreason=wdt_by_pass_pwk`; pstore records
`Unable to handle kernel NULL pointer dereference`, `PC is at
pwm_dump_reg+0x19c/0x2b4`, then `ddp_dump_reg+0xdc/0x15c` and
`ddp_process_dbg_opt+0xafc/0x10b0`. HYPOTHESIS: `DISPSYS_PWM0_BASE` is NULL
or otherwise unmapped for the debug dump path; raw `/d/dispsys dump_reg:13`
must not be used on old kernels. A guard is required before adding PWM register
dumps to the capture helper.

Evidence: current `ddp_hal.h` enum maps `DISP_MODULE_PWM0` to module ID `13`
and `DISP_MODULE_PWM1` to `23`; the earlier side-audit suggestion to try `12`
would have dumped `UFOE`, not PWM. The same follow-up boot logs
`[PWM] backlight is on (1023), ddp_pwm power:(1)`, but source inspection of
`drivers/misc/mediatek/video/common/aal20/ddp_pwm.c` shows this can be dummy
status when `g_pwm_led_mode != MT65XX_LED_MODE_CUST_BLS_PWM`; it is not proof
of a physical PWM backlight branch.

Files changed:

- `kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_dump.c` now checks the
  PWM register base before reading `PWM_EN`, `PWM_CON_0`, `PWM_CON_1`, or
  `PWM_DEBUG`; if the base is NULL it logs `DISP PWM%d base is NULL` and
  returns.
- `BRINGUP_STATE.md` records the pstore evidence, enum correction, artifact
  identity, expected markers, rollback condition, and verification commands.

Why each file changed: `ddp_dump.c` is the exact debugfs path that crashed via
`/d/dispsys dump_reg:13`. Guarding the read-only dump path keeps future capture
helpers from rebooting the device while still preserving evidence: either the
PWM base is missing, or the dump prints real `PWM_EN`/`PWM_CON` values.

Build/artifact result: `Image.gz-dtb` built successfully from
`/srv/forge/work/m6-source-kernel-manual-20260520/out` using the documented
`-j8 Image.gz-dtb` command. Preferred boot-only artifact:
`/srv/forge/android/export/meizu_m6_artifacts/20260606-m6-physical-black-led-dcs-pwmguard-diag/boot-m6-physical-black-led-dcs-pwmguard-diag.img`.
Artifact sha256 identities:

- `boot-m6-physical-black-led-dcs-pwmguard-diag.img`:
  `0e9ba01b34559b03026818e7c9dec02f283d77ab174c75614b9b9c95c261e78d`
- `Image.gz-dtb`:
  `a6547b3a8635d07ef346a995c8d468070457ff54feadfd73016dce37291250e2`
- `System.map`:
  `38406138698fedb88bd55faa0b9b770b6c550ec3f41d88b8c4d1e92a7f3bf1fb`
- `kernel.config`:
  `bc272726035c1a2eca9422e9bc230cf54f8295648a8865a98faba046ab01619e`
- `ramdisk.img`:
  `e82c6695614132e8759b9ee96ee5b9e9efdaf8df96d1ef0c32c5dae8b5e16332`
- `SHA256SUMS`:
  `6cc926cfac6f8d23903c69bf770b90fa80630b6f34bdc532586351d9e85f8bd6`
- `m6_wait_capture_flash_physical_black_led_dcs_pwmguard_diag.sh`:
  `d1d65cfc4a498e5d664c1a51e6f3702a3457787bd87182b5e27e87b00de1ce88`

`sha256sum -c SHA256SUMS` passed, `cmp Image.gz-dtb verify-unpack/zImage`
passed, `cmp ramdisk.img verify-unpack/ramdisk.img` passed, `bash -n` passed
for the helper, and payload strings include `M6 LED path`, `M6 LED drv path`,
`brightness`, `ctrl_display`, `cabc`, `M6 gpio[`, `M6 LCM tps65132 read`, and
`DISP PWM%d base is NULL`.

Expected next marker: after explicit human flash confirmation, use the
`pwmguard` helper, not the older `led-dcs` helper. Postboot capture should show
the LED/DCS markers listed in the previous section plus guarded
`/d/dispsys dump_reg:13` and `dump_reg:23` output. If the PWM base is still
unmapped, dmesg should contain `DISP PWM0 base is NULL` and/or
`DISP PWM1 base is NULL` without a reboot. If the base is mapped, capture should
contain `PWM_EN`, `PWM_CON_0`, `PWM_CON_1`, and `PWM_DEBUG`.

Rollback condition: revert this diagnostic if the verified boot regresses before
ADB/SurfaceFlinger or if guarded `/d/dispsys dump_reg:13` still causes panic.
Do not use this guard as evidence that PWM is working; it only makes the dump
path safe.

Verification commands:

```bash
cd /srv/forge/android/meizu_m6/kernel-meizu_M6-N-ex6-linux-3.18.140
git diff --check -- kernel-3.18/drivers/misc/mediatek/video/mt6755/ddp_dump.c BRINGUP_STATE.md
env CCACHE_DIR=/srv/forge/android/ccache make -C /srv/forge/android/meizu_m6/kernel-meizu_M6-N-ex6-linux-3.18.140/kernel-3.18 \
  O=/srv/forge/work/m6-source-kernel-manual-20260520/out \
  ARCH=arm64 \
  CROSS_COMPILE=/srv/forge/android/meizu_m6/rom-meizu_M6-lineage-cm-14.1/prebuilts/gcc/linux-x86/aarch64/aarch64-linux-android-4.9/bin/aarch64-linux-android- \
  -j8 Image.gz-dtb
cd /srv/forge/android/export/meizu_m6_artifacts/20260606-m6-physical-black-led-dcs-pwmguard-diag
sha256sum -c SHA256SUMS
cmp Image.gz-dtb verify-unpack/zImage
cmp ramdisk.img verify-unpack/ramdisk.img
bash -n m6_wait_capture_flash_physical_black_led_dcs_pwmguard_diag.sh
gzip -cd Image.gz-dtb 2>/tmp/m6-pwmguard-gzip.err | strings | grep -E 'M6 LED path|M6 LED drv path|brightness|ctrl_display|cabc|M6 gpio\[|M6 LCM tps65132 read|DISP PWM%d base is NULL'
# Flash only after explicit human confirmation:
# WAIT_SECONDS=7200 POLL_SECONDS=5 ./m6_wait_capture_flash_physical_black_led_dcs_pwmguard_diag.sh
```

## 2026-06-06 runtime/SF watchdog side audit

Patch category: **DIAGNOSTIC / STATE-ONLY**. No source changed for this runtime
audit in this section.

FACT: runtime side audit of the
`20260606-080219-m6-scrcpy-image-physical-black-live` capture found the first
framework boot did reach enough UI to launch Trebuchet, then Trebuchet ANR/death
triggered a system_server watchdog. The ANR lock chain is: a WMS animation
thread holds `WindowHashMap` while blocked in
`SurfaceComposerClient::createSurface()` binder work; an AMS binder thread holds
`ActivityManagerService` while waiting for the same `WindowHashMap` during
`ActivityManagerService.handleAppDiedLocked()` / `appDiedLocked()` /
`LocalService.setHasOverlayUi`; this blocks ActivityManager, android.ui,
android.fg, and android.display. After system_server restarts, logs stop near
`SystemServer: WaitForDisplay`; `mSystemReady=false`, `mDisplayReady=false`,
and services such as `settings` may be absent even though the stale
`sys.boot_completed=1` property remains.

INFERENCE: this runtime blocker is separate from the physical LCD black split.
SurfaceFlinger/FB/RDMA/DCS evidence is already enough to continue physical
display diagnostics, but a ROM/framework patch will likely be needed to keep
system_server stable after Trebuchet/app-death or SF `createSurface` stalls.

Expected next marker: a runtime patch should first add DIAGNOSTIC timing/lock
markers around AMS app-death handling, WMS `mWindowMap` hold time in
`createSurfaceControl`/`continueSurfaceLayout`, and SurfaceFlinger
`createSurface` binder latency. A proper fix candidate is to avoid calling WMS
surface-layout work while holding the AMS monitor during app death, or to move
the overlay-ui/app-death side effect async/outside the lock, but only after
fresh timing markers prove the exact stall.

Verification commands:

```bash
cd /srv/forge/android/meizu_m6/captures/20260606-080219-m6-scrcpy-image-physical-black-live
grep -n "WindowHashMap\|SurfaceComposerClient::createSurface\|handleAppDiedLocked\|appDiedLocked\|SET_HAS_OVERLAY_UI" anr/anr_2026-06-06-09-06-38-695
grep -E "WATCHDOG KILLING|WaitForDisplay|Display device added|Display device changed state|system_server" logcat-boot-timeline-tail.txt logcat-watchdog-blockers-tail.txt
```

## 2026-06-07 runtime4 lit-black panel marker boot

Patch category: **DIAGNOSTIC / ARTIFACT**. This checkpoint records a boot-only
artifact for the black-but-lit physical panel split. The artifact preserves the
runtime4 ramdisk/cmdline and changes only the kernel payload, but it was built
from the current dirty kernel tree, which also contains parallel Wi-Fi, camera,
and charger edits. Those edits are not claimed as display fixes here.

Hypothesis: FACT: runtime4 shows scrcpy/UI alive, SurfaceFlinger flips, display
state ON, direct link, RDMA0 transfer, PQ bypassed, and DCS/backlight `0x51`
at `0xff`, while the human reports the physical LCD remains black with glow.
FACT: live DSI BIST red/green/blue probes set `BIST_CON=0x200040` and
`self_pat=1` with DSI video-state samples. HYPOTHESIS: the next missing signal
is panel-side physical visibility readback: VSP/VSN/RST GPIO state and
TPS65132 reg0/reg1 after Linux reinit, then MIPI lane/electrical/timing parity
if those are healthy.

Evidence:
- Runtime4 capture:
  `/srv/forge/android/meizu_m6/captures/20260607-135927-m6-runtime4-wifiowner-afterboot-711HEBSR277K5`.
- Live BIST capture:
  `/srv/forge/android/meizu_m6/captures/20260607-141447-m6-live-litblack-dsi-bist-window-711HEBSR277K5`.
- Capture-local report:
  `/srv/forge/android/meizu_m6/captures/20260607-141447-m6-live-litblack-dsi-bist-window-711HEBSR277K5/display-bist-hs-result.md`.
- Artifact directory:
  `/srv/forge/android/export/meizu_m6_artifacts/20260607-m6-runtime4-litblack-panel-markers-boot`.
- Source runtime4 boot sha256:
  `4d19675fa7b2d35930c5a54b70162666b238a2aa9232ce6665cec2471d87ed0c`.
- New boot sha256:
  `31a494473c1fd12e3b87367979c763688e69ce4172091620aca21e1bc8852e95`.
- New `Image.gz-dtb` sha256:
  `da4f27c80333ee69923b2466cbd3fa4816b62b551fea80cd244afa0c20b9d964`.
- Matching `System.map` sha256:
  `0177bdbfddf0405425b7627c597364bbf1893e4ef798513744b29cc8fdc49d13`.
- Runtime4 ramdisk sha256:
  `d17a4fb06cb4005e8e2f8e02f2e558a3c87d0755dd682c6454675f9e0200d016`.

Files changed:
- `captures/20260607-141447-m6-live-litblack-dsi-bist-window-711HEBSR277K5/display-bist-hs-result.md`:
  state-only capture verdict for the lit-black BIST split.
- `BRINGUP_STATE.md`: artifact identity, hypothesis, expected markers, and
  rollback condition for the runtime4-preserving marker boot.

Why each file changed: the capture-local report keeps DSI BIST evidence next to
the capture. This state file records the artifact identity required before
flashing and interpreting `M6 gpio[...]`, TPS readback, LED route, PWM guard,
and DSI BIST markers.

Expected next marker: after flashing
`boot-m6-runtime4-litblack-panel-markers-expanded.img`, run `m6_lcm_reinit:1`,
`ata`, and a short BIST sequence. Dmesg should contain `M6 gpio[...]` for
VSP/VSN/RST, `M6 LCM tps65132 read addr=0x00 ret=15` and `addr=0x01 ret=15`,
LED route markers for `lcd-backlight`, DCS `brightness/ctrl_display/cabc`
readbacks, and `M6 DSI snapshot[bist-post-enable] ... self_pat=1`.

Rollback condition: revert to the runtime4 boot if the marker boot regresses
before ADB, SurfaceFlinger, direct-link/RDMA transfer, or backlight/DCS `0x51`.
If GPIO/TPS readbacks are healthy and physical BIST remains invisible, do not
reopen PQ/HWC; move to MIPI TX electrical/lane/timing parity or stock LK hidden
panel/PHY side effects.

Verification commands:

```bash
cd /srv/forge/android/export/meizu_m6_artifacts/20260607-m6-runtime4-litblack-panel-markers-boot
sha256sum -c SHA256SUMS
cmp Image.gz-dtb verify-expanded-unpack/zImage
cmp runtime4-ramdisk.img verify-expanded-unpack/initrd.img
abootimg -i boot-m6-runtime4-litblack-panel-markers-expanded.img
gzip -cd Image.gz-dtb 2>/tmp/m6-runtime4-litblack-image-gzip.err | strings | grep -E 'M6 gpio\[|M6 LCM tps65132 read|M6 LED path|M6 LED drv path|M6 DSI snapshot\[bist|DISP PWM%d base is NULL'
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell 'svc power stayon true; settings put system screen_off_timeout 2147483647; input keyevent 224; settings put system screen_brightness 255; echo 255 > /sys/class/leds/lcd-backlight/brightness'
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell 'echo m6_lcm_reinit:1 > /d/mtkfb; sleep 2; echo ata > /d/mtkfb; echo dsipattern:0x00ff0000 > /d/mtkfb; sleep 4; echo dsipattern:0 > /d/mtkfb'
adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5 shell 'dmesg | grep -E "M6 gpio\\[|M6 LCM tps65132 read|M6 LCM ATA dcs|power_mode|M6 DSI snapshot\\[bist|BIST_PATTERN|self_pat|M6 LED path|M6 LCM backlight" | tail -260'
```

## 2026-06-07 runtime4 lit-black root marker result

Patch category: **DIAGNOSTIC / STATE-ONLY**. This checkpoint records the
post-flash root marker capture for the black-but-lit physical panel split. No
source code or device behavior is changed by this entry.

Hypothesis: FACT: the physical display is black but lit/glowing while scrcpy/UI
is alive. FACT: the previous runtime4 and BIST captures already reject
SurfaceFlinger, PQ, HWC policy, direct-link route construction, RDMA transfer,
DSI LP DCS transport, and generic backlight-off as the first frontier. This
root marker capture was meant to close the remaining Linux-side panel
power/reset/bias/readback questions before moving below the DSI controller.

Evidence:
- Root marker capture:
  `/srv/forge/android/meizu_m6/captures/20260607-145745-m6-rootmarkers-runtime4-litblack-panel-markers-711HEBSR277K5`.
- Capture-local report:
  `/srv/forge/android/meizu_m6/captures/20260607-145745-m6-rootmarkers-runtime4-litblack-panel-markers-711HEBSR277K5/display-rootmarkers-result.md`.
- Runtime4 capture:
  `/srv/forge/android/meizu_m6/captures/20260607-135927-m6-runtime4-wifiowner-afterboot-711HEBSR277K5`.
- Live BIST capture:
  `/srv/forge/android/meizu_m6/captures/20260607-141447-m6-live-litblack-dsi-bist-window-711HEBSR277K5`.
- Boot identity caveat: preflash artifact sha256 is
  `31a494473c1fd12e3b87367979c763688e69ce4172091620aca21e1bc8852e95`, but
  postboot readback sha256 is
  `d565eae0d3ea38f93a9c92edc8d3afe11c9381cfa108d5fe875a55b40d2a286d`. Runtime
  marker lines are valid evidence for the running kernel, but any future
  symbol/address decoding must use the readback image and its matching map, not
  the `31a494...` artifact map.
- Root/runtime state: root shell, SELinux permissive, `sys.boot_completed=1`,
  SurfaceFlinger running, and boot animation stopped.
- GPIO/TPS evidence: VSP GPIO17, VSN GPIO90, and reset GPIO158 reach
  output/input high during manual reinit; TPS65132 on adapter 0 client
  `0x3e/i2c_lcd_bias` reads back `0x0f` from reg0 and reg1 after writes.
- DCS evidence: after reinit the panel returns `display_id=15 20 00`,
  `display_status=80 03 06 00`, `power_mode=9c`, `pixel_format=07`, and ID
  registers `15/20/00`. `0x51`, `0x53`, and `0x55` read back `00`; keep this as
  a panel-state clue, but it does not reopen generic backlight-off because the
  user reports glow and prior runtime markers show the backlight callback
  writing `dcs51=0xff`.
- DSI BIST evidence: red, green, and blue self-pattern windows set
  `BIST_CON=0x200040` and `self_pat=1`, show DSI `STATE7` video data period
  samples, and print stable MIPITX lane/PLL snapshots. Disabling BIST clears
  `BIST_CON=0x0` and `self_pat=0`.
- Side-effect note: the forced reinit/BIST window also logs CMDQ
  `RDMA0_EOF` waits, DEVAPC SMI_LARB violations, and OVL diagnostics while
  SurfaceFlinger/SystemUI are active. Treat those as stress/interaction
  evidence from the manual BIST window unless they reproduce without forced
  reinit/BIST; they are not the earliest physical-black frontier yet.

Files changed:
- `captures/20260607-145745-m6-rootmarkers-runtime4-litblack-panel-markers-711HEBSR277K5/display-rootmarkers-result.md`:
  capture-local verdict for the root marker pass.
- `BRINGUP_STATE.md`: durable state, evidence, expected next marker, and
  rollback condition for the next display diagnostic patch.

Why each file changed: the capture-local report keeps the raw marker verdict
next to the capture. This state file records the closed branches and prevents
future agents from looping back into PQ, HWC, RDMA, TPS bus, reset, or
generic-backlight hypotheses that the current evidence already rejects.

INFERENCE: if the physical panel stayed black/lit during the verified RGB BIST
windows, the earliest open frontier is below Linux reset/bias/init and below
the DSI controller self-pattern register write. The remaining candidates are
MIPI TX electrical lane mapping/state, lane swap or polarity, PHY settle/drive
settings, panel-side acceptance of HS video despite healthy LP reads, panel LED
electrical routing, or a stock LK-only DSI/PHY/panel side effect that Linux does
not reproduce.

Expected next marker: add a bounded **DIAGNOSTIC** patch, preferably in the DSI
snapshot path rather than policy/userspace, that decodes MIPITX/DSI lane/PHY
acceptance state around `DSI_Start()`, `start-after-hs`, and BIST post-enable:
lane swap, PHY select, SW control, RT code/state bits, PLL/clock lane/data lane
enable bits, and the exact mode/porch/PLL values of the running image. If those
match stock LK-derived expectations and BIST remains physically invisible, move
to stock LK hidden DSI/PHY/panel side effects or board-level panel LED/electrical
routing. Do not build a full zip for this; a boot-only diagnostic kernel is
enough.

Rollback condition: none for this state-only note. Revert the next diagnostic
kernel patch if it changes display timing/route behavior, regresses
ADB/SurfaceFlinger/RDMA/backlight compared with runtime4, or causes new DSI/CMDQ
failures before producing the expected lane/PHY markers.

Verification commands:

```bash
CAP=/srv/forge/android/meizu_m6/captures/20260607-145745-m6-rootmarkers-runtime4-litblack-panel-markers-711HEBSR277K5
grep -E "M6 gpio\\[|M6 LCM tps65132 read|M6 LCM ATA dcs|power_mode|M6 DSI snapshot\\[bist|BIST_PATTERN|self_pat|MIPITX lanes" "$CAP/reinit-ata-rgb-bist-markers.txt"
cat "$CAP/boot-readback-sha256.txt"
sha256sum "$CAP/unpack-readback/zImage" "$CAP/unpack-readback/initrd.img"

A="/usr/bin/adb -H 127.0.0.1 -P 15038 -s 711HEBSR277K5"
NEXT=/srv/forge/android/meizu_m6/captures/$(date +%Y%m%d-%H%M%S)-m6-litblack-dsi-phy-lanes-711HEBSR277K5
mkdir -p "$NEXT"
$A root
$A wait-for-device
$A shell 'svc power stayon true; settings put system screen_off_timeout 2147483647; input keyevent 224; settings put system screen_brightness 255; echo 255 > /sys/class/leds/lcd-backlight/brightness'
$A shell 'echo m6_lcm_reinit:1 > /d/mtkfb; sleep 2; echo ata > /d/mtkfb; echo dsipattern:0x00ff0000 > /d/mtkfb; sleep 3; echo dsipattern:0x0000ff00 > /d/mtkfb; sleep 3; echo dsipattern:0x000000ff > /d/mtkfb; sleep 3; echo dsipattern:0 > /d/mtkfb'
$A shell dmesg > "$NEXT/dmesg-after-rgb-bist.txt"
$A shell 'cat /d/mtkfb 2>&1' > "$NEXT/mtkfb.txt"
$A shell 'dumpsys display; dumpsys SurfaceFlinger; dumpsys power' > "$NEXT/dumpsys-display-sf-power.txt"
```

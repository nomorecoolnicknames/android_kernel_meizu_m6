# Meizu M6 Source Kernel Bring-up State

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

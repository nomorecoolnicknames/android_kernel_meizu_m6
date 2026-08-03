# LANE: Camera — Meizu M6 (711HEBRN23L3N), LOS 16.0 — 2026-08-03

Owner: camera-lane agent. Device access: read-only (adb port 5041 on container,
`-s 711HEBRN23L3N`). No device writes, no flash, no reboot performed by this lane.
Kernel: 3.18.140 `#1 SMP PREEMPT Mon Aug 3 13:00:47 MSK 2026` (out-rot0 build, FACT
`/proc/version`). ROM: `lineage_meizu_m6:9/PQ3A.190801.002/root07251016`, built
Jul 25 10:16 UTC (FACT `ro.build.date`).

## 1. FACT — actual state: enumeration WORKS, every open FAILS

`dumpsys media.camera` (23:12, live):

```
Number of camera devices: 2
== Camera HAL device device@1.0/legacy/0 (v1.0): Facing: Back, Orientation: 90
== Camera HAL device device@1.0/legacy/1 (v1.0): Facing: Front, Orientation: 270
```

The framework sees both cameras via HAL1/API1 passthrough. This is already past the
m681 blocker class (no ISP driver): on the M6 3.18 kernel all camera nodes exist
(FACT, `ls`): `/dev/camera-isp`, `/dev/camera-fdvt`, `/dev/kd_camera_hw{,_bus2}`,
`/dev/kd_camera_flashlight`. Kernel sensor probe succeeded (FACT,
`/proc/driver/camera_info`): `CAM[1]:imx278trulymipiraw; CAM[2]:ov8856mipiraw;`
— same silicon as stock (stock capture 2026-07-25: `imx278trulymipiraw` /
`ov8856jslmipiraw`; see §5 for why the front name differs and why it does not matter).

CameraService lives in mediaserver (pid 503, 32-bit) on this build; the passthrough
HAL loads in-process, which is why all MtkCam lines below carry pid 503.

## 2. FACT — the first real blocker, with the full log chain

Every `connect` (mediaserver shim-metadata, FlashlightController, any app) dies the
same way (logcat, 23:11:17.996, pid 503):

```
CameraService: CameraService::connect call (PID -1 "media", camera ID 0) ... Camera API version 1
MtkCam/devicemgr: [getPlatform] dlopen: libcam_platform.so error=dlopen failed:
                  library "libskia.so" not found (CamDeviceManagerBase.platform.cpp:96)
MtkCam/devicemgr: [openDeviceLocked] No Platform (CamDeviceManagerBase.openDevice.cpp:383)
CameraClient  : initialize: Camera 0: unable to initialize device: Function not implemented (-38)
CameraService : connectHelper: Could not initialize client from HAL.
```

FACT (`ls` on device): no `libskia.so` in `/system/lib{,64}` or `/system/vendor/lib{,64}`.
Pie removed shared libskia platform-wide: `external/skia` is `cc_library_static`, the
code lives inside `libhwui` (same finding as `device/meizu/m95/BLOB_SHIMS.md`).

Mechanism (FACT, readelf): `libcam_platform.so` itself does NOT need libskia; its
DT_NEEDED chain reaches `libcam.device1.so`/`libcam.device3.so` →
`libcam.camadapter.so`, and **`libcam.camadapter.so` carries DT_NEEDED [libskia.so]**.
A missing NEEDED library fails the entire dlopen even if nothing is called from it.

## 3. FACT — full dlopen-closure audit (offline, west)

Script `/home/gun/cam_audit.py` (copy in session scratchpad): BFS over DT_NEEDED from
the runtime entry points (`camera.mt6750.so`, dlopen targets `libcam_platform.so`,
`libcam.camadapter.so`, provider/device impls), pools = built image
(`/home/gun/m6-out16/.../system`) + vendor blobs
(`vendor/meizu/meizu_m6/proprietary`). Two passes: file existence, then non-weak UND
symbol coverage. Results (32-bit closure = 218 libs, 64-bit = 214):

- **Missing files: `libskia.so` only** (plus `libtiff.so`, wanted only BY the stock
  libskia blob — irrelevant with the stub, see below).
- **Unresolved symbols on the camera path: none** — the only camera-adjacent gaps are
  inside the stock `libskia.so` blob itself (4 MTK-patched libpng exports:
  `png_build_index`, `png_configure_decoder`, `png_set_interlaced_pass`,
  `png_set_seek_fn` — Pie's libpng lacks them). The remaining unresolved set is
  ICU-60 imports of framework libs, an audit-pool artifact (framework resolves them
  from /system/lib at runtime; UI demonstrably renders).
- FACT (nm, both ABIs): **`libcam.camadapter.so` imports ZERO Sk\* symbols.** In the
  whole installed vendor set only `libaal.so` (13) and `libvtmal.so` (18) genuinely
  import Sk\* — display-AAL and video-telephony, both already unloadable today, both
  explicitly not rescued (faking N-era Skia layouts = memory corruption, m95 rule).
- FACT (md5): device == out tree == proprietary for the camera path
  (`camera.mt6750.so` `1f4d399b…`, `libcam_platform.so` `e922da34…`,
  `libcam.camadapter.so` `172398a2…`). The stub is the ONLY missing piece for open.

**Decision: empty `libskia.so` stub, both ABIs** — the m95 precedent
(`device/meizu/m95/shims/`, `BLOB_SHIMS.md`) verbatim: shipping the real stock
libskia would drag libtiff + 4 patched-libpng exports (a chain, not a fix), for a
consumer that calls nothing from it.

REJECTED — "ship stock libskia.so + deps": costs libtiff.so + a libpng shim (4 MTK
symbols) + ICU-56 exposure, buys nothing camadapter uses.
REJECTED — "the HAL/manifest is broken like light/gatekeeper were": provider runs,
enumerates 2 devices, `lshal`-visible; failure is strictly at device-open dlopen.

## 4. Change made (tree, west `/home/gun/m6rom16/rom` — plain dirs, not git)

1. **NEW** `device/meizu/meizu_m6/shims/skia.cpp` — one empty marker function
   (`__m6_libskia_stub_marker`), header documents the evidence.
2. **NEW** `device/meizu/meizu_m6/shims/Android.mk` — module `libskia_m6_stub`,
   `LOCAL_MULTILIB := both`, proprietary, `LOCAL_POST_INSTALL_CMD` symlinks
   `$(TARGET_OUT_VENDOR)/lib{,64}/libskia.so → libskia_m6_stub.so` (m95 pattern; the
   module cannot be named `libskia` — ambiguous with Soong's static module,
   `mka libskia` starts building external/skia).
3. `device/meizu/meizu_m6/device_meizu_m6.mk:423-424` — `PRODUCT_PACKAGES +=
   libskia_m6_stub` (evidence comment above it), right after the camera provider block.

Build: `build-m6-cam.sh` (copy of build-m6-shim.sh, target `mka -j8 libskia_m6_stub`),
serialized via `/home/gun/.m6build.lock`. Artifacts + hashes: §7 / MANIFEST.

## 5. Side findings (device, read-only)

- **Front sensor name mismatch RESOLVED-HARMLESS.** LOS reports `ov8856mipiraw`,
  stock reported `ov8856jslmipiraw`. The flashed kernel config builds ONLY the JSL
  driver (`out-rot0/.config: CONFIG_CUSTOM_KERNEL_IMGSENSOR="imx278_mipi_raw
  ov8856jsl_mipi_raw"`), and `kd_sensorlist.h:156-158` registers that ONE driver
  under BOTH IDs/names (`{OV8856_SENSOR_ID, "ov8856mipiraw", OV8856JSLMIPIRAW_SensorInit}` and
  `{OV8856JSL_SENSOR_ID, "ov8856jslmipiraw", OV8856JSLMIPIRAW_SensorInit}`). Same code either way.
- **H1 (clock rates) — idle half MEASURED OUT.** `/sys/kernel/debug/clk/clk_summary`
  is readable without root on LOS (unlike expected). Idle camera clock tree is
  IDENTICAL to the stock idle capture (`captures/m6cap-20260725-1305/43-camera_clk`):
  `scam_sel=109 200 000`, `camtg_sel=48 000 000`, `img_image_*=286 000 000`,
  `img_image_sen_cam=48 000 000` (yes, 48M on stock too — the state-doc summary just
  didn't list it). The load-bearing H1 test — clk_summary DURING preview — becomes
  possible only after this fix lands; commands in §8.
- **camera_compat (June 2026, 15.1-era) is NOT in the flashed build**: no
  `libm6_camera_{glconsumer_compat,tsf_bypass}.so` on device, no LD_PRELOAD in the
  installed `mediaserver.rc`, none in the out tree, and
  `frameworks/av/media/mediaserver/mediaserver.rc` in the 16.0 tree has no LD_PRELOAD
  either — the device.mk comment describes 15.1 intent that never landed here.
  Deliberately NOT added now (written for another Android version; add on evidence).
- `libmtkshim_gui.so` present on device (both ABIs), `TARGET_LD_SHIM_LIBS` already
  wires it to `libcam_utils/libcam.client/libcam.camnode/libeffecthal.base` (32-bit;
  `BoardConfig.mk:69-72`).
- Unrelated crash storm: `com.android.bluetooth` respawns every ~29 s (BT HAL wait) —
  M6 did not get the m681-style BT-HAL `disabled` treatment; load average 11 at idle.
  Not camera; flagged to the lead.
- `libSonyIMX230PdafLibrary.so` exists in proprietary but not in the image; IMX278
  has PDAF — if AF misbehaves later, this is a candidate (HYPOTHESIS, untested).

## 6. Hypotheses for what happens after the stub (contract §3.5)

The stub removes the proven blocker. What follows is genuinely uncertain:

- **H-A (most likely): open proceeds and the lane lands where 15.1 left it** — front
  camera streams, REAR gives black frames (project memory `m6-camera-rootcause`:
  software, stock works). Falsifier: rear preview shows an image ⇒ the 15.1-era
  defect was Oreo-stack-specific and is gone.
- **H-B: open crashes further down the blob chain** (e.g. the 15.1-era libcamalgo
  TSF/LSC NULL-deref that `libm6_camera_tsf_bypass` was written for, or a GLConsumer
  ABI-semantic break the June glconsumer_compat targeted). Falsifier: mediaserver
  tombstone naming libcamalgo/GLConsumer ⇒ port the corresponding camera_compat
  piece to 16.0 as the next minimal change.
- **H-C: rear black reproduces and is kernel-side** — the surviving halves of the
  old H1/H2: (a) clock ENABLE/rate divergence visible only with the camera open
  (test: clk_summary during preview vs stock-open capture — stock-open table was
  never captured, only idle; capture LOS-open first, compare rates against idle and
  the 109.2 MHz scam expectation), (b) DTB/pinmux CSI lane config from the
  Honor-6C-Pro donor (test: diff `kd_camera_hw1` pinctrl + seninf DTS nodes between
  stock DTB (`stock-flyme-7.1.2.0G/boot` unpack) and the flashed
  `out-rot0` DTB; look at `mipi_rx` lane swap/PN swap registers via
  `/sys` seninf dumps during preview).
- **H-D: SELinux/perms block a node open.** Weakest: build runs permissive
  (`androidboot.selinux=permissive` on stock; LOS build permissive per state doc) and
  the -38 arrives before any node open. Falsifier: avc denials in logcat during open.

## 7. Install package

`/home/gun/m6cam-out/` on west (gunwest, 192.168.2.231): see `MANIFEST.txt` there.
Contents: `libskia.so` for `/system/vendor/lib/` (32-bit, md5
`bb36bb66164d1e0fe85587f35bbaee7f`, 15 856 B) and `/system/vendor/lib64/` (64-bit,
md5 `4d1afe820e186a20059762731c1a46ab`, 67 992 B) — the built `libskia_m6_stub.so`
under its install name (`mka libskia_m6_stub` in the android-8.1 container, RC=0,
both ABIs + out-tree symlinks installed). Install from TWRP only (project rule), `chmod 0644`,
`chown root:root`, `chcon u:object_r:system_file:s0`. No boot.img change, no kernel
change. A reboot is inherent to the TWRP route; strictly, restarting mediaserver
would suffice.

## 8. Verification for the lead (after install)

1. Boot; `getprop init.svc.vendor.camera-provider-2-4` = running (unchanged).
2. `logcat -d | grep -E "getPlatform|No Platform|libskia"` — the dlopen error must be
   GONE. That alone proves the fix (mediaserver retries shim-metadata by itself).
3. `dumpsys media.camera` still shows 2 devices; then open something:
   `am start -a android.media.action.STILL_IMAGE_CAMERA` (grant camera permission on
   screen first, or via `pm grant com.android.camera2 android.permission.CAMERA` +
   `...RECORD_AUDIO`, and unlock the keyguard — app currently parks in
   PermissionsActivity), `sleep 8`, `screencap -p /data/local/tmp/cam_after.png`.
4. While preview is (or should be) up:
   `cat /sys/kernel/debug/clk/clk_summary | grep -iE "scam|camtg|img_|mm_disp0_cam"`
   → this is the H-C(a) measurement; compare against §5 idle table.
5. `logcat -d | grep -E "MtkCam|CameraClient|tombstone"` for the H-B discriminators.

## 9. Blocker / next marker / rollback

- **Blocker (was):** missing `libskia.so` ⇒ camera HAL chain unloadable ⇒ -38 on
  every open. Root-caused to FACT level (§2-§3).
- **Next marker:** `getPlatform` dlopen error absent from logcat after install; then
  first real open outcome decides H-A/H-B/H-C.
- **Rollback:** delete `/system/vendor/lib{,64}/libskia.so` (returns to the exact
  current state); tree-side, revert the three §4 changes.

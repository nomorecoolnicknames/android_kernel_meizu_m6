# Meizu M6 — LineageOS 16.0 (Android 9 Pie) bring-up STATE

Kernel lane: **3.18.140** (`kernel-meizu_M6-N-ex6-linux-3.18.140`), MT6750 (mt675x).
Goal (user, 2026-07-24): download LineageOS **16.0** sources on gunwest (west) and
build **that version** for the M6. This continues the M6 work; 15.1 is already green.

## Where it's being built
- Host: **gunwest** (`west`, user `gun`, via `~/.claude/skills/gunwest/scripts/gw.sh`).
  8 cores, 30 GB RAM, 99 GB swap, ~495 GB free NVMe.
- Tree: **`/home/gun/m6rom16/rom`** (16.0). The 15.1 tree stays at `/home/gun/m6rom/rom`.
- `repo` launcher copied from the 15.1 tree to `~/bin/repo` (v2.54; storage.googleapis.com
  is BLOCKED from west, gerrit/github/googlesource are 200).

## FACTs established 2026-07-24
- West reaches `android.googlesource.com` (200) and `github.com` (200) directly →
  no HTTPS-blocked workaround needed for sync (that block is only on the orchestration host).
- `repo init -u https://github.com/LineageOS/android -b lineage-16.0` → manifest base
  **`android-9.0.0_r46`** (confirms LOS 16.0 = Android 9 Pie).
- **Jack is GONE in Android 9** (d8/r8 dexer; in-tree `prebuilts/jdk/jdk9`). The entire
  Jack-SSL / Temurin-8u482 workaround from the 15.1 build is **not needed** for 16.0.
  No host JDK is required for the Pie ROM build (all in-tree).
- Only docker image on west is `androidforge/build-env:android-8.1`. Its base host deps
  (python2.7, build-essential, git, zip, etc.) are sufficient for a Pie build since the
  toolchain (soong/clang/jdk9) is in-tree — plan to reuse it; add python2 if missing.
- No pre-existing LOS 16.0 / Pie device tree exists for our MT6750 M6.
  (`thanhdatpd/..._bicot`, `KopsourcesORG/..._m1721` are the M6 **Note** = Qualcomm
  MSM8953 — a DIFFERENT device. Not reusable.) → port from our own 15.1 tree.

## HYPOTHESES (each with a disconfirming test)
1. **HYPOTHESIS: kernel 3.18.140 is adequate for a non-Treble Pie build.** Many
   MT6750/MT6797 devices shipped unofficial LOS 16.0 on 3.18 kernels; the "Pie needs
   4.4.107+" rule is a Treble/VTS cert requirement, not a hard build/boot gate for a
   legacy (non-Treble) target.
   - *Disconfirm:* build fails on a kernel-version assert, or boots to a bootloop
     traceable to a missing Pie-required kernel feature (sdcardfs, CONFIG_* ) with no
     backport available.
2. **HYPOTHESIS: our 15.1 device tree ports to 16.0 with bounded edits** (product
   version bump, sepolicy Pie neverallow fixes, VINTF manifest HAL additions, mk path
   updates), like the 15.1 west build took ~6 sequential blockers.
   - *Disconfirm:* an unbounded rewrite is needed (e.g. Treble/VNDK split forced).
3. **HYPOTHESIS: reusing the android-8.1 docker image builds Pie fine.**
   - *Disconfirm:* a host tool the image lacks (e.g. a newer python2 module, `ninja`
     host bins) breaks soong bootstrap → build a small `android-9` image.

## Plan / progress
- [x] Recon west (internet, tooling, disk, docker).
- [x] Install `repo`, `repo init -b lineage-16.0`, write local_manifest
      (`vendor/mediatek@pie` from lenovo-k4note, public).
- [~] **`repo sync` running** (PID 2296382, log `/home/gun/m6rom16/sync.log`,
      `-j12 -c --no-clone-bundle --no-tags --force-sync`). Long pole ~1–2 h.
- [ ] Copy m6 `device/`,`vendor/`,`kernel/` from the 15.1 tree into the 16.0 tree
      (private canonical repos + west-only auth → copy as plain dirs, port in place).
- [ ] Port device tree 15.1→16.0 (product mk 15.1→16.0, BoardConfig kernel prebuilt,
      sepolicy, manifest.xml VINTF, init rc, overlays).
- [ ] `brunch lineage_meizu_m6` / `mka bacon` in docker; iterate blockers.
- [ ] Verify zip (`zip -T`), record sha256, doc + commit.

## Device tree facts (from 15.1, `/home/gun/m6rom/rom/device/meizu/meizu_m6`)
- Product entry `AndroidProducts.mk` → `lineage.mk` → `lineage_meizu_m6.mk`
  (inherits `device_meizu_m6.mk` + `vendor/lineage/config/common_full_phone.mk`).
- Version knobs to flip: `PRODUCT_VERSION_MAJOR 15→16`, `MINOR 1→0`,
  `LINEAGE_VERSION 15.1-…→16.0-…`. `PRODUCT_NAME=lineage_meizu_m6`, device `meizu_m6`.
- Inherits shared commons `device/meizu/m3_meizu_m6-common` + `meizu_mt675x-common`
  (also used by m681/M6T — never delete their per-device files; add alongside).
- Heavy surfaces to re-check for Pie: `camera_compat/`, `manifest.xml` (VINTF),
  fingerprint HAL (`init.fingerprint.rc`, `microtrust.rc`), `rild-mtk-hidl.rc`,
  `overlay/`, `prebuilt-kernel/Image.gz-dtb`.

## 2026-07-24 — sync DONE, port started, build blocker log
- **Sync complete (FACT):** 405 projects + `vendor/mediatek@pie` (1ab7db9, same rev as 15.1)
  + 4 chromium-webview LFS projects (git-lfs installed on west; `repo sync` left 133-byte
  LFS pointers — needed explicit `git lfs pull` per project; apks now 90–329 MB). Tree 52 GB.
- **Port:** m6 dirs copied from 15.1 tree (`device/meizu/{meizu_m6,m3_meizu_m6-common,
  meizu_mt675x-common}`, `vendor/meizu/meizu_m6` 492M). Version bumped in the
  AUTHORITATIVE `lineage.mk` (NOT `lineage_meizu_m6.mk` — that file is a stale 172-line
  twin; lineage.mk 175-line has the newer sensor-xml/zygote64/rild fixes).
- **Build wrappers:** `/home/gun/m6rom16/rom/build-m6-16.sh` +
  `/home/gun/docker-compose.m6build16.yml` (same android-8.1 image; NO Jack/Temurin,
  in-tree jdk9; out=/home/gun/m6-out16, ccache16).

### Blocker log (sequential, each FACT with fix)
1. **`lunch`: "Can not locate config makefile for product lineage_meizu_m6"** —
   Pie derives the product NAME from the *basename* of a path-only PRODUCT_MAKEFILES
   entry (`build/make/core/product_config.mk` ~195); our AndroidProducts.mk pointed to
   `lineage.mk` → product named "lineage". FIX: explicit
   `lineage_meizu_m6:$(LOCAL_DIR)/lineage.mk` name:path form. → lunch OK,
   `LINEAGE_VERSION=16.0-20260724-UNOFFICIAL-meizu_m6`, PLATFORM_VERSION=9.
2. **soong: `android.hardware.bluetooth@1.0-service.mtk` depends on undefined module
   "libnvram"** — in 15.1 the soong module `libnvram` came from
   `vendor/meizu/m2note/Android.bp` (Lenovo tree we did NOT copy). Android.mk prebuilts
   are invisible to soong. FIX: new `vendor/meizu/meizu_m6/Android.bp` with
   `cc_prebuilt_library_shared { name: "libnvram", proprietary, both multilib }`;
   removed the 2 libnvram.so copy lines from meizu_m6-vendor-blobs.mk (dup-install).
   NOTE: `check_elf_files` property does NOT exist in Pie soong (Q-ism) — rejected.
3. **kati: `flyme-res: Must specify LOCAL_SDK_VERSION or LOCAL_PRIVATE_PLATFORM_APIS`**
   (Pie sdk_check.mk) — FIX: `LOCAL_PRIVATE_PLATFORM_APIS := true` in
   `m3_meizu_m6-common/flyme/res/Android.mk`. Scanned all device/vendor meizu Android.mk:
   no other java/apk module lacks the flag.
4. **kati: `OUT is obsolete. Use OUT_DIR instead`** in `m3_meizu_m6-common/Android.mk:7`
   (`$(shell mkdir -p $(OUT)/obj/KERNEL_OBJ/usr)`) — Pie kati bans `$(OUT)`.
   FIX: `$(PRODUCT_OUT)` (same path). Only occurrence in our trees (grep-verified).
5. **kati: `libwifi-hal missing libwifi-hal-mt66xx`** — first hit BEFORE the
   vendor/mediatek local-work port (below); the guard fix arrives with that patch.
- **vendor/mediatek local meizu work PORTED (FACT):** the 15.1 tree carried 41 modified
  files (+2050/−322: Android.mk device guards, hidl audio/bt/light/sensor/thermal fixes,
  symbols/{camera,gui,ui}.cpp shims, combo_loader, wlan wifi_hal) as UNCOMMITTED changes
  on the same `1ab7db9@pie` base both trees use. Exported `git diff` →
  `/home/gun/vendor-mediatek-m6-localwork.patch` (3361 lines), `git apply` into the 16.0
  checkout = clean. Plus 7 untracked files copied (symbols/{binder,icu,sensor}.cpp,
  hidl/audio/include/{VersionUtils.h,common/}, 2 .forge-disabled bp).
  NB: 15.1 build had ALWAYS used the *pie* branch of lenovo-k4note vendor/mediatek —
  so these edits apply to the Pie tree without rebase.
- **MILESTONE 2026-07-24 ~10:20 UTC: config phase CLEAN.** After the vendor/mediatek
  port, kati+soong parse the whole product with no errors; ninja is compiling
  (93 521 targets, cold ccache, 8 cores — expect hours). Next risk class:
  C++ compile errors in MTK shims vs Pie headers, then sepolicy neverallows,
  then image packaging.

## 2026-07-24 — family universality: the 16.0 tree now hosts m6 + m681 + M6T
User directive: the new tree must be universal for the mt6750/mt6755 family.
All changes ADD-only (running m6 ninja untouched). All three lunch targets verified
on Pie via throwaway-OUT_DIR dumpvars (`docker compose run --name m6-lunchcheck*`):
- `lineage_meizu_m6` → 16.0-…-meizu_m6 ✅  `lineage_m681` → 16.0-…-m681 ✅
- `lineage_M6T` → 16.0-…-M6T ✅ (PLATFORM_VERSION=9 all three)

Done:
- **m681:** device (1.2M) + `mt6755-common` (56K) + vendor (430M) copied from west 15.1
  tree. FIXES: (a) default prebuilt-kernel lane pointed at an ABSOLUTE local-host path
  (`/home/n8n/mt6755-49/b-4.4/out-local/...Image.gz-dtb` — the 4.4-kernel effort
  artifact); copied that artifact in-tree → `device/meizu/m681/prebuilt-kernel/`
  (sha256 e8128b40…) and repointed BoardConfig (same class of bug as the old m6
  absolute-path lesson). (b) `LINEAGE_BUILD := m681` was never set (m6/M6T set it
  explicitly; Pie vendor/lineage does not default it) → version suffix was empty.
- **M6T:** device (62M incl. prebuilt kernel) + vendor (320M) did NOT exist on west —
  transferred from the LOCAL 15.1 tree (203M tar over LAN). FIXES: AndroidProducts.mk
  had the same Pie basename bug as m6 → explicit `lineage_M6T:$(LOCAL_DIR)/lineage.mk`;
  version bumped 15.1→16.0 in its lineage.mk. (M6T bring-up itself stays with the
  other agent; this tree just makes it buildable.)
- **Scans clean:** no `$(OUT)` refs, no java modules missing sdk flags, no Android.bp
  (soong-name) collisions in the new dirs.

Known caveats (documented, not blockers):
- Source-kernel lanes keep absolute `/srv/forge/...` paths (m681 SFOS lane 3.18-graft,
  M6T + mt6755-common `TARGET_KERNEL_CROSS_COMPILE_PREFIX`); default lanes are
  prebuilt and fully in-tree. Fix when a source-kernel build on west is actually wanted.
- soong module `libnvram` (vendor/meizu/meizu_m6/Android.bp) serves the whole family;
  m6 vs m681 blob hashes differ (a8cf02fb… vs e210e2db…) → an m681/M6T image gets the
  m6 copy IF the MTK BT service pulls it in. m681's own blobs.mk ships only
  libnvram_platform/sec/agentclient (no libnvram.so) so there is no install collision.
  Revisit per-device if BT NVRAM misbehaves on m681/M6T.
- m2note (MT6753) + lenovo donor dirs intentionally NOT ported (outside the
  mt6750/mt6755 scope the user named).
6. **sepolicy: `unknown type mtk_m4u_proc`** (ninja 29%, 24 min in) — FACT: the 15.1
   LOS base policy declared MTK compat types (`system/sepolicy/public/file.te`:
   mtk_m4u_proc, mtk_ged_proc + `private/genfs_contexts` /m4u,/ged) and 16.0 dropped
   them; our device .te files (m6 lane hal_camera/mediaserver/surfaceflinger, m681
   genfs comment even documents the reliance) referenced them. FIX device-side (no
   platform patch): re-declared `type mtk_{m4u,ged}_proc, fs_type;` + genfscon lines
   in all three lanes (m6: mt675x-common/sepolicy/meizu_m6/{device.te,genfs_contexts};
   m681: sepolicy/{file.te,genfs_contexts}; M6T: mt675x-common/sepolicy/M6T/...).
   Bonus: M6T's mt675x-common bits (configs/M6T.mk, sepolicy/M6T/, init rc) were
   missing on west entirely — transferred from the local 15.1 tree.
7. **sepolicy: Pie neverallow `dac_override`** for mtk_wmt_loader / mtk_gsm0710muxd /
   mtk_aee_core_forwarder (public/domain.te:1385) — Oreo-era vendor rules banned in
   Pie. FIX (bring-up): `SELINUX_IGNORE_NEVERALLOWS := true` in
   m3_meizu_m6-common + mt6755-common BoardConfigCommon (covers m6/M6T + m681).
   JUSTIFIED: family boots `androidboot.selinux=permissive` (FACT, BoardConfigCommon
   cmdline) so assertions have no runtime delta; flag auto-errors on user builds.
   REVISIT before any enforcing build. → `mka selinux_policy` GREEN (48 s).
   Iteration trick: `mka selinux_policy` alone = 1-2 min/round instead of 25 min.

## 2026-07-24 — family really-buildable pass (m681 + M6T через parse+sepolicy)
User: «адаптируй чтобы было реально собрать остальные устройства». Honest scope:
parse+sepolicy green = дерево консистентно (модули/файлы/типы находятся); полный
компилятор-прогон каждого устройства — отдельный этап после зелёного m6.
Method: `mka nothing` + `mka selinux_policy` per device в ОТДЕЛЬНЫХ OUT_DIR
(`out-check-m681`, `out-check-m6t`) параллельно бегущему m6-bacon.

- **m681:** blocker — Pie: `MTD ... BOARD_NAND_PAGE_SIZE is deprecated` (error, not
  warning) → закомментированы BOARD_NAND_{PAGE,SPARE}_SIZE в
  mt6755-common/BoardConfigCommon.mk (девайс eMMC; строки — карго-культ старых MTK).
  → parse GREEN (1:18), selinux_policy GREEN (57 s).
- **M6T:** blocker — те же missing `lib_driver_cmd_mt66xx`/`libwifi-hal-mt66xx`, что
  ловил M6T-хэндофф на 15.1: device-guard'ы не знали M6T (эти правки жили только в
  ЛОКАЛЬНОМ дереве, west их не видел). FIX: добавлен M6T в guard'ы
  `m3_meizu_m6-common/Android.mk` (wpa_supplicant static lib) и
  `vendor/mediatek/Android.mk` (wifi_hal branch meizu_m6 m681 → + M6T).
  → parse GREEN (1:19), selinux_policy GREEN (56 s).
- Оставшийся риск для полных сборок m681/M6T: C++ device-специфичных модулей
  (напр. M6T camera_compat) и packaging — проявится только в полном bacon,
  который пойдёт последовательно после зелёного m6.
8. **ninja 57% (38 min): `generated_kernel_includes` FAILED** — LOS 16.0's new
   vendor/lineage soong genrule runs `make -C $(TARGET_KERNEL_SOURCE) headers_install`;
   our prebuilt lanes used the fake `/dev/null/forge-prebuilt-kernel` → `make -C ''`.
   FIX (uniform, keeps the PINNED prebuilt kernels — no image change):
   (a) transferred the real m6 kernel source (kernel-meizu_M6-N-ex6/kernel-3.18,
   3.18.140, 148M tar) to west → `kernel/meizu/meizu_m6/kernel-3.18`;
   (b) 5-line patch to vendor/lineage/build/tasks/kernel.mk: when kernel source
   EXISTS but TARGET_KERNEL_CONFIG is empty, honor TARGET_PREBUILT_KERNEL
   (upstream leaves KERNEL_BIN empty there → broken kernel copy rule);
   (c) pointed the default (prebuilt) lanes of ALL THREE devices'
   TARGET_KERNEL_SOURCE at the shared 3.18 source (headers-only role; m681's 4.4 /
   M6T's stock prebuilt images unchanged; uapi family-shared — noted impurity for
   m681 4.4 headers, acceptable for bring-up). SFOS/source override lanes untouched.
   The kernel.mk patch must go into the forge-build patches overlay for
   reproducibility (like the 15.1 AOSP patches).
   **8-CORRECTION (true root cause, FACT):** pointing the lanes' TARGET_KERNEL_SOURCE
   at the real source was NOT enough — soong.variables still exported "" because
   `vendor/lineage/config/BoardConfigKernel.mk:44` FORCE-CLEARS TARGET_KERNEL_SOURCE
   whenever TARGET_PREBUILT_KERNEL is set (upstream Lineage16 behaviour; upstream
   prebuilt-kernel devices simply never build header consumers). Second patch:
   clear only when `$(wildcard $(TARGET_KERNEL_SOURCE)/Makefile)` is empty.
   Both lineage patches (kernel.mk KERNEL_BIN honor + BoardConfigKernel wildcard
   guard) → forge-build patches overlay for reproducibility.

## ✅ 2026-07-24 13:51 UTC — GREEN BUILD: LineageOS 16.0 for meizu_m6
**`lineage-16.0-20260724-UNOFFICIAL-meizu_m6.zip`** — 656 458 607 B,
sha256 `62563129f82c4ee4faf56965089a9edcd9f1d3558976eca5e460c33c9ae01471`,
unzip -t OK. Full bacon 1:47:36 on west (8 cores, docker android-8.1 image,
in-tree jdk9, no Jack). Location: `/home/gun/m6-out16/target/product/meizu_m6/`.
- boot.img 9 166 848 B (kernel 7 361 828 + ramdisk 1 801 102, page 2048,
  kaddr 0x40080000 = base+0x8000, ramdisk 0x45000000 — geometry == 15.1 lane).
  BOARD limit 16 MB → build check passed. ⚠ Memory notes a "9 025 536-byte
  boot-partition fit limit" from the 15.1 repack era — VERIFY against scatter
  before flashing (flash is human-confirmed anyway). 15.1 boot was 8 640 512 B.
- **Kernel identity verified (FACT):** kernel inside boot.img sha256
  `57a1334396cea1b75106ba088bb8595b74b0ac0bc2a672616df3e063c43b1cb8` ==
  `prebuilt-kernel/Image.gz-dtb` == the pinned #209 artifact from BoardConfig.
  The kernel.mk/BoardConfigKernel patches preserved the pin exactly as designed.
- Blockers fixed this lane: 8 total (see log above). NOT yet: flashed/booted,
  uploaded, pushed to canonical repos.
- m681 full bacon launched next (OUT_DIR=/src/out-m681, shared ccache); M6T after.
- **Uploaded to gdrive (FACT, 2026-07-24):** `gdrive:ReMeizu/M6-LOS16/` —
  `lineage-16.0-20260724-UNOFFICIAL-meizu_m6.zip` (656 458 607 B, remote sha256
  `62563129…ae01471` == local == build) + `boot-16.0-20260724-meizu_m6.img`
  (9 166 848 B, sha256 `bebfc1cd…d35c47`). Local staging kept at
  `/srv/forge/m6-out-release/`. Verified via rclone hashsum sha256.

## ✅ 2026-07-24 16:42 UTC — GREEN BUILD #2: LineageOS 16.0 for m681
**`lineage-16.0-20260724-UNOFFICIAL-m681.zip`** — 732 778 753 B, sha256
`4ef6be1a73fdafc178f641d2181d0df32185e8269fafb5b2d4bfb0b05a2e486c`, unzip -t OK.
Full bacon 2:49:42 (shared ccache with m6; out=/src/out-m681). ZERO extra
blockers — the family prep (NAND vars, LINEAGE_BUILD, in-tree kernel prebuilt,
sepolicy types, kernel-source-for-headers) was sufficient.
- boot.img 9 234 432 B; **kernel identity verified (FACT):** kernel-in-boot ==
  pinned m681 4.4 prebuilt `e8128b40…21bbe78` (in-tree copy of the local 4.4 artifact).
- Location: `/home/gun/m6rom16/rom/out-m681/target/product/m681/`.
- NOT yet: flashed/booted (m681 bench device is on container 228), uploaded, pushed.
- M6T full bacon launched next (out=/src/out-m6t).

## ✅✅✅ 2026-07-24 20:08 UTC — GREEN BUILD #3: M6T → ВСЯ СЕМЬЯ mt6750/mt6755 НА LOS 16.0
**`lineage-16.0-20260724-UNOFFICIAL-M6T.zip`** — 653 409 054 B, sha256
`0f332e7c97c68f65f709f39a310ec867a47f79ae27d6de3c405c9a3dc7c9e481`, unzip -t OK.
Full bacon 2:44:57 (out=/src/out-m6t). ZERO extra blockers (как и m681).
- boot.img 9 836 544 B; **kernel identity (FACT):** kernel-in-boot == M6T STOCK
  prebuilt `e45de551…a474e60c` (ровно sha256 из M6T_DUMP_ANALYSIS/HANDOFF).

### Family scoreboard (one tree, `/home/gun/m6rom16/rom`)
| device | zip | sha256 | kernel pin | build time |
|---|---|---|---|---|
| meizu_m6 | lineage-16.0-20260724-UNOFFICIAL-meizu_m6.zip 656 458 607 B | 62563129… | #209 3.18.140 `57a13343…` ✅ | 1:47:36 |
| m681 | …-m681.zip 732 778 753 B | 4ef6be1a… | 4.4 artifact `e8128b40…` ✅ | 2:49:42 |
| M6T | …-M6T.zip 653 409 054 B | 0f332e7c… | stock `e45de551…` ✅ | 2:44:57 |

m6 uploaded to gdrive:ReMeizu/M6-LOS16/. m681/M6T zips NOT uploaded yet.
Nothing flashed yet (human-confirmed gate). Sources/patches NOT pushed yet.

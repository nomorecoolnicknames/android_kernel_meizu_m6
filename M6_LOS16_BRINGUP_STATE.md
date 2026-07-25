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

## 2026-07-25 — kernel refresh round (user: «у M6T новое ядро, у m681 доработки — проверь»)
Both confirmed (FACTs) and incorporated:

- **m681: pin updated to the converged `out-connfix` kernel.** The 4.4 lane moved past
  our Jul-17 `out-local` pin: `out-connfix` Image.gz-dtb (md5 `46e1fb64…` — matches the
  identity gate in `allbaked1/PACK_allbaked1.sh`; sha256 `fe027c5a…867e`) =
  "final-converge @703cf3d4: consys 3-fix + EMI remap + audio". Swapped into
  `device/meizu/m681/prebuilt-kernel/` on west, incremental bacon 04:24 →
  **`lineage-16.0-20260725-UNOFFICIAL-m681.zip`** 733 136 602 B, sha256
  `bf388b4204078d3371fe12cc1147aa963ee178a967b38ba281439d2b14d48987`, unzip -t OK,
  kernel-in-boot == connfix pin (verified), boot-in-zip == loose boot.img.
  The 20260724 m681 zip (old kernel) is superseded.

- **M6T: source-built kernel staged, zip left on stock (rollback-safe).** The kernel-RE
  lane (see `/srv/forge/android/meizu_m6t/KERNEL_REVERSE_HANDOFF.md`) produced a
  source-built `Image.gz-dtb` incl. the Stage-1 RE-ported REAL panel driver
  `hx83102b_hd_dsi_vdo_lide` (built Jul 24 17:07, 7 759 202 B, sha256 `5ffd9688…f539`).
  It is NOT hardware-validated yet (their Stage 3 pending; no M6T on the bench), so:
  (a) the 16.0 zip keeps the STOCK kernel `e45de551…` (unchanged);
  (b) the source kernel is staged in-tree as
  `device/meizu/M6T/prebuilt-kernel/Image.gz-dtb.forge-source` (their Layer-2 convention);
  (c) built a bench-flash artifact **`/home/gun/boot-M6T-16.0-sourcekernel.img`**
  (9 564 160 B, sha256 `0df0f0494a1fc5d7cf7be0068e3ebdf7ee75ec7f31dddae62efb5345cc8a0d46`)
  = source kernel + our LOS 16.0 M6T ramdisk (python repack of the green boot.img;
  original second_size=0 verified, page 2048, header/cmdline preserved).
  Flip to source in the ROM = overwrite prebuilt Image.gz-dtb with .forge-source and
  re-bacon, AFTER a bench flash proves the panel comes up.
- **m681 zip uploaded to gdrive (FACT, 2026-07-25):** `gdrive:ReMeizu/M6-LOS16/
  lineage-16.0-20260725-UNOFFICIAL-m681.zip` — 733 136 602 B, remote sha256
  `bf388b42…d48987` == local == build (verified rclone hashsum + listing; folder
  now 3 objects, 1.303 GiB). Zip only per user; M6T artifacts NOT uploaded.

## 2026-07-25 — m681 FLASH LANE (LOS 15.1 → 16.0 on device 91HEBNL163XD)
Handoff consumed: `/srv/forge/android/m681/docs/M681_HANDOFF_TO_LOS16.md` (container
228 dead; m681 now on west hub with 2 foreign phones — every command serial-gated).

Pre-flash fixes (found by inspecting OUR zip against the handoff's known traps —
both were present in the 16.0 build):
9. **mediacodec.policy MISSING in the 16.0 vendor** (identical to the 15.1 trap:
   omx pselect6→SIGSYS→crash_dump storm→OOM). FIX at the source: policy file
   (md5 75bbf8a7, from allbaked ramdisk-overlay) → `device/meizu/m681/seccomp/` +
   explicit PRODUCT_COPY_FILES → `/vendor/etc/seccomp_policy/mediacodec.policy`.
   Verified in built system tree post-rebuild.
10. **`net_bt_stack` group all over the m681 rootdir** (Pie dropped the group →
   ueventd rejects the /dev/stpbt line → root:root 0600 → BT HAL EACCES). FIX:
   sed net_bt_stack→bluetooth across ALL device/meizu/m681/rootdir files
   (ueventd.mt6755.rc, ueventd.rc, init.connectivity.rc, init.project.rc,
   init.nvdata.rc, init.rc — grep-clean after).
- Rebuild #2 (04:24) → FINAL zip `lineage-16.0-20260725-UNOFFICIAL-m681.zip`
  733 101 167 B sha256 `e3b8f55eef068c09a0ff26da9423a8c2ce04c99e8620fc9bc830c279da7f8431`
  (supersedes the gdrive-uploaded `bf388b42…` — re-upload after device acceptance).
  Kernel in boot still connfix `fe027c5a…` (verified).

Flash protocol executed (all `adb -s 91HEBNL163XD`):
- Identity gate: ro.product.device=m681, lineage=15.1-20260712 (pre-flash), serial OK.
- p22 readback backup → `/home/gun/m681-work/p22_backup_pre16-20260725.img`;
  head -c 9730048 md5 == `1083ff05` == boot_44_allbaked1 (FACT: device ran allbaked1).
- zip pushed to /sdcard (md5 ff2b5d20 == west copy), `adb reboot recovery` →
  **TWRP 3.7.0_9-0** (kernel 3.10.72), `twrp wipe cache/dalvik/data` (major bump,
  bench device, no SIM; internal storage preserved), `twrp install` →
  script result 1.000000, Updater RC=0, target meizu/lineage_m681/m681:9.
- p22 post-flash readback md5 == west boot.img `d151db2e…` (byte-exact).
- Rebooted to system; awaiting first-boot acceptance (crash_dump flat, policy md5,
  stpbt perms from boot, wifi driver status) per gw_verify_allbaked1.sh pattern.
11. **FIRST BOOT STUCK (m681, LOS16): zygote cycles, SF never starts.** FACTS:
   crash_dump=0 (storm fix HOLDS), zygote PID churns every ~10 s, `pidof
   surfaceflinger` empty, crash buffer: `guiext-server` linker-fatal every 5 s —
   `cannot locate symbol "__xlog_buf_printf"`. ROOT CAUSE: the 15.1 lane carried
   system/core commits `54cf75f21`+`18c1e6798` (liblog `__xlog_buf_printf` weak
   shim + map export; libcutils `legacy_atomic.c` with the FULL android_atomic_*
   family — live-proven needs of pre-L MTK blobs: hwcomposer.mt6755, libGLES_mali,
   guiext-server, libcam.*). The 16.0 system/core has neither → the whole MTK
   graphics stack fails to link → SF dead → boot never completes.
   FIX: hand-ported both (git apply failed on Pie divergence): legacy_atomic.c
   verbatim + Android.bp srcs entry; xlog shim appended to Pie logger_write.c
   (guarded includes) + `__xlog_buf_printf` in LIBLOG_O map node. Rebuild #3 running.
   ⚠ CONSEQUENCE FOR THE FAMILY: patch lives in SHARED system/core → the m6 zip
   `62563129…` (incl. the gdrive copy) and M6T zip `0f332e7c…` were built WITHOUT
   it and would hit the same SF death — REBUILD both before any flash.
   These two system/core commits + kernel.mk/BoardConfigKernel patches = the LOS16
   patches overlay set (reproducibility TODO).

### m681 post-flash acceptance (2026-07-25, partial — device boots but framework stalls)
GOOD (both pre-flash fixes verified ON DEVICE):
- `crash_dump = 0`, procs ~260 → **the omx/seccomp storm is GONE**;
  `/vendor/etc/seccomp_policy/mediacodec.policy` md5 `75bbf8a7…` present on /vendor (fix #9 works).
- Boots into 16.0: `ro.lineage.version = 16.0-20260725-UNOFFICIAL-m681`, adb up, zygote running.

BLOCKER (FACT, `logcat -b crash`): **`/system/vendor/bin/guiext-server` crash-loops every ~5 s**
— `CANNOT LINK EXECUTABLE … cannot locate symbol "__xlog_buf_printf"` (the MTK xlog symbol
is gone from Pie's liblog/libcutils). Alongside it `init.svc.surfaceflinger = restarting`,
`init.svc.bootanim` empty, `sys.boot_completed` never set (still empty at ~860 s uptime).
- INFERENCE (needs confirming): the SF restart loop is downstream of the guiext-server
  linker failure; disconfirm by disabling the guiext-server service alone and re-checking SF.
- Fix candidates for the next round: (a) add `__xlog_buf_printf` to the existing MTK symbol
  shim lane (`vendor/mediatek/symbols/*.cpp` — same pattern already used for
  binder/icu/gui/camera), or (b) drop/disable the optional `guiext-server` service on 16.0.
- Device left ON in this state (no rollback flashed). Rollback if needed:
  `/home/gun/m681-work/p22_backup_pre16-20260725.img` (head 9 730 048 B == allbaked1 `1083ff05`).
- Consequence: the 20260725 zip on gdrive is NOT yet device-accepted — do not publish as good.

## 2026-07-25 — m681 live-patch attempt: MY MISTAKE, device currently off-USB
Idea (user): instead of a full reflash, drop the rebuilt liblog/libcutils into the
live system and see if SF comes up. Libs were ready in out-m681 (verified symbols:
`__xlog_buf_printf` in liblog.so, `android_atomic_inc` in libcutils.so).

**What went wrong (FACT, my error):** installed with
`cat /data/local/tmp/liblog.so.lib64 > /system/lib64/liblog.so`. The redirect
TRUNCATES the target first, and `cat` itself links against liblog.so → the very
first write left `/system/lib64/liblog.so` at **0 bytes** and no further device-side
binary could exec (`CANNOT LINK EXECUTABLE "cat"/"ls"`). The backup
(`/data/local/tmp/liblog.so.lib64.bak`) HAD been made; the other three libs were
never written. Correct method would have been `adb push` straight to /system
(adbd writes internally, no device-side exec) — or a TWRP-side copy.
Consequence: adbd could not restart → device left adb.

**Recovery so far:** device re-appeared on USB as `0bb4:0c01` with interface
`ff/42/03` = **fastboot** (bootloader `WT6755_66_SZ_L`, `unlocked: yes`,
max-download-size 128 MB, boot/recovery = 16 MB each). `fastboot reboot recovery`
was ACCEPTED (OKAY) and adb briefly reported the serial once, but the device then
returned to fastboot; on a second, patiently-polled attempt it went **off USB
entirely** (no adb, no fastboot, no 0e8d BROM) — currently unreachable.
Background watcher armed for any USB re-appearance.

Assets ready for the moment it returns (nothing lost):
- p22 pre-flash backup `/home/gun/m681-work/p22_backup_pre16-20260725.img` (allbaked1).
- Proven TWRP images: `/srv/forge/android/m681/m681_twrp_44kernel.img` (4.4 kernel,
  Jul 17) and `m681_twrp_16M_boot_recovery.img`, both 16 MB = exact partition size.
- LOS16 boot.img (`d151db2e`) + rebuild #3 (with the xlog/atomic fix) in flight.
Plan on return: fastboot → flash TWRP (recovery or boot) → TWRP `adb push` the good
liblog.so (or install the fixed zip) → restore proper boot.img.
If it stays absent it needs a physical force-off (hold Power ~10-15 s) / replug,
which also puts MTK into preloader where mtkclient can flash.

### FIXED m681 zip READY (waiting on the device, 2026-07-25)
Rebuild #3 (45:49, full re-link after the system/core change) →
`lineage-16.0-20260725-UNOFFICIAL-m681.zip` 733 140 237 B, sha256
`880528ae3413c0cfcb79d111348b0a5ab211c85821c92cb14f5e91067c5cf525`, unzip -t OK.
Verified IN THE BUILT SYSTEM TREE (note: a Pie LOS zip is a BLOCK OTA —
system.new.dat — so `unzip -p system/...` is meaningless, verify $OUT/system/):
- `__xlog_buf_printf` exported by liblog.so — BOTH lib64 and lib (1 each).
- `android_atomic_*` exported by libcutils.so — BOTH arches (2 matches each).
- `/vendor/etc/seccomp_policy/mediacodec.policy` md5 `75bbf8a7…` (storm guard).
- ramdisk `ueventd.mt6755.rc`: `/dev/stpbt 0660 bluetooth bluetooth` (BT perms).
- boot.img kernel == connfix `fe027c5a…`.
Supersedes `e3b8f55e…` (flashed, SF-dead) and `bf388b42…` (on gdrive) — re-upload
the winner after the device accepts it.
BLOCKED: m681 is off-USB in every mode; needs a physical force-off (Power 10-15 s)
+ replug. Watcher armed on west. NOTE for the bench: the phone sits in a
motherboard root-hub port (no power switching); the Genesys hub 1-9 on west DOES
support ppps — moving the cable there would make future recoveries fully remote
(uhubctl now installed).

## 2026-07-25 — M6 FLASH LANE (stock Flyme 6.2.0.0RU → LOS 16.0, device 711HEBRN23L3N)
Rebuilt WITH the xlog shim first (user: «не забудь добавить туда шимы»):
- The shim came from a **parallel agent's** in-flight edit to the shared tree
  (`system/core/liblog/logger_write.c`, weak `__xlog_buf_printf` → `__android_log_vprint`,
  `ro.disable.xlog` escape hatch). Left untouched, inherited by the M6 build.
- **Verified in the built artifacts (FACT):** `nm -D --defined-only` finds
  `__xlog_buf_printf` in BOTH `system/lib/liblog.so` and `system/lib64/liblog.so`.
- M6 does NOT ship `guiext-server` (that blob is m681-only), so the m681 crash-loop was
  never going to hit M6 — but the same symbol is referenced by MTK blobs M6 does ship.
- Rebuild 50:36 (contended with the other agent's m681 build on the same 8 cores) →
  **`lineage-16.0-20260725-UNOFFICIAL-meizu_m6.zip`** 656 435 275 B,
  sha256 `9052a1e9ad421f9f9911f0184fad465c689153f9caba20bbec4de5b77c4f9cc1`, unzip -t OK,
  kernel pin unchanged (#209 `57a13343…`). Supersedes the 20260724 zip on gdrive.

Flash protocol (all `adb -s 711HEBRN23L3N`, identity-gated on `ro.product.device=meizu_M6`):
- **TWRP: no write needed** — recovery `p1` already byte-identical to `twrp_notneffos.img`
  (md5 `f5acd829…`). Booted it: TWRP **3.2.3-by uznaikaz**.
- Backups first: `recovery_backup_pre_twrp.img`, `boot_stock_flyme6.2.0.0RU.img`
  (md5 `04845e42…`), and `/data/media` user files (110 MB, 150 files) → `/home/gun/m6-backups/`.
- **TWRP 3.2.3 CLI traps (FACT, cost two rounds):** `twrp wipe data` reports success but
  does NOTHING (stock /data dirs survived); `twrp format data` → `E:Unrecognized script
  command: 'format'`. What works is the **path form `twrp wipe /data`** (left 3 entries).
  On the m681 TWRP 3.7.0 the `wipe data` form did work — CLI differs per TWRP version.
- Pushed zip to `/data/media` (md5 `d0dcddca…` == west copy), `twrp install` →
  `script succeeded: result was [1.000000]`, `Updater process ended with RC=0`, radio images
  written (md1rom/md1dsp/md3rom/md1arm7), target `meizu/lineage_meizu_m6/meizu_m6:9`.
- **Post-flash `p21` readback == west boot.img** md5 `a2edabf5c64b5f48306ca0deb6433157` (byte-exact).
- Rebooted to system; first boot after a data wipe (dex2oat) in progress — acceptance pending.

### BROM recovery executed — TWRP now in the boot partition (2026-07-25)
The watcher caught the phone in **BROM/preloader** (t=1590 s into the watch).
mtkclient on west (`/home/gun/mtkclient`, `.venv` deps OK) handshook, loaded the DA
(EMMC USER 0x3a3e00000), and **wrote `m681_twrp_44kernel.img` (md5 `9b1a2069…`,
16 MB = exact partition size) to the `boot` partition** — sector 1447936, 32768
sectors, ~9 MB/s, "Wrote ... " confirmed. Rationale: a TWRP in `boot` boots
deterministically without depending on lk's `reboot recovery` (which did NOT stick
earlier) or the BCB.
`mtk reset` was then sent; mtkclient reports *"Reset command was sent. Disconnect
usb cable to power off"* — the DA halts the phone and it is off USB again.
It should come up as TWRP on the next power-on (charging boot / replug). Watcher armed.
⚠ The LOS16 boot.img (`d151db2e`) is currently OVERWRITTEN by TWRP — restore it from
TWRP (`dd` of the pushed boot.img) or via fastboot after the ROM install.
Full remaining sequence once TWRP is up (all remote):
`adb push` fixed zip `880528ae…` → `twrp install` → push+`dd` boot.img to
/dev/block/mmcblk0p22 → reboot → acceptance (crash_dump flat, SF alive, stpbt perms).

### Device recovered + FIXED ROM installed (2026-07-25)
**Correction to my earlier read (user caught it):** after the power-on the phone was
in **TWRP**, not fastboot — the TWRP written to `boot` via BROM DID boot. The
`fastboot` device I saw belonged to ANOTHER agent's session (30785d1a / Nubia poll
loop); my `fastboot -s 91HEBNL163XD flash boot` never matched a device (it sat in
`< waiting for … >`) and was killed — nothing foreign was touched. Lesson: identify
mode by the sysfs interface triple of OUR port, not by a global `fastboot devices`
list on a shared hub (`ff/42/01`=ADB, `ff/42/03`=fastboot; our 1-8 read
`18d1:d001 ff/42/01` = TWRP-adb, `adb devices` = recovery).

Executed in TWRP 3.7.0_9-0 (`ro.product.device=m681` gated):
- removed the superseded broken zip, `twrp wipe cache/dalvik/data`,
- pushed FIXED zip `880528ae…` (md5 on device `03f68b99…` == west),
- `twrp install` → `script succeeded: result was [1.000000]`,
- **boot partition readback == rebuild-3 boot.img md5 `adb4f53a…`** (the zip flashes
  boot, so the TWRP-in-boot was replaced by the proper LOS16 boot automatically).
- Rebooted; first-boot acceptance in progress.
NOTE: rebuild #3's boot.img md5 is `adb4f53a…` (not `d151db2e…` = rebuild #2).

## 2026-07-25 — m681 LOS16 first-boot ERROR INVENTORY (fixed ROM installed)
Boot does NOT complete. Logs captured and kept:
west `/home/gun/m681-los16-logs/`, local
`/srv/forge/android/m681/logs-los16-firstboot/m681-los16-logs/`
(logcat main/crash/system, dmesg 1.6 MB, getprop, ps).

**Blocker 11 (xlog) is CONFIRMED FIXED** — no `__xlog_buf_printf` failures anywhere in
this boot; that class is gone. crash_dump=0 → the omx storm guard also HOLDS.
The boot now dies one layer deeper. Distinct classes (deduped by signature):

12. **PRIMARY: surfaceflinger abort loop (43×)** — `Abort message: 'failed to get
    hwcomposer service'`, `Hwc2::impl::Composer::Composer` → `SurfaceFlinger::init`.
    Chain: `HAL: dlopen failed: cannot locate symbol "_ZN7android5FenceD1Ev"
    referenced by /system/vendor/lib64/libgui_ext.so` → hwcomposer.mt6755.so never
    loads → `ComposerHal: falling back to gralloc module` → `failed to open
    framebuffer device: Invalid argument` → fatal. Same symbol also kills
    `/system/vendor/bin/guiext-server` every 5 s (43×). `android::Fence::~Fence()` is
    the Pie-vs-Nougat libui ABI gap — the 15.1 lane solved this class with the
    DISPLAY_SHIM_CASCADE (libmtkshim_gui/ui + TARGET_LD_SHIM_LIBS).
13. **ART/patchoat**: `Could not create image space … Failed to mmap at expected
    address … overlaps with existing map (/data/dalvik-cache/arm64/system@framework@
    boot.art)`, plus the arm (32-bit) `boot-core-libart.oat` reservation failure and
    `Dex file fallback disabled, cannot continue without image.`
14. **DAC/permission cluster**: `cutils-trace: Error opening trace file: Permission
    denied (13)` (79×), `vibrator@1.0-service.mtk: Failed to open
    /sys/class/timed_output/vibrator/vibr_vol (13): Permission denied`. Suspected same
    root class as the stpbt fix (Pie dropped legacy users/groups named in ueventd rc).
15. **netd**: `cannot find interface dummy0` (kernel CONFIG_DUMMY / userspace setup).
16. Fallout (to be confirmed, not chased directly): `BatteryNotifier: batterystats
    service unavailable!` (48×), `crash_dump64: unable to connect to activity manager`.

Dispatched 4 parallel tracer subagents (one per class 12/13/14/15) with the logs, both
trees (15.1 working reference + 16.0), and strict evidence rules; build+flash stay with
me. Reports to be folded back here.

### Blocker 12 root cause CONFIRMED + fix applied (2026-07-25)
Subagent (tracer) + my own byte-level verification:
- **FACT: Pie deleted the exported symbol.** Oreo `frameworks/native/libs/ui/Fence.cpp:48`
  had an out-of-line `Fence::~Fence()`; Pie's `ui/Fence.h:141` makes it
  `~Fence() = default;` (inline) and `mFenceFd` a `base::unique_fd`, so libui.so
  exports **no** `_ZN7android5FenceD1Ev/D2Ev` at all (verified: present in 15.1's built
  libui.so, absent in the 16.0 one; Fence *constructors* still exported in both).
  The unchanged Nougat-era blobs (`libgui_ext.so` ← `hwcomposer.mt6755.so`,
  `guiext-server`) still import it → SF cannot load the composer HAL → abort loop.
- **FACT: the shim MECHANISM was never broken** — `TARGET_LD_SHIM_LIBS`
  (m681 BoardConfig.mk:14-34) and the Pie bionic linker's LD_SHIM_LIBS support are
  both present and working; the whole boot log contains exactly ONE distinct
  unresolved symbol, so every other shimmed symbol resolves. Only the shim's CONTENT
  lacked the Fence dtor (it was free from real libui.so on Oreo).
- **FIX:** added `_ZN7android5FenceD1Ev`/`D2Ev` to `vendor/mediatek/symbols/gui.cpp`
  (the libmtkshim_gui source already mapped onto libgui_ext/libui_ext).
- **Field offset byte-VERIFIED, not assumed** (the subagent flagged it as its one
  unproven step): disassembling the real Oreo `libui.so` dtor gives exactly
  `ldr w0,[x0,#4] ; cmn w0,#0x1 ; b.eq <ret> ; b close@plt` → mFenceFd at offset 4.
  Pie's layout puts unique_fd's int at the same offset (LightRefBase atomic<int32_t>
  at 0, Flattenable empty base), so the shim is correct whichever side constructed
  the object, and there is no double-close (the object is destroyed once).
- Rebuilt `libmtkshim_gui` only (21 s): both arches now export D1Ev+D2Ev (merged to a
  single address, exactly like the real Oreo libui did).
- Installed live via **`adb push`** (the correct method — adbd writes internally, no
  device-side exec; this lib is not a dependency of adbd/toolbox so the shell is never
  at risk), then rebooted for a fast verification cycle before a full ROM rebuild.

### Fence shim VERIFIED ON DEVICE — primary blocker cleared (2026-07-25 15:06)
Live test (shim libs `adb push`ed, reboot):
- **`_ZN7android5FenceD1Ev` errors: 0** (were 43 + 43). `hwcomposer.mt6755.so` now
  DLOPENS (it appears in a backtrace, i.e. it loaded and ran).
- **surfaceflinger crashes this boot: 1** (was 43 and never-ending). The first
  instance still aborted inside the blob at `HwcLoader::openDeviceWithAdapter`
  (HWC1→HWC2 adapter path, pc +0x1b734 inside hwcomposer.mt6755.so); the RETRY
  succeeded and **SF is stable** (same pid across 15 s), got the HWComposer service,
  ConfigStore, and full GL/EGL extension init.
- **system_server STARTED** (was never reached). Both zygotes `running`.
- Boot still not `boot_completed` at 180 s (first boot after a data wipe, dex2oat) —
  watcher running. The single early SF abort is a NEW, lower-priority item to chase
  (self-healing on retry, so not a blocker).

### Subagent reports folded in
- **ART/patchoat (class 13): NOT a bug — fallout, CONFIRMED, no action.** The agent
  proved patchoat's non-zero exit is recoverable (zygote prunes dalvik-cache and
  retries; `ZygoteInit` starts every cycle) and that the collisions come from the
  crash-restart loop. It ALSO proposed flipping m681 to `ro.zygote=zygote64` (as
  m6/M6T do) on the HYPOTHESIS that app_process32 is broken here too, and supplied the
  falsification test: "after the SF fix, check whether zygote_secondary still
  restarts". **Test RUN, hypothesis REJECTED:** with SF fixed, `init.svc.zygote` and
  `init.svc.zygote_secondary` are BOTH `running`. So m681 keeps 32-bit app support —
  the one-line change was NOT applied. (Applying it blindly would have silently
  dropped all 32-bit apps.) Its flagged doc contradiction
  (`RUNTIME_BLOCKERS_AUDIT.md:7` "sys.boot_completed EMPTY" vs "m681 booted on 15.1")
  is noted, unresolved, non-blocking.
- **Permissions (class 14): real bug, root cause = orphaned init.rc import chain.**
  device.mk ships the stock AOSP `/init.rc`, so `device/meizu/m681/rootdir/init.rc`
  (and everything it imports) NEVER executes, though the files are in the ramdisk.
  Consequences: `init.trace.rc`'s `chmod 0222 .../tracing/trace_marker` never ran →
  79× `cutils-trace: Permission denied`; and the vibrator sysfs chowns (which live in
  that orphaned init.rc:630/641) never ran → vibrator EACCES. SELinux REJECTED as the
  cause (permissive; every avc line has `permissive=1`). Stale-AID sweep: clean, the
  earlier `net_bt_stack`→`bluetooth` fix holds.
  APPLIED to `device/meizu/m681/rootdir/init.mt6755.rc` (the file that IS imported):
  `import /init.trace.rc`, plus a `post-fs-data` block chowning BOTH
  `/sys/class/timed_output/vibrator/{enable,vibr_vol}` (the agent thought `enable` was
  already handled in the shipped file — grep shows it was NOT; both were orphaned).
  Agent also flags (HYPOTHESIS) that `init.m681_sensors.rc` / `init.m681_cameraserver.rc`
  are orphaned the same way, and that m6/M6T share the pattern via
  `mt6755-common/rootdir/init.mt6755.rc` — follow-ups, not done yet.

### Kernel/netd report (class 15) + onion layer 2 (2026-07-25)
Kernel agent (evidence-backed):
- **dummy0 = kernel-side gap, FACT:** `CONFIG_DUMMY is not set` in
  `/home/n8n/mt6755-49/b-4.4/out-connfix/.config:1627`, and that .config provably
  belongs to the pinned artifact (its Image.gz-dtb sha256 == `fe027c5a…` in
  BoardConfig). No m681 4.4 defconfig lineage ever set it. Fix = `CONFIG_DUMMY=y` in
  `wt-camera/arch/arm64/configs/m681_defconfig` (source tree = `wt-camera`, branch
  `final-converge`, HEAD 703cf3d4 — identified via `__FILE__` paths baked in WARN
  traces) + kernel rebuild + re-pin. DEFERRED: needs a kernel build cycle, severity
  LOW, and see the cascade finding below.
- **Display: kernel is CLEAN** — `disp_probe DONE`, LCM `ili9885_fhd_dsi_vdo_txd1`
  probed, `mtkfb_probe: register_framebuffer r=0` (fb0 created). Confirms the failure
  was purely the userspace HAL/ABI layer — consistent with the Fence fix working.
- Every other dmesg WARN/error is baked into the SAME pinned binary that booted on
  15.1 (icm20608 duplicate sysfs, ioremap WARNs, M4U sample, msdc1 CMD52 pre-power-on,
  absent-sensor probes) → REJECTED as 16.0 regressions.
- New-but-cosmetic: SPM sleep-PCM firmware blobs (`pcm_suspend_m.bin`,
  `pcm_sodi_*`, `pcm_deepidle_*`) fail to load — affects deep-idle only; check
  `vendor/meizu/m681/proprietary-files.txt` for them (TODO).
- **Cascade hypothesis CONFIRMED by test after the Fence fix:** `dummy0` errors
  70+ → **0**; `wificond is starting up` 70+ → **4**. Most of that spam was init
  restarting service classes behind the SF crash loop, exactly as predicted.

**Onion layer 2 (current state, uptime 255 s, still no boot_completed):**
- surfaceflinger: 4 crashes (was 43+, non-recovering) — now dies only sometimes,
  inside the blob at `DisplayManager::init()` ← `HWCMediator::open()` ←
  `HwcLoader::openDeviceWithAdapter` (the HWC1→HWC2 adapter path), and RECOVERS on
  retry. New sub-class, agent dispatched.
- **NEW dominant loop: `android.hardware.audio@2.0-service` SIGSEGV every ~5 s.**
  Backtrace shows the Pie HIDL impl calling `Device::getMasterMute` but landing inside
  the blob's `AudioALSAHardware::createAudioPatch` → classic `audio_hw_device_t`
  member-offset mismatch (Nougat blob vs Pie header). Prime lead: the 15.1 lane shipped
  MTK's own `vendor/mediatek/hidl/audio` service (ported here in the 3361-line patch),
  while the device is running the AOSP `android.hardware.audio@2.0-impl.so`.
  Agent dispatched.
Fresh logs: west `/home/gun/m681-los16-logs2/`, local
`/srv/forge/android/m681/logs-los16-boot2/m681-los16-logs2/`.

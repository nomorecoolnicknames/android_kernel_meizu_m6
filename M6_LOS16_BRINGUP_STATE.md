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

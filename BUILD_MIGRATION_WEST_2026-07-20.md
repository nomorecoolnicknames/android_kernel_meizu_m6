# LOS 15.1 M6 rebuild moved to west — 2026-07-20

## 1. Why a rebuild at all

- **FACT:** every M6 ROM artifact newer than 2026-06-08 is gone from disk.
  MinIO's newest `meizu_m6` object is `lineage-15.1-20260607-UNOFFICIAL-meizu_m6-m6runtime3-patched`;
  everything after that in MinIO and on the mounted gdrive is **m681**, a
  different device.
- **FACT:** `BRINGUP_STATE.md` describes a `lineage-15.1-20260621-UNOFFICIAL-meizu_m6.zip`
  (548356905 B) and a 597 MB `build1-baseline.zip` under `/srv/forge/work/`.
  Neither file exists any more; only Nubia (`nx549j`) zips remain there.
- **FACT:** the only intact M6 ROM on disk is
  `/srv/forge/android/export/meizu_m6_artifacts/20260608-m6-full-rom-c81boot/lineage-15.1-20260608-UNOFFICIAL-meizu_m6-c81boot.zip`
  — `zip -T` OK, sha256 `04f05e46…` matches its manifest, embedded `boot.img`
  sha256 `c81c52bb…` matches `ARTIFACT_IDENTITY.txt` exactly.
- **FACT:** the ROM device tree HEAD is `0e6ba46` (2026-07-12) and already carries
  the later M6 work (Goodix @2.0 HAL packaging, camera compat shims, microtrust,
  keylayouts, mBack=HOME). The newest usable zip therefore lags the tree by ~5 weeks.
- **INFERENCE:** a full rebuild is the only way to get those tree fixes into a
  flashable image; no newer prebuilt exists to fall back on.

## 2. Disk reclaim (authorised cleanup)

- **FACT:** `/srv/forge/work/nx549j-preserve` held 104.4 GB across 179
  `release-attempt*` dirs (2026-05-27 … 06-09) — full ROM zips 12.8 GB,
  target_files 11.6 GB, OTA 4.0 GB, raw system images 49.5 GB, docs/logs 0.38 GB.
- **FACT:** with explicit user approval, 182 files / **74.31 GB** were deleted
  (full ROM zips, target_files, OTA zips, raw system/userdata images). Kept: 247
  boot/kernel/recovery images (3.64 GB) and all 8996 README/log files.
  Manifest of everything removed:
  `/srv/forge/work/nx549j-preserve-DELETED-MANIFEST-20260720.txt`.
- **FACT:** `/srv/forge` free space went 296 GB → 360 GB; the directory is now 30 GB.

## 3. Why the build moved to west

- **FACT (measured, not assumed):** streaming the 35 GB working tree from the
  local host ran at **82 MB/min** while `tar` sat at **0.7 % CPU** — i.e. not CPU
  bound. That is ≈45 files/s, the signature of per-file seek latency on the
  local `QEMU HARDDISK` (`ROTA=1`). Projected transfer time ≈7 h.
- **FACT:** west (`/dev/nvme0n1p2`, KINGSTON SKC3000, `ROTA=0`) has 701 GB free,
  8 cores, 30 GB RAM.
- **FACT:** transferring `.repo` + root `.git` instead runs at **334 MB/min** —
  4× faster — because `.repo` is 23 248 files dominated by 2.3 GB / 1.3 GB / 1.0 GB
  packfiles, i.e. sequential reads, versus ~10⁶ small files in the worktree.
- **INFERENCE:** the local bottleneck is random small-file I/O, exactly as the
  user diagnosed. Doing the checkout on west's NVMe converts that cost into
  sequential transfer plus local SSD writes.
- **LIMIT:** west has fewer cores (8 vs 32) and less RAM (30 GB vs 62 GB) than the
  local host, so this trade only pays off if the build is I/O-bound rather than
  CPU-bound. That is not yet proven for the compile phase — only for the transfer.

## 4. Tree layout discovered (correction)

- **FACT:** this ROM tree is **not** a pure repo checkout. The root `.git`
  (332 MB, 5176 files) tracks only a **5624-file overlay**: `device/`, `vendor/`,
  `docs/`, `BRINGUP_STATE.md`, `CLEAN_BACON_BUILD_FIXES.md`,
  `verify_boot_to_ui_fixes.sh`.
- **FACT:** the AOSP source itself comes from `.repo` (16 GB); project
  directories such as `frameworks/base`, `art`, `bionic`, `build/make`,
  `external/skia` carry `.git` **symlinks** into `.repo/projects/`.
- **INFERENCE:** transferring `.repo` + root `.git` and running a local
  `repo sync -l` on west reconstructs the full 35 GB worktree without ever
  shipping the small files over the wire.

## 5. Build environment on west

- **FACT:** reused the already-proven toolchain image
  `androidforge/build-env:android-8.1` (LOS 15.1 == Android 8.1). Verified
  contents: OpenJDK **1.8.0_452** (Temurin, `JAVA_HOME=/opt/java/openjdk`),
  `python2`, `python3`, `make`, `gcc`, `git`, `ccache`, `zip`, `unzip`, `bc`,
  `bison`, `flex`. `repo` is absent from the image and is supplied separately.
- **FACT:** the image was exported locally and loaded on west
  (`Loaded image: androidforge/build-env:android-8.1`). A first attempt piping
  `docker load` over ssh **silently failed** with `sudo: Incorrect authentication
  attempt` while still exiting 0 — the pipeline's exit code masked it. Use the
  `gunwest` skill's `sudo` helper, and verify with `docker images`.
- **FACT:** added a 64 GB swapfile on west's NVMe (`/swap-build.img`, priority 10),
  taking total swap to 99 GB with 84 GB free. Swap is disk, so it does not take
  RAM away from the user's running workloads — an explicit constraint they set.
- **FACT:** lunch combo for this device is `lineage_meizu_m6-userdebug`
  (`device/meizu/meizu_m6/AndroidProducts.mk`, `vendorsetup.sh`).
- Build driver files staged on west: `docker-compose.m6build.yml`, `build-m6.sh`
  (pins `python`→python2, caps `_JAVA_OPTIONS=-Xmx6g`, `-j6`, ccache 50 GB on NVMe).

## 6. Open / next

1. Finish the `.repo` + `.git` transfer, then `repo sync -l` on west and
   `git checkout` the overlay.
2. Run the build; confirm whether the compile phase is actually faster on west's
   8 cores — §3's LIMIT is unresolved until then.
3. The stock reference handset `711HEBRN23L3N` was **not** flashed; the stock
   `system` partition was backed up first
   (`captures/stock-m6-711HEBRN23L3N-2026-07-20/images/system.img`).
4. adb for that device lives on port **5037** on container 228 (the earlier
   "device not found" was a wrong-port lookup, not a lost device).

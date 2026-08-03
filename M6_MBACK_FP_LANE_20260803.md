# LANE mback — mBack button + fingerprint, Meizu M6 / LOS 16.0 — 2026-08-03

Owner: lane `mback-lane`. Device `711HEBRN23L3N` (container 228, adb port **5041**,
ports migrate — re-scan 5037..5041 each window). Identity gate this window (FACT):
`ro.build.fingerprint = meizu/lineage_meizu_m6/meizu_m6:9/PQ3A.190801.002/root07251016:userdebug/test-keys`,
`/proc/version = Linux 3.18.140 … Mon Aug 3 13:00:47 MSK 2026`, `sys.boot_completed=1`,
SELinux **Permissive** (`getenforce`), adb shell uid 2000 groups incl. `input`,
`net_bt_admin`, `uhid`. Device clock ≈ −14.5 h vs real time (logcat "08-02 23:xx" during
the 08-03 13:xx MSK window).

⚠ Old-device disclaimer: everything in `/srv/forge/work/mback-button-FINDINGS.md` dated
before 2026-06-24 was measured on the DEAD unit `711HEBSR277K5`. On-device claims from
there are treated as HYPOTHESIS until re-measured here. Several did NOT transfer (§2.1).

---

## 1. mBack — DONE, package ready, verified live with synthetic presses

### 1.1 Live state before the change (FACT)
- Button = `mtk-kpd`, `/dev/input/event1` (`/proc/bus/input/devices`), emits KEY_HOME 102.
- `dumpsys input`: mtk-kpd → `KeyLayoutFile: /vendor/usr/keylayout/Generic.kl`. The
  installed `/system/usr/keylayout/mtk-kpd.kl` (`key 102 HOME WAKE`) is **not** the file
  EventHub uses — same behaviour the device tree already documents and routes around
  (`device_meizu_m6.mk:126-131`): the shipped **vendor Generic.kl override** maps
  `key 102 HOME` (line 124), `key 158 BACK`, `key 172 HOME`, `key 580 APP_SWITCH`,
  `key 116 POWER` (no WAKE). Overlay `config_longPressOnHomeBehavior=2`
  (`m3_meizu_m6-common/overlay/...config.xml:186`) is in the build.
- So natively today: click=Home, long-press=Recents (policy timing). tap=Back is
  impossible natively — a single KEY_HOME carries no duration (old FACT `kpd.c:602-613`,
  source-level, still valid).
- Why EventHub skips the name-matched `mtk-kpd.kl` remains unexplained (HYPOTHESIS: parse
  reject or a probe-order quirk in this tree's `KeyMap::load`; no parse errors in the
  surviving logcat window — inconclusive because boot-time logcat had partially rotated).
  Not load-bearing: the daemon grabs raw events, and the fallback path is the vendor
  Generic.kl override, which is correct.

### 1.2 What was built
`mbackd` — 200-line C daemon, `device/meizu/meizu_m6/mback/` on west
(`/home/gun/m6rom16/rom`): `mbackd.c`, `Android.mk`, `mbackd.rc`,
`Vendor_4642_Product_0001.kl`. Wired into `device_meizu_m6.mk` (appended block:
`PRODUCT_PACKAGES += mbackd`, `PRODUCT_COPY_FILES += …/Vendor_4642_Product_0001.kl`).
Design:
- `EVIOCGRAB` on event1 → raw KEY_HOME never reaches Android while the daemon lives.
- uinput device **"mbackd-keys"** (BUS_VIRTUAL, 4642:0001 → dedicated kl by identifier
  probe; until that kl is installed the vendor Generic.kl fallback maps the injected
  codes identically — verified live, §1.3).
- tap (release ≤ `persist.sys.mback.tap_ms`, default 250) → KEY_BACK 158;
  click (release < hold) → KEY_HOMEPAGE 172 (avoids the 102/MOVE_HOME ambiguity);
  hold (down ≥ `persist.sys.mback.hold_ms`, default 500) → KEY_APPSELECT 580 fired **at
  the threshold**, release swallowed. Props re-read on every press.
- All other keys on event1 (VOL± 114/115, POWER 116, vendor 129/193/408) forwarded
  unchanged — grabbing does not cost the volume/power buttons.
- Failure mode: daemon dead ⇒ grab auto-released ⇒ native mapping resumes. Service
  `class late_start`, `user system`, `group system input net_bt_admin`,
  `seclabel u:r:init:s0` (forge-bootdiag.rc precedent; device is Permissive — needs a
  real domain before any enforcing build).

Build: `build-m6-mback.sh` (copy of shim, `mka -j8 mbackd`), docker
`androidforge/build-env:android-8.1`, under `/home/gun/.m6build.lock` (taken/released).
`RC=0`, 52 s.

### 1.3 Live verification — no flash, no human press needed (FACT)
Prototype `/data/local/tmp/mbackd` (md5 `8385d3be94f32834e00d69e45c948ffa`, gate held at
west → container → device). Run as **shell uid** (event1 is 0660 root:input, uinput is
0660 system:net_bt_admin; shell is in both groups). Synthetic presses were written into
the grabbed event1 with `sendevent` (evdev write → `input_inject_event` → delivered to
the grab holder). Results, from the daemon's logcat (`-s mbackd`) + `dumpsys input`:

| injected | daemon classified | Android saw |
|---|---|---|
| down/up back-to-back | `tap 104ms -> BACK` | KeyEvent pair, device 8 (mbackd-keys) |
| down, sleep 0.25, up | `click 400ms -> HOME` | KeyEvent pair |
| down, sleep 0.9, up | `hold >= 500ms -> APP_SWITCH` | KeyEvent pair at threshold |
| VOL_UP down/up | `fwd code=115` ×2 | KeyEvent pair 117 ms apart (real timing kept) |

- RecentQueue held **exactly** the injected pairs — no duplicate raw-102 events ⇒ the
  grab really excludes Android from event1 (FACT).
- `mbackd-keys` bound `/vendor/usr/keylayout/Generic.kl` (our kl not installed yet) and
  158/172/580 all map correctly there ⇒ the package works even before the kl lands.
- Teardown clean: no process, `event7` gone, `dumpsys input` clear (FACT).
- Method note: the "foreground getevent" buffering trap has an ssh-layer cousin — a
  `timeout`-killed ssh>file session delivered **zero** daemon stdout; logcat tag
  `mbackd` was the reliable channel. One more instrument added on purpose.
- Note: `sendevent` exec overhead ≈ 100 ms/pair on this SoC — synthetic "click" needs
  `sleep 0.25`, not 0.35 (first attempt landed ≥ 500 ms and correctly classified hold).

### 1.4 Package + install
`/home/gun/m6mback-out/` on west: `mbackd`, `mbackd.rc`, `Vendor_4642_Product_0001.kl`,
`MANIFEST.txt` (destinations, modes, md5s, TWRP steps, no-flash live-run option).
md5: mbackd `8385d3be…`, mbackd.rc `1274c330…`, kl `1bc84d30…`.

### 1.5 What needs the user (cannot be closed without a human press)
1. Real-press UX validation: thresholds 250/500 ms are engineering defaults — ask the
   user for a few taps/clicks/holds while `logcat -s mbackd` streams; retune via
   `persist.sys.mback.tap_ms|hold_ms` (lead sets props — setprop is outside this lane's
   permissions).
2. Physical-electrical check that a real finger press produces the same single
   down/up pair as `sendevent` (expected from kpd.c, INFERENCE not FACT on this unit).

---

## 2. Fingerprint — TEE blocker substantially REVISED on this unit

### 2.1 What did NOT transfer from the dead unit (all FACT, this window)
| old claim (711HEBSR277K5) | this unit (711HEBRN23L3N) |
|---|---|
| TEE rejects Goodix TA, enumerate never works | **TA answers**: boot enumerate returned **34 stale Flyme templates** (`FingerprintService: Adding 34 fingerprints for deletion` → `Done with client: android`, 23:05:11) — full chain framework→fps_hal→goodixfingerprintd→libgf_hal→MicroTrust→TA is alive |
| `goodixfingerprintd` mislabeled `u:r:init:s0` | runs `u:r:hal_fingerprint_default:s0` (ps -AZ), rc has explicit seclabel (`/vendor/etc/init/microtrust.rc`) |
| `Fingerprint HAL id: 0` era | **HAL id 518514827264** (non-zero) |
| challenge=0 measured | **not yet measured here** — no enroll attempted on this build (no `preEnroll` line in logcat) |

Kernel FP wrapper is instrumented and healthy: `[M6_FP_IOC]`/`[M6_TEE_FP]` command
cycles complete with `send_fp_command retVal=0 … result=0` (dmesg 423–506 s window).
`teei_interrupt.cc:336 … Error:-1` appeared **2×** in the same window against 3
successful FP command cycles — Error:-1 **coexists with FP success** on this unit, so
attributing it specifically to the Goodix TA (the old reading) does not hold here
(INFERENCE). dmesg ring rotated later in the session (parallel lanes are noisy); the
counts are from the surviving window only.

### 2.2 TA identity vs stock (FACT — closes the "where are the TAs" task)
TAs live in **`/vendor/thh/`** (= `/system/vendor/thh`, `/vendor` is a symlink; no
mcRegistry — MicroTrust, not Trustonic). Runtime store `/data/thh/` (tee/, tee_00..0F,
system/); `tas/` empty, `tee.cur.md5`+`tee.ori.md5` rewritten each boot by teei_daemon
(0700 system — unreadable as shell; lead can diff cur vs ori as root: equal ⇒ TEE image
unmodified).
md5 device == md5 stock (extracted from
`/srv/forge/work/m6-stock-system-7.1.2.0G/system.raw.img` via 7z, no mount):
`alipayapp b6f8073c…`, **`fp_server_goodix 03e190c3…`**, `softsim 924e20fe…`,
**`soter.raw b84be09f…`** — **byte-identical to stock, all four**. (Stock also ships
`fp_server_sunwave` — absent on device; our sensor is Goodix, irrelevant.)

### 2.3 Keymaster wiring — one real defect found (FACT), impact is HYPOTHESIS
- `ro.hardware.keystore=mt6750`, `soter.teei.init=INIT_OK`, `/dev/ut_keymaster` exists
  0666 system:system.
- `/vendor/manifest.xml` declares keymaster@3.0 **transport=passthrough** ⇒ keystore
  loads `android.hardware.keymaster@3.0-impl.so` in-process, which should dlopen
  `keystore.mt6750.so` (present in /vendor/lib64/hw). keystore daemon runs.
- **Defect:** `microtrust.rc:87` does `start keymaster-3-0`, but no `service
  keymaster-3-0` stanza exists anywhere (device rc grep) and
  `/vendor/bin/hw/android.hardware.keymaster@3.0-service` is **not on the image**
  (`init.svc.keymaster-3-0` empty = never declared, per the m681 empty-prop rule). The
  old two-part fix (fp-FINDINGS §B) landed only its `start` line. In passthrough mode
  the binderized service is redundant, so this is *probably* harmless — but whether
  keystore's in-process impl actually opened `/dev/ut_keymaster` is **unverifiable as
  shell** (fd/maps are ptrace-gated). Lead check (root): `ls -l /proc/$(pidof
  keystore)/fd | grep ut_keymaster`; `grep keystore.mt6750 /proc/$(pidof keystore)/maps`.

### 2.4 Hypotheses for the next enroll attempt (≥3, each with a falsifier)
- **H-FP1 (leading): no TEE blocker remains on this unit; enroll will work.** Evidence
  for: TA enumerate works, TAs byte-identical to stock, daemon domain correct, HAL open.
  Falsifier: enroll window (below) shows `preEnroll get challenge:0`.
- **H-FP2: HMAC-key path still dead** (GF_CMD_HMAC_KEY → `ut_pf_km_get_hmac_key` needs a
  live REE keymaster client on `/dev/ut_keymaster`; enumerate does NOT exercise this, so
  §2.1 does not refute it). Evidence for: no binderized keymaster; in-process passthrough
  unverified. Falsifier: challenge≠0 at enroll ⇒ dead. If challenge=0: lead's root fd
  check above discriminates "keystore never opened ut_keymaster" (fix = package
  `android.hardware.keymaster@3.0-service` + the missing stanza, module exists at
  `hardware/interfaces/keymaster/3.0/default`) vs "opened but TA still fails" (⇒ H-FP4).
- **H-FP3: 34 stale Flyme templates block/corrupt enroll** (framework queued deletion;
  TA-side remove may fail or leave storage in a Flyme-format state; `/data/thh/tee/tas`
  is empty on LOS while stock provisioned it). Falsifier: after an enroll attempt,
  `dumpsys fingerprint` count and logcat `remove`/`onRemoved` lines; success of
  enrollment itself refutes.
- **H-FP4 (fallback, kernel/TEE boundary — old §2026-06-22 residue): this build's
  3.18.140 teei/gf_spi_tee path degrades some FP commands** (the Error:-1 lines). Against
  it: commands observed return result=0; enumerate data round-trips. Falsifier: during
  the enroll window stream `dmesg -w | grep -E "M6_TEE_FP|Error:-1"` — a challenge=0
  correlated 1:1 with Error:-1/`result=-N` confirms; clean logs with challenge≠0 refute.

**REJECTED (do not revisit):** "mt6750/keystore prop is the FP blocker" — refuted live
on the old unit (fixed bootloop+PIN, challenge stayed 0, broke credential+multitasking).
This lane touched no keystore config.

### 2.5 Enroll window protocol (needs user finger; lead coordinates)
Precondition: `logcat -d | grep "Fingerprint HAL id"` non-zero this boot.
1. Stream A: `adb -s 711HEBRN23L3N shell "dmesg -w"` → grep `M6_TEE_FP|Error:-1|M6_FP_IOC`.
2. Stream B: `logcat | grep -E "preEnroll|challenge|onError|onEnrollResult|onAcquired"`.
3. User: set PIN, Settings→Security→Fingerprint→Add, ONE touch-and-hold ~2 s.
4. Before/after: `grep goodix /proc/interrupts` (IRQ 300 delta = sensor really scanned).
Decision table: challenge≠0 + onEnrollResult → H-FP1; challenge=0 → H-FP2/H-FP4 per
dmesg correlation; challenge≠0 but enroll errors on remove/storage → H-FP3.

### 2.6 Goodix nav (tap=Back via sensor) — parked
The stock-like nav path (navigate(2) binder + callback receiver) is superseded by
mbackd for the button UX and stays blocked behind enroll anyway. Old nav facts
(transaction 14 etc.) are old-unit; do not rely without re-verification.

---

## 3. Files touched (west, `/home/gun/m6rom16/rom`, plain dirs — not git)
- `device/meizu/meizu_m6/mback/{mbackd.c,Android.mk,mbackd.rc,Vendor_4642_Product_0001.kl}` — NEW
- `device/meizu/meizu_m6/device_meizu_m6.mk` — appended mbackd block (end of file)
- `build-m6-mback.sh` — NEW (build entry, mka mbackd)
- Artifacts: `/home/gun/m6mback-out/` + MANIFEST.txt
- On device (temporary, no /system writes): `/data/local/tmp/mbackd` (md5 `8385d3be…`)

Commits deliberately left to the lead (lane instruction).

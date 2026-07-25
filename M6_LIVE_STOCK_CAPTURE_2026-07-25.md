# M6 (711HEBRN23L3N) — live stock capture 2026-07-25, root, READ-ONLY

Device moved to the **gunwest hub** (container 228 is dead). Captured under Magisk root
with a strictly read-only script (no register writes anywhere — the old M6 board death is
still an open suspicion against PMIC/charger *write* experiments, see
`artifacts/BOARD_KILL_ANALYSIS.md`).

- Script: `/home/gun/m6_live_capture.sh` (source in session scratchpad; 45 capture units).
- Archive: `/srv/forge/android/meizu_m6/captures/m6cap-20260725-1305.tgz`
  sha256 `6808ac08973610c2004af02150b585b7b5ed00104a2961c2bbf4cfdd760b67a9`
  (also on west `/home/gun/m6-captures/`), extracted next to it, 49 files, 1.2 MB.
- Trap for the next agent: Android `sh` is **mksh**, where `r` is a builtin alias
  (`fc -e -`) — a helper function named `r()` silently never defines and every call dies.
  Also **no `awk`** on Flyme 6 (`toybox`/`tar`/`strings` exist). Renamed to `cap()`.

## Identity (FACT)
| field | value |
|---|---|
| serial / device | `711HEBRN23L3N` / `meizu_M6`, model `MEIZU M6` |
| firmware | **Flyme 6.2.0.0RU**, Android 7.0, build fingerprint `Meizu/meizu_M6_RU/meizu_M6:7.0/NRD90M/1513569472:user/release-keys` |
| bootimage fp | `Meizu/full_wt6750s_66_s11_n/wt6750s_66_s11_n:7.0/...` |
| SoC | device-tree model **MT6750V/DN**, compatible `mediatek,MT6755` |
| root | Magisk (`u:r:magisk:s0` under `su`) |
| psn / hw / sw | `711HZZ116E1290C3` / `hw_version=0x10000000` / `sw_version=71151003` |
| stock cmdline | `androidboot.selinux=permissive`, `verifiedbootstate=orange`, `bootreason=wdt_by_pass_pwk` |

⚠ Note vs memory: memory's `stock-flyme-firmware` entry refers to **7.1.2.0G**; this unit
is running **6.2.0.0RU**. Different firmware line — do not mix reference artifacts.

## BOOTLOADER LOCK STATE — appears ALREADY UNLOCKED (INFERENCE from 3 FACTs)
1. `seccfg` partition (`by-name/seccfg` = mmcblk0p?; dumped 128 KB read-only) decodes as
   MTK SEC_CFG v4: magic `MMMM`, size 60, **`lock_state = 3 (LKS_UNLOCK)`**,
   `critical_lock_state = 1 (LKS_DEFAULT)`, `sboot_runtime = 0`, endflag `EEEE`, +32-byte hash.
2. `ro.boot.flash.locked = 0`.
3. `ro.boot.verifiedbootstate = orange` (orange = unlocked / verification off).

Counter-signal: `sys.oem_unlock_allowed = 0` — but that is the **Android dev-options OEM
toggle**, not the LK lock state (`ro.oem_unlock_supported = 1`).

**Disconfirming test (not run — needs the device out of Android, user-gated):**
`adb reboot bootloader` → `fastboot getvar unlocked` / `fastboot oem device-info`, or a
throwaway `fastboot boot <img>`. If LK still refuses writes, the lock is enforced elsewhere
(Meizu LK policy) and mtkclient/BROM seccfg rewrite is the fallback lane.

## Partition map (by-name → mmcblk0pN, FACT)
`/dev/block/platform/mtk-msdc.0/11230000.msdc0/by-name`:
`boot=p21`, `logo=p22`, `lk=p19`, `lk2=p20`, `para=p2`, `custom=p3`, `expdb=p4`, `frp=p5`,
`nvdata=p6`, `proinfo=p7`, `devinfo=p8`, `metadata=p9`, `oemkeystore=p13`, `md1img=p14`,
`md1dsp=p15`, `md1arm7=p16`, `md3img=p17`, `nvram=p18`, `keystore=p26`, `cache=p29`,
`flashinfo=p31` (+ seccfg, recovery, system, userdata — full listing in `95-unlock-inputs`).
⚠ **boot is p21 on this M6** (m681 is p22 — do not cross-wire the flash lanes).

## CAMERA lane (rear-cam-black root cause)
- **Live stock clocks (FACT, `20-clk_summary` / `43-camera_clk`), idle state:**
  `scam_sel = 109 200 000` (109.2 MHz — matches the stock baseline in memory),
  `camtg_sel = 48 000 000`, all `img_image_*` (cam_cam / cam_smi / sen_tg / cam_sv /
  image_fd / larb2_smi*) = `286 000 000`, `mm_disp0_cam_mdp = 286 000 000`.
  enable_cnt/prepare_cnt are 0 at idle (camera closed) — for the LOS diff, capture the
  same table with the camera app OPEN on both stocks and LOS.
- **Sensors (FACT, `/proc/driver/camera_info`):** `CAM[1]: imx278trulymipiraw` (rear),
  `CAM[2]: ov8856jslmipiraw` (front).
- Nodes present: `/dev/kd_camera_hw`, `kd_camera_hw_bus2`, `camera-isp`, `camera-fdvt`,
  `kd_camera_flashlight`, `/dev/MTK_SMI`.

## DISPLAY
- `fb0`: **720x1280**, 32 bpp, stride 736, driver name `mtkfb`.
- **LCM (FACT, `/proc/device-tree/chosen/atag,videolfb`): `ili9881c_hd_dsi_txd`.**
  ⚠ CONTRADICTION to flag, not silently resolve: an earlier note (in the M6T panel
  discussion) claimed the M6 stock DTB lcmname was `nt35695_fhd…`. This unit on
  6.2.0.0RU reports `ili9881c_hd_dsi_txd` at HD 720x1280, which is self-consistent
  (`_hd_`). Either the earlier note was wrong, or panel/LCM varies by unit/firmware line.
  Ground truth for THIS unit = this capture.

## FINGERPRINT / TEE (relevant to the enroll blocker)
- Nodes: `/dev/goodix_fp` (224,0), MicroTrust **TEEI**: `/dev/teei_client`, `/dev/teei_config`,
  `/dev/teei_fp`. Services `goodixfpd` + `fingerprintd` **running**.
- **`/data/thh` on stock is POPULATED**: `system/`, `tee/`, `tee_00`…`tee_03` (20 entries).
  On our LOS build the same path was EMPTY — memory `m6-fp-tee-block` records the enroll
  blocker as "MicroTrust TEE rejects the Goodix FP TA (Error:-1), `/data/thh` empty".
  → **New lead (HYPOTHESIS):** the TA/keyblob material provisioned in stock `/data/thh`
  (and/or its backing partition) is what LOS lacks. Falsification: dump the stock
  `/data/thh` tree + identify which files the TA loader opens (strace/logcat on stock
  enroll), then compare with the LOS path. NOT yet collected — needs a targeted capture.

## Battery / charger (this unit is HEALTHY, unlike the dead board)
`capacity=53`, `status=Charging`, `batt_temp=300` (30.0 °C), `present=1`,
`TempBattVoltage=581`, `TemperatureR=8054`.

## Gaps in this capture (honest list)
- `/proc/config.gz` **absent** on stock → no live kernel config from the device.
- `/sys/kernel/debug/regulator/regulator_summary` absent (no regulator debugfs in this kernel).
- `/proc/driver/camsensor*` read back empty (only `camera_info` yields data).
- No LCM lines in `dmesg` (ring already rotated at capture time, uptime long).
- Camera clock table captured at IDLE only (see camera lane above).

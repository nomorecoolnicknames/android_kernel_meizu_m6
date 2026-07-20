# Stock M6 live reference capture — 2026-07-20

Live root capture from a **working stock** Meizu M6 (the spare handset on
container 228), taken to answer "is our kernel right, or did we get something
wrong?" without stock kernel sources.

Capture root (not committed — contains vendor images):
`/srv/forge/android/meizu_m6/captures/stock-m6-711HEBRN23L3N-2026-07-20/`

## 1. Device identity and capture boundary

- **FACT** (`m6cap/proc/getprop.txt`, `m6cap/proc/version.txt`): the captured
  device is `ro.product.model=MEIZU M6`, `ro.product.device=meizu_M6`,
  `ro.board.platform=mt6750`, `ro.build.display.id=Flyme 6.2.0.0RU`,
  Android 7.0, `ro.serialno=711HEBRN23L3N`.
- **FACT:** stock kernel banner is
  `Linux version 3.18.35+ (flyme@Mz-Builder-L23) ... #1 SMP PREEMPT Mon Dec 18 12:19:05 CST 2017`.
  Our tree is based on 3.18.140, i.e. **a newer 3.18 stable base than stock**.
- **FACT:** root is Magisk 30.7 (`/sbin/magisk`, `su` returns
  `uid=0(root) ... context=u:r:magisk:s0`). All capture commands were reads plus
  writes confined to `/data/local/tmp`. No partition was written, erased or
  flashed.
- **NOTE (identity hygiene):** this serial `711HEBRN23L3N` is **not** the
  `711HECRN25ULN` previously recorded as "the new M6", and not the dead
  original `711HEBSR277K5`. Treat it as its own reference handset. The other
  devices on the same bridge (`91HEBNL163XD` m681, `30785d1a` Nubia) were not
  touched.
- **FACT:** identity-bearing partitions (`nvram`, `nvdata`, `proinfo`,
  `protect1/2`) were deliberately **not** pulled.

## 2. Stock partition map — settles the boot-size question

- **FACT** (`m6cap/inventory/by-name.txt`, `m6cap/proc/partitions.txt`):
  `boot -> mmcblk0p21` with **16384 KiB = 16 MiB**; `recovery -> mmcblk0p1` =
  32 MiB; `lk -> p19` and `lk2 -> p20` = 1 MiB each; `logo -> p22` = 8 MiB;
  `seccfg -> p12` = 8 MiB; preloader lives on `mmcblk0boot0` = 4 MiB.
- **INFERENCE:** the historical "9025536-byte boot fit limit" is **not** a
  partition limit. The boot partition is 16 MiB on real hardware, which
  corroborates the 16 MB correction already noted in the 4.4 port progress doc.

Pulled stock images (SHA-256, in `images/`):

| image | bytes | sha256 |
|---|---:|---|
| boot.img | 16777216 | `7df7636dd32f856a2beb801b8ec690c14da39df3a1633744e8c38640196efa95` |
| recovery.img | 33554432 | `5c878da5b3569bad208aa64be2e161b67ce25367dca57850adca793bdb49f57a` |
| lk.img | 1048576 | `3b7067cbcf79c7514fbb4d11a0fd2329dd616eb49657d63ab0616321062979ac` |
| lk2.img | 1048576 | `3b7067cbcf79c7514fbb4d11a0fd2329dd616eb49657d63ab0616321062979ac` |
| logo.img | 8388608 | `54452a669af9b245cb73bbfe80b6469fb8e085190d4df5e7038bf80655d6e2c5` |
| seccfg.img | 8388608 | `26a49632187c105f0e76a18ef63e41386d989171a7f70775dd82b231f65925c2` |
| preloader.img | 4194304 | `550c3ba4af075d871ae1ada45e156bd1073a2ab94016677b1934b5ba8ab88b37` |

- **FACT:** `lk` and `lk2` are byte-identical (same hash) — lk2 is a plain backup slot.

## 3. Stock boot image internals

- **FACT** (`unpacked/`, parsed from the Android boot header): `page_size=2048`,
  `kernel_size=8027219` (7.66 MiB, gzip magic `1f8b`), `ramdisk_size=3811103`,
  `kernel_addr=0x40080000`, `ramdisk_addr=0x45000000`, `tags_addr=0x44000000`,
  board name `1513569472` (= the 2017-12-18 build stamp).
- **FACT:** stock kernel cmdline is
  `bootopt=64S3,32N2,64N2 androidboot.selinux=permissive`.
  **Stock Flyme ships SELinux permissive.**
- **FACT:** the kernel blob is `Image.gz-dtb`; an appended DTB (`d00dfeed`) starts
  at offset 7946882 and is 80337 bytes. Decompiled to
  `unpacked/stock-appended.dts` (100 KiB, `model = "MT6755"`).
- **FACT:** stock DT `chosen/bootargs` is
  `console=tty0 console=ttyMT0,921600n1 root=/dev/ram initrd=0x44000000,0x4B434E loglevel=8`.
- **LIMIT:** `/proc/config.gz` is **absent** on stock (no `CONFIG_IKCONFIG_PROC`),
  so a direct stock-vs-ours defconfig diff is not available from the live device.

## 4. CAMERA — the reference we were missing

The rear camera works on stock, so this is ground truth for the black-rear-camera
bug. `clk_summary` was captured idle, then again with the stock camera app in the
foreground (`com.meizu.media.camera/.CameraActivity`, confirmed in
`camera-active/foreground.txt`).

### 4.1 Stock clock rates with the rear camera STREAMING

**FACT** (`camera-active/clk_summary_10_rear_active.txt`, diffed against
`clk_summary_00_idle.txt`):

| clock | rate | enable_cnt idle → active | parent |
|---|---:|---|---|
| `scam_sel` | **109 200 000** | 0 → **2** | `syspll3_d2` (109.2 MHz) ← `syspll3_ck` 218.4 MHz |
| `camtg_sel` | **48 000 000** | 0 → **3** | `univpll_d26` (48 MHz) |
| `img_image_sen_cam` | 48 000 000 | 0 → 4 | `camtg_sel` |
| `img_image_sen_tg` | 286 000 000 | 0 → 4 | `mm_sel` |
| `img_image_cam_cam` | 286 000 000 | 0 → 4 | `mm_sel` |
| `img_image_cam_sv` | 286 000 000 | 0 → 4 | `mm_sel` |
| `img_image_cam_smi` | 286 000 000 | 0 → 4 | `mm_sel` |
| `img_image_larb2_smi` | 286 000 000 | 0 → 4 | `mm_sel` |
| `img_image_fd` | 286 000 000 | 0 → 1 | `mm_sel` |
| `mm_sel` | 286 000 000 | 6 → 12 | — |
| `univpll` | 1 248 000 000 | 1 → 2 | — |
| `mainpll` | 1 092 000 000 | 1 → 2 | — |
| power domain `pg_isp` | — | 0 → **5** | — |
| power domain `pg_dis` | — | 1 → 6 | — |

- **FACT:** this confirms the long-standing "stock scam = 109.2 MHz" note with a
  full enable-count table, and adds `camtg_sel = 48 MHz` as the second anchor.

### 4.2 Stock sensor identity and timing

- **FACT** (`camera-active/dmesg_camera_bringup.txt`): the rear sensor is
  `IMX278_truly_camera_sensor`, `IMX278,MIPI 4LANE`,
  `i2c write id: 0x20, sensor id: 0x278`.
- **FACT:** stock modes are
  `preview 2100*1560@30fps,864Mbps/lane; video 4208*3120@30fps,864Mbps/lane; capture 13M@30fps,864Mbps/lane`,
  with `imgsensor.pclk = 240000000` in preview and `480000000` in capture,
  `line_length = 4976`.

### 4.3 Our tree vs stock — camera

Compared against `kernel-3.18/arch/arm64/boot/dts/meizu_m6.dts`,
`kernel-3.18/arch/arm64/boot/dts/mt6755.dtsi` and
`.../imgsensor/src/mt6755/imx278_mipi_raw/imx278mipiraw_Sensor.c`:

- **FACT:** camera GPIO pinmux is **identical** to stock. Decoded stock DTB pins
  vs our DTS: `cam0_rst = GPIO110`, `cam0_pnd = GPIO107`, `cam1_rst = GPIO111`,
  `cam1_pnd = GPIO108`, `sub_vcamd = GPIO82` — all match, including the
  `output-low`/`output-high` pairs and the node names `cam0@0..3`, `cam1@0..5`.
- **FACT:** the stock `kd_camera_hw1` pinctrl list has 11 entries ending in
  `cam_ldo_sub_vcamd_0`/`cam_ldo_sub_vcamd_1`; our DTS declares the same 11
  names and wires `pinctrl-9/-10` to `camera_pins_sub_vcamd0/1`.
- **FACT:** stock `kd_camera_hw1` clock-names are
  `TOP_CAMTG_SEL, TOP_MUX_SCAM, TOP_UNIVPLL_D26, TOP_UNIVPLL2_D2`; our
  `mt6755.dtsi` declares exactly the same four names in the same order.
- **FACT:** our IMX278 driver already matches stock on the numbers stock prints:
  `mipi_lane_num = SENSOR_MIPI_4_LANE`, `i2c_addr_table = {0x20, 0x40}`,
  `mclk = 24`, preview `pclk = 240000000` / `linelength = 4976` /
  `framelength = 1608` / grab window `2100x1560`, capture `pclk = 480000000`
  and grab window `4208x3120`.
- **REJECTED (H2 — "camera pinmux/CSI wiring inherited from the Honor 6C Pro
  donor is wrong for M6"):** for the reset/powerdown/LDO GPIOs and the sensor
  lane/i2c/pclk parameters, our tree is identical to the stock DTB and to what
  the stock driver prints. This hypothesis is refuted **for those specific
  parameters**; it is not refuted for anything not yet compared (e.g. seninf
  MUX/lane routing registers, which stock does not print).
- **OPEN (H1 — clock configuration):** the stock target table in §4.1 is now
  known. What is still missing is the **same table from our LOS build with the
  camera open**. Until that exists, no verdict on H1.

## 5. DISPLAY

- **FACT** (`m6cap/logs/dmesg.txt`): the active stock panel driver is
  `ili9881c_hd_dsi_txd` (`lcm_init`, `lcm_resume`, `lcm_suspend_power`).
  `r63350_fhd_dsi_vdo_tcl_csot` also appears in `lcm_init_power`, consistent
  with list iteration rather than being the selected panel.
- **INFERENCE:** the stock panel on this handset is the **ILI9881C HD** TXD
  panel — not `ili9885_fhd...` and not the `ili9881p` variant used in the 4.4
  port notes. Any panel work should be validated against `ili9881c_hd_dsi_txd`.

## 6. INPUT — mBack / fingerprint keys

- **FACT** (`m6cap/proc/bus-input-devices.txt`): stock exposes, among others,
  `mtk-kpd` (`/devices/soc/10010000.keypad`, event1) and a **virtual device
  named `fp-keys`** (`/devices/virtual/input/input2`, event2), plus `ACCDET`,
  `mtk-tpd`, `m_acc_input`, `m_alsps_input`, `m_mag_input`, `hwmdata`.
- **FACT (decoded key bitmaps):**
  - `mtk-kpd` reports **KEY_HOME(102)**, KEY_VOLUMEDOWN(114), KEY_VOLUMEUP(115),
    KEY_POWER(116), and code 408.
  - `fp-keys` reports KEY_UP(103), KEY_LEFT(105), KEY_RIGHT(106), KEY_DOWN(108),
    KEY_VOLUMEDOWN(114), KEY_VOLUMEUP(115), KEY_POWER(116), **KEY_MENU(139)**,
    **KEY_BACK(158)**, **KEY_HOMEPAGE(172)**, 194, KEY_CAMERA(212), 216,
    KEY_SEARCH(217).
- **INFERENCE:** this is direct confirmation of the existing mBack model — the
  physical button is `mtk-kpd` KEY_HOME, while Back/Menu/Home navigation is
  emitted by the **fingerprint driver's own virtual input device**, not by a
  keylayout trick on the keypad.
- **FACT (concrete divergence):** stock names that virtual device **`fp-keys`**;
  our Goodix driver names it **`gf-keys`**
  (`gf_spi_tee.c:64  #define GF_INPUT_NAME "gf-keys"`, registered at
  `gf_spi_tee.c:1764`). Android selects `/system/usr/keylayout/<device-name>.kl`
  by device name, so the stock ROM's `fp-keys.kl` cannot apply to a device
  called `gf-keys`.
- **HYPOTHESIS:** renaming our device to `fp-keys` (and shipping the matching
  `.kl`) is the low-risk way to inherit stock navigation semantics.
  **Falsify by:** building with the name changed and checking on-device that
  `/proc/bus/input/devices` shows `fp-keys` and that nav events reach the
  framework with the stock keylayout. Note our tree currently ships a
  self-consistent `gf-keys` + `gf-keys.kl` pair, so this must be changed as a
  pair, not one-sided.

## 7. What was NOT obtained

- **LIMIT:** `/proc/config.gz` absent → no live stock kernel config.
- **LIMIT:** `regulator_summary` absent (MTK uses its own PMIC framework, not
  the generic regulator debugfs), and `/sys/firmware/fdt` is absent; the DTB in
  §3 is the authoritative device tree instead.
- **LIMIT:** the first attempt to copy `/proc/device-tree` file-by-file hung the
  collector (procfs walk + per-file fork). Superseded by extracting the appended
  DTB from `boot.img`, which is the same tree.
- **LIMIT:** no LOS-side camera-active `clk_summary` exists yet, so §4.1 is a
  reference table without its comparison partner.

## 8. Next steps this capture enables

1. Boot our kernel on a working M6, open the camera, and capture the same
   `clk_summary` / `clk_dump` / `pg_isp` snapshot; diff against §4.1.
2. Decide the `gf-keys` → `fp-keys` rename together with its keylayout.
3. Validate panel work against `ili9881c_hd_dsi_txd`.
4. Treat 16 MiB as the real boot partition size.

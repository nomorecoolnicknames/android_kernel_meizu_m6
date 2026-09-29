# Meizu M6 · Linux 3.18.140

**Device:** Meizu M6, MediaTek MT6750. **Branch:** `m6-linux-3.18.140`.
M6 Linux 3.18.140 development baseline, also used as a source base for separate board ports.
The BSP calls this platform `mt6755`; the selected defconfig and board resources
identify the actual phone. M6 and M6T use different panel geometry and firmware.

## Hardware and source map

This table reads [meizu_m6_defconfig](kernel-3.18/arch/arm64/configs/meizu_m6_defconfig). Listed options
are defconfig requests; they are not a newly generated configuration or a
compilation receipt. Historical M6 bring-up does not certify every branch tip,
and M6T does not inherit M6 hardware results.

| Component | Source implementation | Configuration / integration | Compiled | Working on this branch |
|---|---|---|---|---|
| Display panels | [LCM drivers](kernel-3.18/drivers/misc/mediatek/lcm) | `ili9881p_hd_dsi_txd` | Not rechecked | Not verified for this tip |
| Touchscreen | [MediaTek touch drivers](kernel-3.18/drivers/input/touchscreen/mediatek) | `TOUCHSCREEN_MTK_FT5X26=y` | Not rechecked | Board-variant tests needed |
| GPU | [Mali GPU drivers](kernel-3.18/drivers/misc/mediatek/gpu) | `mali midgard r12p1` | Not rechecked | Rendering / DVFS tests needed |
| Cameras | [Image-sensor drivers](kernel-3.18/drivers/misc/mediatek/imgsensor) | `imx278_mipi_raw ov8856jsl_mipi_raw` | Not rechecked | Camera pipeline tests needed |
| PMIC | [MT6353](kernel-3.18/drivers/misc/mediatek/pmic/mt6353) | `MTK_PMIC_NEW_ARCH=y`, `MTK_PMIC_CHIP_MT6353=y` | Not rechecked | Power / thermal tests needed |
| Audio | [MediaTek ASoC](kernel-3.18/sound/soc/mediatek) | `MT_SND_SOC_6750=y` | Not rechecked | Call / media routes need testing |
| Wi-Fi / Bluetooth | [CONSYS drivers](kernel-3.18/drivers/misc/mediatek/connectivity) | `CONSYS_6755`, `MTK_COMBO_WIFI=y` | Not rechecked | Firmware and radio tests needed |
| Modem transport | [ECCCI](kernel-3.18/drivers/misc/mediatek/eccci) | `MTK_ECCCI_DRIVER=y` | Not rechecked | Depends on modem firmware and Android RIL |
| Accelerometer | [Accelerometer drivers](kernel-3.18/drivers/misc/mediatek/accelerometer) | `MTK_BMA253=y` | Not rechecked | Orientation / variant tests needed |
| Storage | [MediaTek MMC](kernel-3.18/drivers/mmc/host/mediatek) | `MMC_MTK=y` | Not rechecked | I/O / suspend tests needed |

## Build inputs

Use the kernel in `kernel-3.18/`, `ARCH=arm64`, and an absolute
`CROSS_COMPILE` prefix for AArch64 Android GCC 4.9. Select
[meizu_m6_defconfig](kernel-3.18/arch/arm64/configs/meizu_m6_defconfig) and use a separate Kbuild output
directory. The BSP image target is `Image.gz-dtb`; matching M6 board
generation inputs, ramdisk, command line and boot-image geometry are still
required for device integration. A kernel image alone is not a ROM.

No new build or device test was run for this source-map update. Use the matching
Android device tree and board firmware; a common chipset is not a substitute
for matching panel, touch, power and storage resources.

The inherited `README_Kernel.txt` names a Huawei donor target. Use the Meizu
defconfig above for this branch.

## Related branches

- [M6 kernel-only snapshot](https://github.com/nomorecoolnicknames/android_kernel_meizu_m6/tree/main)
- [M6 3.18.140 BSP](https://github.com/nomorecoolnicknames/android_kernel_meizu_m6/tree/m6-linux-3.18.140)
- [M6T board adaptation](https://github.com/nomorecoolnicknames/android_kernel_meizu_m6/tree/m6t-linux-3.18)
- [U10 unapplied patches](https://github.com/nomorecoolnicknames/android_kernel_meizu_m6/tree/u10-linux-3.18-patches)
- [U20 resource fragments](https://github.com/nomorecoolnicknames/android_kernel_meizu_m6/tree/u20-linux-3.18)

## Credits and licensing

Built on Linux, Android and MediaTek BSP work. Original vendor and downstream
authors remain credited in Git history and per-file notices. See
[COPYING](kernel-3.18/COPYING); individual files may carry additional terms.
Device firmware, calibration and Android vendor libraries are separate inputs.

[ReMeizu project status](https://github.com/nomorecoolnicknames/remeizu/blob/main/PROJECT_STATUS.md)

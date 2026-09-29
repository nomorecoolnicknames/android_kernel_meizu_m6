# Meizu U10 · Linux 3.18.140 patch series

Source work for **Meizu U10 / MT6750**. This branch carries two **unapplied**
Goodix touch-driver patches on an M6 BSP base. It is a patch collection, not a
buildable U10 kernel target. The source changes preserve separate board power,
pinctrl, matching and controller-payload requirements.

## Components and integration

| Component | Source implementation | Configuration / integration | Compiled | Working on this branch |
|---|---|---|---|---|
| Goodix resource profile | [Patch 1](Documentation/remeizu/u10-source-series/0001-ccaeac26e116.patch) | Adds default-off `TOUCHSCREEN_MTK_GT9XX_U10_STOCK_RESOURCES` | Not on this base | Not tested |
| Goodix board payload selection | [Patch 2](Documentation/remeizu/u10-source-series/0002-8a0cf944c508.patch) | Requires separate configuration / firmware headers; no donor fallback | Not on this base | Not tested |
| Driver baseline | [Touch driver collection](kernel-3.18/drivers/input/touchscreen/mediatek) | Required `GT9XX_MZ` baseline is absent | Blocked | Not integrated |
| PMIC API dependency | [MT6353 implementation](kernel-3.18/drivers/misc/mediatek/pmic/mt6353) | Profile requires the MT6353 new-architecture backend | Patch not applied | U10 rail sequence not tested here |
| U10 board DTS / DCT | [DTS collection](kernel-3.18/arch/arm64/boot/dts) | No U10 board target selects the profile | Not integrated | Not tested |
| Display / cameras / audio / radios | [Inherited M6 configuration](kernel-3.18/arch/arm64/configs/meizu_m6_defconfig) | M6 settings remain; they are not a U10 configuration | No U10 build | Not established for U10 |

## Using the patches

The patches are ordered `0001` then `0002`; their paths are relative to the
kernel source root, not the outer BSP directory. The current base lacks
`GT9XX_MZ` and the matching Kconfig context. Import and review that exact driver
baseline before porting the series. Do not treat an unsuccessful apply as a
reason to substitute another board's driver or payload.

The optional U10 profile requires `GT9XX_U10_PRIVATE_INCLUDE` to point to a
directory containing `gt9xx_u10_stock_config.h` and
`gt9xx_u10_stock_firmware.h`. Those controller payloads are not supplied here.
No defconfig enables the profile; a U10 DTS/DCT, complete configuration and
hardware tests are still required. An earlier object check used a different
source base and therefore does not validate this public branch.

Display, camera, audio, modem and power support must be reviewed for U10
separately. The inherited M6 configuration is useful source context, not a
statement that those components work on U10.

## Credits and licensing

Built on Linux, Android and MediaTek BSP work. Original vendor and downstream
authors remain credited in Git history and per-file notices. See
[COPYING](kernel-3.18/COPYING); individual files may carry additional terms.
Device firmware, calibration and Android vendor libraries are separate inputs.

[ReMeizu project status](https://github.com/nomorecoolnicknames/remeizu/blob/main/PROJECT_STATUS.md)

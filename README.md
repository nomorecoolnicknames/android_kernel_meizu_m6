# Meizu U20 · Linux 3.18.140 resources

Board-resource work for **Meizu U20 / MT6755**. This branch adds MT6351 PMIC
configuration and BQ24196 charger resource fragments to the M6 BSP. They are
applied source files, but **no complete U20 kernel configuration selects them**.

## Components and integration

| Component | Source implementation | Configuration / integration | Compiled | Working on this branch |
|---|---|---|---|---|
| MT6351 PMIC / PWRAP | [PMIC configuration fragment](kernel-3.18/arch/arm64/configs/u20_pmic_legacy.config) | Selects existing legacy PMIC backend; not merged automatically | Not built on this base | Not tested |
| Legacy PMIC driver | [MT6755 power sources](kernel-3.18/drivers/misc/mediatek/power/mt6755) | Requires `MTK_PMIC_NEW_ARCH` and MT6353 selection to stay off | Not built for U20 | Rail / suspend validation pending |
| BQ24196 charger resources | [Charger DTS fragment](kernel-3.18/arch/arm64/boot/dts/u20-charger-stock-resources.dtsi) | I2C1, address 0x6b; node disabled and not included by a board | No complete U20 DTB | Not tested |
| Charger build requirements | [Charger configuration fragment](kernel-3.18/arch/arm64/configs/u20_charger_required.config) | BQ24196 driver/controller integration still missing | Blocked | Charging / battery policy not verified |
| Board configuration | [Available defconfigs](kernel-3.18/arch/arm64/configs) | No complete U20 target; inherited M6 configuration remains | No U20 kernel build | Not tested |
| Display / touch / cameras / radios | [MediaTek driver collection](kernel-3.18/drivers/misc/mediatek) | Available BSP code is not U20 board integration | Not checked for U20 | Not established |

## Integration order

1. Add the U20 board DTS/DCT and select the legacy MT6351 backend with
   `u20_pmic_legacy.config`. The base M6 defconfig selects MT6353 and must not be
   reused unchanged as a U20 power configuration.
2. Integrate the actual BQ24196 driver and charger controller, then reconcile
   battery, current-limit and thermal policy. Keep the charger node disabled
   until that work is complete.
3. Generate and inspect the configuration and DTB, compile the selected
   objects and kernel, then perform board-specific testing.

Sources are under `kernel-3.18/`; the configuration fragments are requirements,
not ready-to-run defconfigs. They do not supply a complete boot image or a
validated charging implementation. No compilation of this public-base
combination or U20 hardware test has been completed.

## Credits and licensing

Built on Linux, Android and MediaTek BSP work. Original vendor and downstream
authors remain credited in Git history and per-file notices. See
[COPYING](kernel-3.18/COPYING); individual files may carry additional terms.
Device firmware, calibration and Android vendor libraries are separate inputs.

[ReMeizu project status](https://github.com/nomorecoolnicknames/remeizu/blob/main/PROJECT_STATUS.md)

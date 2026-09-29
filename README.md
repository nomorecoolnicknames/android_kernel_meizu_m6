# Meizu U20 kernel resources

Linux 3.18 resource fragments for the MT6755-based Meizu U20 live in
`kernel-3.18/`. They are not selected by a board configuration yet:

- `arch/arm64/configs/u20_pmic_legacy.config` selects the existing MT6351 PMIC
  and PWRAP backend; the MT6353/new-architecture route must remain disabled.
- `arch/arm64/configs/u20_charger_required.config` lists BQ24196 dependencies.
- `arch/arm64/boot/dts/u20-charger-stock-resources.dtsi` describes the charger
  at I2C1 address 0x6b and keeps its node disabled.

Integration still needs the BQ24196 driver and controller, a U20 board DTS,
and validated battery and thermal policy. These fragments are not a complete
U20 kernel configuration and have not been built together on this base.

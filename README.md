# Meizu U10 kernel patches

The two patches in `Documentation/remeizu/u10-source-series/` add an optional
U10 Goodix resource profile and board-specific payload selection to Linux 3.18.
They are **not applied** to this branch: the M6 base lacks `GT9XX_MZ` and the
matching Kconfig context. Import the matching driver baseline before porting
the series; patch paths are relative to the kernel source root.

The profile requires MT6353 support and separately supplied
`gt9xx_u10_stock_config.h` and `gt9xx_u10_stock_firmware.h` through
`GT9XX_U10_PRIVATE_INCLUDE`. No defconfig enables it. A U10 board DTS and
hardware validation are still required; this is not a buildable U10 target.

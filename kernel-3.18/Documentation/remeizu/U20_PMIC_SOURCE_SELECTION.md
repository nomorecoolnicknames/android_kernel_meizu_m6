# U20 MT6351 PMIC source selection

Category: PROPER-FIX, source selection only. Not a full U20 defconfig or bootable
board claim. Base: `693b6a4d81e17e06545a4329064d5d01cd912060`.

## Hypothesis and verified mismatch

The existing external ReMeizu U20 thin defconfig selects
`CONFIG_MTK_PMIC_NEW_ARCH=y` and `CONFIG_MTK_PMIC_CHIP_MT6353=y`. In this tree
those settings select `pmic/mt6353` and `pwrap_hal_v1.o`, while suppressing the
legacy MT6755 MT6351 power objects and header definitions. This conflicts with
the own U20 stock implementation established below. Changing register writes
or inventing a new MT6351 Kconfig symbol is unnecessary for this selection fix.

## Evidence (FACT)

Own raw kernel SHA256:
`8fc5c4103856a81c33352b57b1e18e845c781d68193f464ef80f52c1d12db96f`.
Own boot SHA256:
`411e2b06494dc2e623f3ddd72bf1cee10c28521666e2ea26e3763e47ecf08997`.
Own DTB SHA256:
`e3a213fba771d9d69488680cc6a47d80668c1c18c3b9c9ccf2237951cd69b4ab`.
Reconstructed ELF SHA256:
`847926eb56f968d24293925b70edd419839373b5cdd3d2293f25c279188104d0`;
its LOAD bytes equal the raw input at base `ffffffc000080000`. Symbol addresses
come from that input's kallsyms, not a donor System.map. No stock `.config` was
recovered.

Real Ghidra MCP decompilation of `pmic_mt_init@ffffffc000fb28b4` shows platform
registration of both PMIC drivers. `mtk_regulator_init@ffffffc00046b7f8` checks
the MT6351 E1 CID `0x5110`, uses the MT6351 VMC voltage tables, and calls
`of_regulator_match(..., ffffffc00106f628, 32)` followed by regulator registration.
The MCP memory read of that 1024-byte match table equals the original raw bytes.

All 32 stock LDO descriptor names, enable/voltage flag IDs and enable register
addresses/masks/shifts match this base's `power/mt6755/pmic.c`, `upmu_common.c`
and `mt6755/include/mach/upmu_hw.h`. Descriptor stride `0x208` and enable-field
offset `0x1d0` are established by decompilation, then cross-checked against
named table entries. `vldo28` uses flag 2165, register `0x0aa2`, mask 1, shift 1;
`vldo28_1` uses flag 2168, register `0x0aa4`, mask 1, shift 1. No register values
are introduced or changed by this patch.

Own DT has 32 LDO child nodes, with `vtouch-supply` resolving to `ldo_vldo28` at
2.8 V. Own `tpd_power_on@ffffffc0007f38a0` uses `regulator_enable`; the provider's
`mtk_regulator_enable@ffffffc00046a3e0` obtains the enum from the descriptor and
calls `pmic_set_register_value`. Own `pwrap_read`, `pwrap_wacs2` and
`pwrap_hal_init` symbols corroborate the wrapper path. An MT6353 string in the
DT's fallback-compatible list does not override this compiled implementation.

## Files and purpose

- `arch/arm64/configs/u20_pmic_legacy.config`: select the existing PMIC/PWRAP,
  regulator and OF code, disable the incompatible new-arch/MT6353 route, and
  keep the separate `MTK_LEGACY` board-data mode off. Legacy PMIC architecture
  and legacy board-data mode are different switches.
- This document: source, evidence, integration gates and rollback boundaries.

Apply the fragment through the normal Forge configuration path for an **U20
MT6755** build only. Verify the generated `.config`; do not append contradictory
config lines by hand. The original thin config still names the placeholder
`wt6755_66_sz_l` board. This fragment does not turn that donor DTS into U20 1MA.
No other board's defconfig/defaults are changed and no board selects this
fragment automatically.

## Source-selection checks and remaining gates

The non-building audit evaluates the actual pinned Makefiles and preprocessor
headers in RAM. With NEW_ARCH unset, MTK_PMIC selects `power/mt6755/{pmic,
pmic_irq,upmu_common,pmic_auxadc,pmic_chr_type_det,mt6311,
pmic_initial_setting}.o`; MTK_PMIC_WRAP selects `pwrap_hal.o`. Header preprocessing
exposes the MT6351 declarations. The unmodified MT6353 selection is the control.

Expected next marker: Forge-generated U20 `.config` retains the seven fragment
settings; the build log and produced System.map contain the selected legacy
PMIC/provider/wrapper objects, and the compiled U20 DT binds the own 32-rail map.
Only then proceed to complete kernel linking and a gated boot/readback test.

Remaining gates: U20 1MA DTS/project selection, generated configuration,
actual compiled DTB/System.map, full charger/thermal/battery policy audit,
complete PMIC initialization/interrupt/suspend comparison, and runtime rail
validation. The donor's `power/mt6755/Makefile` unconditionally adds `bq24157/`
and its subdirectory selects `bq24157_charger.o`; own stock kallsyms contains
`bq24196_driver_probe`. This is a separate charger-selection/policy gate, not
evidence that donor charger code is safe to run on U20. It is unchanged here.
This audit proves the regulator mapping and source route, not every
PMIC policy or physical-board behavior. No full compilation, image, flash or
runtime validation was performed.

Rollback: reject this fragment if the generated config re-enables MT6353/new
arch, the selected objects or header register map differ from the audit, the
actual U20 DT/provider mapping differs, or a linked build exposes an unresolved
dependency. Fix the owning integration layer; do not bypass probe failures or
apply raw donor PMIC writes.

Verification commands (from the Meizu fleet repository, after exporting the
two source files into the isolated checkout):

```sh
python3 kernel-port-work/u20-pmic/verify_mapping.py --source kernel-port-work/u20-pmic/kernel-checkout --raw /srv/forge/android/flyme_fw/u20/unpacked/kernel.decompressed --mcp-match-receipt kernel-port-work/u20-pmic/private/regulator-match-table-mcp.json
python3 kernel-port-work/u20-pmic/verify_selection.py --source kernel-port-work/u20-pmic/kernel-checkout --fragment kernel-port-work/u20-pmic/kernel-checkout/arch/arm64/configs/u20_pmic_legacy.config
```

These are read-only source/stock checks, not Kbuild, generated-config or kernel
build verification. MCP receipts and stock binaries stay private. The fleet
lane's `BRINGUP_STATE.md` records their identities and integration gates.

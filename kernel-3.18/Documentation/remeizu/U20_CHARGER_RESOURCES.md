# U20 charger resources — preparation only

FACT: U20 stock `hq6755_66_1ma_m` registers a BQ24196 I2C driver. Its actual
compiled DT node is `/soc/i2c@11008000/swithing_charger@6b`, bus index 1,
7-bit address 0x6b, compatible `mediatek,swithing_charger`. Preserve that spelling.
Real Ghidra MCP connects the stock driver to its probe and OF match table;
the latter uses `mediatek,SWITHING_CHARGER`. This kernel's OF compatibility
comparison is case-insensitive. The proof does not establish hardware runtime.

`arch/arm64/boot/dts/u20-charger-stock-resources.dtsi` records the resource but
keeps it **disabled** and is not included by any board. The existing `i2c1`
label maps to physical controller 0x11008000 in the pinned MT6755 source.
Stock supplies no charger-node GPIO, IRQ or regulator property. That absence
does not prove that the physical charger has no CE pin, IRQ or supply.
Do not copy L681's CE GPIO or another board's battery limits.

`arch/arm64/configs/u20_charger_required.config` records necessary future
source-route settings; it is **not a build-ready configuration** and is not
automatically merged. The stock-proven MT6351 selection is separately defined
by `u20_pmic_legacy.config`. The current donor still selects BQ24157 and an
incorrect controller owner even if `MTK_BQ24196_SUPPORT=y` is requested.

Before enabling the node, port and select the actual BQ24196 C driver and
charger controller, preserve exactly one `chr_control_interface`, verify the
MT6351 charging fields and the U20 battery/temperature policy, and use Forge
to prove the generated config, objects, linked symbols and compiled own DTB.
Only then perform the separately gated boot/readback/runtime loop.

Evidence and executable audit: fleet `kernel-port-work/u20-charger/`:
`BRINGUP_STATE.md`, `stock-source-audit.json`, `evidence-references.json`,
`audit_charger.py`, `check_rejections.py`. Stock raw SHA256:
`8fc5c4103856a81c33352b57b1e18e845c781d68193f464ef80f52c1d12db96f`;
stock DTB SHA256:
`e3a213fba771d9d69488680cc6a47d80668c1c18c3b9c9ccf2237951cd69b4ab`.

Category: PROPER-FIX, resource description only. Expected next result: a
properly selected custom BQ24196 owner with these own resources and verified
policy. Reject integration on conflicting owners, unproven battery policy,
wrong compiled node, missing/failed I2C transactions or unmatched artifacts.
No generated config, custom DTB, linked kernel, charge behavior or boot is
claimed by this preparation checkpoint.

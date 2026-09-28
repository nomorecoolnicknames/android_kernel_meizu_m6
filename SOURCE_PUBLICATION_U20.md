# U20 kernel source publication — 2026-09-29

Publication kind: **applied, inactive source resource fragments**. Original source `7bc05fb3a25c7dfd11bab02056b74f646f6cd660`, original private base `693b6a4d81e17e06545a4329064d5d01cd912060`, existing public base `49707bf9f5df46806b56de498615cd8082f2d206`. The source bases differ; no artifact equivalence is asserted.

The exact two existing PMIC/charger-resource deltas apply cleanly under the public repository kernel-3.18/ prefix. Their five resulting source files equal the original committed files byte for byte. The new public-base combination has not been compiled. PMIC and charger configuration fragments are requirements only, no board automatically includes them, and the BQ24196 device-tree node stays disabled. A working charger C driver/controller, own board DTS and verified battery/thermal policy remain prerequisites.

Only the already implemented, audited source deltas and this publication record are newly exposed. No private Goodix firmware/config headers, stock kernels/DTBs, object outputs, captures, private journals or full private ancestor history are published. Existing public-parent history/content is retained as-is; this publication does not declare that historical repository entirely blob-free or OSU-license-cleared. Linux GPL terms and existing per-file terms continue to apply.

`SOURCE_PUBLICATION_U20.json` records original-to-public commit mapping, patch hashes and the exact apply result. Verification is source delta/hash inspection plus new-object privacy audit and anonymous branch/SHA access. No compiler, phone, cloud or charging action was executed for publication. Neither branch is a boot-ready kernel target.

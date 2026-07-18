# Dead M6 read-only recovery probe — 2026-07-18

This capture note records the fresh live evidence from the original dead Meizu M6
repair setup. The fuller board-death audit is
`/srv/forge/android/meizu_m6/artifacts/BOARD_KILL_ANALYSIS.md`.

## Identity and operation boundary

- **FACT:** local stock preloader
  `/srv/forge/android/meizu_m6/stock-flyme-7.1.2.0G/preloader_wt6750s_66_s11_n.bin`
  is 171264 bytes with SHA-256
  `aef361ca7a144c293987cb0acb766e9a55bfd52b077147c7f3ffd3dd68ecd7a2`.
- **FACT:** its staged west-host copy `/home/gun/m6_preloader.bin` has the same
  SHA-256.
- **FACT:** all device commands in this probe were limited to mtkclient `printgpt`.
  No partition was erased, formatted, or written.

## Fresh timeline

1. **FACT** (`/home/gun/mtk_catch.log`, 2026-07-18 12:27:48–12:28:40
   MSK): the USB device enumerated as MediaTek `0e8d:0003`; the live handshake
   reported HW code `0x326`, target config `0x7`, `BROM mode detected`, and
   ME_ID `7394E71E7FF0A5BB3782C232249135E8`.
2. **FACT** (same fresh log): the first attempt omitted the stock preloader,
   could not obtain DRAM setup, and stopped at `DA hash mismatch (0xc0070004)` /
   `Failed to upload da`; GPT was not read.
3. **FACT:** after staging the hash-matched preloader, a direct
   `printgpt --preloader /home/gun/m6_preloader.bin` missed the transient device
   window and returned the reconnect prompt.
4. **FACT:** a syntax-checked read-only retry loop was started at 12:42:47 MSK;
   its result log is `/home/gun/m6_gpt_readonly.log`.

## Contradictions and limits

- **CONTRADICTION:** earlier notes used “BROM/preloader alive” as one phrase;
  this fresh window was specifically identified by mtkclient as **BROM mode**.
  It proves BootROM execution, not successful preloader execution or eMMC/DRAM
  access.
- **LIMIT:** Android serial `711HEBSR277K5` is unavailable in BROM. The hardware
  code, stock artifact family, and physical repair setup are consistent with the
  dead M6, but the ME_ID has not yet been linked to that Android serial by a
  prior capture. GPT/eMMC identity remains unverified.
- **LIMIT:** no boot image, GPT, pstore, last_kmsg, expdb, or eMMC data was read.

## Competing hypotheses

1. **HYPOTHESIS:** normal storage boot fails before or within preloader. Test by
   reading GPT and preloader regions with a device-compatible service setup and
   comparing hashes with stock; a matching preloader disfavors corruption.
2. **HYPOTHESIS:** the current blocker is only service-loader compatibility and
   eMMC remains healthy. Confirm with a stable GPT read using a verified
   Meizu/MT6750-compatible loader; disconfirm if storage init still fails.
3. **HYPOTHESIS:** unstable power or USB reset cycling causes the transient
   enumeration. Confirm with USB timestamps plus VBUS/VBAT/rail measurements;
   disconfirm if the connection becomes stable with the compatible loader.
4. **HYPOTHESIS:** eMMC or its power path failed. Confirm if DRAM init succeeds
   but storage identification repeatedly fails; disconfirm with repeatable GPT
   and partition-hash reads.

## Next safe command boundary

Keep retries read-only (`printgpt --preloader`) until GPT and stronger device
identity are captured. If the DA check remains the blocker, use only a verified
Meizu/MT6750 service loader matched to the stock firmware. Before any repair
write, back up and hash preloader, GPT, and boot.

## Retry result

- **FACT:** the retry reader ran from 12:42:47 MSK for more than seven minutes while
  the west host still listed USB `0e8d:0003`; `/home/gun/m6_gpt_readonly.log`
  remained 39 bytes containing only its `reader start` line.
- **FACT:** no GPT marker, partition listing, DRAM/eMMC success marker, or new
  contact/error record was captured by that reader.
- **LIMIT:** this is an inconclusive read-only probe, not evidence that eMMC is
  dead. The available USB device node was root-owned and root-group-only, while
  the retry process ran as root; the missing output therefore does not identify
  whether the blocker was USB timing, loader behavior, or storage initialization.
- **NEXT SAFE STEP:** stop this retry instance and do not escalate to any write.
  A later attempt should use a verified compatible service-loader/auth setup and
  capture the complete command output, still limited to GPT/read-back operations.

## 2026-07-18 loader inventory

- **FACT:** the west retry process is no longer present. The USB device still
  enumerates as `0e8d:0003` when queried on west.
- **FACT:** mtkclient identifies HW code `0x326` as
  `MT6755/MT6750/M/T/S` and uses its XFlash configuration; the local checkout
  is commit `a6a7147e92907b2017027ae404b84101444ee502`.
- **FACT:** the stock firmware directory contains the verified 171264-byte
  preloader, but no `.auth`, `.da`, or service-loader artifact.
- **FACT:** the west mtkclient checkout contains generic DA binaries and many
  unrelated preloaders, but no filename or artifact has been verified as a
  Meizu M6 service DA/auth pair.
- **LIMIT:** a generic DA or a preloader from another handset is not an
  identity-preserving recovery path. It must not be selected merely because
  its filename mentions MT6750/MT6755.
- **NEXT SAFE STEP:** obtain a verified Meizu/MT6750 service-loader and auth
  pair, then retry only GPT/read-back and capture the full output. Do not use
  `--stock`, generic DA files, or unrelated preloaders as a substitute for
  that verification.

## 2026-07-18 14:41 MSK controlled retry

- **FACT** (`west`, 2026-07-18 14:41:13 MSK): USB enumerated as
  `0e8d:0003 MediaTek Inc. MT6227 phone`; no mtkclient process was running
  before the retry.
- **FACT:** the local west checkout is
  `/home/gun/mtkclient` at commit
  `a6a7147e92907b2017027ae404b84101444ee502`; `printgpt` exposes the
  `--ptype` and `--preloader` options.
- **FACT:** the following bounded command was run with no write/erase/format
  action:
  `timeout 35 .venv/bin/python -u mtk.py printgpt --noreconnect --ptype kamakiri2 --preloader /home/gun/m6_preloader.bin`
- **FACT:** the command ended with exit status 124 after repeated
  `Couldn't get device configuration` messages and the reconnect hint; no
  `GPT`, partition listing, DRAM success, or eMMC identification marker was
  produced.
- **LIMIT:** this retry does not distinguish USB timing, the selected exploit
  mode, loader behavior, or storage initialization. It is not evidence that
  eMMC is dead and does not establish device identity beyond the USB BROM
  enumeration.
- **NEXT SAFE STEP:** stop guessing payload modes. Recover the exact historical
  successful bypass invocation or obtain a verified Meizu-compatible service
  loader/auth pair, then repeat only GPT/read-back with complete captured output.

## Historical invocation search result

- **FACT** (bounded read-only artifact search, task `ad98c8216ba74a8c6`): no
  primary timestamped record was found that ties the original Android serial,
  BROM `0e8d:0003`, HW code `0x326`, an exact mtkclient command, and a verified
  bypass/read success marker together in one invocation.
- **FACT:** historical transcript matches for the Android serial describe ADB,
  kernel, display, and flash work, not an mtkclient BROM/bypass command. They
  therefore cannot establish the missing invocation.
- **FACT:** a second local mtkclient checkout at `/home/n8n/mtkclient` was found
  at commit `2c9f4d78601e2b223cacfed773a5c4cbb1808189`; this differs from the
  verified west checkout commit recorded above. Neither checkout identity is
  evidence of which revision performed the historical successful operation.
- **REJECTED:** generic
  `/home/n8n/mtkclient/mtkclient/Loader/MTK_AllInOne_DA_mt6590.bin` is not a
  verified M6-specific service DA. Its parser-level support for `0x6755` proves
  only SoC-family coverage, not board compatibility or a successful operation
  on this handset.
- **REJECTED:** `/home/n8n/mtkclient/Loader/Preloader/preloader_M6T.bin` is not
  a substitute for the hash-verified stock preloader. It is only 1696 bytes and
  identifies internally as `preloader_wt6750_66_b_n.bin`, not the stock
  `preloader_wt6750s_66_s11_n.bin`.
- **INFERENCE:** the latest attempts did not reproduce a fully evidenced
  historical bypass context. The unresolved discriminators are USB state at
  command start, exact flags, executable/checkout revision, and payload hashes;
  the available evidence does not select one of them as the cause.
- **NEXT SAFE STEP:** do not broaden filesystem searches or try further payload
  variants blindly. If a narrowly identified shell transcript or saved stdout
  becomes available, compare its command, cwd, checkout revision, USB VID:PID,
  HW code, exploit markers, and first DA/auth transition. Until then, keep the
  device boundary read-only and do not treat generic family loaders as verified.

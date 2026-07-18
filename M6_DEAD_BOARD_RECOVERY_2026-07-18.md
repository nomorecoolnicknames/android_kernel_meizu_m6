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

#!/usr/bin/env bash
set -euo pipefail

SERIAL="${SERIAL:-711HEBSR277K5}"
ADB_HOST="${ADB_HOST:-127.0.0.1}"
ADB_PORT="${ADB_PORT:-15038}"
WAIT_SECS="${WAIT_SECS:-600}"
BOOT_IMG="${1:-/srv/forge/android/export/meizu_m6_artifacts/20260609-1110-m6-rescue-boot86/boot-m6-rescue-knownboot-86-20260609.img}"
EXPECTED_SHA="${2:-1071cafdaf3e0c5c43266946f18ca0f8b4496f95e5f5ed3a6110034084187857}"
TAG="${3:-rescue-boot86}"

A=(adb -H "$ADB_HOST" -P "$ADB_PORT" -s "$SERIAL")
CAP_ROOT="${CAP_ROOT:-/srv/forge/android/meizu_m6/captures}"
STAMP="$(date +%Y%m%d-%H%M%S)"
CAP="$CAP_ROOT/${STAMP}-m6-${TAG}-${SERIAL}"

mkdir -p "$CAP"

if [[ ! -f "$BOOT_IMG" ]]; then
	echo "missing boot image: $BOOT_IMG" >&2
	exit 2
fi

actual_sha="$(sha256sum "$BOOT_IMG" | awk '{print $1}')"
if [[ "$actual_sha" != "$EXPECTED_SHA" ]]; then
	echo "boot image sha mismatch: expected $EXPECTED_SHA got $actual_sha" >&2
	exit 3
fi

deadline=$((SECONDS + WAIT_SECS))
while (( SECONDS <= deadline )); do
	line="$(adb -H "$ADB_HOST" -P "$ADB_PORT" devices -l | awk -v s="$SERIAL" '$1 == s {print}')"
	printf '%s %s\n' "$(date +%H:%M:%S)" "${line:-absent}" | tee -a "$CAP/adb-presence-poll.txt"
	if [[ -n "$line" ]]; then
		break
	fi
	sleep 2
done

if [[ -z "${line:-}" ]]; then
	echo "device $SERIAL did not appear within ${WAIT_SECS}s" >&2
	exit 4
fi

"${A[@]}" wait-for-device
"${A[@]}" root > "$CAP/adb-root.txt" 2>&1 || true
sleep 2
"${A[@]}" wait-for-device

"${A[@]}" shell 'id; uname -a; getprop sys.boot_completed; getprop ro.bootmode; getprop ro.boot.bootreason; cat /sys/class/power_supply/battery/capacity 2>/dev/null; cat /sys/class/power_supply/battery/status 2>/dev/null; sha256sum /dev/block/platform/mtk-msdc.0/by-name/boot 2>/dev/null' > "$CAP/pre-flash-identity.txt" 2>&1 || true
"${A[@]}" shell 'cat /proc/aed/reboot-reason 2>/dev/null || true' > "$CAP/proc-aed-reboot-reason.txt" 2>&1 || true
"${A[@]}" shell 'cat /proc/last_kmsg 2>/dev/null || true' > "$CAP/proc-last_kmsg.txt" 2>&1 || true
"${A[@]}" shell 'dmesg 2>/dev/null || true' > "$CAP/dmesg-before-rescue-flash.txt" 2>&1 || true
"${A[@]}" shell 'ls -la /sys/fs/pstore 2>/dev/null || true' > "$CAP/pstore-list.txt" 2>&1 || true
"${A[@]}" pull /sys/fs/pstore "$CAP/pstore" > "$CAP/pstore-pull.txt" 2>&1 || true

remote="/cache/$(basename "$BOOT_IMG")"
if ! "${A[@]}" push "$BOOT_IMG" "$remote" > "$CAP/adb-push-cache.txt" 2>&1; then
	remote="/data/local/tmp/$(basename "$BOOT_IMG")"
	"${A[@]}" push "$BOOT_IMG" "$remote" > "$CAP/adb-push-data-local-tmp.txt" 2>&1
fi

"${A[@]}" shell "sha256sum $remote" > "$CAP/device-remote-boot-sha256.txt" 2>&1
grep -q "$EXPECTED_SHA" "$CAP/device-remote-boot-sha256.txt"

"${A[@]}" shell "dd if=$remote of=/dev/block/platform/mtk-msdc.0/by-name/boot bs=1048576; sync" > "$CAP/device-dd-flash.txt" 2>&1
"${A[@]}" shell 'sha256sum /dev/block/platform/mtk-msdc.0/by-name/boot' > "$CAP/device-boot-readback-sha256.txt" 2>&1
grep -q "$EXPECTED_SHA" "$CAP/device-boot-readback-sha256.txt"

printf 'rescue flash readback matches %s\n' "$EXPECTED_SHA" > "$CAP/device-boot-readback-verified.txt"
"${A[@]}" reboot > "$CAP/adb-reboot-after-rescue.txt" 2>&1 || true

echo "$CAP"

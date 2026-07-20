#!/bin/bash
# LOS 15.1 meizu_m6 full ROM build, run inside androidforge/build-env:android-8.1.
# Assumes /src is the synced tree and /src/out is a host NVMe mount.
set -o pipefail

JOBS="${BUILD_JOBS:-6}"
LOG=/src/out/build-m6-$(date +%Y%m%d-%H%M%S).log
mkdir -p /src/out
echo "=== M6 LOS 15.1 build start $(date -Is) ===" | tee "$LOG"

cd /src || exit 1

# ccache
export USE_CCACHE=1
export CCACHE_DIR="${CCACHE_DIR:-/ccache}"
ccache -M "${CCACHE_MAXSIZE:-50G}" >/dev/null 2>&1
ccache -s 2>&1 | head -5 | tee -a "$LOG"

echo "--- java ---" | tee -a "$LOG"
java -version 2>&1 | head -2 | tee -a "$LOG"

# LOS 15.1 build system wants python2 as `python`
if ! python --version 2>&1 | grep -q '^Python 2'; then
    mkdir -p /tmp/py2bin && ln -sf /usr/bin/python2 /tmp/py2bin/python
    export PATH=/tmp/py2bin:$PATH
    echo "pinned python -> $(python --version 2>&1)" | tee -a "$LOG"
fi

set +u
source build/envsetup.sh 2>&1 | tail -5 | tee -a "$LOG"
lunch lineage_meizu_m6-userdebug 2>&1 | tail -25 | tee -a "$LOG"
rc=${PIPESTATUS[0]}
if [ "$rc" != "0" ]; then
    echo "LUNCH-FAILED rc=$rc" | tee -a "$LOG"; exit 1
fi

echo "=== building with -j$JOBS ===" | tee -a "$LOG"
mka bacon -j"$JOBS" 2>&1 | tee -a "$LOG"
rc=${PIPESTATUS[0]}

echo "=== build end $(date -Is) rc=$rc ===" | tee -a "$LOG"
ls -l /src/out/target/product/meizu_m6/*.zip /src/out/target/product/meizu_m6/boot.img 2>&1 | tee -a "$LOG"
[ "$rc" = "0" ] && echo "M6-BUILD-SUCCESS" | tee -a "$LOG" || echo "M6-BUILD-FAILED" | tee -a "$LOG"
exit "$rc"

#!/bin/bash
# Restart on the current build, load Baseline-BP, print ts3native.log, GC counters, then an in-load A/B profile.
# Edit the --tag/--ab line for the switches under test (last used: watchdog,spotspread as bp-wab).
cd /c/Users/<user>/IdeaProjects/sims-3-modernization
BIN="/c/Program Files (x86)/Steam/steamapps/common/The Sims 3/Game/Bin"
timeout 180 node tools/harness/cycle.ts --restart | tail -1
python tools/probe/ts3probe.py serve --launch > probe-out/serve-native.log 2>&1 &
timeout 590 node tools/harness/cycle.ts --save Baseline-BP.sims3 | tail -1 || exit 1
echo "== ts3native.log"; cat "$BIN/ts3native.log" 2>/dev/null || echo "NO ts3native.log"
node tools/probe/gc-read.mjs | sed 's/^/start: /'
rm -f probe-out/profile/bp-wab-*
timeout 1000 node tools/harness/profile-world.ts --tag bp-wab --minutes 12 --slice 3 --ab watchdog,spotspread 2>&1 | grep -E "stuck Sims|done|on$|off$"
node tools/probe/gc-read.mjs | sed 's/^/end: /'
node tools/probe/profile-compare.mjs bp-wab bp-nab bp-all | sed -n 1,11p

#!/bin/bash
# Restart on the current build, load a save, run verify-shortcut.ts:
#   bash tools/harness/restart-verify.sh <names,comma,separated> <save.sims3> [tries] ["extra verify args"]
cd "$(dirname "$0")/../.."
timeout 180 node tools/harness/cycle.ts --restart | tail -1
python tools/probe/ts3probe.py serve --launch > probe-out/serve-verify.log 2>&1 &
timeout 590 node tools/harness/cycle.ts --save $2 | tail -1 || exit 1
node tools/harness/ag.mjs patches | grep -o '"applied":\[[^]]*\]'
timeout 330 node tools/harness/verify-shortcut.ts --name $1 --tries ${3:-20} ${4:-}

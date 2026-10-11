#!/bin/bash
# usage: tools/ghidra/run-headless.sh <Script.java> <args...>   (read-only headless run against TS3 project)
cd "$(dirname "$0")/../.."
GHIDRA_MAXMEM=16G JAVA_HOME=~/.jabba/jdk/openjdk@1.21 PATH=~/.jabba/jdk/openjdk@1.21/bin:$PATH timeout 1800 ~/tools/ghidra_12.1.4_PUBLIC/support/analyzeHeadless.bat "$(cygpath -w $PWD/game-copy/ghidra)" TS3 -process TS3W.exe -noanalysis -readOnly -scriptPath "$(cygpath -w $PWD/tools/ghidra)" -postScript "$@" > game-copy/ghidra-last.log 2>&1
grep -E '> |ERROR' game-copy/ghidra-last.log | grep -v WARN | cut -c1-200 | tail -5

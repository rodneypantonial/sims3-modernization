# Requirements for contributors

What you need to reproduce our measurements and work on fixes. Players only need the game, Windows and Node.js (README "Quick start: players").

## Hardware

**Reference machine** (all results so far): Alienware m15 R6 laptop, Intel i7-11800H (8 cores / 16 threads, 11th gen), 32 GB RAM, RTX 3070 Laptop GPU, NVMe SSD, Windows 11.

| | Minimum | Recommended |
|---|---|---|
| CPU | 4 cores / 8 threads, x64 | 8 cores / 16 threads or more (the game, the probe, profilers and sub-agents run at once) |
| RAM | 16 GB | 32 GB (Ghidra analysis of TS3W.exe used a 16 GB heap; the game itself is 32-bit and uses at most 4 GB) |
| GPU | Vulkan-capable (DXVK needs Vulkan 1.3) | any current discrete GPU |
| Disk | SSD with room for the game, its packs, and game copies, the Ghidra project and captures | NVMe with 100 GB+ free |

Results from other CPUs and GPUs are welcome: the sim thread is single-threaded, so per-core speed decides most of our numbers. Always report your machine with your results. On Intel 12th gen and newer the Steam exe fails to start without a fix (S3SS's CPU thread optimization or LazyDuchess's patcher, docs/01 §E); our reference machine (11th gen) does not need it, so that path is untested here.

## Software

- Windows 10 or 11, x64.
- The Sims 3, Steam edition, patch 1.67. Our results use all expansion and stuff packs; with fewer packs, note which ones you have.
- Build and test tools: see docs/BUILD.md (Node.js 22.18+, .NET Framework `csc.exe`, MSVC x86 build tools, Python 3 with `frida` 17 and `frida-tools`, Git Bash).
- Reverse engineering (optional): Ghidra 12 with JDK 21, `ilspycmd`, REA (MCP server over Ghidra and .NET analysis), x32dbg.

## Game license

Each contributor needs their own legal copy of The Sims 3 and its packs (one copy per person and machine). Do not share game files, extracted DLLs, decompiled code or saves; the repo never contains them. Our tools read your own install and keep copies under the git-ignored `game-copy/`.

## AI model

Most of this project was done with an AI coding agent driving the tools, and the repo is set up for that (`AGENTS.md`, `CLAUDE.md`, `PLAN.md`).

- **Recommended:** Claude Code with Claude Opus 5.5, or an equally capable frontier model, on a plan with high usage limits (Claude Max tier or equivalent).
- **Why:** reverse-engineering sessions are long (hours of disassembly, decompiled C# and measurements in one context); the work runs parallel sub-agents (research and code while the game runs a serial test); the docs and `PLAN.md` are large and must be read before acting; and wrong conclusions are expensive (a wrong address or an unverified assumption crashes the game or corrupts a save). Smaller models lose track of addresses and verification state.
- Smaller models are fine for docs, harness scripts and reviewing results.
- You are responsible for what your agent commits: the rules in `AGENTS.md` (no EA files, verified vs inferred, one change at a time) apply to agent output as to your own.

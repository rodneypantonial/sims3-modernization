# ts3agent: in-game test agent

Script mod that lets our tools read and drive a running game. Forked from
[Sims3-MCP](https://github.com/Critical-Reynolds/Sims3-MCP) (commit 7599b42, MIT, Copyright (c) 2026 Critical-Reynolds; see `LICENSE`).

- Transport: a 1 MB `byte[]` mailbox inside the game, found by its magic header and polled once per sim tick (`src/Mailbox.cs`). Our Frida probe (`tools/probe`) reads and writes it in-process.
- Build: `node tools/harness/build-agent.ts`. It compiles against the game DLLs extracted to `game-copy/assemblies` (never committed) and packs `build/ts3agent.package`.

Changes from upstream are listed in git history.

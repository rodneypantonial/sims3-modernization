# EA app edition on the EA laptop (measured 2026-11-02)

Facts from the EA laptop. Tags: [verified] = observed here, [inferred] = not confirmed.

## Install
- EA app edition, TS3.exe 1.69.47.024017 (`Version.tag` GameVersion).
- Bin: `C:\Program Files\EA Games\The Sims 3\Game\Bin`. Exe sha256 equals `game-copy/ea/TS3.exe`.
- PE timestamp 6707155C (2024-10-09), SizeOfImage 0xEB5000. Sections: .text .rdata .data .tls .rsrc .ooa.
- On disk the import table names only `Core/Activation.dll` (DRM wrapper). Real imports resolve after decryption at run time.

## Launch chain [verified]
- The EA app starts `Sims3Launcher.exe -test` (installerdata.xml `<runtime><launcher>`).
- A direct TS3.exe start, also with an EA launch session open, bounces to EALauncher.exe with `origin2://game/launch/?offerIds=sims3_dd,1002441,70384_CE,70384_Unrevised`. Only the launcher appears.
- The launcher is a .NET WinForms app (`Sims3Launcher.ini`: GameProcessName = TS3). Its Play button is owner-drawn, not reachable by UI Automation (only Pane controls).

## Harness launch [verified]
- Swap `TS3.exe` over `Sims3Launcher.exe` (backup `Sims3Launcher.exe.orig`, sha256 prefix 845199D3ED66), then start `EALauncher.exe <URI>`.
- The game then runs as process `Sims3Launcher.exe`: window "The Sims 3" after ~11 s, parent EADesktop.exe, command line `-test`.
- An EA app repair/update restores the launcher: re-swap.
- Settings (user env): `TS3_BIN`, `TS3_EXE=Sims3Launcher.exe`, `TS3_LAUNCH=ea`. See `docs/TESTING.md`, `tools/harness/game-exe.ts`.

## ASI loader [verified]
- Ultimate ASI Loader v10.0.2 Win32 (`dinput8.dll` from Ultimate-ASI-Loader.zip, sha256 63b465dbc1bf9c5ab4e33c3ccad099d9edb702f2c51587ac8d4f196497396437) placed as `Bin\wininet.dll` loads.
- The game maps both `Bin\WININET.dll` and `system32\WININET.dll`.
- `ts3native.asi` 0.36.152 (CI artifact of commit b99f370) logs `host Sims3Launcher.exe base 0x00400000`, then `SKIP all: host is not TS3W.exe at 0x400000`.
- The `host` line now also prints PE timestamp and SizeOfImage (Steam 1.67 = 52DEC247 / 0xE5A000, EA app 1.69.47 = 6707155C / 0xEB5000).

## Windowed
- `Documents\Electronic Arts\The Sims 3\Options.ini`: `fullscreen = 0`, `resolution = 1600 900 0` (laptop screen 1920x1200). The EA launch cannot pass `-w`.

## Permissions
- The Bin folder is admin-only by default. One-time, elevated: `icacls "<Bin>" /grant "<user>:(OI)(CI)M" /T`.

## Open
- Mod-version popup ("unofficial game modification that does not match your current version", Quit/Continue) blocks start-up with the 1.67 Mods set (separate doc in progress).
- ts3native stays in SKIP until the 1.69 port (docs/68 section 7, `tools/sigmap.ts`, `tools/probe/dump-image.py`).
- Effect of the `-test` argument on the game: unknown.

# 16. Online features: what the game contacts, what can block, how to turn it off

Question (Rodney, 2026-10-13): does The Sims 3 waste time going online while we play, and how do we turn all of it off?

Short answer: TS3W.exe has a lot of online code (store, Sims3.com social "Crossroads", Exchange uploads, telemetry, content-patch check, launcher pages, UPnP and an old in-game-ads SDK). With our current settings almost all of it is idle during play. Most hosts still answer (EA kept `*.thesims3.com` running), so a call that does happen is a real 0.5–1 s internet round trip, not a fast failure. The only online-related call on the **sim thread** we found in steady play is `InternetGetConnectedState`, a local check with no network traffic. The downloads use synchronous WinINet, and we could not tell from the code alone which thread they run on. The `/net` probe run has to answer that.

Tags: **[verified]** = seen in the binary, decompile, registry or local files here; **[community]** = forum or mod page (linked); **[inferred]** = reasoning, to confirm by measurement. Target: TS3W.exe Steam 1.67 (`game-copy/ghidra` project, see docs/15).

## 1. Network APIs in the binaries

| Module | Imports | Notes |
|---|---|---|
| `TS3W.exe` | WININET (InternetOpenA, InternetConnectA, HttpOpenRequestA, HttpSendRequestA/ExA, InternetReadFile/WriteFile, InternetGetConnectedState, cookies, status callback), WS2_32 (mostly by ordinal: socket, connect, gethostbyname, select, send/recv, WSA* overlapped), IPHLPAPI (GetAdaptersInfo/Addresses) | **No WinHTTP** [verified, PE import scan] |
| `TSLHost.dll` (launcher host) | WSOCK32, WININET, WS2_32, IPHLPAPI | Holds `TSLHost/TelemetryUploadJob` [verified] |
| `Sims3LauncherW.exe` | .NET assembly | launcher only [verified] |
| `Sims3Launcher.exe`, `S3Launcher.exe`, `TSLHelper.exe`, `Sims3GDF.dll`, `EAWebkit.dll` | no network imports or game URLs | [verified] |

Code that uses the imports [verified, Ghidra xrefs, `NetXrefs.java` in the session scratchpad]:
- `gethostbyname` and `connect` are used by: the **IGA RADSDK** in-game ads SDK (raw HTTP to `radsdk4.igaworldwide.com`, LuaSocket 2.0.2, `IGACacheRefresherThread`), EA DirtySock **UPnP** (SSDP `WANConnectionDevice`, `AddPortMapping`, `GetExternalIPAddress`, skipped when the args contain `-noupnp`), and Mono's own socket init (`Couldn't initialise networking`, i.e. `WSAStartup` only).
- WinINet is reached through gSOAP's `wininet-2.0` plugin (`0x9f53e0`, used by the SOAP/REST services) and the store and patch downloader `SimpleFileDownload` (`0x87a330`). That downloader first calls `InternetGetConnectedState`, then sends `CodeVersion`, `Locale`, `GameVersion`, `OsVer`, `StoreEnabled` and a MAC-derived `UniqueToken`, then reads the body in a **synchronous 1 KB read loop** that only stops early when a cancel flag is set.
- `IsConnectedToInternet()` (script internal call `0x745c40`) jumps to `0x9f5470`, which only calls `InternetGetConnectedState`. It reads Windows' cached network state and sends nothing [verified decompile].

## 2. Online features, hosts, and whether they are dead

Host status measured 2026-10-13 from this laptop with a DNS lookup plus a bare TCP connect only (no HTTP request, nothing downloaded; `hosts.js` in the session scratchpad) [verified].

| Feature | Code (TS3W unless noted) | Host(s) | Host status | When it runs | Off switch we have |
|---|---|---|---|---|---|
| In-game store / Shop mode: featured items, sets, SimPoints balance, FSI params, main-menu promo | `DownloadStoreFeaturedItemsJob` (`%s/featureditems?start=%d&rows=%d`), `DownloadStoreSetsJob`, `DownloadStoreSimPointsBalanceJob` (`/user/balance?token=`), `DownloadFSIParamsJob`, `DownloadStoreFeaturedMainMenuJob` [verified] | `store.thesims3.com`, `igs.thesims3.com` | **alive** (DNS + TCP 80/443 open, ~220 ms RTT) | main menu, catalog, featured-item TNS [inferred] | `enableingamestore=0` + S3SS `StoreFeaturedItemsDisablePatch` (on) |
| Store manifest / Download Dashboard | `OnlineFeaturesHost_GetStoreManifestFile` (`0x745e50`), called only by `DownloadDashboard` UI [verified] | launcher pipe / store | alive | only when the dashboard opens | don't open it |
| Content patch check (store content patch) | `ContentPatchUpdater::CheckWebForNewContentPatch` (`%s/contentpatch?locale=&country=`, `contentPatchRedirect.txt`) through `SimpleFileDownload` [verified] | content-patch base URL from config | EA stopped serving store patches in Dec 2017 [community, docs/07 §2.6] | start-up [inferred] | none in Options.ini; firewall/hosts |
| Sims3.com social "Crossroads": login, wall, friends, gifting, badges, challenges, Passport, FB/Twitter share | `SocialFeatures`, `OnlineFeatures.Login`, `BlazeWal*Thread` (17 named worker threads), `DynamicChallengeManager.OnSignedInToCrossroads` downloads challenges [verified] | `xroads.thesims3.com`, `mypage.thesims3.com`, `wal.tools.gos.ea.com` (Blaze WAL) | **alive** | only after a login: an auto-login from the launcher cookie (`S3L_CK_DB_RememberPassword=true` + saved user id/password), or the user clicks Log in [verified C#, `SocialFeaturesUIManager`] | `requireloginbeforeload=0` (already set), never log in, untick "Keep me logged in" |
| "Connect" notifications (TNS) | `ReceiveConnectTNS` → `ConnectHomeNotification`, Connected* TNS [verified] | Crossroads | alive | in-world, if logged in | `receiveconnecttns=0` (set) |
| Exchange upload/download (Sims, lots, Sims3Pack) | gSOAP `Sims3UploadDownloadService`, `exchange.webservice.thesims.ea.com` [verified strings] | `sims3web01.sig.max.ad.ea.com`, `exchange.webservice.thesims.ea.com`, `llnw.*` | **dead** (NXDOMAIN, fails in < 0.2 s) | only on user action | don't use |
| Telemetry | Script `OnlineFeatures.LogTelemetryEvent` → native `Misc/Network/Telemetry/Manager` queues events and writes a `Sims3Telemetry` XML file [verified]; the upload is `TSLHost/TelemetryUploadJob` (launcher) [verified string; upload gating inferred] | `TelemetryServerURL` from config | unknown | the C# `TelemetryStats` collector has **no script caller** for `Startup()`, so its event listeners never register [verified grep]; a few direct calls remain (load-file name in `ToInWorldState`, main menu) | `enabletelemetry=0` (set) **and** registry `HKLM\SOFTWARE\WOW6432Node\Sims(Steam)\The Sims 3\telemetry = 0` (already 0 here [verified]; the Options dialog reads the registry value, `DeviceConfig_IsRegistryTelemetryEnabled` = `RegQueryValueExW(... "Telemetry")` [verified]) |
| In-game ads (IGA RADSDK billboards) | `Services/RADSDK/*`, `IGACacheRefresherThread`, `Software\IGA` (HKCU) [verified] | `radsdk4.igaworldwide.com` | **dead** (NXDOMAIN; IGA went bankrupt around 2010 [community-known, not linked]) | its own thread [verified name] | ads are off when ini `[Impressions] Disable` is set or the `disableInGameAds` option is given (app start-up `0xecb600` sets the RADSDK enable flag from them) [verified decompile; exact ini file inferred]. Here `HKCU\Software\IGA` is missing and `IGACache\` is empty, so the SDK has never run on this machine [verified local] |
| UPnP port mapping (DirtySock, for Blaze) | `0xf73760`: unless the args contain `-noupnp`, runs SSDP discovery and AddPortMapping [verified] | LAN router (UDP multicast 239.255.255.250:1900) | n/a | when the net stack starts, i.e. at Blaze login [inferred] | `-noupnp` on the command line |
| Launcher home/news pages, `countries.xml`, `launcher.xml` | `Sims3LauncherW.exe` + TS3W (`launcher.xml` at `0x8873f0`) [verified strings] | `launcher.thesims3.com`, `launcherapi.thesims3.com`, `easo.ea.com` | **alive** | the launcher, before play | our harness starts `TS3W.exe` directly, so the launcher never runs [verified `ts3probe.py launch`] |
| In-game browser (Store, My Page, Exchange tabs) | `SIGS/BrowserWin`, `OverrideInGameBrowserUrl*`, `OfflinePages = community.html,exchange.html,…` [verified] | `mypage.thesims3.com`, `www.thesims3.com` | alive | only when opened | don't open it |
| Version check | `-noversioncheck` skips `FUN_009b0720` at start-up [verified] | probably local (pack versions) [inferred] | — | start-up | `-noversioncheck` (not needed) |
| Origin / EA app | Origin-required strings and `origin://LaunchGameRtP` [verified] | — | Steam SKU does not need it [inferred: game runs here without EA app] | — | — |
| Steam | no `steam_api` import [verified] | — | — | — | — |

## 3. What can block, and on which thread

| Call | Thread | Cost | Evidence |
|---|---|---|---|
| `InternetGetConnectedState` from `IsConnectedToInternet()` | **sim thread** (Mono script). Hot callers: interaction `Test()`s that autonomy runs (`AlchemyPotion` drink test, `Sims4VideoWallProgram`), `DynamicChallengeManager`, Connect TNS, `SocialFeaturesEventSystem`; about 80 call sites, most in UI [verified grep] | local, no network; normally microseconds [inferred, measure] | decompile `0x9f5470` |
| `SimpleFileDownload` (store, featured items, content patch) | job threads, `SIGS/*Job` [inferred from names]; not proven to be off the main thread | full HTTP request to a live host: DNS + TCP (~220 ms here) + TLS + response, so **0.5–1+ s per request**, synchronous | decompile `0x87a330`; host test |
| Blaze/Crossroads | 17 `BlazeWal*Thread` workers; the C# side queues calls and gets callbacks [verified] | only when logged in | strings + `SocialFeaturesEventSystem` |
| IGA, UPnP | own threads / net stack [inferred] | dead host or local LAN; a NXDOMAIN fails fast, but `gethostbyname` is synchronous and can stall seconds on a bad DNS server [inferred] | xrefs |
| Telemetry | in process only a queue + local XML file; the upload is in the launcher [inferred] | small | decompile `0x893460`, `0x892cd0` |

Dead hosts are **not** the problem: their names no longer resolve (NXDOMAIN), so calls fail in about 0.1 s. The risk is the opposite: **the store and Crossroads hosts are still alive**, so any request that does go out costs a real round trip.

## 4. Every off switch

| Switch | Where | Effect | Tag |
|---|---|---|---|
| `enableingamestore = 0` | `Options.ini [options]` | Shop mode/store off; sent as `StoreEnabled` on downloads | set; [verified key in code], benefit [community: [Steam guide thread](https://steamcommunity.com/app/47890/discussions/0/3377008022029487679)] |
| `enabletelemetry = 0` | `Options.ini` | telemetry option | set; [verified key] |
| `telemetry = 0` (DWORD) | `HKLM\SOFTWARE\WOW6432Node\Sims(Steam)\The Sims 3` | the value native telemetry and the Options dialog actually read | already 0 [verified] |
| `receiveconnecttns = 0` | `Options.ini` | no Connect notifications | set; [verified key] |
| `requireloginbeforeload = 0` | `Options.ini` | no login prompt before load | already 0 [verified local] |
| Don't log in; untick "Keep me logged in" (Options → Network) | launcher cookie `S3L_CK_DB_RememberPassword` | stops auto-login, so no Blaze threads, challenge download, wall polling or UPnP | [verified C#] |
| S3SS `StoreFeaturedItemsDisablePatch` | `S3SS` config | blocks the featured-items download | on; [community: S3SS README] |
| `-noupnp` | TS3W command line | skips UPnP discovery and port mapping | [verified]; harmless to add |
| `disableInGameAds` option / `[Impressions] Disable = true` | command line or ini (Sims3.ini/Default.ini family) | turns the IGA SDK off | [verified in code; ini location inferred] |
| Skip the launcher (start `TS3W.exe`) | harness | no launcher news, `launcherapi`, launcher telemetry upload | [verified harness]; [community: [MTS performance guide](https://modthesims.info/t/525052) "Bypass the Launcher", "Don't use Online options", "disconnecting from the internet"] |
| Windows Firewall outbound block for `TS3W.exe` (and `Sims3LauncherW.exe`) | `netsh advfirewall firewall add rule name="TS3 offline" dir=out action=block program="<Bin>\TS3W.exe"` (admin) | every request fails at once (no DNS wait, no round trip); the Steam client checks ownership, so the game still starts | [community: EA forum advice to block it in the firewall, found via search, page returns 403 to us]; startup effect [inferred] |
| hosts file: `0.0.0.0` for `store.thesims3.com igs.thesims3.com xroads.thesims3.com mypage.thesims3.com launcher.thesims3.com launcherapi.thesims3.com www.thesims3.com thesims3.ea.com wal.tools.gos.ea.com easo.ea.com` | `C:\Windows\System32\drivers\etc\hosts` (admin) | the same for these hosts only, system-wide | [inferred]; no community source found |
| Unplug / airplane mode | — | `InternetGetConnectedState` returns false, so the C# code skips most online calls | [community: MTS guide]; [verified that the C# checks it] |

Not switches: NRaas has no "offline" option we found. Owned store content works without the store (it is in `.package`/Sims3Pack files) [inferred].

## 5. Recommendation (minimal, safe)

1. Keep what we have: `enableingamestore=0`, `enabletelemetry=0`, `receiveconnecttns=0`, S3SS `StoreFeaturedItemsDisablePatch`, `requireloginbeforeload=0`, launching `TS3W.exe` directly. Registry `telemetry=0` is already set; the installer could assert it (read-only check) [inferred useful].
2. Add `-noupnp` to the harness launch args (one string, no risk) [verified switch].
3. Make sure nobody is logged in (no auto-login cookie). This is the switch that keeps all Crossroads/Blaze work idle.
4. Only if `/net` shows outbound calls during play: add the **firewall outbound block** for `TS3W.exe` as an opt-in `fixes.json` entry (`target: firewall`, uninstall removes the rule). It is reversible, scoped to the game, and makes any leftover call fail at once. We prefer it over the hosts file, which is system-wide and has to track host names.
5. Don't patch `IsConnectedToInternet` to return false unless `/net` shows `InternetGetConnectedState` costing real sim-thread time. It changes script behaviour (offline dialogs, gated interactions) for a probably tiny gain [inferred].

## 6. How to measure what's left (`/net`)

The probe's `/net?start=1` (agent.js) hooks the system ws2_32/wininet/winhttp exports and counts calls, total and worst ms, and sim-thread calls per API and target.
- Run: start the hook before the main menu (launch with `ts3probe.py serve --launch`, then `/net?start=1` right away), load a world, play 5 min at speed 3, then read `/net`. Repeat with `-noupnp`, and once with the firewall rule, for A/B.
- What to read: any `connect`/`gethostbyname`/`HttpSendRequestA` row (= something still goes online; the target names the feature, see §2). Look at `onSimThread` and `maxMs` for each. Then look at `InternetGetConnectedState` `n` and `ms`, the sim-thread cost of the autonomy checks.
- Gaps to close in agent.js: add `HttpOpenRequestA`, `HttpSendRequestExA`, `InternetWriteFile` (wininet) and `sendto`, `recvfrom`, `select`, `WSAConnect` (ws2_32; UPnP/SSDP and the IGA socket code use these). The `InternetGetConnectedState` hook fires from autonomy tests, so check that its own overhead doesn't distort the run. If `n` is large, count without timing.
- Pass criterion: during play, zero network calls on the sim thread and none with `maxMs` > 50. Otherwise, apply step 4.

## Sources

- Binaries: `Game\Bin\TS3W.exe` (PE import and string scan, Ghidra project `game-copy/ghidra`: decompile of `0x745b70`, `0x745c40`→`0x9f5470`, `0x745e50`, `0x745eb0`, `0x746ba0`, `0x7d04a0`, `0x87a330`, `0x890eb0`, `0x892cd0`, `0x893460`, `0x994c40`, `0x9e6350`, `0xecb600`, `0xf4f2d0`, `0xf73760`). Script side: `game-copy/decompiled` (`OnlineFeatures`, `SocialFeatures`, `SocialFeaturesUIManager`, `OptionsDialog`, `OptionsModel`, `TelemetryStats`, `DynamicChallengeManager`, `SocialFeaturesEventSystem`, `Gameflow`).
- Local state: `Documents\Electronic Arts\The Sims 3\Options.ini`, `IGACache\`, `FeaturedItems\` (both empty), registry `HKLM\SOFTWARE\WOW6432Node\Sims(Steam)\The Sims 3`.
- Community: [MTS "Improving Sims 3 Game Performance"](https://modthesims.info/t/525052); Steam threads [3377008022029487679](https://steamcommunity.com/app/47890/discussions/0/3377008022029487679) and [2960544187641601331](https://steamcommunity.com/app/47890/discussions/0/2960544187641601331) (turn off in-game store and online notifications); EA forum threads on online-features lag and firewall blocking ([11825050](https://forums.ea.com/discussions/-/-/11825050), [net-dies](https://forums.ea.com/discussions/the-sims-3-technical-issues-en/net-dies/13590925); 403 to our fetcher, known only from search snippets); [S3SS README](https://github.com/sims3fiend/Sims3SettingsSetter) (Disable Store Featured Items Download); store patch shutdown: docs/07 §2.6.

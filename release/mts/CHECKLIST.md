# Friday 2026-10-23: v0.2.0 on ModTheSims

In order. MTS comes **after** the GitHub release is published; the MTS files are the GitHub release files, byte for byte.

## A. Before MTS (repo, Claude)

1. **GitHub release is live** (docs/RELEASE.md "Publishing"): tag `v0.2.0` on the public repo, release published with `ts3native-v0.2.0.zip` and `ts3fixes.package`. Note the release URL.
2. **Hashes:** the docs/RELEASE.md "v0.2.0 release build" table is filled, and the files on disk match it (step 5 checks this). If the GitHub assets were downloaded instead of the local build, pass their paths to `mts-zip.ts`.
3. **Numbers:** fill `release/mts/mts-data.json` (every `v020` null, `features.v020`, `thumbnail.claim`/`sub`) from the README v0.2.0 table and the shipped feature list.
4. **Images:** `node tools/mts-images.ts --final` (exit 0, no `TO FILL`). Look at the four PNGs in `release/mts/images/` (text fits, no overlaps). Not during a clean timing run (CPU).
5. **Zip:** `node tools/mts-zip.ts --self-check`, then `node tools/mts-zip.ts --final` (every file `= RELEASE.md`). Note the zip's sha256 from the output.
6. **Text:** fill every `TO-FILL` in `release/mts/listing.md` (new features, numbers, known issues, Overwatch/ErrorTrap line if shipped); `grep -n TO-FILL release/mts/listing.md` is empty. Rodney reads the description once (gains wording, PLAYERS.md "Gains wording OK" rule).
7. Commit the filled `mts-data.json`, images and listing on the release branch.

## B. The upload wizard (Claude fills, in Rodney's browser)

8. **Rodney:** open Chrome, log in to modthesims.info (Claude never types the password), open the upload page (Upload menu > Upload).
9. Game: **The Sims 3**.
10. Section: **Game Mods > Miscellaneous** (RULES.md "Category"); Type **Other** if asked.
11. Title: from listing.md "Wizard fields".
12. Packs required: **none** ticked.
13. Main file: `release/mts/out/Sims3Modernization_v0.2.0.zip` (upload needs Rodney's file-picker click if the browser tool cannot set it).
14. Thumbnail: `release/mts/images/thumbnail.png`; screenshots in order `1-features.png`, `2-benchmark.png`, `3-install.png`.
15. Description: the BBCode block from listing.md (without the surrounding fence). Use the wizard's preview: lists, links and bold render, no raw `[b]`.
16. Additional credits / tags / summary: from listing.md, only where the wizard has the field.
17. Any field not in listing.md: stop and ask Rodney; add it to RULES.md afterwards.

## C. Rodney only

18. Reads the preview end to end.
19. **Clicks the final Submit.** Claude does not submit.
20. Afterwards: add the MTS link to README "Just want to play" and docs/PLAYERS.md, and note it in docs/RELEASE.md (publish date, MTS URL, zip sha256). Moderation can ask for changes ("Changes Required" message): answer from RULES.md.

## Decisions (Rodney, 2026-10-21)

- Category: Sims 3 > Game Mods > Miscellaneous.
- Packs field: None; the listing notes it was tested only with all packs installed.
- ASI loader: link only, not bundled.
- Screenshots: Rodney takes one in-game shot (no ReShade) on Friday; it goes first, before the four info images.
- GitHub link and the "tested together with" list stay as drafted.

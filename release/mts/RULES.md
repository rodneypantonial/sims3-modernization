# ModTheSims upload rules for this release

Research 2026-10-21 (web, read-only, logged out). MTS publishes its moderation rules as "rubrics" (Upload menu > Creator Guidelines, https://modthesims.info/rubrics/). The wiki pages "Upload Policy" and "Mod The Sims:Upload Policy" are empty. The upload wizard needs a login, so its exact field list was **not** seen; field names below come from published uploads and the rubrics. Check them in the wizard on Friday.

## Category

**Recommended: Sims 3 > Game Mods > Miscellaneous.**

| Option | For | Against |
|---|---|---|
| **Game Mods > Miscellaneous** | Closest precedent: "Randomized Loading Screens (ASI mod)" (`.asi` + `.package`, needs an ASI loader) sits in Downloads > Sims 3 > Game Mods > Miscellaneous, Type "Other" [verified page, https://modthesims.info/d/699945]. Our upload is a script mod and a native plugin together, which no narrower section covers. | "Other wrong section" is a rejection reason (rubric 3896), so a moderator may move it. |
| Game Mods > Script & Core Mods | `ts3fixes.package` is a script mod (rubric category 281 exists). | Half the download is a native `.asi`, which is not a script or core mod. |
| Programs and Utilities | Smooth Patch 2.1 (`.package` + `TS3Patch.asi` + `wininet.dll`) is there, Utility Type "Miscellaneous" [verified page, https://modthesims.info/d/658759]. | The rubric for that section: "Programs and utilities are to help mod the game or to make it easier to manage content" (rubric 4177). Ours changes the game, it is not a tool. |

If the wizard's Game Mods > Miscellaneous has a "Type" field, pick **Other** (precedent).

## What moderators check (rubrics that apply to us)

| Rubric | Rule (paraphrased) | Our answer | Source |
|---|---|---|---|
| Content we do not allow (3892) | No content made by someone else, no redirect-only uploads, no pay or pirated content required or linked, no unmodified EA content. | Only our own code; no EA files; community fixes are linked, never included. Pirated copies are named as unsupported, not linked. | https://modthesims.info/rubrics/3892 |
| Usage of AI LLMs (4180) | Mods that **talk to external LLM providers** (Claude, OpenAI, ...) are not allowed, open source or not. | Neither file makes any network call (no socket/HTTP code in `native/ts3native` or `mods/ts3fixes`, checked 2026-10-21; docs/16: no network calls while playing). AI help in *writing* the code is not covered by this rubric; the ASI precedent says "Claude Code was utilized for parts of this mod" in its credits. We disclose it the same way. | https://modthesims.info/rubrics/4180 |
| File type (3914) | TS3 uploads: `.package` and/or `.sims3pack`; programs in `.exe` get extra review. Duplicate copies of the same package are flagged. | `.package` + `.asi` + `.ini` + `README.txt` in one zip. `.asi` is not in the rubric's list, but both precedents above ship one (and Smooth Patch a `.dll`). Risk: a moderator may ask about the `.asi`; answer: source on GitHub, 32-bit plugin, same kind as the precedents. | https://modthesims.info/rubrics/3914 |
| Custom Content Included Files (4143) | Other creators' content may be included only if their terms allow it, it is truly required, and it is credited; archives may be zip/rar/7zip. | Nothing from others is included. The ASI loader is **not** bundled (link only). | https://modthesims.info/rubrics/4143 |
| Title (4141) | Clear, specific, English. | "Sims 3 Modernization v0.1.5: Less Stutter, Background Route Planning (Script Mod + ASI)". | https://modthesims.info/rubrics/4141 |
| Description (3884), Post Presentation (4040) | Informative English description; **at most 4 full-size images in the description**; readable formatting. | `listing.md` description; images go in the screenshot slots, none inline. | https://modthesims.info/rubrics/3884, https://modthesims.info/rubrics/4040 |
| Mod Functionality (4037) | Say what the mod does, what kind of mod it is (script, override, tuning, ...) and where the player sees it, so downloaders can judge conflicts. | "What it does" + "What kind of mod" + Compatibility sections. | https://modthesims.info/rubrics/4037 |
| Special Instructions (4039) | Special install steps must be clear. | Two install places + ASI loader step, also in `README.txt` inside the zip. | https://modthesims.info/rubrics/4039 |
| Packs required (3877) | Mark every EP/SP/GP that is required, and no extra ones. | **None** (see listing.md "Packs"). | https://modthesims.info/rubrics/3877 |
| Search Parameters (3939) | Wizard tick boxes must match the description. | Fill per listing.md "Wizard fields". | https://modthesims.info/rubrics/3939 |
| Informative Screenshot (4166) | Every game mod needs at least one screenshot that shows its function, "or represents its function if difficult to show"; it also becomes the thumbnail. | Info images (features, measured chart, install diagram). Stutter is hard to show in a still. Optional: Rodney adds one in-game screenshot (no ReShade, rubric 3887) if a moderator asks. | https://modthesims.info/rubrics/4166 |
| In-Game (3882), Size (3885), Image Quality (4133) | "Required screenshots" must be in game (written for CC); at least ~800x600 recommended, **max 2592 px**; no upscaling. | Our images are 1200x900 drawn at native size; the thumbnail is 320x240 like the ASI precedent's. The in-game rule targets CC; for game mods rubric 4166 applies. Risk noted. | https://modthesims.info/rubrics/3882, https://modthesims.info/rubrics/3885, https://modthesims.info/rubrics/4133 |
| Advertising (4169) | No links to your own website or social media (Patreon, Discord, ...) in the description or images; credits and links to required content are fine. | Only links: GitHub source/releases/issues (source code is what the Programs rubric 4177 asks for; both precedents link GitHub), the ASI loader, credited projects. No Discord, Patreon or personal site. | https://modthesims.info/rubrics/4169 |
| Unique and Useful (4035) | Not a duplicate of an existing MTS mod or a cheat. | Different fixes from Smooth Patch / S3PP / S3SS; say so in Credits/Compatibility. | https://modthesims.info/rubrics/4035 |
| Custom Content Credits (4139), Updates and Remixes (4045) | Credit others' work with name and link; updates of others' mods need permission rules. | Not an update of anyone's mod; credits list with links. | https://modthesims.info/rubrics/4139, https://modthesims.info/rubrics/4045 |
| Uploads in Sets (3897) | Closely related items go in one upload. | One upload, one zip. | https://modthesims.info/rubrics/3897 |

Also seen on the precedents (not rules): "Packs: None, this is Sims 3 base game compatible!" wording; credits in the description ("Credits:" paragraph); source code link "so anyone can verify it does not contain malicious code" (Smooth Patch); zip or rar archives.

## Fields that MTS has no rubric for

- **Game version:** no field on either precedent; we state "Steam, patch 1.67" in the title-adjacent summary and the description.
- **Tags:** no tag field is visible on published TS3 uploads; MTS search uses title and description, so the keywords are in the text. If the wizard has a tag box, use listing.md "Tags".
- **Additional Credits:** no separate box seen on the precedents (they put credits in the description). If the wizard has one, paste listing.md "Additional Credits".

## Not found / open

- Archive size limit and allowed archive types for the main download: not published in the rubrics (precedent archives: 73 MB rar, 817 KB zip). Ours is ~0.4 MB.
- Whether `.asi` files trigger an extra virus review like `.exe` (rubric 3914 only names `.exe`).
- Moderation queue time: not published.

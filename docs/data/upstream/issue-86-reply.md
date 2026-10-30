<!-- Draft reply for issue #86. Not posted. -->
Thanks, this checks out in the decompiled code.

- `RemovePreferredSim` stops after the first entry.
- `EndService`, and the other ways a Sim leaves `mPool`, never touch `mPreferredServiceNpc`.
- `FindSimForAssignment` keeps a stale entry and sends a random Sim instead.

What's on branch `feature/seasons-spread-ea-fixes` (switch `preferredservice`, off by default):

1. `RemovePreferredSim` removes every entry that names the Sim.
2. `FindSimForAssignment` drops this household's entry when its Sim is null or not in the pool. EA's own no-entry path then records the Sim it actually sends. A preferred Sim who is in the pool but busy keeps the entry, as in EA.
3. Before every save, each service is swept for entries whose Sim left the pool. This changes save data, which is why the switch is opt-in.

Counters: extra entries removed, entries re-picked, entries swept at save.

The offline check (`EaFixCheck prefdup`) records one pool Sim for two test households and calls `RemovePreferredSim`. EA leaves one entry; with the switch on, none are left. `EaFixCheck pref` counts the stale entries per service on a save. It's not run in game yet. If you can count the stale entries on your long save with `pref` before and after a save with the switch on, that would tell us how big the effect is.

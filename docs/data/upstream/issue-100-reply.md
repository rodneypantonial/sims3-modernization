<!-- Draft reply for issue #100. Not posted. -->
Thanks, this checks out in the decompiled code, and it is a bit wider than the title says.

- `SimBuilderTask.Simulate` catches everything, including its own `OperationCanceledException` once `Stop` set `Abort` (aging turned off, or `CancelAgingAlarmsForSim`).
- The "model built" flag is only set after the build returns. So after any exception the task goes to `FinishOutfitSwap`, which removes every outfit and adds back what `NewOutfits` holds. That is the same path for a Sim that has a Sim object. We have not seen that case live, though.

What's built (switch `ageupkeepoutfits`, off by default, docs/205 §1):

- `FinishOutfitSwap` returns at once when the task `Failed` or `NewOutfits` holds no outfit. The old outfits stay and the Sim keeps its age. EA's own age-up paths already stop on `Failed`, and `PreInstantiateSim` sets the age only after it reads the swap map.
- One log line per skip, with the Sim's name and the exception. Counter `ageupkeepoutfits_kept`.
- We left `PreInstantiateSim` alone, for the reason you give (`MergeTravelInformation` would loop).

For saves that already have the damage, we don't rebuild a look. A `MakeSim` face is a new face, and the old outfits are gone, so we have no safe source. We count and name these descriptions instead: `AgeUpCheck empty`, plus counter `ageupkeepoutfits_nooutfit` from the hourly `deadrefsweep` pass (#91). Your rebuild is a reasonable choice for a player who would rather have some look than none.

Check (repro `bp-ageup-outfits-check`): one townie without a Sim object gets its builder started and stopped at once. With the switch off, it loses every outfit; with it on, the count stays. Not run in game yet. If you catch the builder exception behind the tourists, the log line would show whether it is the cancel path or a real build failure.

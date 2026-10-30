<!-- Draft reply for issue #91. Not posted. -->
Thanks for the list, and for the load-time warning. That one saved us a bad first version too: every pass now stands down when an active-household member is invalid, or when more than half of a big holder looks gone.

We read each holder in the decompiled code. All but four check out as you describe (docs/205 §2.1 has the table). Two switches, both off by default:

- **`deadrefguards`:**
  - `Relationship.Get(SimDescription)` adds no key for an invalid description. A description that still has a key gets EA's answer, so `RemoveSimDescriptionRelationships` (which calls `Get` before the flag goes false) is unchanged.
  - `IsAgingStateValid` is false for an invalid description.
- **`deadrefsweep`:**
  - **Hourly pass:** relationship keys and entries, aging states (through `AgingManager.RemoveSimDescription`), alarms owned by gone descriptions (`AlarmManager.OnDispose`), react times, school and job coworkers, former boss, chess challenger, unforgettable topics, fisher, pusher, last talking partner, reserved vehicle, pregnancy dad (null, then EA's `CheckForDad`), car and boat previous passengers, and horseshoe throwers and scores.
  - The pass runs in slices of about 2 ms per tick, so it never costs a noticeable tick.
  - **Before each save:** only a cheap pass over relationship keys, aging states and alarm owners.
  - Coworkers are dropped directly, not through `RemoveCoworker`, for the reason you give.
  - The passenger arrays are rebuilt rather than given null slots, because `GetSimsThatGoHomeWithActor` reads every slot.

Not built yet:
- `Celebrity.mWaitingToReact`: we did not find the holder object in time.
- `CookingProcess` handlers and the stolen-item `mThief` component: each needs its own care.
- Adoption pool pets: disposing them is a behaviour change, not a cleanup.

The "valid but householdless at save" descriptions you trace at the end can't be caught by a validity sweep. We'd be glad to hear what orphans them.

Counters are `deadrefguards_*` and `deadrefsweep_*`, including slices, longest slice and longest save pass. The checks are in repros `bp-deadrefs-check` and `bp-deadrefs-saves` (save matrix: overwrite, back-to-back, unpaused, reload). Not run in game yet. Your ErrorTrap "SimDescription Removed" count per load on the long Bridgeport chain, with both switches on, would be the best end-to-end test if you have time.

# 860. Heckle spam: Sims heckle ordinary Sims at random (hecklefix)

Status 2026-11-21: built, NOT RUN in game. ts3fixes `hecklefix` (tuning `kHeckleFix`) and `hecklefixhh` (`kHeckleFixHousehold`),
both default off. Code `mods/ts3fixes/src/HeckleFix.cs`, check `HeckleFixChecks.cs`, repro `hecklefix-live`, counters 1530-1534.

Player report: "it spams and doesn't work correctly, sims do it randomly".

## 1. Cause [verified, decompiled; harness logs]

`Sim+Heckle` (EP6, added to every Sim when Showtime is installed) is an `Interaction<Sim, Sim>`. Its `Definition.Test`:

- passes for any standing or sitting actor with one of 13 traits: Social Butterfly, Brave, Childish, Diva, Dramatic, Evil,
  Grumpy, Hot-Headed, Inappropriate, Insane, Mean-Spirited, Natural Born Performer, Rebellious;
- has no `isAutonomous` branch and no "target is performing" test. It refuses Bonehilda, swimming or boating targets, the
  actor itself, a target already `Heckled`, a performer in a gig (`OccupationAsPerformanceCareer.StagePerformingOn != null`;
  those are heckled through `ShowStage+WatchTheShowHeckle`, player only), hidden Sims, Sims in a service that blocks
  socials, and two magic-trick interactions.

`Run` routes within 1-10 m, plays the heckle, sends `kHeckleSim`, sets `Target.Heckled`, gives the target Slipped Up and adds
a 10-minute `AlwaysPersisted` alarm on the actor whose callback (`HeckleReset`) clears `Target.Heckled`. The cooldown is per
target, so a heckler walks from Sim to Sim. Every Sim with one of those traits can therefore heckle any passer-by.

Harness logs (321 queue `ts3native.log` files with heckle lines, 854 lines): autonomous heckles of ordinary Sims by household
and townie Sims, and route failures to heckle targets (`routefail: ... Heckle target Sim, Plan ...
FailedBecauseNoPathToDestination`). No script error or error-trap line mentions Heckle, so the "spam" is the behaviour
itself, not an exception loop.

## 2. Fix

`HfTestHook` (swap of `Sim+Heckle+Definition.Test`, stub `TestOrig`, swap-stack-audit row: no path to a yield) runs EA's Test
first. When EA says yes and the heckle is autonomous, the hook says no unless the target is performing. Player-ordered heckles
always keep EA's answer. Household, selected and unknown-scope actors (`Fixes.HouseholdExempt`) keep EA's answer unless
`hecklefixhh` is on as well (it needs `hecklefix`).

"Performing" (`HfPerforming`, on the target's current interaction):

- `IPerformForTipsInteraction` whose `Performing` is true (spellcasting for tips);
- `PerformanceCareer.PerformerPerformForTips` and every `PerformanceCareer.PerformanceTrick` (Showtime street acts: singer,
  magician, acrobat tricks), `Services.DoPerformance` (hired performers);
- any interaction on a `GenericPerformanceObject` (balancing ball, box of danger / mystery, giant ring of fire, front stage
  kit, DJ booth) by a Sim with a performance career;
- by class name on the interaction's class chain: `BandInstrument+PlayForTips<T>` and `+Perform<T>` (guitar, bass, drums,
  piano, laser harp), `Piano+PianistPerform`, `Violin+PlayForTips`, `PoolTable+PerformForTips`,
  `SnakeCharmingBasket+CharmForTips`, `MagicBroomArena+StuntRideForTips`, `KaraokeMachine+Sing / SingAlone / SingWith`.

Not covered (counted as not performing): a DJ without a performance career, and any act not listed. The list is a
`shortcut:` in the code; add classes when a log shows a missed act.

## 3. Stuck `Heckled` flag [verified, decompiled; not measured]

`Sim.mHeckled` is a saved Sim field. Only `HeckleReset` (the actor's alarm) clears it after a `Sim.Heckle`. When the heckler
is disposed before the 10 minutes end (townie cleanup, a Sim deleted, a household leaving the world), `GameObject` disposal
calls `AlarmManager.OnDispose`, which clears the owner's timers without calling them. The target then stays `Heckled` for
good: it is never heckled again (both heckle Tests refuse it) and `BuffFeelingUnlucky.ShouldAdd` keeps treating it as
heckled. A Sim reset does not strand it (`AlwaysPersisted` is not `DeletedOnReset`). Save and load alone should not either
[inferred: the timer and its delegate are saved, and `InteractionInstance` is `[Persistable]`]. `ShowStage`'s heckle path sets
the flag the same way but also clears it in `WatchTheShow.Cleanup` (via its `HeckleReset`) and `PerformTheShow.Cleanup`, so it
does not stick.

With `hecklefix` on:

- at world load every Sim's flag is cleared, except a performer in a gig. A heckle less than 10 sim-minutes old loses the
  rest of its cooldown. Its saved alarm still fires later and clears the flag again, which is harmless.
- in play, the Test hook notes when it first sees a target flagged. A flag still set 60 sim-minutes later (6x EA's
  cooldown) is cleared, except for a performer in a gig. The memo holds at most 256 Sims.

## 4. Counters and agent

| Slot | Meaning |
|---|---|
| 1530 | autonomous heckles of a non-performer refused (NPC actor, `hecklefix`) |
| 1531 | the same case seen with `hecklefix` off |
| 1532 | household, selected or unknown-scope actors refused (`hecklefixhh`) |
| 1533 | stale `Heckled` flags cleared (world load and in play) |
| 1534 | errors (EA's answer kept) |

Agent `patches` reports them as `hecklefix_refused`, `hecklefix_wouldrefuse`, `hecklefixhh_refused`, `hecklefix_staleclear`
and `hecklefix_errors`.

## 5. Repro `hecklefix-live`

`node tools/harness/repro.ts run hecklefix-live` on any world with instantiated townies. `HeckleFixCheck run` picks a townie
pair that EA's own Test (the `TestOrig` stub) lets heckle autonomously. It then runs the Test hook for that pair, for the same
target standing in as a performer, for the pair player-ordered, and for a household or selected actor. It pushes nothing and
changes nothing.

| Arm | Townie pair | At a performer | Player-ordered | Household actor | Verdict |
|---|---|---|---|---|---|
| both off | true | true | true | EA's answer | BROKEN |
| `hecklefix` on | false | true | true | EA's answer | FIXED |
| `hecklefix` + `hecklefixhh` on | false | true | true | false | FIXED |

Any other result reads UNEXPECTED. The check also runs pure decision cases (`HfDecide`, `HfStale`) and reports whether the
swap is live (`CkLive`).

## 6. Open

- Not run in game yet. The "performing" case uses a stand-in, because no street performer is staged.
- The stale-flag clear is not measured. A test needs a heckler disposed within the 10 minutes.
- The in-play clear can come early. Case: one heckle is cleared on time, then a new heckle lands within 60 minutes of the
  first sighting. The new heckle's cooldown is then shorter, so the Sim can be heckled sooner.

# 861. Prom: the date never boards the limo (promdatelimo, promlimolot)

Status 2026-11-21: built, NOT RUN in game. ts3fixes `promdatelimo` (tuning `kPromDateLimo`) and `promlimolot`
(`kPromLimoLot`), both default off. Code `mods/ts3fixes/src/PromDateLimo.cs`, check `PromDateLimoChecks.cs`, repro
`promdatelimo-live`, counters 1535-1542.

Player report: "If a sim has a date ... my sims never get into the limousine together if the date happens to be on the lot.
The date will typically not get to the prom independently, either. They will appear in a greeting card photo, though."

## 1. promdatelimo: the date is never told to get in

### Cause [verified, decompiled `PromSituation.cs`, `CarpoolManager.cs`]

- 16:00 (`kTimeOfEventStart` 18 minus `kTimeToPushDates` 2): `OnPushDatesToActiveHousehold` instantiates each active-household
  teen's prom date if needed and pushes `VisitLot` to the home lot.
- 16:45: the scheduled white limo carpool (`AddAlarmsAndCarpool`, pickup 17:00, `TravelTimeInMinutes` 15) runs
  `CarpoolData.DoPickup`. Its rider list `mRiderList` is `GetSimsRequiringCarpool`: every teen on the active home lot at that
  moment. A date still on the road is not in it.
- Arrival, `DoCarpoolPickupAndDropoff.Run`: the `GetInCarpool` definition's `Riders` is rebuilt (every teen on the lot now,
  plus `mRiderList`), so EA's Test would accept a late date. But `GetInCarpool` is pushed only to `mRiderList`. A rider whose
  home is another lot (`lot != LotHome`, a visitor) is pushed (priority lowered by one, `Add`) only when its queue has at most
  one visible interaction; one with an active career only when its queue is empty or its head is autonomous.
  Correction to the first diagnosis: a visitor without a career is not always skipped; it is skipped when it arrived after
  16:45 or is busy (2 or more visible interactions).
- `GetInCarpool.Definition.Test` passes for the date: `IsHeadedToProm` is true for any teen on prom day from 17:00, and the
  date is in `Riders`.
- `PushAllNpcTeensToLocationOneShot` (17:00 alarm, and again from `OnArrivedDelegate` each time a Sim boards) sends every NPC
  teen to prom except one whose date is selectable: that date is meant to ride the limo.
- `LoopUpdate` keeps the limo waiting while any teen on the lot is not a passenger, up to 18:00 (`DropoffTime`). Then it
  leaves. Only Sims holding a reference to the car (walking to it) get `OnArrivedDelegate` and so `GoToProm`. The date holds
  none, so it stays at the house, and the household teen sat in the limo until 18:00.
- `GoToProm.TakePromPictureOneShot` takes `GetPromDate(Actor).CreatedSim` wherever it is: the date is in the photo.

### Fix

Acts only on the date, and only when the date is an NPC (`IsPlayerSim` false). The household teen's own actions are
unchanged; a date who is household or selectable is left to EA, which pushes Sims who live on the lot itself.

- Limo: swap of `CarpoolWithDriver.SetOnCarEnteredDelegate` (stub `SetOrig`). `Run` calls it once per carpool arrival, right
  after it added its `GetInCarpool` definition and before its own pushes; nothing else calls it [verified, all assemblies].
  The body is a one-field setter (inline shape 4, `ldarg.0; ldarg.1; stfld; ret`): the swap is allowed because a `stfld`
  and a call take the same 2 words (docs/293, `InlineShapeMovesLayout`). The hook returns at once unless the delegate's
  target is the prom's `mCarpool` (school bus and career carpools keep EA's pushes). For each selectable rider in the
  definition's `Riders`, its date is pushed the car's own `GetInCarpool` definition (UserDirected, lowered by one as EA
  lowers visitors, `AddNext`) when the date is on the pickup lot, a visitor there, in `Riders`, not a passenger and not
  already queued for `GetInCarpool` or `GoToProm`. EA may then add a second copy for a date it pushes itself; boarding runs
  `SimEnteredCarpool`, which purges the Sim's interactions on the car.
- Fallback: swap of `PromSituation.AddSim` (stub `AddOrig`; only caller `GoToProm.InRabbitHole`, once per entry). When a
  selectable teen enters prom and its date is not queued for `GoToProm` (running counts: it is at prom), not a passenger of
  the prom limo and not walking to it, the date gets EA's own `ForceSituationSpecificInteraction(mLocation, date,
  GoToProm.Singleton, ...)`, as `PushAllNpcTeensToLocationOneShot` sends NPC teens. Covers a date that arrived after the limo
  came, never arrived, or failed to board. Not done earlier on purpose: a date that already holds `GoToProm` when it boards
  is pushed out of the limo (`SimEnteredCarpool`: `OnArrivedDelegate` returns false, `PushGetOut`).
- A date whose `CreatedSim` is null is skipped (EA instantiates it at 16:00; instantiating from the hook could yield).
- Neither swapped method is passed as a delegate, so no save names a hook class (docs/576 §1).

Household rule (CLAUDE.md): this is an EA bug fix acting on NPC dates only, so no `hh` switch is needed. EA bug fixes stay
even where they touch household Sims; this one does not.

Counters: 1535 dates pushed into the limo, 1536 dates sent to prom on their own, 1537 limo cases seen with the switch off
(EA may still push some of them itself), 1538 dates left behind seen with the switch off, 1539 errors.

## 2. promlimolot: the limo picks up at an old address

### Cause [verified, decompiled]

`AddAlarmsAndCarpool` runs when prom is announced, three days ahead, and fixes the carpool's pickup lot
(`CarpoolData.mStartLotId`, `readonly`) to `Household.ActiveHousehold.LotHome` of that moment. `GetSimsRequiringCarpool` and
`OnPushDatesToActiveHousehold` read the household active on prom day. Careers and schools reschedule their carpools when a
household moves (`GameplayMovingModel`, `MovingSituation`: `RescheduleCarpool`) and drop them when their Sims stop being
selectable (`Sim.NullSelectableSimManagers`: `RemoveCarpool`); the prom limo is never moved. After a move, or a switch to another
household with teens, the limo parks at the old address, the current household's teens are told to walk there (`Run`
treats them as visitors of that lot), and their dates wait at the real home.

### Fix

Swap of `CarpoolManager+CarpoolData.PickupSims` (stub `PickOrig`), called at 16:45 by the carpool's `NeverPersisted` alarm
(so the delegate to it is never saved) and by `CreateOneShotCarpool`. For the prom's `mCarpool` only, when the active
household has a home lot and it differs from the pickup lot, `mStartLotId` is set to the current home before EA's body runs
(reflection writes a `readonly` field; `PromLimoLotCheck` proves it on a throwaway instance). It moves the limo, not a Sim;
an EA bug fix, so it applies with household teens aboard (CLAUDE.md household rule).

Counters: 1540 pickups moved to the current home, 1541 the same seen with the switch off, 1542 errors.

## 3. Other prom leads checked (2026-11-21)

Ranked by what a player would notice; only the first two are fixed.

| Lead | Result |
|---|---|
| Date never boards / stays home (player report) | fixed, `promdatelimo` (§1) |
| Limo goes to the home of announcement day after a move or household switch | fixed, `promlimolot` (§2) |
| Save/load: `sInstance`, `mDates`, `mCarpool`, `mPushNpcsOneShot` lost | dropped: `[PersistableStatic(false)]` statics are saved (docs/582 correction, docs/110 §3.1); the rest are instance fields of the saved situation |
| Phone ask (`PhoneCallAskToProm.OnAnswered`) checks only the callee's date, so a caller who got a date while the phone rang is paired twice (`AddPromDate` leaves the old partner's entry) | dropped: needs the NPC caller to be asked and accept within the ring window; the in-person social (`TestAskToProm`) checks both sides |
| Event weights: Ditched (`5 - 0.25 * liking`) and Seal the Deal (`5 + 0.25 * liking`) go negative, which skews the cumulative pick (high liking: no Fight, little Various; a disliked date: no First Kiss or King/Queen) | dropped: odds only, the total stays positive with EA tuning (Various 20), nothing a player can see in one prom |
| `PushAllNpcTeensToLocationOneShot`: `CreatedSim` null after a failed `InstantiateOffScreen` throws, the rest are not pushed and `mPushNpcsOneShot` stays set | dropped: needs the engine to fail a Sim creation; the missing NPCs are inside the rabbit hole |
| Every teen in the world instantiated and pushed to prom (6 per 20 sim-minutes) | dropped: EA design, batched; a cap would be a performance design, not a bug fix |
| Cleanup at 22:00 sends home only participants on the prom lot | dropped: the `GoToProm` timed stage runs out near 22:00 (its length is set when it is pushed), Sims inside are on the prom lot and are sent home, and a late `GoToProm` fails its Test once `sInstance` is null; the limo leaves empty at 18:00 when nobody boards |
| Photo with a date whose `CreatedSim` is null, or a failed generation | dropped: EA falls back to a solo picture / an event without a screenshot |

## 4. Testing

`promdatelimo-live` (any world, no prom needed) runs the hooks' decisions on live Sims and the `readonly` write; both checks
print `verdict: OK` with the switches off and on. The real limo and prom entry need a prom day between 16:00 and 18:00 and
cannot be staged cheaply, so testers verify: a household teen with an NPC date, the date reaching the house after 16:45 or
busy at 17:00. Expected with `promdatelimo` on: both board the limo (1535 counts), or the date reaches prom on its own after
the teen enters (1536). With it off, 1537 / 1538 count the cases EA misses. For `promlimolot`: switch to another household
with a teen (or move) after the announcement; with the switch on the limo parks at the current home (1540).

<!-- Draft reply for issue #88. Not posted. -->
Thanks for the table and for checking the other `GetRandomObjectFromList` calls. We read rows 1-4 in the decompiled code and wrote our own hooks from that reading. They are on branch `feature/seasons-spread-ea-fixes`, behind one switch, `eanullguards` (off by default), with a counter per guard:

1. **`VelvetRopes.ChargeSimForBribe`:** with no bribe table, it returns false, EA's own answer for a Sim with no bribe on record.
2. **`ActiveTopic.AddToSim`, all four overloads:** a null Sim, or one without a SocialComponent, returns at once, the way the first overload already handles a missing component.
3. **`Motives.CreateMotives`:** for a pet with no household, the `NullReferenceException` from the pet branch's last statement (`Household.UpdateMotivesToLookAfterPets`) is caught. As you say, a hook cannot skip just that one statement. The catch only applies to householdless pets, the case that throws there; every other Sim runs EA's method unchanged.
4. **`CarrySystem.EnterWhileHolding(Sim, ICarryable, bool)`:** a null target returns before `CarryUtils.Acquire`. The callers' next step, `PutDown`, already returns for an empty right hand.

Row 5 (doorbell prank) is not done. The throw is in the middle of `Door.PrankDoorbellDitch.Run`, inside the shared generic `RandomUtil.GetRandomObjectFromList<T>`. A hook would have to copy the method body, which we avoid.

Checks: `EaFixCheck topic|bribe|carry` calls each one with the empty state, first with the switch off (EA throws), then on (no throw, counter up). Row 3 has no direct check: making a Sim with no household safely is not something the agent can do. Not run in game yet.

For the record, we did not use the code in your comment. These are separate hooks written from the decompiled game code (AGENTS.md "Community code").

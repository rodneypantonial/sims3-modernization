# simler90: split AgingFixHomelessPetsAndYoungsters (request, not sent)

Target: simler90 on modthesims.info (no GitHub), thread https://modthesims.info/d/659969/simler90-s-gameplay-systems-core-mod.html or MTS private message. Context: ts3fixes `easpreadlots` runs simler90Extras.WorldMaintenance's four 3 AM jobs as per-tick units from v0.2.1 (EaSpread.cs); the aging fix has no per-Sim entry point, so it runs whole in one tick (cost not measured yet). Method names only, no simler90 code quoted (no license).

```
Hi simler90,

I work on Sims 3 Modernization (https://github.com/rodneypantonial/sims3-modernization), a stutter/freeze mod. Many of our players also use your Gameplay Systems Core Mod, so we test with UPDATE242 installed.

Our mod splits the game's 3 AM venue cleanup (LotManager.CleanUpLotsAndInventories) into small per-tick steps so it no longer freezes the game for a frame. From v0.2.1 we also run your WorldMaintenance additions inside that split cleanup, so they keep working with our mod.

One of them, AgingFixHomelessPetsAndYoungsters, does all its work in one go: on nights when several homeless pets or young townies are due to age up, it instantiates and fades them out back to back without yielding, which can show up as a short freeze. FixMissingSims already sleeps between households, which works great.

Would you consider one of these small changes?
- a Simulator.Sleep(0) (or SleepIfPossible) between Sims in that loop, like FixMissingSims, or
- a per-Sim method we could call one at a time from our split cleanup.

Either way, it's your call of course. Happy to test a build on our side and share timings. Thanks for the mod!
```

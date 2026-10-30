<!-- Draft reply for issue #90. Not posted. -->
Thanks. Both leaks check out in the decompiled code.

- **Throwables:** the `ThrownObjectInfo` constructor creates its item only to read `Cost`, and the game builds this table once per world load, from `PostWorldInitializers`. `PerformInventoryActions` creates a fresh copy on every throw and only sometimes uses it.
- **Fire pit:** `FirePit.mTestChair` is `[Persistable(false)]`, but the chair object itself is still saved.

What's on branch `feature/seasons-spread-ea-fixes` (two switches, both off by default, because both change what goes into a save):

- **`showstagethrowables`:** during `CreateThrowableObjectDb` and `PerformInventoryActions`, every object `CreateObjectFromMedatorName` makes that ends up out of the world, in no inventory and with no parent is destroyed when the call returns. A copy given to the performer's inventory is kept. Counter: items destroyed (expect about 30 per load).
- **`firepitchair`:** before every save, each fire pit's out-of-world test chair is destroyed and the field cleared. The pit makes a new one when it needs it. A hook cannot change the field attribute, so this is the hook alternative you mention. A deleted pit's chair stays an orphan, as before. Counter: chairs destroyed.

One more leak we noticed while reading: when there is no performer, or the performer is on a passport challenge, the copy taken out of the thrower's own inventory is neither put back nor destroyed. Our hook does not catch that one, because it only tracks objects the call creates. We left it as it is.

Checks: `EaFixCheck throwdb` rebuilds the table. With the switch off it adds 30 out-of-world objects; with it on, 0. `chairs` and `orphans` count the rest. Not run in game yet. Your load-save-load count is the better end-to-end test, if you have time to run it with both switches on.

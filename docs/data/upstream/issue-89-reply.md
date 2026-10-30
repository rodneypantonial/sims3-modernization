Thanks for this report, and for tracing the cause down to the setter. Your reading matches the decompiled code: on a pop where `Compatible` is true, the setter links the target back onto the posture being left, and the two then point at each other.

What we changed (branch `bugfix/uf-posture-cycle-hook-frames`, not released yet):

- **`UfHeld` no longer calls `Posture.Satisfies`.** It walks `PreviousPosture` itself and checks `Satisfaction` at each posture, which gives the same answer on a normal chain. It stops after 16 postures. A chain that doesn't end by then counts as "held", so the unseen fallback never moves or fakes that Sim, and the new counter `posture_chain_loops` goes up. This takes our planner out of the overflow, whatever made the loop.
- **Your `set_Posture` fix is in as a switch, `posturepopcycle`, default off.** When a set targets the current posture's `PreviousPosture` and `Compatible` is true, it takes the setter's unwind instead: it exits the posture being left, lands on the target without adding its interactions again, fires `PostureChanged` under the setter's own rule, and queues the idle logic. Every other posture change runs EA's setter unchanged. Counter: `posturepopcycle_unwinds`. The source credits your design.
- **Check:** a new agent command, `posture_chain_check`, lists Sims whose chain doesn't end within 16 postures. That's the offline check you suggested. An offline selftest models the chain: without the guard the pop makes the loop, and with it the chain stays intact.

Still open:

- We haven't run either change in game yet, and we have no repro that builds the loop (toddler carried into a car, a second vehicle posture, then a pop). For now the repro entry is static only.
- `posturepopcycle` stays off until we have seen it in game. Like you, we found no case where EA meant to keep the old posture on a pop, but that is still inferred.
- A loop made by some other path still overflows EA's own `Satisfies` callers. Ours are covered.

If you still have the ScriptError XMLs from the Aurora Skies session, the Sim's postures at the time (vehicle types, and whether a child was carried) would help us build a repro.

<!-- Draft reply for issue #82. Not posted. -->
Thanks for the timings and the two designs. I checked the path in the decompiled code and it matches what you found. `SetSeason` and `EventLotMarker.SetLayout` are the only script callers of `RegenImposter`. They run only in a yielding context, and the cost is the sim thread waiting for the main-thread imposter job. One thing to add: the 3 AM pass gives every lot that needs a season change a 1-sim-minute yielding alarm, so several lots can reach their rebuild in the same tick.

What's on branch `feature/seasons-spread-ea-fixes`: a new switch `seasonimposter` (off by default), built from your option 1 plus your v2 idea:

- In live mode, `World.RegenImposter` only queues the lot, once.
- A task of ours rebuilds lots one at a time, at most one every 2 s. Warm lots go first: the active household's home, a lot one of its Sims is on, and the lot under the camera.
- Cold lots wait until they turn warm or until the next save. With `seasonimpostercold` on, they are rebuilt during play too, nearest to the camera first, still one every 2 s.
- Before every save, all queued lots are rebuilt, so a saved imposter is never stale. Travel does the same. If you quit the world without saving, the queue is dropped (none of it was saved anyway).

Counters: queued, warm, cold, flushed at save, dropped, longest rebuild (ms), most waiting.

It's not run in game yet. The repro (`bp-seasonimposter-loop`) asks for 4 community-lot rebuilds in one tick, flipping the switch in the same load.

Your option 2, not waiting on the job, is the one that would remove the cost instead of moving it. We have not built it, because we have no proof yet that the job is safe while the sim thread keeps changing the lot. If you measure the save cost with the warm-first order, please post the numbers here (`seasonimposter_flushed`, `seasonimposter_max_ms`).

Thanks for this. The frame counts for EA's forwarding overloads are useful, and so is the reminder that ErrorTrap needs about 11 frames of headroom.

We counted what our own hooks add (written up in docs/130 §11). Every ts3fixes hook costs at least one frame: the hook body plus the stub that holds the original. Most hooks are at that floor. The route plan hooks were not:

| Hook | Frames added above the original, before | Now |
|---|---|---|
| `Route.Plan` | +7 (+8 with `failrepeat`) | +4 (+5) |
| `Route.Replan` / `PlanFromPoint` / `ReplanFromPoint` | +5 | +3 |
| `GoHome.Run`, rabbit-hole entry, `IsAllowedInRoom`, `FollowSubPathRouteAction.PerformAction` | +1 | +1 (floor) |

The plan wrappers now decide first and run the plan from the outer frame, and the async plan step is inlined into its only caller. Behaviour is unchanged. This is on branch `bugfix/uf-posture-cycle-hook-frames` and isn't released yet. The posture-loop issue (#89) is fixed on the same branch. That fix also stops `UfHeld` from recursing down the posture chain.

Still open:

- `IsAllowedInRoom` belongs to the autonomy-offload experiment. That hook is installed even while the experiment is off, so it costs one frame for nothing. We may install it only when the experiment is on at load.
- Your two incidents had only floor-cost hooks of ours in the stack (rabbit hole, `IsAllowedInRoom`). So most of the depth there is EA's, and your IL patch for the forwarding overloads is the bigger win. We won't ship IL patches to EA's DLLs ourselves, but we'll point players with deep stacks to your numbers.
- We haven't looked at raising the 63-frame limit. It's a fixed record array in the native interpreter, so that would be native work, if it's possible at all.

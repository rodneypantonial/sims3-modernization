using System;
using System.Collections.Generic;

namespace Ts3Fixes
{
    // Game-independent parts of ReplanDefer.cs (docs/141 §6), checked offline by tools/selftest/replandefer.
    public static class ReplanDeferCore
    {
        // replandefer: a PortalRouteThroughAction.ForceReplan goes to the Sim's own route loop (ReplanRouteInterruptAction,
        // what FollowRouteAction.ForceReplan already does for every other Sim route action) only when the game's own
        // replan would have ended in "insert the replanned path, kill this action": not a load replan, a Sim follower,
        // this action is the Sim's current one, a path adaptor exists and its route is being followed. Otherwise the
        // caller may follow the replanned route in place right after the call (elevator approach inside DoPortal), so
        // the game's synchronous replan stays.
        public static bool ShouldDefer(bool load, bool isSim, bool isCurrentAction, bool hasPathAdaptor, bool routeActive)
        {
            return !load && isSim && isCurrentAction && hasPathAdaptor && routeActive;
        }
    }

    // replanspread: routing components whose "replan your route" call (OnBuildBuyExit / OnRoutingDataChanged) was held
    // back, replayed at most perTick routing ones per sim tick, oldest first; one entry per component.
    public class SpreadQueue<T> where T : class
    {
        readonly List<T> mItems = new List<T>();
        public int Count { get { return mItems.Count; } }
        public void Clear() { mItems.Clear(); }

        // false when the component is already waiting (the game would have replanned twice; once is the same result)
        public bool Add(T x)
        {
            for (int i = 0; i < mItems.Count; i++) if (ReferenceEquals(mItems[i], x)) return false;
            mItems.Add(x);
            return true;
        }

        // Replays entries until perTick of them were routing (non-routing ones cost nothing: the game's call returns at
        // once) or the queue is empty; dead ones are dropped. Returns the number of calls made.
        public int DrainTick(int perTick, Predicate<T> alive, Predicate<T> routing, Action<T> call)
        {
            int calls = 0, routed = 0;
            while (mItems.Count > 0 && routed < perTick)
            {
                T x = mItems[0];
                mItems.RemoveAt(0);
                if (!alive(x)) continue;
                if (routing(x)) routed++;
                calls++;
                call(x);
            }
            return calls;
        }
    }
}

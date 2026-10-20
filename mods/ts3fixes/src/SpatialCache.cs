using System;
using System.Collections.Generic;
using Sims3.SimIFace;

namespace Ts3Fixes
{
    // Lot object query cache (docs/20 candidate 3, rows 8-9). Every Lot.GetObjects<T>() / Queries.GetObjects<T>(lot) ends
    // in Sims3.SimIFace.Queries.GetObjects(Type, ulong lotId), which walks the lot's objects natively (Query_GetObjectsOnLot,
    // impl 0x7499B0). Hot callers ask the same (type, lot) many times in one sim tick: Autonomy.ScoreInteractionsForLocalAutonomy
    // (lot Sims, once per commodity kind), TombRoomManager.AboutToPlanRouteCallback (doors and stairs, every route plan on a
    // tomb lot), Sim.GetMostExpensiveUsableParkedVehicle, LightGameObject.AutoLights*, Lot.FindCallbox [measured, managed
    // samples]. A hit returns a copy of the array the first call got in this tick.
    // Never stale beyond one tick: entries live only for the Simulator tick they were made in (and at most kMaxAgeMs of wall
    // time, for modes where ticks stop), nothing is cached while paused (buy/build edits), and any script object destroy
    // clears the whole cache. Not caught: an object created or moved onto/off the lot earlier in the same tick (seen from the
    // next tick on). Counters: [62] hits, [63] misses, [64] clears on destroy. Switch spatialcache (tuning kSpatialCache).
    public static partial class Fixes
    {
        static bool sSpatialCacheReady; // both hooks in: without the destroy hook a hit could return a destroyed object

        static void ApplySpatialCache()
        {
            bool destroy = Patch(typeof(Simulator), "DestroyObject", new Type[] { typeof(ObjectGuid) }, typeof(SpatialCacheHook), "DestroyHook", "DestroyOrig");
            bool query = Patch(typeof(Sims3.SimIFace.Queries), "GetObjects", new Type[] { typeof(Type), typeof(ulong) }, typeof(SpatialCacheHook), "GetObjectsHook", "GetObjectsOrig");
            sSpatialCacheReady = destroy && query;
        }

        public struct LotQueryKey : IEquatable<LotQueryKey>
        {
            public Type Type; public ulong Lot;
            public bool Equals(LotQueryKey o) { return o.Lot == Lot && ReferenceEquals(o.Type, Type); }
            public override bool Equals(object o) { return o is LotQueryKey && Equals((LotQueryKey)o); }
            public override int GetHashCode() { return Type.GetHashCode() ^ Lot.GetHashCode(); }
        }

        public static class SpatialCacheHook
        {
            const int kMaxAgeMs = 100;
            static readonly Dictionary<LotQueryKey, Array> sCache = new Dictionary<LotQueryKey, Array>();
            static long sTick = -1;
            static int sStampMs;

            public static Array GetObjectsHook(Type type, ulong lotId)
            {
                if (OnWorker()) return GetObjectsOrig(type, lotId);  // docs/128 G-M3: no cache or counter writes off the sim thread
                if (!sSpatialCacheReady || type == null || Sims3.Gameplay.Gameflow.CurrentGameSpeed == Gameflow.GameSpeed.Pause || !ShortcutOn("spatialcache"))
                    return GetObjectsOrig(type, lotId);
                long tick = Simulator.TicksElapsed();
                int now = System.Environment.TickCount;
                if (tick != sTick || now - sStampMs > kMaxAgeMs) { if (sCache.Count > 0) sCache.Clear(); sTick = tick; sStampMs = now; }
                LotQueryKey k; k.Type = type; k.Lot = lotId;
                Array a;
                int[] c = PatchCounters();
                if (sCache.TryGetValue(k, out a)) { c[62]++; return a == null ? null : (Array)a.Clone(); } // copy: callers may sort or edit theirs
                c[63]++;
                a = GetObjectsOrig(type, lotId);
                sCache[k] = a;
                return a == null ? null : (Array)a.Clone();
            }
            public static Array GetObjectsOrig(Type type, ulong lotId) { return null; } // receives Queries.GetObjects(Type, ulong)'s original header

            public static void DestroyHook(ObjectGuid scriptHandle)
            {
                if (sCache.Count > 0) { sCache.Clear(); PatchCounters()[64]++; }
                DestroyOrig(scriptHandle);
            }
            public static void DestroyOrig(ObjectGuid scriptHandle) { } // receives Simulator.DestroyObject's original header
        }
    }
}

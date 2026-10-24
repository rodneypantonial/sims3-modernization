using System;
using System.Collections.Generic;
using Sims3.Gameplay;
using Sims3.Gameplay.Core;
using Sims3.Gameplay.Objects;
using Sims3.SimIFace;

namespace Ts3Fixes
{
    // Season marker query cache (docs/74 §11 (a), I-74-9). GameObject.Dispose(bool) asks LotCurrent.GetObjects<SeasonalLotMarker>()
    // for every disposed object (IL+0x54, MethodSpec 0x2B000013); on the world lot that is WorldLot.GetObjects<T> ->
    // Queries.GetGlobalObjects<T> -> Sims3.SimIFace.Queries.GetGlobalObjects(Type) = Query_GetGlobalObjects, a walk over
    // the whole world (~100 ms per Dispose in Isla Paradiso: off-lot Sims leaving, traffic cars, jigs). For
    // typeof(SeasonalLotMarker) only, the hook answers from a live list: seeded by one real query per world load, kept by
    // postfixes on SeasonalLotMarker.OnStartup (add) and SeasonalLotMarker.Dispose (remove; after the body, so the marker's
    // own base Dispose still finds itself, as with the real query). Never while paused, in build/buy or outside live mode.
    // Every 8th answer is the real query, compared in order (Dispose uses objects[0]); any difference turns the cache off
    // for the rest of the session. Every other type falls through unchanged. Switch seasonmarkercache (tuning
    // kSeasonMarkerCache, default off). Counters: [270] answers from the list, [271] marker queries passed to the game
    // (switch on, not eligible), [272] verify checks, [273] verify mismatches, [274] seeds, [275] markers in the list.
    public static partial class Fixes
    {
        static bool sSeasonMarkerReady, sSeasonMarkerSeeded, sSeasonMarkerBroken;
        static int sSeasonMarkerCalls;
        static readonly Type sSeasonMarkerType = typeof(SeasonalLotMarker);
        static readonly List<SeasonalLotMarker> sSeasonMarkers = new List<SeasonalLotMarker>();

        static void ApplySeasonMarkerCache()
        {
            bool add = Patch(typeof(SeasonalLotMarker), "OnStartup", Type.EmptyTypes, typeof(SeasonMarkerHook), "OnStartupHook", "OnStartupOrig");
            bool remove = Patch(typeof(SeasonalLotMarker), "Dispose", Type.EmptyTypes, typeof(SeasonMarkerHook), "DisposeHook", "DisposeOrig");
            // without both list hooks the list goes stale: leave the query alone
            sSeasonMarkerReady = add && remove
                && Patch(typeof(Sims3.SimIFace.Queries), "GetGlobalObjects", new Type[] { typeof(Type) }, typeof(SeasonMarkerHook), "GlobalHook", "GlobalOrig");
        }

        internal static void SeasonMarkerWorldLoad() { sSeasonMarkerSeeded = false; }

        public class SeasonMarkerHook : SeasonalLotMarker
        {
            public void OnStartupHook()
            {
                OnStartupOrig();
                SeasonalLotMarker m = this;
                if (OnWorker()) { sSeasonMarkerSeeded = false; return; } // docs/128 G-M3: no list writes off the sim thread
                if (!sSeasonMarkers.Contains(m)) sSeasonMarkers.Add(m);
                PatchCounters()[275] = sSeasonMarkers.Count;
            }
            public void OnStartupOrig() { } // receives SeasonalLotMarker.OnStartup's original header

            public void DisposeHook()
            {
                try { DisposeOrig(); }
                finally
                {
                    SeasonalLotMarker m = this;
                    if (OnWorker()) sSeasonMarkerSeeded = false;
                    else { sSeasonMarkers.Remove(m); PatchCounters()[275] = sSeasonMarkers.Count; }
                }
            }
            public void DisposeOrig() { } // receives SeasonalLotMarker.Dispose's original header

            public static Array GlobalHook(Type type)
            {
                if (!ReferenceEquals(type, sSeasonMarkerType) || !sSeasonMarkerReady || sSeasonMarkerBroken || OnWorker() || !ShortcutOn("seasonmarkercache"))
                    return GlobalOrig(type);
                int[] c = PatchCounters();
                if (!GameStates.IsLiveState || GameStates.IsWorldShuttingDown || Paused() || LotManager.sIsBuildBuyModeOn)
                {
                    c[271]++;
                    return GlobalOrig(type);
                }
                Array real;
                if (!sSeasonMarkerSeeded)
                {
                    real = GlobalOrig(type);
                    sSeasonMarkers.Clear();
                    if (real != null)
                        foreach (object o in real) { SeasonalLotMarker m = o as SeasonalLotMarker; if (m != null) sSeasonMarkers.Add(m); }
                    sSeasonMarkerSeeded = true;
                    c[274]++; c[275] = sSeasonMarkers.Count;
                    return real;
                }
                if ((++sSeasonMarkerCalls & 7) == 0)
                {
                    real = GlobalOrig(type);
                    c[272]++;
                    if (!SameMarkers(real))
                    {
                        c[273]++;
                        sSeasonMarkerBroken = true;
                        AppliedLog().Add("seasonmarkercache: verify mismatch (list " + sSeasonMarkers.Count + ", game " + (real == null ? 0 : real.Length) + "), off for the session");
                    }
                    return real;
                }
                c[270]++;
                return sSeasonMarkers.ToArray(); // SeasonalLotMarker[]: Queries.GetGlobalObjects<T> casts with `as T[]`
            }
            public static Array GlobalOrig(Type type) { return null; } // receives Sims3.SimIFace.Queries.GetGlobalObjects(Type)'s original header

            static bool SameMarkers(Array real)
            {
                int n = real == null ? 0 : real.Length;
                if (n != sSeasonMarkers.Count) return false;
                for (int i = 0; i < n; i++)
                    if (!ReferenceEquals(real.GetValue(i), sSeasonMarkers[i])) return false;
                return true;
            }
        }
    }
}

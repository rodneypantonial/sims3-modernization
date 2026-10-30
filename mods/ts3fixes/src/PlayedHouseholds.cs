using System;
using System.Collections.Generic;
using Sims3.Gameplay;
using Sims3.Gameplay.CAS;
using Sims3.Gameplay.EventSystem;
using Sims3.Gameplay.Socializing;
using Sims3.Gameplay.Utilities;
using Sims3.SimIFace;

namespace Ts3Fixes
{
    // Played households and the lighter cleanup tier (docs/14 §8.6). Our data never goes into a save: the set of households
    // that were ever the active one lives in a sidecar file per save that ts3native keeps (native/ts3native/played.cpp,
    // <Documents>\Electronic Arts\The Sims 3\ts3native\played\<key>.txt, key = SaveCleanupCore.PlayedKey of the save's file
    // name), reached over the [UserDiag] text channel ("played: open|saveas|add|ask" lines, op 6 = the answer). Every household
    // that becomes the active one (world load, Edit Town household switch, return from travel, selecting a Sim of another
    // household: all send EventTypeId.kHouseholdSelected from PlumbBob.CheckForChangeInActiveHousehold) is added. Without the
    // store (no ts3native, an older build, [UserDiag] off) the set holds this session's households only and the aggressive
    // tier stands down. Sims of a played household and their genealogy relatives within kPlayedGenerations parent/child steps
    // of a living member get the lighter tier: kSaveCleanup rules, a larger picture cap (kPlayedSnapshotKeep), dead outfits
    // like everyone's dead; no history trim, no quiet-friend rule, no cleanse. Active-household members keep full protection.
    // Counters: [423] played households in the set, [424] ids added this session, [425] pictures removed under the lighter
    // tier, [426] aggressive actions skipped for played Sims. Every cleanup action on a Sim logs one ts3native.log line
    // "savecleanup: <part> tier=<basic|light|aggr|dead|cap> hh=<id> played=<0|1> gen=<steps or -1> near=<0|1> sim=<id>".
    public static partial class Fixes
    {
        static readonly Dictionary<ulong, bool> sPlayed = new Dictionary<ulong, bool>(); // this world's played set (store copy)
        static bool sPlayedStoreReady;  // the sidecar store answered "open": the set is complete
        static string sPlayedKey;       // sidecar key of the save the set belongs to
        static EventListener sPlayedListener;
        static EventHandler sPlayedQuit;
        static Dictionary<ulong, int> sPlayedNear; // description id -> genealogy steps to a living played member, built per pass

        static string PlayedSaveKey()
        {
            string id = null;
            try { using (LoadSaveFileInfo info = LoadSaveManager.GetCurrentSaveGameInfo()) id = info != null ? info.ID : null; }
            catch (Exception) { }
            return SaveCleanupCore.PlayedKey(id, GameUtils.GetCurrentWorld().ToString());
        }

        // World load (StartSaveCleanup): open this save's file, then ask it about every household of the world.
        static void PlayedWorldLoad()
        {
            if (sPlayedQuit == null) sPlayedQuit = delegate { sPlayed.Clear(); sPlayedStoreReady = false; sPlayedKey = null; sPlayedNear = null; };
            World.OnWorldQuitEventHandler -= sPlayedQuit;
            World.OnWorldQuitEventHandler += sPlayedQuit;
            sPlayed.Clear();
            sPlayedKey = PlayedSaveKey();
            sPlayedStoreReady = DiagLine("played: open " + sPlayedKey + " " + GameUtils.GetCurrentWorld()) && DiagSend(6, 0);
            if (sPlayedStoreReady)
                foreach (Household h in Household.sHouseholdList)
                    if (h != null && DiagLine("played: ask " + h.HouseholdId) && DiagSend(6, 0)) sPlayed[h.HouseholdId] = true;
            if (sPlayedListener != null) EventTracker.RemoveListener(sPlayedListener);
            sPlayedListener = EventTracker.AddListener(EventTypeId.kHouseholdSelected, delegate(Event e) { MarkPlayed(Household.ActiveHousehold); return ListenerAction.Keep; });
            MarkPlayed(Household.ActiveHousehold);
            PatchCounters()[423] = sPlayed.Count;
            DiagLine("savecleanup: played households " + sPlayed.Count + " at load (" + sPlayedKey + (sPlayedStoreReady ? ", read from the store" : ", store unavailable: aggressive tier off") + ")");
        }

        // Save As or the first save of a new game changes the save's key: the set is copied to the new key's file. Called after
        // every save, at every add and at the start of each pass (an asynchronous save may update the key later).
        static void PlayedSyncKey()
        {
            if (!sPlayedStoreReady || sPlayedKey == null) return;
            string key = PlayedSaveKey();
            if (key == sPlayedKey) return;
            sPlayedKey = key;
            DiagLine("played: saveas " + key + " " + GameUtils.GetCurrentWorld());
        }

        static void MarkPlayed(Household h)
        {
            if (h == null) return;
            try { PlayedSyncKey(); } catch (Exception) { }
            if (sPlayed.ContainsKey(h.HouseholdId)) return;
            sPlayed[h.HouseholdId] = true;
            if (sPlayedStoreReady) DiagLine("played: add " + h.HouseholdId);
            int[] c = PatchCounters();
            c[423] = sPlayed.Count;
            c[424]++;
            DiagLine("savecleanup: played household added hh=" + h.HouseholdId + " (" + sPlayed.Count + " played)");
        }

        static bool HouseholdPlayed(Household h)
        {
            return h != null && sPlayed.ContainsKey(h.HouseholdId);
        }

        // Genealogy steps (parent or child links) from the nearest living member of a played household, up to
        // kPlayedGenerations; every member of a played household is 0. Breadth-first, so each Sim gets its shortest distance.
        static Dictionary<ulong, int> BuildPlayedNear()
        {
            Dictionary<ulong, int> near = new Dictionary<ulong, int>();
            Dictionary<Genealogy, int> seen = new Dictionary<Genealogy, int>();
            Queue<Genealogy> queue = new Queue<Genealogy>();
            int max = Math.Max(0, Instantiator.kPlayedGenerations);
            foreach (Household h in Household.sHouseholdList)
            {
                if (!HouseholdPlayed(h)) continue;
                foreach (SimDescription d in h.AllSimDescriptions)
                {
                    if (d == null) continue;
                    near[d.SimDescriptionId] = 0;
                    if (!d.IsDead && d.Genealogy != null && !seen.ContainsKey(d.Genealogy)) { seen[d.Genealogy] = 0; queue.Enqueue(d.Genealogy); }
                }
            }
            while (queue.Count > 0)
            {
                Genealogy g = queue.Dequeue();
                int dist = seen[g];
                ulong id = GenealogyId(g);
                int had;
                if (id != 0 && (!near.TryGetValue(id, out had) || had > dist)) near[id] = dist;
                if (dist >= max) continue;
                List<Genealogy> next = new List<Genealogy>(g.Parents);
                next.AddRange(g.Children);
                foreach (Genealogy r in next)
                    if (r != null && !seen.ContainsKey(r)) { seen[r] = dist + 1; queue.Enqueue(r); }
            }
            return near;
        }

        static ulong GenealogyId(Genealogy g)
        {
            if (g.SimDescription != null) return g.SimDescription.SimDescriptionId;
            return g.IMiniSimDescription != null ? g.IMiniSimDescription.SimDescriptionId : 0;
        }

        // Steps to a living played member (<= kPlayedGenerations), or -1: the lighter tier applies when >= 0.
        static int PlayedGen(SimDescription d)
        {
            int g;
            return d != null && sPlayedNear != null && sPlayedNear.TryGetValue(d.SimDescriptionId, out g) ? g : -1;
        }

        static string ActFields(SimDescription d)
        {
            ulong hh = d != null && d.Household != null ? d.Household.HouseholdId : 0;
            int gen = PlayedGen(d);
            return " hh=" + hh + " played=" + (hh != 0 && HouseholdPlayed(d.Household) ? 1 : 0) + " gen=" + gen + " near=" + (gen >= 0 ? 1 : 0)
                + " sim=" + (d != null ? d.SimDescriptionId : 0);
        }

        // One ts3native.log line per cleanup action (the queue fails any "tier=aggr" line with near=1).
        static void CleanupAct(string part, string tier, SimDescription d)
        {
            DiagLine("savecleanup: " + part + " tier=" + tier + ActFields(d));
        }

        static void CleanupAct(string part, string tier, SimDescription a, SimDescription b)
        {
            DiagLine("savecleanup: " + part + " tier=" + tier + " a:" + ActFields(a) + " b:" + ActFields(b));
        }
    }
}

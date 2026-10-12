using System;
using System.Collections.Generic;
using System.Reflection;
using Sims3.Gameplay;
using Sims3.Gameplay.ActorSystems;
using Sims3.Gameplay.CAS;
using Sims3.Gameplay.Careers;
using Sims3.Gameplay.Socializing;
using Sims3.Gameplay.Utilities;
using Sims3.SimIFace;
using Sims3.UI.Controller;

namespace Ts3Fixes
{
    // Opt-in save cleanup (docs/14, ranks 1 and 2 of docs/09 4b), once per sim day at 2:00 (after the game's relationship
    // decay at 0:00 and its daily genealogy purge; before Overwatch's nightly run), never while saving, travelling or on
    // vacation. Off unless Instantiator.kSaveCleanup or the "savecleanup" test switch is on.
    //   (a) Weak NPC-NPC relationships: what the game's own decay (Relationship.DecayAll / CanRelationshipDecayAway) would
    //       drop if NPC decay were not 0.001x, removed with Relationship.RemoveRelationship (both directions).
    //   (b) MiniSimDescriptions of Sims from other worlds with no family links and no tie to the active household,
    //       removed with MiniSimDescription.RemoveMSD (unlinks the other sides' mini relationships).
    //   (c) Empty life-event lists (docs/14 7a): LifeEventManager keys whose List is empty; the game recreates one on demand.
    // Counters: [40] relationship pairs removed, [41] MiniSims removed, [42] cleanup runs, [43] runs skipped or failed,
    // [52] empty life-event lists removed.
    public static partial class Fixes
    {
        const float kCleanupHour = 2f;
        const float kWeakLikingBand = 10f;    // |liking| at most this; Stranger/Acquaintance already sit below Friend and above Disliked
        const float kQuietDays = 2f;          // not talked for this many sim days
        static AlarmHandle sCleanupAlarm = AlarmHandle.kInvalidHandle;

        static bool SaveCleanupOn()
        {
            ShortcutOn("savecleanup"); // makes sure the switch dictionary exists
            bool on;
            if (sSwitches.TryGetValue("savecleanup", out on)) return on;
            return Instantiator.kSaveCleanup; // not in ShortcutOn's list on purpose: its default there is "on"
        }

        // Called on every world load (Instantiator). The alarm is NeverPersisted, so it is not written into the save.
        public static void StartSaveCleanup()
        {
            if (AlarmManager.Global == null) return;
            if (sCleanupAlarm != AlarmHandle.kInvalidHandle) AlarmManager.Global.RemoveAlarm(sCleanupAlarm);
            sCleanupAlarm = AlarmManager.Global.AddAlarmDay(kCleanupHour, DaysOfTheWeek.All, DailySaveCleanup, "Ts3Fixes.SaveCleanup", AlarmType.NeverPersisted, null);
        }

        static void DailySaveCleanup()
        {
            if (!SaveCleanupOn()) return;
            try { RunSaveCleanupOnce(); } catch (Exception) { PatchCounters()[43]++; }
            try { RunLifeEventTrimOnce(); } catch (Exception) { PatchCounters()[43]++; }
        }

        // (c) On demand for the test agent too. Only empty lists go: every reader uses TryGetValue and creates a missing
        // list again (LifeEventManager.ProcessLifeEvent, GetActiveNode), and export/import just walk the dictionaries.
        public static string RunLifeEventTrimOnce()
        {
            int[] c = PatchCounters();
            string unsafeWhy = CleanupUnsafe();
            if (unsafeWhy != null) { c[43]++; return "skipped: " + unsafeWhy; }
            string before = LifeEventCounts();
            int lists = 0;
            foreach (SimDescription d in Household.EverySimDescription())
            {
                if (d == null || d.LifeEventManager == null) continue;
                lists += RemoveEmptyLists(LifeEventDict(d.LifeEventManager, "mLifeEvents")) + RemoveEmptyLists(LifeEventDict(d.LifeEventManager, "mActiveNodes"));
            }
            c[52] += lists;
            string line = "life-event trim: -" + lists + " lists | before " + before + " | after " + LifeEventCounts();
            ShortcutLog(line);
            AppDomain.CurrentDomain.SetData("ts3fixes.lifeeventtrim", line);
            return line;
        }

        // lists/empty over both dictionaries of every Sim; events = LifeEvent objects; visible/snaps for the active household.
        public static string LifeEventCounts()
        {
            int lists = 0, empty = 0, events = 0, visible = 0, snaps = 0;
            foreach (SimDescription d in Household.EverySimDescription())
            {
                if (d == null || d.LifeEventManager == null) continue;
                foreach (string field in new string[] { "mLifeEvents", "mActiveNodes" })
                {
                    System.Collections.IDictionary dict = LifeEventDict(d.LifeEventManager, field);
                    if (dict == null) continue;
                    foreach (System.Collections.ICollection list in dict.Values)
                    {
                        lists++;
                        if (list == null) continue;
                        if (list.Count == 0) empty++;
                        else if (field == "mLifeEvents") events += list.Count;
                    }
                }
            }
            if (Household.ActiveHousehold != null)
                foreach (SimDescription d in Household.ActiveHousehold.AllSimDescriptions)
                {
                    if (d.LifeEventManager == null) continue;
                    foreach (Sims3.UI.Hud.ILifeEvent e in d.LifeEventManager.GetVisibleLifeEvents())
                    {
                        visible++;
                        if (e.HasSnapshot) snaps++;
                    }
                }
            return "lists=" + lists + " empty=" + empty + " events=" + events + " visible=" + visible + " snaps=" + snaps
                + " hh=" + Household.sHouseholdList.Count + " sims=" + Household.EverySimDescription().Count;
        }

        // Private Dictionary<uint, List<T>>; T (LifeEventActiveNode) is a private type, so use the non-generic view.
        static System.Collections.IDictionary LifeEventDict(LifeEventManager m, string field)
        {
            FieldInfo f = typeof(LifeEventManager).GetField(field, BindingFlags.Instance | BindingFlags.NonPublic);
            return f != null ? f.GetValue(m) as System.Collections.IDictionary : null;
        }

        static int RemoveEmptyLists(System.Collections.IDictionary dict)
        {
            if (dict == null) return 0;
            List<object> drop = new List<object>();
            foreach (System.Collections.DictionaryEntry kv in dict)
            {
                System.Collections.ICollection list = kv.Value as System.Collections.ICollection;
                if (list != null && list.Count == 0) drop.Add(kv.Key); // null values are left alone: not ours to judge
            }
            foreach (object key in drop) dict.Remove(key);
            return drop.Count;
        }

        // On demand for the test agent (reflect_call); ignores the tunable but keeps every safety check.
        public static string RunSaveCleanupOnce()
        {
            int[] c = PatchCounters();
            string unsafeWhy = CleanupUnsafe();
            if (unsafeWhy != null) { c[43]++; return "skipped: " + unsafeWhy; }
            string before = SaveCleanupCounts();
            int pairs = PruneWeakRelationships(), minis = PruneMiniSims();
            c[40] += pairs; c[41] += minis; c[42]++;
            string line = "save cleanup: -" + pairs + " pairs, -" + minis + " minis | before " + before + " | after " + SaveCleanupCounts();
            ShortcutLog(line);
            AppDomain.CurrentDomain.SetData("ts3fixes.savecleanup", line);
            return line;
        }

        static string CleanupUnsafe()
        {
            if (!GameStates.IsInWorld() || GameStates.IsGameShuttingDown) return "no world";
            if (GameStates.IsTravelling || GameStates.IsOnVacation) return "travelling or on vacation";
            Sims3.Gameplay.UI.OptionsModel options = Sims3.Gameplay.UI.Responder.Instance != null ? Sims3.Gameplay.UI.Responder.Instance.OptionsModel as Sims3.Gameplay.UI.OptionsModel : null;
            if (options != null && options.SaveGameInProgress) return "saving";
            if (Household.ActiveHousehold == null) return "no active household";
            return null;
        }

        // Counts for before/after checks; also callable by the test agent.
        public static string SaveCleanupCounts()
        {
            int keys = 0, pairs = 0;
            foreach (KeyValuePair<SimDescription, Dictionary<SimDescription, Relationship>> kv in Relationship.sAllRelationships)
            {
                keys++;
                pairs += kv.Value.Count;
            }
            Dictionary<ulong, MiniSimDescription> minis = MiniSims();
            return "rel=" + keys + " pairs=" + (pairs / 2) + " mini=" + (minis != null ? minis.Count : 0)
                + " hh=" + Household.sHouseholdList.Count + " sims=" + Household.EverySimDescription().Count;
        }

        static Dictionary<ulong, MiniSimDescription> MiniSims()
        {
            FieldInfo f = typeof(MiniSimDescription).GetField("sMiniSims", BindingFlags.Static | BindingFlags.NonPublic);
            return f != null ? f.GetValue(null) as Dictionary<ulong, MiniSimDescription> : null;
        }

        // (a) Pairs the game's own decay would drop for players (Stranger/Acquaintance, other household, not related, no
        // working relation), plus: both NPCs outside the active household, alive, near-zero liking, quiet for 2 days, never
        // romantic, not partners, not step/in-law/future family, same exemptions as DecayAll (long-distance friend trait,
        // boarding school).
        static int PruneWeakRelationships()
        {
            Household active = Household.ActiveHousehold;
            List<Relationship> drop = new List<Relationship>();
            foreach (KeyValuePair<SimDescription, Dictionary<SimDescription, Relationship>> kv in Relationship.sAllRelationships)
            {
                SimDescription a = kv.Key;
                if (!NpcForCleanup(a, active)) continue;
                foreach (KeyValuePair<SimDescription, Relationship> inner in kv.Value)
                {
                    SimDescription b = inner.Key;
                    if (a.SimDescriptionId >= b.SimDescriptionId) continue; // each pair once
                    if (NpcForCleanup(b, active) && WeakPair(inner.Value, a, b)) drop.Add(inner.Value);
                }
            }
            int n = 0;
            foreach (Relationship r in drop)
            {
                Dictionary<SimDescription, Relationship> da, db;
                if (!Relationship.sAllRelationships.TryGetValue(r.SimDescriptionA, out da) || !da.ContainsKey(r.SimDescriptionB)) continue;
                if (!Relationship.sAllRelationships.TryGetValue(r.SimDescriptionB, out db) || !db.ContainsKey(r.SimDescriptionA)) continue;
                Relationship.RemoveRelationship(r);
                n++;
            }
            return n;
        }

        static bool NpcForCleanup(SimDescription d, Household active)
        {
            if (d == null || !d.IsValidDescription || d.IsDead || d.Household == null || d.Household == active) return false;
            if (d.CreatedSim != null && d.CreatedSim.IsSelectable) return false;
            if (d.Genealogy == null || d.TraitManager == null || d.TraitManager.HasElement(TraitNames.LongDistanceFriend)) return false;
            if (d.IsEnrolledInBoardingSchool() || GameStates.IsIdTravelling(d.SimDescriptionId)) return false;
            return true;
        }

        static bool WeakPair(Relationship r, SimDescription a, SimDescription b)
        {
            if (r == null || r.LTR == null || r.SimDescriptionA == null || r.SimDescriptionB == null) return false;
            LongTermRelationshipTypes ltr = r.LTR.CurrentLTR;
            if (ltr != LongTermRelationshipTypes.Stranger && ltr != LongTermRelationshipTypes.Acquaintance) return false;
            if (Math.Abs(r.LTR.Liking) > kWeakLikingBand) return false;
            if (r.AreRomantic() || r.WasRomantic()) return false; // any romance visibility state, now or before
            if (a.Household == b.Household || a.Partner == b || b.Partner == a) return false;
            Genealogy ga = a.Genealogy, gb = b.Genealogy;
            if (ga.Spouse == gb || ga.IsSufficientlyRelatedToRuleOutRomance(gb) || gb.IsSufficientlyRelatedToRuleOutRomance(ga)) return false;
            if (Genealogy.IsParentInLaw(ga, gb) || Genealogy.IsParentInLaw(gb, ga) || Genealogy.IsSiblingInLaw(ga, gb)) return false;
            if (Occupation.HaveWorkingRelation(a, b)) return false;
            if (SimClock.ElapsedTime(TimeUnit.Hours, r.LTR.WhenStateStarted) < SocialComponent.MinHoursToHoldRelationshipState) return false;
            if (r.WhenLastTalked.Ticks != 0 && SimClock.ElapsedTime(TimeUnit.Days, r.WhenLastTalked) < kQuietDays) return false;
            return true;
        }

        // (b) MiniSims from other worlds: not this world's Sims, no genealogy links, no protection, no partner, not a
        // playable ghost, not travelling, no active-household member in its household list, no mini relationship to or
        // from the active household, and every mini relationship it has is a plain Stranger/Acquaintance one.
        static int PruneMiniSims()
        {
            Dictionary<ulong, MiniSimDescription> minis = MiniSims();
            if (minis == null) return 0;
            WorldName here = GameUtils.GetCurrentWorld();
            Dictionary<ulong, bool> keep = new Dictionary<ulong, bool>();
            foreach (SimDescription d in SimDescription.GetSimDescriptionsInWorld()) keep[d.SimDescriptionId] = true;
            if (GameStates.TravelerIds != null) foreach (ulong id in GameStates.TravelerIds) keep[id] = true;
            foreach (SimDescription d in Household.ActiveHousehold.AllSimDescriptions)
            {
                keep[d.SimDescriptionId] = true;
                MiniSimDescription own;
                if (minis.TryGetValue(d.SimDescriptionId, out own) && own != null)
                    foreach (MiniRelationship mr in own.MiniRelationships) keep[mr.SimDescriptionId] = true;
            }
            List<ulong> drop = new List<ulong>();
            foreach (KeyValuePair<ulong, MiniSimDescription> kv in minis)
            {
                MiniSimDescription m = kv.Value;
                if (m == null || keep.ContainsKey(kv.Key) || m.HomeWorld == here || m.Instantiated) continue;
                if (m.ProtectionFlags != MiniSimDescription.ProtectionFlag.None || m.PartnerId != 0 || m.IsPlayableGhost) continue;
                if (HasFamilyLinks(m.Genealogy) || TiedToKept(m, keep)) continue;
                drop.Add(kv.Key);
            }
            foreach (ulong id in drop) MiniSimDescription.RemoveMSD(id);
            return drop.Count;
        }

        static bool HasFamilyLinks(Genealogy g)
        {
            return g != null && (g.Spouse != null || g.Parents.Count > 0 || g.Children.Count > 0 || g.ISiblings.Count > 0);
        }

        static bool TiedToKept(MiniSimDescription m, Dictionary<ulong, bool> keep)
        {
            if (m.HouseholdMembers != null)
                foreach (ulong id in m.HouseholdMembers) if (keep.ContainsKey(id) && id != m.SimDescriptionId) return true;
            foreach (MiniRelationship mr in m.MiniRelationships)
            {
                if (keep.ContainsKey(mr.SimDescriptionId)) return true;
                if (mr.CurrentLtr != LongTermRelationshipTypes.Stranger && mr.CurrentLtr != LongTermRelationshipTypes.Acquaintance
                    && mr.CurrentLtr != LongTermRelationshipTypes.Undefined) return true;
                if (mr.RomanceState != RomanceVisibilityStateType.None) return true;
            }
            return false;
        }
    }
}

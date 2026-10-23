using System;
using System.Collections.Generic;
using Sims3.Gameplay.Actors;
using Sims3.Gameplay.ActorSystems;
using Sims3.Gameplay.Autonomy;
using Sims3.Gameplay.Careers;
using Sims3.Gameplay.CAS;
using Sims3.Gameplay.Core;
using Sims3.Gameplay.DreamsAndPromises;
using Sims3.Gameplay.Interactions;
using Sims3.Gameplay.Skills;
using Sims3.Gameplay.Socializing;
using Sims3.UI.Controller;

namespace Ts3Agent
{
    // Sims, households, needs, skills, traits, moodlets, relationships, careers.
    public static partial class Handlers
    {
        static void RegisterSims()
        {
            Dispatcher.Register("list_households", ListHouseholds);
            Dispatcher.Register("list_sims", ListSims);
            Dispatcher.Register("sim_details", SimDetails);
            Dispatcher.Register("set_active_sim", SetActiveSim);
            Dispatcher.Register("household_funds", HouseholdFunds);
            Dispatcher.Register("set_motive", SetMotive);
            Dispatcher.Register("autonomy", AutonomyCmd);
            Dispatcher.Register("set_skill", SetSkill);
            Dispatcher.Register("skill_options", SkillOptions);
            Dispatcher.Register("set_trait", SetTrait);
            Dispatcher.Register("trait_options", TraitOptions);
            Dispatcher.Register("moodlet", Moodlet);
            Dispatcher.Register("lifetime_happiness", LifetimeHappiness);
            Dispatcher.Register("relationships", Relationships);
            Dispatcher.Register("modify_relationship", ModifyRelationship);
            Dispatcher.Register("career", CareerCmd);
            Dispatcher.Register("age_up", AgeUp);
        }

        // ------------------------------------------------------------ households & sims

        static object ListHouseholds(Args a)
        {
            List<object> list = new List<object>();
            Household active = Household.ActiveHousehold;
            foreach (Household h in Household.sHouseholdList)
            {
                if (h == null) continue;
                bool special = h.IsServiceNpcHousehold || h.IsPetHousehold || h.IsTouristHousehold
                               || h.IsAlienHousehold || h.IsMermaidHousehold || h.IsTravelHousehold;
                if (special && !a.Bool("include_special", false)) continue;
                Dictionary<string, object> d = Util.Obj();
                d["id"] = Util.Id(h.HouseholdId);
                d["name"] = h.Name;
                d["funds"] = h.FamilyFunds;
                d["members"] = h.NumMembers;
                d["active"] = h == active;
                if (h.LotHome != null) d["home_lot"] = Util.LotName(h.LotHome);
                else d["homeless"] = true;
                list.Add(d);
            }
            return list;
        }

        static object ListSims(Args a)
        {
            string scope = a.Str("scope", "household").ToLowerInvariant();
            List<SimDescription> sds = new List<SimDescription>();
            if (scope == "world")
            {
                sds = SimDescription.GetSimDescriptionsInWorld();
            }
            else if (scope == "lot")
            {
                Sim me = Util.ActiveSim();
                if (me.LotCurrent != null)
                    foreach (Sim s in me.LotCurrent.GetSims()) sds.Add(s.SimDescription);
            }
            else
            {
                Household hh = Household.ActiveHousehold;
                if (hh == null) throw new McpException("no active household");
                sds = hh.AllSimDescriptions;
            }
            List<object> list = new List<object>();
            foreach (SimDescription sd in sds)
            {
                if (sd == null) continue;
                list.Add(Util.SimSummary(sd));
                if (list.Count >= 500) break;
            }
            return list;
        }

        static object SimDetails(Args a)
        {
            SimDescription sd = Util.FindSimDescription(a, "sim");
            Dictionary<string, object> d = Util.SimSummary(sd);
            if (sd.Household != null)
            {
                d["household"] = sd.Household.Name;
                d["household_funds"] = sd.Household.FamilyFunds;
            }
            List<object> traits = new List<object>();
            foreach (Trait t in sd.TraitManager.List)
                if (t.IsVisible) traits.Add(t.TraitName(sd.IsFemale));
            d["traits"] = traits;
            d["lifetime_happiness"] = sd.LifetimeHappiness;
            d["spendable_happiness"] = sd.SpendableHappiness;
            Sim s = sd.CreatedSim;
            if (s != null)
            {
                d["motives"] = MotiveValues(s);
                if (s.MoodManager != null)
                {
                    d["mood"] = s.MoodManager.MoodValue;
                    d["mood_flavor"] = s.MoodManager.MoodFlavor.ToString();
                }
                d["moodlets"] = BuffList(s);
                try
                {
                    DreamsAndPromisesManager dm = s.DreamsAndPromisesManager;
                    if (dm != null)
                    {
                        if (dm.LifetimeWishNode != null) d["lifetime_wish"] = dm.LifetimeWishNode.GetName();
                        List<object> wishes = new List<object>();
                        foreach (ActiveDreamNode n in dm.GetPromisedNodes())
                            if (n != null) wishes.Add(n.GetName() + " (+" + n.AchievementPoints + " LTH)");
                        d["promised_wishes"] = wishes;
                    }
                }
                catch (Exception) { }
                d["queue"] = QueueList(s);
            }
            d["skills"] = SkillList(sd.SkillManager);
            Occupation job = sd.Occupation;
            if (job != null) d["career"] = CareerInfo(job);
            return d;
        }

        static object SetActiveSim(Args a)
        {
            Sim s = Util.FindSim(a, "sim");
            if (!s.IsSelectable) throw new McpException(s.FullName + " is not selectable (not in your household?)");
            if (!PlumbBob.SelectActor(s)) throw new McpException("the game refused to select " + s.FullName);
            return Util.SimSummary(s.SimDescription);
        }

        static object HouseholdFunds(Args a)
        {
            Household hh;
            if (a.Has("household"))
            {
                string q = a.Str("household");
                hh = null;
                foreach (Household h in Household.sHouseholdList)
                    if (h != null && (string.Compare(h.Name, q, true) == 0 || Util.Id(h.HouseholdId) == q)) { hh = h; break; }
                if (hh == null) throw new McpException("no household named '" + q + "'");
            }
            else
            {
                hh = Household.ActiveHousehold;
                if (hh == null) throw new McpException("no active household");
            }
            int before = hh.FamilyFunds;
            if (a.Has("set")) hh.SetFamilyFunds((int)a.Long("set"));
            else if (a.Has("add")) hh.ModifyFamilyFunds((int)a.Long("add"));
            Dictionary<string, object> d = Util.Obj();
            d["household"] = hh.Name;
            d["before"] = before;
            d["funds"] = hh.FamilyFunds;
            return d;
        }

        // ------------------------------------------------------------ motives & moodlets

        static readonly CommodityKind[] kShownMotives = {
            CommodityKind.Hunger, CommodityKind.Bladder, CommodityKind.Energy, CommodityKind.Social,
            CommodityKind.Hygiene, CommodityKind.Fun, CommodityKind.VampireThirst, CommodityKind.AlienBrainPower,
            CommodityKind.MermaidDermalHydration, CommodityKind.BatteryPower, CommodityKind.DogDestruction,
            CommodityKind.CatScratch, CommodityKind.HorseExercise, CommodityKind.HorseThirst, CommodityKind.Temperature };

        public static Dictionary<string, object> MotiveValues(Sim s)
        {
            Dictionary<string, object> m = Util.Obj();
            if (s.Motives == null) return m;
            foreach (CommodityKind k in kShownMotives)
                if (s.Motives.HasMotive(k)) m[k.ToString()] = Util.Round(s.Motives.GetValue(k));
            return m;
        }

        public static List<object> BuffList(Sim s)
        {
            List<object> list = new List<object>();
            if (s.BuffManager == null) return list;
            foreach (BuffInstance b in s.BuffManager.List)
            {
                Dictionary<string, object> d = Util.Obj();
                d["id"] = b.Guid.ToString();
                d["name"] = Util.Localize(s.IsFemale, b.mBuffName, s, b.BuffNameParameter3);
                d["mood"] = b.EffectValue;
                if (b.TimeoutCount >= 0) d["minutes_left"] = Util.Round(b.TimeoutCount);
                list.Add(d);
            }
            return list;
        }

        static object SetMotive(Args a)
        {
            List<Sim> sims = new List<Sim>();
            if (a.Str("sim", "") == "household")
            {
                Household hh = Household.ActiveHousehold;
                if (hh == null) throw new McpException("no active household");
                sims.AddRange(hh.AllActors);
            }
            else sims.Add(Util.FindSim(a, "sim"));
            string which = a.Str("motive", "all");
            foreach (Sim s in sims)
            {
                Motives m = s.Motives;
                if (m == null) continue;
                if (which.ToLowerInvariant() == "all")
                {
                    if (a.Has("value"))
                    {
                        foreach (CommodityKind k in kShownMotives)
                            if (m.HasMotive(k) && k != CommodityKind.Temperature) m.CheatValue(k, (float)a.Double("value"));
                    }
                    else
                    {
                        foreach (CommodityKind k in kShownMotives)
                            if (m.HasMotive(k)) m.SetMax(k);
                    }
                }
                else
                {
                    CommodityKind k = OccultMotive(m, Util.ParseEnum<CommodityKind>(which));
                    if (!m.HasMotive(k)) throw new McpException(s.FullName + " has no motive " + k);
                    if (a.Has("value")) m.CheatValue(k, (float)a.Double("value"));
                    else m.SetMax(k);
                }
            }
            Dictionary<string, object> r = Util.Obj();
            foreach (Sim s in sims) r[s.FullName] = MotiveValues(s);
            return r;
        }

        // A vampire has VampireThirst in place of Hunger (story-smoke 2026-10-23: "Wogan Hemlock has no motive Hunger"); other
        // motives pass through and a Sim without them gets the "has no motive" error (story engine: a skip).
        internal static CommodityKind OccultMotive(Motives m, CommodityKind k)
        {
            if (k == CommodityKind.Hunger && !m.HasMotive(k) && m.HasMotive(CommodityKind.VampireThirst)) return CommodityKind.VampireThirst;
            return k;
        }

        // autonomy {sim?: name|id|active|household, enabled: bool}: the game's own switch, Sim.Autonomy.Increment/
        // DecrementAutonomyDisabled (a counter). Every increment of ours is tracked per SimDescriptionId in AppDomain data
        // (survives world switches and agent reloads), so enabled: true only takes back what this command added; enabled:
        // true without sim restores every tracked Sim in the world. The counter is persisted (Autonomy.mAutonomyDisabledCount
        // has no [Persistable(false)]), so a save made while disabled keeps it: restore before saving; Sims not in the world
        // stay tracked (`pending`) until a later restore finds them.
        static object AutonomyCmd(Args a)
        {
            AppDomain d = AppDomain.CurrentDomain;
            Dictionary<ulong, int> off = d.GetData("ts3agent.autonomy_off") as Dictionary<ulong, int>;
            if (off == null) { off = new Dictionary<ulong, int>(); d.SetData("ts3agent.autonomy_off", off); }
            bool enable = a.Bool("enabled", true);
            List<SimDescription> sds = new List<SimDescription>();
            if (a.Str("sim", "") == "household")
            {
                Household hh = Household.ActiveHousehold;
                if (hh == null) throw new McpException("no active household");
                foreach (Sim s in hh.AllActors) sds.Add(s.SimDescription);
            }
            else if (a.Has("sim")) sds.Add(Util.FindSimDescription(a, "sim"));
            else if (enable) foreach (ulong id in new List<ulong>(off.Keys)) { SimDescription sd = SimDescription.Find(id); if (sd != null) sds.Add(sd); }
            else throw new McpException("enabled: false needs sim (name, id, active or household)");
            List<object> changed = new List<object>();
            foreach (SimDescription sd in sds)
            {
                Sim s = sd.CreatedSim;
                if (s == null || s.Autonomy == null) continue;
                int n;
                off.TryGetValue(sd.SimDescriptionId, out n);
                if (!enable && n == 0) { s.Autonomy.IncrementAutonomyDisabled(); off[sd.SimDescriptionId] = 1; changed.Add(sd.FullName); }
                else if (enable && n > 0)
                {
                    for (int i = 0; i < n; i++) s.Autonomy.DecrementAutonomyDisabled(); // the game's Decrement stops at 0
                    off.Remove(sd.SimDescriptionId);
                    changed.Add(sd.FullName);
                }
            }
            Dictionary<string, object> r = Util.Obj();
            r["enabled"] = enable;
            r["changed"] = changed;
            List<object> pending = new List<object>();
            foreach (ulong id in off.Keys) { SimDescription sd = SimDescription.Find(id); pending.Add(sd != null ? sd.FullName : Util.Id(id)); }
            r["tracked"] = pending; // still disabled by us (enabled: true without sim takes them back once in the world)
            Dictionary<string, object> now = Util.Obj();
            foreach (SimDescription sd in sds) if (sd.CreatedSim != null && sd.CreatedSim.Autonomy != null) now[sd.FullName] = !sd.CreatedSim.Autonomy.AutonomyDisabled;
            r["autonomy_on"] = now;
            return r;
        }

        static object Moodlet(Args a)
        {
            Sim s = Util.FindSim(a, "sim");
            string action = a.Str("action", "list").ToLowerInvariant();
            if (action == "add")
            {
                BuffNames b = Util.ParseEnum<BuffNames>(a.Str("moodlet"));
                bool ok = a.Has("minutes")
                    ? s.BuffManager.AddElement(b, (float)a.Double("minutes"), Origin.None)
                    : s.BuffManager.AddElement(b, Origin.None);
                if (!ok) throw new McpException("the game refused to add moodlet " + b + " (wrong age/occult, or blocked by another moodlet)");
            }
            else if (action == "remove")
            {
                s.BuffManager.ForceRemoveBuff(Util.ParseEnum<BuffNames>(a.Str("moodlet")));
            }
            else if (action == "search")
            {
                string q = a.Str("query", "").ToLowerInvariant();
                List<string> names = new List<string>();
                foreach (string n in Enum.GetNames(typeof(BuffNames)))
                    if (n.ToLowerInvariant().IndexOf(q) >= 0) names.Add(n);
                return names;
            }
            Dictionary<string, object> r = Util.Obj();
            r["mood"] = s.MoodManager.MoodValue;
            r["moodlets"] = BuffList(s);
            return r;
        }

        // ------------------------------------------------------------ skills & traits

        static List<object> SkillList(SkillManager sm)
        {
            List<object> list = new List<object>();
            if (sm == null) return list;
            foreach (Skill k in sm.List)
            {
                if (k.IsHiddenSkill()) continue;
                Dictionary<string, object> d = Util.Obj();
                d["id"] = k.Guid.ToString();
                d["name"] = k.Name;
                d["level"] = k.SkillLevel;
                d["max"] = k.MaxSkillLevel;
                list.Add(d);
            }
            return list;
        }

        static object SetSkill(Args a)
        {
            SimDescription sd = Util.FindSimDescription(a, "sim");
            SkillNames name = Util.ParseEnum<SkillNames>(a.Str("skill"));
            int level = (int)a.Long("level");
            Skill k = sd.SkillManager.AddElement(name);
            if (k == null) throw new McpException(sd.FullName + " cannot learn " + name + " (age or expansion restriction)");
            if (level < k.SkillLevel) k.ResetStats();
            if (level > 0) k.ForceSkillLevelUp(Math.Min(level, k.MaxSkillLevel));
            Dictionary<string, object> r = Util.Obj();
            r["skill"] = k.Name;
            r["level"] = k.SkillLevel;
            r["max"] = k.MaxSkillLevel;
            return r;
        }

        static object SkillOptions(Args a)
        {
            SimDescription sd = Util.FindSimDescription(a, "sim");
            List<object> list = new List<object>();
            foreach (SkillNames n in Enum.GetValues(typeof(SkillNames)))
            {
                if (n == SkillNames.None) continue;
                Skill st = SkillManager.GetStaticSkill(n);
                bool hidden;
                try { hidden = st == null || st.IsHiddenSkill(); }
                catch (Exception) { hidden = true; }  // skills from packs that aren't installed
                if (hidden) continue;
                Dictionary<string, object> d = Util.Obj();
                d["id"] = n.ToString();
                d["name"] = Skill.GetLocalizedSkillName(n, sd);
                d["level"] = sd.SkillManager.GetSkillLevel(n);
                list.Add(d);
            }
            return list;
        }

        static object SetTrait(Args a)
        {
            SimDescription sd = Util.FindSimDescription(a, "sim");
            TraitNames name = Util.ParseEnum<TraitNames>(a.Str("trait"));
            Trait t = TraitManager.GetTraitFromDictionary(name);
            if (t == null) throw new McpException("trait " + name + " is not available (missing expansion?)");
            if (a.Bool("remove", false))
            {
                if (!sd.TraitManager.HasElement(name)) throw new McpException(sd.FullName + " does not have " + name);
                sd.RemoveTrait(t);
            }
            else
            {
                Trait conflict;
                if (sd.TraitManager.HasElement(name)) throw new McpException(sd.FullName + " already has " + name);
                if (sd.TraitManager.IsConflictingTrait(name, out conflict))
                    throw new McpException(name + " conflicts with " + (conflict != null ? conflict.TraitName(sd.IsFemale) : "an existing trait"));
                if (!t.IsReward && sd.TraitManager.TraitsMaxed())
                    throw new McpException(sd.FullName + " already has the maximum number of traits; remove one first");
                if (!sd.AddTrait(t)) throw new McpException("the game refused to add " + name);
            }
            if (sd.CreatedSim != null && sd.CreatedSim.SocialComponent != null) sd.CreatedSim.SocialComponent.UpdateTraits();
            List<object> traits = new List<object>();
            foreach (Trait x in sd.TraitManager.List) if (x.IsVisible) traits.Add(x.TraitName(sd.IsFemale));
            return traits;
        }

        static object TraitOptions(Args a)
        {
            SimDescription sd = Util.FindSimDescription(a, "sim");
            bool rewards = a.Bool("rewards", false);
            List<object> list = new List<object>();
            foreach (Trait t in TraitManager.GetDictionaryTraits)
            {
                if (!t.IsVisible || t.IsReward != rewards) continue;
                Dictionary<string, object> d = Util.Obj();
                d["id"] = t.Guid.ToString();
                d["name"] = t.TraitName(sd.IsFemale);
                d["has"] = sd.TraitManager.HasElement(t.Guid);
                if (rewards) d["cost"] = t.Score;
                list.Add(d);
            }
            return list;
        }

        static object LifetimeHappiness(Args a)
        {
            SimDescription sd = Util.FindSimDescription(a, "sim");
            if (a.Has("add"))
            {
                float add = (float)a.Double("add");
                if (sd.CreatedSim == null || !sd.CreatedSim.IsSelectable)
                    throw new McpException("lifetime happiness can only be granted to a sim in your household");
                if (add > 0) sd.IncrementLifetimeHappiness(add);
                else sd.SpendLifetimeHappiness(-add);
            }
            if (a.Has("buy_reward"))
            {
                Trait rt = TraitManager.GetTraitFromDictionary(Util.ParseEnum<TraitNames>(a.Str("buy_reward")));
                if (rt == null || !rt.IsReward) throw new McpException("not a lifetime reward trait");
                if (sd.TraitManager.HasElement(rt.Guid)) throw new McpException(sd.FullName + " already has that reward");
                if (sd.SpendableHappiness < (ulong)rt.Score)
                    throw new McpException("not enough points: need " + rt.Score + ", have " + sd.SpendableHappiness);
                sd.SpendLifetimeHappiness(rt.Score);
                sd.TraitManager.AddElement(rt.Guid);
                sd.TraitManager.AddTraitEffects(sd, rt.Guid);
            }
            Dictionary<string, object> r = Util.Obj();
            r["lifetime_happiness"] = sd.LifetimeHappiness;
            r["spendable"] = sd.SpendableHappiness;
            return r;
        }

        // ------------------------------------------------------------ relationships

        static object Relationships(Args a)
        {
            SimDescription sd = Util.FindSimDescription(a, "sim");
            List<object> list = new List<object>();
            foreach (Relationship r in Relationship.GetRelationships(sd))
            {
                if (r == null || r.LTR == null) continue;
                SimDescription o = r.GetOtherSimDescription(sd);
                if (o == null) continue;
                Dictionary<string, object> d = Util.Obj();
                d["sim"] = o.FullName;
                d["id"] = Util.Id(o.SimDescriptionId);
                d["liking"] = Util.Round(r.LTR.Liking);
                d["state"] = r.LTR.CurrentLTR.ToString();
                try { d["label"] = LTRData.Get(r.LTR.CurrentLTR).GetName(sd, o); } catch (Exception) { }
                list.Add(d);
            }
            list.Sort(delegate(object x, object y)
            {
                return ((float)((Dictionary<string, object>)y)["liking"]).CompareTo((float)((Dictionary<string, object>)x)["liking"]);
            });
            return list;
        }

        static object ModifyRelationship(Args a)
        {
            SimDescription me = Util.FindSimDescription(a, "sim");
            SimDescription other = Util.FindSimDescription(a, "other");
            if (me == other) throw new McpException("a sim cannot have a relationship with themselves");
            Relationship r = Relationship.Get(me, other, true);
            if (r == null || r.LTR == null) throw new McpException("could not create a relationship");
            if (a.Has("state")) r.LTR.ForceChangeState(Util.ParseEnum<LongTermRelationshipTypes>(a.Str("state")));
            if (a.Has("liking")) r.LTR.SetLiking((float)a.Double("liking"));
            else if (a.Has("add_liking")) r.LTR.SetLiking(r.LTR.Liking + (float)a.Double("add_liking"));
            Dictionary<string, object> d = Util.Obj();
            d["sim"] = me.FullName;
            d["other"] = other.FullName;
            d["liking"] = Util.Round(r.LTR.Liking);
            d["state"] = r.LTR.CurrentLTR.ToString();
            return d;
        }

        // ------------------------------------------------------------ careers & aging

        static Dictionary<string, object> CareerInfo(Occupation job)
        {
            Dictionary<string, object> d = Util.Obj();
            d["id"] = job.Guid.ToString();
            d["name"] = job.CareerName;
            d["title"] = job.CurLevelJobTitle;
            d["level"] = job.CareerLevel;
            d["pay_per_hour"] = Util.Round(job.PayPerHourOrStipend);
            d["start_hour"] = Util.Round(job.StartTime);
            d["hours"] = Util.Round(job.DayLength);
            d["work_days"] = job.DaysOfWeekToWork.ToString();
            d["hours_until_work"] = Util.Round(job.HoursUntilWork);
            try { d["performance"] = Util.Round(job.Performance); } catch (Exception) { }
            return d;
        }

        static object CareerCmd(Args a)
        {
            SimDescription sd = Util.FindSimDescription(a, "sim");
            string action = a.Str("action", "info").ToLowerInvariant();
            Occupation job = sd.Occupation;
            switch (action)
            {
                case "info":
                    break;
                case "options":
                {
                    List<object> list = new List<object>();
                    foreach (Occupation o in CareerManager.OccupationList)
                    {
                        Career c = o as Career;
                        if (c == null) continue;
                        Dictionary<string, object> d = Util.Obj();
                        d["id"] = c.Guid.ToString();
                        d["name"] = c.CareerName;
                        d["available"] = sd.CreatedSim != null && Career.FindClosestCareerLocation(sd.CreatedSim, c.Guid) != null;
                        list.Add(d);
                    }
                    return list;
                }
                case "join":
                {
                    Sim s = sd.CreatedSim;
                    if (s == null) throw new McpException(sd.FullName + " is not in the world");
                    OccupationNames name = Util.ParseEnum<OccupationNames>(a.Str("career"));
                    CareerLocation loc = Career.FindClosestCareerLocation(s, name);
                    if (loc == null) throw new McpException("no workplace for " + name + " exists in this world");
                    AcquireOccupationParameters p = new AcquireOccupationParameters(loc, false, true);
                    p.CharacterImportRequest = false;
                    if (!s.AcquireOccupation(p)) throw new McpException("the game refused the job (age, existing job, or requirements)");
                    break;
                }
                case "promote":
                    if (job == null) throw new McpException(sd.FullName + " has no job");
                    job.PromoteSim();
                    break;
                case "demote":
                    if (job == null) throw new McpException(sd.FullName + " has no job");
                    job.DemoteSim();
                    break;
                case "quit":
                    if (job == null) throw new McpException(sd.FullName + " has no job");
                    if (!job.CanQuit()) throw new McpException("this job cannot be quit right now");
                    job.LeaveJob(Career.LeaveJobReason.kQuit);
                    break;
                default:
                    throw new McpException("action must be info, options, join, promote, demote or quit");
            }
            job = sd.Occupation;
            if (job == null)
            {
                Dictionary<string, object> none = Util.Obj();
                none["career"] = null;
                return none;
            }
            return CareerInfo(job);
        }

        static object AgeUp(Args a)
        {
            RequireLive();
            Sim s = Util.FindSim(a, "sim");
            if (s.SimDescription.Elder && !a.Bool("confirm_elder_death", false))
                throw new McpException(s.FullName + " is an Elder: aging up means dying of old age. Pass confirm_elder_death=true if that is really wanted.");
            AgingManager.Singleton.AgeTransitionWithoutCake(s);
            return Util.SimSummary(s.SimDescription);
        }
    }
}

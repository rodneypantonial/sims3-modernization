using System;
using System.Collections.Generic;
using Sims3.Gameplay.Abstracts;
using Sims3.Gameplay.Actors;
using Sims3.Gameplay.ActorSystems;
using Sims3.Gameplay.Autonomy;
using Sims3.Gameplay.CAS;
using Sims3.Gameplay.Controllers;
using Sims3.Gameplay.Core;
using Sims3.Gameplay.ObjectComponents;
using Sims3.Gameplay.Objects;
using Sims3.Gameplay.Objects.CookingObjects;
using Sims3.Gameplay.Objects.Miscellaneous;
using Sims3.SimIFace;

namespace Ts3Agent
{
    // fresh_world: put the loaded world into a clean state for base test saves (Rodney, 2026-10-17: base saves start fresh).
    // Every instantiated Sim and every lot, not only the active household. Uses EA's own debug/cheat paths:
    // Sim.FixMood (MaxEverything + remove Polarity.Negative buffs), RepairableComponent.DEBUG_ForceRepaired,
    // CleanableComponent.DEBUG_ForceAllClean, PuddleManager.RemovePuddle, the trash can empty step, Bill.DestroyBill.
    // No Sim reset; relationships, skills, careers and money stay (bills are dropped without charging funds).
    // {check: true}: change nothing, count what is still not fresh, return clean; {fail_if_dirty: true} throws on dirt.
    public static partial class Handlers
    {
        static readonly Dictionary<ulong, bool> sUnsettable = new Dictionary<ulong, bool>(); // per game session
        static readonly System.Reflection.FieldInfo sMotiveDisabled = typeof(Motive).GetField("mInstanceDisabled", System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Instance);
        const float kMotiveMargin = 15f; // motive scale is -100..100; a few game minutes of decay stays under this

        static readonly CommodityKind[] sFreshMotives = {
            CommodityKind.Hunger, CommodityKind.Energy, CommodityKind.Bladder, CommodityKind.Hygiene,
            CommodityKind.Social, CommodityKind.Fun, CommodityKind.VampireThirst, CommodityKind.BatteryPower };

        // Negative buffs tied to story or occult state stay even when timed (EA's FixMood removes them; we are stricter).
        static readonly string[] sKeepBuffNames = {
            "Pregnan", "Werewolf", "Lycan", "Zombie", "Ghost", "Mummy", "Curse", "Vampire", "Fairy", "Witch", "Genie",
            "Plant", "Mermaid", "Imaginary", "Mourn", "Death", "Dead", "Dying", "Transform", "Robot", "Frank", "Occult",
            "AgeUp", "Aging", "Elder" };

        static void RegisterFreshWorld()
        {
            Dispatcher.Register("fresh_world", FreshWorld);
        }

        static bool RemovableNegativeBuff(BuffInstance b)
        {
            if (b == null || b.mBuff == null || b.mBuff.BuffPolarity() != Polarity.Negative) return false;
            if (b.PermaMoodlet || b.mTimeoutCount <= 0f) return false; // permanent / state-driven (motive buffs clear with the motive)
            string name = b.Guid.ToString();
            foreach (string k in sKeepBuffNames) if (name.IndexOf(k, StringComparison.OrdinalIgnoreCase) >= 0) return false;
            return true;
        }

        // loose clutter we may destroy: not carried, not in an inventory, not being used
        static bool Loose(GameObject o)
        {
            return o != null && !o.HasBeenDestroyed && !o.InInventory && !(o.Parent is Sim) && !o.InUse;
        }

        static object FreshWorld(Args a)
        {
            bool check = a.Bool("check", false);
            Dictionary<string, object> r = Util.Obj();
            List<object> errors = new List<object>();
            int motives = 0, buffs = 0, repaired = 0, cleaned = 0, puddles = 0, trashPiles = 0, cans = 0, food = 0, bills = 0, fires = 0;
            List<object> motiveDetails = new List<object>();
            List<object> unsettableDetails = new List<object>(); // max mode: Sims whose motives MaxEverything did not raise
            int unsettableSkipped = 0; // check mode: first 5 "Sim: motive value/ideal"

            Sim[] sims = Sims3.Gameplay.Queries.GetObjects<Sim>();
            try
            {
                foreach (Sim s in sims)
                {
                    try
                    {
                        if (s == null || s.HasBeenDestroyed || s.Motives == null) continue;
                        if (check && sUnsettable.ContainsKey(s.SimDescription.SimDescriptionId)) { unsettableSkipped++; continue; }
                        if (check)
                        {
                            foreach (CommodityKind k in sFreshMotives)
                            {
                                Motive m = s.Motives.GetMotive(k);
                                // Motive.SetValue does nothing for a disabled motive or one without decay, so MaxEverything
                                // cannot raise those: not "dirty" (2026-10-17 first run: 39 Sims flagged right after the cleanup)
                                if (m == null || !m.HasDecay || (sMotiveDisabled != null && (bool)sMotiveDisabled.GetValue(m))) continue;
                                if (m.Value < m.GetIdealMotiveValue() - kMotiveMargin)
                                {
                                    motives++;
                                    if (motiveDetails.Count < 5) motiveDetails.Add(s.FullName + ": " + k + " " + (int)m.Value + "/" + (int)m.GetIdealMotiveValue());
                                    break;
                                }
                            }
                        }
                        else
                        {
                            s.Motives.MaxEverything();
                            motives++;
                            // measured, not guessed: a Sim whose settable motives did not rise is one the game will not
                            // change here (2026-10-17 IP: a townie at a community lot kept Bladder 2 / Social 0 after
                            // MaxEverything, cause not known yet); the check then skips it and lists it instead of failing
                            foreach (CommodityKind k in sFreshMotives)
                            {
                                Motive m = s.Motives.GetMotive(k);
                                if (m == null || !m.HasDecay || (sMotiveDisabled != null && (bool)sMotiveDisabled.GetValue(m))) continue;
                                if (m.Value < m.GetIdealMotiveValue() - kMotiveMargin)
                                {
                                    sUnsettable[s.SimDescription.SimDescriptionId] = true;
                                    if (unsettableDetails.Count < 5) unsettableDetails.Add(s.FullName + ": " + k + " " + (int)m.Value + " after MaxEverything");
                                    break;
                                }
                            }
                        }
                    }
                    catch (Exception e) { AddError(errors, "motives", e); }
                }
            }
            catch (Exception e) { AddError(errors, "motives", e); }

            try
            {
                foreach (Sim s in sims)
                {
                    try
                    {
                        if (s == null || s.HasBeenDestroyed || s.BuffManager == null) continue;
                        foreach (BuffInstance b in new List<BuffInstance>(s.BuffManager.List))
                        {
                            if (!RemovableNegativeBuff(b)) continue;
                            if (!check) s.BuffManager.RemoveElement(b.Guid);
                            buffs++;
                        }
                    }
                    catch (Exception e) { AddError(errors, "buffs", e); }
                }
            }
            catch (Exception e) { AddError(errors, "buffs", e); }

            GameObject[] objects = Sims3.Gameplay.Queries.GetObjects<GameObject>();
            try
            {
                foreach (GameObject o in objects)
                {
                    try
                    {
                        if (o == null || o.HasBeenDestroyed || o is Sim) continue;
                        RepairableComponent rc = o.Repairable;
                        if (rc != null && rc.Broken)
                        {
                            if (!check) rc.ForceRepaired(null); // what RepairableComponent.DEBUG_ForceRepaired runs
                            repaired++;
                        }
                    }
                    catch (Exception e) { AddError(errors, "repaired", e); }
                }
            }
            catch (Exception e) { AddError(errors, "repaired", e); }

            try
            {
                foreach (GameObject o in objects)
                {
                    try
                    {
                        if (o == null || o.HasBeenDestroyed || o is Sim) continue;
                        CleanableComponent cc = o.Cleanable;
                        if (cc == null) continue;
                        if (check) { if (cc.NeedsToBeCleaned) cleaned++; }
                        else if (cc.DirtyLevel < 0f || cc.VisuallyDirty) { cc.ForceClean(); cleaned++; } // DEBUG_ForceAllClean
                    }
                    catch (Exception e) { AddError(errors, "cleaned", e); }
                }
            }
            catch (Exception e) { AddError(errors, "cleaned", e); }

            try
            {
                foreach (Fire f in Sims3.Gameplay.Queries.GetObjects<Fire>())
                {
                    try
                    {
                        if (f == null || f.HasBeenDestroyed) continue;
                        if (!check) f.ExtinguishFire(); // IExtinguishable path the firefighter uses
                        fires++;
                    }
                    catch (Exception e) { AddError(errors, "fires", e); }
                }
            }
            catch (Exception e) { AddError(errors, "fires", e); }

            try
            {
                foreach (object lo in LotManager.AllLots)
                {
                    Lot lot = lo as Lot;
                    if (lot == null) continue;
                    try
                    {
                        LotLocation[] tiles = World.GetPuddles(lot.LotId, LotLocation.Invalid);
                        if (tiles == null) continue;
                        foreach (LotLocation loc in tiles)
                        {
                            if (!check)
                            {
                                if (lot.PuddleManager != null) PuddleManager.RemovePuddle(lot.LotId, loc); // also drops its evaporate alarm
                                else World.SetPuddle(lot.LotId, loc, false);
                            }
                            puddles++;
                        }
                    }
                    catch (Exception e) { AddError(errors, "puddles", e); }
                }
            }
            catch (Exception e) { AddError(errors, "puddles", e); }

            try
            {
                foreach (TrashPile p in Sims3.Gameplay.Queries.GetObjects<TrashPile>())
                {
                    try
                    {
                        if (!Loose(p)) continue;
                        if (!check) p.Destroy(); // TrashPile.DisposeTrash ends with Destroy
                        trashPiles++;
                    }
                    catch (Exception e) { AddError(errors, "trashPiles", e); }
                }
                foreach (AshPile p in Sims3.Gameplay.Queries.GetObjects<AshPile>())
                {
                    try
                    {
                        if (!Loose(p)) continue;
                        if (!check) p.Destroy(); // AshPile.CleanUp destroys the pile
                        trashPiles++;
                    }
                    catch (Exception e) { AddError(errors, "trashPiles", e); }
                }
            }
            catch (Exception e) { AddError(errors, "trashPiles", e); }

            try
            {
                foreach (Trashcan c in Sims3.Gameplay.Queries.GetObjects<Trashcan>())
                {
                    try
                    {
                        if (c == null || c.HasBeenDestroyed || c.InInventory || c.LotCurrent == null || c.TrashUnitsUsed <= 0) continue;
                        if (!check) { c.TrashUnitsUsed = 0; c.SetStateEmpty(); } // TrashcanIndoor's take-out-trash step, without the bag
                        cans++;
                    }
                    catch (Exception e) { AddError(errors, "trashCansEmptied", e); }
                }
            }
            catch (Exception e) { AddError(errors, "trashCansEmptied", e); }

            try
            {
                foreach (PreparedFood p in Sims3.Gameplay.Queries.GetObjects<PreparedFood>())
                {
                    try
                    {
                        if (!Loose(p) || (p.HasFoodLeft() && !p.IsSpoiled)) continue; // empty plate/wrapper or spoiled
                        if (!check) p.Destroy(); // PreparedFood.Destroy: the clean-up/throw-away end state
                        food++;
                    }
                    catch (Exception e) { AddError(errors, "dishesAndFood", e); }
                }
            }
            catch (Exception e) { AddError(errors, "dishesAndFood", e); }

            try
            {
                // Bills on lots, in mailboxes, Sim and family inventories: EA's own household gather plus a global query
                List<Bill> all = new List<Bill>(Sims3.Gameplay.Queries.GetObjects<Bill>());
                foreach (Household h in Household.sHouseholdList)
                {
                    try
                    {
                        if (h == null) continue;
                        if (h.UnpaidBills > 0) { if (!check) h.UnpaidBills = 0; bills++; }
                        if (h.LotHome == null || h.Sims == null) continue;
                        foreach (Sim s in h.Sims)
                        {
                            if (s == null || s.HasBeenDestroyed) continue;
                            Bill.GetTotalAmountForAllBillsInHousehold(s, all, null);
                            break;
                        }
                    }
                    catch (Exception e) { AddError(errors, "bills", e); }
                }
                Dictionary<Bill, bool> seen = new Dictionary<Bill, bool>();
                foreach (Bill b in all)
                {
                    try
                    {
                        if (b == null || seen.ContainsKey(b) || b.HasBeenDestroyed) continue;
                        seen[b] = true;
                        if (!check) b.DestroyBill(false); // what paying does after ModifyFunds; no funds change here
                        bills++;
                    }
                    catch (Exception e) { AddError(errors, "bills", e); }
                }
            }
            catch (Exception e) { AddError(errors, "bills", e); }

            r["check"] = check;
            r["motivesMaxed"] = motives; // check mode: Sims with a core motive more than kMotiveMargin below ideal
            if (check) r["motiveDetails"] = motiveDetails;
            r["unsettableSims"] = sUnsettable.Count; // Sims the game would not max (measured in max mode), skipped by the check
            if (check) r["unsettableSkipped"] = unsettableSkipped; else r["unsettableDetails"] = unsettableDetails;
            r["buffsRemoved"] = buffs;
            r["repaired"] = repaired;
            r["cleaned"] = cleaned;
            r["puddles"] = puddles;
            r["trashPiles"] = trashPiles;
            r["trashCansEmptied"] = cans;
            r["dishesAndFood"] = food;
            r["bills"] = bills;
            r["fires"] = fires;
            r["errorCount"] = errors.Count;
            r["errors"] = errors.Count > 5 ? errors.GetRange(0, 5) : errors;
            if (check)
            {
                List<string> dirty = new List<string>();
                foreach (string k in new string[] { "motivesMaxed", "buffsRemoved", "repaired", "cleaned", "puddles", "trashPiles", "trashCansEmptied", "dishesAndFood", "bills", "fires" })
                    if ((int)r[k] > 0) dirty.Add(k + "=" + r[k]);
                r["clean"] = dirty.Count == 0;
                if (dirty.Count > 0 && a.Bool("fail_if_dirty", false)) throw new McpException("world not fresh: " + string.Join(", ", dirty.ToArray()) + (motiveDetails.Count > 0 ? " (e.g. " + string.Join("; ", motiveDetails.ConvertAll(x => (string)x).ToArray()) + ")" : ""));
            }
            return r;
        }

        static void AddError(List<object> errors, string category, Exception e)
        {
            errors.Add(category + ": " + e.GetType().Name + ": " + e.Message);
        }
    }
}

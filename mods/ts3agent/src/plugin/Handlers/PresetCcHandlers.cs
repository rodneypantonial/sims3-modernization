using System;
using System.Collections.Generic;
using Sims3.Gameplay;
using Sims3.Gameplay.Abstracts;
using Sims3.Gameplay.Core;
using Sims3.Gameplay.CAS;
using Sims3.Metadata;
using Sims3.SimIFace;
using Sims3.SimIFace.BuildBuy;
using Sims3.SimIFace.CAS;

namespace Ts3Agent
{
    // docs/200 §9 (object design presets, tools/cc-gen.ts --kind presets): every cloned object carries K design presets (OBJD
    // material list), each with its own texture copies. preset_cc_check = load smoke (catalogue products and their preset
    // counts as the game lists them); preset_cc_cycle = STRESS (labelled, not a player flow): every product placed once per
    // preset over the town, the preset applied the way the game applies a saved design in live mode (Helm.ImportContent for
    // the houseboat: ObjectDesigner.SetObject, DesignModeSwap + Complate.SetupDesignSwap, ApplyToObject, SetPresetInfo,
    // EndDesigning), so each preset's material is built; mark, remove, mark, as bb_cc_cycle.
    public static partial class Handlers
    {
        static List<BuildBuyProduct> NamedPlaceable(string name)
        {
            List<BuildBuyProduct> l = new List<BuildBuyProduct>();
            string want = name.ToLowerInvariant();
            foreach (BuildBuyProduct p in SortedCatalog())
                if (Placeable(p) && (p.CatalogName ?? "").ToLowerInvariant().IndexOf(want) >= 0) l.Add(p);
            return l;
        }

        static int PresetCount(BuildBuyProduct p)
        {
            int n = 0;
            while (n < 64 && p.PresetAtIndex((uint)n) != null) n++;
            return n;
        }

        // preset_cc_check {name?: "CCGen", min_products?, min_presets?}: placeable catalogue products named like it and how many
        // design presets the game lists for each (BuildBuyProduct.PresetAtIndex); with_min = products with >= min_presets.
        // Fails when fewer than min_products products have min_presets presets.
        static object PresetCcCheck(Args a)
        {
            RequireLive();
            List<BuildBuyProduct> prods = NamedPlaceable(a.Str("name", "CCGen"));
            int min = (int)a.Long("min_presets", 1), withMin = 0;
            Dictionary<string, object> hist = Util.Obj();
            foreach (BuildBuyProduct p in prods)
            {
                int n = PresetCount(p);
                if (n >= min) withMin++;
                hist[n.ToString()] = (hist.ContainsKey(n.ToString()) ? (int)hist[n.ToString()] : 0) + 1;
            }
            Dictionary<string, object> r = Util.Obj();
            r["products"] = prods.Count;
            r["with_min"] = withMin;
            r["presets_histogram"] = hist;
            if (a.Has("min_products") && withMin < a.Long("min_products"))
                throw new McpException("preset_cc_check: " + withMin + " of " + prods.Count + " products list >= " + min + " presets, need " + a.Long("min_products"));
            return r;
        }

        static void WaitDesigner()
        {
            for (int i = 0; i < 300 && ObjectDesigner.IsProcessing; i++) Ticks(1);
        }

        // design preset k of p onto go (in the world); null = done, else why not
        static string ApplyDesignPreset(GameObject go, BuildBuyProduct p, uint k)
        {
            BuildBuyPreset bp = p.PresetAtIndex(k);
            if (bp == null) return "no preset " + k;
            if (!ObjectDesigner.SetObject(go.ObjectId)) return "ObjectDesigner.SetObject refused";
            try
            {
                WaitDesigner();
                string preset = ObjectDesigner.GetDesignPreset(ObjectDesigner.GetDesignPresetIndexFromId(bp.ID));
                if (string.IsNullOrEmpty(preset)) return "empty preset string";
                object entry = SimBuilder.GetPresetEntryFromPresetString(preset);
                if (!(entry is KeyValuePair<string, Dictionary<string, Complate>>)) return "preset string did not parse";
                KeyValuePair<string, Dictionary<string, Complate>> kv = (KeyValuePair<string, Dictionary<string, Complate>>)entry;
                SortedList<string, Complate> patterns = new SortedList<string, Complate>();
                foreach (KeyValuePair<string, Complate> it in kv.Value) patterns.Add(it.Key, it.Value);
                Complate main;
                if (!patterns.TryGetValue(kv.Key, out main)) return "preset has no main complate";
                SortedList<string, bool> stencils = new SortedList<string, bool>(); // as Helm: the preset's own stencil switches
                foreach (Complate.Variable v in main.Variables)
                {
                    if (v.Type != Complate.Variable.Types.Bool) continue;
                    string n = v.Name.ToLower();
                    if (n.StartsWith("stencil ") && n.EndsWith(" enabled") && !stencils.ContainsKey(v.Name)) stencils.Add(v.Name, bool.Parse(v.Value));
                }
                DesignModeSwap swap = new DesignModeSwap();
                try
                {
                    if (swap.SetSourceObject(go.ObjectId)) swap.ClearOldCompositors();
                    if (!Complate.SetupDesignSwap(swap, patterns, preset, false, stencils)) return "SetupDesignSwap refused";
                    swap.ApplyToObject();
                    ObjectDesigner.SetPresetInfo(go.ObjectId, preset);
                }
                finally { swap.Dispose(); }
                return null;
            }
            finally
            {
                ObjectDesigner.EndDesigning(true);
                WaitDesigner();
            }
        }

        // preset_cc_cycle {name?: "CCGen", objects?: 30, presets?: 6, cycles?: 3, skip?: 0, lot?: "town", prefix?: "pcc"} |
        // {result: true}: live mode. Products: the placeable ones named like it with >= presets presets (sorted by key), cycle c
        // takes objects of them from skip + c*objects (wrapping). Per cycle each product is placed `presets` times, copy k with
        // design preset k applied, round-robin over the lots (place_objects lot targets, FindGoodLocationNearby from each lot
        // centre); mark <prefix>-full + GC; every placed object destroyed; mark <prefix>-empty + GC. Counts: placed, no_spot,
        // applied (preset set), apply_failed (+ first reasons in errors), readback_equal (the object's design string equals the
        // preset's afterwards [inferred check]).
        static Dictionary<string, object> sPccResult;
        static object PresetCcCycle(Args a)
        {
            if (a.Has("result"))
            {
                if (sPccResult == null) throw new McpException("no preset_cc_cycle started");
                return sPccResult;
            }
            if (sPccResult != null && !(bool)sPccResult["done"]) throw new McpException("preset_cc_cycle still running");
            RequireLive();
            int per = Math.Max(1, (int)a.Long("objects", 30)), k = Math.Max(1, (int)a.Long("presets", 6)), cycles = Math.Max(1, (int)a.Long("cycles", 3)), skip = (int)a.Long("skip", 0);
            List<BuildBuyProduct> prods = NamedPlaceable(a.Str("name", "CCGen"));
            prods.RemoveAll(delegate(BuildBuyProduct p) { return PresetCount(p) < k; });
            if (prods.Count == 0) throw new McpException("no placeable product named like '" + a.Str("name", "CCGen") + "' with >= " + k + " presets");
            List<Lot> lots = TargetLots(a.Str("lot", "town"), Household.ActiveHousehold);
            if (lots.Count == 0) throw new McpException("no target lot");
            string prefix = a.Str("prefix", "pcc");
            Dictionary<string, object> r = Util.Obj();
            r["done"] = false;
            r["products"] = prods.Count;
            r["lots"] = lots.Count;
            r["cycles_done"] = 0;
            foreach (string c in new string[] { "placed", "no_spot", "applied", "apply_failed", "readback_equal", "destroyed" }) r[c] = 0;
            List<object> perCycle = new List<object>();
            r["per_cycle"] = perCycle;
            List<object> errors = new List<object>();
            r["errors"] = errors;
            sPccResult = r;
            RunAsync("preset_cc_cycle", delegate
            {
                try
                {
                    int slot = 0;
                    for (int c = 0; c < cycles; c++)
                    {
                        Dictionary<string, object> pc = Util.Obj();
                        perCycle.Add(pc);
                        List<GameObject> placed = new List<GameObject>();
                        int applied = 0;
                        for (int i = 0; i < per; i++)
                        {
                            BuildBuyProduct p = prods[(skip + c * per + i) % prods.Count];
                            for (uint pi = 0; pi < (uint)k; pi++)
                            {
                                Lot lot = lots[slot++ % lots.Count];
                                GameObject go = GlobalFunctions.CreateObjectOutOfWorld(p.ProductResourceKey) as GameObject;
                                if (go == null) { Bump(r, "no_spot"); continue; }
                                Vector3 pos = lot.GetCenterPosition(), fwd = Vector3.UnitZ;
                                if (!GlobalFunctions.FindGoodLocationNearby(go, ref pos, ref fwd) || LotManager.GetLotAtPoint(pos) != lot) { go.Destroy(); Bump(r, "no_spot"); continue; }
                                go.SetPosition(pos);
                                go.SetForward(fwd);
                                go.AddToWorld();
                                placed.Add(go);
                                Bump(r, "placed");
                                string why;
                                try { why = ApplyDesignPreset(go, p, pi); }
                                catch (Exception e) { why = DescribeError(e); }
                                if (why != null) { Bump(r, "apply_failed"); if (errors.Count < 20) errors.Add(p.CatalogName + " preset " + pi + ": " + why); continue; }
                                Bump(r, "applied"); applied++;
                                BuildBuyPreset bp = p.PresetAtIndex(pi);
                                if (bp != null && ObjectDesigner.GetObjectDesignPreset(go.ObjectId) == ObjectDesigner.GetDesignPreset(ObjectDesigner.GetDesignPresetIndexFromId(bp.ID))) Bump(r, "readback_equal");
                                Ticks(1);
                            }
                        }
                        pc["placed"] = placed.Count;
                        pc["applied"] = applied;
                        Ticks(30); // let the last materials build before the mark
                        MarkGc(prefix + "-full");
                        foreach (GameObject go in placed) { try { go.Destroy(); Bump(r, "destroyed"); } catch (Exception e) { if (errors.Count < 20) errors.Add("destroy: " + DescribeError(e)); } }
                        Ticks(10);
                        MarkGc(prefix + "-empty");
                        r["cycles_done"] = c + 1;
                    }
                }
                catch (Exception e) { r["error"] = DescribeError(e); }
                finally { r["done"] = true; }
            });
            return r;
        }
    }
}

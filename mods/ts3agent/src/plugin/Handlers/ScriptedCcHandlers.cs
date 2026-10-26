using System;
using System.Collections.Generic;
using Sims3.Gameplay;
using Sims3.Gameplay.Abstracts;
using Sims3.Gameplay.Actors;
using Sims3.Gameplay.Autonomy;
using Sims3.Gameplay.Core;
using Sims3.Gameplay.Interactions;
using Sims3.SimIFace;
using Sims3.SimIFace.BuildBuy;

namespace Ts3Agent
{
    // docs/200 §8 (scripted synthetic CC, tools/cc-gen-script.ts): every item's object runs its own script assembly, a renamed
    // IL copy of the original object class (namespace CCGenScript.S<seed>.I<n>) with its own interactions and animations.
    // scripted_cc_check = load smoke (types bound, interactions listed); scripted_cc_cycle = STRESS (labelled, not a player
    // flow): place, run each item's own interaction with the active Sim, mark, remove, mark, as bb_cc_cycle.
    public static partial class Handlers
    {
        const string kScriptNs = "CCGenScript.";

        static List<BuildBuyProduct> ScriptedProducts(string name)
        {
            List<BuildBuyProduct> l = new List<BuildBuyProduct>();
            string want = name.ToLowerInvariant();
            foreach (BuildBuyProduct p in SortedCatalog())
                if ((p.CatalogName ?? "").ToLowerInvariant().StartsWith(want)) l.Add(p);
            return l;
        }

        static List<InteractionObjectPair> OwnInteractions(GameObject go)
        {
            List<InteractionObjectPair> l = new List<InteractionObjectPair>();
            if (go.Interactions != null)
                foreach (InteractionObjectPair iop in go.Interactions)
                    if (iop != null && iop.InteractionDefinition != null && iop.InteractionDefinition.GetType().FullName.StartsWith(kScriptNs)) l.Add(iop);
            return l;
        }

        static int ScriptAssemblies()
        {
            int n = 0;
            foreach (System.Reflection.Assembly asm in AppDomain.CurrentDomain.GetAssemblies())
                if (asm.GetName().Name.StartsWith("CCGenS")) n++;
            return n;
        }

        // Why a product came up as another type (docs/200 §8.2). FailureObject text = the class name when ScriptProxy found no
        // type by that name in any assembly (Gameflow.CreateObjectFailureCallback), "Missing object resource instance <key>"
        // when the engine create failed (ctor/Init threw: GlobalFunctions.CreateFailureObject). Then the item's own assembly
        // (catalogue "... #n" -> CCGenS*I<n:D5>): per concrete GameObject class, Assembly.GetType by full name (what
        // ScriptProxy.TryCreateLogic does), the class constructor and a bare constructor call (no Init, object dropped).
        static string WhyNotBound(GameObject go, string catalogName)
        {
            string s = "type " + go.GetType().FullName;
            FailureObject f = go as FailureObject;
            if (f != null) s += " (failure text '" + (f.mErrorText ?? "null").Trim() + "')";
            int at = catalogName.LastIndexOf('#'), n;
            if (at < 0 || !int.TryParse(catalogName.Substring(at + 1).Trim(), out n)) return s;
            string tail = "I" + n.ToString("D5");
            foreach (System.Reflection.Assembly asm in AppDomain.CurrentDomain.GetAssemblies())
            {
                string an = asm.GetName().Name;
                if (!an.StartsWith("CCGenS") || !an.EndsWith(tail)) continue;
                s += "; " + an;
                Type[] types;
                try { types = asm.GetTypes(); }
                catch (Exception e) { return s + " GetTypes: " + DescribeError(e); }
                foreach (Type t in types)
                {
                    if (t.IsAbstract || t.IsNested || !typeof(GameObject).IsAssignableFrom(t)) continue;
                    s += " " + t.FullName + ": GetType " + (asm.GetType(t.FullName) != null ? "ok" : "null");
                    try { System.Runtime.CompilerServices.RuntimeHelpers.RunClassConstructor(t.TypeHandle); s += ", cctor ok"; }
                    catch (Exception e) { return s + ", cctor: " + DescribeError(e); }
                    try { Activator.CreateInstance(t); s += ", ctor ok"; }
                    catch (Exception e) { return s + ", ctor: " + DescribeError(e); }
                }
                return s;
            }
            return s + "; no assembly *" + tail + " loaded";
        }

        // scripted_cc_check {name?: "CCGenScript", min_products?, min_typed?, min_with_interactions?, min_assemblies?}: every
        // catalogue product named like it is created out of world (OnStartup runs: the class adds its interactions), its type
        // read (typed = namespace CCGenScript., i.e. the OBJK bound the cloned class), its own interactions counted, then
        // destroyed. Fails below any min. Also counts the loaded CCGenS* assemblies.
        static object ScriptedCcCheck(Args a)
        {
            RequireLive();
            List<BuildBuyProduct> prods = ScriptedProducts(a.Str("name", "CCGenScript"));
            int typed = 0, withOwn = 0, ownTotal = 0, failed = 0;
            List<object> bad = new List<object>();
            foreach (BuildBuyProduct p in prods)
            {
                GameObject go = null;
                try { go = GlobalFunctions.CreateObjectOutOfWorld(p.ProductResourceKey) as GameObject; }
                catch (Exception e) { if (bad.Count < 12) bad.Add(p.CatalogName + ": " + DescribeError(e)); }
                if (go == null) { failed++; continue; }
                string t = go.GetType().FullName;
                int own = OwnInteractions(go).Count;
                if (t.StartsWith(kScriptNs)) typed++;
                else if (bad.Count < 12) bad.Add(p.CatalogName + ": " + (bad.Count < 3 ? WhyNotBound(go, p.CatalogName) : "type " + t));
                if (own > 0) withOwn++;
                ownTotal += own;
                go.Destroy();
            }
            Dictionary<string, object> r = Util.Obj();
            r["products"] = prods.Count;
            r["typed"] = typed;
            r["with_interactions"] = withOwn;
            r["interactions"] = ownTotal;
            r["create_failed"] = failed;
            r["assemblies"] = ScriptAssemblies();
            r["problems"] = bad;
            string[] mins = { "products", "typed", "with_interactions", "assemblies" };
            foreach (string m in mins)
                if (a.Has("min_" + m) && Convert.ToInt64(r[m]) < a.Long("min_" + m))
                    throw new McpException("scripted_cc_check: " + m + " " + r[m] + " < min " + a.Long("min_" + m) + "; problems: " + string.Join(" | ", bad.ConvertAll(delegate(object o) { return (string)o; }).ToArray()));
            return r;
        }

        // scripted_cc_cycle {name?: "CCGenScript", objects?: 25, cycles?: 3, wait_s?: 20, prefix?: "scc"} | {result: true, min_typed?, min_pushed?}:
        // live mode, active Sim, home lot. Per cycle c: the next `objects` products (wrapping) placed on the home lot (as
        // place_objects); for each, the active Sim runs one enabled interaction of the item's own assembly (CCGenScript.*,
        // the c-th one when several), pushed as the pie menu does and waited for until it leaves the queue (wait_s real
        // seconds, then cancelled: timeouts); mark <prefix>-full + GC; every placed object destroyed; mark <prefix>-empty + GC.
        static Dictionary<string, object> sSccResult;
        static object ScriptedCcCycle(Args a)
        {
            if (a.Has("result"))
            {
                if (sSccResult == null) throw new McpException("no scripted_cc_cycle started");
                // {result: true, min_typed?, min_pushed?} after done: fail the step instead of a hollow PASS (2026-10-26 run: 75 FailureObjects, 0 pushed)
                if ((bool)sSccResult["done"])
                    foreach (string m in new string[] { "typed", "pushed" })
                        if (a.Has("min_" + m) && Convert.ToInt64(sSccResult[m]) < a.Long("min_" + m))
                            throw new McpException("scripted_cc_cycle: " + m + " " + sSccResult[m] + " < min " + a.Long("min_" + m) + " (placed " + sSccResult["placed"] + ", none_enabled " + sSccResult["none_enabled"] + ")");
                return sSccResult;
            }
            if (sSccResult != null && !(bool)sSccResult["done"]) throw new McpException("scripted_cc_cycle still running");
            RequireLive();
            Lot lot = HomeLot();
            if (lot == null) throw new McpException("no home lot for the active household");
            Sim actor = Util.ActiveSim();
            List<BuildBuyProduct> prods = ScriptedProducts(a.Str("name", "CCGenScript"));
            prods.RemoveAll(delegate(BuildBuyProduct p) { return !Placeable(p); });
            if (prods.Count == 0) throw new McpException("no placeable scripted CC product (catalogue name CCGenScript ...)");
            int per = Math.Max(1, (int)a.Long("objects", 25)), cycles = Math.Max(1, (int)a.Long("cycles", 3));
            int waitMs = (int)(a.Double("wait_s", 20) * 1000);
            string prefix = a.Str("prefix", "scc");
            Dictionary<string, object> r = Util.Obj();
            r["done"] = false;
            r["products"] = prods.Count;
            r["cycles_done"] = 0;
            foreach (string k in new string[] { "placed", "no_spot", "typed", "pushed", "finished", "timeouts", "refused", "none_enabled", "destroyed" }) r[k] = 0;
            List<object> perCycle = new List<object>();
            r["per_cycle"] = perCycle;
            List<object> errors = new List<object>();
            r["errors"] = errors;
            sSccResult = r;
            RunAsync("scripted_cc_cycle", delegate
            {
                try
                {
                    for (int c = 0; c < cycles; c++)
                    {
                        Dictionary<string, object> pc = Util.Obj();
                        perCycle.Add(pc);
                        List<GameObject> placed = new List<GameObject>();
                        for (int i = 0; i < per; i++)
                        {
                            BuildBuyProduct p = prods[(c * per + i) % prods.Count];
                            GameObject go = GlobalFunctions.CreateObjectOutOfWorld(p.ProductResourceKey) as GameObject;
                            if (go == null) { Bump(r, "no_spot"); continue; }
                            Vector3 pos = lot.GetCenterPosition(), fwd = Vector3.UnitZ;
                            if (!GlobalFunctions.FindGoodLocationNearby(go, ref pos, ref fwd) || LotManager.GetLotAtPoint(pos) != lot) { go.Destroy(); Bump(r, "no_spot"); continue; }
                            go.SetPosition(pos);
                            go.SetForward(fwd);
                            go.AddToWorld();
                            placed.Add(go);
                            Bump(r, "placed");
                            if (go.GetType().FullName.StartsWith(kScriptNs)) Bump(r, "typed");
                        }
                        foreach (GameObject go in placed) RunOwnInteraction(actor, go, c, waitMs, r, errors);
                        pc["placed"] = placed.Count;
                        MarkGc(prefix + "-full");
                        actor.InteractionQueue.CancelAllInteractions();
                        Ticks(30);
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


        static void RunOwnInteraction(Sim actor, GameObject go, int nth, int waitMs, Dictionary<string, object> r, List<object> errors)
        {
            List<MenuEntry> own = BuildMenu(actor, go, HitFor(go, new Args(Util.Obj()))).FindAll(delegate(MenuEntry e)
            { return e.Enabled && e.Iop.InteractionDefinition.GetType().FullName.StartsWith(kScriptNs); });
            if (own.Count == 0) { Bump(r, "none_enabled"); return; }
            MenuEntry pick = own[nth % own.Count];
            try
            {
                InteractionInstanceParameters p = new InteractionInstanceParameters(pick.Iop, actor,
                    new InteractionPriority(InteractionPriorityLevel.UserDirected), AutonomySearchType.None,
                    InteractionFlags.UserDirected, HitFor(go, new Args(Util.Obj())), null, false);
                InteractionInstance inst = pick.Iop.InteractionDefinition.CreateInstanceFromParameters(ref p);
                bool queued;
                if (inst == null) queued = false;
                else if (inst is IImmediateInteraction) { Simulator.AddObject(new ImmediateInteractionTask(actor, inst)); queued = true; }
                else queued = actor.InteractionQueue.Add(inst);
                if (!queued) { Bump(r, "refused"); return; }
                Bump(r, "pushed");
                int t0 = System.Environment.TickCount;
                Ticks(3);
                while (inst is IImmediateInteraction ? false : actor.InteractionQueue.HasInteraction(inst))
                {
                    if (System.Environment.TickCount - t0 > waitMs) { actor.InteractionQueue.CancelInteraction(inst.Id); Bump(r, "timeouts"); Ticks(15); return; }
                    Ticks(10);
                }
                Bump(r, "finished");
            }
            catch (Exception e) { if (errors.Count < 20) errors.Add(pick.Label + ": " + DescribeError(e)); }
        }
    }
}

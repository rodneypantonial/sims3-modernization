// Offline check of NRaas Inventories.CheckInventories: V0 = master's helper, V2 = the patched helper (both extracted by
// run.ts from an NRaas clone). Same random worlds, compared:
//  A/B/C/D: V0, V2 without timer, V2 sleeping before every item, V2 with a timer that never fires: same log lines in the
//           same order, same exceptions, same final state (jam jars included, also the one that empties its stack).
//  E:       V2 sleeping before every item while a fake game changes the world at each sleep (takes items out, adds items
//           and stacks, destroys owners, replaces stacks, drops households): no escaped exception, every removed item
//           was broken when removed (an item the game added before its inventory's turn is checked like any other), every
//           original item left in a live inventory is clean afterwards.
using System;
using System.Collections.Generic;
using System.Text;
using NRaas;
using Sims3.Gameplay;
using Sims3.Gameplay.Abstracts;
using Sims3.Gameplay.CAS;
using Sims3.Gameplay.ObjectComponents;
using Sims3.SimIFace;

static class Program
{
    class Added : GameObject { }

    static Random sRnd;
    static int sNextId;
    static List<Inventory> sInventories = new List<Inventory>();

    static GameObject Item(Inventory inv, bool jamJars, bool corrupt)
    {
        int kind = corrupt ? sRnd.Next(8) : 7;
        GameObject o = (jamJars && kind == 6) ? new JamJar() : new GameObject();
        o.mId = ++sNextId;
        o.mItemComp = new ItemComponent { InventoryParent = inv };
        if (kind == 1) o.mItemComp = null;                     // dropped (2) unless household
        if (kind == 2) o.mDestroyed = true;                    // dropped (3)
        if (kind == 3) o.mItemComp.InventoryParent = null;     // reparented
        if (kind == 4 || kind == 6) o.mCorruptName = true;     // catalog name throws: jam jar deleted, others ignored
        GameObject.sWorld.Add(o);
        return o;
    }

    static Inventory NewInventory(IGameObject owner, bool jamJars)
    {
        Inventory inv = new Inventory { Owner = owner };
        int stacks = sRnd.Next(6);
        for (uint k = 0; k < stacks; k++)
        {
            int r = sRnd.Next(12);
            if (r == 0) { inv.mItems[k * 7 + 1] = null; continue; }
            InventoryStack s = new InventoryStack();
            if (r == 1) { s.List = null; inv.mItems[k * 7 + 1] = s; continue; }
            int n = (r == 2) ? 0 : 1 + sRnd.Next(r == 3 ? 40 : 6);
            for (int i = 0; i < n; i++)
            {
                if (sRnd.Next(9) == 0) { s.List.Add(new InventoryItem { Object = null }); continue; } // dropped (1)
                s.List.Add(new InventoryItem { Object = Item(inv, jamJars, sRnd.Next(3) == 0) });
            }
            inv.mItems[k * 7 + 1] = s;
        }
        sInventories.Add(inv);
        return inv;
    }

    static void Build(int seed, bool jamJars)
    {
        sRnd = new Random(seed);
        sNextId = 0;
        sInventories.Clear();
        GameObject.sWorld.Clear();
        Household.sHouseholdList.Clear();
        for (int i = 0; i < 30; i++)
        {
            GameObject o = new GameObject { mId = ++sNextId };
            GameObject.sWorld.Add(o);
            if (sRnd.Next(2) == 0) o.mInventory = NewInventory(o, jamJars);
        }
        for (int h = 0; h < 4; h++)
        {
            Household house = new Household { Name = "H" + h };
            foreach (bool fridge in new[] { false, true })
            {
                if (sRnd.Next(4) == 0) continue;
                SharedFamilyInventory sfi = new SharedFamilyInventory { mId = ++sNextId };
                sfi.mInventory = NewInventory(sfi, jamJars);
                GameObject.sWorld.Add(sfi);
                if (fridge) house.SharedFridgeInventory = sfi; else house.SharedFamilyInventory = sfi;
            }
            Household.sHouseholdList.Add(house);
        }
    }

    static string State()
    {
        StringBuilder b = new StringBuilder();
        foreach (Inventory inv in sInventories)
        {
            b.Append('[');
            foreach (KeyValuePair<uint, InventoryStack> s in inv.mItems)
            {
                b.Append(s.Key).Append(':');
                if (s.Value == null) { b.Append("null "); continue; }
                if (s.Value.List == null) { b.Append("nolist "); continue; }
                foreach (InventoryItem it in s.Value.List.mList)
                {
                    GameObject o = it.Object as GameObject;
                    b.Append(o == null ? "x" : (o.mId + (o.mDestroyed ? "d" : "") + (o.mItemComp == null ? "n" : o.mItemComp.InventoryParent == null ? "p" : ""))).Append(',');
                }
                b.Append(' ');
            }
            b.Append(']');
        }
        foreach (GameObject o in GameObject.sWorld) b.Append(o.mId).Append(' ');
        return b.ToString();
    }

    static int sFail;
    static void Check(bool ok, string what) { if (!ok) { sFail++; if (sFail < 20) Console.WriteLine("FAIL " + what); } }

    delegate void Run(List<string> log);

    static string Do(int seed, bool jamJars, Run run, out List<string> log, out int exceptions, out int sleeps)
    {
        Build(seed, jamJars);
        List<string> l = new List<string>();
        Common.sExceptions = 0; Common.sSleeps = 0;
        run(l);
        log = l; exceptions = Common.sExceptions; sleeps = Common.sSleeps;
        return State();
    }

    static bool Broken(InventoryItem it, Inventory inv)
    {
        if (it.Object == null) return true;
        if (it.Object.ItemComp == null) return !(inv.Owner is Sims3.Gameplay.Interfaces.IHouseholdInventory);
        return it.Object.HasBeenDestroyed;
    }

    static int Main()
    {
        int compared = 0, slept = 0, jamAborts = 0;
        for (int seed = 1; seed <= 400; seed++)
        {
            List<string> la, lb, lc, ld; int ea, eb, ec, ed, sa, sb, sc, sd;
            string a = Do(seed, true, l => V0.CheckInventories(s => l.Add(s), s => l.Add("d:" + s), true), out la, out ea, out sa);
            string b = Do(seed, true, l => V2.CheckInventories(s => l.Add(s), s => l.Add("d:" + s), true), out lb, out eb, out sb);
            StopWatch.sElapsed = 5;
            string c = Do(seed, true, l => V2.CheckInventories(s => l.Add(s), s => l.Add("d:" + s), true, 1), out lc, out ec, out sc);
            StopWatch.sElapsed = 0;
            string d = Do(seed, true, l => V2.CheckInventories(s => l.Add(s), s => l.Add("d:" + s), true, 1), out ld, out ed, out sd);
            Check(a == b && a == c && a == d, "state seed " + seed);
            Check(string.Join("|", la) == string.Join("|", lb) && string.Join("|", la) == string.Join("|", lc) && string.Join("|", la) == string.Join("|", ld), "log seed " + seed);
            Check(ea == eb && ea == ec && ea == ed, "exceptions seed " + seed + ": " + ea + " " + eb + " " + ec + " " + ed);
            Check(sb == 0 && sd == 0, "no sleep without a firing timer, seed " + seed);
            compared++; slept += sc; jamAborts += ea;
        }

        // E: the game changes things at every sleep
        int mutations = 0, removedByUs = 0, eRuns = 0, addedRemoved = 0;
        for (int seed = 1; seed <= 400; seed++)
        {
            Build(seed, false);
            Random game = new Random(seed * 31);
            List<Inventory> all = new List<Inventory>(sInventories);
            Dictionary<Inventory, bool> dead = new Dictionary<Inventory, bool>();
            List<object> removed = new List<object>();
            Dictionary<InventoryItem, Inventory> home = new Dictionary<InventoryItem, Inventory>();
            foreach (Inventory inv in all) foreach (InventoryStack s in inv.mItems.Values) if (s != null && s.List != null) foreach (InventoryItem it in s.List.mList) home[it] = inv;
            Dictionary<InventoryItem, bool> brokenAtRemove = new Dictionary<InventoryItem, bool>();
            Sims3.Gameplay.Utilities.ListSorted<InventoryItem>.sOnRemove = x => { InventoryItem it = (InventoryItem)x; removed.Add(it); Inventory h; brokenAtRemove[it] = home.TryGetValue(it, out h) ? Broken(it, h) : it.Object.HasBeenDestroyed; };
            Common.sOnSleep = () =>
            {
                mutations++;
                Inventory inv = all[game.Next(all.Count)];
                switch (game.Next(6))
                {
                    case 0: // the game takes an item out
                        foreach (InventoryStack s in new List<InventoryStack>(inv.mItems.Values))
                            if (s != null && s.List != null && s.List.Count > 0) { inv.RemoveInternal(s.List.mList[game.Next(s.List.Count)].Object); break; }
                        break;
                    case 1: // an item added, broken or not, to an existing or a new stack
                    {
                        Added o = new Added { mId = ++sNextId, mItemComp = new ItemComponent { InventoryParent = game.Next(2) == 0 ? null : inv }, mDestroyed = game.Next(3) == 0 };
                        InventoryItem it = new InventoryItem { Object = o };
                        uint key = (uint)game.Next(60);
                        InventoryStack s;
                        if (!inv.mItems.TryGetValue(key, out s) || s == null || s.List == null) { s = new InventoryStack(); inv.mItems[key] = s; }
                        s.List.Add(it);
                        break;
                    }
                    case 2: // the owner destroyed: its inventory is emptied
                    {
                        GameObject owner = inv.Owner as GameObject;
                        if (owner != null && !(owner is SharedFamilyInventory)) { owner.Dispose(); owner.Destroy(); dead[inv] = true; }
                        break;
                    }
                    case 3: // a stack replaced
                        foreach (uint k in new List<uint>(inv.mItems.Keys)) { inv.mItems[k] = new InventoryStack(); inv.mItems[k].List.Add(new InventoryItem { Object = new Added { mId = ++sNextId, mItemComp = new ItemComponent { InventoryParent = inv } } }); break; }
                        break;
                    case 4: // a household moved out of the list
                        if (Household.sHouseholdList.Count > 0) Household.sHouseholdList.RemoveAt(game.Next(Household.sHouseholdList.Count));
                        break;
                    default: break;
                }
            };
            StopWatch.sElapsed = 5;
            try { V2.CheckInventories(s => { }, s => { }, true, 1); }
            catch (Exception e) { Check(false, "E seed " + seed + " escaped " + e.GetType().Name + ": " + e.Message); }
            Common.sOnSleep = null;
            Sims3.Gameplay.Utilities.ListSorted<InventoryItem>.sOnRemove = null;
            Check(Common.sExceptions == 0, "E seed " + seed + ": " + Common.sExceptions + " logged exceptions");
            foreach (object x in removed) { InventoryItem it = (InventoryItem)x; if (it.Object is Added) addedRemoved++; Check(brokenAtRemove[it], "E seed " + seed + ": removed a clean item"); }
            removedByUs += removed.Count;
            foreach (Inventory inv in all)
            {
                if (dead.ContainsKey(inv)) continue;
                foreach (InventoryStack s in inv.mItems.Values)
                {
                    if (s == null || s.List == null) continue;
                    foreach (InventoryItem it in s.List.mList)
                    {
                        if (it.Object is Added) { Check(it.Object.ItemComp == null || it.Object.ItemComp.InventoryParent == null || it.Object.ItemComp.InventoryParent == inv, "E added touched"); continue; }
                        Inventory h;
                        if (!home.TryGetValue(it, out h) || h != inv) continue;
                        Check(!Broken(it, inv), "E seed " + seed + ": original item left broken");
                        Check(it.Object.ItemComp == null || it.Object.ItemComp.InventoryParent != null, "E seed " + seed + ": original item not reparented");
                    }
                }
            }
            eRuns++;
        }
        StopWatch.sElapsed = 0;

        Console.WriteLine(string.Format("A-D: {0} worlds, V0 == V2 (no timer) == V2 (sleep every item, {1} sleeps) == V2 (timer never fires), {2} jam-jar/exception aborts matched", compared, slept, jamAborts));
        Console.WriteLine(string.Format("E: {0} worlds, {1} game changes during sleeps, {2} broken items removed ({3} of them added before their inventory's turn), none clean", eRuns, mutations, removedByUs, addedRemoved));
        Console.WriteLine(sFail == 0 ? "PASS" : ("FAIL " + sFail));
        return sFail == 0 ? 0 : 1;
    }
}

// Minimal stand-ins for the game types NRaas's Inventories.CheckInventories touches, so the real helper source (extracted
// by run.ts from an NRaas clone, never stored here) can run offline. Behaviour follows the game where it matters:
// RemoveInternal drops an emptied stack from mItems (Inventory.cs RemoveInternal), an owner's Dispose clears its
// inventory (Inventory.Dispose -> DestroyItems -> mItems.Clear), Queries.GetObjects returns an array snapshot.
using System;
using System.Collections;
using System.Collections.Generic;

namespace Sims3.SimIFace
{
    public class ResetException : Exception { }

    public class StopWatch
    {
        public enum TickStyles { Milliseconds }
        // the harness sets how many ms each GetElapsedTime call reports
        public static uint sElapsed = 0;
        public static StopWatch Create(TickStyles style) { return new StopWatch(); }
        public void Start() { }
        public void Restart() { }
        public uint GetElapsedTime() { return sElapsed; }
    }
}

namespace Sims3.Gameplay.Utilities
{
    public class ListSorted<T> : IEnumerable<T>
    {
        public List<T> mList = new List<T>();
        public static Action<object> sOnRemove;
        public int Count { get { return mList.Count; } }
        public T this[int i] { get { return mList[i]; } }
        public void Add(T item) { mList.Add(item); }
        public void RemoveAt(int i) { T x = mList[i]; mList.RemoveAt(i); if (sOnRemove != null) sOnRemove(x); }
        public IEnumerator<T> GetEnumerator() { return mList.GetEnumerator(); }
        IEnumerator IEnumerable.GetEnumerator() { return mList.GetEnumerator(); }
    }
}

namespace Sims3.Gameplay.Interfaces
{
    public interface IHouseholdInventory { }
}

namespace Sims3.Gameplay.Actors { public class SimStub { } }

namespace Sims3.Gameplay.Abstracts
{
    using Sims3.Gameplay.ObjectComponents;

    public interface IGameObject
    {
        ItemComponent ItemComp { get; }
        bool HasBeenDestroyed { get; }
        Inventory Inventory { get; }
        string GetLocalizedName();
    }

    public class GameObject : IGameObject
    {
        public static List<GameObject> sWorld = new List<GameObject>();
        public int mId;
        public ItemComponent mItemComp;
        public Inventory mInventory;
        public bool mDestroyed, mCorruptName;
        public ItemComponent ItemComp { get { return mItemComp; } }
        public bool HasBeenDestroyed { get { return mDestroyed; } }
        public Inventory Inventory { get { return mInventory; } }
        public string GetLocalizedName() { return "obj" + mId; }
        public string CatalogName { get { if (mCorruptName) throw new Exception("no product"); return "cat" + mId; } }
        public void Dispose() { if (mInventory != null) mInventory.mItems.Clear(); }
        // as GameObject.Destroy for an object in an inventory: out of its parent inventory, out of the world
        public void Destroy()
        {
            mDestroyed = true;
            sWorld.Remove(this);
            if ((mItemComp != null) && (mItemComp.InventoryParent != null)) mItemComp.InventoryParent.RemoveInternal(this);
        }
    }

    public class JamJar : GameObject { }
}

namespace Sims3.Gameplay.ObjectComponents
{
    public class ItemComponent { public Sims3.Gameplay.Inventory InventoryParent; }
}


namespace Sims3.Gameplay.CAS
{
    public class Household
    {
        public static List<Household> sHouseholdList = new List<Household>();
        public string Name;
        public SharedFamilyInventory SharedFamilyInventory;
        public SharedFamilyInventory SharedFridgeInventory;
    }
    public class SimDescription { }
}

namespace Sims3.Gameplay
{
    using Sims3.Gameplay.Abstracts;
    using Sims3.Gameplay.Utilities;

    public static class Queries
    {
        public static T[] GetObjects<T>() where T : class
        {
            List<T> l = new List<T>();
            foreach (GameObject o in GameObject.sWorld) { T t = o as T; if (t != null) l.Add(t); }
            return l.ToArray();
        }
    }

    public class SharedFamilyInventory : GameObject, Sims3.Gameplay.Interfaces.IHouseholdInventory { }

    public static class GameStates { public static bool IsWorldShuttingDown = false; }

    public class InventoryItem { public IGameObject Object; }

    public class InventoryStack { public ListSorted<InventoryItem> List = new ListSorted<InventoryItem>(); }

    public class Inventory
    {
        public Dictionary<uint, InventoryStack> mItems = new Dictionary<uint, InventoryStack>();
        public IGameObject Owner;

        public void RemoveInternal(IGameObject obj)
        {
            foreach (KeyValuePair<uint, InventoryStack> s in mItems)
            {
                if (s.Value == null || s.Value.List == null) continue;
                for (int i = 0; i < s.Value.List.mList.Count; i++)
                {
                    if (s.Value.List.mList[i].Object != obj) continue;
                    s.Value.List.mList.RemoveAt(i);
                    if (s.Value.List.mList.Count == 0) mItems.Remove(s.Key);
                    return;
                }
            }
        }
    }
}

namespace NRaas
{
    public class Common
    {
        public static Action sOnSleep;
        public static int sSleeps, sExceptions;
        public static bool Sleep() { sSleeps++; if (sOnSleep != null) sOnSleep(); return true; }
        public static void Exception(Sims3.Gameplay.Abstracts.IGameObject obj, Exception e) { sExceptions++; }
    }
}

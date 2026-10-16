using System;
using System.Runtime.CompilerServices;

namespace Ts3Fixes
{
    // Object identity -> int table for the save serializer (RefTable.cs swaps it in for ScriptCore.ReferenceHashtable).
    // No game types here, so tools/selftest/reftable can compile it outside the game.
    // Open addressing with linear probing over two parallel arrays, power-of-two capacity, grown (doubled) when 2/3 full,
    // so a lookup touches ~2 slots at any size (the game's table stops growing at 16384 buckets and walks chains of ~18
    // nodes at 300k objects). Keys compare by reference, as the game's; the hash is the runtime's identity hash
    // (RuntimeHelpers.GetHashCode, an internal call), never the object's own GetHashCode override, which would run
    // interpreted game code per lookup. Mono 1.2 here uses a non-moving GC, so the identity hash is stable [inferred].
    // A Fibonacci multiply spreads it before taking the top bits.
    // Add semantics match the game's: adding a key that is already present makes later lookups return the new index.
    public sealed class FastRefTable
    {
        const int kInitialBits = 5; // 32 slots: CacheManager makes a writer (two tables) per cached object
        object[] mKeys;
        int[] mValues;
        int mMask, mShift, mUsed, mGrowAt;

        // Counters, null outside the game: [sGrowSlot] grows, [sMaxSlot] most entries seen (updated on grow and Reset).
        public static int[] sCounters;
        public static int sGrowSlot, sMaxSlot;

        public FastRefTable() { Init(kInitialBits); }

        public int Used { get { return mUsed; } }

        void Init(int bits)
        {
            int cap = 1 << bits;
            mKeys = new object[cap];
            mValues = new int[cap];
            mMask = cap - 1;
            mShift = 32 - bits;
            mGrowAt = cap / 3 * 2;
            mUsed = 0;
        }

        // Slot holding o, or the empty slot where o goes.
        int Slot(object o)
        {
            object[] k = mKeys;
            int m = mMask;
            int i = (int)((uint)(RuntimeHelpers.GetHashCode(o) * -1640531527) >> mShift);
            object x;
            while ((x = k[i]) != null && x != o) i = (i + 1) & m;
            return i;
        }

        public bool TryGet(object o, out int index)
        {
            if (o == null) throw new NullReferenceException(); // the game's table throws here too (o.GetHashCode())
            int i = Slot(o);
            if (mKeys[i] == null) { index = 0; return false; }
            index = mValues[i];
            return true;
        }

        // Adds o with newIndex when absent (true); else index = its stored index (false).
        public bool TryAdd(object o, int newIndex, out int index)
        {
            if (o == null) throw new NullReferenceException();
            int i = Slot(o);
            if (mKeys[i] != null) { index = mValues[i]; return false; }
            Fill(i, o, newIndex);
            index = newIndex;
            return true;
        }

        // Insert or overwrite.
        public void Set(object o, int index)
        {
            if (o == null) throw new NullReferenceException();
            int i = Slot(o);
            if (mKeys[i] != null) { mValues[i] = index; return; }
            Fill(i, o, index);
        }

        void Fill(int i, object o, int index)
        {
            mKeys[i] = o;
            mValues[i] = index;
            if (++mUsed > mGrowAt) Grow();
        }

        void Grow()
        {
            object[] oldKeys = mKeys;
            int[] oldValues = mValues;
            int used = mUsed;
            Init(32 - mShift + 1);
            for (int j = 0; j < oldKeys.Length; j++)
            {
                object o = oldKeys[j];
                if (o == null) continue;
                int i = Slot(o);
                mKeys[i] = o;
                mValues[i] = oldValues[j];
            }
            mUsed = used;
            int[] c = sCounters;
            if (c != null) { c[sGrowSlot]++; if (used > c[sMaxSlot]) c[sMaxSlot] = used; }
        }

        // Back to an empty small table (drops the big arrays, as the game's Clear drops its nodes).
        public void Reset()
        {
            int[] c = sCounters;
            if (c != null && mUsed > c[sMaxSlot]) c[sMaxSlot] = mUsed;
            if (mShift != 32 - kInitialBits) Init(kInitialBits);
            else if (mUsed > 0) { Array.Clear(mKeys, 0, mKeys.Length); mUsed = 0; }
        }
    }
}

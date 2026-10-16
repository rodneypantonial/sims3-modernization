using System;
using System.Runtime.CompilerServices;

namespace Ts3Fixes
{
    // Per-save cache of the save serializer's per-Type answers for one ScriptCore.PersistedClassDB (CachedReflection.cs
    // hooks Persist.GetPersistedTypeCode and PersistedClassDB.GetPersistedClass). No game types here, so
    // tools/selftest/typecache can compile it outside the game.
    //
    // Why a hit may skip the game's code: for one DB, once a call for type T has returned, every later call for T with the
    // same arguments returns the same value and changes nothing (the class entry exists, Init ran once, the class index was
    // assigned, the field list was built; docs/26 "Cached reflection"). The one exception is a call made while some
    // PersistedClass.Init is still running: Init marks a class tested before it decides mPersistable, so a nested call can
    // see a half-built class. sBusy counts running Inits (hooked) and nothing is stored while it is above 0.
    //
    // One table keyed by the Type object's identity (types are unique objects in Mono [inferred]; a second object for the
    // same type would only miss and run the game's code, which still answers right). Open addressing, linear probing,
    // grown at 2/3, like FastRefTable. mVals packs: bits 0-7 type code, 8-23 type flags, 24 code stored, 25 class stored
    // for requireClassReference false, 26 class stored for true.
    public sealed class TypeCache
    {
        const int kInitialBits = 9; // 512 slots; a Bridgeport save meets a few thousand types [inferred]
        const int kCode = 1 << 24, kClassAny = 1 << 25, kClassRef = 1 << 26;

        public static int sBusy;       // > 0 while a PersistedClass.Init runs
        public static TypeCache sCur;  // cache of the DB seen last

        public readonly object Owner;  // the PersistedClassDB
        public readonly bool On;       // switch state when this DB was first seen; off = the hooks run the game's code
        object[] mKeys, mClasses;
        int[] mVals;
        int mMask, mShift, mUsed, mGrowAt;

        // Counters, null outside the game: [sHitSlot] answers from the cache, [sStoreSlot] answers stored.
        public static int[] sCounters;
        public static int sHitSlot, sStoreSlot;

        public TypeCache(object owner, bool on)
        {
            Owner = owner;
            On = on;
            if (on) Init(kInitialBits);
        }

        void Init(int bits)
        {
            int cap = 1 << bits;
            mKeys = new object[cap];
            mClasses = new object[cap];
            mVals = new int[cap];
            mMask = cap - 1;
            mShift = 32 - bits;
            mGrowAt = cap / 3 * 2;
            mUsed = 0;
        }

        int Slot(object o)
        {
            object[] k = mKeys;
            int m = mMask;
            int i = (int)((uint)(RuntimeHelpers.GetHashCode(o) * -1640531527) >> mShift);
            object x;
            while ((x = k[i]) != null && x != o) i = (i + 1) & m;
            return i;
        }

        // Stored type code and flags for type.
        public bool TryCode(object type, out int code, out int flags)
        {
            int v = mVals[Slot(type)];
            if ((v & kCode) == 0) { code = flags = 0; return false; }
            code = v & 0xFF;
            flags = (v >> 8) & 0xFFFF;
            Hit();
            return true;
        }

        public void StoreCode(object type, int code, int flags)
        {
            if (sBusy != 0 || (code & ~0xFF) != 0 || (flags & ~0xFFFF) != 0) return; // flags known to the game fit 11 bits
            int i = Slot(type);
            mVals[i] |= kCode | (code & 0xFF) | ((flags & 0xFFFF) << 8);
            Stored(i, type);
        }

        // Stored GetPersistedClass answer (may be null: not persistable) for type with this requireClassReference.
        public bool TryClass(object type, bool requireRef, out object pclass)
        {
            int i = Slot(type);
            if ((mVals[i] & (requireRef ? kClassRef : kClassAny)) == 0) { pclass = null; return false; }
            pclass = mClasses[i];
            Hit();
            return true;
        }

        // A null answer or a requireClassReference true answer also stands for false (the game returns the same object then).
        public void StoreClass(object type, bool requireRef, object pclass)
        {
            if (sBusy != 0) return;
            int i = Slot(type);
            mClasses[i] = pclass;
            mVals[i] |= requireRef || pclass == null ? kClassAny | kClassRef : kClassAny;
            Stored(i, type);
        }

        void Stored(int i, object type)
        {
            int[] c = sCounters;
            if (c != null) c[sStoreSlot]++;
            if (mKeys[i] != null) return;
            mKeys[i] = type;
            if (++mUsed > mGrowAt) Grow();
        }

        static void Hit()
        {
            int[] c = sCounters;
            if (c != null) c[sHitSlot]++;
        }

        void Grow()
        {
            object[] oldKeys = mKeys, oldClasses = mClasses;
            int[] oldVals = mVals;
            int used = mUsed;
            Init(32 - mShift + 1);
            for (int j = 0; j < oldKeys.Length; j++)
            {
                object o = oldKeys[j];
                if (o == null) continue;
                int i = Slot(o);
                mKeys[i] = o;
                mClasses[i] = oldClasses[j];
                mVals[i] = oldVals[j];
            }
            mUsed = used;
        }
    }
}

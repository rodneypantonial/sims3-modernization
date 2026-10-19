using System;
using System.Collections.Generic;

namespace Ts3Fixes
{
    // Game-independent half of the P3 door mirror (docs/105 §10.5): the per-door record and the per-lot table with its
    // epochs. No game types here, so tools/selftest/doormirror can compile it outside the game. DoorMirror.cs reads the
    // doors and holds the CommonDoor.ImportContent postfix.

    // One door of a lot, as the door handler (CommonDoor.IsAllowedThrough, docs/105 §3 row 4a) reads it: the four lock
    // fields plus the class facts it tests, and the room pair its portals join.
    public sealed class DoorRecord
    {
        public const int kNoRoom = 36863;   // the game's "no room" id (one-way default, GetRoomConnections fallback)
        public const uint kLockMax = 6;     // CommonDoor.tLock: Nobody 0, SelectedActor 1, SelectedHousehold 2, Anybody 3, Pets 4, OwnerList 5, VIPRoom 6
        public const uint kSelectedHousehold = 2;
        // Kind bits: Door (the only class the Sim handler asks, docs/103 §1), IBarnDoor, GateDouble, IElectronicGate,
        // GameObject.CanLargeAnimalUse, DoorSingleGender (its own rule, tier P, docs/105 §3 row 4b).
        public const int kIsDoor = 1, kBarnDoor = 2, kGateDouble = 4, kElectronicGate = 8, kLargeAnimalUse = 16, kSingleGender = 32;

        public ulong ObjectId;              // the portal owner id: the key of the native portal answer (docs/105 §2)
        public uint LockType;               // mLockType
        public ulong LockOwnerId;           // mLockOwner.SimDescriptionId, 0 = no owner
        public ulong LockOwnerHouseholdId;  // mLockOwner.Household.HouseholdId, 0 = none
        public ulong[] OwnerList;           // copy of mOwnerList (SimDescription ids), never the game's list
        public int OneWayRoom;              // mOneWayDestinationRoomId, kNoRoom = both ways
        public int RoomA, RoomB;            // GetRoomConnections(): the two rooms this door's portals join
        public int Kind;

        // The record of one door, or null when the door cannot be mirrored and its lot must stay invalid: no object id,
        // a lock value outside tLock, or SelectedHousehold with no owner (the game's rule would throw on it, so only the
        // game's own code may answer that door).
        public static DoorRecord Make(ulong objectId, uint lockType, ulong ownerId, ulong ownerHouseholdId, IList<ulong> ownerList,
                                      int oneWayRoom, int[] rooms, int kind)
        {
            if (objectId == 0 || lockType > kLockMax || (lockType == kSelectedHousehold && ownerId == 0)) return null;
            DoorRecord r = new DoorRecord();
            r.ObjectId = objectId;
            r.LockType = lockType;
            r.LockOwnerId = ownerId;
            r.LockOwnerHouseholdId = ownerHouseholdId;
            r.OwnerList = new ulong[ownerList == null ? 0 : ownerList.Count];
            for (int i = 0; i < r.OwnerList.Length; i++) r.OwnerList[i] = ownerList[i];
            r.OneWayRoom = oneWayRoom;
            bool two = rooms != null && rooms.Length >= 2;
            r.RoomA = two ? rooms[0] : kNoRoom;
            r.RoomB = two ? rooms[1] : kNoRoom;
            r.Kind = kind;
            return r;
        }
    }

    // Per-lot door records, built lazily on the sim thread at the first ask after an invalidation (docs/105 §10.3 point
    // 1, §10.4.5). Epoch: a number that changes every time a lot is invalidated, so a reader that kept the records of
    // epoch E knows they are still current while Get answers E with a non-null array.
    public sealed class DoorMirrorTable
    {
        // Appends the lot's door records to into; false = some door could not be read (the lot stays invalid).
        public delegate bool LotReader(ulong lotId, List<DoorRecord> into);

        sealed class Entry { public uint Epoch; public DoorRecord[] Doors; } // Doors null = invalid

        readonly Dictionary<ulong, Entry> mLots = new Dictionary<ulong, Entry>();
        readonly List<DoorRecord> mScratch = new List<DoorRecord>();
        uint mClock;
        public int Builds, Refused, Invalidations;

        Entry EntryOf(ulong lotId)
        {
            Entry e;
            if (!mLots.TryGetValue(lotId, out e)) { e = new Entry(); e.Epoch = ++mClock; mLots[lotId] = e; }
            return e;
        }

        // The lot's records (built by read when invalid), or null when read refused or threw; the epoch is the lot's
        // current one either way. A refused lot is read again at the next call.
        public DoorRecord[] Get(ulong lotId, LotReader read, out uint epoch)
        {
            Entry e = EntryOf(lotId);
            epoch = e.Epoch;
            if (e.Doors != null) return e.Doors;
            mScratch.Clear();
            bool ok;
            try { ok = read(lotId, mScratch); } catch (Exception) { ok = false; }
            if (!ok) { Refused++; return null; }
            Builds++;
            return e.Doors = mScratch.ToArray();
        }

        public void Invalidate(ulong lotId)
        {
            Entry e = EntryOf(lotId);
            e.Doors = null;
            e.Epoch = ++mClock;
            Invalidations++;
        }

        public void InvalidateAll()
        {
            foreach (Entry e in mLots.Values) { e.Doors = null; e.Epoch = ++mClock; }
            Invalidations++;
        }
    }

    // Encoder for the ts3native door channel (docs/94 §14, decoder SpDoorMsg in native/ts3native/snap.cpp): one
    // Route.GetOption2 call per message, RouteOption2 bits 63 + 58 set, the code in bits 48-55 and a 32-bit value in
    // bits 0-31 (native sees optHi = 0x84000000 | code << 16, optLo = value). Answer true = accepted. Without
    // ts3native the game answers false for every message, so Query reports "no channel" and nothing is pushed.
    public static class DoorChannel
    {
        public const int kLotLo = 1, kLotHi = 2, kObjLo = 3, kObjHi = 4, kKind = 5, kLock = 6, kOwnerLo = 7, kOwnerHi = 8,
            kHhLo = 9, kHhHi = 10, kOneWay = 11, kRoomA = 12, kRoomB = 13, kListLo = 14, kListHi = 15, kEpoch = 16, kEnd = 17,
            kInvLo = 18, kInvHi = 19, kInvAll = 20, kQueryLo = 21, kQueryHi = 22;
        public const ulong kBits = 0x8400000000000000UL;

        public delegate bool Sender(ulong option); // one GetOption2 call with this RouteOption2 value

        public static ulong Option(int code, uint value) { return kBits | (ulong)(uint)code << 48 | value; }

        static bool Pair(Sender s, int loCode, ulong v) { return s(Option(loCode, (uint)v)) && s(Option(loCode + 1, (uint)(v >> 32))); }

        // 1 = ts3native's table of the lot is valid in its current epoch, 0 = not valid, -1 = no channel (the low half,
        // which ts3native always accepts, was refused: no ts3native, or a build without the door channel)
        public static int Query(ulong lot, Sender s)
        {
            if (!s(Option(kQueryLo, (uint)lot))) return -1;
            return s(Option(kQueryHi, (uint)(lot >> 32))) ? 1 : 0;
        }

        // The lot's records: open (clears the native lot), epoch, per door its fields and owner list, then END with the
        // count, which ts3native checks before it marks the lot valid. False at the first refused message (lot stays invalid).
        public static bool Push(ulong lot, DoorRecord[] doors, uint epoch, Sender s)
        {
            if (!Pair(s, kLotLo, lot) || !s(Option(kEpoch, epoch))) return false; // epoch once per lot, so an empty lot has one too
            foreach (DoorRecord d in doors)
            {
                if (!Pair(s, kObjLo, d.ObjectId) || !s(Option(kKind, (uint)d.Kind)) || !s(Option(kLock, d.LockType))
                    || !Pair(s, kOwnerLo, d.LockOwnerId) || !Pair(s, kHhLo, d.LockOwnerHouseholdId) || !s(Option(kOneWay, (uint)d.OneWayRoom))
                    || !s(Option(kRoomA, (uint)d.RoomA)) || !s(Option(kRoomB, (uint)d.RoomB))) return false;
                foreach (ulong id in d.OwnerList) if (!Pair(s, kListLo, id)) return false;
            }
            return s(Option(kEnd, (uint)doors.Length));
        }

        public static bool Invalidate(ulong lot, Sender s) { return Pair(s, kInvLo, lot); }

        public static bool InvalidateAll(Sender s) { return s(Option(kInvAll, 0)); }

        // The adapter step for one lot at a route START: ask ts3native; when its table of the lot is not valid, drop our
        // cached records too (ts3native also invalidates on what our table never hears: world epochs, the door lock
        // event, build/buy exit) and push a fresh read. A refused read pushes nothing, so the lot stays invalid there.
        // Returns -1 no channel, 1 already valid, 0 nothing pushed, 2 pushed.
        public static int Sync(ulong lot, DoorMirrorTable table, DoorMirrorTable.LotReader read, Sender s)
        {
            int q = Query(lot, s);
            if (q != 0) return q;
            table.Invalidate(lot);
            uint epoch;
            DoorRecord[] d = table.Get(lot, read, out epoch);
            return d != null && Push(lot, d, epoch, s) ? 2 : 0;
        }
    }
}

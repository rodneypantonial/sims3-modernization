// Usage: node tools/selftest/doormirror/run.ts  (compiles this with mods/ts3fixes/src/DoorMirrorCore.cs)
// Offline self-check of the P3 door mirror records and per-lot table (docs/105 §10.5) with fake doors: record building
// (owner list copied, room fallback, unreadable doors refused), lazy build and epochs, refused or throwing reads keep
// the lot invalid, and a random model run where every mutation is followed by the invalidation the game hooks would
// send: the table must then always answer exactly what a fresh read of the fake doors gives. Exit code 1 on failure.
using System;
using System.Collections.Generic;
using Ts3Fixes;

static class DoorMirrorTest
{
    static int sFails;
    static void Check(bool ok, string what) { if (!ok) { sFails++; Console.WriteLine("FAIL: " + what); } }

    sealed class FakeDoor
    {
        public ulong Id; public uint Lock = 3; public ulong Owner, OwnerHousehold; public List<ulong> OwnerList = new List<ulong>();
        public int OneWay = DoorRecord.kNoRoom; public int[] Rooms = { 1, 2 }; public int Kind = DoorRecord.kIsDoor; public bool Bound = true;
    }

    static readonly Dictionary<ulong, List<FakeDoor>> sLots = new Dictionary<ulong, List<FakeDoor>>();
    static int sReads;
    static bool sThrow;

    // Same shape as DoorMirror.ReadLotDoors: false on a missing lot or the first door that cannot be mirrored.
    static bool Read(ulong lotId, List<DoorRecord> into)
    {
        sReads++;
        if (sThrow) throw new InvalidOperationException("fake read failure");
        List<FakeDoor> doors;
        if (!sLots.TryGetValue(lotId, out doors)) return false;
        foreach (FakeDoor d in doors)
        {
            if (!d.Bound) return false;
            DoorRecord r = DoorRecord.Make(d.Id, d.Lock, d.Owner, d.OwnerHousehold, d.OwnerList, d.OneWay, d.Rooms, d.Kind);
            if (r == null) return false;
            into.Add(r);
        }
        return true;
    }

    static bool Same(DoorRecord a, FakeDoor d)
    {
        if (a.ObjectId != d.Id || a.LockType != d.Lock || a.LockOwnerId != d.Owner || a.LockOwnerHouseholdId != d.OwnerHousehold
            || a.OneWayRoom != d.OneWay || a.RoomA != d.Rooms[0] || a.RoomB != d.Rooms[1] || a.Kind != d.Kind || a.OwnerList.Length != d.OwnerList.Count) return false;
        for (int i = 0; i < a.OwnerList.Length; i++) if (a.OwnerList[i] != d.OwnerList[i]) return false;
        return true;
    }

    static void Records()
    {
        List<ulong> owners = new List<ulong>(new ulong[] { 11, 12 });
        DoorRecord r = DoorRecord.Make(0x100, 5, 0, 0, owners, 7, new int[] { 3, 4 }, DoorRecord.kIsDoor | DoorRecord.kBarnDoor);
        Check(r != null && r.LockType == 5 && r.OneWayRoom == 7 && r.RoomA == 3 && r.RoomB == 4 && r.Kind == 3, "OwnerList door: fields copied");
        owners.Add(13);
        Check(r.OwnerList.Length == 2 && r.OwnerList[1] == 12, "owner list is a copy, not the game's list");
        r = DoorRecord.Make(0x101, 3, 0, 0, null, DoorRecord.kNoRoom, new int[] { 9 }, 0);
        Check(r != null && r.OwnerList.Length == 0 && r.RoomA == DoorRecord.kNoRoom && r.RoomB == DoorRecord.kNoRoom, "null list and short room array: empty list, no-room pair");
        Check(DoorRecord.Make(0, 3, 0, 0, null, 0, null, 0) == null, "object id 0 refused");
        Check(DoorRecord.Make(0x102, 7, 0, 0, null, 0, null, 0) == null, "lock value outside tLock refused");
        Check(DoorRecord.Make(0x103, 2, 0, 0, null, 0, null, 0) == null, "SelectedHousehold without owner refused");
        Check(DoorRecord.Make(0x104, 2, 21, 0, null, 0, null, 0) != null, "SelectedHousehold with owner kept (homeless owner: household 0)");
        Check(DoorRecord.Make(0x105, 1, 0, 0, null, 0, null, 0) != null, "SelectedActor without owner kept (the game's rule answers it: restricted)");
        for (uint t = 0; t <= DoorRecord.kLockMax; t++)
            Check(DoorRecord.Make(0x106, t, 21, 31, null, 0, null, 0) != null, "every tLock value with an owner kept: " + t);
    }

    static void TableRules()
    {
        sLots.Clear();
        FakeDoor a = new FakeDoor(); a.Id = 0x200;
        FakeDoor b = new FakeDoor(); b.Id = 0x201; b.Lock = 0;
        sLots[1] = new List<FakeDoor>(new FakeDoor[] { a, b });
        sLots[2] = new List<FakeDoor>();
        DoorMirrorTable t = new DoorMirrorTable();
        DoorMirrorTable.LotReader read = Read;
        uint e1, e2, e3;
        sReads = 0;
        DoorRecord[] r1 = t.Get(1, read, out e1);
        Check(r1 != null && r1.Length == 2 && Same(r1[0], a) && Same(r1[1], b) && sReads == 1, "first ask builds");
        DoorRecord[] r2 = t.Get(1, read, out e2);
        Check(r2 == r1 && e2 == e1 && sReads == 1, "second ask: same array, same epoch, no read");
        a.Lock = 0; // an import writes Nobody without a lock event
        Check(t.Get(1, read, out e2) == r1, "without the postfix the table is stale (why the ImportContent postfix exists)");
        t.Invalidate(1);
        DoorRecord[] r3 = t.Get(1, read, out e3);
        Check(e3 != e1 && r3 != r1 && r3[0].LockType == 0 && sReads == 2, "after Invalidate: new epoch, rebuilt with the imported lock");
        DoorRecord[] empty = t.Get(2, read, out e2);
        Check(empty != null && empty.Length == 0 && e2 != e3, "lot without doors: empty table, its own epoch");

        b.Bound = false; // a door whose managed target is not bound yet
        t.Invalidate(1);
        int refused = t.Refused;
        Check(t.Get(1, read, out e1) == null && t.Refused == refused + 1, "unbound door: lot invalid, never defaults");
        Check(t.Get(1, read, out e2) == null && e2 == e1 && t.Refused == refused + 2, "refused lot is read again at the next ask, epoch unchanged");
        b.Bound = true;
        r1 = t.Get(1, read, out e2);
        Check(r1 != null && r1.Length == 2 && e2 == e1, "bound again: built under the same epoch");
        Check(t.Get(3, read, out e3) == null, "unknown lot: invalid");

        uint before1, before2, after1, after2;
        t.Get(1, read, out before1); t.Get(2, read, out before2);
        t.InvalidateAll();
        int reads = sReads;
        t.Get(1, read, out after1); t.Get(2, read, out after2);
        Check(after1 != before1 && after2 != before2 && after1 != after2 && sReads == reads + 2, "InvalidateAll: every lot new epoch and rebuilt");

        t.Invalidate(2);
        uint again;
        t.Get(1, read, out again);
        Check(again == after1, "Invalidate of one lot leaves the other's epoch");

        sThrow = true;
        t.Invalidate(1);
        Check(t.Get(1, read, out e1) == null, "a throwing read: lot invalid, no exception out");
        sThrow = false;
        Check(t.Get(1, read, out e1) != null, "next ask after the throw builds");
    }

    // Random lots and doors; every mutation is followed by the invalidation its game hook sends (lock change: that
    // lot; load: all). After any op, a valid answer must equal a fresh read and an epoch seen with valid records must
    // still mean the same records.
    static void RandomModel()
    {
        Random rnd = new Random(105);
        for (int run = 0; run < 50; run++)
        {
            sLots.Clear();
            ulong next = 0x1000;
            for (ulong lot = 1; lot <= 6; lot++)
            {
                List<FakeDoor> doors = new List<FakeDoor>();
                int n = rnd.Next(0, 5);
                for (int i = 0; i < n; i++) { FakeDoor d = new FakeDoor(); d.Id = next++; d.Lock = (uint)rnd.Next(0, 7); d.Owner = d.Lock == 2 ? 50UL : 0UL; doors.Add(d); }
                sLots[lot] = doors;
            }
            DoorMirrorTable t = new DoorMirrorTable();
            DoorMirrorTable.LotReader read = Read;
            Dictionary<ulong, KeyValuePair<uint, DoorRecord[]>> seen = new Dictionary<ulong, KeyValuePair<uint, DoorRecord[]>>();
            for (int op = 0; op < 400; op++)
            {
                ulong lot = (ulong)rnd.Next(1, 7);
                List<FakeDoor> doors = sLots[lot];
                int k = rnd.Next(10);
                if (k < 3 && doors.Count > 0)
                {
                    FakeDoor d = doors[rnd.Next(doors.Count)];
                    d.Lock = (uint)rnd.Next(0, 7); d.Owner = d.Lock == 2 ? 60UL : 0UL; d.OwnerHousehold = d.Owner == 0 ? 0UL : 70UL;
                    if (rnd.Next(3) == 0) d.OwnerList.Add((ulong)rnd.Next(100));
                    t.Invalidate(lot);
                }
                else if (k == 3 && doors.Count > 0)
                {
                    doors[rnd.Next(doors.Count)].Bound = rnd.Next(2) == 0;
                    t.Invalidate(lot);
                }
                else if (k == 4)
                {
                    FakeDoor d = new FakeDoor(); d.Id = next++; doors.Add(d); // door placed on the lot
                    t.Invalidate(lot);
                }
                else if (k == 5 && rnd.Next(8) == 0) t.InvalidateAll();
                else
                {
                    uint e;
                    DoorRecord[] r = t.Get(lot, read, out e);
                    bool allBound = true;
                    foreach (FakeDoor d in doors) allBound &= d.Bound;
                    if (!allBound) { Check(r == null, "run " + run + " op " + op + ": unbound door answered"); continue; }
                    Check(r != null && r.Length == doors.Count, "run " + run + " op " + op + ": bound lot not built");
                    if (r == null || r.Length != doors.Count) continue;
                    for (int i = 0; i < r.Length; i++) Check(Same(r[i], doors[i]), "run " + run + " op " + op + ": stale record");
                    KeyValuePair<uint, DoorRecord[]> old;
                    if (seen.TryGetValue(lot, out old) && old.Key == e) Check(old.Value == r, "run " + run + " op " + op + ": same epoch, other records");
                    seen[lot] = new KeyValuePair<uint, DoorRecord[]>(e, r);
                }
            }
        }
    }

    // C# port of ts3native's door channel decoder (SpDoorMsg in native/ts3native/snap.cpp, contract docs/94 §14) plus
    // the GetOptionHook routing test (bits 63 + 58 in the high byte, else the game's answer: false). Keep it in step
    // with snap.cpp; NativeVectors replays snap.cpp's own selftest sequence (native/ts3native/selftest.cpp, "door
    // channel") on it, so a drift in either shows up there.
    sealed class PortDecoder
    {
        public sealed class Door { public ulong Obj, Owner, Household; public uint Kind, Lock, OneWay, RoomA, RoomB; public List<ulong> Ids = new List<ulong>(); }
        public sealed class Lot { public bool Valid; public uint Epoch, MEpoch; public List<Door> Doors = new List<Door>(); }
        public readonly Dictionary<ulong, Lot> Lots = new Dictionary<ulong, Lot>();
        public uint NativeEpoch;
        public int Calls, Errors, Ends, Invalidations;
        uint mLotLo, mObjLo, mOwnLo, mHhLo, mIdLo, mEpoch;
        Lot mOpen;
        Door mCur;

        public bool Send(ulong opt)
        {
            uint hi = (uint)(opt >> 32), lo = (uint)opt;
            if ((hi & 0xFF000000) != 0x84000000) return false;
            Calls++;
            return Decode(lo, hi);
        }
        public bool Msg(uint code, uint v) { return Decode(v, 0x84000000 | code << 16); }

        public void InvalidateAll() { foreach (Lot l in Lots.Values) if (l.Valid) { l.Valid = false; Invalidations++; } }
        public Lot Find(ulong key) { Lot l; Lots.TryGetValue(key, out l); return l; }
        public Door FindDoor(ulong lot, ulong obj)
        {
            Lot l = Find(lot);
            if (l == null || !l.Valid || l.Epoch != NativeEpoch) return null;
            foreach (Door d in l.Doors) if (d.Obj == obj) return d;
            return null;
        }

        bool Err() { Errors++; mOpen = null; mCur = null; return false; }
        void Flush() { if (mCur != null) { mOpen.Doors.Add(mCur); mCur = null; } }

        bool Decode(uint lo, uint hi)
        {
            uint code = (hi >> 16) & 0xFF;
            ulong key = (ulong)lo << 32 | mLotLo;
            switch (code)
            {
                case 1: case 18: case 21: mLotLo = lo; return true;
                case 2:
                {
                    Lot l = Find(key);
                    if (l == null) Lots[key] = l = new Lot();
                    l.Doors.Clear(); l.Valid = false; l.MEpoch = 0;
                    mOpen = l; mCur = null;
                    return true;
                }
                case 19: { Lot l = Find(key); if (l != null && l.Valid) { l.Valid = false; Invalidations++; } return true; }
                case 20: InvalidateAll(); return true;
                case 22: { Lot l = Find(key); return l != null && l.Valid && l.Epoch == NativeEpoch; }
            }
            if (mOpen == null) return Err();
            switch (code)
            {
                case 3: Flush(); mObjLo = lo; return true;
                case 4: mCur = new Door(); mCur.Obj = (ulong)lo << 32 | mObjLo; return true;
                case 16: mEpoch = lo; return true;
                case 17:
                {
                    Lot l = mOpen;
                    Flush();
                    mOpen = null;
                    if ((uint)l.Doors.Count != lo) return Err();
                    l.Valid = true; l.Epoch = NativeEpoch; l.MEpoch = mEpoch; Ends++;
                    return true;
                }
            }
            if (mCur == null) return Err();
            switch (code)
            {
                case 5: mCur.Kind = lo; return true;
                case 6: mCur.Lock = lo; return true;
                case 7: mOwnLo = lo; return true;
                case 8: mCur.Owner = (ulong)lo << 32 | mOwnLo; return true;
                case 9: mHhLo = lo; return true;
                case 10: mCur.Household = (ulong)lo << 32 | mHhLo; return true;
                case 11: mCur.OneWay = lo; return true;
                case 12: mCur.RoomA = lo; return true;
                case 13: mCur.RoomB = lo; return true;
                case 14: mIdLo = lo; return true;
                case 15: mCur.Ids.Add((ulong)lo << 32 | mIdLo); return true;
                default: return Err();
            }
        }
    }

    // snap.cpp's selftest sequence for the door channel, message for message, with its expected answers
    static void NativeVectors()
    {
        PortDecoder n = new PortDecoder();
        n.NativeEpoch = 5;
        Check(n.Msg(21, 0x123) && !n.Msg(22, 3), "vec: query of an unknown lot");
        Check(n.Msg(1, 0x123) && n.Msg(2, 3), "vec: open");
        Check(n.Msg(3, 0xA1) && n.Msg(4, 0) && n.Msg(5, 1) && n.Msg(6, 2) && n.Msg(12, 4) && n.Msg(13, 5), "vec: door A1");
        Check(n.Msg(3, 0xA2) && n.Msg(4, 0) && n.Msg(5, 9) && n.Msg(6, 6) && n.Msg(7, 0x55) && n.Msg(8, 0), "vec: door A2");
        Check(n.Msg(9, 0x66) && n.Msg(10, 0) && n.Msg(11, 36863) && n.Msg(14, 0x71) && n.Msg(15, 0), "vec: door A2 fields");
        Check(n.Msg(14, 0x72) && n.Msg(15, 1) && n.Msg(16, 42) && n.Msg(17, 2), "vec: list, epoch, end");
        const ulong lot = 0x300000123UL;
        PortDecoder.Door d = n.FindDoor(lot, 0xA2);
        Check(d != null && d.Kind == 9 && d.Lock == 6 && d.Owner == 0x55 && d.Household == 0x66 && d.OneWay == 36863 && d.Ids.Count == 2, "vec: A2 decoded");
        Check(d != null && d.Ids[0] == 0x71 && d.Ids[1] == 0x100000072UL && n.Find(lot).MEpoch == 42, "vec: A2 owner list and managed epoch");
        Check(n.FindDoor(lot, 0xA1) != null && n.FindDoor(lot, 0xA1).RoomB == 5 && n.FindDoor(lot, 0xA3) == null, "vec: A1, no A3");
        Check(n.Msg(21, 0x123) && n.Msg(22, 3), "vec: query valid");
        n.NativeEpoch++;
        Check(n.FindDoor(lot, 0xA2) == null && !n.Msg(22, 3), "vec: world epoch makes the lot stale");
        n.NativeEpoch--;
        int e0 = n.Errors;
        Check(n.Msg(1, 0x123) && n.Msg(2, 3) && n.Msg(3, 1) && n.Msg(4, 0) && !n.Msg(17, 2), "vec: count mismatch refused");
        Check(n.FindDoor(lot, 1) == null && !n.Msg(5, 1) && n.Errors == e0 + 2, "vec: lot invalid, field outside open is an error");
        Check(n.Msg(1, 0x123) && n.Msg(2, 3) && n.Msg(17, 0) && n.Msg(18, 0x123) && n.Msg(19, 3), "vec: empty lot, then invalidate");
        Check(!n.Msg(22, 3) && n.Invalidations >= 1, "vec: invalidated");
        Check(n.Send(0x8400000000000000UL | (ulong)20 << 48), "vec: through the hook (invalidate all)");
        Check(!n.Send(0x0400000000000000UL | (ulong)20 << 48), "vec: without bit 63 not ours");
    }

    static bool Same(DoorRecord a, PortDecoder.Door d)
    {
        if (a.ObjectId != d.Obj || a.LockType != d.Lock || a.LockOwnerId != d.Owner || a.LockOwnerHouseholdId != d.Household || (uint)a.OneWayRoom != d.OneWay
            || (uint)a.RoomA != d.RoomA || (uint)a.RoomB != d.RoomB || (uint)a.Kind != d.Kind || a.OwnerList.Length != d.Ids.Count) return false;
        for (int i = 0; i < a.OwnerList.Length; i++) if (a.OwnerList[i] != d.Ids[i]) return false;
        return true;
    }

    static ulong R64(Random rnd) { return (ulong)(uint)rnd.Next() << 33 ^ (ulong)(uint)rnd.Next() << 2 ^ (ulong)rnd.Next(4); }

    // Encoder bits, the round trip of random records through Push into the port, and the adapter (Sync) against the
    // port with native-only invalidations (world epoch, door lock event) and managed ones (Invalidate over the channel).
    static void Channel()
    {
        Check(DoorChannel.Option(17, 0xDEADBEEF) == 0x84110000DEADBEEFUL && (uint)(DoorChannel.Option(22, 1) >> 32) == (0x84000000u | 22u << 16), "option bits");
        Random rnd = new Random(94);
        for (int run = 0; run < 200; run++)
        {
            PortDecoder n = new PortDecoder();
            n.NativeEpoch = (uint)run;
            ulong lot = R64(rnd);
            DoorRecord[] doors = new DoorRecord[rnd.Next(0, 6)];
            for (int i = 0; i < doors.Length; i++)
            {
                List<ulong> ids = new List<ulong>();
                for (int j = rnd.Next(0, 4); j > 0; j--) ids.Add(R64(rnd));
                uint lk = (uint)rnd.Next(0, 7);
                doors[i] = DoorRecord.Make(R64(rnd) | 1, lk, lk == 2 ? R64(rnd) | 1 : R64(rnd), R64(rnd), ids, rnd.Next(2) == 0 ? DoorRecord.kNoRoom : rnd.Next(),
                                           new int[] { rnd.Next(), -rnd.Next(1, 100) }, rnd.Next(64));
            }
            uint epoch = (uint)rnd.Next();
            Check(DoorChannel.Push(lot, doors, epoch, n.Send) && n.Errors == 0, "run " + run + ": push accepted");
            PortDecoder.Lot l = n.Find(lot);
            Check(l != null && l.Valid && l.MEpoch == epoch && l.Doors.Count == doors.Length && DoorChannel.Query(lot, n.Send) == 1, "run " + run + ": lot valid");
            for (int i = 0; l != null && i < doors.Length && i < l.Doors.Count; i++) Check(Same(doors[i], l.Doors[i]), "run " + run + ": door " + i + " round trip");
            Check(DoorChannel.Invalidate(lot, n.Send) && DoorChannel.Query(lot, n.Send) == 0, "run " + run + ": invalidate");
        }

        // no ts3native: the game answers false; nothing is read or pushed
        DoorMirrorTable t = new DoorMirrorTable();
        DoorMirrorTable.LotReader read = Read;
        sLots.Clear();
        FakeDoor a = new FakeDoor(); a.Id = 0x200; FakeDoor b = new FakeDoor(); b.Id = 0x201; b.Lock = 5; b.OwnerList.Add(9);
        sLots[7] = new List<FakeDoor> { a, b };
        int reads = sReads, calls = 0;
        Check(DoorChannel.Sync(7, t, read, delegate(ulong o) { calls++; return false; }) == -1 && sReads == reads && calls == 1, "no channel: one call, no read");

        PortDecoder p = new PortDecoder();
        p.NativeEpoch = 1;
        Check(DoorChannel.Sync(7, t, read, p.Send) == 2 && p.FindDoor(7, 0x201) != null && p.FindDoor(7, 0x201).Ids[0] == 9, "first START pushes");
        reads = sReads;
        Check(DoorChannel.Sync(7, t, read, p.Send) == 1 && sReads == reads, "valid lot: query only");
        a.Lock = 0; p.InvalidateAll(); // the door lock event: native only, our table never hears it
        Check(DoorChannel.Sync(7, t, read, p.Send) == 2 && p.FindDoor(7, 0x200).Lock == 0, "native lock-event invalidation: fresh read pushed");
        p.NativeEpoch++; a.Lock = 3;
        Check(DoorChannel.Sync(7, t, read, p.Send) == 2 && p.FindDoor(7, 0x200).Lock == 3, "native world epoch: fresh read pushed");
        b.Bound = false; p.InvalidateAll();
        Check(DoorChannel.Sync(7, t, read, p.Send) == 0 && p.FindDoor(7, 0x200) == null && DoorChannel.Query(7, p.Send) == 0, "refused read: nothing pushed, lot invalid natively");
        b.Bound = true;
        Check(DoorChannel.Sync(7, t, read, p.Send) == 2, "bound again: pushed");
        Check(DoorChannel.Invalidate(7, p.Send) && DoorChannel.Sync(7, t, read, p.Send) == 2, "managed Invalidate over the channel: next START pushes");
        Check(DoorChannel.InvalidateAll(p.Send) && DoorChannel.Query(7, p.Send) == 0, "managed InvalidateAll over the channel");
        Check(p.Errors == 0, "adapter: no protocol errors");
    }

    static int Main()
    {
        Records();
        TableRules();
        RandomModel();
        NativeVectors();
        Channel();
        Console.WriteLine(sFails == 0 ? "doormirror selftest: all checks passed" : "doormirror selftest: " + sFails + " failures");
        return sFails == 0 ? 0 : 1;
    }
}

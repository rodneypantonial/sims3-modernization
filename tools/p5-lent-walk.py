"""docs/155: 5-hop closure of every sim-thread +0x918 taker that can run in a P5b / P5c / TailUnlock lent window, and of the
job phases that run beside or after it. Direct calls plus indirect targets from docs/data/routing-indirect-sites.csv
(resolved / partly resolved); unresolved sites and sites the CSV does not cover add no edge and are listed.

    python tools/p5-lent-walk.py --cache DIR [--csv-out docs/data/p5-lent-unresolved.csv]

--cache: directory with mono-glue-index-v2.pkl (built by `tools/mono-glue-closure.py --cache DIR` from game-copy/bin/TS3W.exe).
Prints per root: closure size, alarm functions with their hop (writer leaves, events, destroys, D2 sites, lot planner), wait
imports, locks. shortcut: reachability only, no control flow (a hit says "can reach", not "under +0x918"); read the order of
the call sites (`--order ROOT`) where it matters.
"""
import argparse, bisect, collections, csv, os, pickle

REPO = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
ROOTS = {  # root: (name, class) - takers by wait key (docs/152 §2), then job phases
    0x82EDB0: ("steering ObstacleQuery", "P5b"), 0x82F0B0: ("goal footprint gather", "P5b"),
    0x759B20: ("goal validation, key 0x75A64C", "P5b"), 0x62EF30: ("gate 0 G-a transform wrapper", "P5b gates"),
    0x62EAB0: ("gate 0 GetOrInsert", "P5b gates"), 0x62F760: ("gate 12 F-B PortalsForObject", "P5b gates"),
    0x76F0A0: ("F-B slot helper", "P5b gates"), 0x631800: ("gate 12 F-C, key 0x631814", "P5b gates"),
    0x631F10: ("gate 12 F-G, key 0x631F40", "P5b gates"), 0x62FB60: ("gate 12 GetIfPresent, key 0x62FB74", "kept out"),
    0x831530: ("RouteToObjectCheck", "P5c"), 0x80E5B0: ("C-A polygon rebuild", "P5c"), 0x833560: ("UpdateSimFootprint", "P5c"),
    0x8334A0: ("ObjectFootprintFromTool", "P5c"), 0x82D280: ("UpdateObjectLot", "P5c"), 0x82A0C0: ("RemovePrivateFootprint", "P5c"),
    0x831BA0: ("AddPrivateFootprint, key 0x831BF4", "P5c"), 0x834370: ("Discourage add", "P5c"), 0x834860: ("Discourage remove", "P5c"),
    0x7606E0: ("gate 10 caller, key 0x760BFF", "kept out / RunPlan"), 0x729E10: ("AsyncLoader, key 0x729E2C", "not +0x918"),
    0x728DB0: ("AsyncLoader, key 0x728DC3", "not +0x918"), 0x7345D0: ("key 0x7345F2", "not +0x918"),
    0x4AFFC0: ("GetPackageSource, key 0x4B0021", "not +0x918"),
    0x82C420: ("prelude ApplyPendingUpdates", "job phase"), 0x64C7C0: ("tail pass 16", "job phase"),
    0x64D5B0: ("tail pass 17", "job phase"), 0x64A690: ("tail pass 18", "job phase"), 0x641DF0: ("tail pass 19", "job phase"),
}
ALARM = {
    0x4E16F0: "Lock_Acquire", 0x78F2B0: "World_FireEvent", 0x791C00: "event 59 host call", 0x939420: "DestroyById (D2)",
    0x835240: "gate 14 (D2)", 0x835110: "gate 15 (D2)", 0xAAC110: "save (D2)", 0x78AE40: "BuildRoutingData (D2)", 0x8361B0: "SaveLot (D2)",
    0x665C50: "W1", 0x666750: "W2", 0x62F8A0: "W3", 0x63CB90: "W4 LotGraph_Build", 0x62E680: "W5 LotTable_Insert",
    0x80E5B0: "W6a", 0x80E610: "W6b", 0x80E980: "W6c", 0x80EC30: "W6d", 0x80E0B0: "W6e", 0x80F6A0: "W6f", 0x80CB40: "W6g door",
    0x806770: "W6g", 0x8321F0: "FPCORE", 0x831BA0: "AddPrivateFootprint", 0x82A0C0: "footprint remove", 0x635700: "LotRemoveObjectFootprints",
    0x829D50: "RemoveLot", 0x82B590: "ApplyLotUpdate", 0x636BF0: "grid add", 0x62D9F0: "grid remove", 0x670510: "water mark",
    0x666A40: "QT flush", 0x667480: "QT pinned query", 0x62EAB0: "GetOrInsert", 0x625940: "Rehash", 0x628A90: "Contains",
    0x61E320: "Q reset", 0x63D2F0: "RoomGraph_FirstUse", 0x75C2D0: "goal step", 0x82C6E0: "Route_PlanLocked",
    0x6823E0: "phys add", 0x682080: "phys remove", 0x6819B0: "phys move", 0xC68680: "WM RebuildLotIndex", 0xB54B60: "partition rebuild",
    0x7460F0: "object Release", 0x6379E0: "LotRouting_AddPortal", 0x6399E0: "RemovePortals", 0x82EDB0: "ObstacleQuery",
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cache", required=True)
    ap.add_argument("--sites", default=os.path.join(REPO, "docs", "data", "routing-indirect-sites.csv"))
    ap.add_argument("--csv-out")
    ap.add_argument("--order", help="print the call sites of one function in address order with the alarms each reaches")
    a = ap.parse_args()
    idx = pickle.load(open(os.path.join(a.cache, "mono-glue-index-v2.pkl"), "rb"))
    ind = {}
    for r in csv.DictReader(open(a.sites, encoding="utf-8")):
        if not r["site"].startswith("0x"):
            continue
        ind[int(r["site"], 16)] = (r["status"], [int(x.split()[0], 16) for x in r["targets"].split(";") if x.strip().startswith("0x")])

    def edges(f):
        e = idx[f]
        out = [t for _, t in e["calls"]]
        for (site, _, kind, val) in e["ind"]:
            st, tg = ind.get(site, ("not in csv", []))
            out += tg
            if st.startswith("unresolved") or st == "not in csv":
                unres.append((f, site, kind, str(val)[:60], st))
        return out

    def walk(root, depth=5, track=True):
        seen, q, hits = {root: 0}, collections.deque([root]), {}
        while q:
            f = q.popleft()
            h = seen[f]
            if f in ALARM and ALARM[f] not in hits:
                hits[ALARM[f]] = h
            if f not in idx or h >= depth:
                continue
            for t in edges(f):
                if t in idx and t not in seen:
                    seen[t] = h + 1
                    q.append(t)
        return seen, hits

    if a.order:
        root = int(a.order, 16)
        for site, t in sorted(idx[root]["calls"]):
            unres = []
            _, hits = walk(t, 3)
            if hits:
                print(hex(site), hex(t), ", ".join("%s@%d" % kv for kv in hits.items()))
        return
    rows = []
    for root, (name, cls) in ROOTS.items():
        unres = []
        seen, hits = walk(root)
        apis, locks = set(), set()
        for f in seen:
            if f in idx:
                apis |= {x for x in map(str, idx[f]["apis"]) if "Wait" in x or "Sleep" in x}
                locks |= set(map(str, idx[f]["locks"]))
        u = sorted(set(unres))
        print("%s %-38s %-18s fns %3d  unresolved %3d  waits %s\n    %s" % (hex(root), name, cls, len(seen), len(u), sorted(apis),
              ", ".join("%s@%d" % kv for kv in sorted(hits.items(), key=lambda kv: kv[1]))))
        rows += [(hex(root), name, cls, hex(f), hex(s), k, v, st) for f, s, k, v, st in u]
    if a.csv_out:
        with open(a.csv_out, "w", newline="", encoding="utf-8") as fh:
            w = csv.writer(fh)
            w.writerow(["root", "root name", "class", "function", "site", "kind", "operand", "status"])
            w.writerows(rows)
        print("%d rows, %d distinct sites -> %s" % (len(rows), len({r[4] for r in rows}), a.csv_out))


if __name__ == "__main__":
    main()

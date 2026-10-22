"""Static callee tree of one native function (docs/150: Route_PlanLocked 0x82C6E0 to 5 hops), BFS with the first parent of
each node, direct calls plus indirect sites resolved by tools/mono-glue-closure.py's Resolver (docs/102/106 facts from
docs/data/routing-indirect-sites.csv, receiver typing; unresolved sites add no edge).

    python tools/route-job-callees.py --cache DIR [--root 0x82C6E0] [--depth 5] [--csv docs/data/route-planlocked-callees.csv]

--cache: directory with mono-glue-index-v2.pkl (built by `tools/mono-glue-closure.py --cache DIR`; built here if missing).
Columns: hop, addr, name, parent (first BFS parent), via (d = direct, i:<status> = indirect).
"""
import argparse, collections, importlib.util, os, pickle

REPO = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
spec = importlib.util.spec_from_file_location("mgc", os.path.join(REPO, "tools", "mono-glue-closure.py"))
mgc = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mgc)


def tree(idx, res, root, depth):
    seen = {root: (0, None, "root")}
    q = collections.deque([root])
    while q:
        fn = q.popleft()
        h, f = seen[fn][0], idx.get(fn)
        if f is None or h >= depth:
            continue
        outs = [(t, "d") for _, t in f["calls"]]
        sig = collections.defaultdict(set)
        for (_, _, kind, val) in f["ind"]:
            if kind == "slot" and "'?'" not in repr(val[0]):
                sig[repr(val[0])].add(val[1])
        for (a, mn, kind, val) in f["ind"]:
            s = res.resolve(fn, a, mn, kind, val, sig.get(repr(val[0])) if kind == "slot" else None)
            outs += [(t, "i:%s" % s[0]) for t in s[2]]
        for t, how in outs:
            if t in idx and t not in seen:
                seen[t] = (h + 1, fn, how)
                q.append(t)
    return seen


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", default=os.path.join(REPO, "game-copy", "bin", "TS3W.exe"))
    ap.add_argument("--functions", default=os.path.join(REPO, "game-copy", "analysis", "functions.csv"))
    ap.add_argument("--cache", required=True)
    ap.add_argument("--root", default="0x82C6E0")
    ap.add_argument("--depth", type=int, default=5)
    ap.add_argument("--csv", default=os.path.join(REPO, "docs", "data", "route-planlocked-callees.csv"))
    a = ap.parse_args()
    pe = mgc.PE(a.exe)
    cache = os.path.join(a.cache, "mono-glue-index-v2.pkl")
    if os.path.exists(cache):
        idx = pickle.load(open(cache, "rb"))
    else:
        idx = mgc.build_index(pe, a.functions)
        pickle.dump(idx, open(cache, "wb"))
    names = {}
    for line in open(a.functions).read().splitlines()[1:]:
        e, _, n = line.split(",")[:3]
        names[int(e, 16)] = n
    names.update(mgc.load_names(os.path.join(REPO, "tools", "ghidra", "symbols", "ts3w-1.67.symbols.csv")))
    res = mgc.Resolver(pe, idx, mgc.load_prior(os.path.join(REPO, "docs", "data", "routing-indirect-sites.csv")))
    seen = tree(idx, res, int(a.root, 16), a.depth)
    with open(a.csv, "w", newline="\n") as o:
        o.write("hop,addr,name,parent,via\n")
        for ad, (h, p, how) in sorted(seen.items(), key=lambda x: (x[1][0], x[0])):
            o.write("%d,0x%X,%s,%s,%s\n" % (h, ad, names.get(ad, "FUN_%08X" % ad), "0x%X" % p if p else "", how))
    print("nodes", len(seen), "by hop", dict(sorted(collections.Counter(v[0] for v in seen.values()).items())), "->", a.csv)


if __name__ == "__main__":
    main()

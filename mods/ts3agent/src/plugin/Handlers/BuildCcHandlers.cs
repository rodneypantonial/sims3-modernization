using System;
using System.Collections.Generic;
using Sims3.Gameplay;
using Sims3.Gameplay.Abstracts;
using Sims3.Gameplay.Actors;
using Sims3.Gameplay.Core;
using Sims3.SimIFace;
using Sims3.SimIFace.BuildBuy;
using Sims3.UI;

namespace Ts3Agent
{
    // docs/200 §6: Build-mode CC (tools/cc-gen.ts --kind build: wall / floor patterns, doors, windows, fences, terrain
    // paints, railings, stairs) through the game's own build tools, with the same cycle marks as bb_cc_cycle
    // (<prefix>-full / <prefix>-empty, each followed by a forced-GC pair; MemStressHandlers.cs MarkGc).
    // STRESS (labelled, not a player flow): the counts and the loop are the agent's; each single step is the player's path
    // (tool 1 wallpaper / tool 2 floor with a catalogue preset, tool 4 with a wall object, sell tool 12, mouse at window pixels).
    public static partial class Handlers
    {
        // inv_pool {add?:[keys], clear?}: object product instances buy_object {pool: true, sim_inventory: true} picks from
        // (the cc-synth set's manifest items with inventoriable: true, tools/cc-gen.ts --kind inv), as cas_pool for CAS parts;
        // sent in chunks, kept until clear or a reload. in_catalogue: how many of them the Buy catalogue lists now.
        static readonly Dictionary<ulong, bool> sInvPool = new Dictionary<ulong, bool>();
        static readonly Dictionary<ulong, bool> sInvRefused = new Dictionary<ulong, bool>();  // pool products a Sim inventory refused: out for the session
        static object InvPool(Args a)
        {
            if (a.Bool("clear", false)) { sInvPool.Clear(); sInvRefused.Clear(); }
            if (a.Has("add")) foreach (object o in a.List("add")) sInvPool[ParseKey(Convert.ToString(o)).InstanceId] = true;
            Dictionary<string, object> r = Util.Obj();
            r["pool"] = sInvPool.Count;
            r["in_catalogue"] = InvPoolProducts().Count;
            r["refused"] = sInvRefused.Count;
            return r;
        }

        static List<BuildBuyProduct> InvPoolProducts()
        {
            List<BuildBuyProduct> list = new List<BuildBuyProduct>();
            foreach (BuildBuyProduct p in SortedCatalog()) if (sInvPool.ContainsKey(p.ProductResourceKey.InstanceId) && !sInvRefused.ContainsKey(p.ProductResourceKey.InstanceId)) list.Add(p);
            return list;
        }

        // buy_object {pool: true, sim_inventory: true, n, nth?, sim?}: STRESS (labelled, not a player flow), free: one object of
        // each of the n distinct pool products nth .. nth+n-1 (catalogue order by key) straight into the Sim's inventory, so the
        // steps of one cycle (nth 0, 25, 50, 75) never repeat a product. A product the inventory refuses (TryToAdd false) leaves
        // the pool for the session and the next unused one takes its place, so later nth still index distinct products; fails
        // only when the usable pool runs out (the objects it took stay tracked for remove_objects).
        static object BuyFromInvPool(Args a)
        {
            if (!a.Bool("sim_inventory", false)) throw new McpException("pool: only with sim_inventory");
            if (sInvPool.Count == 0) throw new McpException("inventory pool is empty: inv_pool add first");
            List<BuildBuyProduct> c = InvPoolProducts();
            int n = (int)a.Long("n", 1), nth = (int)a.Long("nth", 0);
            if (nth < 0 || n < 1 || nth + n > c.Count)
                throw new McpException("inventory pool: " + c.Count + " pool products in the catalogue (pool " + sInvPool.Count + " keys), asked for " + n + " distinct from #" + nth);
            Sim s = a.Has("sim") ? Util.FindSim(a, "sim") : Util.ActiveSim();
            int added = 0;
            List<string> refused = new List<string>();
            for (int i = nth; i < c.Count && added < n; i++)
            {
                GameObject g = GlobalFunctions.CreateObjectOutOfWorld(c[i].ProductResourceKey) as GameObject;
                if (g == null || !s.Inventory.TryToAdd(g))
                {
                    if (g != null) g.Destroy();
                    sInvRefused[c[i].ProductResourceKey.InstanceId] = true;
                    refused.Add(c[i].CatalogName);
                    continue;
                }
                CreatedObjects().Add(g.ObjectId.Value);  // remove_objects takes them out again
                added++;
            }
            if (added < n)
                throw new McpException(s.FullName + "'s inventory: pool ran out of usable distinct products, added " + added + " of " + n + " from #" + nth + " (refused now " + refused.Count + ", session " + sInvRefused.Count + "): " + string.Join(", ", refused.GetRange(0, Math.Min(3, refused.Count)).ToArray()));
            Dictionary<string, object> r = Util.Obj();
            r["sim"] = s.FullName;
            r["added"] = added;
            r["first"] = nth;
            r["pool_products"] = c.Count - refused.Count;
            r["refused"] = refused.Count;
            r["refused_session"] = sInvRefused.Count;
            if (refused.Count > 0) r["refused_names"] = refused.GetRange(0, Math.Min(10, refused.Count));
            return r;
        }

        // patterns of one kind, one preset per product, sorted by product key; cc: names containing want, else the others
        static List<BuildBuyPreset> PatternPresets(bool wall, string want, bool cc)
        {
            List<object> src = wall ? UserToolUtils.GetWallPatternPresetListByType(0xFFFFFFFF) : UserToolUtils.GetFloorPatternPresetListByType(0xFFFFFFFF);
            List<BuildBuyPreset> list = new List<BuildBuyPreset>();
            List<string> seen = new List<string>();
            if (src != null)
                foreach (object o in src)
                {
                    BuildBuyPreset p = o as BuildBuyPreset;
                    if (p == null || p.Product == null) continue;
                    bool named = (p.Product.CatalogName ?? "").ToLowerInvariant().IndexOf(want) >= 0;
                    string k = KeyString(p.Product.ProductResourceKey);
                    if (named != cc || seen.Contains(k)) continue;
                    seen.Add(k);
                    list.Add(p);
                }
            list.Sort(delegate(BuildBuyPreset x, BuildBuyPreset y) { return string.CompareOrdinal(KeyString(x.Product.ProductResourceKey), KeyString(y.Product.ProductResourceKey)); });
            return list;
        }

        const uint kOpeningCategories = (uint)(BuildBuyProduct.eBuildCategory.kBuildCategoryDoor | BuildBuyProduct.eBuildCategory.kBuildCategoryWindow
            | BuildBuyProduct.eBuildCategory.kBuildCategoryArch | BuildBuyProduct.eBuildCategory.kBuildCategoryModularArch);

        // door / window / arch: the Build catalogue category, or on the wall (kWFOnWall). IsWallObject alone listed 30 of the
        // 60 cc-build-200 doors + windows (2026-10-26 bp-ccb-catalog): one of the two kinds has no kWFOnWall flag.
        static bool IsOpening(BuildBuyProduct p) { return p.IsWallObject || (p.BuildCategoryFlags & kOpeningCategories) != 0; }

        // products of a build tool type named like want; Object also takes the Buy catalogue (doors / windows may be in either list)
        static List<BuildBuyProduct> NamedProducts(UserToolUtils.BuildBuyProductType type, string want, bool wallObjects)
        {
            List<BuildBuyProduct> list = new List<BuildBuyProduct>();
            List<string> seen = new List<string>();
            List<object> src = new List<object>();
            List<object> l = UserToolUtils.GetProductList(type);
            if (l != null) src.AddRange(l);
            if (type == UserToolUtils.BuildBuyProductType.Object) foreach (BuildBuyProduct p in Catalog()) src.Add(p);
            foreach (object o in src)
            {
                BuildBuyProduct p = o as BuildBuyProduct;
                if (p == null || (wallObjects && !IsOpening(p)) || (p.CatalogName ?? "").ToLowerInvariant().IndexOf(want) < 0) continue;
                string k = KeyString(p.ProductResourceKey);
                if (seen.Contains(k)) continue;
                seen.Add(k);
                list.Add(p);
            }
            list.Sort(delegate(BuildBuyProduct x, BuildBuyProduct y) { return string.CompareOrdinal(KeyString(x.ProductResourceKey), KeyString(y.ProductResourceKey)); });
            return list;
        }

        // build_cc_catalog {name?: "CCGen", min_wall?, min_floor?, min_wall_objects?, min_fence?, min_stairs?, min_railing?, min_terrain?}:
        // how many items named like name each Build catalogue list holds (the lists the build UI fills its pucks from);
        // fails when a count is below its min (load smoke: the synthetic set is in the catalogue).
        static object BuildCcCatalog(Args a)
        {
            string want = a.Str("name", "CCGen").ToLowerInvariant();
            Dictionary<string, object> r = Util.Obj();
            r["wall"] = PatternPresets(true, want, true).Count;
            r["floor"] = PatternPresets(false, want, true).Count;
            List<BuildBuyProduct> opens = NamedProducts(UserToolUtils.BuildBuyProductType.Object, want, true);
            r["wall_objects"] = opens.Count;
            int doors = 0, windows = 0, onWall = 0;
            foreach (BuildBuyProduct p in opens)
            {
                if ((p.BuildCategoryFlags & (uint)BuildBuyProduct.eBuildCategory.kBuildCategoryDoor) != 0) doors++;
                if ((p.BuildCategoryFlags & (uint)BuildBuyProduct.eBuildCategory.kBuildCategoryWindow) != 0) windows++;
                if (p.IsWallObject) onWall++;
            }
            r["doors"] = doors; r["windows"] = windows; r["on_wall"] = onWall;
            r["fence"] = NamedProducts(UserToolUtils.BuildBuyProductType.Fence, want, false).Count;
            r["stairs"] = NamedProducts(UserToolUtils.BuildBuyProductType.ModularStairs, want, false).Count;
            r["railing"] = NamedProducts(UserToolUtils.BuildBuyProductType.ModularRailings, want, false).Count;
            r["terrain"] = NamedProducts(UserToolUtils.BuildBuyProductType.PaintBrush, want, false).Count;
            List<string> low = new List<string>();
            foreach (string k in new string[] { "wall", "floor", "wall_objects", "fence", "stairs", "railing", "terrain" })
                if (a.Has("min_" + k) && (int)r[k] < (int)a.Long("min_" + k)) low.Add(k + " " + r[k] + " < " + a.Long("min_" + k));
            if (low.Count > 0) throw new McpException("build catalogue misses the set: " + string.Join(", ", low.ToArray()) + " (counts: " + Json.Write(r) + ")");
            return r;
        }

        static Vector3 Plus(Vector3 p, Vector3 d, float s) { return new Vector3(p.x + d.x * s, p.y + d.y * s, p.z + d.z * s); }
        static Vector3 Minus(Vector3 a, Vector3 b) { return new Vector3(a.x - b.x, a.y - b.y, a.z - b.z); }

        // world position of the low corner of home-lot tile (lx, lz) on level 0: GetWorldPosition gives either the corner or the
        // centre [not checked]; a quarter tile towards the low side tells which (still the same tile = centre)
        static Vector3 TileCorner(Lot lot, int lx, int lz)
        {
            Vector3 p = Vector3.Zero, px = Vector3.Zero, pz = Vector3.Zero;
            World.GetWorldPosition(lot.LotId, new LotLocation(lx, lz, 0, 0), ref p);
            World.GetWorldPosition(lot.LotId, new LotLocation(lx + 1, lz, 0, 0), ref px);
            World.GetWorldPosition(lot.LotId, new LotLocation(lx, lz + 1, 0, 0), ref pz);
            Vector3 ex = Minus(px, p), ez = Minus(pz, p);
            LotLocation back = new LotLocation();
            World.GetLotLocation(Plus(Plus(p, ex, -0.25f), ez, -0.25f), ref back);
            return back.mX == lx && back.mZ == lz ? Plus(Plus(p, ex, -0.5f), ez, -0.5f) : p;
        }

        // a window pixel on the wall face above floor point p (ground homography pixel, then up until the wall pick lands
        // within 0.9 m of p); null when no wall is under any of them
        static double[] WallPixel(Vector3 p)
        {
            double[] px = ToPixels(p.x, p.z, double.NaN);
            for (int dy = 4; dy <= 120; dy += 4)
            {
                Vector3 hit = Vector3.Zero, orig = Vector3.Zero, dest = Vector3.Zero;
                if (!World.PickWallAtWindowPoint((int)px[0], (int)(px[1] - dy), ref hit, ref orig, ref dest)) continue;
                if (Math.Abs(hit.x - p.x) + Math.Abs(hit.z - p.z) < 0.9f) return new double[] { px[0], px[1] - dy };
            }
            return null;
        }

        static void Drag(double[] p0, double[] p1, uint dwell) { Drag(p0, p1, dwell, 0u); }

        // mods: Sims3.UI.Modifiers (2 = Ctrl), as SceneMgrWindow forwards them
        static void Drag(double[] p0, double[] p1, uint dwell, uint mods)
        {
            UserToolUtils.ForwardUserToolMouseEvent(UserToolUtils.UserToolMouseEventType.kUserToolMouseMove, (float)p0[0], (float)p0[1], 0u, mods);
            Ticks(dwell);
            UserToolUtils.ForwardUserToolMouseEvent(UserToolUtils.UserToolMouseEventType.kUserToolMouseDown, (float)p0[0], (float)p0[1], 1000u, mods);
            for (int s = 1; s <= 8; s++)
            {
                double t = s / 8.0;
                UserToolUtils.ForwardUserToolMouseEvent(UserToolUtils.UserToolMouseEventType.kUserToolMouseMove,
                    (float)(p0[0] + (p1[0] - p0[0]) * t), (float)(p0[1] + (p1[1] - p0[1]) * t), 0u, mods);
                Ticks(1);
            }
            UserToolUtils.ForwardUserToolMouseEvent(UserToolUtils.UserToolMouseEventType.kUserToolMouseUp, (float)p1[0], (float)p1[1], 1000u, mods);
            Ticks(dwell);
        }

        static void Tool(uint tool, ResourceKey key, int preset)
        {
            if (preset >= 0) UserToolUtils.UserToolGeneric(tool, key, (uint)preset); else UserToolUtils.UserToolGeneric(tool, key);
            UIManager.GetSceneWindow().StartForwardingEventsToUserTools();
        }

        // build_cc_cycle {name?: "CCGen", cycles?: 3, walls?: 10, floors?: 24, openings?: 3, room?: [lx0, lz0, w, d], dwell_ticks?: 10,
        // prefix?: "bcc"} | {result: true}: live mode, active household's home lot, needs free ground for the room (default 10 x 4
        // tiles round the lot centre tile). Cycle 1 first builds the room with the room tool (25, first wall product) unless
        // its far wall is already there. Per cycle: enter Build mode, camera inside the room looking at its far wall; walls
        // distinct CC wall patterns, one per far-wall segment (tool 1, click on the wall face); floors distinct CC floor
        // patterns, one per interior tile (tool 2, click in the tile); openings distinct CC doors / windows on far-wall
        // segments 1, 4, 7, ... (tool 4, click on the wall face; placement seen by World.OnObjectPlacedInLot); mark
        // <prefix>-full + GC; sell tool click on every placed opening (missed ones destroyed: fallback_deletes), far wall and
        // the painted tiles repainted with the first EA wall / floor pattern (the CC patterns lose their last user); exit to
        // live mode; mark <prefix>-empty + GC. Cycle c takes the patterns / products from (c-1)*count (wraps).
        // phases?: ["patterns", "walls", "portals"] (default all; portals needs walls). After the pattern cycles,
        // building_cycles?: 2 cycles of phase 2 (walls: a levels?: 3 floor building, building?: [lx0, lz0, w, d] or the first
        // free 16 x 12 / 12 x 12 / 12 x 8 / 8 x 8 home-lot area nearest the centre, split into room_size?: 4 tile rooms; marks
        // <prefix>w-full / -empty) with phase 3 inside (portals: up to doors?: 72 CC + EA doors on the room grid's x-lines and
        // stairs?: 2 per level below the top; marks <prefix>p-full / -empty). BuildCcBuildingHandlers.cs, docs/200 §7.
        static Dictionary<string, object> sBccResult;
        static object BuildCcCycle(Args a)
        {
            if (a.Has("result"))
            {
                if (sBccResult == null) throw new McpException("no build_cc_cycle started");
                return sBccResult;
            }
            if (sBccResult != null && !(bool)sBccResult["done"]) throw new McpException("build_cc_cycle still running");
            RequireLive();
            Lot lot = HomeLot();
            if (lot == null) throw new McpException("no home lot for the active household");
            string want = a.Str("name", "CCGen").ToLowerInvariant(), prefix = a.Str("prefix", "bcc");
            int cycles = Math.Max(1, (int)a.Long("cycles", 3)), nWall = (int)a.Long("walls", 10), nFloor = (int)a.Long("floors", 24), nOpen = (int)a.Long("openings", 3);
            uint dwell = (uint)Math.Max(1, a.Long("dwell_ticks", 10));
            List<BuildBuyPreset> walls = PatternPresets(true, want, true), floors = PatternPresets(false, want, true);
            List<BuildBuyPreset> eaWall = PatternPresets(true, want, false), eaFloor = PatternPresets(false, want, false);
            List<BuildBuyProduct> opens = NamedProducts(UserToolUtils.BuildBuyProductType.Object, want, true);
            List<string> phases = new List<string>();
            if (a.Has("phases")) foreach (object o in a.List("phases")) phases.Add(Convert.ToString(o));
            else phases.AddRange(new string[] { "patterns", "walls", "portals" });
            bool doPatterns = phases.Contains("patterns"), doWalls = phases.Contains("walls"), doPortals = phases.Contains("portals");
            if (doPortals && !doWalls) throw new McpException("phase portals needs phase walls (the doors go on its building)");
            if (doPatterns && nWall > 0 && (walls.Count == 0 || eaWall.Count == 0)) throw new McpException("no CC (" + walls.Count + ") or EA (" + eaWall.Count + ") wall pattern named like '" + want + "'");
            if (doPatterns && nFloor > 0 && (floors.Count == 0 || eaFloor.Count == 0)) throw new McpException("no CC (" + floors.Count + ") or EA (" + eaFloor.Count + ") floor pattern named like '" + want + "'");
            if (doPatterns && nOpen > 0 && opens.Count == 0) throw new McpException("no CC door / window named like '" + want + "'");
            List<object> wallProducts = UserToolUtils.GetProductList(UserToolUtils.BuildBuyProductType.Wall);
            BuildBuyProduct wallStyle = wallProducts != null && wallProducts.Count > 0 ? wallProducts[0] as BuildBuyProduct : null;
            LotLocation centreLoc = new LotLocation();
            World.GetLotLocation(lot.GetCenterPosition(), ref centreLoc);
            int W = 10, D = 4, x0 = centreLoc.mX - 5, z0 = centreLoc.mZ - 2;
            if (a.Has("room"))
            {
                List<object> rm = a.List("room");
                x0 = Convert.ToInt32(rm[0]); z0 = Convert.ToInt32(rm[1]); W = Convert.ToInt32(rm[2]); D = Convert.ToInt32(rm[3]);
            }
            nWall = Math.Min(nWall, W);
            nFloor = Math.Min(nFloor, W * (D - 1));  // the last interior row stays EA: its tiles sit under the wall picks
            int z1 = z0 + D;
            Bld bld = doWalls ? NewBuilding(lot, a, want, dwell, wallStyle, eaFloor, new int[] { x0 - 1, z0 - 1, W + 2, D + 2 }) : null;
            int bCycles = Math.Max(1, (int)a.Long("building_cycles", 2));
            Dictionary<string, object> r = Util.Obj();
            r["phases"] = phases.ToArray();
            r["done"] = false;
            r["room"] = new object[] { x0, z0, W, D };
            r["cc_wall_patterns"] = walls.Count;
            r["cc_floor_patterns"] = floors.Count;
            r["cc_openings"] = opens.Count;
            r["cycles_done"] = 0;
            r["room_built"] = false;
            r["walls_painted"] = 0;
            r["wall_pick_misses"] = 0;
            r["floors_painted"] = 0;
            r["placed"] = 0;
            r["click_misses"] = 0;
            r["sold"] = 0;
            r["fallback_deletes"] = 0;
            List<object> perCycle = new List<object>();
            r["per_cycle"] = perCycle;
            sBccResult = r;
            RunAsync("build_cc_cycle", delegate
            {
                List<ulong> seen = new List<ulong>();
                EventHandler onPlaced = delegate(object s, EventArgs e)
                {
                    World.OnObjectPlacedInLotEventArgs pe = e as World.OnObjectPlacedInLotEventArgs;
                    if (pe != null && pe.LotId == lot.LotId && !seen.Contains(pe.ObjectId.Value)) seen.Add(pe.ObjectId.Value);
                };
                World.OnObjectPlacedInLotEventHandler += onPlaced;
                try
                {
                    Vector3 c00 = TileCorner(lot, x0, z0), cx = TileCorner(lot, x0 + 1, z0), cz = TileCorner(lot, x0, z0 + 1);
                    Vector3 ex = Minus(cx, c00), ez = Minus(cz, c00);  // 1 m along the lot's x / z
                    Vector3 centre = Plus(Plus(c00, ex, W / 2f), ez, D / 2f);
                    for (int c = 1; doPatterns && c <= cycles; c++)
                    {
                        Dictionary<string, object> pc = Util.Obj();
                        perCycle.Add(pc);
                        r["phase"] = "cycle " + c + ": enter";
                        GameStates.TransitionToBuildMode(lot, null);
                        if (!WaitReady(BuildUp, 3000)) { r["error"] = "cycle " + c + ": build mode did not open (state " + SubState() + ")"; return; }
                        Ticks(30);
                        // behind the near wall (cut away in Build mode), looking at the far wall
                        Vector3 cam = Plus(centre, ez, -12f);
                        CameraController.SetPositionAndTarget(new Vector3(cam.x, centre.y + 12f, cam.z), Plus(centre, ez, 1f));
                        Ticks(30);
                        sHomography = null;
                        int farWall = 0;
                        for (int k = 0; k < W; k++)
                            if (World.WallExistsBetween(lot.LotId, new LotLocation(x0 + k, z1 - 1, 0, 0), new LotLocation(x0 + k, z1, 0, 0))) farWall++;
                        if (farWall < W && c == 1)
                        {
                            if (wallStyle == null) { r["error"] = "no wall product for the room tool"; return; }
                            Tool(0u, wallStyle.ProductResourceKey, -1);
                            UserToolUtils.SetUserToolMode(2u);  // room, as BuildController's room button (tool 25 = WallDrag was a guess)
                            Drag(ToPixels(c00.x, c00.z, double.NaN), ToPixels(c00.x + ex.x * W + ez.x * D, c00.z + ex.z * W + ez.z * D, double.NaN), dwell);
                            Ticks(30);
                            farWall = 0;
                            for (int k = 0; k < W; k++)
                                if (World.WallExistsBetween(lot.LotId, new LotLocation(x0 + k, z1 - 1, 0, 0), new LotLocation(x0 + k, z1, 0, 0))) farWall++;
                            r["room_built"] = farWall == W;
                        }
                        pc["far_wall_segments"] = farWall;
                        if (farWall == 0) { r["error"] = "cycle " + c + ": no far wall at z " + z1 + " (room tool did not build: ground not free?)"; return; }
                        // wall-face pixels per far-wall segment (the floor point 0.15 m inside the wall, then up to the wall)
                        double[][] seg = new double[W][];
                        for (int k = 0; k < W; k++) seg[k] = WallPixel(Plus(Plus(Plus(c00, ex, k + 0.5f), ez, D), ez, -0.15f));
                        int painted = 0, pickMiss = 0;
                        for (int k = 0; k < nWall; k++)
                        {
                            if (seg[k] == null) { pickMiss++; continue; }
                            BuildBuyPreset bp = walls[((c - 1) * nWall + k) % walls.Count];
                            Tool(1u, bp.Product.ProductResourceKey, (int)bp.ID);
                            Click(seg[k], 2);
                            Ticks(dwell);
                            painted++;
                        }
                        r["phase"] = "cycle " + c + ": floors";
                        int fl = 0;
                        for (int i = 0; i < nFloor; i++)
                        {
                            BuildBuyPreset bp = floors[((c - 1) * nFloor + i) % floors.Count];
                            Vector3 t = Plus(Plus(c00, ex, i % W + 0.5f), ez, i / W + 0.5f);
                            Tool(2u, bp.Product.ProductResourceKey, (int)bp.ID);
                            double[] p = ToPixels(t.x, t.z, double.NaN);
                            Drag(p, new double[] { p[0] + 1, p[1] }, 2);
                            fl++;
                        }
                        // openings on segments 1, 4, 7, ...: room for 2-tile doors between them
                        r["phase"] = "cycle " + c + ": openings";
                        List<ulong> mine = new List<ulong>();
                        List<double[]> minePx = new List<double[]>();
                        int misses = 0;
                        for (int i = 0, k = 1; i < nOpen; i++, k += 3)
                        {
                            if (k >= W - 1 || seg[k] == null) { misses++; continue; }
                            BuildBuyProduct p = opens[((c - 1) * nOpen + i) % opens.Count];
                            Tool(4u, p.ProductResourceKey, -1);
                            int before = seen.Count;
                            Click(seg[k], 2);
                            Ticks(dwell);
                            if (seen.Count == before) { misses++; continue; }
                            for (int j = before; j < seen.Count; j++) { mine.Add(seen[j]); minePx.Add(seg[k]); }
                        }
                        Tool(4u, new ResourceKey(0uL, 0u, 0u), -1);  // hand tool: nothing on the cursor
                        pc["walls_painted"] = painted;
                        pc["wall_pick_misses"] = pickMiss;
                        pc["floors_painted"] = fl;
                        pc["placed"] = mine.Count;
                        pc["misses"] = misses;
                        r["walls_painted"] = (int)r["walls_painted"] + painted;
                        r["wall_pick_misses"] = (int)r["wall_pick_misses"] + pickMiss;
                        r["floors_painted"] = (int)r["floors_painted"] + fl;
                        r["placed"] = (int)r["placed"] + mine.Count;
                        r["click_misses"] = (int)r["click_misses"] + misses;
                        MarkGc(prefix + "-full");
                        r["phase"] = "cycle " + c + ": sell + restore";
                        int sold = 0, fallback = 0;
                        Tool(12u, new ResourceKey(0uL, 0u, 0u), -1);
                        for (int j = 0; j < mine.Count; j++)
                        {
                            GameObject o = GameObject.GetObject(new ObjectGuid(mine[j]));
                            if (o == null || !o.InWorld) continue;
                            Click(minePx[j], 2);
                            Ticks(dwell);
                            o = GameObject.GetObject(new ObjectGuid(mine[j]));
                            if (o == null || !o.InWorld) { sold++; continue; }
                            o.Destroy();  // the click hit the wall or another object: leave no CC behind
                            fallback++;
                        }
                        // restore: the same segments and tiles with the first EA patterns
                        BuildBuyPreset ew = eaWall.Count > 0 ? eaWall[0] : null, ef = eaFloor.Count > 0 ? eaFloor[0] : null;
                        if (ew != null)
                            for (int k = 0; k < nWall; k++)
                                if (seg[k] != null) { Tool(1u, ew.Product.ProductResourceKey, (int)ew.ID); Click(seg[k], 2); Ticks(dwell); }
                        if (ef != null && fl > 0)
                        {
                            Tool(2u, ef.Product.ProductResourceKey, (int)ef.ID);
                            int rows = (fl + W - 1) / W;
                            Vector3 t0 = Plus(Plus(c00, ex, 0.5f), ez, 0.5f), t1 = Plus(Plus(c00, ex, W - 0.5f), ez, rows - 0.5f);
                            Drag(ToPixels(t0.x, t0.z, double.NaN), ToPixels(t1.x, t1.z, double.NaN), dwell);
                        }
                        Tool(4u, new ResourceKey(0uL, 0u, 0u), -1);
                        pc["sold"] = sold;
                        pc["fallback_deletes"] = fallback;
                        r["sold"] = (int)r["sold"] + sold;
                        r["fallback_deletes"] = (int)r["fallback_deletes"] + fallback;
                        GameStates.TransitionToLiveMode();
                        if (!WaitReady(LiveUp, 3000)) { r["error"] = "cycle " + c + ": live mode did not come back (state " + SubState() + ")"; return; }
                        Ticks(30);
                        MarkGc(prefix + "-empty");
                        r["cycles_done"] = c;
                    }
                    if (doWalls) BuildingCycles(bld, bCycles, doPortals, prefix + "w", prefix + "p", seen, r);
                }
                catch (Exception e) { r["error"] = DescribeError(e); }
                finally
                {
                    World.OnObjectPlacedInLotEventHandler -= onPlaced;
                    r["done"] = true;
                }
            });
            return r;
        }
    }
}

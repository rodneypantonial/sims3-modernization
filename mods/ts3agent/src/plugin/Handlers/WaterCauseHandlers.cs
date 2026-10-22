using System;
using System.Collections.Generic;
using Sims3.Gameplay.Abstracts;
using Sims3.Gameplay.Actors;
using Sims3.Gameplay.Core;
using Sims3.Gameplay.Interactions;
using Sims3.Gameplay.Objects.Island;
using Sims3.Gameplay.Interfaces;
using Sims3.SimIFace;

namespace Ts3Agent
{
    // Stranding cause check (docs/143 §8.16, Rodney: tell a throttle glitch from a Sim's own intent). unseen_water_cause
    // tracks every spawn_sims Sim and classifies each water entry of an unseen one; houseboat_move drives the houseboat
    // and dock-edge repros. Read only except the repro's own moves (Go Here, houseboat autopilot, camera).
    public static partial class Handlers
    {
        // ---- unseen_water_cause ----
        // {start: true, every?: 5, intent_s?: 20, move_s?: 3, skip_ms?: 500} | {stop: true} |
        // {result: true, require?: true, ab?: true, min_entries?: 1, slack?: 1, max_unexplained?: 0}
        // A game task samples every `every` ticks. A water entry = a Sim that stood on something at the last sample (land,
        // dock, houseboat deck, or parented to a vehicle) and is now in water (not parented, y <= sea + 0.3 m, more than
        // 0.5 m of water under it), while out of view (100 m from the camera and its target, not on screen). Per entry:
        // support before (vehicle / houseboat / dock / land), interaction and posture before and after, the last water
        // intent (interaction or queued interaction named swim, dive, snorkel, scuba, jump, fish, boat, mermaid, ocean, pond,
        // water, taxi; or a route action of that kind in the route's action list: StartRoutingOnPondRouteAction,
        // GoFromPondOrOceanWalkToStandingRouteAction, GetInBoatRouteAction, ...), distance to the support's edge, support
        // movement over the last move_s seconds, and ts3native's per-Sim throttle record (viewer codes 19-22: newest skipping
        // part and its age) plus the --ab side (code 23: any part switched on).
        // Class: "intended" = water intent within intent_s before the entry (or at it); "support moved" = not intended, the
        // support moved > 0.5 m while the Sim was within 1 m of its edge and a throttle part skipped the Sim within skip_ms;
        // "unexplained" = the rest. Rule (ab: true): support_moved on == 0 and unexplained on <= unexplained off x
        // (exposure on / exposure off) + slack (exposure = unseen Sim-samples per side, so the warm-up slice's extra on time
        // does not count against on); ab: false (no --ab, all parts on): support_moved == 0 and unexplained <= max_unexplained.
        // Both need >= min_entries entries (else "not exercised") and ts3native answering.
        class WcSim
        {
            public bool known, inWater, onSupportEdge;
            public string support, inter, posture;
            public float edge = -1f;
            public int intentMs = int.MinValue;
            public string intent;
            public readonly List<KeyValuePair<int, Vector3>> supHist = new List<KeyValuePair<int, Vector3>>();
        }
        static int sWcGen, sWcSamples, sWcEvery, sWcIntentMs, sWcMoveMs, sWcSkipMs;
        static bool sWcNative;
        static Dictionary<ulong, WcSim> sWc = new Dictionary<ulong, WcSim>();
        static Dictionary<string, int> sWcCount = new Dictionary<string, int>();      // "<side> <class>"
        static Dictionary<string, List<object>> sWcRows = new Dictionary<string, List<object>>();  // class -> samples
        static int[] sWcExposure = new int[3];  // off, on, unknown
        static readonly string[] kWcSide = { "off", "on", "unknown" };
        static readonly string[] kWcPart = { "list", "avoid", "rate", "footprint" };
        static readonly string[] kWaterWords = { "Swim", "Dive", "Snorkel", "Scuba", "Jump", "Fish", "Boat", "Mermaid", "Ocean", "Pond", "Water", "Taxi" };

        static object UnseenWaterCause(Args a)
        {
            RequireLive();
            Dictionary<string, object> r = Util.Obj();
            if (a.Has("start"))
            {
                int gen = ++sWcGen;
                sWcEvery = Math.Max(1, (int)a.Long("every", 5));
                sWcIntentMs = (int)(a.Double("intent_s", 20) * 1000);
                sWcMoveMs = (int)(a.Double("move_s", 3) * 1000);
                sWcSkipMs = (int)a.Long("skip_ms", 500);
                sWcSamples = 0;
                sWcNative = false;
                sWc = new Dictionary<ulong, WcSim>();
                sWcCount = new Dictionary<string, int>();
                sWcRows = new Dictionary<string, List<object>>();
                sWcExposure = new int[3];
                Simulator.AddObject(new Sims3.Gameplay.OneShotFunctionTask(delegate
                {
                    while (gen == sWcGen)
                    {
                        try { WcSample(); } catch (Exception) { }
                        Simulator.Sleep((uint)sWcEvery);
                    }
                }));
                r["started"] = true;
                return r;
            }
            if (a.Has("stop")) { sWcGen++; r["stopped"] = true; }
            Dictionary<string, object> sides = Util.Obj();
            for (int i = 0; i < 3; i++)
            {
                Dictionary<string, object> s = Util.Obj();
                s["intended"] = WcN(kWcSide[i], "intended");
                s["support_moved"] = WcN(kWcSide[i], "support moved");
                s["unexplained"] = WcN(kWcSide[i], "unexplained");
                s["exposure"] = sWcExposure[i];
                sides[kWcSide[i]] = s;
            }
            r["samples"] = sWcSamples;
            r["native"] = sWcNative;
            r["sides"] = sides;
            Dictionary<string, object> rows = Util.Obj();
            foreach (KeyValuePair<string, List<object>> kv in sWcRows) rows[kv.Key] = kv.Value;
            r["entries"] = rows;
            int entries = 0;
            foreach (int n in sWcCount.Values) entries += n;
            int movedOn = WcN("on", "support moved"), unOn = WcN("on", "unexplained"), unOff = WcN("off", "unexplained");
            int minE = (int)a.Long("min_entries", 1);
            string why = null;
            if (!sWcNative) why = "ts3native per-Sim read not answering ([UnseenThrottle] off or an asi without viewer codes 19-23)";
            else if (entries < minE) why = "not exercised (" + entries + " water entries < " + minE + ", samples " + sWcSamples + ")";
            else if (a.Bool("ab", true))
            {
                if (WcN("unknown", "intended") + WcN("unknown", "support moved") + WcN("unknown", "unexplained") > 0) why = "entries with an unknown side";
                else if (movedOn > 0) why = "support moved on the on side: " + movedOn;
                else
                {
                    double scale = sWcExposure[0] > 0 ? (double)sWcExposure[1] / sWcExposure[0] : 0;
                    double allow = unOff * scale + a.Double("slack", 1);
                    r["unexplained_on_allowed"] = Math.Round(allow, 2);
                    if (sWcExposure[0] == 0) why = "no off-side exposure (the --ab off halves never sampled an unseen Sim)";
                    else if (unOn > allow) why = "unexplained on " + unOn + " > off " + unOff + " x exposure " + Math.Round(scale, 2) + " + slack";
                }
            }
            else
            {
                int moved = movedOn + WcN("off", "support moved") + WcN("unknown", "support moved");
                int un = unOn + unOff + WcN("unknown", "unexplained");
                if (moved > 0) why = "support moved: " + moved;
                else if (un > a.Long("max_unexplained", 0)) why = "unexplained: " + un;
            }
            r["check_ok"] = why == null;
            if (why != null) r["why"] = why;
            if (a.Bool("require", false) && why != null) throw new McpException("unseen_water_cause: " + why + WcFirst());
            return r;
        }

        static int WcN(string side, string cls) { int n; return sWcCount.TryGetValue(side + " " + cls, out n) ? n : 0; }

        // the first "support moved" / "unexplained" samples for the failure line
        static string WcFirst()
        {
            List<string> parts = new List<string>();
            foreach (string c in new string[] { "support moved", "unexplained" })
            {
                List<object> l;
                if (!sWcRows.TryGetValue(c, out l)) continue;
                for (int i = 0; i < l.Count && i < 2; i++)
                {
                    Dictionary<string, object> o = (Dictionary<string, object>)l[i];
                    parts.Add(c + ": " + o["name"] + " from " + o["support"] + ", '" + o["before"] + "' -> '" + o["after"] + "', skip age " + o["skip_age_ms"] + " ms");
                }
            }
            return parts.Count > 0 ? " (" + string.Join("; ", parts.ToArray()) + ")" : "";
        }

        static bool WcMsg(int code, uint value) { return SnapMsg(code, value); }

        static void WcSample()
        {
            float sea = World.GetSeaLevel();
            Vector3 cam = CameraController.GetPosition(), tgt = CameraController.GetTarget();
            int now = System.Environment.TickCount;
            int side = WcMsg(23, 0) ? 1 : 0;
            // code 23 answers 0 both when every part is off and when the channel is down: code 19 always answers 1 when up
            bool up = WcMsg(19, 0);
            if (up) sWcNative = true; else side = 2;
            sWcSamples++;
            foreach (Sim s in SpawnedInWorld())
            {
                WcSim w;
                if (!sWc.TryGetValue(s.ObjectId.Value, out w)) { w = new WcSim(); sWc[s.ObjectId.Value] = w; }
                Vector3 p = s.Position;
                IGameObject parent = s.Parent as IGameObject;
                float depth = sea - World.GetTerrainHeight(p.x, p.z);
                bool inWater = parent == null && p.y <= sea + 0.3f && depth > 0.5f;
                string inter = s.CurrentInteraction != null ? s.CurrentInteraction.GetType().Name : "";
                string posture = s.Posture != null ? s.Posture.GetType().Name : "";
                string intentNow = WcIntent(s);
                if (intentNow != null) { w.intentMs = now; w.intent = intentNow; }
                bool unseen = (p - cam).Length() >= 100f && (p - tgt).Length() >= 100f && !World.IsObjectOnScreen(s);
                if (unseen) sWcExposure[side]++;
                if (inWater && w.known && !w.inWater && unseen) WcEntry(s, w, side, now, inter, posture);
                // support for the next sample
                Lot lot = s.LotCurrent;
                Houseboat hb = lot != null && lot.IsHouseboatLot() ? lot.GetHouseboat() : null;
                Vector3 supPos = Vector3.Invalid;
                string support;
                if (parent != null) { support = "vehicle:" + parent.GetType().Name + " #" + Util.Id(parent.ObjectId); supPos = parent.Position; }
                else if (hb != null) { support = "houseboat:" + (lot.Name ?? "") + " #" + Util.Id(hb.ObjectId); supPos = hb.Position; }
                else if (inWater) support = "water";
                else if (depth > 0.5f) support = "dock:" + (lot != null ? lot.Name ?? lot.LotType.ToString() : "no lot");  // above deep water, not a houseboat
                else support = (depth > -1f ? "beach:" : "land:") + (lot != null ? lot.Name ?? lot.LotType.ToString() : "no lot");
                if (support != w.support) w.supHist.Clear();
                if (supPos != Vector3.Invalid) w.supHist.Add(new KeyValuePair<int, Vector3>(now, supPos));
                while (w.supHist.Count > 0 && now - w.supHist[0].Key > sWcMoveMs + 1000) w.supHist.RemoveAt(0);
                w.edge = !inWater && parent == null && (hb != null || depth > 0.5f) ? WcEdge(p, sea, hb != null ? lot.LotId : 0UL) : -1f;
                w.support = support;
                w.inter = inter;
                w.posture = posture;
                w.inWater = inWater;
                w.known = true;
            }
        }

        // the Sim's water intent now: current or queued interaction, or a route action of that kind; null when none
        static string WcIntent(Sim s)
        {
            if (s.InteractionQueue != null)
                foreach (InteractionInstance ii in s.InteractionQueue.InteractionList)
                    if (ii != null && WcWater(ii.GetType().Name)) return ii.GetType().Name;
            Sims3.Gameplay.ObjectComponents.RoutingComponent rc = s.SimRoutingComponent;
            if (rc == null) return null;
            Sims3.Gameplay.Routing.RouteAction ra = rc.GetCurrentRouteAction();
            for (int i = 0; ra != null && i < 32; i++, ra = rc.GetRouteActionAfter(ra))
                if (WcWater(ra.GetType().Name)) return "route:" + ra.GetType().Name;
            ra = rc.GetPreviousRouteAction();
            return ra != null && WcWater(ra.GetType().Name) ? "route(prev):" + ra.GetType().Name : null;
        }

        static bool WcWater(string n)
        {
            foreach (string k in kWaterWords) if (n.IndexOf(k, StringComparison.OrdinalIgnoreCase) >= 0) return true;
            return false;
        }

        // distance (0.25 m steps, 3 m cap) to the nearest point the Sim could not stand on: off the houseboat lot, or (dock)
        // no terrain / floor above the sea there. shortcut: GetLevelHeight on IP dock tiles is not verified; a dock that
        // reads as water everywhere gives 0.25 m (conservative: "near the edge"), upgrade if the dock rows show only 0.25.
        static float WcEdge(Vector3 p, float sea, ulong hbLot)
        {
            for (float rad = 0.25f; rad <= 3f; rad += 0.25f)
                for (int i = 0; i < 8; i++)
                {
                    double ang = i * Math.PI / 4;
                    Vector3 q = p + new Vector3((float)Math.Cos(ang), 0f, (float)Math.Sin(ang)) * rad;
                    bool stand;
                    if (hbLot != 0) { LotLocation loc = default(LotLocation); stand = World.GetLotLocation(q, ref loc) == hbLot; }
                    else stand = Math.Max(World.GetTerrainHeight(q.x, q.z, true), World.GetLevelHeight(q.x, q.z, 0)) > sea + 0.1f;
                    if (!stand) return rad;
                }
            return 3.25f;
        }

        static void WcEntry(Sim s, WcSim w, int side, int now, string inter, string posture)
        {
            // support movement over the last move_s seconds before the entry
            float moved = 0f;
            if (w.supHist.Count > 0)
            {
                Vector3 last = w.supHist[w.supHist.Count - 1].Value;
                foreach (KeyValuePair<int, Vector3> kv in w.supHist)
                    if (now - kv.Key <= sWcMoveMs + 1000) moved = Math.Max(moved, (last - kv.Value).Length());
            }
            // ts3native per-Sim record: newest skipping part and its age (bisect code 21 up to 65 s)
            ulong id = s.ObjectId.Value;
            int age = -1;
            string part = null;
            if (WcMsg(19, (uint)id) && WcMsg(20, (uint)(id >> 32)) && WcMsg(21, 65535))
            {
                age = 0;
                for (int bit = 1 << 15; bit > 0; bit >>= 1) if (!WcMsg(21, (uint)(age + bit - 1))) age += bit;
                for (int k = 0; k < 4; k++) if (WcMsg(22, (uint)k)) part = kWcPart[k];
            }
            bool intended = w.intentMs != int.MinValue && now - w.intentMs <= sWcIntentMs;
            bool skipped = age >= 0 && age <= sWcSkipMs;
            string cls = intended ? "intended" : moved > 0.5f && w.edge >= 0f && w.edge <= 1f && skipped ? "support moved" : "unexplained";
            string key = kWcSide[side] + " " + cls;
            int n;
            sWcCount[key] = sWcCount.TryGetValue(key, out n) ? n + 1 : 1;
            List<object> rows;
            if (!sWcRows.TryGetValue(cls, out rows)) { rows = new List<object>(); sWcRows[cls] = rows; }
            if (rows.Count >= 10) return;
            Dictionary<string, object> o = Util.Obj();
            Vector3 p = s.Position;
            o["name"] = s.FullName;
            o["side"] = kWcSide[side];
            o["x"] = Math.Round(p.x, 1); o["z"] = Math.Round(p.z, 1);
            o["y_above_sea_m"] = Math.Round(p.y - World.GetSeaLevel(), 2);
            o["support"] = w.support;
            o["before"] = w.inter + " / " + w.posture;
            o["after"] = inter + " / " + posture;
            o["intent"] = w.intent;
            o["intent_age_ms"] = w.intentMs == int.MinValue ? -1 : now - w.intentMs;
            o["edge_m"] = Math.Round(w.edge, 2);
            o["support_moved_m"] = Math.Round(moved, 2);
            o["skip_age_ms"] = age;  // -1: no skip on record for this Sim
            o["skip_part"] = part;
            rows.Add(o);
        }

        // ---- houseboat_move (repros unseen-houseboat-move, unseen-dock-edge) ----
        // {setup: "deck" | "dock", count?: 6} | {move: true, dist?: 80} | {return: true} | {check: true, require?: true} | {stop: true}
        // setup picks a houseboat with a houseboat lot (one with a household and, for "dock", a port gangway first), puts the
        // first `count` spawn_sims Sims on its deck (Houseboat.PlaceSimAtEntrance, the game's own placement) or next to its
        // gangway on the dock, and starts a task that keeps them walking (Go Here between two points every 8 s when not
        // routing); the camera moves to the community lot farthest from the boat when it is within 150 m. move =
        // Houseboat.RequestAutopilotToLocation (what the helm's "Cruise Here on Autopilot" calls, no driver) to open sea
        // `dist` m away; return = RequestAutopilotReturnToPort. The first move records which Sims stand on the support (deck =
        // on the houseboat lot, dock = anywhere) and not in water; the walk task (every ~8 s) and check then watch only those.
        // check fails only for a Sim on the support at move start that is in water now or was at a walk-task sample, with no
        // water intent (WcIntent) seen since the move; Sims that walked off (another lot, Go Home) are listed, not failed
        // (docs/143 §8.17: the first runs failed on Sims our own Go Here sent off the deck to stale world points). Both need
        // the boat to have moved >= 20 m and >= 1 Sim on the support at move start (else "not exercised").
        static int sHbGen;
        static ulong sHbBoat;
        static string sHbMode;
        static Vector3 sHbStart;
        static float sHbMaxMoved;
        static List<ulong> sHbSims = new List<ulong>();
        static Dictionary<ulong, bool> sHbAtMove;  // null until the first move; Sim -> on the support and dry then
        static Dictionary<ulong, string> sHbIntent = new Dictionary<ulong, string>();  // water intent seen since the move
        static Dictionary<ulong, bool> sHbWet = new Dictionary<ulong, bool>();  // in water at a walk-task sample since the move

        static bool HbInWater(Sim s, float sea)
        {
            Vector3 p = s.Position;
            return s.Parent == null && p.y <= sea + 0.3f && sea - World.GetTerrainHeight(p.x, p.z) > 0.5f;
        }

        // a walk target the Sim can stand on: deck = on the houseboat lot now (the boat may have sailed off the setup point),
        // dock = not water by HbInWater's test
        static bool HbStandable(Vector3 q, ulong lotId, float sea)
        {
            if (lotId != 0) { LotLocation loc = default(LotLocation); return World.GetLotLocation(q, ref loc) == lotId; }
            return !(q.y <= sea + 0.3f && sea - World.GetTerrainHeight(q.x, q.z) > 0.5f);
        }

        static object HouseboatMove(Args a)
        {
            RequireLive();
            Dictionary<string, object> r = Util.Obj();
            Houseboat boat = sHbBoat != 0 ? GameObject.GetObject<Houseboat>(new ObjectGuid(sHbBoat)) : null;
            if (a.Has("setup"))
            {
                string mode = a.Str("setup");
                if (mode != "deck" && mode != "dock") throw new McpException("setup must be deck or dock");
                boat = HbPick(mode == "dock");
                if (boat == null) throw new McpException("no houseboat with a houseboat lot" + (mode == "dock" ? " and a port gangway" : "") + " in this world (Island Paradise save?)");
                int count = (int)a.Long("count", 6);
                List<Sim> sims = SpawnedInWorld();
                if (sims.Count == 0) throw new McpException("spawn_sims first");
                sHbGen++;
                sHbBoat = boat.ObjectId.Value;
                sHbMode = mode;
                sHbStart = boat.Position;
                sHbMaxMoved = 0f;
                sHbSims = new List<ulong>();
                sHbAtMove = null;
                sHbIntent = new Dictionary<ulong, string>();
                sHbWet = new Dictionary<ulong, bool>();
                List<object> placed = new List<object>();
                List<Vector3> placedAt = new List<Vector3>();
                Lot hbLot = LotManager.GetLot(boat.GetHouseboatLotId());
                Gangway gw = boat.GetPortGangway();
                for (int i = 0; i < sims.Count && sHbSims.Count < count; i++)
                {
                    Sim s = sims[i];
                    bool ok;
                    if (mode == "deck") ok = boat.PlaceSimAtEntrance(s);
                    else
                    {
                        Vector3 pos = gw.Position + gw.ForwardVector * (2f + i), fwd = gw.ForwardVector;
                        ok = Sims3.Gameplay.GlobalFunctions.FindGoodLocationNearby(s, ref pos, ref fwd);
                        if (ok) { s.SetPosition(pos); s.SetForward(fwd); }
                    }
                    if (ok) { sHbSims.Add(s.ObjectId.Value); placed.Add(s.FullName); placedAt.Add(s.Position); }
                }
                if (sHbSims.Count == 0) throw new McpException("no Sim placed on the " + mode);
                // two pacing points: deck = the entrance (first placed Sim) and the houseboat lot's centre, both kept as offsets
                // from the live lot centre; dock = the first and last placed Sims' spots (the dock itself, not the gangway's
                // forward line, which ran into the boat's berth: the first dock runs walked Sims into the water)
                // shortcut: the deck offset ignores the boat's turn; HbStandable drops a target that falls off the lot.
                Vector3 c0 = hbLot != null ? hbLot.GetCenterPosition() : boat.Position;
                Vector3 off0 = placedAt[0] - c0, off1 = Vector3.Empty;
                Vector3 d0 = placedAt[0], d1 = placedAt[placedAt.Count - 1];
                ulong walkLot = mode == "deck" && hbLot != null ? hbLot.LotId : 0UL;
                int gen = sHbGen;
                Simulator.AddObject(new Sims3.Gameplay.OneShotFunctionTask(delegate
                {
                    bool flip = false;
                    while (gen == sHbGen)
                    {
                        try
                        {
                            float seaW = World.GetSeaLevel();
                            Houseboat b = GameObject.GetObject<Houseboat>(new ObjectGuid(sHbBoat));
                            if (b != null) sHbMaxMoved = Math.Max(sHbMaxMoved, (b.Position - sHbStart).Length());
                            flip = !flip;
                            Vector3 c = hbLot != null ? hbLot.GetCenterPosition() : (b != null ? b.Position : c0);
                            Vector3 goal = sHbMode == "deck" ? c + (flip ? off1 : off0) : (flip ? d1 : d0);
                            foreach (ulong sid in sHbSims)
                            {
                                Sim s = GameObject.GetObject<Sim>(new ObjectGuid(sid));
                                if (s == null) continue;
                                if (sHbAtMove != null)
                                {
                                    string it = WcIntent(s);
                                    if (it != null && !sHbIntent.ContainsKey(sid)) sHbIntent[sid] = it;
                                    if (HbInWater(s, seaW)) sHbWet[sid] = true;
                                }
                                if (!s.IsRouting && s.InteractionQueue.Count == 0 && HbStandable(goal, walkLot, seaW)) QueueGoHere(s, goal);
                            }
                        }
                        catch (Exception) { }
                        Simulator.Sleep(160);  // ~8 s at normal speed
                    }
                }));
                Vector3 tgt = CameraController.GetTarget();
                if ((tgt - boat.Position).Length() < 150f || (CameraController.GetPosition() - boat.Position).Length() < 150f)
                {
                    Lot far = null;
                    float best = -1f;
                    foreach (Lot l in LotManager.AllLots)
                        if (l != null && l.IsCommunityLot && !l.IsWorldLot && (l.GetCenterPosition() - boat.Position).Length() > best)
                        { best = (l.GetCenterPosition() - boat.Position).Length(); far = l; }
                    if (far != null) { Vector3 c = far.GetCenterPosition(); CameraController.SetPositionAndTarget(c + new Vector3(-30f, 40f, -30f), c); r["camera_moved_m"] = Math.Round(best, 0); }
                }
                r["boat"] = Util.Id(boat.ObjectId);
                r["boat_lot"] = hbLot != null ? hbLot.Name : null;
                r["household"] = hbLot != null && hbLot.Household != null ? hbLot.Household.Name : null;
                r["status"] = boat.CurrentStatus.ToString();
                r["placed"] = placed;
                return r;
            }
            if (a.Has("stop")) { sHbGen++; r["stopped"] = true; return r; }
            if (boat == null) throw new McpException("houseboat_move {setup} first (or the boat is gone)");
            if (a.Has("move"))
            {
                float dist = (float)a.Double("dist", 80);
                Vector3 dest = Vector3.Invalid;
                for (float rad = dist; rad <= dist + 120f && dest == Vector3.Invalid; rad += 20f)
                    for (int i = 0; i < 24; i++)
                    {
                        double ang = i * Math.PI / 12;
                        Vector3 dir = new Vector3((float)Math.Cos(ang), 0f, (float)Math.Sin(ang));
                        Vector3 q = boat.Position + dir * rad, q2 = boat.Position + dir * (rad + 20f);
                        if ((TerrainType)World.GetTerrainType(q.x, q.z, 0) == TerrainType.WorldSea && (TerrainType)World.GetTerrainType(q2.x, q2.z, 0) == TerrainType.WorldSea) { dest = q; break; }
                    }
                if (dest == Vector3.Invalid) throw new McpException("no open sea " + dist + "-" + (dist + 120) + " m from the boat");
                if (sHbAtMove == null)
                {
                    float sea0 = World.GetSeaLevel();
                    ulong lot0 = boat.GetHouseboatLotId();
                    sHbAtMove = new Dictionary<ulong, bool>();
                    int onSup = 0;
                    foreach (ulong sid in sHbSims)
                    {
                        Sim s = GameObject.GetObject<Sim>(new ObjectGuid(sid));
                        bool on = s != null && !HbInWater(s, sea0) && (sHbMode == "dock" || (s.LotCurrent != null && s.LotCurrent.LotId == lot0));
                        sHbAtMove[sid] = on;
                        if (on) onSup++;
                    }
                    r["on_support_at_move"] = onSup;
                }
                boat.RequestAutopilotToLocation(dest);
                r["dest"] = Pos(dest);
                r["autopilot_queued"] = boat.AutopilotQueueCount;
                return r;
            }
            if (a.Has("return")) { boat.RequestAutopilotReturnToPort(); r["autopilot_queued"] = boat.AutopilotQueueCount; return r; }
            // check
            float sea = World.GetSeaLevel();
            ulong lotId = boat.GetHouseboatLotId();
            List<object> rows = new List<object>();
            List<string> badNames = new List<string>();
            int bad = 0, watched = 0, left = 0;
            foreach (ulong sid in sHbSims)
            {
                Sim s = GameObject.GetObject<Sim>(new ObjectGuid(sid));
                if (s == null) continue;
                Vector3 p = s.Position;
                bool inWater = HbInWater(s, sea);
                bool onDeck = s.LotCurrent != null && s.LotCurrent.LotId == lotId;
                bool atMove;
                if (sHbAtMove == null || !sHbAtMove.TryGetValue(sid, out atMove)) atMove = false;
                string intent;
                if (!sHbIntent.TryGetValue(sid, out intent)) intent = WcIntent(s);
                bool wet = inWater || sHbWet.ContainsKey(sid);
                bool stranded = atMove && wet && intent == null;
                if (atMove) watched++;
                if (atMove && !wet && sHbMode == "deck" && !onDeck) left++;
                if (stranded) { bad++; badNames.Add(s.FullName + " (" + (s.LotCurrent != null ? s.LotCurrent.Name : "no lot") + ", y " + Math.Round(p.y - sea, 2) + ")"); }
                Dictionary<string, object> o = Util.Obj();
                o["name"] = s.FullName;
                o["on_support_at_move"] = atMove;
                o["on_deck"] = onDeck;
                o["in_water"] = inWater;
                o["wet_since_move"] = wet;
                o["water_intent"] = intent;
                o["x"] = Math.Round(p.x, 1); o["z"] = Math.Round(p.z, 1);
                o["y_above_sea_m"] = Math.Round(p.y - sea, 2);
                o["lot"] = s.LotCurrent != null ? s.LotCurrent.Name : null;
                o["interaction"] = s.CurrentInteraction != null ? s.CurrentInteraction.GetType().Name : null;
                o["ok"] = !stranded;
                rows.Add(o);
            }
            r["mode"] = sHbMode;
            r["sims"] = rows;
            r["watched"] = watched;
            r["left_deck_dry"] = left;  // walked off on their own (other lot, Go Home): not stranding
            r["boat_moved_max_m"] = Math.Round(sHbMaxMoved, 1);
            r["boat_status"] = boat.CurrentStatus.ToString();
            string why = bad > 0 ? bad + " Sim(s) in water from the " + sHbMode + " without a water intent: " + string.Join("; ", badNames.ToArray())
                : sHbMaxMoved < 20f ? "not exercised (boat moved " + Math.Round(sHbMaxMoved, 1) + " m < 20 m)"
                : watched == 0 ? "not exercised (no Sim on the " + sHbMode + " and dry at move start)" : null;
            r["check_ok"] = why == null;
            if (why != null) r["why"] = why;
            if (a.Bool("require", false) && why != null) throw new McpException("houseboat_move check: " + why);
            return r;
        }

        // a houseboat with a houseboat lot; prefer one whose lot has a household, then (dock) one docked at a port gangway
        static Houseboat HbPick(bool needPort)
        {
            Houseboat best = null;
            int bestScore = -1;
            foreach (Houseboat b in Sims3.Gameplay.Queries.GetObjects<Houseboat>())
            {
                if (b == null || b.GetHouseboatLotId() == 0) continue;
                Gangway gw = b.GetPortGangway();
                if (needPort && gw == null) continue;
                Lot l = LotManager.GetLot(b.GetHouseboatLotId());
                int score = (l != null && l.Household != null ? 2 : 0) + (b.IsDocked ? 1 : 0);
                if (score > bestScore) { bestScore = score; best = b; }
            }
            return best;
        }
    }
}

using System;
using System.Collections;
using System.Collections.Generic;
using System.Globalization;
using Sims3.Gameplay;
using Sims3.Gameplay.Abstracts;
using Sims3.Gameplay.CAS;
using Sims3.Gameplay.Core;
using Sims3.SimIFace;
using Sims3.SimIFace.BuildBuy;
using Sims3.UI;

namespace Ts3Agent
{
    // Build mode through the game's own tools: the native wall/floor/object tools are driven with the same
    // calls the build UI makes (UserToolGeneric + forwarded mouse events at window pixels). World points are
    // turned into pixels with a ground-plane homography fitted from PickFloorAtWindowPoint samples.
    public static partial class Handlers
    {
        static void RegisterBuild()
        {
            Dispatcher.Register("build_mode", BuildMode);
            Dispatcher.Register("pick", Pick);
            Dispatcher.Register("calibrate", Calibrate);
            Dispatcher.Register("tile_info", TileInfo);
            Dispatcher.Register("tool_select", ToolSelect);
            Dispatcher.Register("tool_drag", ToolDrag);
            Dispatcher.Register("build_presets", BuildPresets);
            Dispatcher.Register("auto_roof", AutoRoof);
        }

        static Lot HomeLot()
        {
            Household hh = Household.ActiveHousehold;
            if (hh != null && hh.LotHome != null) return hh.LotHome;
            return Util.ActiveSim().LotCurrent;
        }

        static object BuildMode(Args a)
        {
            bool on = a.Bool("on", true);
            Lot lot = HomeLot();
            RunAsync(on ? "enter build mode" : "enter live mode", delegate
            {
                if (on) GameStates.TransitionToBuildMode(lot, null);
                else GameStates.TransitionToLiveMode();
            });
            sHomography = null;
            Dictionary<string, object> r = Util.Obj();
            r["requested"] = on ? "build" : "live";
            return r;
        }

        static object Pick(Args a)
        {
            Vector3 hit = Vector3.Zero;
            Dictionary<string, object> r = Util.Obj();
            r["hit"] = World.PickFloorAtWindowPoint((int)a.Long("x"), (int)a.Long("y"), ref hit);
            r["world"] = Pos(hit);
            return r;
        }

        // ------------------------------------------------------------ world <-> window pixels

        // h maps world (x,z,1) -> window (u,v,w); pixel = (u/w, v/w).
        static double[] sHomography;
        static Vector3 sCalibCamPos, sCalibCamTarget;

        static bool CameraMoved()
        {
            Vector3 p = CameraController.GetPosition(), t = CameraController.GetTarget();
            return (p - sCalibCamPos).Length() > 0.01f || (t - sCalibCamTarget).Length() > 0.01f;
        }

        static object Calibrate(Args a)
        {
            Rect area = UIManager.GetMainWindow().Area;
            int w = (int)area.Width, h = (int)area.Height;
            List<double[]> pts = new List<double[]>(); // wx, wz, px, py
            for (int i = 1; i < 10; i++)
                for (int j = 1; j < 8; j++)
                {
                    int px = w * i / 10, py = h * j / 8;
                    Vector3 hit = Vector3.Zero;
                    if (World.PickFloorAtWindowPoint(px, py, ref hit)) pts.Add(new double[] { hit.x, hit.z, px, py });
                }
            if (pts.Count < 6) throw new McpException("only " + pts.Count + " floor picks hit; point the camera at the lot");
            sHomography = FitHomography(pts);
            sCalibCamPos = CameraController.GetPosition();
            sCalibCamTarget = CameraController.GetTarget();
            double err = 0;
            foreach (double[] p in pts)
            {
                double[] s = Project(p[0], p[1]);
                err = Math.Max(err, Math.Abs(s[0] - p[2]) + Math.Abs(s[1] - p[3]));
            }
            Dictionary<string, object> r = Util.Obj();
            r["samples"] = pts.Count;
            r["max_error_px"] = Math.Round(err, 2);
            r["window"] = w + "x" + h;
            return r;
        }

        static double[] Project(double x, double z)
        {
            double[] H = sHomography;
            double u = H[0] * x + H[1] * z + H[2], v = H[3] * x + H[4] * z + H[5], q = H[6] * x + H[7] * z + 1.0;
            return new double[] { u / q, v / q };
        }

        static double[] ToPixels(double x, double z)
        {
            if (sHomography == null || CameraMoved()) Calibrate(null);
            return Project(x, z);
        }

        // Least-squares DLT with h8 = 1: 2 equations per point, normal equations, Gaussian elimination.
        static double[] FitHomography(List<double[]> pts)
        {
            double[,] A = new double[8, 8];
            double[] b = new double[8];
            foreach (double[] p in pts)
            {
                double x = p[0], z = p[1], u = p[2], v = p[3];
                double[] r1 = { x, z, 1, 0, 0, 0, -u * x, -u * z };
                double[] r2 = { 0, 0, 0, x, z, 1, -v * x, -v * z };
                for (int i = 0; i < 8; i++)
                {
                    for (int j = 0; j < 8; j++) A[i, j] += r1[i] * r1[j] + r2[i] * r2[j];
                    b[i] += r1[i] * u + r2[i] * v;
                }
            }
            for (int c = 0; c < 8; c++)
            {
                int piv = c;
                for (int r = c + 1; r < 8; r++) if (Math.Abs(A[r, c]) > Math.Abs(A[piv, c])) piv = r;
                for (int k = 0; k < 8; k++) { double t = A[c, k]; A[c, k] = A[piv, k]; A[piv, k] = t; }
                double tb = b[c]; b[c] = b[piv]; b[piv] = tb;
                for (int r = 0; r < 8; r++)
                {
                    if (r == c) continue;
                    double f = A[r, c] / A[c, c];
                    for (int k = c; k < 8; k++) A[r, k] -= f * A[c, k];
                    b[r] -= f * b[c];
                }
            }
            double[] h = new double[8];
            for (int i = 0; i < 8; i++) h[i] = b[i] / A[i, i];
            return h;
        }

        // ------------------------------------------------------------ tiles

        static object TileInfo(Args a)
        {
            Lot lot = HomeLot();
            Dictionary<string, object> r = Util.Obj();
            if (a.Has("lx"))
            {
                LotLocation loc = new LotLocation((int)a.Long("lx"), (int)a.Long("lz"), (sbyte)a.Long("level", 0), 0);
                Vector3 wp = Vector3.Zero;
                World.GetWorldPosition(lot.LotId, loc, ref wp);
                r["world"] = Pos(wp);
                r["room"] = World.GetRoomId(lot.LotId, loc);
                r["solid_floor"] = World.HasSolidFloor(lot.LotId, loc);
                if (a.Has("lx2"))
                {
                    LotLocation b = new LotLocation((int)a.Long("lx2"), (int)a.Long("lz2"), loc.mLevel, 0);
                    r["wall_between"] = World.WallExistsBetween(lot.LotId, loc, b);
                }
            }
            else
            {
                Vector3 p = new Vector3((float)a.Double("x"), (float)a.Double("y", 0), (float)a.Double("z"));
                if (!a.Has("y")) p.y = World.GetTerrainHeight(p.x, p.z);
                LotLocation loc = new LotLocation();
                r["lot_id"] = Util.Id(World.GetLotLocation(p, ref loc));
                r["lx"] = loc.mX;
                r["lz"] = loc.mZ;
                r["level"] = loc.mLevel;
                r["room"] = World.GetRoomId(p);
            }
            return r;
        }

        // ------------------------------------------------------------ tools

        static ResourceKey ParseKey(string s)
        {
            if (string.IsNullOrEmpty(s)) return new ResourceKey(0uL, 0u, 0u);
            string[] k = s.Split(':');
            return new ResourceKey(ulong.Parse(k[2], NumberStyles.HexNumber), uint.Parse(k[0], NumberStyles.HexNumber),
                                   uint.Parse(k[1], NumberStyles.HexNumber));
        }

        // tool: 0 wall, 1 wallpaper, 2 floor, 3 foundation, 4 object, ... (Sims3.UI.ToolMessageID)
        static object ToolSelect(Args a)
        {
            uint tool = (uint)a.Long("tool");
            ResourceKey key = ParseKey(a.Has("product") ? a.Str("product") : null);
            if (a.Has("preset")) UserToolUtils.UserToolGeneric(tool, key, (uint)a.Long("preset"));
            else UserToolUtils.UserToolGeneric(tool, key);
            UIManager.GetSceneWindow().StartForwardingEventsToUserTools();
            Dictionary<string, object> r = Util.Obj();
            r["tool"] = tool;
            r["product"] = KeyString(key);
            return r;
        }

        // Press at the first point, move through the rest, release at the last (world x,z pairs, or pixels with
        // "pixels": true). One drag draws a wall run, paints a floor rectangle, or clicks (single point).
        static object ToolDrag(Args a)
        {
            List<object> raw = a.List("points");
            if (raw == null || raw.Count == 0) throw new McpException("points: [[x,z], ...] required");
            bool pixels = a.Bool("pixels", false);
            int frames = (int)a.Long("frames", 3);
            List<double[]> px = new List<double[]>();
            foreach (object o in raw)
            {
                IList p = (IList)o;
                double x = Convert.ToDouble(p[0], CultureInfo.InvariantCulture), z = Convert.ToDouble(p[1], CultureInfo.InvariantCulture);
                px.Add(pixels ? new double[] { x, z } : ToPixels(x, z));
            }
            uint button = (uint)a.Long("button", 1000);
            uint mods = (uint)a.Long("modifiers", 0);
            RunAsync("tool drag " + px.Count + " pts", delegate
            {
                UIManager.GetSceneWindow().StartForwardingEventsToUserTools();
                Move(px[0], mods, frames);
                UserToolUtils.ForwardUserToolMouseEvent(UserToolUtils.UserToolMouseEventType.kUserToolMouseDown,
                                                        (float)px[0][0], (float)px[0][1], button, mods);
                Simulator.Sleep((uint)frames);
                for (int i = 1; i < px.Count; i++)
                {
                    // Interpolate so the tool sees a continuous drag.
                    for (int s = 1; s <= 8; s++)
                    {
                        double t = s / 8.0;
                        Move(new double[] { px[i - 1][0] + (px[i][0] - px[i - 1][0]) * t, px[i - 1][1] + (px[i][1] - px[i - 1][1]) * t },
                             mods, 1);
                    }
                }
                double[] last = px[px.Count - 1];
                Simulator.Sleep((uint)frames);
                UserToolUtils.ForwardUserToolMouseEvent(UserToolUtils.UserToolMouseEventType.kUserToolMouseUp,
                                                        (float)last[0], (float)last[1], button, mods);
                Simulator.Sleep((uint)frames);
            });
            List<object> outPx = new List<object>();
            foreach (double[] p in px) outPx.Add(new double[] { Math.Round(p[0], 1), Math.Round(p[1], 1) });
            Dictionary<string, object> r = Util.Obj();
            r["pixels"] = outPx;
            return r;
        }

        static void Move(double[] p, uint mods, int frames)
        {
            UserToolUtils.ForwardUserToolMouseEvent(UserToolUtils.UserToolMouseEventType.kUserToolMouseMove,
                                                    (float)p[0], (float)p[1], 0u, mods);
            Simulator.Sleep((uint)Math.Max(1, frames));
        }

        static object BuildPresets(Args a)
        {
            string kind = a.Str("kind", "floor");
            uint cat = (uint)a.Long("category", 0xFFFFFFFF);
            List<object> src = kind == "wall" ? UserToolUtils.GetWallPatternPresetListByType(cat)
                                              : UserToolUtils.GetFloorPatternPresetListByType(cat);
            string q = a.Has("query") ? a.Str("query").ToLowerInvariant() : null;
            List<object> list = new List<object>();
            if (src != null)
                foreach (object o in src)
                {
                    BuildBuyPreset p = o as BuildBuyPreset;
                    if (p == null || p.Product == null) continue;
                    string name = p.Product.CatalogName ?? "";
                    if (q != null && name.ToLowerInvariant().IndexOf(q) < 0) continue;
                    Dictionary<string, object> d = Util.Obj();
                    d["name"] = name;
                    d["product"] = KeyString(p.Product.ProductResourceKey);
                    d["preset"] = p.ID;
                    d["price"] = (int)p.Product.Price;
                    list.Add(d);
                    if (list.Count >= (int)a.Long("limit", 40)) break;
                }
            return list;
        }

        static object AutoRoof(Args a)
        {
            Lot lot = HomeLot();
            World.LotDoAutoRoof(lot.LotId);
            Dictionary<string, object> r = Util.Obj();
            r["roofed"] = lot.Name;
            return r;
        }
    }
}

using System;
using System.Collections.Generic;
using Sims3.Gameplay;
using Sims3.Gameplay.Core;
using Sims3.SimIFace;

namespace Ts3Agent
{
    // camera_line (docs/367): scripted world-view pan with no mouse. Only the camera moves (SetPositionAndTarget, as camera_sweep).
    // camera_line {x0,z0,x1,z1,speed?,height?,pitchDeg?,seconds?} | {stop:true} | {status:true}
    // The camera target ping-pongs on the line (x0,z0)-(x1,z1) at `speed` m/s (default 200) by wall clock, camera `height` m above the
    // target (default 600) at `pitchDeg` (default 60) above the horizon, for `seconds` (default 60) after 5 s still and before 5 s still.
    // Segments (still_start / pan / still_end) come back in the camera_sweep status shape, so camera-sweep-report.ts reads them.
    // Per set it records dt (ms since the last set) and the step in metres, and the gap between the position set and read back.
    public static partial class Handlers
    {
        static volatile bool sLineRunning;
        static List<object> sLineSegs = new List<object>();
        static Dictionary<string, object> sLineStats = new Dictionary<string, object>();

        static float Pct(List<float> v, double p) { if (v.Count == 0) return 0; v.Sort(); return v[Math.Min(v.Count - 1, (int)Math.Ceiling(p * v.Count) - 1 < 0 ? 0 : (int)Math.Ceiling(p * v.Count) - 1)]; }

        static object CameraLine(Args a)
        {
            RequireLive();
            Dictionary<string, object> r = Util.Obj();
            if (a.Has("stop") || a.Has("status"))
            {
                if (a.Has("stop")) { ++sPanGen; r["stopped"] = true; }
                lock (sLineSegs) { r["segments"] = new List<object>(sLineSegs); r["stats"] = new Dictionary<string, object>(sLineStats); }
                r["running"] = sLineRunning;
                return r;
            }
            float x0 = (float)a.Double("x0", 0), z0 = (float)a.Double("z0", 0), x1 = (float)a.Double("x1", 4096), z1 = (float)a.Double("z1", 0);
            float speed = (float)a.Double("speed", 200), h = (float)a.Double("height", 600), pitch = (float)(a.Double("pitchDeg", 60) * Math.PI / 180);
            int panMs = (int)(a.Double("seconds", 60) * 1000);
            // keepOffset: keep the camera-to-target offset the game has now (map view's own zoom-out height), move only the target
            bool keep = a.Bool("keepOffset", false);
            Vector3 off0 = CameraController.GetPosition() - CameraController.GetTarget();
            float len = (float)Math.Sqrt((x1 - x0) * (x1 - x0) + (z1 - z0) * (z1 - z0));
            if (!(len >= 10 && speed > 0 && h >= 2 && pitch > 0.05f && pitch < 1.5f && panMs >= 1000)) throw new McpException("line >= 10 m, speed > 0, height >= 2, pitchDeg 3..85, seconds >= 1");
            int gen = ++sPanGen;
            try { CameraController.DisableObjectFollow(); CameraController.Untether(); } catch (Exception) { }
            lock (sLineSegs) { sLineSegs.Clear(); sLineStats.Clear(); }
            long epoch0 = EpochMs();
            int t0 = System.Environment.TickCount;
            sLineRunning = true;
            string[] names = { "still_start", "pan", "still_end" };
            int[] ms = { 5000, panMs, 5000 };
            Simulator.AddObject(new OneShotFunctionTask(delegate
            {
                List<float> dts = new List<float>(), steps = new List<float>(), errs = new List<float>();
                try
                {
                    int cur = -1, sets = 0, last = t0;
                    Dictionary<string, object> open = null;
                    float dist = 0, px = float.NaN, pz = 0;
                    while (sPanGen == gen)
                    {
                        int el = System.Environment.TickCount - t0, s = 0, left = el;
                        while (s < 3 && left >= ms[s]) { left -= ms[s]; s++; }
                        if (s != cur)
                        {
                            if (open != null) CloseSeg(open, epoch0, t0, el, sets);
                            if (s >= 3) break;
                            open = Util.Obj(); open["name"] = names[s]; open["epochMs0"] = epoch0 + el; open["tickMs0"] = (long)(uint)(t0 + el);
                            lock (sLineSegs) sLineSegs.Add(open);
                            cur = s; sets = 0;
                        }
                        int now = System.Environment.TickCount;
                        if (s == 1) dist += speed * (now - last) / 1000f;
                        float m = dist % (2 * len), u = (m <= len ? m : 2 * len - m) / len;
                        float x = x0 + (x1 - x0) * u, z = z0 + (z1 - z0) * u;
                        float dx = (x1 - x0) / len * (m <= len ? 1 : -1), dz = (z1 - z0) / len * (m <= len ? 1 : -1);
                        Vector3 tgt = new Vector3(x, World.GetTerrainHeight(x, z), z);
                        float back = h / (float)Math.Tan(pitch);
                        Vector3 pos = keep ? new Vector3(x + off0.x, tgt.y + off0.y, z + off0.z) : new Vector3(x - dx * back, tgt.y + h, z - dz * back);
                        CameraController.SetPositionAndTarget(pos, tgt);
                        if (s == 1)
                        {
                            dts.Add(now - last);
                            if (!float.IsNaN(px)) steps.Add((float)Math.Sqrt((x - px) * (x - px) + (z - pz) * (z - pz)));
                            Vector3 g = CameraController.GetPosition();
                            errs.Add((g - pos).Length());
                        }
                        px = x; pz = z; last = now; sets++;
                        Simulator.Sleep(0);
                    }
                    if (open != null && !open.ContainsKey("epochMs1")) { CloseSeg(open, epoch0, t0, System.Environment.TickCount - t0, sets); open["cut"] = true; }
                }
                finally
                {
                    lock (sLineSegs)
                    {
                        sLineStats["sets"] = dts.Count;
                        sLineStats["dtMsP50"] = Pct(dts, 0.5); sLineStats["dtMsP99"] = Pct(dts, 0.99); sLineStats["dtMsMax"] = Pct(dts, 1);
                        sLineStats["stepMP50"] = Math.Round(Pct(steps, 0.5), 3); sLineStats["stepMP99"] = Math.Round(Pct(steps, 0.99), 3); sLineStats["stepMMax"] = Math.Round(Pct(steps, 1), 3);
                        sLineStats["setReadGapMMax"] = Math.Round(Pct(errs, 1), 2); sLineStats["setReadGapMP50"] = Math.Round(Pct(errs, 0.5), 2);
                    }
                    sLineRunning = false;
                }
            }));
            r["epochMs0"] = epoch0; r["tickMs0"] = (long)(uint)t0; r["lineM"] = Math.Round(len); r["speed"] = speed; r["offset"] = Pos(off0); r["mapView"] = CameraController.IsMapViewModeEnabled(); r["totalMs"] = ms[0] + ms[1] + ms[2];
            return r;
        }
    }
}

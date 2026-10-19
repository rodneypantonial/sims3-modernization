// Usage: node tools/selftest/screenfit/run.ts  (compiles this with mods/ts3agent/src/plugin/ScreenFit.cs)
// Offline self-check of tool_drag's projection math: a synthetic pinhole camera aimed as build_level aims it (15 m up,
// 12 m back) over a lot whose level 1 (y = 3) is pickable only inside the lot and the terrain (y = 0) outside, as
// bp-snap2-build 2026-10-18 saw on level 1. The plane-filtered fit must project level-1 tiles to within 0.5 px, and
// Degenerate must flag collapsed or NaN drags. Exit code 1 on failure.
using System;
using System.Collections.Generic;
using Ts3Agent;

static class ScreenFitTest
{
    static int sFails;
    static void Check(bool ok, string what) { if (!ok) { sFails++; Console.WriteLine("FAIL: " + what); } }

    const double W = 1920, Hh = 1080, F = 1100;
    static double[] sEye, sRight, sUp, sFwd;

    static double[] Sub(double[] a, double[] b) { return new double[] { a[0] - b[0], a[1] - b[1], a[2] - b[2] }; }
    static double Dot(double[] a, double[] b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
    static double[] Cross(double[] a, double[] b)
    {
        return new double[] { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };
    }
    static double[] Norm(double[] a) { double l = Math.Sqrt(Dot(a, a)); return new double[] { a[0] / l, a[1] / l, a[2] / l }; }

    static void Aim(double[] target)
    {
        sEye = new double[] { target[0], target[1] + 15, target[2] - 12 };
        sFwd = Norm(Sub(target, sEye));
        sRight = Norm(Cross(new double[] { 0, 1, 0 }, sFwd));
        sUp = Cross(sFwd, sRight);
    }

    static double[] Shoot(double x, double y, double z)
    {
        double[] d = Sub(new double[] { x, y, z }, sEye);
        double c = Dot(d, sFwd);
        return new double[] { W / 2 + F * Dot(d, sRight) / c, Hh / 2 - F * Dot(d, sUp) / c };
    }

    // Floor pick of the synthetic scene: level 1 inside the 20 x 20 m lot, else terrain.
    static double[] Pick(double px, double py)
    {
        double a = (px - W / 2) / F, b = -(py - Hh / 2) / F;
        double[] ray = { sFwd[0] + a * sRight[0] + b * sUp[0], sFwd[1] + a * sRight[1] + b * sUp[1], sFwd[2] + a * sRight[2] + b * sUp[2] };
        if (ray[1] >= 0) return null;
        foreach (double y in new double[] { 3, 0 })
        {
            double t = (y - sEye[1]) / ray[1];
            double x = sEye[0] + t * ray[0], z = sEye[2] + t * ray[2];
            if (y == 0 || (x >= 0 && x <= 20 && z >= 0 && z <= 20)) return new double[] { x, y, z, px, py };
        }
        return null;
    }

    static List<double[]> Picks()
    {
        List<double[]> r = new List<double[]>();
        for (int i = 1; i < 20; i++)
            for (int j = 1; j < 16; j++)
            {
                double[] p = Pick(Math.Floor(W * i / 20), Math.Floor(Hh * j / 16));
                if (p != null) r.Add(p);
            }
        return r;
    }

    static void LevelOne()
    {
        Aim(new double[] { 3, 3, 3 });
        List<double[]> picks = Picks();
        List<double[]> on = ScreenFit.OnPlane(picks, 3, 1.25);
        Check(on.Count >= 6 && on.Count < picks.Count, "level 1: plane filter keeps the lot picks only (" + on.Count + "/" + picks.Count + ")");
        double[] H = ScreenFit.FitHomography(on);
        Check(ScreenFit.MaxError(H, on) < 0.5, "level 1: fit error " + ScreenFit.MaxError(H, on));
        double worst = 0;
        for (int x = 1; x <= 5; x++)
            for (int z = 1; z <= 5; z++)
            {
                double[] s = Shoot(x, 3, z), p = ScreenFit.Project(H, x, z);
                worst = Math.Max(worst, Math.Abs(s[0] - p[0]) + Math.Abs(s[1] - p[1]));
            }
        Check(worst < 0.5, "level 1: tiles 1..5 project within 0.5 px, worst " + worst);
        List<double[]> world = new List<double[]> { new double[] { 1, 1 }, new double[] { 5, 1 } };
        List<double[]> px = new List<double[]> { ScreenFit.Project(H, 1, 1), ScreenFit.Project(H, 5, 1) };
        Check(ScreenFit.Degenerate(world, px, 8) == null, "level 1: wall drag 1,1 -> 5,1 is not degenerate");

        // The old fit over every pick (both planes) for comparison: printed, not asserted.
        double[] Hall = ScreenFit.FitHomography(ScreenFit.OnPlane(picks, double.NaN, 0));
        double[] a = ScreenFit.Project(Hall, 1, 1), b = ScreenFit.Project(Hall, 5, 1), s1 = Shoot(1, 3, 1);
        Console.WriteLine("info: mixed-plane fit: 1,1 -> " + Fmt(a) + " 5,1 -> " + Fmt(b) + " (true 1,1 " + Fmt(s1) + ")");
    }

    static void GroundUnchanged()
    {
        Aim(new double[] { 3, 0, 3 });
        List<double[]> picks = new List<double[]>();
        foreach (double[] k in Picks()) if (k[1] == 0) picks.Add(k);
        List<double[]> all = ScreenFit.OnPlane(picks, double.NaN, 0);
        Check(all.Count == picks.Count, "NaN plane keeps every pick");
        double[] H = ScreenFit.FitHomography(all);
        double[] s = Shoot(4, 0, 2), p = ScreenFit.Project(H, 4, 2);
        Check(Math.Abs(s[0] - p[0]) + Math.Abs(s[1] - p[1]) < 0.5, "ground: tile 4,2 within 0.5 px");
    }

    static void DegenerateCases()
    {
        List<double[]> world = new List<double[]> { new double[] { 1, 2 }, new double[] { 2, 3 } };
        // bp-snap2-build level 1 floor drag: 1.4 m apart, 1.3 px apart
        Check(ScreenFit.Degenerate(world, new List<double[]> { new double[] { 233.5, 615.9 }, new double[] { 233, 614.7 } }, 8) != null,
              "collapsed drag flagged");
        Check(ScreenFit.Degenerate(world, new List<double[]> { new double[] { 100, 100 }, new double[] { 140, 120 } }, 8) == null,
              "spread drag passes");
        Check(ScreenFit.Degenerate(world, new List<double[]> { new double[] { double.NaN, 1 }, new double[] { 140, 120 } }, 8) != null,
              "NaN pixel flagged");
        List<double[]> same = new List<double[]> { new double[] { 1, 2 }, new double[] { 1.2, 2 } };
        Check(ScreenFit.Degenerate(same, new List<double[]> { new double[] { 100, 100 }, new double[] { 101, 100 } }, 8) == null,
              "points under 0.5 m apart may share a pixel");
        Check(ScreenFit.Degenerate(new List<double[]> { new double[] { 3, 2 } }, new List<double[]> { new double[] { 232.5, 605.4 } }, 8) == null,
              "single click passes");
    }

    static string Fmt(double[] p) { return "(" + Math.Round(p[0], 1) + "," + Math.Round(p[1], 1) + ")"; }

    static int Main()
    {
        LevelOne();
        GroundUnchanged();
        DegenerateCases();
        Console.WriteLine(sFails == 0 ? "screenfit selftest: all passed" : "screenfit selftest: " + sFails + " failed");
        return sFails == 0 ? 0 : 1;
    }
}

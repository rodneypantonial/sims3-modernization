using System;
using System.Collections.Generic;

namespace Ts3Agent
{
    // Game-independent math behind tool_drag's world -> window pixel projection (tools/selftest/screenfit checks it).
    // A homography maps one world plane (x, z) to window pixels, so it is fitted only from floor picks on the plane
    // the drag is on: on an upper level, picks outside the lot hit the terrain a level lower, and a fit over both
    // planes collapses (bp-snap2-build 2026-10-18: level-1 tiles 4 m apart landed 1 to 20 px apart).
    public static class ScreenFit
    {
        // picks: {wx, wy, wz, px, py}. Keeps those within tol metres of height y (NaN y keeps all) as {wx, wz, px, py}.
        public static List<double[]> OnPlane(List<double[]> picks, double y, double tol)
        {
            List<double[]> r = new List<double[]>();
            foreach (double[] p in picks)
                if (double.IsNaN(y) || Math.Abs(p[1] - y) <= tol) r.Add(new double[] { p[0], p[2], p[3], p[4] });
            return r;
        }

        // h maps world (x,z,1) -> window (u,v,w); pixel = (u/w, v/w).
        public static double[] Project(double[] H, double x, double z)
        {
            double u = H[0] * x + H[1] * z + H[2], v = H[3] * x + H[4] * z + H[5], q = H[6] * x + H[7] * z + 1.0;
            return new double[] { u / q, v / q };
        }

        // Largest |du| + |dv| over the fit points {wx, wz, px, py}.
        public static double MaxError(double[] H, List<double[]> pts)
        {
            double err = 0;
            foreach (double[] p in pts)
            {
                double[] s = Project(H, p[0], p[1]);
                err = Math.Max(err, Math.Abs(s[0] - p[2]) + Math.Abs(s[1] - p[3]));
            }
            return err;
        }

        // Least-squares DLT with h8 = 1: 2 equations per point, normal equations, Gaussian elimination.
        public static double[] FitHomography(List<double[]> pts)
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

        // Null when the drag is usable, else why not: two consecutive points at least half a metre apart in the world
        // that project closer than minPx (or to NaN) mean a broken fit, and the tool would only see a click.
        public static string Degenerate(List<double[]> world, List<double[]> px, double minPx)
        {
            for (int i = 0; i < px.Count; i++)
            {
                if (double.IsNaN(px[i][0]) || double.IsNaN(px[i][1]) || double.IsInfinity(px[i][0]) || double.IsInfinity(px[i][1]))
                    return "point " + i + " projects to no pixel";
                if (i == 0) continue;
                double dw = Math.Sqrt(Sq(world[i][0] - world[i - 1][0]) + Sq(world[i][1] - world[i - 1][1]));
                double dp = Math.Sqrt(Sq(px[i][0] - px[i - 1][0]) + Sq(px[i][1] - px[i - 1][1]));
                if (dw >= 0.5 && dp < minPx)
                    return "points " + (i - 1) + " and " + i + " are " + Math.Round(dw, 2) + " m apart but project "
                           + Math.Round(dp, 1) + " px apart (< " + minPx + ")";
            }
            return null;
        }

        static double Sq(double d) { return d * d; }
    }
}

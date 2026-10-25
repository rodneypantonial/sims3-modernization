using System;
using System.Collections.Generic;
using Ts3Fixes;

// Offline self-check of OverwatchNoticeCore (docs/145 §13): the window, per-Sim counting, the summary text, and the
// alarm flow of OverwatchNotice.cs (summary kWindowMinutes after the notice that opened the window).
static class OverwatchNoticeTest
{
    static int sFails;
    static void Check(bool ok, string what) { if (!ok) { sFails++; Console.WriteLine("FAIL " + what); } }

    static int Main()
    {
        int named;
        OverwatchNoticeCore w = new OverwatchNoticeCore();
        Check(!w.Pending && w.Take(out named) == null && named == 0, "nothing held: no summary");

        Check(w.Hold(1, "Ann Lee"), "first notice opens the window");
        Check(!w.Hold(2, "Bob Ray"), "second notice joins it");
        Check(!w.Hold(1, "Ann Lee"), "same Sim again joins it");
        Check(w.Sims == 2, "a Sim reset twice counts once (" + w.Sims + ")");
        string s = w.Take(out named);
        Check(s == "Overwatch reset 2 stuck Sims in the last 2 hours: Ann Lee, Bob Ray", "two names: " + s);
        Check(named == 2, "two named");
        Check(!w.Pending && w.Take(out named) == null, "window empty after the summary");
        Check(w.Hold(3, "Cy"), "next notice opens a new window");
        w.Clear();
        Check(!w.Pending && w.Hold(4, null), "world quit clears unshown; next notice opens a window");
        Check(w.Take(out named) == "Overwatch reset 1 stuck Sim in the last 2 hours: a Sim", "one Sim, no name");

        List<string> many = new List<string>(new string[] { "A", "B", "C", "D", "E" });
        Check(OverwatchNoticeCore.Summary(many, OverwatchNoticeCore.kMaxNames, out named)
            == "Overwatch reset 5 stuck Sims in the last 2 hours: A, B, C (+2 more)" && named == 3, "names capped with (+M more)");
        Check(OverwatchNoticeCore.Summary(many.GetRange(0, 3), 3, out named) == "Overwatch reset 3 stuck Sims in the last 2 hours: A, B, C" && named == 3, "exactly the cap: no more part");

        // the hook's flow: notices at these sim-minutes; an alarm at open + window shows the summary
        float[] at = { 0, 5, 30, 119, 125, 126, 300, 900, 901 };
        List<float> alarms = new List<float>(), shown = new List<float>();
        List<int> counts = new List<int>();
        w.Clear();
        ulong id = 10;
        int held = 0, next = 0;
        for (float t = 0; t <= 1200; t += 1)
        {
            while (next < at.Length && at[next] == t) { held++; if (w.Hold(id++, "S" + next)) alarms.Add(t + OverwatchNoticeCore.kWindowMinutes); next++; }
            if (alarms.Contains(t)) { int n = w.Sims; string text = w.Take(out named); if (text != null) { shown.Add(t); counts.Add(n); } }
        }
        Check(shown.Count == 4, "four summaries (" + shown.Count + ")");
        for (int i = 1; i < shown.Count; i++) Check(shown[i] - shown[i - 1] >= OverwatchNoticeCore.kWindowMinutes, "summaries 2 sim-hours apart");
        int sum = 0; foreach (int c in counts) sum += c;
        Check(sum == held, "every held notice is in one summary (" + sum + " of " + held + ")");
        Check(counts.Count == 4 && counts[0] == 4 && counts[1] == 2 && counts[2] == 1 && counts[3] == 2, "windows 0-120, 125-245, 300-420, 900-1020");
        Check(!w.Pending, "nothing left pending");

        Console.WriteLine(sFails == 0 ? "overwatchnotice selftest: PASS" : "overwatchnotice selftest: " + sFails + " FAIL");
        return sFails == 0 ? 0 : 1;
    }
}

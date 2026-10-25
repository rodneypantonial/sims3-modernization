using System.Collections.Generic;
using System.Text;

namespace Ts3Fixes
{
    // Game-independent part of OverwatchNotice.cs (docs/145 §13), kept apart for the offline self-check
    // (tools/selftest/overwatchnotice): the 2 sim-hour window and the summary text.
    public sealed class OverwatchNoticeCore
    {
        public const float kWindowMinutes = 120f; // one summary at most per this many sim-minutes
        public const int kMaxNames = 3;           // names in the summary, the rest as "(+M more)"

        readonly List<ulong> mIds = new List<ulong>();
        readonly List<string> mNames = new List<string>();
        bool mOpen;

        public int Sims { get { return mIds.Count; } }
        public bool Pending { get { return mIds.Count > 0; } }

        // A held notice for Sim id. True when it opened the window: the caller then shows the summary kWindowMinutes
        // later. A Sim reset twice in one window is counted and named once.
        public bool Hold(ulong id, string name)
        {
            bool opened = !mOpen;
            mOpen = true;
            if (!mIds.Contains(id)) { mIds.Add(id); mNames.Add(string.IsNullOrEmpty(name) ? "a Sim" : name); }
            return opened;
        }

        // The summary for the window (null with nothing held), and a fresh window. named = names written into it.
        public string Take(out int named)
        {
            named = 0;
            string text = mIds.Count == 0 ? null : Summary(mNames, kMaxNames, out named);
            Clear();
            return text;
        }

        public void Clear() { mIds.Clear(); mNames.Clear(); mOpen = false; }

        public static string Summary(IList<string> names, int maxNames, out int named)
        {
            int n = names.Count;
            named = n < maxNames ? n : maxNames;
            StringBuilder sb = new StringBuilder("Overwatch reset ").Append(n).Append(n == 1 ? " stuck Sim" : " stuck Sims")
                .Append(" in the last 2 hours: ");
            for (int i = 0; i < named; i++) sb.Append(i == 0 ? "" : ", ").Append(names[i]);
            if (n > named) sb.Append(" (+").Append(n - named).Append(" more)");
            return sb.ToString();
        }
    }
}

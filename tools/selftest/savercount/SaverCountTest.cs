// Usage: node tools/selftest/savercount/run.ts  (compiles this with mods/ts3agent/src/plugin/SaverCount.cs)
// Offline self-check of save_watch's count: the bp-hf-saver-real 2026-10-25 record sequence (our scratch save, then
// SaveGameInProgress flickers that leave the file) must count 0 Saver saves; real Saver saves (new file each cycle) count
// once each, per call as a delta from the previous call's cursor. Exit code 1 on failure.
using System;
using System.Collections.Generic;
using Ts3Agent;

static class SaverCountTest
{
    static int sFails;
    static void Check(bool ok, string what) { if (!ok) { sFails++; Console.WriteLine("FAIL: " + what); } }

    static List<Dictionary<string, object>> sSaves = new List<Dictionary<string, object>>();
    static int sCursor;

    static void Add(string via, string before, string after)
    {
        Dictionary<string, object> r = new Dictionary<string, object>();
        r["via"] = via;
        r["file_before"] = before;
        r["file"] = after;
        r["saver_made"] = SaverCount.IsSaverMade(via, before, after);
        sSaves.Add(r);
    }

    // what save_watch does per call
    static int Call() { int n = SaverCount.SaverMadeSince(sSaves, sCursor); sCursor = sSaves.Count; return n; }

    static int Main()
    {
        Check(!SaverCount.IsSaverMade("agent", "Baseline-BP-Spring.sims3", "SaverChk-R-0.sims3"), "our save_game is not Saver's");
        Check(!SaverCount.IsSaverMade("saver_like", "a.sims3", "SaverChk-R-1.sims3"), "saver_like_save is not Saver's");
        Check(!SaverCount.IsSaverMade("watch", "SaverChk-R-0.sims3", "SaverChk-R-0.sims3"), "flicker on the same file");
        Check(!SaverCount.IsSaverMade("watch", "x.sims3", null), "no file after");
        Check(SaverCount.IsSaverMade("watch", "SaverChk-R-0.sims3", "SaverChk-R-1.sims3"), "Saver cycle 1");

        Add("agent", "Baseline-BP-Spring.sims3", "SaverChk-R-0.sims3");
        Check(Call() == 0, "after the scratch save: 0");
        for (int i = 0; i < 8; i++)
        {
            Add("watch", "SaverChk-R-0.sims3", "SaverChk-R-0.sims3"); // the gate run's flickers
            Check(Call() == 0, "flicker firing " + i + ": 0");
        }
        Add("watch", "SaverChk-R-0.sims3", "SaverChk-R-1.sims3");
        Check(Call() == 1, "first Saver save: 1");
        Check(Call() == 0, "same records again: 0 (cursor kept)");
        Add("watch", "SaverChk-R-1.sims3", "SaverChk-R-2.sims3");
        Add("watch", "SaverChk-R-2.sims3", "SaverChk-R-3.sims3");
        Check(Call() == 2, "two Saver saves between calls: 2");
        sSaves.Clear(); // save_watch reset
        Check(SaverCount.SaverMadeSince(sSaves, sCursor) == 0, "cursor past a cleared list: 0");
        Add("watch", "SaverChk-R-4.sims3", "SaverChk-R-1.sims3");
        Check(SaverCount.SaverMadeSince(sSaves, 0) == 1, "wrap R-4 -> R-1 counts");

        Console.WriteLine(sFails == 0 ? "savercount selftest: PASS" : "savercount selftest: " + sFails + " FAIL");
        return sFails == 0 ? 0 : 1;
    }
}

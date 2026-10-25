using System;
using System.Collections.Generic;

namespace Ts3Agent
{
    // Game-independent part of save_watch's count (tools/selftest/savercount checks it). Records carry "via" ("watch" = a
    // save none of our commands started, "agent" = save_game, "saver_like" = saver_like_save), "file_before" and "file".
    // Saver-made = via watch and a new file: Saver's OnSave renames to base + cycle count before SaveGame, so a finished
    // Saver save ends on another file; a SaveGame that gives up at once leaves the file (Saver's OnCheckForSave puts the
    // old name back). bp-hf-saver-real 2026-10-25: 8 such 15-62 ms flickers on SaverChk-R-0 were counted as Saver saves.
    // shortcut: Saver with an empty base name saves over the same file and is not counted; no repro of ours does that.
    public static class SaverCount
    {
        public static bool IsSaverMade(string via, string fileBefore, string fileAfter)
        {
            return via == "watch" && !string.IsNullOrEmpty(fileAfter) && fileAfter != fileBefore;
        }

        // Saver-made saves in saves[from..] (from = how many records the previous save_watch call had seen).
        public static int SaverMadeSince(List<Dictionary<string, object>> saves, int from)
        {
            int n = 0;
            for (int i = Math.Max(0, from); i < saves.Count; i++)
            {
                object v;
                if (saves[i].TryGetValue("saver_made", out v) && v is bool && (bool)v) n++;
            }
            return n;
        }
    }
}

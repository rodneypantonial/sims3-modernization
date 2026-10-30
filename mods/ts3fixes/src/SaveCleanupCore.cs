using System;

namespace Ts3Fixes
{
    // Game-independent rules of the aggressive save cleanup (SaveCleanupSpread.cs, docs/14 §8). Self-check:
    // tools/selftest/savecleanup.
    public static class SaveCleanupCore
    {
        // Outfit categories a dead, non-instantiated human loses (values of Sims3.SimIFace.CAS.OutfitCategories): what
        // stays is Naked (base), Everyday (urnstone thumbnail, ghost fallback), Special and Supernatural (occult forms
        // a resurrected Sim can still need); the description's default outfit is kept in any category.
        public static readonly uint[] kDeadRemovableOutfits = {
            4u,      // Formalwear
            8u,      // Sleepwear
            16u,     // Swimwear
            32u,     // Athletic
            64u,     // Singed
            128u,    // MartialArts
            256u,    // Career
            512u,    // AuxCareer
            1024u,   // Makeover
            2048u,   // SkinnyDippingTowel
            32768u,  // Massage
            65536u,  // ChildImagination
            262144u, // Outerwear
        };

        public static bool DeadOutfitRemovable(uint category)
        {
            return Array.IndexOf(kDeadRemovableOutfits, category) >= 0;
        }

        // Sidecar file key of the played-household store (native played.cpp, §8.6): the save's file name without folders,
        // characters outside [A-Za-z0-9._-] as '_', at most 100; no save yet -> "unsaved_<world>" (memory only until saved).
        public static string PlayedKey(string saveId, string world)
        {
            string s = saveId == null ? "" : saveId.Trim();
            int slash = Math.Max(s.LastIndexOf('\\'), s.LastIndexOf('/'));
            if (slash >= 0) s = s.Substring(slash + 1);
            if (s.Length == 0) s = "unsaved_" + (string.IsNullOrEmpty(world) ? "world" : world);
            char[] k = s.ToCharArray();
            for (int i = 0; i < k.Length; i++)
            {
                char c = k[i];
                if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-')) k[i] = '_';
            }
            s = new string(k);
            if (s[0] == '.') s = "_" + s.Substring(1);
            return s.Length > 100 ? s.Substring(0, 100) : s;
        }

        // Quiet long enough for the aggressive relationship rule: days since the last talk, or since the state began when
        // the pair never talked (WhenLastTalked unset). A clock that went back counts as not quiet.
        public static bool QuietFor(bool neverTalked, double daysSinceTalk, double daysInState, double quietDays)
        {
            double days = neverTalked ? daysInState : daysSinceTalk;
            return quietDays > 0 && days >= quietDays;
        }
    }
}

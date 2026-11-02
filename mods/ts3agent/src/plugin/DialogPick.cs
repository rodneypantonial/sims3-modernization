using System.Collections.Generic;

namespace Ts3Agent
{
    // Game-independent button pick for dialog_expect accept_all (vh-move, docs/236; tools/selftest/dialogpick checks it).
    // accept_all accepts the move flow, but a save offer is always declined: "save" and "don't save" both score 1 in
    // AutoDialog, so the tie went to whichever came first (vanilla Round 8 clicked "don't save", EA mod Run 8 clicked
    // "save" and overwrote the Baseline save). Never accept the save.
    public static class DialogPick
    {
        public static bool IsDeclineSave(string words)
        {
            string w = (words ?? "").ToLowerInvariant().Replace('’', '\'').Trim();
            return w.StartsWith("don't save") || w.StartsWith("dont save") || w.StartsWith("do not save");
        }

        // index of the enabled "don't save" button, -1 when the dialog is no save offer
        public static int DeclineSave(IList<string> words, IList<bool> enabled)
        {
            for (int i = 0; i < words.Count; i++) if (enabled[i] && IsDeclineSave(words[i])) return i;
            return -1;
        }
    }
}

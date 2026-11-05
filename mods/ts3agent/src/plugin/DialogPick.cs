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

        // Named event rules (docs/232 Run 8b): matched on the dialog text, never on the buttons alone; the answer is the passive /
        // no-op button. AutoDialog checks them before its own rule table and logs the click as "rule". Unknown dialogs keep the
        // no-safe-button policy (nothing default-accepted).
        public static readonly string[][][] kEventRules = new[]
        {
            // school practice event (vh1-soak, EA mod Run 8b): "... Should <Sim> compete or just watch the competition?" compete / watch
            new[] { new[] { "compete or just watch" }, new[] { "watch" } },
        };

        // the preferred button words for a dialog whose lower-case text matches a named event rule, else null
        public static string[] EventRule(string text)
        {
            string t = (text ?? "").ToLowerInvariant();
            foreach (string[][] rule in kEventRules)
                foreach (string frag in rule[0])
                    if (t.IndexOf(frag) >= 0) return rule[1];
            return null;
        }

        // index of the enabled "don't save" button, -1 when the dialog is no save offer
        public static int DeclineSave(IList<string> words, IList<bool> enabled)
        {
            for (int i = 0; i < words.Count; i++) if (enabled[i] && IsDeclineSave(words[i])) return i;
            return -1;
        }
    }
}

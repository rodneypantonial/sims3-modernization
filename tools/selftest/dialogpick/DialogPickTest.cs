using System;
using System.Collections.Generic;
using Ts3Agent;

static class DialogPickTest
{
    static int fails;
    static void Eq(int want, int got, string what) { if (want != got) { fails++; Console.WriteLine("FAIL " + what + ": want " + want + ", got " + got); } }
    static int Main()
    {
        // the Run 8 save offer: #2 save first, #3 don't save second -> don't save, whatever the order
        Eq(1, DialogPick.DeclineSave(new List<string> { "save", "don't save" }, new List<bool> { true, true }), "save first");
        Eq(0, DialogPick.DeclineSave(new List<string> { "don't save", "save" }, new List<bool> { true, true }), "don't save first");
        Eq(1, DialogPick.DeclineSave(new List<string> { "save", "Don’t Save" }, new List<bool> { true, true }), "curly apostrophe, case");
        Eq(1, DialogPick.DeclineSave(new List<string> { "save", "dont save" }, new List<bool> { true, true }), "no apostrophe");
        Eq(-1, DialogPick.DeclineSave(new List<string> { "save", "don't save" }, new List<bool> { true, false }), "disabled decline: not picked");
        // move-flow dialogs without a save offer: unchanged (accepted as before)
        Eq(-1, DialogPick.DeclineSave(new List<string> { "yes", "no" }, new List<bool> { true, true }), "yes/no");
        Eq(-1, DialogPick.DeclineSave(new List<string> { "accept", "cancel" }, new List<bool> { true, true }), "accept/cancel");
        Eq(-1, DialogPick.DeclineSave(new List<string>(), new List<bool>()), "no buttons");
        // named event rule: text decides, never the buttons alone
        string[] w = DialogPick.EventRule("Marty was talked into running an obstacle course at practice against some teammates. Should Marty compete or just watch the competition? compete watch");
        Eq(1, w != null && w.Length == 1 && w[0] == "watch" ? 1 : 0, "competition event -> watch");
        Eq(1, DialogPick.EventRule("SHOULD ANNA COMPETE OR JUST WATCH THE COMPETITION?") != null ? 1 : 0, "case");
        Eq(1, DialogPick.EventRule("compete watch") == null ? 1 : 0, "buttons alone: no rule");
        Eq(1, DialogPick.EventRule("Do you want to watch TV?") == null ? 1 : 0, "other text: no rule");
        Eq(1, DialogPick.EventRule(null) == null ? 1 : 0, "null text");
        Console.WriteLine(fails == 0 ? "dialogpick self-check ok" : fails + " FAIL");
        return fails == 0 ? 0 : 1;
    }
}

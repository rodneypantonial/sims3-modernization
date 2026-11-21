using System;
using Sims3.Gameplay.Actors;
using Sims3.SimIFace;
using Sims3.UI;

namespace Ts3Fixes
{
    // tombsting (kTombSting, docs/575 §11) [verified, decompiled]: the tomb-complete notice shows but its sting is sometimes
    // silent (player report). TombRoomManager.OnSimsRoomChanged calls actor.ShowTNSAndPlayStingIfSelectable(text,
    // kGameMessagePositive, InvalidObjectGuid, actor.ObjectId, "sting_tomb_complete"); that overload plays the sting with
    // Audio.StartObjectSound(ObjectId, name, false), a 3D sound at the Sim, which is inaudible when the Sim is far from the
    // camera or underground [inferred]. EA plays its other UI stings with the 2D Audio.StartSound(name).
    // Fix: for that one sting name the hook runs EA's body with Audio.StartSound(name). Same IsSelectable gate, same
    // Format, same StyledNotification.Show, same return value. Any other sting name goes straight to EA's.
    // The method is not virtual [verified, decompiled: no virtual keyword]; Patch's SwapMethod keeps flags either way.
    // Counters [2176] played as 2D, [2177] errors. Checks: TombStingCheck below.
    public static partial class Fixes
    {
        const string kTombStingName = "sting_tomb_complete";

        // Pure: does this call get the 2D sting?
        internal static bool TsgPlay2D(bool on, string stingName)
        {
            return on && stingName == kTombStingName;
        }

        static void ApplyTombSting()
        {
            Patch(typeof(Sim), "ShowTNSAndPlayStingIfSelectable",
                new Type[] { typeof(string), typeof(StyledNotification.NotificationStyle), typeof(ObjectGuid), typeof(ObjectGuid), typeof(string) },
                typeof(TsgHook), "StingHook", "StingOrig");
        }

        public class TsgHook
        {
            public StyledNotification StingHook(string message, StyledNotification.NotificationStyle style, ObjectGuid thumbnailObject1, ObjectGuid thumbnailObject2, string stingName)
            {
                if (stingName != kTombStingName) return StingOrig(message, style, thumbnailObject1, thumbnailObject2, stingName);
                try
                {
                    if (TsgPlay2D(ShortcutOn("tombsting"), stingName))
                    {
                        Sim sim = (Sim)(object)this;
                        if (!sim.IsSelectable) return null;
                        StyledNotification.Format format = new StyledNotification.Format(message, thumbnailObject1, thumbnailObject2, style);
                        Audio.StartSound(stingName);
                        PatchCounters()[2176]++;
                        return StyledNotification.Show(format);
                    }
                }
                catch (ResetException) { throw; }
                catch (Exception) { PatchCounters()[2177]++; }
                return StingOrig(message, style, thumbnailObject1, thumbnailObject2, stingName);
            }
            public StyledNotification StingOrig(string message, StyledNotification.NotificationStyle style, ObjectGuid thumbnailObject1, ObjectGuid thumbnailObject2, string stingName) { return null; } // Sim.ShowTNSAndPlayStingIfSelectable (5 args)
        }

        // Harness: "run" pure decisions with controls. "live": EA's own call on the active Sim, once with the tomb sting and
        // once with another sting name (control); the counter delta says which path ran (switch on: +1 / +0).
        public static object TombStingCheck(string what)
        {
            string counters = B582Counters("tombsting", 2176, new[] { "played as 2D", "errors" }, "");
            if (what == "result") return counters;
            bool on = ShortcutOn("tombsting");
            if (what == "live") return TsgLive(on);
            if (what != "run") return "tombsting <run|live|result>";
            bool fixedNow = TsgPlay2D(on, kTombStingName);
            bool controls = !TsgPlay2D(on, "sting_adv_opp_complete") && !TsgPlay2D(on, "") && !TsgPlay2D(on, null) && !TsgPlay2D(false, kTombStingName);
            return B582Line("tombsting", on, "the tomb sting played in 2D", fixedNow,
                "other sting names / switch off left to EA=" + controls, controls, "", counters);
        }

        static string TsgLive(bool on)
        {
            Sim mine = Sim.ActiveActor;
            if (mine == null) return "need an active Sim";
            int[] c = PatchCounters();
            int before = c[2176];
            mine.ShowTNSAndPlayStingIfSelectable("tombsting check", StyledNotification.NotificationStyle.kGameMessagePositive, ObjectGuid.InvalidObjectGuid, mine.ObjectId, kTombStingName);
            int tomb = c[2176] - before;
            before = c[2176];
            mine.ShowTNSAndPlayStingIfSelectable("tombsting control", StyledNotification.NotificationStyle.kGameMessagePositive, ObjectGuid.InvalidObjectGuid, mine.ObjectId, "sting_adv_opp_complete");
            int control = c[2176] - before;
            return "tomb sting: counter 2176 +" + tomb + (on ? (tomb == 1 ? " (FIXED)" : " (missing: check)") : (tomb == 0 ? " (BROKEN, EA's 3D sting)" : " (unexpected)"))
                + "; control sting: +" + control + (control == 0 ? " (EA's)" : " (WRONG)");
        }
    }
}

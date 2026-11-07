using System;
using Ts3Agent;

static class WaterRuleTest
{
    static int fails;
    static void Is(bool want, bool got, string what) { if (want != got) { fails++; Console.WriteLine("FAIL " + what + ": want " + want + ", got " + got); } }
    static int Main()
    {
        // the EA ip-uboat-repro sample: 2.04 m below sea, standing on the lot's terrain (LotTerrain 16 / LotFloor 32): dry
        Is(false, WaterRule.InWater(2.04f, -2.04f, 16), "dry lot terrain below sea level");
        Is(false, WaterRule.InWater(2.04f, -2.04f, 32), "dry lot floor below sea level");
        Is(false, WaterRule.InWater(2.04f, -2.04f, 1), "dry world terrain below sea level");
        // real water at the point
        Is(true, WaterRule.InWater(2.04f, -1.2f, 2), "sea");
        Is(true, WaterRule.InWater(1.0f, -0.5f, 8), "world pond");
        Is(true, WaterRule.InWater(1.0f, -0.5f, 64), "lot pool");
        Is(true, WaterRule.InWater(1.0f, -0.5f, 128), "lot pond");
        Is(true, WaterRule.InWater(1.0f, -0.5f, 16 | 2), "sea bit with another bit");
        // the old rules stay
        Is(false, WaterRule.InWater(0.4f, -0.4f, 2), "0.5 m or shallower");
        Is(false, WaterRule.InWater(3f, 0.5f, 2), "standing on a deck above the sea");
        Is(true, WaterRule.InWater(3f, 0.3f, 2), "0.3 m above is still in");
        Console.WriteLine(fails == 0 ? "waterrule self-check ok" : fails + " FAIL");
        return fails == 0 ? 0 : 1;
    }
}

# Manual verification: buy and build mode on the far-quadrant lot (2026-11-28, Rodney, by hand)

World `AtlasCawStitchedWN_4096`, far-quadrant lot `0x83E000D95CDEDB30` (x 3050, z 3268, both over 2048 m), main20n.
Rodney placed a house and objects on it by hand in Buy and Build mode; both work past 2048 m on both axes. Manual, not a harness run: no per-step log, no ts3native.log read.
Agent-driven run f (same day) did not prove buy placement: `buy_object` reported "placed" with position (-20000, -20000, -20000), so that step is open (later fix).

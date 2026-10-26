// Interactions that loop by design (a role job re-pushed while the Sim is idle), not stuck Sims: Late Night bouncers
// (VelvetRopes.WatchDoor re-routes to its slot every 5 sim-minutes until the role ends).
export const endlessJob = (interaction: string): boolean => /^Watch Door$/.test(interaction);

// States in which a Sim is meant to stand still for a long time (Rodney, 2026-10-17: a Sim "frozen solid" in Seasons
// winter was counted as stuck). Only when the Sim is not routing: a Sim in one of these that routes without moving is
// still a stuck route.
// 2026-10-21 bp-ut2-stuck-off/-on (docs/143 §8.12): 27 of 37 and 7 of 20 "stuck" Sims were shifts inside a work building
// ("Work Until End of Shift (6:30 PM)", left at shift end) or long venue activities (performances, dancing, reading,
// watching, playing an instrument, swings) that last longer than the 3 minutes of 6 checks; they stand still by design too.
export const stillByDesign = (interaction: string, routing: boolean): boolean =>
  !routing && (/^(Freeze Solid|Frozen Solid|Thaw|Thaw Out|Pass Out|Passed Out|Faint|Sleep|Nap|Be Frozen)$/i.test(interaction) ||
    /^(Work Until End of Shift\b.*|Performance|Perform For Tips|Watch|Swing|Blues|Jam|Slow Dance|Dance\b.*|(Re)?[Rr]ead\b.*)$/.test(interaction));

// The one rule every stuck check uses: an interaction that neither loops by design nor stands still by design.
// inTransit (sims_activity in_transit): a ts3fixes unseenfbpre walk-time hold (docs/202 §3.7), standing still on purpose
// until its end (the agent drops it from the table after the end, so a hold that never ends is judged normally).
export const mayBeStuck = (interaction: string | null | undefined, routing: boolean, inTransit = false): boolean =>
  !inTransit && !!interaction && !endlessJob(interaction) && !stillByDesign(interaction, routing);

// self-check: node tools/harness/stuck-rule.ts
if (process.argv[1]?.endsWith('stuck-rule.ts')) {
  const ok = !mayBeStuck('Freeze Solid', false) && mayBeStuck('Freeze Solid', true) && !mayBeStuck('Watch Door', true) &&
    mayBeStuck('Go Home', false) && !mayBeStuck(null, false) && !mayBeStuck('Sleep', false) &&
    !mayBeStuck('Work Until End of Shift (6:30 PM)', false) && !mayBeStuck('Dance Wildly Together', false) &&
    !mayBeStuck('Reread "I\'m Still Cool"', false) && !mayBeStuck('Read "Zombies? Zombies!"', false) &&
    mayBeStuck('Work Until End of Shift (6:30 PM)', true) && mayBeStuck('Visit Banzai Lounge', true) && mayBeStuck('Ready', false) &&
    !mayBeStuck('Go Home', false, true) && mayBeStuck('Go Home', false, false);
  console.log(ok ? 'stuck-rule ok' : 'stuck-rule FAIL');
  if (!ok) process.exit(1);
}

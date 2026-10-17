// Interactions that loop by design (a role job re-pushed while the Sim is idle), not stuck Sims: Late Night bouncers
// (VelvetRopes.WatchDoor re-routes to its slot every 5 sim-minutes until the role ends).
export const endlessJob = (interaction: string): boolean => /^Watch Door$/.test(interaction);

// States in which a Sim is meant to stand still for a long time (Rodney, 2026-10-17: a Sim "frozen solid" in Seasons
// winter was counted as stuck). Only when the Sim is not routing: a Sim in one of these that routes without moving is
// still a stuck route.
export const stillByDesign = (interaction: string, routing: boolean): boolean =>
  !routing && /^(Freeze Solid|Frozen Solid|Thaw|Thaw Out|Pass Out|Passed Out|Faint|Sleep|Nap|Be Frozen)$/i.test(interaction);

// The one rule every stuck check uses: an interaction that neither loops by design nor stands still by design.
export const mayBeStuck = (interaction: string | null | undefined, routing: boolean): boolean =>
  !!interaction && !endlessJob(interaction) && !stillByDesign(interaction, routing);

// self-check: node tools/harness/stuck-rule.ts
if (process.argv[1]?.endsWith('stuck-rule.ts')) {
  const ok = !mayBeStuck('Freeze Solid', false) && mayBeStuck('Freeze Solid', true) && !mayBeStuck('Watch Door', true) &&
    mayBeStuck('Go Home', false) && !mayBeStuck(null, false) && !mayBeStuck('Sleep', false);
  console.log(ok ? 'stuck-rule ok' : 'stuck-rule FAIL');
  if (!ok) process.exit(1);
}

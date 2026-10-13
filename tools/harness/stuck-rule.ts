// Interactions that loop by design (a role job re-pushed while the Sim is idle), not stuck Sims: Late Night bouncers
// (VelvetRopes.WatchDoor re-routes to its slot every 5 sim-minutes until the role ends).
export const endlessJob = (interaction: string): boolean => /^Watch Door$/.test(interaction);

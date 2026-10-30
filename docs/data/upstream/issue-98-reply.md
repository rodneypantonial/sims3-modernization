Thanks for the dump details. This is the same instruction as a crash we had on 10/22 (docs/67 §11, `0x84EAC3`). That one ran with an experimental switch, `[Discourage] DestroyPark=1`, which delays object destructors. That switch is off in every release, including your v0.2.3, so that mechanism doesn't apply here. Write-up: docs/67 §26.

What the objects are (from the disassembly):

- Vtable `0x00FC2068` is `Animation/ConformToRouteController`, a 0x48-byte controller that bends an actor's animation to follow the route. Constructors `0x56E420` / `0x56E4C0`, destructor `0x56E580`.
- `element+0x1C` is the **actor** it conforms. The controller stores it raw, with no reference taken. Everything else it holds (`+0x2C` path, `+0x30`, `+0x34`) is refcounted.
- Only two places create it, both in `0x84B590`. `0x84B756` passes `[body+0x124]`, the body's own actor. `0x84B958` passes an entry of a second actor array at `[body+0xB4]`.

So what got freed is an actor, a Sim's or an object's animated object. The six Sims the pickup destroyed are the obvious candidates. We went through every release-default ts3native feature: the route gate's destroy wait, footprint records, the async planner, the unseen fallback deadlines. None of them frees an actor or keeps one past its destroy. So far this looks like EA's raw pointer, but we can't rule ourselves out without a few reads from your dump:

1. The element `E = [node+8]`: 0x48 bytes from `E`, plus the dword at `0x0114F590` (a global counter; `[E+0x20]` is this controller's number from it).
2. The body `esi`: `[esi+0x124]`, `[esi+0xB0]`, `[esi+0xB4]`, `[esi+0xB8]` and the dwords of the `+0xB4` array. Is `A = [E+0x1C]` equal to `[esi+0x124]`, to one of the array entries, or to neither?
3. For `A`: VirtualQuery (state, allocation base, region size) and 0x60 bytes from `A-0x10`. The EA chunk size word is at `A-0xC`.
4. The first 0x40 bytes of `[esi+0x124]`, and from the agent `ebx`: `[ebx+8]`, `[ebx+0x68]`, `[ebx+0x6C]`.
5. From `Game\Bin\ts3native.log`: the last 5 minutes of minute lines (`route async`, `route gate`, `unseen fallback`, `discourage`), any `crash log:` lines, and the ts3fixes shortcut and stuck counters. Also, did you have S3SS, S3PP or Apex Radiance installed?

If `A` is one of the pickup Sims and also the body's own `+0x124`, the Sim's locomotion agent outlived its owner. If `A` sits in the `+0xB4` array, another actor of this Sim's route was destroyed under it. A ts3fixes shortcut, teleport or reset of that Sim in the same seconds would point at us.

// Story census (docs/171 §4, §7): what a world has, so the story fuzzer only picks templates whose needs hold.
//   node tools/harness/story-census.ts --save Baseline-SV [--refresh]   world_census through the probe (game in live mode),
//                                                                         cached in tools/harness/stories/census/<save>.json
//   node tools/harness/story-census.ts --show FILE                       print the need table of a cached census
//   node tools/harness/story-census.ts                                    self-check (fixture, no game)
// Raw answer: agent world_census (StoryHandlers.cs, see fromAgent) is mapped to the shape below; missing fields count as "none"
// (a need on them is then missing):
//   { world, save?, eps: ["EP1",...], season?: "Summer", moon?: 0..7 | "FullMoon",
//     lots: [{ id, name, venue?, community?, household?, pos?: {x,y,z} }], rabbitholes: [{ type, name?, id?, lot? }],
//     objects: { <kind>: count | [{ id, name?, lot? }] } | [{ kind, count?, ids? }], shops: same as objects, skillclasses: [names],
//     sims: [{ id, name, age, occult?: string | [..], species?, household?, active?: bool, in_world? }] }
// Need status: available (the world has it), preparable (prepare: true and the engine can make it cheaply: object = buy_object +
// place on a driven lot, age / occult / pet = make_sim (+ make_occult), season / moon = test_env fixed, only when the run's
// environment guard is off), missing (skipped, listed in the coverage table).
import assert from 'node:assert/strict';
import { existsSync, mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { join, resolve } from 'node:path';

export const KINDS = ['world', 'ep', 'venue', 'rabbithole', 'shop', 'object', 'skillclass', 'age', 'occult', 'season', 'moon', 'pet'] as const;
export type Kind = typeof KINDS[number];
export type Need = { kind: Kind; value?: string | string[]; prepare?: boolean };
export type Census = { world: string; save: string; raw: any; have: Record<Kind, Map<string, number>> };
export type NeedStatus = 'available' | 'preparable' | 'missing';
const PREPARABLE: Kind[] = ['object', 'age', 'occult', 'pet', 'season', 'moon'];
export const CENSUS_DIR = resolve(import.meta.dirname, 'stories', 'census');

// one spelling per value: "Pool Ladder" = "pool_ladder" = "poolladder"
export const norm = (s: unknown) => String(s ?? '').toLowerCase().replace(/[^a-z0-9]/g, '');
// moon names test_env's moon_phase understands [verified: HarnessHandlers TestEnv, 0 full, 2 first quarter]; others by number
export const MOON: Record<string, number> = { full: 0, fullmoon: 0, firstquarter: 2 };
// census EP codes (ProductVersion EPn, what world_census sends) -> the set names templates use (docs/171 §8); a census that already
// sends names matches as is. Order [verified]: ProductVersion bits EP1..EP11 and the CommercialLotSubType prefixes (kEP1_Market,
// kEP2_FireStation, kEP3_DanceClub*, kEP5_HorseRanch, kEP6_Bistro, kEP7_WerewolfBar, kEP8_Festival, kEP9_Dormitory, kEP10_Port, kEP11_*)
export const EP_NAMES: Record<string, string> = { ep1: 'WorldAdventures', ep2: 'Ambitions', ep3: 'LateNight', ep4: 'Generations', ep5: 'Pets', ep6: 'Showtime',
  ep7: 'Supernatural', ep8: 'Seasons', ep9: 'UniversityLife', ep10: 'IslandParadise', ep11: 'IntoTheFuture' };
// "any" / "*" / no value: any of that kind
export const isAny = (v: unknown) => v === undefined || [v].flat().some(x => x === '*' || norm(x) === 'any');

// A's world_census answer (StoryHandlers.cs WorldCensus: world, world_file, eps, season, moon_phase, community_lots, rabbit_hole_list,
// objects {kind: {count, samples}}, sims [{.., occult: "A,B", instantiated, active_household}], households) -> the shape read below.
// Venues lose the CommercialLotSubType prefix (kEP3_DanceClubRave -> DanceClubRave); register/stall objects are the shops.
export function fromAgent(a: any): any {
  if (!a?.community_lots && !a?.rabbit_hole_list) return a; // already this shape (fixture, older cache)
  const venue = (v: unknown) => String(v ?? '').replace(/^k(EP\d+_)?/, '');
  const season = /^(spring|summer|fall|winter)$/i.test(String(a.season)) ? a.season : undefined; // "Seasons (EP8) not installed" = none
  return {
    world: a.world_file || a.world, worlds: [a.world, a.world_file], save: a.save, eps: a.eps, season,
    moon: typeof a.moon_phase === 'number' ? a.moon_phase : undefined,
    lots: [...(a.community_lots ?? []).map((l: any) => ({ ...l, venue: venue(l.venue), community: true })),
      ...(a.households ?? []).filter((h: any) => h.lot).map((h: any) => ({ id: h.lot, name: h.name, household: h.name }))],
    homeLot: Object.fromEntries((a.households ?? []).filter((h: any) => h.lot).map((h: any) => [String(h.id), String(h.lot)])),
    rabbitholes: a.rabbit_hole_list ?? [],
    objects: a.objects ?? {},
    shops: Object.fromEntries(['register', 'stall'].filter(k => a.objects?.[k]).map(k => [k, a.objects[k]])),
    sims: (a.sims ?? []).map((s: any) => ({ id: s.id, name: s.name, age: s.age, ...(norm(s.species) !== 'human' && { species: s.species }), household: s.household,
      occult: String(s.occult ?? '').split(',').filter(Boolean), in_world: !!s.instantiated, active: !!s.active_household, ...(s.female !== undefined && { female: !!s.female }) })),
  };
}
// a census object entry: count, [{id, lot?}] or A's {count, samples}; samples in an inventory are not targets
export const samplesOf = (v: any): any[] => (Array.isArray(v) ? v : Array.isArray(v?.samples) ? v.samples : []).filter((o: any) => !o?.inventory);

export function normalize(input: any, save = String(input?.save ?? '')): Census {
  const raw = fromAgent(input);
  const have = Object.fromEntries(KINDS.map(k => [k, new Map<string, number>()])) as Census['have'];
  const add = (k: Kind, v: unknown, n = 1) => { const key = norm(v); if (key) have[k].set(key, (have[k].get(key) ?? 0) + n); };
  const counts = (k: Kind, o: any) => {
    if (Array.isArray(o)) for (const e of o) typeof e === 'string' ? add(k, e) : add(k, e?.kind ?? e?.type ?? e?.name, Number(e?.count ?? e?.ids?.length ?? 1));
    // every sample in an inventory (BP skill books: 180 counted, 20 of 20 samples in inventories): count none placed, so the
    // need prepares (or skips) instead of a story that finds nothing to target (story-smoke 2026-10-23)
    else for (const [key, v] of Object.entries(o ?? {})) add(k, key, Array.isArray(v) ? v.length : (v as any)?.samples?.length && (v as any).samples.every((s: any) => s?.inventory) ? 0 : Number((v as any)?.count ?? v) || 0);
  };
  add('world', raw?.world);
  for (const w of raw?.worlds ?? []) if (norm(w) !== norm(raw?.world)) add('world', w);
  for (const e of raw?.eps ?? []) { add('ep', e); if (EP_NAMES[norm(e)]) add('ep', EP_NAMES[norm(e)]); }
  for (const l of raw?.lots ?? []) if (l?.venue) add('venue', l.venue);
  counts('rabbithole', raw?.rabbitholes);
  counts('object', raw?.objects);
  counts('shop', raw?.shops);
  for (const c of raw?.skillclasses ?? []) add('skillclass', c);
  for (const s of raw?.sims ?? []) {
    add('age', s?.age);
    for (const o of [s?.occult ?? []].flat()) add('occult', o);
    if (s?.species && norm(s.species) !== 'human') add('pet', s.species);
  }
  if (raw?.season) add('season', raw.season);
  if (raw?.moon !== undefined && raw?.moon !== null) {
    add('moon', raw.moon);
    const name = Object.entries(MOON).find(([, n]) => n === Number(raw.moon))?.[0];
    if (name) add('moon', name);
  }
  for (const k of KINDS) for (const [v, n] of have[k]) if (n <= 0) have[k].delete(v); // count 0 = none
  return { world: String(raw?.world ?? ''), save, raw, have };
}

// value absent or "*": any of that kind; a list: any one of them
export function hasNeed(c: Census, n: Need): boolean {
  const m = c.have[n.kind];
  if (!m) return false;
  const vals = isAny(n.value) ? [] : [n.value!].flat();
  return vals.length ? vals.some(v => m.has(norm(v))) : m.size > 0;
}
// envFree: the run's 30 s test_env guard is off (profile-world --no-env), so a season / moon set by test_env stays
export function needStatus(c: Census, n: Need, o: { envFree?: boolean } = {}): NeedStatus {
  if (hasNeed(c, n)) return 'available';
  if (!n.prepare || !PREPARABLE.includes(n.kind)) return 'missing';
  if ((n.kind === 'season' || n.kind === 'moon') && !o.envFree) return 'missing';
  if (n.kind === 'moon' && [n.value ?? ''].flat().every(v => MOON[norm(v)] === undefined && !/^[0-7]$/.test(String(v)))) return 'missing';
  if (n.kind !== 'season' && n.kind !== 'moon' && isAny(n.value)) return 'missing'; // what to make?
  return 'preparable';
}

const safe = (s: string) => s.replace(/\.sims3$/i, '').replace(/[^A-Za-z0-9_.-]/g, '_');
export const censusFile = (save: string) => join(CENSUS_DIR, `${safe(save)}.json`);

// the cached answer, else world_census (one call on the sim thread) written to the cache
export async function getCensus(ag: (cmd: string, args?: object, timeoutMs?: number) => Promise<any>, save: string, refresh = false): Promise<{ census: Census; cached: boolean }> {
  const f = censusFile(save);
  if (!refresh && existsSync(f)) return { census: normalize(JSON.parse(readFileSync(f, 'utf8')), save), cached: true };
  const r = await ag('world_census', {}, 30000);
  if (!r?.ok) throw new Error(`world_census failed: ${r?.error ?? 'no answer'}`);
  mkdirSync(CENSUS_DIR, { recursive: true });
  writeFileSync(f, JSON.stringify({ ...r.result, save, cachedAt: new Date().toISOString() }, null, 1));
  return { census: normalize(r.result, save), cached: false };
}

// A world_census answer in the agent's exact shape (StoryHandlers.cs WorldCensus; ids are decimal strings as Util.Id writes them)
// the object kinds world_census counts: StoryHandlers.cs KindTypes, same order (story-lint.ts self-check compares them)
export const KIND_NAMES = ['stove', 'pool', 'pool_ladder', 'book', 'skill_book', 'bookshelf', 'instrument', 'easel', 'chess', 'gym', 'telescope',
  'garden_plot', 'crystal_ball', 'register', 'stall', 'houseboat', 'smoke_detector', 'fishing_spot',
  'fridge', 'bed', 'crib', 'shower', 'toilet', 'sink', 'tv', 'computer', 'mailbox', 'trash_can', 'table', 'bar', 'dance_floor', 'cake', 'phone', 'homework'];
const fixtureSims = [
  ...['Adult', 'Adult', 'Child', 'Teen'].map((age, i) => ({ id: `7200${i}`, name: `Goth ${i}`, age, species: 'Human', female: i % 2 === 0, occult: '', household: '9100', instantiated: true, active_household: true })),
  ...['YoungAdult', 'Adult', 'Elder', 'Teen', 'YoungAdult', 'Adult', 'Child', 'Elder', 'YoungAdult', 'Adult'].map((age, i) =>
    ({ id: `7300${i}`, name: `Townie ${i}`, age, species: 'Human', female: i % 2 === 1, occult: i === 2 ? 'Vampire' : i === 4 ? 'Witch,Fairy' : '', household: `910${1 + i % 3}`, instantiated: i < 7, active_household: false })),
  { id: '74001', name: 'Rex', age: 'Adult', species: 'Dog', female: false, occult: '', household: '9101', instantiated: true, active_household: false },
];
const count = (xs: string[]) => xs.reduce((o: Record<string, number>, k) => ({ ...o, [k]: (o[k] ?? 0) + 1 }), {});
export const FIXTURE = {
  world: 'SunsetValley', world_file: 'Sunset Valley', eps: ['EP1', 'EP3', 'EP8', 'SP1'],
  season: 'Summer', weather: 'Sunny', moon_phase: 2, days_to_full_moon: 6, hour: 14,
  venues: { kBigPark: 1, kGym: 1, kEP3_DanceClubRave: 1 },
  community_lots: [
    { id: '102', name: 'Central Park', venue: 'kBigPark', diving: false, pos: { x: 300, y: 0, z: 120 } },
    { id: '103', name: 'Body Shoppe', venue: 'kGym', diving: false, pos: { x: 500, y: 0, z: 400 } },
    { id: '104', name: 'Rave Club', venue: 'kEP3_DanceClubRave', diving: false, pos: { x: 650, y: 0, z: 300 } },
  ],
  residential_occupied: 4, residential_vacant: 2,
  rabbit_holes: { Bookstore: 1, CityHall: 1 },
  rabbit_hole_list: [{ id: '8001', type: 'Bookstore', lot: 'Books' }, { id: '8002', type: 'CityHall', lot: null }],
  objects: {
    ...Object.fromEntries(KIND_NAMES.map(k => [k, { count: 0, samples: [] as any[] }])),
    stove: { count: 1, samples: [{ id: '5001', type: 'StoveCheap', lot: '101', inventory: false }] },
    book: { count: 12, samples: [{ id: '5002', type: 'BookGeneral', lot: '101', inventory: true }, { id: '5004', type: 'BookGeneral', lot: '101', inventory: false }] },
    chess: { count: 1, samples: [{ id: '5003', type: 'ChessTable', lot: '102', inventory: false }] },
    // the Goth home (lot 101) has what every lived-in home has, so everyday stories need no purchase
    ...Object.fromEntries(['fridge', 'bed', 'shower', 'toilet', 'sink', 'tv', 'computer', 'mailbox', 'trash_can', 'table'].map((k, i) =>
      [k, { count: 1, samples: [{ id: String(5100 + i), type: k, lot: '101', inventory: false }] }])),
  },
  objects_scanned: 880,
  sims_by_age: count(fixtureSims.map(s => s.age)), sims_by_species: count(fixtureSims.map(s => s.species)),
  sims_by_occult: count(fixtureSims.flatMap(s => s.occult ? s.occult.split(',') : ['None'])),
  sims: fixtureSims,
  households: [{ id: '9100', name: 'Goth', members: 4, lot: '101', active: true }, { id: '9101', name: 'Landgraab', members: 5, lot: '105', active: false },
    { id: '9102', name: 'Alto', members: 3, lot: null, active: false }, { id: '9103', name: 'Hart', members: 3, lot: '106', active: false }],
};

if (import.meta.url === `file:///${process.argv[1]?.replace(/\\/g, '/').replace(/^\//, '')}`) {
  const { parseArgs } = await import('node:util');
  const { values } = parseArgs({ options: { save: { type: 'string' }, refresh: { type: 'boolean' }, show: { type: 'string' } } });
  const table = (c: Census) => { for (const k of KINDS) console.log(`${k.padEnd(11)} ${[...c.have[k]].map(([v, n]) => n > 1 ? `${v} x${n}` : v).join(', ') || '-'}`); };
  if (values.show) table(normalize(JSON.parse(readFileSync(values.show, 'utf8'))));
  else if (values.save) {
    const ag = async (cmd: string, args?: object, to = 8000) => {
      const q = new URLSearchParams({ cmd, timeout: String(to) }); if (args) q.set('args', JSON.stringify(args));
      try { return await (await fetch(`http://127.0.0.1:47891/agent?${q}`, { signal: AbortSignal.timeout(to + 5000) })).json(); } catch (e) { return { ok: false, error: String(e) }; }
    };
    const { census, cached } = await getCensus(ag, values.save, values.refresh);
    console.log(`${census.world} (${cached ? 'cached' : 'fresh'}: ${censusFile(values.save)})`); table(census);
  } else {
    const c = normalize(FIXTURE);
    assert.equal(c.world, 'Sunset Valley');
    assert.equal(c.have.object.get('stove'), 1); assert.equal(c.have.object.get('book'), 12); assert.ok(!c.have.object.has('pool'), 'count 0 = none');
    assert.equal(c.have.age.get('adult'), 6); assert.ok(c.have.occult.has('vampire')); assert.ok(c.have.pet.has('dog'));
    assert.ok(c.have.moon.has('firstquarter') && c.have.moon.has('2'));
    const st = (n: Need, envFree = false) => needStatus(c, n, { envFree });
    assert.equal(st({ kind: 'venue', value: 'Big Park' }), 'available');
    assert.equal(st({ kind: 'rabbithole', value: ['Library', 'bookstore'] }), 'available');
    assert.equal(st({ kind: 'rabbithole', value: 'Spa', prepare: true }), 'missing', 'rabbit holes are never prepared');
    assert.equal(st({ kind: 'object', value: 'pool', prepare: true }), 'preparable');
    assert.equal(st({ kind: 'object', value: 'pool' }), 'missing');
    assert.equal(st({ kind: 'object', prepare: true }), 'available', 'any object');
    assert.equal(st({ kind: 'occult', value: 'Werewolf', prepare: true }), 'preparable');
    assert.equal(st({ kind: 'season', value: 'Winter', prepare: true }), 'missing', 'test_env guard on');
    assert.equal(st({ kind: 'season', value: 'Winter', prepare: true }, true), 'preparable');
    assert.equal(st({ kind: 'moon', value: 'full', prepare: true }, true), 'preparable');
    assert.equal(st({ kind: 'moon', value: 'waxing', prepare: true }, true), 'missing', 'no phase number for that name');
    assert.equal(st({ kind: 'ep', value: 'EP8' }), 'available');
    assert.equal(st({ kind: 'ep', value: 'Seasons' }), 'available', 'EP8 = Seasons');
    assert.equal(st({ kind: 'ep', value: 'IslandParadise' }), 'missing');
    assert.equal(st({ kind: 'venue', value: 'any' }), 'available');
    assert.equal(st({ kind: 'world', value: 'Bridgeport', prepare: true }), 'missing');
    assert.equal(normalize({ objects: [{ kind: 'Easel', count: 2 }, 'Telescope'] }).have.object.get('easel'), 2);
    // A's shape: world enum + file name, prefixed venues, comma occults, inventory samples, households' home lots
    assert.ok(c.have.world.has('sunsetvalley'));
    assert.equal(st({ kind: 'venue', value: ['DanceClubRave', 'DanceClubPool'] }), 'available', 'kEP3_DanceClubRave -> DanceClubRave');
    assert.ok(c.have.occult.has('witch') && c.have.occult.has('fairy'));
    assert.equal(st({ kind: 'object', value: 'pool ladder' }), 'missing', 'pool_ladder count 0');
    assert.equal(st({ kind: 'shop', value: 'stall' }), 'missing');
    assert.ok(c.have.ep.has('sp1'));
    assert.deepEqual(samplesOf(c.raw.objects.book).map((o: any) => o.id), ['5004'], 'inventory samples are not targets');
    assert.equal(c.raw.sims.filter((s: any) => s.in_world).length, 4 + 7 + 1);
    assert.ok(!c.raw.sims[0].species && c.raw.sims.at(-1).species === 'Dog', 'humans without species, as list_sims');
    assert.deepEqual(c.raw.lots.map((l: any) => l.id), ['102', '103', '104', '101', '105', '106']);
    assert.equal(fromAgent(c.raw), c.raw, 'mapped shape passes through');
    console.log('story-census selftest: PASS');
  }
}

// Story households (docs/181): seeded CAS households with their own traits, lifetime wish, favourites and persona, and each
// trait's expected positive and negative consequences (docs/data/traits.csv), for story runs (config household: generated).
//   node tools/harness/story-households.ts --write [--seed 1] [--random 6]  archetypes + random households -> stories/households/
//   node tools/harness/story-households.ts --lint                           every stories/households/*.json vs traits.csv + docs/179
//   node tools/harness/story-households.ts --show <id>                      persona summary of one household
//   node tools/harness/story-households.ts --self-check
// Rules (docs/181 §2, docs/179 R-HH-1..3): 2..8 humans, no Baby (CAS makes Toddler+), at least one YA+, no pets (they join by
// adoption in play); CAS trait slots Toddler 2 / Child 3 / Teen 4 / YA+ 5 [verified TraitManager.NumTraitsForAge], only
// CAS-visible human traits for the age [verified Traits XML AgeSpeciesVisible, Trait.IsVisible], no two traits sharing a
// conflict set [verified TraitManager.DoTraitsConflict]; lifetime wish YA+ only, required traits present, excluded absent
// [verified DreamsAndPromisesManager.GetTopMajorDreamMatches]; balance: every Sim with 3+ traits has an upside and a downside
// trait, and 20..65 % of the household's scored traits are downsides (EA's own Points column: > 0 up, < 0 down).
import { readFileSync, writeFileSync, readdirSync, existsSync, mkdirSync } from 'node:fs';
import { join, resolve } from 'node:path';
import assert from 'node:assert/strict';

const ROOT = resolve(import.meta.dirname, '..', '..');
export const HH_DIR = resolve(import.meta.dirname, 'stories', 'households');

export type Trait = { id: string; ep: string; ages: string[]; sets: string[]; cas: string; points: number; weight: number; persona: string; plus: string; minus: string };
export type Wish = { id: number; name: string; ep: string; required: string[]; excluded: string[]; matches: string[]; occult: boolean };
export type HSim = { key: string; first: string; last: string; age: string; gender: 'Male' | 'Female'; traits: string[]; wish?: { id: number; name: string } | null;
  food: string; music: string; color: string; zodiac: string; voice: string; storyPersona: string; persona: string; expect: { trait: string; plus: string; minus: string }[] };
export type Relation = { a: string; b: string; type: 'spouse' | 'partner' | 'parent' | 'sibling' | 'roommate' };
export type Household = { id: string; name: string; archetype: string | null; seed: number; structure: string; summary: string; funds: number;
  balance: { up: number; down: number; neutral: number }; sims: HSim[]; relations: Relation[]; pets?: { species: string; age: string }[] };

// ---- data (docs/data/traits.csv, docs/data/lifetime-wishes.csv)
function csv(file: string): Record<string, string>[] {
  const rows: string[][] = [];
  let row: string[] = [], cell = '', q = false;
  const s = readFileSync(file, 'utf8');
  for (let i = 0; i < s.length; i++) {
    const c = s[i];
    if (q) { if (c === '"' && s[i + 1] === '"') { cell += '"'; i++; } else if (c === '"') q = false; else cell += c; }
    else if (c === '"') q = true;
    else if (c === ',') { row.push(cell); cell = ''; }
    else if (c === '\n') { row.push(cell); rows.push(row); row = []; cell = ''; }
    else if (c !== '\r') cell += c;
  }
  if (cell || row.length) { row.push(cell); rows.push(row); }
  const [head, ...body] = rows;
  return body.filter(r => r.length > 1).map(r => Object.fromEntries(head.map((h, i) => [h, r[i] ?? ''])));
}
let cache: { traits: Map<string, Trait>; wishes: Wish[] } | null = null;
export function data() {
  if (cache) return cache;
  const traits = new Map<string, Trait>();
  for (const r of csv(join(ROOT, 'docs', 'data', 'traits.csv')))
    traits.set(r.id, { id: r.id, ep: r.ep, ages: r.ages.split(' ').filter(Boolean), sets: r.sets.split(' ').filter(Boolean), cas: r.cas, points: Number(r.points) || 0,
      weight: r.random_weight === '' ? 1 : Number(r.random_weight), persona: r.story_persona, plus: r.positive, minus: r.negative });
  const wishes = csv(join(ROOT, 'docs', 'data', 'lifetime-wishes.csv')).map(r => ({ id: Number(r.id), name: r.name, ep: r.ep, required: r.required.split(' ').filter(Boolean),
    excluded: r.excluded.split(' ').filter(Boolean), matches: r.matches.split(' ').filter(Boolean), occult: r.occult_only === 'yes' }));
  return (cache = { traits, wishes });
}

// ---- game rules
export const AGES = ['Toddler', 'Child', 'Teen', 'YoungAdult', 'Adult', 'Elder'] as const;
const CODE: Record<string, string> = { Toddler: 'P', Child: 'C', Teen: 'T', YoungAdult: 'Y', Adult: 'A', Elder: 'E' };
export const SLOTS: Record<string, number> = { Toddler: 2, Child: 3, Teen: 4, YoungAdult: 5, Adult: 5, Elder: 5 };
const adult = (age: string) => ['YoungAdult', 'Adult', 'Elder'].includes(age);
export const conflict = (a: Trait, b: Trait) => a.sets.some(s => b.sets.includes(s));
const casFor = (age: string) => [...data().traits.values()].filter(t => t.cas === 'human' && t.ages.includes(CODE[age]));
// CASLogic.GetHouseholdStartingFunds [verified]: 20000 - 5000 + 5000 per YA/A/E, 3000 Teen, 2000 Child, 1000 Toddler
export const startingFunds = (sims: { age: string }[]) => 15000 + sims.reduce((n, s) => n + (({ Toddler: 1000, Child: 2000, Teen: 3000 } as Record<string, number>)[s.age] ?? 5000), 0);
// FavoriteFoodType / FavoriteMusicType / CASCharacter.kColors / Zodiac / VoiceVariationType [verified enums]; vegetarians get a
// meat-free food (CASLogic.ChangeFavoriteFoodToVegetarian)
const VEG_FOOD = ['BellagianWaffles', 'FrenchToast', 'Pancakes', 'FruitParfait', 'AutumnSalad', 'GrilledCheese', 'MacAndCheese', 'PeanutButterAndJelly', 'Ratatouille', 'Cobbler', 'Cookies', 'KeyLimePie', 'Crepe', 'Falafel', 'FriedPeanutButterBanana', 'TofuDog', 'VegetarianFishAndChips', 'SpaghettiWithVeggiSauce', 'VeggiRolls', 'CheeseTofuSteak', 'Veggieburger', 'TriTipTofuSteak', 'VegetarianGrilledSalmon', 'VegetarianLobsterThermador', 'VegetarianDimSum', 'MushroomOmelette', 'AlooMasalaCurry', 'VegetarianChili', 'FirecrackerTofu', 'PorciniRisotto', 'PotatoAndTruffleTorte', 'PumpkinPie'];
export const FOOD = [...VEG_FOOD, 'Cheesesteak', 'DimSum', 'FishAndChips', 'GrilledSalmon', 'Hamburger', 'HotDog', 'LobsterThermador', 'GoopyCarbonara', 'Spaghetti', 'TriTipSteak', 'StewSurprise', 'RollSushi', 'EggRolls', 'FrogsLegs', 'Shawarma', 'StirFry', 'Ceviche', 'ChiliConCarne', 'FirecrackerShrimp'];
export const MUSIC = ['Electronica', 'Pop', 'Latin', 'Indie', 'Classical', 'Kids', 'France', 'China', 'Egypt', 'Roots', 'Soul', 'Rockabilly', 'HipHop', 'Country', 'RnB', 'Songwriter', 'DarkWave', 'Disco', 'Rap', 'Rock', 'GeekRock', 'BeachParty', 'IslandLife', 'FutureWorld', 'Horror', 'Epic', 'Spaghetti_Western'];
export const COLORS = ['Blue', 'Aqua', 'Turquoise', 'SpiceBrown', 'Green', 'IrishGreen', 'Lime', 'Grey', 'Orange', 'Purple', 'Spiceberry', 'Violet', 'Lilac', 'Pink', 'HotPink', 'Red', 'White', 'SeaFoam', 'Yellow', 'Black'];
export const ZODIAC = ['Aries', 'Taurus', 'Gemini', 'Cancer', 'Leo', 'Virgo', 'Libra', 'Scorpio', 'Sagittarius', 'Capricorn', 'Aquarius', 'Pisces'];

// ---- seeded randomness (mulberry32, as story-fuzzer.ts; own copy: story-fuzzer imports this file)
export function rng(seed: number) {
  let a = seed >>> 0 || 1;
  const next = () => { a = (a + 0x6d2b79f5) >>> 0; let t = a; t = Math.imul(t ^ (t >>> 15), t | 1); t ^= t + Math.imul(t ^ (t >>> 7), t | 61); return ((t ^ (t >>> 14)) >>> 0) / 4294967296; };
  const int = (lo: number, hi: number) => lo + Math.floor(next() * (hi - lo + 1));
  const pick = <T>(xs: T[]) => xs[int(0, xs.length - 1)];
  const weighted = <T>(xs: T[], w: (x: T) => number) => { const tot = xs.reduce((n, x) => n + w(x), 0); let r = next() * tot; for (const x of xs) if ((r -= w(x)) < 0) return x; return xs[xs.length - 1]; };
  return { next, int, pick, weighted };
}
type R = ReturnType<typeof rng>;

// ---- traits, wish, favourites
// prefer: the archetype's traits, taken in order when valid for the age; the rest by EA's own CAS RandomWeight
export function pickTraits(R: R, age: string, prefer: string[] = []): string[] {
  const pool = casFor(age), T = data().traits, out: Trait[] = [];
  const ok = (t: Trait) => !out.includes(t) && !out.some(o => conflict(o, t));
  for (const id of prefer) { const t = T.get(id); if (t && pool.includes(t) && ok(t) && out.length < SLOTS[age]) out.push(t); }
  while (out.length < SLOTS[age]) { const c = pool.filter(ok); if (!c.length) break; out.push(R.weighted(c, t => Math.max(t.weight, 0.5))); }
  return out.map(t => t.id);
}
const valence = (id: string) => Math.sign(data().traits.get(id)?.points ?? 0);
// every Sim with 3+ traits gets an upside and a downside: replace a non-preferred trait with a valid one of the missing sign
function rebalanceSim(R: R, age: string, traits: string[], prefer: string[]) {
  for (const want of [1, -1]) {
    if (traits.length < 3 || traits.some(t => valence(t) === want)) continue;
    const T = data().traits, np = traits.findLastIndex(t => !prefer.includes(t)), i = np >= 0 ? np : traits.length - 1, rest = traits.filter((_, j) => j !== i);
    const c = casFor(age).filter(t => Math.sign(t.points) === want && !rest.includes(t.id) && !rest.some(r => conflict(T.get(r)!, t)));
    if (i >= 0 && c.length) traits[i] = R.pick(c).id;
  }
}
// the game's CAS ranking: a required trait counts twice (DreamNodeInstance.GetNumTraitMatchesForMajorDream); top group, seeded
export function validWishes(traits: string[]) {
  return data().wishes.filter(w => !w.occult && w.required.every(t => traits.includes(t)) && !w.excluded.some(t => traits.includes(t)));
}
export function pickWish(R: R, traits: string[]) {
  const v = validWishes(traits), score = (w: Wish) => w.matches.filter(t => traits.includes(t)).length + 2 * w.required.length, top = Math.max(0, ...v.map(score));
  const w = R.pick(v.filter(x => score(x) === top));
  return w ? { id: w.id, name: w.name } : null;
}
const PERSONAS = new Set<string>(JSON.parse(readFileSync(resolve(import.meta.dirname, 'stories', 'personas.json'), 'utf8')).map((p: any) => p.id));
function storyPersona(age: string, traits: string[], kids: boolean) {
  if (age === 'Child' || age === 'Teen') return 'student';
  const t = traits.map(x => data().traits.get(x)?.persona).find(p => p && PERSONAS.has(p));
  if (t) return t;
  return age === 'Elder' ? 'retiree' : kids || age === 'Toddler' ? 'family' : 'socialite';
}
const expectOf = (traits: string[]) => traits.map(id => { const t = data().traits.get(id)!; return { trait: id, plus: t.plus, minus: t.minus }; });
const personaText = (first: string, s: Pick<HSim, 'age' | 'traits' | 'expect'>) =>
  `${first} (${s.age}): ${s.traits.join(', ')}. Upside: ${s.expect.filter(e => valence(e.trait) >= 0).slice(0, 2).map(e => e.plus).join('; ')}. Downside: ${s.expect.filter(e => valence(e.trait) <= 0).slice(0, 2).map(e => e.minus).join('; ')}.`;

// ---- households
const MALE = ['Adam', 'Ben', 'Carlos', 'Dmitri', 'Elias', 'Finn', 'Gabe', 'Hiro', 'Ivan', 'Jonah', 'Kofi', 'Leo', 'Marco', 'Nils', 'Omar', 'Pavel', 'Rafael', 'Sami', 'Theo', 'Victor'];
const FEMALE = ['Ada', 'Bea', 'Clara', 'Dana', 'Elena', 'Freya', 'Grace', 'Hana', 'Ines', 'Juno', 'Kira', 'Lena', 'Mira', 'Nora', 'Olive', 'Paula', 'Rosa', 'Sofia', 'Tess', 'Vera'];
const FAMILY = ['Ashford', 'Brook', 'Calder', 'Dunmore', 'Ellery', 'Fairbank', 'Greaves', 'Holloway', 'Ingram', 'Jessop', 'Kettle', 'Lumley', 'Marsh', 'Northcott', 'Oakley', 'Pemberton', 'Quill', 'Rowntree', 'Sallow', 'Thorne'];
type Member = { age: string; gender?: 'Male' | 'Female'; prefer?: string[]; last?: string };
export type Archetype = { id: string; name: string; summary: string; members: Member[]; relations: Relation[] };

// Named archetypes (Rodney 2026-10-23): keys m0..mN follow the member order; the generator fills the trait slots the
// preferred list leaves and repairs the balance, so every archetype still meets the household rule
export const ARCHETYPES: Archetype[] = [
  { id: 'chaotic-artists', name: 'Paintbrush Commune', summary: 'Three artist roommates: great art and music, messy house, stolen trinkets and odd behaviour.',
    members: [{ age: 'YoungAdult', prefer: ['Artistic', 'Insane', 'Slob', 'Virtuoso', 'PartyAnimal'] }, { age: 'YoungAdult', prefer: ['Artistic', 'Kleptomaniac', 'Excitable', 'NightOwlTrait'] }, { age: 'Adult', prefer: ['Virtuoso', 'AbsentMinded', 'Eccentric', 'Brooding', 'Clumsy'] }],
    relations: [{ a: 'm0', b: 'm1', type: 'roommate' }, { a: 'm0', b: 'm2', type: 'roommate' }, { a: 'm1', b: 'm2', type: 'roommate' }] },
  { id: 'strict-overachievers', name: 'Ladder Family', summary: 'Two ambitious parents and their gifted kids: promotions and grades, stress when things slip, no patience for mess.',
    members: [{ age: 'Adult', gender: 'Female', prefer: ['Ambitious', 'Perfectionist', 'Workaholic', 'Neat', 'Snob'] }, { age: 'Adult', gender: 'Male', prefer: ['Ambitious', 'Schmoozer', 'Neurotic', 'Genius', 'NoSenseOfHumor'] },
      { age: 'Teen', prefer: ['Genius', 'Perfectionist', 'BookWorm', 'Grumpy'] }, { age: 'Child', prefer: ['BookWorm', 'Neat', 'Neurotic'] }],
    relations: [{ a: 'm0', b: 'm1', type: 'spouse' }, { a: 'm0', b: 'm2', type: 'parent' }, { a: 'm1', b: 'm2', type: 'parent' }, { a: 'm0', b: 'm3', type: 'parent' }, { a: 'm1', b: 'm3', type: 'parent' }] },
  { id: 'feuding-family', name: 'Thornbury Feud', summary: 'A hot-headed father, a mean-spirited grandmother and kind kids caught between: fights, insults and making up.',
    members: [{ age: 'Elder', gender: 'Female', prefer: ['MeanSpirited', 'Grumpy', 'Snob', 'Inappropriate', 'GreenThumb'] }, { age: 'Adult', gender: 'Male', prefer: ['HotHeaded', 'Athletic', 'Inappropriate', 'Handy', 'CommitmentIssues'] },
      { age: 'Adult', gender: 'Female', prefer: ['Good', 'OverEmotional', 'FamilyOriented', 'NaturalCook', 'Neat'] }, { age: 'Teen', prefer: ['Rebellious', 'HotHeaded', 'Friendly', 'Artistic'] }, { age: 'Child', prefer: ['Good', 'Friendly', 'Clumsy'] }],
    relations: [{ a: 'm0', b: 'm1', type: 'parent' }, { a: 'm1', b: 'm2', type: 'spouse' }, { a: 'm1', b: 'm3', type: 'parent' }, { a: 'm2', b: 'm3', type: 'parent' }, { a: 'm1', b: 'm4', type: 'parent' }, { a: 'm2', b: 'm4', type: 'parent' }] },
  { id: 'lonely-genius', name: 'Quill Lab', summary: 'A reclusive young genius and an elderly father who wants grandchildren: inventions and chess, awkward socials, little company.',
    members: [{ age: 'YoungAdult', prefer: ['Genius', 'Loner', 'ComputerWhiz', 'SociallyAwkward', 'Neurotic'] }, { age: 'Elder', gender: 'Male', prefer: ['FamilyOriented', 'Grumpy', 'HatesOutdoors', 'Angler', 'LightSleeper'] }],
    relations: [{ a: 'm1', b: 'm0', type: 'parent' }] },
  { id: 'party-house', name: 'Neon Flat', summary: 'Four party-loving roommates: friends everywhere and great nights, broke, hungover and late for work.',
    members: [{ age: 'YoungAdult', prefer: ['PartyAnimal', 'Flirty', 'Mooch', 'HeavySleeper', 'Charismatic'] }, { age: 'YoungAdult', prefer: ['PartyAnimal', 'Virtuoso', 'Slob', 'GoodSenseOfHumor', 'Daredevil'] },
      { age: 'YoungAdult', prefer: ['SocialButterfly', 'Dramatic', 'Inappropriate', 'NightOwlTrait', 'Clumsy'] }, { age: 'YoungAdult', prefer: ['Friendly', 'Workaholic', 'Neat', 'LightSleeper', 'Grumpy'] }],
    relations: [{ a: 'm0', b: 'm1', type: 'roommate' }, { a: 'm0', b: 'm2', type: 'partner' }, { a: 'm1', b: 'm3', type: 'roommate' }, { a: 'm2', b: 'm3', type: 'roommate' }] },
  { id: 'full-house', name: 'Pemberton Eight', summary: 'Eight under one roof (the R-HH-1 cap): grandparents, parents, a toddler and four kids; noise, chores and birthdays.',
    members: [{ age: 'Elder', gender: 'Male', prefer: ['Good', 'Angler'] }, { age: 'Elder', gender: 'Female', prefer: ['NaturalCook', 'Neurotic'] }, { age: 'Adult', gender: 'Male', prefer: ['Workaholic', 'FamilyOriented'] },
      { age: 'Adult', gender: 'Female', prefer: ['Nurturing', 'HotHeaded'] }, { age: 'Teen', prefer: ['Athletic', 'CouchPotato'] }, { age: 'Teen', prefer: ['Artistic', 'Loner'] }, { age: 'Child', prefer: ['Excitable', 'Slob'] }, { age: 'Toddler', prefer: ['Clumsy'] }],
    relations: [{ a: 'm0', b: 'm1', type: 'spouse' }, { a: 'm0', b: 'm2', type: 'parent' }, { a: 'm1', b: 'm2', type: 'parent' }, { a: 'm2', b: 'm3', type: 'spouse' },
      ...[4, 5, 6, 7].flatMap(k => [{ a: 'm2', b: `m${k}`, type: 'parent' as const }, { a: 'm3', b: `m${k}`, type: 'parent' as const }])] },
  { id: 'mansion-eight', name: 'Ashworth Eight', summary: 'Eight under one roof for a furnished mansion with no crib (Story-BP-Mansion): grandparents, parents, two teens, two children; noise, chores and birthdays.',
    members: [{ age: 'Elder', gender: 'Male', prefer: ['Good', 'Angler'] }, { age: 'Elder', gender: 'Female', prefer: ['NaturalCook', 'Neurotic'] }, { age: 'Adult', gender: 'Male', prefer: ['Workaholic', 'FamilyOriented'] },
      { age: 'Adult', gender: 'Female', prefer: ['Nurturing', 'HotHeaded'] }, { age: 'Teen', prefer: ['Athletic', 'CouchPotato'] }, { age: 'Teen', prefer: ['Artistic', 'Loner'] }, { age: 'Child', prefer: ['Excitable', 'Slob'] }, { age: 'Child', prefer: ['Clumsy'] }],
    relations: [{ a: 'm0', b: 'm1', type: 'spouse' }, { a: 'm0', b: 'm2', type: 'parent' }, { a: 'm1', b: 'm2', type: 'parent' }, { a: 'm2', b: 'm3', type: 'spouse' },
      ...[4, 5, 6, 7].flatMap(k => [{ a: 'm2', b: `m${k}`, type: 'parent' as const }, { a: 'm3', b: `m${k}`, type: 'parent' as const }])] }
];

// random structures: couple, couple with kids, single parent, multi-generation, roommates, siblings; 2..8 humans
function randomArchetype(R: R, id: string): Archetype {
  const kid = () => R.pick(['Toddler', 'Child', 'Child', 'Teen', 'Teen']), g = () => R.pick(['Male', 'Female'] as const);
  const kind = R.pick(['couple', 'couple-kids', 'couple-kids', 'single-parent', 'multi-gen', 'roommates', 'siblings']);
  const m: Member[] = [], rel: Relation[] = [], k = (i: number) => `m${i}`;
  const couple = (age: string) => { const a = g(), b = R.next() < 0.85 ? (a === 'Male' ? 'Female' : 'Male') : a; m.push({ age, gender: a }, { age, gender: b }); rel.push({ a: k(m.length - 2), b: k(m.length - 1), type: R.next() < 0.7 ? 'spouse' : 'partner' }); };
  const kids = (n: number, parents: number[]) => { for (let i = 0; i < n; i++) { m.push({ age: kid() }); for (const p of parents) rel.push({ a: k(p), b: k(m.length - 1), type: 'parent' }); } };
  if (kind === 'couple') couple(R.pick(['YoungAdult', 'Adult', 'Elder']));
  else if (kind === 'couple-kids') { couple('Adult'); kids(R.int(1, 4), [0, 1]); }
  else if (kind === 'single-parent') { m.push({ age: 'Adult', gender: g() }); kids(R.int(1, 3), [0]); }
  else if (kind === 'multi-gen') { m.push({ age: 'Elder', gender: g() }); couple('Adult'); rel.push({ a: k(0), b: k(R.int(1, 2)), type: 'parent' }); kids(R.int(1, 5), [1, 2]); }
  else if (kind === 'roommates') { const n = R.int(2, 5); for (let i = 0; i < n; i++) { m.push({ age: R.pick(['YoungAdult', 'YoungAdult', 'Adult']), gender: g(), last: R.pick(FAMILY) }); if (i) rel.push({ a: k(i - 1), b: k(i), type: 'roommate' }); } }
  else { m.push({ age: 'YoungAdult', gender: g() }); const n = R.int(1, 3); for (let i = 0; i < n; i++) { m.push({ age: R.pick(['Child', 'Teen', 'YoungAdult']) }); rel.push({ a: k(0), b: k(m.length - 1), type: 'sibling' }); } }
  return { id, name: '', summary: `${kind} (random)`, members: m, relations: rel };
}

export function build(arch: Archetype, seed: number): Household {
  let why: string[] = [];
  for (let attempt = 0; attempt < 50; attempt++) {
    const R = rng(seed * 1009 + attempt), last = R.pick(FAMILY), kids = arch.members.some(x => !adult(x.age));
    const sims: HSim[] = arch.members.map((mm, i) => {
      const gender = mm.gender ?? R.pick(['Male', 'Female'] as const), first = R.pick(gender === 'Male' ? MALE : FEMALE);
      const traits = pickTraits(R, mm.age, mm.prefer);
      rebalanceSim(R, mm.age, traits, mm.prefer ?? []);
      const veg = traits.includes('Vegetarian'), expect = expectOf(traits);
      const s: HSim = { key: `m${i}`, first, last: mm.last ?? last, age: mm.age, gender, traits, wish: adult(mm.age) ? pickWish(R, traits) : null,
        food: R.pick(veg ? VEG_FOOD : FOOD), music: R.pick(MUSIC), color: R.pick(COLORS), zodiac: R.pick(ZODIAC), voice: R.pick(['A', 'B', 'C']),
        storyPersona: storyPersona(mm.age, traits, kids), persona: '', expect };
      s.persona = personaText(first, s);
      return s;
    });
    // first names unique in the household (the report keys on names)
    sims.forEach((s, i) => { while (sims.slice(0, i).some(o => o.first === s.first)) s.first = R.pick(s.gender === 'Male' ? MALE : FEMALE); s.persona = personaText(s.first, s); });
    const all = sims.flatMap(s => s.traits), up = all.filter(t => valence(t) > 0).length, down = all.filter(t => valence(t) < 0).length;
    const h: Household = { id: arch.id, name: arch.name || `${last} Household`, archetype: arch.name ? arch.id : null, seed, structure: arch.summary.endsWith('(random)') ? arch.summary.replace(' (random)', '') : 'archetype',
      summary: arch.summary, funds: startingFunds(sims), balance: { up, down, neutral: all.length - up - down }, sims, relations: arch.relations };
    why = lintHousehold(h);
    if (!why.length) return h;
  }
  throw new Error(`${arch.id}: no valid household in 50 attempts (seed ${seed}): ${why.join("; ")}`);
}
export const randomHousehold = (seed: number, id = `random-${seed}`) => build(randomArchetype(rng(seed), id), seed);

// ---- lint (docs/179 R-HH-1..3, R-LIFE-2; traits.csv; the balance rule)
export function lintHousehold(h: Household): string[] {
  const e: string[] = [], T = data().traits, idx = AGES as readonly string[];
  const humans = h.sims.length, pets = h.pets?.length ?? 0;
  if (humans < 2 || humans > 8) e.push(`${humans} humans (2..8, R-HH-1)`);
  if (pets > 6 || humans + pets > 10) e.push(`${pets} pets / ${humans + pets} total (6 / 10, R-HH-1)`);
  for (const p of h.pets ?? []) if (!['Child', 'Adult', 'Elder'].includes(p.age)) e.push(`pet ${p.species} ${p.age} (pets: Child, Adult, Elder)`);
  if (!h.sims.some(s => adult(s.age))) e.push('no Young Adult or older');
  const keys = new Map(h.sims.map(s => [s.key, s]));
  for (const s of h.sims) {
    const who = `${s.first} (${s.age})`;
    if (!idx.includes(s.age)) { e.push(`${who}: age not made in CAS (Toddler..Elder, R-HH-3)`); continue; }
    if (s.traits.length !== SLOTS[s.age]) e.push(`${who}: ${s.traits.length} traits, CAS has ${SLOTS[s.age]}`);
    if (new Set(s.traits).size !== s.traits.length) e.push(`${who}: duplicate trait`);
    s.traits.forEach((id, i) => {
      const t = T.get(id);
      if (!t || t.cas !== 'human') return e.push(`${who}: ${id} is not a CAS human trait`);
      if (!t.ages.includes(CODE[s.age])) e.push(`${who}: ${id} not for ${s.age}`);
      if (!t.plus || !t.minus) e.push(`${who}: ${id} has no positive or negative consequence in traits.csv`);
      for (const o of s.traits.slice(0, i)) if (T.get(o) && conflict(T.get(o)!, t)) e.push(`${who}: ${o} conflicts with ${id}`);
    });
    if (adult(s.age) && !s.wish) e.push(`${who}: no lifetime wish`);
    if (!adult(s.age) && s.wish) e.push(`${who}: lifetime wish before YA`);
    if (s.wish && !validWishes(s.traits).some(w => w.id === s.wish!.id)) e.push(`${who}: lifetime wish ${s.wish.id} not valid for the traits`);
    if (!FOOD.includes(s.food) || !MUSIC.includes(s.music) || !COLORS.includes(s.color) || !ZODIAC.includes(s.zodiac) || !['A', 'B', 'C'].includes(s.voice)) e.push(`${who}: favourite/zodiac/voice not a game value`);
    if (s.traits.includes('Vegetarian') && !VEG_FOOD.includes(s.food)) e.push(`${who}: vegetarian with a meat favourite`);
    if (s.traits.length >= 3 && (!s.traits.some(t => valence(t) > 0) || !s.traits.some(t => valence(t) < 0))) e.push(`${who}: needs an upside and a downside trait`);
  }
  const rank = (s: HSim) => idx.indexOf(s.age);
  for (const r of h.relations) {
    const a = keys.get(r.a), b = keys.get(r.b);
    if (!a || !b) { e.push(`relation ${r.a}-${r.b}: unknown Sim`); continue; }
    if (r.type === 'parent' && (!adult(a.age) || rank(a) <= rank(b))) e.push(`${a.first} cannot be ${b.first}'s parent (${a.age} -> ${b.age})`);
    if ((r.type === 'spouse' || r.type === 'partner' || r.type === 'roommate') && (!adult(a.age) || !adult(b.age))) e.push(`${r.type} ${a.first}-${b.first}: YA+ only (R-LIFE-2)`);
  }
  for (const s of h.sims.filter(x => !adult(x.age)))
    if (!h.relations.some(r => (r.type === 'parent' || r.type === 'sibling') && (r.b === s.key || r.a === s.key))) e.push(`${s.first} (${s.age}): no parent or sibling in the household`);
  const all = h.sims.flatMap(s => s.traits), up = all.filter(t => valence(t) > 0).length, down = all.filter(t => valence(t) < 0).length;
  if (up + down && (down / (up + down) < 0.2 || down / (up + down) > 0.65)) e.push(`balance: ${down} of ${up + down} scored traits are downsides (20..65 %)`);
  if (h.funds !== startingFunds(h.sims)) e.push(`funds ${h.funds} != CAS starting funds ${startingFunds(h.sims)}`);
  return e;
}

export const loadHouseholds = (dir = HH_DIR): Household[] => existsSync(dir) ? readdirSync(dir).filter(f => f.endsWith('.json')).sort().map(f => JSON.parse(readFileSync(join(dir, f), 'utf8'))) : [];

// make_household args (mods/ts3agent HouseholdHandlers.cs)
export const agentArgs = (h: Household) => ({ name: h.name, relations: h.relations,
  sims: h.sims.map(s => ({ key: s.key, first: s.first, last: s.last, age: s.age, gender: s.gender, traits: s.traits, wish: s.wish?.id ?? null, food: s.food, music: s.music, color: s.color, zodiac: s.zodiac, voice: s.voice })) });

// ---- story run hook (story-fuzzer.ts loadStory, config household: generated | <id>): CAS + move-in, then the game's switch
// to it; one `household` line with every Sim's traits, persona and expected consequences (story-report reads it)
// Fails loudly: any failure throws `household creation failed: <reason>` (loadStory stops the job; never the save's household
// silently, story-days-gen 2026-10-23 lost 25 min that way). Move-in the player's way (docs/181 §3.1): make_household picks a
// house the funds buy furnished or unfurnished, then Buy mode buys its `buy` list (essentials) with household funds, placed on
// the lot. `generated`: a "funds:" or "fit:" (no affordable house has room for the essentials) refusal tries the next smaller
// household; a named household fails. `funds` (story.householdFunds): the familyfunds cheat's effect, set after creation and
// before the lot pick; the household line carries realism cheat (story-fuzzer parseConfig allows it only with realism cheat).
type IO = { ag: (cmd: string, args?: any, timeoutMs?: number) => Promise<any>; log: (l: any) => void; print?: (l: string) => void; now: () => number; sleep?: (ms: number) => Promise<void> };
// lot (story.householdLot): the move-in lot id (list_lots `id`), e.g. a furnished mansion placed for story runs; else the game's pick
export async function createHousehold(io: IO, spec: string, seed: number, funds?: number, lot?: string): Promise<Record<string, string>> {
  const all = loadHouseholds(), first = spec === 'generated' ? rng(seed).pick(all) : all.find(x => x.id === spec);
  const tries = !first ? [] : spec === 'generated' ? [first, ...all.filter(x => x.sims.length < first.sims.length).sort((a, b) => b.sims.length - a.sims.length || a.id.localeCompare(b.id))] : [first];
  const t = io.now();
  const realism = funds !== undefined ? 'cheat' : 'natural';
  const line = (h: Household | undefined, ok: boolean, x: object) => io.log({ kind: 'household', t, ok, realism, ...(funds !== undefined && { funds_cheat: funds }), id: h?.id, name: h?.name, archetype: h?.archetype ?? null, summary: h?.summary,
    sims: h?.sims.map(s => ({ name: `${s.first} ${s.last}`, age: s.age, traits: s.traits, wish: s.wish?.name ?? null, storyPersona: s.storyPersona, persona: s.persona, expect: s.expect })), ...x });
  const fail = (h: Household | undefined, why: string, x: object = {}): never => { line(h, false, { error: why, ...x }); throw new Error(`household creation failed: ${why}`); };
  if (!first) fail(undefined, `no household '${spec}' in stories/households`);
  let h: Household | undefined, made: any;
  for (const c of tries) {
    h = c;
    made = await io.ag('make_household', { ...agentArgs(c), ...(funds !== undefined && { funds }), ...(lot && { lot }) }, 60000);
    if (made?.ok) break;
    const err = String(made?.error ?? 'no answer').slice(0, 300);
    if (!/^(funds|fit):/.test(err) || c === tries.at(-1)) fail(c, `${c.id}: ${err}`);
    line(c, false, { error: err, retry: 'smaller household' });
    io.print?.(`story: household ${c.id} (${c.sims.length} Sims) ${err.startsWith('fit:') ? 'does not fit an affordable house' : 'not affordable'}, trying a smaller one`);
  }
  const m = made.result, hh = h!;
  const sw = await io.ag('switch_household', { household: m.household_id }, 60000);
  if (!sw?.ok) fail(hh, `switch: ${String(sw?.error).slice(0, 200)}`, { household_id: m.household_id, lot: m.lot });
  // essentials in Buy mode before any story (Buy needs Live first: the switch may still be settling; 30 s cap)
  const bought: any[] = [];
  if (m.buy?.length) {
    for (let i = 0; (await io.ag('game_state'))?.result?.sub_state !== 'LiveModeState'; i++) { if (i >= 30) fail(hh, 'not back in Live after the switch (30 s)'); await io.sleep?.(1000); }
    const open = await io.ag('buy_open', {}, 200000);
    if (!open?.ok) fail(hh, `buy_open: ${String(open?.error).slice(0, 200)}`);
    try {
      // a product that fails (not creatable, wrong class, no money) -> the next cheapest of its kind (`alternates`); the household
      // fails only when none of the kind can be bought
      for (const b of m.buy) {
        const errs: string[] = [];
        for (const c of [b, ...(b.alternates ?? [])]) {
          const r = await io.ag('buy_object', { product: c.product, on_lot: true, kind: b.need }, 30000);
          if (r?.ok) { bought.push({ need: b.need, name: c.name, price: c.price, funds: r.result?.funds, ...(errs.length ? { failed: [...errs] } : {}) }); break; }
          errs.push(`${c.name}, ${c.price}: ${String(r?.error).slice(0, 160)}`);
        }
        if (errs.length === 1 + (b.alternates?.length ?? 0)) fail(hh, `buy ${b.need} (${errs.join('; ')})`, { lot: m.lot, bought });
      }
    } finally { await io.ag('buy_close', {}, 200000); }
    // stories start in Live: buy_close returns before the game leaves Buy mode (2026-10-24 world_census failed in BuyModeState)
    for (let i = 0; (await io.ag('game_state'))?.result?.sub_state !== 'LiveModeState'; i++) { if (i >= 30) fail(hh, 'not back in Live after Buy mode (30 s)'); await io.sleep?.(1000); }
  }
  line(hh, true, { household_id: m.household_id, cas_funds: m.cas_funds, fit_skipped: m.fit_skipped, lot: m.lot, lot_cost: m.lot_cost, furnished: m.furnished ?? true, funds: m.starting_funds, buy_cost: m.buy_cost ?? 0, bought, funds_after: bought.at(-1)?.funds ?? null });
  io.print?.(`story: household ${hh.name} (${hh.id}) made on ${m.lot} (${m.furnished === false ? 'unfurnished' : 'furnished'} ${m.lot_cost}${bought.length ? `, bought ${bought.map(b => b.need).join(', ')}` : ''}${funds !== undefined ? `, funds ${funds} by the familyfunds cheat (CAS ${m.cas_funds ?? '?'})` : ''}), now active`);
  return Object.fromEntries(hh.sims.map(s => [`${s.first} ${s.last}`, s.storyPersona]));
}

// ---- report (story-report.ts): each Sim's traits and persona next to its stories, rescues, deaths and hitches
export function householdReport(lines: any[], events: { ms: number; steps: { actor?: string; story?: string; run?: number }[] }[] = []) {
  const h = lines.filter(x => x.kind === 'household').at(-1);
  if (!h?.sims) return '';
  const steps = lines.filter(x => !x.kind && x.story), guards = lines.filter(x => x.kind === 'guard'), deaths = lines.filter(x => x.kind === 'death');
  const L = ['### Household and traits (docs/181)', '', `${h.name} (${h.archetype ?? 'random'}${h.ok ? '' : `, NOT MADE: ${h.error}`}): ${h.summary ?? ''}`, '',
    '| Sim | age | traits | lifetime wish | stories | rescues | deaths | hitches >= 300 ms | expected upside / downside |', '|---|---|---|---|---|---|---|---|---|'];
  for (const s of h.sims) {
    const mine = (a?: string) => a === s.name || a === s.name.split(' ')[0];
    const runs = new Set(steps.filter(x => mine(x.actor)).map(x => `${x.story}#${x.run}`)).size, hit = events.filter(e => e.steps.some(x => mine(x.actor))).length;
    L.push(`| ${s.name} | ${s.age} | ${s.traits.join(', ')} | ${s.wish ?? '-'} | ${runs} | ${guards.filter(x => mine(x.sim)).length} | ${deaths.filter(x => mine(x.sim)).length} | ${hit} | ${s.expect.map((e: any) => `${e.trait}: + ${e.plus} / - ${e.minus}`).join('<br>')} |`);
  }
  return L.join('\n');
}

// ---- CLI
async function hookCheck() {
  // createHousehold against a stub agent: make_household with the file's args, then the switch, one household line
  const calls: any[] = [], logged: any[] = [];
  const ag = async (cmd: string, args?: any) => { calls.push({ cmd, args }); return cmd === 'make_household' ? { ok: true, result: { household_id: '42', lot: 'Lot A', lot_cost: 20000, starting_funds: 25000 } } : { ok: true, result: {} }; };
  const p = await createHousehold({ ag, log: l => logged.push(l), now: () => 0 }, 'lonely-genius', 1);
  assert.deepEqual(calls.map(c => c.cmd), ['make_household', 'switch_household']);
  assert.equal(calls[1].args.household, '42');
  assert.equal(calls[0].args.sims.length, 2);
  assert.ok(logged[0].kind === 'household' && logged[0].ok && logged[0].sims.length === 2 && p && Object.keys(p).length === 2);
  // a refused call throws "household creation failed: <reason>" (the job stops), never null
  await assert.rejects(createHousehold({ ag: async () => ({ ok: false, error: 'not available: BuildModeState (LiveModeState only)' }), log: l => logged.push(l), now: () => 0 }, 'lonely-genius', 1),
    /^Error: household creation failed: lonely-genius: not available: BuildMode/);
  assert.ok(logged.at(-1).ok === false && /BuildMode/.test(logged.at(-1).error));
  // the first live run's case: full-house unaffordable furnished -> named household fails clearly
  const poor = async (cmd: string) => cmd === 'make_household' ? { ok: false, error: 'funds: 26000 simoleons buy no vacant house with the essentials for 8 Sims' } : { ok: true, result: {} };
  await assert.rejects(createHousehold({ ag: poor, log: () => {}, now: () => 0 }, 'full-house', 1), /household creation failed: full-house: funds: 26000/);
  // generated: a funds refusal tries the next smaller household; unfurnished move-in buys the essentials in Buy mode
  const sizes: number[] = [], buys: any[] = [];
  const shop = async (cmd: string, args?: any) => {
    if (cmd === 'make_household') { sizes.push(args.sims.length); return sizes.length === 1 ? { ok: false, error: 'funds: 1 simoleons buy no vacant house' } : { ok: true, result: { household_id: '7', lot: 'Shack', lot_cost: 9000, furnished: false, starting_funds: 30000, buy_cost: 900,
      buy: [{ need: 'bed_double', product: 'P1', name: 'Bed', price: 500 }, { need: 'fridge', product: 'P2', name: 'Fridge', price: 400 }] } }; }
    if (cmd === 'game_state') return { ok: true, result: { sub_state: 'LiveModeState' } };
    if (cmd === 'buy_object') { buys.push(args); return { ok: true, result: { funds: 21000 - buys.length } }; }
    return { ok: true, result: {} };
  };
  const shopLog: any[] = [], gen = await createHousehold({ ag: shop, log: l => shopLog.push(l), now: () => 0 }, 'generated', 3);
  assert.ok(sizes.length === 2 && sizes[1] < sizes[0], `smaller household tried: ${sizes}`);
  assert.deepEqual(buys, [{ product: 'P1', on_lot: true, kind: 'bed_double' }, { product: 'P2', on_lot: true, kind: 'fridge' }]);
  // chain156: a bad first product (not creatable) -> the next cheapest of its kind, logged with the failure
  const tried: string[] = [], altLog: any[] = [];
  await createHousehold({ log: l => altLog.push(l), now: () => 0, ag: async (cmd: string, args?: any) => {
    if (cmd === 'make_household') return { ok: true, result: { household_id: '7', lot: 'Shack', furnished: false, buy: [{ need: 'crib', product: 'S1', name: 'Stroller', price: 50, alternates: [{ product: 'C1', name: 'Crib', price: 150 }] }] } };
    if (cmd === 'buy_object') { tried.push(args.product); return args.product === 'S1' ? { ok: false, error: 'the game could not create Stroller: NullReferenceException (ObjectManagerFailure)' } : { ok: true, result: { funds: 100 } }; }
    return shop(cmd, args);
  } }, 'lonely-genius', 1);
  assert.deepEqual(tried, ['S1', 'C1']);
  assert.ok(altLog.at(-1).ok && altLog.at(-1).bought[0].name === 'Crib' && /Stroller/.test(altLog.at(-1).bought[0].failed[0]), 'alternate bought, failure logged');
  assert.ok(gen && shopLog.at(-1).ok && shopLog.at(-1).furnished === false && shopLog.at(-1).bought.length === 2 && shopLog[0].retry, 'household line with the shopping');
  // a failed buy closes Buy mode and fails the creation
  const closed: string[] = [];
  await assert.rejects(createHousehold({ ag: async (cmd: string, args?: any) => { closed.push(cmd); return cmd === 'buy_object' ? { ok: false, error: 'no free spot on the home lot for Bed' } : shop(cmd === 'make_household' ? 'x' : cmd, args).then(r => cmd === 'make_household' ? { ok: true, result: { household_id: '7', lot: 'Shack', furnished: false, buy: [{ need: 'bed_single', product: 'P3', name: 'Bed', price: 300 }] } } : r); }, log: () => {}, now: () => 0 }, 'lonely-genius', 1),
    /household creation failed: buy bed_single/);
  assert.equal(closed.at(-1), 'buy_close');
  // chain157: no affordable house fits the essentials ("fit:") -> generated tries a smaller household, like "funds:"
  const fitSizes: number[] = [];
  await createHousehold({ log: () => {}, now: () => 0, ag: async (cmd: string, args?: any) => {
    if (cmd === 'make_household') { fitSizes.push(args.sims.length); if (fitSizes.length === 1) return { ok: false, error: 'fit: no affordable house has room for the essentials (Shack: bed_double (Bed))' }; }
    return shop(cmd, args);
  } }, 'generated', 3);
  assert.ok(fitSizes.length === 2 && fitSizes[1] < fitSizes[0], `fit refusal -> smaller household: ${fitSizes}`);
  // story.householdFunds: make_household gets funds (the familyfunds cheat), the household line carries realism cheat
  const fundCalls: any[] = [], fundLog: any[] = [];
  await createHousehold({ log: l => fundLog.push(l), now: () => 0, ag: async (cmd: string, args?: any) => { fundCalls.push({ cmd, args }); return cmd === 'make_household' ? { ok: true, result: { household_id: '9', lot: 'Villa', cas_funds: 20000, starting_funds: 30000 } } : { ok: true, result: {} }; } }, 'lonely-genius', 1, 30000);
  assert.equal(fundCalls[0].args.funds, 30000);
  assert.ok(fundLog.at(-1).ok && fundLog.at(-1).realism === 'cheat' && fundLog.at(-1).funds_cheat === 30000 && fundLog.at(-1).cas_funds === 20000);
  assert.ok(!('funds' in calls[0].args) && logged[0].realism === 'natural', 'no override: CAS funds, natural');
  // story.householdLot: make_household gets the lot id; without it the game picks (no lot arg)
  const lotCalls: any[] = [];
  await createHousehold({ log: () => {}, now: () => 0, ag: async (cmd: string, args?: any) => { if (cmd === 'make_household') lotCalls.push(args); return shop(cmd, args); } }, 'lonely-genius', 1, undefined, '123');
  assert.ok(lotCalls[0].lot === '123' && !('lot' in calls[0].args), 'householdLot reaches make_household only when set');
  // agent source (no game here): crib only from Kids > Furniture / Beds by class, never Kids > Misc; in-house placement; lot fallback
  const src = (f: string) => readFileSync(join(import.meta.dirname, '..', '..', 'mods', 'ts3agent', 'src', 'plugin', 'Handlers', f), 'utf8');
  const hs = src('HouseholdHandlers.cs'), kinds = /static string\[\] KindsFor[\s\S]*?\n {8}\}/.exec(hs)?.[0] ?? '';
  assert.match(kinds, /kBuySubCategoryMiscellaneousKids\) != 0\) return null;/, 'Kids > Misc never an essential (strollers)');
  assert.match(kinds, /kBuyCategoryKids[\s\S]*kBuySubCategoryFurniture\) != 0 \? kCribKinds/, 'cribs from Kids > Furniture');
  assert.match(hs, /Array\.IndexOf\(KindsFor\(p\), k\) < 0\) continue;/, 'the created class must be a kind the product data allows');
  assert.match(hs, /if \(!open\) continue;/, 'no create once a group is full');
  assert.match(hs, /if \(!p\.Furnished\)[\s\S]{0,200}StaysAfterEvict/, 'fit test ignores what MakeLotUnfurnished removes');
  assert.match(hs, /foreach \(MoveInPlan c in PickMoveIn\(funds, need\)\)[\s\S]{0,200}FitFails\(c\)/, 'next affordable lot when the house has no room');
  assert.match(hs, /if \(o is ICrib\) cribs\+\+;[\s\S]{0,200}have\[k\] = true/, 'what the lot already has is counted by class');
  assert.match(src('ControlHandlers.cs'), /if \(onLot && !PlaceInHouse\(go, home, null\)\)/, 'buy_object on_lot places inside the house');
}

function selfCheck() {
  const { traits, wishes } = data();
  const cas = [...traits.values()].filter(t => t.cas === 'human');
  assert.ok(cas.length >= 90, `CAS human traits ${cas.length}`);
  assert.deepEqual(cas.filter(t => !t.plus || !t.minus).map(t => t.id), [], 'every CAS trait has a positive and a negative consequence');
  assert.ok(conflict(traits.get('Neat')!, traits.get('Slob')!) && conflict(traits.get('Good')!, traits.get('Evil')!) && conflict(traits.get('Athletic')!, traits.get('CouchPotato')!), 'known conflicts');
  assert.ok(!conflict(traits.get('Genius')!, traits.get('Loner')!), 'no false conflict');
  assert.ok(!casFor('Toddler').some(t => t.id === 'Kleptomaniac') && casFor('Toddler').some(t => t.id === 'Clumsy'), 'toddler pool');
  assert.ok(wishes.length >= 80 && validWishes(['Kleptomaniac']).some(w => /criminal \(Thief/.test(w.name)) && !validWishes(['Good', 'Kleptomaniac']).some(w => /Thief/.test(w.name)), 'wish required/excluded');
  assert.equal(startingFunds([{ age: 'Adult' }, { age: 'Adult' }, { age: 'Child' }]), 27000);
  // every archetype and 40 random seeds build lint-clean; same seed = same household
  for (const a of ARCHETYPES) { const h = build(a, 1); assert.deepEqual(lintHousehold(h), [], a.id); assert.equal(h.archetype, a.id); }
  const sizes = new Set<number>();
  for (let s = 1; s <= 40; s++) { const h = randomHousehold(s); assert.deepEqual(lintHousehold(h), [], `random ${s}`); sizes.add(h.sims.length); }
  assert.ok(sizes.size >= 4 && Math.max(...sizes) <= 8 && Math.min(...sizes) >= 2, `sizes ${[...sizes]}`);
  assert.deepEqual(randomHousehold(7), randomHousehold(7), 'seeded');
  // the lint catches what CAS forbids
  const base = build(ARCHETYPES[1], 1), bad = (f: (h: Household) => void) => { const h = structuredClone(base); f(h); return lintHousehold(h).join('; '); };
  assert.match(bad(h => { h.sims[0].traits[1] = 'Slob'; h.sims[0].traits[0] = 'Neat'; }), /conflicts/);
  assert.match(bad(h => { h.sims[3].traits.push('Genius'); }), /4 traits, CAS has 3/);
  assert.match(bad(h => { h.sims[3].age = 'Baby'; }), /not made in CAS/);
  assert.match(bad(h => { h.sims[2].wish = { id: 1, name: 'x' }; }), /before YA/);
  assert.match(bad(h => { h.sims.push(...structuredClone(h.sims), ...structuredClone(h.sims).slice(0, 1)); }), /9 humans/);
  assert.match(bad(h => { h.pets = [{ species: 'Dog', age: 'Toddler' }]; }), /pets: Child, Adult, Elder/);
  assert.match(bad(h => { h.relations.push({ a: 'm2', b: 'm3', type: 'spouse' }); }), /YA\+ only/);
  assert.match(bad(h => { for (const s of h.sims) s.traits = s.traits.map(() => ''); }), /not a CAS human trait/);
  const saints = bad(h => { h.sims[0].traits = ['Good', 'Friendly', 'Ambitious', 'Genius', 'Neat']; h.sims[0].wish = pickWish(rng(1), h.sims[0].traits); });
  assert.match(saints, /upside and a downside/);
  assert.deepEqual(agentArgs(base).sims.map(s => s.traits.length), [5, 5, 4, 3]);
  // the report hook: traits next to stories, rescues and hitches
  const lines = [{ kind: 'household', ok: true, name: 'X', archetype: 'x', summary: 's', sims: [{ name: 'Ada Brook', age: 'Adult', traits: ['Kleptomaniac'], wish: null, expect: [{ trait: 'Kleptomaniac', plus: 'p', minus: 'm' }] }] },
    { story: 'social-chat', run: 1, actor: 'Ada Brook' }, { kind: 'guard', sim: 'Ada Brook' }];
  const rep = householdReport(lines, [{ ms: 500, steps: [{ actor: 'Ada Brook' }] }]);
  assert.match(rep, /\| Ada Brook \| Adult \| Kleptomaniac \| - \| 1 \| 1 \| 0 \| 1 \| Kleptomaniac: \+ p \/ - m \|/);
  // the committed households are lint-clean
  for (const h of loadHouseholds()) assert.deepEqual(lintHousehold(h), [], h.id);
  console.log(`story-households self-check ok (${cas.length} CAS traits, ${wishes.length} wishes, ${ARCHETYPES.length} archetypes + 40 random, ${loadHouseholds().length} files)`);
}

if (process.argv[1]?.endsWith('story-households.ts')) {
  const arg = (k: string) => { const i = process.argv.indexOf(k); return i > 0 ? process.argv[i + 1] : undefined; };
  if (process.argv.includes('--self-check')) { selfCheck(); await hookCheck(); console.log('story-households hook check ok'); }
  else if (process.argv.includes('--write')) {
    const seed = Number(arg('--seed') ?? 1), n = Number(arg('--random') ?? 6);
    mkdirSync(HH_DIR, { recursive: true });
    const hs = [...ARCHETYPES.map(a => build(a, seed)), ...Array.from({ length: n }, (_, i) => randomHousehold(seed * 100 + i + 1))];
    for (const h of hs) writeFileSync(join(HH_DIR, `${h.id}.json`), JSON.stringify(h, null, 2) + '\n');
    console.log(hs.map(h => `${h.id}: ${h.name}, ${h.sims.length} Sims, ${h.balance.up} up / ${h.balance.down} down, ${h.funds} simoleons`).join('\n'));
  } else if (process.argv.includes('--lint')) {
    const bad = loadHouseholds().flatMap(h => lintHousehold(h).map(x => `${h.id}: ${x}`));
    console.log(bad.join('\n') || `households ok (${loadHouseholds().length})`);
    if (bad.length) process.exit(1);
  } else if (arg('--show')) {
    const h = loadHouseholds().find(x => x.id === arg('--show'));
    if (!h) { console.error('no such household'); process.exit(1); }
    console.log(`${h.name}: ${h.summary}\n` + h.sims.map(s => `- ${s.persona} Wish: ${s.wish?.name ?? '-'}. Story persona: ${s.storyPersona}.`).join('\n'));
  } else console.log('usage: --write [--seed N --random K] | --lint | --show <id> | --self-check');
}

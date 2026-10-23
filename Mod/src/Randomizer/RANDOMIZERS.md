#SMO Randomizer — How the Randomizers Work

This document explains every randomizer in `src / Randomizer /`: what it does, the options that control it, the algorithm it uses, and how it relates to the others.It is written to be useful to both humans and AI assistants working on the code.

    > **Scope.**This covers the* generation* logic(how a seed becomes a set of > placements / remaps).The runtime hooks that apply those decisions in - game live in > the `* Hooks.cpp` / `* Overlay.cpp` files and are only summarized here.

                                                                                                                                                                                           -- -

                                                                                                                                                                                           ##1. Architecture at a glance

                                                                                                                                                                                           The randomizer runs** entirely on
        - device**.There is no pre - generated spoiler file shipped with a seed;
instead, the game rebuilds every table from the seed at runtime.This means :

    -Generation must be** deterministic**(same seed + same options ⇒ same result)
        .
        - Generation must be** cheap enough** to run on the Switch when settings change.

          The single entry point is `Randomizer::reinit()` in[Randomizer.cpp](Randomizer.cpp #L660)
              .It rebuilds each sub
        - randomizer in a fixed order whenever the seed
    or any option changes :

``` reinit()
    : KingdomRando(if Kingdom Rando on) RequirementRando(always) PaintingRando(always — identity map when Painting Rando off) CaptureRando(if Capture Rando on) EnemySwapRando(always — identity when Enemy Swap off) AbilityRando(if Locked Abilities on) EntranceRando(if Loading Zone Rando on) MoonRando(if Moon Rando on)   ← allocation pre - pass + logic solver SkyboxRando
, MusicRando
, SphinxRando SpoilerLog(if Moon Rando on)
```

    The top
    - level order initializes the shared systems
, but * *Moon Rando is the orchestrator * *and owns the final per - attempt dependency order.Its allocation pre - pass runs before the Capture
, Ability
, Painting
, hint
, and solver consumers;
those tables are then rebuilt from the assigned layout and the current attempt so
the solver can prove a seed is beatable. The detailed order is documented in
§3.5 and the dependency map in §11.

Each sub-randomizer caches its last inputs (`sLastSeed`, plus relevant option
flags) and early-returns if nothing changed, so toggling an unrelated option does
not pay to rebuild everything.

---

## 2. The RNG model

All randomizers share one tiny, portable PRNG so results are reproducible across
host tools and the console. See [Rng.h](../../include/Randomizer/Rng.h).

- **Generator:** a 32-bit Linear Congruential Generator
  `state = state * 1664525 + 1013904223`.
- **Streams:** each randomizer draws from an independent stream
  (`RngStream::Moon`, `Entrance`, `Capture`, …). A stream is just the seed XORed
  with a per-stream salt: `makeRng(seed, stream) = Rng(seed ^ salt(stream))`.
  Distinct salts keep the streams uncorrelated, so changing how many random
  numbers one randomizer consumes does not shift another randomizer's output.
- **Sub-streams:** some passes derive a further-salted seed for an independent
  sub-sequence (e.g. Painting's cloud-link order uses `seed ^ 0xC10D11A6`,
  Entrance retries historically used `seed ^ attempt * 0x9E3779B9`). This isolates
  a sub-decision from the variable number of draws made elsewhere in the same
  build.
- **Moon Allocation:** the allocation pass uses its own `RngStream::MoonAllocation`
  stream (salt `0x4D416C6C`, "MAll"). Its target/tie order, carrier reservations,
  remaining assignments, and profile shaping derive independent sub-streams from
  `seed ^ 0x54415247` ("TARG"), `seed ^ 0x43415252` ("CARR"), and
  `seed ^ 0x4153534E` ("ASSN"), plus `seed ^ 0x50524F46` ("PROF"). The kingdom
  assignment is therefore reproducible
  from seed and settings;
it does not depend on a generation - attempt number.

                                     A
            * *host
        - side Python mirror * *of the whole logic graph
    and solver lives in
            [logic / randomizer_logic.py](../../ logic / randomizer_logic.py)
                .It re
        - implements the
            same LCG,
    the same assumed - fill algorithm, and the same data model so seeds can be analyzed and tested off - console(`logic / test_randomizer_logic.py`).When you change generation behavior in C++, keep the Python mirror in sync.

                                                                                                                                                                                                 -- -

                                                                                                                                                                                                 ##3. Moon Rando(the core)

    * *Files : **[MoonRando.cpp](MoonRando.cpp),
    [MoonHooks.cpp](MoonHooks.cpp),
    [MoonRequirement.cpp](MoonRequirement.cpp)

    * *What it does.**Shuffles which Power Moon(the* item*) you receive at each moon *location *.This is the heart of the randomizer;
every other randomizer either
        feeds it
    or decorates it.

                    **Options
                .**-*Moon Rando on
            / off * — master switch.
        - *Logic Rando on / off *(`isLogicRandoEnabled`) — whether to use the graph - aware solver(below)
    or a plain shuffle.
        - *Logic Difficulty * — Casual / Advanced / Expert / Glitched.Controls which route edges the solver is willing to traverse(harder tricks unlock more edges).The levels are strictly cumulative,
    so Glitched is Expert plus the glitch - only options.
    - *Abilities &Captures *(Unlock Pacing) — how early captures and abilities enter
       the critical path.See §3.3.
    - *Seed Openness * — how many options the seed offers at once,
    how long door
    chains get,
    and what kind of door the entrance fill prefers to wire.See §3.3.
    - *Seed Difficulty * — path length,
    how far an unlock sits from the gate it opens,
    and how much of the allowed harder movement the completion path actually
        requires
.This absorbed the retired *Prioritize Difficult Movement *checkbox(its behavior is now the** Hard** level).See §3.3. - *Moon Difficulty * and *Moon Requirement * — see Requirement Rando(§5); they
  shape how many moons each kingdom gates on.
- *Moon Kingdom Requirements*, *World Peace Requirement*,
  *Cloud-moons-unlock-paintings* — extra logic constraints / pool toggles.

### 3.1 Two modes

**No-logic fill** (Logic Rando off): the **same buffer-managed fill** runs, but every
node restriction is ignored (route-edge requirements, per-node reachability,
moon ability/capture requirements). Kingdom access, per-kingdom requirements,
scenarios and story order still apply, so the fill still verifies enough
accessible moons exist per kingdom (accounting for entrance/painting links).

**Graph-aware buffer-managed sphere placement**
(`runBufferManagedSpherePlacement`, used when Logic Rando is on): a forward-only
placement state machine that reserves enough reachable capacity for each local
moon gate while weaving captures and abilities into the route. The Python mirror
uses the same `buffer_managed_sphere_placement` architecture.

### 3.2 The buffer-managed sphere algorithm

1. Build the eligible **location pool** and **item pool** (filtered by which
   randomizers are on — e.g. capture/ability unlock moons only matter when those
   randomizers are enabled).
2. Track the first logical sphere in which every location becomes reachable.
   Spheres advance only when a placement exposes a genuinely new frontier.
3. For the current route kingdom calculate
   `buffer = compatible reachable locations - remaining required moon value`.
   A high buffer places a random item from any kingdom, a medium buffer prefers
   an immediately useful capture/ability, and a low buffer places one native
   required moon. Required items retain single-vs-multi optimization.
4. Critical-path locations use relative exponential depth weights:
   `1 << min(locationSphere - minimumSphere, 30)`. This strongly favors the
   expanding frontier without making absolute late-game sphere numbers matter.
5. If the buffer is exhausted, a capture/ability bundle must measurably expand
   reachable locations, topology, or the active story frontier. Bowser's Kingdom
   also proves and materializes the RoboBrood route before the Odyssey advances.
6. Branch tiers prove both kingdoms from the same pre-tier state. A sibling is
   not opened through the Odyssey during isolation, but remains reachable if an
   already-accessible shuffled entrance genuinely leads there. The states merge
   after both sides are satisfied.
7. After the route, every remaining capture/ability is placed forward into a
   reachable location. This pass and ordinary cleanup use even cross-kingdom
   distribution instead of depth weights, reducing clumps while keeping every
   unique unlock collectable.
8. The completed bijection is replayed for every branch order, the final Wedding
   Hall objective, and optional world-peace validation.

**The casual mask (Seed Difficulty Hard/Brutal).** At those levels, and when
Logic Difficulty is above Casual, steps 2–7 run under a *casual mask*: a requirement that offers
both a casual and a harder still-allowed alternative is evaluated as if the casual
alternative did not exist (`maskingCasualOptions` /
`hasEligibleDifficultOption` in `MoonRando.cpp`; `mask_casual_methods` /
`requirement_has_eligible_difficult_option` in `randomizer_logic.py`). Sphere
expansion additionally tracks a *difficult-token* set — tokens missing only on
harder alternatives — and orders unlock candidates so those expand first. If the
masked fill reaches a dead end (no placement, unlock, or story materialization is
possible), a **scoped casual fallback** turns the mask off just until the next
placement lands, then the strict fill resumes; the memoized unlock-search failures
are cleared on both transitions since they are policy-dependent. The mask is
**construction-only**: it is cleared before the final replay/validation in step 8,
so beatability is always proven with every allowed method and a seed is never
rejected for needing the fallback. At Standard, or on Casual/No-Logic, the mask
never engages and there is zero added cost on the hot path. Relaxed and Easy
*invert* the candidate ordering instead of masking, so the route grows through
methods the player can execute casually.

### 3.3 The three seed axes: exact behavior

Placement pacing and shape are three independent settings. Each has five levels
plus **Random**; index 2 is **Standard** on every axis and reproduces the
historical behavior exactly.

| Axis | Menu row | Levels |
|---|---|---|
| Unlock Pacing | *Abilities & Captures* | Front-Loaded / Early / Standard / Late / Back-Loaded |
| Seed Openness | *Seed Openness* | Claustrophobic / Compact / Standard / Open / Wide Open |
| Seed Difficulty | *Seed Difficulty* | Relaxed / Easy / Standard / Hard / Brutal |

These replaced the single *Seed Style* roll (Standard / Ability Rush / Focused
Route / Explorer / Mystery) and the *Prioritize Difficult Movement* checkbox.
Both are retired but still parsed, so an old save, Common config or user preset
migrates instead of breaking:

| Legacy Seed Style | Pacing | Openness | Difficulty |
|---|---|---|---|
| Standard | Standard | Standard | Standard |
| Ability Rush | Early | Standard | Standard |
| Focused Route | Standard | Compact | Easy |
| Explorer | Standard | Open | Hard |
| Mystery | Random | Random | Random |

*Prioritize Difficult Movement* was a boolean that behaved exactly like
**Seed Difficulty = Hard**, so a legacy save carrying the bit is promoted to Hard
(never demoted — a style that already resolved harder wins).

**Random.** A `random` axis resolves from the seed alone, with a per-axis salt so
the three stay independent, and consumes no generator RNG — `reinitDeterministic`
rebuilds from seed + attempt on save reload and has to land on the same levels.
This is the old Mystery mechanism (`mixSeedStyleAxis` / `mix_seed_style_axis`),
now per-axis. All arithmetic is unsigned modulo `2^32`:

```
mix(x):
    x = x + 0x9E3779B9
    x = (x ^ (x >> 16)) * 0x85EBCA6B
    x = (x ^ (x >> 13)) * 0xC2B2AE35
    return x ^ (x >> 16)

pacing    = LEVELS[mix(seed ^ 0xA341316C) % 5]
openness  = LEVELS[mix(seed ^ 0xC8013EA4) % 5]
difficulty= LEVELS[mix(seed ^ 0xAD90777D) % 5]
```

The resolved values appear in the spoiler log (`chosen (resolved)`), never in the
menu — picking Random is meant to be a surprise.

#### Unlock Pacing

Biases only a *discretionary* high-buffer placement, so no level can make a seed
unbeatable: required native moons and useful unlock bundles keep their
safety-first paths. Let `C` be the unplaced items compatible with a reachable
slot and `U ⊆ C` the items carrying a capture/ability/cloud-painting unlock.
Each level supplies a weight ratio `carrier : plain` and a carrier cap:

| Level | `carrier : plain` | Cap | Effect |
|---|---:|---|---|
| Front-Loaded | 8 : 1 | none | Most of the moveset lands in the first few spheres |
| Early | 3 : 1 | none | Roughly the old Ability Rush |
| Standard | 1 : 1 | none | The historical unweighted draw |
| Late | 1 : 3 | 1 per sphere | Long stretches on base Mario |
| Back-Loaded | 1 : 8 | 1 per 2 spheres | Most of the moveset arrives in the last third |

An item is drawn with probability `w_i / Σ w_j`. The cap is what actually
stretches the moveset out — a weight alone still lets a lucky run of draws
front-load it. Pacing also scales the expansion backlog `E` below by
`(¼, ½, 1, 2, 4)`, clamped to a minimum of 1. `E` is a threshold the backlog
has to *clear*, so the smaller number is the eager one: a front-loaded seed
quarters `E` and therefore reaches for an unlock far more often, a back-loaded
one quadruples it and almost never does.

#### Seed Openness

Owns every "shape of the world" decision, including the door graph. It absorbed
the retired *Small Chains* / *Long Chains* Loading Zones modes.

**Buffer widths.** For remaining local requirement `R`, compatible reachable
locations `A`, and `B = A - R`, each level supplies
`(H, L, E) = (high buffer, low buffer, expansion backlog)`:

| Level | `H` | `L` | `E` |
|---|---:|---:|---:|
| Claustrophobic | 12 | 8 | 12 |
| Compact | 10 | 6 | 10 |
| Standard | 10 | 3 | 6 |
| Open | 14 | 2 | 4 |
| Wide Open | 16 | 1 | 3 |

The transition is:

1. `B <= 0`: a useful unlock bundle is mandatory;
failing that, an entrance wiring that provably expands the frontier .2. `B > H`: spend the slack — either wire a door(see below) or make one discretionary placement, applying Unlock Pacing .3. Otherwise, if `B > L` or `R > E`: try a useful unlock bundle, then an entrance wiring;
if neither
    expands the frontier, place a native required moon .4. Otherwise : place a native required moon.

                                                                           **Door
            - class weights.**When the fill wires a door it classifies every candidate target and draws by these weights :

        | Target class | Claus.| Compact | Standard | Open | Wide Open | | -- - | -- - : | -- - : | -- - : | -- - : | -- - : | | Dead end(no onward door) | 8 | 5 | 3 | 2 | 1 | | Pocket(holds moons, no onward door) | 6 | 4 | 3 | 2 | 1 | | Pass - through(usable far door) | 3 | 3 | 3 | 3 | 3 | | Cross - kingdom(a home surface) | 1 | 2 | 3 | 5 | 8 |

                                                                                                                             Pass - through is deliberately flat : it is the neutral "the chain continues" outcome,
        and the chain bias below already decides how often chains keep going.Standard is `3` across the board — an even draw, exactly the unweighted behavior the algorithm had before Openness existed.

                                                                                                                                  **Chain bias
                .**The weight on drawing the next wiring source from the chain
            - exit pool rather than a fresh kingdom entrance :

        | Level | chain - exit : kingdom - entrance | | -- - | -- - | | Claustrophobic | 4 : 1 | | Compact | 3 : 1(= the retired Long Chains) | | Standard | 1 : 1(= the retired Standard) | | Open | 1 : 3(= the retired Small Chains) | | Wide Open | 1 : 4 |

        **Long chains are the *less *open shape.**With a fixed door count,
        short chains
        mean *more *chains,
        so more independent overworld entry points and more direct
        kingdom
        - to - kingdom doors.That is why Compact reproduces Long Chains and Open reproduces Small Chains,
        not the other way round.

            * *Open
        - door reserve.** `(0, 1, 2, 4, 6)` by level : the fill will not consume the last `N` reachable - but - unwired source doors on a discretionary wiring,
        so the
            player always has that many unexplored loading zones standing open.An Expand
            wiring ignores the reserve — being stuck outranks keeping options in reserve.

                **Wiring rate
            .**At high buffer the fill spends slack on a door with probability
`(2, 2, 3, 4, 5)
        / 8` by level,
        so the door graph is chosen *throughout *the run
        rather than all at once in the tail.

        ####Seed Difficulty

        Four effects,
        all from one level index.

            **C1 — sphere depth
        .**For each compatible reachable location `i`,
        let `s_i` be
        the sphere in which it first became reachable, `d_i = s_i - min_j(s_j)`,
                                                       and
`D = max_j(d_j)`.The exponent `e_i` is :

            | Level | `e_i` |
            | -- - | -- - |
            | Relaxed | `D - d_i` (prefer shallow) |
            | Easy | `(D - d_i) / 2` |
            | Standard | `d_i` (the historical `1 << relativeDepth`) |
            | Hard | `2 d_i` |
            | Brutal | `3 d_i` |

            **C2 — unlock / use separation.**When the item carries a progression unlock
        and the caller knows which kingdom's gate it is being drawn for, `e_i` gains
`(3, 1, 0, 0, 0)` if the location is in that same kingdom and `(0, 0, 0, 1, 3)` if it is not .Relaxed therefore puts the unlock next to the wall it opens;
Brutal deliberately throws it somewhere else. This is the back-and-forth dial.

The location is sampled with weight `w_i = 2 ^ min(max(e_i, 0), 30)`.

**C3 — required movement tier.** The graded successor to *Prioritize Difficult
Movement*, driving `maskingCasualOptions()` / `mask_casual_methods()`:

| Level | Behavior |
|---|---|
| Relaxed | *Inverted*: unlock candidates serving casual options are tried first |
| Easy | Mild casual preference (tie-break only) |
| Standard | Neutral — the old checkbox off |
| Hard | Mask casual alternatives during construction — the old checkbox on |
| Brutal | Mask casual, and expand through the highest enabled tier first |

No-ops entirely on Casual or No Logic (there is nothing harder to require). The
mask stays **construction-only**: it is cleared before the final
replay/validation, so beatability is always proven with every allowed method and
a seed is never rejected for needing the scoped casual fallback.

**C4 — sphere-count band.** After a successful attempt the realized fill sphere
count is compared against a band; outside it, the attempt is re-rolled. This is a
**soft** preference and must stay one — the last `kSphereBandRelaxedAttempts`
attempts stop enforcing it, so a difficulty level is a request for a shape and
never a reason to fail generation.

**C5 — entrance distance.** Multiplies each wiring candidate's class weight by
`(4, 2, 1, 1, 1)` when the target is in the source's own kingdom and
`(1, 1, 1, 2, 4)` when it is not. Composes with the Openness class weights.

### 3.4 Retries and entrance coupling

Reachability depends on several attempt-salted dependencies, not just the entrance
shuffle, so all of them are rebuilt together and solved as one attempt loop, inside
`MoonRando::buildRemapTable`:

```
for attempt in 0 .. kMaxAttempts(=32):
    KingdomRando::rebuild(seed, attempt)            # if Kingdom Rando on
    RequirementRando::rebuild(seed, attempt)
    PaintingRando::buildRemapTable(seed, attempt)
    CaptureRando::buildRemapTable(seed, attempt)     # if Capture Rando on
    EnemySwapRando::buildRemapTable(seed, attempt)   # identity when off
    AbilityRando::buildRemapTable(seed, attempt)     # if Locked Abilities on
    EntranceRando::buildRemapTable(seed, attempt)    # if Loading Zone Rando on
    if runBufferManagedSpherePlacement(seed, attempt): success;
break
```

    Each failed attempt re
    - derives every one of those tables from the same
`attempt`- salted seed and tries again,
    so a fresh entrance shuffle is only one of
        several things that change between attempts.If all attempts fail,
    the last
            attempt is used and a warning is
            logged(`Logic placement could not prove the full
                    path after N attempts`)
                .This is why a hard
        / unsatisfiable configuration prints one
        block of dependency
    - rebuild logs per attempt.

            **Restoring a save is different
        .** `Randomizer::reinitDeterministic(attempt)` ([Randomizer.cpp](Randomizer.cpp #L755)),
    called from
        [RandoSaveFile.cpp](RandoSaveFile.cpp #L642) when a save is loaded,
    re - derives only the cheap deterministic tables(Kingdom / Requirement / Painting / Capture / Ability / Skybox / Music) from the saved `entranceAttempt` value.It does * * not **rebuild Entrance or Moon : the actual entrance connections and moon placements are restored directly from the save payload(`EntranceRando::restore`,
`MoonRando::restorePlacements`) rather than recomputed by re - running the attempt loop.

                                                                                                                                                                                                                        * *Diagnosing failures.**Each attempt logs its buffer frontier,
    search totals,
    and result as `buffer - sphere attempt = ...`;
the final warning set is dumped as
`logic warning : <…>` lines.Internally these come from `addWarning()` (an
                                                                            in
                      - memory buffer reset per attempt)
    and `sFinalObjectiveNote`;
the per - attempt log is what makes the retries non - silent.Common reasons : a kingdom 's moon capacity couldn' t be proven, a capture / ability had no reachable unlock path, the final objective(Moon Kingdom) wasn't reachable, or — with Entrance Rando on — not every randomized destination could be reached from an overworld entrance.

                                                                                                                                                                                ## #3.5 Moon Allocation(global kingdom assignment)

                                                                                                                                                                                    Moon Allocation is a deterministic preprocessing pass inside Moon Rando.It assigns each eligible moon item a** credited kingdom** before role maps and the logic solver are built.Full allocation is active only when all three conditions hold :

                                                                                                                                                                                1. `Randomize Moons` is enabled .2. `Enable Moon Re
    - allocation` is enabled .3. `Randomize Moon Allocation` is enabled.

      The settings live on the** Moon Allocation** page,
    immediately after** Randomization**and before** Logic Settings**.The master is off by default.With
    the master off,
    the child controls are hidden but retain their values, Triple Moons remains independently usable, and no allocation pass is activated.These settings never enable `Randomize Moons` implicitly.

                                                                                                          ####Preprocessing order

                                                                                                          The order is part of the data contract.For a fresh seed,
    deterministic
    preparation,
    or save restoration, the moon - side work is :

    1. `precomputeMoonEntries()` resolves the logic entries to canonical ShineDB entries and builds identity lookups .2. `computeRewardMultiOverride(seed)` determines randomized Triple values,
    when
    enabled.A single moon contributes value `1`;
a triple contributes value `3`.3. `computeMoonKingdomAssignment(seed)` classifies the now - valued items, reserves mandatory carriers, satisfies floors, and assigns the remaining items .4. Capture, Ability, Painting, Moon Rock Key, Toadette, and other role builders consume the resulting assigned kingdoms .5. The placement / logic solver consumes those role maps and credited kingdoms.

                                                                                                                                                                                                                                                                                                                                      Triple values must be known before allocation because floors,
    conservation, and the capture / ability capacity rules use credited value.The physical triple slots remain a separate compatibility concern.After the allocation pass is complete,
    the per - attempt route, requirement, entrance, role, and solver work may be rebuilt with the attempt salt;
the allocation itself is not attempt - dependent.

                                       ####Three kingdom identities

        - **Physical / home kingdom * *is where the authored location
    and reward originate.Story and hint - art behavior,
    scenario progression, and Moon Coin classification use this identity.- **Credited / assigned kingdom **is where the reward counts after allocation.A moon is allowed to stay home;
allocation means the whole eligible pool can
    participate,
    not that every item must move.Kingdom requirements, credited totals,
    and downstream role maps use this identity.
    - **Role - carrier eligibility **asks whether an entry can serve a downstream role.Capture and Ability use eligible carrier value; Painting uses distinct eligible
  Cloud entries. It is not interchangeable with a raw kingdom total. Carriers
  require a nonzero UID, a valid object ID, the required assigned kingdom, and no
  Toadette status: Capture carriers target Mushroom, Ability carriers target Dark
  or Darker, and Painting carriers target Cloud.

Invalid/non-pool entries, Story moons, Hint-art moons, and Toadette achievement
moons remain at home. The total credited value is conserved at `819` with current
game data.

#### Ordinary credited-value floors

Easy keeps the following distributed-allocation safety floors. For an ordinary
route kingdom the floor is the vanilla route requirement plus five Medium variance
plus five allocation safety. Vanilla and harder allocation profiles instead use
the base moon requirement as their ordinary floor so their excess is measured
directly from the progression gate. Special carrier constraints below remain
mandatory in every profile.

| Kingdom | Minimum credited value |
|---|---:|
| Cap | 5 |
| Cascade | 15 |
| Sand | 26 |
| Wooded | 26 |
| Lake | 18 |
| Cloud | 5 |
| Lost | 20 |
| Metro | 30 |
| Seaside | 20 |
| Snow | 20 |
| Luncheon | 28 |
| Ruined | 13 |
| Bowser's | 18 |
| Moon | 5 |
| Mushroom | 5 |
| Dark | 5 |
| Darker | 5 |

These are value floors, not item-count floors. Fixed and specially reserved items
may put a kingdom above its minimum.

#### Conditional progression capacity

Special capacity is checked in addition to ordinary floors and is measured from
eligible carriers, not from all moons credited to a kingdom:

- **Mushroom / captures:** when Capture Rando is on, Mushroom must hold enough
  eligible carrier value for every non-starting capture. The required count is
  reduced only by the number of capture slots guaranteed by the configured
  Starting Equipment setting.
- **Dark and Darker / abilities:** when Locked Abilities is on, the combined Dark
  and Darker kingdoms must hold enough eligible carrier value for every non-starting
  ability. The required count is reduced only by guaranteed starting movement
  abilities from Starting Equipment.
- **Cloud / paintings:** when Cloud moons unlock paintings, Cloud must hold at least
  `10` distinct eligible carrier entries, one for each painting link.

The capture and ability reductions describe guaranteed starting equipment only.
They do not subtract purple-shop offers or the later attempt-specific choice of
which identities receive those starting slots. If a carrier constraint cannot be
met, allocation fails rather than logging a warning and continuing with an invalid
pool.

#### Difficulty profiles

The base moon requirements, in Cap-through-Darker order, are:

`[0, 5, 16, 16, 8, 0, 10, 20, 10, 10, 18, 3, 8, 0, 0, 0, 0]`

Subtracting them from the native kingdom totals produces the exact Vanilla excess
multiset:

`[31, 37, 77, 64, 36, 9, 25, 65, 63, 47, 54, 9, 56, 38, 55, 26, 3]`

- **Easy:** repeatedly choose the allowed kingdom with the lowest current credited
  value, using a seeded tie order.
- **Vanilla:** seed-randomize which feasible kingdoms receive those exact excess
  buckets; each final target is base requirement plus assigned excess.
- **Hard:** add one or two tight kingdoms by rerolling their excess into the
  inclusive `0..20` range and concentrating the displaced moons into seeded
  outliers.
- **Expert:** retain Hard's choices and tighten another one or two kingdoms.
- **Max:** independently tighten one through seven additional kingdoms.

The exact target vector is filled with indivisible single/triple items. A kingdom
may never exceed `110` assigned entries because `ShineListLayout` owns fixed native
storage for exactly 110 rows. The planner reuses an existing concentration kingdom
while feasible and spills to another outlier when its value/item mix would exceed
that row capacity.

#### Moon Coin policy

Moon Coins are classified once from their physical/home kingdom, before the
destination is assigned:

- Cap, Cloud, and Moon items are Moon Coins when that kingdom's special moon use is
  `SpecialMoonUse_Coins`.
- Mushroom items are Moon Coins when Capture Rando is off.
- Dark and Darker items are Moon Coins when Locked Abilities is off.

The **Exclude Moon Coins:** slider has three values:

- **Vanilla:** classified Moon Coins are fixed at home.
- **Standard:** classified Moon Coins may move, but never to Mushroom, Dark, or
  Darker. They may be credited to Cap through Moon.
- **Ability Rush:** classified Moon Coins may move to any of the 17 kingdoms.

Classification never changes when an item is assigned elsewhere.

#### Hint Token special roles

`SpecialMoonUse_HintTokens` is appended as enum value `6`, so serialized
special-moon enum values remain stable. The Cap selector presents Hint Tokens
first and defaults to it;
tagged presets using the old Cap roll order are
    migrated when loaded.Hint currency is derived,
    not stored : if any Cap, Cloud, or Moon special - moon selector uses Hint Tokens, the Hint System is in Hint Token mode;
otherwise it retains the coin economy.The three
            configured source roles are evaluated against an item's credited/assigned kingdom, after moon allocation, rather than its physical
        / home kingdom.

          An eligible first
    - time collection in a Hint Token role awards one token in place of the ordinary filler - coin reward;
a triple reward awards three.Capture carriers,
    ability carriers, and fool moons remain excluded. `WorldPickup` and `RemoteSync` award once;
`DirectPurchase` does not, preventing a hint - shop purchase from returning its own cost.The overlays are `Got a Hint Token !` and `Got 3 Hint Tokens !`.The token wallet and five progressive counters are stored as one verified native
`GameDataFile` dictionary record.The transaction interface re - quotes before debiting, refunds failures, and increments a category counter only after a successful purchase.

                                                                                                               In team multiplayer,
    successful hint purchases are durable shared events.Each
        accepted event mirrors its debit and one category
    - counter step,
    plus the bought
        location
    / route / capture / ability hint,
    onto every member's save. Event identities use a per - process nonce and local sequence, so replay is at - most - once without collapsing legitimate repeated fixed - price purchases or colliding after a seed reset.Team logs remain independent.

    ####Triple Moon interaction

`OPT_TRIPLE_MOONS` remains option ID `7` and keeps its two
    - bit encoding : bit 0 is Anywhere and bit 1 is Randomized.The player - facing values are exactly **Vanilla **,
    **Anywhere **, **Randomized **, and**Anywhere + Randomized **.

                                                    When full allocation is inactive,
    Randomized preserves the legacy behavior,
    including the rebalance toward vanilla kingdom totals.When full allocation is
        active,
    Randomized determines item values before allocation, and allocation owns all credited destinations;
the old Triple - specific kingdom rebalance does not run afterward.Anywhere continues to control slot compatibility only.

                 ####Caches,
    hints, and save reconstruction

`getMoonRoleLayoutHash()` is a stable digest of the final assigned kingdom and reward - value layout.Capture, Ability, Painting, Hint, hook, and related caches use it as an input, so changing allocation invalidates downstream role data even when the seed is unchanged.The hash is also available to the host mirror and spoiler / diagnostic output.

                                                                                                                                                                                                                                                                                                                                         Rando save format **version 10
        * *appends a `HintConfigurationSettingsRecord` after the allocation and timer
    - penalty records.It packs independent coin and Hint Token price profiles,
    each with five fixed base prices and a separate additive increment;
currency itself is intentionally omitted because it is derived
        from the fixed
    - header special - moon roles.Older supported saves retain their legacy coin tier projection and receive the default token profile(one token base prices except disabled Purchases, with progression disabled).The native game save,
    rather than `rando.bin`, holds the mutable token wallet and progressive counters. `reinitDeterministic(attempt)` reconstructs the seed / settings - determined allocation before Capture and Ability role maps are rebuilt; `restorePlacements()` restores the serialized placement instead of
rerunning the solver. This keeps a loaded save's allocation and downstream roles
deterministic without making allocation depend on the generation attempt.
Difficulty remains a single `u8`: Easy/Vanilla/Hard keep values `0/1/2`, Expert
uses the former in-development value `3`, and the new Max mode uses `4`, so no
record-size or format-version change is required.

---

## 4. Kingdom Rando (route order)

**Files:** [KingdomRando.cpp](KingdomRando.cpp)

**What it does.** Randomizes the **order** in which kingdoms are visited, while
preserving the game's tier structure so the route is still playable.

**Algorithm.** The route is 12 tiers with fixed shapes
`[1,1,1,2,1,1,1,2,1,1,1,1]` — the two size-2 tiers are the vanilla
Lake/Wooded and Snow/Seaside forks. `rebuild` takes the pool of randomizable
worlds, Fisher–Yates shuffles it (`RngStream::Kingdom`), and deals them back into
the tier slots. Moon Kingdom stays pinned to the last tier unless explicitly
randomized into the order.

**Relationships.** Moon Rando's buffer engine walks these tiers; Requirement Rando
can use route position to shape difficulty (earlier kingdoms = fewer required
moons on Easy). When Kingdom Rando is off, the vanilla tiers are used.

---

## 4a. Start Position Rando (where a new save opens)

**Files:** [StartPositionRando.cpp](StartPositionRando.cpp),
[start_position.py](../../logic/start_position.py)

**What it does.** Replaces the Odyssey drop-off on a brand new save with a
seed-chosen logic node.

| Mode | Where the run can open |
|---|---|
| Vanilla | At the Odyssey. |
| Overworld | At the Odyssey, **or** at any sub-area exit into the starting kingdom's overworld. |
| Max | Either of the above, **or** inside any sub-area *linked from that overworld*, provided the player can get from there back into the starting kingdom. |
| Odyssey | Always inside the Odyssey itself (`HomeShipInsideStage`). |

The Odyssey drop-off stays in the pool for Overworld and Max, so "start
normally" remains one of the outcomes rather than something the modes forbid.

**Odyssey mode is not a roll.** It is the one mode that always opens in the same
place, and it exists to hide information rather than to move the player: he does
not learn which kingdom the run starts in until he walks out of the ship. The
interior carries no logic node and no entrance link, and its single exit is a
`ChangeStageArea` aimed at the magic name `CurrentWorldHome`, which `WorldList`
resolves through `mCurrentWorldId` — so stepping outside puts him exactly where a
vanilla file starts. That makes the mode logic-equivalent to Vanilla: there is no
candidate pool, no walk-back proof, and nothing seeded into `accessibleNodes`
(`getStartNode()` stays `-1` while `isActive()` is true — `isActive()` asks about
the *stage*, not the node). It still travels through the ordinary start-position
plumbing, under the same `HomeBackDoor` start id vanilla uses to walk into the
ship, and it still needs the sub-area world-id pin because
`GameDataFile::startStage` cannot derive a world from a non-main stage name.

**Ordering.** Kingdom Order → Entrance Rando → Start Position. By the time this
runs the full kingdom route is known (so the starting kingdom is known) and every
entrance chain is fixed, which is what the two reachability checks below need.

**Algorithm.** Candidates come from `LogicData::sNodeAnchors` — an index-parallel
table of change-stage ids and world coordinates the logic generator emits from
`logic/templates/runtime_anchors.csv`. The pool is Fisher–Yates shuffled
(`RngStream::StartPosition`, salted with the attempt) and the first candidate that
validates wins. Two checks bound it, and they are **not** the same:

- `MoonRando::computeStagesLinkedFromKingdom` (forward) — the sub-area chains that
  actually hang off the starting kingdom's own doors under this shuffle. This is
  what "linked from that overworld" means, and it is what keeps the Max pool from
  picking a room that merely sits inside the starting kingdom geographically. With
  entrances shuffled those are routinely different sets: a Metro sub-area can
  belong to a chain rooted in Lake.
- `MoonRando::canStartAtNode` (backward) — from the node, with only the starting
  equipment, the starting kingdom's overworld is reachable. Running the ordinary
  fixed point is what makes one-way pockets, dead ends and scenario gates count
  without re-deriving any of them.

**The logic model.** The start node is seeded into `accessibleNodes` permanently,
and every door of its sub-area is marked as an available exit
(`accessibleSubEntryFar`). It is permanent because the map's **Return to Start**
warp can always put the player back on it, exactly like a checkpoint flag.

> An earlier revision modelled the opening as *temporary* access (a transient
> location set plus checkpoint promotion) precisely because there was no way back.
> Return to Start removed that constraint and the whole mechanism with it.

The model is additive, so it cannot make a location unfillable. It *does* change
which locations sphere 0 offers, so the fill takes a different trajectory — a
given seed can succeed under one mode and need a retry under another, exactly like
every other per-attempt dependency.

**Return to Start.** `StartPositionRando::warpToStart` sends the player back to
the opening position;
it replaces the * * 2P Mode **row in the in - game pause menu.The redirect hangs off `StageSceneStatePauseMenu::exeStartSeparatePlay` — the nerve `MenuSelectParts` dispatches that option to — rather than off a row index,
    so it captures the option wherever the row happens to sit; the row index is used
only for the caption. Same shape as the existing `exeFadeBeforeHelp` → ModConfig
redirect in `PauseMenuHooks.cpp`.

The warp is a `ChangeStageInfo` to the start stage plus the same arrival arming
the new-save path uses, so the player lands on the node itself: through the
entrance's own PlayerStart when the node is a loading zone, or a synthetic spawn
at its coordinates otherwise. It takes `EntranceHooks::bypassNextTransition()`,
without which the entrance hook would treat the start node's loading-zone id as a
door to re-route and send the player wherever that door leads this seed.

**This is load-bearing for the logic.** The start node is modeled as permanently
reachable *because* this warp always reaches it. Removing the redirect means
going back to modeling the opening as temporary access.

**Caching.** `Randomizer::reinit()` runs on every stage-scene init and MoonRando's
already-generated path calls `rebuild` again straight after, so the result is
cached on (seed, attempt, mode, starting kingdom). Without that guard the full
candidate probe — an `initState` plus a reachability fixed point — ran twice per
stage load for an answer that cannot have changed.

**Runtime.** `ProgressionHooks::getForcedStartStage` returns the node's stage
instead of the kingdom main stage (and its gate now also opens when Kingdom Order
is off). A node that *is* a loading zone travels under that zone's own id so the
game resolves its authored PlayerStart — its recorded coordinates are the trigger
volume, and spawning on them would re-fire the transition. Every other node
travels under a reserved id that resolves to nothing, so
`EntranceHooks::armStartPositionArrival` builds a synthetic PlayerStartInfo at the
coordinates. A sub-area start also needs `mCurrentWorldId` pinned before the scene
builds: `GameDataFile::startStage` only derives a world from a *main* stage name
and leaves the field untouched otherwise, and world -1 crashes
`Shine::initAfterPlacement`.

**Relationships.** Runs last in the per-attempt dependency loop — after the
entrance shuffle and the starting-equipment roll, both of which the walk-back
proof reads. Re-derived from (seed, attempt) on load rather than stored, so a
Soft Nuzlocke reroll gets a fresh opening for free.

---

## 5. Requirement Rando (moon-count gates)

**Files:** [RequirementRando.cpp](RequirementRando.cpp)

**What it does.** Decides how many moons each kingdom requires before you can
leave it, distributing a global target total across kingdoms.

**Options.**
- *Moon Requirement* — global target total: 64 / 124 / 256 (124 is the vanilla
  per-kingdom sum; the old 128 preset maps to 124 on load).
- *Moon Difficulty* — Easy / Medium / Hard, controls the *shape* of the
  distribution.
- *Moon Kingdom Requirements* — whether Moon Kingdom is also scaled.

**Algorithm (`rebuild`).**
1. **Base distribution.**
   - *Medium* uses the vanilla per-kingdom requirements scaled to the target.
   - *Easy/Hard* use a **power curve** over route position. Easy makes later
     kingdoms harder;
Hard makes earlier kingdoms harder.The curve uses
        fixed
    - point integer math(no libm)
    : a squared term with `eps
    = 0.1`
, e.g.Easy `available * (pos / (N - 1) + eps) ^ 2`.2. * *Zero - sum variance * * — add per - kingdom random jitter that sums to zero
, so the
    total is preserved while individual kingdoms vary by seed .3.
    * *Clamp * *every kingdom to `[1, moonsAvailable]` (a kingdom can never require more moons than it contains).4. * *Balance to exact total * * — clamping creates "debt";
redistribute it by adding
    to the kingdom with the most spare capacity and removing from the one furthest
        above its base,
    until the sum equals the target exactly.

    Cap and
    Cloud(vanilla requirement 0)
    and the post - game kingdoms are excluded from scaling; Moon is excluded unless *Moon Kingdom Requirements* is on.

---

## 5a. Nuzlocke modes

**Files:** [Nuzlocke.cpp](Nuzlocke.cpp), [NuzlockeOverlay.cpp](NuzlockeOverlay.cpp),
[Randomizer.cpp](Randomizer.cpp)

Nuzlocke turns a Mario death into a run-level penalty instead of a normal stage
restart. The hard **Nuzlocke** mode treats the death as a full wipe and deletes
the save slot after the loss screen. **Soft Nuzlocke** keeps the slot but rolls a
fresh seed and opening after applying the configured loss rules.

Soft Nuzlocke has independent loss controls for earned moons and for captures and
abilities. Moon loss is 25%, 50%, or 75%;
capture and ability loss can each be
    None,
    25 %, 50 %, 75 %, or 100 %.The new run receives only the retained equipment,
    while the surrendered opener, regional - coin purchases, and lost unlocks stay lost.The loss panel shows the per - kingdom moon change and the captures or abilities that were surrendered.The retained state is persisted in the save's custom dictionary so a death, reload, or seed reconstruction cannot silently restore a lost reward.

                                                                                                                                                                   **Extreme Nuzlocke **is an additional setting that can be combined with either Nuzlocke mode.While active,
    all effective health and health maxima remain at
        one,
    including item - adjusted queries, and the normal low - health warning is suppressed.The ordinary hard / soft death handling still decides what happens after the hit.

                                                                                                             -- -

                                                                                                             ##6. Entrance Rando
        / "Loading Zone Rando"

        * *Files : **[EntranceRando.cpp](EntranceRando.cpp),
    [ EntranceHooks.cpp ](EntranceHooks.cpp)

    * *What it does.**Shuffles where loading zones lead via a **symmetric door pairing **.Every link contributes up to four *doors * : its overworld entry side(`homeEntranceIn`),
    its distinct overworld exit side(`homeEntranceOut`, when different), the sub - area's near side (`subEntranceIn`) and its far side (`subEntranceOut`, when distinct).The shuffle pairs doors; both directions are written, so walking back through the door you arrived from always returns you to that exact side(two - way doors).Chains are emergent : an overworld door leads into a sub, whose other door leads into the next sub or surfaces at another — possibly different - kingdom — overworld door.Direct overworld↔overworld pairs(kingdom - to - kingdom doors) are allowed.

                                                                                       **Modes(`Randomizer::EntranceRandoMode`)
        .**Off,
    Decoupled and Limited are
    their own builders(below)
        .The menu offers Off
    / Coupled / Limited / Decoupled : Loading Zones is a pure *rule *setting now,
    and chain length moved to
        Seed Openness(§3.3)
            .The retired SmallChains
    / LongChains values still exist in
    the enum so an old save parses; they migrate to Coupled plus the Openness level
that reproduces them (`applyLegacyEntranceRandoMode`). (The enum's numeric values
are stable save-file contents - Off/Standard/Decoupled/Limited keep their
original 0-3 for compatibility, with SmallChains/LongChains appended at 4-5; the
player-facing menu shows a different order via an explicit UI-index table in
`StageSceneStateFileSelect.cpp`.)

**The build is incremental (chain-fill v5).** The door graph is no longer decided
before the fill runs. `beginIncrementalBuild` classifies the pools and reserves
the gateways but wires *nothing*; the placement engine then takes one pairing at
a time through `commitWiring`, looking at what each one opened before choosing
the next, and `finalizeIncrementalBuild` pairs whatever it never got to. Between
those two calls the table is **partial**: an unwired door carries
`ConnKind_None` and the reachability model reads that as "leads nowhere", which
is exactly what lets the fill reason about a door before deciding where it goes.

Nothing about *what* is safe to draw changed - both source pools are still
provably reachable by construction, and `commitWiring` refuses a source from
anywhere else. Only the ORDER and the CHOICE moved to the fill. See
"Entrance wiring as a placement move" in `MoonRando.cpp`.

**Pools (`buildPools`).** Two source pools plus a target pool, drawn from
until sub-areas run out:
- **Kingdom-entrance pool.** Every *kingdom-main* entrance: a home door whose
  own home stage is a kingdom's own overworld stage (`MoonRando::
  isEntranceHomeStageAlwaysAccessible`), so it's walkable from the start
  regardless of how anything else is wired. "Nested" links - whose home side
  instead sits physically inside *another* link's sub-area (e.g. a second
  doorway found deep in Deep Woods) - are excluded here;
they only ever
        become real once their own sub
    - area is entered some other way,
    so they live
        in the sub
    - area pool below instead.This pool only shrinks.
    - **Chain - exit pool.**Starts empty;
grows as sub - areas are connected(below).
    - **Sub - area pool.**Every randomizable link 's own sub-side, classified by how its near / far doors relate : no far door(or a far door unreachable from either side) is a one - door dead end; otherwise it' s a single item whose entry side is pinned to whichever side can reach the other per
  `MoonRando::canEntranceSubBeTraversed` (a two - way sub - area picks either side at random),
    with the *other *side recorded as what becomes available once the entry is used.- **Main loop : **repeatedly draw one random door - weighted between the kingdom - entrance and chain - exit pools by `chainBiasForMode`'s numerator/ denominator - and one random item from the sub - area pool, and connect them; a consumed sub - area ' s other door(if any) joins the chain - exit pool, eligible to seed further connections.Chains, of whatever length, are emergent from repeated random draws rather than deliberately built.- **Chain length.** `chainBiasForOpenness` returns equal weights(1 : 1) at Standard openness, which reduces to a uniform draw over the combined pool - the algorithm' s natural, unweighted behavior.Open under - weights the chain - exit pool(1 : 3) so a fresh kingdom entrance is preferred whenever one is available, keeping most sub - areas one hop from the overworld; Compact over - weights it(3 : 1) so an open chain exit is preferred, letting chains extend until they hit a dead end before surfacing.Long chains are the LESS open shape - short chains mean more chains, so more independent ways in.Every weighting draws from the same two provably - reachable pools, so tuning this cannot reintroduce an unreachable destination.Full table in §3.3. - **Unreachable sub - areas are repaired, then warned about - never rejected.**
  `allEntranceDestinationsReachable` runs right after `finalizeIncrementalBuild`,
    ahead of the start - node revalidation and the final replay, because
  `EntranceRando::repairUnreachableSubAreas` REWRITES the wiring : it tries a 2 - opt swap per orphan, breaking a pairing the *leftover *pass made between two already - reachable doors and handing one half to the orphan, which cannot strand anything.Only leftover pairings are eligible; anything the fill chose
  is load-bearing. Whatever survives the repair is a warning and the attempt
  stands. This was briefly a hard rejection, on the theory that the incremental
  builder could drive the residue to zero. Measurement says otherwise: a vanilla,
  completely unshuffled topology already leaves 6 of 124 randomizable links
  unreachable and a shuffled one 9-19, so the residue is the NORM and rejecting
  on it rejected every attempt - generation failed outright rather than producing
  a seed. An isolated island nobody can reach does not stop anyone completing the
  game, and the host solver never enforced it either.
- **Why this - and not "chain-fill v3" (kept in history for the curious):**
  the old algorithm tracked a single "current chain" pointer. The moment a
  chain stopped extending (dead end, untraversable, or a fixed chance to just
  stop), its dangling exit was abandoned straight into an unchecked
  random-leftover pass, and the *next* chain had to draw a fresh start from a
  pool that included nested doors - the actual source of "could not prove
  every randomized entrance destination reachable" warnings, since a nested
  door is only real once its own sub-area is independently entered, which
  wasn't guaranteed. v4 never discards a validated door: every door that
  enters either pool - kingdom entrance or freed-up chain exit - is provably
  reachable, and it stays eligible for the rest of construction instead of
  being handed off to unchecked pairing early. Only the leftover cleanup at
  the very end (below) is unvalidated pairing, and by then everything left in
  the pools is already provably reachable, so it can't strand anything.
- **Leftover cleanup:** whatever's left in either pool once the sub-area pool
  is empty (unused kingdom entrances and chain exits nobody drew) is paired
  off randomly, including direct kingdom-to-kingdom doors. An odd door out is
  sealed to its vanilla behavior. (If both pools ever run dry before the
  sub-area pool does - not expected with real data, since kingdom entrances
  comfortably outnumber dead ends - whatever's left is dumped into this same
  leftover pairing without the reachability guarantee.)
- Connections are stored per door (`sPipeConn` for `home_in`,
  `sHomeOutConn`, `sSubNearConn`, `sSubFarConn`) and looked up statically at
  runtime — no entry stack is needed for routing.
- Because traversability depends on the logic difficulty, the pairing is rebuilt
  when the difficulty changes.

**Which links are randomizable** (`isRandomizableLink`): a link must be flagged
`randomizable` in the logic data. Moon- and Cloud-Kingdom links are always in
the pool.

**Logic integration (full kingdom access).** Surfacing at another kingdom's pipe
grants that kingdom in the placement logic (`addAccessibleHomeArrival`), so
entrance chains are a real alternative to Odyssey travel and required
progression may be placed behind them.

**Relationships.** Coupled to Moon Rando: it is rebuilt once per logic attempt so
the solver can find an entrance layout that keeps the seed beatable (§3.3).

---

## 7. Painting Rando (warp paintings)

**Files:** [PaintingRando.cpp](PaintingRando.cpp)

**What it does.** Shuffles the destinations of the warp paintings (the
inter-kingdom "world warp holes").

**Algorithm (`buildRemapTable`).**
- `shuffleDestinationSlots` permutes painting destinations (`RngStream::Painting`).
- Retry up to 32 times while `hasSelfMapping()` is true — a painting must lead to a
  **different kingdom** (and never to its own stage). Unlike Entrance Rando, this
  cross-kingdom constraint is *desired* and feasible, so rejection sampling is fine
  here.
- `rebuildInverse` builds the reverse map so the return painting works.
- A separate sub-stream (`seed ^ 0xC10D11A6`) assigns which painting link each
  **Cloud-Kingdom moon** unlocks, used only when *Cloud-moons-unlock-paintings* is
  on. When there are more painting links than cloud moons (e.g. 9 moons / 10
  links), the surplus links (`getFreePaintingLinkCount`) are set aside as
  always-unlocked instead of assigning one moon to two links - the leading
  entries of the shuffled link order are "free" (unlocked and displayed as open
  from the start of the game), and the rest are assigned one-to-one to moon
  slots. The permutation preserves full link coverage even when there are fewer
  cloud moons than links.

**Relationships.** The table is **always built**: with Painting Rando off it acts
as an identity map, because the QoL patches force warp paintings open pre-endgame
and the vanilla game can only resolve their destinations from post-game save data
(without the identity table the painting shows closed, and forcing it open
crashes). Moon Allocation runs before the Cloud painting role map is consumed,
and its role-layout hash invalidates Painting data when assigned kingdoms or
reward values change. Painting links feed the logic graph as extra edges, so Moon
Rando's solver accounts for them. In the Python mirror, painting links are
currently treated as always-available edges (the runtime enforces the cloud-moon
gate).

---

## 8. Capture Rando

**Files:** [CaptureRando.cpp](CaptureRando.cpp), [CaptureHooks.cpp](CaptureHooks.cpp),
[CaptureOverlay.cpp](CaptureOverlay.cpp)

**What it does.** Locks captures behind moons: each capturable enemy/object must be
*unlocked* by collecting a specific moon before you can capture it.

**Algorithm.** Shuffle the full capture list (`RngStream::Capture`), then assign
captures to **Mushroom Kingdom credited moon slots** in order (a multi-moon
provides 3 slots). Collecting that moon grants the capture. Under Moon Allocation,
the Mushroom capacity check uses only eligible assigned carriers and the pure
required progression-slot helper, not the raw Mushroom total.

**Relationships.** When on, captures become progression items the logic solver must
place reachably;
Moon Rando 's buffer engine ensures each capture-unlock moon is obtainable before it' s needed.The role map is rebuilt when the Moon Allocation role - layout hash changes.When off, all captures are available from the start(the logic state pre - grants them).

                                                                                                                                                                                     -- -

                                                                                                                                                                                     ##8a. Chaotic Randomization

                                                                                                                                                                                         **Files : **[EnemySwapRando.cpp](EnemySwapRando.cpp),
    [EnemySwapHooks.cpp](EnemySwapHooks.cpp),
    [EnemySwapDB.h](../../ include / Randomizer / EnemySwapDB.h)(generated)

    * *Canonical design : **[enemy - randomizer - architecture.md](../../ docs / enemy - randomizer - architecture.md),
    [enemy - randomizer - authoring.md](../../ docs / enemy - randomizer - authoring.md), and[enemy - randomizer - implementation.md](../../ docs / enemy - randomizer - implementation.md).Those documents own the data contracts, gate semantics, authoring workflow,
    failure behavior, compatibility matrix, and acceptance status.Keep this overview short so it cannot become a competing source of truth.

                                                    **What it does
        .**Three independent settings randomize enemy
    / capture,
    NPC, and prop sources within curated full - actor - safe domains.Combat sources never become NPCs or props.NPCs may become vetted NPC or combat actors, and props may become vetted prop actors or, in the mixed mode, vetted prop or combat actors.Broad incidental - scenery families are accepted as source - only props;
moon - linked props and functional gameplay infrastructure remain vanilla.Unknown, scripted, boss, special, unsafe 2D, and interaction - dependent sources remain vanilla unless the catalog explicitly marks them safe.

                                                                                                                                             **Data ownership
        .**The generated placement inventory contains every extracted object with a stable `(physical_stage, object_id)` key.The curated entity catalog owns friendly names,
    categories, captures, and source / target safety;
the rule table owns replacement policy only.Directional route requirements
                are connected to authored alternatives in `capture_gates.csv` and
`capture_gate_objects.csv`.Generated CSV
            / header output is not hand
        - edited.

            * *Assignment
    and logic.** `EnemySwapRando::buildRemapTable` constructs one versioned deterministic assignment per seed attempt.It satisfies authored
`all`, `any`,
    and `at_least` physical capture witnesses with deterministic
        backtracking,
    fills unconstrained placements from versioned placement hashes,
    then verifies the completed witnesses.An unsatisfiable assignment rejects the
        seed attempt; gates are never silently ignored. Directional route evaluation,
tracker guidance, previews, hints, and critical-capture analysis use the same
edge-aware predicates. Moon-access requirements retain their conservative
behavior for this release.

**Runtime spawn swap.** [EnemySwapHooks.cpp](EnemySwapHooks.cpp) resolves the
physical placement key at actor creation and scopes the replacement identity
through the factory call. Session overrides take precedence over the generated
assignment while retaining target-safety checks.

**Inspector.** The active-stage `Enemy Swap Inspector` reuses the existing
debug-warp subarea list. It provides a no-reload object-ID overlay, paged
enemy/capture placement selectors, and current-map override reset. Overrides
are session-scoped and keyed by the full physical placement identity;
actor
        labels are drawn through one foreground ImGui overlay.

            ** Relationships
    .**Capture Rando still controls whether the player has acquired
          a capture token; enemy swapping controls which safe actor physically occupies a
placement. A gate is usable only when both its physical witness is satisfied and
the player's existing logic state grants the replacement capture token.

---

## 9. Ability Rando / Locked Abilities

**Files:** [AbilityRando.cpp](AbilityRando.cpp), [AbilityLock.cpp](AbilityLock.cpp),
[AbilityOverlay.cpp](AbilityOverlay.cpp)

**What it does.** Locks Mario's moves (cap throw variants, dive, ground pound, etc.)
behind moons.

**Algorithm.**
- Pick a **starting cap-throw** at random from the abilities flagged
  `canStartWithSeed` (so the seed is never softlocked with no throw).
- Build **Dark-Side credited moon slots** (multi-moons count 3×), shuffle them,
  shuffle the unlockable-ability list, and assign abilities to slots round-robin.
  Moon Allocation checks the combined Dark/Darker eligible carrier value against
  the pure required progression-slot helper before this map is consumed.

**Relationships.** Same as Capture Rando — when on, abilities are progression items
the solver places reachably, and the chosen starting cap-throw seeds the initial
logic state. The role map is rebuilt when the Moon Allocation role-layout hash
changes. When off, all abilities are pre-granted.

---

## 9a. Movement Abilities (the Ability Lock sub-page)

**Files:** [MovementAbilityLock.cpp](MovementAbilityLock.cpp), plus the table and
predicates in [AbilityRando.cpp](AbilityRando.cpp)

**Capacity.** Two ceilings bound how many of these can ever exist. The Ability List is
the vanilla souvenir list, and its **icons** run out at **32**: that is how many
souvenir textures `romfs/ObjectData/TextureSouvenir.szs` carries (14 kingdoms x 2, plus
`SouvenirDolphin1/2` and `SouvenirSpace1/2`), and every row that does not name one in
`getSouvenirTextureName` borrows the vanilla record sitting at its display index. Rows
themselves are not capped there — `SouvenirListLayout` sizes its arrays to the gift
list *plus* the sticker list, and `refreshSouvenirListData` clamps only to that — so a
row past the souvenirs renders with whatever record follows them rather than vanishing.
The tighter ceiling is the ability reward pool, hosted on Dark Side plus Darker Side,
which at vanilla size is **29 slots** (25 moons, 2 of them grand). With every dial on
and no starting equipment the pool is **exactly 29** as of Timer Challenges, so the
next ability added needs a host found for it first: Moon Allocation validates
`required <= available` and rejects the seed, and with it off nothing checks and the
surplus ability simply gets no host at all. Count both before adding another ability —
and note the settings page has its own limit, below.

**What it does.** Extra entries in the ability table that gate a traversal
*verb* rather than a move: Moon Kingdom's low gravity, swimming, picking objects
up, riding Jaxi, riding the Motor Scooter, the Rocket Flower, talking to NPCs,
going flat through a 2D pipe and beating a timed challenge. Each has its own dial
on the Movement Abilities settings page, which is only reachable while Locked
Abilities is on — they are sub-options of it, not a separate system.

**Page size.** That page now carries nine rows, one past the eight that
`kMaxBuilderPageRows` documents as the largest count confirmed to render on
hardware (13 is known to overflow; `kMaxPageOptions = 16` is only the array-sizing
cap, so nothing is at risk of corruption). If a tenth row is ever wanted, split the
page the way the Purple Coin Shop was split off the Lock page — which also needs a
`OPT_CUSTOM_TAB_PAGE_*` row, and all three Customize Tabs pages are themselves at
eight.

**How a dial changes the pool.** `isAbilityInUnlockPool` asks
`isMovementAbilityInPool` for these, so an ability whose dial is off never
costs a Dark/Darker Side host and is granted from the start instead
(`isMovementAbilityGranted`). They are nevertheless *always* in the
player-facing Ability List, rendering as already acquired when granted — the list
length is fixed, which is what the Sphinx quiz question about it relies on.
`MoonRando::initState` and `randomizer_logic.py` grant the same tokens on the same
rule, and `required_ability_slots` in `moon_allocation.py` excludes them, so an
existing seed with every dial off allocates and fills exactly as it did before.

**Moon low gravity** is a four-way roll rather than a checkbox, because it has two
ways to arrive that are not item placement:

| Mode | In the reward pool? | How it is earned |
| --- | --- | --- |
| Unlocked | no | granted from the start (vanilla) |
| Ability Lock | yes | a Dark/Darker Side moon, like any locked ability |
| Moon Requirements | no | Moon Kingdom's own moon requirement |
| Extra Moon Requirements | no | that requirement plus a seeded 5-10 |

The surcharge is rolled in `RequirementRando::rebuild` on its own Rng stream, so
adding it cannot shift the requirement distribution of an existing seed, and it is
rolled whether or not the mode is selected so switching modes never re-rolls it.
Under either count mode the ability is *derived* from the save's Moon Kingdom moon
total rather than written to the hack dictionary — which is why
`isAbilityRevocable` reports false for it and Nuzlocke leaves it alone. The tracker
shows the extra mode as a third field on the Moon Kingdom row
(`collected / requirement / low-gravity total`), both right-hand values staying
`?` until the kingdom has been visited.

**Gameplay side.** Each lock re-uses a vanilla mechanism rather than inventing one:

- *Low gravity* swaps the `"Moon"` PlayerConst for the ordinary one in
  `PlayerFunction::createMarioConst`.  const That  is built once during player init
  and cached by the collider, joint keeper and every player state, so earning low
  gravity while standing in Moon Kingdom takes effect on the next load of the
  stage rather than instantly.
- *Swim* takes over the seven swim nerves that mean Mario is in the water off his
  feet — the four entry ones (`exeSwimFallIn`, `exeSwimHeadIn`, `exeSwimDiveIn`,
  `exeSwimDive`) plus surface/move/paddle — and calls the game's own abyss-recovery
  entry point (`0x0042033c`'s helper) on the player. That is the whole vanilla
  sequence: cancel the bind, drop the carry, cancel equipment,
  `PlayerStateAbyss::prepareRecovery`, set the abyss nerve — bubble and respawn at
  the last safe point. Its six arguments are player members read straight off that
  call site. Wading (`exeSwimWalk`) and climbing out (`exeSwimLand`) are
  deliberately not hooked, and captures are unaffected because they do not use
  Mario's swim state.

  `PlayerStateAbyss` owns a `PlayerStateRecoveryDead` sub-state and its `appear()`
  chooses between Fall and Recovery on `PlayerRecoverySafetyPoint::isValid`. Outside
  Kids Mode that refuses unless the player is inside an authored recovery area, so
  the first working version played the fall-into-the-void animation instead of the
  bubble. `isValid` is therefore forced true for the two frames `appear()` needs —
  but only when the game already has a recorded safety point to return to, so with
  none the override stands down and vanilla falls as before rather than recovering
  to nowhere.

  An earlier attempt lied to `isActiveRecoveryArea` / `PlayerAreaChecker::isInRecovery`
  so vanilla would start the sequence itself. It never fired: the block those feed
  sits at the end of a long `control()` with several earlier exits, and it is gated
  on the same `isValid`. Don't go back to it.
- An arrival that *lands* the player in deep water is retreated instead: the
  transition records the door to come back out of
  (`MovementAbilityLock::noteRetreatTarget`, from the same place the Subarea Key
  denial picks its exit), and swimming inside the arrival window bubbles briefly
  then sends the player back through it. With no recorded door it falls back to
  the ordinary safety-point bubble.
- *Grab* refuses `PlayerCarryKeeper::startCarry`, the single chokepoint every
  pick-up goes through.
- *Jaxi* takes over `SphinxRide::exeRun` entirely: it offers the dismount
  (`tryGetOffAndCancelBind`) and then holds the idle `Wait` action, skipping both
  the run animation and `updateRun`. Skipping `updateRun` alone was not enough —
  nothing else leaves the Run nerve, so Jaxi ran on the spot.
- *Scooter* runs `Motorcycle::exeRideWait`'s body in place of the drive, run-start
  and jump nerves. That nerve carries dismount, gravity and damping, so the player
  stands on a parked scooter they can step off.
- *Rocket Flower* refuses `RocketFlowerFunction::requestEquipRocketFlower`, the one
  funnel from "Mario touched the flower" to `RocketFlowerEquipWatcher::equip`.
  `RocketFlower::receiveMsg` already has a branch for that answering no — the
  flower drops its Equip sensor, replays Appear and goes back to waiting — so the
  flower stays usable with no state to unwind. Not `isEnableEquip`: that is only a
  nerve-state predicate, and `receiveMsg` inlines the same test rather than calling
  it, so hooking it does nothing at all.
- *Talk to NPCs* refuses `rs::isPlayerInputTriggerStartTalk`, the press that turns a
  balloon prompt into a conversation. The balloon node is its only caller, and its
  else branch just re-requests the prompt — so the "Talk" button stays on screen and
  only the dialog is withheld. Deliberately not `rs::tryLockStartEventTalkDemo`, which
  sits beside it in the same condition but is also what the DemoForceStart nodes use;
  hooking that would block cutscenes. One ability, not one per species — see the
  capacity note above for why per-species does not fit.
- *2D pipes* overturn `DokanPuppetController::judgeMsgBindStart` — the pipe's own
  "do I take this player" answer — when the pipe is one that changes dimension. A
  pipe into 2D is an ordinary `Dokan` placement whose `DstDokan` link points at a
  `Dokan2D`: `Dokan::init` spots that pair and builds the actor from the
  `DokanChange2D` archive instead of the placement's model, and `DokanKoopa` uses
  that archive unconditionally — so the model name is exactly the signal, and the
  pipes on the 2D side keep their own `Dokan2D` model and are never withheld. The
  vanilla answer is asked for first and only overturned when it was yes, so every
  near miss (wrong direction, too slow) still reads as one and the refusal line
  lands only when the pipe really would have taken him. `Dokan::attackSensor` — the
  one entry that skips the judge, for a *captured* Mario running into a pipe — is
  skipped wholesale for a withheld pipe, silently: it runs on sensor overlap rather
  than on intent, so speaking there would talk at anyone standing nearby.
  **Not covered:** the 2D sections that are their own stage (`Cube2DExStage`,
  `Galaxy2DExStage`, `DotHardExStage`, `Lift2DExStage`, `Theater2DExStage`,
  `Note2D3DRoomExStage`) are reached by `DokanStageChange`/`ChangeStageArea`, which
  are ordinary transitions with an ordinary destination — and under entrance shuffle
  that destination is not even theirs any more. Those stay open.
- *Timer Challenges* end the moment they start. P-Switches (`TrampleSwitchTimer`,
  `CapSwitchTimer`, `HipDropSwitchTimer`) and scarecrows (`CapRack`) all own an
  `AppearSwitchTimer` and start it through `onSwitch`, so its counting nerve
  `exeOnWait` is the one place that covers every timed challenge. The hook answers by
  calling `AppearSwitchTimer::offSwitch` — the game's own "time is up", and the same
  call `exeOnWait` already makes when the player leaves the challenge's area — so the
  appeared actors are killed, the appear demo's camera is ended, the stage switches
  (`CapRackKeepOn` included) go back off and the switch can be pressed again.
  `offSwitch` sets the OffWait nerve, which is why the Cappy line fires once per press
  and needs no latch. Deliberately *not* a refusal at `onSwitch`: that would leave the
  switch actor pressed with nothing counting and nothing to put it back.
  A switch placed with no time limit at all (`mTimerFrames == -1`, the field both
  `exeOnWait` and `offSwitch` test before touching the timer SE and BGM) is not a
  challenge and is left alone.

  The trample switch needs one more hook. Its press is `al::isMsgPlayerTouch`, which
  arrives every frame Mario stands on it, so an expiry on frame one handed the switch
  straight back to a foot that had not moved and it thumped up and down in a loop.
  `TrampleSwitchTimer::receiveMsg` therefore drops touches from the switch the player
  is *already* standing on: the first touch of an approach goes through (that is the
  one that presses, expires and speaks), and the rest are answered `false` — which is
  what the switch itself answers whenever it is not in a state to accept a touch —
  until touches stop arriving for `cTrampleReleaseFrames`, which is what stepping off
  looks like from here. The other three starters are single deliberate acts (a cap
  throw, a ground pound, Cappy landing on a rack) and cannot repeat this way.

The refusals queue a Cappy line through the shared `getSystemMessageString`
override, on invented CapMessage labels answered by
`MovementAbilityLock::tryMakeCapMessage` — the same trick the Subarea Key and Moon
Rock Key refusals use. An invented label must ALSO be claimed by MoonHooks'
`isExistLabelInSystemMessage` hook: without that, `CapMessageLayout` renders the
literal `NULL` and never reaches the text override at all. Each latched line is
spoken once per time the player leans in — the refusing code runs every such frame,
so going `cRefusalReleaseFrames` without one is what re-arms it.

**Icons.** Both the Ability List and the purple shop take an ability's icon from
the vanilla gift record sitting at its *display index*, so the appended rows
would inherit unrelated souvenirs. Their art is instead added to
`romfs/ObjectData/TextureSouvenir.szs` for this mod, and
`AbilityRando::getSouvenirTextureName` names each one — currently `SouvenirSky2`
(low gravity), `SouvenirPeach1/2` (grab, swim), `SouvenirSpecial1/2` (Jaxi, scooter),
`SouvenirDolphin1/2` (Rocket Flower, NPC talk) and `SouvenirSpace1/2` (2D pipes, timer
challenges). Every name in that archive
carries a `Souvenir` prefix (`SouvenirSky2`, not `Sky2`) — a name that does not
resolve fails silently, leaving the row showing whatever native souvenir happens to
sit at its index. Since no vanilla record names these textures there is nothing to
reuse, so they have to be built with `al::createTextureInfo`.

**That call allocates**, so it is only legal where a sead heap is current. The shop
builds its own inside the shop's texture load, which is such a place. The Ability
List cannot: `refreshSouvenirListData` runs inside `StageScene::exeCollectionList`,
a nerve execute with no current heap, and building one there faults in `operator
new` (it surfaces as a crash in `SharcArchiveRes`/`GetTlsValue` on opening the
list). So `HintSystem::prepareAbilityIconTextures` builds them once at StageScene
layout init and caches them; the cache is cleared — never deleted — on scene
teardown, because the textures live on that scene's heap.

**Not starting equipment.** `canStartWithExtraAbility` excludes all of them. They share
the Movement category with the abilities Starting Equipment hands out, and a free
Swim at the start means the lock the player asked for simply never happens.

**Logic.** The tokens live in `logic_tokens.csv` like any other ability.
`Jaxi` and `Bike` were previously always-unlocked *capture* placeholders for the
enemy-swap override props and were retyped in place rather than duplicated, so
there is exactly one token per verb. Which routes and moons actually require them
is authored in the graph editor; nothing is required by default, which means a
seed generated today treats them as free even when a dial is on.

---

## 10. Cosmetic & support randomizers

These do not affect logic/reachability.

- **Skybox Rando** ([SkyboxRando.cpp](SkyboxRando.cpp)) — `RngStream::Skybox`. A
  single **mode** (`SkyboxRandoMode`, set via one roll in both menus) chooses two
  axes: *which* areas get a new sky and *which pool* they draw from.
  - **None** — off.
  - **Kingdoms** — kingdoms only, drawn from the **per-kingdom compatibility
    matrix** (a bitmask of which kingdom skies render correctly there).
  - **Subareas** — sub-areas only, drawn from the **sub-area pool**.
  - **Own Areas** — both, same-type only: kingdoms↔kingdoms (compat matrix),
    sub-areas↔sub-areas (sub-area pool).
  - **Max** — both, anything-goes: every area draws from the **full pool** with no
    type or compatibility restriction (kingdoms can get a sub-area sky and vice
    versa).

  The source pool (`buildSourcePool`) is the 17 kingdom main stages (first
  `sKingdomSourceCount` entries, keeping their tested scenario) **plus every
  distinct sub-area in [EntranceDB.h](../../include/EntranceDB.h)** minus
  obviously-indoor stages (name contains `Shop`/`Costume`/`Slot`/`Secret`/`2D`,
  which carry no real sky), loaded with the default scenario (-1). Sub-area and
  Max picks are keyed deterministically by `seed ^ hash(stageName)` so each area is
  independent. The indoor filter is heuristic (fix-by-testing: add markers as
  sky-less sources turn up). The mode is persisted in both the per-slot save
  (`reserved[Reserved_SkyboxMode]`, stored as `mode+1` with a legacy bit fallback)
  and the global config (`"Sky"` int in [main.cpp](../main.cpp)). The three hooks
  in [SkyboxHooks.cpp](SkyboxHooks.cpp) route main stages and sub-areas through one
  `resolveSkybox` helper; the per-area remap functions self-gate on the mode.
- **Music Rando** ([MusicRando.cpp](MusicRando.cpp)) — collects every shufflable
  track (skipping `NeverShuffle`-flagged ones), then does a **no-repeat shuffle**
  (a derangement, so no track maps to itself). Standard uses that one fixed
  resource mapping per seed. Extended derives an independent, stable mapping
  from the physical stage and vanilla source cue, so areas sharing a source can
  diverge while revisits remain consistent. Max draws a fresh track for each
  newly requested source theme. `RngStream::Music`. The Music Randomization
  settings page trims the pool before the shuffle: Streamer Mode drops the `Jp`
  language variants, *Disable 2D Themes* drops the `_8Bit` pseudo-resources, and
  *Disable Extra Themes* drops `MusicDB::sExtraResourceTable` (titles, demos,
  credits, parades, races, minigames, pause-menu `Clct` arrangements), and
  *Disable Boss Themes* drops `MusicDB::sBossResourceTable`. Filters apply to
  the source side too, so an excluded track keeps playing vanilla.
- **Sphinx Rando** ([SphinxRando.cpp](SphinxRando.cpp)) — replaces the Jaxi/Sphinx
  quiz questions from a fixed question pool. `RngStream::Sphinx`.
- **Shop Price Rando** ([ShopPriceRando.cpp](ShopPriceRando.cpp)) —
  `RngStream::ShopPrice`. Re-prices every gold-coin item in Crazy Cap at 1-1000.
  Crazy Cap sells out of one shared catalogue
  (`GameDataFunction::getShopItemInfoList`), and both the drawn price and the
  amount `ShopStateInShop::exeBuy` charges read the same `ShopItemInfo::price`,
  so rewriting that field is the whole feature. Outfits and caps are priced from
  their name alone and so cost the same everywhere; the Life-Up Heart and the
  shop's Power Moon mix in the kingdom, which means they are re-derived on every
  shop open. Regional-coin souvenirs and stickers are left alone. The catalogue
  is loaded once per boot rather than per save, so the vanilla prices are
  captured on first use and restored whenever the setting is off. Nothing is
  generated at seed time, so it has no entry in `reinit()`; it rides the
  `ShopInfo::createShopItemList` trampoline that
  [PurpleCoinRewards.cpp](PurpleCoinRewards.cpp) already owns.
- **Chest Order Rando** ([ChestOrderRando.cpp](ChestOrderRando.cpp)) —
  `RngStream::Chest`. Shuffles the required opening order of the three
  sequential-chest puzzle rooms (Wooded 3 chests, Seaside 4, Mushroom 8). Purely a
  runtime hook: `TreasureBoxSequentialDirector::init` runs, then the shuffle
  rewrites the director's box array (which *is* the required order), each box's
  popup number, and moves the reward Shine onto whichever chest ends up last. The
  permutation is keyed by `seed ^ hash(stageName)`, so leaving and re-entering a
  room reproduces the same order. Nothing here is generated at seed time, so it has
  no entry in `reinit()`.
- **Hint System** ([HintSystem.cpp](HintSystem.cpp)) — sells reachability/location
  hints using a derived Coins or Hint Tokens mode. It has independent Moon,
  Capture, Ability, Path, and shared direct-Purchases base-price categories; a
  Disabled base price is the only availability gate. Each currency profile also
  has a separate additive Progressive Price selector: current price is base plus
  that category's successful-purchase count times its increment. Counts are
  independent, and the default increment and Purchases base price are Disabled.
  Use Global Progressive Cost swaps the multiplier for the save's total purchase
  count across all five categories, so every purchase raises every price; only
  the ladder position read changes, never the stored per-category counts.
  Hint Configuration lives only on file-select, not in the in-game mod menu.
- **Spoiler Log** ([SpoilerLog.cpp](SpoilerLog.cpp)) — dumps the placement and the
  proven path; always generated after Moon Rando. It used to be an option, which
  only meant a run could silently lose its log — loading any slot saved with the
  option off turned it off for every run created afterwards.

---

## 11. How they relate (dependency map)

```
                 seed + options
                       |
       KingdomRando / RequirementRando / physical moon data
                       |
       Moon Allocation (seed/settings-deterministic pre-pass)
          |                 |                 |
       Capture           Ability           Painting
       role map          role map          role map
          \                 |                 /
           +---- hints / solver / spoiler replay ----+
                                |
                   Moon placement and validation

  Entrance connectivity and per-attempt requirements feed the solver.
  Cosmetic (Skybox / Music / Sphinx): independent, no logic impact.
```

Key couplings to remember:
- **Moon Rando consumes everything logical.** If it can't prove a seed, it retries
  with new entrance shuffles before giving up.
- **Allocation is the dependency boundary.** Moon Allocation runs before Capture,
  Ability, Painting, hint, and solver consumers. Its stable role-layout hash is an
  input to downstream cache invalidation, so assigned-kingdom or reward-value
  changes cannot leave stale role maps or hints behind.
- **Option gates change rewards, not the pool.** Every non-achievement moon
  (including hint-art and the Cloud/Moon/Mushroom/Dark-Side moons) is always in
  the shuffle pool; a special kingdom's moon items pay filler coins when its
  feature toggle is off. *Moon Kingdom Requirements* and
  *Cloud-moons-unlock-paintings* still gate which **entrances/links** are
  randomizable and reachable, so several randomizers read those flags.
- **Capture/Ability off ⇒ pre-granted.** The logic state starts with those tokens
  so locations gated on them are reachable.

---

## 12. Adding or changing a randomizer — checklist

1. Pick/add an `RngStream` in [Rng.h](../../include/Randomizer/Rng.h) with a unique
   salt; never share a stream between two randomizers.
2. Implement a `buildRemapTable(seed[, attempt])` (or `rebuild`) that is pure and
   deterministic, plus a `clear()` that restores identity/vanilla state.
3. Add the on/off (and any tuning) option to [Randomizer.h](../../include/Randomizer/Randomizer.h)
   and wire it into `reinit()` in the correct order relative to Moon Rando.
4. Cache last-inputs and early-return when nothing changed.
5. If it affects reachability, model it in the logic graph **and** mirror it in
   [logic/randomizer_logic.py](../../logic/randomizer_logic.py); add a test in
   `logic/test_randomizer_logic.py`.
6. Keep generation cheap — it runs on-console whenever settings change.
7. When changing Moon Allocation, update both the C++ engine in
   [MoonRando.cpp](MoonRando.cpp) and the host mirror in
   [logic/moon_allocation.py](../../logic/moon_allocation.py) in the same change;
   keep parity fixtures and the role-layout hash contract synchronized.
8. Keep the player-facing labels and behavior synchronized with
   [moon-randomizer.html](../../docs/moon-randomizer.html), including the default
   off state, mandatory floors, progression guarantees, and save compatibility.

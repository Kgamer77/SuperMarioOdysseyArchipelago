# Enemy/NPC memory audit — what the data supports

Generated from `logic/audits/` (see that directory's README for the run set and
the exact import command). 1653 case rows across seven runs: three on schema 2,
whose memory columns are discarded, plus four on schema 3 -- a calibration, an
Entity Lab, a baseline survey and an adaptive request run -- which carry the
measurements.

## Summary

| Question | Answer |
|---|---|
| Which swaps are safe? | **Answered.** 48 of 63 targeted entities are clean everywhere they were tried. |
| Is any entity globally incompatible? | **Yes — 10**, which have never once been created successfully. A further one (Koopa) is safe at one instance and fails at two. |
| Can memory be measured at all? | **Yes.** Ten identical loads used to span 23–51 MB; on schema 3 they span 0–8 KB. |
| What does one swap cost? | **Answered.** Archive 0–20 MB, scene instance 90 KB – 4.1 MB. Tables below. |
| What does a 2nd instance cost? | **Answered.** 87 KB – 770 KB of scene, no extra archive. |

## Compatibility (usable)

`entity_verdict_summary.csv` is the roll-up. 63 entities were targeted at least
once; an entity is bucketed by its worst outcome, so anything not `clean` failed
somewhere even if it also has clean placements.

| Bucket | Count | Entities |
|---|---|---|
| clean | 48 | Bubble, Bull, Byugo, Cactus, CactusMini, CapMan, CityMan, CityManLow, CityManSit, DesertMan, Donsuke, ForestMan, Frog, Fukankun, GabuZou, Gamane, GrowPlantSeed, Grower, GrowerBug, GunetterMove, HackFork, Hosui, Kakku, KaronWing, KuriboMini, KuriboWing, LakeMan, LavaMan, Megane, PackunFire, PackunPoison, Pecho, Pukupuku, PukupukuSnow, Radish, RocketFlower, SeaMan, SnowMan, StatueKoopa, TRex, TRexPatrol, Tank, Togezo, Tsukkun, Utsubo, Wanwan, WanwanBig, Yukimaru |
| crash suspect | 4 | Koopa (**global**, 19 clean contexts / 2 failing), Imomu (18/1), Poetter (2/1), SphinxRide (2/1) |
| creation failed | 11 | Motorcycle (**63 clean contexts / 1 failing**), KuriboPossessed, FireBrosPossessed, HammerBrosPossessed, KillerLauncher, KillerLauncherMagnum, LongGenerator, MummyGenerator, PopnGenerator, ReflectBombGenerator, SenobiGeneratePoint |
| never targeted | 49 | props only: trees, benches, streetlights, signs, boxes, poles, tables |

The seven generators in the creation-failed bucket also recorded
`generator_unactivated` (spawned no children); the harder failure wins.

### 10 global entity defects, plus one instance-count defect

This is the result the second Entity Lab produced. Until it ran, every failure
sat in `CityWorldFactoryStage` sc4 — a context that contradicted itself, marking
Koopa and Motorcycle both `supported` and `creation_failed` — so nothing could
be blamed on the entity. The lab re-ran the same pool in
`LavaWorldCostumeStage` sc1, an unrelated kingdom, and the same 12 entities
failed the same way. The later request run added a third context,
`CityWorldFactory01Zone` sc4, which reproduced Koopa again:

| Entity | Failure | Contexts |
|---|---|---|
| KuriboPossessed, FireBrosPossessed, HammerBrosPossessed | `creation_failed` | CityWorldFactoryStage:4 + LavaWorldCostumeStage:1 |
| SenobiGeneratePoint, MummyGenerator, KillerLauncher, KillerLauncherMagnum, PopnGenerator, LongGenerator, ReflectBombGenerator | `creation_failed` | ″ |
| Koopa | `suspected_crash` + `creation_failed` | ″ + CityWorldFactory01Zone:4 — **but only at 2 instances**, see below |

Two independent contexts is the bar for `evidence_scope = global`, and these are
the first entities to clear it.

**Split these by instance count before acting on them.** `evidence_scope` says
*where* an entity failed, not *how*, and the two are different defects:

| Entity | n=1 ok/fail | n=2 ok/fail | Reading |
|---|---|---|---|
| Koopa | **34 / 0** | 0 / 8 | Instance-count defect only. One Koopa is clean in 34 cases across 19 contexts; two never worked once. |
| KuriboPossessed, FireBrosPossessed, HammerBrosPossessed | 0 / 10 | 0 / 2 | Never created successfully, at any count. |
| MummyGenerator, KillerLauncher, KillerLauncherMagnum, PopnGenerator, LongGenerator, ReflectBombGenerator | 0 / 16 | 0 / 8 | ″ |
| SenobiGeneratePoint | 1 / 16 | 0 / 8 | ″ (the single success is not reproducible) |

So the ten are pool removals — they have no working placement to protect. Koopa
is not: it is a `max_instances = 1` rule, and removing it from the pool would
throw away 34 good placements to avoid a case that only arises when the same
target is picked twice in one stage.

The remaining three stay single-context and should not be widened:

| Entity | Failure | Context | Counter-evidence |
|---|---|---|---|
| Motorcycle | `creation_failed` | CityWorldFactoryStage:4 | clean in **63** other contexts |
| Imomu | `suspected_crash` | CityWorldHomeStage:1 | clean in 18 |
| Poetter | `suspected_crash` | CityWorldFactoryStage:4 | clean in 2 — but **5 of 10** single placements failed |
| SphinxRide | `suspected_crash` | CityWorldFactoryStage:4 | clean in 2 — but **5 of 10** single placements failed |

Poetter and SphinxRide are single-context by the evidence rule, but a 50%
failure rate on single placements is not a benign result; they are the strongest
candidates for the next retest.

Motorcycle is the clearest demonstration that the scoping rule works: it fails
only in the context that fails everything.

**Koopa's crash is understood.** `Koopa::init` →
`al::JointControllerKeeper::pushBackController` with a null keeper. The existing
Koopa mitigations were active (the audit sets `sScope.className`, so
`isKoopaReplacementActive()` fires); the sub-actor skip at
`src/Randomizer/EnemySwapHooks.cpp:2305` assumes nothing later pushes a
controller onto that sub-actor, and on these placements something does. That
hook's own comment already flags it as the least-understood fix in the file.

### What ships

`config/enemy_spawn_restrictions.csv` carries 21 rows, all `status=suspected`,
each scoped to the exact stage+scenario observed. It is a copy of
`enemy_spawn_restrictions.proposed.csv` in this directory, which the importer
regenerates; keep them in step.

`tools/generate_enemy_swap_db.py` emits **only `status=confirmed` rows**, so
`EnemySwapDB.h` currently ships `sEnemySpawnRestrictionCount = 0` and
`kCompatibilityDataVersion = 0` — nothing is blocked. Both runtimes still
enforce whatever the table contains (`EnemySwapRando::spawnRestricted`,
`SwapConfig.restrictions_for`), so promotion is a data change, not a code
change:

- **To enforce the suspected rows now:** regenerate with
  `--enforce-suspected`. That yields 21 restrictions and
  `kCompatibilityDataVersion = 1`.
- **To promote one row permanently:** retest it (a second crash makes it
  `confirmed_crash`, and a surviving allocation failure or a repeatable null
  creation is confirmable evidence per plan section 16.6), then set its
  `status` to `confirmed` in the config.

**A restriction row cannot express a global defect.** `SpawnRestriction` is
keyed on stage+scenario by design and is documented as "never widened to the
entity as a whole", so promoting these rows to `confirmed` only blocks them in
the two or three contexts they happened to be tested in -- they stay selectable
everywhere else. Two different changes are needed:

- **The ten never-created entities:** `swap_out = false` in
  `config/enemy_swap_entities.csv`. They have no working placement to lose.
- **Koopa:** a global `max_instances = 1`, which no current mechanism
  expresses. Either a restriction row with a wildcard stage or an entity-level
  cap column would do it; a per-stage row would leave it uncapped in the ~200
  contexts it was never tested in.

That is the open decision, and it is the one that actually delivers "never
crash".

Note that with nothing enforced, every one of these remains selectable --
including Koopa, which has now crashed a run in three separate contexts and was
the cause of the repeated reboots during the request run.

Note that the `SphinxRide` / `Poetter` crashes persist in the newest run
(`audit_20306876643_docked`) even though the camera-ticket guard at
`src/Randomizer/EnemySwapHooks.cpp:333` was already in that build. That matches
the follow-on risk recorded when the guard was written: `SphinxRide::init` also
starts an event flow from the foreign placement's `EventName`.

## Memory — fixed, and verified by calibration

`audit_2027927865_docked` is the first run on schema 3. Its thirty
`vanilla_anchor_a` cases are ten identical loads of each of three stages, so
their spread *is* the instrument's noise floor:

| Context | schema 3 spread | schema 2 |
|---|---|---|
| Cascade `WaterfallWorldHomeStage` sc2 | **0 bytes** | 23–51 MB |
| Lake `LakeWorldHomeStage` sc4 | **8,192 bytes** | ″ |
| Metro `CityWorldHomeStage` sc3 | **0 bytes** | ″ |

A per-entity cost of a few hundred KB was previously invisible under 7–15 MB of
stdev. It is now three to four orders of magnitude above the noise.

**The world resource heap is the archive heap.** `world_resource_valid = 1` on
every row, size 896 MB, free-at-init differing per kingdom (634 / 713 / 737 MB)
— which is why the 512 KiB `scene_resource` heap never moved. Its spread across
repeats is also 0 bytes.

`scene_pre_transition_free_bytes` now records the value that used to *be* the
measurement: the first anchor of a context reads 163,312,776 pre-transition
against 186,676,624 at init — the 23 MB of previous-stage state that corrupted
every schema-2 delta.

Still dead after 1160 rows, and treated as unavailable rather than measured:
`scene_resource` (constant 409168 free), `process_*` (Ryujinx reports the
reserved arena, not live usage), `stationed` (`valid = 0` in every row).

### The measured costs

From the schema-3 Entity Lab (`audit_1719659187_docked`, 146 supported cases in
`LavaWorldCostumeStage` sc1). Acceptance passed: **0 of 43 first-instance costs
negative**, against 35-of-40 on schema 2.

A swap costs two separate things, and the existing `EntityMemoryCost` model
already separates them:

- **archive** → world resource heap → `firstResourceBytes`, charged **once per
  stage** no matter how many copies are placed;
- **instance** → scene heap → `firstInstanceBytes` / `additionalInstanceBytes`,
  charged **per placement**.

| Entity | Archive | 1st instance | Each extra |
|---|---|---|---|
| Koopa | 20.0 MB | 1.43 MB | — |
| TRex | 11.8 MB | 632 KB | 509 KB |
| Tank | 7.4 MB | 468 KB | 337 KB |
| Donsuke | 5.8 MB | 652 KB | 337 KB |
| Yukimaru | 4.9 MB | 1.02 MB | 705 KB |
| WanwanBig | 4.5 MB | 456 KB | 320 KB |
| Hosui | 2.8 MB | 4.13 MB | 410 KB |
| Imomu | 2.3 MB | 1.14 MB | 771 KB |
| typical enemy | ~2 MB | 90–600 KB | 90–500 KB |

33 of 53 entities loaded an archive; the other 20 cost **zero** because theirs
was already resident — TRexPatrol shares TRex's, and the NPC pool shares
archives across the CityMan/ForestMan family. 97 MB of archives accumulated
across the whole run.

**Archive dominates.** Koopa's archive is 14× its instance cost, so what makes a
seed expensive is the *number of distinct entities* in a stage, not the number
of actors — which is exactly the failure mode the diverse stress ladder was
built to probe.

Second-instance cost is 12 of 42 negative on the scene heap, but at −12 KB to
−78 KB that is allocator placement noise, not the multi-MB nonsense of schema 2.

### Per-stage headroom

The schema-3 Baseline Survey (`audit_31725982529_docked`) covers **208 of 214
contexts** with valid metrics on every row. The six gaps are the five subareas
the audit cannot arrive at cleanly plus `SandWorldHomeStage` sc5 — the same set
that failed the schema-2 survey, so they are an arrival problem, not a stage
problem.

| | Tightest | Widest |
|---|---|---|
| archive headroom (world resource) | 335 MB — `CityWorldHomeStage` sc8 | 842 MB |
| scene headroom | 59.5 MB — `ForestWorldHomeStage` sc5 | 272 MB |

Wooded and Metro overworlds are consistently the tightest on scene; nothing is
close to tight on archives — the tightest stage still fits 16 Koopa-sized
archives or ~159 typical ones.

**The two budget terms had to be re-paired.** `resource_headroom` was being read
from the `scene_resource` heap, a constant 512 KiB with 409,168 bytes free in
every context ever recorded, while entity `first_resource_bytes` now comes from
the world resource heap where a Koopa archive is 20 MB. A 20 MB cost against a
409 KB budget would have rejected every seed. Headroom now reads the same heap
the costs do.

**Not yet promoted.** `config/enemy_swap_costs.csv` still holds the 2 MiB
placeholders and `config/enemy_swap_stage_budgets.csv` is still empty, so
`EnemySwapDB.h` ships `sStageMemoryBudgetCount = 0` and memory admission is
inert. `stage_budget_proposals.csv` carries the numbers and every row is
`review_required`; the reserve column is the tunable knob and is currently zero,
which would let a seed consume 100% of measured headroom.

Still dead, and treated as unavailable rather than measured:
`scene_resource` (constant 409168 free), `process_*` (Ryujinx reports the
reserved arena), `stationed` (`valid = 0` in every row).

---

## Why schema 2's memory columns are discarded (historical)

Kept because it explains the numbers in the schema-2 artifacts, which the
importer now blanks on load.

### 1. Three of four heap domains never moved

Across all 1097 schema-2 rows:

| Metric | Observed | Distinct values |
|---|---|---|
| `scene_resource_size_bytes` | 524288 | 1 |
| `scene_resource_free_init_bytes` | 409168 | 1 |
| `process_used_init_bytes`, `process_peak_used_bytes` | 3429978112 | 1 |
| `stationed_valid` | 0 | 1 |
| `scene_free_init_bytes` | varies | 200+ |

The scene-resource heap is a fixed 512 KiB whose free size never changed once,
in any stage. `al::getWorldResourceHeap()` — the likely home of actor archives —
is declared in `lib/OdysseyHeaders/al/Library/Memory/HeapUtil.h:14` and is never
sampled. Process metrics are constant because Ryujinx reports the reserved
arena, not live usage. The stationed heap was never validly sampled at all.

Only the scene heap carries any signal.

### 2. The scene heap is sampled before the stage transition

`prepareCase` takes the first `observeSnapshot()` of a case *before* the
transition, and `observeHeapMetric` seeds `freeInitBytes` from the first
observation. So every `scene_free_init_bytes` describes **the previous stage**.

It is visible directly in the entity_lab ordering — case N's `free_init` keeps
equalling case N−1's `min_free`:

```
seq  7  Wanwan     free=235943312  min=235636112
seq  8  WanwanBig  free=235636112  min=235636112
seq  9  TRex       free=235636112  min=235464080
seq 10  TRexPatrol free=235464080  min=235464080
seq 11  Fukankun   free=235464080  min=235464080
```

### 3. The baseline is bimodal and the noise dwarfs the signal

Anchors A and B of the *same* lab stage differ by **72,655,112 bytes**; the
entity_lab's supported rows cluster into a 163.3 MB mode (n=3) and a ~235.7 MB
mode (n=137). Separately, the calibration run's ten repeats of an identical
`vanilla_anchor_a` span 23–51 MB (stdev 7–15 MB), against a target signal of
0.2–4 MB. Its suppression anchors, by contrast, span only 24–188 KB.

### 4. The result is physically impossible

Measuring the second instance without any anchor — `free(target_1) −
free(target_2)` — gives a negative cost for **35 of 40** entities, i.e. adding a
second copy of an enemy appeared to free memory. Median −458,752 bytes.
`Motorcycle` reports −73,347,336.

All four defects are fixed in schema 3, and the calibration above is the proof.
The schema-2 memory columns are blanked on load
(`MEMORY_UNTRUSTED_SCHEMA_VERSIONS`), which is why every cost in
`entity_memory_costs.csv` currently reads zero — those rows come from schema-2
runs and none of them may be promoted.

## Run order

| # | Run | Cases | Time | Pass condition | State |
|---|---|---|---|---|---|
| 1 | Calibration | 60 | ~15 min | 10 identical anchors spread under ~1 MB | **passed** — 0–8 KB |
| 2 | Entity Lab | 177 | ~50 min | no negative first-instance cost | **passed** — 0 of 43 |
| 3 | Baseline Survey | 214 | ~95 min | per-stage headroom on schema 3 | **passed** — 208 of 214 |
| 4 | Stress Test | ~120/kingdom | ~30 min | ladder advances, limits land in `stage_stress_limits.csv` | **passed** — Wooded, no rung failed |
| 5 | SD Request | ~340 | ~60 min | retests that can promote suspects to `confirmed` | **passed** — 70 of 103 contexts |
| 6 | Depth Sweep: Enemies/Captures | ~320/context | hours | per-entity `max_instances` in `entity_depth_limits.csv` | **next** |
| 7 | Depth Sweep: Props | ~300/context | hours | the prop pool's first audit evidence of any kind | |

The measurement runs have all passed and the numbers needed to constrain seed
generation exist today. What is missing is the *duplicate* axis: Koopa is clean
at one instance in 34 cases and has never survived two, and almost every other
entity has only two cases at n=2 -- too thin for a second defect of that shape
to have shown up. Depth Sweep is the run that closes it.

Historical note on ordering: Baseline Survey was moved ahead of Stress because
stage budgets are what actually constrains seed generation, and nothing could be
promoted into `config/enemy_swap_stage_budgets.csv` without schema-3 headroom.

Three open decisions, all yours — nothing is enforced today:

- **Set the budget reserves.** `stage_budget_proposals.csv` currently reserves
  zero, so `max_extra = headroom` and a seed could consume every measured byte.
  `enemyRandoPlan.md` section 18 proposes 4 MiB resource / 1 MiB scene as
  starting floors, authored before any of these numbers existed; against a
  59.5 MB tightest scene stage they are thin. Picking them turns memory
  admission on.

- **Promote the 10 never-created entities to `confirmed`?** They are reproduced
  in two contexts, which plan section 16.6 treats as confirmable for repeatable
  null creation, and they have no successful placement to protect. Today they
  sit at `suspected`, so the generator emits nothing and they are not blocked.
  Note a restriction row cannot express this on its own -- see "What ships".
- **Promote the measured costs into `config/enemy_swap_costs.csv`?** They are
  sound but come from one lab context; a second lab context would tell us
  whether archive size is context-independent (it should be).

Step 2 doubles as the check that the audit is actually replacing actors at all:
`actor_created_count` of `0` on a target row means the placement hooks are not
calling `EnemySwapMemoryAudit::getPlacementDecision` /
`noteActorCreateEnd`, and every memory number in the run is meaningless.

Use **one save file** for the whole campaign — the importer refuses to merge
runs whose `save_fingerprint` differs.

## Depth Sweep mode

Walks one entity's duplicate ladder at a time: 1, 2, 3, 4, 6, 8 copies of the
same target, capped at the context's eligible placement count. Where a Stress
rung asks "how many swaps does this stage take", a Depth rung asks "how many
copies of *this entity* does it take", which is the question a `max_instances`
restriction is written in.

The ladder is deliberately denser at the low end than the stress ladder. Koopa
fails between 1 and 2; on a doubling ladder its limit reads as "somewhere below
2" and the run stops there, learning nothing about 3 or 4.

- **Entity-major order.** Every rung of one entity runs before the next entity
  starts, so a crash resumes onto a fresh entity instead of replaying ladders
  that already completed.
- **One entity's failure closes only that entity.** The context is never marked
  unstable and no other ladder is affected -- a run is expected to find several
  limits, not stop at the first.
- **Scoped by pool.** `Depth Sweep: Enemies/Captures` and `Depth Sweep: Props`
  are separate menu actions writing separate runs. Props have never been
  audited in any run, so mixing them in would let a prop crash end the combat
  sweep before it finished.
- Output is `entity_depth_limits.csv`: one row per entity per context, with
  `max_supported_instances`, `first_failing_instances` and the per-instance
  scene slope.

It reuses the Stress rank table, so a rung places its copies into the same
highest-ranked eligible placements a stress rung would, and shares the
eligibility floor and densest-first ordering.

## Stress Test mode

Pause menu → audit page → **Stress Test This Kingdom**. It is scoped to the
kingdom you are standing in and it crashes on purpose when it finds a limit;
crash-resume is the mechanism that records the limit and moves on.

Per context it runs a shared vanilla anchor and then two ladders at
N ∈ {1, 2, 4, 8, 16, 32}, truncated to the context's eligible placement count
and always ending on that full count:

- **uniform** — N copies of one entity, so the rung isolates instance cost;
- **diverse** — N placements each a different entity from the combat pool,
  which is what a real randomized seed looks like and the likelier OOM path.

Only `enemy` and `capture` placements take part. NPCs are excluded because
interaction-driven ones must stay vanilla; generators because their children,
not the rung, would dominate the measurement.

A failing rung ends that ladder only. It never marks the context unstable and
never quarantines an entity — a diverse rung would otherwise blame whichever
entity happened to be named in the row.

The importer derives `stage_stress_limits.csv` from the rows:
`max_supported_instances` is the highest rung that loaded,
`first_failing_instances` the one that did not (`-1` when the ladder simply ran
out of placements), and `scene_bytes_per_instance` is a least-squares slope
across every surviving rung — a slope over 1/2/4/8/16 is what makes the
per-instance cost separable from the noise that sank the schema-2 pairs.

## What schema 3 changed

All verified by `audit_2027927865_docked` except where noted.

1. **Baseline fixed.** `onStageInitEnd` now calls
   `Metrics::rebaselineSnapshotAggregate` before the first snapshot that
   counts, so `freeInit` describes the stage being measured instead of the one
   the audit came from. The old value survives as the diagnostic column
   `scene_pre_transition_free_bytes`.
2. **World resource heap sampled.** `al::getWorldResourceHeap()` is now in
   `captureSnapshot`, with `world_resource_*` columns and a place in the cost
   and calibration metric sets.
3. **Entity lab moved.** `PRIMARY_ENTITY_LAB_STAGE` is now
   `LavaWorldCostumeStage` scenario 1 — the qualifying context with the most
   scene headroom (268.3 MB) and a clean anchor. The context table hash is
   unchanged, so earlier runs still import.
4. **Repetitions raised.** Cost-measuring probes now request
   `MIN_COST_PROBE_REPETITIONS = 3` instead of 1.
5. **Case ids include the plan ordinal**, so two slots in one context can no
   longer collide on one attempt id.

6. **The placement hook is wired.** `notePlacementObserved`,
   `getPlacementDecision` and `noteActorCreateBegin/End` had no callers at all,
   so discovery found no sources and every run died at startup. They are now
   called from `createPlacementActorHook`, reporting **only** placements the
   audit decided on plus their nested children — the counters mean
   "suppressed" at zero and "target built N times" otherwise, so counting
   ordinary stage actors made suppression cases look like creation failures.

Still open: the stationed heap reports `valid = 0` in every row across all four
runs, and the cause is unknown — either `al::getStationedHeap()` is null at
snapshot time or `getMaxAllocatableSize(8)` returns 0 there. Process metrics are
constant under Ryujinx. Both are treated as unavailable rather than measured.

Run the whole campaign on **one save file**: the importer refuses to merge runs
with different save fingerprints. Mixed *schemas* are fine — only the fingerprint
and the identity hashes have to agree.

**Acceptance before any cost is promoted:** no negative second-instance cost, and
a calibration spread below the smallest measured entity cost. The second half is
already satisfied at 0–8 KB.

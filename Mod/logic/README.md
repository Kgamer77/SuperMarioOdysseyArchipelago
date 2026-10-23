# Randomizer Logic Templates

Enemy/capture replacement has separate architecture and authoring references:

- [Enemy randomizer architecture](../docs/enemy-randomizer-architecture.md)
- [Capture-gate authoring](../docs/enemy-randomizer-authoring.md)
- [Implementation handoff](../docs/enemy-randomizer-implementation.md)

The logic should be a reachability graph, not one giant requirements cell per
moon.

Use `logic_nodes.csv` for meaningful places, `route_edges.csv` for movement
between places, and `moon_access.csv` for the final local action needed to
collect a moon once a place is reachable. This handles entrance randomizer
cases cleanly because entering an area from a different side can land on a
different node.

## Files

- `templates/logic_tokens.csv` lists canonical ability and capture tokens for
  requirement expressions, plus generated event tokens such as
  `WorldPeace_Cap` and `MoonRockActive_Cap`.
- `templates/rooms.csv` lists every room/stage known from the stage warp list,
  plus extra stages discovered from moons, entrances, and extracted stage data.
- `templates/logic_nodes.csv` lists starts, known entrance sides, checkpoints,
  generated world-state event nodes, and raw stage-change points found in local
  game data.
- `templates/route_edges.csv` is the movement graph. It starts empty by
  design; add every movement/transition connection manually with the
  abilities/captures needed to move from `from_node_id` to `to_node_id`.
  `difficulty` marks the movement skill level required for each route option.
- `templates/moon_access.csv` lists every `ShineDB` moon entry. Fill
  `from_node_id` with the node where the moon is locally reachable and
  `requirement_expr` with any extra local requirement. `effort_level` is a
  logic-weighting hint for settings that bias toward faster or slower moons.
- `templates/entrance_links.csv` mirrors `EntranceDB.h` and ties each vanilla
  entrance side to the generated node ids. Rows marked `excluded` come from
  valid commented-out links in `EntranceDB.h`; keep them as reference unless
  that entrance becomes part of the randomizer.
- `templates/painting_links.csv` defines default warp-painting links between
  `Stage Change Go` and `Stage Change Come` nodes. These links are consumed by
  the logic runner and runtime as a separate table so painting targets can be
  randomized later without changing the movement graph format.
- `templates/capture_gates.csv` and `templates/capture_gate_objects.csv`
  normalize the directional capture alternatives authored on the editor's
  **Capture Gates** page. Export these from per-kingdom editor saves; do not
  maintain a separate hand-edited copy.
- `templates/progression_events.csv` defines world-peace and moon-rock-active
  event tokens per kingdom. Fill this when you know which story moons/bosses
  unlock world peace or where/how the Moon Rock is activated.
- `templates/runtime_anchors.csv` is the explicit bridge from logic nodes to
  stable in-game identifiers: stage names, entrance/change-stage ids,
  checkpoint obj ids, coordinates, and event tokens.
- `templates/raw_stage_points.csv` is a reference dump of checkpoints and
  stage-change placements extracted from `romfs/StageData`.

The same data is also emitted in smaller chunks:

- `templates/by_kingdom/<kingdom>/` contains per-kingdom CSV slices for humans.
- `templates/area_graphs/<kingdom>/<stage>.json` contains one loadable graph
  bundle per room/subarea for tooling or a future editor.

## Runtime Mapping

Human names are labels only. The checker should key off stable node ids and
anchors:

- Entrances/exits: `stage + ChangeStageId + side`, from `entrance_links.csv`
  and `runtime_anchors.csv`.
- Warp paintings: `stage_change` nodes with `WorldWarpHole` anchors, linked by
  `painting_links.csv`.
- Checkpoints: `stage + checkpoint obj_id`.
- Stage starts/rooms: `stage + stage_start`.
- World state: event tokens like `WorldPeace_Cap` and `MoonRockActive_Cap`.

Yes, keep all entrance and exit names. They are how a randomized entrance maps
back into the graph. If a user creates a human region such as `Top Hat Tower`,
that region should be connected by route edges from one or more concrete
anchors, for example `CapWorldHomeStage::checkpoint::obj2497` or
`CapWorldHomeStage::stage_change::Goal::raw`.

## Areas

`area_scope` follows actual loading boundaries. WorldMeta stages are
`overworld`; stages reached through a real stage load remain `subarea`.
Archives listed in `embedded_zones.csv` are geometry loaded in place by one of
those stages, so their nodes, moons, and routes are folded into the parent
graph. Stable node ids and physical moon keys retain the embedded archive name.

An embedded archive's generated start becomes an ordinary landmark. It is not
seeded as a separately reachable area; authored routes inside the parent graph
must reach it.

## Requirement Expressions

Use token names from `logic_tokens.csv`.

- `free` means no requirement.
- `impossible` means intentionally unreachable.
- `&` means all requirements are needed.
- `|` means any branch works.
- Parentheses are encouraged whenever mixed with `&` and `|`.
- Put world-state checks in `moon_access.csv` `state_requirement_expr`, and put
  local movement/capture checks in `requirement_expr`.

Example:

```text
Frog | (TripleJump & Vault & WallJump & Dive)
```

State example:

```text
WorldPeace_Sand & MoonRockActive_Sand
```

## Node Access Requirements

An option can also require the player to be able to *reach another node*, on top
of (or instead of) any tokens. Write it as `@` followed by the node id:

```text
Frog | (Tsukkun & @SkyWorldHomeStage::checkpoint::obj1626)
```

Use it when an option depends on something the player must have gotten to
elsewhere — a switch that stays pressed, a platform that only lowers from the
far side, a gate opened from the other room — rather than on a movement skill.
The requirement is satisfied when that node is reachable in the current state,
so it applies to `route_edges.csv` `requirement_expr` and to both
`moon_access.csv` requirement columns.

In the graph editor, each OR option has a **Node Access** row with an **Add
Node** button; the dialog searches every node in every kingdom by id, name,
stage, or type, and one option can require several nodes (they are ANDed).

Node access is part of the OR option, so it lines up with that option's
`difficulty` and `option_scenarios` entries like any other requirement. A node
id that no longer exists turns the whole expression into a validation error
rather than silently disappearing.

Because reaching a node can open an edge that leads to more nodes, reachability
is solved as a fixed point: the sweep repeats until nothing new is reached.
Cycles are therefore safe (two nodes each gated on the other simply stay shut),
and No Logic ignores node access along with every other authored requirement.

## Filling Pattern

Prefer adding or renaming nodes until each node is a place a human can reason
about. A route should describe movement between nearby logical places, not a
full kingdom traversal if that traversal can be split into useful landmarks.

For the Cap Kingdom Captain Toad example:

1. Add or rename a node for `Top Hat Tower`.
2. Put `Good Evening, Captain Toad!` in `moon_access.csv` at that node with
   `requirement_expr=free`.
3. Add the route from Cap start to that node in `route_edges.csv`:

```text
Frog | (TripleJump & Vault & WallJump & Dive)
```

If a randomized entrance or checkpoint can place the player at that same node,
connect that entrance/checkpoint node to `Top Hat Tower` with `free`. The moon
does not need duplicate requirements for every possible origin; the graph does
the work.

For moons gated by story state or Moon Rocks, leave the route graph focused on
physical movement and add the kingdom event token in `state_requirement_expr`.
The event itself lives in `progression_events.csv`, where its unlock condition
can be filled once story dependencies are mapped.

## Effort Levels

Each moon has an `effort_level` field. The editor constrains this to:

```text
unrated, low, mid, high
```

Use this for probability settings in the logic processor, separate from whether
the moon is logically reachable or technically difficult. For example, `low`
can mean quick once the source node is reached, while `high` can mean slow,
boring, out-of-the-way, or time-consuming. Put any nuance in `effort_notes`.

## Route Difficulty

Each route option has a `difficulty` value. The editor constrains this to:

```text
casual, advanced, expert, glitched
```

Use this for movement/trick difficulty. The `difficulty` CSV column is aligned
with the top-level OR options in `requirement_expr`; for example,
`casual;expert` means option 1 is casual and option 2 is expert. A single value
applies to every option, so older saves still work. The logic runner and Switch
runtime treat the selected logic difficulty as a maximum: `casual` allows
casual options, `advanced` allows casual and advanced options, `expert` allows
those plus expert options, and `glitched` allows everything — it is Expert plus
the glitch-only options. Missing values from older saves default to `casual`.

Reserve `glitched` for options that need a glitch or unintended engine
behaviour rather than merely demanding execution; demanding-but-intended tricks
stay on `expert`.

Capture-gate alternatives use the same `casual`, `advanced`, `expert`, and
`glitched` levels. A gate can only supply its replacement capture when its
level is at or below the selected logic difficulty; omitted values from older
gate saves are treated as `casual`. The assignment solver validates every
difficulty level, including capture-dependent individual moon requirements.

## Regenerating

The pipeline has a strict order, and running step 1 out of order can silently
destroy hand-authored logic:

1. `python logic\generate_logic_templates.py` -- (re)builds the base CSV
   templates from a clean/updated data extraction (`ShineDB.h`, `EntranceDB.h`,
   abilities, captures, `romfs/StageData`). Run this ONCE per data change,
   before any authoring happens on top of it.
2. Author/edit logic in the graph editor (or hand-edit the CSVs) and save.
3. `python logic\export_saves_to_templates.py` -- merges the graph-editor
   kingdom saves back into the editable `templates/{logic_nodes,route_edges,
   moon_access,capture_gates,capture_gate_objects}.csv`.
4. `python logic\generate_runtime_logic.py` -- compiles the CSVs into
   `include/Randomizer/LogicData.h`.
5. `python tools\validate_capture_gates.py` -- strictly validates placement,
   policy, gate coverage, scenarios, and global assignment satisfiability
   before the enemy-swap database is generated.

```powershell
python logic\generate_logic_templates.py
# ... author logic in the graph editor, then save ...
python logic\export_saves_to_templates.py
python logic\generate_runtime_logic.py
python tools\validate_capture_gates.py
```

Do not re-run `generate_logic_templates.py` after step 2/3 without care. Every
kingdom save's `routeEdges` are merged by `export_saves_to_templates.py`
(and normalized by the editor itself, `workspace.ts`'s `normalizeRouteRows`),
and both treat any row whose `source` column is `generated` as
non-authored/regenerable and silently drop it when merging a save back into
the templates. If a route edge you rely on ever carries (or picks up)
`source=generated` -- including rows reconstructed by re-running
`generate_logic_templates.py` -- the next `export_saves_to_templates.py` run
will drop it, effectively wiping that authored work. If you must regenerate
templates after authoring has started, migrate embedded-zone relationships
first if they changed, and re-export carefully, checking the row counts each
step reports:

```powershell
python logic\migrate_embedded_zones.py
python logic\export_saves_to_templates.py
```

The stage-data extraction uses the Python `oead` package. If it is unavailable,
the generator will still emit the core templates but skip raw checkpoints and
stage-change placements.

## Graph Editor

The Vue editor lives in `logic/graph-editor` and loads the generated template
CSVs directly.

```powershell
cd logic\graph-editor
npm install
npm run dev
```

Open `http://127.0.0.1:5174/`. The editor can autosave locally, import/export a
project JSON, export edited `logic_nodes.csv`, `route_edges.csv`, and
`moon_access.csv`, and save a dev-server snapshot under
`logic/graph-editor/saves/`.

Requirements are edited as OR groups of AND tokens, so
`Frog OR Triple Jump + Vault + Wall Jump + Dive` is stored as:

```text
Frog | (TripleJump & Vault & WallJump & Dive)
```

Run the editor tests with:

```powershell
cd logic\graph-editor
npm test
```

## Logic Runner

`logic/randomizer_logic.py` is the host-side reference runner. It can read
either the generated CSV folder or a Logic Editor project JSON export:

```powershell
python logic\randomizer_logic.py --seed 12345 --input logic\templates
python logic\randomizer_logic.py --seed 12345 --project logic-editor-project.json --logic-difficulty advanced
```

The host runner also accepts `--all-kingdoms-start-unlocked` to expose every
kingdom immediately while retaining the configured route tiers, and
`--randomize-start-kingdom-state` to use the same per-kingdom initial-scenario
selection as the Switch runtime.

It writes:

- `logic/out/logic_result.json`: machine-readable moon placements, entrance
  placements, and path spheres.
- `logic/out/SpoilerLog.txt`: human-readable moon placements, entrance routes,
  and a suggested progress path.

The runner treats missing route data as free, so partially-authored graphs stay
usable while humans fill them in. Scenario availability uses the safe suffix
rule: a moon listed as available in `1;3;4;5` is considered available from
scenario `3` onward, because the player may otherwise be stuck in scenario `2`.
Non-normal scenarios such as Balloon World, KFR/demo scenarios,
volcano-less Luncheon, Bruncheon, morning Metro, and 8-bit Festival are ignored
for persistent logic.

Scenario state follows the runtime scenario hooks: Cap starts in scenario `3`,
Cascade starts in scenario `1`, Mushroom starts in World Peace, peace scenarios
normalize to post-game, and scenario values only move forward. With randomized
start state enabled, the host runner applies the seed-selected pre-peace,
post-peace, or Moon Rock scenario and its matching state events before
reachability begins. Story moon order is inferred from ShineInfo hint order
inside each kingdom; collecting the next story moon advances toward the next
story scenario, and the final story moon grants world peace/post-game. Moon
Rock state is otherwise only granted after the `MoonRockActive_*` event node is
reachable.

## Moon Scenario Audit

Audit every extracted moon against the raw `ShineInfo`, `WorldList`, and
StageMap archives with:

```powershell
python logic\audit_moon_scenarios.py
```

The human-readable report is written to
`logic/reports/moon_scenario_audit.csv`. The JSON report beside it contains
source hashes, all evidence, expected old values, and replacement values. Its
`proposed_changes` array is a complete input for a later fix pass, so applying
the reviewed changes does not require scanning or extracting the game archives
again.

The audit decodes `ShineList.ProgressBitFlag` as the authoritative availability
mask (bit zero is Scenario 1). `ShinePosList` is retained only as a positional
cross-check, while zero-based StageMap indices are retained as independent
placement evidence.

## Switch Runtime

The Switch build does not parse the editor project or raw CSVs during play.
Instead, the authored CSVs are converted into compact C++ tables:

```powershell
python logic\generate_runtime_logic.py --input logic\templates --output include\Randomizer\LogicData.h
```

CMake has a `generate_logic_data` target that regenerates
`include/Randomizer/LogicData.h` from the committed CSVs before building. The
runtime moon placer consumes those tables directly, with pre-parsed
requirements, integer node indexes, stage indexes, and entrance anchors.

The in-game spoiler log is generated from the runtime placement itself. It
includes moon placements, entrance mappings, painting links, logic warnings,
and the suggested progress path that the Switch-side logic proved for the seed.

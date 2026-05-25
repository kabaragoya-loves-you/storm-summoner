# Planning Workspace

A long-lived planning document for the storm-summoner project. Kept here in
git rather than in the chat TODO list (which is vulnerable to conversation
compression) so that decisions, open branches, and ideas survive across
sessions.

When starting a new chat, point the agent at this file first. It is enough
context to reconstitute the arc of major refactors without re-reading the
full transcript.

This file should be kept current as work lands. When something here moves
from PENDING to DONE, update it inline and add a one-line note about where
the implementation actually landed (file + symbol) so the next session can
verify in seconds.

---

## How to use this document

1. **Plan archive** — every multi-step effort started has a row in
   [Plan archive](#plan-archive) with status, links, and a one-line outcome.
2. **Active triage** — short-lived feature/bug lists are in
   [Triage A-G](#triage-a-g-from-the-action-component-conversation) and the
   [Scene transition Option A/B/C](#scene-transition-option-abc) sections.
3. **Open backlog** — everything not yet started lives in
   [Open backlog](#open-backlog). When the chat TODO list gets nuked, this
   is the canonical recovery source.
4. **Decision log** — locked-in choices that future sessions should not
   re-litigate live in [Decision log](#decision-log).
5. **Known bugs** — currently-deferred bugs are in [Known bugs](#known-bugs)
   so they don't get forgotten between sessions.

If you're an agent reading this for the first time in a session: skim
sections in order. The cross-references make it possible to find the actual
implementation in the codebase without re-deriving everything.

---

## Plan archive

Plans live at `c:\Users\leastbad\.cursor\plans\*.plan.md` on the dev
machine. The status column reflects how much of the plan actually shipped,
not just how many to-do checkboxes were marked.

| # | Plan | Status | Notes |
|---|---|---|---|
| 1 | `split_action.c_8bee3121.plan.md` | **DONE** | Monolith split into 12 files in `components/action/`. See [Action component layout](#action-component-layout). |
| 2 | `action_component_dedup_pass_39142d3a.plan.md` | **DONE** | All 8 items shipped. See [Dedup pass outcomes](#dedup-pass-outcomes). |
| 3 | `bugs_and_lfo_shape_menu_4a0c0135.plan.md` | **DONE** | F (LFO paused) and G (Set Scene off-by-one) fixed; LFO Shape menu UI shipped. |
| 4 | `scene_transition_graphics_suspend_6c5df1f5.plan.md` | **DONE (phase 1 only)** | Graphics suspend, touch drop, force release shipped. Phase 2 (Option C ADC pre-register, task reuse) is **partial**; see [Scene transition Option A/B/C](#scene-transition-option-abc). |
| 5 | `action_consolidation_pilot_aa594024.plan.md` | **CODE DONE / SMOKE PENDING** | Tempo family fully refactored: enum, dispatch, JSON, schema, menu, migration component, lazy save-back. Awaiting on-device smoke test before deciding to roll out to remaining 14 families. |

---

## Action component layout

Reference for future agents. The monolith refactor produced this structure:

```
components/action/
├── include/
│   ├── action.h                # Public API
│   ├── action_summary.h
│   └── (etc.)
├── action_internal.h           # Cross-module API (handler dispatch, scheduler hooks)
├── action.c                    # Init, execute dispatch, CC cache, action_create_* helpers
├── action_scheduler.c          # Pending queue, repeating, beat/transport handlers
├── action_morph.c              # Morph engine + shared 10ms periodic timer
├── action_boomerang.c          # ADSR envelope engine (hooks morph's timer)
├── action_punch_in.c           # Looper state machine + beat-tick hook
├── action_clock_burst.c        # Clock burst esp_timer
├── action_strings.c            # All _to_string / _from_string / display names
├── action_validation.c         # Hold/trigger/timing/support helpers
├── action_handlers_midi.c      # CONTROL_*, NOTE, RANDOMIZE, SUSTAIN, SOSTENUTO, RESET
├── action_handlers_scene.c     # PRESET_*, SCENE_*, TEMPO, PLAY/STOP/..., CONFIRM, UI_*, TOUCHWHEEL_*, PARAM_*
├── action_handlers_modulation.c # LFO_*, RTG_*, SAMPLE_HOLD_*, STEP, CLOCK_*, CUT_*, PUNCH_IN, FLAG_CEREMONY, BOOMERANG (trigger side)
└── action_summary.c            # In-app summary card formatter
```

`action_execute` chains the three handler dispatchers in MIDI → scene →
modulation order until one returns `ACTION_HANDLED`. New handler files
follow the same pattern.

Separate component (deletable in the future):

```
components/action_migration/
├── include/action_migration.h
└── action_migration.c          # Legacy name → (type, variant) translation, fixup hook
```

---

## Dedup pass outcomes

All 8 items from `action_component_dedup_pass_39142d3a` shipped. Status of
each as of last verification:

| Item | Where it landed |
|---|---|
| Remove dead `action_create_*` helpers | `components/action/action.c` + `action.h` — 13 helpers + decls removed |
| Move `action_validate_scene_timings` decl | Moved from `scene.h` to `action.h` |
| Drop `screensaver` from CMake REQUIRES | `components/action/CMakeLists.txt` |
| Unify display-name tables | Canonical table in `action_strings.c`; `action_summary.c` and `scene.c` document why their tables stay separate (JSON-key stability, summary formatting) |
| Extract `apply_preset_program()` | `action_handlers_scene.c` — used by `ACTION_PRESET`, `_HOLD`, `_CYCLE` |
| Extract `apply_touchwheel_mode_runtime()` | `action_handlers_scene.c` — used by `ACTION_TOUCHWHEEL_HOLD`, `_CYCLE` |
| Extract per-slot LFO helpers | `action_handlers_modulation.c` — `lfo_start_one/stop_one/toggle_one` |
| Extract `try_kickoff_morph()` | `action_handlers_midi.c` — used by `ACTION_CONTROL_HOLD`, `_CYCLE`, `RANDOMIZE` |

**Remaining cleanup the dedup pass left unaddressed** (low priority,
documented at the end of plan 2 as out-of-scope):

- Split `action_handlers_scene.c` into 3 files (preset_scene /
  transport_tempo / touchwheel_ui_param). File is currently ~14 KB / 21
  cases. Becomes mostly moot if the [action consolidation rollout](#consolidation-remaining-families)
  proceeds, since the case count collapses.
- Lift union members (`control`, `boomerang`, `lfo`, `ui`, `preset_cycle`,
  `tempo`) into named typedefs in `action.h`. Cosmetic; defer until a
  consolidation pass requires struct edits anyway.
- Rework `action_summary.c` as data-driven (586 lines, parallel type-name
  table, runtime dependency on `lfo_is_enabled()`/`sample_hold_is_enabled()`
  instead of stored config). Recommend revisit only when adding the next
  Inspect feature.
- Table-driven dispatch. Premature given the three-file split already
  works and consolidation will reshape the table.

---

## Triage A-G (from the action component conversation)

Items A–G are the "what's left" list compiled mid-conversation, with the
explicit design note that **E (consolidation) should land before A/B/C/D**
because A–D are most naturally expressed as variants on the consolidated
actions. F and G were bugs and were fixed independently.

| Tag | Item | Type | Status |
|---|---|---|---|
| A | Preset Hold: "Original" release option | small feature | **PENDING** — natural variant on consolidated Preset action (waits on consolidation rollout) |
| B | Touchwheel Hold: "Original" release option | small feature | **PENDING** — same |
| C | New LFO Hold action | small feature | **PENDING** — same |
| D | LFO Shape Hold / Cycle + fix menu gap | small-medium feature | **MENU GAP FIXED**; Hold/Cycle variants **PENDING** — natural variant on consolidated LFO Shape action |
| E | Consolidate action types via subtype/variant | LARGE refactor | **TEMPO PILOT SHIPPED**, 14 families remaining (Preset, Scene, Touchwheel mode, LFO, Clock, Cut, UI, Param, RTG, S+H, Control, etc.). Gated on `verdict_review` after `device_smoke_test`. |
| F | LFO `start_mode=PAUSED` ignored after programming-mode exit | bug | **FIXED** — `components/scene/scene.c` `scene_resume_input` now applies start_modes for newly-enabled LFOs (plan 3 item 1) |
| G | Set Scene scheduled fires but doesn't switch | bug | **FIXED** — off-by-one in `components/action/action_handlers_scene.c` ACTION_SCENE case (plan 3 item 2). LOGW added for visibility on failure. |

---

## Scene transition Option A/B/C

Three independently-valuable directions emerged from the 90 ms BEAT
dispatch problem. Recap of what they buy and current status:

### Option A — Action worker task (recommended, **not yet landed**)

> Add a dedicated FreeRTOS task with a queue. Anywhere on the event-bus
> path that wants to run an action, `xQueueSend` an `(action_t,
> trigger_value)` pair instead of calling `action_execute_immediate`
> inline. The worker pulls and runs them at its own priority.

**Status: NOT IMPLEMENTED.** Held in reserve. The user chose Option C first
because it has independent value beyond bus pressure. Option A is still
the cleanest answer for any future heavy handler — should be the first
move if BEAT dispatch grows past ~10 ms again or if rapid scene changes
overflow the bus queue.

When implemented: ~80-line new file + change two call sites
(`action_scheduler.c`, manual button handlers in `midi_scene.c`).
Microsecond return for the dispatcher; action runs ~one task-switch
later.

### Option B — Make `action_execute` itself async

**Status: NOT IMPLEMENTED.** Rejected as too large a blast radius —
touchwheel mappings, pads, on-load actions all become async, some of
which legitimately need synchronous reads. Option A subsumes B's wins
without the blast radius.

### Option C — Speed up `scene_set_current` directly

Four sub-items. Status of each as of last verification:

| # | Sub-item | Status | Notes |
|---|---|---|---|
| C1 | Pre-register ADC channels at boot | **PARTIAL** | Channels stay registered after first use (`adc_manager_register_channel` has an early-return dedup at `adc_manager.c:79-85`), so re-registration is O(1) lookup. But registration still triggers on first scene-enable, not at boot. To complete: call `adc_manager_register_channel` for CV and Expression channels in `main.c` boot path. |
| C2 | Reuse cv/expression tasks across scenes | **NOT DONE** | `cv_disable`/`expression_disable` still cooperative-shutdown + `xTaskDelete`. To complete: add an "active consumer" flag, keep tasks alive across mode changes, gate the ADC reads on the flag. Eliminates the wait-for-task-to-exit step that the cooperative-shutdown comment in `cv.c:82-89` documents. |
| C3 | Batch `tempo_set_*` into `tempo_apply_scene_config()` | **NOT DONE** | `scene.c:1758-1761` and `scene.c:1961-1964` still call 4 separate `tempo_set_*` functions (`bpm`, `source`, `note_divider`, `time_signature`). Each currently re-checks state and may emit an event. A diff-and-apply helper would only touch what actually changed. |
| C4 | UI module switch dedup | **PARTIAL** | Smooth-swap path in `ui.c:194-225` keeps the outgoing screen visible until the new module loads — eliminates the black frame. Same-module reswitch falls back to `legacy_switch_to()` which still rebuilds widgets from scratch (`ui.c:143-158`). For same-module scene-to-scene transitions, could short-circuit by calling only the module's `redraw_func` instead of teardown+rebuild — would require a new optional hook in `ui_draw_module_t`. |

### Phase 1 (graphics suspend) — DONE

`scene_transition_graphics_suspend_6c5df1f5` shipped in full:

- `ui_scene_transition_begin/end/is_transitioning` in `components/ui/ui.h`+`ui.c`
- `scene_set_current` body bracketed in `components/scene/scene.c`
- `handle_touch_event` drops events while transitioning (`components/touch/touch.c`)
- `touch_force_release_all_pads()` on transition end so pads held across
  scene change get clean RELEASE
- `LOGI` on begin/end with elapsed ms

The Phase 1 win was **isolation**: the 90 ms BEAT dispatch no longer
clobbers everything else on the bus during a scene change, even though
the 90 ms itself wasn't reduced. The phase-2/3 work (Option C completion,
Option A) is what would actually shorten the dispatch.

---

## Five BEAT subscribers, now named

The named-handler instrumentation (`event_bus_subscribe_named` +
`handler_max_us` in `components/event_bus/event_bus.c`) was added during
the dispatch-time investigation and remains in place. The five
subscribers as of last verification:

| Slot label | File | What it does | Last known cost |
|---|---|---|---|
| `action_scheduler.beat` | `components/action/action_scheduler.c:460` | Fires scheduled actions; this is the one that can call `scene_set_current` and spike to 90 ms | high during scene transitions, otherwise low |
| `ui.beat_module` | `components/ui/beat.c:691` | Beat-display UI (only subscribed when beat module active) | low |
| `transport.beat_pos` | `components/transport/transport.c:85` | Bar/beat position tracking | low |
| `action_morph.beat_sync` | `components/action/action_morph.c:519` | Morph SYNC completion | low |
| `lfo.beat` | `components/lfo/lfo.c:174` | LFO beat-sync and pending-start handling | low |

The instrumentation persists; the `stats` console command can dump
current `handler_max_us` per subscriber. Use this as the source of truth
when re-evaluating whether Option A is worth doing.

---

## Action consolidation pilot — Tempo family

Status: **code shipped, pending hardware smoke test.**

**What's in:**

- `ACTION_TEMPO` base + `action_variant_t` (`VARIANT_TAP`, `_SET`,
  `_INCREMENT`, `_DECREMENT`, `_HOLD`, `_CYCLE`)
- Dispatch collapse in `action_handlers_scene.c` (one case + inner
  variant switch)
- Variant-aware predicates: `action_supports_timing_for(action)` and
  `action_supports_repeat_for(action)` — TAP/HOLD excluded from timing;
  INC/DEC/CYCLE included in repeat
- `inc_amount` field on tempo params + Amount roller {1,2,3,4,5,10,15,20}
  + handler-side `[20, 300]` clamping
- JSON `{ "type": "tempo", "variant": "...", ... }`; schema updated;
  legacy `tempo_*` names migrated through `components/action_migration/`
- Lazy save-back: scenes that hit migration are marked dirty so the next
  save writes the new format
- Display: compact per-variant labels ("Tempo +1", "Set Tempo", "Tempo
  Hold", "Tempo Cycle") sized for the 12-14 char display
- Schedulers' `Immediate + Repeat` bug fix: `action_scheduler_enqueue`
  gained an `initial_beats_remaining` parameter; `action_execute` queues
  with `interval` after the immediate fire so the repeat actually happens
  (was previously silently broken for **every** action type)

**What's pending:**

| Item | What |
|---|---|
| `device_smoke_test` | Flash + verify: legacy tempo_* scenes migrate cleanly on load, menu shows "Tempo" once with Variant roller, all six variants execute, Amount roller behavior is right, Immediate+Repeat actually repeats |
| `verdict_review` | Decide go/no-go on rolling out the pattern to the remaining 14 families |

**Lessons captured for the rollout (if go):**

- Per-family `*_variant_display()` mini-helpers in `action_strings.c`
  beat a generic `"Type > Variant"` formatter on the 14-char display.
- Legacy aliases go in `action_migration` and never in `scene.c`. New
  families add to `s_legacy_aliases[]` and the singleton `action_migration_fixup_action()` hook.
- Variant-aware `*_for(action)` predicate pattern works well and is the
  template for any future family-level carve-outs.
- The `Immediate + Repeat` scheduler bug was found because we built the
  pilot end-to-end on one family. Doing a wide breadth-first refactor
  across 15 families simultaneously would have missed it.

<a id="consolidation-remaining-families"></a>
### Families remaining (gated on verdict_review)

Roughly 14 families collapse from N action types each to 1 type + variant:

| Family | Current types | Likely variants |
|---|---|---|
| Preset | `PRESET_INC`, `PRESET_DEC`, `PRESET`, `PRESET_HOLD`, `PRESET_CYCLE` | `INCREMENT`, `DECREMENT`, `SET`, `HOLD`, `CYCLE` |
| Scene | `SCENE_INC`, `SCENE_DEC`, `SCENE` | `INCREMENT`, `DECREMENT`, `SET` |
| Touchwheel mode | `TOUCHWHEEL_HOLD`, `TOUCHWHEEL_CYCLE` | `HOLD`, `CYCLE` |
| LFO | `LFO_START`, `LFO_STOP`, `LFO_TOGGLE` | `START`, `STOP`, `TOGGLE` (and `HOLD` for triage item C) |
| LFO Shape | `LFO_SHAPE` (cycle-only today) | `CYCLE`, plus `HOLD` and explicit shape pick for triage item D |
| Clock | `CLOCK_TOGGLE`, `CLOCK_HOLD`, `CLOCK_BURST` | `TOGGLE`, `HOLD`, `BURST` |
| Cut | `CUT_TOGGLE`, `CUT_HOLD` | `TOGGLE`, `HOLD` |
| UI | `SET_UI`, `UI_HOLD`, `UI_CYCLE` | `SET`, `HOLD`, `CYCLE` |
| Param | `PARAM_HOLD`, `PARAM_CYCLE` | `HOLD`, `CYCLE` |
| RTG | `RTG_TOGGLE`, `RTG_HOLD` | `TOGGLE`, `HOLD` |
| S+H | `SAMPLE_HOLD_TOGGLE`, `SAMPLE_HOLD_HOLD` | `TOGGLE`, `HOLD` |
| Control | `CONTROL_CHANGE`, `CONTROL_HOLD`, `CONTROL_CYCLE` | `SET` (current `CONTROL_CHANGE`), `HOLD`, `CYCLE` |
| Transport | `PLAY`, `STOP`, `PAUSE`, `RECORD` | likely stays flat — these are semantically different verbs not variants |
| Note / Randomize / Boomerang / Punch-In / Reset / Flag Ceremony / Sustain / Sostenuto / Step / Confirm Pending | singletons | stay flat |

The Transport family is interesting — could go either way. Default
recommendation is to leave it flat unless we discover a `HOLD` or
`TOGGLE` use case.

---

## Open backlog

Pending items not yet covered above. Maintained here so the chat TODO
list is recoverable.

### Validation gates

- **device_smoke_test** — see [Action consolidation pilot](#action-consolidation-pilot--tempo-family). Blocks everything below.
- **verdict_review** — same.

### Feature requests (deferred behind consolidation rollout)

- **A. Preset Hold "Original" / "Return" release option** — release reverts
  to whatever preset was active at press time, not a fixed value. Lands
  naturally as a variant on the consolidated Preset action.
- **B. Touchwheel Hold "Original" release option** — same semantics for
  touchwheel.
- **C. LFO Hold action** — parallel to Touchwheel Hold / Preset Hold. Lands
  as a variant on the consolidated LFO action.
- **D. LFO Shape Hold / Cycle** — lands as variants on the consolidated
  LFO Shape action.

### UI / quality-of-life

- **inspect_screen_overhaul** — Inspect screen flagged as needing
  significant work. Scope to be defined.
- **initial_scene_animation_delay** — 2.5 s pause between initial scene
  load and start of tempo animation. Deferred as a 20 BPM edge case;
  re-evaluate if it shows up at musical tempos.

### Performance / architecture (not yet on the active list)

- **Option A — Action worker task** (see [Scene transition Option A/B/C](#scene-transition-option-abc)).
  Held in reserve. Trigger: BEAT dispatch grows past ~10 ms again, or
  rapid scene changes overflow the bus queue.
- **Option C completion** — pre-register ADC at boot (C1), reuse
  cv/expression tasks (C2), batch tempo apply (C3), same-module reswitch
  fast path (C4). Independent value, can land piecemeal.
- **Split `action_handlers_scene.c`** — only if the consolidation rollout
  is delayed indefinitely. Otherwise the case count collapses
  organically.
- **`action_summary.c` data-driven rework** — only if the next Inspect
  feature requires it.

---

## Known bugs

Track bugs separately from features so they don't get buried.

| Bug | Severity | Status | Notes |
|---|---|---|---|
| (none currently outstanding from this conversation) | — | — | F and G fixed; deadlock fixed; beat timing fixed; UI desync fixed; Immediate+Repeat fixed. Add new ones here as they appear. |

---

## Decision log

Locked-in choices. Do not re-litigate without good reason.

- **Action consolidation uses one shared `action_variant_t` enum** (not
  per-family). Keeps menu code generic and helpers small. Each family
  uses the subset of variants that makes sense.
- **Migration component lives in its own top-level directory**
  (`components/action_migration/`) so it can be deleted cleanly once
  pre-consolidation scene files are no longer expected in the field.
  Removal procedure is a five-step mechanical deletion documented in the
  consolidation pilot plan.
- **JSON write never emits legacy names.** Only the new
  `{ "type": ..., "variant": ... }` form. Read accepts both.
- **Display names on the device's 12-14 char display use compact
  per-variant strings** ("Tempo +1", "Set Tempo") rather than
  hierarchical `"Type > Variant"` composites. Hierarchical was the
  initial proposal; abandoned after the user pointed out the width
  budget.
- **`scene_transition_graphics_suspend` phase 1 shipped before any
  Option C work** because it was independently valuable (isolates other
  bus subscribers from scene-load cost) and lower risk than Option A.
- **Cooperative shutdown for cv/expression tasks** stays as the safety
  net even after Option C2 lands. The mutex-orphan deadlock that drove
  it is real and the cost (sub-100 ms wait) is acceptable. Documented in
  `cv.c:82-89`.
- **`action_supports_repeat` (by-type) stays conservative**; new
  variant-aware code goes through `action_supports_repeat_for(action)`.
  Existing call sites are safe; nothing forces them all to migrate at
  once.

---

## Maintenance protocol

For the agent and the user, jointly:

1. **When a plan ships:** update the [Plan archive](#plan-archive) row
   with one-line outcome and the section that records what landed where.
2. **When a bug is found:** add to [Known bugs](#known-bugs) with
   severity. When fixed, move to a "Fixed" note in the relevant section
   and remove from the bugs table.
3. **When a feature is requested mid-conversation:** add to [Open
   backlog](#open-backlog) immediately, even if the user is going to
   defer. Better to have a stub entry than to lose it.
4. **When a design decision is locked in:** add to [Decision log](#decision-log).
5. **When the chat TODO list is rebuilt:** use this document as the
   source of truth for pending items. Do not re-derive from chat
   history.
6. **When starting a new chat:** the agent should read this document
   before touching the codebase. Reference relevant sections in early
   replies so the user knows context was preserved.

The agent should never replace the chat TODO list without explicit
permission. If something here changes status, update this file as well
as (or instead of) the chat TODO.

---

*Last updated: 2026-05-25. Update the date when making non-trivial edits.*

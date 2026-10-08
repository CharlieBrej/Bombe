# Bombe UI Audit and Redesign Reference

Status: initial audit, October 2026

This document describes the player-facing functions that the current Bombe UI
provides or implies. It is intended as a reference for a future redesign, not
as a proposal for the final visual style.

## 1. Product model

Bombe is a puzzle workspace with three connected activities:

1. Solve the current grid by revealing information, marking bombs, and using
   logical regions.
2. Build, inspect, organize, and refine reusable rules.
3. Progress through level sets and game modes while tracking stars, completion,
   scores, and server rankings.

The redesign should make these activities feel like one coherent loop:

`choose a challenge → understand the board → solve or experiment → create/refine rules → receive progress → choose the next challenge`

## 2. Required player capabilities

### A. Start, resume, and orient

The player needs to be able to:

- Start a new game and resume the saved game.
- See the current mode, level group, level set, and level number immediately.
- Understand whether the current board is an ordinary level, a server/weekly
  level, a negated level, an if/then level, or a temporary/generated level.
- See loading, saving, synchronizing, and background-processing states.
- Pause and resume active processing without losing the board.
- Recover gracefully when a level, server request, or generated level fails.

### B. Play the grid

The primary board workspace must support:

- Pan, zoom, and fit-to-board navigation for different grid shapes and sizes.
- Selecting cells and regions.
- Marking or clearing a bomb.
- Revealing safe cells where the game rules allow it.
- Seeing revealed state, bomb state, negation state, clue values, region
  boundaries, stale regions, and generated/derived regions distinctly.
- Understanding why a cell or region is considered correct, incorrect, fixed,
  stale, hidden, or unresolved.
- Undoing and redoing meaningful board or rule actions where applicable.
- Clearing mistakes or restarting the current level with confirmation.
- Accessing a hint and understanding its scope, progress, targets, supporting
  regions, and any visibility changes it made.
- Continuing to interact while background robot/reduction work is running,
  with clear indication of what is temporarily unavailable.

### C. Navigate progression

The player needs a level browser that supports:

- Switching between game modes.
- Browsing level groups and level sets.
- Browsing individual levels with previous/next navigation.
- Seeing locked, available, started, solved, starred, and robot-solved states.
- Seeing progress counts and completion summaries at every hierarchy level.
- Seeing why content is locked and what unlocks it.
- Resetting progress at the current level, level set, level group, or mode,
  with explicit scope and confirmation.
- Distinguishing local bundled content from rotating server content.
- Handling weekly-content rollover without losing the historical context of
  completed weekly levels.

### D. Manage rules

Rules are a core player-authored asset and need a first-class workspace. The
player needs to be able to:

- View all active rules for the current mode.
- Select one or multiple rules.
- Inspect a rule in a readable form, including inputs, conditions, actions,
  affected regions, and comments.
- Create a rule by selecting regions and choosing a region type/value.
- Create implication rules with separate “if” and “then” sides.
- Edit or replace an existing rule.
- Delete rules and undo the deletion.
- Duplicate or adapt a rule.
- Reorder or sort rules and understand priority/order effects.
- Search or filter rules by type, comment, affected region, legality, or status.
- See validation feedback for duplicate, illogical, impossible, overly broad,
  or otherwise disallowed rules.
- Understand rule-limit usage and what happens when the limit is reached.
- Export rules to clipboard or another shareable representation.
- Import rules from clipboard or a file with validation and preview.
- See which rules caused or support a derived region.

### E. Inspect explanations and provenance

The current model contains enough causal information that the UI should expose
it deliberately. Players need:

- “Why is this cell known?”
- “Which regions support this deduction?”
- “Which rule created this region?”
- “Why is this region hidden or stale?”
- “What changed since the last action?”
- A way to inspect a rule or deduction without losing the current board view.
- A clear distinction between player choices, rule consequences, hint-owned
  changes, and automatic/robot changes.

### F. Scores and competition

The scores view needs to provide:

- Current mode and level-set context.
- Player score and rank.
- Global leaderboard.
- Friend leaderboard where available.
- Weekly leaderboard and historical weekly context.
- Clear score rules: what counts as completion, when scores update, and why a
  score may be absent or delayed.
- Loading/error/offline states for server data.
- A visible privacy/identity treatment for the player name.

The player should never have to infer whether a score is local, server-backed,
provisional, or synchronized.

### G. Settings and accessibility

The redesign must retain access to:

- Language selection.
- Theme and colour choices.
- Contrast options.
- Fullscreen/window size behavior.
- Sound and music volume.
- Keyboard remapping.
- Rule-limit configuration where it is still an intended player setting.
- Tutorial/help/walkthrough restart.
- Clipboard integration preferences if needed.
- Reduced motion, scalable UI, colour-blind-safe states, and non-colour cues.

### H. Safety and recovery

Destructive or confusing operations need:

- Confirmation for resets, bulk deletes, imports, and replacing rules.
- Undo wherever practical.
- Autosave status and recovery after a crash.
- Explicit handling of invalid imported rules or corrupt saved data.
- A diagnostic view/log export that can be shared without exposing private
  information unnecessarily.

## 3. Current information architecture

The current implementation effectively has these surfaces:

- Main board and side panels.
- Rules panel and rule-construction panel.
- Levels/progression panel.
- Scores panel.
- Main menu/settings overlays.
- Reset confirmation overlays.
- Tooltips and hover explanations.
- Clipboard import/export flows.
- Hint and robot-progress overlays.

The redesign should decide whether these remain panels, become routes/screens,
or become contextual drawers. The important requirement is that the player can
move between them without losing board context.

## 4. Current interaction inventory

The existing code indicates support for:

- Mouse clicks, double clicks, dragging, wheel scrolling, and modifier keys.
- Keyboard shortcuts and remappable key codes.
- Board interaction through `grid_click()`.
- Left and right panel interaction through `left_panel_click()` and
  `right_panel_click()`.
- Keyboard actions through `button_down()` and `events()`.
- Rule undo/redo and rule-region construction.
- Level and score scrolling, sorting, and centering on the current item.
- Modal confirmation for reset operations.
- Tooltips attached to many controls.
- Clipboard-based rule and image export/import.

For a redesign, these should be documented as explicit commands with visible
equivalents. No important function should be keyboard-only or tooltip-only.

## 5. Main usability risks in the current model

### Too many simultaneous concepts

The board, derived regions, rules, levels, scores, robot activity, hints, and
server synchronization compete for attention. A redesign should establish a
clear primary task and move secondary information into inspectable surfaces.

### Hidden state

Important state is represented by colour, visibility, animation, or small panel
controls. Every state should have a text/icon/status equivalent and a legend.

### Weak provenance

The engine tracks causes, but players need a direct explanation path from a
cell to its supporting regions and rules.

### Destructive ambiguity

Resetting levels, deleting rules, replacing rules, and importing content need
scope labels and previews. “Reset” should never be a generic unlabeled action.

### Background work is not a product concept yet

Robots, hints, server requests, and generated levels can all change what is
available. The redesign should use a consistent activity/status system rather
than separate ad-hoc animations.

### Server content is mixed with local content

Weekly levels, scores, generated levels, and local progress need explicit
badges and synchronization states.

## 6. Suggested redesign structure

### Persistent shell

- Current mode and level breadcrumb.
- Board status and save/sync indicator.
- Primary navigation: Play, Rules, Levels, Scores.
- Help/settings access.

### Play workspace

- Large board canvas.
- Compact action bar: undo, redo, hint, reset, pause.
- Contextual inspector for selected cell/region.
- Activity drawer for robot, hint, generation, and synchronization status.

### Rules workspace

- Searchable rule list.
- Rule detail/explanation pane.
- Rule builder with explicit if/then structure.
- Validation and preview before applying.

### Levels workspace

- Mode/group/set hierarchy.
- Progress visualization.
- Filters for local, weekly, negated, if/then, locked, completed, and starred.
- Level preview and clear action to start/resume.

### Scores workspace

- Current score summary.
- Global/friends/weekly tabs.
- Rank and score explanation.
- Server status and last synchronized time.

## 7. Functional requirements for a prototype

The first prototype should prove these flows before visual polish:

1. Resume a level, inspect a cell, and see why it is known.
2. Create a rule from selected regions, validate it, apply it, undo it, and
   inspect its effect.
3. Browse to another level and return without losing context.
4. Reset only the current level with a clear confirmation.
5. Request a hint, observe progress, and undo/clear hint-owned changes.
6. View weekly levels and identify their source/version.
7. View scores with explicit loading, offline, and synchronized states.
8. Change a setting and understand whether it applies immediately or on restart.

## 8. Follow-up audit work

Before implementing the final UI, capture:

- A complete keyboard shortcut map.
- A complete clickable-control inventory with screenshots.
- The current state machine for overlays and panel combinations.
- Every player-visible status and error message.
- Save/load and server synchronization behavior.
- Accessibility gaps, especially colour-only states and minimum text sizes.
- A level/rule terminology glossary.
- A decision on whether generated levels and robot solving remain player-visible
  features or become background infrastructure.

## 9. Source references

The main implementation surfaces are:

- `GameState::render()` and rendering helpers for layout and visual states.
- `grid_click()`, `left_panel_click()`, `right_panel_click()`, `button_down()`,
  and `events()` for interaction.
- Rule construction and inspection around `update_constructed_rule()`,
  `render_rule()`, `rule_gen_undo()`, and `rule_gen_redo()`.
- Level progression around `reset_levels()`, `level_is_accessible()`, and the
  `level_progress` structures.
- Score handling around `fetch_scores()` and `deal_with_scores()`.
- Hint handling around `start_hint()`, `pause_hint()`, and `clear_hint()`.
- Save/load behavior in `GameState` and `main.cpp`.

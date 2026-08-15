# Ashgrove Bug Report - Comprehensive Audit

## Date: 2026-08-16

## Critical Bugs

### 1. Coordinate Go Command Fails from Farmhouse Door ✅ FIXED
**Location:** `src/main.cpp` lines 1048-1070, `src/world.cpp` line 1690 (`bfs_path`)
**Issue:** When player is at farmhouse door (39, 81), `go x,y` commands fail with "No path to those coordinates. Terrain may be blocking the way."
**Root Cause:** The only exit from farmhouse area is south through the farm gate at (38-41, 85), but rocks and trees block the path. The `bfs_path` cannot find a route because the farm fence (y=70, y=85) and vertical fences (x=24, x=43) create a enclosed area with only one gate opening, and that path is blocked by randomly placed rocks/trees.
**Fix:** Added farm gate clearing to `clear_paths()` in `src/world.cpp` (lines 628-635) to keep a 6x4 area around the gate clear of blocking objects.
**Impact:** Players cannot use coordinate movement from the farmhouse starting position.
**Status:** ✅ FIXED - commit 403bf3e

### 2. Farmhouse Interior/Exterior State Mismatch ✅ FIXED
**Location:** `src/main.cpp` lines 1243, 2364-2373, `include/world.hpp` line 761
**Issue:** Player stands in farmhouse area (region "Your Farmhouse", shows "Your farmhouse roof is overhead") but `p.inside` is empty. `exit` command returns "You're outside already."
**Root Cause:** The `in_house()` check (line 761) returns true for farmhouse bounds, triggering farmhouse region/roof messages. But `p.inside` is only set when player uses `enter` command at the door. Walking into the farmhouse area via pathfinding doesn't set `p.inside`.
**Fix:** Modified `exit` command in `src/main.cpp` (lines 2366-2375) to detect when player is in farmhouse exterior (`w.in_house()`) and allow exiting to the door tile.
**Impact:** Confusing UX - player appears inside but can't use interior commands or exit properly.
**Status:** ✅ FIXED - commit 403bf3e

### 3. Movement Message Bug - False "You walk" Message 🔄 NEEDS TESTING
**Location:** `src/main.cpp` lines 1132-1150
**Issue:** `go east` returned "You walk 1 step east" but player position didn't change (remained at 30,74).
**Root Cause:** Possible race condition in async pathfinding movement vs. direct movement commands, or the `walked` counter increments but position update fails in some edge case.
**Impact:** Player thinks they moved but didn't.
**Status:** 🔄 Need to verify if fixed by recent changes

### 4. Object Display Bug - "something" Instead of Proper Name 🔄 IMPROVED
**Location:** `src/main.cpp` line 652 (`obj_name` function)
**Issue:** Object on current cell displays as "something (sturdy, needs 3 more hits)" instead of proper name like "a tall oak tree".
**Root Cause:** The `obj_name` switch statement missing a case for an ObjType value. Likely ObjType::Tree (value 1) not matching due to enum mismatch or object type corruption.
**Fix:** Improved default case to show "(unknown type)" for debugging.
**Impact:** Players can't identify objects they're standing on.
**Status:** 🔄 Partially improved - need to verify no more "something" appears

### 5. Spurious Fence Post Inside Farm Blocking Gate Access ✅ FIXED
**Location:** `src/world.cpp` lines 459-471 (fence generation), `clear_paths` at line 591
**Issue:** Fence post at (38, 84) blocks movement to farm gate at (38-41, 85). Fence should only be at y=70 and y=85.
**Root Cause:** Fence generation at lines 459-465 places horizontal fences at y=70 and y=85. Vertical fences at lines 466-471 place posts at x=24,43 for y=71-84. But something places a fence post at (38, 84) which is not on any fence line.
**Fix:** Added farm gate clearing to `clear_paths()` in `src/world.cpp` (lines 628-635) to keep a 6x4 area around the gate clear of blocking objects including fence posts.
**Impact:** Players cannot exit farm through the gate.
**Status:** ✅ FIXED - commit 403bf3e

### 6. Pickaxe Command Not Recognized ✅ FIXED
**Location:** `src/main.cpp` line 832 (help text), line 1362 (command handler)
**Issue:** `pickaxe` command returns "I don't understand 'pickaxe'. Did you mean: pick?"
**Root Cause:** Help text says "pickaxe" but command handler only checks for "pick" or "mine" (line 1362).
**Fix:** Added "pickaxe" as alias in command handler (line 1362), fuzzy matching list (line 832), and help text (line 882).
**Impact:** Confusing UX - documented command doesn't work.
**Status:** ✅ FIXED - commit 403bf3e

## Medium Bugs

### 7. Axe Command Only Works on Facing Cell, Not Current Cell
**Location:** `src/main.cpp` line 1342-1358
**Issue:** Player standing on a tree ("Also here: something") but `axe` says "Nothing to chop here." because axe acts on facing cell.
**Root Cause:** Tool commands (axe, pickaxe, scythe, hoe, water) all act on `facing_cell(p)` not current position.
**Impact:** Unintuitive - must step back and face the tree to chop it.
**Status:** 🔄 Not yet fixed

### 8. Pathfinding Target Not Walkable
**Location:** `src/main.cpp` lines 1055-1069
**Issue:** `go 30,75` moved player to (30,74) not (30,75). Target coordinate may not be walkable.
**Root Cause:** `bfs_path` requires target to be walkable (line 1691). If target has a rock/fence, pathfinding stops at adjacent tile.
**Impact:** Coordinate movement doesn't reach exact destination.
**Status:** 🔄 Not yet fixed

### 9. Farm Gate Path Blocked by Random Rocks ✅ FIXED (same as Bug 5)
**Location:** `src/world.cpp` lines 448-456 (farm generation), 591-628 (`clear_paths`)
**Issue:** Random rocks placed on farm (6% chance) can block the only gate path.
**Root Cause:** Farm generation places weeds/rocks randomly. `clear_paths` only clears around bridges and building doors, not the farm gate.
**Fix:** Added farm gate clearing to `clear_paths()` - same fix as Bug 5.
**Impact:** Players can be trapped in farm or unable to enter.
**Status:** ✅ FIXED - commit 403bf3e

## Low Priority / Enhancement

### 10. Missing ObjType Value 18 in Enum
**Location:** `include/world.hpp` line 336-344
**Issue:** ObjType enum has gap at value 18 (after Composter=17, before Well=19).
**Impact:** Potential for undefined behavior if value 18 is ever used.
**Status:** 🔄 Not yet fixed

### 11. Client-Side Keyboard Shortcuts Not Implemented
**Location:** PIXI.js client (not in C++ codebase)
**Issue:** No Ctrl+C to clear input, no clear command, no Ctrl+Shift+C/V for copy/paste.
**Impact:** Poor UX for web client.
**Status:** 🔄 Not in C++ scope

---

## Fix Priority Order (Updated)

1. ✅ **Fix 5** - Spurious fence post (blocks progression)
2. ✅ **Fix 1** - Coordinate go from farmhouse (core feature broken)
3. ✅ **Fix 2** - Farmhouse interior state (confusing UX)
4. ✅ **Fix 6** - Pickaxe command alias (easy fix)
5. 🔄 **Fix 4** - Object display name (core feedback broken) - partially improved
6. 🔄 **Fix 3** - Movement message bug (trust issue) - needs verification
7. ✅ **Fix 9** - Farm gate clearing (progression) - same as Fix 5
8. 🔄 **Fix 7** - Axe on current cell (usability)
9. 🔄 **Fix 8** - Pathfinding target handling (edge case)
10. 🔄 **Fix 10** - Enum gap (cleanup)

---

## Test Results Summary

| Feature | Before | After |
|---------|--------|-------|
| `go 35,75` from farmhouse | ❌ No path | ✅ Works |
| `exit` from farmhouse exterior | ❌ "outside already" | ✅ Exits to door |
| Farm gate exit | ❌ Blocked by fence | ✅ Works |
| `pickaxe` command | ❌ Not recognized | ✅ Works |
| `go to farmhouse` | ✅ Works | ✅ Works |
| Coordinate go from farm | ✅ Works | ✅ Works |
| Multi-step walk (`go east 3`) | ✅ Works | ✅ Works |
# AGENTS.md — Grok Exodus

Project instructions for any coding agent working in this repo.

**Engine:** Unreal Engine 5.8  
**Play map:** `/Game/Voxel/Maps/Lvl_VoxelPlanet`  
**Product game mode:** `AGXGameMode` (`/Script/GrokExodus.GXGameMode`). `AVoxelGameMode` is remapped onto the same GX pawn/world so the map override still works.

---

## Mandatory: version bump + auto-commit

These two steps are **not optional**. They apply after every completed change set (feature, fix, or user-visible tweak).

### 1. Bump the on-screen build number

File: `GrokExodus/Plugins/GXCore/Source/GXCore/Public/GXVersion.h`

```cpp
#define GX_VERSION_STRING TEXT("X.Y.Z")
#define GX_VERSION_DATE   TEXT("YYYY-MM-DD")
```

- **Patch** (`0.4.0` → `0.4.1`) for fixes, HUD, perf, small behavior.
- **Minor** (`0.4.1` → `0.5.0`) for a wave/feature (sky, grids, ships).
- **Date** is the calendar day of the bump (use the user_info date).
- The string is drawn **top-left in PIE by a Slate viewport overlay** (`UGXBootOverlaySubsystem`). It does **not** depend on `AHUD::DrawHUD`. If the player does not see the new number, they are on a stale editor binary (close Unreal; Live Coding blocks UBT). Check `GrokExodus/Saved/GX_RUNNING_VERSION.txt` and the log line `overlay attached`.

Do **not** ship a user-facing change without bumping this file in the same commit.

### 2. Auto-commit after each change

When a change is done (compiles, or is docs/process only):

1. `git status` / `git diff` / `git log -5 --oneline`
2. Stage **source and docs only** (never `Binaries/`, `Intermediate/`, `Saved/`, `.pdb`)
3. Commit immediately with a **detailed** message:

```
<title: imperative, what the player/dev will notice>

<body: why, what files/systems, how to verify (GX version, log lines, map).>
```

Include the new `GX_VERSION_STRING` in the title or first body line (`GX 0.4.1: …`).

4. **Do not push** unless the user asks.

Docs-only / AGENTS-only commits may skip the version bump if no binary/HUD change shipped.

---

## Product rules

- **Creativity + hard spaceflight.** No bunkers, no walkers, no new work in `Source/GrokExodus/Voxel/` except crash fixes or routing that file onto GX.
- **Voxels = terrain. Blocks = everything built.**
- **Do not rotate or translate the voxel planet actor.** Dual-layer ephemeris: Kepler in math, body-fixed UE scene.
- **Plugins own simulation:** `GXCore`, `GXVoxel`, `GXCelestial`, `GXConstruct`, `GXPresentation`.
- Hardware ray tracing is **on** (`r.RayTracing=True`, Lumen HW RT). Only **near-field collision** voxel chunks are visible to RT. Do not enable `RayTracingProxies` on every PMC chunk (that was ~2 FPS).
- Close the editor before `Build.bat` if Live Coding is active.
- If the editor says **Plugin 'GXCore' failed to load** (`GetLastError=4551`), the Development DLL is a bad image (UBA cache or a VS rebuild while Live Coding was active). Delete `Plugins/*/Binaries/Win64/UnrealEditor-GX*.dll` and `Binaries/Win64/UnrealEditor-GrokExodus.dll`, then rebuild **Development Editor** with `-NoUBA`. Do not launch until that link finishes. DebugGame DLLs are a different binary and will not fix the default editor.

---

## Where to look

| Need | Place |
|---|---|
| Design / waves | `GrokExodus/Docs/02_Voxel_World_System.md` |
| Session state | `HANDOVER.md` |
| Build stamp | `GXVersion.h` |
| Perf log | `LogGXVoxel` once/sec: `GX-<ver> perf tick=…` in `Saved/Logs/GrokExodus.log` |
| Next feature wave | **C** — GXSky, on-rails vessels, drag/heat, navball |

---

## Play / verify

1. Close Unreal. Reopen. Map `Lvl_VoxelPlanet`.
2. PIE: top-left gold `GX X.Y.Z` from the Slate overlay (not the Canvas HUD).
3. Loading overlay ≥ 2.5 s, then fade. Version stamp stays.
4. Log: `********** GX BUILD X.Y.Z` and `overlay attached`.
5. `GrokExodus/Saved/GX_RUNNING_VERSION.txt` must match `GXVersion.h`.
6. Console: `gx.version` reprints the stamp.

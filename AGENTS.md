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

## Mandatory: close editor + rebuild Development `-NoUBA`

Do **not** tell the user to close Unreal or run Build.bat. After any change that needs a new editor binary (C++, `.Build.cs`, `.Target.cs`, `GXVersion.h`, plugin modules), the agent does this itself.

### 1. Close Unreal if it is running

Live Coding locks `UnrealEditor-GX*.dll` and UBT will fail or write a 4551 image.

```powershell
Get-CimInstance Win32_Process -Filter "Name = 'UnrealEditor.exe'" |
  Where-Object { $_.CommandLine -match 'GrokExodus' } |
  ForEach-Object { Stop-Process -Id $_.ProcessId -Force }
```

If that matches nothing, stop every `UnrealEditor` / `UnrealEditor-Cmd` / `UnrealEditor-Win64-DebugGame` process. Wait until they are gone before compiling. Do not kill Visual Studio.

### 2. Rebuild Development Editor with `-NoUBA`

```
"C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" GrokExodusEditor Win64 Development -Project="E:\Github\grokexodus\GrokExodus\GrokExodus.uproject" -WaitMutex -NoUBA
```

- Always **Development** (the editor loads those DLLs, not DebugGame).
- Always **`-NoUBA`**. UBA has served corrupt `UnrealEditor-GXCore.dll` (`GetLastError=4551`).
- Give the build several minutes. Fix compile errors and rebuild until UBT reports success.
- **Do not** VS-Rebuild Solution first after a 4551 failure.

### 3. 4551 / plugin failed to load

If the last launch said **Plugin 'GXCore' failed to load** or the link looks suspect, delete then rebuild:

```
GrokExodus/Plugins/*/Binaries/Win64/UnrealEditor-GX*.dll
GrokExodus/Binaries/Win64/UnrealEditor-GrokExodus.dll
```

Then run the same `Build.bat … Development … -NoUBA`. Do not launch the editor until that link finishes.

### 4. Relaunch so the user can PIE

After a successful link, start the editor on the play map (do not wait for them to do it):

```
"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "E:\Github\grokexodus\GrokExodus\GrokExodus.uproject" /Game/Voxel/Maps/Lvl_VoxelPlanet -skipcompile
```

Skip this close/rebuild/relaunch loop for docs-only or Python-only changes that do not touch C++.

---

## Product rules

- **Creativity + hard spaceflight.** No bunkers, no walkers, no new work in `Source/GrokExodus/Voxel/` except crash fixes or routing that file onto GX.
- **Voxels = terrain. Blocks = everything built.**
- **Do not rotate or translate the voxel planet actor.** Dual-layer ephemeris: Kepler in math, body-fixed UE scene.
- **Plugins own simulation:** `GXCore`, `GXVoxel`, `GXCelestial`, `GXConstruct`, `GXPresentation`.
- Hardware ray tracing is **on** (`r.RayTracing=True`, Lumen HW RT). Only **near-field collision** voxel chunks are visible to RT. Do not enable `RayTracingProxies` on every PMC chunk (that was ~2 FPS).
- The agent closes the editor and rebuilds Development `-NoUBA` (see above). Do not leave those steps to the user.

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

1. Agent already closed Unreal, rebuilt Development `-NoUBA`, and relaunched `Lvl_VoxelPlanet`.
2. PIE: top-left gold `GX X.Y.Z` from the Slate overlay (not the Canvas HUD).
3. Loading overlay ≥ 2.5 s, then fade. Version stamp stays.
4. Log: `********** GX BUILD X.Y.Z` and `overlay attached`.
5. `GrokExodus/Saved/GX_RUNNING_VERSION.txt` must match `GXVersion.h`.
6. Console: `gx.version` reprints the stamp.

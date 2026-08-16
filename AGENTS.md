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
2. Stage **source, docs, and Unreal content that this change created, edited, or deleted** (see below). Never stage `Binaries/`, `Intermediate/`, `Saved/`, `.pdb`.
3. Commit immediately with a **detailed** message:

```
<title: imperative, what the player/dev will notice>

<body: why, what files/systems, how to verify (GX version, log lines, map).>
```

Include the new `GX_VERSION_STRING` in the title or first body line (`GX 0.4.1: …`).

4. **Do not push** unless the user asks.

Docs-only / AGENTS-only commits may skip the version bump if no binary/HUD change shipped.

### 3. Commit Unreal assets with the change that produced them

If the agent **creates, edits, or removes** editor content, those files **must** be in the same commit as the Python/C++ that caused it. Do not leave cooked-looking graphs only on disk.

Include, when they changed:

- `GrokExodus/Content/**/*.uasset` (materials, textures, maps, meshes, data assets)
- matching `.uexp` / `.ubulk` / `.umap` if present
- `GrokExodus/Content/Python/*.py` that generated them

Typical misses we already paid for: `M_VoxelTerrain_PBR`, `M_VoxelHorizon`, `M_VoxelHorizonFar` updated by `create_voxel_pbr_material.py` but left unstaged because the old rule said “source and docs only.”

After any MCP / editor Python that writes an asset:

1. `git status` under `GrokExodus/Content/`
2. Stage every new/modified/deleted `.uasset` (and siblings) from that run
3. Commit them. A follow-up “commit the materials” commit is required if they were forgotten earlier.

Still never commit `Saved/`, `Binaries/`, `Intermediate/`, `DerivedDataCache/`.

---

## Mandatory: performance traces while features are new

When adding or changing a system (streaming, meshing, clipmap, stamps, atmosphere, foliage), add **`LogGXPerf` / `GX_PERF` traces** so the next pass can see queues, timings, cache hits, and empty work. Console: `gx.perf.trace 0|1|2` (0 off, 1 systems, 2 verbose). Default is **1** until crust LOD is stable.

Each change set, **read** `Saved/Logs/GrokExodus.log` for `LogGXPerf` and `GX-<ver> perf` before calling the work done. Use those lines to find stuck queues, 3 FPS hitches, hollow explosions, and false cache hits. When a subsystem is stable or the trace is noise, set its default to off or drop the verbose sites — do not leave permanent spam.

Do not ask the user to paste logs. The agent opens `GrokExodus/Saved/Logs/GrokExodus.log`.

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

### 4. Relaunch so the user can test

After a successful link, **actually launch** the editor (see **Launch the editor** below) and start PIE. Do not only poll port 12029. Do not end the turn with Unreal closed.

Skip the **close/rebuild** for docs-only or Python-only changes that do not touch C++. Still leave the editor running (launch it if it is not).

---

## Mandatory: leave the editor running so the user can test

Every completed change set ends with **Unreal Editor open** on `Lvl_VoxelPlanet`. The user hits Play (or is already in PIE). Ending a turn with no `UnrealEditor.exe` is a failed handoff — do not say “you can launch the editor.”

- **C++ / plugin / `GXVersion.h`:** close → rebuild `-NoUBA` → launch → wait for `:12029` → start PIE.
- **Python / content:** editor must already be up for MCP; if it is not, launch it first.
- **Docs / AGENTS only:** do not close a live session. If nothing is running, launch so the last binary is one click from Play.

Never wait on `127.0.0.1:12029` unless `UnrealEditor.exe` is already a live process.

### Launch recipe

**Always** run `LaunchEditor.cmd` (repo root). It uses `Win32_Process.Create` so Unreal **breaks away** from the agent Job Object. `Start-Process`, `& UnrealEditor.exe`, and `cmd start` stay in the job and die when the tool command ends — that is why the user has had to start the editor by hand.

```
cmd /c E:\Github\grokexodus\LaunchEditor.cmd
```

Do **not** redirect stdout/stderr. Do **not** pass a bare `/Game/...` through PowerShell (it is a switch). Do **not** chain this after `git commit`.

1. Run that as its **own** command.
2. Sleep 4 s. `Get-Process UnrealEditor` must show a PID **and** a `MainWindowTitle` (or one appears within ~30 s). If there is no PID, run the cmd again.
3. Poll `127.0.0.1:12029` (up to ~90 s). Log line: `TCP server started at 127.0.0.1:12029`.
4. If the port never opens, check `Get-Process UnrealEditor` again. If the process is gone, it **crashed** — do not keep polling. Relaunch once; if it dies again, read `Saved/Logs/GrokExodus.log`.
5. If the process is alive but `:12029` is closed, `UnrealMCPython` did not start. That is a plugin/load bug, not a “user should click the editor” problem.
6. After a rebuild (fresh editor): MCP `unreal-mcpython__util` action `start_pie`. Confirm a window title containing `GrokExodus` before ending the turn. If the editor was already in a session you did not close, leave that session alone.

---

## Mandatory: Unreal MCP + run editor Python yourself

Do **not** tell the user to run `py "…/create_voxel_pbr_material.py"` or any other Output Log command. The agent runs editor Python.

Unity MCP is **disabled** for this repo. Use **unreal-mcpython** (TCP `127.0.0.1:12029` via the `UnrealMCPython` editor plugin). Epic’s HTTP MCP at `:8000/mcp` is optional backup.

### When a script or MCP tool needs the editor

1. Follow **Launch the editor** above (process first, then port).
2. Call `unreal-mcpython__util` `execute_python` (e.g. `runpy.run_path(r"E:/Github/grokexodus/GrokExodus/Content/Python/create_voxel_pbr_material.py", run_name="__main__")`). Close the material editor tab first via MCP if that asset is open.
3. If MCP still says connection refused after the port is open, retry `execute_python` once. Do not fall back to “ask the user to paste py …”.

Python-only material/content work does **not** require a C++ rebuild. C++ changes still follow the close/rebuild/relaunch loop above; after relaunch, wait for a live editor **and** `:12029` before any MCP Python.

---

## Proper solutions, not shortcuts

Do **not** take the easy route. If the right answer is a new system, invent and ship it.

Shortcuts we already paid for — do not repeat:

- Fade landscape to vertex color / unlit / a flat tint to hide tiling
- Two-sided materials to hide winding
- Keep-last-mesh when remesh is empty
- Asking the user to run editor Python or launch the editor
- A gray sphere or default material as a “horizon”

Required bar:

- **Terrain is a landscape.** Near detail plus a larger **macro** scale so color and form remain at distance without wallpaper repeats. Rock / mountain layers tile coarser than grass (scaled-up rock, not more grass repeats).
- **Planets stay body-fixed.** Lighting, materials, and streaming must work at that scale (dual-layer ephemeris, not spinning the actor).
- Prefer a real subsystem (dual-scale triplanar, ephemeris sky, authored horizon, Earth geomorphology, spherical SkyAtmosphere) over a one-line lerp that papers over the bug.
- **Do not replace the voxel planet with a UE Landscape** to get grass. Foliage is HISM scatter from the stamp (`/Game/Foliage/SM_*`). Import Brushify (or any pack) as meshes, not as a landscape actor.

---

## Product rules

- **Creativity + hard spaceflight.** No bunkers, no walkers, no new work in `Source/GrokExodus/Voxel/` except crash fixes or routing that file onto GX.
- **Voxels = terrain. Blocks = everything built.**
- **Do not rotate or translate the voxel planet actor.** Dual-layer ephemeris: Kepler in math, body-fixed UE scene.
- **Plugins own simulation:** `GXCore`, `GXVoxel`, `GXCelestial`, `GXConstruct`, `GXPresentation`.
- Hardware ray tracing is **on** (`r.RayTracing=True`, Lumen HW RT). Only **near-field collision** voxel chunks are visible to RT. Do not enable `RayTracingProxies` on every PMC chunk (that was ~2 FPS).
- The agent closes the editor and rebuilds Development `-NoUBA` (see above). Do not leave those steps to the user.
- The agent launches the editor and starts PIE after every playable change so the user can test. Never ask them to start Unreal.
- The agent runs Unreal Python over MCP. Never ask the user to execute `py` in the Output Log. Unity MCP is off.

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

1. Agent already closed Unreal (if a new binary was needed), rebuilt Development `-NoUBA`, relaunched `Lvl_VoxelPlanet`, and started PIE. The editor is still running.
2. PIE: top-left gold `GX X.Y.Z` from the Slate overlay (not the Canvas HUD).
3. Loading overlay ≥ 2.5 s, then fade. Version stamp stays.
4. Log: `********** GX BUILD X.Y.Z` and `overlay attached`.
5. `GrokExodus/Saved/GX_RUNNING_VERSION.txt` must match `GXVersion.h`.
6. Console: `gx.version` reprints the stamp.

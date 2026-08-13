# HANDOVER — Grok Exodus

Last updated: **2026-08-13** · On-disk build stamp: **GX 0.4.3**  
Branch: `main` (local, several commits ahead of origin; do not push unless asked)

## Current player-facing state

- Play **`/Game/Voxel/Maps/Lvl_VoxelPlanet`**. Do not use `Lvl_FirstPerson`.
- `AVoxelGameMode` (map override) now spawns `AGrokExodusSurvivor` + `AGXVoxelWorld` and destroys `AVoxelPlanetActor`.
- **GX 0.4.3** boot UI is a **Slate viewport overlay**. Hollow near-field chunks count as resolved so the load screen can finish (0.4.2 stuck at 16/64 21% while the world was already meshed).
- Full-screen load overlay + progress + status, ≥2.5 s hold, then fade. Gold `GX 0.4.3` stamp stays top-left.
- `GrokExodus/Saved/GX_RUNNING_VERSION.txt` is written when GXPresentation starts. Console: `gx.version`.
- Terrain: vertex-color debug material (not black). Hardware RT off. Collision only ≤48 m. Hollow chunks not remeshed every frame.
- Live Coding often blocks `Build.bat`. **Quit the editor** before compiling.
- **Plugin GXCore failed to load / GetLastError=4551:** Development `UnrealEditor-GXCore.dll` was an unloadable image (UBA served a bad cached link). DebugGame DLL was fine; the editor loads Development. Fix: delete `Plugins/*/Binaries/Win64/UnrealEditor-GX*.dll` and `Binaries/Win64/UnrealEditor-GrokExodus.dll`, rebuild `GrokExodusEditor Win64 Development -NoUBA`. All six project DLLs now map with `LoadLibraryEx(DONT_RESOLVE)`.

## Next work

**Wave C — sky that tells the truth** (see `GrokExodus/Docs/02_Voxel_World_System.md`):

1. `UGXSkySubsystem` — sun/stars/impostors from `R_inertial_to_body(UT)`; planet actor stays fixed.
2. Vessel INTEGRATED / ON_RAILS.
3. Drag, heating, breakup, parachute hook.
4. Navball + orbit strip + read-only map.
5. Time warp (refuse in atmo / thrusting).

Then Wave D (grids/industry) and Wave E (Earth→Moon).

## Verify after 0.4.3

1. Close Unreal. Rebuild in VS. Reopen.
2. PIE: gold `GX 0.4.3` top-left + dark load screen that **fades** after ≥2.5 s (must not stick at 16/64).
3. Log: `ready=1 status=Ready` and `GX-0.4.3 perf tick=…`.
4. `Saved/GX_RUNNING_VERSION.txt` says `GX 0.4.3`.

If FPS still ~2 after that log exists, use the perf line (`tick` / `stream` / `meshApply` / `chunks`) to see if it is CPU meshing vs remaining Lumen/DF cost.

## Agent process (mandatory)

See `AGENTS.md`:

1. Bump `GXVersion.h` on every user-facing revision.
2. Auto-commit that revision with a detailed message. Do not push unless asked.

## Recent commits

- (this) GX 0.4.3 load screen counts hollow chunks so it can reach Ready
- `e84e128` GX 0.4.2 fix LNK2019: VoxelHUD no longer links unexported GXLoadScreen
- `745b5c4` GX 0.4.1 Slate viewport overlay — load screen + version without HUD
- `3894e37` GX 0.4.0 loading overlay, build stamp, perf traces
- `5bb066a` Vertex-color terrain; stop remeshing empty chunks
- `55cb3dd` Play path / camera / empty crust
- `3e80892` GX plugin foundation

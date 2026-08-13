# HANDOVER — Grok Exodus

Last updated: **2026-08-13** · On-disk build stamp: **GX 0.4.2**  
Branch: `main` (local, several commits ahead of origin; do not push unless asked)

## Current player-facing state

- Play **`/Game/Voxel/Maps/Lvl_VoxelPlanet`**. Do not use `Lvl_FirstPerson`.
- `AVoxelGameMode` (map override) now spawns `AGrokExodusSurvivor` + `AGXVoxelWorld` and destroys `AVoxelPlanetActor`.
- **GX 0.4.2** boot UI is a **Slate viewport overlay** (`UGXBootOverlaySubsystem`), not Canvas `AHUD`. 0.4.1 failed to link: `AVoxelHUD` called unexported `GXLoadScreen` symbols. VoxelHUD is now a no-op.
- Full-screen load overlay + progress + status, ≥2.5 s hold, then fade. Gold `GX 0.4.2` stamp stays top-left.
- `GrokExodus/Saved/GX_RUNNING_VERSION.txt` is written when GXPresentation starts. Console: `gx.version`.
- Terrain: vertex-color debug material (not black). Hardware RT off. Collision only ≤48 m. Hollow chunks not remeshed every frame.
- Live Coding often blocks `Build.bat`. **Quit the editor** before compiling.

## Next work

**Wave C — sky that tells the truth** (see `GrokExodus/Docs/02_Voxel_World_System.md`):

1. `UGXSkySubsystem` — sun/stars/impostors from `R_inertial_to_body(UT)`; planet actor stays fixed.
2. Vessel INTEGRATED / ON_RAILS.
3. Drag, heating, breakup, parachute hook.
4. Navball + orbit strip + read-only map.
5. Time warp (refuse in atmo / thrusting).

Then Wave D (grids/industry) and Wave E (Earth→Moon).

## Verify after 0.4.2

1. Close Unreal. Rebuild in VS. Reopen.
2. PIE: gold `GX 0.4.2` top-left + dark load screen for ≥2.5 s.
3. Log: `overlay attached` and `GX-0.4.2 perf tick=…`.
4. `Saved/GX_RUNNING_VERSION.txt` says `GX 0.4.2`.

If FPS still ~2 after that log exists, use the perf line (`tick` / `stream` / `meshApply` / `chunks`) to see if it is CPU meshing vs remaining Lumen/DF cost.

## Agent process (mandatory)

See `AGENTS.md`:

1. Bump `GXVersion.h` on every user-facing revision.
2. Auto-commit that revision with a detailed message. Do not push unless asked.

## Recent commits

- (this) GX 0.4.2 fix LNK2019: VoxelHUD no longer links unexported GXLoadScreen
- `745b5c4` GX 0.4.1 Slate viewport overlay — load screen + version without HUD
- `3894e37` GX 0.4.0 loading overlay, build stamp, perf traces
- `5bb066a` Vertex-color terrain; stop remeshing empty chunks
- `55cb3dd` Play path / camera / empty crust
- `3e80892` GX plugin foundation

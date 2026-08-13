# HANDOVER — Grok Exodus

Last updated: **2026-08-13** · On-disk build stamp: **GX 0.4.0**  
Branch: `main` (local, several commits ahead of origin; do not push unless asked)

## Current player-facing state

- Play **`/Game/Voxel/Maps/Lvl_VoxelPlanet`**. Do not use `Lvl_FirstPerson`.
- `AVoxelGameMode` (map override) now spawns `AGrokExodusSurvivor` + `AGXVoxelWorld` and destroys `AVoxelPlanetActor`.
- HUD is forced to `AGXHUDLayout` from `AVoxelPlayerController::BeginPlay`.
- **GX 0.4.0** loading overlay (full screen, progress bar, status, ≥2.5 s hold, fade). Version strip top-left.
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

## Known issues to re-check after 0.4.0 is actually running

User rebuilt while Live Coding was active and **did not see 0.4.0**. After a full editor restart, confirm:

- Top-left `GX 0.4.0`
- Log `********** GX BUILD 0.4.0`
- Once/sec `GX-0.4.0 perf tick=… fps~… chunks=… queue=…`

If FPS still ~2 after that log exists, use the perf line (`tick` / `stream` / `meshApply` / `chunks`) to see if it is CPU meshing vs remaining Lumen/DF cost.

## Agent process (mandatory)

See `AGENTS.md`:

1. Bump `GXVersion.h` on every user-facing revision.
2. Auto-commit that revision with a detailed message. Do not push unless asked.

## Recent commits

- `3894e37` GX 0.4.0 loading overlay, build stamp, perf traces
- `5bb066a` Vertex-color terrain; stop remeshing empty chunks
- `55cb3dd` Play path / camera / empty crust
- `3e80892` GX plugin foundation

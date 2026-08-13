# HANDOVER — Grok Exodus

Last updated: **2026-08-13** · On-disk build stamp: **GX 0.5.4**  
Branch: `main` (local, several commits ahead of origin; do not push unless asked)

## Current player-facing state

- Play **`/Game/Voxel/Maps/Lvl_VoxelPlanet`**. Do not use `Lvl_FirstPerson`.
- `AVoxelGameMode` (map override) now spawns `AGrokExodusSurvivor` + `AGXVoxelWorld` and destroys `AVoxelPlanetActor`.
- **GX 0.5.4** Imagine JPGs are imported as real Texture2D assets (`/Game/Voxel/Textures/T_VoxelAlbedoAtlas`, per-biome `T_*_A`). Empty TextureSampleParameter2D nodes were DefaultTextureCube, so the 2D atlas bind was ignored and the crust went black. Re-run the Python script; AlbedoAtlas/RoughAtlas must show the 4×2 atlas, not a cube.
- **GX 0.5.3** `M_VoxelTerrain_PBR` is graph-only. Custom HLSL (`WorldNormal` / `MatId` / `AlbedoAtlas` …) never gets declared in `Material.ush` and compiles to the default gray material. Close the material editor, re-run `create_voxel_pbr_material.py`, look for `[GXPBR] OK graph-only … custom=0`. Mesher remaps mat ids 8–12 into atlas slots 0–7.
- **GX 0.5.2** consistent MC winding (caps had hole walls); brush remesh is LOD0 + face neighbors only (one dig no longer stair-steps the hill); preview hidden off-camera; PBR uses 2D atlases.
- **GX 0.5.1** PBR load no longer OOB-crashes on the 1024 Imagine JPGs.
- **GX 0.5.0** PBR from existing Imagine sets. `M_VoxelTerrain_PBR` via the Python script.
- **GX 0.4.7** no bounce in dug holes. Brush hidden unless the ray hits. Hardware RT on near chunks.
- **GX 0.4.6** brush writes the same voxel corners the mesher samples. Distant sphere hidden. Place works without inventory.
- **GX 0.4.5** crust winding is clockwise (UE/D3D front faces).
- **GX 0.4.4** spawn/stream follow the pawn. 0.4.3 streamed the +X crust while ignoring a pawn inside `0.4*R`.
- Full-screen load overlay + progress + status, ≥2.5 s hold, then fade. Gold stamp stays top-left. Brush sphere only when aiming at terrain.
- `GrokExodus/Saved/GX_RUNNING_VERSION.txt` is written when GXPresentation starts. Console: `gx.version`.
- Terrain: lit vertex-color. Hardware RT on; voxel RT only on near collision chunks. Collision ≤80 m.
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

## Verify after 0.5.4

1. Close the `M_VoxelTerrain_PBR` tab.
2. `py "E:/Github/grokexodus/GrokExodus/Content/Python/create_voxel_pbr_material.py"`
3. Log `[GXPBR] OK graph-only … albedo=/Game/Voxel/Textures/T_VoxelAlbedoAtlas`. Content Browser should list grass/dirt/snow under `/Game/Voxel/Textures`.
4. Re-open the material: AlbedoAtlas / RoughAtlas are 2D atlases (4×2 grass, rock, dirt, sand / snow, mud, volcanic) — **not** DefaultTextureCube.
5. PIE: gold `GX 0.5.4`, tiled biomes, not black.

## Verify after 0.5.3

1. **Close the `M_VoxelTerrain_PBR` editor tab** (a stale Transient preview keeps the old Custom HLSL).
2. Output Log: `py "E:/Github/grokexodus/GrokExodus/Content/Python/create_voxel_pbr_material.py"`
3. Must log `[GXPBR] OK graph-only /Game/Voxel/Materials/M_VoxelTerrain_PBR custom=0`. Re-open the material — **no** `undeclared identifier` errors.
4. PIE: gold `GX 0.5.3`. Grass/dirt/rock/sand/snow tiled, not default gray. Cliffs lean rock.

## Verify after 0.5.0

1. Close Unreal. If `M_VoxelTerrain_PBR` is missing, run `Content/Python/create_voxel_pbr_material.py` in the editor, then rebuild Development Editor `-NoUBA`.
2. Gold `GX 0.5.0`. Grass/dirt/rock/sand/snow should show tiled PBR, not flat vertex color.
3. Cliffs blend toward rock. Dig/place still cuts the textured surface.

## Verify after 0.4.7

1. Close Unreal. Rebuild Development Editor (`-NoUBA` if GXCore 4551 comes back). Reopen.
2. Gold `GX 0.4.7`. Stand in a dug hole — no bouncing out.
3. Look at the sky: brush sphere gone. Aim at dirt: sphere on the surface only.
4. Sun/Lumen should shadow the near crust. If FPS collapses, check `SetVisibleInRayTracing` stayed false on far chunks.

If FPS still ~2 after that log exists, use the perf line (`tick` / `stream` / `meshApply` / `chunks`) to see if it is CPU meshing vs remaining Lumen/DF cost.

## Agent process (mandatory)

See `AGENTS.md`:

1. Bump `GXVersion.h` on every user-facing revision.
2. Auto-commit that revision with a detailed message. Do not push unless asked.

## Recent commits

- (this) GX 0.5.4 import Imagine atlases so PBR is not DefaultTextureCube / black
- `ee565c5` GX 0.5.3 graph-only PBR material (Custom HLSL never compiles)
- `9006942` GX 0.5.2: place walls, hill stair-steps, black flicker, PBR atlases
- `1cff121` Add M_VoxelTerrain_PBR created by the Imagine PBR script
- `42bedda` GX 0.5.1: stop the PBR texture loader from crashing PIE
- `21add16` GX 0.4.7 hole standing, hide miss brush, hardware RT on near chunks
- `037bc13` GX 0.4.6 brush samples match mesher; place without stock; lit shadows
- `fe8d59f` GX 0.4.5 flip crust winding so surface faces are visible
- `976ab84` GX 0.4.4 stand on the crust, cook collision underfoot, show brush sphere
- `f01fa82` GX 0.4.3 load screen counts hollow chunks so it can reach Ready
- `e84e128` GX 0.4.2 fix LNK2019: VoxelHUD no longer links unexported GXLoadScreen
- `745b5c4` GX 0.4.1 Slate viewport overlay — load screen + version without HUD
- `3894e37` GX 0.4.0 loading overlay, build stamp, perf traces
- `5bb066a` Vertex-color terrain; stop remeshing empty chunks
- `55cb3dd` Play path / camera / empty crust
- `3e80892` GX plugin foundation

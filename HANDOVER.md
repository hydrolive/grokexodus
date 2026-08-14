# HANDOVER — Grok Exodus

Last updated: **2026-08-14** · On-disk build stamp: **GX 0.7.10**  
Branch: `main` (local, several commits ahead of origin; do not push unless asked)

## Current player-facing state

- Play **`/Game/Voxel/Maps/Lvl_VoxelPlanet`**. Do not use `Lvl_FirstPerson`.
- `AVoxelGameMode` (map override) now spawns `AGrokExodusSurvivor` + `AGXVoxelWorld` and destroys `AVoxelPlanetActor`.
- **GX 0.7.10** 0.7.9 shots: grass is back (black pit gone). The “mountains” were a 2 km-tall ring at 2 km — a wall in the sky. Ranges now start at 5 km with foothills in front so 2 km peaks sit on the 8 km horizon.
- **GX 0.7.9** 0.7.8 shots: Ready, **black around the pawn still**, a real ridge on the limb. Near crust was single-sided PBR with **inward** SDF normals (0.7.7). Normals now face outward; old `.gxm` cache is invalidated.
- **GX 0.7.8** 0.7.7 shot: Ready, **black around the pawn**, felt glued, hills not mountains. Clipmap hole is 48 m (was 317 m) and sits 1.5 m under the stamp. Collision cooks sync out to 160 m; airborne snap waits 4 s and does not zero walk velocity (that snap-loop was “I cannot move”). Ranges start at 2 km with ~2 km peaks. Debug overlay spam removed.
- **GX 0.7.7** Screenshots: plains OK, **no mountains**, **missing chunks** (pit to the horizon), **grass tiles + hard edges**. Spawn basin is ~2.2 km; a range ring at 3–16 km puts ~1.2 km peaks on the 8 km clipmap. Empty meshes settle only at LOD0 (LOD>0 retries). Clipmap hole overlaps the voxel shell (0.88× stream). SDF-gradient normals (chunk-grid lighting seams). Grass tiles ~11 m / macro ~400 m. New crust fingerprint — first PIE rebakes the atlas.
- **GX 0.7.6** Overlay stuck at **32/164 28%**: 32 real crust meshes, 132 band-air chunks remeshed forever (`hollow=0`, `queue=126+2170`, `inflight=6`). Empty remesh / empty `.gxm` now session-settle; HUD counts settled as done; stream uses 8-corner overlap (not a 76 m band); far jobs wait until near is quiet; inflight chunks are not re-queued every 200 ms. `LogGXPerf` stream is on-change / 2 s; 1 Hz line has cache/inflight/mailbox; `GX-mesh STALL` after 3 s. Play 0.7.6 — overlay should hit Ready without walking.
- **GX 0.7.5** Near-field HUD was 14/171 because Desired counted the whole 110 m ball (air+interior). Stream only enqueues crust-intersecting chunks; empty cache files are rejected. Spawn plains are ~6 km so mountains appear on the 8 km clipmap. `gx.perf.trace` + `LogGXPerf` — check each pass; set 0 when stable.
- **GX 0.7.4** Wider plains (spawn is pushed onto a lake-flat). Soft FBm mountains (no ridged cliffs). Clipmap builds a complete disk (no dropped quads). Near-surface empty meshes are not stored as hollow. Atlas inset 9% + larger grass tiles. Clipmap rebuilds every 400 m (was 90 m — that 3 FPS hitch was the “crash”).
- **GX 0.7.3** Plains (lake-flat valley floors) + foothills only at the mountain skirt. Clipmap starts at 0.9× stream so it no longer z-fights voxels (that was the grid). Far mesh uses `M_VoxelHorizonFar` (lit vertex color).
- **GX 0.7.2** Sparse ~10 km range/valley domains with talus fill. Clipmap rings share a 12/36/72 m grid and overlap; hole sits under the voxel stream. Far rings use vertex-color slope, not the 2 m atlas. **Voxel HLOD is 0.8; cluster “Nanite analog” is 0.9.**
- **GX 0.7.0** Far crust is a 3-ring height clipmap (to 8 km), not a mean-radius sphere. Voxel LOD is screenspace (`v/d`). Stamp has ~4 km ranges plus walkable 200 m undulation. PBR: dominant-axis UVs + grass→dirt→rock by slope. Review shots: `Saved/Review/` (gitignored).
- **GX 0.6.3** Mesa/block terrain was Worley plates hashed to a height plus a saturating massif (flat top, vertical suture walls). Height is continuous FBm + soft ridges now; coasts ramp through a shelf. New crust fingerprint.
- **GX 0.6.2** Wider Earth landforms: mountain wavelength ~90 km (was ~30 km spikes), plains/plateaus are the default, local ridge/gully is gentle fBm not 200 m needles. Stream starts at 140 m so more than a handful of chunks appear. Stamp fingerprint changed — old `crust_*` cache is ignored.
- **GX 0.6.1** First PIE no longer freezes the editor. BeginPlay does not mesh. Surface query is one stamp sample (not a 6 km ray). Height atlas is baked on a worker (or loaded from `Saved/VoxelWorld/crust_<fingerprint>/`). Chunks mesh async with a 6 ms tick budget and a 12-job cap; workers never capture the world actor. Subsequent PIE loads the atlas + `.gxm` meshes and only regenerates if the stamp / radius / relief / seed changes.
- **GX 0.6.0** Earth-scale planet: **60 km** radius, **2.4 km** relief. Stamp is a real geomorphology stack (plates, ranges, rivers, canyons, coasts, volcanoes, glaciers, trenches) plus walkable local ridges. GameMode no longer clamps relief to 220 m. SkyAtmosphere is **kept** and re-centered on the planet (`PlanetCenterAtComponentTransform`); Z-up ExponentialHeightFog is disabled. Haze is Mie + aerial perspective and thins with altitude. Foliage: import meshes to `/Game/Foliage/SM_{Grass,Bush,Tree}` (Brushify is fine as meshes; do **not** convert to Landscape).
- **GX 0.5.9+** Landscape dual-scale triplanar (not fade-to-vertex-color). Near grass ~2 m, macro ~20 m; rock uses a larger tile so mountains keep color without repeating. Horizon sphere still fills the limb.
- **GX 0.5.9** Distant voxel wallpaper fades to vertex color by 150 m. A mean-radius `M_VoxelHorizon` sphere fills the limb past the stream; near pixels are masked so holes are not a second grass layer. Stream ~180–200 m.
- **GX 0.5.8** Dig ignored early “infected” pages (Density=0, no flags) while the mesh still showed stamp grass. Sample/raycast/load now treat only Deformed/PlayerPlaced cells as real edits. Place already overwrote them; dig now carves that grass.
- **GX 0.5.7** PBR UVs are **planar World YZ** (spawn is +X). Planet-tangent frames warped every MC triangle. TileScale 0.0045 on centimetres again. MCP re-ran the material script.
- **GX 0.5.6** Crust winding flipped to I0,I1,I2 (PBR is single-sided; I0,I2,I1 showed only the underside). Space jumps along planet-up; airborne snap ignores the next 2.5 s so the jump is not yanked back.
- **GX 0.5.5** Lit PBR was black because spawn is +X and the sun was aimed at the opposite hemisphere (old unlit vertex-color hid that). Sun now lights +X; SkyLight captures from the crust, not the core. Runtime no longer wraps the authored material in a MID that can stomp the atlas.
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
- Live Coding often blocks `Build.bat`. The agent **closes Unreal and rebuilds Development Editor `-NoUBA`** (see `AGENTS.md`). Do not ask the user to do that.
- **Unreal MCP:** `UnrealMCPython` plugin listens on `127.0.0.1:12029`. Agent **must Start-Process the editor** and confirm a PID before polling the port. Never wait on 12029 with no UnrealEditor process (the user had to launch it by hand). Run Python via `unreal-mcpython__util execute_python`. Unity MCP is disabled.
- **Plugin GXCore failed to load / GetLastError=4551:** Development `UnrealEditor-GXCore.dll` was an unloadable image (UBA served a bad cached link). DebugGame DLL was fine; the editor loads Development. Fix: delete `Plugins/*/Binaries/Win64/UnrealEditor-GX*.dll` and `Binaries/Win64/UnrealEditor-GrokExodus.dll`, rebuild `GrokExodusEditor Win64 Development -NoUBA`. All six project DLLs now map with `LoadLibraryEx(DONT_RESOLVE)`.

## Verify after 0.7.2

1. Gold `GX 0.7.2`. Peak, then a **wide flat**, then the next peak. Far range is gray/dirt, not a red tiled sheet.
2. No sky holes between near ground and far mountains.

## Verify after 0.6.3

1. Gold `GX 0.6.3`. No city-block mesas. Slopes should be diagonal, coasts should beach, plate interiors should be rolling not table-tops.
2. New `crust_<hex>/` (fingerprint changed).

## Verify after 0.6.2

1. Gold `GX 0.6.2`. More than a handful of chunks. Ground should hold a slope or a plain for tens of meters, not peak–valley–peak every few steps.
2. Log a new `crust_<hex>/` folder (fingerprint changed). Old 0.6.1 cache is not reused.

## Verify after 0.6.1

1. PIE: overlay appears immediately (`Preparing planet` / `Baking crust height field`). Editor stays responsive.
2. First boot may take a short while on the worker; log `GXCrustAtlas built …` then `crust atlas ready`. Walkable before the whole 280 m stream is done.
3. Stop PIE, play again: log `GXCrustAtlas loaded` / `crust atlas ready disk=1`. Near field should pop from cache, no 40 s freeze.
4. Change radius/relief/seed: new `crust_<hex>/` folder, old cache ignored.
5. Gold `GX 0.6.1`.

## Verify after 0.6.0

1. Gold `GX 0.6.0`. Spawn is on a **60 km** crust (`PlacePawnOnSurface r≈60000`). Log: `GXPlanetAtmosphere: spherical R=60.00km` and `mode=PlanetCenter`.
2. Look at the horizon: sky/limb should match the local ground plane, not sit sideways. Surface haze that thins if you climb.
3. Terrain should show ridges, gullies, and real elevation — not the old 180 m rolling blob. Distant sphere still fills the limb.
4. No grass/trees until `/Game/Foliage/SM_*` meshes exist. Log will say so.

## Next work

**Wave C — sky that tells the truth** (see `GrokExodus/Docs/02_Voxel_World_System.md`):

1. `UGXSkySubsystem` — sun/stars/impostors from `R_inertial_to_body(UT)`; planet actor stays fixed.
2. Vessel INTEGRATED / ON_RAILS.
3. Drag, heating, breakup, parachute hook.
4. Navball + orbit strip + read-only map.
5. Time warp (refuse in atmo / thrusting).

Then Wave D (grids/industry) and Wave E (Earth→Moon).

## Verify after 0.5.7

1. Gold `GX 0.5.7`. Dig a hole on a hill — the surrounding slope stays smooth, only the crater is voxel-cut.
2. Add/remove should not hitch the frame (maybe a brief async pop).
3. Re-run `create_voxel_pbr_material.py` if flats still look like stretched XY stripes.

## Verify after 0.5.6

1. Gold `GX 0.5.6`. Surface grass visible from above; you should not see the underside unless you clip under the crust.
2. Space jumps along planet-up and lands. No snap-back mid-jump.

## Verify after 0.5.5

1. Close Unreal. Rebuild Development Editor `-NoUBA`. Reopen. PIE.
2. Gold `GX 0.5.5`. Ground on the +X spawn is **lit** grass/dirt, not black. Sun should be in the sky, not behind the planet.
3. Log: `VoxelSunSetup: … +X NdotL=0.78` and `GXTerrainPBR: using authored parent M_VoxelTerrain_PBR`.

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

- (this) GX 0.5.7 smooth hills after dig, cheaper brush, planet-tangent UVs
- `c004db9` GX 0.5.6 outward crust winding + Space jump
- `b884a1a` Fix 0.5.5 compile: UE 5.8 removed MATUSAGE_ProceduralMesh
- `55c6c74` GX 0.5.5 light the +X spawn so lit PBR is not a black night side
- `938ce0d` Commit imported voxel PBR textures and graph-only M_VoxelTerrain_PBR
- `94b0b41` GX 0.5.4 import Imagine atlases so PBR is not DefaultTextureCube / black
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

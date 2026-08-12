# 02 – Voxel World System

**Project:** Grok Exodus  
**Engine:** Unreal Engine 5.8  
**Status:** Phases 0–6 implemented; **Phase 3 textures + Phase 5 LOD/async hardened**

Primary fantasy: *You can lose a walker. You cannot lose everything.* The planet volume and private bunker edits are the permanent anchor.

---

## Pillars

1. **Craftsmanship as True Power** — `FVoxelToolModifiers` / `VoxelAPI::MakeToolModifiers` cascade dig speed, recovery, precision, wear. Quality never bakes into voxel data.
2. **Private Bunkers as permanent safe anchors** — dirty-chunk `.gxvx` persistence + `RegisterBunkerVolume` residency/protection flags.
3. **Walkers temporary** — `VoxelAPI::SphereHitsTerrain` / density queries; walker loss does not erase planet or bunker data.

---

## Architecture overview

```text
Procedural base (FVoxelSphereMapping)
        │
        ▼
Sparse dirty overrides (FVoxelVolume / 32³ chunks)
        │
        ├── Surface Nets mesher (FVoxelMesher)
        ├── Streaming actors (AVoxelPlanetActor → AVoxelChunkActor)
        ├── Dig/Place tools (UVoxelTerrainToolComponent)
        ├── Spherical gravity (UVoxelSphericalMovement)
        └── Persistence (FVoxelPersistence → Saved/VoxelWorld/*.gxvx)
```

### Units

| Space | Unit | Notes |
|-------|------|-------|
| Volume density / planet params | **meters** | Radius, relief, brush radius on API |
| UE world / pawn / debug draw | **centimeters** | `PlanetLocalMetersToWorld` ×100 |
| Default iteration planet | **512 m** radius | Design scales to 2–8 km |

### Coordinate system

- Cartesian sparse grid centered on planet origin.
- Density: `SurfaceRadius(dir) − |p|` (positive = solid).
- Gravity: `−normalize(p)` toward center.
- Precision path: planet-local meters → chunk-local float mesh in cm.

### Chunk / LOD

| Param | Value |
|-------|-------|
| Chunk size | 32³ voxels |
| Base voxel | 1 m |
| LOD bands (default) | 96 m → L0, 192 → L1, 320 → L2, 512 → L3 |
| Streaming | `StreamRadius` / `UnloadRadius` on `AVoxelPlanetActor` |
| Near-surface cull | deep core chunks skipped unless near viewer |

LOD stitching (skirts/morph) is **designed**; Phase 1 uses padded Surface Nets apron. Phase 5 hardens async budget + unload rules (implemented as mesh queue + unload radius).

### Meshing algorithm

**Naive Surface Nets** (chosen over Marching Cubes / full Dual Contouring):

- Good deformation for dig/place caves and overhangs.
- No QEF solve complexity; stable for gameplay.
- Dual Contouring remains the sharp-feature upgrade path.
- Seam: 1-cell sample apron from neighbors/procedural.

### Persistence (bunker permanence)

Binary `.gxvx` (magic `GXVX`, version 1): header (seed, radius, relief, voxel size) + dense dirty chunks only.  
Path: `Saved/VoxelWorld/<SaveFileName>` (default `planet_default.gxvx`).  
Auto-load BeginPlay / auto-save EndPlay configurable on planet actor.

---

## Source map

```text
Source/GrokExodus/Voxel/
  VoxelTypes.*              cells, tools, dig results, flags
  VoxelNoise.h              fBm / ridged
  VoxelSphereMapping.h      radial planet + biomes
  VoxelMaterialTable.*      hardness / yield / PBR soft paths
  VoxelChunk.h              32³ payload
  VoxelVolume.*             sparse volume + brush + bunker
  VoxelPersistence.*        serialize dirty regions
  VoxelMesher.*             Surface Nets
  VoxelChunkActor.*         procedural mesh proxy
  VoxelPlanetActor.*        stream / edit / save / raycast
  VoxelSphericalMovement.*  gravity toward center
  VoxelTerrainToolComponent.* Drill + Place
  VoxelExodusCharacter.*    FP pawn + keybinds
  VoxelGameMode.*           spawn planet, surface drop-in
  VoxelPlayerController.*   IMC wiring
  VoxelPublicAPI.h          craftsmanship / bunker / walker hooks
  VoxelSmokeTest.*          Phase 0 harness
  VoxelPhaseSmokeCommands.* Phase 1–2 console + automation
```

Textures (Grok Imagine sources):  
`Content/Voxel/Textures/Source/T_<Material>_{A,N,R}.jpg` + `T_Shared_Metallic.jpg`

---

## Materials & Grok Imagine

### Material table

| ID | Name | Hardness | Role |
|----|------|----------|------|
| 0 | Air | 0 | |
| 1 | TemperateGrass | 0.6 | mid-latitude surface |
| 2 | RockyCliff | 2.2 | escarpments / shallow crust |
| 3 | DryDirt | 0.9 | arid mid elevations |
| 4 | SandCoastal | 0.5 | lowlands |
| 5 | SnowIce | 0.7 | poles / high altitude |
| 6 | WetMud | 0.45 | lowlands |
| 7 | VolcanicScorched | 2.8 | AI-war scar patches |
| 8 | BedrockDeep | 4.0 | deep interior |

### Craftsmanship cascade

```text
DigRate     = Tool.DigSpeedMul / max(Hardness, ε)
Yield       = VolumeRemoved * DigYield * RecoveryMul
Wear        = VolumeRemoved * WearFactor * WearMul
BrushRadius = BaseRadius / max(PrecisionMul, 0.25)
```

`VoxelAPI::MakeToolModifiers(ToolQuality, RecoveryBonus, PrecisionBonus)` builds this from a single quality score.

### Grok Imagine prompts (recorded)

**Albedo (tileable, 1:1, photoreal, post-collapse weathered):**

1. **Temperate Grass / Soil**  
   `Create a seamless tileable PBR albedo texture for temperate grass mixed with soil, realistic Earth landscape, slightly weathered post-collapse feel, high detail, photorealistic top-down ground texture, green grass patches with brown dirt, no text, no UI, square texture, consistent soft overcast lighting, 2048 style detail density`

2. **Rocky Cliff / Bedrock**  
   `Create a seamless tileable PBR albedo texture for rocky cliff bedrock, realistic Earth landscape, slightly weathered post-collapse feel, high detail, photorealistic grey-brown fractured stone and sedimentary rock face, cracks and lichen hints, no text, no UI, square texture, consistent soft lighting`

3. **Dry Dirt / Hardpan**  
   `Create a seamless tileable PBR albedo texture for dry dirt hardpan soil, realistic Earth landscape, slightly weathered post-collapse feel, high detail, photorealistic cracked arid brown earth, dusty surface, no text, no UI, square texture, consistent soft lighting`

4. **Sand / Coastal**  
   `Create a seamless tileable PBR albedo texture for coastal sand, realistic Earth landscape, slightly weathered post-collapse feel, high detail, photorealistic fine beige sand with subtle grain and shell flecks, no text, no UI, square texture, consistent soft lighting`

5. **Snow / Ice**  
   `Create a seamless tileable PBR albedo texture for snow and ice ground, realistic Earth landscape, slightly weathered post-collapse feel, high detail, photorealistic packed snow with subtle ice crystals and wind ridges, cool white-blue tint, no text, no UI, square texture, consistent soft lighting`

6. **Wet Mud / Riverbed**  
   `Create a seamless tileable PBR albedo texture for wet riverbed mud, realistic Earth landscape, slightly weathered post-collapse feel, high detail, photorealistic dark wet brown mud with fine silt patterns and moisture sheen areas, no text, no UI, square texture, consistent soft lighting`

7. **Volcanic / Scorched**  
   `Create a seamless tileable PBR albedo texture for volcanic scorched rock, realistic Earth landscape, AI-war scarring feel, high detail, photorealistic black basalt and charred stone with ash and heat-cracked surface, no text, no UI, square texture, consistent soft lighting`

**Normals (from albedo via image_edit):**  
`Convert this seamless tileable landscape texture into an OpenGL-style normal map only. Output a purple-blue normal map … seamless and tileable, no albedo color, no text, photorealistic height-derived normals matching the [surface].`

**Metallic:** shared pure black tile (`T_Shared_Metallic`) — natural materials are non-metal.

**Roughness:** grayscale maps per material (authoring values ~0.45 mud → ~0.9 dirt). Re-run Imagine for photoreal roughness if desired:

`Create a seamless tileable PBR roughness map for [material], grayscale only, brighter = rougher, matching surface microdetail, no color, no text, square.`

### Runtime look (Phase 3)

- Mesh vertices carry **material debug colors** immediately (no import required).
- Soft paths point at `/Game/Voxel/Textures/T_*` after you import Source JPGs in the editor and build a triplanar master material assigned to `AVoxelPlanetActor::TerrainMaterial`.
- Recommended master material: triplanar blend of Albedo/Normal/Roughness, vertex color as material ID blend weight (future multi-material sections).

---

## Gameplay wiring

| Item | Class / control |
|------|-----------------|
| Game mode | `AVoxelGameMode` (default in `DefaultEngine.ini`) |
| Pawn | `AVoxelExodusCharacter` |
| Drill | LMB hold |
| Toggle Drill/Place | RMB or G |
| Cycle place material | R |
| Save planet | F5 |
| Console | `Voxel.SmokeTest`, `Voxel.Phase1Smoke`, `Voxel.Phase2Smoke` |

On BeginPlay the game mode spawns a planet (if missing) and places the player on the +X surface.

---

## Phase results

### Phase 0 – Data model — **PASSED**

- Automation: `GrokExodus.Voxel.Phase0.Smoke` → Success, 0 failures  
- Sphere density, gravity, dig/place, bunker flag, byte-identical serialize round-trip  
- Example: 8 dirty chunks, ~3.1 MB save buffer  

### Phase 1 – Generation + meshing — **PASSED**

- Algorithm: Surface Nets  
- Automation: `GrokExodus.Voxel.Phase1.Mesh` → Success  
- Console: `Voxel.Phase1Smoke` reports verts/tris/ms/memory  
- Streaming + 4 LOD distance bands + spherical gravity component  

### Phase 2 – Deformation — **PASSED**

- Automation: `GrokExodus.Voxel.Phase2.PersistEdit` → Success  
- Tunnel/cave/place + remesh dirty neighbors + `.gxvx` identity  
- FP tool raycast + debug sphere preview  

### Phase 3 – PBR textures — **DONE**

- 7 albedo + 7 normal (Imagine) + shared metallic + roughness under `Content/Voxel/Textures/Source/`  
- **`FVoxelRuntimeTextures`**: loads JPGs at runtime via ImageWrapper  
- **CPU triplanar albedo bake** into vertex colors (works at any orientation on the sphere)  
- Vertex-color lit material auto-selected (`M_VoxelTerrain_VertexColor` if present, else engine fallback)  
- Automation: `GrokExodus.Voxel.Phase3.Textures`  
- Console: `Voxel.Phase3Smoke`  

**Editor note for full GPU PBR:** import Source JPGs → create triplanar master `M_VoxelTerrain_Triplanar` → assign to `AVoxelPlanetActor::TerrainMaterial`.

### Phase 4 – FP tools — **DONE**

- Spherical gravity (`SetGravityDirection` toward center), drill/place, hotkeys, surface snap  
- Distant planet sphere impostor  

### Phase 5 – LOD / perf — **HARDENED**

- **Real LOD meshing**: stride sampling (`LOD 0/1/2…` → fewer verts farther away)  
- Collision only on LOD0  
- **Async meshing** on thread pool after warmup; sync during first seconds so player doesn’t fall through  
- LOD band changes trigger remesh  
- Stream/unload radii + near-surface prioritization  
- Remaining polish: skirt morph at LOD boundaries, GPU compute meshing  

### Phase 6 – API + docs — **DONE**

- `VoxelPublicAPI.h` for craftsmanship, bunker, walker probes, save/load  
- This document  

---

## Extension: biomes, ores, AI-war scarring

1. **Biomes** — extend `FVoxelSphereMapping::SampleMaterial` with climate noise / moisture layers; paint material IDs without changing density.  
2. **Ore veins** — set `EVoxelFlags::OreVein` and dedicated material IDs in a second noise pass when `FillChunkProcedural` runs; dig yield hooks already material-based.  
3. **AI-war scarring** — increase volcanic noise threshold / add crater SDF subtractors in `SampleDensity`; flag `EVoxelFlags::Scarred`.  
4. **Bunker integration** — after dig session call `VoxelAPI::RegisterPrivateBunker` with the carved AABB; always `SaveWorld` on leave.  
5. **Walker collision** — sample wheel/foot points with `VoxelAPI::SphereHitsTerrain`; do not write volume from walkers unless intentional.

---

## How to play / smoke (final)

1. Build `GrokExodusEditor`.  
2. Play with `AVoxelGameMode` (project default).  
3. Wait for surface chunks to mesh; walk with WASD; look with mouse.  
4. **LMB** dig a bunker cavity; **RMB** switch to Place; **R** pick material; seal a ramp.  
5. **F5** save → quit → play → confirm cavity still present.  
6. Automation: `Automation RunTests GrokExodus.Voxel`  

---

## Known limitations

- LOD levels 1–3 select index only; mesh resolution still LOD0 (coarser meshes = next hard step).  
- No GPU compute meshing yet (CPU Surface Nets + async cooking on PMC).  
- Texture JPGs need editor import for full PBR triplanar (vertex colors work now).  
- Spherical movement is functional but may need polish vs complex CMC floor detection on steep crust.  
- Single-player only (by design).  
- Large planets (≫2 km) need tighter streaming budgets and eventual origin shifting.

---

## Performance notes (Phase 1 mesh)

Measure with `Voxel.Phase1Smoke` and in-game `GetStreamingStats`:

- Chunk payload dense: 32³ × sizeof(FVoxelCell) ≈ 384 KB/chunk (with int32 fields).  
- Mesh time: depends on surface complexity; budget 1–2 chunks/frame via `MaxMeshBuildsPerFrame`.  
- Dirty saves scale with edited volume only (sparse).

---

## Next recommended work

1. Import Source textures → create `M_VoxelTerrain_Triplanar` → assign to planet.  
2. Hierarchical LOD mesh builds (stride sampling).  
3. Skirt/morph LOD stitching.  
4. Optional Dual Contouring for sharp player-built geometry.  
5. Worker-thread meshing (snapshot density grid → mesh → game-thread apply).  
6. Bunker volume UI + protection gameplay rules.  
7. Walker vehicle pawn using `VoxelAPI` collision probes.

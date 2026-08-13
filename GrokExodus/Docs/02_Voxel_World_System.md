# 02 – Voxel World & Space Foundation

**Project:** Grok Exodus  
**Engine:** Unreal Engine 5.8  
**Status:** Waves **A** (plugins + math) and **B** (walkable planet) implemented.  
**Default play:** `AGXGameMode` (`/Script/GrokExodus.GXGameMode`)

Primary fantasy: **player creativity + hard space exploration.** Mine, build anything, fly it. Gravity, orbits, and re-entry are physically honest enough to punish a sloppy ship.

Not this game: bunkers, walkers, “lose the vehicle / keep the vault.” Those live only in the frozen legacy folder `Source/GrokExodus/Voxel/` and must not grow.

---

## Pillars

1. **Voxels are dirt. Blocks are everything built.** Terrain is a signed density field. Ships and factories are grids (Wave D).
2. **Dual-layer ephemeris.** Earth orbits Sol and rotates in math. The Unreal planet actor is nailed to the origin. The sky and inertial vessels move.
3. **Inverse-square gravity, one μ.** Surface g is a consequence of radius: `μ = g R²`. Earth default **60 km** → LEO ≈ **767 m/s**.
4. **Craftsmanship stays on the tool.** Quality never bakes into voxel data.
5. **Edits persist because they are sparse pages**, not because a volume is pinned in RAM.

---

## Plugin graph

```text
GrokExodus.uproject
├── Plugins/
│   ├── GXCore/            frames, jobs, snapshots, save header, interfaces
│   ├── GXVoxel/           volume, stamps, mesher, world, tools
│   ├── GXCelestial/       Kepler, gravity, atmo, ECI↔body, body movement
│   ├── GXConstruct/       block / item / recipe / grid types (data only)
│   └── GXPresentation/    HUD shell
└── Source/GrokExodus/     AGXGameMode, AGrokExodusSurvivor
```

| Plugin | Depends on | Must not depend on |
|---|---|---|
| GXCore | Engine | everything else |
| GXVoxel | GXCore, GXCelestial | game module |
| GXCelestial | GXCore | GXVoxel (uses `IGXVoxelQuery` / `IGXGravityField`) |
| GXConstruct | GXCore, GXCelestial | game |
| GXPresentation | all of the above | — |

`Source/GrokExodus/Voxel/*` is **frozen** except crash fixes. New work goes in `Plugins/GX*`.

---

## Architecture (Wave B)

```text
Stamp stack (FGXSphereStamp)          ← unedited crust is free
        │
        ▼
Sparse 8³ dirty pages (FGXVoxelVolume) ← only edits allocate
        │
        ├── Snapshot (COW, worker-safe)
        ├── FGXJobGraph + generation stamps
        ├── FGXMesher (Marching Cubes)
        ├── AGXVoxelChunkProxy (UDynamicMeshComponent, not PMC)
        ├── AGXVoxelWorld (invokers, crust cull, stream/unload)
        ├── UGXTerrainToolComponent (drill / place / stock)
        ├── UGXBodyMovement (inverse-square via IGXGravityField)
        └── Sparse save  Saved/VoxelWorld/*.gxsav
```

### Units

| Space | Unit |
|---|---|
| Volume, stamps, Kepler, μ | **meters**, doubles where it matters |
| UE scene / pawn / mesh | **centimeters** (`×100`) |
| Default Earth | **60 000 m** radius, **18 km** atmosphere (authored) |

### Density

`> 0` solid, `< 0` air, `0` isosurface. Packed cell is **4 bytes**:

```text
int16 Density   (quantized, ±32 m @ ~1 mm)
uint8 Material
uint8 Flags     (PlayerPlaced, Deformed, Ore, Scar, Liquid)
```

Unedited space is **not stored**. Mesh jobs sample the stamp + overlay.

### Streaming

`UGXVoxelInvokerComponent` on the pawn. `AGXVoxelWorld` streams a window:

| Band | Default |
|---|---|
| Collision / LOD0 | ~80 m near field |
| Mesh stream | 180 m |
| Unload hysteresis | stream + 70 m |
| Crust shell | radius ± relief; core is not meshed |

LOD: 0 near, 1 mid, 2 far. Collision only on LOD0.

### Meshing

CPU Marching Cubes on a snapshot (padded apron, shared edge verts).  
Apply path: `UDynamicMeshComponent` (Geometry Framework), **not** `UProceduralMeshComponent`.

Warmup (first ~2.5 s) meshes **synchronously** so the player can stand. After warmup, `FGXJobGraph` runs workers; results with a stale stamp are discarded.

### Gravity (now)

`AGXVoxelWorld` implements `IGXGravityField`:

```text
g(r) = −μ r̂ / r²
μ    = SurfaceG × Radius²
```

`UGXBodyMovement` aims `SetGravityDirection` and scales `GravityScale` to the local |g|. Artificial gravity generators come later (construct blocks).

### Sun / planet motion (designed; Wave C owns the sky)

**Do not rotate or translate the voxel planet actor.** Chaos cannot spin a 60 km collision tree.

| Layer | Truth |
|---|---|
| Ephemeris (doubles) | Earth Kepler-orbits Sol; Moon orbits Earth; sidereal spin + tilt |
| UE scene | Active body origin is fixed. No mesh spin. |
| Sky | Directional light + starfield + impostors posed by `R_inertial_to_body(UT)` |
| Ships in orbit | Integrated in inertial space, transformed into the body-fixed scene |

`GXCelestial` already has Kepler, ECI↔body, atmosphere density, and heat/q helpers. `UGXSkySubsystem` is Wave C.

### Persistence

Path: `Saved/VoxelWorld/<SaveFileName>` (default `earth_default.gxsav`).

```text
GXV1 header  (seed, radius, relief, voxel size)
page count
per dirty 8³ page: chunk xyz, page index, 512 packed cells
```

F5 saves. Auto-load BeginPlay / auto-save EndPlay on `AGXVoxelWorld`.

---

## Source map (current)

```text
Plugins/GXCore/Source/GXCore/
  GXMath.h                 m↔cm, safe normals
  GXJobGraph.*             stamps + thread-pool enqueue
  GXSnapshot.h             worker-safe COW base
  GXFrameSubsystem.*       active body, UT, ECI↔scene
  GXInterfaces.h           IGXVoxelQuery, IGXGravityField, IGXAtmosphere
  GXSaveTypes.h            GXS1 header

Plugins/GXVoxel/Source/GXVoxel/
  GXNoise.h                port of FVoxelNoise (identity)
  GXVoxelTypes.h           packed cell, chunk/page keys
  GXVoxelStamps.*          sphere SDF + biomes / ores / scars
  GXVoxelVolume.*          pages, brush, snapshot
  GXMesher.*               Marching Cubes
  GXVoxelInvokerComponent.*
  GXVoxelChunkProxy.*      DynamicMesh apply
  GXVoxelWorld.*           stream / edit / save / gravity
  GXTerrainToolComponent.*

Plugins/GXCelestial/Source/GXCelestial/
  GXKepler.*               patched-conic evaluate / from-state
  GXGravity.*              inverse-square, q, heat
  GXBodyFrame.*            inertial ↔ body-fixed
  GXCelestialBodyAsset.*   Earth / Moon defaults
  GXBodyMovement.*         character movement

Plugins/GXConstruct/       item stacks, recipes, UGXBlockDef, grid data
Plugins/GXPresentation/    AGXHUDLayout

Source/GrokExodus/
  GXExodusCharacter.*      AGrokExodusSurvivor
  GrokExodusGameMode_GX.*  AGXGameMode
  Voxel/*                  LEGACY — frozen
```

---

## Default scale

| Body | Radius | Surface g | Atmo | μ = g R² |
|---|---|---|---|---|
| Earth | 60 000 m | 9.81 | 18 000 m | 3.5316×10¹⁰ |
| Moon (data only) | 16 000 m | 1.62 | none | 4.147×10⁸ |

Moon SMA (authored) ≈ 280 km. Sidereal day (authored) 24 min so ground tracks happen in a session.

---

## Materials

| ID | Name | Role |
|----|------|------|
| 0 | Air | |
| 1 | TemperateGrass | mid-latitude surface |
| 2 | RockyCliff | escarpments / shallow crust |
| 3 | DryDirt | arid mid elevations |
| 4 | SandCoastal | lowlands |
| 5 | SnowIce | poles / high altitude |
| 6 | WetMud | wet lowlands |
| 7 | VolcanicScorched | scar patches |
| 8 | BedrockDeep | deep interior |
| 9–11 | OreIron / OreCopper / OreCrystal | mid-crust veins |
| 12 | Concrete | placeable |

Imagine source JPGs live at `Content/Voxel/Textures/Source/T_<Material>_{A,N,R}.jpg`.  
Runtime: `create_voxel_pbr_material.py` imports those JPGs plus `T_VoxelAtlas_{A,R}.png` as `/Game/Voxel/Textures/T_VoxelAlbedoAtlas` and `T_VoxelRoughAtlas`, and assigns them on the graph-only `M_VoxelTerrain_PBR`. `FGXTerrainPBR` prefers those imported 2D assets (runtime JPEG pack is the fallback). Do **not** leave TextureSampleParameter2D unbound — UE 5.8 substitutes `DefaultTextureCube` and the crust goes black. Confirm `[GXPBR] OK graph-only … albedo=/Game/Voxel/Textures/T_VoxelAlbedoAtlas`.

---

## How to play

1. Build `GrokExodusEditor`.
2. Play (project default game mode is `AGXGameMode`). Restart the editor if it was already open.
3. Wait ~2 s for near-field mesh; you spawn on the +X crust.
4. WASD + mouse. **LMB** drill, **RMB / G** place, **R** cycle material, **T** tool quality, **F5** save.

| Key | Action |
|-----|--------|
| WASD | Move on the local horizon |
| Mouse | Spherical look (parallel transport) |
| LMB | Dig / Place (hold) |
| RMB / G | Toggle drill ↔ place |
| R | Cycle place material |
| T | Toggle tool quality (1× / 2×) |
| F5 | Save dirty pages |

There is **no** bunker claim and **no** walker.

Automation (editor): `Automation RunTests GX`

| Test | What it checks |
|---|---|
| `GX.Core.FrameIdentity` | ECI↔body invertibility, save magic |
| `GX.Core.JobStampDiscard` | stale jobs do not apply |
| `GX.Voxel.DensityIdentity` | unedited volume = stamp |
| `GX.Voxel.PageSparseRoundTrip` | one dig → one 8³ page |
| `GX.Voxel.MeshSphere` | MC emits tris |
| `GX.Celestial.ClosedOrbit` | 10-period Kepler close |
| `GX.Celestial.EciBodyInvertible` | point + velocity invert |
| `GX.Celestial.DragAndHeat` | heat grows with v; surface g |
| `GX.Construct.TypesSmoke` | items / recipes / cell sizes |

---

## Wave status

| Wave | Status | Notes |
|---|---|---|
| **A** — plugin skeletons + math | **Done** | Core / Voxel volume / Celestial math / Construct types / Presentation shell |
| **B** — walkable planet | **Done** | DynamicMesh, invokers, async stamps, tools, `UGXBodyMovement`, `AGXGameMode` |
| **C** — sky that tells the truth | Next | GXSky, on-rails vessels, drag/heat on ships, navball, time warp |
| **D** — creative industry + ships | After C | Place / weld / conveyors / typed thrusters |
| **E** — Earth → Moon vertical slice | After D | Drop-pod, Moon stamps, delete legacy `Voxel/` |

---

## Known limitations

- Mesher is CPU MC; transvoxel skirts and Dual Contouring are still upgrade paths.
- PBR is a 4×2 atlas sampled by a native material graph (no Custom HLSL). Vertex color still tints if the atlas is unbound.
- Sky is still the old `AVoxelSunSetup` directional. Ephemeris is not driving the lamp.
- 60 km surface is 6×10⁶ UU from origin — LWC is on; Chaos is acceptable at that range but not at Earth–Moon span (hence body frames).
- Single-player only.
- Legacy `AVoxelPlanetActor` path still compiles but is not the product.

---

## Non-goals (v1)

Bunkers, walkers, multiplayer, n-body / Lagrange, real-scale Earth, Voxel Plugin marketplace, rotors/pistons/wheels, weapons, jump drives, GPU compute meshing.

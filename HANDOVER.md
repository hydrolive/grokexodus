# HANDOVER — Grok Exodus

Last updated: **2026-08-15** · On-disk build stamp: **GX 0.8.8**  
Branch: `main` (local, several commits ahead of origin; do not push unless asked)

## Current player-facing state

- Play **`/Game/Voxel/Maps/Lvl_VoxelPlanet`**. Do not use `Lvl_FirstPerson`.
- `AVoxelGameMode` (map override) now spawns `AGrokExodusSurvivor` + `AGXVoxelWorld` and destroys `AVoxelPlanetActor`.
- **GX 0.8.8** 0.8.7 shot: dig left a **grass overhang**, spawn **bounced**, crater stayed **grass**. Rim verts now drop on undercut air + one-cell dilate. Dropped clipmap and excavated MC faces use **dirt** (atlas 3). Spawn sits on the capsule floor; the 0.25 s re-place is skipped if already standing; stick/unstick are off for 0.6 s.
- **GX 0.8.7** 0.8.6 shot: dig went **under a grass lid**, stopped on the **8 m floor**, left **floating quads**. Deleting faces cannot work (too much = core hole, too little = islands). Walk-ring verts **drop to the volume floor** (vert or 2 m quad midpoint is air). Ring 1 starts at **160 m** so the 8 m grass is never a floor underfoot. No quad delete. Cave = edited-chunk MC under the undropped roof.
- **GX 0.8.6** 0.8.5: dig near spawn **deleted walk faces** (260+ 2 m quads + 15 8 m quads). 3×3 samples, dilation, and all saved air punched a rectangle. Punch is **mid-only**, **2 m ring only**, and only if that chunk has a **live voxel mesh**.
- **GX 0.8.5** 0.8.4: away from spawn the **8 m clipmap was a dirt lid** (we stopped punching it). The brush went under it and could not cut it. The 8 m ring is punched again, but only on **real surface air** (3×3 samples per quad), not whole saved pages.
- **GX 0.8.4** 0.8.3: dig near spawn **deleted a rectangle of walk faces** so you saw the sunk 8 m ring (and holes through it to the core). 29 saved pages punched **585** 2 m quads. Punch is only where the **surface is authoritative air** (or a mound), and **only the 2 m walk ring**. The 8 m ring stays closed.
- **GX 0.8.3** 0.8.2: mid hills were **blocky dirt stairs** with no grass/dirt blend (#1/#2). Clipmap no longer hard-assigns rock/dirt IDs — PBR slope blends. Fine disk 180 m / 8 m mid / 96 m far. Voxel transvoxel skirts only on LOD≥1 — LOD0 cave skirts were the floating black slabs and poles you could not fill or dig (#3/#4).
- **GX 0.8.2** 0.8.1 shot: landscape was **orange dirt with grass islands**. Radial lid + 4 m pad + dilate punched **685** walk quads so the 10 m ring (sunk, faceted) showed through. Punch is tight AABB; radial lid only for underground pages; no punch on 36/120 m rings; skirts no longer tilt stamp normals; coarse rings blend N toward radial so PBR stays grass.
- **GX 0.8.1** 0.8.0 PIE **asserted on spawn** (`TArray` Add of an element already in that array — skirt UV/color). Copies are locals first. Same fix on clipmap rim skirts.
- **GX 0.8.0** HLOD + transvoxel skirts. Mixed LOD is on: edited chunks are LOD0 underfoot, LOD1 past ~90 m, LOD2 past stream. Open chunk-face edges drop a core-facing skirt so LOD cracks are not sky holes. Clipmap far rings are coarser (10 / 36 / 120 m) and each ring has a rim skirt so the 2→10 m pop does not flash teal. HLOD does **not** replace CreateMeshSection. Cluster DAG is 0.9. Wave C is sky.
- **GX 0.7.60** 0.7.59: dig took **dozens of clicks** (brush nibbled 1 m of SDF; interior density is many metres). Cave walls were **orange/blue** with no shadows (normals forced planet-outward; banks `CastShadow=false`). One click is a **full CSG sphere**. Cave N faces air. Edited meshes **cast/receive shadows**.
- **GX 0.7.59** 0.7.58: pit stopped ~3 m down (ray hit the **page AABB face**), leftover **floating clipmap triangle** (5-point punch missed 8 m quads; unedited neighbors remeshed a stamp sliver). Punch is quad-AABB + 1-cell dilate + **radial lid** (origin→stamp hits the page). Ray uses **authoritative density or stamp**, never the pad box. Only edited chunks remesh. Brush sits *into* the hit so stacked digs deepen. Save/toast unchanged.
- **GX 0.7.58** 0.7.57: could not **cave** (heightfield lid), oldest bowls **healed** (last-8 / 64-cap). Volume pages are the unlimited store. Clipmap **punches edited 8³ page AABBs**; only those chunks get an MC mesh (roof + walls). No stroke cap. Hybrid ray = stamp outside / voxel density inside pages. F5 + **180 s autosave** writes `Saved/VoxelWorld/earth_default.gxsav`; overlay shows **`Saved HH:MM`**. Auto-load on PIE. Walk uses the **nearest isosurface** (cave floor), not the outer crust. Healing is later, only when no player/structure is around.
- **GX 0.7.57** 0.7.56: oldest pits **healed** (last-8 visual), same-spot dig **did not deepen** (merge), deep pit hit the **8 m floor** (#4), hill **texture seam** (#2/#3), look-back **missing chunks** (#5). All 64 strokes keep height; last 8 stay fine. No merge. Ring 1 is punched under hot pits and rebuilt with ring 0. Heightfield can deepen a quarry; a true tunnel under the crust is still volume (later).
- **GX 0.7.56** Dig went **under** the grass and could not cut it. Tool ray used voxel density; after a carve that is a hole under the clipmap (no collision). Ray now hits the **visible stamp+edit surface**.
- **GX 0.7.55** Remove: brush sinks, **grass lid stays**. Ring 1 is a full disk with InnerM=0, so `Sink = (InnerM<1) ? 0` sat it on the walk surface. Only the 2 m ring is unsunk now; ring 1 stays 2 m under.
- **GX 0.7.54** 0.7.53 shots: far range was **wallpaper rock** (`RockMacroMul` 0.07 ≈ 376 m tiles). Dig next to an add **left a grass lid** (max-sub + max-add cancelled). Far rock is ~5 km. CSG is **stroke order**.
- **GX 0.7.53** 0.7.52 look-back: **teal rectangle** (ring 1 kept a 120 m hole at the old center). Jump **floated then snapped** (`StickToStampFloor` 250 m). Ring 1 is a full disk. Jump only micro-corrects. More POI ranges around the compass (fingerprint **17** — first PIE rebakes).
- **GX 0.7.52** 0.7.51: local dig is good, but **mid hills spiked** (#1) and **seams** that a later dig healed (#2). Every stroke remeshed ring 0 **and** 1 for all 48 edits (~200 k verts, 80–130 ms hitch). Fine refine is **ring 0, last 8 strokes** only. 8 m CSG of a 1.2 m brush is gone.
- **GX 0.7.51** 0.7.50 shots: add/remove painted a **black disk** (hex dirt specks). Fine-vert face Cross points inward (same as BuildRing); we never flipped lighting normals. Outward flip restored for new verts only.
- **GX 0.7.50** 0.7.49 shots: **#1/#2** holes (each refined quad had its own verts — T-junctions). **#3** huge dirt triangle (recomputed stamp normals pulled crater slope onto neighbouring 8 m tris). Fine verts are **shared**. Stamp normals stay.
- **GX 0.7.49** 0.7.48 shots: **entire crust inside-out** (underside hills, teal void, brush fragments). `FixOutwardWinding` treated planet-outward as front; UE culls the other way, so every triangle flipped. That pass is gone. Bowl displace is along the **quad normal** so winding cannot invert.
- **GX 0.7.48** 0.7.47 shots: still **missing faces** on remove (2 m rectangle + a pie slice in the bowl). Steep CSG inverted triangles; we flipped the *normal* but the GPU culls on **winding**. Winding is repaired, tessellation is finer, skirt is two cells.
- **GX 0.7.47** 0.7.46 shots: **#1/#2/#5** 2 m black rectangles (PBR was MASKED on VertexColor.A). **#4** crater had no floor (stacked sphere CSG + no skirt, window to the core). **#3** walk away and look back — edits only lived on ring 0. Material is **opaque**. CSG is a **union**. Refine **dilates one cell**. Rings 0 **and** 1 take edits.
- **GX 0.7.46** Same PlaceSphere AV the user pasted (`SetEditHoles` :226). That write is gone in 0.7.45; 0.7.46 **deletes the call** from dig/place so that frame cannot exist.
- **GX 0.7.45** 0.7.44: spawn dig was fine; away from spawn the cut was **2 m stairs**, and **add crashed** (`SetEditHoles` → dead `PatchMid`, LoginId `a2d1a034…`). Spawn is not special — every landing uses the same path. Brush quads are **refined in the clipmap grid** (welded, no hole). `SetEditHoles` is a no-op; PatchMid is gone.
- **GX 0.7.44** 0.7.43 shot: dig was a **separate shell on uncut grass**. Vertex-color A never punched the lid. Deleted the edit-patch PMC. Ring 0 now **cuts those triangles out** and **stitches the 0.22 m crater into the same mesh**. One surface, same material.
- **GX 0.7.43** 0.7.42: subtract made a **large low-res dent** plus a high-res bowl; add had a **trim** off the grass. Clipmap stays at stamp height (no sag). Lid is **vertex-color A** on ring 0 only. Hide and patch share `EditCoverM` (R+4.5 m) so the A fade cannot open a halo. Patch rim is **local stamp ± drop**.
- **GX 0.7.42** 0.7.41 shot: still a **stone ring** on uncut grass. Shader mask cannot punch LWC WP. Subtract now **sags clipmap verts** out to R+1.25×cell via `UpdateMeshSection` (no full remesh). Fine patch is the circular bowl inside that pit.
- **GX 0.7.41** 0.7.40 shot: add OK, subtract only a **stone ring** — the bowl sat under uncut grass. Mask compared absolute hole cm to LWC/camera WP so `d >> r` and never clipped. Mask is **WP − ObjectPos** (actor-local cm). Patch is two-sided so the crater floor draws.
- **GX 0.7.40** 0.7.39 shot: add/remove **floated above the grass**. Ring 0 was sunk 50 cm (`Max(Sink,0.5)`) while the patch sat on the stamp, and the hole mask read radius from Vector A (often unused) so the clipmap never punched. Ring 0 sink is 0. Mask uses `EditRadiusN` scalars. Radius padded 30 cm.
- **GX 0.7.39** 0.7.38: add/remove **wobbled** the whole near crust (2 m ring remeshed every stroke) and the cut was a **flat pyramid**, not a circle. Circular 22 cm patch is back. Walk ring is not remeshed on brush. Clipmap **masks** pixels inside the last 8 brush spheres so the circle is the surface, not a blob on the grass.
- **GX 0.7.38** 0.7.37: tiling OK, but add/remove still spawned a **second mesh on the grass**. Deleted the edit patch. Fine walk ring is **2 m cells to 140 m**. Brush deforms that ring only — one surface.
- **GX 0.7.37** 0.7.36 shots: **tile-edge cross** (atlas+frac jumps inside a cell). Add/subtract sat as a **grey gumdrop on the grass** (place center 30 cm up, `HoleR+Drop` hemisphere, clipmap not rebuilt so the patch is a prop). Place is a shallow cap (sphere sunk 0.55 R). Dig/place rebuild ring 0 so the crust itself moves. Near PBR samples wrap grass/rock/dirt textures — no atlas frac.
- **GX 0.7.36** 0.7.35 shots: **mirror triangles** (dual-UV blend ÷0 at wrap corners) and a **sawtooth reflective skirt** around every dig (8 m quads deleted, 20 m patch/void). Clipmap stays closed. Patch is brush+1.2 m only. PBR is single rotated YZ + 2% inset. Dig does not rebuild the 560 m ring.
- **GX 0.7.35** 0.7.34 shot: texture **cross-seam** (atlas inset 16%/68% + single planar wrap). Dig **heaved a 24 m disk** (density search retargeted stamp height). Carve is the **brush sphere only**. 8 m clipmap opens a one-cell window; a 0.4 m **edit patch** fills it. PBR: 2% atlas inset + dual YZ frames (32°/77°) blended off the wrap. No Atan2.
- **GX 0.7.34** 0.7.33 shots: looking up = **black hills**, brush = **flat brown**, dig = **teal void**. Root cause of #1/#2: lon/lat `Arctangent2` never compiled (`Missing Arctangent2 input`) so UE used the default material. PBR is **rotated YZ 32°** again + 18% albedo emissive and a tiny ambient. Dig no longer deletes verts — clipmap **displaces** from `SampleDensityMeters` at the brush center. No voxel overlay.
- **GX 0.7.33** 0.7.32 shots: seam still down the view and **on every world axis** (YZ planar UVs). Dig spawned a voxel shell on the clipmap (layering). Detail popped underfoot (ring 0 rebuild at 70 m). PBR now uses **lon/lat × R** UVs (date line on the far -X side). Dig punches a 30 m clipmap hole and applies a voxel patch. Ring 0 rebuilds at 220 m / 560 m radius.
- **GX 0.7.32** 0.7.31 shots: leftover layering (ring 0 + ring 1 at sink 0 after recenter) and a **seam down the view** (grid vertex under the pawn). Outer rings sink 2 m / 4 m. Grid is half-cell offset. Stamp-only height.
- **GX 0.7.31** 0.7.30: FPS/layering OK, but **landscape ended** after a walk. Atlas `SampleHeight` is 0 off the ~400 m spawn atlas, and clipmap refused to rebuild until 400 m (the fine ring’s own radius). Clipmap now uses the **global stamp** off-atlas and recenters the inner ring every ~70 m, outers every 350 m.
- **GX 0.7.30** 0.7.29 shots: stacked sheets (#1) and a **cliff / end of the world** (#2). Voxel PMC was a 41-chunk island; clipmap sat 8 m below as a flat plane. Dual mesh cannot work. **Clipmap is the only crust** (full disk, 8 m cells to 400 m, follows every 80 m). Voxels stay data (snap/dig), `bDrawVoxelVisuals=false`. Fingerprint 16.
- **GX 0.7.29** 0.7.28: overlay stuck **Cooking collision 92%** flipping with Meshing. `near=25/32`, queue empty, 120 FPS — playable, but warmup was reset every frame until 90% meshed (never). Ready now fires at 2 near meshes. Empty crust stops after 2 retries. Fingerprint 16.
- **GX 0.7.28** 0.7.27 shots: teal river (300 m clipmap hole + 173 crust chunks hollowed as “slack air”), stacked plates (clipmap height ≠ voxel atlas), Ready at 30/64, overlay thrash, 3 FPS bursts. New split: **clipmap is the continuous crust** (48 m hole, atlas height, sunk 8 m). **Voxels are a 140 m detail shell** filled to 90% before Ready. Overlay latches Ready. One clipmap ring per rebuild. Fingerprint 16 reused.
- **GX 0.7.27** 0.7.26 shots: still stacked (clipmap hole 229 m inside 260 m stream), ground gone + overlay 12% (player fell; snap only searched 12 m; remesh storm 6000 cache misses), 11 m grass tile borders, no rock on hill sides. Clipmap starts at **stream+40 m**. Never drop an existing visual on empty remesh. Do not re-queue in-flight / deferred chunks. Snap searches 80 m+ and sticks up to 250 m. Grass tiles ~26 m, slope rock at 0.09–0.30. Fingerprint **16**.
- **GX 0.7.26** 0.7.25 shots: **two meshes stacked** (hex stone plates + floating slab — clipmap PBR inside the 80 m hole), **ground vanished** after it was loaded (`near=0/2 hollow=803`, crust chunks settled empty), stone on gentle grass hills (`Height>288 m` / `Orogeny>0.04` stamped rock). Clipmap starts at **0.88× stream** (~230 m) sunk 5–6 m. Crust-overlap empties are never hollowed (1.25 s retry). Rock only on real ranges (`Orogeny>0.22`, slope `>0.32`, height `>1 km`). Stream 260 m. Fingerprint **15** — first PIE rebakes `.gxm`. Walk FPS: stand-still is 60–120; the 81 ms Create is still a mesh-path problem. **0.8 HLOD** cuts far draw, it does not replace CreateMeshSection.
- **GX 0.7.25** 0.7.24 shot: far landscape was a **chocolate slab** (clipmap used untextured `M_VoxelHorizonFar`). Walking was **10 FPS / 81 ms CreateMeshSection** on 48-section PMCs, near dropped to 21/48, black ponds in the hole. Clipmap now uses `M_VoxelTerrain_PBR` (same grass/dirt/rock atlas ids). Hole ~80 m. Banks are 120×8 (grow to 200) so a Create rebuilds 8 sections, not 48. At most one Create per tick after warmup. Fingerprint 14.
- **GX 0.7.24** 0.7.23 play: **bouncing on the voxels**. Banks still have no collision, so CMC had no floor. The 0.08 s airborne snap teleported the capsule ~2 m up, then Unstick ejected until the *feet* were in air, then gravity dropped you through the stamp — trampoline. Stamp isosurface is now a real FindFloor; walk sticks within 80 cm; unstick only lifts the torso; last-resort snap is 1.5 s. Jump-up still ignores the floor. Fingerprint 14.
- **GX 0.7.23** 0.7.22 shots: grass hills were back, but a **black polygonal hole** sat in the near slope and walking still hit **81 ms CreateMeshSection**. Mixed LOD0/1 seams + 16×48 banks evicting in-stream crust + LOD0 empty-settle were the holes. Stream is LOD0 until transvoxel (0.8). Near/crust empties retry 4 times (not keep-last). Evict only past UnloadRadius; banks start at 28×48 and grow to 36. Same-topology remesh uses UpdateMeshSection. Clipmap valley grass is brighter. Fingerprint 14 — cache reused.
- **GX 0.7.22** 0.7.21 shot: teal void, no ground. Bank verts were chunk-local on a PMC at the planet origin, so the crust sat in the core. Verts are planet-local cm now. Apply stayed 2–5 ms.
- **GX 0.7.21** 0.7.20 shots: still walked off voxels. `CreateMeshSection` on the **600th chunk actor** was ~81 ms. Crust is now **16 PMCs × 48 sections** on the world actor — no SpawnActor on the walk path. Stamp snap 0.08 s is the floor. Same cache as 0.7.19/20.
- **GX 0.7.20** 0.7.19 shots: still walked onto chocolate clipmap (near 21/48). Collision on every new chunk was **81 ms apply / 11 FPS**, so the stream never kept up. Visuals first; collision only in 90 m. Airborne snap 0.35 s (not 4 s) so a late chunk does not drop you through. Apply drain is time-budgeted. Clipmap hole ~140 m so you do not walk on the far sheet. No stamp fingerprint change.
- **GX 0.7.19** 0.7.18 shots: 12 m clipmap sink made a **floating voxel slab** + black void; `Volcano*0.50` still stacked a **teal cone**; remesh-for-collision was the **81 ms** walk hitch; LOD1/2 cracks. Sink is 2–3 m. No cone add. Collision cooks on first mesh (no remesh while walking). LOD0 to 220 m. Jagged east-range summits, rock clipmap (no snow-white). Unload +220 m. Fingerprint 14.
- **GX 0.7.18** 0.7.17 play: walk far and **voxels stop** (near-busy deferred the 110–360 m band), everything grass, fake cone peak, FPS hitches (clipmap 460 ms, stream every 200 ms). Stream no longer defers the walk floor. Clipmap rings are coarser and rebuild independently. Summit is a warped/ridged peak on the east range, not a cone. Rock stamps at 280 m / orogeny 0.04; PBR keeps rock/snow ids. Stream 0.55 s. Collision 240 m. Fingerprint 13.
- **GX 0.7.17** 0.7.16 shots: walked onto a **black clipmap mountain** (collision only 160 m), dug a **pond** (clipmap disk under the crater), grainy sand plains, grass “mountains”, stretched dirt UVs. Inland plains are grass. Ranges are rock. One ENE stratovolcano (5 km apron, snow only on the cap — the first cone was a teal gumdrop). Clipmap is a full disk sunk 12–16 m (no hole, no pond). Collision cooks async to 320 m. Dirt is triplanar. Far mesh has an 18% fill so back-slopes are not chocolate. Fingerprint 12 — first PIE rebakes.
- **GX 0.7.16** 0.7.15 shots: near hills OK, but mid-ground was a **teal void** and the far range a detached dark sheet. Clipmap winding faced the core (single-sided Far material culled the landscape). `RangeW` never reached 1 so “mountains” were ~30 m bumps, and foothills did not start until you were on the crest. Outward winding, 3.5–6 m sink, no inner hole, rings overlap without z-fight. Spines actually peak (0.7–1.6 km). Wide rise/feet so land goes **up** to the range. First PIE rebakes crust (fingerprint 10).
- **GX 0.7.15** 0.7.14 shots: hills OK, but Domain FBm still made a **sky wall** (and voxel stairs). Cap Domain at hills; only the 8–10 km spines may be mountains.
- **GX 0.7.14** 0.7.13: near hills good, but blobs at 5.5 km + 4.6 km radius made a **sky wall** and voxel **stair towers**. Earthly spines at 8–10 km (3 km flanks, 0.8–1.5 km ridged peaks, a second range behind). Voxel stream stays on rolling ground.
- **GX 0.7.13** 0.7.12: grass OK, one mesa, **only walked downhill** (2 km plateau), no near hills, hole at the mountain base, nothing behind the first peak. Spawn pad 500 m; hills rise from 350 m (up to ~120 m). Blobs have 2.8 km flanks + ridged crests. Five massifs out to 8 km. Clipmap rings overlap more and draw to 10 km.
- **GX 0.7.12** 0.7.11 shots: grass OK, walked, but mountains were a **floating ribbon** (azimuth wedges = knife-edge walls). Three compact blobs on the 8 km limb instead. Collision cook is async again (14 FPS hitch). Clipmap sink 0.4 m.
- **GX 0.7.11** 0.7.10 shots: grass OK, but the range was a **doughnut wall** (forced 360° ring → floating arch). No ring. Azimuth FBm places a few massifs on the limb. Spawn basin still flat.
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

## Verify after 0.7.57

1. Gold `GX 0.7.57`. Dig 10 bowls in a line — the first ones stay, they do not fill in.
2. Dig the same pit several times — it goes deeper.
3. Walk 200 m, turn around — ground is there (coarse old pits OK).

## Verify after 0.7.56

1. Gold `GX 0.7.56`. Aim at grass — the brush sits on it. Dig cuts that grass. No sinking under a lid.

## Verify after 0.7.55

1. Gold `GX 0.7.55`. Subtract is a hole in the grass. The brush and the crater match. No leftover lid.

## Verify after 0.7.54

1. Gold `GX 0.7.54`. Far mountains keep color without a repeating grid.
2. Dig is a bowl in the grass, not a ring around leftover crust.

## Verify after 0.7.53

1. Gold `GX 0.7.53`. Walk 200 m, turn around — no teal rectangle in the path you came from.
2. Mountains on more than one horizon (left/right/behind, not only ahead).
3. Jump goes up and lands. No yank back to the grass at the top.

## Verify after 0.7.52

1. Gold `GX 0.7.52`. Dig underfoot. Mid hills stay still — no new spikes, no new seam.
2. Dig hitch should be much smaller (one ring, last 8 strokes).

## Verify after 0.7.51

1. Gold `GX 0.7.51`. Add/remove keeps grass/dirt texture. No black disk around the brush.

## Verify after 0.7.50

1. Gold `GX 0.7.50`. Subtract is a closed bowl — no window through the wall, no 2 m rectangle.
2. Grass/dirt blend around the crater stays the hillside, not a huge dirt triangle.

## Verify after 0.7.49

1. Gold `GX 0.7.49`. You stand on grass, not under the crust. Sky is above.
2. Subtract is a closed bowl. No missing slice. No teal void.

## Verify after 0.7.48

1. Gold `GX 0.7.48`. Subtract is a closed bowl — no missing rectangle, no pie-slice gap.

## Verify after 0.7.47

1. Gold `GX 0.7.47`. Dig is a closed bowl — no black rectangle, no window to the core.
2. Walk 200 m, turn around. The crater is still in the hill.

## Verify after 0.7.46

1. Gold `GX 0.7.46`. Add on a hill does not crash.

## Verify after 0.7.45

1. Gold `GX 0.7.45`. Walk off spawn onto a hill. Subtract is a smooth crater, not 2 m stairs.
2. Add on that hill does not crash. Bump sits in the grass.

## Verify after 0.7.44

1. Gold `GX 0.7.44`. Subtract is a crater **in** the grass, not a bowl sitting on it.
2. Add is a bump **in** the grass, not a second shell.

## Verify after 0.7.43

1. Gold `GX 0.7.43`. Subtract is one circular crater, not a low-res pit with a bowl inside.
2. Add sits flush on the grass — no halo / trim.

## Verify after 0.7.42

1. Gold `GX 0.7.42`. Subtract is a crater in the grass, not a ring on a flat lid.

## Verify after 0.7.41

1. Gold `GX 0.7.41`. Subtract opens a crater through the grass (you see the bowl, not a stone ring on flat ground).

## Verify after 0.7.40

1. Gold `GX 0.7.40`. Add is a bump *in* the grass, not a ball sitting on it.
2. Subtract is a crater *in* the grass. No floating shell.

## Verify after 0.7.39

1. Gold `GX 0.7.39`. Add/subtract is a clean circle. Landscape does not wobble.
2. The cut/add is deep and local to the sphere, not a wide shallow pyramid.

## Verify after 0.7.38

1. Gold `GX 0.7.38`. Add/subtract change the grass under the sphere. No second blob sitting on it.

## Verify after 0.7.37

1. Gold `GX 0.7.37`. No dark cross / tile-edge grid on the grass.
2. Add — a low dirt bump that is the landscape, not a grey ball sitting on it.
3. Subtract — a crater in the grass, not a leftover dome.

## Verify after 0.7.36

1. Gold `GX 0.7.36`. No dark mirror triangles / sawtooth puddles.
2. Subtract — crater matches the sphere. Grass around it stays grass.

## Verify after 0.7.35

1. Gold `GX 0.7.35`. No dark cross / tile-edge seam under the brush.
2. Subtract — crater matches the green sphere, not a 20 m hill moving up and down.
3. Several deletes stay local. Ground around them does not heave.

## Verify after 0.7.34

1. Gold `GX 0.7.34`. Look at the sky — hills keep grass/dirt texture, not black silhouettes.
2. Aim the brush at the ground — still tiled grass/dirt, not flat brown.
3. Dig a hole — crater in the clipmap, not a teal void. Walk away; the dent stays.

## Verify after 0.7.33

1. Gold `GX 0.7.33`. No cross of seams underfoot / down the view.
2. Dig a hole — one crater, not a second sheet. Clipmap has a hole there.
3. Walk 300 m. Detail changes should be out in the mid hills, not under your feet.

## Verify after 0.7.32

1. Gold `GX 0.7.32`. No stacked sheets on mid hills.
2. No dark line down the center of the view / underfoot.
3. Walk 1 km. Hills continue. FPS holds.

## Verify after 0.7.31

1. Gold `GX 0.7.31`. Walk 1 km. Hills keep going — no cliff, no drop to a flat disk.
2. Layering / FPS from 0.7.30 still hold.

## Verify after 0.7.30

1. Gold `GX 0.7.30`. One surface — no stacked sheets.
2. Walk 500 m. No cliff, no flat “lower world.” Hills continue.
3. Overlay Ready once. FPS should stay high (3 clipmap meshes, no voxel PMC).

## Verify after 0.7.29

1. Gold `GX 0.7.29`. Overlay hits Ready once and stays. No 92% flip.
2. You can walk immediately after Ready.

## Verify after 0.7.28

1. Gold `GX 0.7.28`. Overlay goes to Ready once and stays. No flipping.
2. No teal river / missing near chunks. Far hills are one surface.
3. No stacked plates on the grass underfoot.
4. Stand-still ~100 FPS. Walking should not collapse to 3 FPS after a minute.

## Verify after 0.7.27

1. Gold `GX 0.7.27`. First Play rebakes (fingerprint 16).
2. No second mesh / dark plate on the near hill. No 11 m grass grid.
3. Walk 400 m — ground stays. If you fall, you snap back. Overlay stays Ready.
4. Hill *sides* show rock; tops/plains stay grass.
5. After a minute of walking, FPS should not collapse to 3.

## Verify after 0.7.26

1. Gold `GX 0.7.26`. First Play rebakes (fingerprint 15).
2. No hex stone plates or a second mesh on the near grass. Far hills still textured.
3. Walk 400 m — ground that appeared stays. You should not fall into a teal void.
4. Stone lives on steep ranges / high peaks, not on rolling grass.
5. Stand-still ~60 FPS. Walking may still hitch on a Create; that is not 0.8 work.

## Verify after 0.7.25

1. Gold `GX 0.7.25`. Far hills are grass/dirt/rock PBR, not a chocolate sheet.
2. Walk 400 m. `meshApply` should stay well under 81 ms. FPS should stay near stand-still, not 10.
3. No black ponds in the mid hills. `visual banks` starts at 120.
4. Bounce / jump checks from 0.7.24 still apply.

## Verify after 0.7.24

1. Gold `GX 0.7.24`. Stand still — no bounce. Walk a hill — feet stay planted.
2. Jump still works (2.5 s ignore while going up). You land, you do not trampoline.
3. Walk 400 m. Holes / hitch checks from 0.7.23 still apply.

## Verify after 0.7.23

1. Gold `GX 0.7.23`. You stand on grass. No black polygonal hole in the near hillside.
2. Walk 400 m — voxels follow. `visual banks` starts at 28. `meshApply` a few ms (Update, not 81 ms Create every step).
3. near/desired stays filled. Log may show `GX-empty near-retry` then a real mesh, not an instant settle.
4. Jump still works. Clipmap valleys should not read as black ponds.

## Verify after 0.7.22

1. Gold `GX 0.7.22`. You stand on grass, not a teal void.
2. Walk 400 m — voxels follow. `meshApply` a few ms.
3. Log `visual banks=16`.

## Verify after 0.7.21

1. Gold `GX 0.7.21`. Log `visual banks=16 slots=768`.
2. Walk 400 m — grass/dirt voxels stay under you. No chocolate sheet, no floating slab.
3. `meshApply` should stay a few ms, not 81 ms. near/desired should stay filled (not 16/40).
4. Jump still works. A missed chunk snaps you back in ~0.08 s.

## Verify after 0.7.20

1. Gold `GX 0.7.20`. Cache from 0.7.19 fingerprint 14 is reused.
2. Walk 400 m — textured voxels stay underfoot. You should not step onto a chocolate sheet.
3. No black ponds in the mid hills. Walking FPS should stay near stand-still (apply << 81 ms).
4. Jump still works (2.5 s snap ignore). If you fall in a hole, snap back in ~0.35 s, not 4 s.

## Verify after 0.7.19

1. Gold `GX 0.7.19`. First Play rebakes (fingerprint 14).
2. Walk 400 m — no floating textured pancake, no black void under the crust.
3. East skyline is a **jagged rock range** with a taller summit, not a teal gumdrop.
4. Near plains stay grass; the range reads grey/brown rock. No black crack at your feet.
5. Walking FPS should stay near stand-still (no 81 ms apply every new chunk).

## Verify after 0.7.18

1. Gold `GX 0.7.18`. First Play rebakes (fingerprint 13).
2. Walk 400 m — voxel grass/dirt stays underfoot; you do not step onto a flat untextured sheet.
3. Look east: a **ridged** high peak on the range (rock flanks, small snow), not a smooth gumdrop. Foothills show rock, plains stay grass.
4. FPS while walking should stay near the 0.7.17 stand-still ~100, not drop to 11 on every 400 m.

## Verify after 0.7.17

1. Gold `GX 0.7.17`. First Play rebakes (`crust_*` fingerprint 12).
2. Spawn / plains are **grass**, not grainy sand. Walk 160 m+ — you keep collision (no bounce on a black sheet).
3. Dig a hole: rock/dirt layers, **not** a dark pond (clipmap sits 12 m under the stamp).
4. Look ENE: one taller volcanic mountain (wide flanks, **small** snow cap), not a teal gumdrop. Other ranges are rock, not grass hills.
5. Crater walls should not be stretched YZ wood-grain.

## Verify after 0.7.16

1. Gold `GX 0.7.16`. First Play rebakes (`crust_*` fingerprint 10).
2. Near: rolling grass/dirt, **no dark sheet sitting on the hills**.
3. Mid: continuous ground — no teal lake / floating strip. Land should **rise** toward the range.
4. Far: an elongated range with peaks behind peaks, not a detached sawtooth wall. Looking under a hillside should not show a paper-thin clipmap hole.
5. Log: `GXHorizonClipmap rebuilt inner=0 outer=10000` and `GX-clipmap rebuild`.

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

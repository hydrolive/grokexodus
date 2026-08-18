# HANDOVER — Grok Exodus

Last updated: **2026-08-17** · On-screen build stamp: **GX 0.12.7**  
Branch: `main` (local, several commits ahead of origin; do not push unless asked)

## Dig invariants (do not break these)

These are why 0.8–0.10 went in a circle. A later pass that violates one will
reintroduce a bug we already paid for.

1. **The edit island owns the mouth.** Brush + 2 m collar is one voxel MC (collar, lip, cave). Do not hide tile quads with air/disk/sliver predicates. Consume stamp centroids **inside the island** only after remesh has ≥12 tris.
2. **Never hide a cave mesh that backs a punched hole.** That is the black triangle (GX-shot-0130).
3. **Never move tile verts off the planet radial.** 3D wall dent folds tris into growing black blades.
4. **Texture:** floor uses rest-position triplanar (no crack swim). Steep walls use live WorldPosition (rest-pos smears the ground photo down the cliff).
5. **Do not use page AABBs as the consume shape.** That was the 6 m square lawn (GX-shot-0139). Island = union of spheres.

## Reset the voxel save on major gen / handling changes

PIE auto-loads `Saved/VoxelWorld/earth_default.gxsav` (F5 / EndPlay / 180 s
autosave). Old CSG pages stay under a fresh heightfield lid — black windows
look into a leftover cave (GX-shot-0132). After any change to density CSG,
tile sculpt, punch, remesh, or cave filter:

1. Stop PIE (EndPlay rewrites the save).
2. Copy `earth_default.gxsav` → `earth_default.gxsav.bak_pre_<ver>`.
3. Delete `earth_default.gxsav`.
4. Start PIE. Overlay should not show a stale `Saved HH:MM` from the old pages.

Do **not** skip this because “the player already has a crater.” That crater’s
density no longer matches the new lid.

Normal play **keeps** the save. Load reconstructs the lid hole from those
pages (`RestoreEditedSurfaces`) so a cave comes back with its mouth.

## Current player-facing state

- Play **`/Game/Voxel/Maps/Lvl_VoxelPlanet`**. Do not use `Lvl_FirstPerson`.
- `AVoxelGameMode` (map override) now spawns `AGrokExodusSurvivor` + `AGXVoxelWorld` and destroys `AVoxelPlanetActor`.
- **GX 0.12.7** Shots 200014–200233: globe winding was inward (A-C-B) so cube faces vanished from orbit (black planet, light through digs to the far sky). Outward + flip repair. Unlit vertex-color globe so night side still reads as crust. Stars are depth-tested `M_GXStar` ISM (Slate painted over hills/vessel). Dig preview is unlit orange.
- **GX 0.12.6** Orbit/ground sky pass from shots 192854–193642. Globe is 80² stamp + 80 m sink, **no punch** (1 km cells were cutting orbital holes). Vertex-color `M_VoxelHorizonFar` — not walk-PBR grass UV. Visual air 8 km / Rayleigh 0.12× @ 1.35 km / Mie 0 so noon is blue. Night stars are Slate dots with planet-sphere occlusion (no 3D ISM). Moon + vessel use unlit sun-Lambert (`M_GXSunLambert`) so they stay sun-white, not atmosphere-red. Sun lamp is 6500 K white; SkyLight fill 0.28.
- **GX 0.12.5** Globe punch uses triangle AABB overlap (1 km cells never sit inside a 14 m island — that was the filled dig).
- **GX 0.12.4** Vessel-view FPS: clipmap stays on the pawn (no 500 ms 90 km rebuild). Globe is 48² stamp + **punches the edit island** (that filled the dig). Stars are inertial, depth-tested, not camera-parented. Noon sky 4.5 km / Rayleigh 2.2 km / near-zero Mie. `GX-vessel` perf line.
- **GX 0.12.3** Night stars are debug points + larger ISM (M_GXStar needed InstancedStaticMeshes).
- **GX 0.12.2** Follow cam orbits the vessel (mouse look) with scroll zoom. Clipmap + stamp globe follow the camera so orbit is not a blank SkyAtmosphere sphere. Noon sky retuned Earth-blue (visual 8 km / less Mie; drag still 18 km). Stars are instanced meshes (HUD canvas was skipped). Moon impostor is Movable (kills mobility spam).
- **GX 0.12.1** Wave C polish. 40-star inertial catalog drawn on the HUD (dim in daylight). **V** cycles follow-vessel chase cam; **P** deploys chute (rails→integrated reentry). Overlay/strip shows season + solar dec. `gx.follow`, `gx.vessel.chute`, `gx.sky.season 0..3`.
- **GX 0.12.0** Wave C first ship. Ephemeris sky (`UGXSkySubsystem`) poses the sun from Kepler + sidereal spin; planet actor stays fixed. Moon impostor. Demo LEO vessel on rails. INTEGRATED path has drag/heat/breakup/parachute. Navball + orbit strip + polar map. Time warp `,` / `.` or `gx.warp` (1–1000); physics warp refused in atmo / while thrusting. Overlay shows `UT` + `W×`. `gx.sky.dump`, `gx.vessel.spawn [rails|int]`.
- **GX 0.11.1** Reload left you under a closed lid (cap + missing faces). Save v2 wrote 4 bytes of each `FVector` (doubles in UE 5.8) so the island reloaded at the origin; consume=0; `SnapToSurface` then sat you on the cave floor. Save **v3** writes explicit floats; invalid/v2 islands rebuild from near-surface air cells; spawn stays on the stamp crust. Keep the cave save (reconstruct on load).
- **GX 0.11.0** Shot GX-shot-0151: rim still leftover tile sheets — hide predicates cannot join 0.5 m lawn to 1 m MC. **Edit island:** brush+2 m is one voxel mesh (collar+cave). Tiles consume stamp centroids inside the union of spheres. Save v2 stores island spheres. Wipe `earth_default.gxsav` (bak_pre_0152).
- **GX 0.10.51** Shot GX-shot-0150: cave works; **grass sheets on unrefined tiles** as the hole grows. FineCell 8 nearest tiles. Fine 0.5 m quads hide if **any** corner is in the disk; 1 m quads still need 3 corners.
- **GX 0.10.50** Shot GX-shot-0149: cave works, 4 tiles FineCell, but **rim grass sheets** (3-corner hide). Hide if centroid is in the disk **or ≥2 corners**.
- **GX 0.10.49** Shot GX-shot-0148: FineCell **missed** (reach 37 m, spawn tiles 45 m). Same 0.80×64+8 reach as sculpt. 3-corner disk hide stays.
- **GX 0.10.48** Shot GX-shot-0147: ball fits the hole; **two black neighbor triangles** — 1 m tiles hid a whole coarse quad. FineCell up to 4 tiles under the brush. Hide a disk only if **≥3 stamp corners** are inside R+0.55.
- **GX 0.10.47** Shot GX-shot-0146: air-gated disk still hid only 8–11 quads — 1 m voxels make stamp-0.20 look solid. Hide is a **geometric surface disk R+0.55** (remesh fills). Dig still does not slump.
- **GX 0.10.46** Shot GX-shot-0146: hole opens, **too small** (hide-air n=11) so the ball sits on the rim. Hide is an air-gated **surface disk R+0.40** plus stamp-air. Dig still does not slump; remesh after hide.
- **GX 0.10.45** Shot GX-shot-0145: hole opens but **black stair-step rim** — hide disk (R+1.25) punched grass with no cave behind it. Hide is **stamp-air only**; remesh again after hide so MC fills the mouth. Dig still does not slump.
- **GX 0.10.44** Shot GX-shot-0144: still a **mess of spanning grass lids**. Slump and remesh were fighting — NotifyBrush still moved 5–25 verts while hide only opened 5–20 quads. Dig **no longer slumps** (FineCell only); remesh + hide own the hole. Hide a **brush disk** (R+1.25 m) plus stamp-air. **Reset `earth_default.gxsav`** (bak_pre_0144) — leftover pages no longer match the lid.
- **GX 0.10.43** Shot GX-shot-0143: stamp-air hid 131 quads but **rim-to-floor spans** stayed (they are steep after slump, so a lid test skips them). Hide those spans (ΔR>0.70 m or long edge) and whole dropped lids. **Stop the density-floor walk** that yanked lid verts onto the cave wall. Skip slump when the stamp column is already air. Place still never hides. Keep the cave save.
- **GX 0.10.42** Shot GX-shot-0142: cave works; mouth still a **mess of grass lids + black tris**. Sliver hide caught 1 quad — leftover patches are whole lids that slumped onto the wall (live density solid). Hide now tests the **stamp-surface column** (air at stamp−0.30 and −0.85 m), cover 14 m. Restore solid stamp columns (heals black windows). Place still never hides. Keep the cave save.
- **GX 0.10.41** Shot GX-shot-0141: cave works; mouth still had **grass slivers** spanning the pit. Centroid-air missed them because slumped lid verts sit on the dirt wall (solid). Hide also drops **lid-like slivers** (ΔR>0.85 m or edge>2.2 cell) whose verts dropped from the stamp. Steep cave walls stay. Place still never hides. Keep the cave save.
- **GX 0.10.40** Shot GX-shot-0140: **place punched far hills** (`hide-air n=14–31` after each teal click) and **dig left grass sheets** over the mouth (4-corner-only + r=1.7 missed mixed rim). Place never hides lid. Dig hide is brush-local (r=R+2) when the quad **centroid is air** and ≥2 corners are air; restore only 4-corner surface-solid. Clipmap punch is surface-air only (no 0.5/0.9 m under-cap). Keep the cave save.
- **GX 0.10.39** Shot GX-shot-0139: cave works; **square lawn punch** from `RestoreEditedSurfaces` mid-dig (`hide-air r=6.04` on the 8 m page box). Restore is **load-only**. Load hide walks a 1.5 m grid at brush radius 1.7 m. Live hide-air is **4-corner air only** (no sliver / 3-corner). Wipe leftover trench save.
- **GX 0.10.38** Shot GX-shot-0138: load restored the **old experimental cave** as a blocky trench with black windows. That `earth_default.gxsav` is not a good test (bak_pre_0138, deleted). Fresh PIE is stamp grass. New digs still save; wipe only when leftover pages are junk.
- **GX 0.10.37** Shot GX-shot-0137: leftover **save cave** under a closed lid; re-digging cut a triangle mess. Density pages were saved; the lid hole was not. Load now marks surface edit chunks as caves and, when tiles stream in, remeshes then hide-air so the mouth matches the saved volume. Do not re-punch a closed lawn.
- **GX 0.10.36** Shot GX-shot-0136: cave works; punching the lid left a **mess of stretched triangles** over the ball. Mixed rim quads spanned the pit. Hide-air now also drops 3-corner-air cells and slivers (ΔR > 1 m or edge > 2.4 cell). Next stroke heals the existing fins.
- **GX 0.10.35** Shot GX-shot-0135: cave works; **sawtooth rim** and **dirt stains** on the remaining lid. Hide-air only removes quads whose **four corners** are air; mixed rim stays as a collar. Flat remaining verts get grass UV back; lip verts stay dirt.
- **GX 0.10.34** Plan D: edited patch has **one visual owner**. Every dig remeshes cave MC first; tile quads hide only when density under them is air **and** the remesh has ≥12 tris. Tool ray hits cave PMC first. Cave sections cook collision. Punch-if-cave-vert is gone. Reset `earth_default.gxsav`.
- **GX 0.10.33** Shot GX-shot-0133: cave existed but the **lid stayed**, then **faces vanished** and the **orange ball** sat in the window. CSG carved under a lid that only slumped 0.4 m/tick; punch opened 13 quads while the cave filter kept 0 tris (`1990→0`). Lid verts now drop toward the density floor (cap follows the dig). Cave remesh keeps all non-floor MC. Punch only if ≥24 cave verts and a vert is within 0.65 m of the quad. Reset `earth_default.gxsav`.
- **GX 0.10.32** Shot GX-shot-0132: rectangular **black windows** looked into a **leftover underground cave** from `earth_default.gxsav` (9 dirty pages). Save backed up to `bak_pre_0132` and deleted. Fresh PIE is stamp crust only. Reset that save after any major voxel gen/handling change.
- **GX 0.10.32** Shot GX-shot-0131: cave **started**, then **black missing faces** and the tunnel **stopped**. Cave filter dropped walls next to the punched rim (`1233→66` tris). NearLid now only strips floor-like lid sheets. Remesh 6 chunks / 8 m so the pocket can grow. A punched mouth keeps remeshing even if the hit is not steep. Tile ray skips a face with air behind it so the ball sits on the next solid, not a leftover sliver.
- **GX 0.10.31** Editor shot GX-shot-0130: **black triangle hole**, **stretched wall dirt**, no cave. 0.10.30 punched one quad then hid the cave on the next click (`punch=0 hide=2`). Hide only when there is no punch nearby; empty remesh closes punches. Walls sample live WorldPos so dirt is not a smeared ground photo.
- **GX 0.10.30** Shots 011733–011838: crater **dirt slid every stroke** (world-pos triplanar followed dropping verts) and a **wall hit could not open a cave** (0.10.29 stripped remesh+punch). Tile UV0.y is the stamp surface radius; PBR samples that rest position so the bowl does not swim. A wall aim (hit N vs radial) remeshes two cave chunks and punches only steep quads a cave vertex covers. Floor stays a closed radial bowl. No 3D wall dent.
- **GX 0.10.29** Shots 010122–010203: **black blades grew**, **undug hills turned dirt**, some clicks did nothing. Cause: 0.10.27 3D wall dent folded tris (backfaces); each fold recomputed slope normals across the Reach window and welded every 64 m edge within 74 m. Dig is **radial-only** again (reseats leftover dents onto StampDir). Per-tick drop capped at 0.85 cell so quads cannot invert. Normals/winding only on verts that moved. Weld only the brush tile's four edges, pairs inside Cover+2 m. Inside-ball clicks that miss THit still drop MaxStep so the stroke is not a no-op. Heightfield cannot tunnel.
- **GX 0.10.28** Shots 005306 / 005323 / 005337: each dig added **seams** and **dirt on undug hills**. Full-tile winding rebuild flipped stamp tris; PaintSteepDirt painted natural slopes; punch opened black blades. Winding repair is **window-only**. Dirt only on verts the brush moved. No punch/remesh. FineCell 0.5 m welds to 1 m neighbors.
- **GX 0.10.27** Could only dig **down** — crater walls ignored the brush (radial slump). Steep verts now **recede into the dirt** toward the brush (capped 0.45 R/tick). A steep hit remeshes 2 cave chunks and punches only quads a cave vert covers (1 m). Unbacked cave meshes are hidden (no floating sheets).
- **GX 0.10.26** Shot GX-exposed-0125: a **black standing triangle** next to the ball — a crater-wall tri folded so the backface showed. After each drop, rebuild indices so Cross faces the core (0.9.11 winding). CreateMeshSection if any tri flipped.
- **GX 0.10.25** Shots 224544 → 224558: a dig **slid grass/dirt on undug hills**. First stroke FineCell-rebuilt the whole 64 m tile from the stamp (and a full-tile normal heal). Refine is now **in-place subdivide** (old verts stay put). Normals/dirt paint stay in the brush window.
- **GX 0.10.24** Shot GX-print-0123: 0.10.23 antipode yank pulled a few verts ~1 m down — **impact print + dirt slivers**, no bowl. Dig is radial again: snap to the near sphere hit (THit). If the radial misses but the vert is inside the ball (wall), slump by `R-dist`. Place still paints rock.
- **GX 0.10.23** Shot GX-tools-0122: orange **dig raised a stone cap** (projected to the outer hemisphere). Teal place raised verts but never painted UV so it stayed **grass**. Dig now moves verts to the **inner** side of the brush sphere (subtract). Place paints `PlaceMaterialId` (default rock=2).
- **GX 0.10.22** Shot GX-wallgrass-0121: radial THit **missed wall** strokes (THit≥CurR / Disc<0) and **grass ran down old crater walls** (steep verts kept grass UV + radial N). Dig projects verts **inside the 3D brush sphere** onto that sphere (walls recede). First touch of a sculpted tile recomputes N and paints dirt on steep faces.
- **GX 0.10.21** Shot GX-pool-0120: hole was a **rectangular swimming pool** with vertical walls — FlattenLongQuads snapped ΔR>1.75 m quads to min R, and cover was 1.45 R + 2 cell so verts outside the ball still dropped. Dig snaps verts to the **brush-sphere hit** only (one-cell rim). No flatten. Overlay GX 0.10.21.
- **GX 0.10.20** Shot GX-float-0119: **1 m MC sheets** cut through the orange ball — cave remesh of 23–45 tris sat on the heightfield. Dig no longer remeshes or punches. Nearby cave visuals are dropped. Punched quads restore. Quads with ΔR > 1.75 m snap to min R (no sliver-delete). Density CSG stays 3D; visual is the tile bowl.
- **GX 0.10.19** Shot GX-holes-0118: wall punch opened **black windows**. Cave filter kept ~17 floor tris; HasCaveVisualNear treated that as “filled.” A quad is punched only if a cave **vertex** is within 0.70 m. Uncovered punches are restored on the next stroke. Lid filter drops only floor-like tris so steep cave walls can back a mouth.
- **GX 0.10.18** Spawn sits on a four-tile corner. Brush reach was `0.55*64+4=39 m`; tile centers are 45 m away so **no verts moved**. Reach is `0.80*64+8`. 0.10.17 tunnel/normal work is in this binary.
- **GX 0.10.17** Shot GX-stretch-0116: could only dig **down** (heightfield) and crater walls had **vertically stretched dirt** (`LiveN = radial`). Floor still drops radially. After each window drop we **recompute face normals** (triplanar uses the wall plane). A steep hit remeshes the **2 closest** cave chunks sync, then **punches steep quads** only if that cave mesh exists. Ray skips punched quads. Lid filter ignores rim verts inside the carve ball so the mouth is not emptied.
- **GX 0.10.16** Shot GX-through-0115: orange ball went **through the visible wall** — ray used carved voxel density (air) so it sat below the tile mesh. Tool ray hits the **live tile triangles** first (two-sided). Sit-into is 0.18 R.
- **GX 0.10.15** Hold-dig hitch: FineCell cooked every nearby **34 k** vert tile and each stroke walked/uploaded the whole mesh + welded neighbors (later clicks never applied). FineCell is **0.5 m**, **one tile per stroke**. Drop walks only the brush window. No full RecomputeNormals. Weld uploads only edges that actually moved.
- **GX 0.10.14** Shot GX-seam-0113: sharp tris overlapping the orange ball + a **ripped dark seam** in the crater wall. Wall-push along −N is gone (radial only). Nearby tiles FineCell **before** any drop. Shared U/V edges are **welded to min R** after each stroke.
- **GX 0.10.13** Shot GX-leftover-0112: leftover **dirt fins** across the pit — verts inside R dropped a metre, neighbours at R+ε did not. Cover is **1.75 R + 2.5 cell** with a smoothstep falloff so the rim blends. Still no deleted tris, no remesh on the click.
- **GX 0.10.12** Shot GX-holes-0111: wall punch deleted large tris — **black windows**, remesh did not fill. Dig **never deletes tris**. Floor: radial bowl. Wall: push verts into the dirt along −N, capped 1.35×cell per tick. No remesh on the click.
- **GX 0.10.11** Shot GX-nocave-0110: orange ball on a **vertical crater wall** — radial drop cannot destroy a wall. Steep hits **punch tris** whose centroid is inside the brush and **async-remesh** the cave (no 312 ms sync flush). Floor hits still only drop verts. Lid filter 1.0 m, used tile verts only, remesh debounce left on.
- **GX 0.10.10** Shot GX-lowpoly-0109: crater walls were **low-poly with black missing triangles**. Sliver strip (edge > 2.8×cell) deleted steep wall faces after a radial drop. Dig never deletes tris. First sculpt refines to **0.35 m**. Still no per-click remesh.
- **GX 0.10.9** Shot GX-faces-0108: leftover **vertical sheet** in the pit; each hold-tick remeshed 2–4 chunks (**312 ms**, 3 FPS) and got slower as carve-balls piled up (`cache=0/114`). Dig no longer remeshes voxels. Tile verts drop radially; stretched leftover tris (edge > 2.8×cell) are stripped. Density CSG still writes.
- **GX 0.10.8** Shot GX-spikes-0107 / 0108: cave mouth had a **spikey grass lid** then a **1 m voxel slab** (punched tris + MC on the rim). Heightfield stays watertight: radial drop + 3D sphere project (wall dent). Voxel remesh only keeps tris > 1.5 m from a live tile vert. Empty lid filters do not remesh every tick.
- **GX 0.10.7** Shot GX-nocave-0106: orange ball on a **crater wall** but heightfield only dropped radially — could not destroy walls or make caves. Dig now **punches tris whose centroid is inside the brush**, remeshes 1 m voxels into that sphere, and clips the MC to the carve balls. Stream no longer drops those cave chunks. Lid scrape only near the stamp crust (no chimney in a pit). First FineCell cook still happens on virgin tiles; already-sculpted tiles are not rebuilt (that restored the lid).
- **GX 0.10.6** Shot GX-thrash-1: one crater soft, the next a **mineshaft**; texture/mesh thrash. Uncapped ray-sphere in a pit dropped verts tens of metres. Each stroke now **caps at 0.9 R**. Overlap cell removed (z-fight). UpdateMeshSection after the first FineCell cook. Clear leftover save.
- **GX 0.10.5** Shot GX-dig-0104: dig **yanked 1 m verts toward the brush** — huge dirt pyramids. First stroke refines the tile to **0.5 m**. Verts move **only along the planet radial** to the brush-sphere hit (closed crater matching the ball). Dirt on the floor.
- **GX 0.10.4** Shot GX-seam-0103: **dark crack down Y=0** (tile U=0/−1). Underfoot Nanite displaced 48 cm; the PMC neighbor did not. Tiles overlap one 1 m cell. Auto Nanite off (`gx.nanite.tiles 0`).
- **GX 0.10.3** 0.10.2 punch deleted lid tris; voxel remesh never filled them — **teal L-holes through the planet**. Dig/Add project verts onto the brush sphere (closed lid, same shape as the ball). No punch, no surface remesh, no dirt rim. Cleared leftover `earth_default.gxsav` pages.
- **GX 0.10.2** Load was 68×320 ms Nanite (3 FPS for 22 s). Dig FineCell-rebuilt the 64 m tile + recooked Nanite and sagged a **cosine bowl with a dirt rim**. Dig now **punches lid tris** in the CSG sphere and remeshes 1 m voxels into the hole. No FineCell, no Nanite on dig, no clipmap brush when a tile is live. Nanite cooks **only the tile underfoot**, 2 s after Ready. Warmup 9 tiles (not 25).
- **GX 0.10.1** 0.10.0 live: Nanite cooked 25 tiles × 320 ms on Ready (8 s hitch) and 22 cm displace was invisible at landscape range. PMC first (Ready), Nanite +1/tick. MID forces tessellation + **48 cm** world-space Magnitude. Larger world noise.
- **GX 0.10.0** Nanite displacement on walk tiles (Toreler world-space scale). PMC is collision only; a Nanite SM is the visual. Material tessellates from grass/rock roughness + world noise, Magnitude 22 cm, divided by object scale. `r.Nanite.Tessellation=1`. Toggle `gx.nanite.tiles 0|1`.
- **GX 0.9.18** Shots 032528 / 032549 / 032635: load was leftover **2 m density pyramids**; Add rebuilt those CSG spikes into a **stone canyon**; close-up stone was **radial N + RockTileMul 0.18** (35 m YZ grain). Tiles are **stamp-only** (1 m, first brush 0.25 m). Sculpt recomputes face normals. Dig paints dirt (id 3), Add stays grass. Near rock ~10 m (`RockTileMul` 0.60). Cleared `earth_default.gxsav` leftover pages.
- **GX 0.9.17** Shots 031447 / 031606: **blocky 2 m spikes**, grass texture zoomed like a mountain, orange cannot cut distant clipmap. Sculpt uses a 0.5 m refine + cosine falloff (never raise on dig). Clipmap rings sculpt too. Grass TileScale 0.0016 (~6 m).
- **GX 0.9.16** Shots 021139 / 021216 / 021258: **hide 64 m tile** = load seam, add impact crater, subtract square shaft. Never hide tiles. Edits only move tile verts (min drop / max raise). Cell 1 m. No reveal-hide on load.
- **GX 0.9.15** Shots 020228 / 020247: 2 m tile yank onto a new sphere — **harsh pyramids**, and the next orange **raised** the spike. Dig only subtracts density. Hide the lid (UV-rect test, always the tile under the brush). Remesh 1 m voxels. Tile verts never rise on remove.
- **GX 0.9.14** 0.9.13 live: add is a **dirt bump in the grass** (no extra mesh). Dig still easy to miss — 1.2 m brush vs 2 m verts. Influence is R+cell so every click moves a quad.
- **GX 0.9.13** 0.9.12 live: teal add is a **dirt blob on the grass**; orange dig did not open the lid. Hide-tile missed (tile-center test). Remesh stacked an MC sphere on the tiles. Dig/place now move tile verts (bowl / cap). Surface edited chunks do not remesh while the tile is live.
- **GX 0.9.12** Dig (orange) did not cut the grass lid; add (teal) stacked extra faces. Tiles were never hidden — density + voxel remesh sat under/on the stamp tiles. Hide overlapping tiles (they do not respawn). Remesh the tile footprint and flush 8 meshes on the click. Place remeshes too.
- **GX 0.9.11** 0.9.10 live: 5×5 + 100 m hole still a **teal window**. Walk tiles were backface-culled (A,B,C faces outward; UE wants clockwise / Cross toward the core). The “grass” in 0.9.6/0.9.8 was the clipmap. Winding flipped to A,C,B.
- **GX 0.9.10** 0.9.9 live: 140 m circular hole past the 16-tile square (~128 m) — **through the planet** along the axes. Need a 5×5 (320 m) before opening a 100 m hole. Tile stream 256 m. First tick builds 25 tiles.
- **GX 0.9.9** 0.9.8 live: on the grass, **dark fins still on mid hills** — 8 m clipmap full disk through the tiles even at 16 m sink. Clipmap hole is 140 m (inside the 192 m tile stream) only after tiles cover 140 m. Visible annulus sink 2.5 m. Safety full disk (no cover) sink 16 m.
- **GX 0.9.8** 0.9.7 live 097: hole 200 m > tile stream 192 m — **through the planet again**. No clipmap hole. Keep 0.9.7 tile winding (no extra cell, no per-tri flip) and 16 m sunk full disk.
- **GX 0.9.7** 0.9.6 live viewport: standing on grass, but **dark fins** still on mid hills. Extra tile overlap + per-triangle winding flip stacked undersides. Tiles are a single 64 m grid, one winding. Clipmap hole (200 m) opens only when tiles cover 180 m; sink 16/22/30 so coarse rings stay under.
- **GX 0.9.6** 0.9.5 shots 004847 / 004907: still **under the crust** (teal void + mirrored hills) and **layered far terrain** popping in. Clipmap hole at 192 m was a window to the core; rings 640–700 overlapped with the farther ring *higher* (sink 3.2 over 5). Clipmap is a full disk again (no hole). Rings abut and sink grows with distance (10 / 16 / 24 m). Tiles overlap one shared cell for collision. Pawn >2.5 m under the stamp snaps back up.
- **GX 0.9.5** 0.9.4 shot 001529: **see through the ground** — mirrored underside + teal void. Clipmap opened a 192 m hole on the first tick while tiles built the far corners (`u=-3,v=-3`). Ready fired at 8 tiles with nothing under the pawn. Tiles now build nearest-first; Ready needs the pawn's tile. Clipmap stays a full disk until then, then punches 192 m.
- **GX 0.9.4** 0.9.3 shot 235243: dark brown **underside sheets** still sat above the grass. Clipmap `Update` was passed `InnerHoleM=0`, so the 8 m ring was a full disk under the 2 m tiles; coarse valleys + a 2 m tile overlap drew the backfaces. Clipmap hole is the tile stream (192 m). Tiles share the 64 m edge (no extra cell).
- **GX 0.9.3** Standing on the crust, but tile seams were **dark fins** (underside / flipped winding, shot 192443). Each triangle is wound so Cross faces the planet radial. Vertex N is radial so shared edges light the same.
- **GX 0.9.2** Still under the lid (192046). `earth_default.gxsav` had **100 dirty pages** of old caves; density spawn sat in them. Save cleared (bak_pre_09). Tile verts are **tile-local** so collision bounds are 64 m, not 60 km. Spawn always uses the stamp crust, not "already on a cave floor".
- **GX 0.9.1** 0.9.0 shot: fell **through** the tiles (looking up at the underside). Tiles had no collision; density snap dropped into saved caves under the stamp. Walk tiles now cook **QueryAndPhysics** collision. HideTile turns collision off.
- **GX 0.9.0** 0.8.23 shot 183840: half a planet, hanging clipmap spikes, voxel slab. Dual-mesh is done as a strategy. Walk crust is **64 m tiles** (no punch/skirts). Clipmap starts at 180 m for the limb only. Ready waits for tiles. Dig still hits the old clipmap path until PR2 (hide tile + voxel). Nanite displacement is PR0 spike next.
- **GX 0.8.23** Digs work, but each remove hitch'd FPS. 0.8.22 scanned the whole 80 m disk (`FindEditFloorM` × 5 k verts, ~400 ms) and remeshed chunks sync on the click, then again on every first visual. Clicks are **local** (no disk rescan). Rebuilds only sample columns that look edited. Remesh is async, debounced 0.4 s. `UpdateMeshSection` when no punch. Apply at most 2 meshes/frame.
- **GX 0.8.22** Still could not dig through the **top layer** (0.8.21 log: drops 5–50 cm, many `MISS`). A 2 m grass quad does not open from a 30 cm sag. Walk disk now **punches** lid triangles over the brush / excavated columns; edited crust chunks remesh and stay streamed so the dirt bowl fills the hole. Column walk skips the stamp crust.
- **GX 0.8.21** 0.8.20 #1/#2: crater opened then a **grass sheet stayed** and the **ball sat on top**. Dig dropped 4 m (`drop=3.54..4.85`) then the walk disk **rebuilt from the stamp** 140 ms later; density walk stopped in the crust so the lid came back. Column drop now skips the crust to the excavated floor (48 m). Ray skips that lid so the ball sits in the hole. No walk-disk stamp reset for 2.5 s after a click. Bowl depth matches the preview ball.
- **GX 0.8.20** Fresh-ground dig was a **second skin** (shot 044722): 0.8.19 dropped the 40 m disk (log `verts=8..16 mesh=1584`) while the **180 m / 2 m ring at sink 0** stayed uncut. One **80 m / 2 m** walk disk is the crust; far rings start at 70 m and sit 2.4 m under. Click drops a cosine bowl + volume floor on that disk (`CreateMeshSection` + keep material). Recenter every 16 m. Brush sit 0.35 R.
- **GX 0.8.19** Still lagged: ball on grass, rectangle hole late (UpdateMeshSection on 30 k verts; first click often dropped 0). Underfoot **40 m / 2 m** disk is CreateMeshSection'd on the click (~400 verts). If no vert is in the sphere, the nearest 8 still drop.
- **GX 0.8.18** Open-ground dig: clipmap lagged, then the ball sat **under** it. 1.2 m CSG only moved verts inside 1.2 m on a 2 m grid. Visual drop radius is **R+cell** so the first click is a bowl. Brush sit 0.20 R. MarkRenderStateDirty after UpdateMeshSection.
- **GX 0.8.17** Play never left the load screen. 0.8.16 stopped meshing surface chunks so near stayed 0/0; overlay waited. Ready as soon as the clipmap exists. Overlay force-fades after ~6.5 s. No spawn FlushMeshQueue in clipmap-only mode.
- **GX 0.8.16** 0.8.15 #1: walk **behind clipmap** (edited chunks still meshed). #2: new ground only **stained** (2 m verts missed the 1.2 m SDF). Clipmap verts now drop to the **sphere floor** along each radial. Surface edited chunks are **not** meshed.
- **GX 0.8.15** 0.8.14 shots: brush **not on the hit** (aim-ray offset slid the hole) and **overlapping layers** (voxel remesh + clipmap). Ball sits **into the surface** at the hit (floor = radial, wall = aim). Surface digs do not remesh voxels. Ray step 12 cm.
- **GX 0.8.14** 0.8.13 shot: dug from **behind** the grass; the lid never moved (3D distance + “cave roof” skip). Extra clicks. Brush now drops verts by **surface** distance so a carve under the lawn pulls the top down. Edited chunks remesh again so the interior exists.
- **GX 0.8.13** 0.8.12: **untouched grass lid** over spawn and every dig (you could walk under it). `UpdateMeshSection` was given grid-only arrays after rim skirts — UE ignored the update. Live mesh is stored after skirts. Surface-air columns drop. No punch.
- **GX 0.8.12** 0.8.11 hill shot: still **removed faces + stacked layers**; each dig spiked FPS. Log: `punch=466` (55 k tris Create) vs 2 cave meshes; then 3 fps. Clipmap **never deletes quads**. A dig only **UpdateMeshSection** verts near the brush. Surface digs do not remesh voxels (that was the second layer).
- **GX 0.8.11** 0.8.10 hill shot: **[VSM] Non-Nanite Marking Job Queue overflow**, missing faces, overlapping layers. Logs: 437 punched 2 m quads vs **2** live meshes; bank PMC shadows + 60 km bounds filled the VSM; clipmap remeshed every ApplyBuiltMesh (8 fps). Punch only over a **live cave mesh**, never the 8 m ring. Banks do not cast shadows. `r.Shadow.Virtual.NonNaniteVSM=0`. NotifyEdits only on first visual.
- **GX 0.8.10** 0.8.9 shot: dirt bowl is good, but a **thin grass plane** sits on the new voxels (2 m quad missed mid/corners). Punch samples edge midpoints and dilates one shared edge. Walk ring opens air quads even before the cave mesh. Dig also scrapes a radial **lid column** so MC cannot keep a surface sheet.
- **GX 0.8.9** 0.8.8 shot: some digs **raised** terrain (mound search on rim verts), grass **overhang**, grass crater, **black floating shards** (clipmap vs voxel z-fight), clicks that did nothing. Air clipmap quads **open** over a live cave mesh so dirt/rock MC is the hole. Never search up. Brush sits along the **aim ray**. No clipmap dirt paint.
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

**Wave C is 0.12.1.** Remaining C niceties: star dome mesh (HUD catalog is live), vessel IVA, thrust input.

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
3. Leave Unreal running on `Lvl_VoxelPlanet` when the change is done. Launch **only** via `LaunchEditor.cmd` (`cmd start`, not `Start-Process`) then PIE after a rebuild. Do not end a turn with the editor closed.

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

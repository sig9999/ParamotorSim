## Session 50 — Technical-debt audit (read-only) + behavior-neutral cleanup

Repo-wide debt audit run in parallel with the pilot-appearance and
equipment-architecture passes. Scope was deliberately narrow: only changes
whose behavior equivalence is provable by inspection were applied; everything
else is recorded below as debt.

### Audit scope
Dead/stale declarations, tick cost, asset loading, Development/Shipping
boundary, logging verbosity, pointer lifetime, duplicated constants, config
and map routing, `Build.cs` dependencies, `Ps*` exec-command registration,
doc accuracy.

### Safe fixes applied
- `ParamotorEnvironment.h` — dropped `SetupWater()` and `WaterPlane`; neither
  had a definition or a single reference anywhere in the module (the ocean
  moved to `AParamotorOceanSystem`). `OceanSys` is now a `UPROPERTY()` so the
  spawned ocean actor is GC-visible instead of a bare raw pointer.
- `LandCoverService.h` — removed `TilesMutex`; never locked anywhere. Tile
  assembly runs on the game thread via HTTP completion callbacks, so no
  synchronization was lost.
- `ForestScatter.h` / `SandDebrisScatter.h` — `CachedPawn` is now a
  `UPROPERTY()`. It was cached once and never refreshed, so a destroyed or
  respawned pawn left a dangling raw pointer that `Repopulate()` dereferences.
  GC now nulls it and the existing `if (!CachedPawn)` re-fetch reacquires it;
  scatter output is unchanged while the pawn lives.
- `ParamotorOceanSystem.cpp` — the 3-second `Ocean dbg:` wave sample is now
  `#if !UE_BUILD_SHIPPING`. Development/Editor keep it; the sampled call is a
  pure query, so Shipping loses only the log line.

### Deliberately not changed
- All flight/cloth/line/engine/prop/turbulence code and constants.
- Cesium settling, globe traces, vegetation/surface semantics, sand runtime.
- MetaHuman/garment assets and their docs (Agent #1), equipment definitions
  and `AParamotorActor` asset mappings (Agent #2).
- Dev file-pipe (`C:\DevinTools\ps_cmd.txt`) — already correctly gated behind
  `#if WITH_EDITOR || UE_BUILD_DEVELOPMENT`.

### Current technical debt (documented, not fixed)
- 28 `UFUNCTION(Exec) Ps*` commands compile into Shipping. All have matching
  definitions and no duplicates; gating them out needs a Shipping build to
  verify and was left alone.
- ~116 synchronous `ConstructorHelpers`/`LoadObject`/`LoadSynchronous` sites.
  Most are constructor- or setup-time; the runtime ones inside `SpawnCell`
  re-resolve the same static mesh/material per cell.
- `AParamotorGameMode::Tick` rebuilds class-name `FString`s, re-runs
  `GetAllActorsOfClass`, and re-traces terrain each settle frame.
- Unused constants `LATITUDE_DEG`/`DALR`/`MALR`/`VK_CONST` (`AtmosphereModel.h`),
  `BobPhase` (`ParamotorActor.h`), `FlightLogTimer` (`WingActor.h`) — all in
  protected physics/equipment headers.
- `ParamotorSandSystem` caches `CachedPawn`/`Ocean`/`SurfSvc` as raw pointers.
- `PARAMOTOR_PHYSICS.md` exists at the repo root and under `docs/` with
  divergent content; neither is a strict superset.
- `docs/ARCHITECTURE_MAP.md` still says no `SK_Pilot_*` garments were
  generated, but the assets are in `Content/Characters/PilotMale/Clothing`.
  Left for Agent #1, who owns that area.

### Build results
Not run — this machine has no UE 5.8 install and no Cesium plugin, so
Development and Shipping builds and the smoke test are outstanding. All edits
are header-level declaration removals, `UPROPERTY()` annotations, and one
preprocessor gate; they need a compile before merge.

## Session 49 — Custom skinned garments (shirt/pants/boots) + pilot front-view verification

The pilot is now fully clothed: generated skinned T-shirt, cargo pants, and
boots on the MetaHuman skeleton, verified in-game front and back.

### Garment pipeline (`C:\DevinTools\gen_garments.py`)
- Reads `body_dump.json` (written by `PsDumpBody`), builds lofted tube
  geometry per garment region, transfers skin weights from the body by
  region-filtered KNN + adjacency smoothing, and emits binary FBX
  (writer `fbx_bin.py` — canonical order, `Name\x00\x01Class` naming,
  `TransformAssociateModel`, `Link_DeformAcuracy`, byte-verified footer).
- **Handedness**: UE's importer conjugates matrices by `diag(1,-1,1)`; all
  emitted `Transform`/`TransformLink`/`PoseNode` matrices and bone-local
  transforms are pre-conjugated, and verts/normals are Y-negated (winding
  reversed to compensate) so the mirror lands on authored values.
- **Region filtering** (`ALLOWED`/`RADIUS` maps) is required twice: once for
  weight transfer (torso verts must not bind to fingers) and once for
  `axis_radius` ring sizing (arm verts beside the torso inflate rings).
- Shirt specifics: neck-snapped crew collar ring, a shoulder-yoke pass that
  re-sizes rings z>128 against torso+upperarm verts (pushes only outward),
  an extra ring at t=0.955 covering the collar→shoulder slope, and tapered
  sleeve roots (`0.70` first ring) tucked inside the yoke.
- Import via `import_3garments.py` (AssetImportTask, skeletal, merges onto
  `/Game/Techwear/techwearOutfit/Meshes/metahuman_base_skel`); reassign MICs
  with `assign_mics2.py` after every reimport (slots reset).

### Materials
- `M_Pilot{Shirt,Pants,Boots}` = constant-color materials (deep sage tee,
  khaki cargo, dark leather — constants render ~2 stops brighter in sun).
- `MI_Pilot*` MaterialInstanceConstants carry the skeletal-mesh usage flag —
  the base materials' flag never persisted; MICs + `save_asset(...,
  only_if_is_dirty=False)` did. No more "missing usage flag" warnings.
- `LoadPilotGear()` leader-poses the three garment components to `GetMesh()`,
  enables all material sections — garment verts verified 2–3cm outside the
  skinned body surface via `ComputeSkinnedPositions`.

### Dev commands added
- `PsGarment <0-4>` — hide/show/ref-pose/diagnostics/detached-inspect.
- `PsFaceCam [dist] [aimZ]` — fixed `CameraActor` in front of the pawn and
  view-target blend; the follow cam always shows the back, and pawn/mesh
  rotation is re-driven per frame, so a detached camera is the only reliable
  front view. `PsCamYaw`/`PsTurn`/`PsMeshYaw` exist but are overridden by
  the orientation logic — prefer `PsFaceCam`.

### Ion token injection (globe maps)
- Committed tileset assets carry the placeholder token; at runtime
  `CesiumWorld::InjectIonTokens` (called from `DetectGlobe`) patches
  `IonAccessToken`/`DefaultIonAccessToken` on the ion server default and all
  tileset actors/components that still hold `..._REMOVED_FOR_SOURCE_CONTROL`,
  then calls `RefreshTileset`. Real token lives in
  `Saved/LocalSecrets/cesium_token.txt` (never committed).
- NOTE: `UE_LOG(LogParamotor, Log, ...)` is invisible in `-stdout` output
  (verbosity >= Warning only) — read `Saved/Logs` or use Warning for
  diagnostics you need to see.

### Verified
- In-game front/back: crew collar at neck, shoulders covered, waist sealed,
  sleeves hang, pants to ankles, dark boots, helmet+earcups+boom mic.
- Dev build green. Remaining: walk-cycle clipping not yet exercised (no
  input-injection exec exists); minor knee-seam rings on the pants; hip
  section reads slightly lighter than the legs.



## Session 48 — MetaHuman pilot + PPG helmet/visor

### Pilot meshes, grooms, and headgear
- `ParamotorCharacter.h/.cpp` creates a dedicated MetaHuman head mesh, five
  `UGroomComponent`s, PPG helmet/visor static components, and the three
  leader-posed garment components used by Session 49.
- Runtime base remains Techwear clothing (`bodyShapeG_CombinedSkelMesh`, with
  `bodyShapeH` fallback), while the generated shirt/pants/boots overlay the
  relevant body regions. `MHC_Frey_Head` supplies the face/head.
- `LoadPilotGear()` binds brush-cut hair, eyebrow, eyelash, mustache, and
  peach-fuzz grooms from `/MetaHumanCharacter/Optional/Grooms/...` to
  `MHC_Frey_Head` using plugin groom bindings.
- Project-authored PPG headgear lives under
  `/Game/Characters/PilotMale/Headgear/PPGHelmet/`:
  `SM_PPGHelmet`, `SM_PPGVisor`, and
  `M_PPG_{Shell,Trim,Liner,Cup,Pad,Webbing,Visor}`.
- `C:\DevinTools\build_helmet.py` authored the shell, liner, rim trim, ear
  cups/pads, chin straps, buckle, visor pivots, and boom microphone.
- Helmet/visor attach to the `head` bone using the inverse component-space
  reference transform; `SetVisorUp()` rotates about an authored temple pivot.
  Helmet-on hides the hair groom to avoid clipping.

### Source-side split and tooling
- Character implementation is split into `ParamotorCharacter.cpp`,
  `_Input.cpp`, `_IK.cpp`, `_Audio.cpp`, and `_Injury.cpp`.
- `ParamotorAnimInstance` receives `SitAmount`, `KiteAmount`, `AirAmount`,
  and `AirTuck`; seated/kiting/air poses run before brake-arm IK.
- `ParamotorPlayerController_Spawn.cpp` owns equipment spawning plus dev
  commands `PsHelmet`, `PsVisor`, `PsHair`, `PsEngine`, `PsGarment`,
  `PsFaceCam`, and editor-only `PsDumpBody`.
- `PsDumpBody` writes MetaHuman body sections/skin weights to
  `C:\DevinTools\body_dump.json` for garment generation.

## Session 47 — Semantic surface/material/physical-surface architecture

### The pipeline (surface twin of the vegetation architecture)
```
geodetic pos → NLCD pixel + FVegContext (coastal/runway/height)
            → ESurfaceSemantic          (WHAT it is — geographic truth)
            → USurfaceLibrary           (HOW it's currently represented)
            → FResolvedSurface          (phys type, scalars, bound assets)
```
- `ESurfaceSemantic` extended in place (VegetationSemantics.h): added
  `Mud`, `IceSnow` → 33 categories. `VegSem::ToName` covers all; existing
  ordering preserved (new values appended — don't reorder, the DA
  serializes by enum value).
- **Runway fix**: `AVegetationField::RunwayDistM` returns
  `dist − HalfWidthM`, so points ON the runway are *negative*. The old
  `>= 0 && < 20` check classified only the 0–20 m edge ring and left the
  pavement itself to NLCD. Sentinel is now `-10000` (was `-1`, which
  collided with 1 m-inside-edge) and `ClassifySurface` uses
  `> -9000 && < 20` → on-runway now reports `AirportRunway`.

### Registry — `USurfaceLibrary` / `DA_SurfaceLibrary`
(`SurfaceLibrary.h/.cpp`; asset at `/Game/Data/DA_SurfaceLibrary`,
regenerate with `C:\DevinTools\build_surf_lib.py`)
- `Surfaces: ESurfaceSemantic → FSurfaceAssetSet` {Phys (ESurfacePhysType),
  DetailPatchMaterial (soft), DetailRadiusM, Dustiness01, Deformability01,
  Trackability01, WetDarken01, WetCounterpart}.
- `PhysProfiles: ESurfacePhysType → FSurfacePhysProfile` {PhysMat (soft
  UPhysicalMaterial), FootstepSounds/Heavy (soft, lazy-cached),
  FootstepFallback (Grass|Dirt), ImpactFX/WashFX (soft Niagara), FrictionScalar,
  WashGrainMul, WashDustMul}.
- `ESurfacePhysType` (12 values) is deliberately COARSER than the 33
  semantics: Runway/Taxiway/Apron/Road → Asphalt; DevelopedHigh/Apron →
  Concrete; lawns/meadow/pasture → GrassTurf; forest floors → ForestFloor;
  WetSand/DrySand/Mud/IceSnow/Water/Gravel/Rock/Dirt for the rest.
- `USurfaceLibrary::Get()` → DA if present, else transient code defaults
  (`BuildDefaults`). `Find/FindPhys/PhysTypeFor` fall back to the same
  static table for missing entries.

### Resolver — `ASurfaceService` (`SurfaceService.h/.cpp`)
- Spawned on globe maps in GameMode beside LandCover/VegField.
- `ResolveAtLL / ResolveAtWorldPos(pos, out, knownGroundZ=-inf)` →
  FResolvedSurface {Semantic, EffectiveSemantic, Phys, Set, PhysProfile,
  Wetness01, AboveWaterCm, bResolved}.
- Wetness layering: waterline proximity (ocean `GetWaterHeightAt`, lazy
  lookup in Tick — spawn order isn't guaranteed) + `WeatherWetness()` METAR
  hook, then `WetCounterpart` re-labels the effective semantic
  (Dirt→Mud, DrySand→WetSand). Waterline upgrades to ShallowWater/OpenWater.
- **Detail patch**: pawn-following flat PMC disc rendering
  `DetailPatchMaterial` (null by default → real imagery stays dominant).
  Gate: material bound AND ground trace within 15 m → dormant in flight.
  MID gets `Wetness` + `SurfTime` scalars every frame.

### Detail-patch gotchas (cost three iterations)
1. PMC sections created in the ctor (pre-registration) never render —
   build on first use (`GetNumSections()==0 → build`).
2. Fan winding `(0, s, s+1)` with verts at (cos,sin) faces **DOWN** in UE —
   use `(0, s+1, s)`. Two-sided materials (water, sand overlay) masked this.
3. The collision trace lands on the tile collision hull; the rendered PRT
   surface sits a few cm above it — patch needs +30 cm, not +4.
4. `CreateMeshSection_LinearColor` signature differs from `CreateMeshSection`
   (FLinearColor vs FColor vertex colors).

### Consumers rewired
- **Footsteps** (`ParamotorCharacter_Audio.cpp::PlayFootstep`): trace →
  `SurfaceService::ResolveAtWorldPos` → profile `FootstepSounds` (soft,
  cached) else `FootstepFallback` → existing Grass/Dirt arrays. Off-globe
  maps keep the legacy physmat-name path.
- **Sand** (`ParamotorSandSystem`): now spawns on GLOBE maps too (was
  baked-map only). When SurfSvc resolves: `Deformability01 >= 0.5` gates
  the overlay (sand/mud/marsh on, grass/pavement off — replaces the
  geometric low-ground-near-water test); `WetnessAt` replaces the local
  wetness curve; prop-wash grain/dust scale by `WashGrainMul/WashDustMul`;
  wet sand (>0.7) skips grain lift. Deform field, footprints, ISM pools,
  recentering all untouched.
- **Prop wash**: per-slice resolved surface drives scour amount +
  grain/dust emission through the same profile.

### Dev commands (exec on PC, all work via ps_cmd.txt)
- `PsSurfGeo` — NLCD + semantic → effective + phys type + physMat +
  footstep + wash/deform/track/friction + wetness + aboveWater + patch diag.
- `PsSurfLib` — full registry dump.
- `PsSurfDebug 0|1` — semantic-colored ground discs around the pawn.
- `PsSurfSwap <Semantic> <MatPath>` / `PsSurfReset` — live rebind/restore.

### Verified live (this session)
- Swap test PASSED: `PsSurfSwap DevelopedHigh /Game/Materials/M_SurfTestPatch2`
  → green patch renders on the KWST apron, `semantic=DevelopedHigh`
  unchanged, `physMat=PM_Concrete` unchanged; `PsSurfReset` restored.
- Physical matrix: AirportRunway→PM_Asphalt (runway=-11m), DevelopedHigh→
  PM_Concrete, MowedGrass/Meadow→PM_Grass, Marsh→PM_Mud, DrySand→PM_DrySand,
  WetSand→PM_WetSand (same cell flipped by refined tile height — 8 cm above
  water), ShallowWater/OpenWater→PM_Water, Concrete→PM_Concrete,
  ForestFloor_Deciduous→PM_ForestFloor, BareSoil→PM_Dirt.
- Sand overlay active on globe at the Watch Hill wet-sand cell
  (PsSand 4: init=1 active=1, terrain RT bound).
- Wetness gradient live: 0→0.28→0.59→1.0 approaching the Misquamicut
  waterline; ShallowWater at −1 cm.
- Dev + Shipping green.

### Honest limitations
- DetailPatchMaterial is null for every semantic — the patch only draws
  during swap tests. Real close-range material packs are the drop-in.
- Footstep audio is still the two existing banks via FootstepFallback
  (`Grass*`/`Dirt*` in dumps) — bound profile arrays are the upgrade path.
- PM_* physical materials are generated placeholders (friction/restitution
  only). Named `SurfaceType` defs exist in DefaultEngine.ini; wiring UPMs
  to them is a content task.
- Aerial imagery is the ground truth at all altitudes; no macro material
  routing exists (by design — nothing is painted over photogrammetry).
- Runways detect via runways.bin capsules — taxiways/aprons resolve through
  NLCD impervious (→Asphalt/Concrete), not dedicated semantics.
- Coventry/Arcadia forest hit the known settle/floorless cycle — unrelated
  watchdog residual, not surface code.

## Session 46 — Semantic vegetation/surface layer + replaceable asset registry

### The two-layer architecture (this is the important part)
- **Semantics = identity, assets = art.** `VegetationSemantics.h/.cpp` defines
  `EVegSemantic` (20 categories: Grass_AirportMowed/Mowed/NaturalShort/
  NaturalTall/Pasture/Cropland/Coastal/Dune/Wetland/ForestFloor; Shrub_Low/
  Coastal/Wetland/Understory; Tree_Deciduous/Conifer/Coastal/Wetland/Dead/
  Sapling) and `ESurfaceSemantic` (30 categories: OpenWater…DevelopedHigh).
  `VegSem::ClassifyGrass/Tree/Shrub/Surface(FLandCoverPixel, FVegContext)` are
  pure functions — NLCD class + coastal flag + runway distance + geodetic
  height → semantic. No mesh names anywhere in classification/placement.
- **The registry** — `UVegetationLibrary` (`VegetationLibrary.h/.cpp`), a
  `UDataAsset` mapping `EVegSemantic → FVegAssetSet` {NearMeshes, MidMeshes,
  FarMeshes (all `TSoftObjectPtr` — lazy/regional loading), MaterialOverrides,
  ScaleRange, DensityScale, DrynessRange, MaxSlopeDeg, GroundClearanceM,
  bCastShadow, NearRadiusM, MaxDrawDistanceM}. ONE asset owns every art
  binding. `UVegetationLibrary::Get()` loads `/Game/Data/DA_VegetationLibrary`
  (built by `C:\DevinTools\build_veg_lib.py` — mirrors `BuildDefaults()`) and
  falls back to a transient code-built default so the sim never breaks.

### Asset-swap recipe (future premium packs)
1. Import pack → meshes exist under /Game.
2. Open `/Game/Data/DA_VegetationLibrary` → reassign the meshes under e.g.
   `Grass_NaturalTall` (fill Mid/Far tiers too for LOD reps).
3. Save → next run uses them everywhere that semantic classifies.
   Zero placement/classification/exclusion changes.
- Live test without relaunching: `PsVegSwap <SemanticName> <MeshPath>` rebinds
  the category + rebuilds all cells; `PsVegReset` re-resolves the library.
  Verified: `PsVegSwap Tree_Conifer …/jacaranda_tree_1k` → `PsSemGeo` reported
  `tree=Tree_Conifer [jacaranda_tree_1k]` and every cell respawned with it.

### Interaction contract (documented for future assets)
Wind/gusts/prop-wash travel via `MPC_Veg` params (`WindDir`, `WindSpeed`,
`GustSpeed`, `VegTime`, `PropPos`, `PropDir`, `PropStrength`), fed by
`AVegetationField::UpdateWindParams`. A replacement grass material inherits
interaction free by sampling the collection (see `M_GrassClump` for the WPO
pattern); per-instance custom data [0]=dryness [1]=hue jitter.

### VegetationField rewired
- `LoadMeshes()` and its hardcoded arrays are gone. `ResolveSet(EVegSemantic)`
  resolves + caches the runtime set; `SpawnTreeCellContent` and
  `SpawnGrassCellContent` classify each candidate point and group instances
  per resolved mesh. Placement (geodetic cells, NLCD exclusions, runway/road/
  water/slope rules, determinism, altitude thinning, rebase rebuild) untouched.
- Grass now classifies per-clump → mixed cells render mixed semantics
  (e.g., a dune edge). Barren(31) on coastal cells now grows sparse
  `Grass_Dune` — beachgrass exists on dunes where it was hard-excluded before.

### Dev tools (exec on the player controller — all work via ps_cmd.txt)
- `PsSemGeo` — NLCD pixel + surface/grass/tree/shrub semantics + resolved
  mesh names at the pawn.
- `PsVegLib` — dump the whole registry (which asset, tier counts, source).
- `PsVegDebug 0|1` — semantic-colored debug spheres over every live cell
  (grass=greens, shrub=orange, tree=blues, dead=red, none=gray).
- `PsVegSwap` / `PsVegReset` — runtime bind / restore.
- `PsVeg`/`PsGeo` unchanged.

### Verified classifications (live)
- KWST apron: NLCD 24 → DevelopedHigh, runway=126m.
- Coventry forest (41.62,-71.72): NLCD 42 → Tree_Conifer + ForestFloor_Conifer
  + Grass_ForestFloor + Shrub_Understory; 2582 trees.
- Narragansett beachfront: NLCD 23 → ParkingLot + Grass_Mowed + Shrub_Coastal.
- Watch Hill: NLCD 71 coastal → Grass_Coastal + Shrub_Coastal + Meadow.
- Block Island airport: NLCD 21 runway=95m → Grass_Mowed + MowedGrass.
- Providence downtown: NLCD 24 imperv 93% → Concrete, veg excluded.

### Gotcha
`TSoftObjectPtr` built from a bare package path (`/Game/x/y`) resolves via
`LoadObject` but serializes EMPTY — always use `Package.AssetName` form, or
populate the asset with loaded objects (build_veg_lib.py does the latter).

## Session 45 — Landmark-bridge system, PRT melt-clipper, dev file-pipe

### Landmark bridges — spec-driven (`NewportBridge.h/.cpp`)
- `ANewportBridge` generalized from the Pell-only builder into a data-driven
  landmark-bridge actor: `FBridgeSpec` (endpoints, lengths, heights, style,
  clip params) + `EBridgeStyle` {Suspension, Truss, Viaduct}. `GetSpecs()`
  returns four RI crossings; GameMode deferred-spawns one actor per spec.
  - Claiborne Pell (Newport): suspension, 3,428 m
  - Jamestown Verrazzano: through-truss over the nav span, 2,237 m
  - Mount Hope: suspension, 1,860 m, towers 87 m, deck 41 m MSL
  - Sakonnet River: low beam viaduct, 750 m
- Each computes midpoint + axis yaw from WGS84 endpoints, converts through
  `CesiumWorld::GeodeticToUnreal`, and builds local geometry (boxes/deck/
  cylinders) into three world-space ISMs.

### Georeference-rebase awareness
- `PsFlyToLL` teleports >50 km recenter the Cesium origin — world-space ISM
  instances were stranded ~55 km off (bridges reported over Cape Cod).
- Bridges now tick at 0.5 s and poll `CesiumWorld::GetGeorefOrigin`; on any
  change they re-place geodetically and rebuild all instances (log line:
  "rebuilt after georeference rebase"). Verified all four survive hops.

### PRT melt clipper (`CesiumWorldHelpers` + GameMode)
- Google PRT (ion 2275207 — the map's ONLY tileset, terrain+photo fused)
  leaves "melted" exploded photogrammetry over bridge/tidal water: km-scale
  shard fields. Runtime fix: `ACesiumCartographicPolygon` actors +
  a reflected `UCesiumPolygonRasterOverlay` (`ExcludeSelectedTiles`,
  `InvertSelection=false`) pushed onto the tileset via `AddToTileset`,
  retried in GameMode::Tick until the native tileset exists.
- `CesiumWorld::SpawnClipPolygon` / `AddPolygonClipper` are reflection-based
  (no Cesium module link). `FScriptArrayHelper::AddValue()` returns an
  INDEX, not a pointer — use `GetElementPtr(idx)` (fixed an AV crash).
- `RasterizedPolygonsTileExcluder` only drops tiles ENTIRELY inside a
  polygon — pixel masking does not visibly cut PRT shards — so zones must
  cover each melt field. Melt tiles are water-bounded: hugging the water
  also strips their over-land overhang while shore tiles straddling the
  edge survive. Don't cover land — excluded land tiles leave void holes.
- Per-spec: `ClipHalfWidthM`/`ClipHalfLengthM` build an axis corridor
  (Pell 130 m so Newport waterfront survives; Jamestown/Sakonnet 200 m);
  `ClipPolyLonLat` overrides with an explicit vertex list — Mount Hope's
  bay-wide decagon cleared the whole debris field.
- Non-bridge melt fields live in `MeltZones` (GameMode) — same polygon
  array. Pawcatuck River mouth / Little Narragansett Bay (RI-CT line)
  zone cleared most of a km-scale shard dome.

### Settle loop — clip-zone floor (`ParamotorGameMode`)
- All clip zones stored in `ClipZoneLonLat`; `InsideClipZone(lat,lon)` is a
  ray-cast point-in-polygon. Inside a zone, terrain never loads — the
  water sheet is accepted as the floor immediately (was: pawn looped at
  the −200 m geodetic floor for up to 150 s over land-classified zones).
- The globe water disc IS pawn-solid (PMC `CreateMeshSection(…,true)` →
  cooked trimesh, BlockAll) — the pawn stands on open water at msl ~0.9.
  A transient "floorless — re-settling" right after placement is just the
  async PMC collision cook; the watchdog recovers.

### Dev-harness reliability (input was dead in the harness environment)
- OS-level key/click injection into the game window is unreliable here —
  `GameInputSvc` holds a foreground lock and synthetic input never lands.
- `AParamotorPlayerController` now polls `C:\DevinTools\ps_cmd.txt` every
  ~0.5 s, executes the contents via `ConsoleCommand`, and deletes the file
  (logs `PsFileCmd: …`). Dev-only; drives PsFlyToLL/PsGeo reliably.
- Launch straight into the flight map to skip the menu when automating:
  `UnrealEditor.exe <uproject> L_Earth -game …`.
- `C:\DevinTools\shot.py` now uses `mss` on the window's screen rect —
  `PrintWindow` returns stale frames once the window loses foreground.

### Verified live (this session)
- All four bridges placed at correct geodetic coords with working deck
  collision; Mount Hope deck stands at ~41 m MSL, Pell ~66 m.
- Mount Hope Bay + channel: completely clean after the decagon.
- Jamestown: melt mound gone, through-truss + piers + deck present.
- Providence downtown: full PRT 3D after ~45 s stream-in (rooftop stand).
- N/W RI rural, Block Island moors, Greenwich Bay, Providence River,
  Newport wharf, Watch Hill/Misquamicut/Narragansett beaches: all clean.
- Water landings anywhere: pawn settles onto the disc at msl ~0.

### Known residuals / data artifacts
- Pawcatuck: one residual crumple tile at the melt-zone edge — its bounds
  straddle the polygon; excluding it would eat Pawcatuck-village tiles
  (void). Left as a documented Google-data artifact.
- Other unmapped PRT melt pockets may exist over coastal water — extend
  `MeltZones` when found.
- `LogPhysics` "Bad transform … invalid transform" spam during a bridge
  rebase rebuild is transient (ClearInstances+rebuild same tick); recovers.
- HUD ALT shows local-Z ft, not MSL.

## Session 44 — Newport Bridge hero structure + globe water disc collision

### Claiborne Pell Newport Bridge (ANewportBridge, `NewportBridge.h/.cpp`)
- Procedural suspension bridge, geodetically placed from real OSM way
  883791472 endpoints (W 41.50787,-71.36138 / E 41.49955,-71.32220), spawned
  by the GameMode on globe maps, globe-anchored via `CesiumWorld::AnchorToGlobe`.
- Real dimensions: 3,428 m total, 488 m main span, towers 122 m, deck ~66 m
  MSL midspan, 15 m deck. Three ISMs: Boxes (towers/truss/piers/rails),
  Deck (asphalt slabs), Cyls (cables/suspenders). ~1315/229/714 instances.
- **Cable bug**: `/Engine/BasicShapes/Cylinder` is 100x100 cm, not the
  assumed 200 cm — segments were half-length with visible gaps. Scaling is
  now mesh-bounds-aware (`CylHalfSize` from `GetBounds()`).
- **Material**: `M_BridgeGreen` darkened to real Pell forest green
  (0.055/0.105/0.085) — pale sage washed out against sky/water and made the
  bridge look "invisible" at distance. It renders fine at 1 km+ broadside;
  edge-on it collapses to a line, which is physically real.
- Landing on deck verified: `NewportBridge_0` at ~66.9 m MSL.

### Globe water disc collision + land-cover arbitration
- `ParamotorOceanSystem` globe disc now has query/blocking collision —
  settle can land ON water instead of tunneling to the seabed.
- `GetWaterHeightAt` globe path uses direct geodetic/geoid sampling (the old
  curvature denominator was off ~50x).
- **Settle arbitration** (`ParamotorGameMode.cpp`): an ocean-disc hit is a
  valid floor ONLY where `ALandCoverService::GetPixelAt` reports OpenWater.
  - Disc over land/unclassified → hold the settle (keep hovering); real
    terrain streams in above the sheet and wins the topmost trace.
  - Escape valves: 45 s with no land-cover tile accepts the disc (offshore
    beyond NLCD); 150 s hard last resort.
  - Verified: KWST spawn lands on real runway (geo -10 m), bay spawn lands
    on disc at msl ~0, PRT water skin wins the trace where it sits above 0.
- Peel rules: only Cesium hits peel; non-Cesium hits (bridge deck,
  buildings) are real structures — stop. Interior faces of closed shell
  hulls (normal.Z < 0.2) are skipped so the peel reaches real ground.
- Watchdog re-arms settle if the floor disappears after placement
  (`GlobeVerifyT` 120 s window, `NoFloorT` accumulation).

### Shipping build fixes
- `UWaterBodyOceanComponent::SetOceanExtent` and `UStaticMesh::SetMaterial`
  are editor-only — guarded. Grass material now applied via component-level
  `ISM->SetMaterial(GrassMat)` (valid in all builds).
- `ParamotorEditorUtils.cpp`: last 7 functions lived entirely inside
  `#if WITH_EDITOR` with no stubs — Shipping link failed on the UFUNCTION
  exec thunks. Added `#else` no-op stubs.

### Diagnostics
- `PsGeo` reports: pawn lat/lon, ellipsoid, geoid undulation, MSL est, AGL,
  water datum, ground actor + its geodetic height, and bridge ISM state
  (registered/visible/instance counts/bounds/bounds-geodetic).
- Heading conversion: true heading = 90 - Unreal yaw (ENU frame).

### Rhode Island sweep results (all validated live)
- KWST apron clean at ground level; Block Island grounded with veg boost;
  Providence downtown = full PRT (rooftop landing); Newport town real
  photogrammetry; CT border crossing shows no quality cliff; Charlestown
  moraine terrain correct (~43 m MSL); N. RI forested.
- Remaining known-cosmetic: pawn floats ~1 m over the water disc visually
  (capsule standoff vs. reported water level); distant conifers read as
  needle-spikes (LOD0 trunk-only, no imposter LODs); standing inside a
  jacaranda canopy is giant-leaf chaos (not flight-relevant).

## Session — Interactive Sand (AParamotorSandSystem)

Working dimensional footprints + prop-wash erosion in `L_Westerly`, verified
in the playable game. `AParamotorSandSystem` is spawned by the GameMode on
non-globe maps and self-detects everything — no calls into it from the
character or paramotor are needed.

### Architecture
- **Deform field**: `/Game/Textures/RT_SandDeform` + `RT_SandDeformB`
  (2048² RGBA16F — R=height offset cm, G=disturbance). Ping-pong canvas
  stamps. `RT_SandDeform` is still the asset the beach landscape material
  samples, so both paths share one field.
- **Terrain height**: top-down ortho `USceneCaptureComponent2D`
  (`SCS_SceneDepth`) into a runtime `PF_R32_FLOAT` RT. **Must use
  `InitCustomFormat` — `InitAutoFormat` silently downgraded to 8-bit and
  saturated depth at 1.0**, which launched overlay verts to the capture plane.
- **Overlay**: `UProceduralMeshComponent` 512×512 grid over a 24m recentered
  window, WPO = terrainZ + deform + lift. Triangle winding must produce
  UP normals — the first attempt wound down and the surface lit only from
  skylight (dark blue-gray) while emissive still rendered — deceptive.
- **Materials**: all built by `C:\DevinTools\build_sand_mats.py` —
  M_SandOverlay (WPO + deform-gradient normals + wetness + DbgView),
  M_SandStampFoot / M_SandStampWash (additive), M_SandErode / M_SandFade
  (modulate — needed to move dents toward zero; additive can't),
  M_SandShift (RT scroll), M_SandGrain / M_SandDust (ISM + custom-data
  ballistic WPO). `build_sand_tail.py` upgrades RTs + MPC params + wires
  the M_GroundBeach6 WPO sink via `ParamotorEditorUtils::EnsureLandscapeSandSink`.
- **`If` node pin names are `A`, `B`, `A > B`, `A == B`, `A < B`** —
  wrong names fail shader compile silently into default material.
- `CollectionParameter` nodes need the MPC param's `ParameterId` GUID, not
  just the name — `add_collection_scalar` in the tail script handles it.

### Footprints
- `foot_l`/`foot_r` are ANKLE bones riding 10-20cm up; this rig slides, so
  there is no clean Z-plant or XY-velocity stance. Working detector:
  per-foot min/max gap EMAs, stamp when the gap enters the lower ~45% band
  (or `lo+5cm` backstop). Produces clean alternating ~67cm-stride prints.
- Stamps are heel/sole/toe lobes + raised rim, oriented by velocity yaw.
- `ReadRenderTargetUV` CLAMPS to [0,1] — a -5cm dent reads 0. Verify dents
  visually or read G (disturbance), never R.

### Prop wash
- Wake marches a cone from the real `CodexPropActor` transform, intensity
  from `GetPropRPM()` (>WashMinRPM 1200). Writes scour (additive negative)
  + erode (modulate) per tick, emits ISM grains + dust.
- **Grains need world-time, not shader Time**: `MaterialExpressionTime` is
  engine real-time (seconds since app start) — a `GetTimeSeconds()` T0 is
  ~a minute behind → tau huge → giant shard artifacts. Solved with a
  `SandTime` MPC scalar fed every Tick; grain/dust materials read that.
- **Capture contamination = walls**: the depth capture must not see the
  pawn, rig, wing, water surfaces, or tree canopies — anything topmost
  lifts overlay verts into jagged walls. HiddenActors only covers the
  actor's own components; late-equipped gear needs periodic
  `RefreshCaptureHiddenActors()` (0.5s). The ocean's wave surface lives on
  the `AWaterZone` (not AWaterBody) — hide `AWaterZone` too.

### Wetness
- `GetWetnessAt` = ground Z vs `Ocean->GetWaterHeightAt` + wave offset.
  Near the waterline MISQ reads ~0.5-0.9; >0.7 suppresses grain emission
  and the wash material's `resist` term reduces scour. Verified: wet band
  produces no plume at full throttle.

### Dev console commands (player controller)
`psflyto X Y Z [Pitch Yaw]` (MOVE_Flying hover teleport — compass heading
= `(450-yaw)%360`), `pswalk` (gravity back), `pssand 0-2` (debug draws),
`pssand 3` (20cm test trench), `pssand 4` (state dump + RT readback),
`pssand 5-7` (DbgView lit/field/wet), `psengine <thr>` (spawn+equip+crank;
`thr>=0` latches throttle via `DevThrottleHold` for scripted tests,
`psengine -1` releases). NOTE: a latched 100% throttle pushes the pawn —
release before teleporting or it slides back.

### Known cosmetic (not sand)
- A translucent crumpled lattice sheet rides beside the equipped Codex rig
  (part-BP sub-mesh, likely harness/reserve). Pre-existing rig cosmetic —
  worth a fix pass later.

## Session — World-Scale Ocean (Water plugin, runtime-spawned)

`AParamotorOceanSystem` (spawned by `ParamotorEnvironment::Setup` on maps with
a Landscape) owns a runtime `AWaterZone` + `AWaterBodyOcean` + Gerstner
spectrum driven by the wind model. See `docs/OCEAN_SYSTEM.md` for the full
control/perf reference.

### The critical gotcha
**A runtime-spawned `UWaterBodyComponent` never joins a WaterZone on its own** —
`UpdateWaterZones()` only runs via `SetWaterZoneOverride`, editor
`OnWaterBodyChanged(bUserTriggered)`, or cook fixup. No zone → no WaterInfo
texture → view extension skips the zone → zero water mesh renders (you see
the under-map plane instead). Fix in `ParamotorOceanSystem::Setup()`:
`OC->SetWaterZoneOverride(Zone)` — the single line that makes runtime water work.

### Other findings
- Local-only tessellation OFF on purpose: the 1km sliding window works, but the
  far mesh beyond it has no wave data → a hard pale wedge seam from the air.
  Quadtree LODs the whole 40km zone instead.
- `SetOceanExtent()` triggers `UpdateWaterBodyRenderData()` — call it AFTER
  reshaping the island spline or the render mesh bakes the default island.
- Far mesh needs `SetFarMeshMaterial()` + `FarDistanceMeshExtent` on the zone's
  `UWaterMeshComponent`, else water ends at the zone edge.
- Private tuning members set via reflection: `bEnableLocalOnlyTessellation`,
  `LocalTessellationExtent`, `TessellationFactor`, `LODScale`.
- `MI_ParamotorOcean` (child of stock `Water_Material`) has Ocean Foam /
  Ocean Transition / Caustics on — built via `ConfigureOceanMaterial()` in
  ParamotorEditorUtils (Python can't set static permutations).
- Water perf is pixel-bound (SingleLayerWater), not vertex-bound — wave count
  is the lever (kept at 12-18 per state).
- Console tools: `PsFlyTo X Y Z [Pitch Yaw]`, `PsSea 0-4`, `PsWalk`.

### Underwater
`Water_Underside` path works out of the box — looking up from below the
surface shows the rippled ceiling + caustics. The OTST spawn site sits on the
submerged flat at -13ft (intentional offshore test site).

## Session 43 — CONUS as one streamable world: geodetic teleport, NLCD vegetation, phantom-shell fixes

`L_Earth` now functions as ONE continuous US world: geodetic dev teleport
(`PsFlyToLL lat lon [altM] [pitch] [yaw]` in `ParamotorPlayerController`), real
NLCD/MRLC land-cover streaming with disk cache (`LandCoverService`), a
CONUS runway-capsule exclusion set (`Content/Data/runways.bin`, ~7790
capsules), and `AVegetationField` — a geodetic-keyed cell streamer that grows
NLCD-appropriate trees/shrubs/grass around the pawn with wind + prop-wash MPC
response. `PsVeg` dumps cell/instance stats.

### Settle / teleport pitfalls burned into this session
- **`PsFlyToLL` zeroes `GravityScale` for the hop — placement MUST restore it.**
  Without `GravityScale=1.0` on settle-complete the pawn hangs wherever placed:
  `CurrentFloor` never populates, it never falls, and nothing downstream can
  tell parked-on-shell from on-the-ground.
- **PRT coarse LOD leaves phantom collision shells.** L_Earth's saved tileset is
  ion 2275207 = Google PRT, which owns ALL collision — its low-LOD regional
  hulls sit hundreds of meters above/below real terrain (Denver shell at +2711m
  over a ~1577m city; Death Valley hull at +1122m over a -121m floor). They are
  stable, geodetically "sane" surfaces — height sanity alone cannot reject them.
- **Peel layers, don't trust the first hit.** The settle probe now cascades:
  after a sane hit it keeps tracing downward and prefers any deeper sane
  surface >75 m below (75m+ gaps = shells; <75m = real roofs/structures).
  Same peel is applied in `VegetationField::GroundAt` so trees don't land on
  shells either.
- **Post-settle watchdog**: for 120s after placement, a walking pawn whose
  `CurrentFloor` sits >400 m above the deepest sane surface below it gets
  snapped to that surface (>400m ≈ beyond any real structure). In practice the
  restored gravity usually wins first — pawn falls through when the shell's
  collision unloads.
- **UPROPERTY or die**: `ConiferMeshes`/`DeciduousMeshes`/`ShrubMeshes`/
  `GrassMesh` were raw pointers. A rebase despawned every ISM → GC collected
  the meshes → next `SetStaticMesh` hit a zombie vtable → `_purecall` → modal
  dialog froze the game thread. All four are `UPROPERTY()` now; `MakeISM` also
  `IsValid()`-checks.
- **`EnforceCulledScreenSpaceError` matters.** With `EnableFrustumCulling=false`
  (needed so off-camera ground keeps collision), "would-be-culled" tiles froze
  at whatever LOD they first loaded — giant shells rendered overhead shadowed
  the whole scene into fake night. `EnforceCulledScreenSpaceError=true` +
  `CulledScreenSpaceError=32` refines them to a bounded moderate LOD.
- **VegField freezes while `IsAwaitingGlobeGround()`** — cells populated during
  the settle hover land on transient coarse geometry; it now waits for the
  settle to finish before spawning.
- **Downtown teleports land on real roofs** (39.74,-104.99 put the pawn on a
  ~218m Denver tower — correct, PRT buildings collide). Rural PRT-sparse areas
  (Death Valley) render as coarse smeared hulls — data limitation, not a bug.

### Verified live (this build)
- Westerly spawn → settle on runway (geo-h -10m); veg 338 trees / 1427 grass.
- `PsFlyToLL 39.74 -104.99` (Denver) → ground at geo-h 1578m, AGL stable.
- `PsFlyToLL 47.90 -123.95` (Olympic NF) → geo-h 924m, 4233 trees + 20224 grass.
- `PsFlyToLL 36.23 -116.77` (Death Valley) → placed on +1122m shell, gravity
  restored, pawn fell to real valley floor, AGL=1m stable.
- `PsFlyToLL 36.17 -115.14` (Las Vegas Strip) → real PRT rooftop, daylight.
- Zero crashes/errors; MPC param writes guarded; disk-cached NLCD tiles reuse.

## Session 42 — Globe Flight: Rhode Island on Cesium

`L_Earth` is now a working flight map — the whole US (and planet) is flyable
via streamed terrain, with Rhode Island verified end-to-end.

### What was already wired
- `DetectGlobeMap()` spawns a georeference at `GI->SelectedAirportLat/Lon/Elev`
  (defaults 41.35, -71.80 = Westerly) + Cesium World Terrain + Bing Aerial.
- `GetLaunchFieldSpawnLocation()` geodetic→Unreal spawn + `bAwaitingGlobeGround`
  settle loop: pawn hovers at 50m in MOVE_Flying, traces down until tiles load,
  then drops to real ground and walks.
- Free-flight FLY routes: curated site → its baked map (`L_Westerly`);
  any other airport → `L_Earth`.

### What was missing (fixed)
- **Frustum culling killed physics** — culled tiles lose collision, so looking
  away from the ground unloads it and the character falls. `SetupGlobeTerrain`
  now sets `CreatePhysicsMeshes=true`, `EnableFrustumCulling=false`,
  `EnableOcclusionCulling=false` on the tileset.
- **Globe anchors** — new `CesiumWorld::AnchorToGlobe(World, Actor)` attaches a
  `CesiumGlobeAnchorComponent` via reflection so actors track georeference
  origin rebasing. Applied to the pawn (RestartPlayer) and every spawned
  paramotor/wing in `ParamotorPlayerController_Spawn.cpp`.

### Atmosphere model reconnected to Charger wing (session 43)
- `ChargerWingActor::Tick` never ran `AWingActor::Tick` → `UpdateDynamicWind`/
  `UpdateThermals` were dead and the wing used a 2-sine gust hack. Now:
  `Tick` calls `InvalidateGroundZCache`, `UpdateDynamicWind(Dt, WindGusts)`,
  `UpdateThermals(Dt)`; `Step` reads `GetWindVector()` + vertical air motion
  (`GetThermalLiftAtPosition - GetThermalSinkAtPosition + TurbVertState`).
- `WindDirection` is seeded from `WindHeadingDegrees` in BeginPlay and
  reseeded when `GI->WindDirection` changes (`LastGIWindDir` tracks it);
  UpdateDynamicWind only *drifts* the base direction, never sets it.
- Verified: HUD now shows real CHALLENGING + Cloudbase 3279ft; METAR data
  flows GI → atmosphere → wing. HUD `Conditions:`/`Cloudbase:` are live.
- Still dead: `TickTestWing`, `UpdateConnectedState`, `CheckTakeoff`,
  `UpdateFlight`, `SimulateTestWingCloth/Lines`, `UpdateTestWingCanopyMesh`,
  `WingFlightPhysics`, `WingTestSim` — legacy test-wing path, unused by the
  Charger. Removal is a separate pass (decls live in WingActor.h).

### Codex equip — bone attach (session 42, cont.)
- `SpawnParamotor()` (the real Codex rig) was **dead code** — no menu button
  called it. New "Spawn Paramotor (Hangar Build)" button in the EQUIPMENT
  tab → `OnSpawnParamotorClicked` → `PC->SpawnParamotor()`.
- Codex equip attached to the **mesh root** with a static offset — the rig
  hung at camera height / tipped the character. Now attaches to `spine_02`
  like the test frame, offset `(-60, 5, 15)` rot `(0,-90,90)` — same rot as
  the test frame since the Codex -90 yaw is baked inside the actor.
- Verified: straps on shoulders, seat at hips, cage at shoulder height,
  follows the torso. The Tick sway still adds bounce on top.

### Charger wing material fix
- `M_ChargerFabric` was a near-empty material (BaseColor unhooked → black).
  Rebuilt via commandlet (`fix_charger_mat.py`): `BaseColor =
  T_Wing_Charger25 × VertexColor`, rough 0.75, spec 0.35, two-sided.
  The sail's orange/white/blue scheme is per-vertex (`SailColors` in
  ChargerWingActor.cpp); verified colored canopy in-game.
- Diagnostic: `dump_charger_mat.py` prints a material's expressions +
  property inputs via pythonscript commandlet.

### Google Photorealistic 3D Tiles — CONFIRMED WORKING
- The saved tileset in `L_Earth` is **ion asset 2275207 = Google PRT** —
  already streaming through the project's ion account, no extra key needed.
  Providence test: real TF Green runway + 3D building masses visible.
- PRT covers ~2,500 cities — Providence/etc. get full 3D photogrammetry;
  rural Westerly gets PRT's base imagery (still sharper than the old path).
- **Reflection-param gotcha that cost a test:** `SetOriginLongitudeLatitudeHeight`
  takes param `TargetLongitudeLatitudeHeight` — a missed FindPropertyByName
  silently zeroes the origin to the Gulf of Guinea (spawn at −4.5e8 cm).
  `GeodeticToUnreal`'s transform fn uses `LongitudeLatitudeHeight` instead.
- `SetGeoreferenceOrigin` must run BEFORE any `GeodeticToUnreal` spawn math —
  it's inside `DetectGlobeMap` (which FindPlayerStart/RestartPlayer call
  first), NOT BeginPlay. Distance-gated at 50km so a nearby origin isn't
  disturbed.

### Architecture notes (session 42, cont.)
- `L_Earth` has a **saved** georeference + tileset (ion bundle 2275207 =
  World Terrain + Bing Aerial). `DetectGlobe()` sees it → `SetupGlobeTerrain`
  never runs → runtime spawn path is only for georef-less globe maps.
- Raster overlays CANNOT be retro-attached at runtime — they bind during
  tileset BeginPlay. Adding one to a saved tileset must happen in the editor
  (or via commandlet level edit), not in code.
- Physics/culling flags DO take effect at runtime — `ConfigureFlightTileset`
  (SSE 4.0, CreatePhysicsMeshes, frustum+occlusion culling off, preload
  knobs) runs on saved tilesets via `EnsureFlightTileset` from BeginPlay.
- `SetGeoreferenceOrigin` re-centers a saved georef on the selected airport,
  gated by a 50km distance check — re-centering mid-load perturbs tile
  streaming, so it's skipped when already close. L_Earth's saved georef is
  already at Westerly.
- Runtime-spawned tilesets (georef-less maps) get the full stack: World
  Terrain + Bing + USGS NAIP url-template overlay (ArcGIS XYZ scheme →
  `{z}/{reverseY}/{x}` template; verified endpoint live).
- Known unsolved: the runtime-spawned tileset's collision wasn't found by
  the settle loop in one test (pawn held at 50m) — the saved tileset finds
  ground in ~50s. If a georef-less globe map is used later, verify collision
  generation on the spawned tileset.

### Detail level — honest assessment
- `MaximumScreenSpaceError` stays at **4.0** — tried 2.0 and it exploded the
  texture streaming pool ("OVER 4,434 MiB BUDGET", ground went tan-blur).
  The source imagery is already at its ~1m/px floor at foot level; lower
  SSE only pulls deeper *terrain mesh* LODs, not sharper imagery.
- `r.Streaming.PoolSize` raised 4096→8192 in `DefaultEngine.ini` for
  headroom anyway.
- Reality: globe = genuinely good at altitude (runway, buildings, coastline
  all readable) and decent on the ground, but foot-level crispness lives in
  the baked `L_Westerly` map (0.6m NAIP + LiDAR + real trees). Google PRT
  is the upgrade path for photogrammetry towns — needs a Maps API key.

### Verified live
- Launched `L_Earth` directly: georeference at Westerly, tileset streams,
  pawn placed on real ground (`Globe ground found at Z=-3509cm`).
- Standing on the actual Westerly runway surface (Bing texture + tire marks).
- Pause menu session card: `Westerly State Airport (KWST)`.
- Wing + paramotor spawn on the globe; HUD reports laid-out Charger 25M.
- Cosmetic note: HUD ALT reads local-Z (-112 ft) not MSL — relative to the
  georeference origin, not field elevation. Worth a pass later.

## Session 41 — Full-Cleanliness Audit (verification pass)

Ran the mechanical audit suite across the entire module — it's clean:

- Declaration audit: every declared method is defined, every member var
  referenced. (The lone `UW_PauseMenu.h METHODS` hit is the comment
  false-positive on `// === ... METHODS ===`.)
- IWYU first-header: all split TUs correctly lead with their owning class
  header; `ChargerAutomation.cpp` leads with AutomationTest.h (no matching
  header exists — correct).
- No duplicate includes anywhere (fixed `UW_PauseMenu_Builders.cpp` dupe +
  `ParamotorSim.cpp` corruption from the earlier trim script — dup
  `ParamotorLogging.h`, missing `ParamotorSim.h` first-include).
- No orphan section banners or >3-line blank runs.
- `WingFlightPhysics.cpp` is LIVE code (called by `TickTestWing`).
- `ParamotorBuilder` is LIVE (hangar podium assembly).
- `UW_WingSelect` now uses `UIStyle::` factories via `using` declarations.
- Runtime log warnings are all pre-existing/benign: MetaHuman `MHC_Frey_*`
  script-package import warnings, missing `M_GrassReal` reference on the
  engine Plane, first-run `ParamotorBuildSlot.sav` miss.
- Added `cesium-request-cache.sqlite*` to `.gitignore`.
- Deliberately did NOT chase every unused include — grep-based trimming has
  broken the build twice this session; redundant includes are harmless.

## Session 40 — Second Full Refactor Pass

Five more translation-unit splits; every file is now a focused unit:

- `WingActor_Ground.cpp` — bag/edge-grab/riser/carabiner rigging + ground Z
  (11 fns); `WingActor_Wind.cpp` — dynamic wind, gusts, thermals (9 fns).
  `WingActor.cpp` core is now lifecycle/state/flight-mode only (~500 lines).
- `ParamotorActor_Build.cpp` — `BuildParamotorMeshes`, `BuildAArmHierarchy`,
  `UpdateAArmPivots`, `GetCarabinerWorldPositions`.
- `UW_FlightHUD_Build.cpp` — palette + row factories + all `Build*Panel`
  fns. Shared colors live in `Private/HUDPalette.h` (update code uses them).
- `UW_PauseMenu_Builders.cpp` — all factories + `Build*` fns. Shared colors
  + `ApplyButtonColors` in `Private/PauseMenuPalette.h`.
- `UW_WingSelect.cpp` — dropped its private `MakeText/MakePanel/MakeLine/Place`
  in favor of `UIStyle::` (exact same signatures).
- Fixed `ParamotorSim.cpp` — a trim script had duplicated
  `ParamotorLogging.h` and dropped `ParamotorSim.h` (IWYU first-include
  rule was erroring in Shipping but masked by a successful link).
- Verified `WingFlightPhysics.cpp` is LIVE (called from TickTestWing) —
  not dead code despite the "legacy solver inactive" log line.
- Verified: dev + shipping green, pause menu works, wing+paramotor spawn
  works (HUD shows laid-out Charger 25M live data).

## Session 39 — Pause Menu Polish

- Footer ghost row: `ESC to resume` keycap | `RETURN TO MAIN MENU`
  (OpenLevel `L_MainMenu`) | `QUIT TO DESKTOP`.
- Drop shadow border behind the panel for depth.
- Resolution slider now drives `r.ScreenPercentage` (50–150%) and reads the
  live cvar — the game sets 125% via cvar in GI::Init, so GameUserSettings
  reported a stale 50% before. Writes both cvar + GUS for persistence.
- RESUME FLIGHT gets keyboard focus on open — Enter/Space resumes.
- "RANGE MANAGEMENT" section renamed "CLEANUP".
- Quality label dims to `Col_TextDim` when the level is CUSTOM (no preset
  highlighted); returns to accent when a preset is picked.
- FLIGHT tab quick-keys strip (F/P/R/Q-E/↑↓ chips) fills the dead space.
- Verified live: all tabs, resume, return-to-menu all work.

## Session 38 — Pause Menu Revamp (modern centered design)

- Replaced the left-sidebar layout with a centered glass panel:
  3px accent strip, "PARAMOTOR SIM" kicker + "PAUSED" title, centered
  top tab row (FLIGHT / EQUIPMENT / OPTIONS) with 2px accent underlines,
  content area, and a footer band (ESC keycap hint + QUIT TO DESKTOP ghost).
- Palette brightened across the board (lighter panel/cards, brighter text
  tiers, accent `#1AB8FF`-ish cyan); scrim alpha 0.62 so the world shows through.
- FLIGHT tab = session card (live site name + wind/temp/METAR source from GI)
  + full-width accent RESUME FLIGHT primary button + hint line.
- `CreateNavButton` → `CreateTabButton`; underline visibility toggles the
  UImage's parent SizeBox (HeightOverride 2) — UImage alone can't hold a
  2px height inside a VBox.
- SetTabActive now bolds the active tab label + shows its underline.
- Dev + Shipping green; all three tabs verified live in-game.

## Session 37 — Full Audit + Include Hygiene

- `ParamotorPlayerController_Spawn.cpp` — all spawn helpers moved out
  (`SpawnParamotor`, `FindGroundZ`, `SpawnTestWing/Paramotor/Rider185`,
  `ClearSpawnedActors`); core is now input + per-tick rig/throttle (~712 lines).
  **New vehicle kinds go in that file.**
- Third copy of `MakeText`/`FillSlot` found in `UW_MainMenu.cpp` — deleted;
  it now uses `MenuUtil::` like the rest.
- Per-file include audit: split TUs trimmed to only what they use
  (`ParamotorCharacter_*` dropped 12-16 dead includes each;
  `UW_HangarMenu_*`/`UW_FreeFlightMenu_*` dropped 10-25 each;
  `ParamotorActor.cpp` dropped WingActor/GI/Billboard/UserWidget/GameplayStatics
  after the split). Careful: `WingActor.h` defines `CM_PER_M`,
  `PROP_REDUCTION_RATIO`, `PERCENT_FACTOR` — ParamotorActor + Injury need it.
- Copyright headers added to all 23 files that lacked them; orphaned
  `// ====` banners + blank runs cleaned from split parents.
- Decl/impl audit: every UFUNCTION/declared method across all headers
  resolves to a definition (verified mechanically).
- Dev + Shipping builds green; runtime verified.

## Session 36 — Expansion-Ready Refactor

### CodexParts.h — the single source of truth for part assembly
New `Public/CodexParts.h` owns `EHangarPart` (moved out of ParamotorBuilder.h),
`CodexParts::BPPath(Part)` (real-manufacturer BP per slot),
`CodexParts::RelTransform(Part)` (frame-space mounts), and the authored socket
names (`PropSocket`, `EngineAudioSocket`, `PropAxis`, `PropHub`).
Both `AParamotorBuilder` (hangar) and `AParamotorActor` (in-world rig) read this
table — the same math was duplicated as parallel switches/arrays before.
**To add a part kind (trike wheel, quad motor, second tank):** add the enum
value, a `BPPath`/`RelTransform` case, a slot in `ParamotorBuilder`'s ctor +
`GetSlot`, and a spec array + `FParamotorBuild` index.

### ParamotorCharacter split (1136 → ~679 core)
- `_Injury.cpp` — Trip, UpdateFallState (ragdoll/get-up), GetInjuryName,
  ApplyImpactInjury, HealInjury
- `_Audio.cpp` — footstep loading/playback, wind + fall audio
- `_Input.cpp` — SetupPlayerInputComponent + all axis/action handlers
- `_IK.cpp` — brake-arm IK targets + solver
- Core keeps: ctor, BeginPlay, mesh/anim loading, Tick dispatch, thrust
  physics, posture/animation, trip-risk, rotation.

### UW_HangarMenu split (877 → ~480 core)
- `_Catalog.cpp` — catalog overlay, brand/model browsing, equip, spec text
- `_Garage.cpp` — saved-builds overlay + save/load/delete
- Core keeps init, RefreshUI, BuildHangarData, SyncBuilderVisibility.

### Airport table ↔ site table unified
- `UParamotorGameInstance::FindSiteByICAO(ICAO)` — const lookup (WST↔KWST
  tolerant); `SelectSiteByICAO` now delegates to it.
- `WorldMapGameMode` airport `bAvailable` is now computed from
  `FindSiteByICAO` — adding a `LaunchSites` entry lights up the airport pin
  automatically instead of editing two tables.
- `AWorldMapGameMode::GeodeticToUnreal` deleted — calls
  `CesiumWorld::GeodeticToUnreal`, which now tries BOTH the renamed
  `TransformLongitudeLatitudeHeightPositionToUnreal` and the old name.

### Verified
Dev + Shipping builds green; game launches to KWST apron. Title bar now shows
"Paramotor Sim" (new project name).

## Session 35 — Release Hygiene Pass (cont.)

### Dedup pass 2
- `CodexHelpers.h` — `CodexHelpers::FindHelper` / `HideHelperVisuals`, shared
  by `ParamotorBuilder` and `ParamotorActor_Codex` (they each carried identical
  private copies — both deleted).
- `ReflectionProps.h` — `Refl::SetNumeric` / `SetNumberOrBool` / `SetInt` /
  `SetBool`, shared by `CesiumWorldHelpers` and `WorldMapGameMode`
  (`AWorldMapGameMode::SetNumericProperty` + the helpers' three anonymous
  setters all deleted).
- `MenuWidgetHelpers.h` gained `GameUserSettings` helpers:
  `UserSettings()`, `QualityName()`, `CurrentQualityLevel()`,
  `ApplyQualityLevel()`, `ApplyVSync()`, `ApplyShadows()`,
  `ApplyResolutionScale()` — both `UW_PauseMenu` and
  `UW_FreeFlightMenu_Settings` now call these instead of duplicating the
  GUS apply blocks (~60 lines of duplication gone).
- Stale "Ninigret" strings in GameMode logs renamed to "launch site".
- Shipping build + dev build both green after all of the above.

## Session 35 — Release Hygiene Pass

### Logging
- `ParamotorLogging.h` declares `LogParamotor` (module-wide category);
  `ParamotorSim.cpp` defines it. All 142 `UE_LOG(LogTemp, …)` callsites
  converted across 19 files — routine `Warning` status spam demoted to
  `Log`, real `Error`/`Fatal` kept. Include `"ParamotorLogging.h"` in any
  file that logs.

### Shipping config
- `DefaultGame.ini` now has `GeneralProjectSettings`: `ProjectID`
  (0CB6461F…), `ProjectName="Paramotor Sim"`, `CompanyName="SD"`,
  `CopyrightNotice`, `Description`, `ProjectVersion=0.1.0`,
  window/resize flags.
- `.uproject` filled in: Category=Games, Description.
- `HangarGameMode`: `RerunConstructionScripts` guarded `#if WITH_EDITOR`
  (editor-gated in 5.8; runtime spawn already runs construction).

### Verified
- `ParamotorSim Win64 Shipping` compiles clean.
- `UnrealEditor-Cmd -run=cook -targetplatform=Windows -iterate`:
  **0 errors, 37 warnings** — all benign (cvar priority notes, MetaHuman
  RigLogic script packages not loaded in the cook commandlet).

### Still needed before an actual Steam build
- **Steamworks**: AppID + `OnlineSubsystemSteam` plugin + `[OnlineSubsystemSteam]`
  block in DefaultEngine.ini + `SteamAppId` in DefaultEngine.ini
  `URL`/`Steam` sections. Needs the user's registered AppID.
- **Icons/exe branding**: `Build\Windows\Application.ico` + project icon in
  project settings. Needs art.
- **Min-spec decision**: `RayTracingMode=Full` in DefaultEngine.ini gates
  hardware — deliberate quality choice; confirm before store page copy.
- **Cesium tokens** live in the level/settings — make sure none are committed
  before publishing.
- Pause-menu Spawn tab kept — it's player-facing equipment spawning, not a
  dev cheat.

## Session 34 — Full Source Refactor

### New files
- `WeatherService.h/.cpp` — `UWeatherService` UObject owns the whole METAR
  pipeline (URL build → HTTP fetch → JSON parse → writes all `Live*`/weather
  fields on the GameInstance) and broadcasts `OnWeatherUpdated`. The weather
  tab is pure UI now.
- `ParamotorActor_Codex.cpp` — Codex real-part assembly: `FindCodexHelper`,
  `HideCodexHelpers`, `BuildCodexVisuals`, `ConfigureFromBuild`.
- `ParamotorActor_Propulsion.cpp` — `UpdatePropMeshRotation` + blade-element
  thrust/torque/power model (`ComputeBladeElement`, `GetThrustForceN`,
  `GetPropTorqueNM`, `GetPropRPM`, `GetEnginePowerW`).
- `CesiumWorldHelpers.h/.cpp` — `namespace CesiumWorld`: `DetectGlobe`,
  `SetupGlobeTerrain`, `SetHires`, `GeodeticToUnreal`. All reflection-based
  Cesium access lives here; will also host the Google 3D Tiles layer.
- `SpecCatalog.h` — all part-catalog structs (`FParamotorWingSpec`,
  `FParamotorEngineSpec`, `FParamotorFrameSpec`, `FParamotorPropSpec`,
  `FParamotorHarnessSpec`, `FParamotorBuild`) + `FLaunchSite`, moved out of
  `ParamotorGameInstance.h` (which includes it, so includers are unaffected).
- `MenuWidgetHelpers.h` — `namespace MenuUtil`: the menu-local `MakeText`
  (with outline support), `MakePanelStyle`, `FillSlot` — shared by the
  FreeFlightMenu TUs via `using namespace MenuUtil;`.

### File splits
- `UW_FreeFlightMenu.cpp` 1632 → 499: `_Weather.cpp` (panel + manual sliders
  + fetch hook), `_Settings.cpp` (quality/vsync/res/volume/sensitivity),
  `_Hangar.cpp` (catalog browse + spec detail builders). Main file keeps
  init, tab switching, airport display, FLY.
- `ParamotorActor.cpp` 1071 → 783.
- `ParamotorGameMode.cpp` 552 → ~390: Cesium block moved out, dead
  `GetCoastalHeight`/`GeodeticToUnreal`/`SetCesiumHires`/`SetupGlobeTerrain`
  members removed, dead `ProceduralMeshComponent.h` include gone.
- `UParamotorGameInstance::Init()` moved inline → cpp (header is data only).

### Dead code removed
`GetTrimmerPitchOffset` (alias of `GetTrimmerAoAOffset`), `ClearInjury`,
`WorldMapGameMode::GetSelectedAirport`, `ParamotorBuilder::HasPart` /
`GetPartMeshCount`, per-frame `DrawDebugDirectionalArrow` thrust arrow,
the 4-second `Tick loc` debug log.

### Still open
- `M_GrassReal` missing-asset warning when the fallback plane loads
  (pre-existing; harmless — landscape uses `M_GroundClean`).
- `WingActor.cpp`/`WingFlightPhysics.cpp`/`WingTestSim.cpp` already split;
  `ParamotorCharacter` (movement+injury+audio+IK ~1136) and `UW_PauseMenu`
  (917) are cohesive — left alone.
- `UW_FlightHUD.cpp` (1154) is sectioned internally — left alone.

## Session 33 — EOS Audio Socket, Live-Weather Checkbox, World Strip + Site Table

### EOS engine audio (verified in log)
- The EOS 100 BP has an authored `SOCKET_EngineAudio` component — that is the
  "built-in" engine sound point. `BuildCodexVisuals` now attaches `EngineAudio`
  to it (prop wind → `AXIS_Prop`, exhaust → engine root, starter stays on the
  rig root). Log line: `engine audio on SOCKET_EngineAudio`.
- **No EOS-specific wav exists** — the loops are still
  `/Game/Sounds/engine_idle|engine_running|exhaust|prop_wind`. A real EOS 100
  recording dropped into Content/Sounds is the remaining upgrade.
- `FParamotorEngineSpec` gained `IdleRPM`/`RedlineRPM`; EOS 100 = 2600/9500.
  `AParamotorActor` uses `EngineIdleRPM`/`EngineRedlineRPM` (set in
  `ConfigureFromBuild`), HUD bar uses `GetRedlineRPM()`.

### Weather tab — real checkbox
- LIVE toggle is now a bordered square (`LiveCheckOutline`/`LiveCheckFill`)
  + "LIVE WEATHER" label + "real METAR feed" hint — checked = green fill.
- Status line falls back to "custom mode — sliders unlocked" when live is off.

### World state — deliberately bare
- User direction: scrap Ninigret visuals, build Westerly State Airport (KWST)
  properly. L_Westerly stripped to landscape-only: 6 ISM actors deleted
  (RoadsActor + 5 tree ISM groups). Spawn moved to the **KWST apron**
  (UTM19N 265748/4581206 → world -383231, 119083).
- `M_GroundClean` — ortho inside its 5km bounds, muted green outside, flat
  normal, no grass tiling (the old `M_GroundFix` grass×ortho×2.8 is gone).
- **Ortho does NOT cover KWST** — it spans world X∈[649600,1149600],
  Y∈[192200,692200] (Ninigret only). The airport area is the fallback green.
  NAIP 2023 tiles + USGS 1m LiDAR DEMs are on disk under `TerrainData/`.

### Launch site table (data-driven spawn)
- `FLaunchSite{Name, ICAO, Lat/Lon, Elev_m, MapName, LocalSpawn}` in
  `ParamotorGameInstance.h`; `LaunchSites` + `ActiveSiteIndex` +
  `EnsureSiteData()` + `GetActiveSite()` + `SelectSiteByICAO()` (matches
  "WST"→"KWST" via K-prefix).
- `GetLaunchFieldSpawnLocation` uses `ActiveSite.LocalSpawn`; the FLY button
  calls `SelectSiteByICAO` — curated site opens its MapName, everything else
  still opens `L_Earth` (Cesium globe).

### Cleanup / refactor
- `GetCoastalHeight` dead code removed; `AParamotorEnvironment` already owned
  sky/sun/fog/post/water (spawned by GameMode BeginPlay).
- Deleted maps: `L_OpenWorld`, `LV_MainMenu` (+ its HLOD asset). Live maps:
  `L_MainMenu`, `LV_World`, `L_Westerly`, `L_Earth`, `L_Hangar`.
- `TerrainData/` reorganized: `DEM/ Imagery/ Meshes/ Processed/ Scripts/`.
- `C:\DevinTools` — 350 stale test/fix/check scripts + logs moved to
  `_archive/`. Active tooling kept: `shot.py`, `world_key.py`,
  `click_client.py`, `ground_clean.py`, `strip_westerly.py`, road/tree/geo
  pipeline scripts.

### Tomorrow's KWST build-out (agreed direction)
1. Extend ortho west to cover KWST (verify NAIP quad coverage, pull more).
2. OSM roads/runway/taxiway for the airport bbox (roads_geom.py pipeline).
3. OSM building footprints → extruded hangars at real positions.
4. Props: parked planes at tie-downs, windsock, signage (patch plane smears
   out of the ortho first).
5. Trees for the airport area need a real foliage source — PolyHaven game
   exports are decimated/leafless (Session 26 root cause).
6. Spawn-in-hangar flow: player starts inside a hangar, paramotor staged.
7. Google Photorealistic 3D Tiles = the "fly anywhere" layer — needs the
   user's Google Cloud API key (Map Tiles API); melted at ground level,
   cities-only coverage — NOT a replacement for curated fields.

## Session 32 — Live METAR Weather Tab, Codex Equip Pose, Spawn Cleanup

### Weather tab (UUW_FreeFlightMenu, tab 2)
- Rebuilt into a live-weather panel: **LIVE/CUSTOM** toggle + **REFRESH**,
  station + flight-category readout, raw METAR, parsed obs (wind dir/spd/gust,
  temp, dew point, humidity, altimeter inHg, visibility, cloud layers, obs Z),
  and a CONDITIONS ANALYSIS block (Pasquill class, cloudbase, mixing layer,
  thermal climb rate, flyability verdict from `AtmosphereModel.h`).
- Live source: `aviationweather.gov/api/data/metar?format=json` — ICAO lookup
  (`ids=Kxxx`, 3-letter gets a `K` prefix) when an airport pin is selected,
  `bbox=` around focused coords otherwise. No API key needed.
- METAR JSON fields: `temp/dewp/wdir/wspd/wgst` (knots), `altim` (hPa -> inHg
  x0.02953), `visib` (number or "10+"), `fltCat`, `rawOb`, `clouds[].cover/base`,
  **`obsTime` is a Unix timestamp, not ISO** — `ParseIso8601` will fail on it.
- New GI fields: `WindDirection`, `DewPoint`, `Altimeter_inHg`,
  `Visibility_mi`, `bLiveWeather`, `LiveStation/MetarRaw/ObsTime/FlightCat/
  CloudLayers/Status/FetchUTC`. Manual edit of temp/humidity re-derives
  `DewPoint` via `ComputeDewPoint`.
- Live mode locks all 7 sliders; unchecking LIVE unlocks custom editing.
  Refetches when an airport pin is clicked while live is on.
- Dev hook: launch with `-weathertab` to open the menu straight onto the
  weather tab with live mode on (verification shortcut).
- Wing gets the wind heading at spawn: `SpawnWingActor` sets
  `ChargerWingActor->WindHeadingDegrees = GI->WindDirection`.
- Build.cs deps added: `HTTP`, `Json`, `JsonUtilities`.

### Codex paramotor equip pose + spawn (verified visually)
- **The Codex part Blueprints bake a -90 yaw into their roots** — "frame space"
  is rotated 90 vs the paramotor actor's axes. Prop aft is at actor +Y, not -X.
  Equip rotation is `FRotator(0,90,0)` + loc `(-33, 30, 55)` on the mesh root;
  ground spawn gets `SpawnRot + -90 yaw`. Derive the sign empirically, do not
  trust frame-space coordinates.
- Equipped rig no longer camera-locked: `Tick` adds walk-cycle bob (2.5 cm Z),
  lateral sway, and roll scaled by the character's ground speed — the pack
  visibly moves with the body.
- Procedural rigging meshes (AArm/Ring/Carabiner/BrakeToggle/QuickLink) are
  **hidden in codex mode** — they were authored against the procedural frame
  and rendered as floating debris. Pivots remain as pure physics anchors; the
  procedural line mesh still renders wing lines.
- `AParamotorActor::Destroyed()` kills tracked Codex part actors; respawning
  destroys the prior `SpawnedParamotorActor` first. No orphan parts.

### Verification gotcha (UI automation)
- The game window is DPI-virtualized (120 dpi). `win32gui` reports physical
  px but the cursor that Slate sees lands with a large, non-linear offset —
  scripted pyautogui clicks are unreliable. `PrintWindow` capture includes the
  title bar (image y = client y + ~31). Prefer `-weathertab`-style command-line
  hooks over click automation.
- `WorldMapGameMode` HANGAR tab calls `OpenLevel(L_Hangar)` — a click there
  unloads the whole FreeFlight menu widget.


### Main menu (UW_MainMenu)
- Full-bleed photo + fake gradient scrim: ~80 thin alpha bands (10px each,
  quadratic falloff, max 0.82) — smooth photo->dark falloff, no visible
  banding. Bottom scrim = 14 bands for footer legibility.
- Numbered entries (01 FREE FLIGHT / 02 HANGAR / 03 CAREER-disabled /
  04 QUIT) with subtitles; hover drives a right-side detail panel via the
  new CatalogItemWidget OnItemHovered delegate (broadcasts index; unhover
  sends -1).
- Detail panel: kicker/title/description/feature tags per entry.
- Launch-site card bottom-left (name/ICAO/temp/wind/local time), hairline
  footer, ~0.5s fade-in via NativeTick + SetRenderOpacity.

### Empty harness — seat fold (ParamotorBuilder)
- Dudek_PowerSeatDP_Seat is its own component: empty mode now rotates the
  seat board ~75 deg up against the backrest, hinged at its front edge
  (~(18,0,9) harness-local): T' = Hinge * Rot(+75pitch) * Hinge^-1 * T_orig.
- SyncBuilderVisibility pushes the menu's bPilotSeated into the builder
  every pass — ordering can't drift.
- Flags: -emptypilot (empty harness), -wings (auto-open wing catalog).

### Wing is its own screen now (UW_WingSelect)
- Paramotor build = frame/engine/prop/harness only (rail + sheet are 4
  sections; wing never on podium).
- SAVE BUILD (relabeled "SAVE BUILD - CHOOSE WING") saves then opens
  UUW_WingSelect as an AddToViewport(100) overlay — full-screen light theme
  (near-white bg, ink text, cyan accent), Forza-style brand tabs across the
  top, wing cards with planform art, right-side spec sheet (areas, spans,
  AR, cells, weight, TOW, trim/top speed, risers, colors), notes caption.
  BACK closes the overlay; FLY saves WingIndex + opens LV_World.
- New UIndexedButton widget — zero-arg UButton::OnClicked wrapped with an
  index + content setter (used for cards/tabs).
- FParamotorWingSpec extended: Cells, FlatArea, ProjectedArea, FlatSpan,
  ProjectedSpan, AspectRatioFlat/Projected, WingWeight_kg, TowRange,
  Certification, Risers, Colors, ImagePath.
- Wing planform art generated by C:/DevinTools/gen_wing_planform.py
  (PIL: elliptical LE arc, chord taper, cell seams, accent LE band + tips),
  imported via import_wing_tex.py to /Game/Textures/Wings/T_Wing_Charger25.

### GOTCHA — widgets created during NativeOnInitialized get wiped
- The viewport runs InvalidateAllWidgets ~0.6s after map load — any widget
  AddToViewport'd inside NativeOnInitialized is silently destroyed. Defer
  overlay creation with a 1s timer (see -wings flag path).
- UImage inside a VerticalBox/SizeBox renders at texture desired size —
  SetDesiredSizeOverride + direct canvas placement works; nested sizing
  doesn't reliably.

# ParamotorSim Project Notes

## Session 31 — Main Menu Redesign, Seat Fold, Wing Catalog Screen

## Session 29 — Podium Drag, Unequip, Painted Turntable, Pilot Mannequin

### Overview
Hangar polish pass: LMB-drag spins the podium, every equipment section can be
unequipped (empty podium state), the podium got a painted two-tone finish with
an accent rim ring, and a procedural pilot mannequin sits in the harness in
seated mode.

### Podium drag (HangarPawn + HangarGameMode)
- Input mode switched `UIOnly` → `GameAndUI` in `CreateMenu()` so LMB drags
  over the preview reach the pawn while buttons still capture their clicks.
- `AHangarPawn` binds `LeftMouseButton` pressed/released (`bDragging`) — while
  held, `Turn()` applies yaw to the podium actor (0.4 deg/px), `LookUp()`
  tilts camera pitch. The `URotatingMovementComponent` turntable pauses via
  `SetTurntablePaused` during drag + 1.5s grace (`TurntableHold` in Tick).
- The podium cylinder now uses `M_HangarTop` for the body, plus a thin
  `M_HangarAccent` disc at local Z=2.2 — the painted cyan rim ring.

### Unequip (UW_HangarMenu)
- Catalog bottom bar has a `REMOVE` ghost button (left of EQUIP) — sets the
  section index to -1 and closes the catalog.
- `LoadSavedBuild()` returns bool; `ApplyDefaultBuild()` only runs when no
  save exists, so deliberately-unequipped (-1) indices persist.
- `SyncBuilderVisibility` gates ALL parts on frame-equipped — no frame means
  no paramotor on the podium.
- `-emptypodium` launch flag forces all indices -1 — verified the bare podium
  state (build sheet shows "— NOT EQUIPPED" everywhere, center reads
  "SELECT A FRAME TO BEGIN"). `-fullbuild` equips index 0 of every section
  for a complete-assembly screenshot without a save.
- The hangar opens EMPTY by default — `ApplyDefaultBuild` was removed;
  `FParamotorBuild` indices default to -1, saved builds still restore.
- Pilot mannequin uses `M_HangarTop` (light showroom grey) and anchors to
  the harness's `SOCKET_PilotPelvis` — pelvis-relative transforms.

### Pilot mannequin (ParamotorBuilder)
- 8-part primitive mannequin (cylinder torso/limbs, sphere head) attached to
  `HarnessSlot`; `SetPilotVisible(bool)` toggles pilot vs. loose-strap
  visibility (`HarnessStrapComps` hidden when seated). Hangar bottom bar has
  `PILOT · SEATED/EMPTY` toggle.
- **UE cylinder primitive is ~2m tall / ~1m dia at scale 1** — part scales
  are halved accordingly. `/Engine/BasicShapes/Capsule` does NOT exist —
  use Cylinder.
- The harness BP has an authored `SOCKET_PilotPelvis` at local (-4.5,0,10.5);
  PilotParts anchor to it (pelvis-relative transforms), falling back to
  HarnessSlot frame-space coords if absent.
- Component audit (C:\DevinTools\audit_parts.py): `L_Hangar` has ZERO
  pre-placed actors, and the frame/engine/prop/harness BPs contain only real
  meshes + SceneComponent helpers — no stray/legacy geometry anywhere.
- Visibility dependency chain: frame required for everything; prop requires
  engine (`SyncBuilderVisibility`). Equipping calls `Builder->SetPart` so
  model swaps actually re-spawn. Re-equipping the engine re-attaches the
  prop to the new SOCKET_Propeller and re-dedupes the PropHub flange;
  `SetPartVisible(Prop,false)` restores the bare flange.
- Wing preview additionally gated on `bCatalogOpen` — it no longer lingers
  on the podium after closing the wing catalog.
- Harness dangling gear hidden in seated mode: Straps, HangLoops, Hardware
  (carabiners), Pulleys — they float at the hang-points with no risers.
  Empty mode shows them.

### Testing gotchas
- Synthetic Slate clicks are unreliable: pyautogui/PostMessage clicks don't
  activate UMG buttons in `-game` windowed mode (UE reads the real cursor).
  Verify button paths with the `-emptypodium` flag or by hand.
- `pygetwindow` left/top is the WINDOW corner — use
  `win32gui.ClientToScreen(hwnd, (0,0))` for the client origin, and the
  window resizes to the requested res during startup (1600→1280 client).
- Reliable screenshot: `PrintWindow(hwnd, dc, 2)` on the game hwnd.
- To foreground the game for input tests: `AttachThreadInput` to the current
  foreground thread, then `SetForegroundWindow`.

### Files changed
- `HangarGameMode.cpp/.h` — GameAndUI input, turntable ref/pause, podium paint
- `HangarPawn.cpp/.h` — drag state, podium rotation, camera pitch on drag
- `UW_HangarMenu.cpp/.h` — REMOVE button, OnUnequipClicked, save/default
  ordering, frame-gated visibility, -emptypodium flag
- `ParamotorBuilder.cpp` — mannequin scale/position fix (halved, tucked)

## Session 28 — Hangar Real-Part Preview, Studio Lighting, World Zoom

### Overview
Replaced the generic procedural paramotor in the hangar preview with the real
Codex asset pack (Amaruk 140 frame, EOS 100 engine, PT130 prop, PowerSeat DP
harness), rebuilt the environment as a studio rig, slowed world-map zoom, and
restyled the interact prompt. End state: Forza-style dark showroom, grey
concrete slab floor, specular-damped lights, turntable podium.

### Hangar assembly (ParamotorBuilder)
- `SetPart` spawns the real part Blueprints into per-part SceneComponent slots;
  `PartActors[EHangarPart]` tracks them for cleanup/visibility.
- All parts attach in frame space (frame BP sits at `FrameSlot` identity).
- Prop attaches live to the engine's `SOCKET_Propeller` helper component when
  present; fallback transform lands on the same point.
- Harness uses the Codex-authored fixture transform `(0.3345, 0, 0.352)` m —
  seat forward of the cage, backrest toward the engine.
- `HideHelperVisuals()` hides renderable helper/marker/billboard components
  (`SOCKET_*`, `AXIS_*`, `MOUNT_*`, *Helper/Marker/Gizmo/Debug/Icon*).
- Wing stays a static mesh, only shown while browsing the wing catalog
  (`SyncBuilderVisibility` gates it on `SelectedSection == 4`).

### Studio lighting (HangarGameMode::SpawnEnvironment)
- File-static helpers: `SpawnDecorMesh`, `AttachDecorMesh` (podium children),
  `SpawnRectLight` (softbox, `SpecularScale 0.2`), `SpawnPointLight`.
- Floor = flat cube slab at Z=-28 (top ~2 cm under podium base), podium
  material — reads as a concrete pad. Bright PBR materials (M_Concrete,
  M_RoadWorn, BasicShapeMaterial) all blow out under this rig — the podium
  material is the only one that lands mid-grey.
- Walls do NOT cast shadows (`bCastShadow=false`) — they blacken the slab.
- Lights: key/fill/rim softboxes, floor-wash panel, spot pool, point spot +
  low bounce, directional sun (`SpecularScale 0.3`), skylight 1.4 real-time.
- Metallic parts mirror the environment — a bright floor turns the whole rig
  into white chrome. Dark slab + damped speculars = readable metal.

### GOTCHA (re-learned the hard way)
- Session 27 documented this and it bit again during refactor: spawning an
  `AActor` and then creating+registering its root component does NOT apply
  the spawn transform — everything lands at `(0,0,0)`. `SetActorLocationAndRotation`
  at the end of the spawn helper is required, not redundant.

### World map (WorldMapPawn)
- `ZoomFraction 0.08`, `FInterpTo 1.8` — slower zoom.
- `ZoomMin 642.1M cm` (~50 km alt) — below that Cesium imagery blurs/voids.
- Default pitch -89.5° (near straight down on the RI origin); clamp -89..-25.

### Quality baseline
- `UParamotorGameInstance::Init()` — global 125% supersample + maxed sg.*.
- Hangar overrides to 150% (`ApplyStudioQuality`); world map uses ~120%.

### Files changed
- `ParamotorBuilder.cpp/.h` — real BP part spawning, PartActors, helpers
- `HangarGameMode.cpp/.h` — SpawnEnvironment studio rig, ApplyStudioQuality
- `WorldMapPawn.cpp/.h` — zoom speed/min/tilt defaults
- `ParamotorGameInstance.h` — Init() global render quality
- `UW_InteractPrompt.cpp` — UIStyle chip + keycap prompt
- `UW_HangarMenu.cpp`, `UW_FreeFlightMenu.cpp` — spec/copy updates

## Session 27 — Hangar Empty Podium Presentation

### Overview
Finished the temporary Hangar presentation state while the real frame asset is unavailable. The Hangar now opens on a clean dark UI with `FRAME` in the left equipment rail, a top-right preview panel, a build sheet, and an empty rotating podium. No engine/prop/harness/wing meshes appear until a valid frame catalog entry exists.

### Runtime studio gotcha
- `AHangarGameMode::SpawnEnvironment()` spawns an actor and then creates/registers a new root component. Creating the root after `SpawnActor` reset actor transforms to origin, which stacked the floor, walls, and podium at `(0,0,0)` and made the preview black or fully occluded.
- Always call `Actor->SetActorLocationAndRotation(Location, Rotation)` again after replacing/registering a spawned actor's root component.
- The podium uses `/Engine/BasicShapes/Cylinder.Cylinder`; assign materials to all material slots, not only slot 0.
- Current podium: base at `Z=-12`, scale `(1.5,1.5,0.24)`; top disc at local `Z=2.5`, scale `(1.35,1.35,0.05)`; cyan index marker at local `(48,0,6)`, scale `(0.75,0.10,0.04)`; `URotatingMovementComponent` yaw `12 deg/s`.
- The Hangar uses generated unlit materials (`M_HangarStudio`, `M_HangarPodium`, `M_HangarTop`, `M_HangarAccent`, `M_HangarPreview`) plus an unbound `APostProcessVolume` with `AEM_Basic` min/max exposure `1.0` to avoid auto-exposure blowing out or blacking out the preview.

### UI
- `UW_HangarMenu.cpp` uses a viewport fallback of `1280x720`; preview starts at `36%` screen width and runs to `right-24px`.
- Clicking an equipment category opens `BuildCatalogOverlay()`, a full-screen Forza-style catalog page with brand tabs, model cards, a right-side specification panel, back/equip actions, and a top-right close control.
- Catalog layout uses normalized anchors (`0.03`/`0.64`/`0.98` horizontally and `0.27`/`0.82` vertically). Do not rely on `GameViewport->GetViewportSize()` for full-screen overlay placement; the reported size can differ from the final client size while the game window settles.
- `SyncBuilderVisibility()` requires `FrameSpecs` to contain a valid selected frame before any part is shown. This prevents partial equipment appearing on the podium.
- Detailed per-category spec text lives in `BuildSpecText()`.

### Files changed
- `Source/ParamotorSim/Private/HangarGameMode.cpp`
- `Source/ParamotorSim/Private/HangarPawn.cpp`
- `Source/ParamotorSim/Private/UW_HangarMenu.cpp`
- `Source/ParamotorSim/Private/ParamotorBuilder.cpp`
- `C:\DevinTools\_make_hangar_materials.py`

### Verification
- `ParamotorSimEditor Win64 Development` build succeeded.
- `Saved\Screenshots\Hangar3D.png` shows the clean empty podium.
- Two screenshots taken 3 seconds apart showed the cyan marker moving around the podium, confirming rotation.

## Session 26 — Ninigret Real Terrain: Spawn, Roads, Ground Material

### Overview
Focused on Ninigret Park: real spawn location, real road network from OpenStreetMap, real grass/aerial ground material, real scanned tree assets. All on the real LiDAR-derived landscape in `L_Westerly`.

### Critical bug: wrong default map (user-facing)
- **`GameDefaultMap` was `/Game/Maps/L_OpenWorld`** — the game never loaded the real-terrain map on normal launch, so none of the Ninigret work was visible. Changed to `/Game/Maps/L_Westerly` in `Config/DefaultEngine.ini`.
- **`EditorStartupMap`** was the empty OpenWorld template — changed to `L_Westerly` so opening the editor shows the real map.
- **`UW_MainMenu::OnPlayClicked()`** opened `L_OpenWorld` — changed to `L_Westerly`.
- This is why the user "wasn't spawning at Ninigret at all" — everything was being built on a map the game never loaded.

### Spawn at Ninigret (auxiliary airstrip parking)
- `AParamotorGameMode::GetLaunchFieldSpawnLocation()` line-traces the landscape for ground Z.
- Spawn moved to airstrip-side parking: world X=957000, Y=378500 cm (UTM19N ~E279150/N4583800), confirmed standing at ~19 ft alt, walking mode.
- Underground-spawn fix: old procedural tree ISMs had blocking collision that teleported the pawn ~14km under the map; tree collision disabled, plus post-spawn verification/teleport in `RestartPlayer()`.

### Real road network (OpenStreetMap → FBX → ISM)
- Queried Overpass API for all `highway=*`, `aeroway=runway/taxiway`, `amenity=parking`, `area:highway` ways in the Ninigret bbox (41.355–41.405, -71.715 to -71.635): **767 ways**.
- `roads_geom.py` (system Python, rasterio+pyproj): lat/lon → UTM19N → world cm, ~15m subdivision, per-node Z from `westerly_dem_2p5m.tif`, +12cm offset → `roads_geom.json` (624 buildable strips).
- `build_roads_mesh.py` (Blender headless): ribbon strips with widths by road class (trunk 11m → service 5m), parking lots as filled polygons, world-XY tiling UVs → `roads.fbx` (21.8k verts, 10.5k tris, 2 material slots).
- **Axis fix:** UE FBX import negates Blender Y — baked `-Y` in the source verts + reversed winding.
- Placed as single-instance ISM actor `RoadsActor` via `UParamotorEditorUtils::ScatterStaticMeshInstances` (the C++ path persists correctly; Python `spawn_actor_from_class` did not).
- Materials: `M_RoadAsphalt` (asphalt_04 PBR) + `M_RoadWorn` (worn_asphalt PBR) from PolyHaven CC0, in `/Game/Textures/roads/`.

### Ground material — proper technique (aerial macro + tiled detail)
- Old approach (big translucent decal over the landscape) produced a washed-out pale blur; removed `NinigretOrthoDecal` from the map.
- New `M_GroundFix` (`/Game/Materials`): `WorldPosition` → tiled grass texture (`aerial_grass_rock`, 3m tile, real normal map) × NAIP ortho (`T_NinigretOrtho`, world-mapped over E276076–281076 / N4581937–4586937, V flipped for texture space) × brightness factor 2.8.
- **Lesson:** a failed material compile gets cached — a broken `M_Ground` kept failing after fixes; rebuilding under a new asset name (`M_GroundFix`) bypassed the stale cache. Simpler node graphs only (Multiply/Subtract/Constant2Vector) — exotic nodes (OneMinus/AppendVector path) had been part of the failing build.
- Result: checkerboard gone, real grass field with aerial color modulation. Verify the parking-lot pavement reads correctly under the spawn.

### Real trees (partial — leaves still missing)
- Downloaded 5 PolyHaven CC0 scanned species (fir, pine, jacaranda, island trees), imported via native Interchange glTF into `/Game/Meshes/gltf_test/` (Blender FBX round-trip had dropped foliage).
- Scattered **10,733 instances** across 5 ISM groups at NDVI-derived forest positions (real tree locations from NAIP near-infrared).
- **Root cause of leafless trees found:** the game-ready PolyHaven exports are heavily decimated — `DumpMeshSections` (new C++ helper) shows the fir "twig" section has ~10 triangles, pine has 0. The leaf geometry simply isn't in these assets. Needs a real foliage pack (Fab/Megascans/Brushify) — deferred.
- Materials: leaf MICs reparented to `MI_Default_Mask_DS`; also built standalone masked leaf materials (`M_PH_*Leaf`) on the combined diff+alpha textures — all moot until leaf geometry exists.
- **Material usage flags matter:** ISM-rendered sections show "missing usage flag InstancedStaticMeshes → Default Material" warnings; Nanite-enabled meshes need the Nanite usage flag too. Set `used_with_instanced_static_meshes` on materials used by scattered meshes.

### Tooling notes
- **Save failures are silent file locks:** `save_current_level` in a commandlet fails if a game/editor instance holds `L_Westerly.umap` — log shows `MoveFile was unable to move`. Always `taskkill` UnrealEditor processes before commandlet saves; verify with `list_actors.py` afterwards.
- `spawn_actor_from_class` needs `SomeClass.static_class()` and `actor.static_mesh_component` (not `get_static_mesh_component()`).
- Blender's bundled Python has no rasterio/pyproj — split geo work (system Python → JSON) from mesh building (Blender → FBX).
- Screenshotting: `PrintWindow(hwnd, mdc, 2)` works on the game window; avoid stealing focus (the game auto-pauses and shows the pause menu).
- New C++ helper: `UParamotorEditorUtils::DumpMeshSections(MeshAssetPath)` — logs per-section triangle counts + materials from `GetRenderData()->LODResources[0].Sections`.
- Working scripts live in `C:\DevinTools\` (`roads_geom.py`, `build_roads_mesh.py`, `reimport_roads.py`, `place_roads.py`, `ground_mat3.py`, `check_roadmesh.py`, `list_actors.py`).

### Files changed
- `Config/DefaultEngine.ini` — GameDefaultMap + EditorStartupMap → L_Westerly
- `Source/ParamotorSim/Private/UW_MainMenu.cpp` — Play opens L_Westerly
- `Source/ParamotorSim/Public/ParamotorEditorUtils.h` + `Private/ParamotorEditorUtils.cpp` — added `DumpMeshSections`
- `Source/ParamotorSim/Private/ParamotorGameMode.cpp` — Ninigret airstrip-parking spawn (X=957000, Y=378500), post-spawn position verification
- Content: `Meshes/roads`, `Materials/M_RoadAsphalt`, `M_RoadWorn`, `M_GroundFix`, `M_PH_*Leaf`, `Textures/roads/*`, `Textures/ground/*`, `Meshes/gltf_test/*` (5 species), map `L_Westerly` (RoadsActor, ISM_Tree_0-4, decal removed)

### Backup
- `C:\Users\sdean\Desktop\ParamotorSim_2026-09-08_22-18.zip` — 8.5 GB project archive (excl. Binaries/Intermediate/Saved/.git)

### Next
- Real foliage pack (needs Epic sign-in for Fab/Megascans) → re-scatter onto real tree positions (pipeline done, just swap meshes)
- Verify parking-lot pavement reads correctly at spawn; tune ortho brightness factor if aerial over/under-modulates
- Real 3D grass (Megascans clusters or grass-type instancing on the NDVI mask)
- Remove per-tick debug logging once spawn confirmed stable

## Session 25 — Procedural World Environment Setup

### Overview
Added procedural sky, lighting, fog, post-processing, and ocean water to `AParamotorGameMode`. All environment actors are spawned at runtime in `BeginPlay()` — no manual editor placement required. Works in `L_OpenWorld`.

### Environment actors spawned by GameMode
1. **ASkyAtmosphere** — Earth-like atmosphere (BottomRadius=6361km, AtmosphereHeight=100km, Rayleigh/Mie scattering scales)
2. **ADirectionalLight** (sun) — 75000 lux, 4 cascade shadows, atmosphere sun light enabled, dynamic time-of-day positioning
3. **ASkyLight** — Real-time sky capture, lower hemisphere tint
4. **AExponentialHeightFog** — Volumetric fog enabled, directional inscattering, fall atmosphere colors
5. **APostProcessVolume** — Unbound, fall color grading (warm tones), auto exposure, bloom, lens flare, vignette, motion blur, SSR
6. **AStaticMeshActor** (ocean) — 20km x 20km water plane at Z=0, south of spawn, dark blue fallback material
7. **AStaticMeshActor** (under-map plane) — 200x200 scale at Z=-2000, hides default concrete from below

### Time-of-day sun positioning
- `UpdateSunPosition(float TimeH)` called from `Tick()` using `GameInstance->TimeOfDay`
- Sun elevation modeled for 41.3°N fall equinox (max 45° elevation)
- Sunrise 6:00, sunset 18:00, below horizon at night
- Sun intensity ramps from 2000 lux (horizon) to 75000 lux (noon)
- Light color shifts warm at low elevation (sunset/sunrise orange)
- Sky light intensity tracks sun elevation
- Fog color interpolates: day (blue-grey) → sunset (orange) → night (dark blue)

### UE 5.8 API fixes applied
- `#include "Components/SkyAtmosphereComponent.h"` (not `Engine/SkyAtmosphere.h`)
- `#include "Engine/PostProcessVolume.h"` (not `PostProcess/PostProcessSettings.h` — `FPostProcessSettings` is in `Engine/Scene.h` via `PostProcessVolume.h`)
- `USkyAtmosphereComponent* SkyComp = SkyAtmosphereActor->GetComponent()` (properties on component, not actor)
- `Cast<UDirectionalLightComponent>(SunLight->GetLightComponent())` (GetLightComponent returns base `ULightComponent*`)
- `bAtmosphereSunLight` (not `bUsedAsAtmosphereSunLight` which is deprecated)
- `FogInscatteringLuminance` (not `FogInscatteringColor` which is deprecated)
- `DirectionalInscatteringLuminance` (not `DirectionalInscatteringColor` which is deprecated)
- `bEnableVolumetricFog` (not `SetVolumetricFogEnable()`)
- `VolumetricFogAlbedo` is `FColor` (not `FLinearColor`)
- `AutoExposureMinBrightness`/`AutoExposureMaxBrightness` (not `AutoExposureMinEV100`/`AutoExposureMaxEV100`)

### Missing assets (pre-existing, not caused by environment work)
- `M_Water` material — ocean plane falls back to dark blue dynamic material
- `M_GrassReal` material — referenced by landscape, missing
- Fab grass materials (`025_Grass?Material...`) — referenced by external actors, missing
- `T_LeafyGrass_D/N/R` textures — referenced by `M_Landscape`, missing

### Files changed
- `Source/ParamotorSim/Public/ParamotorGameMode.h` — environment actor members, SetupSkyAndLighting/UpdateSunPosition/SetupPostProcessing/SetupWater/SetupCoastlineDirection declarations
- `Source/ParamotorSim/Private/ParamotorGameMode.cpp` — full procedural environment implementation

### Build status
- Build succeeded (UE 5.8, MSVC 14.44, Win64 Development, 4 actions, 39.45 seconds)
- Game launched successfully, all environment actors spawned (confirmed in logs)
- Sky atmosphere, sun light, sky light, fog, post-process, ocean water all confirmed spawned

## Session 24 — Real Atmospheric Wind & Thermal System

### New file: AtmosphereModel.h
A header-only library of real atmospheric physics formulas, all inline:
- **Pasquill stability classification** (A-F) from solar insolation, wind speed, cloud cover, time of day
- **Log-law wind profile** with Monin-Obukhov stability correction (Businger-Dyer-Paulson ψm)
- **Ekman spiral** direction veering with altitude (Coriolis parameter for 41.3°N Westerly, RI)
- **MIL-HDBK-1797B Dryden turbulence** scale lengths (altitude-dependent: low/high altitude regimes)
- **Turbulence intensity** from friction velocity u*, stability class, and gust amplitude
- **Dew point** (Magnus formula over water) and **cloudbase** (LCL = 125 × (T - Td))
- **Thermal physics**: trigger temperature, climb rate (0.5-10 m/s), spacing (2.5× BL height), radius (15-250m from strength), lifetime (3-10 min), compensating subsidence
- **Overdevelopment risk** detection (afternoon + high temp + high humidity)
- **Sea breeze** (coastal: 3-8 m/s onshore during day, 1-3 m/s land breeze at night)
- **Inversion layer** altitude (caps thermals)
- **Gust factor** (WMO 3-s gust = mean + 3.5σ)
- **Wind noise** amplitude (SPL ∝ U^2.5 for audio scaling)
- **Flyability assessment** (Excellent/Good/Challenging/Dangerous/Unflyable)
- **Boundary layer height** (diurnal cycle: 200m night → 3000m unstable midday)

### GameInstance: Humidity added
- New `Humidity` field (0-100%) on `UParamotorGameInstance`
- Main menu slider added for Humidity
- Used for dew point → cloudbase calculation and thermal strength

### WingActor: Complete wind system rewrite

**UpdateDynamicWind** — now implements real atmospheric physics:
1. Pasquill stability class computed from weather + time of day
2. Boundary layer height from stability + diurnal cycle
3. Dew point from temperature + humidity (Magnus formula)
4. Cloudbase from dew point (125 × (T - Td))
5. Inversion altitude from stability + time of day
6. Monin-Obukhov length from stability class
7. Friction velocity u* from log law at 10m reference
8. Wind speed from log-law profile with stability correction (not just power law)
9. Above-boundary-layer wind increase (geostrophic)
10. Ekman spiral direction veering (20° total through BL)
11. Sea breeze contribution (coastal, daytime onshore)
12. MIL-HDBK-1797B Dryden turbulence with proper altitude-dependent scale lengths
13. 2nd-order lateral/vertical filters (per MIL-HDBK-1797B, not just 1st-order)
14. Stability-dependent turbulence intensity (σu/u* = 2.4, multiplied by stability factor)
15. Mechanical turbulence boost near ground (within 50m)
16. Overdevelopment risk detection
17. Flyability assessment
18. Direction drift rate depends on stability (faster in unstable conditions)

**Thermal system rewrite:**
- Thermal trigger: only spawns when surface temp > 8°C AND sufficient solar insolation
- Thermal climb rate: computed from temp, BL height, cloud cover, time of day, humidity (0-10 m/s)
- Thermal spacing: 2.5× BL height (real atmospheric scaling, not fixed 500m)
- Thermal radius: inversely proportional to strength (strong=narrow 15-30m, weak=wide 60-250m)
- Thermal lifetime: 3-10 minutes (stronger = longer)
- Thermal top: minimum of cloudbase and inversion altitude (blue thermals stop at inversion)
- Thermal lifecycle: bubble (ramp up 10%) → column (full strength 80%) → decay (ramp down 10%)
- Thermal drift: 80% of wind speed (advected by mean flow)
- Cloud street alignment: thermals bias along wind direction when wind > 5 m/s
- Compensating subsidence (sink between thermals): proportional to thermal strength, peaks at mid-BL
- Overdevelopment risk: afternoon + high temp + high humidity → dangerous conditions

**ComputeAeroState fix:**
- Now receives real PawnLoc from UpdateFlight (was using GetAttachParentActor() which returned nullptr)
- Altitude computation is now correct for all wind profile calculations

**New accessors (public):**
- `GetFlyability()` — returns Excellent/Good/Challenging/Dangerous/Unflyable
- `IsOverdevelopmentRisk()` — true when OD conditions present
- `GetBoundaryLayerHeight()` — current BL height in m
- `GetCloudbase()` — cloudbase altitude in m
- `GetDewPoint()` — dew point in °C
- `GetThermalSinkAtPosition()` — subsidence m/s between thermals

### WingFlightPhysics: Thermal sink integration
- `IntegrateForces` now applies both thermal updraft AND compensating subsidence
- Sink coupling (0.3) is gentler than updraft coupling (0.5)
- Sink only applies when NOT in a thermal (lift < 0.5 m/s) and within BL

### Cloth simulation: Vertical turbulence
- Cloth now responds to vertical turbulence component (TurbVertState)
- Adds realistic flutter in gusts

### Wind audio: Real apparent wind with U^2.5 scaling
- Uses wing's live wind vector (includes gusts, turbulence, gradient) from `GetWindVector()`
- Apparent wind = true wind - vehicle velocity (vector difference)
- Volume scales as U^2.5 (real wind noise SPL ∝ U^5, amplitude ∝ U^2.5)
- Pitch shifts with wind speed (higher wind = higher frequency content)

### HUD: Atmospheric conditions display
- New "Conditions" text: EXCELLENT/GOOD/CHALLENGING/DANGEROUS/UNFLYABLE + OD RISK
- New "Cloudbase" text: cloudbase altitude in feet
- Color-coded: green (good) → yellow (challenging) → red (dangerous/OD)

### Files changed
- `Source/ParamotorSim/Public/AtmosphereModel.h` — NEW: 693 lines of real atmospheric physics
- `Source/ParamotorSim/Public/ParamotorGameInstance.h` — added Humidity field
- `Source/ParamotorSim/Public/UW_MainMenu.h` — added Humidity slider/callbacks
- `Source/ParamotorSim/Private/UW_MainMenu.cpp` — added Humidity slider UI
- `Source/ParamotorSim/Public/WingActor.h` — new atmospheric state members, public accessors, FThermal fields
- `Source/ParamotorSim/Private/WingActor.cpp` — complete rewrite of UpdateDynamicWind + thermal system
- `Source/ParamotorSim/Private/WingFlightPhysics.cpp` — ComputeAeroState PawnLoc fix, thermal sink integration
- `Source/ParamotorSim/Private/WingTestSim.cpp` — vertical turbulence on cloth
- `Source/ParamotorSim/Private/ParamotorCharacter.cpp` — real apparent wind audio with U^2.5 scaling
- `Source/ParamotorSim/Public/UW_FlightHUD.h` — ConditionsText, CloudbaseText members
- `Source/ParamotorSim/Private/UW_FlightHUD.cpp` — conditions/cloudbase display, AtmosphereModel include

### Build status
- Build succeeded (UE 5.8, MSVC 14.44, Win64 Development)
- 12 actions, 82.46 seconds

### Real physics implemented (no fake numbers)
- Von Karman constant κ = 0.41
- Coriolis parameter f = 9.58×10⁻⁵ s⁻¹ (41.3°N)
- Roughness length z0 = 0.03m (coastal grass)
- Dry adiabatic lapse rate = 0.0098°C/m
- Magnus dew point formula (over water)
- Cloudbase = 125 × (T - Td) m AGL
- MIL-HDBK-1797B Dryden scale lengths (altitude-dependent)
- Businger-Dyer-Paulson Monin-Obukhov stability corrections
- Pasquill-Turner stability classification
- Ekman spiral (20° veering through BL)
- Sea breeze (3-8 m/s, 100-500m depth)
- Thermal climb rates (0.5-10 m/s from literature)
- Thermal spacing (2.5× BL height)
- Compensating subsidence (mass conservation)
- Gust factor (peak = mean + 3.5σ)
- Wind noise SPL ∝ U^5 (amplitude ∝ U^2.5)

## Session 23 — Wind System Overhaul

### Unified wind access (single source of truth)
- Added `GetShiftedWindDirection()` — wind direction with turbulence shift applied (normalized, world space)
- Added `GetWindVector()` — full wind vector in cm/s including turbulence shift + gusts
- Added `GetWindHeadingDeg()` — wind compass heading in degrees (0=N=+X, 90=E=+Y) with turbulence shift
- All consumers now use these instead of duplicating `WindDirection.RotateAngleAxis(WindDirShift * RAD_TO_DEG, ...) * CurrentWindSpeed * CM_PER_M`
- Replaced duplicated wind vector computation in: `ComputeAeroState`, `CheckTakeoff`, `GetTakeoffChance`, `UpdateConnectedState`, `SimulateTestWingCloth`, HUD `UpdateWindInfo`, HUD `UpdateFlightStats`, HUD instruction text

### Wind direction shift instability fix
- `WindDirShift = TurbLatState / Max(1.0f, BaseWind)` could produce 115°+ swings at low wind (1 m/s base + 2 m/s gust)
- Fixed: `Clamp(TurbLatState / Max(2.0f, BaseWind), -0.35f, 0.35f)` — ±20° max, higher floor
- File: `WingActor.cpp` `UpdateDynamicWind()`

### Wind gradient fix
- Was only applied above 2m altitude; below 2m used full base wind (contradicting the "near-zero at ground" comment)
- Now applied at all altitudes with a 0.5m floor (1/7 power law naturally reduces wind at low altitudes)
- Turbulence is added on top without gradient scaling (turbulence is a local eddy effect, not a mean profile effect)
- File: `WingActor.cpp` `UpdateDynamicWind()`

### Cloth sim wind direction fix
- `SimulateTestWingCloth` was using `GetWindDirection()` (base direction, no turbulence shift)
- Now uses `GetShiftedWindDirection()` — cloth responds to gust direction changes
- File: `WingTestSim.cpp`

### WindsockActor connected to live wind
- Was completely standalone with its own `WindSpeed` (knots) and `WindDirection` (degrees) UPROPERTYs
- Now reads live wind from `UParamotorGameInstance` (base wind) and `AWingActor` (live wind with gusts + turbulence shift)
- Switched from knots to m/s internally (`MaxInflationSpeed` changed from 15 knots to 7.7 m/s)
- Removed `WindDirection` and `WindSpeed` UPROPERTYs (no longer manually set)
- Added `CachedGameInstance` member
- Uses `GetAllActorsOfClass(AWingActor)` to find the wing in the world
- Files: `WindsockActor.h`, `WindsockActor.cpp`

### Build status
- Build succeeded (UE 5.8, MSVC 14.44, Win64 Development)
- 12 actions, 86.56s

## Session 22c — HUD Splits + RunningShoes Removal

### Assets removed
- `Content/MetaHumans/RunningShoes/` — 248.8 MB of unused MetaHuman shoe assets (cloth, meshes, materials, textures). No C++ references. Deleted.

### HUD function splits

**UW_FlightHUD.cpp — UpdateWingData split (210 → 15 lines):**
- `UpdateWingData()` reduced to 15-line dispatcher
- Extracted: `UpdateWingStateDisplay()`, `UpdateWingInflationProgress()`, `UpdateWingFlightMetrics()`, `UpdateWingTakeoffInfo()`, `ClearWingData()`
- Each helper calls `Wing->GetWingState()` internally (avoids exposing `EWingState` in the HUD header)

**UW_PauseMenu.cpp — BuildOptionsTab split (198 → 25 lines):**
- `BuildOptionsTab()` reduced to 25-line dispatcher
- Extracted: `BuildQualitySection()`, `BuildResolutionSection()`, `BuildDisplayTogglesSection()`, `BuildKeybindSection()`
- Added `CreateLabeledCheckbox()` helper — deduplicates VSync and Shadows checkbox creation (~30 lines of duplication eliminated)

### Build status
- Build succeeded (UE 5.8, MSVC 14.44, Win64 Development)

## Session 22b — UpdateFlight Split + Continued Cleanup

### WingFlightPhysics.cpp — UpdateFlight split (~400 → 55 lines)
- `UpdateFlight()` reduced from ~398 lines to ~55 lines (slim dispatcher)
- Added `FFlightCtx` nested struct to hold intermediate flight computation state (Vms, AoA, CL, CD, forces, velocity, etc.)
- Extracted helpers:
  - `ComputeAeroState(FFlightCtx&, FVector Vel, float Dt)` — apparent wind, AoA, ground effect, aero coefficients, collapse detection/recovery (~95 lines)
  - `ComputeForces(FFlightCtx&)` — lift (per-side), wing/pilot/line drag, thrust, mass, glide ratio (~30 lines)
  - `IntegrateForces(FFlightCtx&, FVector Vel, float Dt, FVector PawnLoc)` — force integration, thermal velocity contribution, velocity safety caps (~35 lines)
  - `UpdatePendulumAndSteering(float Dt)` — pendulum physics, weight shift, brake yaw, collapse yaw, spiral dive, roll rate (~40 lines)
  - `UpdatePilotOrientation(APawn*, ACharacter*, float Dt)` — yaw, pilot roll lag, auto-seat, pitch, mesh rotation (~25 lines)
  - `HandleLanding(...)` — ground contact, friction, injury, flight state reset (~55 lines)
  - `UpdateWingTransform(FFlightCtx&, FVector PilotLoc, float Dt)` — wing position above pilot, wing yaw/pitch/roll (~20 lines)

### Build status
- Build succeeded (UE 5.8, MSVC 14.44, Win64 Development)

## Session 22 — Refactoring Pass + Dead Code Removal

### Function splits (readability)

**ParamotorCharacter.cpp — BeginPlay split:**
- `BeginPlay()` reduced from ~188 lines to 12 lines (slim dispatcher)
- Extracted: `LoadCharacterMesh()`, `LoadAnimations()`, `ApplySkinTone()`, `LoadFootstepSounds()`, `LoadAmbientAudio()`
- Preserved all fallback paths (techwear → Hightop, old single footstep sounds, etc.)

**ParamotorCharacter.cpp — UpdateThrustGroundPhysics split:**
- Extracted `ApplyThrustRunningBoost()` — the walking+thrust velocity boost section (~55 lines)
- `UpdateThrustGroundPhysics` now dispatches to the helper for running, handles standing/no-thrust inline

**ParamotorActor.cpp — BuildAArmHierarchy split:**
- Extracted `BuildSingleAArmSide(bool bLeft, USceneComponent* Stud)` — builds one side of the A-arm hierarchy
- Eliminated ~50 lines of left/right duplication (mesh loading, quick links, riser setup)
- `BuildAArmHierarchy` now finds studs, calls the helper twice, captures carabiner offset

### Dead code removed

**WingActor.h — dead constants (never referenced):**
- `KMH_PER_MS`, `KNOTS_PER_MS` — unused unit conversions
- `BRAKE_SLACK_CM`, `MAX_KITE_OFFSET_CM`, `MAX_KITE_VELOCITY_CM_S` — kiting logic was removed previously
- `INFLATION_TARGET_HEIGHT_CM`, `INFLATION_OVERSHOOT_BASE_CM`, `TAKEOFF_HEIGHT_THRESHOLD_CM` — unused
- `OSWALD_EFFICIENCY` — global constant unused (member `OswaldEfficiency` is used instead)
- `MAX_THRUST_N` — never referenced

**WingActor.h — dead member variables:**
- `InflationHeight` — reset to 0 but never read
- `ALineFadeFactor` — declared, never read or written
- `GroundLogTimer`, `SustainedAirTime`, `StallLogTimer` — declared, never referenced
- `bInParachutalStall` — set true/false in flight physics but never read (kept TODO comment)
- `TrimSpeedMs` — written in flight physics but never read
- `CachedCanopyMin`, `CachedCanopyMax`, `bCanopyBoundsCached` — set in cloth init but never read
- `bCanopyMaterialApplied`, `bLineMaterialApplied` — set to true but never checked

**WingActor.h — redundant forward declarations:**
- `UStaticMeshComponent`, `UProceduralMeshComponent` — already covered by includes
- `UMaterialInstanceDynamic` — not referenced in the class

**ParamotorCharacter.h:**
- `bInjuryLimpActive` — write-only (set in ApplyImpactInjury/HealInjury/ClearInjury, never read). Removed all 6 assignments.

**ParamotorAnimInstance.h:**
- `SetBrakeArmData()` — dead public function (data path bypassed; NativeUpdateAnimation copies IK directly from character)

**UW_FlightHUD.h:**
- `class UImage;` forward declaration — unused

### Unused includes removed
- `WingActor.cpp`: `ParamotorCharacter.h`
- `ParamotorCharacter.cpp`: `Animation/AnimSingleNodeInstance.h`, `Materials/MaterialInstance.h`, `Engine/Texture2D.h`
- `UW_FlightHUD.cpp`: `Components/SizeBox.h`, `Components/SizeBoxSlot.h`, `Components/Spacer.h`, `Kismet/GameplayStatics.h`
- `UW_MainMenu.cpp`: `ParamotorCharacter.h`, `GameFramework/PlayerStart.h`, `GameFramework/PlayerController.h`
- `ParamotorPlayerController.cpp`: `Engine/StaticMeshActor.h`
- `ParamotorGameMode.cpp`: `Engine/World.h`
- `WindsockActor.cpp`: `Math/UnrealMathUtility.h`

### Build errors fixed during this session
- `ParamotorGameMode.cpp` still uses `UAudioComponent` — re-added `Components/AudioComponent.h` (scan was wrong)
- `UW_FlightHUD.cpp` still uses `UCharacterMovementComponent` — re-added `GameFramework/CharacterMovementComponent.h` (scan was wrong)

### Build status
- Build succeeded (UE 5.8, MSVC 14.44, Win64 Development)

### Remaining refactor targets (not yet done)
- `WingFlightPhysics.cpp::UpdateFlight` — ~400 lines, the largest function. Needs splitting into ApplyLiftDrag, ApplyThrust, ApplyThermal, UpdatePendulum, UpdateCollapses, UpdateRollYaw, HandleLanding.
- `UW_FlightHUD.cpp` — `BuildWingPanel` (~146 lines), `UpdateFlightStats` (~107 lines), `UpdateEngineData` (~136 lines), `UpdateWingData` (~210 lines)
- `UW_PauseMenu.cpp::BuildOptionsTab` — ~202 lines
- `ParamotorAnimInstance.cpp::FParamotorAnimProxy::ApplyArmIK` — ~133 lines
- Magic numbers in WindsockActor, ParamotorActor temperature curves, riser offset, cloth/line stiffness
- Wind vector computation duplicated between WingActor.cpp and WingFlightPhysics.cpp — should be shared `GetWindVector()`
- Stall-speed calculation duplicated between GetTakeoffChance and UpdateFlight — should be `GetStallSpeedMs(float Weight)`

## Session 21b — Additional Cleanup (UI, dead code, correctness fixes)

### Fixes
1. **Main menu Play button** — Was loading `/Engine/Maps/Templates/Template_Default` (empty placeholder). Now loads `L_OpenWorld` (the real game map).
2. **Pause menu null guard** — `TakeWidget()` return value wasn't checked before `SetWidgetToFocus()`. Now guarded with `IsValid()`.
3. **GetCarabinerPositions fallback** — Computed valid estimated positions but returned `false`. Now returns `true` (callers were ignoring the result anyway, but the contract was wrong).
4. **GetTakeoffChance +1.5kg** — Unexplained `+1.5f` in stall speed weight calculation. Removed (pilot weight already includes paramotor + fuel mass).
5. **Starter audio** — Kept asset-level `bLooping` (UE5 `UAudioComponent` doesn't expose a looping flag). Added comment explaining why.

### Dead code removed
- **`UpdateWingTarget()`** — No-op in `ParamotorActor.cpp`, kept only for API compatibility. Removed from both `.cpp` and `.h`.
- **`CurrentLeanPitch`** — Always forced to 0 in `UpdatePostureLean()`. The `LeanQuat * BaseQuat` multiplication was always `identity * BaseQuat = BaseQuat`. Removed variable and simplified rotation code.
- **`SmoothedLeftBrake`/`SmoothedRightBrake`** — Assigned from `EffectiveLeftBrake`/`EffectiveRightBrake` but never read. Removed from header and physics.
- **`ParachutalStallTimer`** — Accumulated but never read. Removed (kept `bInParachutalStall` flag for future force modulation/HUD).

### Build status
- Build succeeded (UE 5.8, MSVC 14.44, Win64 Development)
- 4 actions, 4.77s (incremental)

## Session 21 — Critical Bug Fixes + Dead Code Cleanup

### 8 critical bugs fixed

1. **Takeoff lift check (WingFlightPhysics.cpp)** — Was using `CL0 = 0.3` to compute lift at takeoff speed derived from `CLMax = 1.5`. Lift was only ~29% of weight, so the 80% threshold was never reached. Fix: CL now interpolates from CL0 to CLMax as airspeed approaches takeoff speed.

2. **Ground effect inverted (WingFlightPhysics.cpp)** — Formula `1/(1+0.5*H²)` was 1.0 at ground (full induced drag) and decreased with altitude. Fix: linear formula `0.5 + 0.5*H` gives 0.5 at ground (50% induced drag reduction) and 1.0 at one wingspan (no effect).

3. **Thermal force runaway climb (WingFlightPhysics.cpp)** — Was dividing by DeltaTime, adding ~70% of updraft speed to vertical velocity *every frame*. Fix: thermal now applied as a velocity Lerp toward the updraft speed with 0.5 coupling, capped at the updraft speed. Frame-rate independent, no runaway.

4. **Brake Target vs Amount plumbing (WingActor.cpp)** — `SetLeftBrake`/`SetRightBrake` wrote to `LeftBrakeTarget`/`RightBrakeTarget`, but `UpdateFlight` read `LeftBrakeAmount`/`RightBrakeAmount`. Brakes had no effect in flight. Fix: `SetLeftBrake`/`SetRightBrake` now write to both `Amount` and `Target`.

5. **Landing friction dimensionally wrong (WingFlightPhysics.cpp)** — Was `0.4 * TotalMassKg * GRAVITY_MS2 * DeltaTime * CM_PER_M` (units: kg·cm/s, mass-dependent). Fix: `0.4 * GRAVITY_CM_S2 * DeltaTime` (units: cm/s, mass-independent). Also fixed landing speed reading pre-physics velocity instead of post-zeroing.

6. **Trip injury reads zeroed velocity (ParamotorCharacter.cpp)** — `Trip()` called `GetVelocity().Size2D()` after `StopMovementImmediately()` which zeroed it. Fix: use `LaunchVel` (captured before stop).

7. **VSI color order wrong (UW_FlightHUD.cpp)** — `-800 fpm` check was unreachable because `-300 fpm` caught it first. Fix: check most severe condition first (`< -800` → red, then `< -300` → yellow).

8. **Wing model set after FinishSpawning (ParamotorPlayerController.cpp)** — Wing model specs were assigned after `FinishSpawning` which triggers `BeginPlay`. Fix: set specs before `FinishSpawning`.

### Dead code removed from WingActor.h

Removed ~100 lines of unused state from the old procedural system:
- `CellPressure[46]`, `CellFill[46]`, `CellLift[46]`, `CellDrag[46]`, `NUM_WING_CELLS`, `WingPorosity` — "real cell pressure model" never implemented
- `KiteOffset`, `KitePitchOvershoot`, `KiteYawDrift`, `KiteYawVel`, `KiteRollSmoothed`, `PrevSymBrake`, `WingGroundYaw`, `WingGroundYawRate`, `WingGroundVel` — kiting dynamics never implemented
- `WingVel`, `WingPitchVel`, `WingRollVel`, `WingYawVel` — wing body physics never used
- `KiteLeftStall`, `KiteRightStall`, `KiteLeftDrag`, `KiteRightDrag` — per-side kiting never implemented
- `LineTensionA/B/C/D`, `LineTensionFraction`, `LineStretchA/D`, `PrevPilotVel` — line tension never computed
- `TakeoffSkipZ`, `TakeoffFootContacts`, `LaunchAngleOffset`, `CollapseRecoveryTimer` — never used
- `CanopyOpenness`, `CellFillFactor` — write-only (set to 0, never read)
- `EndLogTimer`, `InflLogTimer`, `RiserLogTimer`, `EntryLogTimer`, `RisersLogTimer`, `PawnLogTimer`, `RollDebugTimer` — unused log timers
- `ELineLevel` enum — never used
- `DEFAULT_TRIM_AOA_DEG`, `DEFAULT_STALL_AOA_DEG`, `PENDULUM_PERIOD_S` — unused constants
- Constructor cell array zeroing — removed
- DisconnectRisers cell array reset — removed
- ConnectRisers kiting variable initialization — removed (replaced with local variable)

### Files changed
- `Source/ParamotorSim/Public/WingActor.h` — removed ~100 lines of dead state, enums, and constants
- `Source/ParamotorSim/Private/WingActor.cpp` — removed cell array zeroing, kiting init, fixed brake plumbing
- `Source/ParamotorSim/Private/WingFlightPhysics.cpp` — fixed takeoff CL, ground effect, thermal force, landing friction/speed
- `Source/ParamotorSim/Private/ParamotorCharacter.cpp` — fixed trip injury velocity
- `Source/ParamotorSim/Private/UW_FlightHUD.cpp` — fixed VSI color order
- `Source/ParamotorSim/Private/ParamotorPlayerController.cpp` — fixed wing model spawn order

### Build status
- Build succeeded (UE 5.8, MSVC 14.44, Win64 Development)
- 11 actions, 106.65s (full rebuild from header change)

## Session 20 — Thermal System + Wind System Fixes

### What was built

**Thermal system (new):**
- `FThermal` struct: position, radius, strength (m/s updraft), altitude range (base to cloudbase), lifetime, drift velocity
- `UpdateThermals(float DeltaTime)` — spawns, drifts (with wind), ages, and removes thermals
- `SpawnThermal()` — creates thermals near pilot at random positions (50-500m away), 30-100m radius, 2-6 m/s peak strength
- `GetThermalLiftAtPosition(FVector, float AltitudeM)` — returns updraft m/s at a given position/altitude. Uses cosine radial falloff + parabolic altitude profile (strongest at mid-height)
- `ComputeThermalStrengthMultiplier()` — scales thermal strength from weather conditions:
  - Temperature: 5°C = no thermals, 25°C = full, 35°C = 1.5×
  - Cloud cover: clear = full strength, overcast = 20%
  - Time of day: bell curve peaking at 14:00, zero outside 09:00-19:00
- Cloudbase altitude computed from cloud cover (600m overcast → 2200m clear)
- Thermal lift applied in `WingFlightPhysics.cpp` as vertical force on the wing (70% coupling factor)

**Wind gradient with altitude (fix):**
- Wind speed now follows 1/7 power law profile: `V(h) = V_ref × (h/10m)^0.143`
- Near-zero wind at ground level, reference at 10m, up to 2.5× at altitude
- Applied in `UpdateDynamicWind()` after base + turbulence calculation

**Wind direction drift (fix):**
- Wind direction now slowly drifts ±5°/minute via random walk
- No longer locked at riser connection time
- Applied in `UpdateDynamicWind()`

**Wind audio fix:**
- `UpdateWindAudio()` now scales with actual wind speed + pilot airspeed (apparent wind)
- Was: volume from pilot speed only (standing in 20 m/s wind = silence)
- Now: volume from `WindSpeed + PilotSpeed × 0.5`, mapped 0-15 m/s to 0.15-0.6 volume

**HUD temperature fix:**
- `UpdateFlightStats()` now uses `GameInstance->Temperature` as sea-level base
- Was: hardcoded ISA 15°C base, ignoring user-set Temperature
- Now: user-set ground temp with ISA lapse rate (-6.5°C/1000m)

**Dead variables now live:**
- `CloudCover` → thermal strength multiplier + cloudbase altitude
- `Temperature` → thermal strength multiplier + HUD temperature base
- `TimeOfDay` → thermal strength multiplier (day/night cycle for thermals)

### Files changed
- `Source/ParamotorSim/Public/WingActor.h` — added `FThermal` struct, thermal system members and method declarations
- `Source/ParamotorSim/Private/WingActor.cpp` — implemented thermal system, wind gradient, wind direction drift
- `Source/ParamotorSim/Private/WingFlightPhysics.cpp` — added thermal lift force to flight physics
- `Source/ParamotorSim/Private/ParamotorCharacter.cpp` — fixed wind audio to use wind speed
- `Source/ParamotorSim/Private/UW_FlightHUD.cpp` — fixed temperature to use GameInstance Temperature

### Build status
- Build succeeded (UE 5.8, MSVC 14.44, Win64 Development)
- 11 actions, 108.58s (full rebuild from header change)

### Remaining wind/thermal work (future)
- Visual thermal indicators (dust devils, birds circling, cloud streets)
- Sea breeze modeling for coastal environment
- Wind shadowing from terrain
- Ground-based wind visual effects (grass movement, dust)
- Day/night lighting cycle from TimeOfDay
- Thermal detection audio (vario beep)

## Session 19 — WingActor Refactor

### What was refactored
`WingActor.cpp` was refactored to split `TickTestWing()` and extract reusable helpers from `BeginPlay` and `ForceTakeoff`.

**Before:**
- `TickTestWing()` was 147 lines mixing connected state, flight dispatch, cloth/line sim, mesh updates, and riser mesh positioning
- `BeginPlay()` had inline material loading (20 lines)
- `ForceTakeoff()` had inline CMC setup (10 lines) that could be reusable

**After:**
- `TickTestWing()` is 25 lines (slim dispatcher)
- Material loading extracted to `LoadMaterials()`
- CMC flight setup extracted to `EnterFlightMode(ACharacter*)` — reusable by `CheckTakeoff` in `WingFlightPhysics.cpp`

### Architecture

**Init helpers (split from BeginPlay):**
- `LoadMaterials()` — loads canopy + line materials with fallback chain

**Flight mode helper (extracted from ForceTakeoff):**
- `EnterFlightMode(ACharacter* Char)` — disables CMC, sets MOVE_None, zero gravity, disables rotation flags, sets mesh to flight base rotation. Reusable by both `ForceTakeoff()` and `CheckTakeoff()`.

**Tick sub-helpers (split from TickTestWing):**
- `UpdateConnectedState(float DeltaTime)` — line tension drag, yaw to wind, inflation transition (Connected → Inflating → Inflated)
- `UpdateRiserMesh()` — positions riser mesh at carabiner midpoint (connected) or average line bottom (disconnected), with proper yaw alignment

### Files changed
- `Source/ParamotorSim/Public/WingActor.h` — added `LoadMaterials`, `EnterFlightMode`, `UpdateConnectedState`, `UpdateRiserMesh` declarations
- `Source/ParamotorSim/Private/WingActor.cpp` — extracted helpers, slimmed `TickTestWing` to dispatcher

### Build status
- Build succeeded (UE 5.8, MSVC 14.44, Win64 Development)
- 11 actions, 92.36s (full rebuild due to header change)

## Session 18 — Player Controller Refactor

### What was refactored
`AParamotorPlayerController` was refactored to split the monolithic `Tick()` and deduplicate spawn logic.

**Before:**
- `Tick()` was 163 lines mixing throttle, grab, carabiners, brakes, weight shift, and IK
- 3 spawn functions each duplicated the ground-trace pattern (~10 lines each)

**After:**
- `Tick()` is 8 lines (slim dispatcher)
- Ground-trace deduplicated into `FindGroundZ()` helper

### Architecture

**Tick helpers (split from Tick):**
- `UpdateThrottle(float DeltaTime)` — throttle ramp up/down + spring return + thrust to character
- `UpdateGrabPosition(APawn* CachedPawn)` — crosshair-follow grab position update
- `UpdateWingRig(float DeltaTime, APawn* CachedPawn)` — carabiner positions, brake ramping, weight shift, arm IK
- `ComputeBrakeArmTargets(AParamotorCharacter*, AWingActor*, FVector&, FVector&)` — determines which brake handle is on pilot's left vs right (wing may be yawed relative to pilot)

**Spawn helper:**
- `FindGroundZ(FVector Start, APawn* IgnorePawn)` — traces down to find ground Z, used by all 3 spawn functions

### Files changed
- `Source/ParamotorSim/Public/ParamotorPlayerController.h` — added forward declaration for AParamotorCharacter, added helper method declarations
- `Source/ParamotorSim/Private/ParamotorPlayerController.cpp` — split Tick, extracted helpers, deduplicated spawn ground traces

### Build status
- Build succeeded (UE 5.8, MSVC 14.44, Win64 Development)
- 9 actions, 24.85s

## Session 17 — HUD Refactor

### What was refactored
`UW_FlightHUD` (the in-flight HUD) was refactored from a monolithic 1110-line file into a clean, split architecture.

**Before:**
- `NativeOnInitialized()` was 463 lines with 6 local lambda helpers
- `NativeTick()` was 700 lines with deeply nested logic (compass, wind, stats, engine, wing, injury, trimmer, instructions all inline)

**After:**
- `NativeOnInitialized()` is 12 lines (slim dispatcher calling 5 builders)
- `NativeTick()` is 18 lines (slim dispatcher calling 8 update methods)

### Architecture

**UI construction helpers (extracted from lambdas):**
- `MakeText()` — styled text block with font size + outline
- `AddToVBox()` — add widget to vertical box with padding
- `MakePanel()` — styled semi-transparent border panel
- `MakeHeader()` — section header text
- `AddStatRow()` — label + value row
- `AddSeparator()` — thin divider line
- `AddGaugeRow()` — label + value + progress bar row

**UI builders (split from NativeOnInitialized):**
- `BuildFlightPanel()` — top-right compass + flight stats
- `BuildEnginePanel()` — bottom-left engine gauges
- `BuildWingPanel()` — bottom-left wing state + brakes + inflation + glide + trimmer
- `BuildInstructionBar()` — bottom-center context-sensitive instructions
- `BuildCrosshair()` — center dot + ring

**Tick update methods (split from NativeTick):**
- `UpdateCompass()` — heading cardinal + degrees
- `UpdateWindInfo()` — wind direction cardinal + speed
- `UpdateFlightStats()` — altitude, ground speed, airspeed, VSI, temp, wind speed
- `UpdateEngineData()` — engine state, RPM, throttle, CHT, EGT, fuel, hours
- `UpdateWingData()` — wing state, brakes, inflation, pendulum, weight support, glide ratio, takeoff chance, debug
- `UpdateInjuryStatus()` — injury name + severity
- `UpdateTrimmerStatus()` — trimmer position bar + seat/hang point
- `UpdateInstructions()` — context-sensitive on-screen guidance

**Static helper:**
- `HeadingToCardinal()` — converts heading degrees to N/NE/E/SE/S/SW/W/NW (deduplicated from 2 inline copies)

### Files changed
- `Source/ParamotorSim/Public/UW_FlightHUD.h` — added forward declarations, helper/builder/update method declarations, static HeadingToCardinal
- `Source/ParamotorSim/Private/UW_FlightHUD.cpp` — full rewrite into split architecture

### Build status
- Build succeeded (UE 5.8, MSVC 14.44, Win64 Development)
- 4 actions, 5.18s

## Session 16 — Pause Menu Refactor

### What was refactored
`UW_PauseMenu` was refactored from a monolithic 1131-line file with an 866-line `NativeOnInitialized()` into a clean, split architecture.

**Before:**
- `NativeOnInitialized()` was 866 lines (largest function in the project)
- 4 duplicated quality preset handlers
- `WidgetStyle` deprecation warnings

**After:**
- `NativeOnInitialized()` is 12 lines (slim dispatcher)
- 1 common `SetQualityLevel()` method
- Zero deprecation warnings

### Architecture

**UI construction helpers:**
- `MakeText()`, `MakeLeftText()` — styled text blocks
- `CreateCardButton()`, `CreatePillButton()` — styled buttons
- `CreateSectionHeader()`, `CreateNavButton()`, `CreateTabPanel()`, `CreateWingCard()` — UI element builders
- `AddKeybindRow()` — key binding row
- `ApplyButtonColors()` — static helper replacing deprecated WidgetStyle access

**UI builders:**
- `BuildRootLayout()` — root canvas, dim background, main panel, sidebar shell, content area
- `BuildSidebar()` — logo, nav buttons, footer
- `BuildMainTab()` — resume/quit buttons
- `BuildSpawnTab()` — paramotor/wing spawn buttons
- `BuildOptionsTab()` — quality presets, resolution scale, toggles, keybinds
- `InitFromCurrentSettings()` — sync UI to current game settings

### Files changed
- `Source/ParamotorSim/Public/UW_PauseMenu.h` — refactored widget members and helper declarations
- `Source/ParamotorSim/Private/UW_PauseMenu.cpp` — full rewrite into split architecture

### Build status
- Build succeeded (UE 5.8, MSVC 14.44, Win64 Development)

## Session 15 — New Flight Physics for MacPara Charger Wing

### What was built
A completely new `WingFlightPhysics.cpp` (506 lines) implementing full aerodynamic flight for the MacPara Charger 25M wing. This replaces the ~1000-line procedural flight physics that was deleted in Session 14.

### Architecture
Two new functions added to `AWingActor`:

1. **`CheckTakeoff(float DeltaTime)`** — Called every frame while `Inflated` and risers connected. Detects when the pilot has enough airspeed and lift to leave the ground, then transitions to `Flying` state. Uses real stall speed calculation: `V_stall = sqrt(2 * W / (rho * S * CLmax))` with 20% margin.

2. **`UpdateFlight(float DeltaTime)`** — Called every frame while `Flying`. Full flight physics:
   - Trimmer interpolation (smooth position changes)
   - Brake lag filter (trailing edge deflection takes ~0.3s)
   - Apparent wind computation (wind + turbulence - velocity)
   - Angle of attack from trim + brake + trimmer + turbulence
   - Ground effect (induced drag reduction near ground)
   - NACA 4415 polar lookup with camber adjustment from brake input
   - Per-side lift (left/right halves for asymmetric collapse effects)
   - Wing drag + pilot drag + line drag
   - Thrust from paramotor engine (blade-element theory via `GetThrustForceN`)
   - Force integration (lift, drag, thrust, gravity → acceleration → velocity)
   - Pendulum physics (pilot swings under wing on 7.5m lines)
   - Roll steering (weight shift + brake asymmetry + collapse-induced roll)
   - Yaw steering (brake yaw + collapse yaw, with inertia)
   - Spiral dive modeling (bank > 40° → spiral tendency increases)
   - Collapse detection (asymmetric from heavy brake at high AoA, frontal from negative AoA)
   - Collapse recovery (wind-dependent recovery rate, brake slows recovery)
   - Parachutal stall (too much brake at low speed)
   - Pilot orientation (yaw, pitch transition to seated, roll lag from pendulum)
   - Landing detection (ground contact, friction deceleration, injury from hard landing)
   - Full flight state reset on landing
   - Wing position/orientation (at line length above pilot, yawed into wind, pitched from AoA, rolled from bank)
   - HUD variable updates (airspeed, climb rate, glide ratio)
   - Flight logging (1 Hz)

### Key physics formulas
- **Lift**: `L = 0.5 * ρ * V² * S * CL` (per-side, with collapse fraction)
- **Drag**: `D = 0.5 * ρ * V² * S * (CD0 + CL²/(π*AR*e) * ground_effect + brake_CD)`
- **Thrust**: From `ParamotorActor::GetThrustForceN(airspeed)` — blade-element momentum theory
- **Stall speed**: `V_stall = sqrt(2 * W / (ρ * S * CLmax))`
- **Pendulum**: `ω = sqrt(g/L)`, damped oscillation
- **Ground effect**: `GE = 1/(1 + 0.5*(h/b)²)` where h=altitude, b=wingspan

### Takeoff transition
- `CheckTakeoff()` runs while Inflated + connected
- Conditions: airspeed ≥ 1.2 × stall speed, lift ≥ 80% weight, throttle > 25%
- On takeoff: sets MOVE_None, gravity 0, stores ground-run velocity, begins seat transition
- Debug diagnostics stored for HUD: ground airspeed, ground speed, headwind, takeoff threshold, weight support

### Landing transition
- Ground contact detected when `Z ≤ groundZ + 1cm` and descending
- Friction decelerates horizontal velocity
- Hard landing (> 3 m/s vertical) applies injury via `ApplyImpactInjury`
- Full flight state reset: pendulum, pitch, roll, brakes, collapses, trimmers, cells
- Returns to Inflated state, restores MOVE_Walking

### Files changed
- **New**: `Source/ParamotorSim/Private/WingFlightPhysics.cpp` (506 lines)
- **Modified**: `Source/ParamotorSim/Public/WingActor.h` — added `UpdateFlight()` and `CheckTakeoff()` declarations
- **Modified**: `Source/ParamotorSim/Private/WingActor.cpp` — added flight physics calls in `TickTestWing()`

### Build status
- Build succeeded (UE 5.8, MSVC 14.44, Win64 Development)
- Game launched and ran without crashes
- No runtime errors in log

### What works now (full core loop)
- Spawn → Equip paramotor → Connect risers → Inflate (wind-driven) → Kite → Run + throttle → Takeoff → Fly → Land
- All flight controls: throttle (W/S), brakes (Q/E), weight shift (mouse), trimmers (=/-), seat toggle (G), hang point ([/])
- HUD displays: airspeed, climb rate, glide ratio, pendulum angle, brake amounts, trimmer position, seat/hang point status
- F5 force takeoff still works for debugging

## Session 14 — Deleted Procedural Wing Path (MacPara Charger is the only wing)

### What was deleted
The entire procedural wing simulation path was removed. The MacPara Charger 25M (formerly "test wing") is now the only wing. This eliminates ~5000 lines of duplicated simulation code and the two-path maintenance nightmare.

**Deleted source files:**
- `WingClothSim.cpp` — procedural Verlet cloth simulation (1320 lines)
- `WingLinesSim.cpp` — procedural line cascade simulation
- `WingVisuals.cpp` — procedural canopy/line mesh generation + ApplyInflatedShape
- `WingGroundHandling.cpp` — procedural inflation/kiting physics + GetTakeoffChance
- `WingFlightPhysics.cpp` — procedural flight physics (1009 lines) + trimmer/seat helpers
- `ParagliderWingMesh.h` — 15k-line generated procedural wing mesh data header

**Deleted from WingActor.h:**
- `bUseTestWingMesh` UPROPERTY (no longer needed — test wing is the only wing)
- `FFlightContext` struct (procedural flight physics helper state)
- `FClothContext` struct (procedural cloth sim helper state)
- `FLineParticle` struct + `LineParticles` array (procedural line particles)
- `FLineAttachment` struct + `LineAttachments` array
- `LineConstraints` array
- `LeftRiserIdx`, `RightRiserIdx`, `LeftCarabinerIdx`, `RightCarabinerIdx`, `LeftBrakeHandleIdx`, `RightBrakeHandleIdx`
- `LeftRiserDir`, `RightRiserDir`
- `Gravity`, `Damping`, `ConstraintIterations` (procedural physics params)
- All procedural function declarations: `InitCloth`, `InitLines`, `SimulateCloth`, `SimulateLines`, `UpdateInflation`, `UpdateFlight`, `UpdateCellPressure`, `GetTotalCellLiftN`, `GetTotalCellDragN`, `GetLeftCellDragN`, `GetRightCellDragN`, `UpdateCanopyMesh`, `UpdateLineMesh`, `SetInflatedShape`, `ApplyInflatedShape`, `GetRibVertexIndex`, `UpdateKitingCollapses`, `ComputeThermalLiftMs`, `HandleLanding`, `UpdateWingPositionFromLines`, `ApplyVelocitySafetyCaps`, `ShiftAllParticles`, all `UpdateFlight*` helpers, all `SimulateCloth*`/`UpdateKiting*` helpers, `TickProceduralWing`

**Deleted from WingActor.cpp:**
- `#include "ParagliderWingMesh.h"`
- Procedural `else` branch in `BeginPlay()`
- `TickProceduralWing()` function (180 lines)
- Procedural carabiner unpin in `DisconnectRisers()`

### Functions moved to WingActor.cpp
These were in the deleted files but called from outside the procedural path:
- `SetSpreadState()` — from WingVisuals.cpp (visibility toggle)
- `GetTakeoffChance()` — from WingGroundHandling.cpp (HUD reads this)
- `GetTrimmerAoAOffset()`, `GetTrimmerPitchOffset()`, `GetTrimmerCollapseRisk()` — from WingFlightPhysics.cpp
- `GetTrimmerLabel()` — from WingFlightPhysics.cpp (HUD and controller read this)
- `ToggleSeat()`, `AdjustHangPoint()` — from WingFlightPhysics.cpp (controller calls these)

### New implementations
- `GetLeftBrakeHandleWorldPos()` / `GetRightBrakeHandleWorldPos()` — were in deleted WingLinesSim.cpp, reimplemented in WingActor.cpp using carabiner positions minus riser length (approximation for arm IK)

### What remains
- `WingActor.cpp` — actor lifecycle, wind, shared API, moved functions
- `WingTestSim.cpp` — the only wing simulation (MacPara Charger cloth + lines + meshes)
- `MacParaChargerData.h` — real wing geometry
- `MacParaChargerLines.h` — real line endpoints
- `AirfoilPolar.h/.cpp` — aerodynamic polar data

### Build verification
- Build succeeded (UE 5.8, MSVC 14.44, Win64 Development)
- Only pre-existing `UW_PauseMenu.cpp` `UButton::WidgetStyle` deprecation warnings remain

### Architecture note
The test wing path does NOT have flight physics yet. It handles ground handling (inflation, kiting, line tension) but `UpdateFlight()` was part of the deleted procedural path. Flight physics will need to be reimplemented for the MacPara Charger wing. This is the next major task.

## Session 13 — Whole-Game Audit & Refactor

### Bug Fixes
- **Engine state machine** (`ParamotorActor.cpp` `Tick()`): `UpdateEngineSpoolDown()` was running for both `Off` and `Cranking` states (the `else` branch caught both). Fixed to only spool down when `EngineState == Off`.
- **DisconnectRisers state reset** (`WingActor.cpp`): Did not reset `bIsAirborne`, `CellFillFactor`, `CanopyOpenness`, `InflationProgress`, `InflationHeight`, `CellFill[]`, or `CellPressure[]` on disconnect. Now resets all flight state to avoid stale values on next connect.
- **HandleLanding speed threshold** (`WingFlightPhysics.cpp`): Only transitioned `WingState` from `Flying` to `Inflated` if `NewVel.Size2D() < 100.0f` (1 m/s). Real landings can be faster. Now always transitions on ground contact. Also resets `CanopyOpenness`, `CellFillFactor`, and `CellFill[]`/`CellPressure[]` on landing.

### Dead Code Removed
- **WingActor.h**: `TestWingMesh` (never created), `KiteLogTimer` (never referenced), `DragReductionFromSeat` (assigned but never read), `DutchRollYaw`/`DutchRollRoll` (always 0, no effect), `FlightTurbulence` (always ZeroVector, no effect), `SmoothedKillFactor` (never referenced), `WindGustPhase` (never referenced), `GroundFriction` (CMC's GroundFriction used instead).
- **Dead functions**: `ComputeGroundWind()` (never called), `SetTestWingInflated()` (never called), `PumpBrakes()` (stub — only logged), `GetTrimmerSpeedFactor()`/`GetTrimmerDragFactor()`/`GetTrimmerCLFactor()` (all returned 1.0 — no-op multipliers, polar handles trimmer effects through AoA).
- **Dead variable**: `StretchAoAEffect` (computed but never used), `YawRestoring` (always 0, added to yaw velocity for no effect).
- **UW_FlightHUD**: `HeadingIndicator`/`WindIndicator` (never created), `MakeImage` lambda (never called).
- **UW_PauseMenu**: `OnSpawnParamotorClicked` (never bound to a button), `SetTabActive` unused parameters `InactiveBtn1`/`InactiveBtn2` (function derived state from internal array instead).
- **ParamotorPlayerController**: `bTrimmerFastHeld`/`bTrimmerSlowHeld` (set but never read).
- **ParamotorCharacter**: `TargetLeanPitch` (set but never read), `bWasMoving` (set but never read).
- **ParamotorGameMode.h**: Unused forward declarations `UMaterial`/`UMaterialInstanceDynamic`.

### Function Splitting
- **WingActor::Tick** (324 lines) split into `TickTestWing()` + `TickProceduralWing()` with a simple dispatcher. Each branch handles its own wind update, simulation, mesh updates, and riser positioning.

### Code Deduplication
- **Carabiner fallback**: Duplicated in `OnInteract()` and `Tick()` of `ParamotorPlayerController.cpp`. Extracted to `GetCarabinerPositions()` helper that tries real paramotor hardware first, falls back to character-position estimation.
- **Thrust query**: Duplicated 3x in `ParamotorCharacter.cpp` (`UpdateThrustGroundPhysics`, `UpdateThrustAirPhysics`, `UpdateTripRisk`). Extracted to `GetEquippedThrustN()` helper.

### Build Verification
- All builds succeeded (UE 5.8, MSVC 14.44, Win64 Development).
- Only pre-existing `UW_PauseMenu.cpp` `UButton::WidgetStyle` deprecation warnings remain.

### Deferred (Future Work)
- Split `UW_FlightHUD::NativeTick` (700 lines) and `NativeOnInitialized` (471 lines) into helpers.
- Split `UW_PauseMenu::NativeOnInitialized` (867 lines) into tab-creation helpers.
- Extract shared Verlet/constraint helper to eliminate duplication between `SimulateCloth`/`SimulateTestWingCloth` and `SimulateLines`/`SimulateTestWingLines`.
- Extract shared cylinder-tube mesh builder used in 3 places.
- Move `MacParaChargerData.h` (7800 lines) to `extern const` + `.cpp` definition or runtime-loaded asset.
- Consolidate magic numbers into named constants (engine audio, injury thresholds, attachment offsets).
- Make detailed paramotor hierarchy the authoritative connection rig (replace test-frame fallback).

## Session 12 — Fixed Broken Reflex Airfoil / Trailing Edge (ParagliderWingMesh.h + Blender asset)

### The bug
The wing canopy mesh (both `ParagliderWingMesh.h` used by `WingActor`/`WingClothSim`/`WingVisuals`/`WingLinesSim`, and the Blender hero asset `C:\DevinTools\charger_full.blend`) had two serious geometry defects, diagnosed via `C:\DevinTools\_diag_arch_full.py`:
1. **Trailing edge never closed** — every one of the 47 ribs had a 7-19cm gap between the top and bottom surface at the TE, from a post-hoc "blend near x>0.95" hack in the old `reflex_airfoil()` function that didn't actually guarantee closure and was further corrupted by prior ad-hoc `_fix_airfoil*.py` edits.
2. **Spanwise arch was not monotonic** — it had two sharp ~0.85m discontinuities (around rib 9/10 and rib 36/37) instead of a smooth dome, i.e. two different generation passes had been stitched together badly.

### The fix
New shared module `C:\DevinTools\charger_wing_geometry.py`:
- Airfoil defined as `camber(x) ± thickness(x)/2`, with `thickness(x)` analytically zero at `x=0` and `x=1` — closure is a mathematical guarantee, not a blend hack. Self-test asserts closure < 1e-6cm before any consumer trusts it.
- Reflex camber: rises to peak (~9.3cm at root) near 28% chord, eases down through mid-chord, dips negative (~-3.2cm, the actual "reflex") near 85-90% chord, returns exactly to 0 at the TE.
- Spanwise arch: single continuous `cos()` dome (30cm anhedral rise at center) + smoothstep-eased tip drop (90cm, starting at 72% span) — no discontinuities by construction.
- Taper: root chord fixed at 271cm (real spec), TE held at a fixed unswept X (matches the existing rig convention so lines/risers/hardware didn't need to move), tip chord ratio **calibrated to 0.6136** (not guessed) so the resulting planform area/AR exactly match the published spec (25.43 m², AR 5.14 over an 11.44m span). Note: this is *wider* than both the old broken generator's guess (0.55) and an intermediate guess informed by digitized plan data (0.40) — the digitized planform extraction (`charger_planform_exact.json`) turned out to be internally inconsistent (puts max chord at 79% span instead of the root, impossible for this shape) due to a flawed single-global-perpendicular-direction extraction method, so it was not trusted for the final taper; the real area/AR/span spec numbers were used instead.

Applied to both:
- `C:\DevinTools\gen_charger_wing.py` → regenerated `Source/ParamotorSim/Public/ParagliderWingMesh.h` (same `PGPointsPerRib=84`/`PGNumRibs=47` convention, verified closure on every rib before writing, UE5 build succeeds).
- `C:\DevinTools\_rebuild_wing_canopy.py` → rebuilt the `Wing_Inflated` mesh object in `charger_full.blend` in place (backup at `charger_full_before_reflex_fix.blend`), preserving the rig (armature, 112 bones, lines, risers, hardware, logo), existing material slots, and the `RibLE_NN`/`RibTE_NN` vertex-group naming convention. Verified via `_diag_arch_full.py`: TE gap now ~0.0002-0.0009m on every rib (down from 7-19cm), arch is a single smooth symmetric dome.

### Honesty note (carried into code comments)
This is still a physically plausible approximation built from published top-level specs (span, root chord, AR, area, cell count, tip drop), not a verified 1:1 CAD/airfoil reproduction — MacPara has not published the actual rib/airfoil coordinates for the Charger. What changed is that the approximation is now internally consistent (closes properly, smooth arch, area/AR match spec) instead of being broken.

## Session 11 — ParamotorCharacter::Tick Refactor

### ParamotorCharacter.cpp Tick Split
- `Tick` was 616 lines (lines 330-944) — the largest function in the file
- Split into a dispatcher (170 lines) + 9 helper methods:
  1. `UpdateFallState` — ragdoll/get-up logic, returns true to early-out
  2. `UpdateThrustGroundPhysics` — thrust on ground (walking boost, standing brace, wind knockdown)
  3. `UpdateThrustAirPhysics` — thrust in air (falling, not wing flight)
  4. `UpdatePostureLean` — lean from thrust torque balance, mesh quaternion rotation
  5. `UpdateAnimationState` — anim speed, play rate, clip selection
  6. `UpdateFootstepAudio` — step interval timing
  7. `UpdateWindAudio` — wind volume from speed
  8. `UpdateTripRisk` — physical moment balance trip risk accumulation
  9. `UpdateRotationHandling` — final rotation, camera roll, mesh transform
- Air time tracking and landed-after-air-thrust trip check kept in dispatcher (they need early access to `bOnGround`, `bThrustActive`, `IsWingFlying`)
- File went from 1420 to 1334 lines
- Pure structural refactor — all physics formulas and execution order preserved
- Build verified: Succeeded

## Session 10 — Critical Bug Fix + WingClothSim Refactor

### CRITICAL Bug Fix: ApplyWingModel Switch Fall-Through
- `ParamotorPlayerController.cpp`: Every `case 0`-`case 5` and `default` in `ApplyWingModel` had `break;` swallowed by `//` comments (e.g., `// Default trim AoA\t\tbreak;`). This caused switch fall-through — selecting any wing model applied the next model's values too, ending with case 6 (MacPara Charger) values for overlapping fields. Fixed by moving all `break;` statements to their own lines.

### WingClothSim.cpp Refactor — SimulateCloth Split
- `SimulateCloth` was 858 lines (lines 355-1213) — the largest function in the project
- Split into a short dispatcher + 10 helper methods using `FClothContext` struct (same pattern as `WingFlightPhysics.cpp` refactor)
- Helpers (in call order):
  1. `SimulateClothParticles` — Verlet integration + ground collision + grabbed particles
  2. `SimulateClothConstraints` — Constraint solving loop
  3. `UpdateConnectedState` — Connected state cell pressure + position dynamics + Connected→Inflating transition
  4. `UpdateKitingGeometry` — Pilot frame, apparent wind, geometry, line tension, cell pressure, A-line fade, wing mass
  5. `UpdateKitingForces` — All 9 forces (A-line pull, wind catch, cell lift, gravity, pendulum, wind drag, brake damping, air damping, turbulence) + inertial lag + brake surge
  6. `UpdateKitingYawDynamics` — Per-side stall dynamics, yaw from drag differential + weathervane, yawed flight force
  7. `UpdateKitingIntegration` — Force integration, velocity damping, line constraint, ground collision, line tension pulls pilot
  8. `UpdateKitingCanopy` — Pitch from pendulum, ApplyInflatedShape, spring-coupled Verlet dynamics for canopy
  9. `UpdateKitingStateTransitions` — Inflation progress, state transitions (Inflating→Inflated, Inflated→Inflating, Inflated→Connected)
  10. `UpdateKitingRotation` — Wing rotation (yaw/pitch/roll), debug log
- `FClothContext` struct added to `WingActor.h` with fields for all cross-helper data
- File grew from 1238 to 1320 lines (helper headers + dispatcher structure overhead)
- Pure structural refactor — all physics formulas, constants, and execution order preserved
- Build verified: Succeeded (UE 5.8, MSVC 14.44, Win64 Development)

## Code Cleanup (Session 9 — Dead Code + Null Init + HUD Wind Fix)

### Dead Code Removed
- `ParamotorActor.h`: Removed `WingTargetPos` and `bWingConnected` (never read/written, `UpdateWingTarget` is a no-op)
- `WingActor.h`: Removed `GetWingName()` and `IsGroundRunActive()` (never called)
- `AirfoilPolar.h`: Removed 4 unused inline functions: `EffectiveCamberPercent`, `SchrenkDistribution`, `LocalCL`, `RollMomentFromBrakeAsym` (declared but never called from any .cpp)

### HUD Wind Compass Fix
- `UW_FlightHUD.h`: Removed static `WindDirection = 45.0f` and `WindSpeed = 8.0f` member variables (were never updated, showed stale defaults)
- `UW_FlightHUD.cpp`: Wind compass now reads live wind speed and direction from `WingActor->GetCurrentWindSpeedMs()` and `WingActor->GetWindDirection()`, falling back to `UParamotorGameInstance->WindSpeed`
- Wind direction vector converted to compass heading via `atan2(Y, X) * RAD_TO_DEG`

### Null Initialization (Safety/Consistency)
Added `= nullptr` in-header initializers to all UPROPERTY pointer members that were missing them:
- `ParamotorActor.h`: `Root`, `InteractionSphere`, `PromptWidget`
- `WingActor.h`: `Root`, `CanopyMesh`, `BagMesh`, `LineMesh`
- `ParamotorGameMode.h`: `BirdsAudio`, `OceanAudio`
- `ParamotorPlayerController.h`: `PauseMenuInstance`, `FlightHUDInstance`, `SpawnedParamotorActor`, `bIsPaused`
- `UW_FlightHUD.h`: All 28 widget pointers
- `UW_InteractPrompt.h`: `PromptText`
- `UW_MainMenu.h`: `GameInstanceRef`, 5 text labels, 5 sliders
- `UW_PauseMenu.h`: All 22 widget/button/border pointers, `bSigWingsExpanded`
- `WindsockActor.h`: `PoleMesh`, `SockMesh`, `SmoothedYaw`, `SmoothedInflation`
- `ParamotorCharacter.h`: 10 `TObjectPtr` members (Camera, SpringArm, HeadMesh, OutfitMesh, ShoesMesh, WalkIdleAnim, WalkAnimAsset, JogAnimAsset, CurrentAnim)

### Build Verification
- Build succeeded after all changes (UE5 5.8, MSVC 14.44, Win64 Development)
- Only pre-existing `UW_PauseMenu.cpp` `UButton::WidgetStyle` deprecation warnings remain

## Code Refactor (Session 8 — WingFlightPhysics.cpp Split)

### UpdateFlight Split into 9 Helpers
- `UpdateFlight` was ~1068 lines (lines 24-1092 of `WingFlightPhysics.cpp`), the largest single function in the project
- Split into a slim dispatcher + 9 helper methods, each handling one contiguous section of the original function
- File reduced from 1398 to 1009 lines
- Helpers called in exact original order to preserve data flow (later sections read values from earlier ones)
- Added `FFlightContext` nested struct in `WingActor.h` to hold per-frame shared locals (Pawn, Char, Vel, ApparentWind, AoA, CL/CD, forces, accelerations, etc.)
- Helpers:
  1. `UpdateFlightEnvironment` — wind, altitude, air density, wind gradient, turbulence, cell pressure, thermals, turbulence AoA, brake lag
  2. `UpdateAerodynamicCoefficients` — brake-to-AoA, polar lookup (CL/CD), ground effect
  3. `UpdateCollapseDetection` — asymmetric/frontal collapse triggers, collapse amounts, parachutal stall
  4. `UpdateLineTensionAndStretch` — per-side lift forces, cell pressure coupling, A/B/C/D line tension, line stretch
  5. `UpdateFlightForces` — per-side drag, thrust, glide ratio, total mass, trim speed, roll/yaw moments, accelerations
  6. `UpdatePendulumAndPitch` — trim speed stability, pendulum physics, pilot pitch/seat transition
  7. `UpdateYawRollDynamics` — weight shift, wing inertia, prop torque, yaw/roll integration, spiral dive, Dutch roll
  8. `UpdatePilotOrientation` — wing surge/pitch oscillation, roll pendulum, pilot rotation, velocity rotation, stall log
  9. `ApplyFlightForces` — banked turn centripetal, thrust/drag/lift/gravity forces, NewVel integration, safety caps, landing, move pilot, wing position, flight log
- No physics formulas changed — pure structural refactor
- Pre-existing `WingFlightPhysics_backup.cpp` moved out of Source directory (was causing LNK2005 duplicate symbol errors)
- Backups stored at project root: `WingFlightPhysics_backup.cpp`, `WingFlightPhysics_cpp.bak`
- Build verified: compiles and links successfully

## Code Refactor (2026-09-06 Session 7 — Code Review + Cleanup)

### Critical Bug Fixes
- Zero-initialized `CellPressure[]`, `CellFill[]`, `CellLift[]`, `CellDrag[]` arrays in `AWingActor` constructor (raw C arrays are not default-initialized)
- Removed all `static float` local timers in member functions — replaced with instance member variables (`InflLogTimer`, `RiserLogTimer`, `EntryLogTimer`, `RisersLogTimer`, `PawnLogTimer`, `RollDebugTimer`). Static locals were shared across all `AWingActor` instances.
- Clamped line load fractions (`LoadFractionA/B/C/D`) to prevent negative tension at extreme brake input
- Removed dead `GustTotal` variable in `UpdateDynamicWind` (was always 0.0f)
- `nullptr`-initialized all `UStaticMeshComponent*`, `USceneComponent*`, `UAudioComponent*` members in `ParamotorActor.h`

### Performance Fixes
- Cached line mesh material (`LineMaterial`) in `BeginPlay` — was calling `LoadObject` every frame in `UpdateLineMesh`
- Added `GetCachedGroundZ()` with per-tick caching — avoids multiple `LineTraceSingleByChannel` calls per frame for the same position
- Cached `UParamotorGameInstance*` pointer in `BeginPlay` — avoids repeated `GetWorld()->GetGameInstance<>()` calls per frame
- Ground Z cache invalidated at start of each `Tick()`

### Code Deduplication
- Extracted `ComputeBladeElement()` helper in `ParamotorActor.cpp` — eliminates ~105 lines of duplicated propeller blade-element integration between `GetThrustForceN()` and `GetPropTorqueNM()`
- Extracted `UpdatePropMeshRotation()` helper — eliminates duplicated prop mesh rotation code between running and spool-down paths
- Replaced magic numbers `2.23694f` with `MPH_PER_MS` constant and `1.225f` with `AIR_DENSITY_SEA_LEVEL` across `WingGroundHandling.cpp`, `WingFlightPhysics.cpp`, `UW_FlightHUD.cpp`

### Dead Code Removed
- Removed `WingMeshData.h` (1913 lines) — completely unused, superseded by `ParagliderWingMesh.h`

### A-Arm Pivot Hierarchy (New)
- Built proper mechanical component hierarchy: `Frame → Stud → AArmPivot → A-arm mesh + Ring + Carabiner + Riser + BrakeToggle + QuickLinks`
- A-arms pivot to follow wing position (`UpdateAArmPivots()` called in `Tick()`)
- `WingActor::Tick()` notifies paramotor of wing position via `UpdateWingTarget()`
- Procedural riser cylinders, carabiner spheres, and brake toggle spheres removed from line mesh (real meshes handle visualization)

### Wing Mesh Rebuild
- Rebuilt wing mesh from real MacPara Charger 1 25M data: 46 cells (47 ribs), reflex airfoil, 18% thickness
- Flat top surface with peak at 25% chord, steep LE bottom drop, subtle TE reflex
- Anhedral arch (30cm center above tips), wingtip drop (90cm stabilizers)
- Generated by `C:\DevinTools\gen_charger_wing.py`

### Kiting Physics Improvements
- Reduced artificial velocity damping from 3.0/s to 0.5/s (was killing momentum in 0.3s)
- Increased inertial lag force to full magnitude (was scaled by mass ratio ~1/6)
- Wing now overshoots and pendulums naturally when pilot stops running

### Function Splitting (Session 7 continued)
- `ParamotorActor::Tick()` split into 6 helpers: UpdateCranking, UpdateEngineRPM, UpdateEngineTemp, UpdateFuel, UpdateEngineAudio, UpdateEngineSpoolDown
- `WingGroundHandling::UpdateInflation()` split: extracted `UpdateKitingCollapses()` and `ComputeGroundWind()`
- `WingFlightPhysics::UpdateFlight()` split: extracted `ComputeThermalLiftMs()`, `HandleLanding()`, `UpdateWingPositionFromLines()`, `ApplyVelocitySafetyCaps()`
- `UpdateFlight` now ~870 lines (down from 1315)

### Additional Cleanup (Session 7 continued)
- Moved `NACA4415_Polar[]` from `AirfoilPolar.h` to `AirfoilPolar.cpp` (eliminates per-translation-unit duplication)
- Replaced `GetGroundZ()` with `GetCachedGroundZ()` in `WingClothSim.cpp` (4 calls) and `WingLinesSim.cpp` (2 calls)
- Replaced duplicated particle shift loop in `WingLinesSim.cpp` with `ShiftAllParticles()` call
- `SetMaterial` calls in `WingVisuals.cpp` now only apply once (not per-frame)
- Added null guards for `CanopyMesh`, `LineMesh`, `BagMesh` in `WingVisuals.cpp`
- `nullptr`-initialized `SpawnedWingActor` in `ParamotorPlayerController.h`
- `nullptr`-initialized `FootstepAudio`, `WindAmbientAudio`, `FallAudio` in `ParamotorCharacter.h`
- Removed dead variables: `FootstepInterval`, `RagdollVelocity`, `EffectiveJumpZ`
- Cached `GetActorLocation()` in `SimulateCloth` particle loop (was 2 calls per particle per frame)
- Named constants added: `RAD_TO_DEG`, `PERCENT_FACTOR`
- Replaced `180.0f / PI` → `RAD_TO_DEG` (39 instances), `PI / 180.0f` → `PI_DIV_180` (15 instances)
- Replaced `9.81f` → `GRAVITY_MS2` (16 instances), `736.0f` → `MAX_THRUST_N`, `52.5f` → `RISER_LENGTH_CM` (7 instances)
- Replaced `100.0f` → `CM_PER_M` for unit conversions, `100.0f` → `PERCENT_FACTOR` for percentage conversions

### Deferred (Needs Testing Session)
- Further splitting of `UpdateFlight()` (~870 lines remain, tightly coupled)
- Move `ParagliderWingMesh.h` data (15k lines) to runtime-loaded asset
- Extract left/right line setup helper in `WingLinesSim.cpp`
- Data-drive wing model specs in `ParamotorPlayerController.cpp` (currently a long switch)

## Real Physics Overhaul (2026-09-05 Session 6 — Brake Lines + Line Tension)

### Real Brake Line Physics
- Replaced magic BrakeToAoA curve with physical trailing edge deflection
- Brake handle pulls brake line → pulls trailing edge down → increases camber
- Trailing edge deflection: θ_TE = asin(pull / chord), where pull = 0-40cm, chord = 200cm
- At 100% brake: θ_TE = 11.5°, effective AoA increase = 9.2° (×0.8 for fabric flex)
- The non-linear "progressive feel" comes naturally from the asin() curve
- Removed double-counting: was applying BOTH CLMax×(1+brake×0.30) AND polar camber boost
- Now camber effect is handled entirely by LookupAirfoilPolarWithCamber()
- Added TrailingEdgeDeflectionDeg() and EffectiveCamberPercent() to AirfoilPolar.h
- Camber change: 4% (NACA 4415) → 8% (NACA 8415) at full brake, from real XFOIL data

### Real Line Tension Per Group (A/B/C/D)
- Added per-group line tension computation during flight
- Load distribution from real paraglider manufacturer data:
  - A lines: 40% of total load (leading edge, highest pressure)
  - B lines: 30% (mid-chord)
  - C lines: 20% (rear)
  - D lines: 10% (trailing edge, lowest pressure)
- Brake input shifts load rearward (C/D carry more when brakes pulled)
- Line stretch from tension: stretch = (tension / stiffness) × length
  - Paracord stiffness: ~5000 N for 2% stretch on 7m line
  - A lines stretch most (highest tension)
- Line stretch affects AoA (A stretching more than D → leading edge drops → AoA up)
- Brake line tension: ~50N at full brake (real pilot pull force)
- Line tension logged to flight log for debugging
- Visual line sim uses per-group stretch for line positioning

### What This Means
- Brake response is now driven by real geometry (trailing edge deflection)
- No more double-counting of camber effects
- Line loads are physically distributed (A carries 4x the load of D)
- Line stretch is computed from real material properties
- Brake input shifts load rearward (real physics)
- Foundation for future wear/fatigue system (A lines wear fastest)

## Real Physics Overhaul (2026-09-05 Session 5 — von Kármán + Blade Element)

### Von Kármán Turbulence Model
- Replaced random white noise turbulence with the von Kármán spectral model
- The von Kármán model is the FAA/USAF standard for atmospheric turbulence
- Implementation: Dryden approximation (filtered white noise through first-order
  filters shaped to match the von Kármán spectrum)
- Three independent axes: longitudinal, lateral, vertical
- Each axis has its own filter state (TurbLongState, TurbLatState, TurbVertState)
- Filter time constant: τ = L / V, where L = scale length (150-500m by altitude)
- Turbulence intensity σ scales with gust amplitude
- Scale length increases with altitude (bigger eddies higher up)
- Produces turbulence with proper temporal correlation (not independent frame-to-frame)
- Gusts build up and decay naturally (not instant on/off)
- Direction shift now includes lateral turbulence influence
- Flight physics uses the 3-axis states for realistic bumps, rolls, and AoA variations

### Blade Element Propeller Theory
- Replaced simple thrust curve (T = throttle × 736N × efficiency) with real
  blade element theory integration
- Each blade is divided into 20 elements along its length
- For each element:
  - Tangential velocity: V_t = ω × r (from prop rotation)
  - Axial velocity: V_a = airspeed
  - Relative velocity: V_rel = √(V_a² + V_t²)
  - Inflow angle: φ = atan(V_a / V_t)
  - Blade pitch angle: β(r) — linear twist from 30° at root to pitch angle at tip
  - Angle of attack: α = β - φ
  - Lift: dL = 0.5 × ρ × V_rel² × c × CL(α) × dr (thin airfoil theory: CL = 2πα)
  - Drag: dD = 0.5 × ρ × V_rel² × c × CD(α) × dr (CD = 0.02 + 0.05α²)
  - Thrust: dT = dL × cos(φ) - dD × sin(φ)
- Total thrust: T = N_blades × Σ dT
- Engine power limit: T ≤ P × η / V (can't exceed engine power at high speed)
- Propeller specs (Moster 185):
  - Diameter: 130cm (51")
  - 2 blades, 8cm average chord
  - Pitch: ~50cm
  - Max prop RPM: 3023 (7800/2.58)
  - Linear twist: 30° at root to ~12° at tip
- Stall model: blades stall at α > 15° (flow separates, CL drops, CD rises)

### What This Means
- Thrust now varies realistically with airspeed, RPM, and altitude
- At static (zero airspeed): full blade element thrust (~736N at full throttle)
- At cruise (10 m/s): thrust decreases naturally from blade element physics
- At high speed: engine power limits thrust (can't exceed P×η/V)
- Propeller stall at very high airspeed (blades can't generate lift)
- Turbulence now has realistic frequency content and temporal correlation
- Turbulence builds up and decays naturally instead of instant on/off

## Real Physics Overhaul (2026-09-05 Session 4 — Real Lines)

### Real Line Wind Drag
- Each line particle now experiences wind drag: F = 0.5 × ρ × V² × d × L × CD
- Line diameter: 0.8mm, segment length: ~2m, CD=1.0 (cylinder in crossflow)
- Wind drag applied in wind direction during Verlet integration
- Lines visibly catch wind and drift (not just visual Lerps)

### Real Line Tension During Kiting
- Added KiteVerticalVel — wing has real vertical velocity during kiting
- Gravity pulls wing down: F = m × g (wing + apparent air mass)
- Wind lift pushes wing up: F = 0.5 × ρ × V² × S × CL × CellFillFactor
- Net force integrates into vertical velocity: v += a × dt
- Line tension constraint: wing can't exceed line length from carabiner
- When line is taut and wing tries to fall, tension stops it
- When line is slack (wing closer than line length), wing is free to fall
- Wing now FALLS when there's not enough wind (real physics, not fake force)

### Line Mass in Flight
- Added line mass (1.5 kg) to total flying mass
- Was: PilotWeight + WingWeight
- Now: PilotWeight + WingWeight + LineMass (1.5kg)
- Affects acceleration, glide ratio, and pendulum dynamics

### What's Still Not Real (Honest Assessment)
- Line particles are still repositioned as Lerps during flight (not Verlet)
- Individual line tension is not computed during flight (pendulum model handles this)
- Canopy cloth is procedurally shaped, not physics-driven
- Turbulence is random noise, not von Kármán model
- Cell inflation is not CFD (wind-dependent fill rate approximation)
- Propeller is a thrust curve, not blade element theory

## Real Physics Overhaul (2026-09-05 Session 3 — "Make it real")

### Real Airfoil Polars (XFOIL Data)
- Created `AirfoilPolar.h` with actual XFOIL 6.96 polar data for NACA 4415 at Re=500,000
- NACA 4415 is a thick (15%), cambered (4%) airfoil similar to paraglider sections
- Polar covers AoA from -15° to +19° with real CL and CD at each point
- Key values from real data:
  - CL0 = 0.41 at zero AoA
  - CLMax = 1.47 at 15° AoA
  - Best L/D = 97 at 5.75° (airfoil only, not full system)
  - CD0 = 0.0082 at zero AoA
- Replaced the simplified linear CL/AoA model with real polar lookup
- Binary search interpolation between data points
- Post-stall behavior comes from real XFOIL data (not a formula)

### Camber Effect from Brakes (Real Aerodynamics)
- Pulling brakes increases wing camber → higher CLMax and CL0
- At 100% brake: CL increases by +0.30, CD increases by +0.02
- This is what makes landing flares work (real physics, not a hack)
- Removed the fake "flare boost" timer system entirely
- The camber effect is built into the polar lookup function

### Lifting Line Theory
- Implemented Schrenk approximation for spanwise lift distribution
- distribution(y) = (elliptical + rectangular) / 2
- Used for:
  - Tip stall detection (tips stall 2° before center)
  - More realistic asymmetric collapse from turbulence
  - Roll moment correction from spanwise lift distribution
- Functions: `SchrenkDistribution()`, `LocalCL()`, `RollMomentFromBrakeAsym()`

### Real Kiting Gravity (Lift vs Weight)
- Replaced fake "sink force" with real lift-vs-weight gravity model
- Wing has mass (canopy + apparent air mass = 3x wing weight)
- Gravity pulls wing down: F = m × g
- Wind generates aerodynamic lift: F = 0.5 × ρ × V² × S × CL
- If lift < weight: wing sinks (real physics, not arbitrary force)
- If lift > weight: wing rises
- Cell deflation when apparent wind < 2 m/s (cells can't stay pressurized)
- KiteSinkHeight accumulates from net vertical force (lift - gravity)

### What's Now Real vs Still Approximate
**Real (from data/physics):**
- Lift/drag equations: F = 0.5 × ρ × V² × S × CL/CD
- Airfoil polar data: real XFOIL NACA 4415 at Re=500,000
- Induced drag: CL²/(π × AR × e)
- Ground effect: 1 - exp(-h/(b/2))
- Pendulum dynamics: T = 2π√(L/g)
- Line drag: 0.06 m², CD=1.0 (cylinder crossflow)
- Camber effect: real CLMax increase from brake input
- Kiting gravity: real lift-vs-weight model
- Lifting line: Schrenk spanwise distribution
- Thrust: momentum theory with airspeed-dependent efficiency

**Still approximate (but physics-based):**
- Oswald efficiency (0.90) — in the right range for paragliders
- Apparent mass (3x wing weight) — in the right ballpark
- Reynolds number effects — fixed at Re=500,000 (real wings vary)
- 3D wing effects — 2-panel model (left/right) with lifting line correction
- Turbulence — random noise (not von Kármán model)
- Cell inflation — not CFD, but wind-dependent fill rate

**Not simulated (would need CFD/cloth):**
- Actual airflow through cells
- Real cloth/canopy deformation physics
- Individual line physics (lines are visual only)
- Wing-structure aeroelasticity

## Major Realism Overhaul (2026-09-05 Session 2)

### Glide Ratio Fix
- All wing CL0 values halved (1.0 → 0.50) — was generating 2x too much lift at trim
- CLMax reduced (1.8 → 1.5) — realistic for beginner EN-A wings
- CD0 adjusted (0.035 → 0.037) — matches real paraglider profile drag
- Base AoA changed from 2.5° to 3.0° — realistic trim AoA
- Result: L/D ≈ 8.2:1 with motor+gear+line drag (was unrealistically high)
- At trim (10.5 m/s): CL ≈ 0.67, L ≈ W (level flight, not climbing)

### Line Drag
- Added line drag to total flight drag: 0.06 m² area, CD=1.0 (cylinder crossflow)
- ~150 lines, avg 0.8mm diameter, avg 4m length
- Adds ~5% to total system drag — affects glide ratio
- Also added to kiting aerodynamic force

### Pilot Drag
- Increased from 0.3 m² to 0.4 m² (realistic for seated pilot with motor)
- CD increased from 0.4 to 0.5 (bluff body)

### Flare Boost (Landing)
- Temporary CL boost when brakes > 40%: peaks at +0.5 CL over 0.5s, decays over 3s
- This is what makes landing flares work — wing "catches" you before stalling
- After 3s or brake release: boost ends, wing stalls (parachutal stall)
- Variables: FlareBoostTimer, FlareBoostAmount, bFlareActive, LastTotalBrake

### Ground Effect
- Induced drag reduced near ground: factor = 1 - exp(-h / (b/2))
- At 10m AGL: 14% reduction. At 5m: 37%. At 1m: 82%
- Applied to induced drag only (CD - CD0), not profile drag
- Makes landing flare more effective and takeoff easier

### Inflation Difficulty
- Added CellFillFactor (0-1): cells must fill with air before wing can rise
- Fill rate depends on apparent wind (wind + running speed)
- At 6 m/s wind: fills in ~3s. At 3 m/s: ~6s. Below 2 m/s: barely fills
- Added apparent mass: wing + air inside = 3x wing mass when full
- Removed constant BaseForce (was 120, now scales with cell fill)
- Wind force now quadratic (V²) instead of linear (V) — more realistic
- Wing deflates when A-lines released or no wind

### Kiting Realism
- Wing now falls when insufficient apparent wind (< 3 m/s)
- KiteSinkHeight accumulates when wind deficit, restores when wind returns
- Gravity always pulls wing down slightly (15 cm/s² baseline)
- Cell deflation when apparent wind < 3 m/s
- Line drag added to kiting aerodynamic force

### Physics Knowledgebase
- Created docs/PARAMOTOR_PHYSICS.md with detailed physics reference
- Covers: glide ratio, flare, inflation, kiting, line drag, thrust, speed envelope,
  pendulum, turn dynamics, collapse, ground effect, wind, weight/loading, UE5 units

## Realism Refactor (2026-09-05)

### Physics Constants
- All physics constants are now in `WingActor.h` as `static constexpr`:
  - `GRAVITY_CM_S2 = 981.0f` (was inconsistent: 600 in cloth, 980 in flight)
  - `CM_PER_M = 100.0f`, `MPH_PER_MS = 2.23694f`, `AIR_DENSITY_SEA_LEVEL = 1.225f`
  - `LINE_LENGTH_CM = 700.0f`, `MAX_KITE_OFFSET_CM = 350.0f`, `MAX_THRUST_N = 736.0f`
  - `MIN_TAKEOFF_AIRSPEED_MS = 4.0f` (was 3.0, too low)
- Use these constants instead of magic numbers.

### Common Helpers (extracted from duplicated code)
- `GetPilotFrame()` — returns pilot forward/right/velocity/speed (was duplicated in 4 places)
- `UpdateDynamicWind(DeltaTime, GustAmplitude)` — unified wind gust/turbulence update (was duplicated in UpdateInflation + UpdateFlight)
- `ShiftAllParticles(Shift)` — shifts all particles by a world delta (was duplicated in 3 places)
- `ApplyInflatedShape(DeltaTime)` — now accepts DeltaTime parameter (was fetching internally)
- Cached canopy bounds in `CachedCanopyMin/Max` (was recomputing every frame)

### Critical Bug Fixes
- **World/local coordinate bug**: `SimulateCloth` ground collision was writing world Z to local particle Z. Fixed by converting `GZ - ActorZ` to local space.
- **Wind speed integration bug**: `CurrentWindSpeed += GustTotal * DeltaTime` was accumulating gusts as accelerations. Fixed to assign `CurrentWindSpeed = Base + GustTotal + Turbulence` each frame.
- **Total mass error**: `TotalMassKg = PilotWeight` ignored paramotor + wing weight (~30% error). Fixed to `PilotWeight + WingModel.WingWeight`.
- **Trimmer bar bug**: HUD trimmer center marker had dead ternary `(i == BarPos) ? "|" : "|"`. Fixed to show `*` when marker is at center.

### Kiting Pendulum Physics (mass-based)
- Restoring force now uses real aerodynamic force: `F = 0.5 * rho * V² * S * (CL+CD)`
- Effective mass includes apparent mass: `m_eff = m_wing * 3.5` (canopy + dragged air)
- Crosswind force uses proper drag: `F = 0.5 * rho * V_cross² * S * CD`
- Brake forces use differential drag based on aerodynamic coefficients
- Damping derived from wing area and air density (not arbitrary 0.3)
- Weathervaning uses proper yaw moment: `M = C_N_beta * q * S * b * beta`

### Flight Aerodynamics
- **Thrust**: Now uses `ParamotorActor::GetThrustForceN(AirspeedMs)` which models:
  - RPM² scaling (propeller momentum theory)
  - Airspeed-dependent efficiency (thrust decreases with speed)
  - Real Moster 185 redline (7800 RPM, was 9000)
- **Prop torque**: Now based on engine power output, not throttle position
- **Thermals**: Added thermal lift model based on time of day, cloud cover, temperature, and altitude
- **Ground wind gradient**: Wind near ground reduced by surface friction (power law)

### ParamotorActor
- New propulsion API: `GetThrustForceN(AirspeedMs)`, `GetPropRPM()`, `GetEnginePowerW()`
- Moster 185 specs: 2.58:1 reduction ratio, 18.5 kW peak power, 7800 RPM redline
- Propeller RPM = EngineRPM / 2.58

### ParamotorCharacter
- Thrust now uses real `ParamotorActor::GetThrustForceN()` when paramotor is equipped
- Falls back to simple throttle-based estimate when no paramotor actor

### HUD
- Wind speed display now reads live from `WingActor::GetCurrentWindSpeedMs()` (was stale default)
- Trimmer bar center marker fixed

### PlayerController
- Removed dead `SpawnMeshSet` function (was never called)

## Backup Discipline
- **Before any major source change**: create a zip backup using the `zip-backup` skill.
- **Periodically during long sessions**: create a backup after every few significant edits.
- Backups are saved to `C:\DevinTools\backups\ParamotorSim_YYYY-MM-DD_HH-MM.zip`.
- Always kill UnrealEditor processes before backing up to avoid file locks.
- A source-only backup (excluding Content) can be done with: `7z a -tzip <path> "C:\Users\sdean\Documents\Unreal Projects\ParamotorSim\Source" "C:\Users\sdean\Documents\Unreal Projects\ParamotorSim\*.uproject" "C:\Users\sdean\Documents\Unreal Projects\ParamotorSim\AGENTS.md"`

## Build and Run
- Build editor (with Live Coding disabled):  
  `C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat ParamotorSimEditor Win64 Development "C:\Users\sdean\Documents\Unreal Projects\ParamotorSim\ParamotorSim.uproject" -NoLiveCoding`
- Run game windowed:  
  `C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe "C:\Users\sdean\Documents\Unreal Projects\ParamotorSim\ParamotorSim.uproject" -game -NoLiveCoding -ResX=1280 -ResY=720 -Windowed -NoSplash -log`

## Character / Animation
- Active body mesh: `MHC_Frey_FullBody` (`/Game/Frey_Exported/MHC_Frey_FullBody`)
- Skeletal mesh uses `metahuman_base_skel`
- Single-node animation mode must be set in `BeginPlay` before playing: `GetMesh()->SetAnimationMode(EAnimationMode::AnimationSingleNode);`
- Plugin locomotion clips used directly:
  - Idle: `/MetaHumanCharacter/Optional/Animation/UEFNAnimPreset/Locomotion/AS_MH_Neutral_Stand_Idle_Loop`
  - Walk: `/MetaHumanCharacter/Optional/Animation/UEFNAnimPreset/Locomotion/AS_MH_Neutral_Walk_Loop_F`
  - Run/Jog: `/MetaHumanCharacter/Optional/Animation/UEFNAnimPreset/Locomotion/AS_MH_Neutral_Run_Loop_F`
- Shift is mapped to `Run` action (`DefaultInput.ini`) and is used to request a jog (no separate sprint).

## Input
- `MoveForward`: W/S
- `MoveRight`: A/D
- `Turn`: MouseX
- `LookUp`: MouseY
- `Run`: LeftShift
- `Interact`: F (equip paramotor when near, connect risers to wing when near)
- `StartEngine`: P (hold to crank electric starter; engine only catches if throttle > 0; press P while running to stop)
- `ThrottleUp`: Up arrow (hold to increase throttle; also needed to start engine)
- `ThrottleDown`: Down arrow (hold to decrease throttle)
- `LiftALines`: R (hold to lift A-lines and inflate wing; release to stop lifting)
- `TrimmerFast`: = (press to trim faster; each press moves trimmer +0.5 toward full fast)
- `TrimmerSlow`: - (press to trim slower; each press moves trimmer -0.5 toward full slow)
- `Pause`: Escape (bound in `ParamotorPlayerController::SetupInputComponent` via `BindKey`)

## Engine Realism
- Engine states: `Off` → `Cranking` → `Running`
- Hold P to crank the electric starter (plays realistic starter sound: clunk, motor whir, compression pulses, sputtering)
- Engine will NOT start unless throttle is being given (Up arrow) while cranking
- Starter gives up after 5 seconds of cranking without throttle
- Propeller does NOT spin at idle (throttle=0); spins proportional to throttle
- Engine idle sound is a 2-stroke at 2500 RPM; pitch and volume scale up with RPM to ~9000 RPM
- Exhaust sound plays alongside engine sound, also pitch-shifts with RPM
- Press P while running to stop the engine

## Trip/Fall System
- When the paramotor is equipped and the engine is running with high throttle (>0.3) while the character is moving on the ground, trip risk accumulates
- Running (Shift) and exceeding max walk speed increase risk further
- When risk exceeds a random threshold (1.0-1.5), the character trips and falls face-down
- Fall plays a stumble + body impact + grunt sound
- Character is immobile for 4 seconds, then automatically gets back up
- Trip risk decays when not thrusting or not on the ground

## Wing Physics
- `AWingActor` uses a `UBoxComponent` physics body (flat box, 1000x546x30cm) with REAL gravity and physics from spawn
- The skeletal mesh is visual-only (no collision/physics), attached to the physics box
- Wing spawns at FULL SIZE, laid flat on the ground (no scaling, no crinkle)
- No bag/box - the actual wing mesh is visible from spawn
- Press F near the wing (with paramotor equipped) to connect risers
- Hold R to lift A-lines (leading edge) - this starts inflation like a real paramotor
- Inflation is driven by A-line lift + wind + line tension (no I key)
- Once inflated, wind force from `UParamotorGameInstance` (WindSpeed, WindGusts) pushes the wing
- Wind force includes aerodynamic drag, turbulence, lift, and torque
- Wing has 5kg mass with high damping (fabric drag simulation)
- Skeletal mesh collision is disabled (physics is on the box component, not the mesh)
- On-screen instructions in the HUD guide the player through each step

## Game Audio
- **Engine sounds**: realistic 2-stroke single-cylinder (starter, idle, exhaust) — pitch shifts with RPM
- **Footsteps**: grass and dirt footstep sounds, triggered by walking/jogging speed, with random pitch/volume variation
- **Wind ambient**: continuous breeze with gusts, volume increases with movement speed (apparent wind)
- **Birds ambient**: distant coastal bird calls (seagull-style)
- **Ocean waves**: distant wave pattern with periodic swells
- **Wing inflation**: procedural ripstop nylon crinkle + air rush
- **Fall sound**: stumble + body impact + grunt + dust settling
- All sounds under `/Game/Sounds/`

## Pause Menu
- `UW_PauseMenu` — C++ widget built dynamically in `NativeOnInitialized` using `WidgetTree->ConstructWidget`.
- **MSFS-style design**: wide panel (920x640) with left sidebar navigation and right content area.
- Left sidebar: PARAMOTOR SIMULATOR logo, 3 nav buttons (MAIN, SPAWN, OPTIONS) with blue accent bar on active tab, ESC hint at bottom.
- Right content area: scrollable, with large page titles, section headers with accent bars, and card-style buttons.
- Color palette: deep navy backgrounds, MSFS blue accent (0.15, 0.55, 0.95), dim text labels, bright white headings.
- `AParamotorPlayerController::TogglePauseMenu()` handles pause/unpause, widget creation/removal, input mode, and mouse cursor.
- Esc is handled two ways: `BindKey` in the controller (works when unpaused) and `NativeOnKeyDown` in the widget (works when paused, since `SetPause(true)` blocks controller input).
- The `MetaHumanVisual` child actor component (BP_MHC_Frey) was removed from `ParamotorCharacter` — it caused a clothed T-pose clone attached to the player's foot.

## Devin Project Skills
Located in `.devin/skills/`. Invoke with `/build`, `/run-game`, `/inspect-asset <path>`, `/zip-backup`.

## Python Helpers
- `C:\DevinTools\ue.py` — reusable build/run/screenshot helper for ParamotorSim.

## Canopy Material
- Created via `make_canopy.py` and saved to `/Game/Materials/M_Canopy`. It is an unlit, two-sided procedural canopy pattern with red/white/blue sections and rib lines, ready for a wing mesh.

## Paramotor Flight Physics Notes
Based on research and implemented in `AWingActor`:
- Forces: **Weight** (down), **Lift** (up, from AoA + airspeed), **Thrust** (forward, propeller), **Drag** (rearward, from AoA + brakes).
- **Airspeed is naturally stable** due to speed/pendulum stability; climb is controlled by throttle.
- **Glide ratio** of a modern paramotor wing is roughly 9:1 (9 ft forward per 1 ft down).
- **Critical angle of attack** (stall) is around 15 degrees.
- **Trimmers / risers** change the profile and angle of attack for faster or slower flight.
- **Brakes / steering** pull the trailing edge down, causing drag on that side and turning the wing.
- **Launch**: the pilot runs forward; the wing inflates as air fills the ram-air cells and forms an airfoil. Headwind reduces the required ground speed.

### Wing Physics Implementation (`AWingActor`)
- **Physics body**: `UBoxComponent` with real gravity, real mass (5 kg), free rotation. No angular lock.
- **Visual mesh**: Skeletal mesh attached to physics box, no collision/physics. Pitch animates -90° (flat) → 0° (upright) during inflation.
- **Aerodynamic model**: Lift and drag computed from angle of attack (AoA) and airspeed using real paraglider polar coefficients (CL0=0.8, CLMax=1.6, CD0=0.045). Induced drag computed from CL²/(π·AR·e).
- **Angle of attack**: Computed from apparent wind vector relative to wing chord plane. Brake input increases effective AoA.
- **Stall/collapse**: AoA > 15° with heavy brake → frontal collapse. Asymmetric brake at high AoA → asymmetric collapse. Recovery requires releasing brakes + 2s wait.
- **Pendulum stability**: Pilot hangs below wing via virtual spring (riser lines). Restoring force pulls wing directly above pilot. Damped harmonic oscillator (0.4 Hz, 0.6 damping ratio).
- **Wind gradient**: Wind speed increases with altitude using power law V(h) = V_ref · (h/h_ref)^0.15.
- **Brake steering**: Differential brake (Q/E) creates yaw torque. Symmetric brake creates drag + flare lift. Both brakes deep = flare.
- **Yaw follow**: Wing naturally aligns with pilot's facing direction, scaled by airspeed (more authority at higher speed).
- **Line tension**: Spring + damping connecting wing to pilot's clip point. Over-stretch pulls wing toward pilot and can pull pilot in high wind.
- **Ground inflation**: Hold R to lift A-lines → upward + forward force → progressive inflation (0→1) → wing pitches from flat to upright → transitions to Inflated state at 70% momentum.
- **States**: LaidOut → Connected → Inflating → Inflated → Collapsed → (recovery) → Inflating.
- **Telemetry**: Airspeed (km/h), glide ratio (L/D), AoA (degrees), CL, CD, altitude (m) — available for HUD.

## Session 27 — Free Flight World-Selection Screen

### Overview
Built the Free Flight world-selection page: `LV_World`, `AWorldMapGameMode`, `AWorldMapPawn`, and `UUW_FreeFlightMenu`.

### New assets and classes
- `Source/ParamotorSim/Private/WorldMapPawn.cpp/.h` — spring-arm camera pawn; auto-rotates the globe, zooms with mouse wheel, click-drag orbits.
- `Source/ParamotorSim/Private/WorldMapGameMode.cpp/.h` — spawns `AWorldMapPawn`, sets `GameAndUI` input mode, builds the textured globe in `SetupGlobe()`.
- `Source/ParamotorSim/Private/UW_FreeFlightMenu.cpp/.h` — C++ UMG menu with Garage/Settings/Weather/World tabs, Weather sliders, Fly and Back buttons, transparent world-map overlay.
- `Content/Maps/LV_World` — world-selection level.
- `Content/Textures/T_WorldMap.png` — world-map texture imported and applied to the globe sphere.

### Key fix: UI input mode
- `FInputModeUIOnly` calls `GameViewportClient.SetIgnoreInput(true)`, which blocks the viewport from receiving mouse-wheel and click-drag events.
- Switching to `FInputModeGameAndUI` with `SetHideCursorDuringCapture(false)` lets the globe receive wheel zoom and click-drag orbit while Slate UI buttons still consume clicks when hovered.

### Verification
- Main menu `Free Flight` opens `LV_World`.
- `LV_World` shows a real textured globe with grid overlay and `FREE FLIGHT` UI.
- All tab buttons switch the `WidgetSwitcher` content area.
- `FLY FROM WESTERLY` opens `L_Westerly` and spawns the pilot at Ninigret.
- `< BACK` returns to `L_MainMenu`.
- Globe auto-rotates, mouse wheel zooms, click-drag orbits.
- Screenshot saved to `Saved/Screenshots/FreeFlight.png`.
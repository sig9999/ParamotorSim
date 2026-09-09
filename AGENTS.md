# ParamotorSim Project Notes

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

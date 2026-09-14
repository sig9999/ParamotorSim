# ParamotorSim Architecture Map

This is the shortest path into the current project. `AGENTS.md` is the living
engineering log; this file is the map.

## Entry, menus, and map routing

- `ParamotorSim.uproject` enables `CesiumForUnreal`, `Water`, `Landmass`, and
  `HDRIBackdrop`. The runtime module is `Source/ParamotorSim`.
- `Plugins/CesiumForUnreal/` is intentionally not committed. Install Cesium for
  Unreal `2.29.1` into the project or engine before building.
- `Config/DefaultEngine.ini` starts at `/Game/Maps/L_MainMenu` and maps:
  - `L_MainMenu` -> `MainMenuGameMode`
  - `LV_World` -> `WorldMapGameMode`
  - `L_Westerly` -> `ParamotorGameMode`
  - `L_Earth` -> `ParamotorGameMode`
  - `L_Hangar` -> `HangarGameMode`
- Menu/UI classes: `UW_MainMenu`, `UW_FreeFlightMenu*`, `WorldMapGameMode`,
  `WorldMapPawn`, `UW_WingSelect`, `UW_HangarMenu*`, `UW_PauseMenu*`,
  `UW_FlightHUD*`.
- Free Flight uses `FLaunchSite` records in `SpecCatalog.h` and
  `ParamotorGameInstance.cpp`. Curated sites can open a baked flat map;
  airports without a curated map open `L_Earth`.

## Earth-scale Cesium world

- `Content/Maps/L_Earth.umap` is the globe flight map. `L_Westerly` remains the
  detailed baked Rhode Island map.
- `Source/ParamotorSim/Private/ParamotorGameMode.cpp` detects globe maps,
  configures saved/runtime Cesium tilesets, manages georeference origin,
  deferred environment actors, clip polygons, and pawn settling.
- `CesiumWorldHelpers.*` is reflection-based glue around Cesium classes so the
  module can use Cesium without linking its module directly.
- `PsFlyToLL <lat> <lon> [altM] [pitch] [yaw]` is the geodetic teleport command.
  Teleports over 50 km recenter the Cesium georeference; bridge actors and
  vegetation rebuild after rebasing.
- The globe water sheet is intentionally pawn-solid and acts as the floor in
  photogrammetry clip zones.

## Rhode Island showcase and melt clipping

- `NewportBridge.*` contains the spec-driven landmark bridge system:
  `FBridgeSpec`, `EBridgeStyle`, and four crossings (Pell/Newport, Jamestown,
  Mount Hope, Sakonnet).
- `ParamotorGameMode` owns non-bridge `MeltZones` and calls the Cesium polygon
  raster overlay path to exclude Google photorealistic tiles that are fully
  inside melt zones.
- `InsideClipZone` accepts the water floor inside excluded tile areas.
- Known residual: one Pawcatuck/Little Narragansett edge tile can remain because
  excluding it would remove nearby village tiles.

## Land cover, vegetation, and surfaces

- `LandCoverService.*` fetches/caches NLCD/MRLC land-cover pixels for the
  current geodetic position.
- `VegetationSemantics.*` owns `EVegSemantic`, `ESurfaceSemantic`, `FVegContext`,
  and pure classification functions.
- `VegetationLibrary.*` is the semantic-to-art registry. The data asset is
  `/Game/Data/DA_VegetationLibrary`; code defaults remain as fallback.
- `VegetationField.*` performs deterministic placement, cell streaming,
  georeference rebuilds, debug visualization, and `MPC_Veg` interaction params
  (`WindDir`, `WindSpeed`, `GustSpeed`, `VegTime`, `PropPos`, `PropDir`,
  `PropStrength`).
- `SurfaceLibrary.*` is the semantic-to-physical registry. The data asset is
  `/Game/Data/DA_SurfaceLibrary`; `ESurfacePhysType` is the coarse interaction
  layer and `FSurfacePhysProfile` carries physical material, footstep, FX,
  friction, and prop-wash scalars.
- `SurfaceService.*` resolves `FResolvedSurface` from geodetic/world positions,
  adds waterline and weather wetness, and drives the optional detail patch.

## Sand, ocean, and environmental interaction

- `ParamotorSandSystem.*` is the runtime deformable-sand system. On globe maps
  it is gated by `SurfaceService` deformability, uses service wetness, stamps
  footprints, scours under prop wash, and emits grains/dust through the shared
  render targets and ISM pools.
- `ParamotorOceanSystem.*` owns the baked-map Water plugin ocean. `L_Earth`
  uses the Cesium/globe water sheet instead.
- `ParamotorEnvironment.*` owns sky, sun, fog, post-process, ocean/water setup,
  and weather-driven environment state.
- `WeatherService.*` fetches aviationweather.gov METAR data and stores parsed
  wind/weather state on `UParamotorGameInstance`.

## Pilot character

- `ParamotorCharacter.*` is the pilot. Implementation is split across:
  `ParamotorCharacter.cpp`, `_Input.cpp`, `_IK.cpp`, `_Audio.cpp`,
  `_Injury.cpp`, plus `ParamotorAnimInstance.*`.
- Current base meshes are MetaHuman Frey plus Techwear combined skeletal meshes:
  `/Game/Frey_Exported/MHC_Frey_*` and `/Game/Techwear/techwearOutfit/...`.
- Generated PPG headgear now exists under
  `/Game/Characters/PilotMale/Headgear/PPGHelmet/` (`SM_PPGHelmet`,
  `SM_PPGVisor`, and seven `M_PPG_*` materials).
- Optional custom garment hooks remain for `/Game/Characters/PilotMale/Clothing`,
  but no `SK_Pilot_*` shirt/pants/boots assets were generated in this pass;
  Techwear clothing is the current appearance.
- Development commands added for inspection: `PsHelmet`, `PsVisor`, `PsHair`,
  and editor-only `PsDumpBody`.

## Paramotor equipment and Hangar

- `SpecCatalog.h` defines wing/engine/frame/prop/harness specs, launch sites,
  and `FParamotorBuild`.
- `ParamotorGameInstance` owns the active/default catalog and saved build state.
- `CodexParts.h` is the shared registry for real manufacturer Blueprint paths
  and frame-space transforms.
- `ParamotorBuilder.*` spawns Codex parts for the Hangar; `ParamotorActor_*`
  uses the same registry for the in-world paramotor.
- `L_Hangar`, `HangarGameMode`, `HangarPawn`, and `UW_HangarMenu*` provide the
  rotating podium, camera, categories, brand tabs, model cards, specs, saved
  builds, and equip/remove actions.
- Frame-first visibility is intentional: no frame means an empty podium;
  dependent parts do not appear without their prerequisites.

## Wing and flight systems — DO NOT MODIFY IN THIS AUDIT

- `WingActor.*` is the general wing/ground/line/wind/thermal state surface.
- `ChargerWingActor.*` is the active Macpara Charger implementation and derives
  from `WingActor` for compatibility.
- `ChargerDynamics.h` contains the SI-unit aerodynamic model.
- `ChargerRigData.h` contains generated canopy/line node and edge data.
- `ParamotorActor.*` and `ParamotorActor_*.cpp` contain engine state, propeller,
  thrust, mounting, riser, hang-swing, and equipment interaction code.
- `ParamotorPlayerController.*` and `ParamotorPlayerController_Spawn.cpp` own
  input, spawning, development commands, teleports, and equipment state.

## Development automation

- Development/Editor builds poll `C:\DevinTools\ps_cmd.txt` roughly every 0.5 s
  and execute the first line through `ConsoleCommand`. This is gated by
  `#if WITH_EDITOR || UE_BUILD_DEVELOPMENT` and exists because synthetic window
  input is unreliable in this environment.
- `C:\DevinTools\shot.py` captures screenshots with `mss`.
- Useful commands: `PsFlyToLL`, `PsGeo`, `PsSemGeo`, `PsVeg`, `PsVegLib`,
  `PsVegDebug`, `PsVegSwap`, `PsVegReset`, `PsSurfGeo`, `PsSurfLib`,
  `PsSurfDebug`, `PsSurfSwap`, `PsSurfReset`, `PsSand`, `PsSea`, `PsHelmet`,
  `PsVisor`, `PsHair`, `PsDumpBody`.

## Current known limitations

- Flight is preserved but frozen for this synchronization task; do not tune it.
- Custom `SK_Pilot_*` garments are still absent; the pilot currently uses the
  Techwear outfit plus MetaHuman head/grooms and generated PPG headgear.
- Some physical materials are generated placeholders; footstep audio still falls
  back to the legacy grass/dirt banks when a profile has no bound sounds.
- Surface detail patch materials are mostly unbound by design; aerial imagery is
  the default ground truth.
- Taxiways/aprons still rely on land-cover impervious classification rather
  than dedicated vector semantics.
- Some remote forest teleports can still hit the documented settle/floorless
  watchdog cycle.
- HUD altitude is local-Z feet, not MSL.
- `Content/CesiumSettings/CesiumIonServers/CesiumIonSaaS.uasset` and `L_Earth`
  may carry a local Cesium access token on a developer machine. Repository
  commits must keep the sanitized placeholder; restore/configure the real token
  locally after syncing. Never commit or document the token value.

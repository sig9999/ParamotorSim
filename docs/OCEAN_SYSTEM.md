# Ocean System

`AParamotorOceanSystem` — spawned by `AParamotorEnvironment::Setup()` on any map
that has a `ALandscapeProxy` (i.e. the baked flat maps; globe maps skip it and
keep Cesium's own water).

Source: `Source/ParamotorSim/Private/ParamotorOceanSystem.cpp`
Header: `Source/ParamotorSim/Public/ParamotorOceanSystem.h`

## Architecture

- One `AWaterZone` (40km x 30km) + one `AWaterBodyOcean`, both spawned at the
  world origin. The ocean body's island spline is moved ~10km inland so the
  rendered surface covers the whole map with no hole.
- **Runtime spawn caveat that cost a session:** `UWaterBodyComponent` only
  discovers its zone in the editor (`OnPostActorCreated`, cook fixups, or
  `SetWaterZoneOverride`). A runtime-spawned body never joins a zone on its
  own — no zone ownership means no WaterInfo texture, no quadtree, no mesh,
  and the material gets no `WaterBodyIndex`. `Setup()` calls
  `OC->SetWaterZoneOverride(Zone)` — do not remove it.
- Local-only tessellation is **off** deliberately: it limits the mesh to a
  ~1km window around the camera and the far mesh beyond it has no wave data,
  which read as a hard pale wedge from the air. The quadtree LODs the whole
  zone instead (`ForceCollapseDensityLevel=0`, `LODScale=2`,
  `TessellationFactor=5` — all set via reflection; they're private members).
- `FarDistanceMeshExtent=400km` + `SetFarMeshMaterial(MI_ParamotorOcean)`
  keeps water to the horizon past the zone edge.
- `SetRenderTargetResolution(4096)` — the WaterInfo depth/attenuation texture
  covers the whole zone (~10m/texel).

## Sea states

`EOceanState`: `Calm / LightWind / Normal / Rough / Storm` — presets in
`Preset()` differ in wavelength range, amplitude, steepness and directional
spread (fetch-limited coastal spectrum, not a linear amplitude scale).

`UpdateFromWeather(WindSpeedMS, WindDirDeg, GustMS)` is driven by the
environment's wind model:

- Hs = clamp(0.016 * U10^2, 0.05, 4.5) m — coastal fetch-limited growth
- T  = clamp(0.55 * U10 + 1.2, 2.5, 9.5) s; L = 1.56 * T^2 m
- METAR wind is FROM-direction; converted to wave-travel yaw.
- The spectrum lerps toward the target over seconds (the sea builds/falls,
  doesn't snap) and only recomputes `UGerstnerWaterWaves` when the target
  actually changed.

## Material

`/Game/Materials/MI_ParamotorOcean` — child of `/Water/.../Water_Material`
with `Enable Ocean Foam`, `Enable Ocean Transition`, `Caustics` on and
Atlantic-tuned `Scattering`/`Absorption`/`Foam Scattering` +
`Water Distant Normal Offset/Length`. Built by
`ParamotorEditorUtils` (C++; the Python API can't reach static permutations).
If it ever needs rebuilding, the helper is `ConfigureOceanMaterial()`.

## API / controls

- `SetSeaState(EOceanState)` / `ForceSeaState(int)` — manual override
  (-1 clears back to weather-driven).
- `GetWaterHeightAt(FVector)` — CPU wave-height query for future
  buoyancy/splash/water-contact work.
- Console: `PsSea <0..4|-1>` — force a sea state live.
  `PsFlyTo X Y Z [Pitch Yaw]` — teleport into fly mode.
  `PsWalk` — back to walking after a fly teleport.

## Perf notes

Full-screen SingleLayerWater + per-pixel Gerstner is the real cost
(~15-20ms GPU at 1600x900x1.25 on this machine); tri count is handled by
the quadtree LODs. Useful cvars: `r.Water.WaterMesh.LODCountBias`,
`r.Water.WaterMesh.TessFactorBias`, `r.Water.WaterMesh.ShowLODLevels`,
`r.Water.WaterInfo.DrawPerViewDebugInfo`.

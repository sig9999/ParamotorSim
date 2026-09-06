# ParamotorSim Project Notes

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

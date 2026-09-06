# Paramotor Physics Knowledgebase
## For building the most realistic paramotor simulator in Unreal Engine 5

---

## 1. GLIDE RATIO (L/D)

### Real-world values
- **Beginner EN-A wing**: 8:1 to 9:1 glide ratio (trim, no motor running)
- **EN-B wing**: 9:1 to 10:1
- **Reflex/competition wing**: 10:1 to 12:1 (at speed)
- **With motor running (prop drag)**: Reduces glide by ~0.5-1.0 point
- **With motor + gear (total weight)**: Heavier loading = slightly higher speed but same L/D (L/D is independent of weight in theory, but real wings have optimal loading)

### Key physics
- Glide ratio = Lift / Drag = CL / CD (at trim AoA)
- At trim: L/D ≈ 8.5 for a beginner wing with pilot+motor
- Total drag = wing drag + pilot drag + line drag + paramotor drag
- Pilot+motor drag area: ~0.3-0.5 m² (significant!)
- Line drag: ~0.05-0.1 m² equivalent (lines are thin but numerous)

### Implementation notes
- Wing CL at trim AoA (~7°): ~0.9-1.0
- Wing CD at trim: ~0.035-0.045 (profile) + induced drag (CL²/(π·AR·e))
- Total system CD (wing + pilot + lines + motor): ~0.06-0.08
- Realistic L/D = CL_total / CD_total ≈ 0.95 / 0.11 ≈ 8.6:1
- Current code may have CD too low → unrealistically good glide

---

## 2. FLARE (LANDING)

### Real-world behavior
- At ~2-3m above ground, pilot pulls both brakes smoothly to ~50-80%
- This increases camber → CL jumps from ~1.0 to ~1.5-1.6 (CLMax)
- The increased lift temporarily arrests the descent
- The increased drag slows forward speed
- The flare lasts ~2-4 seconds before the wing stalls and settles
- Too early: wing stalls, pilot drops hard
- Too late: pilot hits ground at full sink rate
- Ground effect: below 1 wingspan altitude, induced drag reduces → slightly better glide

### Key physics
- Brake input increases CL by up to 60% (from 1.0 to 1.6)
- But also increases CD by up to 200% (from 0.04 to 0.12+)
- The flare is a transient: CL spikes, then drops as the wing stalls
- After full brake hold > 2-3 seconds, the wing enters parachutal stall
- In parachutal stall: CL ≈ 0.2, CD ≈ 1.0+ (just a parachute)
- Sink rate in parachutal stall: ~3-5 m/s ( survivable landing)

### Implementation notes
- Need a "flare boost" that temporarily increases CL when brakes are applied
- The boost should decay after 2-3 seconds (wing stalls)
- Ground effect should reduce induced drag below ~10m altitude
- The transition from flying → parachutal stall → landing must be smooth

---

## 3. INFLATION (REVERSE LAUNCH)

### Real-world behavior
- Wing is laid out flat, leading edge downwind
- Pilot faces the wing (reverse launch)
- Pilot pulls A-lines and moves backward/into wind
- Wind enters the cells → internal pressure builds → wing rises
- The wing rises in an arc: starts flat → 45° → 90° (zenith) → may overshoot
- Pilot must "catch" the wing with brakes to stop overshoot
- If wind is too light: pilot must run forward to create apparent wind
- If wind is too strong: wing rockets up and overshoots violently
- Typical inflation time: 2-5 seconds depending on wind
- The wing does NOT inflate instantly — it takes time for cells to fill

### Key physics
- Inflation force = dynamic pressure × cell opening area
- Cell opening area decreases as the wing rises (angle changes)
- Internal pressure = 0.5 × ρ × V² × (cell fill factor)
- The wing has inertia + apparent mass of air inside it
- A-line pull angle affects how quickly the leading edge lifts
- Once the leading edge is up, the rest follows from internal pressure
- The wing must reach ~70-80% inflation before it can support its own weight
- Below 70%: the wing is a sail, not a wing — it falls back down without wind

### Current issues
- Inflation is too easy — wing rises too quickly
- Need to model cell fill time (not instant)
- Need apparent mass of air inside the wing
- Need A-line pull angle to matter
- Need wind speed threshold below which inflation is very difficult

---

## 4. KITING (GROUND HANDLING)

### Real-world behavior
- Once inflated, the wing sits at zenith (~85-90° above pilot)
- Wind keeps it inflated and pressurized
- The wing naturally weathervanes (leading edge into wind)
- Pilot steers with brakes and weight shift
- If the wing drifts left, pilot pulls right brake to steer it back
- If the wing overshoots forward, pilot pulls both brakes to catch it
- If the wing falls behind, pilot runs forward to create apparent wind
- Crosswind pushes the wing sideways — pilot must counter
- In light wind, the pilot must keep moving to keep the wing up
- In strong wind, the wing pulls hard — pilot leans back

### Key physics
- The wing is a kite on a 7m string (the riser lines)
- Pendulum dynamics: the wing swings on the line
- Wind force = 0.5 × ρ × V² × S × (CL² + CD²)^0.5 (total aerodynamic force)
- The wing generates lift even at zenith (it's an airfoil in a wind stream)
- Apparent wind = true wind + pilot movement (running into wind adds to it)
- Line tension = aerodynamic force + wing weight
- If line tension drops to zero, the wing falls (no wind or pilot too slow)
- Brake input during kiting: damps oscillation + steers

### Current issues
- Need realistic line tension (wing falls if no wind/movement)
- Need apparent wind from pilot movement
- Need the wing to actually fall if the pilot stops in light wind
- Need brake damping to be more realistic

---

## 5. LINE DRAG

### Real-world behavior
- A paramotor has ~100-200 individual lines
- Total line length: ~7m (cascade from riser to wing)
- Line diameter: 0.6-1.5mm (thin but numerous)
- Line drag area: ~0.05-0.1 m² equivalent flat plate
- Line drag is small but NOT negligible — it affects glide ratio
- Lines create turbulence behind them
- In rain, lines get heavier and drag increases

### Key physics
- Line drag = 0.5 × ρ × V² × S_lines × CD_line
- CD_line ≈ 1.0 (cylinder in crossflow at low Re)
- S_lines = N_lines × diameter × length
- For a typical wing: ~150 lines, avg 0.8mm, avg 4m = 0.48 m² total surface
- But most lines are at an angle, so effective drag area ≈ 0.05-0.08 m²
- This adds ~5-10% to total system drag

### Implementation notes
- Add line drag as a constant force opposite to apparent wind
- Line drag area: 0.06 m²
- Line CD: 1.0 (cylinder)
- This should be included in total drag calculation

---

## 6. THRUST AND CLIMB

### Real-world behavior
- Moster 185: ~75 kgf (736N) static thrust
- Thrust decreases with airspeed (propeller efficiency drops)
- At 10 m/s: thrust ≈ 60% of static (~440N)
- At 20 m/s: thrust ≈ 30% of static (~220N)
- Climb rate at full throttle: 2-4 m/s (depends on wing loading)
- Climb angle: 10-20° at full power
- Speed in climb: slightly above trim (10-12 m/s)
- Level flight at full power: wing accelerates to trim + small margin, then climbs

### Key physics
- Excess thrust = Thrust - Drag
- Climb rate = Excess_thrust × V / Weight (power equation)
- At trim: Drag ≈ Weight / L/D ≈ 120kg × 9.81 / 8.5 ≈ 138N
- Excess thrust at full power = 440 - 138 = 302N
- Climb rate = 302 × 10.5 / (120 × 9.81) ≈ 2.7 m/s ✓
- This matches real-world data

---

## 7. SPEED ENVELOPE

### Real-world behavior (beginner EN-A wing)
- **Trim speed**: 10-11 m/s (22-25 mph)
- **Min speed (stall)**: 6-7 m/s (13-16 mph)
- **Max speed (trimmers open)**: 13-14 m/s (29-31 mph)
- **Speed bar**: +1-2 m/s over trimmers
- **Absolute max**: ~15 m/s (33 mph) with trimmers + speedbar
- **Collapse speed**: ~16-18 m/s (wing tucks from low AoA)
- **Never exceed**: ~20 m/s (full frontal collapse)

### Key physics
- At trim: AoA ≈ 7°, CL ≈ 0.9, L/D ≈ 8.5
- At min speed: AoA ≈ 15° (stall), CL ≈ 1.6, L/D ≈ 3
- At max speed: AoA ≈ 3°, CL ≈ 0.6, L/D ≈ 6
- Below stall: wing collapses, CL drops to 0.2, CD jumps to 1.0
- Above max: frontal collapse, CL drops to 0.1, wing tucks

---

## 8. PENDULUM DYNAMICS

### Real-world behavior
- Pilot hangs 7m below wing on riser lines
- Pendulum period: T = 2π√(L/g) = 2π√(7/9.81) ≈ 5.3 seconds
- Damping ratio: 0.3-0.5 (aerodynamic damping from wing + pilot)
- Throttle punch: pilot swings forward, wing stays → pendulum oscillation
- Throttle cut: wing slows, pilot keeps going → swing forward
- Brake pull: wing slows, pilot swings forward
- Brake release: wing surges forward, pilot swings back
- In turbulence: random pendulum excitation

### Key physics
- Pendulum equation: θ'' + 2ζωθ' + ω²θ = F(t)/(m·L)
- ω = √(g/L) = √(9.81/7) = 1.18 rad/s
- Period T = 2π/ω = 5.3s
- Damping: ζ ≈ 0.35 (from wing aerodynamic damping)
- The pendulum affects pitch (forward/back swing) and roll (side/side)
- Weight shift: pilot shifts CG → changes pendulum equilibrium → bank

---

## 9. TURN DYNAMICS

### Real-world behavior
- **Weight shift**: Primary steering. Pilot shifts weight under wing.
  - Lowers riser on weight-shift side → wing banks that direction
  - Bank angle: up to 20-30° from weight shift alone
  - Response: smooth, 2-3 second lag
- **Brake steering**: Secondary. Pulls trailing edge down.
  - Creates drag on braked side → wing yaws that direction
  - Yaw → bank → turn (coordinated)
  - Heavy brake: braked side stalls → wing rolls into turn sharply
  - Response: 1-2 second lag (airflow takes time to respond)
- **Coordinated turn**: Weight shift + brake on same side
  - This is how real pilots turn
  - Weight shift initiates, brake fine-tunes
- **Turn radius**: R = V² / (g · tan(bank))
  - At 10 m/s, 30° bank: R = 100 / (9.81 × 0.577) = 17.7m
  - At 10 m/s, 45° bank: R = 100 / 9.81 = 10.2m
- **Spiral dive**: Bank > 45° → self-sustaining
  - Speed increases → more lift → more bank → more speed
  - Sink rate: 10-20+ m/s
  - G-force: 2-4G at 60° bank

---

## 10. COLLAPSE BEHAVIOR

### Real-world behavior
- **Frontal collapse (tuck)**: AoA goes negative → leading edge tucks under
  - Cause: strong downdraft, sudden brake release at speed, trimmers too fast
  - Wing tucks 30-60% → CL drops to ~0.1 → pilot drops
  - Recovery: 2-5 seconds (wing reinflates from internal pressure)
  - Pilot should NOT brake during recovery (causes secondary collapse)
- **Asymmetric collapse**: One side tucks
  - Cause: turbulence, asymmetric brake at speed
  - Wing yaws hard toward collapsed side (drag asymmetry)
  - Pilot swings away from collapsed side (pendulum)
  - Recovery: weight shift away from collapse, light brake on open side
- **Parachutal stall**: Too much brake → wing stops flying
  - CL drops to 0.2, CD jumps to 1.0+
  - Wing becomes a parachute: straight down at 3-5 m/s
  - Recovery: release brakes, wait for wing to fly again

### Key physics
- Collapse trigger: AoA < -1° (frontal) or AoA > StallAoA + 6° (deep stall)
- Collapse amount: proportional to how far past the trigger
- During collapse: CL = 0.1 × (1 - collapse_amount), CD = 0.8 + collapse_amount × 0.5
- Recovery: collapse amount decays at 0.3-0.5 per second (2-3s to recover)
- During recovery: wing reinflates progressively, CL ramps back up

---

## 11. GROUND EFFECT

### Real-world behavior
- Below ~1 wingspan altitude (10m for a paramotor wing)
- Induced drag reduces by up to 50% at ground level
- Wing floats / "sucks" to the ground
- Makes landing flare more effective
- Makes takeoff easier (less speed needed)

### Key physics
- Ground effect factor = 1 - exp(-h / (b/2))
  - h = altitude above ground
  - b = wingspan (~10m)
- At h = 10m: factor = 1 - exp(-2) = 0.86 (14% reduction)
- At h = 5m: factor = 1 - exp(-1) = 0.63 (37% reduction)
- At h = 1m: factor = 1 - exp(-0.2) = 0.18 (82% reduction)
- Applied to induced drag only, not profile drag

---

## 12. WIND AND WEATHER

### Real-world behavior
- Wind gradient: wind speed increases with altitude
  - V(h) = V_ref × (h/h_ref)^0.15 (power law)
  - At 10m: 1.0×, at 50m: 1.25×, at 100m: 1.41×
- Gusts: ±20-50% of base wind, 2-10 second periods
- Turbulence: random perturbations, stronger near ground and obstacles
- Thermals: rising air columns, strongest in midday, over dark ground
- Wind direction: shifts 10-30° over time

### Key physics
- Apparent wind = true wind - velocity (vector subtraction)
- The wing feels apparent wind, not true wind
- Headwind: increases apparent wind → more lift at lower ground speed
- Tailwind: decreases apparent wind → less lift, need more ground speed
- Crosswind: pushes wing sideways during kiting/takeoff

---

## 13. WEIGHT AND LOADING

### Real-world values
- Pilot: 75-85 kg
- Paramotor (Moster 185): 23-28 kg
- Wing: 5-7 kg
- Fuel (10L): 7 kg
- Total: 110-130 kg
- Wing loading: 110-130 kg / 25m² = 4.4-5.2 kg/m²
- Optimal loading varies by wing size

### Key physics
- Wing loading affects:
  - Trim speed (higher loading = faster trim)
  - Sink rate (higher loading = faster sink)
  - Climb rate (higher loading = less climb for same thrust)
  - Collapse resistance (higher loading = more collapse-resistant)
  - Brake authority (higher loading = less authority per brake)

---

## 14. UNIT CONVENTIONS (UE5)

- UE5 uses centimeters: 1 UE unit = 1 cm
- Velocity: cm/s (divide by 100 for m/s)
- Acceleration: cm/s² (divide by 100 for m/s²)
- Mass: kg (UE uses kg for physics)
- Force: N (Newton) — but in UE, forces are in (kg × cm/s²) = centi-Newtons
  - To convert N to UE force: multiply by 100 (1 N = 100 cN = 100 kg·cm/s²)
- Gravity: -981 cm/s² (9.81 m/s² × 100)
- Air density: 1.225 kg/m³ (at sea level, 15°C)
- Dynamic pressure: q = 0.5 × ρ × V² (V in m/s, q in Pa = N/m²)
- Force from q: F = q × S (S in m², F in N)
- Convert N to cm/s² acceleration: a = F / m × 100 (cm/s²)

---

## IMPLEMENTATION PRIORITY

1. **Glide ratio fix** — ensure L/D ≈ 8.5:1 with motor+gear
2. **Flare for landing** — temporary CL boost from brake input
3. **Ground effect** — reduce induced drag near ground
4. **Line drag** — add to total drag
5. **Inflation difficulty** — model cell fill time, apparent mass
6. **Kiting realism** — line tension, apparent wind, wing falls if no wind
7. **Speed envelope** — verify trim/stall/collapse speeds
8. **Pendulum** — verify 5.3s period, 0.35 damping
9. **Turn dynamics** — verify coordinated turn behavior
10. **Collapse** — verify realistic collapse and recovery

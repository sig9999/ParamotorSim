# Paramotor Physics Knowledge Base

> Comprehensive reference for realistic paramotor/paraglider simulation.
> Sources: glidersim (Heatwole), "Paragliders' launch trajectory is universal" (arXiv:2205.08411),
> manufacturer line sheets, training texts, accident studies.

---

## 1. Coordinate Frames

- **World `W`**: x=forward (launch direction), y=right, z=up
- **Canopy body `B`**: origin at center of pressure; x=chord (trailing→leading); z=up
- **Harness `H`**: origin at pilot CM; x=forward, y=right, z=up

## 2. Core State

```
Wing:   r_CP (world pos), q_BW (orientation), v_CP (velocity), omega_B (angular vel)
        cell_pressure[] (per-cell inflation 0-1), collapse_left/right (0-1)
Pilot:  r_CM (world pos), q_HW, v_CM, on_ground
Control: brake_left_m, brake_right_m (meters of pull), weight_shift (-1..+1),
         accelerator (0..1), a_riser_pull, trimmer_position (-1..+1)
```

## 3. Wing Loading and Collapse Resistance

### Key Principle: Heavier = More Stable
- Higher wing loading (pilot weight / wing area) = more internal canopy pressure
- More internal pressure = stiffer canopy = MORE collapse-resistant
- An overloaded wing stays inflated MORE strongly, not less
- The trade-off of overloading is: faster/more dynamic behavior, worse climb performance, higher stall speed
- Overloaded wings CAN be kited for ground handling practice
- Overloaded wings simply cannot take off (lift < weight at achievable airspeed)

### Collapse Resistance Factor
- `CollapseResistance = clamp(WeightRatio, 0.5, 2.0)`
  - 0.5x at light load (50% of max weight)
  - 1.0x at max weight
  - 2.0x at 2x overload
- Fast trimmers reduce resistance: `/= GetTrimmerCollapseRisk()`
  - Slow trim: 0.7x risk (more stable)
  - Fast trim: 2.0x risk (reflex mode is twitchy)

## 4. Yaw Stability During Kiting

### Why an overhead wing doesn't freely spin
- **Pitch and roll** are stabilized by pendulum effect (pilot hangs 6-8m below canopy)
- **Yaw is NOT pendulum-stable** — no direct gravitational restoring torque

### What stabilizes yaw:
| Mechanism | Effect |
|---|---|
| Canopy arc/sweepback | Yawed wing → one tip deeper in wind (more speed) → differential lift → restoring yaw moment |
| Differential line drag | Advancing side lines see more apparent wind → more drag → yaw back |
| Aerodynamic yaw damping | Rotating canopy pushes air laterally → opposing side force |
| Pilot/harness inertia | Pilot resists rapid yaw → coupling/phase lag damps oscillation |
| Yaw-roll coupling | Yaw → sideslip → anhedral/dihedral → roll → pendulum stabilization |

### Yaw moment model
```
M_yaw_stab = -C_N_beta * q * S * b * beta       // restoring (C_N_beta ≈ -0.02..-0.10)
M_yaw_damp = -C_N_r * rho * S * b² * |v_rel| * omega_z  // damping (C_N_r ≈ -0.5..-2.0)
M_brake_yaw = -C_brk_yaw * q * S * b * (brake_L - brake_R)  // asymmetric brake
```

## 5. Line Topology and Crossing

### Preventing crossings in normal operation
- Ordered stacking: A on top, then B, C, D, brake lines separate
- Tension keeps lines straight and parallel once loaded
- Quick links / maillons and line keepers maintain order
- Pre-flight "wall": laid out as arc into wind with lines clear

### What goes wrong
| Problem | Cause |
|---|---|
| Riser twist | Wrong turn during reverse launch; rapid canopy rotation |
| Line-over | Line looped over leading edge or wingtip |
| Cravat | Wingtip/trailing edge tucked through lines |
| Knot | Packing errors or vegetation |

### Topology model
- Don't rely on 2D segment intersection alone
- Track `wrap_count` per line strand — true tangle when canopy passes through a line
- Harmless projection crossings have `wrap_count == 0`

## 6. Launch Types

### Forward launch
- Pilot faces launch direction; wing laid behind
- Run to generate apparent wind; pull A-risers up
- Best in nil to light wind (< 8-10 mph / < 4.5 m/s)
- Pilot can't see wing well; risk of surge; brakes to stop

### Reverse launch
- Pilot faces wing (upwind); risers crossed
- Walk backward, pull A-risers; canopy visible throughout
- Used in stronger wind (> 8-15 mph / > 3.5-6.7 m/s)
- After stable overhead, turn 180°; risers uncross
- Wrong turn direction = twisted brake lines

### Launch state machine
```
LAYOUT → INFLATION → RISING → OVERHEAD_CHECK → [REVERSE_TURNAROUND] → COMMIT_RUN → AIRBORNE
```
- INFLATION→RISING: center cells P > 0.6 and A-line tension > threshold
- RISING→OVERHEAD: canopy center within ~10° of vertical
- OVERHEAD→COMMIT: symmetric, no line-overs, pilot centered
- Reverse: OVERHEAD→TURNAROUND→COMMIT after 180° yaw

## 7. Physical Inflation Sequence

1. Lay out canopy in horseshoe/arc into wind; center cell highest
2. A-lines tension first in center (closest to pilot)
3. Air enters open leading-edge cells; pressurize center→outward
4. Spanwise tension pulls adjacent cells open
5. Canopy becomes semi-rigid airfoil; generates lift + drag
6. Rises along roughly vertical trajectory, trailing edge lagging
7. Near zenith may surge forward; release A, apply light brakes
8. Stable overhead → commit to launch

### Universal launch trajectory (arXiv:2205.08411)
- Trailing edge trajectory is universal (independent of pulling force)
- Force only sets time scale: `τ = sqrt(m·ℓ / F₀)`
- Dimensionless equations govern θ (riser angle) and V_O

### Inflation pressure model
```
P_target = 1.0 if (q > q_min && alpha > alpha_inflate_min)
P_target = 0.3 if (q > q_min)  // partial
P += (P_target - P) * (1 - exp(-dt / tau_inflate))
```
- `tau_inflate` ≈ 0.1-0.4s for cell filling
- `alpha_inflate_min` ≈ 0-2°
- Full inflation in 2-5s from first pull

## 8. Ground Handling Forces

### Forces acting
1. Canopy aerodynamics (lift + drag per cell)
2. Line tension (tension-only cables)
3. Pilot gravity (m_p · g)
4. Ground contact (normal + friction)
5. Pilot muscle/ground reaction
6. Brake forces (trailing edge deflection)

### Pendulum
- Natural frequency: `f_p = (1/2π)·sqrt(g/L_eff)`
- L_eff ≈ 6.5-8.0m → f_p ≈ 0.18-0.20 Hz, period ≈ 5.0-5.7s
- Pitch oscillations driven by: canopy surge, delayed brake, gusts
- Roll oscillations damped by dihedral/anhedral + pendulum

### Brake effects
| Input | Effect |
|---|---|
| Symmetric light | Slows wing, increases AoA, stops forward surge |
| Symmetric deep | Stall risk; partial deflation if held |
| Asymmetric | Drag on one side → yaw → sideslip → differential lift → roll into turn |
| Too much in gust | Collapse by unloading LE or too-high AoA |

## 9. Apparent Wind and Wind Window

```
v_rel = v_point - wind    // velocity of point relative to air
```
- Same formula for canopy and pilot
- Magnitude and direction drive all aerodynamic forces

### Wind window (adapted for paraglider)
| Region | Position | Effect |
|---|---|---|
| Neutral/zenith | Canopy overhead, aligned with v_rel | Low pull; good for kiting |
| Power/drive | Downwind and lower | High tension + forward pull; launch speed |
| Edge | Off to the side | Asymmetric loading; turning/parking |

- Window rotates with pilot velocity: running into wind → v_rel grows; downwind → shrinks

## 10. Line Geometry

### Line groups
- **A-lines**: leading edge, shortest, carry most load (~80% with B)
- **B-lines**: support top of airfoil
- **C/D-lines**: rear rows, longer, support back of canopy
- **Brake lines**: non-load-bearing, trailing edge, routed through pulley
- **Stabilo**: outer canopy stabilizers

### Representative dimensions
| Parameter | Value |
|---|---|
| Riser length | 540 mm (0.54m) |
| Total line length (QL→canopy) | 5.5-7.6m |
| Line measurement tension | 50N (5 daN) |
| Carabiner spacing (chest strap) | 38-57 cm |
| Seat-to-carabiner height | 41-48 cm |
| Brake slack before deflection | 5-10cm (~7cm common) |
| Brake pull near landing | 30-40 cm |
| Carabiner major-axis strength | 18-28 kN |

## 11. Pitch and AoA While Running

```
alpha = theta - gamma    // AoA = pitch - flight path angle
gamma = atan2(-v_rel.z, v_rel.x)
```

### Running sequence
1. Run forward → v_rel increases
2. A-risers pull LE up → canopy pitches up → inflates
3. Near zenith → may surge forward → AoA decreases
4. AoA too low → LE folds under → frontal collapse
5. Light symmetric brake → trailing edge down → increases camber+AoA → arrests surge
6. Release brakes → wing accelerates and pitches forward again

### Typical values
- Trim AoA: 5-8°
- Stall AoA (model): 12-18°
- Launch AoA: starts +30-60° (vertical risers), settles to trim

## 12. Collapse Causes and Recovery

### Causes
- Low AoA (forward surge, too much speed, speedbar)
- Turbulence/gusts (downward air, rotor, wind shear)
- Asymmetric loading (crosswind, uneven inflation, excessive one-side brake)
- Tip collapse (gust on one tip or braked turn)
- Frontal collapse (whole LE folds under from low pressure)

### Frequency (accident study, 409 German accidents 1997-1999)
- Deflation/collapse: 32.5% of accidents
- Of collapses: 85.1% asymmetric, 15.9% frontal

### Recovery
- Many self-reinflate in 1-3s (modern EN A/B)
- Asymmetric: weight shift to inflated side, opposite brake, wait for reinflation
- Frontal: brief symmetric brake pulse (≤1s), then hands up
- Ground handling: move under wing + light timely brake

### Recovery time constants
- `tau_recover` ≈ 0.5-2.5s for minor collapses
- 3s is EN classification threshold
- `alpha_front_collapse` ≈ -2° to +2°

## 13. Weight Shift

### Mechanics
- Moving hips/CM under one side changes line-tension distribution
- Canopy rolls toward weighted side
- Wider carabiner spacing = more effective but more twist risk

### Model
```
y_shift = weight_shift * max_lateral_shift  // ~0.12m
hang_point.y += y_shift
M_weight_shift = cross(r_pilot_to_cp, m_pilot * g)
```

## 14. Representative Parameter Table

| Quantity | Value | Notes |
|---|---|---|
| Total mass (pilot+wing+harness) | 85-120 kg | |
| Canopy mass | 3.0-6.5 kg | |
| Harness mass | 3.5-5.5 kg | |
| Flat area | 20-30 m² | |
| Flat span | 8-13 m | |
| Aspect ratio | 4.0-6.5 | |
| Trim speed (free flight) | 37-40 km/h | |
| Paramotor min/trim/max | 25-27 / 45 / 65-67 km/h | |
| Riser length | 0.50-0.54 m | |
| Total line length | 5.5-7.6 m | |
| Effective pendulum length | 6.5-8.0 m | |
| Pendulum period | 5.0-5.7 s | |
| Kiting wind speed | 7-12 mph (3.1-5.4 m/s) | |
| Difficult kiting wind | >9-15 knots (>4.5-7.7 m/s) | |
| A+B load share | ~80% | |
| CLmax | 1.2-1.5 | |
| CD0 | 0.04-0.06 | |
| Stall AoA (model) | 12-18° | |
| Oswald efficiency | 0.85-0.92 | |
| Yaw stiffness C_N_beta | -0.02..-0.10 /rad | |
| Yaw damping C_N_r | -0.5..-2.0 | |

## 15. Wind Gradient

- Wind speed increases with altitude: `V(h) = V_ref · (h/h_ref)^0.15`
- At 10m reference height, wind at 100m is ~41% faster
- Important for takeoff (ground level = slower) vs cruise altitude

## 16. Implementation Notes for C++

### Unit consistency
- Unreal uses cm for distance, deg for rotation, seconds for time
- Physics formulas use SI (m, rad, s)
- Always convert: `m = cm / 100`, `rad = deg * PI / 180`

### Key constants
```cpp
static constexpr float CM_PER_M = 100.0f;
static constexpr float GRAVITY_MS2 = 9.81f;        // m/s²
static constexpr float AIR_DENSITY_SEA_LEVEL = 1.225f; // kg/m³
static constexpr float MPH_PER_MS = 2.23694f;
static constexpr float KMH_PER_MS = 3.6f;
static constexpr float KNOTS_PER_MS = 1.94384f;
```

### Recommended approach
1. Model wing as pendulum suspended below pilot (not drifting flat object)
2. Per-cell inflation pressure (not single inflation value)
3. Apparent wind = point velocity - wind velocity
4. Aerodynamic forces from AoA + airspeed using real coefficients
5. Line topology with wrap tracking (not just 2D intersection)
6. State machine: LAYOUT→INFLATION→RISING→OVERHEAD→COMMIT→AIRBORNE
7. Collapse triggers: low AoA, turbulence, asymmetric loading
8. Recovery: self-reinflation with time constants

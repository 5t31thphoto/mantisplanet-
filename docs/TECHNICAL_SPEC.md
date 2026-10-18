# MantisPlanet: Technical Specification & Architecture Blueprint

This document outlines the complete engineering specification for **MantisPlanet**, a passive exploration engine built for the M5Core2 (ESP32) hardware platform. The architecture relies on zero-persistence, stateless procedural generation—rendering an infinite dream-like world on the fly using M5Unified IMU tilt controls and optimized canvas double-buffering.

---

## 1. Core Architectural Principle: The Ephemeral Dream Engine

To achieve an infinite, immersive exploration experience within the RAM constraints of the ESP32 (520KB SRAM), MantisPlanet rejects traditional world maps and coordinate grids.

* **Stateless Slicing:** The world is treated as a real-time raymarched column function. The renderer samples a deterministic procedural noise function driven entirely by the player's current view angle ($\theta$) and cumulative forward distance ($D$).
* **Non-Persistent Bi-Directional Illusion:** When the user changes heading or reverses direction, the engine generates a fresh, coherent slice of wilderness in front of the camera. No history is stored, allowing seamless exploration without memory exhaustion.

### Data Flow

```
IMU (MPU6886) ──► sim.cpp (controls) ──► yaw θ, speed v
                                         │
                                         ▼
main loop ──► totalDistance D = ∫ v dt
         │
         ├──► hx::hum(v) ──► vibration motor
         │
         └──► renderDreamWorld(θ, D) ──► M5Canvas (320×240 RGB565)
                      │
                      └──► universe::checkEasterEggSpawn(θ, x) ──► rare entities
```

---

## 2. Hardware Integration & Flight Control Pipeline (`sim.cpp`)

Navigation uses the M5Core2 internal MPU6886 6-axis IMU. Following the neutral calibration pattern, the system captures a baseline orientation on startup and maps pitch to throttle and gyroscope yaw to camera rotation.

### Control Spec

| Input            | Source          | Mapping                                      |
|------------------|-----------------|----------------------------------------------|
| Default state    | —               | Continuous baseline forward drift $0.35$ u/s |
| Throttle (Pitch) | Accel Y − neutral | Tip forward → accelerate; pull back → brake / stop |
| Steering (Yaw)   | Gyro Z          | Gravity-anchored integration → smooth AR-style panning |

### Implementation Notes

* Neutral calibration is performed once on the first valid IMU sample after boot.
* Speed is exponentially smoothed (`0.88 / 0.12`) to avoid jitter.
* Yaw accumulates with a small gain (`0.015 × dt × 60`) so the feel remains stable at typical frame rates.

---

## 3. Visual Rendering & Cosmic Light Show Engine (`render.cpp`)

The visual style delivers high-contrast, dithered retro nature aesthetics—featuring layered depth-sorted forest canopies, vertical volumetric light beams, and pulsing celestial halos.

* **Internal Buffer:** Rendered inside a fast 320×240 `M5Canvas` instance with 16-bit RGB565 color depth.
* **Sky & Gradient System:** Uses a slow sine-wave oscillator tied to total travel distance to organically cycle through dawn, solar noon, sunset, and cosmic night palettes.
* **Celestial Beam:** A vertical light column and pink halo whose X position is modulated by `sin(yaw)`.
* **Procedural Forest:** Column-space noise decides tree presence, height, and canopy; sampling stride is 4 px for performance.
* **Easter Egg Scatter:** Deterministic rare glowing entities (see §4).

### Color Helpers

* `rgb(r,g,b)` → RGB565 packing
* `hsv(h,s,v)` → HSV → RGB565 for sky cycling

---

## 4. Deterministic Easter Egg Seeding (`universe.cpp`)

Because the world has no memory, rare entities (wandering wildlife, glowing monoliths, artifacts) must appear organically when crossing specific spatial thresholds.

* World seed is set once via `universeInit(seed)`.
* Hash mixes world seed, quantized heading angle, and screen-column offset.
* Trigger rate is deliberately sparse: `h % 397 == 0`.

This guarantees that the same $(\theta, x)$ pair always yields the same decision, preserving the illusion of a persistent world without storing any state.

---

## 5. Haptic Atmosphere (`haptics.cpp`)

Utilizes the M5Core2 ERM vibration motor to provide physical feedback for engine hum and environmental presence.

| API              | Behavior                                              |
|------------------|-------------------------------------------------------|
| `hx::begin()`    | Zero the motor                                        |
| `hx::hum(i)`     | Set continuous intensity $i \in [0,1]$ (clamped)     |
| `hx::thud(s)`    | Short one-shot pulse (strength → 120 or 255)          |
| `hx::update(dt)` | Apply continuous hum value to the motor each frame    |

Intensity is mapped to PWM roughly as `50 + intensity × 180` (dead-zone below 0.03).

---

## 6. Main Loop & Timing (`main.cpp`)

```
setup:
  M5.begin (power + IMU)
  Display rotation 1, brightness 100
  Create 320×240 RGB565 sprite
  mantis::initControls()
  mantis::universeInit(1337)
  hx::begin()

loop (≈ 100 Hz target, clamped dt ≤ 50 ms):
  updateControls → θ, v
  D += v × dt × 8
  hx::hum(v × 0.8); hx::update(dt)
  renderDreamWorld(canvas, θ, D)
  pushSprite(0,0)
  delay(10)
```

---

## 7. Build & Delivery Pipeline

| File              | Role                                                                 |
|-------------------|----------------------------------------------------------------------|
| `platformio.ini`  | ESP32 / M5Core2 env, M5Unified + M5GFX, PSRAM, 240 MHz, post-merge  |
| `merge_bin.py`    | Post-build script that merges bootloader + partitions + firmware into a single `firmware_merged.bin` |
| `.github/workflows/ci.yml` | Builds on push to main/master and stages the merged binary under `website/firmware/` |

Target board: **m5stack-core2** (espressif32@6.9.0, Arduino framework).

---

## 8. Memory & Performance Budget

* No heap allocations after `setup`.
* Single off-screen canvas (320×240×2 ≈ 150 KB) lives in PSRAM / heap.
* Column loop stride = 4 → ~80 noise evaluations per frame.
* All procedural math is pure float; no lookup tables required.

This keeps the entire experience inside the ESP32’s practical limits while still delivering an infinite, navigable dreamscape.

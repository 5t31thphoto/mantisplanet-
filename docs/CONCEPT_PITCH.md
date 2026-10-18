# MantisPlanet: Concept Pitch & Design Document

> *"An infinite world that exists only as long as you look at it. Elite Dangerous on a flippin' ESP32, turned inside out into a passive cosmic dream."*

---

## 1. Executive Summary & Vision

**MantisPlanet** is a passive, atmospheric world-exploration simulator engineered from scratch to run natively on a battery-powered **M5Core2 (ESP32)** microcontroller.

Modern open-world games rely on massive memory footprints, persistent world states, and heavy coordinate tracking. MantisPlanet turns this convention on its head. Using a devilishly clever stateless procedural illusion, it generates an infinite, breathtaking wilderness on the fly—rendering only what is directly in front of the player's current perspective like a waking dream.

Coupled with physical **IMU tilt-steering controls** and ambient **haptic feedback**, MantisPlanet transforms a pocket-sized IoT development kit into an ethereal window onto an alien planet.

---

## 2. Core Mechanics & The "Dream" Illusion

The defining engineering marvel of MantisPlanet is how it bypasses the strict RAM limits of the ESP32 (520 KB SRAM) to deliver an infinite world:

* **Zero Persistence (The Ephemeral Map):** The engine does not store a world map. There is no backtracking or coordinate memory. If you walk forward, the world materializes ahead of you. If you turn around and walk the opposite direction, the engine simply weaves a fresh, coherent landscape in that direction without remembering what was there before.
* **The Raymarched Column Pipeline:** The view is computed dynamically per frame. A fast procedural noise function evaluates column-by-column based on your current heading angle ($\theta$) and cumulative forward distance ($D$), rendering depth-sorted forest canopies, organic flora, and terrain strips in real time.
* **Deterministic Easter Egg Scatter:** Because nothing is saved, rare discoveries—such as wandering alien wildlife or glowing obsidian monoliths—are seeded deterministically using a spatial hash of your heading and distance. When your travel milestone hits a threshold, a unique artifact spawns directly into your field of view and dissolves peacefully into the mist as you pass it.

---

## 3. Flight Controls & Hardware Integration

MantisPlanet utilizes the M5Core2’s built-in **MPU6886 6-axis IMU** and tactile features to create an intuitive, hands-on exploration experience:

* **Gravity-Anchored Yaw Steering:** Rotating the device horizontally integrates the gyroscope Z-axis, creating a smooth, augmented-reality camera rotation that feels like holding a window into another dimension.
* **Pitch Throttle (The Cruise Controller):**
  * You are **always moving slowly forward** by default, drifting through the wilderness at a meditative pace.
  * **Tip forward** past the neutral deadzone to increase your forward velocity.
  * **Pull back** to brake or bring your travel to a complete standstill, allowing you to stop and admire a cosmic sunset.
* **Haptic Sound Design:** Utilizing the M5Core2's ERM vibration motor, the device provides a continuous physical hum that matches your cruising velocity—making the hardware itself feel alive in your hands.

---

## 4. Visual Aesthetic & The Cosmic Sky

MantisPlanet’s visual language bridges retro pixel-art nostalgia with high-contrast alien beauty:

* **Dynamic Palette Swapping:** Real-time color interpolation shifts the world's atmosphere seamlessly through a full day-night cycle—from vibrant sunlit greens and golden meadows to deep twilight purples, magenta sunsets, and neon-lit cosmic nights.
* **The Cosmic Light Show Sky:** Above the canopy, the sky is a living canvas. Giant celestial halos, pulsing rings, and dramatic vertical light beams stretch downward into the landscape, shifting organically over time to give the planet a majestic, monumental scale.

---

## 5. Why MantisPlanet Matters

MantisPlanet is more than a game—it is a masterclass in constrained engineering. By weaponizing procedural mathematics, stateless rendering architectures, and the robust M5Unified framework, it proves that deeply immersive, visually striking, infinite virtual worlds do not require high-end GPUs or gigabytes of storage. They just require imagination, clever code, and a flippin' ESP32.

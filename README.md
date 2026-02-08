### LED Matrix Lamp

#### Task Requirements

The objective is to design and implement an advanced IoT lighting system based on a high-density LED matrix (22x13), controlled via a dual-interface system: a native Apple HomeKit integration for seamless smart home automation and a custom-built Web Interface for more control. 

The software architecture must bridge high-speed real-time rendering with asynchronous network communication. Key requirements include implementing a coordinate mapping algorithm for a manually soldered LED strips in zig-zag , a Pixel Grouping system allowing users to define and control arbitrary clusters of LEDs, and a suite of pre-made effects like Starlight or Fire running smoothly on the ESP32. The system requires non-volatile memory management for schedules and state persistence, along with a responsive UI stored in PROGMEM.

### Implementation

#### 1. Components

For this project, I utilized:

* **1x ESP32-WROOM-32 Development Board** 
* **286x WS2812B Addressable LEDs** (WS2812B 60 LEDS/m density)
* **1x USB-C Breakout Board** (Power delivery)
* **1x 100µF Capacitor** (Power smoothing/Decoupling)
* **Power Supply:** 5V High Amperage Source (Calculated for ~2.5A limit in software)
* **Wiring & Enclosure:** Custom PETG 3D printed case (details in the Setup section).

#### 2. Implementation

##### Hardware:

For this project, I designed a 3D-printable cylindrical design made from three parts:
* **Base**: Includes a slot for a USB-C breakout board.
* **Center cylinder**: 22-faced shape for mounting LED strips.
* **Cap**: Top enclosure.
The parts assemble modularly for easy printing and integration.

The core of the visual display is a custom matrix with 22 vertical columns, each holding 13 LEDs. These LEDs originate from a 5m continuous strip that I cut to the required lengths, and I soldered every connection (GND, DATA and VCC).

To optimize the wiring path and reduce cable clutter, I utilized a **zig-zag pattern**. The data signal flows alternatively: Upwards on even columns and Downwards on odd columns. This physical layout necessitates a software-side coordinate remapping system to treat the array as a logical Cartesian grid (X, Y).

Power distribution is managed via a **USB-C Breakout board**. To prevent voltage sags during high-brightness transients, a **100µF capacitor** is placed in parallel across the power rails near the ESP32 and LED injection point. The data signal is fed into the ESP32's **GPIO 4**.

##### Software:

The software is built upon a hybrid architecture integrating **FastLED** for rendering, **HomeSpan** for Apple HomeKit certification-less integration, and a native asynchronous **WebServer** for the GUI.

**Technical & Architectural Overview**
I implemented a modular state-machine approach. The main loop orchestrates three critical non-blocking tasks: Network Polling (HomeKit/Web), State Management (Effect logic), and Frame Rendering.

**1. The Render Engine (The View)**
At the lowest level, the `FastLED` library drives the WS2812B protocol. However, drawing directly to the strip index `leds[i]` is unintuitive due to the Zig-Zag wiring. I implemented a hardware abstraction layer via the **`mapLEDs(x, y)`** function. This function translates logical Cartesian coordinates (0..21, 0..12) into the physical 1D index of the LED strip using bitwise operations to detect column parity (`x & 1`).

The rendering pipeline supports multiple generative modes:

* **Particle Systems:** The **Fire Effect** uses a cooling/sparking heat map algorithm (`qsub8`, `random8`) simulated on a virtual grid and mapped to the LEDs.
* **Fluid Dynamics:** The **Liquid** and **Lava** effects utilize 3D Simplex Noise (`inoise8`) to generate organic, flowing textures that evolve over time (Z-axis offset).
* **Trigonometric Patterns:** The **Neon Wave** and **Chroma** modes use `sin8` and polar coordinate math to create rotating gradients and oscillating interference patterns.

**2. State Management & Connectivity (The Controller)**
The system features a dual-control scheme:

* **Apple HomeKit (HomeSpan):** I implemented a `LampAccessory` class inheriting from `Service::LightBulb`. This exposes the standard Hue/Saturation/Brightness (HSV) characteristics to iOS devices. When a user creates a scene in the Apple Home app, the `update()` override intercepts the payload, converts the HSB color space to RGB, and overrides the current animation mode to a solid color.
* **Web Dashboard (The Interface):** For advanced control, I embedded a complete Single Page Application (SPA) within the ESP32's flash memory (`PROGMEM`). The UI connects to the backend via REST API endpoints (for example: , `/mode?m=1`, `/set?r=255...`).

* **Save**
To ensure a seamless user experience across power cycles, the system implements a robust non-volatile memory management strategy using the ESP32's Preferences API (a modern replacement for deprecated EEPROM). Instead of writing to flash on every loop cycle (which would degrade memory lifespan), the system performs "write-on-change" operations within the Web Server handlers.

Key system states—including Power Status, Global Brightness, Active Mode, and Color Configurations (RGB/HSV values)—are serialized and stored in the lamp namespace. Upon boot (setup()), the firmware queries the NVS partition; if valid keys exist, the lamp restores its exact previous state immediately, bypassing the default initialization values. This ensures that the lamp behaves like a consumer appliance rather than a development board.

**3. Advanced Feature: Dynamic Grouping**
A unique feature of this architecture is the **user pixel editor**. To allow users to highlight specific areas of the lamp (for example: drawing a shape), I implemented a `ledGroupMap[NUM_LEDS]` array.
The Web UI features a JavaScript-generated grid allowing users to "paint" specific pixels. When saved, this bitmask is sent to the ESP32, which assigns a Group ID to those physical indices. The rendering loop checks `currentMode == 6`, iterates through the map, and applies distinct effects (Static, Breath, Rainbow) solely to the user-defined clusters, effectively allowing the matrix to act as a canvas.

**4. System Utilities & Persistence**

* **NTP Synchronization:** The system connects to `pool.ntp.org` to retrieve precise local time, enabling the **Scheduler** feature (auto-turn off at specific hours).
* **Non-Blocking Timers:** Instead of `delay()`, the code uses `millis()` deltas for animation timing and `EVERY_N_MILLISECONDS` macros to maintain a consistent framerate (~30 FPS) while keeping the web server responsive.
* **Safety:** A software power limiter is implemented via `FastLED.setMaxPowerInVoltsAndMilliamps(5, 2500)` to ensure the current draw never exceeds the USB-C capabilities, preventing brownouts.

### 3. Setup & Video



*Note: The physical assembly involves a complex 3D printed housing designed to diffuse the light and manage heat dissipation. The 22 distinct strips were aligned using a printed jig to ensure perfect grid alignment.*

# Project: Cylindrical LED Matrix Lamp

## 1. General Description

In this project I'll be creating a *cylindrical rgb lamp* from zero. Using my prior skills, I will 3D model the case, program the ESP-32 and solder the WS2812 led strip. For diffusion I will be using a custom Plexiglass outer shell.

Unlike standard lamps, this device is an IoT node powered by an **ESP32 microcontroller**. It hosts a local web server, allowing users to control it via a custom web app.

**Key Features:**

* **Web Control:** Adjust color, brightness, effects and modify other settings. 
* **Scheduling:** User-configurable ON/OFF timers based on NTP (Network Time Protocol).
* **Dynamic Effects:** Pre-programmed animations (Rainbow, Fire, Breathe).
* **Custom Matrix Creator:** A pixel-art style interface on the web app allowing users to draw custom static or add dynamic effects patterns on the cylinder.
* **Automatic Brightness** The lamp can change its brightness based on the ambient light.

---

## 2. Bill of Materials (BOM)

- 1x **ESP32-WROOM-32** (Microcontroller & Wi-Fi Server)
- 1x **5m WS2812 Addressable LED Strip** (Light and custom effects) 
- 1x **Plexiglass sheet** (diffusion layer/outer shell)
- 2x **3D printed pieces** (lamp construction)
- 1x **USB-C Breakout Board** (power delivery)
- 1x **Logic Level Shifter** (used for stepping ESP32 3.3v data to 5v for the LED Strip)
- 1x **1000µF capacitor** (Power smoothing for LEDs)
- Consumable **Wires/Solder** (connections) 

---

## 3. Tutorial Source

For this project I am not following a specific tutorial. This lamp is built from the ground up using knowledge accumulated 
throughout all of the university years. The outer shell will be modeled by me in *Fusion 360* and 3D printed. I will manually write the software to handle my specific case and requirements for the cylindrical mapping (1D LED Strip to 2D matrix). The connections will be soldered rather than using a pre-made kit.

---

## 4. Technical Analysis

### Q1 - What is the system boundary?

The system boundary includes everything from the physical lamp hardware to the hosted software interface.

* **Inputs:** USB-C Power (5V), User commands via Wi-Fi, Time data (NTP), photoresistor.
* **Outputs:** Light Effects, Web Interface for mobile (maybe pc) (UI), *maybe Apple homekit implementation*.
* **Excluded:** The user's smartphone hardware and the wall adapter are external actors; the system boundary ends at the USB port and the ESP-32 Wi-Fi antenna.

### Q2 - Where does intelligence live?

The intelligence lives entirely **locally on the ESP32**.

* It does not rely on a cloud service (like AWS or Blynk) for logic.
* The ESP32 handles the web server hosting, the mathematical mapping of pixels to the cylinder and the lighting/effects, and the real-time generation of PWM signals for the LEDs.

### Q3 - What is the hardest technical problem?

The project presents three distinct technical hurdles:

1. **Cylindrical Matrix Mapping:** converting a linear strip of LEDs into a 2D matrix and then wrapping that logically around a cylinder. This requires complex index calculation.
2. **Power & Logic Levels:** The WS2812B LEDs require 5V logic for reliable data transmission, while the ESP32 outputs 3.3V. Designing a stable circuit that bridges this gap while handling the high current draw of the LEDs is required.
3. **Physical Design:** Modeling a 3D-printable structure that holds the circuits and the LEDs firmly while allowing the plexiglass to diffuse light evenly without hotspots.

### Q4 - What is the minimum demo?

A successful minimum demo consists of:

1. Powering the system via USB-C.
2. The device connecting to local Wi-Fi for simple web controls.
3. Changing the color of the lamp successfully from the phone.
4. The system must be capable of continuous operation for a minimum of 30 minutes.

### Q5 - Why is this not just a tutorial?

While basic tutorials exist for driving LED strips, this project represents a complex system integration challenge rather than a simple reproduction:

- Custom Full-Stack Firmware: Unlike other projects, this firmware integrates a non-blocking asynchronous web server, NTP time synchronization, and sensor-based logic (automatic brightness via photoresistor). It also handles the custom mathematical translation required to map 2D drawing tools to a 3D cylindrical surface for lighting/custom effects.

- Original Mechanical Design: I am not printing a downloaded model, the case is being modeled from scratch in *Fusion 360* to specifically address the optical challenges of diffusing LEDs through plexiglass without creating hotspots.

- Advanced IoT Features: The project aims to go beyond simple remote control by exploring integration with ecosystem standards like Apple HomeKit. (if time allows)
    
### Q6 - Do you need an ESP32?

**Yes:**
I require an ESP32 (purchased by me) because the project relies heavily on **Wi-Fi capabilities** to host the web server and better computation (also the small form-factor helps). A standard Arduino Uno would not be enought.

---

Prototype:
<p align="center">
  <img src="prototip.jpeg" alt="Prototip Lampa" width="500">
</p>

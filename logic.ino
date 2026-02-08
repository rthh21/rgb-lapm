#define FASTLED_ESP32_RMT
#include <WiFi.h>
#include <WebServer.h>
#include <FastLED.h>
#include <HomeSpan.h> // HomeKit integration library
#include <time.h>
#include <Preferences.h> // For saving settings to non-volatile storage

// | Hardware & Matrix Configuration |
#define LED_PIN             4
#define MATRIX_WIDTH        22
#define MATRIX_HEIGHT       13
#define NUM_LEDS            (MATRIX_WIDTH * MATRIX_HEIGHT + 1)
#define LED_TYPE            WS2812B
#define COLOR_ORDER         GRB

// | Power Management |
#define POWER_VOLTS         5
#define POWER_MILLIAMPS     2500

// | Server & Time Settings |
#define SERVER_PORT         8080
#define GMT_OFFSET          7200
#define DAYLIGHT_OFFSET     3600
#define NTP_SERVER          "pool.ntp.org"

// | HomeKit & Device Info |
#define PAIRING_CODE        "44455666"
#define QR_ID               "RESET99"
#define DEVICE_NAME         "Andrei's Lamp"
#define PREFS_NAMESPACE     "lamp"

#define MAX_GROUPS          16
#define FRAME_DELAY_MS      33
#define FIRE_WIDTH          11 

// | Operation Modes |
#define MODE_STATIC         0
#define MODE_FIRE           1
#define MODE_STARLIGHT      2
#define MODE_CHROMA         3
#define MODE_LIQUID         4
#define MODE_WAVE           5
#define MODE_GROUPS         6
#define MODE_LAVA           7
#define MODE_BREATHE        8
#define MODE_FIRE_2012      9

// | Default Values |
#define DEFAULT_BRIGHTNESS  255
#define DEFAULT_FIRE_COOL   55
#define DEFAULT_FIRE_SPARK  120
#define DEFAULT_STAR_SPEED  20
#define DEFAULT_STAR_CHANCE 15
#define DEFAULT_CHROMA_SPD  40
#define DEFAULT_LIQUID_SPD  4
#define DEFAULT_WAVE_ANG    90
#define DEFAULT_WAVE_SPD    25
#define DEFAULT_WAVE_BLK    100
#define DEFAULT_LAVA_SPD    15
#define DEFAULT_BREATHE_SPD 30

// | Global Variables |
CRGB leds[NUM_LEDS];       // Actual LEDs sent to strip
CRGB targetLeds[NUM_LEDS]; // Target buffer for smooth transitions
WebServer server(SERVER_PORT);
Preferences prefs;

bool webServerStarted = false;
bool powerOn = true;
int currentMode = MODE_STATIC;
int globalBrightness = DEFAULT_BRIGHTNESS;
CRGB solidColor = CRGB::Blue;
bool reverseDirection = false; 

// Effect specific parameters
int fireCooling = DEFAULT_FIRE_COOL;
int fireSparking = DEFAULT_FIRE_SPARK;

int starlightSpeed = DEFAULT_STAR_SPEED;
int starlightChance = DEFAULT_STAR_CHANCE;
CRGB starlightColor = CRGB::White;
bool starlightWhite = false;

int chromaSpeed = DEFAULT_CHROMA_SPD;
int liquidSpeed = DEFAULT_LIQUID_SPD;

int waveAngle = DEFAULT_WAVE_ANG;
int waveSpeed = DEFAULT_WAVE_SPD;
int waveBlack = DEFAULT_WAVE_BLK;
int waveColorMode = 0;
CRGB waveColor1 = CRGB::Blue;
CRGB waveColor2 = CRGB::Red;

int lavaSpeed = DEFAULT_LAVA_SPD;
CRGB lavaBgColor = CRGB::Black;
CRGB lavaBlobColor = CRGB::Red;

int breatheSpeed = DEFAULT_BREATHE_SPD;
int breatheBrightness = DEFAULT_BRIGHTNESS;
CRGB breatheColor = CRGB::Blue;

// Matrix mapping helpers
byte heatMap[MATRIX_WIDTH][MATRIX_HEIGHT];
uint16_t xOffset = 0;
uint16_t zOffset = 0;
uint16_t yOffset = 0;

// Time & Schedule
long gmtOffsetSec = GMT_OFFSET;
int daylightOffsetSec = DAYLIGHT_OFFSET;

bool timerActive = false;
unsigned long timerTarget = 0;
int timerOriginalMins = 0;

bool schedActive = false;
int schedHour = 22;
int schedMinute = 0;

// Grouping system (mapping specific LED indices to logical groups)
uint8_t ledGroupMap[NUM_LEDS];

struct GroupData {
  bool active;
  CRGB color;
  uint8_t effect;
  uint8_t brightness;
} groups[MAX_GROUPS];

// | HomeSpan (HomeKit) Accessory Definition |
struct LampAccessory : Service::LightBulb {
  SpanCharacteristic *power;
  SpanCharacteristic *hue;
  SpanCharacteristic *saturation;
  SpanCharacteristic *value;

  LampAccessory() : Service::LightBulb() {
    power = new Characteristic::On();
    hue = new Characteristic::Hue(0);
    saturation = new Characteristic::Saturation(0);
    value = new Characteristic::Brightness(100);
  }

  // Handle updates from Apple Home App
  boolean update() override {
    if(power->updated()) {
      powerOn = power->getNewVal();
      prefs.putBool("pwr", powerOn);
    }

    // If color/brightness changes, force Static Mode
    if(hue->updated() || saturation->updated() || value->updated()) {
      float hVal = hue->getNewVal<float>();
      float sVal = saturation->getNewVal<float>();
      float vVal = value->getNewVal<float>();

      globalBrightness = map((int)vVal, 0, 100, 0, 255);
      FastLED.setBrightness(globalBrightness);
      prefs.putInt("brt", globalBrightness);

      uint8_t flH = map((int)hVal, 0, 360, 0, 255);
      uint8_t flS = map((int)sVal, 0, 100, 0, 255);

      solidColor = CHSV(flH, flS, 255);
      prefs.putInt("r", solidColor.r);
      prefs.putInt("g", solidColor.g);
      prefs.putInt("b", solidColor.b);

      currentMode = MODE_STATIC;
      prefs.putInt("m", MODE_STATIC);
      powerOn = true;
      prefs.putBool("pwr", true);
    }
    return true;
  }
};

// | Web Interface HTML (Compressed) |
const char index_html[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1, user-scalable=no, viewport-fit=cover">
  <title>Control Center</title>
  <style>
    body {
      margin: 0;
      padding-top: 0;
      width: 100%;
      height: 100vh;
      background-color: #000;
      overflow: hidden;
      overscroll-behavior: none;
      touch-action: none;
      font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif;
      color: #e0e0e0;
      display: flex;
      flex-direction: column;
      overflow-x: hidden;
    }
    .bg-fixed {
      position: fixed;
      top: 0;
      left: 0;
      width: 100%;
      height: 100%;
      background: rgb(9, 9, 9);
      z-index: -1;
      pointer-events: none;
    }
    h2 {
      padding-top: 10%;
      padding-bottom: 10%;
      font-weight: 800;
      letter-spacing: 2px;
      color: #d8d8d8;
      font-size: 2.5em;
      text-align: center;
      margin-bottom: 10px;
    }
    .container {
      flex: 1;
      padding: 10px 20px;
      overflow-y: auto;
      overflow-x: hidden;
      padding-top: env(safe-area-inset-top);
      padding-bottom: calc(90px + env(safe-area-inset-bottom));
    }
    .content {
      display: none;
      animation: fadeIn 0.3s;
    }
    .active {
      display: block;
    }
    .color-wheel-container {
      display: flex;
      justify-content: center;
      margin-bottom: 10%;
      position: relative;
    }
    .color-wheel {
      width: 240px;
      height: 240px;
      border-radius: 50%;
      background: radial-gradient(white 0%, transparent 70%), conic-gradient(red, yellow, lime, aqua, blue, magenta, red);
      position: relative;
      touch-action: none;
      box-shadow: 0 0 20px rgba(0,0,0,0.6);
    }
    .wheel-cursor {
      width: 20px;
      height: 20px;
      border-radius: 50%;
      border: 2px solid white;
      position: absolute;
      top: 50%;
      left: 50%;
      transform: translate(-50%, -50%);
      background: transparent;
      box-shadow: 0 0 5px rgba(0,0,0,0.8);
      pointer-events: none;
    }
    .preset-grid {
      display: grid;
      grid-template-columns: repeat(8, 1fr);
      gap: 8px;
      margin-bottom: 20px;
    }
    .preset-btn {
      aspect-ratio: 1;
      border-radius: 6px;
      border: 1px solid rgba(255,255,255,0.1);
      cursor: pointer;
      box-shadow: 0 2px 5px rgba(0,0,0,0.4);
    }
    .control-group {
      background: rgba(30, 30, 30, 0.5);
      padding: 15px;
      border-radius: 12px;
      margin-bottom: 15px;
      display: none;
      border: 1px solid #333;
    }
    .control-group.visible {
      display: block;
    }
    .effects-grid {
      display: grid;
      grid-template-columns: 1fr 1fr;
      gap: 15px;
      margin-bottom: 15px;
    }
    .btn-effect {
      display: flex;
      flex-direction: column;
      align-items: center;
      justify-content: center;
      width: 100%;
      padding: 25px 15px;
      font-size: 15px;
      letter-spacing: 0.5px;
      min-height: 100px;
      background: #141213;
      color: #e3e3e3;
      border: 1px solid #333;
      border-radius: 12px;
      cursor: pointer;
      transition: all 0.2s;
      font-weight: 600;
      text-transform: uppercase;
      box-shadow: 0 4px 6px rgba(0,0,0,0.3);
    }
    .btn-effect.active-mode {
      background: #ff006e;
      border-color: #ff006e;
      color: #fff;
      box-shadow: 0 0 15px rgba(255, 0, 110, 0.5);
    }
    .btn-small {
      padding: 8px;
      font-size: 11px;
      margin-top: 5px;
    }
    .chk-row {
      display: flex;
      align-items: center;
      justify-content: space-between;
      margin-bottom: 15px;
    }
    input[type=checkbox] {
      width: 20px;
      height: 20px;
      accent-color: #ff006e;
    }
    label {
      display: block;
      margin-bottom: 5px;
      font-size: 11px;
      color: #aaa;
      text-align: left;
      text-transform: uppercase;
      letter-spacing: 1px;
    }
    input[type=range] {
      width: 100%;
      height: 6px;
      border-radius: 3px;
      outline: none;
      -webkit-appearance: none;
      background: #444;
      margin-bottom: 10px;
    }
    input[type=range]::-webkit-slider-thumb {
      -webkit-appearance: none;
      appearance: none;
      width: 20px;
      height: 20px;
      border-radius: 50%;
      background: #ff006e;
      cursor: pointer;
      box-shadow: 0 0 10px rgba(255, 0, 110, 0.4);
    }
    input[type=time], input[type=number], input[type=color] {
      background: #333;
      color: #fff;
      border: 1px solid #555;
      padding: 5px;
      border-radius: 5px;
      width: 100%;
      box-sizing: border-box;
      font-size: 16px;
      margin-bottom: 10px;
      height: 40px;
    }
    .angle-presets, .color-presets {
      display: flex;
      gap: 5px;
      margin-bottom: 10px;
    }
    .angle-btn, .color-mode-btn {
      flex: 1;
      background: #333;
      border: 1px solid #555;
      color: #ddd;
      padding: 10px;
      border-radius: 6px;
      font-size: 11px;
      cursor: pointer;
    }
    .color-mode-btn.active-cm {
      background: #ff006e;
      border-color: #ff006e;
      color: #fff;
    }
    .nav {
      display: flex;
      height: 65px;
      background-color: rgba(20, 20, 20, 0.95);
      backdrop-filter: blur(10px);
      -webkit-backdrop-filter: blur(10px);
      position: fixed;
      bottom: 20px;
      bottom: calc(20px + env(safe-area-inset-bottom));
      left: 20px;
      right: 20px;
      z-index: 10;
      border-radius: 18px;
      box-shadow: 0 10px 30px rgba(0,0,0,0.6);
      border: 1px solid rgba(255,255,255,0.05);
      justify-content: space-around;
      align-items: center;
    }
    .nav-item {
      flex: 1;
      display: flex;
      flex-direction: column;
      align-items: center;
      justify-content: center;
      font-size: 10px;
      cursor: pointer;
      color: #666;
      height: 100%;
      text-transform: uppercase;
      font-weight: 600;
      transition: color 0.3s;
    }
    .nav-item.selected {
      color: #ff006e;
    }
    .nav-item.pwr-btn {
      color: #e0e0e0;
      font-weight: 800;
    }
    .grid-wrapper {
      overflow-x: auto;
      padding-bottom: 10px;
      display: flex;
      justify-content: center;
    }
    .grid-container {
      display: grid;
      grid-template-columns: repeat(22, 1fr);
      gap: 5px;
      margin-bottom: 15px;
      min-width: 400px;
    }
    .grid-pixel {
      aspect-ratio: 1;
      background: #222;
      cursor: pointer;
      border-radius: 50%;
      border: 1px solid #333;
      transition: transform 0.1s, background-color 0.2s;
    }
    .grid-pixel:active {
      transform: scale(0.8);
    }
    .grid-pixel.selected {
      background: #ff006e;
      box-shadow: 0 0 8px #ff006e;
      border-color: #fff;
    }
    .grid-pixel.grouped {
      box-shadow: inset 0 0 0 2px #fff;
      opacity: 0.8;
    }
    .group-list {
      display: flex;
      flex-wrap: wrap;
      gap: 8px;
      margin: 15px 0;
      justify-content: center;
    }
    .group-btn {
      padding: 8px 16px;
      background: #222;
      border: 1px solid #444;
      color: #747474;
      border-radius: 20px;
      font-size: 12px;
      cursor: pointer;
    }
    .timer-display {
      font-size: 14px;
      color: #ff006e;
      text-align: right;
      margin-bottom: 5px;
    }
    .dual-color-inputs {
      display: grid;
      grid-template-columns: 1fr 1fr;
      gap: 10px;
      margin-bottom: 10px;
    }
    .switch {
      position: relative;
      display: inline-block;
      width: 50px;
      height: 26px;
    }
    .switch input {
      opacity: 0;
      width: 0;
      height: 0;
    }
    .slider {
      position: absolute;
      cursor: pointer;
      top: 0;
      left: 0;
      right: 0;
      bottom: 0;
      background-color: #333;
      transition: .4s;
      border-radius: 34px;
      border: 1px solid #444;
    }
    .slider:before {
      position: absolute;
      content: "";
      height: 20px;
      width: 20px;
      left: 2px;
      bottom: 2px;
      background-color: #888;
      transition: .4s;
      border-radius: 50%;
    }
    input:checked + .slider {
      background-color: #ff006e;
      border-color: #ff006e;
    }
    input:checked + .slider:before {
      transform: translateX(24px);
      background-color: white;
    }
    .st-card {
      background: rgba(25,25,25,0.6);
      padding: 15px;
      border-radius: 12px;
      margin-bottom: 15px;
      border: 1px solid #333;
    }
    .st-head {
      display: flex;
      align-items: center;
      margin-bottom: 15px;
      color: #fff;
      font-size: 14px;
      font-weight: 600;
      text-transform: uppercase;
      letter-spacing: 1px;
    }
    .st-icon {
      margin-right: 10px;
      font-size: 18px;
    }
    @keyframes fadeIn {
      from {
        opacity: 0;
        transform: translateY(5px);
      }
      to {
        opacity: 1;
        transform: translateY(0);
      }
    }
  </style>
</head>
<body>
  <div class="bg-fixed"></div>

  <div class="container">
    <div id="tab-color" class="content active">
      <h2>Color</h2>
      <div class="color-wheel-container"><div class="color-wheel" id="wheel"><div class="wheel-cursor" id="cursor"></div></div></div>
      <div class="preset-grid">
        <div class="preset-btn" style="background:#0000FF" onclick="setPreset(0,0,255)"></div>
        <div class="preset-btn" style="background:#00FFFF" onclick="setPreset(0,255,255)"></div>
        <div class="preset-btn" style="background:#FF0000" onclick="setPreset(255,0,0)"></div>
        <div class="preset-btn" style="background:#FF0080" onclick="setPreset(255,0,128)"></div>
        <div class="preset-btn" style="background:#800080" onclick="setPreset(128,0,128)"></div>
        <div class="preset-btn" style="background:#00FF00" onclick="setPreset(0,255,0)"></div>
        <div class="preset-btn" style="background:#FFFF00" onclick="setPreset(255,255,0)"></div>
        <div class="preset-btn" style="background:#FFFFFF" onclick="setPreset(255,255,255)"></div>
      </div>
      <div class="control-group visible">
        <label>Color</label>
        <input type="color" id="mainColorPicker" value="#0000FF" style="height:60px" oninput="updateFromPicker()">
        <input type="hidden" id="r" value="0">
        <input type="hidden" id="g" value="0">
        <input type="hidden" id="b" value="255">
      </div>
    </div>

    <div id="tab-effects" class="content">
      <h2>Effects</h2>
      <div class="effects-grid">
        <button class="btn-effect" id="btn-1" onclick="selectEffect(1)">Fire</button>
        <button class="btn-effect" id="btn-9" onclick="selectEffect(9)">Fire 2012</button>
        <button class="btn-effect" id="btn-2" onclick="selectEffect(2)">Starlight</button>
        <button class="btn-effect" id="btn-3" onclick="selectEffect(3)">Chroma</button>
        <button class="btn-effect" id="btn-4" onclick="selectEffect(4)">Liquid</button>
        <button class="btn-effect" id="btn-5" onclick="selectEffect(5)">Neon Wave</button>
        <button class="btn-effect" id="btn-7" onclick="selectEffect(7)">Lava Lamp</button>
        <button class="btn-effect" id="btn-8" onclick="selectEffect(8)">Breathe</button>
      </div>

      <div id="ctrl-1" class="control-group">
        <label>Cooling</label><input type="range" min="20" max="100" value="55" onchange="updateParam(1, 'cool', this.value)">
        <label>Spark Activity</label><input type="range" min="50" max="200" value="55" onchange="updateParam(1, 'spark', this.value)">
      </div>

      <div id="ctrl-2" class="control-group">
        <div class="chk-row"><label>Pure White</label><input type="checkbox" onchange="updateParam(2, 'white', this.checked ? 1 : 0)"></div>
        <label>Fade Speed</label><input type="range" min="2" max="50" value="20" onchange="updateParam(2, 'speed', this.value)">
        <label>Star Density</label><input type="range" min="5" max="100" value="15" onchange="updateParam(2, 'dens', this.value)">
      </div>

      <div id="ctrl-3" class="control-group"><label>Rotation Speed</label><input type="range" min="2" max="100" value="40" onchange="updateParam(3, 'speed', this.value)"></div>

      <div id="ctrl-4" class="control-group"><label>Speed</label><input type="range" min="1" max="5" value="1" onchange="updateParam(4, 'speed', this.value)"></div>

      <div id="ctrl-5" class="control-group">
        <label>Color</label>
        <div class="color-presets">
          <button class="color-mode-btn" id="cm0" onclick="setWaveCM(0)">Solid Base</button>
          <button class="color-mode-btn" id="cm1" onclick="setWaveCM(1)">Dual Color</button>
        </div>
        
        <div id="wave-dual-ctrl" style="display:none">
            <label>Wave Colors</label>
            <div class="dual-color-inputs">
                <input type="color" id="wc1" value="#0000FF" onchange="sendWaveColors()">
                <input type="color" id="wc2" value="#FF0000" onchange="sendWaveColors()">
            </div>
            <label>Quick Presets</label>
            <div class="color-presets">
              <button class="angle-btn" onclick="setWavePreset('#0000FF', '#FF0000')">R-B</button>
              <button class="angle-btn" onclick="setWavePreset('#00FFFF', '#FF00FF')">C-M</button>
              <button class="angle-btn" onclick="setWavePreset('#FFA500', '#800080')">O-P</button>
            </div>
        </div>

        <label>Wave Angle</label>
        <div class="angle-presets">
          <button class="angle-btn" onclick="setWaveAngle(0)">0</button>
          <button class="angle-btn" onclick="setWaveAngle(90)">90</button>
          <button class="angle-btn" onclick="setWaveAngle(180)">180</button>
          <button class="angle-btn" onclick="setWaveAngle(270)">270</button>
        </div>
        <label>Fine Tune (0-360)</label><input type="range" min="0" max="360" value="90" id="wvAng" oninput="updateParam(5, 'ang', this.value)">
        <label>Speed</label><input type="range" min="1" max="100" value="25" onchange="updateParam(5, 'speed', this.value)">
        <label>Black Intensity</label><input type="range" min="0" max="250" value="100" onchange="updateParam(5, 'blk', this.value)">
      </div>

      <div id="ctrl-7" class="control-group">
        <label>Lava Speed</label><input type="range" min="5" max="60" value="15" onchange="updateParam(7, 'speed', this.value)">
        <label>Colors</label>
        <div class="dual-color-inputs">
            <div><label>Background</label><input type="color" id="lavaBg" value="#000000" onchange="sendLavaColors()"></div>
            <div><label>Blobs</label><input type="color" id="lavaBlob" value="#FF0000" onchange="sendLavaColors()"></div>
        </div>
      </div>

      <div id="ctrl-8" class="control-group">
        <label>Brightness</label><input type="range" min="5" max="255" value="255" onchange="updateParam(8, 'brt', this.value)">
        <label>Speed</label><input type="range" min="5" max="60" value="30" onchange="updateParam(8, 'speed', this.value)">
        <label>Color</label><input type="color" id="breathCol" value="#00FFFF" onchange="sendBreatheColor()">
      </div>
    </div>
    
    <div id="tab-groups" class="content">
      <h2>Pixel Map</h2>
      <div style="text-align:center; margin-bottom:10px; color:#888; font-size:15px;">Select pixels on the matrix below</div>
      <div class="grid-wrapper">
         <div class="grid-container" id="grpGrid"></div>
      </div>
      
      <div style="display:flex; gap:10px;">
          <button class="btn-effect" onclick="createGroup()">Save Selection</button>
          <button class="btn-effect btn-small" style="width:auto; background:#333" onclick="clearSelection()">X</button>
      </div>

      <div class="group-list" id="grpList"></div>
      
      <div id="grp-controls" class="control-group">
        <h3 id="grpTitle" style="margin:0 0 10px 0; color:#ff006e; font-size:14px"></h3>
        <label>Effect</label>
        <div class="angle-presets">
           <button class="angle-btn" onclick="setGrpFx(0)">Static</button>
           <button class="angle-btn" onclick="setGrpFx(1)">Breath</button>
           <button class="angle-btn" onclick="setGrpFx(2)">Rainbow</button>
        </div>
        <label>Color</label>
        <input type="color" id="grpColor" value="#ffffff" onchange="setGrpColor()">
        <label>Intensity</label>
        <input type="range" min="0" max="255" id="grpBrt" onchange="setGrpBrt()">
        <button class="btn-effect" style="border-color: red; color: red; margin-top:10px" onclick="deleteGroup()">Delete Group</button>
      </div>
    </div>

    <div id="tab-settings" class="content">
      <h2 style="font-size: 2em; margin-bottom: 2px;">Options</h2>
      <p style="text-align:center; color:#666; font-size: 11px; margin-bottom: 25px; letter-spacing: 1.5px; font-weight: 600;">SYSTEM & AUTOMATION</p>
      
      <div class="st-card">
        <div class="st-head"><span class="st-icon"></span>Global Settings</div>
        
        <div style="margin-bottom: 20px;">
           <div style="display:flex; justify-content:space-between; margin-bottom:5px;">
              <label style="margin:0;">Brightness</label>
              <span id="brtVal" style="font-size:11px; color:#ff006e; font-weight:bold;">100%</span>
           </div>
           <input type="range" min="10" max="255" value="255" id="brt" oninput="document.getElementById('brtVal').innerText=Math.round(this.value/2.55)+'%'" onchange="sendBrt()">
        </div>

        <div style="border-top:1px solid #333; padding-top:15px;">
           <div class="chk-row" style="margin-bottom:0;">
             <div>
               <label style="margin:0; font-size:12px; color:#ddd;">GMT Offset</label>
               <div style="font-size:10px; color:#666; margin-top:2px;">Timezone correction</div>
             </div>
             <input type="number" id="gmtoff" value="2" min="-12" max="12" style="width:60px; text-align:center; border-radius:8px; background:#222;" onchange="setOff(this.value)">
           </div>
        </div>
      </div>

      <div class="st-card">
         <div class="st-head"><span class="st-icon"></span>Auto Shut-off</div>
         
         <div style="background:#1a1a1a; padding:12px; border-radius:10px; border:1px solid #333; margin-bottom:20px;">
            <div style="display:flex; justify-content:space-between; margin-bottom:8px;">
              <label style="margin:0;">Sleep Timer</label>
              <span id="tmrVal" style="font-size:11px; color:#fff; font-weight:700;">0 min</span>
            </div>
            <input type="range" min="0" max="60" value="0" id="tmrInput" oninput="document.getElementById('tmrVal').innerText=this.value + ' min'" onchange="setTimer(this.value)">
            <div id="tmrStatus" style="font-size:11px; color:#ff006e; text-align:right; height:14px; font-weight:600;"></div>
         </div>

         <div class="chk-row" style="border-top:1px solid #333; padding-top:15px;">
            <div>
               <label style="margin:0; font-size:12px; color:#ddd;">Daily Schedule</label>
               <div style="font-size:10px; color:#666; margin-top:2px;">Force off at specific time</div>
            </div>
            <label class="switch">
              <input type="checkbox" id="schEn" onchange="setSchedule()">
              <span class="slider"></span>
            </label>
         </div>
         <input type="time" id="schTime" style="margin-top:5px; background:#222; border-radius:8px; text-align:center; border:1px solid #333;" onchange="setSchedule()">
      </div>
    </div>
  </div>

  <div class="nav">
    <div class="nav-item selected" onclick="switchTab('tab-color', this)">Color</div>
    <div class="nav-item" onclick="switchTab('tab-effects', this)">Effects</div>
    <div class="nav-item" onclick="switchTab('tab-groups', this)">Map</div>
    <div class="nav-item" onclick="switchTab('tab-settings', this)">Settings</div>
    <div class="nav-item pwr-btn" onclick="togglePower()">ON/OFF</div>
  </div>

<script>
  setInterval(updateStatus, 3000);

  function updateStatus() {
    if(!document.getElementById('tab-settings').classList.contains('active')) return;
    fetch('/status').then(r=>r.json()).then(d => {
      let el = document.getElementById('tmrStatus');
      if(d.rem > 0) el.innerText = "Closing in " + Math.ceil(d.rem/60) + " min";
      else el.innerText = "";
    });
  }

  function setTimer(val) {
    fetch('/setTimer?m=' + val).then(updateStatus);
  }

  function setSchedule() {
    let en = document.getElementById('schEn').checked ? 1 : 0;
    let t = document.getElementById('schTime').value;
    if(!t) return;
    let parts = t.split(':');
    fetch(`/setSchedule?e=${en}&h=${parts[0]}&m=${parts[1]}`);
  }

  function setOff(v) {
    fetch('/setOffset?o=' + v);
  }

  function switchTab(id, el) {
    document.querySelectorAll('.content').forEach(c => c.classList.remove('active'));
    document.querySelectorAll('.nav-item').forEach(n => {
      if(!n.classList.contains('pwr-btn')) n.classList.remove('selected');
    });
    document.getElementById(id).classList.add('active');
    el.classList.add('selected');
    if(id === 'tab-groups') {
      refreshGroups();
    }
  }

  function selectEffect(id) {
    fetch('/mode?m=' + id);
    document.querySelectorAll('#tab-effects .control-group').forEach(c => c.classList.remove('visible'));
    document.querySelectorAll('.btn-effect').forEach(b => b.classList.remove('active-mode'));
    
    let ctrlId = id;
    if(id === 9) ctrlId = 1; 
    
    let ctrl = document.getElementById('ctrl-' + ctrlId);
    if(ctrl) ctrl.classList.add('visible');
    document.getElementById('btn-' + id).classList.add('active-mode');
  }

  function updateParam(mode, param, val) {
    fetch('/update?m=' + mode + '&p=' + param + '&v=' + val);
  }

  function setWaveAngle(val) {
    document.getElementById('wvAng').value = val;
    updateParam(5, 'ang', val);
  }

  function setWaveCM(val) {
    document.querySelectorAll('.color-mode-btn').forEach(b => b.classList.remove('active-cm'));
    document.getElementById('cm' + val).classList.add('active-cm');
    document.getElementById('wave-dual-ctrl').style.display = val === 1 ? 'block' : 'none';
    updateParam(5, 'wc', val);
  }

  function hexToRgb(hex) {
    let bigint = parseInt(hex.substring(1), 16);
    return { r: (bigint >> 16) & 255, g: (bigint >> 8) & 255, b: bigint & 255 };
  }

  function rgbToHex(r, g, b) {
    return "#" + ((1 << 24) + (r << 16) + (g << 8) + b).toString(16).slice(1);
  }

  function sendWaveColors() {
    let c1 = hexToRgb(document.getElementById('wc1').value);
    let c2 = hexToRgb(document.getElementById('wc2').value);
    fetch(`/update?m=5&p=cols&r1=${c1.r}&g1=${c1.g}&b1=${c1.b}&r2=${c2.r}&g2=${c2.g}&b2=${c2.b}`);
  }

  function setWavePreset(c1, c2) {
    document.getElementById('wc1').value = c1;
    document.getElementById('wc2').value = c2;
    sendWaveColors();
  }

  function sendLavaColors() {
    let bg = hexToRgb(document.getElementById('lavaBg').value);
    let bl = hexToRgb(document.getElementById('lavaBlob').value);
    fetch(`/update?m=7&p=cols&r1=${bg.r}&g1=${bg.g}&b1=${bg.b}&r2=${bl.r}&g2=${bl.g}&b2=${bl.b}`);
  }

  function sendBreatheColor() {
    let c = hexToRgb(document.getElementById('breathCol').value);
    fetch(`/update?m=8&p=col&r=${c.r}&g=${c.g}&b=${c.b}`);
  }

  function togglePower() {
    fetch('/pwr');
  }
  
  let tm;
  function sendColor() {
    updateWheelCursorFromSliders();
    clearTimeout(tm);
    tm = setTimeout(() => {
      let r = document.getElementById('r').value;
      let g = document.getElementById('g').value;
      let b = document.getElementById('b').value;
      fetch(`/set?r=${r}&g=${g}&b=${b}`);
    }, 50);
  }

  function updateFromPicker() {
    let hex = document.getElementById('mainColorPicker').value;
    let c = hexToRgb(hex);
    document.getElementById('r').value = c.r;
    document.getElementById('g').value = c.g;
    document.getElementById('b').value = c.b;
    sendColor();
  }

  function syncPicker() {
    let r = parseInt(document.getElementById('r').value);
    let g = parseInt(document.getElementById('g').value);
    let b = parseInt(document.getElementById('b').value);
    document.getElementById('mainColorPicker').value = rgbToHex(r,g,b);
  }

  function setPreset(r, g, b) { 
    document.getElementById('r').value = r; 
    document.getElementById('g').value = g; 
    document.getElementById('b').value = b; 
    syncPicker(); 
    sendColor(); 
  }

  function sendBrt() {
    fetch(`/brt?v=${document.getElementById('brt').value}`);
  }

  function updateGlobalBrt(val) {
    document.getElementById('brt').value = val;
    sendBrt();
  }

  const wheel = document.getElementById('wheel');
  const cursor = document.getElementById('cursor');

  function hsvToRgb(h, s, v) {
    let r, g, b, i, f, p, q, t;
    i = Math.floor(h * 6);
    f = h * 6 - i;
    p = v * (1 - s);
    q = v * (1 - f * s);
    t = v * (1 - (1 - f) * s);
    switch (i % 6) {
      case 0:
        r = v;
        g = t;
        b = p;
        break;
      case 1:
        r = q;
        g = v;
        b = p;
        break;
      case 2:
        r = p;
        g = v;
        b = t;
        break;
      case 3:
        r = p;
        g = q;
        b = v;
        break;
      case 4:
        r = t;
        g = p;
        b = v;
        break;
      case 5:
        r = v;
        g = p;
        b = q;
        break;
    }
    return { r: Math.round(r * 255), g: Math.round(g * 255), b: Math.round(b * 255) };
  }

  function rgbToHsv(r, g, b) {
    r /= 255;
    g /= 255;
    b /= 255;
    let max = Math.max(r, g, b);
    let min = Math.min(r, g, b);
    let h, s, v = max;
    let d = max - min;
    s = max === 0 ? 0 : d / max;
    if (max === min) {
      h = 0;
    } else {
      switch (max) {
        case r:
          h = (g - b) / d + (g < b ? 6 : 0);
          break;
        case g:
          h = (b - r) / d + 2;
          break;
        case b:
          h = (r - g) / d + 4;
          break;
      }
      h /= 6;
    }
    return { h: h, s: s, v: v };
  }

  function updateWheel(e) {
    e.preventDefault();
    const rect = wheel.getBoundingClientRect();
    const x = (e.touches ? e.touches[0].clientX : e.clientX) - rect.left - rect.width / 2;
    const y = (e.touches ? e.touches[0].clientY : e.clientY) - rect.top - rect.height / 2;
    let angle = Math.atan2(y, x);
    let dist = Math.sqrt(x*x + y*y);
    let maxDist = rect.width / 2;
    if (dist > maxDist) dist = maxDist;
    let cx = Math.cos(angle) * dist;
    let cy = Math.sin(angle) * dist;
    cursor.style.left = (cx + rect.width / 2) + 'px';
    cursor.style.top = (cy + rect.height / 2) + 'px';
    let deg = (angle * 180 / Math.PI) + 90;
    if(deg < 0) deg += 360;
    let hue = deg / 360;
    let sat = dist / maxDist;
    let rgb = hsvToRgb(hue, sat, 1);
    document.getElementById('r').value = rgb.r;
    document.getElementById('g').value = rgb.g;
    document.getElementById('b').value = rgb.b;
    syncPicker();
    clearTimeout(tm);
    tm = setTimeout(() => {
      fetch(`/set?r=${rgb.r}&g=${rgb.g}&b=${rgb.b}`);
    }, 50);
  }

  function updateWheelCursorFromSliders() {
    let r = parseInt(document.getElementById('r').value);
    let g = parseInt(document.getElementById('g').value);
    let b = parseInt(document.getElementById('b').value);
    let hsv = rgbToHsv(r, g, b);
    let angle = (hsv.h * 360) - 90;
    let rad = angle * (Math.PI / 180);
    const rect = wheel.getBoundingClientRect();
    let maxDist = rect.width / 2;
    let dist = hsv.s * maxDist;
    let cx = Math.cos(rad) * dist;
    let cy = Math.sin(rad) * dist;
    cursor.style.left = (cx + rect.width / 2) + 'px';
    cursor.style.top = (cy + rect.height / 2) + 'px';
  }

  wheel.addEventListener('touchmove', updateWheel, {passive: false});
  wheel.addEventListener('click', updateWheel);

  const grpGrid = document.getElementById('grpGrid');
  const MW = 22, MH = 13;
  let selectedPixels = new Set();
  let currentActiveGroup = -1;

  for(let y = MH - 1; y >= 0; y--) {
    for(let x = 0; x < MW; x++) {
       let d = document.createElement('div');
       d.className = 'grid-pixel';
       
       let idx;
       if (x & 1) {
         idx = (x * MH) + (MH - 1 - y);
       } else {
         idx = (x * MH) + y;
       }
       idx = idx + 1;

       d.dataset.idx = idx;
       d.onclick = () => toggleSel(d, idx);
       grpGrid.appendChild(d);
    }
  }

  function toggleSel(el, idx) {
    if(selectedPixels.has(idx)) {
      selectedPixels.delete(idx);
      el.classList.remove('selected');
    } else {
      selectedPixels.add(idx);
      el.classList.add('selected');
    }
  }
  
  function clearSelection() {
    selectedPixels.clear();
    document.querySelectorAll('.grid-pixel').forEach(p => p.classList.remove('selected'));
  }

  function createGroup() {
    if(selectedPixels.size === 0) return;
    let arr = Array.from(selectedPixels).join(',');
    fetch('/grpCreate?l=' + arr).then(r => r.text()).then(id => {
      clearSelection();
      refreshGroups();
    });
  }

  function refreshGroups() {
    fetch('/grpList').then(r => r.json()).then(data => {
      let html = '';
      data.forEach(g => {
         html += `<button class="group-btn" onclick="editGroup(${g.id})">G ${g.id}</button>`;
      });
      document.getElementById('grpList').innerHTML = html;
      fetch('/grpMap').then(r2 => r2.json()).then(mapData => {
         document.querySelectorAll('.grid-pixel').forEach(p => {
             let idx = parseInt(p.dataset.idx);
             if(mapData[idx] > 0) {
               p.classList.add('grouped');
               p.style.backgroundColor = `hsl(${mapData[idx] * 45}, 70%, 50%)`;
             } else {
               p.classList.remove('grouped');
               p.style.backgroundColor = '#222';
             }
         });
      });
    });
  }

  function editGroup(id) {
    currentActiveGroup = id;
    document.getElementById('grp-controls').classList.add('visible');
    document.getElementById('grpTitle').innerText = "Edit Group " + id;
  }

  function setGrpFx(fx) {
    if(currentActiveGroup < 0) return;
    fetch(`/grpSet?id=${currentActiveGroup}&fx=${fx}`);
  }

  function setGrpColor() {
    if(currentActiveGroup < 0) return;
    let hex = document.getElementById('grpColor').value;
    let r = parseInt(hex.substr(1,2),16);
    let g = parseInt(hex.substr(3,2),16);
    let b = parseInt(hex.substr(5,2),16);
    fetch(`/grpSet?id=${currentActiveGroup}&r=${r}&g=${g}&b=${b}`);
  }

  function setGrpBrt() {
    if(currentActiveGroup < 0) return;
    fetch(`/grpSet?id=${currentActiveGroup}&brt=${document.getElementById('grpBrt').value}`);
  }

  function deleteGroup() {
    if(currentActiveGroup < 0) return;
    fetch(`/grpDel?id=${currentActiveGroup}`).then(() => {
      document.getElementById('grp-controls').classList.remove('visible');
      refreshGroups();
    });
  }
</script>
</body>
</html>
)rawliteral";

// Map 2D coordinates to 1D strip index (Zig-Zag layout handling)
uint16_t mapPixels(uint8_t x, uint8_t y) {
  if (x >= MATRIX_WIDTH || y >= MATRIX_HEIGHT) {
    return 0;
  }
  uint16_t i;
  if (x & 1) { // Odd columns run upwards (or downwards depending on wiring)
    i = (x * MATRIX_HEIGHT) + (MATRIX_HEIGHT - 1 - y);
  } else {     // Even columns
    i = (x * MATRIX_HEIGHT) + y;
  }
  return i + 1;
}

// Serve the main UI
void handleRoot() {
  server.send(200, "text/html", index_html);
}

// Handle static color request
void handleSet() {
  currentMode = MODE_STATIC;
  if (server.hasArg("r")) solidColor.r = server.arg("r").toInt();
  if (server.hasArg("g")) solidColor.g = server.arg("g").toInt();
  if (server.hasArg("b")) solidColor.b = server.arg("b").toInt();
  
  // Save to persistent storage
  prefs.putInt("sr", solidColor.r);
  prefs.putInt("sg", solidColor.g);
  prefs.putInt("sb", solidColor.b);
  prefs.putInt("m", MODE_STATIC);
  server.send(200, "text/plain", "OK");
}

void handleMode() {
  if (server.hasArg("m")) {
      currentMode = server.arg("m").toInt();
      prefs.putInt("m", currentMode);
  }
  server.send(200, "text/plain", "OK");
}

void handlePwr() {
  powerOn = !powerOn;
  prefs.putBool("pwr", powerOn);
  server.send(200, "text/plain", powerOn ? "ON" : "OFF");
}

// Universal handler for updating effect parameters from UI sliders
void handleUpdate() {
  if (!server.hasArg("m") || !server.hasArg("p")) {
    server.send(400); return;
  }
  int m = server.arg("m").toInt(); // Mode ID
  String p = server.arg("p");      // Parameter Name
  int v = server.hasArg("v") ? server.arg("v").toInt() : 0; // Value

  if (m == MODE_FIRE || m == MODE_FIRE_2012) {
    if (p == "cool") 
      fireCooling = v;
    if (p == "spark") 
      fireSparking = v;
  }
  else if (m == MODE_STARLIGHT) {
    if (p == "speed") 
      starlightSpeed = v;
    if (p == "dens") 
      starlightChance = v;
    if (p == "white") 
      starlightWhite = (v == 1);
  }
  else if (m == MODE_CHROMA) {
    if (p == "speed") 
      chromaSpeed = v;
  }
  else if (m == MODE_LIQUID) {
    if (p == "speed") 
      liquidSpeed = v;
  }
  else if (m == MODE_WAVE) {
    if (p == "ang")  
      waveAngle = v;
    if (p == "speed") 
      waveSpeed = v;
    if (p == "blk") 
      waveBlack = v;
    if (p == "wc") 
      waveColorMode = v;
    if (p == "cols") {
        waveColor1 = CRGB(server.arg("r1").toInt(), server.arg("g1").toInt(), server.arg("b1").toInt());
        waveColor2 = CRGB(server.arg("r2").toInt(), server.arg("g2").toInt(), server.arg("b2").toInt());
    }
  }
  else if (m == MODE_LAVA) {
    if (p == "speed") 
      lavaSpeed = v;
    if (p == "cols") {
        lavaBgColor = CRGB(server.arg("r1").toInt(), server.arg("g1").toInt(), server.arg("b1").toInt());
        lavaBlobColor = CRGB(server.arg("r2").toInt(), server.arg("g2").toInt(), server.arg("b2").toInt());
    }
  }
  else if (m == MODE_BREATHE) {
    if (p == "speed") {
        breatheSpeed = v;
        prefs.putInt("brsp", v);
    }
    if (p == "brt") {
        breatheBrightness = v;
        prefs.putInt("brbr", v);
    }
    if (p == "col") {
        breatheColor = CRGB(server.arg("r").toInt(), server.arg("g").toInt(), server.arg("b").toInt());
        prefs.putInt("brcr", breatheColor.r);
        prefs.putInt("brcg", breatheColor.g);
        prefs.putInt("brcb", breatheColor.b);
    }
  }
  server.send(200, "text/plain", "OK");
}

void handleBrt() {
  if (server.hasArg("v")) {
    globalBrightness = server.arg("v").toInt();
    FastLED.setBrightness(globalBrightness);
    prefs.putInt("brt", globalBrightness);
  }
  server.send(200, "text/plain", "OK");
}

void handleTimer() {
  if(server.hasArg("m")) {
      int m = server.arg("m").toInt();
      if(m > 0) {
          timerActive = true;
          timerOriginalMins = m;
          timerTarget = millis() + (m * 60000);
      } else {
          timerActive = false;
      }
  }
  server.send(200);
}

void handleSchedule() {
    if(server.hasArg("e")) 
      schedActive = (server.arg("e").toInt() == 1);
    if(server.hasArg("h")) 
      schedHour = server.arg("h").toInt();
    if(server.hasArg("m")) 
      schedMinute = server.arg("m").toInt();
    server.send(200);
}

void handleOffset() {
    if(server.hasArg("o")) {
        int off = server.arg("o").toInt();
        gmtOffsetSec = off * 3600;
        configTime(gmtOffsetSec, daylightOffsetSec, NTP_SERVER);
    }
    server.send(200);
}

// Returns JSON status for UI (Timer remaining)
void handleStatus() {
    long rem = 0;
    if(timerActive && powerOn) {
        long r = timerTarget - millis();
        if(r > 0) {
            rem = r / 1000;
        } else {
            timerActive = false;
        }
    }
    String json = "{\"rem\":" + String(rem) + "}";
    server.send(200, "application/json", json);
}

// Parses comma-separated LED indices from UI to create a group
void handleGrpCreate() {
  if(!server.hasArg("l")) {
      server.send(400); return;
  }
  String list = server.arg("l");
  int grpId = -1;
  // Find first free group slot
  for(int i=0; i<MAX_GROUPS; i++) {
    if(!groups[i].active) {
      groups[i].active = true;
      groups[i].color = CRGB::White;
      groups[i].effect = 0;
      groups[i].brightness = 255;
      grpId = i + 1;
      break;
    }
  }
  if(grpId == -1) {
    server.send(500, "text/plain", "Full"); return;
  }
  
  // Parse CSV string "1,5,10" etc
  int start = 0;
  int end = list.indexOf(',');
  while(end != -1) {
    int idx = list.substring(start, end).toInt();
    if(idx < NUM_LEDS) ledGroupMap[idx] = grpId;
    start = end + 1;
    end = list.indexOf(',', start);
  }
  int idx = list.substring(start).toInt();
  if(idx < NUM_LEDS) ledGroupMap[idx] = grpId;

  currentMode = MODE_GROUPS;
  server.send(200, "text/plain", String(grpId));
}

void handleGrpList() {
  String json = "[";
  bool first = true;
  for(int i=0; i<MAX_GROUPS; i++) {
    if(groups[i].active) {
      if(!first) json += ",";
      json += "{\"id\":" + String(i+1) + "}";
      first = false;
    }
  }
  json += "]";
  server.send(200, "application/json", json);
}

// Send array of LED-to-Group mappings to UI for coloring the grid
void handleGrpMap() {
  String json = "[";
  for(int i=0; i<NUM_LEDS; i++) {
    if(i>0) json += ",";
    json += String(ledGroupMap[i]);
  }
  json += "]";
  server.send(200, "application/json", json);
}

void handleGrpSet() {
  if(!server.hasArg("id")) {
      server.send(400); return;
  }
  int id = server.arg("id").toInt();
  if(id < 1 || id > MAX_GROUPS) return;
  GroupData &g = groups[id-1];
  
  if(server.hasArg("r")) g.color.r = server.arg("r").toInt();
  if(server.hasArg("g")) g.color.g = server.arg("g").toInt();
  if(server.hasArg("b")) g.color.b = server.arg("b").toInt();
  if(server.hasArg("fx")) g.effect = server.arg("fx").toInt();
  if(server.hasArg("brt")) g.brightness = server.arg("brt").toInt();
  
  currentMode = MODE_GROUPS;
  server.send(200);
}

void handleGrpDel() {
  if(!server.hasArg("id")) return;
  int id = server.arg("id").toInt();
  if(id < 1 || id > MAX_GROUPS) return;
  
  groups[id-1].active = false;
  // Clear map for this group
  for(int i=0; i<NUM_LEDS; i++) {
    if(ledGroupMap[i] == id) ledGroupMap[i] = 0;
  }
  server.send(200);
}

void setup() {
  Serial.begin(115200);
  prefs.begin(PREFS_NAMESPACE, false); // Load NVS
  
  // Load saved settings
  powerOn = prefs.getBool("pwr", true);
  currentMode = prefs.getInt("m", MODE_STATIC);
  globalBrightness = prefs.getInt("brt", DEFAULT_BRIGHTNESS);
  
  solidColor.r = prefs.getInt("sr", 0);
  solidColor.g = prefs.getInt("sg", 0);
  solidColor.b = prefs.getInt("sb", 255);
  
  breatheSpeed = prefs.getInt("brsp", DEFAULT_BREATHE_SPD);
  breatheBrightness = prefs.getInt("brbr", DEFAULT_BRIGHTNESS);
  breatheColor.r = prefs.getInt("brcr", 0);
  breatheColor.g = prefs.getInt("brcg", 255);
  breatheColor.b = prefs.getInt("brcb", 255);

  // Init HomeKit
  homeSpan.setLogLevel(2);
  homeSpan.setPairingCode(PAIRING_CODE);
  homeSpan.setQRID(QR_ID);
  homeSpan.begin(Category::Lighting, DEVICE_NAME);
  new SpanAccessory();
    new Service::AccessoryInformation();
      new Characteristic::Identify();
    new LampAccessory();

  // Init LEDs
  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS).setCorrection(TypicalLEDStrip);
  FastLED.setMaxPowerInVoltsAndMilliamps(POWER_VOLTS, POWER_MILLIAMPS);
  FastLED.clear();
  FastLED.show();
  
  // Startup Animation (Boot test)
  for(int k=0; k<2; k++) {
    for(int b=0; b<=200; b+=5) {
        fill_solid(leds, NUM_LEDS, CRGB::White);
        FastLED.setBrightness(b);
        FastLED.show();
        delay(10);
    }
    for(int b=200; b>=0; b-=5) {
        fill_solid(leds, NUM_LEDS, CRGB::White);
        FastLED.setBrightness(b);
        FastLED.show();
        delay(10);
    }
    fill_solid(leds, NUM_LEDS, CRGB::Black);
    FastLED.show();
    delay(50);
  }
  
  if (currentMode == MODE_STATIC) fill_solid(targetLeds, NUM_LEDS, solidColor);
  else fill_solid(targetLeds, NUM_LEDS, CRGB::Black);

  // Fade in to last known state
  for(int b=0; b<=globalBrightness; b+=3) {
      FastLED.setBrightness(b);
      FastLED.show();
      delay(15);
  }

  FastLED.setBrightness(globalBrightness);
  
  configTime(gmtOffsetSec, daylightOffsetSec, NTP_SERVER);

  // Register Web Routes
  server.on("/", handleRoot);
  server.on("/set", handleSet);
  server.on("/mode", handleMode);
  server.on("/update", handleUpdate);
  server.on("/brt", handleBrt);
  server.on("/pwr", handlePwr);
  server.on("/setTimer", handleTimer);
  server.on("/setSchedule", handleSchedule);
  server.on("/setOffset", handleOffset);
  server.on("/status", handleStatus);
  server.on("/grpCreate", handleGrpCreate);
  server.on("/grpList", handleGrpList);
  server.on("/grpMap", handleGrpMap);
  server.on("/grpSet", handleGrpSet);
  server.on("/grpDel", handleGrpDel);
}

// | Effects Logic |

void effectFire() {
  // Classic FastLED Fire2012 adapted for matrix
  for (int x = 0; x < MATRIX_WIDTH; x++) {
    for (int y = 0; y < MATRIX_HEIGHT; y++) {
      heatMap[x][y] = qsub8(heatMap[x][y], random8(0, ((fireCooling * 10) / MATRIX_HEIGHT) + 2));
    }
    for (int y = MATRIX_HEIGHT - 1; y >= 2; y--) {
      heatMap[x][y] = (heatMap[x][y - 1] + heatMap[x][y - 2] + heatMap[x][y - 2]) / 3;
    }
    if (random8() < fireSparking) {
      int y = random8(3);
      heatMap[x][y] = qadd8(heatMap[x][y], random8(160, 255));
    }
    for (int y = 0; y < MATRIX_HEIGHT; y++) {
      targetLeds[mapPixels(x, y)] = HeatColor(heatMap[x][y]);
    }
  }
}

void effectFire2012() {
  // Fire effect calculated on a virtual width, then mirrored/mapped
  static byte heatMap2012[FIRE_WIDTH][MATRIX_HEIGHT]; 

  for (int g = 0; g < FIRE_WIDTH; g++) {
    for (int y = 0; y < MATRIX_HEIGHT; y++) {
      heatMap2012[g][y] = qsub8(heatMap2012[g][y], random8(0, ((fireCooling * 10) / MATRIX_HEIGHT) + 2));
    }

    for (int y = MATRIX_HEIGHT - 1; y >= 2; y--) {
      heatMap2012[g][y] = (heatMap2012[g][y - 1] + heatMap2012[g][y - 2] + heatMap2012[g][y - 2]) / 3;
    }

    if (random8() < fireSparking) {
      int y = random8(3);
      heatMap2012[g][y] = qadd8(heatMap2012[g][y], random8(160, 255));
    }

    for (int y = 0; y < MATRIX_HEIGHT; y++) {
      CRGB color = HeatColor(heatMap2012[g][y]);
      int col1 = g * 2;
      int col2 = g * 2 + 1;
      targetLeds[mapPixels(col1, y)] = color;
      targetLeds[mapPixels(col2, y)] = color;
    }
  }
}

void effectStarlight() {
  fadeToBlackBy(targetLeds, NUM_LEDS, starlightSpeed);
  if (random8() < starlightChance) {
    int pos = random16(1, NUM_LEDS);
    targetLeds[pos] = starlightWhite ? CRGB::White : starlightColor;
  }
}

void effectChroma() {
  uint32_t ms = millis();
  int yHueDelta = 30;
  int xHueDelta = 20;
  for (int x = 0; x < MATRIX_WIDTH; x++) {
    for (int y = 0; y < MATRIX_HEIGHT; y++) {
      byte hue = (ms / chromaSpeed) + (x * xHueDelta) + (y * yHueDelta);
      byte sat = 240;
      byte val = sin8((ms / (chromaSpeed < 5 ? 2 : 8)) + (y * 16) + (x * 8));
      targetLeds[mapPixels(x, y)] = CHSV(hue, sat, map(val, 0, 255, 100, 255));
    }
  }
}

void effectLiquid() {
  // Simplex noise based liquid effect
  uint8_t scale = 30;
  for (int y = 0; y < MATRIX_HEIGHT; y++) {
    for (int x = 0; x < MATRIX_WIDTH; x++) {
      uint8_t noise = inoise8(x * scale + xOffset, y * scale + yOffset, zOffset);
      uint8_t hue = noise + (zOffset / 10);
      targetLeds[mapPixels(x, y)] = CHSV(hue, 255, 255);
    }
  }
  zOffset += liquidSpeed;
  xOffset += (liquidSpeed / 2);
  yOffset += (liquidSpeed / 4);
}

void effectWave() {
  float rad = waveAngle * 0.0174533; // Deg to Rad
  uint32_t ms = millis();
  float cost = cos(rad);
  float sint = sin(rad);

  uint32_t speedFactor = ms * waveSpeed / 32;

  for(int x=0; x<MATRIX_WIDTH; x++) {
    for(int y=0; y<MATRIX_HEIGHT; y++) {
       float rotPos = (x * cost) + (y * sint);
       uint8_t waveVal = sin8((rotPos * 25) + speedFactor);
       
       CRGB pixelColor;
       
       if (waveColorMode == 0) { // Solid color with black waves
         pixelColor = solidColor;
         if(waveVal < waveBlack) {
             waveVal = 0;
         } else {
             waveVal = map(waveVal, waveBlack, 255, 0, 255);
         }
         pixelColor.nscale8(waveVal);
       }
       else { // Gradient between two colors
         pixelColor = blend(waveColor1, waveColor2, waveVal);
         if(waveVal < waveBlack) pixelColor = CRGB::Black;
       }

       targetLeds[mapPixels(x,y)] = pixelColor;
    }
  }
}

void effectLava() {
    // 3D Noise generator for lava lamp look
    uint32_t ms = millis();
    uint16_t scale = 35;
    uint32_t speed = lavaSpeed * 8;
    for(uint8_t x = 0; x < MATRIX_WIDTH; x++) {
        for(uint8_t y = 0; y < MATRIX_HEIGHT; y++) {
            uint16_t realX = x * scale;
            uint16_t realY = (y * scale) - (ms * speed / 255);
            uint16_t realZ = ms / 12;
            uint8_t noise = inoise8(realX, realY, realZ);
            uint8_t blendAmt = 0;
            // Thresholding noise to create "blobs"
            if(noise > 180) {
                blendAmt = 255;
            } else if(noise > 120) {
                blendAmt = map(noise, 120, 180, 0, 255);
            } else {
                blendAmt = 0;
            }
            leds[mapPixels(x,y)] = blend(lavaBgColor, lavaBlobColor, dim8_raw(blendAmt));
        }
    }
}

void effectBreathe() {
    uint8_t bright = beatsin8(breatheSpeed, 0, breatheBrightness);
    CRGB col = breatheColor;
    col.nscale8(bright);
    fill_solid(targetLeds, NUM_LEDS, col);
}

void renderGroups() {
  fill_solid(targetLeds, NUM_LEDS, CRGB::Black);
  uint32_t ms = millis();
  
  // Iterate all pixels and check if they belong to a group
  for(int i=0; i<NUM_LEDS; i++) {
    uint8_t gid = ledGroupMap[i];
    if(gid > 0 && gid <= MAX_GROUPS) {
      GroupData &g = groups[gid-1];
      if(g.active) {
        CRGB col = g.color;
        if(g.effect == 1) { // Breathe
           uint8_t b = beatsin8(30, 50, 255);
           col.nscale8(b);
        } else if(g.effect == 2) { // Rainbow
           col = CHSV((ms/20 + i*10)%255, 255, 255);
        }
        col.nscale8(g.brightness);
        targetLeds[i] = col;
      }
    }
  }
}

void loop() {
  homeSpan.poll(); // Keep HomeKit connection alive
  
  if (WiFi.status() == WL_CONNECTED) {
    if (!webServerStarted) {
      server.begin();
      webServerStarted = true;
    }
    server.handleClient();
  }

  // Auto-off Timer Logic
  if (timerActive && powerOn) {
    if (millis() > timerTarget) {
        powerOn = false;
        prefs.putBool("pwr", false);
        timerActive = false;
    }
  }

  // Schedule Logic
  if (schedActive && powerOn) {
    struct tm timeinfo;
    if(getLocalTime(&timeinfo)) {
        if (timeinfo.tm_hour == schedHour && timeinfo.tm_min == schedMinute && timeinfo.tm_sec == 0) {
            powerOn = false;
            prefs.putBool("pwr", false);
        }
    }
  }

  if (powerOn) {
    if (currentMode == MODE_STATIC) fill_solid(targetLeds, NUM_LEDS, solidColor);
    else if (currentMode == MODE_FIRE) effectFire();
    else if (currentMode == MODE_STARLIGHT) effectStarlight();
    else if (currentMode == MODE_CHROMA) effectChroma();
    else if (currentMode == MODE_LIQUID) effectLiquid();
    else if (currentMode == MODE_WAVE) effectWave();
    else if (currentMode == MODE_GROUPS) renderGroups();
    else if (currentMode == MODE_LAVA) effectLava();
    else if (currentMode == MODE_BREATHE) effectBreathe();
    else if (currentMode == MODE_FIRE_2012) effectFire2012();
  } else {
    fill_solid(targetLeds, NUM_LEDS, CRGB::Black);
  }

  // Smooth transition between frames (temporal dithering/smoothing)
  for(int i = 0; i < NUM_LEDS; i++) {
    leds[i] = nblend(leds[i], targetLeds[i], 15);
  }

  // Hardcode first LED off (optional, depends on wiring)
  leds[0] = CRGB::Black;

  EVERY_N_MILLISECONDS(FRAME_DELAY_MS) {
    FastLED.show();
  }
}
#include "web_page.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEClient.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <base64.h>

Preferences preferences;

// ====================================================
// Dual OLED Displays Configuration (128x32 each)
// Total Virtual Resolution: 256x32 (Side-by-Side)
// ====================================================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32
#define OLED_RESET -1

// Left Screen on default hardware I2C: Wire (SDA=21, SCL=22)
Adafruit_SSD1306 displayL(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
bool leftDisplayAvailable = false;

// Right Screen on secondary hardware I2C: Wire1 (SDA=18, SCL=19)
Adafruit_SSD1306 displayR(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire1, OLED_RESET);
bool rightDisplayAvailable = false;

// ====================================================
// Hardware Pinout Reference
// ====================================================
const int BTN_MENU = 32;   // Button 1: Menu / Next (Internal Pullup)
const int BTN_SELECT = 33; // Button 2: Select / Action (Internal Pullup)
const int BTN_BACK = 27;   // Button 3: Back / Mood (Internal Pullup)
const int BUZZER_PIN = 26; // Audio Buzzer

// ====================================================
// WiFi & Embedded Web Server Configuration
// ====================================================
const char *apSSID =
    "DeskBot-WiFi";      // Hotspot name visible in phone/laptop WiFi settings
const char *apPass = ""; // Open network (no password needed to connect)
char wifiSSID[32] = "Airtel_402"; // Fallback/Default router SSID
char wifiPass[64] = "9550175678"; // Fallback/Default router password
bool wifiConnected = false;
String espIPAddress = "192.168.4.1";
WebServer server(80);

// ====================================================
// Cloud MQTT Bridge Configuration (24/7 HTTPS Web Sync)
// ====================================================
const char* mqttServer = "broker.emqx.io";
const int mqttPort = 1883;
const char* mqttTopicCmd = "deskbot/2b4fcd/cmd";
const char* mqttTopicStatus = "deskbot/2b4fcd/status";

WiFiClient mqttTcpClient;
PubSubClient mqttClient(mqttTcpClient);
unsigned long lastMqttReconnectAttempt = 0;
unsigned long lastMqttStatusPublish = 0;

// ====================================================
// Spotify Web API Configuration
// (Get free Client ID & Secret from developer.spotify.com)
// ====================================================
String spotifyClientId = "001f2f27609349db91cea22353df81ce";
String spotifyClientSecret = "9e26492500724b659b5a3ae35d389752";
String spotifyRefreshToken = "AQAqe4VQuN7ZnP-mE1rnsICJg_-JBk1W0ZGa2qVx0h5lB-"
                             "KjorgUhXj3qJ4FhSDIo0XqslcfNyVBbg30RfSRVgEz4SEd01f"
                             "BE-Jr2NOcQoJSFi_LxYgCbqLyK1STx1ZgyV4";
String spotifyAccessToken = "";
unsigned long spotifyTokenExpiry = 0;
unsigned long lastSpotifyApiPoll = 0;
bool spotifyApiActive = false;

// Live Spotify state fetched from Spotify Web API
String liveSpotifyTrack = "Starboy";
String liveSpotifyArtist = "The Weeknd";
int liveSpotifyProgressSec = 78;
int liveSpotifyTotalSec = 230;
bool liveSpotifyPlaying = true;
bool spotifyPlaying = true;

// ====================================================
// Application Modes
// ====================================================
enum BotMode {
  MODE_COMPANION = 0,   // Animated Dual Eyes (0)
  MODE_POMODORO = 1,    // Focus Timer (1)
  MODE_OLLIE = 2,       // Sphero Ollie BLE (2)
  MODE_REFLEX_GAME = 3, // Reflex Minigame (3)
  MODE_SPOTIFY = 4,     // Spotify Controller (4)
  MODE_SYSTEM_INFO = 5, // System Telemetry (5)
  MODE_MENU = 6         // Main Menu (6)
};

BotMode currentMode = MODE_COMPANION;

const int TOTAL_MENU_ITEMS = 6;
int selectedMenuItem = 0;
const char *menuNames[TOTAL_MENU_ITEMS] = {
    "1. Dual Eyes",       "2. Focus Pomodoro", "3. Ollie Battery",
    "4. Reflex Reaction", "5. Spotify Player", "6. System Status"};

// ====================================================
// Companion Eyes Expressions
// ====================================================
enum EyeExpression {
  EXPR_NEUTRAL,
  EXPR_LOOK_LEFT,
  EXPR_LOOK_RIGHT,
  EXPR_HAPPY,
  EXPR_SURPRISED,
  EXPR_SLEEPY,
  EXPR_WINK
};

EyeExpression currentExpr = EXPR_NEUTRAL;
bool isBlinking = false;
unsigned long lastBlinkTime = 0;
unsigned long blinkInterval = 3400;
unsigned long blinkStartTime = 0;
unsigned long lastEyeShiftTime = 0;

// ====================================================
// Focus Pomodoro Timer State
// ====================================================
bool timerRunning = false;
unsigned long timerTotalSeconds = 25 * 60;
unsigned long timerRemainingSeconds = 25 * 60;
unsigned long lastTimerTick = 0;

// ====================================================
// Sphero Ollie BLE Controller (v4.0 Verified Protocol)
// Target Device: 2B-4FCD (MAC: ea:a4:5f:42:4f:cd)
// ====================================================
static BLEAddress targetOllieAddress("ea:a4:5f:42:4f:cd");

static BLEUUID antiDosServiceUUID("22bb746f-2bb0-7554-2d6f-726568705327");
static BLEUUID antiDosCharUUID("22bb746f-2bbd-7554-2d6f-726568705327");
static BLEUUID txPowerCharUUID("22bb746f-2bb2-7554-2d6f-726568705327");
static BLEUUID wakeCharUUID("22bb746f-2bbf-7554-2d6f-726568705327");

static BLEUUID robotControlServiceUUID("22bb746f-2ba0-7554-2d6f-726568705327");
static BLEUUID commandCharUUID("22bb746f-2ba1-7554-2d6f-726568705327");
static BLEUUID responseCharUUID("22bb746f-2ba6-7554-2d6f-726568705327");

BLEClient* pOllieClient = nullptr;
BLERemoteCharacteristic* pOllieAntiDosChar = nullptr;
BLERemoteCharacteristic* pOllieTxPowerChar = nullptr;
BLERemoteCharacteristic* pOllieWakeChar = nullptr;
BLERemoteCharacteristic* pOllieCmdChar = nullptr;
BLERemoteCharacteristic* pOllieRespChar = nullptr;

BLEAdvertisedDevice* foundOllieDevice = nullptr;
bool ollieConnected = false;
int ollieBatteryPercent = 90;
int ollieColorIndex = 0;
const int TOTAL_OLLIE_COLORS = 5;
const char *ollieColors[TOTAL_OLLIE_COLORS] = {"Cyan", "Purple", "Green", "Red", "Yellow"};
const uint8_t ollieColorRGB[TOTAL_OLLIE_COLORS][3] = {
  { 0, 229, 255 },   // Cyan
  { 179, 136, 255 }, // Purple
  { 0, 230, 118 },   // Green
  { 255, 23, 68 },    // Red
  { 255, 214, 0 }     // Yellow
};

uint8_t olliePacketSeq = 0;
unsigned long lastOllieKeepAlive = 0;

void ollieNotifyCallback(BLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify) {
  // Asynchronous response / battery / sensor notification from Ollie
}

// Send Sphero Command Packet (withResponse = true is CRITICAL!)
void sendSpheroPacket(uint8_t did, uint8_t cid, uint8_t* data, uint8_t dataLen, bool withResponse = true) {
  if (!pOllieCmdChar || !ollieConnected) return;
  if (pOllieClient && !pOllieClient->isConnected()) {
    ollieConnected = false;
    return;
  }

  uint8_t packet[32];
  packet[0] = 0xFF; // SOP1
  packet[1] = 0xFE; // SOP2 (resetTimeout=1, answer=0)
  packet[2] = did;
  packet[3] = cid;
  packet[4] = olliePacketSeq++;
  packet[5] = dataLen + 1;

  uint16_t sum = packet[2] + packet[3] + packet[4] + packet[5];
  for (uint8_t i = 0; i < dataLen; i++) {
    packet[6 + i] = data[i];
    sum += data[i];
  }
  packet[6 + dataLen] = ~(sum & 0xFF);
  uint8_t totalPacketLen = 7 + dataLen;

  pOllieCmdChar->writeValue(packet, totalPacketLen, withResponse);
}

void setOllieColor(uint8_t r, uint8_t g, uint8_t b) {
  Serial.printf("[Ollie] Color: R=%d G=%d B=%d\n", r, g, b);
  uint8_t colorData[] = { r, g, b, 0x01 };
  sendSpheroPacket(0x02, 0x20, colorData, sizeof(colorData));
}

void setOllieBackLed(uint8_t brightness) {
  uint8_t ledData[] = { brightness };
  sendSpheroPacket(0x02, 0x21, ledData, sizeof(ledData));
}

void setOllieStabilization(bool enable) {
  uint8_t stabData[] = { (uint8_t)(enable ? 0x01 : 0x00) };
  sendSpheroPacket(0x02, 0x02, stabData, sizeof(stabData));
}

void rollOllie(uint8_t speed, uint16_t heading, uint8_t state) {
  uint8_t rollData[] = {
    speed,
    (uint8_t)((heading >> 8) & 0xFF),
    (uint8_t)(heading & 0xFF),
    state
  };
  sendSpheroPacket(0x02, 0x30, rollData, sizeof(rollData), true);
}

void stopOllie() {
  rollOllie(0, 0, 0);
  delay(50);
}

// Drive specific distance in centimeters!
void rollOllieDistanceCm(int distanceCm, uint16_t heading) {
  if (distanceCm < 5) distanceCm = 5;
  if (distanceCm > 200) distanceCm = 200;

  // Approx 55ms per centimeter at speed 80
  unsigned long durationMs = distanceCm * 55;

  Serial.printf("[Ollie] Rolling %d cm (heading %d) for %lu ms...\n", distanceCm, heading, durationMs);
  setOllieColor(0, 255, 0); // Green while driving
  rollOllie(80, heading, 1);
  delay(durationMs);
  stopOllie();

  // Restore current color
  setOllieColor(ollieColorRGB[ollieColorIndex][0], 
                ollieColorRGB[ollieColorIndex][1], 
                ollieColorRGB[ollieColorIndex][2]);
}

void spinTrick() {
  Serial.println(F("[Ollie] 360 Spin Trick!"));
  setOllieStabilization(false);
  setOllieColor(255, 0, 255); // Magenta

  uint8_t rawMotor[] = { 0x01, 110, 0x02, 110 }; // Left fwd, right rev
  sendSpheroPacket(0x02, 0x33, rawMotor, sizeof(rawMotor));
  delay(650);

  uint8_t stopMotor[] = { 0x00, 0, 0x00, 0 };
  sendSpheroPacket(0x02, 0x33, stopMotor, sizeof(stopMotor));
  setOllieStabilization(true);

  setOllieColor(ollieColorRGB[ollieColorIndex][0], 
                ollieColorRGB[ollieColorIndex][1], 
                ollieColorRGB[ollieColorIndex][2]);
}

void sendOllieKeepAlive() {
  sendSpheroPacket(0x00, 0x01, nullptr, 0, true);
}

class DeskBotOllieScanCallbacks: public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    if (foundOllieDevice != nullptr) return;
    String name = advertisedDevice.getName().c_str();
    if (name.startsWith("2B-") || name.indexOf("Ollie") != -1 || advertisedDevice.getAddress().equals(targetOllieAddress)) {
      foundOllieDevice = new BLEAdvertisedDevice(advertisedDevice);
      Serial.printf("[BLE Scan] Found Ollie: %s\n", advertisedDevice.getAddress().toString().c_str());
    }
  }
};

bool setupOllieServices() {
  Serial.println(F("[BLE] Linking Ollie Services..."));

  BLERemoteService* pAntiDosService = pOllieClient->getService(antiDosServiceUUID);
  if (!pAntiDosService) return false;

  pOllieAntiDosChar = pAntiDosService->getCharacteristic(antiDosCharUUID);
  pOllieTxPowerChar = pAntiDosService->getCharacteristic(txPowerCharUUID);
  pOllieWakeChar = pAntiDosService->getCharacteristic(wakeCharUUID);
  if (!pOllieAntiDosChar || !pOllieTxPowerChar || !pOllieWakeChar) return false;

  // 1. Anti-DoS Unlock "011i3"
  const char* antiDosKey = "011i3";
  pOllieAntiDosChar->writeValue((uint8_t*)antiDosKey, strlen(antiDosKey), true);
  delay(120);

  // 2. Set TX Power
  uint8_t txPower2[] = { 0x00, 0x07 };
  pOllieTxPowerChar->writeValue(txPower2, 2, true);
  delay(120);

  // 3. Wake CPU
  uint8_t wakeVal = 0x01;
  pOllieWakeChar->writeValue(&wakeVal, 1, true);
  delay(250);

  // 4. Robot Control Service
  BLERemoteService* pControlService = pOllieClient->getService(robotControlServiceUUID);
  if (!pControlService) return false;

  pOllieCmdChar = pControlService->getCharacteristic(commandCharUUID);
  if (!pOllieCmdChar) return false;

  pOllieRespChar = pControlService->getCharacteristic(responseCharUUID);
  if (pOllieRespChar && pOllieRespChar->canNotify()) {
    pOllieRespChar->registerForNotify(ollieNotifyCallback);
    delay(100);
  }

  ollieConnected = true;
  lastOllieKeepAlive = millis();
  Serial.println(F("[BLE] ✅ OLLIE LINKED & READY!"));

  setOllieStabilization(true);
  delay(100);
  setOllieColor(ollieColorRGB[ollieColorIndex][0], 
                ollieColorRGB[ollieColorIndex][1], 
                ollieColorRGB[ollieColorIndex][2]);
  setOllieBackLed(255);
  return true;
}

class DeskBotClientCallbacks : public BLEClientCallbacks {
  void onConnect(BLEClient* pclient) {
    Serial.println(F("[BLE] Ollie connected!"));
    ollieConnected = true;
  }
  void onDisconnect(BLEClient* pclient) {
    Serial.println(F("[BLE] Ollie disconnected!"));
    ollieConnected = false;
  }
};

bool connectToOllie() {
  if (ollieConnected && pOllieClient && pOllieClient->isConnected()) return true;

  if (!pOllieClient) {
    pOllieClient = BLEDevice::createClient();
    pOllieClient->setClientCallbacks(new DeskBotClientCallbacks());
  }

  if (foundOllieDevice != nullptr) {
    BLEAddress addr = foundOllieDevice->getAddress();
    esp_ble_addr_type_t addrType = (esp_ble_addr_type_t)foundOllieDevice->getAddressType();
    if (pOllieClient->connect(addr, addrType)) {
      return setupOllieServices();
    }
  }

  // Direct connect RANDOM address
  if (pOllieClient->connect(targetOllieAddress, BLE_ADDR_TYPE_RANDOM)) {
    return setupOllieServices();
  }

  return false;
}

// ====================================================
// Offline Fallback Playlist
// ====================================================
int spotifyTrackIdx = 0;
const int TOTAL_SPOTIFY_TRACKS = 4;
const char *spotifyTracks[TOTAL_SPOTIFY_TRACKS] = {
    "Starboy", "Blinding Lights", "Get Lucky", "Midnight City"};
const char *spotifyArtists[TOTAL_SPOTIFY_TRACKS] = {"The Weeknd", "The Weeknd",
                                                    "Daft Punk", "M83"};
const int spotifyTrackLengths[TOTAL_SPOTIFY_TRACKS] = {230, 200, 248, 243};

// ====================================================
// Reflex Game State
// ====================================================
enum ReflexState {
  REFLEX_WAIT_START,
  REFLEX_WAIT_CUE,
  REFLEX_SHOW_CUE,
  REFLEX_RESULT,
  REFLEX_FALSE_START
};

ReflexState reflexState = REFLEX_WAIT_START;
unsigned long reflexWaitTargetTime = 0;
unsigned long reflexCueStartTime = 0;
unsigned long reflexReactionTime = 0;

// ====================================================
// Button Software Debounce (35ms Filter)
// ====================================================
int btn1LastRaw = HIGH;
int btn2LastRaw = HIGH;
int btn3LastRaw = HIGH;

int btn1Stable = HIGH;
int btn2Stable = HIGH;
int btn3Stable = HIGH;

unsigned long btn1LastChange = 0;
unsigned long btn2LastChange = 0;
unsigned long btn3LastChange = 0;

bool btn1IsHeld = false;
bool btn2IsHeld = false;
bool btn3IsHeld = false;

unsigned long modeSwitchCooldownUntil = 0;
const unsigned long DEBOUNCE_DELAY = 35;

void switchMode(BotMode newMode) {
  currentMode = newMode;
  modeSwitchCooldownUntil = millis() + 300;
  if (newMode == MODE_SPOTIFY) {
    lastSpotifyApiPoll = 0; // Trigger Spotify poll immediately
  }
}

// ====================================================
// Forward Declarations
// ====================================================
void playTone(int freq, int durationMs);
void soundChirp();
void soundHappy();
void soundPoke();
void soundAlarm();
void soundCoin();
void soundVictory();
void soundPowerup();

void drawEyeOnDisplay(Adafruit_SSD1306 &disp, bool isLeftEye);
void drawBothEyesOnSingleDisplay(Adafruit_SSD1306 &disp);
void drawLiveButtonIndicators(Adafruit_SSD1306 &disp);
void drawRightScreenPanel();
void drawMenuRight();
void drawPomodoroRight();
void drawOllieRight();
void drawReflexGameRight();
void drawSpotifyRight();
void drawSystemInfoRight();

void handleButtons();
void updateAsyncState();
void renderDisplays();
void setupWebServer();
String jsonExtractString(const String &json, const String &key,
                         int searchStart = 0);
bool jsonExtractBool(const String &json, const String &key,
                     int searchStart = 0);
long jsonExtractLong(const String &json, const String &key,
                     int searchStart = 0);
void refreshSpotifyToken();
bool exchangeSpotifyAuthCode(String code);
void pollSpotifyCurrentlyPlaying();
void sendSpotifyCommand(const char *endpoint, const char *method);

// ====================================================
// SETUP
// ====================================================
void setup() {
  Serial.begin(115200);
  Serial.println(F("\n=== DeskBot 2.0 Starting Up ==="));

  // 1. Audio boot beep FIRST
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, HIGH);
  delay(60);
  digitalWrite(BUZZER_PIN, LOW);

  // 2. Buttons (All 3 with internal pullups)
  pinMode(BTN_MENU, INPUT_PULLUP);
  pinMode(BTN_SELECT, INPUT_PULLUP);
  pinMode(BTN_BACK, INPUT_PULLUP);

  btn1Stable = btn1LastRaw = digitalRead(BTN_MENU);
  btn2Stable = btn2LastRaw = digitalRead(BTN_SELECT);
  btn3Stable = btn3LastRaw = digitalRead(BTN_BACK);

  // 3. Initialize Left Display on Wire (SDA=21, SCL=22)
  Wire.begin(21, 22);
  Wire.setTimeOut(25);
  leftDisplayAvailable =
      displayL.begin(SSD1306_SWITCHCAPVCC, 0x3C, false, false);
  if (!leftDisplayAvailable) {
    leftDisplayAvailable =
        displayL.begin(SSD1306_SWITCHCAPVCC, 0x3D, false, false);
  }

  // 4. Initialize Right Display on Wire1 (SDA=18, SCL=19)
  Wire1.begin(18, 19);
  Wire1.setTimeOut(25);
  rightDisplayAvailable =
      displayR.begin(SSD1306_SWITCHCAPVCC, 0x3C, false, false);
  if (!rightDisplayAvailable) {
    rightDisplayAvailable =
        displayR.begin(SSD1306_SWITCHCAPVCC, 0x3D, false, false);
  }

  // 5. Welcome Splash
  if (leftDisplayAvailable) {
    displayL.clearDisplay();
    displayL.setTextSize(1);
    displayL.setTextColor(SSD1306_WHITE);
    displayL.setCursor(18, 12);
    displayL.println(F("LEFT OLED OK"));
    displayL.display();
  }

  if (rightDisplayAvailable) {
    displayR.clearDisplay();
    displayR.setTextSize(1);
    displayR.setTextColor(SSD1306_WHITE);
    displayR.setCursor(14, 12);
    displayR.println(F("RIGHT OLED OK"));
    displayR.display();
  }

  // 6. Load saved WiFi from Flash memory (competition & travel ready!)
  preferences.begin("deskbot", false);
  String storedSSID = preferences.getString("ssid", "Airtel_402");
  String storedPass = preferences.getString("pass", "9550175678");
  strncpy(wifiSSID, storedSSID.c_str(), sizeof(wifiSSID));
  strncpy(wifiPass, storedPass.c_str(), sizeof(wifiPass));

  // PURGE any corrupted or experimental Spotify credentials saved previously!
  preferences.remove("sp_cid");
  preferences.remove("sp_sec");
  preferences.remove("sp_tok");

  // Restore known verified working Spotify account
  spotifyClientId = "001f2f27609349db91cea22353df81ce";
  spotifyClientSecret = "9e26492500724b659b5a3ae35d389752";
  spotifyRefreshToken = "AQAqe4VQuN7ZnP-mE1rnsICJg_-JBk1W0ZGa2qVx0h5lB-"
                        "KjorgUhXj3qJ4FhSDIo0XqslcfNyVBbg30RfSRVgEz4SEd01fBE-"
                        "Jr2NOcQoJSFi_LxYgCbqLyK1STx1ZgyV4";
  Serial.println(F("[Spotify] Reset credentials to verified working account!"));

  // Launch DeskBot-WiFi Hotspot (SoftAP) + Station Mode
  WiFi.mode(WIFI_AP_STA);
  WiFi.setSleep(false); // Crucial for ESP32 WiFi + BLE Coexistence without packet loss!
  WiFi.softAP(apSSID, apPass);
  IPAddress apIP = WiFi.softAPIP();
  espIPAddress = apIP.toString();

  // Try to connect to saved router (Home router or Competition venue)
  if (strlen(wifiSSID) > 0) {
    if (rightDisplayAvailable) {
      displayR.clearDisplay();
      displayR.setCursor(0, 4);
      displayR.print(F("JOINING ROUTER..."));
      displayR.setCursor(0, 16);
      displayR.print(wifiSSID);
      displayR.display();
    }
    Serial.printf("[WiFi] Connecting to '%s'...\n", wifiSSID);
    WiFi.begin(wifiSSID, wifiPass);
    unsigned long startAttempt = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 8000) {
      delay(200);
    }
    if (WiFi.status() == WL_CONNECTED) {
      wifiConnected = true;
      espIPAddress = WiFi.localIP().toString();
      Serial.printf("[WiFi] Connected! IP: %s\n", espIPAddress.c_str());
      soundVictory();
      refreshSpotifyToken();
      pollSpotifyCurrentlyPlaying();
    } else {
      Serial.println(F("[WiFi] Router connection timed out. DeskBot-WiFi "
                       "Hotspot is on 192.168.4.1"));
    }
  }

  // Web server runs continuously on 192.168.4.1 (and home IP if connected)
  setupWebServer();

  // Initialize Cloud MQTT Client for 24/7 HTTPS Web Sync
  mqttClient.setServer(mqttServer, mqttPort);
  mqttClient.setCallback(onMqttMessage);
  mqttClient.setBufferSize(512);

  // Initialize BLE & auto-connect to Sphero Ollie (2B-4FCD)
  BLEDevice::init("DeskBot-2.0");
  Serial.println(F("[BLE] Attempting auto-connection to Sphero Ollie (2B-4FCD)..."));
  connectToOllie();

  if (rightDisplayAvailable) {
    displayR.clearDisplay();
    if (wifiConnected) {
      displayR.setCursor(0, 2);
      displayR.print(F("WIFI: ONLINE"));
      displayR.setCursor(0, 14);
      displayR.print(F("IP: "));
      displayR.print(espIPAddress);
      delay(1800);
    } else {
      displayR.setCursor(0, 2);
      displayR.print(F("HOTSPOT: DeskBot"));
      displayR.setCursor(0, 14);
      displayR.print(F("IP: 192.168.4.1"));
    }
    displayR.display();
    delay(1800);
  }

  switchMode(MODE_COMPANION);
}

// ====================================================
// MAIN LOOP
// ====================================================
void loop() {
  handleButtons();
  updateAsyncState();

  // Sphero Ollie 2.5-second keep-alive ping
  if (ollieConnected && pOllieClient && pOllieClient->isConnected() && millis() - lastOllieKeepAlive >= 2500) {
    lastOllieKeepAlive = millis();
    sendOllieKeepAlive();
  }

  // Always service web requests (whether on DeskBot-WiFi or home network)
  server.handleClient();

  renderDisplays();
  delay(25); // ~40 FPS target
}

// ====================================================
// EMBEDDED REST API WEB SERVER
// ====================================================
void sendCorsHeader() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET,POST,OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "*");
}

void handleOptions() {
  sendCorsHeader();
  server.send(204);
}

void setupWebServer() {
  server.on("/api/status", HTTP_GET, []() {
    sendCorsHeader();
    String json = "{";
    json += "\"mode\":" + String(currentMode) + ",";
    json += "\"mood\":" + String(currentExpr) + ",";
    json +=
        "\"wifi\":{\"connected\":" + String(wifiConnected ? "true" : "false") +
        ",\"ip\":\"" + espIPAddress + "\"},";
    json += "\"spotify\":{";
    json +=
        "\"apiActive\":" + String(spotifyApiActive ? "true" : "false") + ",";
    json += "\"playing\":" +
            String(spotifyApiActive ? (liveSpotifyPlaying ? "true" : "false")
                                    : (spotifyPlaying ? "true" : "false")) +
            ",";
    json += "\"track\":\"" +
            (spotifyApiActive ? liveSpotifyTrack
                              : String(spotifyTracks[spotifyTrackIdx])) +
            "\",";
    json += "\"artist\":\"" +
            (spotifyApiActive ? liveSpotifyArtist
                              : String(spotifyArtists[spotifyTrackIdx])) +
            "\",";
    json += "\"progress\":" +
            String(spotifyApiActive ? liveSpotifyProgressSec
                                    : (millis() / 1000) % 200) +
            ",";
    json += "\"total\":" + String(spotifyApiActive
                                      ? liveSpotifyTotalSec
                                      : spotifyTrackLengths[spotifyTrackIdx]);
    json += "},";
    json += "\"ollie\":{\"connected\":" + String(ollieConnected ? "true" : "false") +
            ",\"battery\":" + String(ollieBatteryPercent) +
            ",\"color\":\"" + String(ollieColors[ollieColorIndex]) + "\"},";
    json +=
        "\"pomodoro\":{\"running\":" + String(timerRunning ? "true" : "false") +
        ",\"remaining\":" + String(timerRemainingSeconds) + "}";
    json += "}";
    server.send(200, "application/json", json);
  });

  auto sendOkResponse = []() {
    sendCorsHeader();
    if (server.hasHeader("Referer") && !server.hasHeader("X-Requested-With")) {
      server.sendHeader("Location", "/");
      server.send(303);
    } else {
      server.send(200, "application/json", "{\"status\":\"ok\"}");
    }
  };

  server.on("/api/mode", HTTP_GET, [sendOkResponse]() {
    sendCorsHeader();
    if (server.hasArg("set")) {
      int m = server.arg("set").toInt();
      if (m >= 0 && m < TOTAL_MENU_ITEMS) {
        switchMode((BotMode)m);
        soundChirp();
      }
    }
    sendOkResponse();
  });

  server.on("/api/mood", HTTP_GET, [sendOkResponse]() {
    sendCorsHeader();
    if (server.hasArg("set")) {
      String m = server.arg("set");
      if (m == "neutral")
        currentExpr = EXPR_NEUTRAL;
      else if (m == "happy")
        currentExpr = EXPR_HAPPY;
      else if (m == "look_left")
        currentExpr = EXPR_LOOK_LEFT;
      else if (m == "look_right")
        currentExpr = EXPR_LOOK_RIGHT;
      else if (m == "surprised")
        currentExpr = EXPR_SURPRISED;
      else if (m == "sleepy")
        currentExpr = EXPR_SLEEPY;
      else if (m == "wink")
        currentExpr = EXPR_WINK;
      soundHappy();
    }
    sendOkResponse();
  });

  server.on("/api/spotify", HTTP_GET, [sendOkResponse]() {
    sendCorsHeader();
    if (server.hasArg("action")) {
      String act = server.arg("action");
      if (act == "play") {
        if (spotifyApiActive)
          sendSpotifyCommand("play", "PUT");
        else
          spotifyPlaying = true;
        soundHappy();
      } else if (act == "pause") {
        if (spotifyApiActive)
          sendSpotifyCommand("pause", "PUT");
        else
          spotifyPlaying = false;
        soundChirp();
      } else if (act == "next") {
        if (spotifyApiActive)
          sendSpotifyCommand("next", "POST");
        else
          spotifyTrackIdx = (spotifyTrackIdx + 1) % TOTAL_SPOTIFY_TRACKS;
        soundChirp();
      }
    }
    sendOkResponse();
  });

  // Sphero Ollie Controller API (2-Axis Pilot, Precision cm & Tricks)
  server.on("/api/ollie", HTTP_GET, [sendOkResponse]() {
    sendCorsHeader();
    String action = server.hasArg("action") ? server.arg("action") : "";

    if (action == "move_cm") {
      int dist = server.hasArg("distance") ? server.arg("distance").toInt() : 20;
      int heading = server.hasArg("heading") ? server.arg("heading").toInt() : 0;
      rollOllieDistanceCm(dist, heading);
      server.send(200, "application/json", "{\"status\":\"ok\",\"action\":\"move_cm\",\"distance\":" + String(dist) + "}");
      return;
    }
    else if (action == "roll" || action == "drive") {
      int speed = server.hasArg("speed") ? server.arg("speed").toInt() : 80;
      int heading = server.hasArg("heading") ? server.arg("heading").toInt() : 0;
      rollOllie(speed, heading, 1);
      server.send(200, "application/json", "{\"status\":\"ok\",\"action\":\"roll\"}");
      return;
    }
    else if (action == "stop") {
      stopOllie();
      server.send(200, "application/json", "{\"status\":\"ok\",\"action\":\"stop\"}");
      return;
    }
    else if (action == "spin") {
      spinTrick();
      server.send(200, "application/json", "{\"status\":\"ok\",\"action\":\"spin\"}");
      return;
    }
    else if (action == "color") {
      if (server.hasArg("name")) {
        String col = server.arg("name");
        for (int i = 0; i < TOTAL_OLLIE_COLORS; i++) {
          if (col.equalsIgnoreCase(ollieColors[i])) {
            ollieColorIndex = i;
            setOllieColor(ollieColorRGB[i][0], ollieColorRGB[i][1], ollieColorRGB[i][2]);
            break;
          }
        }
      } else if (server.hasArg("r") && server.hasArg("g") && server.hasArg("b")) {
        int r = server.arg("r").toInt();
        int g = server.arg("g").toInt();
        int b = server.arg("b").toInt();
        setOllieColor(r, g, b);
      }
      server.send(200, "application/json", "{\"status\":\"ok\",\"action\":\"color\"}");
      return;
    }
    else if (action == "connect") {
      bool ok = connectToOllie();
      server.send(200, "application/json", "{\"status\":\"" + String(ok ? "connected" : "failed") + "\"}");
      return;
    }

    server.send(400, "application/json", "{\"error\":\"invalid_action\"}");
  });

  server.on("/api/button", HTTP_GET, [sendOkResponse]() {
    sendCorsHeader();
    if (server.hasArg("btn")) {
      int b = server.arg("btn").toInt();
      if (b >= 1 && b <= 3) {
        processButtonEvent(b);
      }
    }
    sendOkResponse();
  });

  server.on("/api/spotify-auth", HTTP_GET, []() {
    sendCorsHeader();
    if (server.hasArg("code")) {
      String code = server.arg("code");
      code.trim();
      int cIdx = code.indexOf("code=");
      if (cIdx != -1) {
        code = code.substring(cIdx + 5);
        int amp = code.indexOf('&');
        if (amp != -1)
          code = code.substring(0, amp);
      }
      bool ok = exchangeSpotifyAuthCode(code);
      if (ok) {
        soundVictory();
        server.send(200, "application/json",
                    "{\"success\":true,\"message\":\"Connected to Spotify!\"}");
        return;
      }
    }
    server.send(400, "application/json",
                "{\"success\":false,\"message\":\"Failed to exchange Spotify "
                "authorization code. Check WiFi & Code.\"}");
  });

  server.on("/api/config", HTTP_GET, []() {
    sendCorsHeader();
    if (server.hasArg("client_id")) {
      spotifyClientId = server.arg("client_id");
      preferences.putString("sp_cid", spotifyClientId);
    }
    if (server.hasArg("client_secret")) {
      spotifyClientSecret = server.arg("client_secret");
      preferences.putString("sp_sec", spotifyClientSecret);
    }
    if (server.hasArg("refresh_token")) {
      spotifyRefreshToken = server.arg("refresh_token");
      preferences.putString("sp_tok", spotifyRefreshToken);
      spotifyAccessToken = "";
      refreshSpotifyToken();
    }
    soundVictory();
    server.send(200, "text/plain", "CONFIG_SAVED");
  });

  server.on("/api/wifi-setup", HTTP_GET, []() {
    sendCorsHeader();
    if (server.hasArg("ssid") && server.hasArg("pass")) {
      String newSSID = server.arg("ssid");
      String newPass = server.arg("pass");
      strncpy(wifiSSID, newSSID.c_str(), sizeof(wifiSSID));
      strncpy(wifiPass, newPass.c_str(), sizeof(wifiPass));

      // Persist permanently into Flash Memory (survives competitions &
      // reboots!)
      preferences.putString("ssid", newSSID);
      preferences.putString("pass", newPass);

      WiFi.begin(wifiSSID, wifiPass);

      unsigned long startAttempt = millis();
      while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 6000) {
        delay(200);
      }

      String resp = "<!DOCTYPE html><html><body "
                    "style='font-family:sans-serif;background:#0d1224;color:#"
                    "fff;text-align:center;padding:40px;'>";
      if (WiFi.status() == WL_CONNECTED) {
        wifiConnected = true;
        espIPAddress = WiFi.localIP().toString();
        soundVictory();
        resp += "<h2 style='color:#00e676;'>Connected to " + newSSID + "!</h2>";
        resp +=
            "<p>DeskBot Local IP: <b><a style='color:#00e5ff;' href='http://" +
            espIPAddress + "'>" + espIPAddress + "</a></b></p>";
        resp += "<p>Saved to flash memory! You can now switch your "
                "laptop/phone to <b>" +
                newSSID + "</b>.</p>";
      } else {
        resp += "<h2 style='color:#ffd600;'>Saved & Connecting in "
                "background...</h2>";
        resp += "<p>Check the Right OLED screen for your IP address.</p>";
      }
      resp += "<p><a style='color:#00e5ff;' href='/'>Back to DeskBot "
              "Portal</a></p></body></html>";
      server.send(200, "text/html", resp);
      return;
    }
    server.send(400, "text/plain", "MISSING_ARGS");
  });

  server.on("/api/wifi-reset", HTTP_GET, []() {
    sendCorsHeader();
    preferences.remove("ssid");
    preferences.remove("pass");
    wifiSSID[0] = '\0';
    wifiPass[0] = '\0';
    WiFi.disconnect();
    wifiConnected = false;
    espIPAddress = WiFi.softAPIP().toString();
    soundAlarm();
    server.send(
        200, "text/html",
        "<!DOCTYPE html><html><body "
        "style='font-family:sans-serif;background:#0d1224;color:#fff;text-"
        "align:center;padding:40px;'><h2 style='color:#ff5252;'>WiFi Reset to "
        "Hotspot!</h2><p>Broadcasting <b>DeskBot-WiFi</b> (192.168.4.1).</p><a "
        "style='color:#00e5ff' href='/'>Back to Portal</a></body></html>");
  });

  server.on("/", HTTP_GET, []() {
    sendCorsHeader();
    server.sendHeader("Content-Encoding", "gzip");
    server.send_P(200, "text/html", (const char*)INDEX_HTML_GZ, INDEX_HTML_GZ_LEN);
  });

  server.onNotFound([]() {
    if (server.method() == HTTP_OPTIONS) {
      sendCorsHeader();
      server.send(204);
    } else {
      sendCorsHeader();
      server.sendHeader("Content-Encoding", "gzip");
      server.send_P(200, "text/html", (const char*)INDEX_HTML_GZ, INDEX_HTML_GZ_LEN);
    }
  });

  server.begin();
}

// ====================================================
// ROBUST WHITESPACE-TOLERANT JSON EXTRACTORS
// ====================================================
String jsonExtractString(const String &json, const String &key,
                         int searchStart) {
  int kIdx = json.indexOf("\"" + key + "\"", searchStart);
  if (kIdx == -1)
    return "";
  int colon = json.indexOf(':', kIdx + key.length() + 2);
  if (colon == -1)
    return "";
  int q1 = json.indexOf('\"', colon + 1);
  if (q1 == -1)
    return "";
  int q2 = q1 + 1;
  while (q2 < (int)json.length()) {
    if (json[q2] == '\"' && json[q2 - 1] != '\\')
      break;
    q2++;
  }
  if (q2 > q1) {
    String val = json.substring(q1 + 1, q2);
    val.replace("\\\"", "\"");
    return val;
  }
  return "";
}

bool jsonExtractBool(const String &json, const String &key, int searchStart) {
  int kIdx = json.indexOf("\"" + key + "\"", searchStart);
  if (kIdx == -1)
    return false;
  int colon = json.indexOf(':', kIdx + key.length() + 2);
  if (colon == -1)
    return false;
  int idx = colon + 1;
  while (idx < (int)json.length() && (json[idx] == ' ' || json[idx] == '\t' ||
                                      json[idx] == '\r' || json[idx] == '\n')) {
    idx++;
  }
  return json.startsWith("true", idx);
}

long jsonExtractLong(const String &json, const String &key, int searchStart) {
  int kIdx = json.indexOf("\"" + key + "\"", searchStart);
  if (kIdx == -1)
    return 0;
  int colon = json.indexOf(':', kIdx + key.length() + 2);
  if (colon == -1)
    return 0;
  int idx = colon + 1;
  while (idx < (int)json.length() && (json[idx] == ' ' || json[idx] == '\t' ||
                                      json[idx] == '\r' || json[idx] == '\n')) {
    idx++;
  }
  int end = idx;
  while (end < (int)json.length() && (isdigit(json[end]) || json[end] == '-')) {
    end++;
  }
  if (end > idx) {
    return json.substring(idx, end).toInt();
  }
  return 0;
}

// ====================================================
// SPOTIFY WEB API OAUTH TOKEN & DATA REFRESH
// ====================================================
void refreshSpotifyToken() {
  if (spotifyClientId.length() < 5 || spotifyRefreshToken.length() < 5)
    return;

  WiFiClientSecure client;
  client.setInsecure(); // Skip certificate verification for speed & memory
  HTTPClient http;

  Serial.println(
      F("[Spotify] Requesting access token from accounts.spotify.com..."));
  if (http.begin(client, "https://accounts.spotify.com/api/token")) {
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    String auth = base64::encode(spotifyClientId + ":" + spotifyClientSecret);
    auth.trim();
    auth.replace("\r", "");
    auth.replace("\n", "");
    http.addHeader("Authorization", "Basic " + auth);
    http.setTimeout(8000);

    String body =
        "grant_type=refresh_token&refresh_token=" + spotifyRefreshToken;
    int code = http.POST(body);
    Serial.printf("[Spotify] Refresh token HTTP response code: %d\n", code);
    if (code == 200) {
      String resp = http.getString();
      String tok = jsonExtractString(resp, "access_token");
      if (tok.length() > 10) {
        spotifyAccessToken = tok;
        spotifyTokenExpiry = millis() + (3500UL * 1000UL);
        spotifyApiActive = true;
        Serial.println(F("[Spotify] Access token refreshed successfully!"));
      }
    } else {
      Serial.printf("[Spotify] Token refresh error (%d): %s\n", code,
                    http.getString().substring(0, 100).c_str());
    }
    http.end();
  }
}

bool exchangeSpotifyAuthCode(String code) {
  if (spotifyClientId.length() < 5 || spotifyClientSecret.length() < 5 ||
      code.length() < 5)
    return false;

  WiFiClientSecure client;
  client.setInsecure(); // Skip certificate verification for speed & memory
  HTTPClient http;

  if (http.begin(client, "https://accounts.spotify.com/api/token")) {
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    String auth = base64::encode(spotifyClientId + ":" + spotifyClientSecret);
    http.addHeader("Authorization", "Basic " + auth);
    http.setTimeout(6000);

    String body = "grant_type=authorization_code&code=" + code +
                  "&redirect_uri=https://example.com/callback";
    int status = http.POST(body);
    if (status == 200) {
      String resp = http.getString();
      String rTok = jsonExtractString(resp, "refresh_token");
      String aTok = jsonExtractString(resp, "access_token");
      if (rTok.length() > 10) {
        spotifyRefreshToken = rTok;
        preferences.putString("sp_tok", spotifyRefreshToken);
      }
      if (aTok.length() > 10) {
        spotifyAccessToken = aTok;
        spotifyTokenExpiry = millis() + (3500UL * 1000UL);
        spotifyApiActive = true;
      }
      http.end();
      pollSpotifyCurrentlyPlaying();
      return true;
    }
    http.end();
  }
  return false;
}

void pollSpotifyCurrentlyPlaying() {
  if (spotifyRefreshToken.length() < 10)
    return;

  if (spotifyAccessToken.length() < 10 || millis() > spotifyTokenExpiry) {
    refreshSpotifyToken();
    if (spotifyAccessToken.length() < 10)
      return;
  }

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;

  if (http.begin(client,
                 "https://api.spotify.com/v1/me/player/currently-playing")) {
    http.addHeader("Authorization", "Bearer " + spotifyAccessToken);
    http.setTimeout(1800);
    int code = http.GET();
    if (code == 200) {
      String resp = http.getString();
      spotifyApiActive = true;

      // Extract playback state
      liveSpotifyPlaying = jsonExtractBool(resp, "is_playing");

      // Extract progress
      long pms = jsonExtractLong(resp, "progress_ms");
      if (pms > 0) {
        liveSpotifyProgressSec = pms / 1000;
      }

      // Check item
      int itemIdx = resp.indexOf("\"item\"");
      if (itemIdx != -1) {
        // Track duration
        long dms = jsonExtractLong(resp, "duration_ms", itemIdx);
        if (dms > 0) {
          liveSpotifyTotalSec = dms / 1000;
        }

        // The track name is the LAST "name" inside the JSON response
        int lastPos = resp.lastIndexOf("\"name\"");
        if (lastPos != -1 && lastPos > itemIdx) {
          String tName = jsonExtractString(resp, "name", lastPos - 1);
          if (tName.length() > 0)
            liveSpotifyTrack = tName;
        }

        // Artist name: find "artists" inside "item"
        int artIdx = resp.indexOf("\"artists\"", itemIdx);
        if (artIdx != -1) {
          String aName = jsonExtractString(resp, "name", artIdx);
          if (aName.length() > 0)
            liveSpotifyArtist = aName;
        }
      }
    } else if (code == 204) {
      liveSpotifyPlaying = false;
    } else if (code == 401) {
      spotifyAccessToken = "";
      spotifyTokenExpiry = 0;
    }
    http.end();
  }
}

void sendSpotifyCommand(const char *endpoint, const char *method) {
  if (spotifyAccessToken.length() < 10)
    return;
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  String url = String("https://api.spotify.com/v1/me/player/") + endpoint;
  if (http.begin(client, url)) {
    http.addHeader("Authorization", "Bearer " + spotifyAccessToken);
    http.addHeader("Content-Length", "0");
    http.setTimeout(3000);
    if (strcmp(method, "POST") == 0)
      http.POST("");
    else if (strcmp(method, "PUT") == 0)
      http.PUT("");
    http.end();
  }
  // Schedule quick refresh of track state right after command
  lastSpotifyApiPoll = millis() - 3200;
}

// ====================================================
// BUTTON INPUT HANDLER
// ====================================================
void handleButtons() {
  unsigned long now = millis();

  int r1 = digitalRead(BTN_MENU);
  int r2 = digitalRead(BTN_SELECT);
  int r3 = digitalRead(BTN_BACK);

  btn1IsHeld = (r1 == LOW);
  btn2IsHeld = (r2 == LOW);
  btn3IsHeld = (r3 == LOW);

  if (now < modeSwitchCooldownUntil)
    return;

  // Button 1 (GPIO 32)
  if (r1 != btn1LastRaw) {
    btn1LastChange = now;
    btn1LastRaw = r1;
  }
  if ((now - btn1LastChange) > DEBOUNCE_DELAY) {
    if (r1 != btn1Stable) {
      btn1Stable = r1;
      if (btn1Stable == LOW) {
        processButtonEvent(1);
        return;
      }
    }
  }

  // Button 2 (GPIO 33)
  if (r2 != btn2LastRaw) {
    btn2LastChange = now;
    btn2LastRaw = r2;
  }
  if ((now - btn2LastChange) > DEBOUNCE_DELAY) {
    if (r2 != btn2Stable) {
      btn2Stable = r2;
      if (btn2Stable == LOW) {
        processButtonEvent(2);
        return;
      }
    }
  }

  // Button 3 (GPIO 27)
  if (r3 != btn3LastRaw) {
    btn3LastChange = now;
    btn3LastRaw = r3;
  }
  if ((now - btn3LastChange) > DEBOUNCE_DELAY) {
    if (r3 != btn3Stable) {
      btn3Stable = r3;
      if (btn3Stable == LOW) {
        processButtonEvent(3);
        return;
      }
    }
  }
}

// ====================================================
// DISPATCH BUTTON ACTIONS
// ====================================================
void processButtonEvent(int btnId) {
  bool btn1 = (btnId == 1);
  bool btn2 = (btnId == 2);
  bool btn3 = (btnId == 3);

  switch (currentMode) {
  case MODE_COMPANION:
    if (btn1) {
      soundChirp();
      switchMode(MODE_MENU);
    } else if (btn2) {
      soundPoke();
      currentExpr = EXPR_SURPRISED;
      lastEyeShiftTime = millis() + 1500;
    } else if (btn3) {
      soundHappy();
      switch (currentExpr) {
      case EXPR_NEUTRAL:
        currentExpr = EXPR_HAPPY;
        break;
      case EXPR_HAPPY:
        currentExpr = EXPR_LOOK_LEFT;
        break;
      case EXPR_LOOK_LEFT:
        currentExpr = EXPR_LOOK_RIGHT;
        break;
      case EXPR_LOOK_RIGHT:
        currentExpr = EXPR_SURPRISED;
        break;
      case EXPR_SURPRISED:
        currentExpr = EXPR_WINK;
        break;
      case EXPR_WINK:
        currentExpr = EXPR_SLEEPY;
        break;
      case EXPR_SLEEPY:
      default:
        currentExpr = EXPR_NEUTRAL;
        break;
      }
      lastEyeShiftTime = millis() + 4000;
    }
    break;

  case MODE_MENU:
    if (btn1) {
      selectedMenuItem = (selectedMenuItem + 1) % TOTAL_MENU_ITEMS;
      soundChirp();
    } else if (btn2) {
      soundCoin();
      switch (selectedMenuItem) {
      case 0:
        switchMode(MODE_COMPANION);
        break;
      case 1:
        switchMode(MODE_POMODORO);
        break;
      case 2:
        switchMode(MODE_OLLIE);
        break;
      case 3:
        reflexState = REFLEX_WAIT_START;
        switchMode(MODE_REFLEX_GAME);
        break;
      case 4:
        switchMode(MODE_SPOTIFY);
        break;
      case 5:
        switchMode(MODE_SYSTEM_INFO);
        break;
      }
    } else if (btn3) {
      soundChirp();
      switchMode(MODE_COMPANION);
    }
    break;

  case MODE_POMODORO:
    if (btn1) {
      if (!timerRunning) {
        if (timerTotalSeconds == 25 * 60)
          timerTotalSeconds = 45 * 60;
        else if (timerTotalSeconds == 45 * 60)
          timerTotalSeconds = 5 * 60;
        else
          timerTotalSeconds = 25 * 60;
        timerRemainingSeconds = timerTotalSeconds;
        soundChirp();
      }
    } else if (btn2) {
      timerRunning = !timerRunning;
      lastTimerTick = millis();
      if (timerRunning)
        soundHappy();
      else
        soundChirp();
    } else if (btn3) {
      soundChirp();
      switchMode(MODE_MENU);
    }
    break;

  case MODE_OLLIE:
    if (btn1) {
      ollieColorIndex = (ollieColorIndex + 1) % TOTAL_OLLIE_COLORS;
      setOllieColor(ollieColorRGB[ollieColorIndex][0], 
                    ollieColorRGB[ollieColorIndex][1], 
                    ollieColorRGB[ollieColorIndex][2]);
      soundHappy();
    } else if (btn2) {
      spinTrick();
      soundVictory();
    } else if (btn3) {
      soundChirp();
      switchMode(MODE_MENU);
    }
    break;

  case MODE_REFLEX_GAME:
    if (reflexState == REFLEX_WAIT_START) {
      if (btn2 || btn1) {
        reflexState = REFLEX_WAIT_CUE;
        reflexWaitTargetTime = millis() + random(1800, 4200);
        soundChirp();
      } else if (btn3) {
        switchMode(MODE_MENU);
      }
    } else if (reflexState == REFLEX_WAIT_CUE) {
      if (btn2) {
        reflexState = REFLEX_FALSE_START;
        soundAlarm();
      }
    } else if (reflexState == REFLEX_SHOW_CUE) {
      if (btn2) {
        reflexReactionTime = millis() - reflexCueStartTime;
        reflexState = REFLEX_RESULT;
        soundVictory();
      }
    } else if (reflexState == REFLEX_RESULT ||
               reflexState == REFLEX_FALSE_START) {
      if (btn2 || btn1) {
        reflexState = REFLEX_WAIT_START;
        soundChirp();
      } else if (btn3) {
        switchMode(MODE_MENU);
      }
    }
    break;

  case MODE_SPOTIFY:
    if (btn1) {
      if (spotifyApiActive)
        sendSpotifyCommand("next", "POST");
      else
        spotifyTrackIdx = (spotifyTrackIdx + 1) % TOTAL_SPOTIFY_TRACKS;
      soundChirp();
    } else if (btn2) {
      if (spotifyApiActive) {
        sendSpotifyCommand(liveSpotifyPlaying ? "pause" : "play", "PUT");
        liveSpotifyPlaying = !liveSpotifyPlaying;
      } else {
        spotifyPlaying = !spotifyPlaying;
      }
      if (spotifyPlaying || liveSpotifyPlaying)
        soundHappy();
      else
        soundChirp();
    } else if (btn3) {
      soundChirp();
      switchMode(MODE_MENU);
    }
    break;

  case MODE_SYSTEM_INFO:
    if (btn1 || btn2 || btn3) {
      soundChirp();
      switchMode(MODE_MENU);
    }
    break;
  }
}

// ====================================================
// CLOUD MQTT MESSAGE HANDLER & MAINTENANCE
// ====================================================
void onMqttMessage(char* topic, byte* payload, unsigned int length) {
  char msg[256];
  if (length >= sizeof(msg)) length = sizeof(msg) - 1;
  memcpy(msg, payload, length);
  msg[length] = '\0';
  String json = String(msg);
  Serial.printf("[MQTT] Recv: %s\n", msg);

  String action = jsonExtractString(json, "action");
  if (action == "move_cm") {
    int dist = jsonExtractLong(json, "distance");
    if (dist <= 0) dist = 20;
    int heading = jsonExtractLong(json, "heading");
    rollOllieDistanceCm(dist, heading);
  } else if (action == "roll") {
    int speed = jsonExtractLong(json, "speed");
    int heading = jsonExtractLong(json, "heading");
    rollOllie(speed, heading, 1);
  } else if (action == "stop") {
    stopOllie();
  } else if (action == "spin") {
    spinTrick();
  } else if (action == "color") {
    String col = jsonExtractString(json, "name");
    if (col.length() > 0) {
      for (int i = 0; i < TOTAL_OLLIE_COLORS; i++) {
        if (col.equalsIgnoreCase(ollieColors[i])) {
          ollieColorIndex = i;
          setOllieColor(ollieColorRGB[i][0], ollieColorRGB[i][1], ollieColorRGB[i][2]);
          break;
        }
      }
    }
  } else if (action == "mode") {
    int m = jsonExtractLong(json, "mode");
    if (m >= 0 && m <= 6) {
      switchMode((BotMode)m);
    }
  } else if (action == "spotify") {
    String sub = jsonExtractString(json, "cmd");
    if (sub == "play") sendSpotifyCommand("play", "PUT");
    else if (sub == "pause") sendSpotifyCommand("pause", "PUT");
    else if (sub == "next") sendSpotifyCommand("next", "POST");
    else if (sub == "prev") sendSpotifyCommand("previous", "POST");
  }
}

void maintainMqtt() {
  if (!wifiConnected) return;

  unsigned long now = millis();
  if (!mqttClient.connected()) {
    if (now - lastMqttReconnectAttempt > 5000) {
      lastMqttReconnectAttempt = now;
      String clientId = "DeskBot-ESP32-" + String((uint32_t)ESP.getEfuseMac(), HEX);
      if (mqttClient.connect(clientId.c_str())) {
        Serial.println(F("[MQTT] Connected to broker.emqx.io!"));
        mqttClient.subscribe(mqttTopicCmd);
      }
    }
  } else {
    mqttClient.loop();

    // Publish telemetry/status every 2.5 seconds
    if (now - lastMqttStatusPublish > 2500) {
      lastMqttStatusPublish = now;
      String status = "{";
      status += "\"mode\":" + String(currentMode) + ",";
      status += "\"ollie\":{\"connected\":" + String(ollieConnected ? "true" : "false") +
                ",\"battery\":" + String(ollieBatteryPercent) +
                ",\"color\":\"" + String(ollieColors[ollieColorIndex]) + "\"},";
      status += "\"wifi\":{\"ip\":\"" + espIPAddress + "\"}";
      status += "}";
      mqttClient.publish(mqttTopicStatus, status.c_str());
    }
  }
}

// ====================================================
// ASYNCHRONOUS STATE TIMERS
// ====================================================
void updateAsyncState() {
  maintainMqtt();
  unsigned long now = millis();

  // Natural Blinking
  if (!isBlinking && (now - lastBlinkTime > blinkInterval)) {
    isBlinking = true;
    blinkStartTime = now;
  }
  if (isBlinking && (now - blinkStartTime > 130)) {
    isBlinking = false;
    lastBlinkTime = now;
    blinkInterval = random(2500, 5500);
  }

  // Idle eye glances
  if (currentMode == MODE_COMPANION && (now - lastEyeShiftTime > 4200)) {
    lastEyeShiftTime = now;
    int r = random(0, 10);
    if (r < 5)
      currentExpr = EXPR_NEUTRAL;
    else if (r < 7)
      currentExpr = EXPR_LOOK_LEFT;
    else if (r < 9)
      currentExpr = EXPR_LOOK_RIGHT;
    else
      currentExpr = EXPR_HAPPY;
  }

  // Pomodoro Ticking
  if (timerRunning && (now - lastTimerTick >= 1000)) {
    lastTimerTick = now;
    if (timerRemainingSeconds > 0) {
      timerRemainingSeconds--;
      if (timerRemainingSeconds == 0) {
        timerRunning = false;
        soundAlarm();
      }
    }
  }

  // Reflex Game Cues
  if (currentMode == MODE_REFLEX_GAME) {
    if (reflexState == REFLEX_WAIT_CUE && (now >= reflexWaitTargetTime)) {
      reflexState = REFLEX_SHOW_CUE;
      reflexCueStartTime = now;
      soundPoke();
    }
  }

  // Auto-maintain WiFi connection state
  static unsigned long lastWifiReconnectAttempt = 0;
  if (WiFi.status() == WL_CONNECTED) {
    if (!wifiConnected) {
      wifiConnected = true;
      espIPAddress = WiFi.localIP().toString();
      Serial.printf("[WiFi] Reconnected! IP: %s\n", espIPAddress.c_str());
      refreshSpotifyToken();
      pollSpotifyCurrentlyPlaying();
    }
  } else {
    wifiConnected = false;
    if (strlen(wifiSSID) > 0 && (now - lastWifiReconnectAttempt > 10000)) {
      lastWifiReconnectAttempt = now;
      WiFi.reconnect();
    }
  }

  // Spotify Web API live polling: only poll when actively in Spotify mode!
  if (currentMode == MODE_SPOTIFY && wifiConnected && (now - lastSpotifyApiPoll > 3500)) {
    lastSpotifyApiPoll = now;
    pollSpotifyCurrentlyPlaying();
  }
}

// ====================================================
// RENDER DISPLAYS
// ====================================================
void renderDisplays() {
  if (leftDisplayAvailable && rightDisplayAvailable) {
    // Left Screen: Companion Eye
    displayL.clearDisplay();
    drawEyeOnDisplay(displayL, true);
    displayL.display();

    // Right Screen: Eye or Active Feature Panel
    displayR.clearDisplay();
    if (currentMode == MODE_COMPANION) {
      drawEyeOnDisplay(displayR, false);
    } else {
      drawRightScreenPanel();
    }
    drawLiveButtonIndicators(displayR);
    displayR.display();
  } else if (leftDisplayAvailable) {
    displayL.clearDisplay();
    if (currentMode == MODE_COMPANION) {
      drawBothEyesOnSingleDisplay(displayL);
    } else {
      drawRightScreenPanel();
    }
    drawLiveButtonIndicators(displayL);
    displayL.display();
  } else if (rightDisplayAvailable) {
    displayR.clearDisplay();
    if (currentMode == MODE_COMPANION) {
      drawBothEyesOnSingleDisplay(displayR);
    } else {
      drawRightScreenPanel();
    }
    drawLiveButtonIndicators(displayR);
    displayR.display();
  }
}

// ====================================================
// GRAPHICS: LIVE BUTTON STATUS INDICATOR (Top-Right)
// ====================================================
void drawLiveButtonIndicators(Adafruit_SSD1306 &disp) {
  int x = 104;
  int y = 0;
  disp.drawRect(x, y, 7, 7, SSD1306_WHITE);
  if (btn1IsHeld)
    disp.fillRect(x + 1, y + 1, 5, 5, SSD1306_WHITE);

  disp.drawRect(x + 8, y, 7, 7, SSD1306_WHITE);
  if (btn2IsHeld)
    disp.fillRect(x + 9, y + 1, 5, 5, SSD1306_WHITE);

  disp.drawRect(x + 16, y, 7, 7, SSD1306_WHITE);
  if (btn3IsHeld)
    disp.fillRect(x + 17, y + 1, 5, 5, SSD1306_WHITE);
}

// ====================================================
// GRAPHICS: FULL-SIZE COMPANION EYE (Per Screen)
// ====================================================
void drawEyeOnDisplay(Adafruit_SSD1306 &disp, bool isLeftEye) {
  int cx = 64;
  int cy = 16;
  int eyeWidth = 36;
  int eyeHeight = 22;

  if (isBlinking) {
    disp.fillRoundRect(cx - (eyeWidth / 2), cy - 1, eyeWidth, 3, 1,
                       SSD1306_WHITE);
    return;
  }

  switch (currentExpr) {
  case EXPR_HAPPY:
    disp.fillCircle(cx, cy + 4, 14, SSD1306_WHITE);
    disp.fillCircle(cx, cy + 7, 13, SSD1306_BLACK);
    break;

  case EXPR_SURPRISED:
    disp.drawCircle(cx, cy, 14, SSD1306_WHITE);
    disp.drawCircle(cx, cy, 13, SSD1306_WHITE);
    disp.fillCircle(cx, cy, 5, SSD1306_WHITE);
    break;

  case EXPR_WINK:
    if (!isLeftEye) {
      disp.drawFastHLine(cx - 16, cy, 32, SSD1306_WHITE);
      disp.drawFastHLine(cx - 16, cy + 1, 32, SSD1306_WHITE);
    } else {
      disp.fillRoundRect(cx - (eyeWidth / 2), cy - (eyeHeight / 2), eyeWidth,
                         eyeHeight, 6, SSD1306_WHITE);
      disp.fillRect(cx - 4, cy - 4, 5, 5, SSD1306_BLACK);
    }
    break;

  case EXPR_SLEEPY:
    disp.fillRoundRect(cx - (eyeWidth / 2), cy + 2, eyeWidth, 10, 3,
                       SSD1306_WHITE);
    if (!isLeftEye) {
      disp.setTextSize(1);
      disp.setTextColor(SSD1306_WHITE);
      disp.setCursor(cx + 20, 4);
      disp.print(F("zZ"));
    }
    break;

  case EXPR_LOOK_LEFT:
    disp.drawRoundRect(cx - (eyeWidth / 2), cy - (eyeHeight / 2), eyeWidth,
                       eyeHeight, 6, SSD1306_WHITE);
    disp.fillRect(cx - 14, cy - 6, 12, 12, SSD1306_WHITE);
    break;

  case EXPR_LOOK_RIGHT:
    disp.drawRoundRect(cx - (eyeWidth / 2), cy - (eyeHeight / 2), eyeWidth,
                       eyeHeight, 6, SSD1306_WHITE);
    disp.fillRect(cx + 2, cy - 6, 12, 12, SSD1306_WHITE);
    break;

  case EXPR_NEUTRAL:
  default:
    disp.fillRoundRect(cx - (eyeWidth / 2), cy - (eyeHeight / 2), eyeWidth,
                       eyeHeight, 6, SSD1306_WHITE);
    disp.fillRect(cx + 4, cy - 6, 4, 4, SSD1306_BLACK);
    break;
  }
}

// Fallback single display mode
void drawBothEyesOnSingleDisplay(Adafruit_SSD1306 &disp) {
  int w = 24;
  int h = 18;
  int y = 7;
  int xL = 20;
  int xR = 84;

  if (isBlinking) {
    disp.fillRoundRect(xL, y + (h / 2) - 1, w, 2, 1, SSD1306_WHITE);
    disp.fillRoundRect(xR, y + (h / 2) - 1, w, 2, 1, SSD1306_WHITE);
    return;
  }

  disp.fillRoundRect(xL, y, w, h, 4, SSD1306_WHITE);
  disp.fillRoundRect(xR, y, w, h, 4, SSD1306_WHITE);
}

// ====================================================
// DISPATCH RIGHT SCREEN PANELS
// ====================================================
void drawRightScreenPanel() {
  switch (currentMode) {
  case MODE_MENU:
    drawMenuRight();
    break;
  case MODE_POMODORO:
    drawPomodoroRight();
    break;
  case MODE_OLLIE:
    drawOllieRight();
    break;
  case MODE_REFLEX_GAME:
    drawReflexGameRight();
    break;
  case MODE_SPOTIFY:
    drawSpotifyRight();
    break;
  case MODE_SYSTEM_INFO:
    drawSystemInfoRight();
    break;
  default:
    break;
  }
}

// ====================================================
// RIGHT SCREEN: MENU
// ====================================================
void drawMenuRight() {
  displayR.setTextSize(1);
  displayR.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
  displayR.setCursor(0, 0);
  displayR.print(F(" [ DESKBOT MENU ] "));

  displayR.setTextColor(SSD1306_WHITE);
  displayR.setCursor(0, 11);
  displayR.print(F("> "));
  displayR.print(menuNames[selectedMenuItem]);

  int nextItem = (selectedMenuItem + 1) % TOTAL_MENU_ITEMS;
  displayR.setCursor(0, 22);
  displayR.print(F("  "));
  displayR.print(menuNames[nextItem]);
}

// ====================================================
// RIGHT SCREEN: POMODORO
// ====================================================
void drawPomodoroRight() {
  displayR.setTextSize(1);
  displayR.setTextColor(SSD1306_WHITE);
  displayR.setCursor(0, 0);
  displayR.print(F("POMODORO"));

  displayR.setCursor(56, 0);
  displayR.print(timerRunning ? F("[RUNNING]") : F("[PAUSED]"));

  displayR.setTextSize(2);
  displayR.setCursor(14, 11);

  int mins = timerRemainingSeconds / 60;
  int secs = timerRemainingSeconds % 60;

  if (mins < 10)
    displayR.print(F("0"));
  displayR.print(mins);
  displayR.print(F(":"));
  if (secs < 10)
    displayR.print(F("0"));
  displayR.print(secs);
}

// ====================================================
// RIGHT SCREEN: OLLIE BATTERY & CONTROLLER
// ====================================================
void drawOllieRight() {
  displayR.setTextSize(1);
  displayR.setTextColor(SSD1306_WHITE);
  displayR.setCursor(0, 0);
  displayR.print(F("OLLIE: 2B-4FCD"));

  displayR.drawRect(0, 11, 48, 9, SSD1306_WHITE);
  displayR.fillRect(48, 13, 2, 5, SSD1306_WHITE);

  int fillW = map(ollieBatteryPercent, 0, 100, 0, 44);
  if (fillW > 0) {
    displayR.fillRect(2, 13, fillW, 5, SSD1306_WHITE);
  }

  displayR.setCursor(54, 11);
  displayR.print(ollieBatteryPercent);
  displayR.print(F("% 3.9V"));

  displayR.setCursor(0, 23);
  displayR.print(F("LED: "));
  displayR.print(ollieColors[ollieColorIndex]);
}

// ====================================================
// RIGHT SCREEN: REFLEX MINIGAME
// ====================================================
void drawReflexGameRight() {
  displayR.setTextSize(1);
  displayR.setTextColor(SSD1306_WHITE);

  switch (reflexState) {
  case REFLEX_WAIT_START:
    displayR.setCursor(0, 0);
    displayR.print(F("REFLEX TEST"));
    displayR.setCursor(0, 12);
    displayR.print(F("Press [B2] to start"));
    displayR.setCursor(0, 23);
    displayR.print(F("Fastest fingers win!"));
    break;

  case REFLEX_WAIT_CUE:
    displayR.setCursor(0, 4);
    displayR.print(F("GET READY..."));
    displayR.setCursor(0, 18);
    displayR.print(F("Wait for beep!"));
    break;

  case REFLEX_SHOW_CUE:
    displayR.fillScreen(SSD1306_WHITE);
    displayR.setTextColor(SSD1306_BLACK);
    displayR.setTextSize(2);
    displayR.setCursor(6, 8);
    displayR.print(F("PRESS [B2]!"));
    break;

  case REFLEX_RESULT:
    displayR.setCursor(0, 0);
    displayR.print(F("Reaction Time:"));
    displayR.setTextSize(2);
    displayR.setCursor(18, 10);
    displayR.print(reflexReactionTime);
    displayR.print(F(" ms"));
    displayR.setTextSize(1);
    displayR.setCursor(0, 24);
    displayR.print(F("Press [B2] retry"));
    break;

  case REFLEX_FALSE_START:
    displayR.setCursor(14, 4);
    displayR.print(F("TOO EARLY!"));
    displayR.setCursor(0, 18);
    displayR.print(F("Wait for signal [B2]"));
    break;
  }
}

// ====================================================
// RIGHT SCREEN: SPOTIFY NOW PLAYING CONTROLLER
// ====================================================
void drawSpotifyRight() {
  displayR.setTextSize(1);
  displayR.setTextColor(SSD1306_WHITE);

  // Line 1: Header / Status (y = 0)
  displayR.setCursor(0, 0);
  if (spotifyApiActive) {
    displayR.print(liveSpotifyPlaying ? F("SPOTIFY [LIVE]")
                                      : F("SPOTIFY [PAUSED]"));
  } else {
    displayR.print(spotifyPlaying ? F("SPOTIFY [LOCAL]")
                                  : F("SPOTIFY [PAUSED]"));
  }

  // Line 2: Track Title (y = 9) - up to 21 characters
  displayR.setCursor(0, 9);
  String tTitle = spotifyApiActive ? liveSpotifyTrack
                                   : String(spotifyTracks[spotifyTrackIdx]);
  if (tTitle.length() > 21)
    tTitle = tTitle.substring(0, 20) + F("~");
  displayR.print(tTitle);

  // Line 3: Artist (y = 18) - up to 21 characters
  displayR.setCursor(0, 18);
  String aName = spotifyApiActive ? liveSpotifyArtist
                                  : String(spotifyArtists[spotifyTrackIdx]);
  if (aName.length() > 18)
    aName = aName.substring(0, 17) + F("~");
  displayR.print(F("by "));
  displayR.print(aName);

  // Line 4: Sleek Progress Bar (y = 27, height = 4)
  int curSec =
      spotifyApiActive ? liveSpotifyProgressSec : (millis() / 1000) % 200;
  int totSec = spotifyApiActive ? liveSpotifyTotalSec
                                : spotifyTrackLengths[spotifyTrackIdx];
  if (totSec <= 0)
    totSec = 180;
  int progW = map(constrain(curSec, 0, totSec), 0, totSec, 0, 126);
  displayR.drawRect(0, 27, 128, 4, SSD1306_WHITE);
  if (progW > 0) {
    displayR.fillRect(1, 28, progW, 2, SSD1306_WHITE);
  }
}

// ====================================================
// RIGHT SCREEN: SYSTEM TELEMETRY (Clean, Undistorted)
// ====================================================
void drawSystemInfoRight() {
  displayR.setTextSize(1);
  displayR.setTextColor(SSD1306_WHITE);

  displayR.setCursor(0, 0);
  displayR.print(F("SYSTEM STATUS"));

  displayR.setCursor(0, 11);
  if (wifiConnected) {
    displayR.print(F("IP:"));
    displayR.print(espIPAddress);
  } else {
    displayR.print(F("WiFi: Not Linked"));
  }

  displayR.setCursor(0, 22);
  displayR.print(F("RAM:"));
  displayR.print(ESP.getFreeHeap() / 1024);
  displayR.print(F("K "));
  displayR.print(spotifyApiActive ? F("Spot:API") : F("Spot:Off"));
}

// ====================================================
// AUDIO PIEZO CHIMES
// ====================================================
void playTone(int freq, int durationMs) {
  tone(BUZZER_PIN, freq, durationMs);
  delay(durationMs + 10);
  noTone(BUZZER_PIN);
}

void soundChirp() { playTone(1800, 25); }

void soundHappy() {
  playTone(1318, 60);
  playTone(1760, 90);
}

void soundPoke() {
  playTone(880, 50);
  playTone(1200, 60);
  playTone(1600, 70);
}

void soundCoin() {
  playTone(988, 70);
  playTone(1319, 180);
}

void soundVictory() {
  int notes[] = {523, 659, 784, 1046};
  int durations[] = {80, 80, 80, 220};
  for (int i = 0; i < 4; i++) {
    playTone(notes[i], durations[i]);
  }
}

void soundAlarm() {
  for (int i = 0; i < 3; i++) {
    playTone(2000, 90);
    delay(40);
    playTone(1400, 90);
    delay(40);
  }
}

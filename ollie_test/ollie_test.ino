#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEClient.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

// =========================================================================
// Sphero Ollie BLE Controller Test Sketch (v4.0 - Verified Protocol)
// Target Device: 2B-4FCD (MAC: ea:a4:5f:42:4f:cd)
// =========================================================================

static BLEAddress targetAddress("ea:a4:5f:42:4f:cd");

// Sphero BLE Services & Characteristics
static BLEUUID antiDosServiceUUID("22bb746f-2bb0-7554-2d6f-726568705327");
static BLEUUID antiDosCharUUID("22bb746f-2bbd-7554-2d6f-726568705327");
static BLEUUID txPowerCharUUID("22bb746f-2bb2-7554-2d6f-726568705327");
static BLEUUID wakeCharUUID("22bb746f-2bbf-7554-2d6f-726568705327");

static BLEUUID robotControlServiceUUID("22bb746f-2ba0-7554-2d6f-726568705327");
static BLEUUID commandCharUUID("22bb746f-2ba1-7554-2d6f-726568705327");
static BLEUUID responseCharUUID("22bb746f-2ba6-7554-2d6f-726568705327");

BLEClient* pClient = nullptr;
BLERemoteCharacteristic* pAntiDosChar = nullptr;
BLERemoteCharacteristic* pTxPowerChar = nullptr;
BLERemoteCharacteristic* pWakeChar = nullptr;
BLERemoteCharacteristic* pCmdChar = nullptr;
BLERemoteCharacteristic* pRespChar = nullptr;

BLEAdvertisedDevice* foundOllie = nullptr;
volatile bool ollieFound = false;
bool isConnected = false;
uint8_t packetSeq = 0;
unsigned long lastKeepAlive = 0;

// Notification callback for responses from Ollie
void notifyCallback(BLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify) {
  // Received packet from Ollie
  // Serial.printf("[Ollie ACK] %d bytes\n", length);
}

// Send Sphero API Command Packet (withResponse = true is CRITICAL for Sphero BLE!)
void sendSpheroPacket(uint8_t did, uint8_t cid, uint8_t* data, uint8_t dataLen) {
  if (!pCmdChar || !isConnected) return;

  uint8_t packet[32];
  packet[0] = 0xFF; // SOP1
  packet[1] = 0xFE; // SOP2 (0xFE: resetTimeout=1, answer=0)
  packet[2] = did;
  packet[3] = cid;
  packet[4] = packetSeq++; // SEQ (increment sequence number)
  packet[5] = dataLen + 1; // DLEN (data length + checksum)

  uint16_t sum = packet[2] + packet[3] + packet[4] + packet[5];

  for (uint8_t i = 0; i < dataLen; i++) {
    packet[6 + i] = data[i];
    sum += data[i];
  }

  packet[6 + dataLen] = ~(sum & 0xFF); // Checksum: bitwise NOT of LSB
  uint8_t totalPacketLen = 7 + dataLen;

  // Crucial: writeValue with response = true!
  pCmdChar->writeValue(packet, totalPacketLen, true);
}

// Set Main RGB LED Color (DID 0x02, CID 0x20)
void setOllieColor(uint8_t r, uint8_t g, uint8_t b) {
  Serial.printf("[Ollie] Setting Color: R=%d G=%d B=%d\n", r, g, b);
  uint8_t colorData[] = { r, g, b, 0x01 }; // 0x01 = user color persistence
  sendSpheroPacket(0x02, 0x20, colorData, sizeof(colorData));
}

// Set Back Blue LED (Tail Light) brightness (0-255)
void setBackLed(uint8_t brightness) {
  uint8_t ledData[] = { brightness };
  sendSpheroPacket(0x02, 0x21, ledData, sizeof(ledData));
}

// Enable or disable internal gyroscope stabilization
void setStabilization(bool enable) {
  uint8_t stabData[] = { (uint8_t)(enable ? 0x01 : 0x00) };
  sendSpheroPacket(0x02, 0x02, stabData, sizeof(stabData));
}

// Drive Ollie: speed (0-255), heading (0-359), state (1=drive, 0=brake)
void rollOllie(uint8_t speed, uint16_t heading, uint8_t state) {
  uint8_t rollData[] = {
    speed,
    (uint8_t)((heading >> 8) & 0xFF),
    (uint8_t)(heading & 0xFF),
    state
  };
  sendSpheroPacket(0x02, 0x30, rollData, sizeof(rollData));
}

// Full Stop / Brake
void stopOllie() {
  Serial.println("[Ollie] Braking / Stopping...");
  rollOllie(0, 0, 0);
  delay(100);
}

// Roll forward a few centimeters
void rollFewCentimeters() {
  Serial.println("\n[Ollie] >>> MOVING FORWARD FEW CENTIMETERS... <<<");
  setOllieColor(0, 255, 0); // Green while driving
  
  // Speed 80, Heading 0 (forward), State 1 (drive)
  rollOllie(80, 0, 1);
  delay(800); // Drive for 800ms (approx 10-15 cm)

  // Brake immediately
  stopOllie();
  setOllieColor(0, 229, 255); // Cyan when stopped
  Serial.println("[Ollie] >>> MOVEMENT COMPLETE! <<<\n");
}

// Perform a 360 Spin Trick
void spinTrick() {
  Serial.println("\n[Ollie] >>> PERFORMING 360 SPIN TRICK... <<<");
  setStabilization(false);
  setOllieColor(255, 0, 255); // Magenta
  
  uint8_t rawMotor[] = { 0x01, 110, 0x02, 110 }; // Left forward, right reverse
  sendSpheroPacket(0x02, 0x33, rawMotor, sizeof(rawMotor));
  delay(650);
  
  uint8_t stopMotor[] = { 0x00, 0, 0x00, 0 };
  sendSpheroPacket(0x02, 0x33, stopMotor, sizeof(stopMotor));
  setStabilization(true);
  setOllieColor(0, 229, 255);
  Serial.println("[Ollie] Spin trick finished!\n");
}

// Keep-Alive Ping (DID 0x00, CID 0x01)
void sendKeepAlive() {
  sendSpheroPacket(0x00, 0x01, nullptr, 0);
}

// Lightweight BLE Scan Callback
class OllieScanCallbacks: public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    if (ollieFound) return;

    String name = advertisedDevice.getName().c_str();

    if (name.startsWith("2B-") || name.indexOf("Ollie") != -1 || advertisedDevice.getAddress().equals(targetAddress)) {
      foundOllie = new BLEAdvertisedDevice(advertisedDevice);
      ollieFound = true;
      Serial.printf("\n[BLE Scan] 🎯 TARGET FOUND: Name='%s', MAC=%s, Type=%d, RSSI=%d dBm\n", 
                    name.length() > 0 ? name.c_str() : "Ollie",
                    advertisedDevice.getAddress().toString().c_str(),
                    (int)advertisedDevice.getAddressType(),
                    advertisedDevice.getRSSI());
    }
  }
};

// Handshake & Wakeup sequence
bool setupOllieServices() {
  Serial.println("[BLE] Discovering Ollie services...");

  // 1. Anti-DoS Service
  BLERemoteService* pAntiDosService = pClient->getService(antiDosServiceUUID);
  if (!pAntiDosService) {
    Serial.println("[BLE] ❌ Anti-DoS Service not found!");
    return false;
  }

  pAntiDosChar = pAntiDosService->getCharacteristic(antiDosCharUUID);
  pTxPowerChar = pAntiDosService->getCharacteristic(txPowerCharUUID);
  pWakeChar = pAntiDosService->getCharacteristic(wakeCharUUID);

  if (!pAntiDosChar || !pTxPowerChar || !pWakeChar) {
    Serial.println("[BLE] ❌ Anti-DoS characteristics missing!");
    return false;
  }

  // 2. Perform Anti-DoS Security Unlock
  // Sphero Ollie exact key is "011i3" (withResponse = true)
  Serial.println("[BLE] Sending Anti-DoS unlock string ('011i3')...");
  const char* antiDosOllie = "011i3";
  pAntiDosChar->writeValue((uint8_t*)antiDosOllie, strlen(antiDosOllie), true);
  delay(150);

  // Fallback unlock string just in case
  const char* antiDosAlt = "usetheforce...band";
  pAntiDosChar->writeValue((uint8_t*)antiDosAlt, strlen(antiDosAlt), true);
  delay(150);

  // 3. Set TX Power (write \x00\x07 with response)
  Serial.println("[BLE] Setting TX Power to 7 (Max)...");
  uint8_t txPower2[] = { 0x00, 0x07 };
  pTxPowerChar->writeValue(txPower2, 2, true);
  delay(150);

  // 4. Wake up CPU (write \x01 with response)
  Serial.println("[BLE] Waking up Ollie CPU (\\x01)...");
  uint8_t wakeVal = 0x01;
  pWakeChar->writeValue(&wakeVal, 1, true);
  delay(300);

  // 5. Robot Control Service
  BLERemoteService* pControlService = pClient->getService(robotControlServiceUUID);
  if (!pControlService) {
    Serial.println("[BLE] ❌ Robot Control Service not found!");
    return false;
  }

  pCmdChar = pControlService->getCharacteristic(commandCharUUID);
  if (!pCmdChar) {
    Serial.println("[BLE] ❌ Command Characteristic not found!");
    return false;
  }

  pRespChar = pControlService->getCharacteristic(responseCharUUID);
  if (pRespChar && pRespChar->canNotify()) {
    Serial.println("[BLE] Subscribing to Ollie Response notifications...");
    pRespChar->registerForNotify(notifyCallback);
    delay(150);
  }

  isConnected = true;
  lastKeepAlive = millis();
  Serial.println("\n[BLE] 🚀 OLLIE IS FULLY AWAKE AND READY FOR COMMANDS!\n");

  // Visual confirmation: Cyberpunk Cyan glow + tail light
  setStabilization(true);
  delay(150);
  setOllieColor(0, 229, 255); // Bright Cyan
  delay(100);
  setBackLed(255); // Blue tail light
  return true;
}

// Connect to discovered device or fallback to RANDOM address type
bool connectToOllie() {
  pClient = BLEDevice::createClient();

  if (foundOllie != nullptr) {
    BLEAddress addr = foundOllie->getAddress();
    esp_ble_addr_type_t addrType = (esp_ble_addr_type_t)foundOllie->getAddressType();
    Serial.printf("[BLE] Connecting using discovered Address %s (Type: %d)...\n", addr.toString().c_str(), (int)addrType);
    
    if (pClient->connect(addr, addrType)) {
      Serial.println("[BLE] ✅ Connected to Ollie via discovered Address & Type!");
      return setupOllieServices();
    }

    if (pClient->connect(foundOllie)) {
      Serial.println("[BLE] ✅ Connected to Ollie via Advertised Device!");
      return setupOllieServices();
    }
  }

  // Ollie uses BLE_ADDR_TYPE_RANDOM!
  Serial.println("[BLE] Trying direct connect with RANDOM address type...");
  if (pClient->connect(targetAddress, BLE_ADDR_TYPE_RANDOM)) {
    Serial.println("[BLE] ✅ Connected via RANDOM address type!");
    return setupOllieServices();
  }

  // Fallback to PUBLIC address type
  Serial.println("[BLE] Trying direct connect with PUBLIC address type...");
  if (pClient->connect(targetAddress, BLE_ADDR_TYPE_PUBLIC)) {
    Serial.println("[BLE] ✅ Connected via PUBLIC address type!");
    return setupOllieServices();
  }

  return false;
}

void setup() {
  Serial.begin(115200);
  delay(1500);

  Serial.println("\n=============================================");
  Serial.println("   SPHERO OLLIE (2B-4FCD) CONTROLLER TEST   ");
  Serial.println("=============================================");
  Serial.println("[IMPORTANT] Turn OFF Bluetooth on your phone now!");
  Serial.println("[IMPORTANT] If your phone is connected, Ollie won't connect to ESP32.\n");

  BLEDevice::init("DeskBot-Ollie");

  // Step 1: Scan for 3 seconds to discover Ollie
  Serial.println("[BLE Scan] Scanning for nearby Ollie for 3 seconds...");
  BLEScan* pScan = BLEDevice::getScan();
  pScan->setAdvertisedDeviceCallbacks(new OllieScanCallbacks());
  pScan->setActiveScan(true);
  pScan->setInterval(100);
  pScan->setWindow(99);
  pScan->start(3, false);
  Serial.println("[BLE Scan] Scan window closed.");

  // Step 2: Connect
  if (connectToOllie()) {
    Serial.println("\n--- WAITING 1.5 SECONDS BEFORE MOVEMENT TEST ---");
    delay(1500);

    // MOVE FEW CENTIMETERS TO PROVE WIRELESS CONTROL!
    rollFewCentimeters();

    Serial.println("=============================================");
    Serial.println("INTERACTIVE SERIAL COMMANDS READY:");
    Serial.println(" Type 'f' -> Roll Forward 10-15 cm");
    Serial.println(" Type 'b' -> Roll Backward 10-15 cm");
    Serial.println(" Type 's' -> 360 Spin Trick");
    Serial.println(" Type 'c' -> Cycle LED Color");
    Serial.println(" Type 'x' -> Emergency Stop / Brake");
    Serial.println("=============================================\n");
  } else {
    Serial.println("[ERROR] Could not link to Ollie. Tap Ollie to wake it up and restart!");
  }
}

uint8_t colorCycle = 0;

void loop() {
  // Automatic Keepalive every 2 seconds so Ollie NEVER disconnects / sleeps
  if (isConnected && millis() - lastKeepAlive >= 2000) {
    lastKeepAlive = millis();
    sendKeepAlive();
  }

  // Read Serial Monitor keypresses from user
  if (Serial.available()) {
    char key = Serial.read();
    if (key == '\r' || key == '\n') return;

    Serial.printf("\n[Input] Command '%c' received:\n", key);

    if (key == 'f' || key == 'F') {
      rollFewCentimeters();
    } 
    else if (key == 'b' || key == 'B') {
      Serial.println("[Ollie] Rolling Backward...");
      setOllieColor(255, 100, 0); // Orange
      rollOllie(80, 180, 1); // Heading 180 = reverse
      delay(800);
      stopOllie();
      setOllieColor(0, 229, 255);
    }
    else if (key == 's' || key == 'S') {
      spinTrick();
    }
    else if (key == 'c' || key == 'C') {
      colorCycle = (colorCycle + 1) % 5;
      if (colorCycle == 0) setOllieColor(0, 229, 255);       // Cyan
      else if (colorCycle == 1) setOllieColor(179, 136, 255); // Purple
      else if (colorCycle == 2) setOllieColor(0, 230, 118);   // Green
      else if (colorCycle == 3) setOllieColor(255, 23, 68);    // Red
      else if (colorCycle == 4) setOllieColor(255, 214, 0);   // Yellow
    }
    else if (key == 'x' || key == 'X') {
      stopOllie();
    }
  }

  delay(30);
}

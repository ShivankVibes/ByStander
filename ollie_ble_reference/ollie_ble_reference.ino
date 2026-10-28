#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEClient.h>

// Sphero Ollie Device Information
// Device Name: 2B-4FCD
static BLEAddress ollieAddress("ea:a4:5f:42:4f:cd");

// Sphero Ollie Robot Control Service UUID
static BLEUUID serviceUUID("22bb746f-2ba0-7554-2d6f-726568705327");

boolean connected = false;

bool connectToOllie() {
  Serial.print("Connecting to Ollie at: ");
  Serial.println(ollieAddress.toString().c_str());

  BLEClient* pClient = BLEDevice::createClient();
  
  if (!pClient->connect(ollieAddress)) {
    Serial.println("Connection failed!");
    return false;
  }
  Serial.println("Connected to Ollie!");

  BLERemoteService* pRemoteService = pClient->getService(serviceUUID);
  if (pRemoteService == nullptr) {
    Serial.println("Failed to find Sphero Service UUID.");
    pClient->disconnect();
    return false;
  }
  
  Serial.println("Ollie Service verified successfully!");
  return true;
}

void setup() {
  Serial.begin(115200);
  BLEDevice::init("");

  connected = connectToOllie();
  if (connected) {
    Serial.println("Desk Bot successfully linked to Ollie!");
  }
}

void loop() {
  // Add movement or color trigger commands here later
  delay(2000);
}

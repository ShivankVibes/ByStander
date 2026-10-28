/**
 * 4x4 Keypad Matrix + OLED Hardware Tester for ByStander
 * 
 * Tests your 4x4 Keypad Matrix in real time:
 * - Shows last pressed key in GIANT font on OLED displays
 * - Shows recent key press history (e.g., "1 2 3 A # *")
 * - Plays a short confirmation click beep on the buzzer (GPIO 26)
 * - Logs exact key, row, and column to Serial Monitor (115200 baud)
 */

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Keypad.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32
#define OLED_RESET -1

// Dual OLED Displays:
// Left OLED: Wire (SDA=21, SCL=22)
Adafruit_SSD1306 displayL(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
bool hasLeftOLED = false;

// Right OLED: Wire1 (SDA=18, SCL=19)
Adafruit_SSD1306 displayR(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire1, OLED_RESET);
bool hasRightOLED = false;

// Buzzer for audible click confirmation
const int BUZZER_PIN = 26;

// ====================================================
// 4x4 KEYPAD MATRIX CONFIGURATION
// ====================================================
const byte ROWS = 4;
const byte COLS = 4;

char keys[ROWS][COLS] = {
  {'1', '2', '3', 'A'},
  {'4', '5', '6', 'B'},
  {'7', '8', '9', 'C'},
  {'*', '0', '#', 'D'}
};

// 4 Row pins on ESP32:
// Pin 1 to 4 on standard keypad ribbon
byte rowPins[ROWS] = {13, 14, 27, 25};

// 4 Column pins on ESP32:
// Pin 5 to 8 on standard keypad ribbon
byte colPins[COLS] = {33, 32, 4, 23};

Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// Key press tracking
char currentKey = '-';
String keyHistory = "";
unsigned long totalPresses = 0;
unsigned long lastPressTime = 0;

void beep(int durationMs = 30) {
  digitalWrite(BUZZER_PIN, HIGH);
  delay(durationMs);
  digitalWrite(BUZZER_PIN, LOW);
}

void renderDisplays() {
  // Update Left Screen (Companion View / Last Key Status)
  if (hasLeftOLED) {
    displayL.clearDisplay();
    displayL.setTextColor(SSD1306_WHITE);
    
    displayL.setTextSize(1);
    displayL.setCursor(0, 0);
    displayL.print(F("KEYPAD TEST"));

    displayL.setCursor(75, 0);
    displayL.printf("#%lu", totalPresses);

    // Large Key Display
    displayL.setTextSize(3);
    displayL.setCursor(18, 9);
    displayL.printf("[%c]", currentKey);

    displayL.display();
  }

  // Update Right Screen (Detailed Info & Key History)
  if (hasRightOLED) {
    displayR.clearDisplay();
    displayR.setTextColor(SSD1306_WHITE);

    displayR.setTextSize(1);
    displayR.setCursor(0, 0);
    displayR.printf("LAST: '%c' (Total:%lu)", currentKey, totalPresses);

    displayR.setCursor(0, 11);
    displayR.print(F("HISTORY:"));

    displayR.setCursor(0, 22);
    // Show last 14 characters of history
    String dispHist = keyHistory;
    if (dispHist.length() > 14) {
      dispHist = dispHist.substring(dispHist.length() - 14);
    }
    displayR.print(dispHist.length() > 0 ? dispHist : F("(Press any key)"));

    displayR.display();
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println(F("\n=========================================="));
  Serial.println(F("=== 4x4 Keypad Matrix Hardware Tester ==="));
  Serial.println(F("=========================================="));
  Serial.println(F("Keypad Pinout Reference:"));
  Serial.println(F("  Row 1 -> GPIO 13"));
  Serial.println(F("  Row 2 -> GPIO 14"));
  Serial.println(F("  Row 3 -> GPIO 27"));
  Serial.println(F("  Row 4 -> GPIO 25"));
  Serial.println(F("  Col 1 -> GPIO 33"));
  Serial.println(F("  Col 2 -> GPIO 32"));
  Serial.println(F("  Col 3 -> GPIO 4"));
  Serial.println(F("  Col 4 -> GPIO 23"));
  Serial.println(F("==========================================\n"));

  pinMode(BUZZER_PIN, OUTPUT);
  beep(60); // Power on beep

  // Initialize Left OLED on Wire (SDA=21, SCL=22)
  Wire.begin(21, 22);
  hasLeftOLED = displayL.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  if (!hasLeftOLED) hasLeftOLED = displayL.begin(SSD1306_SWITCHCAPVCC, 0x3D);

  // Initialize Right OLED on Wire1 (SDA=18, SCL=19)
  Wire1.begin(18, 19);
  hasRightOLED = displayR.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  if (!hasRightOLED) hasRightOLED = displayR.begin(SSD1306_SWITCHCAPVCC, 0x3D);

  Serial.printf("[OLED] Left Screen: %s | Right Screen: %s\n", 
                hasLeftOLED ? "ONLINE" : "NOT FOUND",
                hasRightOLED ? "ONLINE" : "NOT FOUND");

  keypad.setDebounceTime(20);
  renderDisplays();
}

void loop() {
  char key = keypad.getKey();

  if (key != NO_KEY) {
    currentKey = key;
    totalPresses++;
    lastPressTime = millis();

    // Append to history string
    if (keyHistory.length() > 0) keyHistory += " ";
    keyHistory += key;

    // Beep confirmation
    beep(25);

    // Print to Serial
    Serial.printf("[Keypad] 🎯 Key Pressed: '%c' (Total: %lu)\n", key, totalPresses);

    // Update OLED screens
    renderDisplays();
  }

  delay(10);
}

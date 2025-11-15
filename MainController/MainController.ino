// MainController.ino
// Top-level sketch for IoT Motion Logger
// - Coordinates PIR detection, EEPROM ring buffer (overwrite), ESP01 sender, status and serial UI
// - Stores durations in seconds and persistent eventCounter per entry

#include <Arduino.h>
#include "MotionDetector.h"
#include "ESP01Driver.h"
#include "Status.h"
#include "EEPROMStorage.h"

// --- Configuration ---
const uint8_t PIR_PIN = 2;        // PIR input (HC-SR501 OUT)
const uint8_t ESP_POWER_PIN = 8;  // control CH_PD/EN through level shifter / transistor (set to -1 if not used)
const unsigned long THINGSPEAK_MIN_INTERVAL = 20000UL; // 20 seconds between ThingSpeak updates

// Serial options
const unsigned long SERIAL_BAUD = 115200;

// Instances (singletons used across files)
MotionDetector motion(PIR_PIN);
ESP01Driver esp(10 /*RX to ESP TX*/, 11 /*TX to ESP RX*/, ESP_POWER_PIN);
EEPROMStorage eepromStorage; // manages circular buffer with overwrite + persistent event counter
Status sysStatus; // shared status

// For user toggles
bool showEspRaw = false;
unsigned long lastThingSpeakSendTime = 0;

// helper: print user section header
void printUserHeader() {
  Serial.print("(USER) ");
}

// Process incoming serial commands from the user
void processSerialCommands() {
  if (!Serial.available()) return;
  String cmd = Serial.readStringUntil('\n');
  cmd.trim();

  printUserHeader();

  if (cmd.equalsIgnoreCase("esp on")) {
    Serial.println("ESP ON");
    esp.powerOn();
  }
  else if (cmd.equalsIgnoreCase("esp off")) {
    Serial.println("ESP OFF (graceful)");
    esp.powerOff(); // will request graceful disconnect then cut power
  }
  else if (cmd.equalsIgnoreCase("send")) {
    Serial.println("Force send (if ESP READY)");
    esp.requestImmediateSend = true;
  }
  else if (cmd.equalsIgnoreCase("status")) {
    sysStatus.print(Serial);
  }
  else if (cmd.equalsIgnoreCase("dump") || cmd.equalsIgnoreCase("show")) {
    eepromStorage.printAll(Serial);
  }
  else if (cmd.equalsIgnoreCase("clear")) {
    Serial.println("Clearing EEPROM storage (header + readings)...");
    eepromStorage.clearAll(); // implemented in EEPROMStorage.h
    sysStatus.storedReadingsCount = eepromStorage.size();
    Serial.println("EEPROM cleared.");
  }
  else if (cmd.equalsIgnoreCase("toggle_esp_raw")) {
    showEspRaw = !showEspRaw;
    Serial.print("ESP raw = ");
    Serial.println(showEspRaw ? "ON" : "OFF");
  }
  else {
    Serial.print("Unknown: ");
    Serial.println(cmd);
    Serial.println("Commands: esp on | esp off | send | status | dump | clear | toggle_esp_raw");
  }

  Serial.println();
}

void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(200);
  Serial.println();
  Serial.println("=== IoT Motion Logger ===");
  Serial.println();

  // initialize status and storage
  sysStatus.init();
  eepromStorage.begin(); // loads count/head and persistent event counter

  // initialize motion detector and give it storage & status
  motion.begin(&sysStatus, &eepromStorage);

  // initialize ESP driver with pointers
  esp.begin(&sysStatus, &eepromStorage);

  // Do NOT auto power on ESP
  sysStatus.print(Serial);
  Serial.println();
}

void loop() {
  // 1) Process user commands (non-blocking)
  processSerialCommands();

  // 2) PIR detection (handles warm-up and storage)
  motion.loop();

  // 3) ESP state machine (silent if OFF)
  esp.loop(showEspRaw);

  // 4) Send logic — only when ESP is READY
  unsigned long now = millis();
  bool canSendNow = (now - lastThingSpeakSendTime) >= THINGSPEAK_MIN_INTERVAL;

  if (sysStatus.espState == Status::ESPState::READY &&
      eepromStorage.hasPending() &&
      (esp.requestImmediateSend || canSendNow)) {

    EEPROMStorage::Reading r;
    if (eepromStorage.peekOldest(r)) {
      Serial.print("[MAIN] Sending oldest reading -> dur_sec=");
      Serial.print(r.duration_sec);
      Serial.print("  evt#=");
      Serial.println(r.eventCounter);

      if (esp.sendReadingToThingSpeak(r)) {
        // start timestamp for rate limiting
        lastThingSpeakSendTime = now;
        esp.requestImmediateSend = false;
        sysStatus.lastSendAttemptTime = now;
      } else {
        Serial.println("[MAIN] ESP could not start send (busy).");
      }
    }
  }

  // small idle delay
  delay(20);
}

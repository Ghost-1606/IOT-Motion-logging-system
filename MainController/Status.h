// Status.h
// Simple shared status structure updated by all modules.

#ifndef STATUS_H
#define STATUS_H

#include <Arduino.h>

struct Status {
  enum class ESPState : uint8_t { OFF=0, BOOTING=1, READY=2, SENDING=3, ERROR=4 };
  enum class PIRState : uint8_t { OFF=0, IDLE=1, MOTION=2 };

  ESPState espState;
  PIRState pirState;

  uint8_t storedReadingsCount;
  unsigned long lastSendAttemptTime;
  unsigned long lastSendSuccessTime;
  bool lastSendOk;

  void init() {
    espState = ESPState::OFF;
    pirState = PIRState::IDLE;
    storedReadingsCount = 0;
    lastSendAttemptTime = 0;
    lastSendSuccessTime = 0;
    lastSendOk = false;
  }

  void print(Print &out) {
    out.print("ESP: ");
    switch(espState) {
      case ESPState::OFF: out.print("OFF"); break;
      case ESPState::BOOTING: out.print("BOOTING"); break;
      case ESPState::READY: out.print("READY"); break;
      case ESPState::SENDING: out.print("SENDING"); break;
      case ESPState::ERROR: out.print("ERROR"); break;
    }
    out.print("  | PIR: ");
    switch(pirState) {
      case PIRState::OFF: out.print("OFF"); break;
      case PIRState::IDLE: out.print("IDLE"); break;
      case PIRState::MOTION: out.print("MOTION"); break;
    }
    out.print("  | Stored: ");
    out.print((int)storedReadingsCount);
    out.print("  | LastSendOk: ");
    out.print(lastSendOk ? "YES" : "NO");
    out.print("  | LastSendAt: ");
    out.print(lastSendSuccessTime);
    out.println();
  }
};

#endif

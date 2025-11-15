// MotionDetector.h
// Handles PIR input, measures duration of motion events (milliseconds),
// stores completed events in EEPROMStorage (via push).
// Minimizes writes: only writes when an event completes.

#ifndef MOTION_DETECTOR_H
#define MOTION_DETECTOR_H

#include <Arduino.h>
#include "EEPROMStorage.h"
#include "Status.h"

class MotionDetector {
  public:
    MotionDetector(uint8_t pin) : pirPin(pin) {}

    void begin(Status *statusPtr = nullptr) {
      status = statusPtr;
      pinMode(pirPin, INPUT);
      lastState = digitalRead(pirPin);
      if (status) status->pirState = (lastState ? Status::PIRState::MOTION : Status::PIRState::IDLE);
    }

    // main loop: call frequently
    void loop() {
      int state = digitalRead(pirPin);
      unsigned long now = millis();

      if (state && !lastState) {
        // rising edge - motion started
        motionStart = now;
        motionActive = true;
        if (status) status->pirState = Status::PIRState::MOTION;
      } else if (!state && lastState && motionActive) {
        // falling edge - motion ended
        unsigned long duration = now - motionStart;
        onMotionComplete(duration, motionStart);
        motionActive = false;
        if (status) status->pirState = Status::PIRState::IDLE;
      }
      lastState = state;
    }

    // summary print
    void printSummary(Print &out) {
      out.print("MotionActive: ");
      out.print(motionActive ? "YES" : "NO");
      out.print("  currState: ");
      out.print(lastState ? "HIGH" : "LOW");
      out.print("  lastDur(ms): ");
      out.print(lastDurationMs);
    }

    // set storage pointer (alternatively, main will push)
    void setStorage(EEPROMStorage *s) { storage = s; }

  private:
    uint8_t pirPin;
    int lastState = 0;
    bool motionActive = false;
    unsigned long motionStart = 0;
    unsigned long lastDurationMs = 0;
    EEPROMStorage *storage = nullptr;
    Status *status = nullptr;

    // Called when a motion event completes
    void onMotionComplete(unsigned long duration_ms, unsigned long ts) {
      lastDurationMs = duration_ms;
      // store reading into storage (if available)
      if (storage) {
        EEPROMStorage::Reading r;
        r.duration_ms = duration_ms;
        r.ts = ts;
        if (storage->push(r)) {
          if (status) {
            status->storedReadingsCount = storage->size();
          }
        } else {
          // storage full - (could signal user via status)
        }
      }
    }
};

#endif

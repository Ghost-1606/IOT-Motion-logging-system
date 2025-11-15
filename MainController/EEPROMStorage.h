// EEPROMStorage.h
// Circular buffer stored in EEPROM with minimized writes.
// - header at address 0: uint8_t count, uint8_t head (index of oldest)
// - readings start at address 16 (safe offset).
// - each Reading stored as 4 bytes duration_ms (uint32_t) and 4 bytes timestamp (uint32_t) -> 8 bytes per entry.
// - max entries = 10

#ifndef EEPROM_STORAGE_H
#define EEPROM_STORAGE_H

#include <Arduino.h>
#include <EEPROM.h>

class EEPROMStorage {
  public:
    static const uint8_t MAX_ENTRIES = 10;
    struct Reading {
      uint32_t duration_ms;
      uint32_t ts; // recorded timestamp (millis at record or epoch)
    };

    void begin() {
      // read header
      count = EEPROM.read(0);
      head = EEPROM.read(1);
      if (count > MAX_ENTRIES) { count = 0; head = 0; }
    }

    bool isFull() const { return count >= MAX_ENTRIES; }
    bool isEmpty() const { return count == 0; }
    bool hasPending() const { return !isEmpty(); }
    uint8_t size() const { return count; }

    // add reading to next free slot in EEPROM (tail). Minimizes writes:
    // only writes the reading bytes and updates count/head bytes.
    bool push(const Reading &r) {
      if (isFull()) return false;
      uint8_t tailIndex = (head + count) % MAX_ENTRIES;
      writeReadingToEEPROM(tailIndex, r);
      ++count;
      EEPROM.update(0, count);
      // head remains same
      return true;
    }

    // peek oldest reading without removing
    bool peekOldest(Reading &outR) {
      if (isEmpty()) return false;
      readReadingFromEEPROM(head, outR);
      return true;
    }

    // remove oldest (after confirmed send)
    bool popOldest() {
      if (isEmpty()) return false;
      // optional: clear memory (not necessary). We'll just advance head & decrement count.
      head = (head + 1) % MAX_ENTRIES;
      if (count) --count;
      EEPROM.update(1, head);
      EEPROM.update(0, count);
      return true;
    }

    // print a short summary
    void printSummary(Print &out) {
      out.print("Entries: "); out.print((int)count);
      out.print("  head: "); out.print((int)head);
    }

    void printAll(Print &out) {
      out.println("EEPROM Stored Readings:");
      for (uint8_t i = 0; i < count; ++i) {
        uint8_t idx = (head + i) % MAX_ENTRIES;
        Reading r;
        readReadingFromEEPROM(idx, r);
        out.print(i); out.print(": duration_ms="); out.print(r.duration_ms); out.print(" ts=");
        out.println(r.ts);
      }
    }

  private:
    uint8_t count = 0;
    uint8_t head = 0;
    static const uint16_t ADDR_HEADER = 0;
    static const uint16_t ADDR_READINGS = 16; // start addr for readings
    static const uint16_t READING_BYTES = 8;

    void writeReadingToEEPROM(uint8_t index, const Reading &r) {
      uint16_t addr = ADDR_READINGS + index * READING_BYTES;
      // write 4 bytes duration_ms then 4 bytes ts; use EEPROM.update to minimize writes
      EEPROM.update(addr + 0, (r.duration_ms >> 0) & 0xFF);
      EEPROM.update(addr + 1, (r.duration_ms >> 8) & 0xFF);
      EEPROM.update(addr + 2, (r.duration_ms >> 16) & 0xFF);
      EEPROM.update(addr + 3, (r.duration_ms >> 24) & 0xFF);
      EEPROM.update(addr + 4, (r.ts >> 0) & 0xFF);
      EEPROM.update(addr + 5, (r.ts >> 8) & 0xFF);
      EEPROM.update(addr + 6, (r.ts >> 16) & 0xFF);
      EEPROM.update(addr + 7, (r.ts >> 24) & 0xFF);
    }

    void readReadingFromEEPROM(uint8_t index, Reading &r) {
      uint16_t addr = ADDR_READINGS + index * READING_BYTES;
      uint32_t v0 = EEPROM.read(addr) | (EEPROM.read(addr + 1) << 8) | (EEPROM.read(addr + 2) << 16) | (EEPROM.read(addr + 3) << 24);
      uint32_t v1 = EEPROM.read(addr + 4) | (EEPROM.read(addr + 5) << 8) | (EEPROM.read(addr + 6) << 16) | (EEPROM.read(addr + 7) << 24);
      r.duration_ms = v0;
      r.ts = v1;
    }
};

#endif

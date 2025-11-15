// ESP01Driver.h
// Manages SoftwareSerial communication with ESP01 (AT commands).
// Boot control via a power pin (optional).
// Sends ThingSpeak updates via TCP using AT commands.
// Waits and parses responses and signals success to EEPROMStorage to pop entries.

#ifndef ESP01_DRIVER_H
#define ESP01_DRIVER_H

#include <Arduino.h>
#include <SoftwareSerial.h>
#include "Status.h"
#include "EEPROMStorage.h"

// Replace with your ThingSpeak API key
#define THINGSPEAK_API_KEY "YOUR_THINGSPEAK_API_KEY"
// Replace with your SSID / PWD
#define WIFI_SSID "YOUR_SSID"
#define WIFI_PASS "YOUR_PASSWORD"

class ESP01Driver {
  public:
    ESP01Driver(uint8_t rxPin, uint8_t txPin, int8_t powerPin = -1)
      : ss(rxPin, txPin), powerPin(powerPin) { requestImmediateSend = false; }

    void begin(Status *statusPtr, EEPROMStorage *storagePtr) {
      sysStatus = statusPtr;
      storage = storagePtr;
      ss.begin(4800); // as requested
      if (powerPin >= 0) {
        pinMode(powerPin, OUTPUT);
        digitalWrite(powerPin, LOW); // keep off by default
      }
      lastAtCheck = 0;
    }

    // power control
    void powerOn() {
      if (powerPin >= 0) {
        digitalWrite(powerPin, HIGH);
        delay(300); // let module settle
      }
      sysStatus->espState = Status::ESPState::BOOTING;
      // flush serial buffer
      while (ss.available()) ss.read();
      // try to set up wifi connection
      sendAt("AT\r\n"); // wake
      delay(200);
      configureWiFi();
    }

    void powerOff() {
      if (powerPin >= 0) {
        digitalWrite(powerPin, LOW);
      }
      sysStatus->espState = Status::ESPState::OFF;
    }

    // main state machine - call frequently
    void loop(bool showRawResponses) {
      // read raw responses from ESP and process lines
      while (ss.available()) {
        String line = ss.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) continue;
        if (showRawResponses) {
          Serial.print("[ESP RAW] "); Serial.println(line);
        }
        handleResponse(line);
      }

      // periodic check: if it's booting, try to see if it responds
      unsigned long now = millis();
      if (sysStatus->espState == Status::ESPState::BOOTING && now - lastAtCheck > 2000) {
        sendAt("AT\r\n");
        lastAtCheck = now;
      }
    }

    // return true if esp is ready to accept send (wifi connected & not busy)
    bool isReadyForSend() {
      return (sysStatus->espState == Status::ESPState::READY);
    }

    // send a reading to ThingSpeak; returns true if start succeeded (CIPSTART issued)
    bool sendReadingToThingSpeak(const EEPROMStorage::Reading &r) {
      if (!isReadyForSend()) return false;
      // construct GET request
      char buffer[200];
      // field1 used for duration_ms, field2 for timestamp if desired
      snprintf(buffer, sizeof(buffer),
        "GET /update?api_key=%s&field1=%lu HTTP/1.1\r\nHost: api.thingspeak.com\r\nConnection: close\r\n\r\n",
        THINGSPEAK_API_KEY, (unsigned long)r.duration_ms);
      // Start TCP connection
      sendAt("AT+CIPSTART=\"TCP\",\"api.thingspeak.com\",80\r\n");
      delayingForResponse = true;
      pendingPayload = String(buffer);
      pendingSendState = 1; // next step after CIPSTART is to wait for "OK" then send CIPSEND
      sysStatus->espState = Status::ESPState::SENDING;
      return true;
    }

    // call to show summary
    void printSummary(Print &out) {
      out.print("ESPstate=");
      switch(sysStatus->espState) {
        case Status::ESPState::OFF: out.print("OFF"); break;
        case Status::ESPState::BOOTING: out.print("BOOTING"); break;
        case Status::ESPState::READY: out.print("READY"); break;
        case Status::ESPState::SENDING: out.print("SENDING"); break;
        case Status::ESPState::ERROR: out.print("ERROR"); break;
      }
      out.print("  pendingSend="); out.print(pendingPayload.length() ? "YES" : "NO");
      out.print("  reqSend="); out.print(requestImmediateSend ? "Y" : "N");
    }

    // public flag can be triggered by main to force immediate send
    bool requestImmediateSend;

  private:
    SoftwareSerial ss;
    int8_t powerPin;
    Status *sysStatus = nullptr;
    EEPROMStorage *storage = nullptr;

    // send buffer/payload management
    String pendingPayload = "";
    int pendingSendState = 0; // 0 none, 1 waiting for CIPSTART OK, 2 waiting for '>' for CIPSEND
    bool delayingForResponse = false;
    unsigned long lastAtCheck;

    void sendAt(const char *cmd) {
      ss.print(cmd);
      // also echo to Serial for debugging
      Serial.print("[ESP CMD] "); Serial.print(cmd);
    }

    void configureWiFi() {
      // Basic WiFi connection steps; minimal and synchronous-ish
      sendAt("AT\r\n");
      delay(200);
      sendAt("AT+CWMODE=1\r\n"); // station
      delay(200);
      // connect to WiFi (may take a while) - we do it non-blocking by issuing command and waiting for response in loop()
      String cmd;
      cmd = String("AT+CWJAP=\"") + WIFI_SSID + "\",\"" + WIFI_PASS + "\"\r\n";
      sendAt(cmd.c_str());
      // After successful connection, the ESP will reply with "WIFI CONNECTED" and "OK"
      // handleResponse() will set state to READY when we detect connection.
    }

    // Process one received line from ESP
    void handleResponse(const String &line) {
      // common responses: OK, ERROR, WIFI CONNECTED, WIFI GOT IP, SEND OK, > (prompt), CONNECT, CLOSED
      if (line.indexOf("OK") >= 0 && sysStatus->espState == Status::ESPState::BOOTING) {
        // simple heuristic: treat OK after setup as indication everything's fine
        // but wait for "WIFI GOT IP" ideally
      }
      if (line.indexOf("WIFI GOT IP") >= 0) {
        sysStatus->espState = Status::ESPState::READY;
        Serial.println("[ESP] WiFi connected, READY.");
      }
      if (line.indexOf("WIFI CONNECTED") >= 0) {
        // waiting for GOT IP
      }
      if (line.indexOf("ERROR") >= 0) {
        sysStatus->espState = Status::ESPState::ERROR;
      }
      if (line.indexOf("DNS FAIL") >= 0) {
        sysStatus->espState = Status::ESPState::ERROR;
      }

      // Sent when AT+CIPSTART succeeds: "OK" then "CONNECT" or "ALREADY CONNECT"
      if ((line.indexOf("CONNECT") >= 0 || line.indexOf("ALREADY CONNECT") >= 0) && pendingSendState == 1) {
        // Now send CIPSEND with payload length
        int len = pendingPayload.length();
        char tmp[40];
        snprintf(tmp, sizeof(tmp), "AT+CIPSEND=%d\r\n", len);
        sendAt(tmp);
        pendingSendState = 2;
        return;
      }

      // ESP shows '>' when ready to accept payload
      if (line.endsWith(">") && pendingSendState == 2) {
        // send payload
        ss.print(pendingPayload);
        Serial.print("[ESP CMD] <payload sent> len=");
        Serial.println(pendingPayload.length());
        pendingSendState = 3; // waiting for SEND OK
        return;
      }

      // confirm that payload was sent
      if (line.indexOf("SEND OK") >= 0 || line.indexOf("SEND FAIL") >= 0) {
        if (line.indexOf("SEND OK") >= 0) {
          Serial.println("[ESP] SEND OK - marking reading as sent.");
          // On success, remove oldest from EEPROM storage
          if (storage && storage->hasPending()) {
            storage->popOldest();
            if (sysStatus) {
              sysStatus->storedReadingsCount = storage->size();
              sysStatus->lastSendOk = true;
              sysStatus->lastSendSuccessTime = millis();
            }
          }
          // after send, close TCP (it was connection: close; connection closes when remote closes)
        } else {
          Serial.println("[ESP] SEND FAIL");
          if (sysStatus) sysStatus->lastSendOk = false;
        }
        // clear pending
        pendingPayload = "";
        pendingSendState = 0;
        sysStatus->espState = Status::ESPState::READY;
        return;
      }

      // When remote closes connection - we can treat it as send complete if not already handled
      if (line.indexOf("CLOSED") >= 0) {
        // clear pending just in case
        pendingPayload = "";
        pendingSendState = 0;
        sysStatus->espState = Status::ESPState::READY;
      }
    }
};

#endif

/**
 * Copyright 2022 IBM Corp. All Rights Reserved.
 */

#include <SPI.h>
#include "eeprom.h"
#include "protocol.h"
#include "RF24.h"
#include "global.h"

// Main entities
RF24 radio(P2_0, P2_1); // P2.0=CE, P2.1=CSN
//Enrf24 radio(P2_0, P2_1, P2_2);  // P2.0=CE, P2.1=CSN, P2.2=IRQ
Eeprom eeprom;
Protocol protocol;

// state variables
unsigned int tagid = 0;
unsigned long lastPing = 0;
unsigned long lastListen = 0;
unsigned long listenDuration = 0;
unsigned long readerListen = 0;
unsigned long readerDuration = 0;
unsigned long lastStopped  = 0;

// Power down and sleep for just under the specified time
void deepSleep(unsigned long time) {
  if (time >= PING_PERIOD_MS) {
    // should not sleep longer than ping period
    time = PING_PERIOD_MS - 10;
  }
  radio.powerDown();

  if (time > 10) {
    sleep(time - 10); // adjustment for wakeup time, etc.
  } else {
    sleep(time);
  }
}

#ifdef SHUTDOWN_TIMEOUT_MS
void interruptTag() {
    if (lastStopped == 0) return; // avoid multiple interrupt triggers
    lastStopped = 0;

    //wake tag up when interrupted
    wakeup();
}

void shutdownTag() {
  if (!protocol.isStopped) return; 

  //if the tag is stopped, shutdown after timeout
  if (lastStopped == 0) {
    //start the timer for shutdown timeout
    lastStopped = millis();
  } else {
    if (millis() - lastStopped > SHUTDOWN_TIMEOUT_MS) {
      //shutdown timeout has occured so shutdown tag
      PRINTLN("Shutting down to save power...");

      //set up interrupt pin
      pinMode(P2_3, INPUT_PULLDOWN); // pin with reed switch attached
      attachInterrupt(P2_3, interruptTag, RISING);

      // shutdown hardware and then the MCU
      radio.stopListening();
      radio.powerDown();
      digitalWrite(LED, LOW);
      suspend();

      detachInterrupt(P2_3);
      PRINTLN("Waking up...");
      digitalWrite(LED, HIGH);
      sleep(500);
      digitalWrite(LED, LOW);
    }
  }
}
#endif

unsigned int readTagId() {
    unsigned long timer = millis();
    boolean on = false;
    char intBuffer[6], index=0;
    int delimiter = (int) '\n';
    int ch;
    while ((ch = Serial.read()) != delimiter) {
        if (TIME_INTERVAL(timer) > 100) {
          timer = millis();
          digitalWrite(LED, on);
          on = ! on;
        }
        
        if (ch == -1) {
            // Handle error
        } else if (ch == delimiter) {
            intBuffer[index++] = 0;
            break;
        } else {
            intBuffer[index++] = (char) ch;
            Serial.print((char) ch);
        }
    }

    // Convert ASCII-encoded integer to an int
    unsigned int tagid = atoi(intBuffer);
    return tagid;
}

boolean sendPing() {
  if (protocol.isStopped) return false;

  if (TIME_INTERVAL(lastPing) >= (protocol.metaData.pingPeriodMs - 10)) {
    lastPing = millis();
  
    protocol.setTXPower();
    byte pingStrong = ((protocol.metaData.pingTxRange & 0b10000000) > 0 ? 1 : 0);

    protocol.packetLen = 0;
    protocol.packet[protocol.packetLen++] = CMD_PING;
    protocol.packet[protocol.packetLen++] = tagid >> 8;
    protocol.packet[protocol.packetLen++] = tagid & 0xFF;
    protocol.packet[protocol.packetLen++] = pingStrong;
    protocol.radioWrite();
    delay(10);
    protocol.radioWrite();

    #ifdef DEBUG
      Serial.print(".");
      Serial.flush();      
    #endif
    
    return true;
  }

  return false;
}

void listenForPings() {
  if (protocol.isStopped) return;

  if (TIME_INTERVAL(lastListen) >= (protocol.metaData.listenPeriodSecs * 1000)) {
    lastListen = millis();    
    radio.startListening();

    #ifdef DEBUG 
      Serial.print("&");
      Serial.flush();
    #endif

    listenDuration = millis();
    while (TIME_INTERVAL(listenDuration) <= (protocol.metaData.pingPeriodMs + 10)) {
      // did we get something?
      if (protocol.radioRead() > 0) {
        protocol.process(protocol.packet, protocol.packetLen);
      }
      digitalWrite(LED, LOW);
  
      if (sendPing()) {
        radio.startListening();
      }
    }

    radio.stopListening();
    radio.flush_rx();
    digitalWrite(LED, LOW);
  }
}

void listenForReaders() {
  if (TIME_INTERVAL(readerListen) >= (protocol.metaData.readerPeriodSecs * 1000)) {
    #ifdef DEBUG
      //tagid++; // uncomment for session load testing
      Serial.print("%");
      Serial.flush();
    #endif

    readerListen = millis();
    protocol.switchToReaderChannel();

    readerDuration = millis();
    while (TIME_INTERVAL(readerDuration) <= (unsigned long) READER_DURATION) {
      radio.powerUp();
      // is a reader nearby?
      if (protocol.radioRead() > 0) {
        if (protocol.packet[0] == CMD_PING) {
          while (protocol.radioRead() > 0) delay(1); // clear read buffer

          // let the reader know we're here by sending 
          // a few PING packets on (the noisy) READER channel
          protocol.packetLen = 0;
          protocol.packet[protocol.packetLen++] = CMD_PING;
          protocol.packet[protocol.packetLen++] = tagid >> 8;
          protocol.packet[protocol.packetLen++] = tagid & 0xFF;
          protocol.packet[protocol.packetLen++] = 0x01;
          for (byte i=0; i < 3; i++) { // send 3 pings
            protocol.radioWrite();
            delay(1);
          }
          
          // listen on download channel in case reader wants to download data
          radio.flush_rx();
          protocol.switchToDownloadChannel();
          unsigned long timer = millis();
          while (TIME_INTERVAL(timer) <= 100) {
            if (protocol.radioRead() > 0) {
              protocol.process(protocol.packet, protocol.packetLen);
            }
          }

          #ifdef DEBUG
            Serial.print(")");
            Serial.flush();
          #endif
        }
      }

      digitalWrite(LED, LOW);
    }

    protocol.switchToPingChannel();
  }  
}

void testEeprom() {
  tagid = eeprom.read(0x00) + (eeprom.read(0x01) << 8);
  byte check1 = eeprom.read(0x02);
  byte check2 = eeprom.read(0x03);

  if (check1 != CHECK_BYTE1 || check2 != CHECK_BYTE2) {
    // corrupted or unprogrammed eeprom
    Serial.print("** No Tag ID. Tag ID ranges: 0 -> ");
    Serial.print(MAX_TAG_ID);
    Serial.print(" for wearable, ");
    Serial.print((unsigned int) MAX_TAG_ID + 1);
    Serial.print(" -> ");
    Serial.print(MAX_TAGS);
    Serial.println(" for locator");
    Serial.println("** Enter tag id: ");

    tagid = readTagId();

    Serial.println("");
    Serial.print("Writing tag id ");
    Serial.println(tagid, DEC);
    eeprom.write(0x00, tagid);
    eeprom.write(0x01, tagid >> 8);
    eeprom.write(0x02, CHECK_BYTE1);
    eeprom.write(0x03, CHECK_BYTE2);

    protocol.resetMetaData();
    testEeprom();
  } else {
    Serial.print("** TAG ID ");
    Serial.println(tagid, DEC);
  }
}

void setup() {
  // setup led and keep it on for a second to indicate power-up
  pinMode( LED, OUTPUT );
  digitalWrite(LED, HIGH);
  sleep(1000);
  digitalWrite(LED, LOW);

  // set up serial comms
  Serial.begin(9600);
  Serial.println("Starting RF tag v8.0");
  Serial.flush();

  // Setup SPI
  SPI.begin();
  SPI.setDataMode(SPI_MODE0);
  SPI.setBitOrder(MSBFIRST);
  SPI.setClockDivider(SPI_CLOCK_DIV2);

  // set up eeprom
  eeprom.begin();
  testEeprom();

  // setup protocol handler
  protocol.begin(tagid, &radio, &eeprom);

  // By default, tags/locators are in STOP mode until started
  #ifdef DEBUG
  protocol.isStopped = false;
  #else
  protocol.isStopped = true;
  #endif

  //protocol.resetData(); // for testing purposes only

  #ifdef DEBUG
    Serial.print("Meta data size: ");
    Serial.println(sizeof(MetaData));
    Serial.print("Tag data size: ");
    Serial.println(sizeof(TagData));
  #endif

  #ifdef TEST_BED
    protocol.metaData.listenPeriodSecs = 10;
    protocol.metaData.sessionTimeoutSecs = 30;
  #endif
}

void loop() {
  if (sendPing()) {
    radio.powerDown();
  }

  if (!IS_LOCATOR(tagid)) {
    radio.powerUp();
    listenForPings();
    protocol.tick();
  }

  listenForReaders();

  // Power optimization: deep sleep until next event
  unsigned long nextPing = TIME_INTERVAL2(lastPing + protocol.metaData.pingPeriodMs, millis());
  unsigned long nextListen = TIME_INTERVAL2(lastListen + (protocol.metaData.listenPeriodSecs * 1000), millis());
  unsigned long nextReader = TIME_INTERVAL2(readerListen + (protocol.metaData.readerPeriodSecs * 1000), millis());

  if (protocol.isStopped) {
    // when stopped, keep listening for readers
    deepSleep(nextReader);

    // shutdown tag if stopped for a long time, to save battery
    shutdownTag();
  } else if (IS_LOCATOR(tagid)) {
    // locators don't listen for tags
    deepSleep(min(nextPing, nextReader));
  } else {
    // normal operation: sleep until next ping/listen/reader
    deepSleep(min(nextPing, min(nextListen, nextReader)));
  }
}

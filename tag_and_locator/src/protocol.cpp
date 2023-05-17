/**
 * Copyright 2022 IBM Corp. All Rights Reserved.
 */

#include "protocol.h"
#include "global.h"

// multicast address
byte Protocol::addr[] = MULTICAST_ADDR;

// constructor
Protocol::Protocol() {
  isStopped = true;
  noCommand = true;
  lastReset = 0;
  sessionStartSecs = 0;
  d = TagData();
}

// call in setup() to initialize
void Protocol::begin(unsigned int _tagid, RF24 *_radio, Eeprom *_eep) {
  tagid = _tagid;
  eeprom = _eep;
  radio = _radio;

  resetSessionData();

  readMetaData();
  if (metaData.listenPeriodSecs == 0 ||
       metaData.readerPeriodSecs == 0 ||
       metaData.sessionTimeoutSecs == 0 ||
       metaData.pingChannel != PING_CHANNEL ||
       metaData.downloadChannel != DOWNLOAD_CHANNEL ||
       metaData.readerChannel != READER_CHANNEL) {
    Serial.println("Resetting metadata");
    resetMetaData();
  }

  // set up NRF radio
  radio->begin();
  if (!radio->isChipConnected()) Serial.println("NRF not connected");
  radio->setDataRate(NRF_SPEED);
  radio->setChannel(metaData.pingChannel);  // speed, channel
  radio->openWritingPipe(addr);
  radio->openReadingPipe(1, addr);
  //radio->enableDynamicPayloads();
  #ifdef AUTO_ACK
    radio->setAutoAck(true);
    radio->setRetries(10, 20);
  #else
    radio->setAutoAck(false);
  #endif
  radio->setCRCLength(RF24_CRC_8); // 8-bit CRC
}

// regular house-keeping - should not execute for more than a few milliseconds!
void Protocol::tick() {
  #ifdef LOAD_TEST
    loadTest();
  #endif

  // check for expired sessions and write them out to EEPROM
  for (int j=0; j < MAX_RAM_SESSIONS; j++) {
    if (sessions[j].tagid > 0 && 
          (secondsElapsed(sessions[j].lastSeenSeconds)) > metaData.sessionTimeoutSecs) {
        // this session has expired, so write to EEPROM and remove from RAM
        #ifdef DEBUG
          PRINT("[");
          PRINT(sessions[j].tagid);
          PRINT("]-");
        #endif

        unsigned long startAddr = EEPROM_DATA_START + sizeof(MetaData);
        for (i = startAddr; i < EEPROM_SIZE; i += sizeof(TagData)) {
          readTagData(&d, i);
          if (d.tagid > 0 && d.check == CHECK_BYTE) {
            // this slot is occupied
          } else {
            // this slot is free
            sessionToTagData(&sessions[j], &d);
            writeTagData(i, &d); // write the tag data to EEPROM
            break;
          }
        }

        // NOTE: At this point, if EEPROM is full, data is lost
        sessions[j].tagid = 0; // free this slot in RAM
        sessions[j].lastSeenSeconds = 0;
    }
  }

  // for new wearables with no off switch, do an auto-stop to save battery
  if (!IS_LOCATOR(tagid) && !isStopped && 
      TIME_INTERVAL2(seconds(), sessionStartSecs) > AUTO_STOP_SECONDS) {
    isStopped = true;
  }
}

int Protocol::radioRead() {
  if (radio->available()) {
    packetLen = radio->getPayloadSize();
    radio->read(packet, sizeof(packet));
    // Serial.print("< ");
    // for (i=0; i < 10; i++) {
    //   Serial.print(" ");
    //   Serial.print(packet[i], HEX);  
    // }
    // Serial.println();
    return packetLen;
  }

  return 0;
}

void Protocol::radioWrite() {
  radio->stopListening();
  radio->write(packet, packetLen);
  // if (packet[0] != CMD_PING) {
  //   Serial.print("> ");
  //   for (i=0; i < 10; i++) {
  //     Serial.print(" ");
  //     Serial.print(packet[i], HEX);  
  //   }
  //   Serial.println();
  // }
}

// process an incoming payload from a remote tag
void Protocol::process(byte* inbuf, int len) {
  digitalWrite(LED, LOW); // make sure we don't leave LED on
  unsigned int remoteTagId = getRemoteTagId(inbuf);

  if (len == 0) return;

  if (inbuf[0] == CMD_PING) {
    handlePing(inbuf, len);
  } else if (inbuf[0] == CMD_START && remoteTagId == tagid) {
    PRINTLN("> START");
    sendAckWithBatteryLevel();
    if (isStopped) {
      noCommand = false;
      isStopped = false;
      resetSessionData();
      resetData();
      sessionStartSecs = seconds();
    }
  } else if (inbuf[0] == CMD_STOP && remoteTagId == tagid) {
    PRINTLN("> STOP");
    sendAck();
    if (!isStopped) {
      noCommand = false;
      isStopped = true;
    }
  } else if (inbuf[0] == CMD_RESET && remoteTagId == tagid) {
    sendAck();
    if (TIME_INTERVAL(lastReset) > 2000) {
      lastReset = millis();
      PRINTLN("> RESET");
      noCommand = false;
  
      resetData();
    }
  } else if (inbuf[0] == CMD_DOWNLOAD && remoteTagId == tagid) {
    PRINTLN("> DOWNLOAD");
    noCommand = false;
    uploadData();
  } else if (inbuf[0] == CMD_DL_AND_RESET && remoteTagId == tagid) {
    PRINTLN("> DOWNLOAD_AND_RESET");
    noCommand = false;
    uploadData();
  } else if (inbuf[0] == CMD_READ_SETTINGS && remoteTagId == tagid) {
    PRINTLN("> READ_SETTINGS");
    uploadSettings(inbuf, len);
  } else if (inbuf[0] == CMD_WRITE_SETTING && remoteTagId == tagid) {
    PRINTLN("> WRITE_SETTING");
    writeSetting(inbuf, len);
  } else {
    PRINT("> Unknown ");
    PRINTLN(inbuf[0], HEX);
  }
}

void Protocol::clearBuffer() {
  radio->flush_rx();
}

void Protocol::resetSessionData() {
  // reset session data
  for (int i=0; i < MAX_RAM_SESSIONS; i++) {
    sessions[i].tagid = 0;
    sessions[i].lastSeenSeconds = 0;
  }
}

void Protocol::handlePing(byte* inbuf, int len) {
  if (isStopped) return;
  unsigned int remoteTagId = getRemoteTagId(inbuf);

  // Transmitter will specify if ping must be strong in the third byte
  boolean strong = radio->testRPD();
  bool needStrongPing = inbuf[3]; //metaData.pingTxRange & 0b10000000;
  if ((needStrongPing && strong) || !needStrongPing) {
    #ifdef DEBUG
      digitalWrite(LED, HIGH);  // show pings on LED    
    #endif

    if (strong) {
      PRINT("[");
      PRINT(remoteTagId, DEC);
      PRINT("]");
    } else {
      PRINT("{");
      PRINT(remoteTagId, DEC);
      PRINT("}");      
    }

    SessionLookup* d = getTagData(remoteTagId);
    if (d == NULL) {
      #ifdef DEBUG
        PRINTLN("!Session not found!");
      #endif
      return;
    }

    d->lastSeenSeconds = seconds();
  }
}

void Protocol::handleDownload(byte* inbuf, int len) {
  // listen for the download command for a limited time

  switchToPingChannel();
  radio->startListening();
  
  unsigned long timer1 = millis();
  boolean dataSent = false;

  while (!dataSent && TIME_INTERVAL(timer1) < 30000) {
    delay(1);
    radio->read(inbuf, sizeof(inbuf));
    if (radio->getDynamicPayloadSize() > 0) {
      unsigned int remoteTagId = getRemoteTagId(inbuf);
      if (inbuf[0] == CMD_DOWNLOAD && remoteTagId == tagid) {
        uploadData();
        dataSent = true;
        break;
      }
    }
  }

  if (!dataSent) PRINT("Timed out.");
  clearBuffer();
}

// extrax the tagid from the ping packet sent by remote
unsigned int Protocol::getRemoteTagId(byte* inbuf) {
  return (inbuf[1] << 8) + inbuf[2];
}

// return the tag data for specified tag id
SessionLookup* Protocol::getTagData(unsigned int tagId) {
  SessionLookup *ret = NULL;

  for (i=0; i < MAX_RAM_SESSIONS; i++) {
    if (sessions[i].tagid == tagId) {
      // we found an existing session in RAM
      ret = &sessions[i];
      break;
    }
  }

  if (ret == NULL) {
    PRINT("+"); // indicate new session
    // create a new session for this tag
    i = 0;
    while (sessions[i].tagid != 0 && i <= MAX_RAM_SESSIONS) i++;
    if (i >= MAX_RAM_SESSIONS) {
      // ERROR - we have run out of RAM!
      PRINTLN("!OOM!");
      return NULL; // ignore this tag
    }

    sessions[i].tagid = tagId;
    sessions[i].firstSeenSeconds = seconds();
    sessions[i].lastSeenSeconds = seconds();
    ret = &sessions[i];
  }

  return ret;
}

void Protocol::readTagData(TagData *tagData, unsigned long addr) {
  // read data from eeprom
  for (int i = 0; i < TAGDATA_SIZE; i++) {
    byte b = eeprom->read(addr + i);
    *((byte *)tagData + i) = b;
  }
}

// write modified tag data back into eeprom
void Protocol::writeTagData(unsigned long addr, TagData *data) {
  byte* bytes = reinterpret_cast<byte*>(data);
  for (int i = 0; i < TAGDATA_SIZE; i++) {
    byte b = bytes[i];
    byte d = eeprom->read(addr + i);
    if (b != d) {
      eeprom->write(addr + i, b);
    }
  }
}

byte Protocol::batteryLevel() {
  // add power drain to get worst-case battery voltage
  radio->startListening();
  
  // read battery voltage as a percentage between 1.5V -> 3.3V
  analogReference(INTERNAL1V5);
  delay(10);
  uint16_t batt = analogRead(A10 + 1); // A11 = (Vcc - Vss) / 2

  // save battery
  radio->stopListening();
  analogReference(DEFAULT);
  
  PRINT("Vcc: ");
  PRINTLN(batt);
  
  return map(batt, 0, 1023, 0, 255);
}

void Protocol::uploadSettings(byte* inbuf, int len) {
  byte batteryVal = batteryLevel();

  // upload metadata
  uint8_t j = 0;
  packet[j++] = PKT_DATA;
  packet[j++] = batteryVal;
  packet[j++] = metaData.pingTxRange;
  packet[j++] = metaData.pingChannel;
  packet[j++] = metaData.readerChannel;
  packet[j++] = metaData.downloadChannel;
  packet[j++] = metaData.pingPeriodMs & 0xFF;
  packet[j++] = metaData.pingPeriodMs >> 8;
  packet[j++] = metaData.listenPeriodSecs & 0xFF;
  packet[j++] = metaData.listenPeriodSecs >> 8;
  packet[j++] = metaData.readerPeriodSecs & 0xFF;
  packet[j++] = metaData.readerPeriodSecs >> 8;
  packet[j++] = metaData.sessionTimeoutSecs & 0xFF;
  packet[j++] = metaData.sessionTimeoutSecs >> 8;
  packetLen = j;
  radioWrite();

  sendAck();
  switchToPingChannel();
}

void Protocol::writeSetting(byte* inbuf, int len) {
  switch (inbuf[3]) {
    case SET_PING_TX_RANGE:
      PRINT("Ping Tx Range = ");
      metaData.pingTxRange = inbuf[4];
      PRINTLN(metaData.pingTxRange);
      break;
    case SET_PING_CHANNEL:
      PRINT("Ping Channel = ");
      metaData.pingChannel = inbuf[4];
      PRINTLN(metaData.pingChannel);
      break;
    case SET_READER_CHANNEL:
      PRINT("Reader Channel = ");
      metaData.readerChannel = inbuf[4];
      PRINTLN(metaData.readerChannel);
      break;
    case SET_DOWNLOAD_CHANNEL:
      PRINT("Download Channel = ");
      metaData.downloadChannel = inbuf[4];
      PRINTLN(metaData.downloadChannel);
      break;
    case SET_PING_PERIOD_MS:
      PRINT("Ping period ms = ");
      metaData.pingPeriodMs = (inbuf[4] << 8) + inbuf[5];
      PRINTLN(metaData.pingPeriodMs);
      break;
    case SET_LISTEN_PERIOD_S:
      PRINT("Listen period sec = ");
      metaData.listenPeriodSecs = (inbuf[4] << 8) + inbuf[5];
      PRINTLN(metaData.listenPeriodSecs);
      break;
    case SET_READER_PERIOD_S:
      PRINT("Reader period sec = ");
      metaData.readerPeriodSecs = (inbuf[4] << 8) + inbuf[5];
      PRINTLN(metaData.readerPeriodSecs);
      break;
    case SET_SESSION_TIMEOUT_S:
      PRINT("Session timeout sec = ");
      metaData.sessionTimeoutSecs = (inbuf[4] << 8) + inbuf[5];
      PRINTLN(metaData.sessionTimeoutSecs);
      break;
    case SET_DEFAULTS:
      PRINT("Resetting metadata to defaults");
      resetMetaData();
    default:
      PRINTLN("Unknown!");
      break;
  }

  writeMetaData();
  sendAck();
}

void Protocol::uploadData() {
  // upload the data stored in EEPROM (saved sessions)
  unsigned int startAddr = EEPROM_DATA_START + sizeof(MetaData);
  for (i = startAddr; i < EEPROM_SIZE; i += sizeof(TagData)) {
    readTagData(&d, i);
    if (d.tagid > 0 && d.check == CHECK_BYTE) {
      uploadTagData(&d);
    } else {
      break;
    }
  }

  // upload data in RAM (current sessions)
  for (i=0; i < MAX_RAM_SESSIONS; i++) {
    if (sessions[i].tagid > 0) {
      sessionToTagData(&sessions[i], &d);
      uploadTagData(&d);
    }
  }

  sendAck();
  PRINTLN("Upload complete");
  isStopped = true;

  switchToPingChannel();
}

void Protocol::sessionToTagData(SessionLookup *s, TagData *d) {
  d->tagid = s->tagid;
  d->firstSeenSeconds = (unsigned long) s->firstSeenSeconds;
  d->lastSeenSeconds = (unsigned long) s->lastSeenSeconds;
  d->check = CHECK_BYTE;
}

void Protocol::uploadTagData(TagData *d) {
  unsigned long firstSeenSeconds = d->firstSeenSeconds - sessionStartSecs; 
  unsigned long lastSeenSeconds = d->lastSeenSeconds - sessionStartSecs;
  unsigned long now = seconds() - sessionStartSecs;

  byte j =0;
  packet[j++] = PKT_DATA;
  packet[j++] = d->tagid >> 8;
  packet[j++] = d->tagid;
  packet[j++] = firstSeenSeconds >> 24;
  packet[j++] = firstSeenSeconds >> 16;
  packet[j++] = firstSeenSeconds >> 8;
  packet[j++] = firstSeenSeconds;
  packet[j++] = lastSeenSeconds >> 24;
  packet[j++] = lastSeenSeconds >> 16;
  packet[j++] = lastSeenSeconds >> 8;
  packet[j++] = lastSeenSeconds;
  packet[j++] = now >> 24;
  packet[j++] = now >> 16;
  packet[j++] = now >> 8;
  packet[j++] = now;
  packetLen = j;
  radioWrite();
  delay(2);  
}

void Protocol::resetData() {
  // reset our data
  unsigned int startAddr = EEPROM_DATA_START + sizeof(MetaData);
  for (i = startAddr; i < EEPROM_SIZE; i += sizeof(TagData)) {
    // check if we have tag data
    readTagData(&d, i);
    if (d.check == CHECK_BYTE) {
      d.tagid = 0;
      d.check = 0xff; // undo check byte
      writeTagData(i, &d);
    }
  }  

  for (i=0; i < MAX_RAM_SESSIONS; i++) {
    sessions[i].tagid = 0;
  }

  PRINTLN("Data reset");
}

void Protocol::switchToDownloadChannel() {
  radio->setChannel(metaData.downloadChannel);
  radio->startListening();  
  radio->setAutoAck(true);
  radio->setPALevel(RF24_PA_MAX); // (0, -6, -12, -18 dBm)
  delay(1);
}

void Protocol::switchToPingChannel() {
  radio->setChannel(metaData.pingChannel);
  #ifdef AUTO_ACK
    radio->autoAck(true);
    radio->setAutoAckParams(10, 1000);
  #else
    radio->setAutoAck(false);
  #endif
  setTXPower();
  radio->stopListening();  
  delay(1);
}

void Protocol::switchToReaderChannel() {
  radio->setChannel(metaData.readerChannel);
  radio->setPALevel(READER_TX_POWER); // (0, -6, -12, -18 dBm)
  radio->startListening();  
  delay(1);
}

void Protocol::readMetaData() {
  // read data from eeprom
  unsigned int addr = EEPROM_DATA_START;
  for (int i = 0; i < sizeof(MetaData); i++) {
    byte b = eeprom->read(addr + i);
    *((byte *)&metaData + i) = b;
  }  
}

void Protocol::writeMetaData() {
  unsigned int addr = EEPROM_DATA_START;
  byte* bytes = reinterpret_cast<byte*>(&metaData);
  for (int i = 0; i < sizeof(MetaData); i++) {
    byte b = bytes[i];
    byte d = eeprom->read(addr + i);
    if (b != d) {
      eeprom->write(addr + i, b);
    }
  }
}

void Protocol::setTXPower() {
  unsigned int txPower = 0;
  switch (metaData.pingTxRange & 0b01111111) {
    case 0: txPower = RF24_PA_MAX; break;
    case 1: txPower = RF24_PA_HIGH; break;
    case 2: txPower = RF24_PA_LOW; break;
    default: txPower = RF24_PA_MIN;
  }
  radio->setPALevel(txPower); // (0, -6, -12, -18 dBm)
  radio->powerUp();
}

void Protocol::sendAck() {
  byte j = 0;
  packet[j++] = CMD_ACK;
  packet[j++] = tagid >> 8;
  packet[j++] = tagid & 0xFF;
  packetLen = j;
  radioWrite();
}

// uses some battery, use sparingly
void Protocol::sendAckWithBatteryLevel() {
  byte batteryVal = batteryLevel();
  byte j = 0;
  packet[j++] = CMD_ACK;
  packet[j++] = tagid >> 8;
  packet[j++] = tagid & 0xFF;
  packet[j++] = batteryVal;
  packetLen = j;
  radioWrite();
}

unsigned long Protocol::seconds() {
  return millis() / 1000;
}

unsigned int Protocol::secondsElapsed(unsigned int start) {
  return ((unsigned int)(millis() / 1000)) - start;
}

void Protocol::resetMetaData() {
  // 0->3 = 0,-6,-12,-18 dBm, high bit = PING_STRONG
  if (PING_STRONG) {
    // set high bit to indicate "strong signal required"
    metaData.pingTxRange = PING_TX_POWER | 0b10000000;
  } else {
    // unset high bit to indicate "strong signal not required"
    metaData.pingTxRange = PING_TX_POWER & 0b01111111;
  }

  metaData.pingChannel = PING_CHANNEL;
  metaData.readerChannel = READER_CHANNEL;
  metaData.downloadChannel = DOWNLOAD_CHANNEL;
  metaData.pingPeriodMs = PING_PERIOD_MS;
  metaData.listenPeriodSecs = LISTEN_PERIOD_SECS;
  metaData.readerPeriodSecs = READER_PERIOD_SECS;
  metaData.sessionTimeoutSecs = SESSION_TIMEOUT_SECS;
  writeMetaData();
}

void Protocol::loadTest() {
  // trigger test 1
  if (millis() > 2000 & millis() < 3000) {
    // create max sessions in RAM
    metaData.sessionTimeoutSecs = 1;
    for (i = 0; i < MAX_RAM_SESSIONS; i++) {
      sessions[i].tagid = i+1;
      sessions[i].lastSeenSeconds = 0;
    }
    delay(1000);
  }

  // trigger test 2
  if (millis() > 6000 & millis() < 7000) {
    PRINTLN("Test2");

    // create max sessions in RAM again (prev should have expired)
    metaData.sessionTimeoutSecs = 5;
    for (i = 0; i < MAX_RAM_SESSIONS; i++) {
      sessions[i].tagid = i+1;
      sessions[i].lastSeenSeconds = 1;
    }

    delay(1000);
    uploadData();
  }
}

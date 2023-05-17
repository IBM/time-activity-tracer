/**
 * Copyright 2022 IBM Corp. All Rights Reserved.
 */

#ifndef _RFT_PROTOCOL_H
#define _RFT_PROTOCOL_H

#include <Energia.h>
#include <stdint.h>
#include "eeprom.h"
#include "global.h"
#include "RF24.h"

#define CHECK_BYTE    0x5A
#define TAGDATA_SIZE  sizeof(TagData)

// Max sessions data to store in RAM. Adjust so that after compilation, 
// memory usage is not more than 420 bytes out of 512 bytes
#define MAX_RAM_SESSIONS  16

struct TagData {
  unsigned int tagid; // remote tag id
  unsigned long firstSeenSeconds; // session start time
  unsigned long lastSeenSeconds; // last seen time
  byte check;
};

// Store session data in RAM. To mimize RAM usage (to store more sessions)
// using only 2 bytes to store time, i.e. max 18.2 hours per session.
struct SessionLookup {
  unsigned int tagid; // remote tag id
  unsigned int firstSeenSeconds; // session start time
  unsigned int lastSeenSeconds; // last seen time
};

class Protocol {
  public:
    static byte addr[];
    unsigned int tagid;
    MetaData metaData;
    Eeprom *eeprom;
    RF24 *radio;
    boolean isStopped;
    boolean noCommand;
    unsigned long lastReset;
    unsigned long sessionStartSecs;
    SessionLookup sessions[MAX_RAM_SESSIONS]; // store session lookup data in RAM

    // common variables
    unsigned long i; // loop counter
    TagData d; // tag data

    // packet buffer for radio
    byte packet[32];
    byte packetLen;

    Protocol();
    void begin(unsigned int tagId, RF24 *radio, Eeprom *eeprom);
    void process(byte* inbuf, int len);
    void tick();
    void clearBuffer();
    void resetData();
    void switchToPingChannel();
    void switchToReaderChannel();
    void switchToDownloadChannel();
    void readMetaData();
    void writeMetaData();
    void resetMetaData();
    void resetSessionData();
    void setTXPower();
    unsigned long seconds();
    void writeSetting(byte *inbuf, int len);
    byte batteryLevel();
    int radioRead();
    void radioWrite();

  private:
    unsigned int getRemoteTagId(byte* inbuf);
    SessionLookup* getTagData(unsigned int tagId);    
    void readTagData(TagData *tagData, unsigned long addr);    
    void sessionToTagData(SessionLookup *s, TagData *tagData);
    void writeTagData(unsigned long addr, TagData *tagData);    
    void relay(byte command);
    void sendAck();
    void sendAckWithBatteryLevel(); // uses some battery, use sparingly
    void uploadData();
    void uploadSettings(byte *inbuf, int len);
    void uploadTagData(TagData *d);
    void handlePing(byte *inbuf, int len);
    void handleDownload(byte *inbuf, int len);
    unsigned int secondsElapsed(unsigned int start);
    void loadTest();
};
#endif

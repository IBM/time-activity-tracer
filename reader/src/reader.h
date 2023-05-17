/**
 * Copyright 2022 IBM Corp. All Rights Reserved.
 */

#include <SPI.h>
#include "RF24.h"
#include "global.h"
#include "utilities.h"

//#define DEBUG

// Main entities
RF24 radio(P2_0, P2_1);  // P2.0=CE, P2.1=CSN, P2.2=IRQ

// multicast address
byte addr[] = MULTICAST_ADDR;

// state variables
unsigned int reader_id = 0;
byte inbuf[32];
byte inbufLen = 0;

boolean autoDownload = false;
unsigned long ledBlinkPeriod = 1000;
unsigned long ledTime = 0;
boolean ledState = false;

const byte ping_packet[] = { CMD_PING, reader_id >> 8, reader_id & 0xFF };
unsigned long lastPing = 0;

const char* ranges[] = {
    "20 m", "17 m", "12 m", "6 m", "3 m", "60 cm", "40 cm", "20 cm"
};

unsigned int sendCommand(byte command);
unsigned int sendCommand(byte command, byte *data, int dataLen);
unsigned int waitForAnyTag();
boolean processDownloadData(unsigned int tagid) ;
unsigned int sendCommandForTag(byte command, unsigned int tagId);
void showSettingsMenu();
void printMenu();
void sendReaderPing();
void listenForTags();
void handleUserInput();
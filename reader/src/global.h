/**
 * Copyright 2022 IBM Corp. All Rights Reserved.
 */

#ifndef _RFT_GLOBAL_H
#define _RFT_GLOBAL_H

#define LED RED_LED

// enable debug output
//#define DEBUG
//#define TEST_BED    // enable functionality for testing on test bed

/* 
 *  Tweak the ping/listen timings here. All in milliseconds.
 *  Be mindful of battery life trade-off 
 */

#define MULTICAST_ADDR    "abcde"
#define NRF_SPEED         1000000
//#define AUTO_ACK

// ====================================================================
// The following are defaults to be written to EEPROM. They can be
// reconfigured later over-the-air using CMD_SETTINGS
// ====================================================================
#define PING_CHANNEL      100
#define READER_CHANNEL    110
#define DOWNLOAD_CHANNEL  120

// too often -> battery drain, too seldom -> missed pings
#define PING_PERIOD_MS     350

// listen too often -> battery drain, too seldom -> missed pings
#define LISTEN_PERIOD_SECS   10

// listen too often -> battery drain, too seldom -> missed readers
#define READER_PERIOD_SECS   5

// how long after which a tag session is timed out (in seconds)
// at least LISTEN_DURATION * 3 (in case we missed one ping)
#define SESSION_TIMEOUT_SECS    120
#define AUTO_STOP_SECONDS       43200 // auto-stop after 12 hours 

// PING radio parameters
#define PING_TX_POWER     2      // (0, 1, 2, 3) -> (0, -6, -12, -18 dBm)
                                // -> (red, green, yellow, blue)
#define PING_STRONG       false  // strong signal needed for proximity?

// ====================================================================

// Locator tag settings
#define MAX_TAGS          65535  // tag ID is currently 16-bit value
#define MAX_TAG_ID        32767  // max tag id, all the rest are locators

// how long to listen for a nearby reader -> battery drain if long
#define READER_DURATION   20

// start address of tag data structures in EEPROM
#define EEPROM_DATA_START 0x04
#define EEPROM_SIZE       0xFFFF  // last address in EEPROM
#define CHECK_BYTE1       0xBE
#define CHECK_BYTE2       0xEF

// Commands are bitmasked onto tag ID, since tag id's <= 63
#define CMD_PING          0xA1  // a ping packet
#define CMD_ACK           0xA2  // an acknowledge packet
#define CMD_START         0xA3  // start pinging 
#define CMD_STOP          0xA4  // stop pinging
#define CMD_DOWNLOAD      0xA5  // upload the data to a reader
#define CMD_RESET         0xA6  // reset tag data (data is lost)
#define CMD_DIAGNOSTIC    0xA7  // for testing tag hardware
#define CMD_DL_AND_RESET  0xA8  // download and then reset data
#define PKT_DATA          0xA9  // this is a data packet
#define CMD_WRITE_SETTING 0xAA  // configure device EEPROM metadata
#define CMD_READ_SETTINGS 0xAB

#define SET_PING_TX_RANGE       0
#define SET_PING_CHANNEL        1
#define SET_READER_CHANNEL      2
#define SET_DOWNLOAD_CHANNEL    3
#define SET_PING_PERIOD_MS      4
#define SET_LISTEN_PERIOD_S     5
#define SET_READER_PERIOD_S     6
#define SET_SESSION_TIMEOUT_S   7
#define SET_DEFAULTS            8 // reset settings to default

// *******************  Utility macros
#define IS_LOCATOR(tagid) (tagid > MAX_TAG_ID) 
// Time macros to also handle roll-over of millis() after 49 days
#define TIME_INTERVAL2(var1, var2) (unsigned long)((unsigned long)(var1) - (unsigned long)(var2))
#define TIME_INTERVAL(var1) (unsigned long)(millis() - (unsigned long)(var1))

#ifdef DEBUG
    #define PRINT Serial.print
    #define PRINTLN Serial.println
#else
    #define PRINT //Serial.print
    #define PRINTLN //Serial.println
#endif

// ****** Common Structs

struct MetaData {
  byte pingTxRange; // 0->3 = 0,-6,-12,-18 dBm, high bit = PING_STRONG
  byte pingChannel;
  byte readerChannel;
  byte downloadChannel;
  
  unsigned int pingPeriodMs;
  unsigned int listenPeriodSecs;
  unsigned int readerPeriodSecs;
  unsigned int sessionTimeoutSecs;
};

#endif

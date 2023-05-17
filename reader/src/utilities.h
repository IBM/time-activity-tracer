/**
 * Copyright 2022 IBM Corp. All Rights Reserved.
 */

#include "Arduino.h"

unsigned long toULong(byte a, byte b, byte c, byte d) {
  unsigned long ret = (unsigned long)(a) << 24;
  ret = ret | (unsigned long)(b) << 16;
  ret = ret | (unsigned int)(c) << 8;
  ret = ret | d;
  return ret;
}

// extract the tagid from the ping packet sent by remote
unsigned int getRemoteTagId(byte* inbuf) {
  return (inbuf[1] << 8) + inbuf[2];
}

unsigned int readInput() {
    unsigned long timer = millis();
    unsigned long timer2 = millis();
    boolean on = false;
    char intBuffer[12];
    String intData = "";
    int delimiter = (int) '\n';
    int ch;
    while ((ch = Serial.read()) != delimiter) {
        if (millis() - timer2 > 5000) {
          // timeout
          return 0;
        }

        if (millis() - timer > 100) {
          timer = millis();
          digitalWrite(LED, on);
          on = ! on;
        }
        
        if (ch == -1) {
            // Handle error
        } else if (ch == delimiter) {
            break;
        } else {
            intData += (char) ch;
        }
    }

    // Copy read data into a char array for use by atoi
    // Include room for the null terminator
    int intLength = intData.length() + 1;
    intData.toCharArray(intBuffer, intLength);

    // Convert ASCII-encoded integer to an int
    unsigned int tagid = atoi(intBuffer);
    return tagid;
}
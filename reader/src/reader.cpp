/**
 * Copyright 2022 IBM Corp. All Rights Reserved.
 */

#include "reader.h"

void setup() {
  Serial.begin(9600);
  Serial.println("Starting RF READER v8.0");

  // setup SPI for nrf module
  SPI.begin();
  SPI.setDataMode(SPI_MODE0);
  SPI.setBitOrder(MSBFIRST);
  SPI.setClockDivider(SPI_CLOCK_DIV2);

  // setup led and keep it on for a second to indicate power-up
  pinMode(LED, OUTPUT );
  digitalWrite(LED, HIGH);
  sleep(1000);
  digitalWrite(LED, LOW);

  // set up NRF radio
  radio.begin();  // speed, channel
  radio.setChannel(READER_CHANNEL);
  radio.setDataRate(RF24_1MBPS);
  radio.setPALevel(RF24_PA_MAX); // (0, -6, -12, -18 dBm)
  radio.setAutoAck(false);
  radio.setCRCLength(RF24_CRC_8);
  radio.openWritingPipe(addr);
  radio.openReadingPipe(1, addr);
  //radio.enableDynamicPayloads();
  radio.startListening();
  delay(2);

  printMenu();
}

void loop() {
  if (millis() - ledTime > ledBlinkPeriod) {
    ledTime = millis();
    ledState = !ledState;
    digitalWrite(LED, ledState);    
  }

  if (autoDownload) {
    sendReaderPing(); 
    listenForTags();
  }

  handleUserInput();
}

byte radioRead() {
  if (radio.available()) {
    radio.read(inbuf, radio.getPayloadSize());

    #ifdef DEBUG
      Serial.print("< ");
      for (int i=0; i < 10; i++) {
        Serial.print(" ");
        Serial.print(inbuf[i], HEX);
      }
      Serial.println("");
    #endif

    return radio.getPayloadSize();
  }

  return 0;
}

void radioWrite(const byte *buf, uint8_t len) {
  radio.stopListening();
  radio.write(buf, len);
  radio.startListening();
}

unsigned int sendCommand(byte command) {
  sendCommand(command, 0, 0);
}

unsigned int sendCommand(byte command, byte *data, int dataLen) {
  unsigned int tagid = 0;
  if ((tagid = waitForAnyTag()) == 0) {
    Serial.println("No tags found.");
    return 0;
  }

  radio.setChannel(DOWNLOAD_CHANNEL);
  delay(10);
  
  inbuf[0] = command;
  inbuf[1] = tagid >> 8;
  inbuf[2] = tagid & 0xFF;

  if (dataLen > 0) {
    for (int i=3; i < (dataLen+3); i++) {
      if (i < sizeof(inbuf)) {
        inbuf[i] = data[i - 3];
      }
    }
  }

  radio.flush_rx();
  radio.setAutoAck(true);
  radioWrite(inbuf, dataLen + 3);

  // wait for data
  unsigned long timer2 = millis();
  while (millis() - timer2 < 1000) {
    if (radio.available()) break;  
  }

  if (!radio.available()) {
    Serial.println("Tag timed out.");
    return 0;
  }

  return tagid;
}

byte getBatteryPercentage(byte batteryLevel) {
  return map(batteryLevel, 0, 255, 0, 100);  
}

boolean downloadTagData(unsigned int remoteTagId, boolean stopAfter, boolean resetAfter) {
  if (remoteTagId == 0) {
    remoteTagId = sendCommand(CMD_DL_AND_RESET);
  } else {
    sendCommandForTag(CMD_DL_AND_RESET, remoteTagId);
  }

  if (remoteTagId > 0) {
    return processDownloadData(remoteTagId);
  }

  radio.setChannel(READER_CHANNEL);
  return false;
}

boolean processDownloadData(unsigned int tagid) {
  // wait for data
  unsigned long timer2 = millis();
  while (millis() - timer2 < 1000) {
    if (radio.available()) break;  
  }

  boolean ret = false;
  if (radio.available()) {
    byte count = 0;
    unsigned long timer1 = millis();
    while (!ret && millis() - timer1 < 250) {
      if ((count = radioRead()) > 0) {
        unsigned int remoteTagId = getRemoteTagId(inbuf);
        if (inbuf[0] == CMD_ACK) {
          // done
          Serial.println("Download complete");
          ret = true;
          break;
        } else if (inbuf[0] == PKT_DATA) {
          timer1 = millis(); // reset timeout
          // print out data for collation
          Serial.print("|"); // indicates data line - do not use elsewhere
          Serial.print(tagid, DEC);
          Serial.print("|");
          Serial.print(remoteTagId, DEC);
          Serial.print("|");
          Serial.print(toULong(inbuf[3], inbuf[4], inbuf[5], inbuf[6]), DEC);
          Serial.print("|");
          Serial.print(toULong(inbuf[7], inbuf[8], inbuf[9], inbuf[10]), DEC);
          Serial.print("|");
          Serial.println(toULong(inbuf[11], inbuf[12], inbuf[13], inbuf[14]), DEC);
        } else {
          Serial.print("Unknown command ");
          Serial.println(inbuf[0], HEX);
        }
      }
    }
  }

  return ret;
}

void sendReaderPing() {
  if (millis() - lastPing >= (READER_DURATION / 2)) {
    lastPing = millis();
    radio.setAutoAck(false);
    radioWrite(ping_packet, sizeof(ping_packet));
  }
}

void listenForTags() {
  if ((inbufLen = radioRead()) > 2) {
    unsigned int remoteTagId = getRemoteTagId(inbuf);
    boolean strong = radio.testRPD();

    if (inbuf[0] == CMD_PING && strong) {
      radio.setChannel(DOWNLOAD_CHANNEL);
      delay(5);

      // multiple pings are sent, so clear the rx buffer
      while (radioRead() > 0);
      
      if (downloadTagData(remoteTagId, true, true)) {
        Serial.println("Done");
      }

      radio.setChannel(READER_CHANNEL);
    }
    
    // completely read the buffer
    while (radioRead() > 0);
  }      
}

void rangeTester() {
  Serial.println("Range tester - showing devices in range");
  radio.setChannel(PING_CHANNEL);
  radio.setAutoAck(false);
  radio.startListening();
  delay(2);

  while (Serial.available() == 0) {
    digitalWrite(LED, LOW); 
    if ((inbufLen = radioRead()) > 2) {
      unsigned int remoteTagId = getRemoteTagId(inbuf);
      boolean strong = radio.testRPD();

      if (inbuf[0] == CMD_PING) {
        if (inbuf[3] == 0 || (inbuf[3] == 1 && strong)) {
          digitalWrite(LED, HIGH);
        }

        // print out the ping for range testing
        Serial.print(inbuf[3]); // pingStrong flag from tag
        if (strong) {
          Serial.print(" [");
          Serial.print(remoteTagId, DEC);
          Serial.println("]");
        } else {
          Serial.print(" (");
          Serial.print(remoteTagId, DEC);
          Serial.println(")");
        }
      }
    }
  }

  // clear serial buffer
  while (Serial.available() > 0) Serial.read();
  radio.setChannel(READER_CHANNEL);
  Serial.println("Exiting range tester.");
}

void broadcastCommand(byte command, char *description) {
  // send out reset command
  Serial.print("> Sending ");
  Serial.print(description);

  radio.stopListening();
  delay(1);

  for (int i=0; i < 5; i++) {
    radioWrite(&command, sizeof(command));
    delay(100);
    Serial.print(".");
  }
  
  radio.startListening();
  Serial.println(" done.");
}

unsigned int waitForAnyTag() {
  Serial.println("Waiting for tag...");
  radio.setChannel(READER_CHANNEL);
  radio.startListening();
  delay(2);
  while (radioRead() > 0); // clear read buffer

  // wait for our target tag to get in range
  unsigned int remoteTagId = 0;
  boolean strong = false;
  unsigned long timer = millis();
  while (remoteTagId == 0 && millis() - timer <= 6000) {
    sendReaderPing();
    if ((inbufLen = radioRead()) > 2) {
      strong = radio.testRPD();
      if (strong) {
        remoteTagId = getRemoteTagId(inbuf);
      }
    }
  }

  if (remoteTagId > 0) {
    Serial.print("Found tag ");
    Serial.println(remoteTagId);
  }

  return remoteTagId;
}

unsigned int sendCommandForTag(byte command, unsigned int tagId) {
  byte packet[3];
  packet[0] = command;
  packet[1] = tagId >> 8;
  packet[2] = tagId & 0xFF;

  radio.setAutoAck(true);
  radioWrite(packet, sizeof(packet));
}


boolean downloadTagSettings() {
  unsigned int tagid = 0;

  if ((tagid = sendCommand(CMD_READ_SETTINGS)) != 0) {
    unsigned long timer2 = millis();
    while (!radio.available() && millis() - timer2 < 1000) {
      // wait for data
      delay(10);
    }

    if (radio.available()) {
      inbufLen = radioRead();
      MetaData metaData = MetaData();

      // inbuf has PKT_DATA followed by battery level byte, then
      // bytes of data from MetaData struct
      if (inbufLen > 0 && inbuf[0] == PKT_DATA) {
        for (int i = 2; i < sizeof(MetaData) + 2; i++) {
          byte b = inbuf[i];
          *((byte *)&metaData + i - 2) = b;
        }

        // print out the current configuration parameters
        Serial.print("==== Configuration TAG ");
        Serial.print(tagid);
        Serial.println(" ====");
        Serial.print("Range: ");
        if ((metaData.pingTxRange & 0b10000000) != 0) {
          Serial.println(ranges[(metaData.pingTxRange & 0b01111111) + 4]);
        } else {
          Serial.println(ranges[metaData.pingTxRange]);
        }
        Serial.print("pingChannel: ");
        Serial.println(metaData.pingChannel);
        Serial.print("readerChannel: ");
        Serial.println(metaData.readerChannel);
        Serial.print("downloadChannel: ");
        Serial.println(metaData.downloadChannel);
        Serial.print("pingPeriodMs: ");
        Serial.println(metaData.pingPeriodMs);
        Serial.print("listenPeriodSecs: ");
        Serial.println(metaData.listenPeriodSecs);
        Serial.print("readerPeriodSecs: ");
        Serial.println(metaData.readerPeriodSecs);
        Serial.print("sessionTimeoutSecs: ");
        Serial.println(metaData.sessionTimeoutSecs);

        byte batteryLevel = inbuf[1]; // second byte is battery level
        Serial.print("Battery level: ");
        Serial.print(getBatteryPercentage(batteryLevel));
        Serial.println("%");
        Serial.println("=======================");
      }
    }
  }

  radio.setChannel(READER_CHANNEL);
}

void channelScan() {
  // TODO - implement this from:
  //  https://github.com/spirilis/Enrf24/blob/master/examples/Enrf24_ChannelScan/Enrf24_ChannelScan.ino
  Serial.println("Sorry, not yet implemented");
}

void runDiagnostics() {
  // test data transmission of tag
  broadcastCommand(CMD_DIAGNOSTIC, "DIAGNOSTIC");
  Serial.print("Waiting for 100 pings ");
  radio.startListening();
  delay(2);

  unsigned int tagid = 0;
  byte count = 0;
  unsigned long timer = millis();
  while (millis() - timer < 10000 && count < 100) {
    if (radio.available()) {
      timer = millis();
      Serial.print("|");
      radioRead();
      if (inbuf[0] == CMD_PING) {
        unsigned int remoteTagId = getRemoteTagId(inbuf);
        count++;
        if (radio.testRPD()) Serial.print("+");
        else Serial.print(".");
        Serial.print(remoteTagId, DEC);
        tagid = remoteTagId;
      }
    }
  }

  Serial.println("!");
  Serial.println("Pings received: ");
  Serial.println(count, DEC);
  
  broadcastCommand(CMD_STOP, "STOP");
  Serial.println("Trying to download data");

  timer = millis();
  while (millis() - timer < 10000) {
    if (downloadTagData(tagid, false, false)) break;
    delay(20);
  }
  Serial.println("Done.");
}

void printMenu() {
  Serial.print("RF READER, channel ");
  Serial.println(radio.getChannel(), DEC);
  
  Serial.println("+ - Enable auto-download");
  Serial.println("- - Disable auto-download");
  Serial.println("1 - READ tag settings");
  Serial.println("2 - START tag");
  Serial.println("3 - DOWNLOAD tag data");
  Serial.println("4 - STOP tag");
  Serial.println("5 - NOISE scan");
  Serial.println("6 - WRITE tag settings");
  Serial.println("7 - RANGE tester");
}

void handleUserInput() {
  if (Serial.available() > 0) {
    byte b = Serial.read();

    // clear read buffer
    while (Serial.available() > 0) Serial.read();
    
    if (b == '+') {
      autoDownload = true;
      ledBlinkPeriod = 250;
      Serial.println("Auto-download enabled");
    } else if (b == '-') {
      autoDownload = false;
      ledBlinkPeriod = 1000;
      Serial.println("Auto-download disabled");
    } else if (b == '1') {
      downloadTagSettings();
    } else if (b == '2') {
      unsigned int tag_id = sendCommand(CMD_START);
      delay(20); // give time for response     
      inbufLen = radioRead();
      if (tag_id > 0 && inbufLen > 0 && inbuf[0] == CMD_ACK) {
        Serial.print("Tag started: ");
        Serial.print(tag_id, DEC);

        if (inbufLen > 3) {
          // battery level in fourth byte
          Serial.print(", ");
          Serial.println(getBatteryPercentage(inbuf[3]), DEC); // battery level
        }
      }
    } else if (b == '3') {
      downloadTagData(0, true, true);
    } else if (b == '4') {
      unsigned int tag_id = sendCommand(CMD_STOP);      
      delay(20); // give time for response     
      if (tag_id > 0 && radioRead() > 0 && inbuf[0] == CMD_ACK) {
        Serial.print("Tag stopped: ");
        Serial.println(tag_id, DEC);
      }
    } else if (b == '5') {
      channelScan();
    } else if (b == '6') {
      showSettingsMenu();
    } else if (b == '7') {
      rangeTester();
    } else if (b == 'x' || b == 'X') {
      // ignore this, it is the "escape" key
    } else {
      Serial.println("Unknown command.");
      printMenu();
    }

    // absorb any additional characters
    while (Serial.available() > 0) Serial.read();
  }
}

void showSettingsMenu() {
  Serial.println("Select setting:");
  Serial.println("1 - Transmit range for tag");
  Serial.println("2 - Set ping period");
  Serial.println("3 - Set listen period");
  Serial.println("4 - Set session timeout");
  Serial.println("5 - RESET settings to tag defaults");

  while (!Serial.available());
  byte b = Serial.read() - '0';
  if (b == 1) {
    Serial.println("Select transmit range: ");
    for (int i=0; i < 8; i++) {
      Serial.print(i);
      Serial.print(" - ");
      Serial.println(ranges[i]);
    }

    while (!Serial.available());
    b = Serial.read() - '0';
    Serial.println(b);

    if (b < 0 || b > 7) {
      Serial.print("\nInvalid level, aborting ");
      Serial.println(b);
    } else {
      // Range is made up of TX power (0 -> 3), and whether or not the
      // received signal must be STRONG (high-bit set to 1) or not
      int range = (b > 3 ? (b - 4) | 0b10000000 : b);
      byte data[] = { SET_PING_TX_RANGE, range };
      sendCommand(CMD_WRITE_SETTING, data, sizeof(data));
    }
  } else if (b == 2) {
    Serial.print("Listen period (milli seconds): ");
    int timeout = readInput();
    Serial.println(timeout);
    if (timeout > 0) {
      byte data[] = { SET_PING_PERIOD_MS, timeout >> 8, timeout & 0xff };
      sendCommand(CMD_WRITE_SETTING, data, sizeof(data));
    }
  } else if (b == 2) {
    Serial.print("Listen period (seconds): ");
    int timeout = readInput();
    Serial.println(timeout);
    if (timeout > 0) {
      byte data[] = { SET_LISTEN_PERIOD_S, timeout >> 8, timeout & 0xff };
      sendCommand(CMD_WRITE_SETTING, data, sizeof(data));
    }
  } else if (b == 3) {
    Serial.print("Listen period (seconds): ");
    int timeout = readInput();
    Serial.println(timeout);
    if (timeout > 0) {
      byte data[] = { SET_LISTEN_PERIOD_S, timeout >> 8, timeout & 0xff };
      sendCommand(CMD_WRITE_SETTING, data, sizeof(data));
    }
  } else if (b == 4) {
    Serial.print("Session timeout (seconds): ");
    int timeout = readInput();
    Serial.println(timeout);
    if (timeout > 0) {
      byte data[] = { SET_SESSION_TIMEOUT_S, timeout >> 8, timeout & 0xff };
      sendCommand(CMD_WRITE_SETTING, data, sizeof(data));
    }
  } else if (b == 5) {
    byte data[] = { SET_DEFAULTS };
    sendCommand(CMD_WRITE_SETTING, data, sizeof(data));
  } else {
    Serial.println("Function not implemented yet, sorry.");
    return;
  }

  inbufLen = radioRead();
  if (inbufLen > 0 && inbuf[0] == CMD_ACK) {
    Serial.println("Done.");
  } else {
    Serial.println("Tag timed out.");
  }
}


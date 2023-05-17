# Time-activity Tracer

## Introduction

Time-Activity Tracer is a system of devices that can be used for the purpose of automating ["time and motion" studies](https://en.wikipedia.org/wiki/Time_and_motion_study) electronically. A typical use case is to record the time spent by patients in a clinic at the various rooms, and the duration of a "contact" (close proximity) of the patient with other patients and health care workers. 

The system consists of tags (wearable battery-powered devices) that the participants wear during the study, locators (small battery-powered devices) placed inside rooms, and one or more reader devices (a hardware attachment for laptops or phones) used by the study investigators.

## Method of operation

The tags and locators send out periodic radio beacons (pings) and listen for pings from other tags or locators. When a ping is received, the tag keeps track of when last this particular device was seen, and for how long and stores this in internal memory. When a reader device is near the tag, the tag will listen for commands, such as "start", "stop", or "download". When the reader issues the "download" command, the tag will send the contents of it's memory to the reader, which then transfers the data via it's UART serial port to an attached laptop or phone. A program will be needed on the attached device to read the serial port and copy the contents (lines beginning with the | character) into a file, such as a CSV file.

## Hardware

The tags, locators, and reader devices all use the same hardware. The [schematic](docs/hardware_schematic.pdf) shows the details of this hardware.It uses the MSP430G2553 microcontroller, the NRF24L01+ radio module for communication, an EEPROM for data storage, and an LED. It requires the use of the [Texas Instruments LaunchPad](https://www.ti.com/tool/MSP-EXP430G2ET) board for programming and communication via serial port.

## Getting Started

- You will need to create at least 3 hardware devices, 2 as tags and one as a reader. They are only differentiated by the firmware. 
- Install PlatformIO: `pip install --user platformio`
- Tags and locators should be programmed with the firmware in the [tag_and_locator](tag_and_locator) folder, and the reader with the firmware in the [reader](reader) folder. 
- Plugging in the LaunchPad board hooked up to the device, and then running `platformio run -t upload` in the appropriate folder should compile and upload the firmware. Use `platformio run -t monitor` to interact with the reader device.
- The reader device provides a textual menu on the serial port that you can interact with. The menu can be used to start/stop/download from a tag or locator that is placed very close to the reader antenna.
- Tags and locators use the same firmware. When the device is programmed, it will request a unique tag number over the serial port. You can simply type in a unique number for each device. A tag number greater than 32757 indicates a locator, the rest will be normal tags. Locators work similarly to tags, but don't store session data and hence have battery saving that allows them to run off 2x AA battery for over a year.


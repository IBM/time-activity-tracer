#include <Arduino.h>
#include <stdint.h>
#include <SPI.h>
#include "eeprom.h"

/* Constructor */
Eeprom::Eeprom() {
  _addrwidth = 16 / 8;
}

void Eeprom::begin() {
  pinMode( EEPROM_CS, OUTPUT);
  digitalWrite(EEPROM_CS, HIGH);  // idle
}

// read status register
byte Eeprom::readStatus()
{
  byte ret;

  digitalWrite(EEPROM_CS, LOW);
  SPI.transfer(SPIEEP_RDSR);
  ret = SPI.transfer(0x0);
  digitalWrite(EEPROM_CS, HIGH);

  return ret;
}

// write the address part of our read/write
void Eeprom::_write_address(uint32_t p) {
  int8_t i;

  for (i = _addrwidth - 1; i >= 0; i--)
    SPI.transfer( (byte)(p >> (8 * i)) & 0xFF );
}

// read a byte
byte Eeprom::read(uint32_t p) {
  byte ret;

  digitalWrite(EEPROM_CS, LOW);
  SPI.transfer(SPIEEP_READ);
  _write_address(p);
  ret = SPI.transfer(0x0);
  digitalWrite(EEPROM_CS, HIGH);

  return ret;
}

// enable write
void Eeprom::wren() {
  digitalWrite(EEPROM_CS, LOW);
  SPI.transfer(SPIEEP_WREN);
  digitalWrite(EEPROM_CS, HIGH);
}

// disable write
void Eeprom::wrdi() {
  digitalWrite(EEPROM_CS, LOW);
  SPI.transfer(SPIEEP_WRDI);
  digitalWrite(EEPROM_CS, HIGH);
}

// check if write enabled
boolean Eeprom::is_wren() {
  byte ret;

  ret = readStatus();
  if (((ret >> SPIEEP_STATUS_WEL) & 0x1) == 0x1)
    return true;
  return false;
}

// write a byte
boolean Eeprom::write(uint32_t p, byte b) {

  wren();
  if (!is_wren())
    return false;  // Couldn't enable WREN for some reason?
  digitalWrite(EEPROM_CS, LOW);
  SPI.transfer(SPIEEP_WRITE);
  _write_address(p);
  SPI.transfer(b);
  digitalWrite(EEPROM_CS, HIGH);

  return _write_validation();
}

boolean Eeprom::_write_validation() {
  long m = millis();
  byte ret;

  // Wait until the Write-In-Progress status register has cleared
  // Or 20ms (timeout) has passed, whichever happens first.  No write operations
  // should take more than 10ms or so.
  do {
    delayMicroseconds(200);
    ret = readStatus();
  } while (((ret >> SPIEEP_STATUS_WIP) & 0x01) == 0x01 && (millis() - m) < 20);

  // Check if Write-Enable has cleared to validate whether this command succeeded.
  return !is_wren();
}

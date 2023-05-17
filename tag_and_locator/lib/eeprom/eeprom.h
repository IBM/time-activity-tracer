#ifndef _RFT_EEPROM_H
#define _RFT_EEPROM_H

#define EEPROM_CS P2_5

//SPI EEPROM Instruction Set
#define SPIEEP_READ 0x03
#define SPIEEP_WRITE 0x02
#define SPIEEP_WREN 0x06
#define SPIEEP_WRDI 0x04
#define SPIEEP_RDSR 0x05
#define SPIEEP_WRSR 0x01
#define SPIEEP_PE 0x42
#define SPIEEP_SE 0xD8
#define SPIEEP_CE 0xC7
#define SPIEEP_RDID 0xAB
#define SPIEEP_DPD 0xB9

//SPI EEPROM Status Register Bits
#define SPIEEP_STATUS_WPEN 7
#define SPIEEP_STATUS_BP1 3
#define SPIEEP_STATUS_BP0 2
#define SPIEEP_STATUS_WEL 1
#define SPIEEP_STATUS_WIP 0

class Eeprom {
  public:
    Eeprom();

    // setup eeprom
    void begin();

    // read status register
    byte readStatus();

    // write the address part of our read/write
    void _write_address(uint32_t p);

    // read a byte
    byte read(uint32_t p);
    
    // enable write
    void wren();
    
    // disable write
    void wrdi();
    
    // check if write enabled
    boolean is_wren();
    
    // write a byte
    boolean write(uint32_t p, byte b);

  private:
    int _addrwidth;
    boolean _write_validation();
    
};

#endif

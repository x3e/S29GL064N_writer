#include <Arduino.h>

#include "SdFat.h"

// Connection to three daisy-chained SNx4HC595 shift register to change the address of the flash chip
const uint8_t PIN_SREG_SER = 45;
const uint8_t PIN_SREG_RCLK = 42;
const uint8_t PIN_SREG_SCLK = 41;

// SD card to read / write the ROM to / from.
const uint8_t PIN_SD_SCK = 12;
const uint8_t PIN_SD_MISO = 13;
const uint8_t PIN_SD_MOSI = 11;
const uint8_t PIN_SD_SS = 10;

// Swapped data pins (connected to S29GL064N), to account for the weird bit-order in the rom
const uint8_t PIN_DATA_SWAPPED[] = {
  33, 34, 35, 36, 37, 2, 3, 40, 8, 7, 6, 5, 4, 39, 38, 1
};

// Normal data pin order for S29GL064N
const uint8_t PIN_DATA_NOSWAP[] = {
  40, 39, 38, 37, 36, 35, 34, 33, 1, 2, 3, 4, 5, 6, 7, 8
};

// Other S29GL064N pins
const uint8_t PIN_FLASH_OE = 20;
const uint8_t PIN_FLASH_WE = 19;
const uint8_t PIN_FLASH_CE = 18;

SPIClass spi {FSPI};
SdFat32 sd;

void flashDataPinMode(uint8_t mode) {
  if (mode == OUTPUT)
    digitalWrite(PIN_FLASH_OE, HIGH);
  for (const uint8_t pin : PIN_DATA_SWAPPED) {
    pinMode(pin, mode);
  }
}

void setAddr(uint32_t addr) {
  digitalWrite(PIN_SREG_SCLK, LOW);
  digitalWrite(PIN_SREG_SER, LOW);
  digitalWrite(PIN_SREG_RCLK, LOW);
  for (int i=23; i>=0; i--) {
    bool dataBit = addr>>i & 1;
    digitalWrite(PIN_SREG_SER, dataBit);
    digitalWrite(PIN_SREG_SCLK, HIGH);
    digitalWrite(PIN_SREG_SCLK, LOW);
  }
  digitalWrite(PIN_SREG_RCLK, HIGH);
  digitalWrite(PIN_SREG_RCLK, LOW);
}

uint16_t readData() {
  uint16_t result = 0;
  for (const uint8_t pin : PIN_DATA_SWAPPED) {
    result <<= 1;
    bool dataBit = digitalRead(pin);
    if (dataBit) result |= 1;
  }
  return result;
}

uint8_t readData(bool highByte) {
  uint16_t result = 0;
  uint8_t start = highByte ? 8 : 0;
  for (uint8_t bit = start; bit < start + 8; bit++) {
    result <<= 1;
    bool dataBit = digitalRead(PIN_DATA_SWAPPED[bit]);
    if (dataBit) result |= 1;
  }
  return result;
}

void writeGarbled(uint16_t data) {
  int bitNum = 15;
  for (const uint8_t pin : PIN_DATA_SWAPPED) {
    digitalWrite(pin, (data >> bitNum) & 1);
    bitNum--;
  }
}

void writeData(uint16_t data) {
  int bitNum = 0;
  for (const uint8_t pin : PIN_DATA_NOSWAP) {
    digitalWrite(pin, (data >> bitNum) & 1);
    bitNum++;
  }
}

uint16_t readAddress(uint32_t addr) {
  setAddr(addr);
  digitalWrite(PIN_FLASH_CE, LOW);
  digitalWrite(PIN_FLASH_OE, LOW);
  delayMicroseconds(1);
  uint16_t data = readData();
  digitalWrite(PIN_FLASH_CE, HIGH);
  digitalWrite(PIN_FLASH_OE, HIGH);
  return data;
}

void writeCommand(uint32_t command, uint16_t data, bool isData = false) {
  setAddr(command);
  flashDataPinMode(OUTPUT);
  if (isData)
    writeGarbled(data);
  else
    writeData(data);
  digitalWrite(PIN_FLASH_CE, LOW);
  digitalWrite(PIN_FLASH_WE, LOW);
  digitalWrite(PIN_FLASH_CE, HIGH);
  digitalWrite(PIN_FLASH_WE, HIGH);
  flashDataPinMode(INPUT_PULLUP);
}

void setup() {
  Serial.begin(9600);

  pinMode(PIN_SREG_SER, OUTPUT);
  pinMode(PIN_SREG_RCLK, OUTPUT);
  pinMode(PIN_SREG_SCLK, OUTPUT);

  pinMode(PIN_FLASH_OE, OUTPUT);
  pinMode(PIN_FLASH_WE, OUTPUT);
  pinMode(PIN_FLASH_CE, OUTPUT);
  digitalWrite(PIN_FLASH_WE, HIGH);
  digitalWrite(PIN_FLASH_CE, HIGH);
  digitalWrite(PIN_FLASH_OE, HIGH);

  flashDataPinMode(INPUT_PULLUP);
  
  spi.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_SS);
  if (!sd.begin(SdSpiConfig{PIN_SD_SS, SHARED_SPI, SPI_FULL_SPEED, &spi})) {
    while(1) {
      Serial.println("cannot connect to SD card");
      delay(10000);
    }
  }
}

void dumpFlash() {
  File32 file;
  if (!file.open("/sdrom.bin", O_WRITE | O_CREAT | O_TRUNC)) {
    Serial.println("Cannot open /sdrom.bin for writing");
    return;
  }
  uint32_t addr = 0;
  unsigned long start = millis();
  while(addr != 0x400000UL) {
    int16_t data = readAddress(addr);
    file.write(data >> 8);
    file.write(data);
    addr++;
    if (addr % 0x1000 == 0) {
      Serial.print(addr / 0x1000, HEX);
      Serial.print(" ETA: ");
      float minutesLeft = (float(4194304 - addr)) * float(millis() - start) / float(addr) / 60000.0;
      int roundMinutes = floor(minutesLeft);
      int secondsLeft = floor((minutesLeft - roundMinutes) * 60.0);
      Serial.print(roundMinutes);
      Serial.print(":");
      if (secondsLeft < 10)
        Serial.print("0");
      Serial.println(secondsLeft);
      if (Serial.read() == 'c') {
        Serial.println("cancelled");
        break;
      }
    }
  }
  file.close();
  Serial.println("done");
}

void programFlash() {
  File32 file;
  if (!file.open("/newrom.bin", O_RDONLY)) {
    Serial.println("Cannot open /newrom.bin for reading");
    return;
  }

  const uint32_t sectorMask = 0b1111111;
  uint32_t addr = 0;
  unsigned long start = millis();
  while(addr < 0x400000UL) {
    uint32_t sector = addr & (~sectorMask);
    uint16_t data = 0;

    writeCommand(0x555, 0xAA);
    writeCommand(0x2AA, 0x55);
    writeCommand(sector, 0x25);
    writeCommand(sector, 15);

    for(int i=0; i<16; i++) {
      data = file.read() << 8 | file.read();
      writeCommand(addr, data, true);
      addr++;
    }
    writeCommand(sector, 0x29);

    uint16_t read = 0;
    do {
      read = readAddress(addr-1);
    } while(read != data && Serial.peek() != 'c');

    if (addr % 0x1000 == 0) {
      Serial.print(addr / 0x1000, HEX);
      Serial.print(" ETA: ");
      float minutesLeft = (float(4194304 - addr)) * float(millis() - start) / float(addr) / 60000.0;
      int roundMinutes = floor(minutesLeft);
      int secondsLeft = floor((minutesLeft - roundMinutes) * 60.0);
      Serial.print(roundMinutes);
      Serial.print(":");
      if (secondsLeft < 10)
        Serial.print("0");
      Serial.println(secondsLeft);
    }
    if (Serial.read() == 'c') {
      Serial.println("cancelled");
      break;
    }
  }

  file.close();
  Serial.println("done");
}

void eraseFlash() {
  Serial.println("Erasing...");
  writeCommand(0x555, 0xAA);
  writeCommand(0x2AA, 0x55);
  writeCommand(0x555, 0x80);
  writeCommand(0x555, 0xAA);
  writeCommand(0x2AA, 0x55);
  writeCommand(0x555, 0x10);
  uint16_t read = 0;
  while(read != 0xFFFF) {
    delay(500);
    read = readAddress(0xA5000/2);
    Serial.print("0xA5000: ");
    Serial.println(read, HEX);
  }
  Serial.println("Erase done!");
}

void loop() {
  int s = Serial.read();
  if (s != -1) {
    switch(s) {
      case 'd':
        dumpFlash();
        break;
      case 'e':
        eraseFlash();
        break;
      case 'p':
        programFlash();
        break;
      default:
        Serial.println("press (d) to dump flash to sd/sdrom.bin\npress (e) to erase flash\npress (p) to program sd/newrom.bin to flash (MAKE SURE TO ERASE FIRST!)\npress (c) during any operation to cancel");
    }
  }
  delay(100);
}
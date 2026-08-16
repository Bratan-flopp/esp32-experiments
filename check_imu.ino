#include <SPI.h>

#define PIN_SCLK 22
#define PIN_SDI  21   // SDA/SDI  -> MOSI
#define PIN_SDO  19   // SAO/SDO  -> MISO
#define PIN_CS   27
#define PIN_PS   26

SPIClass* vspi = nullptr;

uint8_t spiRd(uint8_t reg, uint8_t mode) {
  vspi->beginTransaction(SPISettings(1000000, MSBFIRST, mode));
  digitalWrite(PIN_CS, LOW);
  vspi->transfer(reg | 0x80);
  uint8_t v = vspi->transfer(0x00);
  digitalWrite(PIN_CS, HIGH);
  vspi->endTransaction();
  return v;
}

// Таблица известных чипов: {имя, регистр WHO_AM_I, ожидаемое значение}
struct Chip { const char* name; uint8_t reg; uint8_t id; };
const Chip chips[] = {
  {"ICM-45686", 0x72, 0xE9},
  {"ICM-45605", 0x72, 0xE5},
  {"ICM-42688-P",0x75, 0x47},
  {"ICM-42670-P",0x75, 0x67},
  {"MPU-6050",   0x75, 0x68},
  {"MPU-9250",   0x75, 0x71},
  {"ICM-20948",  0x00, 0xEA},
};

bool identify(uint8_t mode) {
  for (auto &c : chips) {
    uint8_t v = spiRd(c.reg, mode);
    if (v == c.id) {
      Serial.printf(">>> ЧИП ОПРЕДЕЛЁН: %s  (reg 0x%02X = 0x%02X)\n",
                    c.name, c.reg, v);
      return true;
    }
  }
  return false;
}

void setup() {
  Serial.begin(115200);
  delay(600);
  pinMode(PIN_CS, OUTPUT);
  digitalWrite(PIN_CS, HIGH);
  pinMode(PIN_PS, OUTPUT);
  digitalWrite(PIN_PS, LOW);          // SPI-режим
  delay(50);

  vspi = new SPIClass(VSPI);
  vspi->begin(PIN_SCLK, PIN_SDO, PIN_SDI, PIN_CS);

  Serial.println("\n===== ОПРЕДЕЛЕНИЕ ЧИПА =====");

  bool found = identify(SPI_MODE0);
  if (!found) found = identify(SPI_MODE3);

  if (!found) {
    // Не опознали — покажем сырые ID-регистры для диагностики
    Serial.println("Не совпало с таблицей. Сырые значения:");
    for (uint8_t md = 0; md < 2; md++) {
      uint8_t mode = md ? SPI_MODE3 : SPI_MODE0;
      Serial.printf("  MODE%d: reg0x72=0x%02X  reg0x75=0x%02X  reg0x00=0x%02X\n",
                    md ? 3 : 0,
                    spiRd(0x72, mode), spiRd(0x75, mode), spiRd(0x00, mode));
    }
  }

  Serial.println("===== ГОТОВО =====");
}

void loop() {}
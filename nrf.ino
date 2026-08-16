#include <SPI.h>
#include <RF24.h>

// nRF24L01 подключение к ESP32
// CE   GPIO16
// CSN GPIO17
// SCK GPIO18  (SPI SCK)
// MOSI GPIO23  (SPI MOSI)
// MISO GPIO19  (SPI MISO)

RF24 radio(16, 17);

#define CHANNELS 126
int yanaidytebyablya[CHANNELS];

void setup() {
  Serial.begin(115200);
  radio.begin();
  radio.setAutoAck(false);
  radio.setPALevel(RF24_PA_MIN);
  radio.setDataRate(RF24_2MBPS);
  radio.startListening();
  Serial.println("Сканирование эфира 2.4 ГГц...");
}

void loop() {
  for (int ch = 0; ch < CHANNELS; ch++) {
    radio.setChannel(ch);
    radio.startListening();
    delayMicroseconds(200);
    if (radio.testCarrier()) yanaidytebyablya[ch]++;
    radio.stopListening();
  }

  Serial.println("=== Эфир 2.4 ГГц ===");
  for (int ch = 0; ch < CHANNELS; ch++) {
    float freq = 2.400 + ch * 0.001;
    if (yanaidytebyablya[ch] > 0) {
      Serial.print(String(freq, 3) + " ГГц | канал " + String(ch) + " | ");
      for (int i = 0; i < yanaidytebyablya[ch]; i++) Serial.print("#");
      Serial.println();
    }
    yanaidytebyablya[ch] = 0;
  }
  delay(1000);
}
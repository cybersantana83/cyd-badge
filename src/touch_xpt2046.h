#pragma once
// Leitura do touch XPT2046 da CYD por bit-bang.
// O touch fica em pinos proprios (CLK 25, MOSI 32, MISO 39, CS 33, IRQ 36),
// e o barramento VSPI fica livre para o cartao SD. Para gestos (tocar, arrastar,
// segurar) a velocidade do bit-bang e mais que suficiente.
#include <Arduino.h>

class TouchXPT2046 {
 public:
  static constexpr uint8_t PIN_CLK = 25;
  static constexpr uint8_t PIN_MOSI = 32;
  static constexpr uint8_t PIN_MISO = 39;
  static constexpr uint8_t PIN_CS = 33;
  static constexpr uint8_t PIN_IRQ = 36;

  void begin() {
    pinMode(PIN_CLK, OUTPUT);
    pinMode(PIN_MOSI, OUTPUT);
    pinMode(PIN_CS, OUTPUT);
    pinMode(PIN_MISO, INPUT);
    pinMode(PIN_IRQ, INPUT);
    digitalWrite(PIN_CS, HIGH);
    digitalWrite(PIN_CLK, LOW);
    read12(0x90);  // primeira leitura liga o PENIRQ
  }

  // Retorna true se houver toque; rawX/rawY de 0 a 4095.
  bool read(uint16_t &rawX, uint16_t &rawY) {
    if (digitalRead(PIN_IRQ) == HIGH) return false;
    uint16_t z1 = read12(0xB1);
    uint16_t z2 = read12(0xC1);
    int z = (int)z1 + 4095 - (int)z2;
    if (z < 350) return false;

    // Media de algumas amostras para reduzir ruido
    uint32_t sx = 0, sy = 0;
    const int n = 4;
    for (int i = 0; i < n; i++) {
      sx += read12(0xD1);
      sy += read12(0x91);
    }
    read12(0x90);  // volta ao modo power-down com IRQ habilitado
    rawX = sx / n;
    rawY = sy / n;
    return true;
  }

 private:
  uint16_t read12(uint8_t cmd) {
    digitalWrite(PIN_CS, LOW);
    for (int i = 7; i >= 0; i--) {
      digitalWrite(PIN_MOSI, (cmd >> i) & 1);
      digitalWrite(PIN_CLK, HIGH);
      delayMicroseconds(1);
      digitalWrite(PIN_CLK, LOW);
      delayMicroseconds(1);
    }
    uint16_t v = 0;
    for (int i = 0; i < 16; i++) {
      digitalWrite(PIN_CLK, HIGH);
      delayMicroseconds(1);
      v <<= 1;
      if (digitalRead(PIN_MISO)) v |= 1;
      digitalWrite(PIN_CLK, LOW);
      delayMicroseconds(1);
    }
    digitalWrite(PIN_CS, HIGH);
    return (v >> 3) & 0x0FFF;
  }
};

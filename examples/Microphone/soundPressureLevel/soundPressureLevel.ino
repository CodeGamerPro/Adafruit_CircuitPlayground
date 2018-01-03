#include <Adafruit_CircuitPlayground.h>

void setup() {
  CircuitPlayground.begin();
  Serial.begin(115200);
}

void loop() {
  Serial.println(CircuitPlayground.mic.soundPressureLevel(50));
}

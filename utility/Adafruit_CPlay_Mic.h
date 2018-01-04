// Adafruit Circuit Playground microphone library
// by Phil Burgess / Paint Your Dragon.
// Fast Fourier transform section is derived from
// ELM-ChaN FFT library (see comments in ffft.S).

#ifndef ADAFRUIT_CPLAY_MIC_H
#define ADAFRUIT_CPLAY_MIC_H

#include "Adafruit_ZeroPDM.h"

class Adafruit_CPlay_Mic {
 public:
  Adafruit_CPlay_Mic(void) {}; // Empty constructor
  int  peak(uint16_t ms);
  void capture(int16_t *buf, uint16_t nSamples),
       fft(uint16_t *spectrum);

  float soundPressureLevel(uint16_t ms);

private:
#if defined(ARDUINO_ARCH_SAMD)
	static Adafruit_ZeroPDM pdm;
#endif
};

#endif // ADAFRUIT_CPLAY_MIC_H

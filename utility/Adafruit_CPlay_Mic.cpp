// Adafruit Circuit Playground microphone library
// by Phil Burgess / Paint Your Dragon.
// Fast Fourier transform section is derived from
// ELM-ChaN FFT library (see comments in ffft.S).

#include <Arduino.h>
#include "Adafruit_CPlay_Mic.h"

#if !defined(__AVR__)

#define SAMPLERATE_HZ 22000
#define DECIMATION    64

Adafruit_ZeroPDM Adafruit_CPlay_Mic::pdm = Adafruit_ZeroPDM(34, 35);

// a windowed sinc filter for 44 khz, 64 samples
uint16_t sincfilter[DECIMATION] = {0, 2, 9, 21, 39, 63, 94, 132, 179, 236, 302, 379, 467, 565, 674, 792, 920, 1055, 1196, 1341, 1487, 1633, 1776, 1913, 2042, 2159, 2263, 2352, 2422, 
                                  2474, 2506, 2516, 2506, 2474, 2422, 2352, 2263, 2159, 2042, 1913, 1776, 1633, 1487, 1341, 1196, 1055, 920, 792, 674, 565, 467, 379, 302, 236, 179, 132, 94, 63, 39, 21, 9, 2, 0, 0};

// a manual loop-unroller!
#define ADAPDM_REPEAT_LOOP_16(X) X X X X X X X X X X X X X X X X

static bool pdmConfigured = false;

#endif

#define DC_OFFSET       (1023 / 3)
#define NOISE_THRESHOLD 3

// -------------------------------------------------------------------------

// Reads ADC for given interval (in milliseconds, 1-65535), returns max
// deviation from DC_OFFSET (e.g. 0-341).  Uses ADC free-run mode w/polling.
// Any currently-installed ADC interrupt handler will be temporarily
// disabled while this runs.

int Adafruit_CPlay_Mic::peak(uint16_t ms) {
#ifdef __AVR__
  uint8_t  admux_save, adcsra_save, adcsrb_save, timsk0_save, channel;
  uint16_t i, nSamples;
  int16_t  adc, adcMin, adcMax;

  nSamples = (9615L * ms + 500) / 1000;
  if(!nSamples) nSamples = 1;

  channel     = analogPinToChannel(4); // Pin A4 to ADC channel
  admux_save  = ADMUX;                 // Save ADC config registers
  adcsra_save = ADCSRA;
  adcsrb_save = ADCSRB;

  // Init ADC free-run mode; f = ( 8MHz/prescaler ) / 13 cycles/conversion
  ADCSRA = 0;                          // Stop ADC interrupt, if any
  ADMUX  = _BV(REFS0) | channel;       // Aref=AVcc, channel sel, right-adj
  ADCSRB = 0;                          // Free run mode, no high MUX bit
  ADCSRA = _BV(ADEN)  |                // ADC enable
           _BV(ADSC)  |                // ADC start
           _BV(ADATE) |                // Auto trigger
           _BV(ADIF)  |                // Reset interrupt flag
           _BV(ADPS2) | _BV(ADPS1);    // 64:1 / 13 = 9615 Hz

  // ADC conversion-ready bit is polled for each sample rather than using
  // an interrupt; avoids contention with application or other library
  // using ADC ISR for other things (there can be only one) while still
  // providing the speed & precise timing of free-run mode (a loop of
  // analogRead() won't get you that).
  adcMin = 1023;
  adcMax = 0;
  for(i=0; i<nSamples; i++) {
    while(!(ADCSRA & _BV(ADIF)));      // Wait for ADC result
    adc     = ADC;                     // 0-1023
    ADCSRA |= _BV(ADIF);               // Clear bit
    if(adc < adcMin) adcMin = adc;
    if(adc > adcMax) adcMax = adc;
  }

  ADMUX  = admux_save;                 // Restore ADC config
  ADCSRB = adcsrb_save;
  ADCSRA = adcsra_save;
  (void)analogRead(A4);                // Purge residue from ADC register

  adcMin = abs(adcMin - DC_OFFSET);
  adcMax = abs(adcMax - DC_OFFSET);
  if(adcMin > DC_OFFSET) adcMin = DC_OFFSET;
  if(adcMax > DC_OFFSET) adcMax = DC_OFFSET;
  if(adcMin > adcMax)    adcMax = adcMin;

  return adcMax;
#else
  return 0;
#endif
}

// -------------------------------------------------------------------------

// Captures ADC audio samples at maximum speed supported by 32u4 (9615 Hz).
// Ostensibly for FFT code (below), but might have other uses.  Uses ADC
// free-run mode w/polling.  Any currently-installed ADC interrupt handler
// will be temporarily disabled while this runs.  No other interrupts are
// disabled; as long as interrupt handlers are minor (e.g. Timer/Counter 0
// handling of millis() and micros()), this isn't likely to lose readings.

void Adafruit_CPlay_Mic::capture(int16_t *buf, uint16_t nSamples) {
#ifdef __AVR__
  uint8_t admux_save, adcsra_save, adcsrb_save, timsk0_save, channel;
  int16_t adc;

  channel     = analogPinToChannel(4); // Pin A4 to ADC channel
  admux_save  = ADMUX;                 // Save ADC config registers
  adcsra_save = ADCSRA;
  adcsrb_save = ADCSRB;

  // Init ADC free-run mode; f = ( 8MHz/prescaler ) / 13 cycles/conversion 
  ADCSRA = 0;                          // Stop ADC interrupt, if any
  ADMUX  = _BV(REFS0) | channel;       // Aref=AVcc, channel sel, right-adj
  ADCSRB = 0;                          // Free run mode, no high MUX bit
  ADCSRA = _BV(ADEN)  |                // ADC enable
           _BV(ADSC)  |                // ADC start
           _BV(ADATE) |                // Auto trigger
           _BV(ADIF)  |                // Reset interrupt flag
           _BV(ADPS2) | _BV(ADPS1);    // 64:1 / 13 = 9615 Hz

  // ADC conversion-ready bit is polled for each sample rather than using
  // an interrupt; avoids contention with application or other library
  // using ADC ISR for other things (there can be only one) while still
  // providing the speed & precise timing of free-run mode (a loop of
  // analogRead() won't get you that).
  for(uint16_t i=0; i<nSamples; i++) {
    while(!(ADCSRA & _BV(ADIF)));      // Wait for ADC result
    adc     = ADC;
    ADCSRA |= _BV(ADIF);               // Clear bit
    // FFT requires signed inputs; ADC output is unsigned.  DC offset is
    // NOT 512 on Circuit Playground because it uses a 1.1V OpAmp input
    // as the midpoint, and may swing asymmetrically on the high side.
    // Sign-convert and then clip range to +/- DC_OFFSET.
    if(adc <= (DC_OFFSET - NOISE_THRESHOLD)) {
      adc  -= DC_OFFSET;
    } else if(adc >= (DC_OFFSET + NOISE_THRESHOLD)) {
      adc  -= DC_OFFSET;
      if(adc > (DC_OFFSET * 2)) adc = DC_OFFSET * 2;
    } else {
      adc   = 0; // Below noise threshold
    }
    buf[i]  = adc;
  }

  ADMUX  = admux_save;                 // Restore ADC config
  ADCSRB = adcsrb_save;
  ADCSRA = adcsra_save;
  (void)analogRead(A4);                // Purge residue from ADC register
#else
#endif
}

float Adafruit_CPlay_Mic::soundPressureLevel(uint16_t ms){
  float gain;
  int16_t *ptr;
#ifdef __AVR__
  gain = 4.5;
  uint16_t len = 9.615 * ms;
  int16_t data[len];

  capture(data, len);
#else
  gain = 9;

  if(!pdmConfigured){
    pdm.begin();
    pdm.configure(SAMPLERATE_HZ * DECIMATION / 16, true);
    pdmConfigured = true;
  }

  uint16_t len = (float)(SAMPLERATE_HZ/1000) * ms;
  int16_t data[len];
  ptr = data;

  while(ptr < (data + len)){
    uint16_t runningsum = 0;
    uint16_t *sinc_ptr = sincfilter;

    for (uint8_t samplenum=0; samplenum < (DECIMATION/16) ; samplenum++) {
       uint16_t sample = pdm.read() & 0xFFFF;    // we read 16 bits at a time, by default the low half

       ADAPDM_REPEAT_LOOP_16(      // manually unroll loop: for (int8_t b=0; b<16; b++) 
         {
           // start at the LSB which is the 'first' bit to come down the line, chronologically 
           // (Note we had to set I2S_SERCTRL_BITREV to get this to work, but saves us time!)
           if (sample & 0x1) {
             runningsum += *sinc_ptr;     // do the convolution
           }
           sinc_ptr++;
           sample >>= 1;
        }
      )
    }

    // since we wait for the samples from I2S peripheral, we dont need to delay, we will 'naturally'
    // wait the right amount of time between analog writes
    //Serial.println(runningsum);
    
    runningsum /= 64 ; // convert 16 bit -> 10 bit

    *ptr++ = runningsum;
  }
#endif
  int16_t *end = data + len;
  float pref = 0.00002;

  /*******************************
   *   REMOVE DC OFFSET
   ******************************/
  int32_t avg = 0;
  ptr = data;
  while(ptr < end) avg += *ptr++;
  avg = avg/len;

  ptr = data;
  while(ptr < end) *ptr++ -= avg;
  
   /*******************************
   *   GET MAX VALUE
   ******************************/

   int16_t maxVal = 0;
   ptr = data;
   while(ptr < end){
     int32_t v = abs(*ptr++);
     if(v > maxVal) maxVal = v;
   }
   
   float conv = ((float)maxVal)/1023 * gain;

   /*******************************
   *   CALCULATE SPL
   ******************************/
   return 20 * log10(conv/pref);
}

// -------------------------------------------------------------------------

// Performs one cycle of fast Fourier transform (FFT) with audio captured
// from mic on A4.  Output is 32 'bins,' each covering an equal range of
// frequencies from 0 to 4800 Hz (i.e. 0-150 Hz, 150-300 Hz, 300-450, etc).
// Needs about 450 bytes free RAM to operate.

typedef struct {
  int16_t r;
  int16_t i;
} complex_t;

extern "C" { // In ffft.S
  void fft_input(const int16_t *, complex_t *),
       fft_execute(complex_t *),
       fft_output(complex_t *, uint16_t *);
} 

void Adafruit_CPlay_Mic::fft(
 uint16_t *spectrum) {               // Spectrum output buffer, uint16_t[32]
  if(spectrum) {
    int16_t   capBuf[64];            // Audio capture buffer
    complex_t butterfly[64];         // FFT "butterfly" buffer

    capture(capBuf, 64);             // Collect mic data into capBuf
    fft_input(capBuf, butterfly);    // Samples -> complex #s
    fft_execute(butterfly);          // Process complex data
    fft_output(butterfly, spectrum); // Complex -> spectrum (32 bins)
  }
}

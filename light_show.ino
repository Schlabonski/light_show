// ESP32 to control a light show. Based on https://github.com/s-marley/ESP32_FFT_VU/.

#include <arduinoFFT.h>
#include <FastLED.h>

#define SAMPLES         1024          // Must be a power of 2
#define SAMPLING_FREQ   40000         // Hz, must be 40000 or less due to ADC conversion time. Determines maximum frequency that can be analysed by the FFT Fmax=sampleF/2.
#define AMPLITUDE       500          // Depending on your audio source level, you may need to alter this value. Can be used as a 'sensitivity' control.
#define AUDIO_IN_PIN    34            // Signal in on this pin

#define NUM_BANDS       8            // To change this, you will need to change the bunch of if statements describing the mapping from bins to bands
#define NOISE           1000           // Used as a crude noise filter, values below this are ignored

#define ONBOARD_LED     2
#define FLOODLIGHT_OUT 25
#define LED_STRIPE_OUT_0 2

// Sampling and FFT stuff
unsigned int sampling_period_us;
byte peak[] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};              // The length of these arrays must be >= NUM_BANDS
int oldBarHeights[] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
int bandValues[] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
double vReal[SAMPLES];
double vImag[SAMPLES];
unsigned long newTime;
bool beat_detected_low = false;
bool beat_detected_high = false;
arduinoFFT FFT = arduinoFFT(vReal, vImag, SAMPLES, SAMPLING_FREQ);

// led stripes
const int freq = 5000;
const int ledChannel_0 = 0;
const int resolution = 8;
byte brightness_stripe_0 = 0;

// floodlights
byte brightnness_floodlight = 0;
int counter_low = 0;


void setup() {
  // FFT setting
  sampling_period_us = round(1000000 * (1.0 / SAMPLING_FREQ));

  // onboard LED for debugging
  pinMode(ONBOARD_LED, OUTPUT);
  digitalWrite(ONBOARD_LED, HIGH);

  // LED stripes
  ledcSetup(ledChannel_0, freq, resolution);
  ledcAttachPin(LED_STRIPE_OUT_0, ledChannel_0);

}

void loop() {
  // Reset bandValues[]
  for (int i = 0; i<NUM_BANDS; i++){
    bandValues[i] = 0;
  }

  // Sample the audio pin
  for (int i = 0; i < SAMPLES; i++) {
    newTime = micros();
    vReal[i] = analogRead(AUDIO_IN_PIN); // A conversion takes about 9.7uS on an ESP32
    vImag[i] = 0;
    while ((micros() - newTime) < sampling_period_us) { /* chill */ }
  }

  // Compute FFT
  FFT.DCRemoval();
  FFT.Windowing(FFT_WIN_TYP_HAMMING, FFT_FORWARD);
  FFT.Compute(FFT_FORWARD);
  FFT.ComplexToMagnitude();

  // Analyse FFT results
  for (int i = 2; i < (SAMPLES/2); i++){       // Don't use sample 0 and only first SAMPLES/2 are usable. Each array element represents a frequency bin and its value the amplitude.
    if (vReal[i] > NOISE) {                    // Add a crude noise filter

    //8 bands, 12kHz top band
      if (i > 0 && i<=6 )           bandValues[0]  += (int)vReal[i]; // volume band
      if (i>3   && i<=6  ) bandValues[1]  += (int)vReal[i]; // messed with the value to combine lower, old was 3
      if (i>6   && i<=13 ) bandValues[2]  += (int)vReal[i];
      if (i>13  && i<=27 ) bandValues[3]  += (int)vReal[i];
      if (i>27  && i<=55 ) bandValues[4]  += (int)vReal[i];
      if (i>55  && i<=112) bandValues[5]  += (int)vReal[i];
      if (i>112 && i<=229) bandValues[6]  += (int)vReal[i];
      if (i>229          ) bandValues[7]  += (int)vReal[i];

    /*16 bands, 12kHz top band
      if (i<=2 )           bandValues[0]  += (int)vReal[i];
      if (i>2   && i<=3  ) bandValues[1]  += (int)vReal[i];
      if (i>3   && i<=5  ) bandValues[2]  += (int)vReal[i];
      if (i>5   && i<=7  ) bandValues[3]  += (int)vReal[i];
      if (i>7   && i<=9  ) bandValues[4]  += (int)vReal[i];
      if (i>9   && i<=13 ) bandValues[5]  += (int)vReal[i];
      if (i>13  && i<=18 ) bandValues[6]  += (int)vReal[i];
      if (i>18  && i<=25 ) bandValues[7]  += (int)vReal[i];
      if (i>25  && i<=36 ) bandValues[8]  += (int)vReal[i];
      if (i>36  && i<=50 ) bandValues[9]  += (int)vReal[i];
      if (i>50  && i<=69 ) bandValues[10] += (int)vReal[i];
      if (i>69  && i<=97 ) bandValues[11] += (int)vReal[i];
      if (i>97  && i<=135) bandValues[12] += (int)vReal[i];
      if (i>135 && i<=189) bandValues[13] += (int)vReal[i];
      if (i>189 && i<=264) bandValues[14] += (int)vReal[i];
      if (i>264          ) bandValues[15] += (int)vReal[i];*/
    }
  }

  // Process the FFT data into bar heights
  for (byte band = 0; band < NUM_BANDS; band++) {

    // Scale the bars for the display
    int barHeight = bandValues[band] / AMPLITUDE;

    // Small amount of averaging between frames
    // barHeight = ((oldBarHeights[band] * 1) + barHeight) / 2;

    // Move peak up
    if (barHeight > peak[band]) {
      peak[band] = barHeight;
      if (band == 0){
          beat_detected_low = true;
      }
      else if (band >= 7) {
          //beat_detected_low = true;
          beat_detected_high = true;
      }
    }

    // Save oldBarHeights for averaging later
    oldBarHeights[band] = barHeight;
  }

  // Decay peak
  EVERY_N_MILLISECONDS(10) {
    brightness_stripe_0 /= 2;
    if (brightnness_floodlight > 0) {
      brightnness_floodlight = 0;
    }
    ledcWrite(ledChannel_0, brightness_stripe_0);
    dacWrite(FLOODLIGHT_OUT, brightnness_floodlight);
    
    for (byte band = 0; band < NUM_BANDS; band++)
      if (peak[band] > 0) peak[band] -= 1;
  }

  // flash the lights
  EVERY_N_MILLISECONDS(1) {
      if (beat_detected_low){
          brightnness_floodlight = 150;
          dacWrite(FLOODLIGHT_OUT, brightnness_floodlight);
          beat_detected_low = false;
          counter_low++;
      }
      if (beat_detected_high){
          brightness_stripe_0 = 255;
          ledcWrite(ledChannel_0, brightness_stripe_0);
          beat_detected_high = false;
      }
  }
}

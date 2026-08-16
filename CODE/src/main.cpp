#include <Arduino.h>
#include <Adafruit_GFX.h>    // Core graphics library
#include <Adafruit_ST7789.h>
#include <SPI.h>
#include <Adafruit_AS7341.h>
#include <ESPAsyncWebServer.h>
#include "webPage.h"
#include <math.h> // Added for the log10 function

// Configuration constants
#define MIN_LED_CURRENT 4
#define MAX_LED_CURRENT 258
#define MIN_GAIN 1
#define MAX_GAIN 512

#define BUTTON_PIN 1  // GPIO1

// WiFi configuration
const char* ssid = "Smart_Sense_Network";
const char* password = "smarteza";

// Use dedicated hardware SPI pins
Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);
Adafruit_AS7341 as7341;

struct MeasurementConfig {
    uint16_t gain = 1;
    uint16_t current = 100;
    float concentration_values[5];
    bool isValid = false;
};

// States of the measurement state machine
enum MeasurementState {
    WAIT_SETUP,
    WAIT_BLANK,           // Waits for the reference (blank) measurement
    MEASURING_BLANK,      // Measures the reference (blank)
    WAIT_BUTTON,
    MEASURING,
    WAIT_UNKNOWN_SAMPLE,
    MEASURE_UNKNOWN_SAMPLE
};

MeasurementState measureState = WAIT_SETUP;
uint8_t currentSample = 0;
bool lastButtonState = LOW;
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 50; // milliseconds

MeasurementConfig config;
AsyncWebServer server(80);

// Channel variables
#define MAX_CHANNELS 8
#define NUM_CHANNELS 8
#define READINGS_PER_SAMPLE 20
uint16_t selectedChannels[MAX_CHANNELS];
uint8_t numSelectedChannels = 0;

// Data storage variables
float blankReadings[MAX_CHANNELS];          // Stores the reference (blank) reading
float sampleAbsorbances[5][MAX_CHANNELS];   // Stores the absorbance of each sample

// Global arrays holding the regression coefficients (y = ax + b)
float a_reg[MAX_CHANNELS]; // Slope 'a'
float b_reg[MAX_CHANNELS]; // Intercept 'b'

// Function prototypes
void initializeDisplay();
void initializeSensor();
void writeTextToDisplay(const String& text, uint16_t color, uint8_t size, uint16_t x, uint16_t y, uint16_t durationMs);
void writeTextCenteredToDisplay(const String& text, uint16_t color, uint8_t size, uint16_t durationMs);
bool configureSensor();
void setupWebRoutes();
void clearDisplay();
float calculateAverage(uint16_t readings[], int size);
void performSingleMeasurement(float* resultArray);
void showRegressionsOnDisplay();
void calculateLinearRegressionPerChannel(float x_values[5], float y_values[5][MAX_CHANNELS], uint8_t numChannels, float* a_out, float* b_out);

void setup(void) {
  // WiFi configuration
  WiFi.softAP(ssid, password);
  // Initialize the display and the sensor
  initializeDisplay();
  initializeSensor();
  setupWebRoutes();
  server.begin();

  pinMode(BUTTON_PIN, INPUT_PULLDOWN);

  writeTextToDisplay("Access the web", ST77XX_WHITE, 2, 10, 20, 0);
  writeTextToDisplay("interface at:", ST77XX_WHITE, 2, 10, 40, 0);
  writeTextToDisplay(WiFi.softAPIP().toString(), ST77XX_CYAN, 2, 10, 60, 0);
  writeTextToDisplay("to send a setup", ST77XX_WHITE, 2, 10, 80, 0);
}

void loop() {
  // 1. Waits for the configuration sent from the web page
  if (config.isValid && measureState == WAIT_SETUP) {
    clearDisplay();
    writeTextToDisplay("Setup OK", ST77XX_GREEN, 2, 10, 30, 0);
    writeTextToDisplay("Insert reference", ST77XX_CYAN, 2, 10, 50, 0);
    writeTextToDisplay("and press the", ST77XX_CYAN, 2, 10, 70, 0);
    writeTextToDisplay("button", ST77XX_CYAN, 2, 10, 90, 0);
    measureState = WAIT_BLANK;
  }

  // 2. Waits for the button to measure the REFERENCE (blank)
  if (measureState == WAIT_BLANK) {
      if (digitalRead(BUTTON_PIN) == HIGH) {
          measureState = MEASURING_BLANK;
          delay(300); // Debounce
      }
  }

  // 3. Performs the REFERENCE (blank) measurement
  if (measureState == MEASURING_BLANK) {
      clearDisplay();
      writeTextCenteredToDisplay("Measuring Reference...", ST77XX_YELLOW, 2, 3000);
      performSingleMeasurement(blankReadings); // Stores the reference reading

      writeTextToDisplay("reference measured", ST77XX_GREEN, 2, 10, 30, 0);
      writeTextToDisplay("Insert sample 1", ST77XX_CYAN, 2, 10, 50, 0);
      writeTextToDisplay("and press the", ST77XX_CYAN, 2, 10, 70, 0);
      writeTextToDisplay("button", ST77XX_CYAN, 2, 10, 90, 0);
      measureState = WAIT_BUTTON;
      currentSample = 0;
  }

  // 4. Waits for the button for the calibration samples
  if (measureState == WAIT_BUTTON) {
    if (digitalRead(BUTTON_PIN) == HIGH) {
      measureState = MEASURING;
      delay(300); // Debounce
    }
  }

  // 5. Measures the 5 calibration samples
  if (measureState == MEASURING) {
    clearDisplay();
    writeTextToDisplay("Measuring sample", ST77XX_YELLOW, 2, 10, 20, 0);
    writeTextToDisplay("number " + String(currentSample + 1), ST77XX_YELLOW, 2, 10, 40, 0);
    delay(3000); // Time for the user to read the message

    float sampleResults[MAX_CHANNELS];
    performSingleMeasurement(sampleResults);

    // Calculates the absorbance and saves the results
    for (uint8_t i = 0; i < numSelectedChannels; i++) {
        // Absorbance calculation: A = -log10(I / I0)
        // I = sampleResults[i], I0 = blankReadings[i]
        if (blankReadings[i] > 0 && sampleResults[i] > 0) {
            float transmittance = sampleResults[i] / blankReadings[i];
            sampleAbsorbances[currentSample][i] = -log10(transmittance);
        } else {
            sampleAbsorbances[currentSample][i] = 0; // Avoids division by zero or log of zero
        }

        // Shows the calculated absorbance
        writeTextToDisplay("Ch " + String(selectedChannels[i]) + " Abs: " + String(sampleAbsorbances[currentSample][i], 3),
                         ST77XX_WHITE, 2, 10, 10 + i * 15, 3000);
    }

    currentSample++;
    if (currentSample < 5) { // If the 5 samples have not been measured yet
      clearDisplay();
      writeTextToDisplay("Insert sample " + String(currentSample + 1), ST77XX_WHITE, 2, 10, 40, 0);
      writeTextToDisplay("and press the", ST77XX_WHITE, 2, 10, 60, 0);
      writeTextToDisplay("button", ST77XX_WHITE, 2, 10, 80, 0);
      measureState = WAIT_BUTTON;
    } else { // If all 5 samples have already been measured
      clearDisplay();
      writeTextCenteredToDisplay("Calculating curve...", ST77XX_CYAN, 2, 2000);
      showRegressionsOnDisplay(); // Calculates and shows the a and b values for each channel
      writeTextToDisplay("Calibration Done!", ST77XX_GREEN, 2, 10, 40, 2000);

      clearDisplay();
      writeTextToDisplay("Press the button", ST77XX_WHITE, 2, 10, 20, 0);
      writeTextToDisplay("for unknown sample", ST77XX_WHITE, 2, 10, 40, 0);
      measureState = WAIT_UNKNOWN_SAMPLE;
    }
  }

  // 6. Waits for the button to measure an unknown sample
  if (measureState == WAIT_UNKNOWN_SAMPLE) {
    if (digitalRead(BUTTON_PIN) == HIGH) {
      measureState = MEASURE_UNKNOWN_SAMPLE;
      delay(300);
    }
  }

  // 7. Measures the unknown sample and calculates its concentration
  if (measureState == MEASURE_UNKNOWN_SAMPLE) {
    clearDisplay();
    writeTextToDisplay("Measuring unknown", ST77XX_YELLOW, 2, 10, 20, 0);
    writeTextToDisplay("sample...", ST77XX_YELLOW, 2, 10, 40, 0);
    delay(3000);

    float unknownRawResults[MAX_CHANNELS];
    performSingleMeasurement(unknownRawResults);

    clearDisplay();
    writeTextCenteredToDisplay("Calculating...", ST77XX_CYAN, 2, 1000);

    for (uint8_t i = 0; i < numSelectedChannels; i++) {
      // Calculates the concentration from the absorbance
      float unknownAbsorbance = 0;
      if (blankReadings[i] > 0 && unknownRawResults[i] > 0) {
        unknownAbsorbance = -log10(unknownRawResults[i] / blankReadings[i]);
      }

      // Calculates the concentration using the inverted line equation: x = (y - b) / a
      float concentration = 0;
      if (a_reg[i] != 0) { // Avoids division by zero
          concentration = (unknownAbsorbance - b_reg[i]) / a_reg[i];
      }

      clearDisplay();
      String concStr = "Ch " + String(selectedChannels[i]) + " : " + String(concentration, 4);
      writeTextToDisplay(concStr, ST77XX_WHITE, 2, 10, 50, 10000);
    }

    clearDisplay();
    writeTextToDisplay("Press the button", ST77XX_WHITE, 2, 10, 20, 0);
    writeTextToDisplay("for another", ST77XX_WHITE, 2, 10, 40, 0);
    writeTextToDisplay("unknown sample", ST77XX_WHITE, 2, 10, 60, 0);
    measureState = WAIT_UNKNOWN_SAMPLE;
  }
}

void performSingleMeasurement(float* resultArray) {
    as7341.enableLED(true);
    delay(100); // Short delay to let the LED stabilize

    uint16_t readings[READINGS_PER_SAMPLE][NUM_CHANNELS] = {0};

    for (int i = 0; i < READINGS_PER_SAMPLE; i++) {
        uint16_t rawReadings[12];
        if (as7341.readAllChannels(rawReadings)) {
            for (int j = 0; j < numSelectedChannels; j++) {
                int idx = -1;
                switch(selectedChannels[j]) {
                    case 415: idx = 0; break; // F1
                    case 445: idx = 1; break; // F2
                    case 480: idx = 2; break; // F3
                    case 515: idx = 3; break; // F4
                    case 555: idx = 6; break; // F5
                    case 590: idx = 7; break; // F6
                    case 630: idx = 8; break; // F7
                    case 680: idx = 9; break; // F8
                }
                if (idx >= 0) {
                    readings[i][j] = rawReadings[idx];
                }
            }
        }
        delay(50);
        clearDisplay(); // Clears the screen for each reading
        writeTextCenteredToDisplay("Reading " + String(i + 1) + " done", ST77XX_WHITE, 2, 0);
    }

     as7341.enableLED(false);

    // Calculate the averages for each selected channel
    for (int channel = 0; channel < numSelectedChannels; channel++) {
        uint16_t channelReadings[READINGS_PER_SAMPLE];
        for (int i = 0; i < READINGS_PER_SAMPLE; i++) {
            channelReadings[i] = readings[i][channel];
        }
        resultArray[channel] = calculateAverage(channelReadings, READINGS_PER_SAMPLE);
    }
    clearDisplay();
}

// Computes the regression for X = Concentration, Y = Absorbance
void calculateLinearRegressionPerChannel(float x_values[5], float y_values[5][MAX_CHANNELS], uint8_t numChannels, float* a_out, float* b_out) {
    for (uint8_t channel = 0; channel < numChannels; channel++) {
        float sumX = 0, sumY = 0, sumXY = 0, sumXX = 0;
        int n = 5;
        for (uint8_t i = 0; i < n; i++) {
            float x = x_values[i];
            float y = y_values[i][channel];
            sumX += x;
            sumY += y;
            sumXY += x * y;
            sumXX += x * x;
        }
        float denom = n * sumXX - sumX * sumX;
        if (denom != 0) {
            a_out[channel] = (n * sumXY - sumX * sumY) / denom; // 'a' (slope)
            b_out[channel] = (sumY * sumXX - sumX * sumXY) / denom; // 'b' (intercept)
        } else {
            a_out[channel] = 0;
            b_out[channel] = 0;
        }
    }
}

void showRegressionsOnDisplay() {
    // Passes the concentration and absorbance values to the regression function
    calculateLinearRegressionPerChannel(config.concentration_values, sampleAbsorbances, numSelectedChannels, a_reg, b_reg);

    for (uint8_t i = 0; i < numSelectedChannels; i++) {
        clearDisplay();
        String channelStr = "Ch " + String(selectedChannels[i]);
        String aStr = "a: " + String(a_reg[i], 4); // slope
        String bStr = "b: " + String(b_reg[i], 4); // intercept
        writeTextToDisplay(channelStr, ST77XX_WHITE, 2, 10, 20, 0);
        writeTextToDisplay(aStr, ST77XX_YELLOW, 2, 10, 50, 0);
        writeTextToDisplay(bStr, ST77XX_YELLOW, 2, 10, 80, 0);
        delay(5000);
    }
    clearDisplay();
}

void clearDisplay(){
    tft.fillScreen(ST77XX_BLACK);
}

void initializeDisplay() {
  // Turn on the display backlight
  pinMode(TFT_BACKLITE, OUTPUT);
  digitalWrite(TFT_BACKLITE, HIGH);

  // Turn on the power for TFT / I2C
  pinMode(TFT_I2C_POWER, OUTPUT);
  digitalWrite(TFT_I2C_POWER, HIGH);
  delay(10);

  // Initialize the TFT display
  tft.init(135, 240);
  tft.setRotation(3);
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextWrap(true);
}

void initializeSensor() {
  // Initialize the AS7341 sensor
  while(!as7341.begin()) {
    writeTextToDisplay("Failed to initialize AS7341", ST77XX_RED, 2, 10, 40, 0);
    delay(1000);
  }

  clearDisplay();

  writeTextToDisplay("AS7341 initialized", ST77XX_GREEN, 2, 10, 40, 5000);

  // Configure sensor parameters
  as7341.setATIME(100);
  as7341.setASTEP(999);
}

void writeTextToDisplay(const String& text, uint16_t color, uint8_t size, uint16_t x, uint16_t y, uint16_t durationMs) {
  tft.setTextColor(color);
  tft.setTextSize(size);
  tft.setCursor(x, y);
  tft.println(text);
  if (durationMs > 0) {
    delay(durationMs);
    tft.fillScreen(ST77XX_BLACK); // Clears the screen after the delay
  }
}

void writeTextCenteredToDisplay(const String& text, uint16_t color, uint8_t size, uint16_t durationMs) {
  tft.setTextColor(color);
  tft.setTextSize(size);
  int16_t x1, y1;
  uint16_t w, h;
  tft.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  uint16_t x = (tft.width() - w) / 2;
  uint16_t y = (tft.height() - h) / 2;
  tft.setCursor(x, y);
  tft.println(text);
  if (durationMs > 0) {
    delay(durationMs);
  }
}

bool configureSensor() {
    if(config.current < MIN_LED_CURRENT || config.current > MAX_LED_CURRENT) {
        writeTextCenteredToDisplay("Invalid LED current", ST77XX_RED, 2, 2000);
        return false;
    }
    as7341.setLEDCurrent(config.current);
    as7341_gain_t gainSetting;
    switch (config.gain) {
        case 1: gainSetting = AS7341_GAIN_1X; break;
        case 2: gainSetting = AS7341_GAIN_2X; break;
        case 4: gainSetting = AS7341_GAIN_4X; break;
        case 8: gainSetting = AS7341_GAIN_8X; break;
        case 16: gainSetting = AS7341_GAIN_16X; break;
        case 32: gainSetting = AS7341_GAIN_32X; break;
        case 64: gainSetting = AS7341_GAIN_64X; break;
        case 128: gainSetting = AS7341_GAIN_128X; break;
        case 256: gainSetting = AS7341_GAIN_256X; break;
        case 512: gainSetting = AS7341_GAIN_512X; break;
        default:
        writeTextCenteredToDisplay("Invalid gain setting", ST77XX_RED, 2, 2000);
        return false;
    }
    as7341.setGain(gainSetting);
    return true;
}

void setupWebRoutes() {
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "text/html", webpage);
    });

    server.on("/setup", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!request->hasParam("gain") || !request->hasParam("current") ||
            !request->hasParam("concentration1") || !request->hasParam("concentration2") ||
            !request->hasParam("concentration3") || !request->hasParam("concentration4") ||
            !request->hasParam("concentration5") || !request->hasParam("channels")) {
            request->send(400, "application/json", "{\"error\":\"Missing parameters\"}");
            return;
        }

        config.gain = request->getParam("gain")->value().toInt();
        config.current = request->getParam("current")->value().toInt();
        // Stores the concentration values
        config.concentration_values[0] = request->getParam("concentration1")->value().toFloat();
        config.concentration_values[1] = request->getParam("concentration2")->value().toFloat();
        config.concentration_values[2] = request->getParam("concentration3")->value().toFloat();
        config.concentration_values[3] = request->getParam("concentration4")->value().toFloat();
        config.concentration_values[4] = request->getParam("concentration5")->value().toFloat();

        // Parse the selected channels
        String channelsStr = request->getParam("channels")->value();
        numSelectedChannels = 0;
        int lastIdx = 0;
        while (numSelectedChannels < MAX_CHANNELS) {
            int commaIdx = channelsStr.indexOf(',', lastIdx);
            String ch = (commaIdx == -1) ? channelsStr.substring(lastIdx) : channelsStr.substring(lastIdx, commaIdx);
            if (ch.length() > 0) selectedChannels[numSelectedChannels++] = ch.toInt();
            if (commaIdx == -1) break;
            lastIdx = commaIdx + 1;
        }

        if (!configureSensor()) {
            request->send(500, "application/json", "{\"error\":\"Sensor configuration failed\"}");
            writeTextToDisplay("Sensor config failed", ST77XX_RED, 2, 10, 10, 2000);
            return;
        }
        config.isValid = true;

        request->send(200, "application/json", "{\"status\":\"success\"}");
    });
}

float calculateAverage(uint16_t readings[], int size) {
  for (int i = 0; i < size - 1; i++) {
    for (int j = i + 1; j < size; j++) {
      if (readings[j] < readings[i]) {
        uint16_t temp = readings[i];
        readings[i] = readings[j];
        readings[j] = temp;
      }
    }
  }

  int firstQuartile = size / 4;
  int lastQuartile = firstQuartile * 3;
  float sum = 0.0;
  int count = 0;
  for (int i = firstQuartile; i < lastQuartile; i++) {
    sum += readings[i];
    count++;
  }
  return count > 0 ? sum / count : 0;
}

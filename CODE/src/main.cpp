#include <Arduino.h>
#include <Adafruit_GFX.h>    // Core graphics library
#include <Adafruit_ST7789.h>
#include <SPI.h>
#include <Adafruit_AS7341.h>
#include <ESPAsyncWebServer.h>
#include "webPage.h"
#include <math.h> // Adicionado para a função log10

// Constantes de configuração
#define MIN_LED_CURRENT 4
#define MAX_LED_CURRENT 258
#define MIN_GAIN 1
#define MAX_GAIN 512

#define BUTTON_PIN 1  // GPIO1

// Configuração WiFi
const char* ssid = "Smart_Sense_Network";
const char* password = "smarteza";

// Use dedicated hardware SPI pins
Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);
Adafruit_AS7341 as7341;

struct MeasurementConfig {
    uint16_t gain = 1;
    uint16_t current = 100;
    float concentration_values[5]; // Modificado de pH_values
    bool isValid = false;
};

// Estados da máquina de estados de medição
enum MeasurementState {
    WAIT_SETUP,
    WAIT_BLANK,           // Novo estado para aguardar a medição do branco
    MEASURING_BLANK,      // Novo estado para medir o branco
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

// Variáveis de canal
#define MAX_CHANNELS 8
#define NUM_CHANNELS 8
#define READINGS_PER_SAMPLE 20
uint16_t selectedChannels[MAX_CHANNELS];
uint8_t numSelectedChannels = 0;

// Variáveis para armazenamento de dados
float blankReadings[MAX_CHANNELS];          // MODIFICAÇÃO: Armazena a leitura do branco
float absorbanceAmostras[5][MAX_CHANNELS];  // MODIFICAÇÃO: Armazena a absorbância de cada amostra

// Arrays globais para armazenar coeficientes da regressão (y = ax + b)
float a_reg[MAX_CHANNELS]; // Coeficiente angular (slope 'a')
float b_reg[MAX_CHANNELS]; // Intercepto (intercept 'b')

// Protótipos de função
void initializeDisplay();
void initializeSensor();
void writeTextToDisplay(const String& text, uint16_t color, uint8_t size, uint16_t x, uint16_t y, uint16_t durationMs);
void writeTextCenteredToDisplay(const String& text, uint16_t color, uint8_t size, uint16_t durationMs);
bool configureSensor();
void setupWebRoutes();
void clearDisplay();
float calculateAverage(uint16_t readings[], int size);
void performSingleMeasurement(float* resultArray);
void mostrarRegressoesNaTela();
void calcularRegressaoLinearPorCanal(float x_values[5], float y_values[5][MAX_CHANNELS], uint8_t numCanais, float* a_out, float* b_out);

void setup(void) {
  // Configuração WiFi
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
  // 1. Aguarda configuração via web
  if (config.isValid && measureState == WAIT_SETUP) {
    clearDisplay();
    writeTextToDisplay("Setup OK", ST77XX_GREEN, 2, 10, 30, 0);
    writeTextToDisplay("Insert reference", ST77XX_CYAN, 2, 10, 50, 0);
    writeTextToDisplay("and press the", ST77XX_CYAN, 2, 10, 70, 0);
    writeTextToDisplay("button", ST77XX_CYAN, 2, 10, 90, 0);
    measureState = WAIT_BLANK;
  }
  
  // 2. Aguarda botão para medir o BRANCO
  if (measureState == WAIT_BLANK) {
      if (digitalRead(BUTTON_PIN) == HIGH) {
          measureState = MEASURING_BLANK;
          delay(300); // Debounce
      }
  }

  // 3. Realiza a medição do BRANCO
  if (measureState == MEASURING_BLANK) {
      clearDisplay();
      writeTextCenteredToDisplay("Measuring Reference...", ST77XX_YELLOW, 2, 3000);
      performSingleMeasurement(blankReadings); // Armazena a leitura do branco
      
      writeTextToDisplay("reference measured", ST77XX_GREEN, 2, 10, 30, 0);
      writeTextToDisplay("Insert sample 1", ST77XX_CYAN, 2, 10, 50, 0);
      writeTextToDisplay("and press the", ST77XX_CYAN, 2, 10, 70, 0);
      writeTextToDisplay("button", ST77XX_CYAN, 2, 10, 90, 0);
      measureState = WAIT_BUTTON;
      currentSample = 0;
  }

  // 4. Aguarda botão para as amostras de calibração
  if (measureState == WAIT_BUTTON) {
    if (digitalRead(BUTTON_PIN) == HIGH) {
      measureState = MEASURING;
      delay(300); // Debounce
    }
  }

  // 5. Realiza a medição das 5 amostras de calibração
  if (measureState == MEASURING) {
    clearDisplay();
    writeTextToDisplay("Measuring sample", ST77XX_YELLOW, 2, 10, 20, 0);
    writeTextToDisplay("number " + String(currentSample + 1), ST77XX_YELLOW, 2, 10, 40, 0);
    delay(3000); // Tempo para o usuário ver a mensagem

    float sampleResults[MAX_CHANNELS];
    performSingleMeasurement(sampleResults);

    // MODIFICAÇÃO: Calcula a absorbância e salva os resultados
    for (uint8_t i = 0; i < numSelectedChannels; i++) {
        // Cálculo de Absorbância: A = -log10(I / I0)
        // I = sampleResults[i], I0 = blankReadings[i]
        if (blankReadings[i] > 0 && sampleResults[i] > 0) {
            float transmittance = sampleResults[i] / blankReadings[i];
            absorbanceAmostras[currentSample][i] = -log10(transmittance);
        } else {
            absorbanceAmostras[currentSample][i] = 0; // Evita divisão por zero ou log de zero
        }
        
        // Exibe a absorbância calculada
        writeTextToDisplay("Ch " + String(selectedChannels[i]) + " Abs: " + String(absorbanceAmostras[currentSample][i], 3),
                         ST77XX_WHITE, 2, 10, 10 + i * 15, 3000);
    }

    currentSample++;
    if (currentSample < 5) { // Se ainda não mediu as 5 amostras
      clearDisplay();
      writeTextToDisplay("Insert sample " + String(currentSample + 1), ST77XX_WHITE, 2, 10, 40, 0);
      writeTextToDisplay("and press the", ST77XX_WHITE, 2, 10, 60, 0);
      writeTextToDisplay("button", ST77XX_WHITE, 2, 10, 80, 0);
      measureState = WAIT_BUTTON;
    } else { // Se já mediu todas as 5 amostras
      clearDisplay();
      writeTextCenteredToDisplay("Calculating curve...", ST77XX_CYAN, 2, 2000);
      mostrarRegressoesNaTela(); // Calcula e mostra os valores de a e b para cada canal
      writeTextToDisplay("Calibration Done!", ST77XX_GREEN, 2, 10, 40, 2000);
      
      clearDisplay();
      writeTextToDisplay("Press the button", ST77XX_WHITE, 2, 10, 20, 0);
      writeTextToDisplay("for unknown sample", ST77XX_WHITE, 2, 10, 40, 0);
      measureState = WAIT_UNKNOWN_SAMPLE;
    }
  }

  // 6. Espera botão para medir amostra desconhecida
  if (measureState == WAIT_UNKNOWN_SAMPLE) {
    if (digitalRead(BUTTON_PIN) == HIGH) {
      measureState = MEASURE_UNKNOWN_SAMPLE;
      delay(300);
    }
  }

  // 7. Mede amostra desconhecida e calcula a concentração
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
      // MODIFICAÇÃO: Calcula a concentração a partir da absorbância
      float unknownAbsorbance = 0;
      if (blankReadings[i] > 0 && unknownRawResults[i] > 0) {
        unknownAbsorbance = -log10(unknownRawResults[i] / blankReadings[i]);
      }

      // Calcula a concentração usando a equação da reta invertida: x = (y - b) / a
      float concentration = 0;
      if (a_reg[i] != 0) { // Evita divisão por zero
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
    delay(100); // Pequeno delay para estabilizar o LED

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
        clearDisplay(); // Limpa a tela para cada leitura
        writeTextCenteredToDisplay("Reading " + String(i + 1) + " done", ST77XX_WHITE, 2, 0);
    }

     as7341.enableLED(false);

    // Calcular médias para cada canal selecionado
    for (int channel = 0; channel < numSelectedChannels; channel++) {
        uint16_t channelReadings[READINGS_PER_SAMPLE];
        for (int i = 0; i < READINGS_PER_SAMPLE; i++) {
            channelReadings[i] = readings[i][channel];
        }
        resultArray[channel] = calculateAverage(channelReadings, READINGS_PER_SAMPLE);
    }
    clearDisplay();
}

// MODIFICAÇÃO: A função agora calcula a regressão para X=Concentração, Y=Absorbância
void calcularRegressaoLinearPorCanal(float x_values[5], float y_values[5][MAX_CHANNELS], uint8_t numCanais, float* a_out, float* b_out) {
    for (uint8_t canal = 0; canal < numCanais; canal++) {
        float sumX = 0, sumY = 0, sumXY = 0, sumXX = 0;
        int n = 5;
        for (uint8_t i = 0; i < n; i++) {
            float x = x_values[i];
            float y = y_values[i][canal];
            sumX += x;
            sumY += y;
            sumXY += x * y;
            sumXX += x * x;
        }
        float denom = n * sumXX - sumX * sumX;
        if (denom != 0) {
            a_out[canal] = (n * sumXY - sumX * sumY) / denom; // 'a' (slope)
            b_out[canal] = (sumY * sumXX - sumX * sumXY) / denom; // 'b' (intercept)
        } else {
            a_out[canal] = 0;
            b_out[canal] = 0;
        }
    }
}

void mostrarRegressoesNaTela() {
    // MODIFICAÇÃO: Passa os valores de concentração e absorbância para a função de regressão
    calcularRegressaoLinearPorCanal(config.concentration_values, absorbanceAmostras, numSelectedChannels, a_reg, b_reg);
    
    for (uint8_t i = 0; i < numSelectedChannels; i++) {
        clearDisplay();
        String canalStr = "Ch " + String(selectedChannels[i]);
        String aStr = "a: " + String(a_reg[i], 4); // slope
        String bStr = "b: " + String(b_reg[i], 4); // intercept
        writeTextToDisplay(canalStr, ST77XX_WHITE, 2, 10, 20, 0);
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
    tft.fillScreen(ST77XX_BLACK); // Limpa a tela após o tempo
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
        // MODIFICAÇÃO: Armazena os valores de concentração
        config.concentration_values[0] = request->getParam("concentration1")->value().toFloat();
        config.concentration_values[1] = request->getParam("concentration2")->value().toFloat();
        config.concentration_values[2] = request->getParam("concentration3")->value().toFloat();
        config.concentration_values[3] = request->getParam("concentration4")->value().toFloat();
        config.concentration_values[4] = request->getParam("concentration5")->value().toFloat();

        // Parse dos canais selecionados
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

  int primeiroQuartil = size / 4;
  int ultimoQuartil = primeiroQuartil * 3;
  float soma = 0.0;
  int contagem = 0;
  for (int i = primeiroQuartil; i < ultimoQuartil; i++) {
    soma += readings[i];
    contagem++;
  }
  return contagem > 0 ? soma / contagem : 0;
}
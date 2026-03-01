#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <vector>
#include <SoftwareSerial.h>

// ESP8266 Pins for ILI9341 SPI
// Hardware SPI on ESP8266:
// SCLK: GPIO 14 (D5)
// MOSI: GPIO 13 (D7)
// MISO: GPIO 12 (D6) - Not used here but reserved
// CS:   GPIO 15 (D8)
// DC:   GPIO 2  (D4)
// RST:  GPIO 0  (D3)

#define TFT_CS   15
#define TFT_DC    2
#define TFT_RST   0

// Define SoftwareSerial Pins for Data Input
#define RX1_PIN 4  // D2
#define TX1_PIN 5  // D1 (Usually not needed for plotting)
#define BAUD_RATE 115200

//SoftwareSerial SerialData(RX1_PIN, TX1_PIN);
EspSoftwareSerial::UART SerialData(RX1_PIN, TX1_PIN);


// Structure to hold parsed data details
struct DataField {
    String label;
    float value;
    String unit;
    String rawSegment; 
};

class LGFX : public lgfx::LGFX_Device {
    lgfx::Panel_ILI9341 _panel_instance; 
    lgfx::Bus_SPI        _bus_instance;

public:
    LGFX(void) {
        auto cfg = _bus_instance.config();
        //cfg.spi_host = 0;           // ESP8266 hardware SPI
        cfg.spi_mode = 0;
        cfg.freq_write = 40000000;  // 40MHz (ESP8266 limit is stable here)
        cfg.freq_read  = 16000000;
        cfg.pin_sclk = 14;          // D5
        cfg.pin_mosi = 13;          // D7
        cfg.pin_miso = 12;          // D6
        cfg.pin_dc   = TFT_DC;
        _bus_instance.config(cfg);
        _panel_instance.setBus(&_bus_instance);

        auto panel_cfg = _panel_instance.config();
        panel_cfg.pin_cs           = TFT_CS;
        panel_cfg.pin_rst          = TFT_RST;
        panel_cfg.pin_busy         = -1;
        panel_cfg.panel_width      = 240;
        panel_cfg.panel_height     = 320;
        panel_cfg.offset_x         = 0;
        panel_cfg.offset_y         = 0;
        panel_cfg.offset_rotation  = 0;
        panel_cfg.readable         = false;
        panel_cfg.invert           = false;
        panel_cfg.rgb_order        = false;
        panel_cfg.bus_shared       = true;

        _panel_instance.config(panel_cfg);
        setPanel(&_panel_instance);
    }
};

LGFX tft;
LGFX_Sprite footer(&tft); 

const int SCREEN_W = 320;
const int SCREEN_H = 240;
const int FOOTER_H = 45; 
const int PLOT_H = SCREEN_H - FOOTER_H;

// Data handling
std::vector<float> dataPoints;
std::vector<DataField> currentFields;
String globalRawLine = "";
int xPos = 0;
int lastY = -1;
int selectedField = 0;

// Auto-scale tracking
float lastMinV = 0;
float lastMaxV = 0;

float fmap(float x, float in_min, float in_max, float out_min, float out_max) {
    if (in_max == in_min) return out_min + (out_max - out_min) / 2.0;
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

std::vector<DataField> parseSerialLine(String line) {
    std::vector<DataField> fields;
    globalRawLine = line; 
    
    String processingLine = line;
    if (processingLine.indexOf("RX1:") >= 0) {
        processingLine = processingLine.substring(processingLine.indexOf("RX1:") + 4);
    }
    processingLine.trim();

    int start = 0;
    while (start < (int)processingLine.length()) {
        int commaPos = processingLine.indexOf(',', start);
        String pair = (commaPos == -1) ? processingLine.substring(start) : processingLine.substring(start, commaPos);
        
        DataField df;
        df.rawSegment = pair; 
        pair.trim();

        if (pair.length() > 0) {
            int colonPos = pair.indexOf(':');
            if (colonPos != -1) {
                df.label = pair.substring(0, colonPos);
                df.label.trim();
                String valPart = pair.substring(colonPos + 1);
                valPart.trim();
                char* endPtr;
                df.value = strtof(valPart.c_str(), &endPtr);
                df.unit = String(endPtr);
                df.unit.trim();
            } else {
                char* endPtr;
                df.value = strtof(pair.c_str(), &endPtr);
                df.label = "CH" + String(fields.size());
                df.unit = String(endPtr);
                df.unit.trim();
            }
            fields.push_back(df);
        }

        if (commaPos == -1) break;
        start = commaPos + 1;
    }
    return fields;
}

void drawYScale(float minV, float maxV) {
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.setTextSize(1);
    tft.setTextDatum(top_right);
    tft.drawString(String(maxV, 2), SCREEN_W - 5, 5);
    tft.setTextDatum(bottom_right);
    tft.drawString(String(minV, 2), SCREEN_W - 5, PLOT_H - 5);
}

void updatePlotter() {
    if (currentFields.empty() || selectedField >= (int)currentFields.size()) return;
    
    float val = currentFields[selectedField].value;
    dataPoints[xPos] = val;

    float minV = dataPoints[0];
    float maxV = dataPoints[0];
    for (float v : dataPoints) {
        if (v < minV) minV = v;
        if (v > maxV) maxV = v;
    }

    float range = maxV - minV;
    if (range < 0.0001) range = 1.0;
    float padMin = minV - (range * 0.1);
    float padMax = maxV + (range * 0.1);

    bool scaleChanged = (abs(padMin - lastMinV) > (range * 0.05) || abs(padMax - lastMaxV) > (range * 0.05));

    tft.startWrite();
    if (scaleChanged) {
        tft.fillRect(0, 0, SCREEN_W, PLOT_H, TFT_BLACK);
        int prevY = -1;
        for (int i = 0; i < SCREEN_W; i++) {
            int drawY = (int)fmap(dataPoints[i], padMin, padMax, PLOT_H - 10, 10);
            if (i > 0 && prevY != -1) {
                tft.drawLine(i - 1, prevY, i, drawY, TFT_GREEN);
            }
            prevY = drawY;
            if (i == xPos) lastY = drawY;
           // if ((i & 31) == 0) yield(); // yield every 32 iterations
        }
        lastMinV = padMin;
        lastMaxV = padMax;
        drawYScale(padMin, padMax);
    } else {
        tft.drawFastVLine((xPos + 1) % SCREEN_W, 0, PLOT_H, TFT_BLACK);
        tft.drawFastVLine((xPos + 2) % SCREEN_W, 0, PLOT_H, TFT_CYAN);

        int y = (int)fmap(val, padMin, padMax, PLOT_H - 10, 10);
        if (xPos > 0 && lastY != -1) {
            tft.drawLine(xPos - 1, lastY, xPos, y, TFT_GREEN);
        } else {
            tft.drawPixel(xPos, y, TFT_GREEN);
        }
        lastY = y;
        
        if (xPos % 50 == 0) drawYScale(padMin, padMax);
    }
    tft.endWrite();

    footer.fillSprite(tft.color565(30, 30, 35));
    
    int rawY = 10;
    footer.setCursor(5, rawY);
    
    if (globalRawLine.indexOf("RX1:") >= 0) {
        footer.setTextColor(TFT_DARKGREY);
        footer.print("RX1: ");
    }

    for (int i = 0; i < (int)currentFields.size(); i++) {
        if (i == selectedField) {
            footer.setTextColor(TFT_YELLOW); 
            footer.print(currentFields[i].rawSegment);
        } else {
            footer.setTextColor(TFT_LIGHTGREY);
            footer.print(currentFields[i].rawSegment);
        }
        
        if (i < (int)currentFields.size() - 1) {
            footer.setTextColor(TFT_DARKGREY);
            footer.print(",");
        }
    }
    
    footer.pushSprite(0, PLOT_H);
    xPos = (xPos + 1) % SCREEN_W;
}

void showWelcomeScreen() {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_CYAN);
    tft.setTextSize(2);
    tft.setCursor(10, 10);
    tft.println("ESP8266 SERIAL PLOTTER");
    
    tft.setTextSize(1);
    tft.setTextColor(TFT_WHITE);
    tft.setCursor(10, 45);
    tft.println("------------------------------------");
    
    tft.setTextColor(TFT_YELLOW);
    tft.println("HARDWARE CONFIG:");
    tft.setTextColor(TFT_WHITE);
    tft.printf(" - Interface: SwSerial\n");
    tft.printf(" - RX Pin:    GPIO %d\n", RX1_PIN);
    tft.printf(" - Baudrate:  %d bps\n", BAUD_RATE);
    
    tft.setCursor(10, 210);
    tft.setTextColor(TFT_DARKGREY);
    tft.println("Starting...");
    
    delay(3000);
}

void setup() {
    Serial.begin(115200); // Standard Debug
    SerialData.begin(BAUD_RATE); // Plotter Data
    
    // GPIO 16 (D0) as the channel cycle button on many ESP8266 boards
    pinMode(16, INPUT_PULLUP);
    
    tft.init();
    tft.setRotation(1);
    tft.fillScreen(TFT_BLACK);
    showWelcomeScreen();
    
    if (!footer.createSprite(SCREEN_W, FOOTER_H)) {
        Serial.println("Sprite creation failed! Out of memory.");
    }
    dataPoints.assign(SCREEN_W, 0.0f);
}

void loop() {
    // Cycle channel with GPIO 16 (D0)
    if (digitalRead(16) == LOW) {
        if (!currentFields.empty()) {
            selectedField = (selectedField + 1) % currentFields.size();
            tft.fillScreen(TFT_BLACK);
            dataPoints.assign(SCREEN_W, 0.0f);
            xPos = 0;
            lastY = -1;
            lastMinV = 0;
            lastMaxV = 0;
        }
        delay(300);
    }

    if (SerialData.available()) {
        String input = SerialData.readStringUntil('\n');
        input.trim();
        if (input.length() > 0) {
            std::vector<DataField> fields = parseSerialLine(input);
            if (!fields.empty()) {
                currentFields = fields;
                if (selectedField >= (int)currentFields.size()) selectedField = 0;
                updatePlotter();
            }
        }
    }
    yield();
    //delay(100); //bug with smaller delay it crashes
}

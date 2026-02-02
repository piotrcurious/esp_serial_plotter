#include <ESPvgax.h>
#include <SoftwareSerial.h>

// VGA Configuration: Red: D0, Green: D8, Blue: D3 (Standard ESPvgax wiring)
ESPvgax vga;

// Software Serial Pins
#define SOFT_RX D1
#define SOFT_TX D2
#define BAUD_RATE 9600 // High bauds like 115200 may cause VGA glitches

SoftwareSerial swSerial(SOFT_RX, SOFT_TX);

// Resolution for ESPvgax
const int SCREEN_W = 256;
const int SCREEN_H = 224;
const int FOOTER_H = 20;
const int PLOT_H = SCREEN_H - FOOTER_H;

// Data handling - Fixed array to save RAM
float dataPoints[256]; 
int xPos = 0;
int lastY = -1;
int selectedField = 0;
int totalFields = 0;

float currentVal = 0;
String currentLabel = "CH0";

// Auto-scale
float lastMinV = 0;
float lastMaxV = 0;

float fmap(float x, float in_min, float in_max, float out_min, float out_max) {
    if (in_max == in_min) return out_min + (out_max - out_min) / 2.0;
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

void parseAndStore(String line) {
    // Simplified parser for memory efficiency
    int commaCount = 0;
    int start = 0;
    
    // Find the value for the selected channel
    for (int i = 0; i <= selectedField; i++) {
        int commaPos = line.indexOf(',', start);
        if (i == selectedField) {
            String segment = (commaPos == -1) ? line.substring(start) : line.substring(start, commaPos);
            segment.trim();
            // Handle "label:value"
            int colonPos = segment.indexOf(':');
            if (colonPos != -1) {
                currentLabel = segment.substring(0, colonPos);
                currentVal = segment.substring(colonPos + 1).toFloat();
            } else {
                currentLabel = "CH" + String(i);
                currentVal = segment.toFloat();
            }
        }
        if (commaPos == -1) {
            totalFields = i + 1;
            break;
        }
        start = commaPos + 1;
    }
}

void updatePlotter() {
    dataPoints[xPos] = currentVal;

    // Find min/max for scaling
    float minV = 1000000;
    float maxV = -1000000;
    for (int i = 0; i < SCREEN_W; i++) {
        if (dataPoints[i] < minV) minV = dataPoints[i];
        if (dataPoints[i] > maxV) maxV = dataPoints[i];
    }

    if (maxV - minV < 0.1) { minV -= 1; maxV += 1; }

    // Clear vertical sliver (cursor)
    int nextX = (xPos + 1) % SCREEN_W;
    for(int y=0; y<PLOT_H; y++) vga.putpixel(nextX, y, 0); // Black out

    int y = (int)fmap(currentVal, minV, maxV, PLOT_H - 5, 5);
    
    if (xPos > 0 && lastY != -1) {
        // Simple vertical line to bridge gaps (ESPvgax has no drawLine)
        int startY = (y < lastY) ? y : lastY;
        int endY = (y < lastY) ? lastY : y;
        for(int i = startY; i <= endY; i++) vga.putpixel(xPos, i, 2); // 2 = Green
    } else {
        vga.putpixel(xPos, y, 2);
    }
    
    lastY = y;

    // Update Text Footer area (Very basic text support in ESPvgax)
    // Clear footer
    for(int i=0; i<SCREEN_W; i++) {
        for(int j=PLOT_H; j<SCREEN_H; j++) vga.putpixel(i, j, 1); // 1 = Blue/Grey
    }
    
    vga.print((char*)currentLabel.c_str(), 5, PLOT_H + 5, 3); // 3 = White
    
    xPos = (xPos + 1) % SCREEN_W;
}

void setup() {
    vga.begin();
    swSerial.begin(BAUD_RATE);
    pinMode(D3, INPUT_PULLUP); // Flash button on most NodeMCUs is D3/GPIO0
    
    for(int i=0; i<256; i++) dataPoints[i] = 0;
    
    vga.clear(0);
    vga.print((char*)"ESP8266 VGA PLOTTER", 40, 100, 2);
    delay(2000);
    vga.clear(0);
}

void loop() {
    // Cycle Channels with Flash Button
    if (digitalRead(D3) == LOW) {
        selectedField = (selectedField + 1) % totalFields;
        vga.clear(0);
        xPos = 0;
        delay(300);
    }

    if (swSerial.available()) {
        String input = swSerial.readStringUntil('\n');
        input.trim();
        if (input.length() > 0) {
            parseAndStore(input);
            updatePlotter();
        }
    }
}

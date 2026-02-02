#include <Arduino.h> #include <ESPVGAX2.h> // ESPVGAX2 (or ESPVGAX) - install from Library Manager / GitHub #include <SoftwareSerial.h> #include <vector>

// Pins (GPIO numbers) const int RX1_PIN = 4; // D2 on many NodeMCU boards const int TX1_PIN = 5; // D1 -- avoid GPIO2 (boot strapping) for TX const long BAUD_RATE = 115200;

// Display and plotting geometry (ESPVGAX2 supports 320x240) const int SCREEN_W = 320; const int SCREEN_H = 240; const int FOOTER_H = 45; const int PLOT_H = SCREEN_H - FOOTER_H;

// Color palette (ESPVGAX2 uses small integer color values). You can remap if needed. #define COL_BG        0 #define COL_PLOT      3 #define COL_CURSOR    7 #define COL_TEXT      15 #define COL_HIGHLIGHT 14 #define COL_GUIDE     8

struct DataField { String label; float value; String unit; String rawSegment; };

// Globals ESPVGAX2 vga;               // main vga object SoftwareSerial ss(RX1_PIN, TX1_PIN); // rx, tx (SoftwareSerial is compatible with ESP8266 core)

std::vector<float> dataPoints; std::vector<DataField> currentFields; String globalRawLine = ""; int xPos = 0; int lastY = -1; int selectedField = 0;

float lastMinV = 0; float lastMaxV = 0;

float fmap(float x, float in_min, float in_max, float out_min, float out_max) { if (in_max == in_min) return out_min + (out_max - out_min) / 2.0; return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min; }

// Simple Bresenham line drawer using vga.putpixel static void drawLineBresenham(int x0, int y0, int x1, int y1, uint8_t col) { int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1; int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1; int err = dx + dy, e2; while (true) { vga.putpixel(x0, y0, col); if (x0 == x1 && y0 == y1) break; e2 = 2 * err; if (e2 >= dy) { err += dy; x0 += sx; } if (e2 <= dx) { err += dx; y0 += sy; } } }

std::vector<DataField> parseSerialLine(String line) { std::vector<DataField> fields; globalRawLine = line; String processingLine = line; if (processingLine.indexOf("RX1:") >= 0) { processingLine = processingLine.substring(processingLine.indexOf("RX1:") + 4); } processingLine.trim();

int start = 0; while (start < processingLine.length()) { int commaPos = processingLine.indexOf(',', start); String pair = (commaPos == -1) ? processingLine.substring(start) : processingLine.substring(start, commaPos); DataField df; df.rawSegment = pair; pair.trim(); if (pair.length() > 0) { int colonPos = pair.indexOf(':'); if (colonPos != -1) { df.label = pair.substring(0, colonPos); df.label.trim(); String valPart = pair.substring(colonPos + 1); valPart.trim(); char* endPtr; df.value = strtof(valPart.c_str(), &endPtr); df.unit = String(endPtr); df.unit.trim(); } else { char* endPtr; df.value = strtof(pair.c_str(), &endPtr); df.label = String("CH") + String(fields.size()); df.unit = String(endPtr); df.unit.trim(); } fields.push_back(df); } if (commaPos == -1) break; start = commaPos + 1; } return fields; }

void drawYScale(float minV, float maxV) { // draw text at right side of plot area (reserve a few px) char buf[24]; sprintf(buf, "%.2f", maxV); vga.print( SCREEN_W - 60, 4, String(buf)); sprintf(buf, "%.2f", minV); vga.print( SCREEN_W - 60, PLOT_H - 12, String(buf)); }

void updatePlotter() { if (currentFields.empty() || selectedField >= currentFields.size()) return; float val = currentFields[selectedField].value; dataPoints[xPos] = val;

// Min/max excluding NaN float minV = FLT_MAX; float maxV = -FLT_MAX; bool hasValid = false; for (float v : dataPoints) { if (!isnan(v)) { if (v < minV) minV = v; if (v > maxV) maxV = v; hasValid = true; } } if (!hasValid) { minV = 0.0; maxV = 1.0; } float range = maxV - minV; if (fabs(range) < 0.0001f) range = 1.0f;

float padMin = minV - (range * 0.1f); float padMax = maxV + (range * 0.1f);

bool scaleChanged = (fabs(padMin - lastMinV) > (range * 0.1f) || fabs(padMax - lastMaxV) > (range * 0.1f));

if (scaleChanged) { // full redraw vga.clear(COL_BG); int prevY = -1; for (int i = 0; i < SCREEN_W; i++) { if (isnan(dataPoints[i])) { prevY = -1; continue; } int drawY = (int)fmap(dataPoints[i], padMin, padMax, PLOT_H - 10, 10); if (prevY != -1) drawLineBresenham(i - 1, prevY, i, drawY, COL_PLOT); prevY = drawY; if (i == xPos) lastY = drawY; } lastMinV = padMin; lastMaxV = padMax; drawYScale(padMin, padMax); } else { // optimized sweeping update int nextX = (xPos + 1) % SCREEN_W; // clear vertical sliver for (int y = 0; y < PLOT_H; y++) vga.putpixel(nextX, y, COL_BG); int next2 = (xPos + 2) % SCREEN_W; for (int y = 0; y < PLOT_H; y++) vga.putpixel(next2, y, COL_BG); int cursorX = (xPos + 3) % SCREEN_W; for (int y = 0; y < PLOT_H; y++) vga.putpixel(cursorX, y, COL_CURSOR);

if (!isnan(val)) {
  int y = (int)fmap(val, padMin, padMax, PLOT_H - 10, 10);
  if (xPos > 0 && lastY != -1) drawLineBresenham(xPos - 1, lastY, xPos, y, COL_PLOT);
  else vga.putpixel(xPos, y, COL_PLOT);
  lastY = y;
} else {
  lastY = -1;
}

if (xPos % 50 == 0) drawYScale(padMin, padMax);

}

// Footer: draw a filled rect and print raw line with highlight // Simple filled rectangle for (int yy = PLOT_H; yy < SCREEN_H; yy++) { for (int xx = 0; xx < SCREEN_W; xx++) vga.putpixel(xx, yy, COL_BG); }

// Row 1: label and value DataField selected = currentFields[selectedField]; vga.print(2, PLOT_H + 2, selected.label + ": " + String(selected.value, 4) + " " + selected.unit);

// Row 2: raw segments int cursor = 0; int colX = 2; if (globalRawLine.indexOf("RX1:") >= 0) { vga.print(colX, PLOT_H + 18, String("RX1: ")); colX += 40; } for (int i = 0; i < currentFields.size(); i++) { if (i == selectedField) vga.print(colX, PLOT_H + 18, currentFields[i].rawSegment); else vga.print(colX, PLOT_H + 18, currentFields[i].rawSegment); colX += currentFields[i].rawSegment.length() * 6 + 4; // rough advance if (i < currentFields.size() - 1) { vga.print(colX, PLOT_H + 18, ","); colX += 6; } }

xPos = (xPos + 1) % SCREEN_W; }

void showWelcomeScreen() { vga.clear(COL_BG); vga.print(10, 10, "ESP8266 SERIAL PLOTTER (ESPVGAX)"); vga.print(10, 30, "Hardware:\n - Interface: SoftwareSerial\n - RX Pin: " + String(RX1_PIN) + "\n - TX Pin: " + String(TX1_PIN) + "\n - Baudrate: " + String(BAUD_RATE)); vga.print(10, 110, "Send data as CSV (e.g. '1.2,4.5,9')"); vga.print(10, 140, "Press BOOT (GPIO0) to cycle channels"); delay(3000); }

void setup() { Serial.begin(115200); ss.begin(BAUD_RATE); pinMode(0, INPUT_PULLUP); // BOOT button

vga.begin(); vga.clear(COL_BG); showWelcomeScreen();

dataPoints.assign(SCREEN_W, NAN); }

void loop() { if (digitalRead(0) == LOW) { if (!currentFields.empty()) { selectedField = (selectedField + 1) % currentFields.size(); vga.clear(COL_BG); dataPoints.assign(SCREEN_W, 0.0f); xPos = 0; lastY = -1; lastMinV = 0; lastMaxV = 0; } delay(300); }

if (ss.available()) { String input = ss.readStringUntil('\n'); input.trim(); if (input.length() > 0) { std::vector<DataField> fields = parseSerialLine(input); if (!fields.empty()) { currentFields = fields; if (selectedField >= currentFields.size()) selectedField = 0; updatePlotter(); } } } }

#include <ESPVGAX.h>
#include <espvgax_gfx.h>
#include <SoftwareSerial.h>

// Software Serial Pins for ESP8266
// Note: D7 (GPIO13) is used by ESPVGAX for VGA, so avoid it for serial
#define RX_PIN D6  // GPIO12
#define TX_PIN D5  // GPIO14
#define BAUD_RATE 115200

// Button pin (avoid D0, D1, D2, D4, D5, D7 which are used by VGA)
#define BUTTON_PIN D8  // GPIO15

// VGA resolution for ESPVGAX
#define SCREEN_W 512
#define SCREEN_H 480
#define FOOTER_H 30
#define PLOT_H (SCREEN_H - FOOTER_H)

// Maximum number of data fields
#define MAX_FIELDS 10

// Structure to hold parsed data details
struct DataField {
  String label;
  float value;
  String unit;
  String rawSegment;
};

ESPVGAX vga;
SoftwareSerial mySerial(RX_PIN, TX_PIN); // RX, TX

// Data handling
float dataPoints[SCREEN_W];
DataField currentFields[MAX_FIELDS];
int fieldCount = 0;
String globalRawLine = "";
int xPos = 0;
int lastY = -1;
int selectedField = 0;

// Auto-scale tracking
float lastMinV = 0;
float lastMaxV = 0;

// ESPVGAX uses 1bpp (black/white)
#define COLOR_BLACK 0
#define COLOR_WHITE 1

float fmap(float x, float in_min, float in_max, float out_min, float out_max) {
  if (in_max == in_min) return out_min + (out_max - out_min) / 2.0;
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

void parseSerialLine(String line) {
  globalRawLine = line;
  fieldCount = 0;
  
  String processingLine = line;
  if (processingLine.indexOf("RX1:") >= 0) {
    processingLine = processingLine.substring(processingLine.indexOf("RX1:") + 4);
  }
  processingLine.trim();
  
  int start = 0;
  while (start < processingLine.length() && fieldCount < MAX_FIELDS) {
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
        df.value = valPart.toFloat();
        // Extract unit
        int i = 0;
        while (i < valPart.length() && (isDigit(valPart[i]) || valPart[i] == '.' || valPart[i] == '-')) i++;
        df.unit = valPart.substring(i);
        df.unit.trim();
      } else {
        df.value = pair.toFloat();
        df.label = "CH" + String(fieldCount);
        // Extract unit
        int i = 0;
        while (i < pair.length() && (isDigit(pair[i]) || pair[i] == '.' || pair[i] == '-')) i++;
        df.unit = pair.substring(i);
        df.unit.trim();
      }
      currentFields[fieldCount] = df;
      fieldCount++;
    }
    
    if (commaPos == -1) break;
    start = commaPos + 1;
  }
}

void drawYScale(float minV, float maxV) {
  // Draw scale values on right side
  char buffer[16];
  
  // Top value
  dtostrf(maxV, 6, 2, buffer);
  vga.printStr(SCREEN_W - 50, 5, buffer, COLOR_WHITE, COLOR_BLACK);
  
  // Bottom value
  dtostrf(minV, 6, 2, buffer);
  vga.printStr(SCREEN_W - 50, PLOT_H - 15, buffer, COLOR_WHITE, COLOR_BLACK);
}

void updatePlotter() {
  if (fieldCount == 0 || selectedField >= fieldCount) return;
  
  float val = currentFields[selectedField].value;
  dataPoints[xPos] = val;
  
  // Calculate min/max excluding invalid values
  float minV = 3.4028235E38;
  float maxV = -3.4028235E38;
  bool hasValidData = false;
  
  for (int i = 0; i < SCREEN_W; i++) {
    float v = dataPoints[i];
    if (!isnan(v)) {
      if (v < minV) minV = v;
      if (v > maxV) maxV = v;
      hasValidData = true;
    }
  }
  
  // Default range if no valid data
  if (!hasValidData) { minV = 0.0; maxV = 1.0; }
  float range = maxV - minV;
  if (range < 0.0001) range = 1.0;
  
  // Add 10% padding
  float padMin = minV - (range * 0.1);
  float padMax = maxV + (range * 0.1);
  
  // Check if scale changed significantly
  bool scaleChanged = (abs(padMin - lastMinV) > (range * 0.1) || abs(padMax - lastMaxV) > (range * 0.1));
  
  if (scaleChanged) {
    // Redraw entire plot
    vga.clear(COLOR_BLACK);
    int prevY = -1;
    
    for (int i = 0; i < SCREEN_W; i++) {
      if (isnan(dataPoints[i])) {
        prevY = -1;
        continue;
      }
      int drawY = (int)fmap(dataPoints[i], padMin, padMax, PLOT_H - 10, 10);
      if (prevY != -1 && i > 0) {
        vga.drawLine(i - 1, prevY, i, drawY, COLOR_WHITE);
      } else {
        vga.putpixel(i, drawY, COLOR_WHITE);
      }
      prevY = drawY;
      if (i == xPos) lastY = drawY;
    }
    
    lastMinV = padMin;
    lastMaxV = padMax;
    drawYScale(padMin, padMax);
  } else {
    // Optimized update - clear next column and draw cursor
    int nextX = (xPos + 1) % SCREEN_W;
    
    // Clear ahead of cursor
    for (int y = 0; y < PLOT_H; y++) {
      vga.putpixel(nextX, y, COLOR_BLACK);
      vga.putpixel((xPos + 2) % SCREEN_W, y, COLOR_BLACK);
    }
    
    // Draw cursor line (inverted pixels for visibility)
    for (int y = 0; y < PLOT_H; y += 4) {
      vga.putpixel((xPos + 3) % SCREEN_W, y, COLOR_WHITE);
    }
    
    if (!isnan(val)) {
      int y = (int)fmap(val, padMin, padMax, PLOT_H - 10, 10);
      if (xPos > 0 && lastY != -1) {
        vga.drawLine(xPos - 1, lastY, xPos, y, COLOR_WHITE);
      } else {
        vga.putpixel(xPos, y, COLOR_WHITE);
      }
      lastY = y;
    } else {
      lastY = -1;
    }
    
    // Redraw scale periodically
    if (xPos % 50 == 0) drawYScale(padMin, padMax);
  }
  
  // Draw footer with current field info
  int footerY = PLOT_H + 5;
  
  // Clear footer area
  vga.fillRect(0, PLOT_H, SCREEN_W, FOOTER_H, COLOR_BLACK);
  
  // Draw horizontal line separator
  vga.drawLine(0, PLOT_H, SCREEN_W, PLOT_H, COLOR_WHITE);
  
  // Show selected field value
  DataField selected = currentFields[selectedField];
  
  // Format: "Label: Value Unit"
  String line1 = selected.label + ": " + String(selected.value, 4) + " " + selected.unit;
  vga.printStr(5, footerY, line1.c_str(), COLOR_WHITE, COLOR_BLACK);
  
  // Show raw line on second line
  String line2 = "Data: " + globalRawLine;
  if (line2.length() > 80) line2 = line2.substring(0, 80); // Truncate if too long
  vga.printStr(5, footerY + 10, line2.c_str(), COLOR_WHITE, COLOR_BLACK);
  
  // Show field selection indicator
  String line3 = "Field " + String(selectedField + 1) + "/" + String(fieldCount) + " (Press button to cycle)";
  vga.printStr(5, footerY + 20, line3.c_str(), COLOR_WHITE, COLOR_BLACK);
  
  xPos = (xPos + 1) % SCREEN_W;
}

void showWelcomeScreen() {
  vga.clear(COLOR_BLACK);
  
  vga.printStr(150, 50, "ESP8266 VGA PLOTTER", COLOR_WHITE, COLOR_BLACK);
  vga.drawLine(140, 70, 372, 70, COLOR_WHITE);
  
  vga.printStr(100, 100, "HARDWARE CONFIG:", COLOR_WHITE, COLOR_BLACK);
  vga.printStr(100, 120, "Interface: Software Serial", COLOR_WHITE, COLOR_BLACK);
  
  char buffer[50];
  sprintf(buffer, "RX Pin: D6 (GPIO%d)", RX_PIN);
  vga.printStr(100, 140, buffer, COLOR_WHITE, COLOR_BLACK);
  
  sprintf(buffer, "TX Pin: D5 (GPIO%d)", TX_PIN);
  vga.printStr(100, 160, buffer, COLOR_WHITE, COLOR_BLACK);
  
  sprintf(buffer, "Baudrate: %d bps", BAUD_RATE);
  vga.printStr(100, 180, buffer, COLOR_WHITE, COLOR_BLACK);
  
  vga.printStr(100, 200, "Format: 8-N-1", COLOR_WHITE, COLOR_BLACK);
  
  vga.printStr(100, 240, "INSTRUCTIONS:", COLOR_WHITE, COLOR_BLACK);
  vga.printStr(100, 260, "- Send data as CSV (e.g. '1.2, 4.5, 9')", COLOR_WHITE, COLOR_BLACK);
  vga.printStr(100, 280, "- Or labeled: 'Temp:25.3C, Hum:60%'", COLOR_WHITE, COLOR_BLACK);
  vga.printStr(100, 300, "- Press button (D8) to cycle channels", COLOR_WHITE, COLOR_BLACK);
  vga.printStr(100, 320, "- Plotter autoscales on value changes", COLOR_WHITE, COLOR_BLACK);
  
  vga.printStr(150, 380, "VGA Resolution: 512x480 pixels", COLOR_WHITE, COLOR_BLACK);
  
  vga.printStr(180, 430, "Starting in 5 seconds...", COLOR_WHITE, COLOR_BLACK);
  
  delay(5000);
  vga.clear(COLOR_BLACK);
}

void setup() {
  // Initialize hardware serial for debugging
  Serial.begin(115200);
  Serial.println("ESP8266 VGA Plotter Starting...");
  
  // Initialize software serial for data input
  mySerial.begin(BAUD_RATE);
  
  // Configure button with internal pullup
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  
  // Initialize VGA - IMPORTANT: This must be done before any VGA operations
  vga.begin();
  vga.clear(COLOR_BLACK);
  
  Serial.println("VGA initialized");
  
  showWelcomeScreen();
  
  // Initialize data points array with NaN
  for (int i = 0; i < SCREEN_W; i++) {
    dataPoints[i] = NAN;
  }
  
  Serial.println("Ready to receive data");
}

void loop() {
  // Button handling with debounce
  static unsigned long lastButtonPress = 0;
  if (digitalRead(BUTTON_PIN) == LOW && millis() - lastButtonPress > 300) {
    if (fieldCount > 0) {
      selectedField = (selectedField + 1) % fieldCount;
      
      // Clear screen for new field
      vga.clear(COLOR_BLACK);
      
      // Reset data points
      for (int i = 0; i < SCREEN_W; i++) {
        dataPoints[i] = NAN;
      }
      
      xPos = 0;
      lastY = -1;
      lastMinV = 0;
      lastMaxV = 0;
      
      Serial.print("Switched to field: ");
      Serial.println(selectedField);
    }
    lastButtonPress = millis();
  }
  
  // Read from software serial (primary data source)
  if (mySerial.available()) {
    String input = mySerial.readStringUntil('\n');
    input.trim();
    if (input.length() > 0) {
      parseSerialLine(input);
      if (fieldCount > 0) {
        if (selectedField >= fieldCount) selectedField = 0;
        updatePlotter();
      }
    }
  }
  
  // Also check hardware serial for testing/debugging
  if (Serial.available()) {
    String input = Serial.readStringUntil('\n');
    input.trim();
    if (input.length() > 0) {
      Serial.print("Received: ");
      Serial.println(input);
      parseSerialLine(input);
      if (fieldCount > 0) {
        if (selectedField >= fieldCount) selectedField = 0;
        updatePlotter();
      }
    }
  }
  
  // Small yield to allow ESP8266 background tasks
  yield();
}

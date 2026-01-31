#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <vector>

// Updated for ST7789 320x240
class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ST7789 _panel_instance;
  lgfx::Bus_SPI      _bus_instance;
public:
  LGFX(void) {
    auto cfg = _bus_instance.config();
    cfg.spi_host = VSPI_HOST;
    cfg.spi_mode = 3; // ST7789 often prefers mode 3
    cfg.pin_sclk = 18;
    cfg.pin_mosi = 23;
    _bus_instance.config(cfg);
    _panel_instance.config_bus(&_bus_instance);

    auto p_cfg = _panel_instance.config();
    p_cfg.panel_width  = 240;
    p_cfg.panel_height = 320;
    p_cfg.pin_cs    = 5;
    p_cfg.pin_dc    = 2;
    p_cfg.pin_rst   = 4;
    _panel_instance.config(p_cfg);
    setPanel(&_panel_instance);
  }
};

LGFX tft;
// We only use a tiny sprite for the text area to prevent flicker at the bottom
LGFX_Sprite footer(&tft); 

const int SCREEN_W = 320;
const int SCREEN_H = 240;
const int FOOTER_H = 24;
const int PLOT_H = SCREEN_H - FOOTER_H;

std::vector<float> dataPoints;
int xPos = 0; // Current horizontal draw position
int selectedField = 0;
int maxFields = 1;
float currentVal = 0;
String lastLine = "";

void setup() {
  Serial.begin(115200);
  pinMode(0, INPUT_PULLUP);
  
  tft.init();
  tft.setRotation(1); // Landscape 320x240
  tft.fillScreen(TFT_BLACK);
  
  // Create a small sprite just for the footer (approx 15KB - fits easily)
  footer.createSprite(SCREEN_W, FOOTER_H);
  dataPoints.resize(SCREEN_W, 0);
}

void loop() {
  if (digitalRead(0) == LOW) {
    selectedField++;
    if (selectedField >= maxFields) selectedField = 0;
    delay(200);
  }

  if (Serial.available()) {
    String input = Serial.readStringUntil('\n');
    lastLine = input;
    
    // Parser
    std::vector<float> fields;
    String currentNum = "";
    for (char c : input) {
      if (isdigit(c) || c == '.' || c == '-') currentNum += c;
      else if (currentNum.length() > 0) {
        fields.push_back(currentNum.toFloat());
        currentNum = "";
      }
    }
    if (currentNum.length() > 0) fields.push_back(currentNum.toFloat());
    
    if (!fields.empty()) {
      maxFields = fields.size();
      if (selectedField >= maxFields) selectedField = 0;
      currentVal = fields[selectedField];

      updatePlotter();
    }
  }
}

void updatePlotter() {
  // 1. Calculate Auto-scale
  dataPoints[xPos] = currentVal;
  float minV = dataPoints[0], maxV = dataPoints[0];
  for (float v : dataPoints) {
    if (v < minV) minV = v;
    if (v > maxV) maxV = v;
  }
  if (maxV == minV) maxV += 1.0;

  // 2. Draw Plotter using "Wipe" method
  // Clear the vertical column before drawing new data
  tft.startWrite();
  tft.drawFastVLine(xPos, 0, PLOT_H, TFT_BLACK); 
  
  // Map values
  int y = map(currentVal * 100, minV * 100, maxV * 100, PLOT_H - 2, 2);
  tft.drawPixel(xPos, y, TFT_GREEN);
  
  // Optional: Draw line connecting to previous point
  static int lastY = 0;
  tft.drawLine(xPos - 1, lastY, xPos, y, TFT_GREEN);
  lastY = y;
  
  tft.endWrite();

  // 3. Update Footer Sprite
  footer.clear();
  footer.setTextSize(1);
  footer.setTextColor(TFT_WHITE, TFT_BLACK);
  footer.drawString(lastLine.substring(0, 35), 2, 5);

  String info = " F:" + String(selectedField) + " V:" + String(currentVal) + " ";
  int tw = footer.textWidth(info);
  footer.fillRect(SCREEN_W - tw, 0, tw, FOOTER_H, TFT_WHITE);
  footer.setTextColor(TFT_BLACK);
  footer.drawString(info, SCREEN_W - tw, 5);
  footer.pushSprite(0, PLOT_H);

  // 4. Increment X and wrap around
  xPos++;
  if (xPos >= SCREEN_W) {
    xPos = 0;
    tft.fillScreen(TFT_BLACK); // Clear for next pass
  }
}

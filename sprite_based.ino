#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <vector>

// Configure your LGFX class here based on your display model
class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ST7789 _panel_instance; // Example for ST7789
  lgfx::Bus_SPI      _bus_instance;
public:
  LGFX(void) {
    auto cfg = _bus_instance.config();
    cfg.spi_host = VSPI_HOST;
    cfg.spi_mode = 0;
    cfg.pin_sclk = 18;
    cfg.pin_mosi = 23;
    _bus_instance.config(cfg);
    _panel_instance.config_bus(&_bus_instance);
    
    auto p_cfg = _panel_instance.config();
    p_cfg.pin_cs    = 5;
    p_cfg.pin_dc    = 2;
    p_cfg.pin_rst   = 4;
    _panel_instance.config(p_cfg);
    setPanel(&_panel_instance);
  }
};

LGFX tft;
LGFX_Sprite canvas(&tft);

// Variables
std::vector<float> dataPoints;
const int BUTTON_PIN = 0;
int selectedField = 0;
int maxFields = 1;
String lastLine = "";
float currentVal = 0;

void setup() {
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  
  tft.init();
  tft.setRotation(1);
  canvas.createSprite(tft.width(), tft.height());
  dataPoints.resize(tft.width(), 0);
}

void loop() {
  // 1. Handle Button Press (Field Selection)
  if (digitalRead(BUTTON_PIN) == LOW) {
    selectedField++;
    if (selectedField >= maxFields) selectedField = 0;
    delay(200); // Debounce
  }

  // 2. Parse Serial Input
  if (Serial.available()) {
    String input = Serial.readStringUntil('\n');
    lastLine = input;
    
    // Filter and parse numbers (comma or space separated)
    std::vector<float> fields;
    String currentNum = "";
    for (char c : input) {
      if (isdigit(c) || c == '.' || c == '-') {
        currentNum += c;
      } else if (currentNum.length() > 0) {
        fields.push_back(currentNum.toFloat());
        currentNum = "";
      }
    }
    if (currentNum.length() > 0) fields.push_back(currentNum.toFloat());
    
    maxFields = fields.size();
    if (maxFields > 0) {
      if (selectedField >= maxFields) selectedField = 0;
      currentVal = fields[selectedField];
      
      // Shift buffer
      for (int i = 0; i < dataPoints.size() - 1; i++) {
        dataPoints[i] = dataPoints[i + 1];
      }
      dataPoints[dataPoints.size() - 1] = currentVal;
    }
  }

  drawPlotter();
}

void drawPlotter() {
  canvas.clear();
  
  // Find min/max for auto-scaling
  float minVal = dataPoints[0];
  float maxVal = dataPoints[0];
  for (float v : dataPoints) {
    if (v < minVal) minVal = v;
    if (v > maxVal) maxVal = v;
  }
  if (maxVal == minVal) maxVal = minVal + 1; // Prevent div by zero

  // Draw Plot
  int h = tft.height() - 20; // Leave space for text
  for (int x = 1; x < dataPoints.size(); x++) {
    int y0 = map(dataPoints[x - 1] * 100, minVal * 100, maxVal * 100, h, 0);
    int y1 = map(dataPoints[x] * 100, minVal * 100, maxVal * 100, h, 0);
    canvas.drawLine(x - 1, y0, x, y1, TFT_GREEN);
  }

  // Draw Footer
  canvas.setTextSize(1);
  canvas.setTextColor(TFT_WHITE, TFT_BLACK);
  canvas.setCursor(0, h + 5);
  canvas.print(lastLine.substring(0, 30)); // Truncate if too long

  // Draw Selected Field in Reverse Colors
  String fieldInfo = " Field: " + String(selectedField) + " Val: " + String(currentVal) + " ";
  int fieldWidth = canvas.textWidth(fieldInfo);
  canvas.fillRect(tft.width() - fieldWidth, h + 2, fieldWidth, 18, TFT_WHITE);
  canvas.setTextColor(TFT_BLACK); // Reverse
  canvas.drawString(fieldInfo, tft.width() - fieldWidth, h + 5);

  canvas.pushSprite(0, 0);
}

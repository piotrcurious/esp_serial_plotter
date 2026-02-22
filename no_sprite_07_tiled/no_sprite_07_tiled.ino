#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <vector>
#include <new>

// Define UART1 Pins
#define RX1_PIN 4
#define TX1_PIN 2
#define BAUD_RATE 115200

// Structure to hold parsed data details
struct DataField {
    String label;
    float value;
    String unit;
    String rawSegment; // Store the original text segment for highlighting
};

class LGFX : public lgfx::LGFX_Device {
    lgfx::Panel_ILI9341 _panel_instance;
    lgfx::Bus_SPI        _bus_instance;

public:
    LGFX(void) {
        auto cfg = _bus_instance.config();
        cfg.spi_host = VSPI_HOST;
        cfg.spi_mode = 0;
        cfg.freq_write = 80000000;
        cfg.freq_read  = 16000000;
        cfg.pin_sclk = 18;
        cfg.pin_mosi = 23;
        cfg.pin_miso = -1;
        cfg.pin_dc   = 16;
        _bus_instance.config(cfg);
        _panel_instance.setBus(&_bus_instance);

        auto panel_cfg = _panel_instance.config();
        panel_cfg.pin_cs           = 5;
        panel_cfg.pin_rst          = 17;
        panel_cfg.pin_busy         = -1;
        panel_cfg.panel_width      = 240;
        panel_cfg.panel_height     = 320;
        panel_cfg.offset_x         = 0;
        panel_cfg.offset_y         = 0;
        panel_cfg.offset_rotation  = 0;
        panel_cfg.readable         = true;
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

#define TILE_SHIFT 4
#define TILE_SIZE (1 << TILE_SHIFT)

class Tile {
public:
    uint16_t x0, y0, w, h;
    uint8_t* buf;
    bool dirty_curr;
    bool dirty_prev;

    Tile() : x0(0), y0(0), w(0), h(0), buf(nullptr), dirty_curr(false), dirty_prev(false) {}

    void init(uint16_t _x0, uint16_t _y0, uint16_t _w, uint16_t _h) {
        x0 = _x0; y0 = _y0; w = _w; h = _h;
        size_t n = (size_t)w * (size_t)h;
        if (buf) heap_caps_free(buf);
        buf = (uint8_t*) heap_caps_malloc(n, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
        if (!buf) buf = (uint8_t*) malloc(n);
        dirty_curr = true;
        dirty_prev = true;
        if (buf) memset(buf, 0, n);
    }

    void prepareFrame() {
        dirty_prev = dirty_curr;
        dirty_curr = false;
    }

    void clear(uint8_t color = 0) {
        if (buf) {
            memset(buf, color, (size_t)w * h);
            dirty_curr = true;
        }
    }
};

class TileManager {
public:
    Tile* tiles;
    uint16_t screen_w, screen_h, tile_size;
    uint16_t cols, rows;

    TileManager() : tiles(nullptr) {}

    void init(uint16_t sw, uint16_t sh, uint16_t tsize) {
        screen_w = sw; screen_h = sh; tile_size = tsize;
        cols = (screen_w + tile_size - 1) / tile_size;
        rows = (screen_h + tile_size - 1) / tile_size;
        tiles = (Tile*)malloc(sizeof(Tile) * cols * rows);
        for (uint16_t r = 0; r < rows; ++r) {
            for (uint16_t c = 0; c < cols; ++c) {
                uint16_t x0 = c * tile_size;
                uint16_t y0 = r * tile_size;
                uint16_t tw = (x0 + tile_size <= screen_w) ? tile_size : (screen_w - x0);
                uint16_t th = (y0 + tile_size <= screen_h) ? tile_size : (screen_h - y0);
                new (&tiles[r * cols + c]) Tile();
                tiles[r * cols + c].init(x0, y0, tw, th);
            }
        }
    }

    Tile* tileAtIdx(uint16_t tx, uint16_t ty) {
        if (tx >= cols || ty >= rows) return nullptr;
        return &tiles[ty * cols + tx];
    }

    void writePixelGlobal(int16_t x, int16_t y, uint8_t color) {
        if (x < 0 || y < 0 || x >= (int)screen_w || y >= (int)screen_h) return;
    #ifdef TILE_SHIFT
        uint16_t tx = (uint16_t)(x >> TILE_SHIFT);
        uint16_t ty = (uint16_t)(y >> TILE_SHIFT);
        uint16_t lx = (uint16_t)(x & (tile_size - 1));
        uint16_t ly = (uint16_t)(y & (tile_size - 1));
    #else
        uint16_t tx = (uint16_t)(x / tile_size);
        uint16_t ty = (uint16_t)(y / tile_size);
        uint16_t lx = (uint16_t)(x - tx * tile_size);
        uint16_t ly = (uint16_t)(y - ty * tile_size);
    #endif
        Tile* t = tileAtIdx(tx, ty);
        if (!t || !t->buf) return;
        t->buf[(size_t)ly * (size_t)t->w + (size_t)lx] = color;
        t->dirty_curr = true;
    }

    void drawLine(int x0, int y0, int x1, int y1, uint8_t color) {
        if ((x0 < 0 && x1 < 0) || (x0 >= (int)screen_w && x1 >= (int)screen_w) ||
            (y0 < 0 && y1 < 0) || (y0 >= (int)screen_h && y1 >= (int)screen_h)) return;

        int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
        int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
        int err = dx + dy, e2;

        while (true) {
            writePixelGlobal(x0, y0, color);
            if (x0 == x1 && y0 == y1) break;
            e2 = 2 * err;
            if (e2 >= dy) { err += dy; x0 += sx; }
            if (e2 <= dx) { err += dx; y0 += sy; }
        }
    }

    void startFrame() {
        uint32_t count = (uint32_t)cols * rows;
        for (uint32_t i = 0; i < count; ++i) tiles[i].prepareFrame();
    }

    void flush(lgfx::LGFX_Device &dev) {
        dev.startWrite();
        uint32_t total = (uint32_t)cols * rows;
        for (uint32_t i = 0; i < total; ++i) {
            Tile &t = tiles[i];
            if (t.dirty_curr || t.dirty_prev) {
                dev.pushImage(t.x0, t.y0, t.w, t.h, t.buf, lgfx::grayscale_8bit);
            }
        }
    }

    void clear(uint8_t color = 0) {
        uint32_t total = (uint32_t)cols * rows;
        for (uint32_t i = 0; i < total; ++i) {
            tiles[i].clear(color);
        }
    }

    void drawFastVLine(int16_t x, int16_t y, int16_t h, uint8_t color) {
        for (int16_t i = 0; i < h; ++i) {
            writePixelGlobal(x, y + i, color);
        }
    }
};

static TileManager& get_canvas() {
    static TileManager tm;
    return tm;
}

#define C_BLACK 0
#define C_WHITE 255
#define C_GREEN 180
#define C_CYAN  220

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
    while (start < processingLine.length()) {
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
    tft.setTextColor(TFT_DARKGREY,TFT_BLACK);
    tft.setTextSize(1);
    tft.setTextDatum(top_right);
    tft.drawString(String(maxV, 2), SCREEN_W - 5, 5);
    tft.setTextDatum(bottom_right);
    tft.drawString(String(minV, 2), SCREEN_W - 5, PLOT_H - 5);

    // Draw subtle horizontal guides at the scale points
    //tft.drawFastHLine(SCREEN_W - 30, 5, 30, tft.color565(40, 40, 40));
    //tft.drawFastHLine(SCREEN_W - 30, PLOT_H - 5, 30, tft.color565(40, 40, 40));
}

#include <cmath> // Required for isnan()

void updatePlotter() {
    if (currentFields.empty() || selectedField >= currentFields.size()) return;

    float val = currentFields[selectedField].value;
    dataPoints[xPos] = val;

    // 1. Robust Min/Max calculation (Excluding NaN)
    float minV = 3.4028235E38; // FLT_MAX
    float maxV = -3.4028235E38; // FLT_MIN
    bool hasValidData = false;

    for (float v : dataPoints) {
        if (!std::isnan(v)) {
            if (v < minV) minV = v;
            if (v > maxV) maxV = v;
            hasValidData = true;
        }
    }

    // Default range if all data is NaN or constant
    if (!hasValidData) { minV = 0.0; maxV = 1.0; }
    float range = maxV - minV;
    if (range < 0.0001) range = 1.0;

    // Add 10% padding
    float padMin = minV - (range * 0.1);
    float padMax = maxV + (range * 0.1);

    // 2. Hysteresis to prevent constant rescale flicker
    bool scaleChanged = (abs(padMin - lastMinV) > (range * 0.1) || abs(padMax - lastMaxV) > (range * 0.1));

    TileManager &canvas = get_canvas();
    canvas.startFrame();

    if (scaleChanged) {
        // Redraw entire background and plot
        canvas.clear(C_BLACK);
        int prevY = -1;
        for (int i = 0; i < SCREEN_W; i++) {
            if (std::isnan(dataPoints[i])) {
                prevY = -1; // Break the line on NaN
                continue;
            }
            int drawY = (int)fmap(dataPoints[i], padMin, padMax, PLOT_H - 10, 10);
            if (prevY != -1) {
                canvas.drawLine(i - 1, prevY, i, drawY, C_GREEN);
            }
            prevY = drawY;
            if (i == xPos) lastY = drawY;
        }
        lastMinV = padMin;
        lastMaxV = padMax;
    } else {
        // 3. Optimized "Sweeping" Update
        // Clear a vertical sliver ahead of the current point (the "cursor")
        int nextX = (xPos + 1) % SCREEN_W;
        canvas.drawFastVLine(nextX, 0, PLOT_H, C_BLACK);
        canvas.drawFastVLine((xPos + 2) % SCREEN_W, 0, PLOT_H, C_BLACK); // Buffer gap

        // Draw cyan "cursor" line to show current position
        canvas.drawFastVLine((xPos + 3) % SCREEN_W, 0, PLOT_H, C_CYAN);

        if (!std::isnan(val)) {
            int y = (int)fmap(val, padMin, padMax, PLOT_H - 10, 10);
            // Only draw line if previous point wasn't NaN and we aren't wrapping around
            if (xPos > 0 && lastY != -1) {
                canvas.drawLine(xPos - 1, lastY, xPos, y, C_GREEN);
            } else {
                canvas.writePixelGlobal(xPos, y, C_GREEN);
            }
            lastY = y;
        } else {
            lastY = -1; // Signal that the line is broken
        }
    }

    canvas.flush(tft);

    // Draw Y Scale on top of the tiles
    if (scaleChanged || xPos % 50 == 0) {
        drawYScale(padMin, padMax);
    }
    tft.endWrite();


    // UPDATE FOOTER

        footer.fillSprite(tft.color565(30, 30, 35));

    // Row 1: Label and Value (Top Left)
    DataField selected = currentFields[selectedField];
    footer.setTextSize(1);
    //footer.setTextColor(TFT_CYAN);
    //footer.setCursor(5, 4);
    //footer.print(selected.label + ": ");

    //footer.setTextColor(TFT_WHITE);
    //footer.print(String(selected.value, 4) + " " + selected.unit);

    // Row 2: Raw Line with Highlight
    int rawY = 0;
    footer.setCursor(0, rawY);
    footer.setTextColor(TFT_DARKGREY);

    // Split the raw line into segments and print, highlighting the selected one
    if (globalRawLine.indexOf("RX1:") >= 0) {
        footer.setTextColor(TFT_DARKGREY);
        footer.print("RX1: ");
    }

    for (int i = 0; i < currentFields.size(); i++) {
        if (i == selectedField) {
            footer.setTextColor(TFT_YELLOW);
            footer.print(currentFields[i].rawSegment);
        } else {
            footer.setTextColor(TFT_LIGHTGREY);
            footer.print(currentFields[i].rawSegment);
        }

        if (i < currentFields.size() - 1) {
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
    tft.println("ESP32 SERIAL PLOTTER");

    tft.setTextSize(1);
    tft.setTextColor(TFT_WHITE);
    tft.setCursor(10, 45);
    tft.println("------------------------------------");

    tft.setTextColor(TFT_YELLOW);
    tft.println("HARDWARE CONFIG:");
    tft.setTextColor(TFT_WHITE);
    tft.printf(" - Interface: UART1\n");
    tft.printf(" - RX Pin:    GPIO %d\n", RX1_PIN);
    tft.printf(" - TX Pin:    GPIO %d\n", TX1_PIN);
    tft.printf(" - Baudrate:  %d bps\n", BAUD_RATE);
    tft.printf(" - Format:    8-N-1\n\n");

    tft.setTextColor(TFT_YELLOW);
    tft.println("INSTRUCTIONS:");
    tft.setTextColor(TFT_WHITE);
    tft.println(" - Send data as CSV (e.g. '1.2, 4.5, 9')");
    tft.println(" - Press BOOT (GPIO 0) to cycle channels");
    tft.println(" - Plotter autoscales on value changes");

    tft.setCursor(10, 210);
    tft.setTextColor(TFT_DARKGREY);
    tft.println("Starting in 5 seconds...");

    delay(5000);
    //tft.fillScreen(TFT_BLACK); leave the info until we get data
}

void setup() {
    Serial.begin(115200);
    Serial1.begin(BAUD_RATE, SERIAL_8N1, RX1_PIN, TX1_PIN);
    pinMode(0, INPUT_PULLUP);

    tft.init();
    tft.setRotation(1);
    tft.fillScreen(TFT_BLACK);
    showWelcomeScreen();

    get_canvas().init(SCREEN_W, PLOT_H, TILE_SIZE);

    footer.createSprite(SCREEN_W, FOOTER_H);
    dataPoints.assign(SCREEN_W, NAN);
}

void loop() {
    if (digitalRead(0) == LOW) {
        if (!currentFields.empty()) {
            selectedField = (selectedField + 1) % currentFields.size();
            tft.fillScreen(TFT_BLACK);
            get_canvas().clear(C_BLACK);
            dataPoints.assign(SCREEN_W, 0.0f);
            xPos = 0;
            lastY = -1;
            lastMinV = 0;
            lastMaxV = 0;
        }
        delay(300);
    }

    if (Serial1.available()) {
        String input = Serial1.readStringUntil('\n');
        input.trim();
        if (input.length() > 0) {
            std::vector<DataField> fields = parseSerialLine(input);
            if (!fields.empty()) {
                currentFields = fields;
                if (selectedField >= currentFields.size()) selectedField = 0;
                updatePlotter();
            }
        }
    }
}

#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <vector>
#include <new>
#include <cmath>

// Define UART1 Pins
#define RX1_PIN 4
#define TX1_PIN 2
#define BAUD_RATE 115200

// --- COLOR CONFIGURATION ---
// The TileManager needs swapped bytes because it writes raw buffers via pushImage.
// The Sprite and TFT functions handle swapping internally.
#define COLOR_SWAP 1

static inline uint16_t swap_bytes(uint16_t color) {
    return (color << 8) | (color >> 8);
}

#if COLOR_SWAP
  #define RAW_RGB(r, g, b) swap_bytes(((r & 0x1F) << 11) | ((g & 0x3F) << 5) | (b & 0x1F))
#else
  #define RAW_RGB(r, g, b) (((r & 0x1F) << 11) | ((g & 0x3F) << 5) | (b & 0x1F))
#endif

// Define standard colors for the TileManager (Swapped)
#define C_BLACK   RAW_RGB(0, 0, 0)
#define C_WHITE   RAW_RGB(31, 63, 31)
#define C_GREEN   RAW_RGB(0, 63, 0)
#define C_CYAN    RAW_RGB(0, 63, 31)
#define C_YELLOW  RAW_RGB(31, 63, 0)
#define C_GREY    RAW_RGB(15, 31, 15)
#define C_DKGREY  RAW_RGB(5, 10, 5)

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
        cfg.dma_channel = 1;
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
    uint16_t* buf;
    bool dirty_curr;
    bool dirty_prev;

    Tile() : x0(0), y0(0), w(0), h(0), buf(nullptr), dirty_curr(false), dirty_prev(false) {}

    void init(uint16_t _x0, uint16_t _y0, uint16_t _w, uint16_t _h) {
        x0 = _x0; y0 = _y0; w = _w; h = _h;
        size_t n = (size_t)w * h;
        if (buf) { free(buf); buf = nullptr; }
        buf = (uint16_t*) heap_caps_malloc(n * 2, MALLOC_CAP_DMA | MALLOC_CAP_32BIT);
        if (!buf) buf = (uint16_t*) malloc(n * 2);
        dirty_curr = true;
        dirty_prev = true;
        if (buf) clear(C_BLACK);
    }

    void prepareFrame() {
        dirty_prev = dirty_curr;
        dirty_curr = false;
    }

    void clear(uint16_t color = C_BLACK) {
        if (buf) {
            size_t n = (size_t)w * h;
            for(size_t i=0; i<n; i++) buf[i] = color;
            dirty_curr = true;
        }
    }
    
    void clearColumn(uint16_t localX, uint16_t color = C_BLACK) {
        if (!buf || localX >= w) return;
        for (uint16_t i = 0; i < h; i++) {
            buf[i * w + localX] = color;
        }
        dirty_curr = true;
    }
};

class TileManager {
public:
    Tile* tiles;
    uint16_t cols, rows;

    void init(uint16_t sw, uint16_t sh, uint16_t tsize) {
        cols = (sw + tsize - 1) / tsize;
        rows = (sh + tsize - 1) / tsize;
        tiles = (Tile*)malloc(sizeof(Tile) * cols * rows);
        for (uint16_t r = 0; r < rows; ++r) {
            for (uint16_t c = 0; c < cols; ++c) {
                uint16_t x0 = c * tsize;
                uint16_t y0 = r * tsize;
                uint16_t tw = (x0 + tsize <= sw) ? tsize : (sw - x0);
                uint16_t th = (y0 + tsize <= sh) ? tsize : (sh - y0);
                new (&tiles[r * cols + c]) Tile();
                tiles[r * cols + c].init(x0, y0, tw, th);
            }
        }
    }

    void writePixelGlobal(int16_t x, int16_t y, uint16_t color) {
        if (x < 0 || y < 0 || x >= SCREEN_W || y >= PLOT_H) return;
        uint16_t tx = x >> TILE_SHIFT;
        uint16_t ty = y >> TILE_SHIFT;
        Tile* t = &tiles[ty * cols + tx];
        t->buf[(size_t)(y - t->y0) * t->w + (x - t->x0)] = color;
        t->dirty_curr = true;
    }

    void clearColumnGlobal(int16_t x, uint16_t color = C_BLACK) {
        if (x < 0 || x >= SCREEN_W) return;
        uint16_t tx = x >> TILE_SHIFT;
        uint16_t localX = x - (tx << TILE_SHIFT);
        for (uint16_t r = 0; r < rows; r++) {
            tiles[r * cols + tx].clearColumn(localX, color);
        }
    }

    void drawLine(int x0, int y0, int x1, int y1, uint16_t color) {
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

    void flush(lgfx::LGFX_Device &dev) {
        for (uint32_t i = 0; i < (uint32_t)cols * rows; ++i) {
            Tile &t = tiles[i];
            if (t.dirty_curr || t.dirty_prev) {
                dev.pushImage(t.x0, t.y0, t.w, t.h, t.buf);
            }
        }
    }

    void clear(uint16_t color = C_BLACK) {
        for (uint32_t i = 0; i < (uint32_t)cols * rows; ++i) tiles[i].clear(color);
    }

    static TileManager& getInstance() {
        static TileManager tm;
        return tm;
    }
};

// Data handling
std::vector<float> dataPoints;
struct DataField { String label; float value; String unit; String rawSegment; };
std::vector<DataField> currentFields;
String globalRawLine = "";
int xPos = 0, lastY = -1, selectedField = 0;
float lastMinV = 0, lastMaxV = 0;

float fmap(float x, float in_min, float in_max, float out_min, float out_max) {
    if (in_max == in_min) return out_min + (out_max - out_min) / 2.0;
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

std::vector<DataField> parseSerialLine(String line) {
    std::vector<DataField> fields;
    globalRawLine = line;
    String proc = line;
    if (proc.indexOf("RX1:") >= 0) proc = proc.substring(proc.indexOf("RX1:") + 4);
    proc.trim();
    int start = 0;
    while (start < proc.length()) {
        int commaPos = proc.indexOf(',', start);
        String pair = (commaPos == -1) ? proc.substring(start) : proc.substring(start, commaPos);
        DataField df; df.rawSegment = pair; pair.trim();
        if (pair.length() > 0) {
            int colonPos = pair.indexOf(':');
            if (colonPos != -1) {
                df.label = pair.substring(0, colonPos); df.label.trim();
                char* endPtr; df.value = strtof(pair.substring(colonPos + 1).c_str(), &endPtr);
                df.unit = String(endPtr); df.unit.trim();
            } else {
                char* endPtr; df.value = strtof(pair.c_str(), &endPtr);
                df.label = "CH" + String(fields.size()); df.unit = String(endPtr); df.unit.trim();
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
    if (currentFields.empty() || selectedField >= currentFields.size()) return;
    float val = currentFields[selectedField].value;
    dataPoints[xPos] = val;

    float minV = 3.4E38, maxV = -3.4E38;
    bool valid = false;
    for (float v : dataPoints) if (!std::isnan(v)) { if (v < minV) minV = v; if (v > maxV) maxV = v; valid = true; }
    if (!valid) { minV = 0.0; maxV = 1.0; }
    float range = maxV - minV; if (range < 0.001) range = 1.0;
    float padMin = minV - (range * 0.1), padMax = maxV + (range * 0.1);

    bool scaleChanged = (abs(padMin - lastMinV) > (range * 0.1) || abs(padMax - lastMaxV) > (range * 0.1));
    TileManager &canvas = TileManager::getInstance();
    
    for (uint32_t i = 0; i < (uint32_t)canvas.cols * canvas.rows; ++i) canvas.tiles[i].prepareFrame();

    if (scaleChanged) {
        canvas.clear(C_BLACK);
        int prevY = -1;
        for (int i = 0; i < SCREEN_W; i++) {
            if (std::isnan(dataPoints[i])) { prevY = -1; continue; }
            int drawY = (int)fmap(dataPoints[i], padMin, padMax, PLOT_H - 10, 10);
            if (prevY != -1) canvas.drawLine(i - 1, prevY, i, drawY, C_GREEN);
            prevY = drawY; if (i == xPos) lastY = drawY;
        }
        lastMinV = padMin; lastMaxV = padMax;
    } else {
        int nextX = (xPos + 1) % SCREEN_W;
        int gapX = (xPos + 4) % SCREEN_W; 
        
        canvas.clearColumnGlobal(xPos, C_BLACK);
        canvas.clearColumnGlobal(nextX, C_BLACK);
        canvas.clearColumnGlobal(gapX, C_CYAN); 

        if (!std::isnan(val)) {
            int y = (int)fmap(val, padMin, padMax, PLOT_H - 10, 10);
            if (xPos > 0 && lastY != -1) {
                canvas.drawLine(xPos - 1, lastY, xPos, y, C_GREEN);
            } else {
                canvas.writePixelGlobal(xPos, y, C_GREEN);
            }
            lastY = y;
        } else lastY = -1;
    }

    tft.startWrite();
    canvas.flush(tft);
    if (scaleChanged || xPos % 50 == 0) drawYScale(padMin, padMax);
    tft.endWrite();

    // Footer - Using TFT standard color constants as original code did
    footer.fillSprite(tft.color565(30, 30, 35));
    footer.setCursor(0, 5);
    if (globalRawLine.indexOf("RX1:") >= 0) { footer.setTextColor(TFT_DARKGREY); footer.print("RX1: "); }
    for (int i = 0; i < (int)currentFields.size(); i++) {
        footer.setTextColor(i == selectedField ? TFT_YELLOW : TFT_LIGHTGREY);
        footer.print(currentFields[i].rawSegment);
        if (i < (int)currentFields.size() - 1) { footer.setTextColor(TFT_DARKGREY); footer.print(","); }
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
    tft.printf(" - UART1 RX: GPIO %d\n", RX1_PIN);
    tft.printf(" - Baudrate: %d\n", BAUD_RATE);
    tft.setCursor(10, 210);
    tft.setTextColor(TFT_DARKGREY);
    tft.println("Waiting for data...");
}

void setup() {
    Serial.begin(115200);
    Serial1.begin(BAUD_RATE, SERIAL_8N1, RX1_PIN, TX1_PIN);
    pinMode(0, INPUT_PULLUP);
    tft.init();
    tft.setRotation(1);
    showWelcomeScreen();
    delay(5000);
    TileManager::getInstance().init(SCREEN_W, PLOT_H, TILE_SIZE);
    
    // Create footer sprite. 
    // In original code, swapBytes was NOT used for the sprite.
    footer.createSprite(SCREEN_W, FOOTER_H);
    footer.setColorDepth(16);
    
    dataPoints.assign(SCREEN_W, NAN);
}

void loop() {
    if (digitalRead(0) == LOW) {
        if (!currentFields.empty()) {
            selectedField = (selectedField + 1) % currentFields.size();
            tft.fillScreen(TFT_BLACK);
            TileManager::getInstance().clear(C_BLACK);
            dataPoints.assign(SCREEN_W, NAN);
            xPos = 0; lastY = -1; lastMinV = 0; lastMaxV = 0;
        }
        delay(300);
    }
    if (Serial1.available()) {
        String input = Serial1.readStringUntil('\n'); input.trim();
        if (input.length() > 0) {
            std::vector<DataField> fields = parseSerialLine(input);
            if (!fields.empty()) {
                currentFields = fields;
                if (selectedField >= (int)currentFields.size()) selectedField = 0;
                updatePlotter();
            }
        }
    }
}

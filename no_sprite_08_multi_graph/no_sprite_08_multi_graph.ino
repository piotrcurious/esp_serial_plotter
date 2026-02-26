#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <vector>
#include <new>
#include <cmath>
#include <algorithm>

// --- TYPES AND STRUCTURES ---
struct DataField { String label; float value; String unit; String rawSegment; };

enum GraphType {
    LINE_PLOT = 0,
    MULTI_LINE_PLOT,
    HISTOGRAM,
    DELTA_PLOT,
    STATS_DASHBOARD,
    BOX_PLOT,
    ROLLING_BOX_PLOT,
    XY_PLOT,
    RADAR_CHART,
    COUNT
};

struct Stats {
    float min, max, avg, stdDev, median, q1, q3, skewness, minDelta, maxDelta;
    int count;
};

// --- TILE SYSTEM ---
const int SCREEN_W = 320;
const int SCREEN_H = 240;
const int FOOTER_H = 45;
const int PLOT_H = SCREEN_H - FOOTER_H;

#define TILE_SHIFT 4
#define TILE_SIZE (1 << TILE_SHIFT)

// RAW RGB for TileManager
#define COLOR_SWAP 1
static inline uint16_t swap_bytes(uint16_t color) { return (color << 8) | (color >> 8); }
#if COLOR_SWAP
  #define RAW_RGB(r, g, b) swap_bytes(((r & 0x1F) << 11) | ((g & 0x3F) << 5) | (b & 0x1F))
#else
  #define RAW_RGB(r, g, b) (((r & 0x1F) << 11) | ((g & 0x3F) << 5) | (b & 0x1F))
#endif

#define C_BLACK   RAW_RGB(0, 0, 0)
#define C_WHITE   RAW_RGB(31, 63, 31)
#define C_GREEN   RAW_RGB(0, 63, 0)
#define C_CYAN    RAW_RGB(0, 63, 31)
#define C_YELLOW  RAW_RGB(31, 63, 0)
#define C_MAGENTA RAW_RGB(31, 0, 31)
#define C_RED     RAW_RGB(31, 0, 0)
#define C_BLUE    RAW_RGB(0, 0, 31)
#define C_GREY    RAW_RGB(15, 31, 15)
#define C_DKGREY  RAW_RGB(5, 10, 5)

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
        dirty_curr = true; dirty_prev = true;
        if (buf) clear(C_BLACK);
    }
    void prepareFrame() { dirty_prev = dirty_curr; dirty_curr = false; }
    void clear(uint16_t color = C_BLACK) {
        if (buf) { size_t n = (size_t)w * h; for(size_t i=0; i<n; i++) buf[i] = color; dirty_curr = true; }
    }
    void clearColumn(uint16_t localX, uint16_t color = C_BLACK) {
        if (!buf || localX >= w) return;
        for (uint16_t i = 0; i < h; i++) { buf[i * w + localX] = color; }
        dirty_curr = true;
    }

    void fillRect(int16_t lx, int16_t ly, int16_t lw, int16_t lh, uint16_t color) {
        if (!buf) return;
        int16_t xStart = std::max((int16_t)0, lx);
        int16_t xEnd = std::min((int16_t)w, (int16_t)(lx + lw));
        int16_t yStart = std::max((int16_t)0, ly);
        int16_t yEnd = std::min((int16_t)h, (int16_t)(ly + lh));
        if (xStart >= xEnd || yStart >= yEnd) return;
        for (int16_t j = yStart; j < yEnd; j++) {
            uint16_t* row = &buf[j * w];
            for (int16_t i = xStart; i < xEnd; i++) {
                row[i] = color;
            }
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
                uint16_t x0 = c * tsize; uint16_t y0 = r * tsize;
                uint16_t tw = (x0 + tsize <= sw) ? tsize : (sw - x0);
                uint16_t th = (y0 + tsize <= sh) ? tsize : (sh - y0);
                new (&tiles[r * cols + c]) Tile();
                tiles[r * cols + c].init(x0, y0, tw, th);
            }
        }
    }
    void writePixelGlobal(int16_t x, int16_t y, uint16_t color) {
        if (x < 0 || y < 0 || x >= SCREEN_W || y >= PLOT_H) return;
        uint16_t tx = x >> TILE_SHIFT; uint16_t ty = y >> TILE_SHIFT;
        Tile* t = &tiles[ty * cols + tx];
        t->buf[(size_t)(y - t->y0) * t->w + (x - t->x0)] = color;
        t->dirty_curr = true;
    }
    void clearColumnGlobal(int16_t x, uint16_t color = C_BLACK) {
        if (x < 0 || x >= SCREEN_W) return;
        uint16_t tx = x >> TILE_SHIFT;
        uint16_t localX = x - (tx << TILE_SHIFT);
        for (uint16_t r = 0; r < rows; r++) { tiles[r * cols + tx].clearColumn(localX, color); }
    }
    void fillRectGlobal(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
        if (x >= SCREEN_W || y >= PLOT_H || x + w <= 0 || y + h <= 0) return;
        int16_t x1 = x, y1 = y, x2 = x + w, y2 = y + h;
        uint16_t tx1 = std::max(0, x1 >> TILE_SHIFT), tx2 = std::min((int)cols - 1, (x2 - 1) >> TILE_SHIFT);
        uint16_t ty1 = std::max(0, y1 >> TILE_SHIFT), ty2 = std::min((int)rows - 1, (y2 - 1) >> TILE_SHIFT);
        for (uint16_t r = ty1; r <= ty2; r++) {
            for (uint16_t c = tx1; c <= tx2; c++) {
                Tile &t = tiles[r * cols + c];
                t.fillRect(x - t.x0, y - t.y0, w, h, color);
            }
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
            Tile &t = tiles[i]; if (t.dirty_curr || t.dirty_prev) dev.pushImage(t.x0, t.y0, t.w, t.h, t.buf);
        }
    }
    void clear(uint16_t color = C_BLACK) {
        for (uint32_t i = 0; i < (uint32_t)cols * rows; ++i) tiles[i].clear(color);
    }
    void drawChar(int16_t x, int16_t y, char c, uint16_t color, uint16_t bg, uint8_t size) {
        static const uint8_t font[] PROGMEM = {
            0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x5f,0x00,0x00, 0x00,0x07,0x00,0x07,0x00, 0x14,0x7f,0x14,0x7f,0x14,
            0x24,0x2a,0x7f,0x2a,0x12, 0x23,0x13,0x08,0x64,0x62, 0x36,0x49,0x55,0x22,0x50, 0x00,0x05,0x03,0x00,0x00,
            0x00,0x1c,0x22,0x41,0x00, 0x00,0x41,0x22,0x1c,0x00, 0x14,0x08,0x3e,0x08,0x14, 0x08,0x08,0x3e,0x08,0x08,
            0x00,0x50,0x30,0x00,0x00, 0x08,0x08,0x08,0x08,0x08, 0x00,0x60,0x60,0x00,0x00, 0x20,0x10,0x08,0x04,0x02,
            0x3e,0x51,0x49,0x45,0x3e, 0x00,0x42,0x7f,0x40,0x00, 0x42,0x61,0x51,0x49,0x46, 0x21,0x41,0x45,0x4b,0x31,
            0x18,0x14,0x12,0x7f,0x10, 0x27,0x45,0x45,0x45,0x39, 0x3c,0x4a,0x49,0x49,0x30, 0x01,0x71,0x09,0x05,0x03,
            0x36,0x49,0x49,0x49,0x36, 0x06,0x49,0x49,0x29,0x1e, 0x00,0x36,0x36,0x00,0x00, 0x00,0x56,0x36,0x00,0x00,
            0x08,0x14,0x22,0x41,0x00, 0x24,0x24,0x24,0x24,0x24, 0x00,0x41,0x22,0x14,0x08, 0x02,0x01,0x51,0x09,0x06,
            0x32,0x49,0x79,0x41,0x3e, 0x7e,0x11,0x11,0x11,0x7e, 0x7f,0x49,0x49,0x49,0x36, 0x3e,0x41,0x41,0x41,0x22,
            0x7f,0x41,0x41,0x22,0x1c, 0x7f,0x49,0x49,0x49,0x41, 0x7f,0x09,0x09,0x09,0x01, 0x3e,0x41,0x49,0x49,0x7a,
            0x7f,0x08,0x08,0x08,0x7f, 0x00,0x41,0x7f,0x41,0x00, 0x20,0x40,0x41,0x3f,0x01, 0x7f,0x08,0x14,0x22,0x41,
            0x7f,0x40,0x40,0x40,0x40, 0x7f,0x02,0x0c,0x02,0x7f, 0x7f,0x04,0x08,0x10,0x7f, 0x3e,0x41,0x41,0x41,0x3e,
            0x7f,0x09,0x09,0x09,0x06, 0x3e,0x41,0x51,0x21,0x5e, 0x7f,0x09,0x19,0x29,0x46, 0x46,0x49,0x49,0x49,0x31,
            0x01,0x01,0x7f,0x01,0x01, 0x3f,0x40,0x40,0x40,0x3f, 0x1f,0x20,0x40,0x20,0x1f, 0x3f,0x40,0x38,0x40,0x3f,
            0x63,0x14,0x08,0x14,0x63, 0x07,0x08,0x70,0x08,0x07, 0x61,0x51,0x49,0x45,0x43, 0x00,0x7f,0x41,0x41,0x00,
            0x02,0x04,0x08,0x10,0x20, 0x00,0x41,0x41,0x7f,0x00, 0x04,0x02,0x01,0x02,0x04, 0x40,0x40,0x40,0x40,0x40,
            0x00,0x01,0x02,0x04,0x00, 0x20,0x54,0x54,0x54,0x78, 0x7f,0x48,0x44,0x44,0x38, 0x38,0x44,0x44,0x44,0x20,
            0x38,0x44,0x44,0x48,0x7f, 0x38,0x54,0x54,0x54,0x18, 0x08,0x7e,0x09,0x01,0x02, 0x0c,0x52,0x52,0x52,0x3e,
            0x7f,0x08,0x04,0x04,0x78, 0x00,0x44,0x7d,0x40,0x00, 0x20,0x40,0x44,0x3d,0x00, 0x7f,0x10,0x28,0x44,0x00,
            0x00,0x41,0x7f,0x40,0x00, 0x7c,0x04,0x18,0x04,0x78, 0x7c,0x08,0x04,0x04,0x78, 0x38,0x44,0x44,0x44,0x38,
            0x7c,0x14,0x14,0x14,0x08, 0x08,0x14,0x14,0x18,0x7c, 0x7c,0x08,0x04,0x04,0x08, 0x48,0x54,0x54,0x54,0x20,
            0x04,0x3f,0x44,0x40,0x20, 0x3c,0x40,0x40,0x20,0x7c, 0x1c,0x20,0x40,0x20,0x1c, 0x3c,0x40,0x30,0x40,0x3c,
            0x44,0x28,0x10,0x28,0x44, 0x0c,0x50,0x50,0x50,0x3c, 0x44,0x64,0x54,0x4c,0x44, 0x00,0x08,0x36,0x41,0x00,
            0x00,0x00,0x7f,0x00,0x00, 0x00,0x41,0x36,0x08,0x00, 0x10,0x08,0x18,0x10,0x08
        };
        if (c < 32 || c > 126) return;
        uint16_t idx = (c - 32) * 5;
        for (int i = 0; i < 5; i++) {
            uint8_t line = pgm_read_byte(&font[idx + i]);
            for (int j = 0; j < 8; j++) {
                if (line & 0x1) fillRectGlobal(x + i * size, y + j * size, size, size, color);
                else if (bg != color) fillRectGlobal(x + i * size, y + j * size, size, size, bg);
                line >>= 1;
            }
        }
    }
    void drawString(int16_t x, int16_t y, const char* s, uint16_t color, uint16_t bg, uint8_t size) {
        while (*s) { drawChar(x, y, *s++, color, bg, size); x += 6 * size; if (x >= SCREEN_W) return; }
    }
    static TileManager& getInstance() { static TileManager tm; return tm; }
};

// --- PROTOTYPES ---
Stats computeStats(const std::vector<float>& data);
void drawHistogram(TileManager &canvas, const Stats &s);
void drawDeltaPlot(TileManager &canvas, const Stats &s);
void drawStatsDashboard(TileManager &canvas, const Stats &s);
void drawBoxPlot(TileManager &canvas, const Stats &s);
void drawMultiLine(TileManager &canvas);
void drawRollingBoxPlot(TileManager &canvas);
void drawXYPlot(TileManager &canvas);
void drawRadarChart(TileManager &canvas);
void drawYScale(float minV, float maxV);
void updatePlotter();
float fmap(float x, float in_min, float in_max, float out_min, float out_max);
std::vector<DataField> parseSerialLine(String line);
void showWelcomeScreen();

// Define UART1 Pins
#define RX1_PIN 4
#define TX1_PIN 2
#define BAUD_RATE 115200

#define BTN_FIELD 0
#define BTN_GRAPH 35

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

// Data handling
const int MAX_FIELDS = 8;
std::vector<float> fieldHistory[MAX_FIELDS];
std::vector<DataField> currentFields;
String globalRawLine = "";
int xPos = 0, lastY = -1, selectedField = 0;
GraphType currentGraphType = LINE_PLOT;
float lastMinV = 0, lastMaxV = 0;

uint16_t fieldColors[MAX_FIELDS] = {
    C_GREEN, C_YELLOW, C_CYAN, C_MAGENTA, C_RED, C_BLUE, C_WHITE, C_GREY
};

Stats computeStats(const std::vector<float>& data) {
    Stats s;
    std::vector<float> valid;
    for (float v : data) if (!std::isnan(v)) valid.push_back(v);
    s.count = valid.size();
    if (valid.empty()) {
        s.min = s.max = s.avg = s.stdDev = s.median = s.q1 = s.q3 = s.skewness = s.minDelta = 0;
        return s;
    }
    float sum = 0;
    s.min = valid[0]; s.max = valid[0];
    for (float v : valid) {
        sum += v;
        if (v < s.min) s.min = v;
        if (v > s.max) s.max = v;
    }
    s.avg = sum / s.count;

    float sqSum = 0;
    float cubeSum = 0;
    for (float v : valid) {
        float diff = v - s.avg;
        sqSum += diff * diff;
        cubeSum += diff * diff * diff;
    }
    s.stdDev = sqrt(sqSum / s.count);
    if (s.stdDev > 0) {
        s.skewness = (cubeSum / s.count) / pow(s.stdDev, 3);
    } else {
        s.skewness = 0;
    }

    std::sort(valid.begin(), valid.end());
    s.median = valid[s.count / 2];
    s.q1 = valid[s.count / 4];
    s.q3 = valid[3 * s.count / 4];

    float minD = 1e38;
    float maxD = 0;
    bool foundDelta = false;
    for (size_t i = 1; i < data.size(); i++) {
        if (!std::isnan(data[i]) && !std::isnan(data[i-1])) {
            float d = abs(data[i] - data[i-1]);
            if (d > 1e-6) {
                if (d < minD) minD = d;
                if (d > maxD) maxD = d;
                foundDelta = true;
            }
        }
    }
    s.minDelta = foundDelta ? minD : 0;
    s.maxDelta = foundDelta ? maxD : 1.0;

    return s;
}

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
    TileManager &canvas = TileManager::getInstance();
    char buf[16];
    snprintf(buf, sizeof(buf), "%.2f", maxV);
    canvas.drawString(SCREEN_W - 50, 5, buf, C_GREY, C_BLACK, 1);
    snprintf(buf, sizeof(buf), "%.2f", minV);
    canvas.drawString(SCREEN_W - 50, PLOT_H - 15, buf, C_GREY, C_BLACK, 1);
}

void drawHistogram(TileManager &canvas, const Stats &s) {
    canvas.clear(C_BLACK);
    if (s.count < 2) return;
    const int bins = 40;
    int counts[bins] = {0};
    float range = s.max - s.min;
    if (range < 1e-6) range = 1.0;
    for (float v : fieldHistory[selectedField]) {
        if (std::isnan(v)) continue;
        int bin = (int)((v - s.min) / range * (bins - 1));
        if (bin >= 0 && bin < bins) counts[bin]++;
    }
    int maxCount = 0;
    for (int i = 0; i < bins; i++) if (counts[i] > maxCount) maxCount = counts[i];

    int binW = (SCREEN_W - 20) / bins;
    for (int i = 0; i < bins; i++) {
        int h = fmap(counts[i], 0, maxCount, 0, PLOT_H - 40);
        int x = 10 + i * binW;
        canvas.fillRectGlobal(x, PLOT_H - 20 - h, binW - 1, h, fieldColors[selectedField]);
    }
}

void drawDeltaPlot(TileManager &canvas, const Stats &s) {
    canvas.clear(C_BLACK);
    std::vector<float> deltas;
    for (size_t i = 1; i < fieldHistory[selectedField].size(); i++) {
        if (!std::isnan(fieldHistory[selectedField][i]) && !std::isnan(fieldHistory[selectedField][i-1])) {
            deltas.push_back(abs(fieldHistory[selectedField][i] - fieldHistory[selectedField][i-1]));
        }
    }
    if (deltas.empty()) return;
    float maxD = 0;
    for (float d : deltas) if (d > maxD) maxD = d;
    if (maxD < 1e-6) maxD = 1.0;

    const int bins = 40;
    int counts[bins] = {0};
    for (float d : deltas) {
        int bin = (int)(d / maxD * (bins - 1));
        if (bin >= 0 && bin < bins) counts[bin]++;
    }
    int maxCount = 0;
    for (int i = 0; i < bins; i++) if (counts[i] > maxCount) maxCount = counts[i];

    int binW = (SCREEN_W - 20) / bins;
    for (int i = 0; i < bins; i++) {
        int h = fmap(counts[i], 0, maxCount, 0, PLOT_H - 40);
        int x = 10 + i * binW;
        canvas.fillRectGlobal(x, PLOT_H - 20 - h, binW - 1, h, C_CYAN);
    }
}

void drawStatsDashboard(TileManager &canvas, const Stats &s) {
    canvas.clear(C_BLACK);
    char buf[64];
    int ty = 10;
    canvas.drawString(10, ty, "STATISTICAL DASHBOARD", C_CYAN, C_BLACK, 1); ty += 15;
    snprintf(buf, sizeof(buf), "AVG: %.6f", s.avg); canvas.drawString(10, ty, buf, C_YELLOW, C_BLACK, 1); ty += 12;
    snprintf(buf, sizeof(buf), "STD: %.6f", s.stdDev); canvas.drawString(10, ty, buf, C_YELLOW, C_BLACK, 1); ty += 12;
    snprintf(buf, sizeof(buf), "MIN: %.6f", s.min); canvas.drawString(10, ty, buf, C_YELLOW, C_BLACK, 1); ty += 12;
    snprintf(buf, sizeof(buf), "MAX: %.6f", s.max); canvas.drawString(10, ty, buf, C_YELLOW, C_BLACK, 1); ty += 12;
    snprintf(buf, sizeof(buf), "MED: %.6f", s.median); canvas.drawString(10, ty, buf, C_YELLOW, C_BLACK, 1); ty += 12;
    snprintf(buf, sizeof(buf), "Q1 : %.6f", s.q1); canvas.drawString(10, ty, buf, C_YELLOW, C_BLACK, 1); ty += 12;
    snprintf(buf, sizeof(buf), "Q3 : %.6f", s.q3); canvas.drawString(10, ty, buf, C_YELLOW, C_BLACK, 1); ty += 12;
    snprintf(buf, sizeof(buf), "SKEW: %.6f", s.skewness); canvas.drawString(10, ty, buf, C_YELLOW, C_BLACK, 1); ty += 12;
    snprintf(buf, sizeof(buf), "Q-STEP: %.6f", s.minDelta); canvas.drawString(10, ty, buf, C_CYAN, C_BLACK, 1); ty += 12;
    snprintf(buf, sizeof(buf), "SAMPLES: %d", s.count); canvas.drawString(10, ty, buf, C_WHITE, C_BLACK, 1);
}

void drawBoxPlot(TileManager &canvas, const Stats &s) {
    canvas.clear(C_BLACK);
    if (s.count < 4) return;
    float range = s.max - s.min;
    if (range < 1e-6) range = 1.0;
    float padMin = s.min - (range * 0.1), padMax = s.max + (range * 0.1);

    int yQ1 = fmap(s.q1, padMin, padMax, PLOT_H - 20, 20);
    int yQ3 = fmap(s.q3, padMin, padMax, PLOT_H - 20, 20);
    int yMed = fmap(s.median, padMin, padMax, PLOT_H - 20, 20);
    int yMin = fmap(s.min, padMin, padMax, PLOT_H - 20, 20);
    int yMax = fmap(s.max, padMin, padMax, PLOT_H - 20, 20);

    int xMid = SCREEN_W / 2;
    int boxW = 60;

    // Whiskers
    canvas.drawLine(xMid, yMin, xMid, yMax, C_WHITE);
    canvas.fillRectGlobal(xMid - 10, yMin, 20, 1, C_WHITE);
    canvas.fillRectGlobal(xMid - 10, yMax, 20, 1, C_WHITE);

    // Box
    canvas.fillRectGlobal(xMid - boxW/2, std::min(yQ1, yQ3), boxW, std::max(1, abs(yQ3 - yQ1)), fieldColors[selectedField]);
    // Median line
    canvas.fillRectGlobal(xMid - boxW/2, yMed, boxW, 1, C_WHITE);
}

void drawMultiLine(TileManager &canvas) {
    canvas.clear(C_BLACK);
    float globalMin = 1e38, globalMax = -1e38;
    bool anyValid = false;
    for (int f = 0; f < (int)currentFields.size() && f < MAX_FIELDS; f++) {
        for (float v : fieldHistory[f]) {
            if (!std::isnan(v)) {
                if (v < globalMin) globalMin = v;
                if (v > globalMax) globalMax = v;
                anyValid = true;
            }
        }
    }
    if (!anyValid) return;
    float range = globalMax - globalMin; if (range < 0.001) range = 1.0;
    float padMin = globalMin - (range * 0.1), padMax = globalMax + (range * 0.1);

    for (int f = 0; f < (int)currentFields.size() && f < MAX_FIELDS; f++) {
        int prevY = -1;
        for (int i = 0; i < SCREEN_W; i++) {
            if (std::isnan(fieldHistory[f][i])) { prevY = -1; continue; }
            int drawY = (int)fmap(fieldHistory[f][i], padMin, padMax, PLOT_H - 10, 10);
            if (prevY != -1) canvas.drawLine(i - 1, prevY, i, drawY, fieldColors[f]);
            prevY = drawY;
        }
    }
}

void drawRollingBoxPlot(TileManager &canvas) {
    canvas.clear(C_BLACK);
    const int window = 20;
    const int step = 10;

    float globalMin = 1e38, globalMax = -1e38;
    for (float v : fieldHistory[selectedField]) {
        if (!std::isnan(v)) {
            if (v < globalMin) globalMin = v;
            if (v > globalMax) globalMax = v;
        }
    }
    float range = globalMax - globalMin; if (range < 0.001) range = 1.0;
    float padMin = globalMin - (range * 0.1), padMax = globalMax + (range * 0.1);

    for (int x = 0; x < SCREEN_W; x += step) {
        std::vector<float> winData;
        for (int i = x - window / 2; i <= x + window / 2; i++) {
            int idx = (i + SCREEN_W) % SCREEN_W;
            if (!std::isnan(fieldHistory[selectedField][idx])) winData.push_back(fieldHistory[selectedField][idx]);
        }
        if (winData.size() < 4) continue;
        Stats ws = computeStats(winData);

        int yQ1 = fmap(ws.q1, padMin, padMax, PLOT_H - 10, 10);
        int yQ3 = fmap(ws.q3, padMin, padMax, PLOT_H - 10, 10);
        int yMed = fmap(ws.median, padMin, padMax, PLOT_H - 10, 10);
        int yMin = fmap(ws.min, padMin, padMax, PLOT_H - 10, 10);
        int yMax = fmap(ws.max, padMin, padMax, PLOT_H - 10, 10);

        canvas.fillRectGlobal(x, yMax, 1, yMin - yMax, C_GREY);
        canvas.fillRectGlobal(x - 2, std::min(yQ1, yQ3), 5, std::max(1, abs(yQ3 - yQ1)), fieldColors[selectedField]);
        canvas.fillRectGlobal(x - 2, yMed, 5, 1, C_WHITE);
    }
}

void drawXYPlot(TileManager &canvas) {
    canvas.clear(C_BLACK);
    if (currentFields.size() < 2) return;

    Stats sx = computeStats(fieldHistory[0]);
    Stats sy = computeStats(fieldHistory[1]);

    float rangeX = sx.max - sx.min; if (rangeX < 0.001) rangeX = 1.0;
    float rangeY = sy.max - sy.min; if (rangeY < 0.001) rangeY = 1.0;

    float padMinX = sx.min - (rangeX * 0.1), padMaxX = sx.max + (rangeX * 0.1);
    float padMinY = sy.min - (rangeY * 0.1), padMaxY = sy.max + (rangeY * 0.1);

    for (int i = 0; i < SCREEN_W; i++) {
        if (std::isnan(fieldHistory[0][i]) || std::isnan(fieldHistory[1][i])) continue;
        int dx = fmap(fieldHistory[0][i], padMinX, padMaxX, 10, SCREEN_W - 10);
        int dy = fmap(fieldHistory[1][i], padMinY, padMaxY, PLOT_H - 10, 10);
        canvas.writePixelGlobal(dx, dy, fieldColors[0]);
        if (i == xPos) { // Mark current point
            canvas.fillRectGlobal(dx - 2, dy - 2, 5, 5, C_WHITE);
        }
    }
}

void drawRadarChart(TileManager &canvas) {
    canvas.clear(C_BLACK);
    int numFields = std::min((int)currentFields.size(), MAX_FIELDS);
    if (numFields < 3) return;

    int cx = SCREEN_W / 2, cy = PLOT_H / 2;
    int maxR = std::min(cx, cy) - 20;

    // Draw axes
    for (int i = 0; i < numFields; i++) {
        float angle = i * 2.0 * PI / numFields - PI / 2.0;
        int ax = cx + cos(angle) * maxR;
        int ay = cy + sin(angle) * maxR;
        canvas.drawLine(cx, cy, ax, ay, C_DKGREY);
    }

    // Draw current values
    int prevX = -1, prevY = -1, firstX = -1, firstY = -1;
    for (int i = 0; i < numFields; i++) {
        float val = fieldHistory[i][xPos];
        if (std::isnan(val)) val = 0;

        // Use global stats for scaling each axis or local stats?
        // Let's use history stats for each field
        Stats s = computeStats(fieldHistory[i]);
        float r = fmap(val, s.min, s.max, 0, maxR);
        if (r < 0) r = 0; if (r > maxR) r = maxR;

        float angle = i * 2.0 * PI / numFields - PI / 2.0;
        int px = cx + cos(angle) * r;
        int py = cy + sin(angle) * r;

        if (prevX != -1) canvas.drawLine(prevX, prevY, px, py, C_GREEN);
        else { firstX = px; firstY = py; }

        prevX = px; prevY = py;
        canvas.fillRectGlobal(px - 2, py - 2, 5, 5, fieldColors[i]);
    }
    if (firstX != -1 && prevX != -1) canvas.drawLine(prevX, prevY, firstX, firstY, C_GREEN);
}

void updatePlotter() {
    if (currentFields.empty()) return;

    for (int i = 0; i < (int)currentFields.size() && i < MAX_FIELDS; i++) {
        fieldHistory[i][xPos] = currentFields[i].value;
    }

    if (selectedField >= currentFields.size()) selectedField = 0;
    float val = currentFields[selectedField].value;

    Stats s = computeStats(fieldHistory[selectedField]);
    float range = s.max - s.min; if (range < 0.001) range = 1.0;
    float padMin = s.min - (range * 0.1), padMax = s.max + (range * 0.1);

    TileManager &canvas = TileManager::getInstance();
    for (uint32_t i = 0; i < (uint32_t)canvas.cols * canvas.rows; ++i) canvas.tiles[i].prepareFrame();

    bool fullRedraw = (abs(padMin - lastMinV) > (range * 0.1) || abs(padMax - lastMaxV) > (range * 0.1) || currentGraphType != LINE_PLOT);

    if (currentGraphType == LINE_PLOT) {
        if (fullRedraw) {
            canvas.clear(C_BLACK);
            int prevY = -1;
            for (int i = 0; i < SCREEN_W; i++) {
                if (std::isnan(fieldHistory[selectedField][i])) { prevY = -1; continue; }
                int drawY = (int)fmap(fieldHistory[selectedField][i], padMin, padMax, PLOT_H - 10, 10);
                if (prevY != -1) canvas.drawLine(i - 1, prevY, i, drawY, fieldColors[selectedField]);
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
                if (xPos > 0 && lastY != -1) canvas.drawLine(xPos - 1, lastY, xPos, y, fieldColors[selectedField]);
                else canvas.writePixelGlobal(xPos, y, fieldColors[selectedField]);
                lastY = y;
            } else lastY = -1;
        }
    } else if (xPos % 5 == 0) { // Update other graphs less frequently
        if (currentGraphType == MULTI_LINE_PLOT) drawMultiLine(canvas);
        else if (currentGraphType == HISTOGRAM) drawHistogram(canvas, s);
        else if (currentGraphType == DELTA_PLOT) drawDeltaPlot(canvas, s);
        else if (currentGraphType == STATS_DASHBOARD) drawStatsDashboard(canvas, s);
        else if (currentGraphType == BOX_PLOT) drawBoxPlot(canvas, s);
        else if (currentGraphType == ROLLING_BOX_PLOT) drawRollingBoxPlot(canvas);
        else if (currentGraphType == XY_PLOT) drawXYPlot(canvas);
        else if (currentGraphType == RADAR_CHART) drawRadarChart(canvas);
    }

    if (currentGraphType == LINE_PLOT) {
        if (fullRedraw || xPos % 50 == 0) drawYScale(padMin, padMax);
    } else if (xPos % 5 == 0) {
        char buf[64];
        if (currentGraphType == HISTOGRAM || currentGraphType == DELTA_PLOT) {
             canvas.drawString(10, 5, currentGraphType == HISTOGRAM ? "Distribution" : "Quantization (Delta)", C_WHITE, C_BLACK, 1);
             if (currentGraphType == HISTOGRAM) snprintf(buf, sizeof(buf), "Value Range: [%.2f, %.2f]", s.min, s.max);
             else snprintf(buf, sizeof(buf), "Delta Range: [0, %.2f]", s.maxDelta);
             canvas.drawString(10, PLOT_H - 15, buf, C_WHITE, C_BLACK, 1);
        } else if (currentGraphType == BOX_PLOT || currentGraphType == ROLLING_BOX_PLOT) {
             canvas.drawString(10, 5, currentGraphType == BOX_PLOT ? "Box & Whisker" : "Rolling Box Plot", C_WHITE, C_BLACK, 1);
             snprintf(buf, sizeof(buf), "Min:%.1f Q1:%.1f M:%.1f Q3:%.1f Max:%.1f", s.min, s.q1, s.median, s.q3, s.max);
             canvas.drawString(10, PLOT_H - 15, buf, C_WHITE, C_BLACK, 1);
        } else if (currentGraphType == XY_PLOT && currentFields.size() >= 2) {
             snprintf(buf, sizeof(buf), "XY Plot: %s vs %s", currentFields[0].label.c_str(), currentFields[1].label.c_str());
             canvas.drawString(10, 5, buf, C_WHITE, C_BLACK, 1);
        } else if (currentGraphType == RADAR_CHART) {
             canvas.drawString(10, 5, "Radar Chart", C_WHITE, C_BLACK, 1);
        } else if (currentGraphType == MULTI_LINE_PLOT) {
             canvas.drawString(10, 5, "Multi-Channel Plot", C_WHITE, C_BLACK, 1);
        }
    }

    tft.startWrite();
    canvas.flush(tft);
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
    tft.drawRect(0, 0, SCREEN_W, SCREEN_H, TFT_DARKGREY);
    tft.setTextColor(TFT_CYAN);
    tft.setTextSize(2);
    tft.setCursor(20, 20);
    tft.println("ESP32 ANALYZER");
    tft.setTextSize(1);
    tft.setTextColor(TFT_WHITE);
    tft.setCursor(20, 50);
    tft.println("Advanced Serial Plotter & Stats");
    tft.setCursor(20, 70);
    tft.setTextColor(TFT_YELLOW);
    tft.println("HARDWARE:");
    tft.setTextColor(TFT_WHITE);
    tft.printf(" - RX1: GPIO %d @ %d\n", RX1_PIN, BAUD_RATE);
    tft.printf(" - BTN FIELD: GPIO %d\n", BTN_FIELD);
    tft.printf(" - BTN GRAPH: GPIO %d\n", BTN_GRAPH);

    tft.setCursor(20, 130);
    tft.setTextColor(TFT_GREEN);
    tft.println("FEATURES:");
    tft.setTextColor(TFT_WHITE);
    tft.println(" - Rolling Time Series");
    tft.println(" - Value Distribution (Histogram)");
    tft.println(" - Quantization Analysis (Delta)");
    tft.println(" - Statistical Dashboard");
    tft.println(" - Box & Whisker Plot");

    tft.setCursor(20, SCREEN_H - 30);
    tft.setTextColor(TFT_DARKGREY);
    tft.println("Waiting for serial data...");
}

void setup() {
    Serial.begin(115200);
    Serial1.begin(BAUD_RATE, SERIAL_8N1, RX1_PIN, TX1_PIN);
    pinMode(BTN_FIELD, INPUT_PULLUP);
    pinMode(BTN_GRAPH, INPUT_PULLUP);
    tft.init();
    tft.setRotation(1);
    showWelcomeScreen();
    delay(5000);
    TileManager::getInstance().init(SCREEN_W, PLOT_H, TILE_SIZE);

    // Create footer sprite.
    footer.createSprite(SCREEN_W, FOOTER_H);
    footer.setColorDepth(16);

    for(int i=0; i<MAX_FIELDS; i++) fieldHistory[i].assign(SCREEN_W, NAN);
}

void loop() {
    if (digitalRead(BTN_FIELD) == LOW) {
        if (!currentFields.empty()) {
            selectedField = (selectedField + 1) % currentFields.size();
            tft.fillScreen(TFT_BLACK);
            TileManager::getInstance().clear(C_BLACK);
            // Don't clear history, just force a redraw
            lastMinV = 0; lastMaxV = 0; lastY = -1;
        }
        delay(300);
    }
    if (digitalRead(BTN_GRAPH) == LOW) {
        currentGraphType = (GraphType)((currentGraphType + 1) % COUNT);
        tft.fillScreen(TFT_BLACK);
        TileManager::getInstance().clear(C_BLACK);
        // Force full redraw on next update
        lastMinV = 0; lastMaxV = 0;
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

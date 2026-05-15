#include <Arduino.h>
#include "target_config.h"
#if TARGET_M5STICKC_PLUS
#include <M5StickCPlus.h>
#endif
#include <Wire.h>
#include <U8g2lib.h>
#include <math.h>
#include "cobra_demo.h"
#include "power_stats.h"

#define POWER_TEST_DEMOS_ENABLED 0

/*
Power consumption:
M5 Stick C Plus LCD Off by LDO2 running demos:
Full black screen for OLED: 53mA total. (no LCD)
Full white screen for OLED: 80mA total. (no LCD)

M5 LCD on default brightness: 147mA
M5 LCD on brightness 1 : 107mA

M5 LCD elite fps 27 fps

ESP32-C3 elite graphics: 26mA
ESP32-C3 full black: 26mA
ESP32-C3 full white: 48mA

ESP32-C3 elite graphics, 10 MHz: 9mA      4 fps
ESP32-C3 elite graphics, 20 MHz: 11mA     7 - 8 fps
ESP32-C3 elite graphics, 40 MHz: 13mA    12 - 13 fps
ESP32-C3 elite graphics, 80 MHz: 20mA    19 - 20 fps
ESP32-C3 elite graphics, 160 Mhz: 26mA   25 - 27 fps


*/

// U8G2_R0 == No rotation; U8g2 HW_I2C takes 8-bit address (0x78 = 7-bit 0x3C << 1)
U8G2_SSD1306_128X64_NONAME_F_HW_I2C tinyOLEDDisplay(U8G2_R0, U8X8_PIN_NONE, OLED_SCK, OLED_SDA);

bool oledReady = false;

typedef void (*DemoRenderFn)(uint32_t localMs, bool firstFrame);

struct DemoPage {
    const char *name;
    DemoRenderFn render;
    uint32_t durationMs;
};

#if POWER_TEST_DEMOS_ENABLED
void demoPowerBlack(uint32_t localMs, bool firstFrame);
void demoPowerWhite(uint32_t localMs, bool firstFrame);
#endif
void demoScrolling(uint32_t localMs, bool firstFrame);
void demoShapes(uint32_t localMs, bool firstFrame);
void demoXorMasks(uint32_t localMs, bool firstFrame);
void demoInversion(uint32_t localMs, bool firstFrame);
void demoBoingBall(uint32_t localMs, bool firstFrame);
void demoGameOfLife(uint32_t localMs, bool firstFrame);
void demoFern(uint32_t localMs, bool firstFrame);
void demoMountains(uint32_t localMs, bool firstFrame);
void demoStarTunnel(uint32_t localMs, bool firstFrame);
void demoWaveField(uint32_t localMs, bool firstFrame);
void demoLorenz(uint32_t localMs, bool firstFrame);
void demoRossler(uint32_t localMs, bool firstFrame);
void demoFeigenbaum(uint32_t localMs, bool firstFrame);
void demoSierpinski(uint32_t localMs, bool firstFrame);
void demoWeatherSun(uint32_t localMs, bool firstFrame);
void demoWeatherRain(uint32_t localMs, bool firstFrame);
void demoWeatherSnow(uint32_t localMs, bool firstFrame);

const DemoPage demos[] = {
#if POWER_TEST_DEMOS_ENABLED
    {"BLACK", demoPowerBlack, 5000},
    {"WHITE", demoPowerWhite, 5000},
#endif
    {"ELITE", demoEliteLogo, 30000},
    {"COBRA", demoCobraMkIII, 42000},
    {"THARG", demoThargoid, 30000},
    {"SCROLL", demoScrolling, 8000},
    {"SHAPES", demoShapes, 8000},
    {"XOR", demoXorMasks, 8000},
    {"INVERT", demoInversion, 8000},
    {"BOING", demoBoingBall, 12000},
    {"LIFE", demoGameOfLife, 12000},
    {"FERN", demoFern, 10000},
    {"RIDGE", demoMountains, 10000},
    {"STARS", demoStarTunnel, 9000},
    {"WAVES", demoWaveField, 9000},
    {"LORENZ", demoLorenz, 12000},
    {"ROSSLER", demoRossler, 12000},
    {"FEIGEN", demoFeigenbaum, 12000},
    {"SIERP", demoSierpinski, 10000},
    {"SUNNY", demoWeatherSun, 9000},
    {"RAIN", demoWeatherRain, 9000},
    {"SNOW", demoWeatherSnow, 9000},
};

const uint8_t demoCount = sizeof(demos) / sizeof(demos[0]);
uint8_t currentDemo = 0;
uint32_t demoStartedAt = 0;
uint32_t lastFrameAt = 0;
bool demoFirstFrame = true;

bool showingPowerStatsInterstitial = false;
uint32_t powerInterstitialStartedAt = 0;
uint8_t nextDemoAfterPowerStats = 0;

const uint8_t LIFE_WIDTH = 64;
const uint8_t LIFE_HEIGHT = 32;
uint8_t lifeCells[LIFE_WIDTH * LIFE_HEIGHT];
uint8_t lifeNextCells[LIFE_WIDTH * LIFE_HEIGHT];
uint16_t lifeGeneration = 0;
uint16_t lifeStaleSteps = 0;

void startDemo(uint8_t index)
{
    currentDemo = index % demoCount;
    demoStartedAt = millis();
    demoFirstFrame = true;
    beginPowerStatsWindow(demoStartedAt);
}

void drawTitle(const char *title)
{
    tinyOLEDDisplay.setDrawColor(1);
    tinyOLEDDisplay.setFontMode(1);
    tinyOLEDDisplay.setFont(u8g2_font_5x7_tr);
    tinyOLEDDisplay.drawStr(0, 7, title);
    tinyOLEDDisplay.drawHLine(0, 9, OLED_WIDTH);
}

int16_t textWidth(const char *text)
{
    return (int16_t)tinyOLEDDisplay.getStrWidth(text);
}

void drawClippedText(int16_t x, int16_t y, const char *text)
{
    int16_t cursor = x;

    for (const char *p = text; *p != '\0'; p++) {
        uint16_t glyph = (uint8_t)*p;
        char singleGlyph[2] = {*p, '\0'};
        int8_t glyphWidth = tinyOLEDDisplay.getStrWidth(singleGlyph);

        if (cursor > OLED_WIDTH) {
            break;
        }

        if (cursor + glyphWidth >= 0) {
            tinyOLEDDisplay.drawGlyph(cursor, y, glyph);
        }

        cursor += glyphWidth;
    }
}

void drawMarquee(int16_t y, const char *text, uint16_t speedDivisor, uint8_t gap)
{
    int16_t width = textWidth(text);
    int16_t travel = width + gap;
    int16_t x = OLED_WIDTH - (int16_t)((millis() / speedDivisor) % (OLED_WIDTH + travel));

    drawClippedText(x, y, text);
    drawClippedText(x + travel, y, text);
}

void drawTriangleLines(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2)
{
    tinyOLEDDisplay.drawLine(x0, y0, x1, y1);
    tinyOLEDDisplay.drawLine(x1, y1, x2, y2);
    tinyOLEDDisplay.drawLine(x2, y2, x0, y0);
}

void drawHLineClipped(int16_t x, int16_t y, int16_t width)
{
    if (y < 0 || y >= OLED_HEIGHT || width <= 0 || x >= OLED_WIDTH) {
        return;
    }

    if (x < 0) {
        width += x;
        x = 0;
    }

    if (x + width > OLED_WIDTH) {
        width = OLED_WIDTH - x;
    }

    if (width > 0) {
        tinyOLEDDisplay.drawHLine(x, y, width);
    }
}

void drawVLineClipped(int16_t x, int16_t y, int16_t height)
{
    if (x < 0 || x >= OLED_WIDTH || height <= 0 || y >= OLED_HEIGHT) {
        return;
    }

    if (y < 0) {
        height += y;
        y = 0;
    }

    if (y + height > OLED_HEIGHT) {
        height = OLED_HEIGHT - y;
    }

    if (height > 0) {
        tinyOLEDDisplay.drawVLine(x, y, height);
    }
}

void drawBoxClipped(int16_t x, int16_t y, int16_t width, int16_t height)
{
    if (width <= 0 || height <= 0 || x >= OLED_WIDTH || y >= OLED_HEIGHT) {
        return;
    }

    if (x < 0) {
        width += x;
        x = 0;
    }

    if (y < 0) {
        height += y;
        y = 0;
    }

    if (x + width > OLED_WIDTH) {
        width = OLED_WIDTH - x;
    }

    if (y + height > OLED_HEIGHT) {
        height = OLED_HEIGHT - y;
    }

    if (width > 0 && height > 0) {
        tinyOLEDDisplay.drawBox(x, y, width, height);
    }
}

void drawPixelClipped(int16_t x, int16_t y)
{
    if (x >= 0 && x < OLED_WIDTH && y >= 0 && y < OLED_HEIGHT) {
        tinyOLEDDisplay.drawPixel(x, y);
    }
}

void drawSlantedDropClipped(int16_t x, int16_t y)
{
    for (uint8_t i = 0; i < 6; i++) {
        drawPixelClipped(x - (i / 2), y + i);
    }
}

void drawDiamond(int16_t cx, int16_t cy, int16_t r)
{
    tinyOLEDDisplay.drawLine(cx, cy - r, cx + r, cy);
    tinyOLEDDisplay.drawLine(cx + r, cy, cx, cy + r);
    tinyOLEDDisplay.drawLine(cx, cy + r, cx - r, cy);
    tinyOLEDDisplay.drawLine(cx - r, cy, cx, cy - r);
}

void drawFilledDiamond(int16_t cx, int16_t cy, int16_t r)
{
    for (int16_t dy = -r; dy <= r; dy++) {
        int16_t halfWidth = r - abs(dy);
        drawHLineClipped(cx - halfWidth, cy + dy, halfWidth * 2 + 1);
    }
}

void drawProgressBar(uint32_t localMs, uint32_t durationMs)
{
    uint8_t width = (uint8_t)((localMs * OLED_WIDTH) / durationMs);

    tinyOLEDDisplay.setDrawColor(1);
    tinyOLEDDisplay.drawHLine(0, OLED_HEIGHT - 1, width);
}

#if POWER_TEST_DEMOS_ENABLED
void demoPowerBlack(uint32_t localMs, bool firstFrame)
{
    (void)localMs;

    if (!firstFrame) {
        return;
    }

    tinyOLEDDisplay.clearBuffer();
    tinyOLEDDisplay.sendBuffer();
}

void demoPowerWhite(uint32_t localMs, bool firstFrame)
{
    (void)localMs;

    if (!firstFrame) {
        return;
    }

    tinyOLEDDisplay.clearBuffer();
    tinyOLEDDisplay.setDrawColor(1);
    tinyOLEDDisplay.drawBox(0, 0, OLED_WIDTH, OLED_HEIGHT);
    tinyOLEDDisplay.sendBuffer();
}
#endif

uint32_t mixBits(uint32_t value)
{
    value ^= value >> 16;
    value *= 0x7feb352dUL;
    value ^= value >> 15;
    value *= 0x846ca68bUL;
    value ^= value >> 16;
    return value;
}

uint8_t hashByte(uint32_t x, uint32_t y, uint32_t salt)
{
    return (uint8_t)(mixBits(x * 374761393UL + y * 668265263UL + salt * 2246822519UL) >> 24);
}

uint16_t lifeIndex(uint8_t x, uint8_t y)
{
    return y * LIFE_WIDTH + x;
}

void setLifeCell(uint8_t x, uint8_t y)
{
    if (x < LIFE_WIDTH && y < LIFE_HEIGHT) {
        lifeCells[lifeIndex(x, y)] = 1;
    }
}

void addLifeGlider(uint8_t x, uint8_t y)
{
    setLifeCell(x + 1, y);
    setLifeCell(x + 2, y + 1);
    setLifeCell(x, y + 2);
    setLifeCell(x + 1, y + 2);
    setLifeCell(x + 2, y + 2);
}

void seedLife(uint32_t salt)
{
    for (uint16_t i = 0; i < LIFE_WIDTH * LIFE_HEIGHT; i++) {
        lifeCells[i] = 0;
        lifeNextCells[i] = 0;
    }

    for (uint8_t y = 0; y < LIFE_HEIGHT; y++) {
        for (uint8_t x = 0; x < LIFE_WIDTH; x++) {
            lifeCells[lifeIndex(x, y)] = hashByte(x, y, salt) < 70 ? 1 : 0;
        }
    }

    addLifeGlider(3, 3);
    addLifeGlider(46, 6);
    addLifeGlider(21, 22);
    lifeGeneration = 0;
    lifeStaleSteps = 0;
}

uint8_t readLifeCell(int16_t x, int16_t y)
{
    if (x < 0) {
        x += LIFE_WIDTH;
    } else if (x >= LIFE_WIDTH) {
        x -= LIFE_WIDTH;
    }

    if (y < 0) {
        y += LIFE_HEIGHT;
    } else if (y >= LIFE_HEIGHT) {
        y -= LIFE_HEIGHT;
    }

    return lifeCells[lifeIndex((uint8_t)x, (uint8_t)y)];
}

void stepLife()
{
    uint16_t liveCount = 0;
    uint16_t changedCount = 0;

    for (uint8_t y = 0; y < LIFE_HEIGHT; y++) {
        for (uint8_t x = 0; x < LIFE_WIDTH; x++) {
            uint8_t neighbours =
                readLifeCell(x - 1, y - 1) + readLifeCell(x, y - 1) + readLifeCell(x + 1, y - 1) +
                readLifeCell(x - 1, y) + readLifeCell(x + 1, y) +
                readLifeCell(x - 1, y + 1) + readLifeCell(x, y + 1) + readLifeCell(x + 1, y + 1);
            uint8_t alive = readLifeCell(x, y);
            uint8_t nextAlive = (neighbours == 3 || (alive && neighbours == 2)) ? 1 : 0;
            uint16_t index = lifeIndex(x, y);

            lifeNextCells[index] = nextAlive;
            liveCount += nextAlive;
            changedCount += lifeCells[index] != nextAlive ? 1 : 0;
        }
    }

    for (uint16_t i = 0; i < LIFE_WIDTH * LIFE_HEIGHT; i++) {
        lifeCells[i] = lifeNextCells[i];
    }

    lifeGeneration++;
    if (changedCount < 8 || liveCount < 18 || liveCount > 1200) {
        lifeStaleSteps++;
    } else {
        lifeStaleSteps = 0;
    }
}

void demoScrolling(uint32_t localMs, bool firstFrame)
{
    (void)firstFrame;

    tinyOLEDDisplay.clearBuffer();
    drawTitle("SCROLLING");

    tinyOLEDDisplay.setFont(u8g2_font_6x10_tf);
    drawMarquee(23, "128 x 64 OLED  /  0.96 inch  /  U8g2", 24, 28);

    tinyOLEDDisplay.setFont(u8g2_font_5x8_tr);
    drawMarquee(38, "smooth-ish horizontal ticker", 15, 24);

    const char *lines[] = {
        "vertical list",
        "tiny pixels",
        "readability",
        "motion test",
        "font rhythm",
    };
    uint8_t verticalOffset = (localMs / 70) % 9;

    for (uint8_t i = 0; i < 5; i++) {
        int16_t y = 55 + (i * 9) - verticalOffset;
        if (y >= 46 && y <= 63) {
            tinyOLEDDisplay.drawStr(2, y, lines[i]);
        }
    }

    for (uint8_t x = 0; x < OLED_WIDTH; x += 4) {
        float wave = sinf((localMs * 0.006f) + (x * 0.14f));
        int16_t y = 49 + (int16_t)(wave * 4.0f);
        tinyOLEDDisplay.drawPixel(x, y);
    }

    drawProgressBar(localMs, demos[currentDemo].durationMs);
    tinyOLEDDisplay.sendBuffer();
}

void demoShapes(uint32_t localMs, bool firstFrame)
{
    (void)firstFrame;

    tinyOLEDDisplay.clearBuffer();
    drawTitle("SHAPES");

    float t = localMs * 0.004f;
    int16_t orbitX = 88 + (int16_t)(cosf(t) * 22.0f);
    int16_t orbitY = 35 + (int16_t)(sinf(t * 1.3f) * 13.0f);
    uint8_t pulse = 4 + ((localMs / 160) % 8);

    tinyOLEDDisplay.drawFrame(2, 14, 31, 19);
    tinyOLEDDisplay.drawBox(7, 19, 21, 9);
    tinyOLEDDisplay.drawRFrame(39, 14, 32, 21, 4);
    tinyOLEDDisplay.drawCircle(55, 25, pulse);
    drawTriangleLines(9, 55, 25, 39, 41, 55);
    drawDiamond(59, 48, 10);
    tinyOLEDDisplay.drawDisc(orbitX, orbitY, 4);
    tinyOLEDDisplay.drawCircle(88, 35, 22);
    tinyOLEDDisplay.drawLine(88, 35, orbitX, orbitY);

    for (uint8_t i = 0; i < 6; i++) {
        int16_t x = 100 + i * 5;
        int16_t y = 58 - ((localMs / (90 + i * 20)) % 28);
        tinyOLEDDisplay.drawVLine(x, y, 6 + i);
    }

    drawProgressBar(localMs, demos[currentDemo].durationMs);
    tinyOLEDDisplay.sendBuffer();
}

void demoXorMasks(uint32_t localMs, bool firstFrame)
{
    (void)firstFrame;

    tinyOLEDDisplay.clearBuffer();
    drawTitle("XOR DRAW COLOR");

    tinyOLEDDisplay.setFont(u8g2_font_6x10_tf);
    tinyOLEDDisplay.drawStr(2, 24, "normal pixels");

    for (uint8_t x = 0; x < OLED_WIDTH; x += 8) {
        tinyOLEDDisplay.drawVLine(x, 12, 50);
    }
    for (uint8_t y = 16; y < OLED_HEIGHT; y += 8) {
        tinyOLEDDisplay.drawHLine(0, y, OLED_WIDTH);
    }

    tinyOLEDDisplay.drawFrame(2, 29, 44, 25);
    tinyOLEDDisplay.drawBox(52, 29, 28, 25);
    tinyOLEDDisplay.drawCircle(103, 42, 14);

    tinyOLEDDisplay.setDrawColor(2);
    int16_t x = (localMs / 22) % (OLED_WIDTH + 36);
    drawBoxClipped(x - 36, 12, 36, 50);
    tinyOLEDDisplay.drawDisc(28 + ((localMs / 35) % 74), 42, 11);
    tinyOLEDDisplay.setFont(u8g2_font_7x13B_tf);
    tinyOLEDDisplay.drawStr(50, 49, "XOR");
    tinyOLEDDisplay.setDrawColor(1);

    drawProgressBar(localMs, demos[currentDemo].durationMs);
    tinyOLEDDisplay.sendBuffer();
}

void demoInversion(uint32_t localMs, bool firstFrame)
{
    (void)firstFrame;

    tinyOLEDDisplay.clearBuffer();
    drawTitle("INVERSION");

    tinyOLEDDisplay.setFont(u8g2_font_6x10_tf);
    tinyOLEDDisplay.drawStr(3, 22, "black on clear");

    tinyOLEDDisplay.drawBox(0, 29, OLED_WIDTH, 21);
    tinyOLEDDisplay.setDrawColor(0);
    tinyOLEDDisplay.drawStr(5, 44, "clear text in a box");
    tinyOLEDDisplay.drawFrame(93, 32, 27, 13);
    tinyOLEDDisplay.setDrawColor(1);

    uint8_t wipeX = (localMs / 24) % OLED_WIDTH;
    tinyOLEDDisplay.setDrawColor(2);
    tinyOLEDDisplay.drawBox(wipeX, 11, 22, 48);
    tinyOLEDDisplay.drawFrame(2 + ((localMs / 90) % 30), 53, 94, 8);
    tinyOLEDDisplay.setDrawColor(1);

    for (uint8_t i = 0; i < 8; i++) {
        uint8_t x = 106 + (i % 4) * 5;
        uint8_t y = 53 + (i / 4) * 5;
        if (((localMs / 180) + i) % 2 == 0) {
            tinyOLEDDisplay.drawBox(x, y, 3, 3);
        } else {
            tinyOLEDDisplay.drawFrame(x, y, 3, 3);
        }
    }

    drawProgressBar(localMs, demos[currentDemo].durationMs);
    tinyOLEDDisplay.sendBuffer();
}

void drawPerspectiveFloor()
{
    const uint8_t horizon = 44;

    tinyOLEDDisplay.drawHLine(0, horizon, OLED_WIDTH);

    for (uint8_t i = 0; i < 7; i++) {
        int16_t x = 18 + i * 15;
        tinyOLEDDisplay.drawLine(64, horizon, x, OLED_HEIGHT - 1);
    }

    for (uint8_t y = horizon + 3; y < OLED_HEIGHT; y += 5) {
        uint8_t inset = (uint8_t)((y - horizon) * 2);
        tinyOLEDDisplay.drawHLine(inset, y, OLED_WIDTH - inset * 2);
    }
}

void drawShadow(int16_t cx, int16_t cy, uint8_t r)
{
    tinyOLEDDisplay.setDrawColor(1);
    for (int8_t dy = -2; dy <= 2; dy++) {
        uint8_t width = (r * 2) - abs(dy) * 4;
        drawHLineClipped(cx - (width / 2), cy + dy, width);
    }
}

void drawBoingBallSprite(int16_t cx, int16_t cy, uint8_t r, uint32_t localMs)
{
    uint8_t phase = (localMs / 55) % 8;

    tinyOLEDDisplay.setDrawColor(1);
    tinyOLEDDisplay.drawCircle(cx, cy, r);

    for (int8_t y = -r; y <= (int8_t)r; y++) {
        for (int8_t x = -r; x <= (int8_t)r; x++) {
            if ((x * x) + (y * y) <= (r * r)) {
                bool lit = ((((x + phase) / 4) + ((y + 12) / 4)) & 1) == 0;
                tinyOLEDDisplay.setDrawColor(lit ? 1 : 0);
                tinyOLEDDisplay.drawPixel(cx + x, cy + y);
            }
        }
    }

    tinyOLEDDisplay.setDrawColor(1);
    tinyOLEDDisplay.drawCircle(cx, cy, r);
    tinyOLEDDisplay.drawPixel(cx - 3, cy - 3);
}

void demoBoingBall(uint32_t localMs, bool firstFrame)
{
    static float x = 18.0f;
    static float y = 20.0f;
    static float vx = 46.0f;
    static float vy = 0.0f;
    static uint32_t lastPhysicsAt = 0;

    const uint8_t radius = 8;
    const float gravity = 92.0f;
    const float floorY = 55.0f - radius;
    const float leftWall = radius + 1.0f;
    const float rightWall = OLED_WIDTH - radius - 1.0f;

    if (firstFrame) {
        x = 18.0f;
        y = 18.0f;
        vx = 48.0f;
        vy = -22.0f;
        lastPhysicsAt = millis();
    }

    uint32_t now = millis();
    float dt = (now - lastPhysicsAt) / 1000.0f;
    lastPhysicsAt = now;

    if (dt > 0.05f) {
        dt = 0.05f;
    }

    vy += gravity * dt;
    x += vx * dt;
    y += vy * dt;

    if (x < leftWall) {
        x = leftWall;
        vx = fabsf(vx) * 0.96f;
    } else if (x > rightWall) {
        x = rightWall;
        vx = -fabsf(vx) * 0.96f;
    }

    if (y > floorY) {
        y = floorY;
        vy = -fabsf(vy) * 0.82f;

        if (fabsf(vy) < 20.0f) {
            vy = -52.0f;
        }

        vx *= 0.985f;
        if (fabsf(vx) < 28.0f) {
            vx = vx < 0.0f ? -48.0f : 48.0f;
        }
    }

    tinyOLEDDisplay.clearBuffer();

    tinyOLEDDisplay.setFont(u8g2_font_5x7_tr);
    tinyOLEDDisplay.drawStr(0, 7, "AMIGA-ISH BOING");
    tinyOLEDDisplay.drawHLine(0, 9, OLED_WIDTH);

    drawPerspectiveFloor();
    drawShadow((int16_t)x, 59, radius + 5);
    drawBoingBallSprite((int16_t)x, (int16_t)y, radius, localMs);

    tinyOLEDDisplay.setDrawColor(2);
    drawFilledDiamond(112, 20 + (int16_t)(sinf(localMs * 0.006f) * 5.0f), 6);
    tinyOLEDDisplay.setDrawColor(1);

    drawProgressBar(localMs, demos[currentDemo].durationMs);
    tinyOLEDDisplay.sendBuffer();
}

void demoGameOfLife(uint32_t localMs, bool firstFrame)
{
    static uint32_t lastStepMs = 0;

    if (firstFrame) {
        seedLife(millis());
        lastStepMs = 0;
    }

    while (localMs - lastStepMs >= 90) {
        stepLife();
        lastStepMs += 90;

        if (lifeStaleSteps > 20 || lifeGeneration > 220) {
            seedLife(millis() ^ (uint32_t)lifeGeneration);
            lastStepMs = localMs;
            break;
        }
    }

    tinyOLEDDisplay.clearBuffer();

    for (uint8_t y = 0; y < LIFE_HEIGHT; y++) {
        for (uint8_t x = 0; x < LIFE_WIDTH; x++) {
            if (lifeCells[lifeIndex(x, y)]) {
                tinyOLEDDisplay.drawBox(x * 2, y * 2, 2, 2);
            }
        }
    }

    tinyOLEDDisplay.setDrawColor(2);
    tinyOLEDDisplay.setFont(u8g2_font_5x7_tr);
    tinyOLEDDisplay.drawStr(1, 7, "CONWAY LIFE");
    tinyOLEDDisplay.drawFrame(0, 0, 58, 9);
    tinyOLEDDisplay.setDrawColor(1);

    drawProgressBar(localMs, demos[currentDemo].durationMs);
    tinyOLEDDisplay.sendBuffer();
}

uint32_t nextLcg(uint32_t &state)
{
    state = state * 1664525UL + 1013904223UL;
    return state;
}

void demoFern(uint32_t localMs, bool firstFrame)
{
    static float x = 0.0f;
    static float y = 0.0f;
    static uint32_t randomState = 1;

    if (firstFrame) {
        x = 0.0f;
        y = 0.0f;
        randomState = mixBits(millis() ^ 0x5eed1234UL);
    }

    tinyOLEDDisplay.clearBuffer();

    float wind = sinf(localMs * 0.002f) * 5.0f;
    for (uint16_t i = 0; i < 850; i++) {
        uint8_t r = (uint8_t)(nextLcg(randomState) >> 24);
        float nextX;
        float nextY;

        if (r < 3) {
            nextX = 0.0f;
            nextY = 0.16f * y;
        } else if (r < 220) {
            nextX = 0.85f * x + 0.04f * y;
            nextY = -0.04f * x + 0.85f * y + 1.6f;
        } else if (r < 238) {
            nextX = 0.2f * x - 0.26f * y;
            nextY = 0.23f * x + 0.22f * y + 1.6f;
        } else {
            nextX = -0.15f * x + 0.28f * y;
            nextY = 0.26f * x + 0.24f * y + 0.44f;
        }

        x = nextX;
        y = nextY;

        int16_t px = 64 + (int16_t)(x * 22.0f + wind * (y * 0.08f));
        int16_t py = 63 - (int16_t)(y * 6.15f);
        drawPixelClipped(px, py);

        if ((i & 31) == 0) {
            drawPixelClipped(px + 1, py);
        }
    }

    tinyOLEDDisplay.setDrawColor(2);
    tinyOLEDDisplay.setFont(u8g2_font_5x7_tr);
    tinyOLEDDisplay.drawStr(1, 7, "BARNSLEY FERN");
    tinyOLEDDisplay.setDrawColor(1);

    drawProgressBar(localMs, demos[currentDemo].durationMs);
    tinyOLEDDisplay.sendBuffer();
}

int16_t mountainY(uint8_t layer, int16_t x, uint32_t localMs)
{
    float speed = 0.0015f + layer * 0.0016f;
    float xf = x * (0.055f + layer * 0.011f) + localMs * speed;
    float amplitude = 5.0f + layer * 2.5f;
    float y =
        23.0f + layer * 10.0f +
        sinf(xf + layer * 1.7f) * amplitude +
        sinf(xf * 2.31f + layer * 3.2f) * (amplitude * 0.45f) +
        sinf(xf * 5.13f + layer) * 1.7f;

    if (y < 12.0f) {
        y = 12.0f;
    } else if (y > 63.0f) {
        y = 63.0f;
    }

    return (int16_t)y;
}

void demoMountains(uint32_t localMs, bool firstFrame)
{
    (void)firstFrame;

    tinyOLEDDisplay.clearBuffer();

    int16_t moonY = 16 + (int16_t)(sinf(localMs * 0.001f) * 4.0f);
    tinyOLEDDisplay.drawCircle(105, moonY, 8);
    tinyOLEDDisplay.drawCircle(108, moonY - 2, 7);

    for (uint8_t i = 0; i < 24; i++) {
        uint8_t x = hashByte(i, 1, 44);
        uint8_t y = 11 + (hashByte(i, 2, 98) % 20);
        if (((localMs / 250) + i) % 3 != 0) {
            drawPixelClipped(x, y);
        }
    }

    for (uint8_t layer = 0; layer < 4; layer++) {
        int16_t previousY = mountainY(layer, 0, localMs);
        for (uint8_t x = 1; x < OLED_WIDTH; x++) {
            int16_t y = mountainY(layer, x, localMs);
            tinyOLEDDisplay.drawLine(x - 1, previousY, x, y);

            if (layer >= 2) {
                uint8_t stride = layer == 2 ? 3 : 2;
                if ((x + layer) % stride == 0) {
                    tinyOLEDDisplay.drawVLine(x, y, OLED_HEIGHT - y);
                }
            } else if ((x + layer) % 12 == 0) {
                tinyOLEDDisplay.drawPixel(x, y + 1);
            }

            previousY = y;
        }
    }

    tinyOLEDDisplay.setDrawColor(2);
    tinyOLEDDisplay.setFont(u8g2_font_5x7_tr);
    tinyOLEDDisplay.drawStr(1, 7, "PARALLAX RIDGE");
    tinyOLEDDisplay.setDrawColor(1);

    drawProgressBar(localMs, demos[currentDemo].durationMs);
    tinyOLEDDisplay.sendBuffer();
}

void demoStarTunnel(uint32_t localMs, bool firstFrame)
{
    (void)firstFrame;

    tinyOLEDDisplay.clearBuffer();

    for (uint8_t i = 0; i < 76; i++) {
        uint32_t h = mixBits(i * 0x9e3779b9UL + 0x144cbc89UL);
        float angle = ((h & 1023) * 0.006135923f) + sinf(localMs * 0.00045f) * 0.25f;
        uint16_t baseRadius = ((h >> 10) % 96);
        uint8_t speedDivisor = 5 + ((h >> 22) & 7);
        uint8_t radius = (baseRadius + (localMs / speedDivisor)) % 96;
        int16_t x = 64 + (int16_t)(cosf(angle) * radius);
        int16_t y = 32 + (int16_t)(sinf(angle) * radius * 0.52f);
        int16_t tailRadius = radius > 6 ? radius - 6 : 0;
        int16_t tailX = 64 + (int16_t)(cosf(angle) * tailRadius);
        int16_t tailY = 32 + (int16_t)(sinf(angle) * tailRadius * 0.52f);

        if (x >= 0 && x < OLED_WIDTH && y >= 0 && y < OLED_HEIGHT) {
            tinyOLEDDisplay.drawLine(tailX, tailY, x, y);

            if (radius > 62) {
                drawBoxClipped(x - 1, y - 1, 3, 3);
            } else if (radius > 38) {
                drawBoxClipped(x, y, 2, 2);
            } else {
                tinyOLEDDisplay.drawPixel(x, y);
            }
        }
    }

    tinyOLEDDisplay.drawCircle(64, 32, 3 + ((localMs / 80) % 9));
    tinyOLEDDisplay.setDrawColor(2);
    tinyOLEDDisplay.setFont(u8g2_font_5x7_tr);
    tinyOLEDDisplay.drawStr(1, 7, "STAR TUNNEL");
    tinyOLEDDisplay.setDrawColor(1);

    drawProgressBar(localMs, demos[currentDemo].durationMs);
    tinyOLEDDisplay.sendBuffer();
}

void demoWaveField(uint32_t localMs, bool firstFrame)
{
    (void)firstFrame;

    tinyOLEDDisplay.clearBuffer();

    float t = localMs * 0.005f;
    for (uint8_t y = 10; y < OLED_HEIGHT; y += 2) {
        for (uint8_t x = 0; x < OLED_WIDTH; x += 2) {
            float v =
                sinf(x * 0.19f + t) +
                sinf(y * 0.31f - t * 1.2f) +
                sinf((x + y) * 0.085f + t * 0.7f);

            if (v > 1.45f || v < -2.45f) {
                tinyOLEDDisplay.drawPixel(x, y);
                if (v > 2.35f) {
                    tinyOLEDDisplay.drawPixel(x + 1, y);
                }
            }
        }
    }

    int16_t orbX = 64 + (int16_t)(sinf(t * 0.7f) * 30.0f);
    int16_t orbY = 37 + (int16_t)(cosf(t * 0.9f) * 14.0f);
    tinyOLEDDisplay.setDrawColor(2);
    tinyOLEDDisplay.drawCircle(orbX, orbY, 13);
    tinyOLEDDisplay.drawDisc(64, 37, 5 + ((localMs / 120) % 9));
    tinyOLEDDisplay.setDrawColor(1);

    tinyOLEDDisplay.setFont(u8g2_font_5x7_tr);
    tinyOLEDDisplay.drawStr(1, 7, "INTERFERENCE");
    tinyOLEDDisplay.drawHLine(0, 9, OLED_WIDTH);

    drawProgressBar(localMs, demos[currentDemo].durationMs);
    tinyOLEDDisplay.sendBuffer();
}

bool pointOnScreen(int16_t x, int16_t y)
{
    return x >= 0 && x < OLED_WIDTH && y >= 0 && y < OLED_HEIGHT;
}

void drawTracePoint(int16_t x, int16_t y, bool bright)
{
    drawPixelClipped(x, y);

    if (bright) {
        drawPixelClipped(x + 1, y);
        drawPixelClipped(x, y + 1);
    }
}

void demoLorenz(uint32_t localMs, bool firstFrame)
{
    (void)firstFrame;

    tinyOLEDDisplay.clearBuffer();

    float x = 0.1f;
    float y = 0.0f;
    float z = 0.0f;
    float angle = localMs * 0.00045f;
    float ca = cosf(angle);
    float sa = sinf(angle);
    int16_t lastX = 0;
    int16_t lastY = 0;
    bool hasLast = false;

    for (uint16_t i = 0; i < 1450; i++) {
        float dx = 10.0f * (y - x);
        float dy = x * (28.0f - z) - y;
        float dz = x * y - 2.6667f * z;

        x += dx * 0.006f;
        y += dy * 0.006f;
        z += dz * 0.006f;

        if (i > 120) {
            float side = x * ca + y * sa;
            float depth = x * sa - y * ca;
            int16_t px = 64 + (int16_t)(side * 2.05f);
            int16_t py = 41 - (int16_t)((z - 24.0f) * 0.86f + depth * 0.22f);
            bool onScreen = pointOnScreen(px, py);

            if (hasLast && onScreen && pointOnScreen(lastX, lastY) && abs(px - lastX) < 18 && abs(py - lastY) < 18) {
                tinyOLEDDisplay.drawLine(lastX, lastY, px, py);
            } else {
                drawTracePoint(px, py, i > 1320);
            }

            lastX = px;
            lastY = py;
            hasLast = onScreen;
        }
    }

    tinyOLEDDisplay.setDrawColor(2);
    tinyOLEDDisplay.setFont(u8g2_font_5x7_tr);
    tinyOLEDDisplay.drawStr(1, 7, "LORENZ ATTRACTOR");
    tinyOLEDDisplay.drawFrame(0, 0, 80, 9);
    tinyOLEDDisplay.setDrawColor(1);

    drawProgressBar(localMs, demos[currentDemo].durationMs);
    tinyOLEDDisplay.sendBuffer();
}

void demoRossler(uint32_t localMs, bool firstFrame)
{
    (void)firstFrame;

    tinyOLEDDisplay.clearBuffer();

    float x = 0.2f;
    float y = 0.0f;
    float z = 0.2f;
    float angle = localMs * 0.00055f;
    float ca = cosf(angle);
    float sa = sinf(angle);
    int16_t lastX = 0;
    int16_t lastY = 0;
    bool hasLast = false;

    for (uint16_t i = 0; i < 1650; i++) {
        float dx = -y - z;
        float dy = x + 0.2f * y;
        float dz = 0.2f + z * (x - 5.7f);

        x += dx * 0.025f;
        y += dy * 0.025f;
        z += dz * 0.025f;

        if (i > 180) {
            float xr = x * ca - y * sa;
            float yr = x * sa + y * ca;
            int16_t px = 64 + (int16_t)(xr * 5.2f);
            int16_t py = 34 + (int16_t)(yr * 2.45f - z * 0.75f);
            bool onScreen = pointOnScreen(px, py);

            if (hasLast && onScreen && pointOnScreen(lastX, lastY) && abs(px - lastX) < 18 && abs(py - lastY) < 18) {
                tinyOLEDDisplay.drawLine(lastX, lastY, px, py);
            } else {
                drawTracePoint(px, py, i > 1520);
            }

            lastX = px;
            lastY = py;
            hasLast = onScreen;
        }
    }

    uint8_t pulse = 3 + ((localMs / 100) % 6);
    tinyOLEDDisplay.setDrawColor(2);
    tinyOLEDDisplay.drawCircle(105, 18, pulse);
    tinyOLEDDisplay.setFont(u8g2_font_5x7_tr);
    tinyOLEDDisplay.drawStr(1, 7, "ROSSLER FLOW");
    tinyOLEDDisplay.setDrawColor(1);

    drawProgressBar(localMs, demos[currentDemo].durationMs);
    tinyOLEDDisplay.sendBuffer();
}

void demoFeigenbaum(uint32_t localMs, bool firstFrame)
{
    (void)firstFrame;

    tinyOLEDDisplay.clearBuffer();

    float rMin = 2.78f + sinf(localMs * 0.00037f) * 0.04f;
    float rMax = 4.0f;

    for (uint8_t sx = 0; sx < OLED_WIDTH; sx++) {
        float r = rMin + (rMax - rMin) * ((float)sx / (OLED_WIDTH - 1));
        float x = 0.5f;

        for (uint8_t i = 0; i < 95; i++) {
            x = r * x * (1.0f - x);
        }

        for (uint8_t i = 0; i < 34; i++) {
            x = r * x * (1.0f - x);
            int16_t y = 62 - (int16_t)(x * 50.0f);
            drawPixelClipped(sx, y);
        }
    }

    uint8_t sweep = (localMs / 45) % OLED_WIDTH;
    tinyOLEDDisplay.setDrawColor(2);
    tinyOLEDDisplay.drawVLine(sweep, 11, 51);
    tinyOLEDDisplay.drawHLine(0, 62, OLED_WIDTH);
    tinyOLEDDisplay.setFont(u8g2_font_5x7_tr);
    tinyOLEDDisplay.drawStr(1, 7, "FEIGENBAUM");
    tinyOLEDDisplay.drawStr(91, 7, "r");
    tinyOLEDDisplay.setDrawColor(1);

    drawProgressBar(localMs, demos[currentDemo].durationMs);
    tinyOLEDDisplay.sendBuffer();
}

void demoSierpinski(uint32_t localMs, bool firstFrame)
{
    (void)firstFrame;

    tinyOLEDDisplay.clearBuffer();

    const int16_t vx[] = {64, 6, 122};
    const int16_t vy[] = {11, 62, 62};
    float x = 64.0f;
    float y = 36.0f;
    uint32_t randomState = 0x51e7f00dUL;
    uint16_t points = 280 + (uint16_t)((localMs * 1900UL) / demos[currentDemo].durationMs);

    if (points > 2200) {
        points = 2200;
    }

    tinyOLEDDisplay.drawLine(vx[0], vy[0], vx[1], vy[1]);
    tinyOLEDDisplay.drawLine(vx[1], vy[1], vx[2], vy[2]);
    tinyOLEDDisplay.drawLine(vx[2], vy[2], vx[0], vy[0]);

    for (uint16_t i = 0; i < points; i++) {
        uint8_t corner = nextLcg(randomState) % 3;
        x = (x + vx[corner]) * 0.5f;
        y = (y + vy[corner]) * 0.5f;

        if (i > 8) {
            drawPixelClipped((int16_t)x, (int16_t)y);
        }
    }

    tinyOLEDDisplay.setDrawColor(2);
    tinyOLEDDisplay.drawDisc((int16_t)x, (int16_t)y, 3);
    tinyOLEDDisplay.setFont(u8g2_font_5x7_tr);
    tinyOLEDDisplay.drawStr(1, 7, "SIERPINSKI");
    tinyOLEDDisplay.setDrawColor(1);

    drawProgressBar(localMs, demos[currentDemo].durationMs);
    tinyOLEDDisplay.sendBuffer();
}

void drawSunIcon(int16_t cx, int16_t cy, uint8_t radius, uint32_t localMs)
{
    float phase = localMs * 0.003f;

    tinyOLEDDisplay.drawDisc(cx, cy, radius);
    tinyOLEDDisplay.setDrawColor(2);
    tinyOLEDDisplay.drawCircle(cx, cy, radius + 2);
    tinyOLEDDisplay.setDrawColor(1);

    for (uint8_t i = 0; i < 8; i++) {
        float angle = phase + i * 0.785398f;
        int16_t x1 = cx + (int16_t)(cosf(angle) * (radius + 4));
        int16_t y1 = cy + (int16_t)(sinf(angle) * (radius + 4));
        int16_t x2 = cx + (int16_t)(cosf(angle) * (radius + 9));
        int16_t y2 = cy + (int16_t)(sinf(angle) * (radius + 9));
        tinyOLEDDisplay.drawLine(x1, y1, x2, y2);
    }
}

void drawCloudIcon(int16_t x, int16_t y, bool filled)
{
    if (filled) {
        tinyOLEDDisplay.drawDisc(x + 14, y + 12, 9);
        tinyOLEDDisplay.drawDisc(x + 28, y + 9, 12);
        tinyOLEDDisplay.drawDisc(x + 43, y + 13, 8);
        tinyOLEDDisplay.drawBox(x + 10, y + 13, 40, 10);
    } else {
        tinyOLEDDisplay.drawCircle(x + 14, y + 12, 9);
        tinyOLEDDisplay.drawCircle(x + 28, y + 9, 12);
        tinyOLEDDisplay.drawCircle(x + 43, y + 13, 8);
        tinyOLEDDisplay.drawHLine(x + 8, y + 22, 42);
    }
}

void drawWeatherGraph(uint32_t localMs, int16_t baseY)
{
    int16_t lastX = 2;
    int16_t lastY = baseY;

    for (uint8_t x = 2; x < 58; x += 4) {
        int16_t y = baseY + (int16_t)(sinf(localMs * 0.002f + x * 0.34f) * 5.0f);
        tinyOLEDDisplay.drawLine(lastX, lastY, x, y);
        lastX = x;
        lastY = y;
    }
}

void demoWeatherSun(uint32_t localMs, bool firstFrame)
{
    (void)firstFrame;

    tinyOLEDDisplay.clearBuffer();

    drawSunIcon(30, 28, 8, localMs);
    drawCloudIcon(61, 22 + (int16_t)(sinf(localMs * 0.002f) * 2.0f), false);

    tinyOLEDDisplay.setFont(u8g2_font_7x13B_tf);
    tinyOLEDDisplay.drawStr(3, 56, "72F");
    tinyOLEDDisplay.setFont(u8g2_font_5x7_tr);
    tinyOLEDDisplay.drawStr(31, 56, "clear");
    tinyOLEDDisplay.drawStr(77, 56, "UV 6");
    drawWeatherGraph(localMs, 47);

    tinyOLEDDisplay.setDrawColor(2);
    tinyOLEDDisplay.drawBox(0, 0, 64, 9);
    tinyOLEDDisplay.setFont(u8g2_font_5x7_tr);
    tinyOLEDDisplay.drawStr(2, 7, "WEATHER NOW");
    tinyOLEDDisplay.setDrawColor(1);

    drawProgressBar(localMs, demos[currentDemo].durationMs);
    tinyOLEDDisplay.sendBuffer();
}

void demoWeatherRain(uint32_t localMs, bool firstFrame)
{
    (void)firstFrame;

    tinyOLEDDisplay.clearBuffer();

    drawCloudIcon(37, 8, true);

    tinyOLEDDisplay.setDrawColor(0);
    tinyOLEDDisplay.drawCircle(63, 16, 9);
    tinyOLEDDisplay.setDrawColor(1);

    for (uint8_t i = 0; i < 22; i++) {
        uint8_t seedX = hashByte(i, 9, 22) % OLED_WIDTH;
        int16_t y = 29 + ((localMs / (18 + (i % 5))) + hashByte(i, 4, 11)) % 34;
        int16_t x = seedX + (int16_t)(sinf(localMs * 0.001f) * 4.0f);
        drawSlantedDropClipped(x, y);
    }

    if ((localMs % 2200) < 420) {
        tinyOLEDDisplay.setDrawColor(2);
        tinyOLEDDisplay.drawLine(68, 26, 59, 39);
        tinyOLEDDisplay.drawLine(59, 39, 68, 39);
        tinyOLEDDisplay.drawLine(68, 39, 54, 59);
        tinyOLEDDisplay.setDrawColor(1);
    }

    tinyOLEDDisplay.setFont(u8g2_font_7x13B_tf);
    tinyOLEDDisplay.drawStr(3, 21, "58F");
    tinyOLEDDisplay.setFont(u8g2_font_5x7_tr);
    tinyOLEDDisplay.drawStr(4, 31, "rain");
    tinyOLEDDisplay.drawStr(3, 61, "gusts 18");

    for (uint8_t x = 83; x < 124; x += 6) {
        uint8_t h = 3 + ((hashByte(x, localMs / 300, 7) % 14));
        tinyOLEDDisplay.drawVLine(x, 62 - h, h);
    }

    tinyOLEDDisplay.setDrawColor(2);
    tinyOLEDDisplay.drawBox(0, 0, 35, 9);
    tinyOLEDDisplay.setFont(u8g2_font_5x7_tr);
    tinyOLEDDisplay.drawStr(2, 7, "STORM");
    tinyOLEDDisplay.setDrawColor(1);

    drawProgressBar(localMs, demos[currentDemo].durationMs);
    tinyOLEDDisplay.sendBuffer();
}

void drawSnowflake(int16_t x, int16_t y, uint8_t size)
{
    drawPixelClipped(x, y);
    drawHLineClipped(x - size, y, size * 2 + 1);
    drawVLineClipped(x, y - size, size * 2 + 1);

    if (size > 1) {
        drawPixelClipped(x - 1, y - 1);
        drawPixelClipped(x + 1, y - 1);
        drawPixelClipped(x - 1, y + 1);
        drawPixelClipped(x + 1, y + 1);
    }
}

void demoWeatherSnow(uint32_t localMs, bool firstFrame)
{
    (void)firstFrame;

    tinyOLEDDisplay.clearBuffer();

    tinyOLEDDisplay.drawCircle(105, 15, 9);
    tinyOLEDDisplay.setDrawColor(0);
    tinyOLEDDisplay.drawDisc(109, 12, 9);
    tinyOLEDDisplay.setDrawColor(1);
    drawCloudIcon(48, 12, false);

    for (uint8_t i = 0; i < 34; i++) {
        uint8_t baseX = hashByte(i, 17, 91) % OLED_WIDTH;
        int16_t x = baseX + (int16_t)(sinf(localMs * 0.0016f + i) * 5.0f);
        int16_t y = 11 + ((localMs / (42 + (i % 7) * 6)) + hashByte(i, 23, 41)) % 53;
        drawSnowflake(x, y, 1 + (i % 3 == 0 ? 1 : 0));
    }

    for (uint8_t i = 0; i < 5; i++) {
        int16_t y = 40 + i * 4;
        int16_t offset = (localMs / (45 + i * 8)) % 24;
        drawHLineClipped(73 - offset, y, 28);
        drawHLineClipped(105 - offset, y + 1, 18);
    }

    tinyOLEDDisplay.setFont(u8g2_font_7x13B_tf);
    tinyOLEDDisplay.drawStr(3, 23, "28F");
    tinyOLEDDisplay.setFont(u8g2_font_5x7_tr);
    tinyOLEDDisplay.drawStr(4, 33, "snow");
    tinyOLEDDisplay.drawStr(3, 61, "wind chill");

    tinyOLEDDisplay.setDrawColor(2);
    tinyOLEDDisplay.drawBox(0, 0, 44, 9);
    tinyOLEDDisplay.setFont(u8g2_font_5x7_tr);
    tinyOLEDDisplay.drawStr(2, 7, "WINTER");
    tinyOLEDDisplay.setDrawColor(1);

    drawProgressBar(localMs, demos[currentDemo].durationMs);
    tinyOLEDDisplay.sendBuffer();
}

void testCour8()
{
    tinyOLEDDisplay.clearBuffer();
    tinyOLEDDisplay.setFont(u8g2_font_courR08_tf);
    int8_t height = tinyOLEDDisplay.getMaxCharHeight();
    int8_t y_pos = 0;

    // 5 lines fit
    y_pos+=height;
    tinyOLEDDisplay.drawStr(0, y_pos, "012345678901234567890"); // 21 numbers
    y_pos+=height;
    tinyOLEDDisplay.drawStr(0, y_pos, "ABCDEFGHJKLMNOPQRSTUV"); // 21 capital letters
    y_pos+=height;
    tinyOLEDDisplay.drawStr(0, y_pos, "abcdefghjklmnopqrstuv"); // 21 lower case letters
    y_pos+=height;
    tinyOLEDDisplay.drawStr(0, y_pos, "abcdefghjklmnopqrstuv"); // 21 lower case letters
    y_pos+=height;
    tinyOLEDDisplay.drawStr(0, y_pos, "abcdefghjklmnopqrstuv"); // 21 lower case letters
    y_pos+=height;

    tinyOLEDDisplay.sendBuffer();  
}

void testCour10()
{
    tinyOLEDDisplay.clearBuffer();
    tinyOLEDDisplay.setFont(u8g2_font_courR10_tf);
    int8_t height = tinyOLEDDisplay.getMaxCharHeight();
    int8_t y_pos = 0;

    // 4 lines fit
    y_pos+=height;
    tinyOLEDDisplay.drawStr(0, y_pos, "01234567890123"); // 14 numbers
    y_pos+=height;
    tinyOLEDDisplay.drawStr(0, y_pos, "ABCDEFGHJKLMNO"); // 14 capital letters
    y_pos+=height;
    tinyOLEDDisplay.drawStr(0, y_pos, "abcdefghjklmn"); // 14 lower case letters
    y_pos+=height;
    tinyOLEDDisplay.drawStr(0, y_pos, "abcdefghjklmn"); // 14 lower case letters

    tinyOLEDDisplay.sendBuffer();  
}

void testHelv10()
{
    tinyOLEDDisplay.clearBuffer();
    tinyOLEDDisplay.setFont(u8g2_font_helvR10_tr);
    int8_t height = tinyOLEDDisplay.getMaxCharHeight();
    int8_t y_pos = 0;

    // 4 lines fit
    y_pos+=height;
    tinyOLEDDisplay.drawStr(0, y_pos, "0123456789012345"); // 16 numbers
    y_pos+=height;
    tinyOLEDDisplay.drawStr(0, y_pos, "ABCDEFGHJKLMN"); // 13 capital letters
    y_pos+=height;
    tinyOLEDDisplay.drawStr(0, y_pos, "abcdefghjklmnopqrs"); // 18 lower case letters
    y_pos+=height;
    tinyOLEDDisplay.drawStr(0, y_pos, "abcdefghjklmnopqrs"); // 18 lower case letters
    y_pos+=height;

    tinyOLEDDisplay.sendBuffer();  
}

void testHelv12()
{
    tinyOLEDDisplay.clearBuffer();
    tinyOLEDDisplay.setFont(u8g2_font_helvR12_tr);
    int8_t height = tinyOLEDDisplay.getMaxCharHeight();
    int8_t y_pos = 0;

    // 3 lines fit
    y_pos+=height;
    tinyOLEDDisplay.drawStr(0, y_pos, "01234567890123"); // 14 numbers
    y_pos+=height;
    tinyOLEDDisplay.drawStr(0, y_pos, "ABCDEFGHJKL"); // 11 capital letters
    y_pos+=height;
    tinyOLEDDisplay.drawStr(0, y_pos, "abcdefghjklmnop"); // 16 lower case letters
    y_pos+=height;

    tinyOLEDDisplay.sendBuffer();  
}

void testHelv14()
{
    // 3 lines fit
    tinyOLEDDisplay.clearBuffer();
    tinyOLEDDisplay.setFont(u8g2_font_helvR14_tr);
    int8_t height = tinyOLEDDisplay.getMaxCharHeight();
    int8_t y_pos = 0;

    y_pos+=height;
    tinyOLEDDisplay.drawStr(0, y_pos, "0123456789012"); // 13 numbers
    y_pos+=height;
    tinyOLEDDisplay.drawStr(0, y_pos, "ABCDEFGHJ"); // 9 capital letters
    y_pos+=height;
    tinyOLEDDisplay.drawStr(0, y_pos, "abcdefghjklmn"); // 13 lower case letters
    y_pos+=height;

    tinyOLEDDisplay.sendBuffer();  
}

void testHelv18()
{
    // 3 lines fit
    tinyOLEDDisplay.clearBuffer();
    tinyOLEDDisplay.setFont(u8g2_font_helvR18_tr);
    int8_t height = tinyOLEDDisplay.getMaxCharHeight();
    int8_t y_pos = 0;

    // 2 lines fit
    y_pos+=height;
    tinyOLEDDisplay.drawStr(0, y_pos, "0123456789"); // 10 numbers
    y_pos+=height;
    tinyOLEDDisplay.drawStr(0, y_pos, "ABCDEFG"); // 6 capital letters
    y_pos+=height;
//    tinyOLEDDisplay.drawStr(0, y_pos, "abcdefghjk"); // 10 lower case letters
//    y_pos+=height;

    tinyOLEDDisplay.sendBuffer();  
}

void testHelv24()
{
    tinyOLEDDisplay.clearBuffer();
    tinyOLEDDisplay.setFont(u8g2_font_helvR24_tr);
    int8_t height = tinyOLEDDisplay.getMaxCharHeight();
    int8_t y_pos = 0;

    // 1 line fits
    y_pos+=height;
    tinyOLEDDisplay.drawStr(0, y_pos, "0123456"); // 7 numbers
//    y_pos+=height;
//    tinyOLEDDisplay.drawStr(0, y_pos, "ABCDE"); // 5 capital letters
//    y_pos+=height;
//    tinyOLEDDisplay.drawStr(0, y_pos, "abcdefg"); // 7 lower case letters

    tinyOLEDDisplay.sendBuffer();  
}

void testTimB14()
{
    // 3 lines fit
    tinyOLEDDisplay.clearBuffer();
    tinyOLEDDisplay.setFont(u8g2_font_timB14_tr);
    int8_t height = tinyOLEDDisplay.getMaxCharHeight();
    int8_t y_pos = 0;

    y_pos+=height;
    tinyOLEDDisplay.drawStr(0, y_pos, "0123456789012"); // 13 numbers
    y_pos+=height;
    tinyOLEDDisplay.drawStr(0, y_pos, "ABCDEFGHJ"); // 9 capital letters
    y_pos+=height;
    tinyOLEDDisplay.drawStr(0, y_pos, "abcdefghjklmn"); // 13 lower case letters
    y_pos+=height;

    tinyOLEDDisplay.sendBuffer();  
}

void testNcenR14()
{
    // 2 lines fit
    tinyOLEDDisplay.clearBuffer();
    tinyOLEDDisplay.setFont(u8g2_font_ncenR14_tf);
    int8_t height = tinyOLEDDisplay.getMaxCharHeight();
    int8_t y_pos = 0;

    y_pos+=height;
    tinyOLEDDisplay.drawStr(0, y_pos, "0123456789012"); // 13 numbers
    y_pos+=height;
    tinyOLEDDisplay.drawStr(0, y_pos, "ABCDEFGHJ"); // 9 capital letters
//    y_pos+=height;
//    tinyOLEDDisplay.drawStr(0, y_pos, "abcdefghjklmn"); // 13 lower case letters

    tinyOLEDDisplay.sendBuffer();  
}

void testFont(const uint8_t font_[])
{
    tinyOLEDDisplay.clearBuffer();
    tinyOLEDDisplay.setFont(font_);
    int8_t height = tinyOLEDDisplay.getMaxCharHeight();
    int8_t y_pos = 0;

    y_pos+=height;
    tinyOLEDDisplay.drawStr(0, y_pos, "012345678901234567890"); // 21 numbers
    y_pos+=height;
    tinyOLEDDisplay.drawStr(0, y_pos, "ABCDEFGHJKLMNOPQRSTUV"); // 21 capital letters
    y_pos+=height;
    tinyOLEDDisplay.drawStr(0, y_pos, "abcdefghjklmnopqrstuv"); // 21 lower case letters
    y_pos+=height;
    tinyOLEDDisplay.drawStr(0, y_pos, "abcdefghjklmnopqrstuv"); // 21 lower case letters
    y_pos+=height;
    tinyOLEDDisplay.drawStr(0, y_pos, "abcdefghjklmnopqrstuv"); // 21 lower case letters
    y_pos+=height;

    tinyOLEDDisplay.sendBuffer();  
}

void scanI2C()
{
  byte error, address;
  int nDevices;
  Serial.println("Scanning...");
  nDevices = 0;
  for(address = 1; address < 127; address++ ) {
    Wire.beginTransmission(address);
    error = Wire.endTransmission();
    if (error == 0) {
      Serial.print("I2C device found at address 0x");
      if (address<16) {
        Serial.print("0");
      }
      Serial.println(address,HEX);
      nDevices++;
    }
    else if (error==4) {
      Serial.print("Unknow error at address 0x");
      if (address<16) {
        Serial.print("0");
      }
      Serial.println(address,HEX);
    }
  }
  if (nDevices == 0) {
    Serial.println("No I2C devices found\n");
  }
  else {
    Serial.println("done\n");
  }
  delay(5000);
}

void setup()
{
    Serial.begin(115200);
/*
#if TARGET_ESP32_C3
    uint32_t serialWaitStartedAt = millis();
    while (!Serial && millis() - serialWaitStartedAt < 2000) {
        delay(10);
    }
    delay(200);
#endif
*/
    Serial.println("OLED demo boot");
    Serial.printf("SDA: %i SCK: %i\n",OLED_SDA, OLED_SCK);

#if TARGET_M5STICKC_PLUS
    M5.begin();
    M5.Lcd.fillScreen(TFT_RED);
    delay(1000);
    M5.Lcd.fillScreen(TFT_BLACK);
//    M5.Axp.ScreenBreath(1);
    M5.Axp.SetLDO2(false);   // cuts LCD logic power
#endif

    tinyOLEDDisplay.setI2CAddress(I2C_ADDRESS);
    
    if (!tinyOLEDDisplay.begin()) {
        Serial.println("U8g2 begin() FAILED");
        return;
    } else {
        Serial.println("U8g2 begin() OK");
    }
    
    setupPowerStats();

    oledReady = true;
    startDemo(0);
    Serial.println("Demo carousel started");

 // OK, but dense: 
// testFont(u8g2_font_Born2bSportyV2_tr);
// Not bad
// testFont(u8g2_font_tallpixelextended_tr);
}

void loop()
{
    if (!oledReady) {
        return;
    }

    uint32_t now = millis();
    if (now - lastFrameAt < 33) {
        return;
    }

    lastFrameAt = now;

    if (showingPowerStatsInterstitial) {
        uint32_t interstitialMs = now - powerInterstitialStartedAt;
        uint32_t interstitialDurationMs = powerStatsInterstitialDurationMs();
        if (interstitialMs >= interstitialDurationMs) {
            showingPowerStatsInterstitial = false;
            startDemo(nextDemoAfterPowerStats);
            return;
        }

        renderPowerStatsInterstitial(tinyOLEDDisplay, interstitialMs, interstitialDurationMs);
        return;
    }

    samplePowerStats(now, false);

    uint32_t localMs = now - demoStartedAt;
    if (localMs >= demos[currentDemo].durationMs) {
        uint8_t nextDemo = (currentDemo + 1) % demoCount;

        finalizePowerStatsWindow(demos[currentDemo].name, now);
        uint32_t interstitialDurationMs = powerStatsInterstitialDurationMs();
        if (powerStatsInterstitialIsEnabled() && interstitialDurationMs > 0) {
            showingPowerStatsInterstitial = true;
            powerInterstitialStartedAt = now;
            nextDemoAfterPowerStats = nextDemo;
            renderPowerStatsInterstitial(tinyOLEDDisplay, 0, interstitialDurationMs);
            return;
        }

        startDemo(nextDemo);
        return;
    }

    demos[currentDemo].render(localMs, demoFirstFrame);
    demoFirstFrame = false;
}

#include "linear_compass_demo.h"

#include "target_config.h"
#include <U8g2lib.h>
#include <math.h>

extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C tinyOLEDDisplay;

namespace {

const uint32_t COMPASS_RENDER_FRAME_MS = 33;
const uint32_t COMPASS_BEARING_UPDATE_THROTTLE_MS = 20;
const float COMPASS_DEFAULT_INERTIA = 0.82f;
const float PIXELS_PER_DEGREE = 1.28f;
const int16_t COMPASS_TAPE_Y_OFFSET = 4;
const int16_t COMPASS_SCROLL_Y_SHIFT = 3;
const int8_t TURN_ANTICLOCKWISE = -1;
const int8_t TURN_NONE = 0;
const int8_t TURN_CLOCKWISE = 1;
const float TARGET_ALIGNED_DEGREES = 7.0f;

// Bottom direction arrow tuning.
const int16_t TURN_ARROW_Y = 58;
const int16_t TURN_ARROW_LENGTH_PX = 27; // was 22
const int16_t TURN_ARROW_LEFT_EDGE_TIP_X = 6;
const int16_t TURN_ARROW_RIGHT_EDGE_TIP_X = 122;
const int16_t TURN_ARROW_HEAD_WIDTH_PX = 10;
const int16_t TURN_ARROW_HEAD_HALF_HEIGHT_PX = 5;
const int16_t TURN_ARROW_STEM_HALF_HEIGHT_PX = 2;
const uint8_t TURN_ARROW_TRAVEL_PX = 8;
const uint16_t TURN_ARROW_PHASE_MS = 45; // was 60
const bool ANIMATE_ARROW_PLAIN_END = true;
const int16_t INWARD_ARROW_LEFT_OUTER_TIP_X = 36;
const int16_t INWARD_ARROW_RIGHT_OUTER_TIP_X = 92;
const uint8_t INWARD_ARROW_TRAVEL_PX = 8;
const uint16_t INWARD_ARROW_PHASE_MS = 45;
const bool TARGET_DRAW_AS_GLYPH = false;
const uint16_t TARGET_GLYPH = 8615;
const int16_t TARGET_GLYPH_BASELINE_Y = 15;
const int16_t TARGET_GLYPH_CLIP_MARGIN = 8;
const uint16_t HOME_GLYPH = 9661;
const int16_t HOME_GLYPH_BASELINE_Y = 15;
const int16_t HOME_GLYPH_CLIP_MARGIN = 8;

TaskHandle_t compassTaskHandle = nullptr;
QueueHandle_t compassBearingQueue = nullptr;
volatile bool compassTaskShouldRun = false;
volatile bool compassTaskRunning = false;
float compassInertia = COMPASS_DEFAULT_INERTIA;
uint32_t lastBearingSentAt = 0;

struct CompassBearingPacket {
    float bearing;
    float targetBearing;
    float homeBearing;
    bool hasHomeBearing;
};

float wrap360(float degrees)
{
    while (degrees < 0.0f) {
        degrees += 360.0f;
    }
    while (degrees >= 360.0f) {
        degrees -= 360.0f;
    }
    return degrees;
}

float angleDelta(float target, float current)
{
    float delta = wrap360(target) - wrap360(current);
    if (delta > 180.0f) {
        delta -= 360.0f;
    } else if (delta < -180.0f) {
        delta += 360.0f;
    }

    return delta;
}

float smoothStep(float t)
{
    if (t <= 0.0f) {
        return 0.0f;
    }
    if (t >= 1.0f) {
        return 1.0f;
    }

    return t * t * (3.0f - 2.0f * t);
}

float lerpBearing(float from, float to, float t)
{
    return wrap360(from + angleDelta(to, from) * smoothStep(t));
}

float segmentBearing(float seconds, float startSeconds, float endSeconds, float from, float to)
{
    float t = (seconds - startSeconds) / (endSeconds - startSeconds);
    return lerpBearing(from, to, t);
}

float scriptedBearing(uint32_t localMs)
{
    float seconds = localMs * 0.001f;
    float bearing = 0.0f;

    if (seconds < 6.0f) {
        bearing = segmentBearing(seconds, 0.0f, 6.0f, 285.0f, 350.0f);
    } else if (seconds < 12.0f) {
        bearing = segmentBearing(seconds, 6.0f, 12.0f, 350.0f, 42.0f);
    } else if (seconds < 18.0f) {
        bearing = segmentBearing(seconds, 12.0f, 18.0f, 42.0f, 94.0f);
    } else if (seconds < 24.0f) {
        bearing = segmentBearing(seconds, 18.0f, 24.0f, 94.0f, 20.0f);
    } else {
        bearing = segmentBearing(seconds, 24.0f, 30.0f, 20.0f, 315.0f);
    }

    float handSway = sinf(seconds * 4.2f) * 1.3f + sinf(seconds * 1.15f) * 0.7f;
    return wrap360(bearing + handSway);
}

float scriptedTargetBearing(uint32_t localMs)
{
    float seconds = (localMs % 30000UL) * 0.001f;
    float target = 0.0f;

    if (seconds < 7.5f) {

//        target = segmentBearing(seconds, 0.0f, 7.5f, 8.0f, 72.0f);
        target = 180;
    } else if (seconds < 15.0f) {
//        target = segmentBearing(seconds, 7.5f, 15.0f, 72.0f, 148.0f);

        target = 180;
    } else if (seconds < 22.5f) {
      target=20;
  //      target = segmentBearing(seconds, 15.0f, 22.5f, 148.0f, 286.0f);
    } else {
//        target = segmentBearing(seconds, 22.5f, 30.0f, 286.0f, 18.0f);
      target=350;
    }
    return target;
//    return wrap360(target + sinf(seconds * 0.9f) * 0.8f);
}

float scriptedHomeBearing(uint32_t localMs)
{
    (void)localMs;
    return 30.0f;
}

const char *cardinalLabel(uint16_t degrees)
{
    switch ((degrees % 360) / 45) {
        case 0:
            return "N";
        case 1:
            return "NE";
        case 2:
            return "E";
        case 3:
            return "SE";
        case 4:
            return "S";
        case 5:
            return "SW";
        case 6:
            return "W";
        default:
            return "NW";
    }
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

void drawFilledTriangleUp(int16_t cx, int16_t apexY, uint8_t halfWidth, uint8_t height)
{
    for (uint8_t row = 0; row < height; row++) {
        uint8_t width = (uint8_t)((row * halfWidth) / (height - 1));
        drawHLineClipped(cx - width, apexY + row, width * 2 + 1);
    }
}

void drawFilledTriangleDown(int16_t cx, int16_t baseY, uint8_t halfWidth, uint8_t height)
{
    for (uint8_t row = 0; row < height; row++) {
        uint8_t width = (uint8_t)(((height - 1 - row) * halfWidth) / (height - 1));
        drawHLineClipped(cx - width, baseY - row, width * 2 + 1);
    }
}

void drawTick(int16_t x, uint16_t degrees)
{
    bool cardinal = (degrees % 45) == 0;
    bool major = (degrees % 10) == 0;
    uint8_t top = cardinal ? 25 + COMPASS_SCROLL_Y_SHIFT : (major ? 27 + COMPASS_SCROLL_Y_SHIFT : 29 + COMPASS_SCROLL_Y_SHIFT);
    uint8_t centerY = 30 + COMPASS_TAPE_Y_OFFSET + COMPASS_SCROLL_Y_SHIFT;

    tinyOLEDDisplay.drawVLine(x, top, centerY - top);

    if (cardinal) {
        tinyOLEDDisplay.setFont(u8g2_font_7x13B_tf);
        char text[4];
        snprintf(text, sizeof(text), "%u", degrees % 360);
        int16_t w = tinyOLEDDisplay.getStrWidth(text);
        tinyOLEDDisplay.drawStr(x - (w / 2), 24 + COMPASS_SCROLL_Y_SHIFT, text);
    }

    if (cardinal) {
        tinyOLEDDisplay.setFont(u8g2_font_7x13B_tf);
        const char *label = cardinalLabel(degrees);
        int16_t w = tinyOLEDDisplay.getStrWidth(label);
        tinyOLEDDisplay.drawStr(x - (w / 2), 43 + COMPASS_TAPE_Y_OFFSET + COMPASS_SCROLL_Y_SHIFT, label);
    }
}

void drawBearingReadout(float bearing, float target)
{
    char text[8];
    snprintf(text, sizeof(text), "%03u", (uint16_t)(bearing + 0.5f) % 360);

    tinyOLEDDisplay.setFont(u8g2_font_9x15B_tf); // was 7x13B
    int16_t w = tinyOLEDDisplay.getStrWidth(text);
    tinyOLEDDisplay.drawStr((OLED_WIDTH - w) / 2, 64, text); // was 61

    tinyOLEDDisplay.setFont(u8g2_font_7x13_t_symbols);
    tinyOLEDDisplay.drawGlyph((OLED_WIDTH + 24) / 2 + 1, 60, 9702); // Degree sign

    /* test commented out*/
//    snprintf(text, sizeof(text), "%03u", (uint16_t)(target + 0.5f) % 360);
//    tinyOLEDDisplay.setFont(u8g2_font_9x15B_tf); // was 7x13B
//    tinyOLEDDisplay.drawStr(OLED_WIDTH - 40, 64, text); // was 61
}

void drawTargetSymbol(int16_t x)
{
    if (x < -6 || x > OLED_WIDTH + 6) {
        return;
    }

    tinyOLEDDisplay.drawCircle(x, 11, 3);
    drawHLineClipped(x - 5, 11, 11);
    if (x >= 0 && x < OLED_WIDTH) {
        tinyOLEDDisplay.drawVLine(x, 8, 7);
    }
}

bool isMarkerVisible(int16_t x, int16_t clipMargin)
{
    return x >= -clipMargin && x <= OLED_WIDTH + clipMargin;
}

bool encodeGlyphUtf8(uint16_t codepoint, char *out, size_t outSize)
{
    if (outSize < 2) {
        return false;
    }

    if (codepoint < 0x80) {
        out[0] = (char)codepoint;
        out[1] = '\0';
        return true;
    }

    if (codepoint < 0x800 && outSize >= 3) {
        out[0] = (char)(0xC0 | (codepoint >> 6));
        out[1] = (char)(0x80 | (codepoint & 0x3F));
        out[2] = '\0';
        return true;
    }

    if (outSize >= 4) {
        out[0] = (char)(0xE0 | (codepoint >> 12));
        out[1] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        out[2] = (char)(0x80 | (codepoint & 0x3F));
        out[3] = '\0';
        return true;
    }

    return false;
}

void drawCenteredGlyphMarker(int16_t x, int16_t baselineY, uint16_t glyph, int16_t clipMargin)
{
    if (!isMarkerVisible(x, clipMargin)) {
        return;
    }

    char glyphText[4];
    if (!encodeGlyphUtf8(glyph, glyphText, sizeof(glyphText))) {
        return;
    }

    tinyOLEDDisplay.setFont(u8g2_font_7x13_t_symbols);
    int16_t glyphWidth = tinyOLEDDisplay.getUTF8Width(glyphText);
    tinyOLEDDisplay.drawUTF8(x - (glyphWidth / 2), baselineY, glyphText);
}

void drawTargetMarker(int16_t x)
{
    if (TARGET_DRAW_AS_GLYPH) {
        drawCenteredGlyphMarker(x, TARGET_GLYPH_BASELINE_Y, TARGET_GLYPH, TARGET_GLYPH_CLIP_MARGIN);
    } else {
        drawTargetSymbol(x);
    }
}

void drawHomeSymbol(int16_t x)
{
    drawCenteredGlyphMarker(x, HOME_GLYPH_BASELINE_Y, HOME_GLYPH, HOME_GLYPH_CLIP_MARGIN);
}

bool isTargetVisible(int16_t x)
{
    return TARGET_DRAW_AS_GLYPH ? isMarkerVisible(x, TARGET_GLYPH_CLIP_MARGIN) : isMarkerVisible(x, 6);
}

int8_t shortestTurnDirection(float delta)
{
    if (delta > 0.0f) {
        return TURN_CLOCKWISE;
    }
    if (delta < 0.0f) {
        return TURN_ANTICLOCKWISE;
    }
    return TURN_NONE;
}

void drawRightArrowHead(int16_t tipX, int16_t y)
{
    for (int16_t x = tipX - TURN_ARROW_HEAD_WIDTH_PX; x <= tipX; x++) {
        int16_t halfHeight = ((tipX - x) * TURN_ARROW_HEAD_HALF_HEIGHT_PX) / TURN_ARROW_HEAD_WIDTH_PX;
        tinyOLEDDisplay.drawVLine(x, y - halfHeight, halfHeight * 2 + 1);
    }
}

void drawLeftArrowHead(int16_t tipX, int16_t y)
{
    for (int16_t x = tipX; x <= tipX + TURN_ARROW_HEAD_WIDTH_PX; x++) {
        int16_t halfHeight = ((x - tipX) * TURN_ARROW_HEAD_HALF_HEIGHT_PX) / TURN_ARROW_HEAD_WIDTH_PX;
        tinyOLEDDisplay.drawVLine(x, y - halfHeight, halfHeight * 2 + 1);
    }
}

void drawRightArrowStem(int16_t tailX, int16_t headBaseX, int16_t y)
{
    if (headBaseX <= tailX + 2) {
        return;
    }

    tinyOLEDDisplay.drawVLine(tailX, y - 1, 3);
    tinyOLEDDisplay.drawVLine(tailX + 1, y - TURN_ARROW_STEM_HALF_HEIGHT_PX, TURN_ARROW_STEM_HALF_HEIGHT_PX * 2 + 1);
    tinyOLEDDisplay.drawBox(tailX + 2, y - TURN_ARROW_STEM_HALF_HEIGHT_PX, headBaseX - tailX - 1, TURN_ARROW_STEM_HALF_HEIGHT_PX * 2 + 1);
}

void drawLeftArrowStem(int16_t headBaseX, int16_t tailX, int16_t y)
{
    if (headBaseX >= tailX - 2) {
        return;
    }

    tinyOLEDDisplay.drawBox(headBaseX, y - TURN_ARROW_STEM_HALF_HEIGHT_PX, tailX - headBaseX - 1, TURN_ARROW_STEM_HALF_HEIGHT_PX * 2 + 1);
    tinyOLEDDisplay.drawVLine(tailX - 1, y - TURN_ARROW_STEM_HALF_HEIGHT_PX, TURN_ARROW_STEM_HALF_HEIGHT_PX * 2 + 1);
    tinyOLEDDisplay.drawVLine(tailX, y - 1, 3);
}

void drawBottomTurnArrow(int8_t direction)
{
    if (direction == TURN_NONE) {
        return;
    }

    const int16_t y = TURN_ARROW_Y;
    const uint16_t phaseMs = TURN_ARROW_PHASE_MS == 0 ? 1 : TURN_ARROW_PHASE_MS;
    const uint8_t phase = (millis() / phaseMs) % (TURN_ARROW_TRAVEL_PX * 2 + 1);
    const uint8_t travel = phase <= TURN_ARROW_TRAVEL_PX ? phase : TURN_ARROW_TRAVEL_PX * 2 - phase;

    if (direction == TURN_CLOCKWISE) {
        const int16_t tipX = TURN_ARROW_RIGHT_EDGE_TIP_X - TURN_ARROW_TRAVEL_PX + travel;
        const int16_t tailX = ANIMATE_ARROW_PLAIN_END
                                  ? tipX - TURN_ARROW_LENGTH_PX
                                  : TURN_ARROW_RIGHT_EDGE_TIP_X - TURN_ARROW_TRAVEL_PX - TURN_ARROW_LENGTH_PX;
        drawRightArrowStem(tailX, tipX - TURN_ARROW_HEAD_WIDTH_PX, y);
        drawRightArrowHead(tipX, y);
    } else {
        const int16_t tipX = TURN_ARROW_LEFT_EDGE_TIP_X + TURN_ARROW_TRAVEL_PX - travel;
        const int16_t tailX = ANIMATE_ARROW_PLAIN_END
                                  ? tipX + TURN_ARROW_LENGTH_PX
                                  : TURN_ARROW_LEFT_EDGE_TIP_X + TURN_ARROW_TRAVEL_PX + TURN_ARROW_LENGTH_PX;
        drawLeftArrowStem(tipX + TURN_ARROW_HEAD_WIDTH_PX, tailX, y);
        drawLeftArrowHead(tipX, y);
    }
}

void drawBottomInwardArrows()
{
    const int16_t y = TURN_ARROW_Y;
    const uint16_t phaseMs = INWARD_ARROW_PHASE_MS == 0 ? 1 : INWARD_ARROW_PHASE_MS;
    const uint8_t phase = (millis() / phaseMs) % (INWARD_ARROW_TRAVEL_PX * 2 + 1);
    const uint8_t travel = phase <= INWARD_ARROW_TRAVEL_PX ? phase : INWARD_ARROW_TRAVEL_PX * 2 - phase;

    const int16_t leftTipX = INWARD_ARROW_LEFT_OUTER_TIP_X + travel;
    const int16_t leftTailX = ANIMATE_ARROW_PLAIN_END
                                  ? leftTipX - TURN_ARROW_LENGTH_PX
                                  : INWARD_ARROW_LEFT_OUTER_TIP_X - TURN_ARROW_LENGTH_PX;
    drawRightArrowStem(leftTailX, leftTipX - TURN_ARROW_HEAD_WIDTH_PX, y);
    drawRightArrowHead(leftTipX, y);

    const int16_t rightTipX = INWARD_ARROW_RIGHT_OUTER_TIP_X - travel;
    const int16_t rightTailX = ANIMATE_ARROW_PLAIN_END
                                   ? rightTipX + TURN_ARROW_LENGTH_PX
                                   : INWARD_ARROW_RIGHT_OUTER_TIP_X + TURN_ARROW_LENGTH_PX;
    drawLeftArrowStem(rightTipX + TURN_ARROW_HEAD_WIDTH_PX, rightTailX, y);
    drawLeftArrowHead(rightTipX, y);
}

void renderCompass(float bearing, float targetBearing, float homeBearing, bool hasHomeBearing)
{
    tinyOLEDDisplay.clearBuffer();
    tinyOLEDDisplay.setDrawColor(1);
    tinyOLEDDisplay.setFontMode(1);

    int16_t startTick = ((int16_t)floorf((bearing - 95.0f) / 5.0f)) * 5;
    for (int16_t tick = startTick; tick <= startTick + 190; tick += 5) {
        uint16_t tickDegrees = (uint16_t)((tick % 360 + 360) % 360);
        int16_t x = 64 + (int16_t)(angleDelta((float)tickDegrees, bearing) * PIXELS_PER_DEGREE);
        if (x >= 5 && x <= 123) {
            drawTick(x, tickDegrees);
        }
    }

    drawFilledTriangleDown(64, 10 + COMPASS_TAPE_Y_OFFSET, 5, 7);

    float targetDelta = angleDelta(targetBearing, bearing);
    bool targetAligned = fabsf(targetDelta) <= TARGET_ALIGNED_DEGREES;
    int8_t targetTurnDirection = shortestTurnDirection(targetDelta);
    int16_t targetX = 64 + (int16_t)(targetDelta * PIXELS_PER_DEGREE);
    int16_t homeX = 64 + (int16_t)(angleDelta(homeBearing, bearing) * PIXELS_PER_DEGREE);
    tinyOLEDDisplay.setDrawColor(2);
    tinyOLEDDisplay.drawVLine(63, 12 + COMPASS_TAPE_Y_OFFSET, 33);
    tinyOLEDDisplay.drawVLine(64, 12 + COMPASS_TAPE_Y_OFFSET, 33);
    tinyOLEDDisplay.drawVLine(65, 12 + COMPASS_TAPE_Y_OFFSET, 33);
    if (isTargetVisible(targetX)) {
        drawTargetMarker(targetX);
    }
    if (hasHomeBearing) {
        drawHomeSymbol(homeX);
    }
    tinyOLEDDisplay.setDrawColor(1);

    drawBearingReadout(bearing,targetBearing);
    if (targetAligned) {
        drawBottomInwardArrows();
    } else if (!isTargetVisible(targetX)) {
        drawBottomTurnArrow(targetTurnDirection);
    }

    tinyOLEDDisplay.sendBuffer();
}

void compassTask(void *parameter)
{
    (void)parameter;

    float targetBearing = 315.0f;
    float targetMarkerBearing = 8.0f;
    float homeMarkerBearing = 270.0f;
    bool hasHomeMarkerBearing = false;
    float displayedBearing = targetBearing;
    compassTaskRunning = true;

    TickType_t lastWake = xTaskGetTickCount();
    while (compassTaskShouldRun) {
        CompassBearingPacket queuedBearing = {};
        // Bearing updates are input samples only; rendering and arrow animation keep
        // running at COMPASS_RENDER_FRAME_MS even when no new packet arrives.
        while (xQueueReceive(compassBearingQueue, &queuedBearing, 0) == pdTRUE) {
            targetBearing = wrap360(queuedBearing.bearing);
            targetMarkerBearing = wrap360(queuedBearing.targetBearing);
            homeMarkerBearing = wrap360(queuedBearing.homeBearing);
            hasHomeMarkerBearing = queuedBearing.hasHomeBearing;
        }

        float step = 1.0f - compassInertia;
        if (step < 0.02f) {
            step = 0.02f;
        }
        displayedBearing = wrap360(displayedBearing + angleDelta(targetBearing, displayedBearing) * step);
        renderCompass(displayedBearing, targetMarkerBearing, homeMarkerBearing, hasHomeMarkerBearing);

        vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(COMPASS_RENDER_FRAME_MS));
    }

    compassTaskRunning = false;
    compassTaskHandle = nullptr;
    vTaskDelete(nullptr);
}

void startCompassTask()
{
    if (compassTaskHandle != nullptr) {
        return;
    }

    compassBearingQueue = xQueueCreate(1, sizeof(CompassBearingPacket));
    if (compassBearingQueue == nullptr) {
        return;
    }

    compassTaskShouldRun = true;
    if (xTaskCreate(compassTask, "linear_compass", 4096, nullptr, 1, &compassTaskHandle) != pdPASS) {
        compassTaskShouldRun = false;
        vQueueDelete(compassBearingQueue);
        compassBearingQueue = nullptr;
    }
}

void sendBearingsToCompass(float bearing, float targetBearing, float homeBearing, bool hasHomeBearing)
{
    if (compassBearingQueue != nullptr) {
        CompassBearingPacket packet = {
            wrap360(bearing),
            wrap360(targetBearing),
            wrap360(homeBearing),
            hasHomeBearing
        };
        xQueueOverwrite(compassBearingQueue, &packet);
    }
}

} // namespace

void setLinearCompassInertia(float inertia)
{
    if (inertia < 0.0f) {
        inertia = 0.0f;
    } else if (inertia > 0.96f) {
        inertia = 0.96f;
    }

    compassInertia = inertia;
}

void updateLinearCompassBearings(float bearing, float targetBearing)
{
    sendBearingsToCompass(bearing, targetBearing, 0.0f, false);
}

void updateLinearCompassBearings(float bearing, float targetBearing, float homeBearing)
{
    sendBearingsToCompass(bearing, targetBearing, homeBearing, true);
}

void stopLinearCompassDemo()
{
    compassTaskShouldRun = false;

    uint32_t waitStartedAt = millis();
    while (compassTaskRunning && millis() - waitStartedAt < 150) {
        delay(1);
    }

    if (compassTaskHandle != nullptr && compassTaskRunning) {
        vTaskDelete(compassTaskHandle);
        compassTaskHandle = nullptr;
        compassTaskRunning = false;
    }

    if (compassBearingQueue != nullptr) {
        vQueueDelete(compassBearingQueue);
        compassBearingQueue = nullptr;
    }
}

void demoLinearCompass(uint32_t localMs, bool firstFrame)
{
    if (firstFrame) {
        lastBearingSentAt = 0;
        startCompassTask();
        updateLinearCompassBearings(scriptedBearing(0), scriptedTargetBearing(0), scriptedHomeBearing(0));
    }

    if (firstFrame || localMs - lastBearingSentAt >= COMPASS_BEARING_UPDATE_THROTTLE_MS) {
        updateLinearCompassBearings(scriptedBearing(localMs), scriptedTargetBearing(localMs), scriptedHomeBearing(localMs));
        lastBearingSentAt = localMs;
    }
}

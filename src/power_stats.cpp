#include "power_stats.h"

#include "target_config.h"
#if TARGET_M5STICKC_PLUS
#include <M5StickCPlus.h>
#include <stdio.h>

const uint8_t POWER_OLED_WIDTH = 128;
const uint8_t POWER_OLED_HEIGHT = 64;
const uint16_t POWER_STATS_SAMPLE_INTERVAL_MS = 250;
const uint8_t POWER_GRAPH_POINTS = 44;
const float POWER_EXTERNAL_PRESENT_V = 4.25f;

// Interstitial power report. Flip this off for an uninterrupted demo loop.
static bool powerStatsEnabled = true;
static uint32_t powerStatsDisplayMs = 10000;

struct PowerSnapshot {
    float batV;
    float batMA;
    float batMW;
    float vbusV;
    float vbusMA;
    float vinV;
    float vinMA;
    float apsV;
    float sourceV;
    float sourceMA;
    float sourceMW;
    const char *sourceName;
};

struct PowerAccumulator {
    bool active;
    bool hasSample;
    uint32_t windowStartedAt;
    uint32_t lastSampleAt;
    uint16_t sampleCount;
    double milliAmpHours;
    double milliWattHours;
    double voltageHours;
    float startCoulombMah;
    PowerSnapshot lastSnapshot;
    float powerHistory[POWER_GRAPH_POINTS];
    uint8_t historyHead;
    uint8_t historyCount;
};

struct PowerReport {
    const char *demoName;
    const char *sourceName;
    uint32_t durationMs;
    uint16_t sampleCount;
    float avgV;
    float avgMA;
    float avgMW;
    float milliAmpHours;
    float milliWattHours;
    float coulombDeltaMah;
    float batV;
    float batMA;
    float vbusV;
    float vbusMA;
    float apsV;
    float powerHistory[POWER_GRAPH_POINTS];
    uint8_t historyCount;
};

static PowerAccumulator powerStats = {};
static PowerReport lastPowerReport = {};

void setupPowerStats()
{
    M5.Axp.ClearCoulombcounter();
    M5.Axp.EnableCoulombcounter();
}

void configurePowerStatsInterstitial(bool enabled, uint32_t displayMs)
{
    powerStatsEnabled = enabled;
    powerStatsDisplayMs = displayMs;
}

bool powerStatsInterstitialIsEnabled()
{
    return powerStatsEnabled;
}

uint32_t powerStatsInterstitialDurationMs()
{
    return powerStatsDisplayMs;
}

static float sanePowerFloat(float value)
{
    if (value != value || value < -100000.0f || value > 100000.0f) {
        return 0.0f;
    }

    return value;
}

static float positivePowerFloat(float value)
{
    value = sanePowerFloat(value);
    return value < 0.0f ? -value : value;
}

static PowerSnapshot readPowerSnapshot()
{
    PowerSnapshot snapshot = {};

    snapshot.batV = sanePowerFloat(M5.Axp.GetBatVoltage());
    snapshot.batMA = sanePowerFloat(M5.Axp.GetBatCurrent());
    snapshot.batMW = positivePowerFloat(M5.Axp.GetBatPower());
    snapshot.vbusV = sanePowerFloat(M5.Axp.GetVBusVoltage());
    snapshot.vbusMA = positivePowerFloat(M5.Axp.GetVBusCurrent());
    snapshot.vinV = sanePowerFloat(M5.Axp.GetVinVoltage());
    snapshot.vinMA = positivePowerFloat(M5.Axp.GetVinCurrent());
    snapshot.apsV = sanePowerFloat(M5.Axp.GetAPSVoltage());

    if (snapshot.vbusV >= POWER_EXTERNAL_PRESENT_V) {
        snapshot.sourceName = "VBUS";
        snapshot.sourceV = snapshot.vbusV;
        snapshot.sourceMA = snapshot.vbusMA;
    } else if (snapshot.vinV >= POWER_EXTERNAL_PRESENT_V) {
        snapshot.sourceName = "VIN";
        snapshot.sourceV = snapshot.vinV;
        snapshot.sourceMA = snapshot.vinMA;
    } else {
        snapshot.sourceName = "BAT";
        snapshot.sourceV = snapshot.batV;
        snapshot.sourceMA = positivePowerFloat(snapshot.batMA);
    }

    snapshot.sourceMW = snapshot.sourceV * snapshot.sourceMA;
    if (snapshot.sourceName[0] == 'B' && snapshot.sourceMW < 0.1f && snapshot.batMW > 0.1f) {
        snapshot.sourceMW = snapshot.batMW;
    }

    return snapshot;
}

static void addPowerHistorySample(float powerMW)
{
    powerStats.powerHistory[powerStats.historyHead] = powerMW;
    powerStats.historyHead = (powerStats.historyHead + 1) % POWER_GRAPH_POINTS;
    if (powerStats.historyCount < POWER_GRAPH_POINTS) {
        powerStats.historyCount++;
    }
}

void beginPowerStatsWindow(uint32_t now)
{
    PowerAccumulator empty = {};
    powerStats = empty;
    powerStats.active = true;
    powerStats.windowStartedAt = now;
    powerStats.lastSampleAt = now;
    powerStats.startCoulombMah = sanePowerFloat(M5.Axp.GetCoulombData());
    powerStats.lastSnapshot = readPowerSnapshot();
    powerStats.hasSample = true;
    powerStats.sampleCount = 1;
    addPowerHistorySample(powerStats.lastSnapshot.sourceMW);
}

void samplePowerStats(uint32_t now, bool force)
{
    if (!powerStats.active) {
        return;
    }

    if (!force && powerStats.hasSample && now - powerStats.lastSampleAt < POWER_STATS_SAMPLE_INTERVAL_MS) {
        return;
    }

    if (powerStats.hasSample && now == powerStats.lastSampleAt) {
        return;
    }

    PowerSnapshot snapshot = readPowerSnapshot();

    if (powerStats.hasSample) {
        uint32_t dtMs = now - powerStats.lastSampleAt;
        if (dtMs > 0) {
            double hours = (double)dtMs / 3600000.0;
            double avgMA = ((double)powerStats.lastSnapshot.sourceMA + (double)snapshot.sourceMA) * 0.5;
            double avgMW = ((double)powerStats.lastSnapshot.sourceMW + (double)snapshot.sourceMW) * 0.5;
            double avgV = ((double)powerStats.lastSnapshot.sourceV + (double)snapshot.sourceV) * 0.5;

            powerStats.milliAmpHours += avgMA * hours;
            powerStats.milliWattHours += avgMW * hours;
            powerStats.voltageHours += avgV * hours;
        }
    }

    powerStats.lastSnapshot = snapshot;
    powerStats.lastSampleAt = now;
    powerStats.sampleCount++;
    addPowerHistorySample(snapshot.sourceMW);
}

void finalizePowerStatsWindow(const char *demoName, uint32_t now)
{
    samplePowerStats(now, true);

    PowerReport report = {};
    report.demoName = demoName;
    report.sourceName = powerStats.lastSnapshot.sourceName;
    report.durationMs = now - powerStats.windowStartedAt;
    report.sampleCount = powerStats.sampleCount;
    report.milliAmpHours = (float)powerStats.milliAmpHours;
    report.milliWattHours = (float)powerStats.milliWattHours;
    report.coulombDeltaMah = sanePowerFloat(M5.Axp.GetCoulombData()) - powerStats.startCoulombMah;
    report.batV = powerStats.lastSnapshot.batV;
    report.batMA = powerStats.lastSnapshot.batMA;
    report.vbusV = powerStats.lastSnapshot.vbusV;
    report.vbusMA = powerStats.lastSnapshot.vbusMA;
    report.apsV = powerStats.lastSnapshot.apsV;

    float hours = report.durationMs / 3600000.0f;
    if (hours > 0.0f) {
        report.avgMA = report.milliAmpHours / hours;
        report.avgMW = report.milliWattHours / hours;
        report.avgV = (float)(powerStats.voltageHours / hours);
    }

    if (report.avgV <= 0.0f) {
        report.avgV = powerStats.lastSnapshot.sourceV;
    }
    if (report.avgMA <= 0.0f) {
        report.avgMA = powerStats.lastSnapshot.sourceMA;
    }
    if (report.avgMW <= 0.0f) {
        report.avgMW = powerStats.lastSnapshot.sourceMW;
    }

    report.historyCount = powerStats.historyCount;
    uint8_t firstHistory = (powerStats.historyHead + POWER_GRAPH_POINTS - powerStats.historyCount) % POWER_GRAPH_POINTS;
    for (uint8_t i = 0; i < powerStats.historyCount; i++) {
        report.powerHistory[i] = powerStats.powerHistory[(firstHistory + i) % POWER_GRAPH_POINTS];
    }

    lastPowerReport = report;
    powerStats.active = false;
}

static uint8_t batteryPercentFromVoltage(float volts)
{
    if (volts <= 3.30f) {
        return 0;
    }
    if (volts >= 4.20f) {
        return 100;
    }

    return (uint8_t)(((volts - 3.30f) * 100.0f) / 0.90f);
}

static void drawBatteryGauge(U8G2 &display, int16_t x, int16_t y, uint8_t percent)
{
    display.drawFrame(x, y, 18, 8);
    display.drawBox(x + 18, y + 2, 2, 4);
    uint8_t fillWidth = (uint8_t)((percent * 16U) / 100U);
    if (fillWidth > 0) {
        display.drawBox(x + 1, y + 1, fillWidth, 6);
    }
}

static void drawPowerSparkline(U8G2 &display, int16_t x, int16_t y, uint8_t width, uint8_t height, const PowerReport &report)
{
    display.drawFrame(x, y, width, height);

    if (report.historyCount < 2) {
        display.drawHLine(x + 2, y + height / 2, width - 4);
        return;
    }

    float minMW = report.powerHistory[0];
    float maxMW = report.powerHistory[0];
    for (uint8_t i = 1; i < report.historyCount; i++) {
        if (report.powerHistory[i] < minMW) {
            minMW = report.powerHistory[i];
        }
        if (report.powerHistory[i] > maxMW) {
            maxMW = report.powerHistory[i];
        }
    }

    float spanMW = maxMW - minMW;
    if (spanMW < 1.0f) {
        spanMW = 1.0f;
    }

    int16_t lastX = x + 1;
    int16_t lastY = y + height - 2 - (int16_t)(((report.powerHistory[0] - minMW) * (height - 3)) / spanMW);

    for (uint8_t i = 1; i < report.historyCount; i++) {
        int16_t px = x + 1 + (int16_t)(((uint16_t)i * (width - 3)) / (report.historyCount - 1));
        int16_t py = y + height - 2 - (int16_t)(((report.powerHistory[i] - minMW) * (height - 3)) / spanMW);
        display.drawLine(lastX, lastY, px, py);
        lastX = px;
        lastY = py;
    }
}

void renderPowerStatsInterstitial(U8G2 &display, uint32_t localMs, uint32_t durationMs)
{
    const PowerReport &report = lastPowerReport;
    char line[28];

    display.clearBuffer();
    display.setDrawColor(1);
    display.setFontMode(1);
    display.drawBox(0, 0, POWER_OLED_WIDTH, 10);

    display.setDrawColor(0);
    display.setFont(u8g2_font_5x7_tr);
    display.drawStr(2, 8, "POWER");
    display.drawStr(40, 8, report.demoName ? report.demoName : "DEMO");
    display.setDrawColor(1);

    display.drawLine(8, 14, 3, 28);
    display.drawLine(3, 28, 10, 28);
    display.drawLine(10, 28, 6, 38);
    display.drawLine(6, 38, 16, 23);
    display.drawLine(16, 23, 10, 23);
    display.drawLine(10, 23, 14, 14);

    display.setFont(u8g2_font_7x13B_tf);
    snprintf(line, sizeof(line), "%4.0f mW", report.avgMW);
    display.drawStr(19, 25, line);

    display.setFont(u8g2_font_5x7_tr);
    display.drawFrame(82, 14, 23, 10);
    display.drawStr(86, 22, report.sourceName ? report.sourceName : "?");
    drawBatteryGauge(display, 107, 14, batteryPercentFromVoltage(report.batV));

    snprintf(line, sizeof(line), "I %5.1fmA", report.avgMA);
    display.drawStr(2, 37, line);
    snprintf(line, sizeof(line), "V %5.2f", report.avgV);
    display.drawStr(2, 46, line);
    snprintf(line, sizeof(line), "%6.3fmAh", report.milliAmpHours);
    display.drawStr(2, 55, line);
    snprintf(line, sizeof(line), "%6.3fmWh", report.milliWattHours);
    display.drawStr(2, 62, line);

    drawPowerSparkline(display, 82, 29, 44, 26, report);
    if (report.coulombDeltaMah < -0.001f) {
        snprintf(line, sizeof(line), "D%.3f", -report.coulombDeltaMah);
    } else if (report.coulombDeltaMah > 0.001f) {
        snprintf(line, sizeof(line), "CHG%.3f", report.coulombDeltaMah);
    } else {
        snprintf(line, sizeof(line), "C 0.000");
    }
    display.drawStr(84, 62, line);

    uint8_t progressWidth = durationMs > 0 ? (uint8_t)((localMs * POWER_OLED_WIDTH) / durationMs) : POWER_OLED_WIDTH;
    display.drawHLine(0, POWER_OLED_HEIGHT - 1, progressWidth);
    display.sendBuffer();
}
#else
void setupPowerStats()
{
}

void configurePowerStatsInterstitial(bool enabled, uint32_t displayMs)
{
    (void)enabled;
    (void)displayMs;
}

bool powerStatsInterstitialIsEnabled()
{
    return false;
}

uint32_t powerStatsInterstitialDurationMs()
{
    return 0;
}

void beginPowerStatsWindow(uint32_t now)
{
    (void)now;
}

void samplePowerStats(uint32_t now, bool force)
{
    (void)now;
    (void)force;
}

void finalizePowerStatsWindow(const char *demoName, uint32_t now)
{
    (void)demoName;
    (void)now;
}

void renderPowerStatsInterstitial(U8G2 &display, uint32_t localMs, uint32_t durationMs)
{
    (void)display;
    (void)localMs;
    (void)durationMs;
}
#endif

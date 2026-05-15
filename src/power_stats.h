#ifndef POWER_STATS_H
#define POWER_STATS_H

#include <Arduino.h>
#include <U8g2lib.h>

void setupPowerStats();
void configurePowerStatsInterstitial(bool enabled, uint32_t displayMs);
bool powerStatsInterstitialIsEnabled();
uint32_t powerStatsInterstitialDurationMs();
void beginPowerStatsWindow(uint32_t now);
void samplePowerStats(uint32_t now, bool force);
void finalizePowerStatsWindow(const char *demoName, uint32_t now);
void renderPowerStatsInterstitial(U8G2 &display, uint32_t localMs, uint32_t durationMs);

#endif

#ifndef LINEAR_COMPASS_DEMO_H
#define LINEAR_COMPASS_DEMO_H

#include <Arduino.h>

void demoLinearCompass(uint32_t localMs, bool firstFrame);
void stopLinearCompassDemo();
void setLinearCompassInertia(float inertia);
void updateLinearCompassBearings(float bearing, float targetBearing);
void updateLinearCompassBearings(float bearing, float targetBearing, float homeBearing);

#endif

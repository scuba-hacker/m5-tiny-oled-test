#include "cobra_demo.h"

#include <U8g2lib.h>
#include <math.h>

extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C tinyOLEDDisplay;

static const bool SHOW_FPS = true;

namespace {

const int16_t SCREEN_WIDTH = 128;
const int16_t SCREEN_HEIGHT = 64;
const bool DEFAULT_HIDDEN_LINE_REMOVAL = true;
const uint16_t DEFAULT_SCALE_PERCENT = 60;
const uint16_t DEFAULT_THARGOID_SCALE_PERCENT = 42;
const uint16_t DEFAULT_ELITE_LOGO_SCALE_PERCENT = 32;
const uint8_t MAX_MODEL_VERTICES = 48;
const uint8_t MAX_MODEL_FACES = 16;
const float FACE_VISIBILITY_EPSILON = 1.5f;
const float CAMERA_DISTANCE = 320.0f;
const float PROJECTION_DISTANCE = 184.0f;
const float NEAR_PLANE = 12.0f;

struct Vec3 {
    float x;
    float y;
    float z;
};

struct ProjectedPoint {
    int16_t x;
    int16_t y;
    float z;
    bool visible;
};

struct Edge {
    uint8_t a;
    uint8_t b;
    int8_t faceA;
    int8_t faceB;
    uint8_t visibility;
    bool strong;
};

struct Face {
    float nx;
    float ny;
    float nz;
    uint8_t visibility;
};

struct VectorModel {
    const char *label;
    const Vec3 *vertices;
    uint8_t vertexCount;
    const Edge *edges;
    uint8_t edgeCount;
    const Face *faces;
    uint8_t faceCount;
    const uint8_t *engineVertices;
    uint8_t engineVertexCount;
    bool useFaceCulling;
};

// Clean-room Elite-style Cobra silhouette. It is deliberately not copied from
// the original game data; it is a compact homage tuned for a 128x64 OLED.
const Vec3 cobraHomageVertices[] = {
    {0.0f, -1.0f, 86.0f},      // 0 needle nose
    {-24.0f, -2.0f, 54.0f},    // 1 left front shoulder
    {24.0f, -2.0f, 54.0f},     // 2 right front shoulder
    {-73.0f, -4.0f, 4.0f},     // 3 left broad wing point
    {73.0f, -4.0f, 4.0f},      // 4 right broad wing point
    {-57.0f, -2.0f, -62.0f},   // 5 rear left outer corner
    {57.0f, -2.0f, -62.0f},    // 6 rear right outer corner
    {0.0f, -2.0f, -78.0f},     // 7 rear center notch
    {0.0f, 8.0f, 61.0f},       // 8 upper nose ridge
    {0.0f, 18.0f, 8.0f},       // 9 raised central spine
    {0.0f, 11.0f, -48.0f},     // 10 rear upper spine
    {-42.0f, 5.0f, 20.0f},     // 11 left upper hull crease
    {42.0f, 5.0f, 20.0f},      // 12 right upper hull crease
    {-51.0f, 3.0f, -34.0f},    // 13 left rear deck crease
    {51.0f, 3.0f, -34.0f},     // 14 right rear deck crease
    {-12.0f, 20.0f, 31.0f},    // 15 cockpit front left
    {12.0f, 20.0f, 31.0f},     // 16 cockpit front right
    {19.0f, 13.0f, 1.0f},      // 17 cockpit rear right
    {-19.0f, 13.0f, 1.0f},     // 18 cockpit rear left
    {0.0f, 24.0f, 17.0f},      // 19 cockpit roof peak
    {0.0f, -16.0f, 52.0f},     // 20 belly front keel
    {0.0f, -18.0f, -48.0f},    // 21 belly rear keel
    {-38.0f, 4.0f, -67.0f},    // 22 left engine top outer
    {-17.0f, 4.0f, -73.0f},    // 23 left engine top inner
    {-39.0f, -13.0f, -66.0f},  // 24 left engine bottom outer
    {-17.0f, -13.0f, -73.0f},  // 25 left engine bottom inner
    {17.0f, 4.0f, -73.0f},     // 26 right engine top inner
    {38.0f, 4.0f, -67.0f},     // 27 right engine top outer
    {17.0f, -13.0f, -73.0f},   // 28 right engine bottom inner
    {39.0f, -13.0f, -66.0f},   // 29 right engine bottom outer
};

const Edge cobraHomageEdges[] = {
    {0, 1, -1, -1, 31, true}, {0, 2, -1, -1, 31, true},
    {1, 3, -1, -1, 31, true}, {2, 4, -1, -1, 31, true},
    {3, 5, -1, -1, 31, true}, {4, 6, -1, -1, 31, true},
    {5, 7, -1, -1, 31, true}, {7, 6, -1, -1, 31, true},
    {1, 8, -1, -1, 31, false}, {2, 8, -1, -1, 31, false}, {8, 9, -1, -1, 31, true}, {9, 10, -1, -1, 31, true}, {10, 7, -1, -1, 31, false},
    {1, 11, -1, -1, 31, false}, {11, 3, -1, -1, 31, true}, {2, 12, -1, -1, 31, false}, {12, 4, -1, -1, 31, true},
    {11, 13, -1, -1, 31, false}, {13, 5, -1, -1, 31, true}, {12, 14, -1, -1, 31, false}, {14, 6, -1, -1, 31, true},
    {9, 11, -1, -1, 31, false}, {9, 12, -1, -1, 31, false}, {10, 13, -1, -1, 31, false}, {10, 14, -1, -1, 31, false},
    {15, 16, -1, -1, 31, true}, {16, 17, -1, -1, 31, true}, {17, 18, -1, -1, 31, true}, {18, 15, -1, -1, 31, true},
    {15, 19, -1, -1, 31, true}, {16, 19, -1, -1, 31, true}, {17, 19, -1, -1, 31, false}, {18, 19, -1, -1, 31, false},
    {15, 8, -1, -1, 31, false}, {16, 8, -1, -1, 31, false}, {17, 9, -1, -1, 31, false}, {18, 9, -1, -1, 31, false},
    {0, 20, -1, -1, 31, false}, {20, 1, -1, -1, 31, false}, {20, 2, -1, -1, 31, false},
    {20, 21, -1, -1, 31, true}, {21, 5, -1, -1, 31, false}, {21, 6, -1, -1, 31, false}, {21, 7, -1, -1, 31, false},
    {3, 21, -1, -1, 31, false}, {4, 21, -1, -1, 31, false},
    {5, 22, -1, -1, 31, false}, {22, 23, -1, -1, 31, true}, {23, 25, -1, -1, 31, true}, {25, 24, -1, -1, 31, true}, {24, 5, -1, -1, 31, false}, {22, 24, -1, -1, 31, false},
    {6, 27, -1, -1, 31, false}, {27, 26, -1, -1, 31, true}, {26, 28, -1, -1, 31, true}, {28, 29, -1, -1, 31, true}, {29, 6, -1, -1, 31, false}, {27, 29, -1, -1, 31, false},
    {23, 26, -1, -1, 31, false}, {25, 28, -1, -1, 31, false}, {7, 23, -1, -1, 31, false}, {7, 26, -1, -1, 31, false},
};

const uint8_t cobraHomageEngineVertices[] = {22, 23, 24, 25, 26, 27, 28, 29};

const VectorModel cobraHomageModel = {
    "COBRA HOMAGE",
    cobraHomageVertices,
    sizeof(cobraHomageVertices) / sizeof(cobraHomageVertices[0]),
    cobraHomageEdges,
    sizeof(cobraHomageEdges) / sizeof(cobraHomageEdges[0]),
    nullptr,
    0,
    cobraHomageEngineVertices,
    sizeof(cobraHomageEngineVertices) / sizeof(cobraHomageEngineVertices[0]),
    false,
};

// Original Elite Cobra Mk III vector data, transcribed from the 6502 ship table
// supplied in the prompt.
const Vec3 eliteCobraMkIIIBytesVertices[] = {
    {32.0f, 0.0f, 76.0f}, {-32.0f, 0.0f, 76.0f}, {0.0f, 26.0f, 24.0f},
    {-120.0f, -3.0f, -8.0f}, {120.0f, -3.0f, -8.0f}, {-88.0f, 16.0f, -40.0f},
    {88.0f, 16.0f, -40.0f}, {128.0f, -8.0f, -40.0f}, {-128.0f, -8.0f, -40.0f},
    {0.0f, 26.0f, -40.0f}, {-32.0f, -24.0f, -40.0f}, {32.0f, -24.0f, -40.0f},
    {-36.0f, 8.0f, -40.0f}, {-8.0f, 12.0f, -40.0f}, {8.0f, 12.0f, -40.0f},
    {36.0f, 8.0f, -40.0f}, {36.0f, -12.0f, -40.0f}, {8.0f, -16.0f, -40.0f},
    {-8.0f, -16.0f, -40.0f}, {-36.0f, -12.0f, -40.0f}, {0.0f, 0.0f, 76.0f},
    {0.0f, 0.0f, 90.0f}, {-80.0f, -6.0f, -40.0f}, {-80.0f, 6.0f, -40.0f},
    {-88.0f, 0.0f, -40.0f}, {80.0f, 6.0f, -40.0f}, {88.0f, 0.0f, -40.0f},
    {80.0f, -6.0f, -40.0f},
};

const Edge eliteCobraMkIIIEdges[] = {
    {0, 1, 0, 11, 31, true}, {0, 4, 4, 12, 31, true}, {1, 3, 3, 10, 31, true},
    {3, 8, 7, 10, 31, true}, {4, 7, 8, 12, 31, true}, {6, 7, 8, 9, 31, true},
    {6, 9, 6, 9, 31, true}, {5, 9, 5, 9, 31, true}, {5, 8, 7, 9, 31, true},
    {2, 5, 1, 5, 31, true}, {2, 6, 2, 6, 31, true}, {3, 5, 3, 7, 31, true},
    {4, 6, 4, 8, 31, true}, {1, 2, 0, 1, 31, true}, {0, 2, 0, 2, 31, true},
    {8, 10, 9, 10, 31, true}, {10, 11, 9, 11, 31, true}, {7, 11, 9, 12, 31, true},
    {1, 10, 10, 11, 31, true}, {0, 11, 11, 12, 31, true}, {1, 5, 1, 3, 29, true},
    {0, 6, 2, 4, 29, true}, {20, 21, 0, 11, 6, false}, {12, 13, 9, 9, 20, false},
    {18, 19, 9, 9, 20, false}, {14, 15, 9, 9, 20, false}, {16, 17, 9, 9, 20, false},
    {15, 16, 9, 9, 19, false}, {14, 17, 9, 9, 17, false}, {13, 18, 9, 9, 19, false},
    {12, 19, 9, 9, 19, false}, {2, 9, 5, 6, 30, true}, {22, 24, 9, 9, 6, false},
    {23, 24, 9, 9, 6, false}, {22, 23, 9, 9, 8, false}, {25, 26, 9, 9, 6, false},
    {26, 27, 9, 9, 6, false}, {25, 27, 9, 9, 8, false},
};

const Face eliteCobraMkIIIFaces[] = {
    {0.0f, 62.0f, 31.0f, 31}, {-18.0f, 55.0f, 16.0f, 31}, {18.0f, 55.0f, 16.0f, 31},
    {-16.0f, 52.0f, 14.0f, 31}, {16.0f, 52.0f, 14.0f, 31}, {-14.0f, 47.0f, 0.0f, 31},
    {14.0f, 47.0f, 0.0f, 31}, {-61.0f, 102.0f, 0.0f, 31}, {61.0f, 102.0f, 0.0f, 31},
    {0.0f, 0.0f, -80.0f, 31}, {-7.0f, -42.0f, 9.0f, 31}, {0.0f, -30.0f, 6.0f, 31},
    {7.0f, -42.0f, 9.0f, 31},
};

const uint8_t eliteCobraMkIIIEngineVertices[] = {22, 23, 24, 25, 26, 27};

const VectorModel eliteCobraMkIIIModel = {
    "COBRA MK III",
    eliteCobraMkIIIBytesVertices,
    sizeof(eliteCobraMkIIIBytesVertices) / sizeof(eliteCobraMkIIIBytesVertices[0]),
    eliteCobraMkIIIEdges,
    sizeof(eliteCobraMkIIIEdges) / sizeof(eliteCobraMkIIIEdges[0]),
    eliteCobraMkIIIFaces,
    sizeof(eliteCobraMkIIIFaces) / sizeof(eliteCobraMkIIIFaces[0]),
    eliteCobraMkIIIEngineVertices,
    sizeof(eliteCobraMkIIIEngineVertices) / sizeof(eliteCobraMkIIIEngineVertices[0]),
    true,
};

const Vec3 eliteThargoidVertices[] = {
    {32.0f, -48.0f, 48.0f}, {32.0f, -68.0f, 0.0f}, {32.0f, -48.0f, -48.0f},
    {32.0f, 0.0f, -68.0f}, {32.0f, 48.0f, -48.0f}, {32.0f, 68.0f, 0.0f},
    {32.0f, 48.0f, 48.0f}, {32.0f, 0.0f, 68.0f}, {-24.0f, -116.0f, 116.0f},
    {-24.0f, -164.0f, 0.0f}, {-24.0f, -116.0f, -116.0f}, {-24.0f, 0.0f, -164.0f},
    {-24.0f, 116.0f, -116.0f}, {-24.0f, 164.0f, 0.0f}, {-24.0f, 116.0f, 116.0f},
    {-24.0f, 0.0f, 164.0f}, {-24.0f, 64.0f, 80.0f}, {-24.0f, 64.0f, -80.0f},
    {-24.0f, -64.0f, -80.0f}, {-24.0f, -64.0f, 80.0f},
};

const Edge eliteThargoidEdges[] = {
    {0, 7, 4, 8, 31, true}, {0, 1, 0, 4, 31, true}, {1, 2, 1, 4, 31, true},
    {2, 3, 2, 4, 31, true}, {3, 4, 3, 4, 31, true}, {4, 5, 4, 5, 31, true},
    {5, 6, 4, 6, 31, true}, {6, 7, 4, 7, 31, true}, {0, 8, 0, 8, 31, true},
    {1, 9, 0, 1, 31, true}, {2, 10, 1, 2, 31, true}, {3, 11, 2, 3, 31, true},
    {4, 12, 3, 5, 31, true}, {5, 13, 5, 6, 31, true}, {6, 14, 6, 7, 31, true},
    {7, 15, 7, 8, 31, true}, {8, 15, 8, 9, 31, true}, {8, 9, 0, 9, 31, true},
    {9, 10, 1, 9, 31, true}, {10, 11, 2, 9, 31, true}, {11, 12, 3, 9, 31, true},
    {12, 13, 5, 9, 31, true}, {13, 14, 6, 9, 31, true}, {14, 15, 7, 9, 31, true},
    {16, 17, 9, 9, 30, false}, {18, 19, 9, 9, 30, false},
};

const Face eliteThargoidFaces[] = {
    {103.0f, -60.0f, 25.0f, 31}, {103.0f, -60.0f, -25.0f, 31},
    {103.0f, -25.0f, -60.0f, 31}, {103.0f, 25.0f, -60.0f, 31},
    {64.0f, 0.0f, 0.0f, 31}, {103.0f, 60.0f, -25.0f, 31},
    {103.0f, 60.0f, 25.0f, 31}, {103.0f, 25.0f, 60.0f, 31},
    {103.0f, -25.0f, 60.0f, 31}, {-48.0f, 0.0f, 0.0f, 31},
};

const VectorModel eliteThargoidModel = {
    "THARGOID",
    eliteThargoidVertices,
    sizeof(eliteThargoidVertices) / sizeof(eliteThargoidVertices[0]),
    eliteThargoidEdges,
    sizeof(eliteThargoidEdges) / sizeof(eliteThargoidEdges[0]),
    eliteThargoidFaces,
    sizeof(eliteThargoidFaces) / sizeof(eliteThargoidFaces[0]),
    nullptr,
    0,
    true,
};

const Vec3 eliteLogoVertices[] = {
    {0.0f, -9.0f, 55.0f}, {-10.0f, -9.0f, 30.0f}, {-25.0f, -9.0f, 93.0f},
    {-150.0f, -9.0f, 180.0f}, {-90.0f, -9.0f, 10.0f}, {-140.0f, -9.0f, 10.0f},
    {0.0f, -9.0f, -95.0f}, {140.0f, -9.0f, 10.0f}, {90.0f, -9.0f, 10.0f},
    {150.0f, -9.0f, 180.0f}, {25.0f, -9.0f, 93.0f}, {10.0f, -9.0f, 30.0f},
    {-85.0f, -9.0f, -30.0f}, {85.0f, -9.0f, -30.0f}, {-70.0f, 11.0f, 5.0f},
    {-70.0f, 11.0f, -25.0f}, {70.0f, 11.0f, -25.0f}, {70.0f, 11.0f, 5.0f},
    {0.0f, -9.0f, 5.0f}, {0.0f, -9.0f, 5.0f}, {0.0f, -9.0f, 5.0f},
    {-28.0f, 11.0f, -2.0f}, {-49.0f, 11.0f, -2.0f}, {-49.0f, 11.0f, -10.0f},
    {-49.0f, 11.0f, -17.0f}, {-28.0f, 11.0f, -17.0f}, {-28.0f, 11.0f, -10.0f},
    {-24.0f, 11.0f, -2.0f}, {-24.0f, 11.0f, -17.0f}, {-3.0f, 11.0f, -17.0f},
    {0.0f, 11.0f, -2.0f}, {0.0f, 11.0f, -17.0f}, {4.0f, 11.0f, -2.0f},
    {25.0f, 11.0f, -2.0f}, {14.0f, 11.0f, -2.0f}, {14.0f, 11.0f, -17.0f},
    {49.0f, 11.0f, -2.0f}, {28.0f, 11.0f, -2.0f}, {28.0f, 11.0f, -10.0f},
    {28.0f, 11.0f, -17.0f}, {49.0f, 11.0f, -17.0f}, {49.0f, 11.0f, -10.0f},
};

const Edge eliteLogoEdges[] = {
    {0, 1, 0, 0, 31, true}, {1, 2, 0, 0, 31, true}, {2, 3, 0, 0, 31, true},
    {3, 4, 0, 0, 31, true}, {4, 5, 0, 0, 31, true}, {5, 6, 0, 0, 31, true},
    {6, 7, 0, 0, 31, true}, {7, 8, 0, 0, 31, true}, {8, 9, 0, 0, 31, true},
    {9, 10, 0, 0, 31, true}, {10, 11, 0, 0, 31, true}, {11, 0, 0, 0, 31, true},
    {14, 15, 3, 0, 30, true}, {15, 16, 1, 0, 30, true}, {16, 17, 4, 0, 30, true},
    {17, 14, 1, 0, 30, true}, {4, 12, 3, 0, 30, true}, {12, 13, 2, 2, 30, true},
    {13, 8, 4, 0, 30, true}, {8, 4, 1, 1, 30, true}, {4, 14, 3, 1, 30, true},
    {12, 15, 3, 1, 30, true}, {13, 16, 4, 2, 30, true}, {8, 17, 4, 1, 30, true},
    {21, 22, 0, 0, 30, true}, {22, 24, 0, 0, 30, true}, {24, 25, 0, 0, 30, true},
    {23, 26, 0, 0, 30, true}, {27, 28, 0, 0, 30, true}, {28, 29, 0, 0, 30, true},
    {30, 31, 0, 0, 30, true}, {32, 33, 0, 0, 30, true}, {34, 35, 0, 0, 30, true},
    {36, 37, 0, 0, 30, true}, {37, 39, 0, 0, 30, true}, {39, 40, 0, 0, 30, true},
    {41, 38, 0, 0, 30, true},
};

const Face eliteLogoFaces[] = {
    {0.0f, 23.0f, 0.0f, 31}, {0.0f, 4.0f, 15.0f, 31}, {0.0f, 13.0f, -52.0f, 31},
    {-81.0f, 81.0f, 0.0f, 31}, {81.0f, 81.0f, 0.0f, 31},
};

const VectorModel eliteLogoModel = {
    "ELITE",
    eliteLogoVertices,
    sizeof(eliteLogoVertices) / sizeof(eliteLogoVertices[0]),
    eliteLogoEdges,
    sizeof(eliteLogoEdges) / sizeof(eliteLogoEdges[0]),
    eliteLogoFaces,
    sizeof(eliteLogoFaces) / sizeof(eliteLogoFaces[0]),
    nullptr,
    0,
    false,
};

const VectorModel *activeCobraModel = &eliteCobraMkIIIModel;

bool onScreen(int16_t x, int16_t y)
{
    return x >= 0 && x < SCREEN_WIDTH && y >= 0 && y < SCREEN_HEIGHT;
}

uint8_t lineClipCode(int16_t x, int16_t y)
{
    uint8_t code = 0;

    if (x < 0) {
        code |= 1;
    } else if (x >= SCREEN_WIDTH) {
        code |= 2;
    }

    if (y < 0) {
        code |= 4;
    } else if (y >= SCREEN_HEIGHT) {
        code |= 8;
    }

    return code;
}

bool clipLineToScreen(int16_t &x0, int16_t &y0, int16_t &x1, int16_t &y1)
{
    int32_t ax = x0;
    int32_t ay = y0;
    int32_t bx = x1;
    int32_t by = y1;
    uint8_t aCode = lineClipCode(x0, y0);
    uint8_t bCode = lineClipCode(x1, y1);

    while (true) {
        if ((aCode | bCode) == 0) {
            x0 = (int16_t)ax;
            y0 = (int16_t)ay;
            x1 = (int16_t)bx;
            y1 = (int16_t)by;
            return true;
        }

        if ((aCode & bCode) != 0) {
            return false;
        }

        uint8_t outCode = aCode != 0 ? aCode : bCode;
        int32_t x = 0;
        int32_t y = 0;

        if ((outCode & 8) != 0) {
            y = SCREEN_HEIGHT - 1;
            x = ax + (bx - ax) * (y - ay) / (by - ay);
        } else if ((outCode & 4) != 0) {
            y = 0;
            x = ax + (bx - ax) * (y - ay) / (by - ay);
        } else if ((outCode & 2) != 0) {
            x = SCREEN_WIDTH - 1;
            y = ay + (by - ay) * (x - ax) / (bx - ax);
        } else {
            x = 0;
            y = ay + (by - ay) * (x - ax) / (bx - ax);
        }

        if (outCode == aCode) {
            ax = x;
            ay = y;
            aCode = lineClipCode((int16_t)ax, (int16_t)ay);
        } else {
            bx = x;
            by = y;
            bCode = lineClipCode((int16_t)bx, (int16_t)by);
        }
    }
}

void drawPixelClipped(int16_t x, int16_t y)
{
    if (onScreen(x, y)) {
        tinyOLEDDisplay.drawPixel(x, y);
    }
}

void drawBoxClipped(int16_t x, int16_t y, int16_t width, int16_t height)
{
    if (width <= 0 || height <= 0 || x >= SCREEN_WIDTH || y >= SCREEN_HEIGHT) {
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

    if (x + width > SCREEN_WIDTH) {
        width = SCREEN_WIDTH - x;
    }

    if (y + height > SCREEN_HEIGHT) {
        height = SCREEN_HEIGHT - y;
    }

    if (width > 0 && height > 0) {
        tinyOLEDDisplay.drawBox(x, y, width, height);
    }
}

void drawHLineClipped(int16_t x, int16_t y, int16_t width)
{
    if (y < 0 || y >= SCREEN_HEIGHT || width <= 0 || x >= SCREEN_WIDTH) {
        return;
    }

    if (x < 0) {
        width += x;
        x = 0;
    }

    if (x + width > SCREEN_WIDTH) {
        width = SCREEN_WIDTH - x;
    }

    if (width > 0) {
        tinyOLEDDisplay.drawHLine(x, y, width);
    }
}

void drawDashedLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t pattern)
{
    if (!clipLineToScreen(x0, y0, x1, y1)) {
        return;
    }

    int16_t dx = abs(x1 - x0);
    int16_t sx = x0 < x1 ? 1 : -1;
    int16_t dy = -abs(y1 - y0);
    int16_t sy = y0 < y1 ? 1 : -1;
    int16_t err = dx + dy;
    uint16_t step = 0;

    while (true) {
        if ((step & pattern) != pattern) {
            drawPixelClipped(x0, y0);
        }

        if (x0 == x1 && y0 == y1) {
            break;
        }

        int16_t e2 = err * 2;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }

        step++;
    }
}

void drawLineClipped(int16_t x0, int16_t y0, int16_t x1, int16_t y1)
{
    if (clipLineToScreen(x0, y0, x1, y1)) {
        tinyOLEDDisplay.drawLine(x0, y0, x1, y1);
    }
}

uint32_t mixBits(uint32_t value)
{
    value ^= value >> 16;
    value *= 0x7feb352dUL;
    value ^= value >> 15;
    value *= 0x846ca68bUL;
    value ^= value >> 16;
    return value;
}

void drawStarField(uint32_t localMs)
{
    for (uint8_t i = 0; i < 28; i++) {
        uint32_t h = mixBits(i * 0x9e3779b9UL + 0x251c6d35UL);
        int16_t x = (h + localMs / (85 + (i % 5) * 18)) & 127;
        int16_t y = (h >> 9) & 63;

        if (y > 1 && y < 62 && ((localMs / 250 + i) % 4) != 0) {
            tinyOLEDDisplay.drawPixel(x, y);
        }
    }
}

Vec3 rotateVector(const Vec3 &point, float yaw, float pitch, float roll)
{
    float cy = cosf(yaw);
    float sy = sinf(yaw);
    float cp = cosf(pitch);
    float sp = sinf(pitch);
    float cr = cosf(roll);
    float sr = sinf(roll);

    float x1 = point.x * cy + point.z * sy;
    float z1 = -point.x * sy + point.z * cy;
    float y1 = point.y;

    float y2 = y1 * cp - z1 * sp;
    float z2 = y1 * sp + z1 * cp;
    float x2 = x1;

    float x3 = x2 * cr - y2 * sr;
    float y3 = x2 * sr + y2 * cr;

    return {x3, y3, z2};
}

ProjectedPoint rotateAndProject(const Vec3 &point, float yaw, float pitch, float roll, uint16_t scalePercent)
{
    if (scalePercent < 25) {
        scalePercent = 25;
    } else if (scalePercent > 220) {
        scalePercent = 220;
    }

    Vec3 rotated = rotateVector(point, yaw, pitch, roll);
    float cameraZ = rotated.z + CAMERA_DISTANCE;
    float scale = (PROJECTION_DISTANCE * ((float)scalePercent / 100.0f)) / cameraZ;

    ProjectedPoint projected = {
        (int16_t)(64.0f + rotated.x * scale),
        (int16_t)(32.0f - rotated.y * scale),
        rotated.z,
        cameraZ > NEAR_PLANE,
    };

    return projected;
}

void computeFaceVisibility(const VectorModel &model, bool faceVisible[], float yaw, float pitch, float roll)
{
    for (uint8_t i = 0; i < model.faceCount; i++) {
        const Face &face = model.faces[i];
        Vec3 normal = rotateVector({face.nx, face.ny, face.nz}, yaw, pitch, roll);
        faceVisible[i] = normal.z <= FACE_VISIBILITY_EPSILON;
    }
}

bool edgeFaceIsVisible(const VectorModel &model, const Edge &edge, const bool faceVisible[])
{
    if (model.faceCount == 0 || edge.faceA < 0 || edge.faceB < 0) {
        return false;
    }

    bool faceAVisible = edge.faceA < model.faceCount && faceVisible[edge.faceA];
    bool faceBVisible = edge.faceB < model.faceCount && faceVisible[edge.faceB];
    return faceAVisible || faceBVisible;
}

bool edgeIsFarSide(const VectorModel &model, const Edge &edge, const ProjectedPoint &a, const ProjectedPoint &b, const bool faceVisible[])
{
    if (model.useFaceCulling && model.faceCount > 0 && edge.faceA >= 0 && edge.faceB >= 0) {
        return !edgeFaceIsVisible(model, edge, faceVisible);
    }

    float averageZ = (a.z + b.z) * 0.5f;
    return averageZ > 8.0f && !edge.strong;
}

void drawProjectedEdge(
    const VectorModel &model,
    const Edge &edge,
    const ProjectedPoint &a,
    const ProjectedPoint &b,
    const bool faceVisible[],
    bool hiddenLineRemoval)
{
    if (!a.visible || !b.visible) {
        return;
    }

    float averageZ = (a.z + b.z) * 0.5f;
    bool farSide = edgeIsFarSide(model, edge, a, b, faceVisible);

    if (farSide) {
        if (hiddenLineRemoval) {
            return;
        }

        drawDashedLine(a.x, a.y, b.x, b.y, 3);
        return;
    }

    drawLineClipped(a.x, a.y, b.x, b.y);

    if (edge.strong && averageZ < -18.0f) {
        drawPixelClipped(a.x, a.y);
        drawPixelClipped(b.x, b.y);
    }
}

int32_t cross2D(int16_t ox, int16_t oy, int16_t ax, int16_t ay, int16_t bx, int16_t by)
{
    return (int32_t)(ax - ox) * (by - oy) - (int32_t)(ay - oy) * (bx - ox);
}

int32_t distanceSquared2D(int16_t ax, int16_t ay, int16_t bx, int16_t by)
{
    int32_t dx = ax - bx;
    int32_t dy = ay - by;
    return dx * dx + dy * dy;
}

bool addUniquePoint(int16_t xs[], int16_t ys[], uint8_t &count, int16_t x, int16_t y)
{
    for (uint8_t i = 0; i < count; i++) {
        if (xs[i] == x && ys[i] == y) {
            return false;
        }
    }

    xs[count] = x;
    ys[count] = y;
    count++;
    return true;
}

uint8_t buildConvexHull(
    const int16_t xs[],
    const int16_t ys[],
    uint8_t pointCount,
    int16_t hullX[],
    int16_t hullY[])
{
    if (pointCount < 3) {
        for (uint8_t i = 0; i < pointCount; i++) {
            hullX[i] = xs[i];
            hullY[i] = ys[i];
        }
        return pointCount;
    }

    uint8_t start = 0;
    for (uint8_t i = 1; i < pointCount; i++) {
        if (xs[i] < xs[start] || (xs[i] == xs[start] && ys[i] < ys[start])) {
            start = i;
        }
    }

    uint8_t hullCount = 0;
    uint8_t current = start;

    do {
        hullX[hullCount] = xs[current];
        hullY[hullCount] = ys[current];
        hullCount++;

        uint8_t next = current == 0 ? 1 : 0;
        for (uint8_t i = 0; i < pointCount; i++) {
            if (i == current) {
                continue;
            }

            int32_t turn = cross2D(xs[current], ys[current], xs[next], ys[next], xs[i], ys[i]);
            if (turn < 0 ||
                (turn == 0 &&
                 distanceSquared2D(xs[current], ys[current], xs[i], ys[i]) >
                     distanceSquared2D(xs[current], ys[current], xs[next], ys[next]))) {
                next = i;
            }
        }

        current = next;
    } while (current != start && hullCount < MAX_MODEL_VERTICES);

    return hullCount;
}

void fillPolygonClipped(const int16_t xs[], const int16_t ys[], uint8_t pointCount)
{
    if (pointCount < 3) {
        return;
    }

    int16_t minY = ys[0];
    int16_t maxY = ys[0];
    for (uint8_t i = 1; i < pointCount; i++) {
        if (ys[i] < minY) {
            minY = ys[i];
        }
        if (ys[i] > maxY) {
            maxY = ys[i];
        }
    }

    if (minY < 0) {
        minY = 0;
    }
    if (maxY >= SCREEN_HEIGHT) {
        maxY = SCREEN_HEIGHT - 1;
    }

    for (int16_t y = minY; y <= maxY; y++) {
        int16_t intersections[MAX_MODEL_VERTICES];
        uint8_t intersectionCount = 0;
        uint8_t previous = pointCount - 1;

        for (uint8_t current = 0; current < pointCount; current++) {
            int16_t y0 = ys[previous];
            int16_t y1 = ys[current];

            if ((y0 < y && y1 >= y) || (y1 < y && y0 >= y)) {
                int16_t x0 = xs[previous];
                int16_t x1 = xs[current];
                intersections[intersectionCount] =
                    x0 + (int32_t)(y - y0) * (x1 - x0) / (y1 - y0);
                intersectionCount++;
            }

            previous = current;
        }

        for (uint8_t i = 1; i < intersectionCount; i++) {
            int16_t value = intersections[i];
            uint8_t j = i;
            while (j > 0 && intersections[j - 1] > value) {
                intersections[j] = intersections[j - 1];
                j--;
            }
            intersections[j] = value;
        }

        for (uint8_t i = 0; i + 1 < intersectionCount; i += 2) {
            drawHLineClipped(intersections[i], y, intersections[i + 1] - intersections[i] + 1);
        }
    }
}

void maskStarsBehindModel(const VectorModel &model, const ProjectedPoint points[])
{
    int16_t xs[MAX_MODEL_VERTICES];
    int16_t ys[MAX_MODEL_VERTICES];
    int16_t hullX[MAX_MODEL_VERTICES];
    int16_t hullY[MAX_MODEL_VERTICES];
    uint8_t pointCount = 0;

    for (uint8_t i = 0; i < model.vertexCount; i++) {
        if (points[i].visible) {
            addUniquePoint(xs, ys, pointCount, points[i].x, points[i].y);
        }
    }

    uint8_t hullCount = buildConvexHull(xs, ys, pointCount, hullX, hullY);

    tinyOLEDDisplay.setDrawColor(0);
    fillPolygonClipped(hullX, hullY, hullCount);
    tinyOLEDDisplay.setDrawColor(1);
}

void drawEngineSparkles(const VectorModel &model, const ProjectedPoint points[], uint32_t localMs, bool hiddenLineRemoval)
{
    tinyOLEDDisplay.setDrawColor(1);
    for (uint8_t i = 0; i < model.engineVertexCount; i++) {
        const ProjectedPoint &p = points[model.engineVertices[i]];

        if (p.visible && (!hiddenLineRemoval || p.z <= FACE_VISIBILITY_EPSILON) && ((localMs / 90 + i) & 1) == 0) {
            drawBoxClipped(p.x - 1, p.y - 1, 3, 3);
        }
    }
    tinyOLEDDisplay.setDrawColor(1);
}

void drawCobraLabel(const VectorModel &model, uint32_t localMs)
{
    if ((localMs % 5000) < 3500) {
        tinyOLEDDisplay.setFontMode(1);
        tinyOLEDDisplay.setFont(u8g2_font_5x7_tr);
        tinyOLEDDisplay.drawStr(2, 62, model.label);
    }
}

void renderVectorModel(const VectorModel &model, uint32_t localMs, bool hiddenLineRemoval, uint16_t scalePercent)
{
    ProjectedPoint points[MAX_MODEL_VERTICES];
    bool faceVisible[MAX_MODEL_FACES] = {};
    float yaw = localMs * 0.00072f + sinf(localMs * 0.00019f) * 0.55f;
    float pitch = localMs * 0.00058f + sinf(localMs * 0.00041f) * 0.62f;
    float roll = localMs * 0.00122f + sinf(localMs * 0.00033f) * 0.46f;

    tinyOLEDDisplay.clearBuffer();
    tinyOLEDDisplay.setDrawColor(1);
    drawStarField(localMs);

    if (model.vertexCount > MAX_MODEL_VERTICES || model.faceCount > MAX_MODEL_FACES) {
        drawCobraLabel(model, localMs);
        tinyOLEDDisplay.sendBuffer();
        return;
    }

    computeFaceVisibility(model, faceVisible, yaw, pitch, roll);

    for (uint8_t i = 0; i < model.vertexCount; i++) {
        points[i] = rotateAndProject(model.vertices[i], yaw, pitch, roll, scalePercent);
    }

    if (hiddenLineRemoval) {
        maskStarsBehindModel(model, points);
    }

    for (uint8_t pass = 0; pass < 2; pass++) {
        for (uint8_t i = 0; i < model.edgeCount; i++) {
            const Edge &edge = model.edges[i];
            bool farSide = edgeIsFarSide(model, edge, points[edge.a], points[edge.b], faceVisible);

            if ((pass == 0 && farSide) || (pass == 1 && !farSide)) {
                drawProjectedEdge(model, edge, points[edge.a], points[edge.b], faceVisible, hiddenLineRemoval);
            }
        }
    }

    drawEngineSparkles(model, points, localMs, hiddenLineRemoval);
    drawCobraLabel(model, localMs);

    if (SHOW_FPS) {
        static uint32_t lastFrameMs = 0;
        uint32_t now = millis();
        uint32_t delta = now - lastFrameMs;
        lastFrameMs = now;
        if (delta > 0) {
            char buf[8];
            snprintf(buf, sizeof(buf), "%u", (unsigned)(1000u / delta));
            tinyOLEDDisplay.setFontMode(1);
            tinyOLEDDisplay.setFont(u8g2_font_5x7_tr);
            tinyOLEDDisplay.setDrawColor(1);
            uint8_t w = tinyOLEDDisplay.getStrWidth(buf);
            tinyOLEDDisplay.drawStr(SCREEN_WIDTH - w, 7, buf);
        }
    }

    tinyOLEDDisplay.sendBuffer();
}

}

void demoCobraMkIII(uint32_t localMs, bool firstFrame, bool hiddenLineRemoval, uint16_t scalePercent)
{
    (void)firstFrame;
    renderVectorModel(*activeCobraModel, localMs, hiddenLineRemoval, scalePercent);
}

void demoCobraMkIII(uint32_t localMs, bool firstFrame)
{
    demoCobraMkIII(localMs, firstFrame, DEFAULT_HIDDEN_LINE_REMOVAL, DEFAULT_SCALE_PERCENT);
}

void demoThargoid(uint32_t localMs, bool firstFrame, bool hiddenLineRemoval, uint16_t scalePercent)
{
    (void)firstFrame;
    renderVectorModel(eliteThargoidModel, localMs, hiddenLineRemoval, scalePercent);
}

void demoThargoid(uint32_t localMs, bool firstFrame)
{
    demoThargoid(localMs, firstFrame, DEFAULT_HIDDEN_LINE_REMOVAL, DEFAULT_THARGOID_SCALE_PERCENT);
}

void demoEliteLogo(uint32_t localMs, bool firstFrame, bool hiddenLineRemoval, uint16_t scalePercent)
{
    (void)firstFrame;
    renderVectorModel(eliteLogoModel, localMs, hiddenLineRemoval, scalePercent);
}

void demoEliteLogo(uint32_t localMs, bool firstFrame)
{
    demoEliteLogo(localMs, firstFrame, DEFAULT_HIDDEN_LINE_REMOVAL, DEFAULT_ELITE_LOGO_SCALE_PERCENT);
}

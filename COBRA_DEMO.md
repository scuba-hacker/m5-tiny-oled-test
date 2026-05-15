# Cobra / Elite Vector Demo

This project includes an Elite-style vector renderer for the 128 x 64 OLED in
`src/cobra_demo.cpp`. Despite the filename, it now renders several vector
models:

- Cobra Mk III
- Thargoid
- ELITE logo
- The earlier hand-made Cobra homage model, kept as an optional model in the file

The public demo entry points are declared in `src/cobra_demo.h`:

```cpp
void demoCobraMkIII(uint32_t localMs, bool firstFrame);
void demoCobraMkIII(uint32_t localMs, bool firstFrame, bool hiddenLineRemoval, uint16_t scalePercent);

void demoThargoid(uint32_t localMs, bool firstFrame);
void demoThargoid(uint32_t localMs, bool firstFrame, bool hiddenLineRemoval, uint16_t scalePercent);

void demoEliteLogo(uint32_t localMs, bool firstFrame);
void demoEliteLogo(uint32_t localMs, bool firstFrame, bool hiddenLineRemoval, uint16_t scalePercent);
```

The two-argument versions are carousel-friendly wrappers. The four-argument
versions let you control hidden-line removal and model scale.

## Main Data Types

The renderer uses small structs to represent Elite-style wireframe tables.

```cpp
struct Vec3 {
    float x;
    float y;
    float z;
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
```

`VectorModel` is the important wrapper. It binds one model's vertices, edges,
faces, optional engine sparkle points, and hidden-line policy into a single
object that `renderVectorModel()` can draw.

## Render Pipeline

The shared renderer is:

```cpp
renderVectorModel(model, localMs, hiddenLineRemoval, scalePercent)
```

It does the following each frame:

1. Computes yaw, pitch, and roll from `localMs`.
2. Clears the OLED buffer and draws the moving starfield.
3. Rotates the model's face normals with `computeFaceVisibility()`.
4. Rotates and projects each vertex with `rotateAndProject()`.
5. If hidden-line removal is enabled, clears stars inside the projected model silhouette with `maskStarsBehindModel()`.
6. Draws far-side edges first, then near-side edges.
7. Draws optional engine sparkle points.
8. Draws the model label and sends the OLED buffer.

Projection happens in `rotateAndProject()`. It rotates the 3D point, applies a
simple perspective divide, and scales the result by `scalePercent`. The scale is
clamped to `25..220` percent to avoid extreme projection values.

## Coordinate System

The model data follows the Elite tables directly:

- `x`: left/right
- `y`: up/down in model space
- `z`: forward/back in model space

The renderer rotates the model in 3D first, then projects it to OLED pixels:

```cpp
screenX = 64 + rotated.x * scale;
screenY = 32 - rotated.y * scale;
```

The `screenY` subtraction means positive model `y` appears higher on the OLED.
That is convenient for Elite data because ships with positive upper hull points
show correctly rather than upside-down.

Perspective uses:

```cpp
cameraZ = rotated.z + CAMERA_DISTANCE;
scale = (PROJECTION_DISTANCE * (scalePercent / 100.0f)) / cameraZ;
```

So larger positive `z` points are farther away after rotation, and points with
smaller `cameraZ` are drawn larger. The current constants are tuned for the tiny
OLED and the Elite coordinate range, not for physically accurate projection.

The camera is deliberately set far enough back for the wide models:

```cpp
CAMERA_DISTANCE = 320.0f
PROJECTION_DISTANCE = 184.0f
NEAR_PLANE = 12.0f
```

`PROJECTION_DISTANCE / CAMERA_DISTANCE` keeps roughly the same apparent size as
the earlier projection, while the larger camera distance prevents wide models
such as the ELITE logo from crossing the near plane during rotation.

## Current Models

The current models live in `src/cobra_demo.cpp`:

```cpp
cobraHomageModel
eliteCobraMkIIIModel
eliteThargoidModel
eliteLogoModel
```

`cobraHomageModel` is the older hand-built model and is kept for optional later
use. The carousel currently uses:

- `demoEliteLogo()` for `eliteLogoModel`
- `demoCobraMkIII()` for `eliteCobraMkIIIModel`
- `demoThargoid()` for `eliteThargoidModel`

`activeCobraModel` points at the canonical Cobra model:

```cpp
const VectorModel *activeCobraModel = &eliteCobraMkIIIModel;
```

That pointer exists so the Cobra entry point can be redirected to another Cobra
variant later without changing the public function name.

## Hidden-Line Removal

The key hidden-line functions are:

```cpp
computeFaceVisibility()
edgeFaceIsVisible()
edgeIsFarSide()
drawProjectedEdge()
```

`computeFaceVisibility()` rotates every face normal using the same yaw, pitch,
and roll as the model. A face is treated as visible when its rotated normal faces
towards the viewer:

```cpp
faceVisible[i] = normal.z <= FACE_VISIBILITY_EPSILON;
```

`edgeFaceIsVisible()` checks the two face indices stored on an edge. If either
face is visible, the edge is considered visible.

`FACE_VISIBILITY_EPSILON` gives edge-on faces a small tolerance. Without that,
faces whose normals are extremely close to sideways can flicker between visible
and hidden from one frame to the next, making silhouette edges pop.

`edgeIsFarSide()` is the main decision point. For models with `useFaceCulling =
true`, it uses the face-normal logic:

```cpp
return !edgeFaceIsVisible(model, edge, faceVisible);
```

For non-culling models, such as the ELITE logo, it falls back to a simple depth
heuristic for weak edges. This keeps line-art models from incorrectly losing
letter strokes.

`drawProjectedEdge()` enforces the `hiddenLineRemoval` flag:

- If an edge is far-side and hidden-line removal is enabled, it is skipped.
- If an edge is far-side and hidden-line removal is disabled, it is drawn dashed.
- Otherwise, it is drawn as a normal clipped line.

Line clipping is handled by `clipLineToScreen()` before drawing, so off-screen
vertices do not make U8g2 draw wild lines across the buffer.

### Face Culling Flag

Each `VectorModel` has:

```cpp
bool useFaceCulling;
```

Use `true` when the model is a closed or mostly closed 3D object with meaningful
face normals. The Cobra and Thargoid use `true`.

Use `false` when the model is decorative line art, lettering, or otherwise not a
closed solid. The ELITE logo uses `false`, because its letter strokes and logo
detail are intended to remain visible from all angles.

When `useFaceCulling` is false, `edgeIsFarSide()` does not use face normals. It
falls back to depth for weak edges only:

```cpp
return averageZ > 8.0f && !edge.strong;
```

That is why most ELITE logo edges are marked `strong = true`.

### Hidden-Line Flag

The public four-argument demo functions take:

```cpp
bool hiddenLineRemoval
```

When this is `true`, far-side edges are skipped. When it is `false`, far-side
edges are drawn dashed:

```cpp
drawDashedLine(a.x, a.y, b.x, b.y, 3);
```

This is useful when debugging a new model. Turn hidden-line removal off first so
you can see all edges and confirm that vertex indices are correct. Then turn it
on once the model shape looks right.

## Starfield Occlusion

The starfield is drawn before the model. To stop stars showing through solid
ships, `maskStarsBehindModel()` runs after vertex projection and before the
wireframe is drawn.

It:

1. Collects the model's projected 2D points.
2. Builds a convex hull with `buildConvexHull()`.
3. Fills that hull in draw color `0`, clearing stars behind the ship.
4. Restores draw color `1`.

This is a deliberately cheap silhouette mask. It does not fill the actual 3D
faces, but on a 128 x 64 OLED it gives the right effect: stars disappear behind
the object while the object itself remains wireframe.

Because the mask uses a convex hull, it can over-clear stars around concave
models. That is acceptable for the Cobra, Thargoid, and logo at this resolution.
If future models have large concave gaps, a per-face mask would be more accurate,
but also more expensive and more fiddly.

## Line Clipping

U8g2 line drawing can misbehave when endpoints are far outside the display
rectangle. The renderer avoids that by clipping every wireframe line first:

```cpp
clipLineToScreen()
drawLineClipped()
drawDashedLine()
```

`clipLineToScreen()` is a Cohen-Sutherland style clipper. It restricts endpoints
to:

```text
x = 0..127
y = 0..63
```

Both solid and dashed vector edges pass through this logic. If a model suddenly
draws long stray lines across the OLED, check whether any new drawing path is
bypassing `drawLineClipped()` or `drawDashedLine()`.

## Adding Another Elite Model

For the renderer, the useful parts of an Elite ship table are:

- `SHIP_NAME_VERTICES`
- `SHIP_NAME_EDGES`
- `SHIP_NAME_FACES`

The header metadata is mostly optional. Counts are useful as sanity checks, but
the code can infer counts from the arrays.

### Vertices

Elite source row:

```asm
VERTEX   32,  -48,   48,     0,      4,    8,     8,         31
```

Code array entry:

```cpp
{32.0f, -48.0f, 48.0f}
```

Only the first three fields are needed for `Vec3`: `x`, `y`, and `z`.

### Edges

Elite source row:

```asm
EDGE       0,       7,     4,     8,         31
```

Code array entry:

```cpp
{0, 7, 4, 8, 31, true}
```

The fields map like this:

```text
vertex1    -> a
vertex2    -> b
face1      -> faceA
face2      -> faceB
visibility -> visibility
strong     -> true/false chosen by us
```

`strong` is not in the original Elite row. In this demo it controls small visual
emphasis and fallback depth behavior. For main hull edges, use `true`. For tiny
details, gun lines, engine marks, logo strokes, or optional decorative edges,
`false` is usually fine.

For the canonical Cobra and Thargoid tables, most main ship edges are marked
`true`; small internal detail edges can be marked `false`. For the ELITE logo,
all edges are marked `true` so the line-art survives depth fallback.

### Faces

Elite source row:

```asm
FACE      103,      -60,       25,         31
```

Code array entry:

```cpp
{103.0f, -60.0f, 25.0f, 31}
```

The first three fields are the face normal. The final value is stored as
`visibility`.

### VectorModel Wrapper

After making the arrays, add a wrapper:

```cpp
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
```

Use `true` for `useFaceCulling` when the model is a closed-ish 3D ship with
valid face normals. Use `false` for line-art models like the ELITE logo, where
you want every edge drawn.

If the model has engine points or other sparkle markers, provide a vertex list:

```cpp
const uint8_t eliteCobraMkIIIEngineVertices[] = {22, 23, 24, 25, 26, 27};
```

Then put that list and its count into the `VectorModel`.

If the model has no sparkle points, use:

```cpp
nullptr,
0,
```

for `engineVertices` and `engineVertexCount`.

### Worked Mini Example

Given this Elite-style data:

```asm
VERTEX   10,   20,   30,     0,      1,    1,     1,         31
VERTEX  -10,   20,   30,     0,      1,    1,     1,         31

EDGE       0,       1,     0,     1,         31

FACE        0,       62,       31,         31
FACE        0,      -30,        6,         31
```

The C++ conversion is:

```cpp
const Vec3 exampleVertices[] = {
    {10.0f, 20.0f, 30.0f},
    {-10.0f, 20.0f, 30.0f},
};

const Edge exampleEdges[] = {
    {0, 1, 0, 1, 31, true},
};

const Face exampleFaces[] = {
    {0.0f, 62.0f, 31.0f, 31},
    {0.0f, -30.0f, 6.0f, 31},
};
```

The unused vertex face fields are intentionally omitted. Edge face indices are
kept because hidden-line removal needs them.

### Demo Entry Point

Add a wrapper like:

```cpp
void demoNewShip(uint32_t localMs, bool firstFrame, bool hiddenLineRemoval, uint16_t scalePercent)
{
    (void)firstFrame;
    renderVectorModel(newShipModel, localMs, hiddenLineRemoval, scalePercent);
}

void demoNewShip(uint32_t localMs, bool firstFrame)
{
    demoNewShip(localMs, firstFrame, DEFAULT_HIDDEN_LINE_REMOVAL, 50);
}
```

Declare both functions in `src/cobra_demo.h`, then add the two-argument function
to the carousel in `src/main.cpp`.

For example:

```cpp
const DemoPage demos[] = {
    {"NEW", demoNewShip, 30000},
    ...
};
```

The duration is in milliseconds.

## Scale Tuning

Each public vector demo has a default scale:

```cpp
DEFAULT_SCALE_PERCENT
DEFAULT_THARGOID_SCALE_PERCENT
DEFAULT_ELITE_LOGO_SCALE_PERCENT
```

You can also call the four-argument function directly:

```cpp
demoCobraMkIII(localMs, firstFrame, true, 75);
demoThargoid(localMs, firstFrame, false, 40);
demoEliteLogo(localMs, firstFrame, true, 32);
```

Good first guesses:

- Large Elite ships: `35..55`
- Cobra-sized ships: `55..75`
- Small/simple ships: `70..110`
- Wide logos or title objects: `25..40`

If the model clips too often, lower the scale. If it looks too small or lacks
impact, increase the scale. The renderer clamps scale to `25..220`.

## Troubleshooting New Models

If the model looks inside-out:

- Verify the face normal signs copied correctly.
- Try setting `useFaceCulling = false` temporarily.
- Try calling the demo with `hiddenLineRemoval = false` so dashed far-side edges are visible.

If edges connect to the wrong places:

- Check that Elite vertex numbers are zero-based in the table.
- Check every `EDGE` row maps to `{vertex1, vertex2, face1, face2, visibility, strong}`.

If the model is invisible or mostly missing:

- Increase or decrease the default scale.
- Disable hidden-line removal while debugging.
- Check `MAX_MODEL_VERTICES` and `MAX_MODEL_FACES`.

If stars show through the model:

- Confirm `hiddenLineRemoval` is enabled.
- Confirm `maskStarsBehindModel()` is still called before edges are drawn.
- For very concave models, the convex hull mask may not match the exact outline.

If a decorative logo loses lines:

- Set `useFaceCulling = false`.
- Mark logo/detail edges as `strong = true`.

## Capacity Limits

The renderer currently allows:

```cpp
MAX_MODEL_VERTICES = 48
MAX_MODEL_FACES = 16
```

The Cobra, Thargoid, and ELITE logo all fit within these limits. If a future
model has more vertices or faces, increase these constants. The arrays are stack
allocated per frame, so avoid making them huge without checking RAM.

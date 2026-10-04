// Hydraulic erosion compute shader — pipe model (Mei et al. 2007)
// Two passes per iteration: PASS_FLUX (rain + flux) and PASS_TRANSPORT (water + erosion + evaporation)
//
// SSBO layout:
//   binding 0: float height[]        — terrain height per cell (1025*1025)
//   binding 1: float waterSediment[] — interleaved [water, sediment] per cell (1025*1025*2)
//   binding 2: float flux[]          — outflow flux [L, R, T, B] per cell (1025*1025*4)
//   binding 3: float params[]        — simulation + constraint parameters
//   (original heights stored at end of height buffer, offset = width*height)

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout(std430, binding = 0) buffer HeightBuffer
{
    float height[];  // [0..N-1] = current heights, [N..2N-1] = original heights
};

layout(std430, binding = 1) buffer WaterSedimentBuffer
{
    float waterSediment[];  // interleaved: [water0, sed0, water1, sed1, ...]
};

layout(std430, binding = 2) buffer FluxBuffer
{
    float flux[];  // [L0, R0, T0, B0, L1, R1, T1, B1, ...]
};

layout(std430, binding = 3) buffer ParamsBuffer
{
    float params[];
};

// Parameter indices
#define P_DT            0
#define P_RAINFALL      1
#define P_GRAVITY       2
#define P_PIPE_AREA     3
#define P_KC            4
#define P_KS            5
#define P_KD            6
#define P_KE            7
#define P_WIDTH         8
#define P_HEIGHT        9
#define P_MAX_DEPTH    10   // max carve depth below original (world units)
#define P_MIN_HEIGHT   11   // absolute min height floor (world units)
#define P_RIDGE_PROTECT 12  // ridge protection [0..1]
#define P_BORDER_PAD   13   // no-erode border in cells

// Helper: 2D index to 1D
int idx(int x, int y)
{
    int w = int(params[P_WIDTH]);
    return y * w + x;
}

// Helper: read water at (x,y)
float getWater(int x, int y)
{
    return waterSediment[idx(x, y) * 2];
}

// Helper: read sediment at (x,y)
float getSediment(int x, int y)
{
    return waterSediment[idx(x, y) * 2 + 1];
}

// Helper: read flux component at (x,y), component c (0=L, 1=R, 2=T, 3=B)
float getFlux(int x, int y, int c)
{
    return flux[idx(x, y) * 4 + c];
}

// Check if cell is in the no-erode border zone
bool inBorder(int x, int y, int w, int h, int pad)
{
    return (x < pad || x >= w - pad || y < pad || y >= h - pad);
}

#ifdef PASS_FLUX
void CS()
{
    int x = int(gl_GlobalInvocationID.x);
    int y = int(gl_GlobalInvocationID.y);
    int w = int(params[P_WIDTH]);
    int h = int(params[P_HEIGHT]);

    if (x >= w || y >= h)
        return;

    int borderPad = int(params[P_BORDER_PAD]);
    if (inBorder(x, y, w, h, borderPad))
        return;  // no rain or flux in border zone

    float dt       = params[P_DT];
    float rainfall = params[P_RAINFALL];
    float g        = params[P_GRAVITY];
    float A        = params[P_PIPE_AREA];

    int i = idx(x, y);
    int wi = i * 2;      // water/sediment index
    int fi = i * 4;      // flux index

    // Add rainfall
    waterSediment[wi] += rainfall * dt;
    float water = waterSediment[wi];

    // Total height at this cell
    float totalH = height[i] + water;

    // Compute flux to each neighbor (pipe model)
    float fL = 0.0, fR = 0.0, fT = 0.0, fB = 0.0;

    if (x > borderPad)
    {
        float nH = height[idx(x - 1, y)] + getWater(x - 1, y);
        fL = max(0.0, flux[fi + 0] + dt * A * g * (totalH - nH));
    }
    if (x < w - 1 - borderPad)
    {
        float nH = height[idx(x + 1, y)] + getWater(x + 1, y);
        fR = max(0.0, flux[fi + 1] + dt * A * g * (totalH - nH));
    }
    if (y > borderPad)
    {
        float nH = height[idx(x, y - 1)] + getWater(x, y - 1);
        fT = max(0.0, flux[fi + 2] + dt * A * g * (totalH - nH));
    }
    if (y < h - 1 - borderPad)
    {
        float nH = height[idx(x, y + 1)] + getWater(x, y + 1);
        fB = max(0.0, flux[fi + 3] + dt * A * g * (totalH - nH));
    }

    // Scale flux so total outflow doesn't exceed available water
    float totalOutflow = fL + fR + fT + fB;
    if (totalOutflow > 0.0)
    {
        float K = min(1.0, water / (totalOutflow * dt + 0.0001));
        fL *= K;
        fR *= K;
        fT *= K;
        fB *= K;
    }

    flux[fi + 0] = fL;
    flux[fi + 1] = fR;
    flux[fi + 2] = fT;
    flux[fi + 3] = fB;
}
#endif

#ifdef PASS_TRANSPORT
void CS()
{
    int x = int(gl_GlobalInvocationID.x);
    int y = int(gl_GlobalInvocationID.y);
    int w = int(params[P_WIDTH]);
    int h = int(params[P_HEIGHT]);

    if (x >= w || y >= h)
        return;

    int borderPad = int(params[P_BORDER_PAD]);
    if (inBorder(x, y, w, h, borderPad))
        return;

    float dt = params[P_DT];
    float Kc = params[P_KC];
    float Ks = params[P_KS];
    float Kd = params[P_KD];
    float Ke = params[P_KE];
    float maxDepth = params[P_MAX_DEPTH];
    float minHeight = params[P_MIN_HEIGHT];
    float ridgeProtect = params[P_RIDGE_PROTECT];

    int i = idx(x, y);
    int N = w * h;  // offset for original heights
    int wi = i * 2;
    int fi = i * 4;

    float oldWater = waterSediment[wi];
    float sediment = waterSediment[wi + 1];
    float origH = height[N + i];  // original height before erosion

    // Compute inflow from neighbors
    float inflow = 0.0;
    if (x > 0)     inflow += getFlux(x - 1, y, 1);
    if (x < w - 1) inflow += getFlux(x + 1, y, 0);
    if (y > 0)     inflow += getFlux(x, y - 1, 3);
    if (y < h - 1) inflow += getFlux(x, y + 1, 2);

    // Outflow from self
    float outflow = flux[fi + 0] + flux[fi + 1] + flux[fi + 2] + flux[fi + 3];

    // Update water level
    float dV = (inflow - outflow) * dt;
    float newWater = max(0.0, oldWater + dV);

    // Compute velocity from flux differences
    float avgWater = 0.5 * (oldWater + newWater) + 0.0001;
    float vx = 0.0, vy = 0.0;

    float fL_self = flux[fi + 0];
    float fR_self = flux[fi + 1];
    float fL_right = (x < w - 1) ? getFlux(x + 1, y, 0) : 0.0;
    float fR_left  = (x > 0)     ? getFlux(x - 1, y, 1) : 0.0;
    vx = ((fR_left - fL_self) + (fR_self - fL_right)) / (2.0 * avgWater);

    float fT_self = flux[fi + 2];
    float fB_self = flux[fi + 3];
    float fT_below = (y < h - 1) ? getFlux(x, y + 1, 2) : 0.0;
    float fB_above = (y > 0)     ? getFlux(x, y - 1, 3) : 0.0;
    vy = ((fB_above - fT_self) + (fB_self - fT_below)) / (2.0 * avgWater);

    // Compute local slope
    float dHx = 0.0, dHy = 0.0;
    if (x > 0 && x < w - 1)
        dHx = (height[idx(x + 1, y)] - height[idx(x - 1, y)]) * 0.5;
    else if (x > 0)
        dHx = height[i] - height[idx(x - 1, y)];
    else
        dHx = height[idx(x + 1, y)] - height[i];

    if (y > 0 && y < h - 1)
        dHy = (height[idx(x, y + 1)] - height[idx(x, y - 1)]) * 0.5;
    else if (y > 0)
        dHy = height[i] - height[idx(x, y - 1)];
    else
        dHy = height[idx(x, y + 1)] - height[i];

    float sinTilt = length(vec2(dHx, dHy));
    sinTilt = max(sinTilt, 0.01);

    // Ridge protection: reduce erosion rate on convex terrain (ridgelines)
    float effectiveKs = Ks;
    if (ridgeProtect > 0.0)
    {
        // Laplacian of height — negative = convex (ridge), positive = concave (valley)
        float laplacian = 0.0;
        if (x > 0 && x < w - 1)
            laplacian += height[idx(x + 1, y)] + height[idx(x - 1, y)] - 2.0 * height[i];
        if (y > 0 && y < h - 1)
            laplacian += height[idx(x, y + 1)] + height[idx(x, y - 1)] - 2.0 * height[i];

        // laplacian < 0 = ridge/peak → reduce erosion
        // laplacian > 0 = valley → full erosion
        float ridgeFactor = clamp(laplacian * 10.0 + 1.0, 0.0, 1.0);
        effectiveKs *= mix(1.0, ridgeFactor, ridgeProtect);
    }

    // Sediment transport capacity
    float speed = length(vec2(vx, vy));
    float capacity = Kc * speed * sinTilt * max(0.01, newWater);

    // Erosion / deposition
    if (sediment < capacity)
    {
        float erodeAmt = effectiveKs * (capacity - sediment);
        erodeAmt = min(erodeAmt, height[i] * 0.1);

        // Max depth constraint: don't erode below (origH - maxDepth)
        if (maxDepth > 0.0)
        {
            float floorFromDepth = origH - maxDepth;
            float availableErode = max(0.0, height[i] - floorFromDepth);
            erodeAmt = min(erodeAmt, availableErode);
        }

        // Min height constraint: don't erode below absolute floor
        if (minHeight > 0.0)
        {
            float availableErode = max(0.0, height[i] - minHeight);
            erodeAmt = min(erodeAmt, availableErode);
        }

        height[i] -= erodeAmt;
        sediment += erodeAmt;
    }
    else
    {
        float depositAmt = Kd * (sediment - capacity);
        height[i] += depositAmt;
        sediment -= depositAmt;
    }

    // Semi-Lagrangian sediment advection
    float srcX = float(x) - vx * dt;
    float srcY = float(y) - vy * dt;
    srcX = clamp(srcX, 0.0, float(w - 1));
    srcY = clamp(srcY, 0.0, float(h - 1));

    int x0 = int(floor(srcX));
    int y0 = int(floor(srcY));
    int x1 = min(x0 + 1, w - 1);
    int y1 = min(y0 + 1, h - 1);
    float fx = srcX - float(x0);
    float fy = srcY - float(y0);

    float s00 = getSediment(x0, y0);
    float s10 = getSediment(x1, y0);
    float s01 = getSediment(x0, y1);
    float s11 = getSediment(x1, y1);
    float advectedSediment = mix(mix(s00, s10, fx), mix(s01, s11, fx), fy);

    sediment = mix(sediment, advectedSediment, 0.5);

    // Evaporation
    newWater *= (1.0 - Ke * dt);

    // Write back
    waterSediment[wi] = max(0.0, newWater);
    waterSediment[wi + 1] = max(0.0, sediment);
}
#endif

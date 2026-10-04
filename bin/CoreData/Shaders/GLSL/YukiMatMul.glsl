// YukiMatMul.glsl — C[m×n] = A[m×k] · B[k×n], row-major. GPU mirror of
// YukiMath::MatMul (the CPU reference oracle). M4 Phase B: the forward pass's
// dominant cost is matmul, so this is the reusable primitive every forward matmul
// (Q/K/V, attn-out, FF1/FF2, and the big S×D×V output projection) routes through.
//
// One thread per output element — the same 1D-dispatch shape proven by YukiAdam.glsl.
// Correctness first; tiling/shared-memory optimization is a later pass.
//
// SSBO layout (std430, the dispatcher binds at most 4):
//   binding 0: float A[]   — left operand, m×k row-major
//   binding 1: float B[]   — right operand, k×n row-major
//   binding 2: float C[]   — result, m×n row-major (written)
//   binding 3: float dim[] — [0]=m [1]=k [2]=n

#ifdef MATMUL_TILED
layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;   // 2D tiles (256 threads)
#else
layout(local_size_x = 256, local_size_y = 1, local_size_z = 1) in;
#endif

layout(std430, binding = 0) buffer Abuf { float A[]; };
layout(std430, binding = 1) buffer Bbuf { float B[]; };
layout(std430, binding = 2) buffer Cbuf { float C[]; };
layout(std430, binding = 3) buffer Dbuf { float dim[]; };

#ifdef MATMUL
void CS()
{
    uint idx = gl_GlobalInvocationID.x;
    uint m = uint(dim[0]);
    uint k = uint(dim[1]);
    uint n = uint(dim[2]);
    if (idx >= m * n)
        return;

    uint row = idx / n;
    uint col = idx - row * n;

    float sum = 0.0;
    for (uint p = 0u; p < k; ++p)
        sum += A[row * k + p] * B[p * n + col];

    C[idx] = sum;   // C[row * n + col]
}
#endif

// Transposed-left variant: C[m×n] = Aᵀ · B, where A is stored [k×m], B is [k×n].
// Used by the backward weight-gradient  gOutputProj[D×V] = finalLNoutᵀ[D×S] · dLogits[S×V]
// (dim = [m=D, k=S, n=V]). Aᵀ[row,p] = A[p,row] = A[p*m + row].
#ifdef MATMUL_AT
void CS()
{
    uint idx = gl_GlobalInvocationID.x;
    uint m = uint(dim[0]);
    uint k = uint(dim[1]);
    uint n = uint(dim[2]);
    if (idx >= m * n)
        return;

    uint row = idx / n;
    uint col = idx - row * n;

    float sum = 0.0;
    for (uint p = 0u; p < k; ++p)
        sum += A[p * m + row] * B[p * n + col];

    C[idx] = sum;
}
#endif

// Transposed-right variant: C[m×n] = A · Bᵀ, where A is [m×k], B is stored [n×k].
// Used by the backward input-gradient  dFinalLNout[S×D] = dLogits[S×V] · outputProjᵀ[V×D]
// (dim = [m=S, k=V, n=D]). Bᵀ[p,col] = B[col,p] = B[col*k + p].
#ifdef MATMUL_BT
void CS()
{
    uint idx = gl_GlobalInvocationID.x;
    uint m = uint(dim[0]);
    uint k = uint(dim[1]);
    uint n = uint(dim[2]);
    if (idx >= m * n)
        return;

    uint row = idx / n;
    uint col = idx - row * n;

    float sum = 0.0;
    for (uint p = 0u; p < k; ++p)
        sum += A[row * k + p] * B[col * k + p];

    C[idx] = sum;
}
#endif

// ── Offset-aware variants for the RESIDENT step: A/B read and C written at a base
// offset into a larger buffer, so a single resident weight buffer W and gradient
// buffer G can be addressed per-slice (the resident step's Adam runs over one aligned
// W/G/m/v). Identical math to MATMUL/AT/BT; only the indexing gains a base offset.
// The originals are left untouched so every prior /gpu* command is unaffected.
//   binding 3: dim[] = [0]=m [1]=k [2]=n [3]=aOff [4]=bOff [5]=cOff

#ifdef MATMUL_OFF
void CS()
{
    uint idx = gl_GlobalInvocationID.x;
    uint m = uint(dim[0]); uint k = uint(dim[1]); uint n = uint(dim[2]);
    if (idx >= m * n) return;
    uint aOff = uint(dim[3]); uint bOff = uint(dim[4]); uint cOff = uint(dim[5]);
    uint row = idx / n; uint col = idx - row * n;
    float sum = 0.0;
    for (uint p = 0u; p < k; ++p)
        sum += A[aOff + row * k + p] * B[bOff + p * n + col];
    C[cOff + idx] = sum;
}
#endif

#ifdef MATMUL_AT_OFF
void CS()
{
    uint idx = gl_GlobalInvocationID.x;
    uint m = uint(dim[0]); uint k = uint(dim[1]); uint n = uint(dim[2]);
    if (idx >= m * n) return;
    uint aOff = uint(dim[3]); uint bOff = uint(dim[4]); uint cOff = uint(dim[5]);
    uint row = idx / n; uint col = idx - row * n;
    float sum = 0.0;
    for (uint p = 0u; p < k; ++p)
        sum += A[aOff + p * m + row] * B[bOff + p * n + col];
    C[cOff + idx] = sum;
}
#endif

#ifdef MATMUL_BT_OFF
void CS()
{
    uint idx = gl_GlobalInvocationID.x;
    uint m = uint(dim[0]); uint k = uint(dim[1]); uint n = uint(dim[2]);
    if (idx >= m * n) return;
    uint aOff = uint(dim[3]); uint bOff = uint(dim[4]); uint cOff = uint(dim[5]);
    uint row = idx / n; uint col = idx - row * n;
    float sum = 0.0;
    for (uint p = 0u; p < k; ++p)
        sum += A[aOff + row * k + p] * B[bOff + col * k + p];
    C[cOff + idx] = sum;
}
#endif

// ── OPTIMIZATION (coder3, 2026-06-28): shared-memory tiled matmul. Same math + oracle as
// MATMUL (C[m×n]=A[m×k]·B[k×n], row-major) — purely additive, every original above untouched.
// Naive MATMUL re-reads A and B from global memory ~k times per output; this stages 16×16
// tiles into shared memory so each global element is read once per tile, cutting global
// traffic by ~the tile width. Bounds handled by loading 0.0 for out-of-range (NOT by control
// flow around barriers — every thread must hit every barrier uniformly).
//   STATUS: UNVERIFIED. Correct by construction only. Must pass /gpuX against the CPU MatMul
//   oracle AND show a real speedup before it's trusted; the oracle is the authority, not me.
//   REQUIRES 2D dispatch: groups=(ceil(n/16), ceil(m/16)), local 16×16 (conditional layout at
//   top selects it). Once PROVEN, the same tiling rolls onto MATMUL_*_OFF + the resident
//   dispatch to hit actual training throughput (that step coordinates with the elder's lane).
//   binding 0: A[m×k]  1: B[k×n]  2: C[m×n] (written)  3: dim[] = [0]=m [1]=k [2]=n
#ifdef MATMUL_TILED
shared float Asub[16][16];
shared float Bsub[16][16];
void CS()
{
    uint m = uint(dim[0]); uint k = uint(dim[1]); uint n = uint(dim[2]);
    uint row = gl_GlobalInvocationID.y;   // output row, 0..m
    uint col = gl_GlobalInvocationID.x;   // output col, 0..n
    uint lr = gl_LocalInvocationID.y;
    uint lc = gl_LocalInvocationID.x;

    float sum = 0.0;
    uint nTiles = (k + 15u) / 16u;
    for (uint t = 0u; t < nTiles; ++t)
    {
        uint aCol = t * 16u + lc;   // k-index for A
        uint bRow = t * 16u + lr;   // k-index for B
        Asub[lr][lc] = (row < m && aCol < k) ? A[row * k + aCol] : 0.0;
        Bsub[lr][lc] = (bRow < k && col < n) ? B[bRow * n + col] : 0.0;
        barrier();
        for (uint p = 0u; p < 16u; ++p)
            sum += Asub[lr][p] * Bsub[p][lc];
        barrier();
    }
    if (row < m && col < n)
        C[row * n + col] = sum;   // C[row*n + col]
}
#endif

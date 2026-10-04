// YukiOps.glsl — non-matmul GPU compute kernels for the Yuki training step.
// Each variation is a GPU mirror of a YukiMath CPU primitive (the oracle); the
// matmuls live in YukiMatMul.glsl. SSBO layout is std430; the dispatcher binds
// at most 4 buffers, named A/B/C/dim to match the matmul shader's scheme.

layout(local_size_x = 256, local_size_y = 1, local_size_z = 1) in;

layout(std430, binding = 0) buffer Abuf { float A[]; };
layout(std430, binding = 1) buffer Bbuf { float B[]; };
layout(std430, binding = 2) buffer Cbuf { float C[]; };
layout(std430, binding = 3) buffer Dbuf { float dim[]; };

// LayerNorm forward, per row: y = gamma·(x-mean)·invStd + beta over D elements.
// Mirror of YukiMath::LayerNorm — population variance (/D), invStd=1/sqrt(var+eps),
// eps MUST match the CPU (1e-5). One thread per row.
//   binding 0: A = X[S×D]    input rows
//   binding 1: B = GB[2×D]   gamma[0..D] then beta[D..2D] (packed to fit 4 SSBOs)
//   binding 2: C = Y[S×D]    output (written)
//   binding 3: dim[] = [0]=S [1]=D [2]=eps
#ifdef LAYERNORM
void CS()
{
    uint row = gl_GlobalInvocationID.x;
    uint S = uint(dim[0]);
    uint D = uint(dim[1]);
    float eps = dim[2];
    if (row >= S)
        return;

    uint base = row * D;

    float mean = 0.0;
    for (uint i = 0u; i < D; ++i)
        mean += A[base + i];
    mean /= float(D);

    float var = 0.0;
    for (uint i = 0u; i < D; ++i)
    {
        float d = A[base + i] - mean;
        var += d * d;
    }
    var /= float(D);

    float invStd = 1.0 / sqrt(var + eps);
    for (uint i = 0u; i < D; ++i)
        C[base + i] = B[i] * (A[base + i] - mean) * invStd + B[D + i];
}
#endif

// GELU forward, elementwise. Mirror of YukiMath::GELU — tanh approximation
// (C=sqrt(2/pi)=0.7978845608, A=0.044715), so GLSL tanh() matches the CPU exactly.
//   binding 0: A = X[N]   input          binding 2: C = Y[N]   output
//   binding 3: dim[] = [0]=N
#ifdef GELU_FWD
void CS()
{
    uint idx = gl_GlobalInvocationID.x;
    uint N = uint(dim[0]);
    if (idx >= N)
        return;
    float x = A[idx];
    float cube = x * x * x;
    C[idx] = 0.5 * x * (1.0 + tanh(0.7978845608 * (x + 0.044715 * cube)));
}
#endif

// GELU backward, elementwise. Mirror of YukiMath::GELUBackward (which accumulates
// dx += dy·g'(x)); this WRITES dx = dy·g'(x), i.e. accumulate-from-zero.
//   binding 0: A = X[N]   pre-activation input   binding 1: B = dY[N]  upstream grad
//   binding 2: C = dX[N]  output (written)       binding 3: dim[] = [0]=N
#ifdef GELU_BWD
void CS()
{
    uint idx = gl_GlobalInvocationID.x;
    uint N = uint(dim[0]);
    if (idx >= N)
        return;
    float xi = A[idx];
    float u = 0.7978845608 * (xi + 0.044715 * xi * xi * xi);
    float t = tanh(u);
    float du = 0.7978845608 * (1.0 + 3.0 * 0.044715 * xi * xi);
    float grad = 0.5 * (1.0 + t) + 0.5 * xi * (1.0 - t * t) * du;
    C[idx] = B[idx] * grad;
}
#endif

// LayerNorm backward — INPUT gradient dx, per row (one thread per row).
// Mirror of YukiMath::LayerNormBackward dx branch: g_i = dy_i·w_i,
// dx_i = invStd/N·(N·g_i − Σg − xhat_i·Σ(g·xhat)). Writes dx (= accumulate-from-zero).
//   binding 0: A = X[S×D]   forward input
//   binding 1: B = dY[S×D]  upstream grad
//   binding 2: C = dX[S×D]  output
//   binding 3: dim[] = [0]=S [1]=D [2]=eps [3+i]=w[i]   (scale weights packed in)
#ifdef LN_BWD_DX
void CS()
{
    uint row = gl_GlobalInvocationID.x;
    uint S = uint(dim[0]);
    uint D = uint(dim[1]);
    float eps = dim[2];
    if (row >= S)
        return;

    uint base = row * D;
    float mean = 0.0;
    for (uint i = 0u; i < D; ++i) mean += A[base + i];
    mean /= float(D);
    float var = 0.0;
    for (uint i = 0u; i < D; ++i) { float d = A[base + i] - mean; var += d * d; }
    var /= float(D);
    float invStd = 1.0 / sqrt(var + eps);

    float sumG = 0.0, sumGxhat = 0.0;
    for (uint i = 0u; i < D; ++i)
    {
        float g = B[base + i] * dim[3u + i];
        float xhat = (A[base + i] - mean) * invStd;
        sumG += g; sumGxhat += g * xhat;
    }

    float invN = 1.0 / float(D);
    for (uint i = 0u; i < D; ++i)
    {
        float g = B[base + i] * dim[3u + i];
        float xhat = (A[base + i] - mean) * invStd;
        C[base + i] = invStd * invN * (float(D) * g - sumG - xhat * sumGxhat);
    }
}
#endif

// LayerNorm backward — PARAMETER gradients dw,db, per column (one thread per column),
// reducing over all S rows. dw_c = Σ_r dy[r,c]·xhat[r,c],  db_c = Σ_r dy[r,c].
//   binding 0: A = X[S×D]   forward input
//   binding 1: B = dY[S×D]  upstream grad
//   binding 2: C = dWdB[2×D]  dw[0..D] then db[D..2D] (written)
//   binding 3: dim[] = [0]=S [1]=D [2]=eps
#ifdef LN_BWD_DWDB
void CS()
{
    uint c = gl_GlobalInvocationID.x;
    uint S = uint(dim[0]);
    uint D = uint(dim[1]);
    float eps = dim[2];
    if (c >= D)
        return;

    float dwAcc = 0.0, dbAcc = 0.0;
    for (uint r = 0u; r < S; ++r)
    {
        uint base = r * D;
        float mean = 0.0;
        for (uint i = 0u; i < D; ++i) mean += A[base + i];
        mean /= float(D);
        float var = 0.0;
        for (uint i = 0u; i < D; ++i) { float d = A[base + i] - mean; var += d * d; }
        var /= float(D);
        float invStd = 1.0 / sqrt(var + eps);

        float xhat = (A[base + c] - mean) * invStd;
        dwAcc += B[base + c] * xhat;
        dbAcc += B[base + c];
    }
    C[c] = dwAcc;
    C[D + c] = dbAcc;
}
#endif

// Causal attention softmax forward — one thread per (head h, query row i).
// Mirror of the attention-weight loop in ForwardWithCache + YukiMath::Softmax:
// per head/query, scores[j]=dot(Q[i,head-slice],K[j,head-slice])*scale for j<=i,
// then max-subtract softmax over the i+1 causal entries. Writes the full probs row,
// zeroing the j>i upper-triangle (output buffer is uninitialised, CPU memsets it).
// Three passes over j<=i (max → exp+accumulate → normalise); the dot is recomputed
// in passes 1 and 2 so no per-row scratch array is needed (bounded by S).
//   binding 0: A = Q[S×D]    query projection
//   binding 1: B = K[S×D]    key projection
//   binding 2: C = probs[H×S×S]  causal softmax weights (written; j>i zeroed)
//   binding 3: dim[] = [0]=S [1]=D [2]=H [3]=scale (=1/sqrt(D/H), precomputed on CPU)
#ifdef ATTN_SOFTMAX
void CS()
{
    uint idx = gl_GlobalInvocationID.x;
    uint S = uint(dim[0]);
    uint D = uint(dim[1]);
    uint H = uint(dim[2]);
    float scale = dim[3];
    if (idx >= H * S)
        return;

    uint h = idx / S;
    uint i = idx % S;
    uint hd = D / H;
    uint qOff = i * D + h * hd;
    uint rowBase = (h * S + i) * S;

    // pass 1: max of scaled scores over j in [0,i]
    float maxVal = -3.402823466e38;   // -FLT_MAX
    for (uint j = 0u; j <= i; ++j)
    {
        float dot = 0.0;
        uint kOff = j * D + h * hd;
        for (uint t = 0u; t < hd; ++t)
            dot += A[qOff + t] * B[kOff + t];
        dot *= scale;
        if (dot > maxVal) maxVal = dot;
    }

    // pass 2: exp(score-max) into probs, accumulate denominator
    float sum = 0.0;
    for (uint j = 0u; j <= i; ++j)
    {
        float dot = 0.0;
        uint kOff = j * D + h * hd;
        for (uint t = 0u; t < hd; ++t)
            dot += A[qOff + t] * B[kOff + t];
        float e = exp(dot * scale - maxVal);
        C[rowBase + j] = e;
        sum += e;
    }

    // pass 3: normalise the causal entries, zero the rest of the row
    float invSum = 1.0 / sum;
    for (uint j = 0u; j <= i; ++j)
        C[rowBase + j] *= invSum;
    for (uint j = i + 1u; j < S; ++j)
        C[rowBase + j] = 0.0;
}
#endif

// Causal attention softmax backward — one thread per (head h, query row i).
// Mirror of YukiMath::SoftmaxBackward over the i+1 causal entries of the row:
// dot = Σ_{j<=i} dy_j·y_j,  dx_j = y_j·(dy_j − dot).  The CPU primitive accumulates
// (dx +=); this WRITES dx (accumulate-from-zero, like GELU_BWD) and zeros the j>i
// upper-triangle (output buffer is uninitialised; the j>i entries never enter dot).
//   binding 0: A = y   = probs[H×S×S]   softmax output (forward weights)
//   binding 1: B = dy  = [H×S×S]        upstream gradient
//   binding 2: C = dx  = [H×S×S]        output (written; j>i zeroed)
//   binding 3: dim[] = [0]=S [1]=H
#ifdef ATTN_SOFTMAX_BWD
void CS()
{
    uint idx = gl_GlobalInvocationID.x;
    uint S = uint(dim[0]);
    uint H = uint(dim[1]);
    if (idx >= H * S)
        return;

    uint h = idx / S;
    uint i = idx % S;
    uint rowBase = (h * S + i) * S;

    float dot = 0.0;
    for (uint j = 0u; j <= i; ++j)
        dot += B[rowBase + j] * A[rowBase + j];

    for (uint j = 0u; j <= i; ++j)
        C[rowBase + j] = A[rowBase + j] * (B[rowBase + j] - dot);
    for (uint j = i + 1u; j < S; ++j)
        C[rowBase + j] = 0.0;
}
#endif

// Causal attention context forward — head-strided (NOT plain matmul).
// Mirror of the ctx loop in ForwardWithCache: one thread per (head h, query i),
// context[i, h-slice] = Σ_{j<=i} probs[h,i,j]·V[j, h-slice]. Each thread writes its
// own hd-wide output slice (every output element is covered exactly once, so no
// zeroing is needed — unlike the probs upper-triangle).
//   binding 0: A = probs[H×S×S]   causal softmax weights
//   binding 1: B = V[S×D]         value projection
//   binding 2: C = context[S×D]   output (written, full coverage)
//   binding 3: dim[] = [0]=S [1]=D [2]=H
#ifdef ATTN_CTX_FWD
void CS()
{
    uint idx = gl_GlobalInvocationID.x;
    uint S = uint(dim[0]);
    uint D = uint(dim[1]);
    uint H = uint(dim[2]);
    if (idx >= H * S)
        return;

    uint h = idx / S;
    uint i = idx % S;
    uint hd = D / H;
    uint rowBase = (h * S + i) * S;     // probs[h, i, :]
    uint outBase = i * D + h * hd;      // context[i, h-slice]

    for (uint tt = 0u; tt < hd; ++tt)
    {
        float sum = 0.0;
        for (uint j = 0u; j <= i; ++j)
            sum += A[rowBase + j] * B[j * D + h * hd + tt];
        C[outBase + tt] = sum;
    }
}
#endif

// ── Attention backward, head-strided pieces. Mirror of the per-head core of
// YukiMath::AttentionBackward (dHeadsOut = dContext after the plain Wo-backward).
// Each kernel writes one output with FULL coverage (every element by exactly one
// thread), so no zeroing — except ATTN_DPROBS which leaves the j>i triangle, zeroed
// explicitly to match the CPU dA (which only fills j<=i). scale = 1/sqrt(D/H). ──

// dV = probsᵀ·dContext, per head, causal. One thread per (head h, key row j):
// dV[j, h-slice] = Σ_{i>=j} probs[h,i,j]·dContext[i, h-slice].
//   binding 0: A = probs[H×S×S]   1: B = dContext[S×D]   2: C = dV[S×D]
//   binding 3: dim[] = [0]=S [1]=D [2]=H
#ifdef ATTN_DV
void CS()
{
    uint idx = gl_GlobalInvocationID.x;
    uint S = uint(dim[0]); uint D = uint(dim[1]); uint H = uint(dim[2]);
    if (idx >= H * S) return;
    uint h = idx / S; uint j = idx % S; uint hd = D / H;
    uint outBase = j * D + h * hd;
    for (uint tt = 0u; tt < hd; ++tt)
    {
        float sum = 0.0;
        for (uint i = j; i < S; ++i)
            sum += A[(h * S + i) * S + j] * B[i * D + h * hd + tt];
        C[outBase + tt] = sum;
    }
}
#endif

// dProbs = dContext·Vᵀ, per head, causal. One thread per (head h, query row i):
// dProbs[h,i,j] = Σ_d dContext[i, h-slice]·V[j, h-slice] for j<=i, else 0.
//   binding 0: A = dContext[S×D]   1: B = V[S×D]   2: C = dProbs[H×S×S]
//   binding 3: dim[] = [0]=S [1]=D [2]=H
#ifdef ATTN_DPROBS
void CS()
{
    uint idx = gl_GlobalInvocationID.x;
    uint S = uint(dim[0]); uint D = uint(dim[1]); uint H = uint(dim[2]);
    if (idx >= H * S) return;
    uint h = idx / S; uint i = idx % S; uint hd = D / H;
    uint qOff = i * D + h * hd;
    uint rowBase = (h * S + i) * S;
    for (uint j = 0u; j <= i; ++j)
    {
        float sum = 0.0;
        uint vOff = j * D + h * hd;
        for (uint d = 0u; d < hd; ++d)
            sum += A[qOff + d] * B[vOff + d];
        C[rowBase + j] = sum;
    }
    for (uint j = i + 1u; j < S; ++j)
        C[rowBase + j] = 0.0;
}
#endif

// dQ = (dScores·scale)·K, per head, causal. One thread per (head h, query row i):
// dQ[i, h-slice] = scale·Σ_{j<=i} dScores[h,i,j]·K[j, h-slice].
//   binding 0: A = dScores[H×S×S]   1: B = K[S×D]   2: C = dQ[S×D]
//   binding 3: dim[] = [0]=S [1]=D [2]=H [3]=scale
#ifdef ATTN_DQ
void CS()
{
    uint idx = gl_GlobalInvocationID.x;
    uint S = uint(dim[0]); uint D = uint(dim[1]); uint H = uint(dim[2]); float scale = dim[3];
    if (idx >= H * S) return;
    uint h = idx / S; uint i = idx % S; uint hd = D / H;
    uint rowBase = (h * S + i) * S;
    uint outBase = i * D + h * hd;
    for (uint tt = 0u; tt < hd; ++tt)
    {
        float sum = 0.0;
        for (uint j = 0u; j <= i; ++j)
            sum += A[rowBase + j] * B[j * D + h * hd + tt];
        C[outBase + tt] = sum * scale;
    }
}
#endif

// dK = (dScoresᵀ·scale)·Q, per head, causal. One thread per (head h, key row j):
// dK[j, h-slice] = scale·Σ_{i>=j} dScores[h,i,j]·Q[i, h-slice].
//   binding 0: A = dScores[H×S×S]   1: B = Q[S×D]   2: C = dK[S×D]
//   binding 3: dim[] = [0]=S [1]=D [2]=H [3]=scale
#ifdef ATTN_DK
void CS()
{
    uint idx = gl_GlobalInvocationID.x;
    uint S = uint(dim[0]); uint D = uint(dim[1]); uint H = uint(dim[2]); float scale = dim[3];
    if (idx >= H * S) return;
    uint h = idx / S; uint j = idx % S; uint hd = D / H;
    uint outBase = j * D + h * hd;
    for (uint tt = 0u; tt < hd; ++tt)
    {
        float sum = 0.0;
        for (uint i = j; i < S; ++i)
            sum += A[(h * S + i) * S + j] * B[i * D + h * hd + tt];
        C[outBase + tt] = sum * scale;
    }
}
#endif

// Embedding forward — gather rows by token id. x[s, :] = embedding[tokens[s], :].
// One thread per output element (s,d). Mirror of the embedding lookup in ForwardWithCache.
//   binding 0: A = embedding[vocab×D]   1: B = tokens[S] (token id as float, exact < 2^24)
//   binding 2: C = x[S×D] (written)     binding 3: dim[] = [0]=S [1]=D
#ifdef EMB_FWD
void CS()
{
    uint idx = gl_GlobalInvocationID.x;
    uint S = uint(dim[0]); uint D = uint(dim[1]);
    if (idx >= S * D) return;
    uint s = idx / D; uint d = idx - s * D;
    uint v = uint(B[s]);
    C[idx] = A[v * D + d];
}
#endif

// Embedding backward — scatter-add adjoint of the gather, done GATHER-BY-OUTPUT so no
// atomics and no serial pass: one thread per dEmb element (v,d) sums every position whose
// token is v. dEmb[v, d] = Σ_{s: tokens[s]==v} dx[s, d]. Repeated tokens accumulate
// correctly + deterministically (the thread scans all S). Rows with no token stay 0
// (matches YukiMath::EmbeddingBackward's accumulate-into-zeroed-dEmbedding).
//   binding 0: A = dx[S×D]              1: B = tokens[S] (float)
//   binding 2: C = dEmb[vocab×D] (written)  binding 3: dim[] = [0]=S [1]=D [2]=vocab
#ifdef EMB_BWD
void CS()
{
    uint idx = gl_GlobalInvocationID.x;
    uint S = uint(dim[0]); uint D = uint(dim[1]); uint vocab = uint(dim[2]);
    if (idx >= vocab * D) return;
    uint v = idx / D; uint d = idx - v * D;
    float sum = 0.0;
    for (uint s = 0u; s < S; ++s)
        if (uint(B[s]) == v)
            sum += A[s * D + d];
    C[idx] = sum;
}
#endif

// Sequence cross-entropy loss + gradient. Mirror of YukiMath::SequenceCrossEntropy
// (SoftmaxCrossEntropy + CrossEntropyGradient): one thread per position p computes the
// per-row softmax over vocab, writes dLogits[p,:] = (probs − onehot(tgt))·invCount, and
// the raw per-position nll = −log(max(probs[tgt],1e-10)) into the tail slot. A sentinel
// target>=V (e.g. the last position) zeros that row + nll. invCount = 1/(#valid targets),
// computed CPU-side from the targets (a known input). The exp values are stashed in the
// dLogits row as scratch, then overwritten in place — each thread owns its own row.
//   binding 0: A = logits[S×V]    1: B = targets[S] (id as float, sentinel >= V skips)
//   binding 2: C = out[S×V + S]   dLogits[0 .. S·V), then raw nll[S·V .. S·V+S)
//   binding 3: dim[] = [0]=S [1]=V [2]=invCount
#ifdef CROSS_ENTROPY
void CS()
{
    uint p = gl_GlobalInvocationID.x;
    uint S = uint(dim[0]); uint V = uint(dim[1]); float invCount = dim[2];
    if (p >= S) return;
    uint base = p * V;
    uint tgt = uint(B[p]);
    if (tgt >= V)                       // sentinel: no target here
    {
        for (uint v = 0u; v < V; ++v) C[base + v] = 0.0;
        C[S * V + p] = 0.0;
        return;
    }
    float mx = A[base];
    for (uint v = 1u; v < V; ++v) if (A[base + v] > mx) mx = A[base + v];
    float sum = 0.0;
    for (uint v = 0u; v < V; ++v) { float e = exp(A[base + v] - mx); C[base + v] = e; sum += e; }
    float inv = 1.0 / sum;
    float ptgt = C[base + tgt] * inv;   // read before the row is overwritten
    for (uint v = 0u; v < V; ++v)
    {
        float pr = C[base + v] * inv;
        C[base + v] = (pr - ((v == tgt) ? 1.0 : 0.0)) * invCount;
    }
    float pc = ptgt < 1e-10 ? 1e-10 : ptgt;
    C[S * V + p] = -log(pc);
}
#endif

// On-GPU glue for the resident step (replaces verify-time CPU-side adds/zeroing so
// activations/grads never leave the GPU). One thread per element.

// Elementwise add: C = A + B. Residuals (a=x+ctxWo, b=a+ff2) and grad sums
// (dA=dB+dhFF, dX=dA+dHq+dHk+dHv via chaining) route through this.
//   binding 0: A[N]   1: B[N]   2: C[N] (written; may alias A or B)   3: dim[]=[0]=N
#ifdef ELEM_ADD
void CS()
{
    uint idx = gl_GlobalInvocationID.x;
    if (idx >= uint(dim[0])) return;
    C[idx] = A[idx] + B[idx];
}
#endif

// Zero-fill: C = 0. Clears the gradient buffer G between steps on-GPU.
//   binding 2: C[N] (written)   binding 3: dim[]=[0]=N
#ifdef ELEM_ZERO
void CS()
{
    uint idx = gl_GlobalInvocationID.x;
    if (idx >= uint(dim[0])) return;
    C[idx] = 0.0;
}
#endif

// Offset copy: C[dstOff + idx] = A[srcOff + idx]. Places a computed grad block (LN
// weight grads, embedding grad) into the single resident gradient buffer G at its
// BuildModelGrads-layout offset, on-GPU.
//   binding 0: A[src]   2: C[dst]   3: dim[]=[0]=N [1]=dstOff [2]=srcOff
#ifdef COPY_OFFSET
void CS()
{
    uint idx = gl_GlobalInvocationID.x;
    if (idx >= uint(dim[0])) return;
    C[uint(dim[1]) + idx] = A[uint(dim[2]) + idx];
}
#endif

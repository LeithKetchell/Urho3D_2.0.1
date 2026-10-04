// Yuki Adam optimizer step — one elementwise compute dispatch over the flat weight
// vector. GPU mirror of YukiMath::AdamStep (the CPU reference oracle). M4 Phase A.
//
// SSBO layout (std430, the dispatcher binds at most 4):
//   binding 0: float w[]      — weights, updated in place
//   binding 1: float g[]      — gradients (read)
//   binding 2: float mv[]     — Adam moments, INTERLEAVED [m0,v0, m1,v1, ...]
//                               (m and v packed into one buffer to stay within 4 SSBOs)
//   binding 3: float params[] — [0]=lr [1]=beta1 [2]=beta2 [3]=eps
//                               [4]=bc1 [5]=bc2 [6]=count [7]=gclip [8]=gscale
//   gclip: per-element gradient value-clip threshold applied BEFORE the moment
//   update; 0 = disabled. Tames the intermittent huge page-sum spikes (gradMax
//   bounced to 300..6500 on a converged model) that poison Adam's v and drive
//   the solver uphill. Value-clip (not norm-clip) — cheap, no reduction.
// bc1 = 1-beta1^t, bc2 = 1-beta2^t are computed on the CPU (matching AdamStep), so
// the shader needs no timestep.

layout(local_size_x = 256, local_size_y = 1, local_size_z = 1) in;

layout(std430, binding = 0) buffer Weights   { float w[]; };
layout(std430, binding = 1) buffer Gradients { float g[]; };
layout(std430, binding = 2) buffer Moments   { float mv[]; };
layout(std430, binding = 3) buffer Params    { float params[]; };

#ifdef ADAM_STEP
void CS()
{
    uint i = gl_GlobalInvocationID.x;
    uint n = uint(params[6]);
    if (i >= n)
        return;

    float lr  = params[0];
    float b1  = params[1];
    float b2  = params[2];
    float eps = params[3];
    float bc1 = params[4];
    float bc2 = params[5];

    float gclip = params[7];
    float gscale = params[8];            // page-average scale (1/P): keeps the gradient scale into
                                          // Adam CONSTANT regardless of page size, so v isn't poisoned
                                          // by the partial last page. 1.0 = no scale (single-seq step).
    // Clip the SUMMED gradient FIRST (so /gclip is tunable directly against the gradMax readout,
    // which is also the summed Gacc), THEN apply the page-average scale.
    float gi = g[i];
    if (gclip > 0.0)
        gi = clamp(gi, -gclip, gclip);   // clamp hard-batch spikes before they poison v
    gi *= gscale;                        // page-average by 1/P after clipping
    float mi = b1 * mv[2u * i]      + (1.0 - b1) * gi;
    float vi = b2 * mv[2u * i + 1u] + (1.0 - b2) * gi * gi;
    mv[2u * i]      = mi;
    mv[2u * i + 1u] = vi;

    float mhat = mi / bc1;
    float vhat = vi / bc2;
    w[i] -= lr * mhat / (sqrt(vhat) + eps);
}
#endif

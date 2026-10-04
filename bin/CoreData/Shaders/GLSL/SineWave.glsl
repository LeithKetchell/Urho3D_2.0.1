// SineWave.glsl — fill a flat SSBO with a sine wave, computed entirely on the GPU.
// A smoke test for the compute path: upload params -> dispatch -> readback -> draw.
// If the harness shows a clean sine, the GPU dispatch + readback + ProgressGraph
// display path all work end to end. Same CS()/#ifdef variation shape as YukiAdam.glsl.
//
// SSBO layout (std430):
//   binding 0: float outv[]   — sine samples written here (the only output)
//   binding 1: float params[] — [0]=count [1]=freq [2]=phase [3]=amp

layout(local_size_x = 256, local_size_y = 1, local_size_z = 1) in;

layout(std430, binding = 0) buffer Out    { float outv[]; };
layout(std430, binding = 1) buffer Params { float params[]; };

#ifdef SINE_FILL
void CS()
{
    uint i = gl_GlobalInvocationID.x;
    uint n = uint(params[0]);
    if (i >= n)
        return;

    float freq  = params[1];
    float phase = params[2];
    float amp   = params[3];
    outv[i] = amp * sin(freq * float(i) + phase);
}
#endif

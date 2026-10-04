// YukiGradCheck — Finite-difference gradient checker.
// Copyright (c) 2026 Urho3D project. License: MIT.
//
// The gate. Every backward pass in the Yuki training chain (M3–M8) must clear
// this before it is trusted: perturb each parameter by ±eps, measure the loss
// response, and compare that numerical gradient against the analytic one. If
// they disagree, the backward math is wrong — no exceptions.
//
// CheckGradient is the reusable primitive; RunYukiGradCheckSelfTest exercises
// the M0 math library so the foundation itself is proven, not assumed.

#pragma once

#include "../Urho3D.h"

#include <functional>

namespace Urho3D
{

namespace YukiMath
{

/// Outcome of a gradient check.
struct GradCheckResult
{
    float maxRelErr{};      ///< Worst central-difference-vs-analytic relative error.
    unsigned worstIndex{};  ///< Index of the parameter with the worst error.
    bool passed{};          ///< True if maxRelErr < tolerance.
};

/// Central-difference gradient check. `loss` evaluates the scalar loss from the
/// current contents of `params`. `analyticGrad[count]` is the claimed
/// d(loss)/d(params). Each parameter is perturbed ±eps (and restored) to form a
/// numerical gradient; the worst relative error against `analyticGrad` is
/// returned. An element whose ABSOLUTE error is below `atol` is treated as a pass
/// and excluded from the relative-error worst-case: at a legitimately-zero
/// gradient (e.g. a saturated GELU neuron) relative error is ill-conditioned —
/// finite-difference noise of ~1e-4 against a true zero reads as rel-err 1.0 — so
/// it must be judged by absolute error. A real gradient bug lands where the
/// gradient is O(1), far above `atol`, and still fails on relative error.
URHO3D_API GradCheckResult CheckGradient(const std::function<double()>& loss,
                                         float* params, const float* analyticGrad,
                                         unsigned count,
                                         float eps = 1e-3f, float tol = 2e-2f,
                                         float atol = 1e-3f);

/// Run the built-in self-test over every M0 primitive (MatMul, Softmax, GELU,
/// LayerNorm, cross-entropy). Logs a PASS/FAIL line per check via the engine Log.
/// Returns true only if all checks pass. Deterministic (fixed RNG seed).
URHO3D_API bool RunYukiGradCheckSelfTest();

}

}

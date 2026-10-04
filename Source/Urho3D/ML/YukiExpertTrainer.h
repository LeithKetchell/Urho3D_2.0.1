// YukiExpertTrainer — Yuki Federation P5: trial-and-error training harness for expert subcarts.
// Copyright (c) 2026 Urho3D project. License: MIT.
//
// WHAT (design: Claude/YUKI_FED_P5_DESIGN.md, author coder3):
//   Trains a small "expert" subcart by TRIAL AND ERROR (a bounded two-tank hybrid GA),
//   NOT by backprop. The Yuki core stays gradient-trained and UNTOUCHED — a subcart is an
//   evolved critter that learns to act against the core's outputs. Output = a finished
//   "<expertId>.cart" that P2's registry Load()s and P3 dispatches to. P5 is a PRODUCER;
//   P2/P3 are consumers. No core change, no new P2 API.
//
// SCAFFOLD STATUS (2026-07-06, coder5): this is the STRUCTURE only. Two seams are deliberately
//   left pluggable and UNIMPLEMENTED because they are Leith's open gates:
//     - Fitness  (§10.2, OPEN) — how a subcart's "acting against Yuki's outputs" scores.
//       Injected via SetFitnessEvaluator(); with none set, evaluation is a no-op sentinel.
//     - Graduation criterion + value (§10.3, OPEN) — SetGraduationCriterion(); default never
//       graduates on fitness, only on the plateau / generation-cap bound (§9).
//   NO reward math is built here (respects §11). Tank sizes / move scales (§10.5), dataset
//   location (§10.4) and where the harness is invoked from (§10.6, e.g. a /experttrain Yuki
//   mode vs a tool) are all still Leith's — this class is invocation-agnostic on purpose.
//
// DESIGN NOTE (coder5): coder3's §3 sketched "reuse a fresh YukiTrainer bound to the subcart."
//   Doing that literally would mean swapping YukiTrainer's scoring (RatchetGuard/EvalProbeLoss,
//   which are wired to the core LM probe + SQLite and sit on the live GPU-training path) for
//   subcart-fitness — i.e. MODIFYING the core trainer, which violates the design's stronger
//   §1/§5 "no core change / untouched-core" invariant. Resolved by reusing the ALGORITHMIC
//   structure (ratchet/pawl select, plateau-stop, difference-move recombination) in this NEW
//   standalone class, leaving YukiTrainer byte-for-byte untouched. Flagged for coder3/Leith.
//
// INTERFACE-LOCK (§6): a subcart is NOT free-topology. It must match the core's embedDim and
//   speak the core's exact vocabulary (identity, not just size), or P3 rejects the emitted cart.
//   "Small" comes ONLY from fewer nLayers / smaller ffDim. Initialize() validates the match.

#pragma once

#include "../Core/Object.h"
#include "../Container/Str.h"
#include "../Container/Vector.h"
#include "../Container/Ptr.h"
#include "../ML/PagedFloatBuffer.h"

#include <functional>

namespace Urho3D
{

class YukiModel;

/// Fitness of a subcart, HIGHER = better. Maps a subcart (its weights already loaded into the
/// work model) to a scalar task-success score. THE §10.2 gate — supplied by the caller; nothing
/// in this class computes reward. Return YukiExpertTrainer::FITNESS_UNSET to signal "no score".
using YukiFitnessFn = std::function<float(YukiModel* subcart)>;

/// Graduation predicate (§10.3 gate): given (bestFitness, generation, plateaued), is the champion
/// good enough to emit? Supplied by the caller. Default (none set) graduates only on the bound.
using YukiGraduationFn = std::function<bool(float bestFitness, int generation, bool plateaued)>;

/// Bounded two-tank hybrid GA over expert subcarts (Federation P5). Living challengers evolve by
/// bare-perturb + difference-move recombination; the immortals tank keeps the fitness-ranked top-K
/// champions, weakest-evicted. The Yuki core is never touched — subcarts only.
class URHO3D_API YukiExpertTrainer : public Object
{
    URHO3D_OBJECT(YukiExpertTrainer, Object);

public:
    /// Sentinel fitness meaning "not evaluated / no scorer wired" (worse than any real score).
    static const float FITNESS_UNSET;

    explicit YukiExpertTrainer(Context* context);
    ~YukiExpertTrainer() override;

    // ─── Seams (Leith's open gates — injected, never computed here) ──────────────────────────
    /// §10.2: supply the subcart fitness. Without it, Evaluate() returns FITNESS_UNSET (no reward math).
    void SetFitnessEvaluator(YukiFitnessFn fn) { fitnessFn_ = std::move(fn); }
    /// §10.3: supply the graduation criterion. Without it, graduation falls to the plateau/gen bound.
    void SetGraduationCriterion(YukiGraduationFn fn) { graduationFn_ = std::move(fn); }
    /// Optional HELD-OUT validator: scored on the champion each generation to measure GENERALISATION (not
    /// training memorisation). When set, the trainer tracks the best-validating champion and Emit() emits
    /// THAT one (early-stopping style), and GetBestValFitness() reports its held-out score. Without it,
    /// selection and emit fall back to raw training fitness. Same YukiFitnessFn shape (higher = better).
    void SetValidator(YukiFitnessFn fn) { validatorFn_ = std::move(fn); }

    // ─── Config (§10.5 numbers are Leith's; these defaults are PROVISIONAL placeholders) ──────
    void SetLivingPop(unsigned n) { livingPop_ = n < 1 ? 1 : n; }
    void SetImmortalsK(unsigned k) { immortalsK_ = k < 1 ? 1 : k; }
    /// Bare-perturb noise scale (std = sigma·RMS(weights)). Seam (b) scale.
    void SetPerturbSigma(float s) { perturbSigma_ = s; }
    /// Difference-move F range: F<1 interpolates between tank members, F>1 extrapolates past them.
    void SetDiffMoveRange(float fMin, float fMax) { diffMoveFMin_ = fMin; diffMoveFMax_ = fMax; }
    /// Hard generation cap (§9 bound) — a subcart that can't learn its field is reported, not run forever.
    void SetMaxGenerations(unsigned g) { maxGenerations_ = g; }
    /// Plateau-stop (mirrors YukiTrainer's EMA/stagnation approach on fitness).
    void SetPlateau(float delta, int passes) { plateauDelta_ = delta; plateauPasses_ = passes; }

    // ─── Lifecycle ────────────────────────────────────────────────────────────────────────────
    /// Validate the interface-lock (§6) of `subcartSeed` against `core`, then seed both tanks from
    /// the seed's weights. `core` is used ONLY as the interface reference — never trained, never
    /// mutated. Returns false if the subcart is interface-incompatible (P3 would reject it).
    bool Initialize(YukiModel* core, YukiModel* subcartSeed);

    /// Run ONE GA generation: recombine/perturb challengers off the tanks, evaluate (via the fitness
    /// seam), rank, promote any challenger that beats the weakest immortal, update the plateau tracker.
    /// Returns Graduated(). A no-op returning false if no fitness seam is wired (nothing to select on).
    bool Step();
    /// Convenience: Step() until Graduated() or the generation cap. Returns Graduated().
    bool Run();

    bool Graduated() const { return graduated_; }
    float GetBestFitness() const { return bestFitness_; }
    /// Best held-out validation score seen (of the best-validating champion). Meaningful only when a
    /// validator is wired; else FITNESS_UNSET.
    float GetBestValFitness() const { return bestValFitness_; }
    /// True once a best-validating champion has been captured (a validator is wired and ran).
    bool HasValidatedChampion() const { return haveBestVal_; }
    int GetGeneration() const { return generation_; }
    bool IsPlateaued() const { return stagnation_ >= plateauPasses_; }
    unsigned GetLivingCount() const { return living_.Size(); }
    unsigned GetImmortalCount() const { return immortals_.Size(); }

    /// Emit the current champion (fittest immortal) as "<path>". Non-destructive to the tanks.
    /// Returns false if there is no champion yet or the save fails.
    bool Emit(const String& path);

private:
    /// One tank member: a subcart weight snapshot + its last-measured fitness (higher better).
    struct Candidate
    {
        PagedFloatBuffer weights;
        float fitness{-3.0e38f};   ///< == FITNESS_UNSET value; unevaluated.
    };

    /// §6 interface-lock check: embedDim equal AND vocab identity (same tokenizer/vocab, not just
    /// size). Depth/width (nLayers/ffDim) may differ — that's where a subcart shrinks.
    bool InterfaceMatches(YukiModel* core, YukiModel* subcart) const;

    /// Seam (b): BARE perturb — add Gaussian noise (std = sigma·RMS) to every weight, page-wise.
    /// NO Adam reset, NO memory reprioritise (those are backprop-specific and irrelevant to a
    /// non-backprop subcart) — the single reason YukiTrainer::Perturb() can't be reused verbatim.
    void BarePerturb(PagedFloatBuffer& w, float sigma);

    /// Recombination: out = a + F·(b − a). F∈(0,1) interpolates a→b; F>1 extrapolates past b; F<0
    /// past a. Generalises YukiTrainer's extrapolation-only difference move to interpolate+extrapolate.
    void DifferenceMove(const PagedFloatBuffer& a, const PagedFloatBuffer& b, float f, PagedFloatBuffer& out) const;

    /// Score a candidate via the fitness seam: load its weights into the work model, call fitnessFn_.
    /// Returns FITNESS_UNSET when no seam is wired (the §10.2 hold).
    float Evaluate(Candidate& c);

    /// Promote each living challenger whose fitness beats the WEAKEST immortal into the tank, displacing
    /// it (merit-based, weakest-evicted — not age-based). No sort: the weakest slot is found by a scan.
    void RankAndPromote();

    /// Feed one generation's best fitness into the smoothed plateau tracker (EMA + stagnation),
    /// mirroring YukiTrainer's UpdatePlateau logic on a maximised score instead of a minimised loss.
    void UpdatePlateau(float genBestFitness);

    /// Small deterministic RNG (LCG) + unit Gaussian (Box–Muller), independent per instance so a
    /// scaffold run is reproducible without touching global RNG state.
    float NextUniform();
    float NextGaussian();

    // Seams (unset by default — the gates).
    YukiFitnessFn fitnessFn_;
    YukiGraduationFn graduationFn_;
    YukiFitnessFn validatorFn_;     ///< Optional held-out validator (generalisation-based emit selection).
    bool warnedNoFitness_{false};   ///< Log the "no fitness seam" warning once, not per candidate.

    // Best-validating champion (early-stopping style): the weights that scored highest on the held-out
    // validator across all generations. Emit() prefers these over the raw-training-fittest immortal.
    Candidate bestVal_;             ///< Snapshot weights + its validation score (in .fitness).
    float bestValFitness_{-3.0e38f};///< == FITNESS_UNSET; best held-out score seen.
    bool haveBestVal_{false};       ///< True once the validator has captured a champion.

    // The two tanks (§2).
    Vector<Candidate> living_;      ///< Challenger subcarts (evolved by bare-perturb / difference-move).
    Vector<Candidate> immortals_;   ///< Fitness-ranked top-K champions, weakest-evicted.

    // Interface reference + work model (never mutates `core_`; `work_` holds the weights being scored).
    WeakPtr<YukiModel> core_;       ///< §6 interface reference only — read for topology/vocab, never trained.
    WeakPtr<YukiModel> work_;       ///< Subcart model; candidate weights are copied in before each Evaluate.

    // Config — provisional; real numbers are §10.5 (Leith).
    unsigned livingPop_{8};
    unsigned immortalsK_{4};
    float perturbSigma_{0.02f};
    float diffMoveFMin_{0.5f};      ///< interpolation floor (<1)
    float diffMoveFMax_{1.5f};      ///< extrapolation ceiling (>1)
    unsigned maxGenerations_{200};

    // State.
    int generation_{0};
    float bestFitness_{-3.0e38f};
    bool graduated_{false};

    // Plateau (mirrors YukiTrainer: EMA of the maximised score + stagnation counter).
    float fitnessEMA_{0.0f};
    bool  emaInit_{false};
    float bestEMA_{-3.0e38f};
    int   stagnation_{0};
    float plateauDelta_{0.001f};
    int   plateauPasses_{30};
    float emaAlpha_{0.1f};

    unsigned rng_{0x9e3779b9u};     ///< LCG state (seeded like YukiTrainer's kick RNG).
    bool haveGauss_{false};         ///< Box–Muller caches one of its two normals.
    float gaussCache_{0.0f};
};

}

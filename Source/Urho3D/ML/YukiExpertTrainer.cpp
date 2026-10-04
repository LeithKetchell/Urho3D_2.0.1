// YukiExpertTrainer — Yuki Federation P5 trial-and-error subcart harness (SCAFFOLD).
// Copyright (c) 2026 Urho3D project. License: MIT.
//
// See YukiExpertTrainer.h for the design/scaffold/gate notes. This file builds the STRUCTURE
// (two-tank GA, bare-perturb, difference-move, plateau bound, emit) and leaves the fitness (§10.2)
// and graduation (§10.3) seams pluggable. No reward math (respects §11). YukiTrainer is untouched.

#include "../Precompiled.h"

#include "../ML/YukiExpertTrainer.h"
#include "../ML/YukiModel.h"
#include "../Core/Context.h"
#include "../IO/Log.h"
#include "../Math/MathDefs.h"

#include <cmath>

#include "../DebugNew.h"

namespace Urho3D
{

// Worse than any plausible real fitness — an unevaluated candidate or a missing scorer.
const float YukiExpertTrainer::FITNESS_UNSET = -3.0e38f;

YukiExpertTrainer::YukiExpertTrainer(Context* context) :
    Object(context)
{
}

YukiExpertTrainer::~YukiExpertTrainer() = default;

bool YukiExpertTrainer::InterfaceMatches(YukiModel* core, YukiModel* subcart) const
{
    if (!core || !subcart)
    {
        URHO3D_LOGERROR("YukiExpertTrainer: interface check needs both a core and a subcart model");
        return false;
    }
    const YukiTopology& ct = core->GetTopology();
    const YukiTopology& st = subcart->GetTopology();

    // §6: embedDim is the hidden-state handoff width — MUST match the core.
    if (st.embedDim != ct.embedDim)
    {
        URHO3D_LOGERROR("YukiExpertTrainer: subcart embedDim " + String(st.embedDim) +
            " != core embedDim " + String(ct.embedDim) + " — P3 would reject the cart");
        return false;
    }
    // §6: vocab IDENTITY, not just size — the signal handoff is in token/logit space, so the subcart
    // must speak the core's EXACT vocabulary. Size first (cheap), then token-by-token identity.
    if (st.vocabSize != ct.vocabSize)
    {
        URHO3D_LOGERROR("YukiExpertTrainer: subcart vocabSize " + String(st.vocabSize) +
            " != core vocabSize " + String(ct.vocabSize));
        return false;
    }
    for (unsigned i = 0; i < ct.vocabSize; ++i)
        if (subcart->GetToken(i) != core->GetToken(i))
        {
            URHO3D_LOGERROR("YukiExpertTrainer: vocab identity mismatch at token " + String(i) +
                " (core '" + core->GetToken(i) + "' vs subcart '" + subcart->GetToken(i) + "')");
            return false;
        }
    return true;
}

bool YukiExpertTrainer::Initialize(YukiModel* core, YukiModel* subcartSeed)
{
    if (!InterfaceMatches(core, subcartSeed))
        return false;

    core_ = core;
    work_ = subcartSeed;
    const YukiTopology& t = subcartSeed->GetTopology();

    immortals_.Clear();
    living_.Clear();

    // Seed the immortals tank with the starting subcart as the first champion.
    Candidate seed;
    seed.weights.Configure(t.embedDim, t.nLayers, t.ffDim, t.vocabSize);
    if (!seed.weights.SameLayout(subcartSeed->GetWeights()))
    {
        URHO3D_LOGERROR("YukiExpertTrainer: candidate page layout does not match the subcart weights "
            "(stride/topology mismatch) — cannot seed the tanks");
        return false;
    }
    seed.weights.CopyFrom(subcartSeed->GetWeights());
    seed.fitness = Evaluate(seed);
    immortals_.Push(seed);
    bestFitness_ = seed.fitness;

    // Fill the living tank with bare-perturbed copies of the seed (the first challengers).
    for (unsigned i = 0; i < livingPop_; ++i)
    {
        Candidate c;
        c.weights.Configure(t.embedDim, t.nLayers, t.ffDim, t.vocabSize);
        c.weights.CopyFrom(subcartSeed->GetWeights());
        BarePerturb(c.weights, perturbSigma_);
        c.fitness = Evaluate(c);
        living_.Push(c);
    }

    generation_ = 0;
    graduated_ = false;
    emaInit_ = false;
    stagnation_ = 0;
    haveBestVal_ = false;
    bestValFitness_ = FITNESS_UNSET;
    URHO3D_LOGINFO("YukiExpertTrainer: initialized — living=" + String(living_.Size()) +
        " immortals=" + String(immortals_.Size()) + " (fitness seam " +
        String(fitnessFn_ ? "wired" : "UNWIRED — §10.2 gate open") + ")");
    return true;
}

void YukiExpertTrainer::BarePerturb(PagedFloatBuffer& w, float sigma)
{
    // Global weight RMS (the sumsq/rms body reused from YukiTrainer::Perturb — WITHOUT its Adam reset
    // and memory reprioritise, which are backprop-only). Page-wise so it stays 64-bit addressable.
    double sumsq = 0.0;
    unsigned long long n = 0;
    for (unsigned p = 0; p < w.PageCount(); ++p)
    {
        const float* d = w.PageData(p);
        const unsigned e = w.PageElems(p);
        for (unsigned i = 0; i < e; ++i)
            sumsq += (double)d[i] * (double)d[i];
        n += e;
    }
    if (n == 0)
        return;
    float rms = (float)sqrt(sumsq / (double)n);
    if (rms <= 0.0f)
        rms = 1e-8f;
    const float std = sigma * rms;

    for (unsigned p = 0; p < w.PageCount(); ++p)
    {
        float* d = w.PageData(p);
        const unsigned e = w.PageElems(p);
        for (unsigned i = 0; i < e; ++i)
            d[i] += std * NextGaussian();
    }
}

void YukiExpertTrainer::DifferenceMove(const PagedFloatBuffer& a, const PagedFloatBuffer& b,
                                       float f, PagedFloatBuffer& out) const
{
    // out = a + f·(b − a). f∈(0,1) interpolates a→b; f>1 extrapolates past b; f<0 past a.
    if (!out.SameLayout(a) || !b.SameLayout(a))
        return;   // layouts must match; caller configures out to the subcart topology.
    for (unsigned p = 0; p < a.PageCount(); ++p)
    {
        const float* ad = a.PageData(p);
        const float* bd = b.PageData(p);
        float* od = out.PageData(p);
        const unsigned e = a.PageElems(p);
        for (unsigned i = 0; i < e; ++i)
            od[i] = ad[i] + f * (bd[i] - ad[i]);
    }
}

float YukiExpertTrainer::Evaluate(Candidate& c)
{
    if (!fitnessFn_)
    {
        if (!warnedNoFitness_)
        {
            URHO3D_LOGWARNING("YukiExpertTrainer: no fitness seam wired (§10.2 gate open) — "
                "candidates cannot be scored; evolution is inert until SetFitnessEvaluator() is called");
            warnedNoFitness_ = true;
        }
        return FITNESS_UNSET;
    }
    YukiModel* work = work_.Get();
    if (!work || !work->GetWeights().SameLayout(c.weights))
        return FITNESS_UNSET;
    // Load this candidate's weights into the shared work model, then let the seam score it.
    work->GetWeights().CopyFrom(c.weights);
    return fitnessFn_(work);
}

void YukiExpertTrainer::RankAndPromote()
{
    // Weakest-evicted (merit-based, NOT age-based): a living challenger that beats the weakest immortal
    // displaces it. No full sort needed — find the weakest slot and overwrite it in place.
    for (Vector<Candidate>::ConstIterator it = living_.Begin(); it != living_.End(); ++it)
    {
        const Candidate& ch = *it;
        if (immortals_.Size() < immortalsK_)
        {
            immortals_.Push(ch);   // room to spare — admit unconditionally
            continue;
        }
        unsigned weakest = 0;
        for (unsigned i = 1; i < immortals_.Size(); ++i)
            if (immortals_[i].fitness < immortals_[weakest].fitness)
                weakest = i;
        if (ch.fitness > immortals_[weakest].fitness)
        {
            immortals_[weakest].weights.CopyFrom(ch.weights);   // reuse the evicted slot's allocation
            immortals_[weakest].fitness = ch.fitness;
        }
    }
}

void YukiExpertTrainer::UpdatePlateau(float genBestFitness)
{
    // Mirror YukiTrainer's smoothed-loss/stagnation approach, but on a MAXIMISED score: progress means
    // the EMA rose by at least plateauDelta_; otherwise stagnation accrues toward the plateau stop.
    if (!emaInit_)
    {
        fitnessEMA_ = genBestFitness;
        bestEMA_ = genBestFitness;
        emaInit_ = true;
        stagnation_ = 0;
        return;
    }
    fitnessEMA_ = emaAlpha_ * genBestFitness + (1.0f - emaAlpha_) * fitnessEMA_;
    if (fitnessEMA_ > bestEMA_ + plateauDelta_)
    {
        bestEMA_ = fitnessEMA_;
        stagnation_ = 0;
    }
    else
        ++stagnation_;
}

bool YukiExpertTrainer::Step()
{
    if (!fitnessFn_)
    {
        if (!warnedNoFitness_)
        {
            URHO3D_LOGWARNING("YukiExpertTrainer::Step — no fitness seam wired (§10.2 gate open); "
                "evolution is inert until SetFitnessEvaluator() is called");
            warnedNoFitness_ = true;
        }
        return false;   // nothing to select on until the §10.2 seam is wired
    }
    YukiModel* wm = work_.Get();
    if (!wm || immortals_.Empty())
        return false;   // not initialized (or the work model expired)

    ++generation_;

    const YukiTopology& t = wm->GetTopology();

    // WATCH-ITEM (coder3 design review, 2026-07-06 — the FIRST knob to watch once the §10.2 fitness
    // seam lands): every challenger below is RE-BRED from the immortals each generation (parent `a` is
    // always an immortal, `ch` is overwritten), i.e. pure elitism. Clean, but it risks UNDER-EXPLORING —
    // every challenger is a perturbed convex-ish combo of current champions, so the population can collapse
    // into the elite basin and converge fast+shallow. If real runs show that, loosen `living_` toward a
    // PERSISTENT lineage (challengers hill-climb their OWN line, only sometimes recombining with an
    // immortal) to keep diversity. Fine for the scaffold; not a blocker — logged so it isn't forgotten.
    // Breed + perturb each challenger off the tanks, then score it.
    for (Vector<Candidate>::Iterator it = living_.Begin(); it != living_.End(); ++it)
    {
        Candidate& ch = *it;
        if (!ch.weights.IsConfigured())
            ch.weights.Configure(t.embedDim, t.nLayers, t.ffDim, t.vocabSize);

        // Recombination ("a little GA in between"): difference-move between two tank members
        // (immortal↔immortal, or immortal↔living), F spanning interpolation and extrapolation.
        const Candidate& a = immortals_[(unsigned)(NextUniform() * immortals_.Size()) % immortals_.Size()];
        const Candidate* b = nullptr;
        if (immortals_.Size() >= 2)
            b = &immortals_[(unsigned)(NextUniform() * immortals_.Size()) % immortals_.Size()];
        else if (!living_.Empty())
            b = &living_[(unsigned)(NextUniform() * living_.Size()) % living_.Size()];

        if (b && b != &a && b->weights.SameLayout(a.weights))
        {
            const float f = diffMoveFMin_ + NextUniform() * (diffMoveFMax_ - diffMoveFMin_);
            DifferenceMove(a.weights, b->weights, f, ch.weights);
        }
        else
            ch.weights.CopyFrom(a.weights);   // degenerate: single champion — just clone it

        BarePerturb(ch.weights, perturbSigma_);
        ch.fitness = Evaluate(ch);
    }

    RankAndPromote();

    // Champion = fittest immortal.
    float genBest = immortals_[0].fitness;
    for (unsigned i = 1; i < immortals_.Size(); ++i)
        if (immortals_[i].fitness > genBest)
            genBest = immortals_[i].fitness;
    if (genBest > bestFitness_)
        bestFitness_ = genBest;

    UpdatePlateau(genBest);
    const bool plateaued = IsPlateaued();

    // Held-out validation (if wired): score the current champion on the untouched val split and KEEP the
    // best-validating weights for emit. This makes the emitted expert GENERALISE rather than memorise the
    // training batch — early stopping via best-checkpoint selection (the run still bounds on train plateau/cap).
    if (validatorFn_)
    {
        YukiModel* work = work_.Get();
        unsigned fittest = 0;
        for (unsigned i = 1; i < immortals_.Size(); ++i)
            if (immortals_[i].fitness > immortals_[fittest].fitness)
                fittest = i;
        if (work && work->GetWeights().SameLayout(immortals_[fittest].weights))
        {
            work->GetWeights().CopyFrom(immortals_[fittest].weights);
            const float valScore = validatorFn_(work);
            if (!haveBestVal_ || valScore > bestValFitness_)
            {
                if (!bestVal_.weights.IsConfigured())
                {
                    const YukiTopology& t = work->GetTopology();
                    bestVal_.weights.Configure(t.embedDim, t.nLayers, t.ffDim, t.vocabSize);
                }
                if (bestVal_.weights.SameLayout(immortals_[fittest].weights))
                {
                    bestVal_.weights.CopyFrom(immortals_[fittest].weights);
                    bestVal_.fitness = valScore;
                    bestValFitness_ = valScore;
                    haveBestVal_ = true;
                }
            }
        }
    }

    // Graduation: the §10.3 seam if wired, else the §9 bound (plateau OR generation cap).
    if (graduationFn_ && graduationFn_(bestFitness_, generation_, plateaued))
        graduated_ = true;
    else if (plateaued || generation_ >= (int)maxGenerations_)
        graduated_ = true;

    return graduated_;
}

bool YukiExpertTrainer::Run()
{
    if (!fitnessFn_)
    {
        URHO3D_LOGWARNING("YukiExpertTrainer::Run — no fitness seam wired (§10.2); nothing to run");
        return false;
    }
    while (!graduated_ && generation_ < (int)maxGenerations_)
        Step();
    return graduated_;
}

bool YukiExpertTrainer::Emit(const String& path)
{
    if (immortals_.Empty())
    {
        URHO3D_LOGERROR("YukiExpertTrainer::Emit — no champion to emit (not initialized / no immortals)");
        return false;
    }
    YukiModel* wm = work_.Get();
    if (!wm)
    {
        URHO3D_LOGERROR("YukiExpertTrainer::Emit — work model expired");
        return false;
    }
    // Prefer the best-VALIDATING champion (generalises) over the raw-training-fittest immortal when a
    // validator captured one; else fall back to the fittest immortal (no validator wired).
    const PagedFloatBuffer* src = nullptr;
    float reportFit = 0.0f;
    String which;
    if (haveBestVal_ && bestVal_.weights.IsConfigured())
    {
        src = &bestVal_.weights;
        reportFit = bestValFitness_;
        which = "best-val";
    }
    else
    {
        unsigned fittest = 0;
        for (unsigned i = 1; i < immortals_.Size(); ++i)
            if (immortals_[i].fitness > immortals_[fittest].fitness)
                fittest = i;
        src = &immortals_[fittest].weights;
        reportFit = immortals_[fittest].fitness;
        which = "train-fittest";
    }

    if (!wm->GetWeights().SameLayout(*src))
    {
        URHO3D_LOGERROR("YukiExpertTrainer::Emit — champion layout mismatch with the work model");
        return false;
    }
    wm->GetWeights().CopyFrom(*src);
    const bool ok = wm->SaveAs(path);
    URHO3D_LOGINFO("YukiExpertTrainer: emit " + which + " champion (fitness " + String(reportFit) +
        ") -> " + path + (ok ? " [ok]" : " [FAILED]"));
    return ok;
}

float YukiExpertTrainer::NextUniform()
{
    // Numerical Recipes LCG; take the high bits for a [0,1) float.
    rng_ = rng_ * 1664525u + 1013904223u;
    return (float)(rng_ >> 8) * (1.0f / 16777216.0f);
}

float YukiExpertTrainer::NextGaussian()
{
    if (haveGauss_)
    {
        haveGauss_ = false;
        return gaussCache_;
    }
    float u1 = NextUniform();
    if (u1 < 1e-7f)
        u1 = 1e-7f;   // guard log(0)
    const float u2 = NextUniform();
    const float r = sqrtf(-2.0f * logf(u1));
    const float theta = 2.0f * (float)M_PI * u2;
    gaussCache_ = r * sinf(theta);
    haveGauss_ = true;
    return r * cosf(theta);
}

}

// YukiExpertDataset — Yuki Federation P5: per-expert training dataset + task-success fitness.
// Copyright (c) 2026 Urho3D project. License: MIT.
//
// Closes the P5 fitness gate (§10.2, Leith 2026-07-08: "offline dataset task-success"). A dataset is a set
// of (context tokens -> target token) samples HARVESTED once from the core's corpus and tokenised through
// the core's EXACT vocabulary (interface-lock §6). Fitness = how well a subcart predicts those targets —
// next-token accuracy in [0,1], higher = better. This is task-success on the harvested task, NOT invented
// reward math: the task IS whatever corpus was harvested. Offline by construction (§8) — scoring never
// runs the live core, so P5 training has zero live-core lock coupling.
//
// PRODUCER role (P5): YukiExpertTrainer's fitness seam calls Score(); a harvest step calls BuildFromText()
// + Save(); training loads via Load(). Datasets live in bin/Data/GameDB/experts/ (§10.4, Leith).

#pragma once

#include "../Core/Object.h"
#include "../Container/Str.h"
#include "../Container/Vector.h"

namespace Urho3D
{

class YukiModel;

/// A harvested (context -> next-token) dataset for training + scoring one expert subcart.
class URHO3D_API YukiExpertDataset : public Object
{
    URHO3D_OBJECT(YukiExpertDataset, Object);

public:
    /// One training sample: a context token window and the token that should follow it.
    struct Sample
    {
        Vector<unsigned> context_;
        unsigned target_;
    };

    explicit YukiExpertDataset(Context* context);
    ~YukiExpertDataset() override;

    /// Number of samples.
    unsigned Size() const { return samples_.Size(); }
    /// Drop all samples.
    void Clear() { samples_.Clear(); }
    const Vector<Sample>& Samples() const { return samples_; }

    /// Harvest samples from `text`, tokenised through `core`'s EXACT vocab (interface-lock — the subcart
    /// shares that vocab, so its predictions are directly comparable). Slides a window of up to `maxContext`
    /// tokens; each position's context predicts the next token (the target). Appends up to `maxSamples` new
    /// samples (0 = unlimited). Returns the number added. Unknown words are dropped by the core tokenizer.
    unsigned BuildFromText(YukiModel* core, const String& text, unsigned maxContext, unsigned maxSamples);

    /// Persist to `path` (engine File I/O, compact binary). Round-trips with Load(). False on write failure.
    bool Save(const String& path) const;
    /// Load from `path`, replacing current samples. False on read failure / bad magic.
    bool Load(const String& path);

    /// Reserve the LAST `frac` fraction of samples as a held-out VALIDATION split (the rest is TRAIN). The
    /// GA selects on TRAIN (Score); the champion's generalisation is measured on the untouched VAL split
    /// (ScoreValidation), so we emit the champion that validates best rather than the one that memorised the
    /// training batch. `frac` clamps to [0, 0.9]; 0 (default) = no split (all TRAIN, VAL falls back to all).
    void SetValidationFraction(float frac);
    /// Number of TRAIN samples (== Size() when no split is set).
    unsigned TrainSize() const { return (trainCount_ > 0 && trainCount_ <= samples_.Size()) ? trainCount_ : samples_.Size(); }
    /// Number of held-out VALIDATION samples (0 when no split is set).
    unsigned ValSize() const { return (trainCount_ > 0 && trainCount_ < samples_.Size()) ? (samples_.Size() - trainCount_) : 0; }

    /// TASK-SUCCESS FITNESS (§10.2): mean softmax probability the `subcart` assigns to the CORRECT next
    /// token, averaged over the (mini)batch — in (0,1], higher = better. Dense on purpose: top-1 accuracy is
    /// a flat 0 for a random-init subcart (no GA gradient), whereas the target's probability moves smoothly
    /// with the weights. Scores the TRAIN split only. Runs `subcart` on its OWN inference context (offline;
    /// never touches the core). Returns 0 for an empty dataset or a null/unloaded subcart.
    ///
    /// MINIBATCH: `sampleCap` > 0 scores a pseudo-random subset of that many TRAIN samples (cheap stochastic
    /// estimate for the GA inner loop); `seed` selects the subset. `sampleCap == 0` scores the whole TRAIN split.
    float Score(YukiModel* subcart, unsigned sampleCap = 0, unsigned seed = 0) const;

    /// Held-out generalisation score: the same mean-target-probability metric over the VALIDATION split
    /// (full, deterministic). Falls back to the whole dataset if no split is set. This is what the champion
    /// is judged by for emit — a subcart that only memorised the train batch scores worse here.
    float ScoreValidation(YukiModel* subcart) const;

private:
    /// Score samples in the index range [lo, hi): mean target probability. If `cap` > 0 and < the range,
    /// samples `cap` indices from the range pseudo-randomly (seeded by `seed`); else scores the whole range.
    float ScoreRange(YukiModel* subcart, unsigned lo, unsigned hi, unsigned cap, unsigned seed) const;

    Vector<Sample> samples_;
    /// TRAIN/VAL boundary: samples [0, trainCount_) are TRAIN, [trainCount_, Size()) are VAL. 0 = no split.
    unsigned trainCount_{0};
};

}

// YukiExpertDataset — Yuki Federation P5: per-expert dataset + task-success fitness. See header.
// Copyright (c) 2026 Urho3D project. License: MIT.

#include "../Precompiled.h"

#include "../ML/YukiExpertDataset.h"
#include "../ML/YukiModel.h"
#include "../ML/YukiInference.h"
#include "../Core/Context.h"
#include "../Container/Ptr.h"
#include "../IO/File.h"
#include "../IO/Log.h"

#include <cmath>

#include "../DebugNew.h"

namespace Urho3D
{

static const unsigned YUKI_DATASET_MAGIC = 0x53444559;   // 'YEDS' little-endian
static const unsigned YUKI_DATASET_VERSION = 1;

YukiExpertDataset::YukiExpertDataset(Context* context) :
    Object(context)
{
}

YukiExpertDataset::~YukiExpertDataset() = default;

unsigned YukiExpertDataset::BuildFromText(YukiModel* core, const String& text, unsigned maxContext,
    unsigned maxSamples)
{
    if (!core || !core->IsLoaded() || text.Empty() || maxContext == 0)
    {
        URHO3D_LOGERROR("YukiExpertDataset::BuildFromText: needs a loaded core, text, and maxContext>0");
        return 0;
    }

    // Tokenise through the core's EXACT vocab (interface-lock §6): the same canonical tokenizer the core
    // trains/infers with, so the subcart (which shares this vocab) predicts directly comparable indices.
    const unsigned vocabSize = core->GetTopology().vocabSize;
    Vector<String> words = YukiModel::Tokenize(text);
    Vector<unsigned> tokens;
    tokens.Reserve(words.Size());
    for (const String& w : words)
    {
        unsigned idx = core->GetTokenIndex(w);
        if (idx < vocabSize)          // drop unknown words (mirrors YukiInference::Tokenize)
            tokens.Push(idx);
    }
    if (tokens.Size() < 2)
    {
        URHO3D_LOGWARNING("YukiExpertDataset::BuildFromText: fewer than 2 known tokens — no samples");
        return 0;
    }

    // Sliding window: each position's preceding (up to maxContext) tokens predict the next token.
    unsigned added = 0;
    for (unsigned i = 1; i < tokens.Size(); ++i)
    {
        if (maxSamples != 0 && added >= maxSamples)
            break;
        Sample s;
        const unsigned start = (i > maxContext) ? (i - maxContext) : 0u;
        s.context_.Reserve(i - start);
        for (unsigned j = start; j < i; ++j)
            s.context_.Push(tokens[j]);
        s.target_ = tokens[i];
        samples_.Push(s);
        ++added;
    }

    URHO3D_LOGINFO("YukiExpertDataset::BuildFromText: added " + String(added) + " sample(s) (total " +
        String(samples_.Size()) + ")");
    return added;
}

bool YukiExpertDataset::Save(const String& path) const
{
    if (path.Empty())
        return false;

    File file(context_);
    if (!file.Open(path, FILE_WRITE))
    {
        URHO3D_LOGERROR("YukiExpertDataset::Save: cannot open " + path + " for writing");
        return false;
    }
    file.WriteU32(YUKI_DATASET_MAGIC);
    file.WriteU32(YUKI_DATASET_VERSION);
    file.WriteU32(samples_.Size());
    for (const Sample& s : samples_)
    {
        file.WriteU32(s.context_.Size());
        for (unsigned t : s.context_)
            file.WriteU32(t);
        file.WriteU32(s.target_);
    }
    file.Close();
    URHO3D_LOGINFO("YukiExpertDataset::Save: wrote " + String(samples_.Size()) + " sample(s) to " + path);
    return true;
}

bool YukiExpertDataset::Load(const String& path)
{
    if (path.Empty())
        return false;

    File file(context_);
    if (!file.Open(path, FILE_READ))
    {
        URHO3D_LOGERROR("YukiExpertDataset::Load: cannot open " + path);
        return false;
    }
    if (file.ReadU32() != YUKI_DATASET_MAGIC)
    {
        URHO3D_LOGERROR("YukiExpertDataset::Load: bad magic in " + path);
        return false;
    }
    file.ReadU32();   // version (only v1 exists)
    const unsigned count = file.ReadU32();

    samples_.Clear();
    samples_.Reserve(count);
    for (unsigned i = 0; i < count && !file.IsEof(); ++i)
    {
        Sample s;
        const unsigned len = file.ReadU32();
        s.context_.Reserve(len);
        for (unsigned j = 0; j < len; ++j)
            s.context_.Push(file.ReadU32());
        s.target_ = file.ReadU32();
        samples_.Push(s);
    }
    file.Close();
    URHO3D_LOGINFO("YukiExpertDataset::Load: read " + String(samples_.Size()) + " sample(s) from " + path);
    return true;
}

void YukiExpertDataset::SetValidationFraction(float frac)
{
    if (frac < 0.0f) frac = 0.0f;
    if (frac > 0.9f) frac = 0.9f;
    trainCount_ = (unsigned)((double)samples_.Size() * (1.0 - (double)frac));
    URHO3D_LOGINFO("YukiExpertDataset: split — train " + String(TrainSize()) + " / val " + String(ValSize()) +
        " (val frac " + String(frac, 2) + ")");
}

float YukiExpertDataset::ScoreRange(YukiModel* subcart, unsigned lo, unsigned hi, unsigned cap, unsigned seed) const
{
    if (hi <= lo || !subcart || !subcart->IsLoaded())
        return 0.0f;

    // Offline scoring on the subcart's OWN inference context — never touches the core.
    SharedPtr<YukiInference> inf(new YukiInference(context_));
    inf->SetModel(subcart);

    const unsigned range = hi - lo;
    const bool full = (cap == 0 || cap >= range);
    const unsigned count = full ? range : cap;

    // DENSE fitness: mean softmax probability the subcart assigns to the CORRECT next token, averaged over
    // the (mini)batch. In (0,1], higher = better. Chosen over top-1 accuracy because a random-init tiny
    // subcart top-1-matches almost never, so accuracy is a flat 0 with no gradient for the GA to climb; the
    // target's probability moves smoothly with the weights, so evolution has a signal from the first gen.
    const unsigned vocab = subcart->GetTopology().vocabSize;
    double sumProb = 0.0;
    unsigned scored = 0;
    unsigned rng = seed * 2654435761u + 1u;   // hash the seed so consecutive seeds give distinct subsets
    for (unsigned k = 0; k < count; ++k)
    {
        unsigned idx;
        if (full)
            idx = lo + k;
        else
        {
            rng = rng * 1103515245u + 12345u;   // LCG; sample with replacement within [lo, hi)
            idx = lo + ((rng >> 8) % range);
        }
        const Sample& s = samples_[idx];
        if (s.context_.Empty() || s.target_ >= vocab)
            continue;

        inf->Predict(s.context_);                       // runs the forward; fills the logits
        const Vector<float>& logits = inf->GetLogits();
        if (logits.Size() < vocab)
            continue;

        // Numerically-stable softmax probability of the target token: exp(l_t - max) / sum_j exp(l_j - max).
        float maxL = logits[0];
        for (unsigned i = 1; i < vocab; ++i)
            if (logits[i] > maxL)
                maxL = logits[i];
        double se = 0.0;
        for (unsigned i = 0; i < vocab; ++i)
            se += exp((double)logits[i] - (double)maxL);
        if (se > 0.0)
        {
            sumProb += exp((double)logits[s.target_] - (double)maxL) / se;
            ++scored;
        }
    }
    return scored ? (float)(sumProb / (double)scored) : 0.0f;
}

float YukiExpertDataset::Score(YukiModel* subcart, unsigned sampleCap, unsigned seed) const
{
    if (samples_.Empty())
        return 0.0f;
    return ScoreRange(subcart, 0, TrainSize(), sampleCap, seed);   // TRAIN split only
}

float YukiExpertDataset::ScoreValidation(YukiModel* subcart) const
{
    if (samples_.Empty())
        return 0.0f;
    const unsigned lo = ValSize() > 0 ? TrainSize() : 0;           // held-out region, or whole set if no split
    return ScoreRange(subcart, lo, samples_.Size(), 0, 0);         // full, deterministic
}

}

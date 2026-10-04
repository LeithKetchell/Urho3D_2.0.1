//
// Copyright (c) 2008-2024 the Urho3D project.
// License: MIT
//
// Yuki expert federation — P3 per-hop primitive + P4 call/return orchestration (bounded acyclic tree).
//

#include "../Precompiled.h"

#include "../ML/YukiDispatch.h"
#include "../ML/YukiCartRegistry.h"
#include "../ML/YukiInference.h"
#include "../ML/YukiModel.h"
#include "../IO/Log.h"

#include <cmath>
#include <cstring>

#include "../DebugNew.h"

namespace Urho3D
{

YukiDispatch::YukiDispatch(Context* context, YukiCartRegistry* registry) :
    Object(context),
    registry_(registry)
{
}

YukiDispatch::~YukiDispatch() = default;

bool YukiDispatch::VocabMatches(YukiModel* core, YukiModel* expert)
{
    if (!core || !expert)
        return false;

    const YukiTopology& ct = core->GetTopology();
    const YukiTopology& et = expert->GetTopology();

    // Same size is necessary but NOT sufficient — the token<->index mapping must be identical, or logit
    // index i means a different token in each model and composing them in vocab space is meaningless.
    if (ct.vocabSize != et.vocabSize)
        return false;

    for (unsigned i = 0; i < ct.vocabSize; ++i)
    {
        if (core->GetToken(i) != expert->GetToken(i))
            return false;
    }
    return true;
}

bool YukiDispatch::AllFinite(const float* v, unsigned count)
{
    // Bit-pattern test: a 32-bit float is NaN or +/-inf iff its exponent field is all ones. This is robust
    // under -ffast-math (which can compile std::isfinite to a constant true). Matches the trainer's guard.
    for (unsigned i = 0; i < count; ++i)
    {
        uint32_t bits;
        memcpy(&bits, &v[i], sizeof(bits));
        if ((bits & 0x7F800000u) == 0x7F800000u)
            return false;
    }
    return true;
}

bool YukiDispatch::PinExpert(const String& id, const String& path, YukiModel* core)
{
    if (!registry_)
    {
        URHO3D_LOGERROR("YukiDispatch::PinExpert: no registry");
        return false;
    }
    if (id.Empty() || path.Empty())
    {
        URHO3D_LOGERROR("YukiDispatch::PinExpert: empty id or path");
        return false;
    }
    if (!core || !core->IsLoaded())
    {
        URHO3D_LOGERROR("YukiDispatch::PinExpert: core model not loaded");
        return false;
    }

    // Load + pin (idempotent). Held for this object's whole lifetime — the lifetime guarantee for the hot
    // path, so no per-hop registry lock and no mid-pass load/unload.
    SharedPtr<YukiModel> expert = registry_->Load(id, path);
    if (!expert || !expert->IsLoaded())
    {
        URHO3D_LOGERROR("YukiDispatch::PinExpert: failed to load expert '" + id + "' from " + path);
        return false;
    }

    // Vocab IDENTITY vs the core (tap-A requirement). On mismatch, evict from the registry and pin nothing.
    if (!VocabMatches(core, expert))
    {
        URHO3D_LOGERROR("YukiDispatch::PinExpert: expert '" + id +
            "' vocab does not match the core vocab (identity required for the V-logit tap) — not pinned");
        registry_->Unload(id);
        return false;
    }

    // Dedicated inference context, buffers sized to the EXPERT topology, wired back to THIS dispatch so the
    // expert's own output layer can trigger sub-experts (bounded by depth/cycle/budget). This is the P4
    // promotion of P3's "no dispatch on the expert context" — recursion is now BOUNDED, not forbidden.
    SharedPtr<YukiInference> ctx = MakeShared<YukiInference>(context_);
    ctx->SetModel(expert);
    ctx->SetDispatch(this);

    pinnedExperts_[id] = expert;
    expertContexts_[id] = ctx;
    pinnedIds_.Insert(id);            // explicitly pinned — resident and EXEMPT from P6 LRU eviction
    lruTick_[id] = ++accessTick_;
    core_ = core;                     // remember the interface reference for P6 load-on-demand compat checks

    // Preserve P3's single-expert one-liner: a bare pin auto-routes the default trigger channel to this
    // expert IF that channel is not already routed (explicit /fed route overrides).
    bool channelRouted = false;
    for (const YukiRoute& r : routes_)
    {
        if (r.channel_ == triggerChannel_) { channelRouted = true; break; }
    }
    if (!channelRouted)
        routes_.Push(YukiRoute{triggerChannel_, id});

    URHO3D_LOGINFO("YukiDispatch::PinExpert: pinned expert '" + id + "' (vocab identity OK, " +
        String(expert->GetTopology().vocabSize) + " tokens); pinned=" + String(pinnedExperts_.Size()) +
        ", routes=" + String(routes_.Size()));
    return true;
}

bool YukiDispatch::SetRoute(unsigned channel, const String& expertId)
{
    if (expertId.Empty())
        return false;
    for (YukiRoute& r : routes_)
    {
        if (r.channel_ == channel) { r.expertId_ = expertId; return true; }
    }
    routes_.Push(YukiRoute{channel, expertId});
    return true;
}

void YukiDispatch::ClearRoute(unsigned channel)
{
    for (unsigned i = 0; i < routes_.Size(); ++i)
    {
        if (routes_[i].channel_ == channel) { routes_.Erase(i); return; }
    }
}

YukiInference* YukiDispatch::EnsureResident(const String& id)
{
    // Resident already? Touch the LRU tick and return it.
    HashMap<String, SharedPtr<YukiInference> >::Iterator it = expertContexts_.Find(id);
    if (it != expertContexts_.End() && it->second_)
    {
        lruTick_[id] = ++accessTick_;
        return it->second_;
    }

    // P6 load-on-demand: resolve the id via the manifest catalog and make it resident. LoadFromManifest is
    // mutex-safe and allocates ONLY this expert's own CPU memory — it never touches the core's weights, so
    // it cannot race the core trainer (see YUKI_FED_P6_DESIGN §3).
    if (!registry_)
        return nullptr;
    SharedPtr<YukiModel> model = registry_->LoadFromManifest(id);
    if (!model || !model->IsLoaded())
        return nullptr;   // unknown id / load failure — caller degrades gracefully

    // Interface-lock (§6): the same vocab-identity gate an eager pin applies, done here for a lazy load.
    YukiModel* core = core_.Get();
    if (core && !VocabMatches(core, model))
    {
        URHO3D_LOGERROR("YukiDispatch: lazy-loaded expert '" + id +
            "' vocab does not match the core — refusing (unloaded)");
        registry_->Unload(id);
        return nullptr;
    }

    SharedPtr<YukiInference> ctx = MakeShared<YukiInference>(context_);
    ctx->SetModel(model);
    ctx->SetDispatch(this);                 // sub-triggers (P4), bounded by the guards
    pinnedExperts_[id] = model;             // owning copy keeps it alive until evicted
    expertContexts_[id] = ctx;
    lruTick_[id] = ++accessTick_;
    // NOTE: not added to pinnedIds_ — a lazily loaded expert IS evictable.
    EvictIfOverCap();

    // EvictIfOverCap never evicts the id we just loaded (freshest LRU), so this lookup is always valid.
    return expertContexts_[id];
}

void YukiDispatch::EvictIfOverCap()
{
    if (residentCap_ == 0)
        return;   // unlimited

    while (expertContexts_.Size() > residentCap_)
    {
        // Least-recently-used resident that is neither explicitly pinned nor CURRENTLY on the call stack —
        // evicting a live frame's context (a P4 parent executing above us) would be a use-after-free.
        String victim;
        unsigned oldest = 0xFFFFFFFFu;
        for (HashMap<String, SharedPtr<YukiInference> >::ConstIterator e = expertContexts_.Begin();
             e != expertContexts_.End(); ++e)
        {
            const String& id = e->first_;
            if (pinnedIds_.Contains(id) || onStack_.Contains(id))
                continue;
            HashMap<String, unsigned>::ConstIterator t = lruTick_.Find(id);
            const unsigned tick = (t != lruTick_.End()) ? t->second_ : 0u;
            if (tick < oldest) { oldest = tick; victim = id; }
        }
        if (victim.Empty())
            break;   // nothing evictable (all pinned / on-stack) — run temporarily over cap (correctness first)

        expertContexts_.Erase(victim);
        pinnedExperts_.Erase(victim);
        lruTick_.Erase(victim);
        if (registry_)
            registry_->Unload(victim);   // ref-counted: any copy still held elsewhere keeps the model alive
    }
}

bool YukiDispatch::MaybeRoute(const Vector<unsigned>& contextTokens, float* logits, unsigned vocabSize)
{
    // Zero-cost no-op when the federation is off (no routes). Cardinal rule: caller keeps its logits. Note
    // P6: routes may resolve to experts that are cataloged-but-not-yet-resident, so we do NOT require any
    // expert to be pre-pinned here — EnsureResident() loads on demand below.
    if (routes_.Empty() || !logits || vocabSize == 0)
        return false;

    // A top-level (core) entry begins a new OUTPUT STEP: reset the per-token call budget. Nested entries
    // (an expert's own output triggering) keep the running budget/stack — that is the whole point of P4.
    const bool topLevel = callStack_.Empty();
    if (topLevel)
        callsRemaining_ = callBudget_;

    bool composedAny = false;

    // Deterministic scan of the routing table (insertion order).
    for (unsigned ri = 0; ri < routes_.Size(); ++ri)
    {
        const unsigned channel = routes_[ri].channel_;
        const String id = routes_[ri].expertId_;   // copy: routes_ is not mutated during dispatch
        if (channel >= vocabSize)
            continue;

        // --- Trigger read (tap A). tanh-shaped signal in [-1,+1]. ---
        const float signal = tanhf(logits[channel]);
        if (!(forceMode_ || signal > threshold_))
            continue;

        // --- Guards (each trip degrades gracefully — the caller keeps its own logits). ---
        if (callsRemaining_ <= 0)
            break;                                          // budget exhausted for this output step
        if ((int)callStack_.Size() >= depthCap_)
            break;                                          // depth cap reached — refuse deeper calls
        if (onStack_.Contains(id))
            continue;                                       // cycle (id already on stack) — refuse this edge

        // P6: resolve the expert, LOADING IT ON DEMAND from the manifest if not resident (id is not yet on
        // the stack here, so a load-triggered eviction can never drop an active frame). Null = unknown/failed.
        YukiInference* ctx = EnsureResident(id);
        if (!ctx)
            continue;                                       // not resident and not loadable — skip

        // --- Dispatch this hop. Push frame, run the callee to completion (it may push sub-frames), pop. ---
        --callsRemaining_;
        callStack_.Push(id);
        onStack_.Insert(id);

        ctx->Predict(contextTokens);                        // expert forward + its own nested MaybeRoute
        const Vector<float>& expertLogits = ctx->GetLogits();

        // Per-hop finite gate + whole-subtree discard: commit the callee's output to the caller ONLY if the
        // whole subtree returned clean (right size AND all finite). Otherwise no-op — nothing partial leaks.
        const bool ok = (expertLogits.Size() >= vocabSize) && AllFinite(expertLogits.Buffer(), vocabSize);

        onStack_.Erase(id);
        callStack_.Pop();

        if (!ok)
            continue;                                       // failed/poisoned subtree — degrade gracefully

        // --- Compose (REPLACE): the callee's logits become the caller's composed output. ---
        switch (composePolicy_)
        {
        case YUKI_COMPOSE_REPLACE:
        default:
            for (unsigned i = 0; i < vocabSize; ++i)
                logits[i] = expertLogits[i];
            break;
        }
        composedAny = true;
        ++fireCount_;
    }

    // Symmetric push/pop leaves the stack empty at top-level exit; nothing else to reset here.
    return composedAny;
}

}

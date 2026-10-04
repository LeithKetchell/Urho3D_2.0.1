//
// Copyright (c) 2008-2024 the Urho3D project.
// License: MIT
//
// Yuki expert federation — P3: output-layer trigger + single-expert dispatch (the per-hop primitive).
//                           P4: call/return orchestration over that primitive — a bounded, acyclic call
//                               TREE (core -> A -> {B, C -> D}) that unwinds within one output step,
//                               kept finite + acyclic by depth cap + on-stack cycle guard + call budget,
//                               and degrades gracefully to "as if the trigger never fired" on ANY failure.
// Design: Claude/YUKI_FED_P3_DESIGN.md, Claude/YUKI_FED_P4_DESIGN.md.
//

#pragma once

#include "../Core/Object.h"
#include "../Container/Str.h"
#include "../Container/Vector.h"
#include "../Container/HashMap.h"
#include "../Container/HashSet.h"
#include "../Container/Ptr.h"

namespace Urho3D
{

class YukiCartRegistry;
class YukiModel;
class YukiInference;

/// Compose policy for a returned (expert) step. First cut ships REPLACE only — deterministic and trivial
/// to validate; add/gate/learned-blend are deferred (they tie into P5's learned routing).
enum YukiComposePolicy
{
    YUKI_COMPOSE_REPLACE = 0,   ///< Overwrite the caller's output signals with the callee's.
};

/// A routing edge: an output-neuron trigger CHANNEL -> the expert id fired when that channel crosses θ.
/// The SAME table is consulted at every hop (the core and every expert have trigger channels alike, P3 §2),
/// so an expert firing channel c dispatches to routes with channel c just as the core does. Stored as an
/// ordered Vector (not a HashMap) so route scanning is deterministic (ascending insertion order).
struct YukiRoute
{
    unsigned channel_;
    String expertId_;
};

/// P3 primitive + P4 orchestrator. ADDITIVE: reads the caller's output-layer signals and, on an organic
/// trigger, runs a pinned expert's OWN forward pass in its OWN inference context and composes the result
/// back as a secondary step; the caller then samples as usual. The output head is NEVER touched, and an
/// absent/cold/guard-refused trigger is a zero-cost no-op — the caller proceeds with its own logits
/// (the CARDINAL RULE: federation is never worse than no-federation).
///
/// P4 promotes P3's inert guards (depthCap=1, triggerBudget=1) into real knobs and adds the call stack:
/// an expert, during its forward pass, may itself trigger another expert (its inference context also holds
/// this dispatch), forming a call TREE. Three independent bounds keep the tree finite + acyclic:
///   - depthCap_  : max call-stack depth (bounds a DEEP chain A->B->C->...).
///   - on-stack   : an id already on the stack is refused (kills cycles A->B->A precisely; String-keyed,
///                  NOT StringHash — a hashed key would collide two ids and FALSELY refuse a valid expert).
///   - callBudget_: max expert invocations across the WHOLE tree per output step (bounds a wide fan-out).
///
/// LIFECYCLE: experts are pinned ONCE at startup (off any live pass) and held as owning SharedPtr copies
/// for this object's life; the hot path is pure (no registry lock, no mid-pass Load/Unload — P2 live-pass
/// contract). Multi-hop means the whole reachable working set must be pinned up front (P4 design §7).
class URHO3D_API YukiDispatch : public Object
{
    URHO3D_OBJECT(YukiDispatch, Object);

public:
    /// Construct. `registry` is BORROWED (owned by Yuki) — used only for the one-time startup pins.
    YukiDispatch(Context* context, YukiCartRegistry* registry);
    /// Destruct — drops the pinned-expert copies and their inference contexts (off any live pass).
    ~YukiDispatch() override;

    /// Pin an expert at STARTUP (never mid-pass): Load() it through the registry, verify vocab IDENTITY
    /// against `core` (same size AND same token<->index mapping — required so composing in vocab space is
    /// meaningful), and build its dedicated inference context (buffers sized to the expert topology). The
    /// context is wired back to THIS dispatch so the expert's own output can trigger sub-experts (bounded
    /// by the guards). Idempotent per id. Also auto-adds a route on the current default trigger channel if
    /// that channel is unrouted (preserves P3's single-expert one-liner UX). Returns false and pins nothing
    /// on load failure or vocab mismatch. `core` is borrowed, not held.
    bool PinExpert(const String& id, const String& path, YukiModel* core);

    /// Map a trigger CHANNEL to a pinned expert id (an edge in the routing table). Replaces any existing
    /// route on that channel. The id need not be pinned yet, but a route to an unpinned id is a no-op at
    /// dispatch time (graceful degrade). Returns false on empty args.
    bool SetRoute(unsigned channel, const String& expertId);
    /// Remove the route on `channel` (if any).
    void ClearRoute(unsigned channel);

    /// Hot call at a trigger site — AFTER the caller's logits, BEFORE its pick. Scans the routing table:
    /// for each channel whose signal `tanh(logits[channel])` crosses `threshold_` (or `forceMode_`), and
    /// subject to the depth/cycle/budget guards, runs the mapped expert on `contextTokens` and composes its
    /// (finite-checked) logits back into `logits` per `composePolicy_`. Returns true iff at least one hop
    /// composed. A no-op returning false when nothing is pinned or every route is cold/refused. `logits`/
    /// `vocabSize` are the CALLER's; every expert shares that vocab (guaranteed at pin time), so composing
    /// any expert's logits into any caller's is valid at every hop.
    bool MaybeRoute(const Vector<unsigned>& contextTokens, float* logits, unsigned vocabSize);

    /// @{ Config. Organic/learned θ and gates are P5; these are the P4 knobs + a force debug mode.
    void SetThreshold(float t) { threshold_ = t; }
    /// The default trigger channel used by a bare `PinExpert` auto-route (keeps P3's single-expert UX).
    void SetTriggerChannel(unsigned c) { triggerChannel_ = c; }
    void SetForceMode(bool f) { forceMode_ = f; }
    void SetComposePolicy(YukiComposePolicy p) { composePolicy_ = p; }
    /// Max call-stack depth (>=1). Default 2 = core -> expert -> sub-expert.
    void SetDepthCap(int d) { depthCap_ = d < 1 ? 1 : d; }
    /// Max expert invocations across the whole call tree per output step (>=0). Default 4.
    void SetCallBudget(int b) { callBudget_ = b < 0 ? 0 : b; }
    /// P6: the CORE model, used to interface-lock (§6) experts LOADED ON DEMAND (whose compat can't be
    /// checked at pin time because they were never pinned). Borrowed; read for vocab/topology, never trained.
    void SetCore(YukiModel* core) { core_ = core; }
    /// P6: max RESIDENT expert contexts (memory budget). Beyond this, the least-recently-used non-pinned,
    /// non-on-stack expert is evicted + unloaded on the next load. 0 = unlimited. Default 4.
    void SetResidentCap(unsigned n) { residentCap_ = n; }
    /// @}

    /// True once at least one expert is resident (pinned or loaded on demand).
    bool HasExpert() const { return !pinnedExperts_.Empty(); }
    /// Number of RESIDENT experts (pinned + lazily loaded).
    unsigned PinnedCount() const { return pinnedExperts_.Size(); }
    /// Ids of the resident experts (diagnostics).
    Vector<String> PinnedIds() const { return pinnedExperts_.Keys(); }
    /// The routing table (diagnostics).
    const Vector<YukiRoute>& Routes() const { return routes_; }
    /// Diagnostics: how many hops have fired+composed since construction.
    unsigned FireCount() const { return fireCount_; }
    int DepthCap() const { return depthCap_; }
    int CallBudget() const { return callBudget_; }
    unsigned ResidentCap() const { return residentCap_; }

private:
    /// Vocab IDENTITY: same vocabSize AND core->GetToken(i) == expert->GetToken(i) for every i.
    static bool VocabMatches(YukiModel* core, YukiModel* expert);
    /// True iff all `count` values are finite (no NaN/inf). Bit-pattern test — robust under -ffast-math,
    /// where std::isfinite may be optimised away. Per-hop finite gate (P4 §4): checked at EVERY return so a
    /// non-finite sub-expert can never leak poison up into a finite caller.
    static bool AllFinite(const float* v, unsigned count);

    /// P6: return `id`'s resident inference context, LOADING IT ON DEMAND from the manifest if not resident
    /// (interface-locked vs core_, context built, LRU-tracked, evicting to stay under the cap). Null if the
    /// id is not in the manifest / fails to load / fails the interface lock. Touches the LRU tick on hit.
    YukiInference* EnsureResident(const String& id);
    /// P6: while more than `residentCap_` experts are resident, evict the least-recently-used one that is
    /// neither explicitly pinned nor currently on the call stack (evicting a live frame = UAF), unloading it.
    void EvictIfOverCap();

    /// Registry — BORROWED (owned by Yuki). Used at startup pins AND for P6 load-on-demand (LoadFromManifest
    /// is mutex-safe and touches only the expert's own CPU memory — never the core — so no core-trainer race).
    YukiCartRegistry* registry_;
    /// P6: the core model, for interface-locking experts loaded on demand. Borrowed; never trained.
    WeakPtr<YukiModel> core_;

    /// Pinned experts: owning SharedPtr copies held startup->shutdown (keeps each alive under any concurrent
    /// Unload, and keeps its inference context's WeakPtr valid). Keyed by full String id (never a hash).
    HashMap<String, SharedPtr<YukiModel> > pinnedExperts_;
    /// One dedicated inference context per pinned expert (scratch sized to that expert's topology). Each has
    /// this dispatch attached, so an expert's output can trigger sub-experts (bounded by the guards).
    HashMap<String, SharedPtr<YukiInference> > expertContexts_;
    /// The routing table (channel -> expert id), scanned in insertion order for deterministic dispatch.
    Vector<YukiRoute> routes_;

    // ── Call stack + guards (the P4 orchestration state; one owner) ──
    /// Ids currently on the call stack, in order (depth == size). Empty between output steps.
    Vector<String> callStack_;
    /// Fast on-stack membership for the cycle guard. String-keyed (NOT StringHash — see class doc / §2b).
    HashSet<String> onStack_;
    /// Remaining expert invocations for the current output step; reset to callBudget_ at each top-level entry.
    int callsRemaining_{0};

    // ── P6: load-on-demand residency (memory-bounded scale-out) ──
    /// Explicitly `/fed pin`-ned ids — resident-and-eviction-EXEMPT (a lazily loaded id is not in here).
    HashSet<String> pinnedIds_;
    /// Last-access tick per resident id (for LRU eviction). Bumped on every EnsureResident hit/load.
    HashMap<String, unsigned> lruTick_;
    /// Monotonic access counter feeding lruTick_.
    unsigned accessTick_{0};
    /// Max resident expert contexts; 0 = unlimited. Default 4.
    unsigned residentCap_{4};

    // ── Knobs ──
    float threshold_{0.0f};        ///< A route fires when tanh(logits[channel]) > threshold_ (tap A).
    unsigned triggerChannel_{0};   ///< Default channel for a bare PinExpert auto-route (P3 UX).
    bool forceMode_{false};        ///< Debug: fire every route regardless of signal (validate wiring).
    YukiComposePolicy composePolicy_{YUKI_COMPOSE_REPLACE};
    int depthCap_{2};              ///< Max call-stack depth. Default 2.
    int callBudget_{4};            ///< Max invocations per output step across the tree. Default 4.

    /// Diagnostics.
    unsigned fireCount_{0};
};

}

//
// Copyright (c) 2008-2024 the Urho3D project.
// License: MIT
//
// Yuki expert federation — P2: multi-cart registry + loader.
//

#pragma once

#include "../Core/Object.h"
#include "../Core/Mutex.h"
#include "../Container/Str.h"
#include "../Container/Vector.h"
#include "../Container/HashMap.h"
#include "../Container/Ptr.h"
#include "YukiModel.h"

namespace Urho3D
{

/// Registry of auxiliary "expert" cart models for the Yuki federation (Phase P2).
/// \details Holds more than one loaded `.cart` — a registry of experts — with load-on-trigger and
/// unload-for-memory. This is DELIBERATELY additive: the Yuki CORE cart (`Yuki::model_`) is NOT held
/// here and is never touched by the registry (untouched-core, Leith rule 1). Keeping the core OUT is
/// what makes it UAF-safe: the trainer holds the core as a `WeakPtr` (YukiTrainer.h) whose `operator->`
/// does NOT check expiry, so a dropped core SharedPtr would be a use-after-free — and the registry can
/// never drop what it never holds. Each expert is a fixed-topology `YukiModel` (Leith rule 2).
///
/// THREAD-SAFETY: all public methods lock `mutex_`, so the registry map is safe against concurrent
/// train/dispatch-thread mutation. Load()/Get()/etc return a SharedPtr COPY (never a raw `YukiModel*`),
/// so a caller holding the result cannot dangle across a concurrent Unload().
///
/// LIVE-PASS CONTRACT (caller's responsibility, NOT enforced here): `mutex_` guards the registry
/// STRUCTURE only. Loading/unloading an expert touches GPU-resident + weight memory the live trainer may
/// be using, so any Load()/Unload() DURING a training pass MUST additionally hold the trainer's
/// WeightExclusive lock and coordinate GPU-resident state — or it races the live trainer. The core is
/// pinned at STARTUP only (never mid-pass). P2 is the registry + loader ONLY; the output-layer
/// trigger/dispatch that selects an expert is P3.
class URHO3D_API YukiCartRegistry : public Object
{
    URHO3D_OBJECT(YukiCartRegistry, Object);

public:
    /// Construct.
    explicit YukiCartRegistry(Context* context);
    /// Destruct — unloads every registered expert cart (never touches the core).
    ~YukiCartRegistry() override;

    /// Load a `.cart` from `path` and register it under `id`. Idempotent: if an expert is already loaded
    /// under `id`, the existing one is returned (no double-load / no leak). Returns the loaded cart as a
    /// SharedPtr COPY, or null on empty args / load failure (a failed load registers nothing).
    SharedPtr<YukiModel> Load(const String& id, const String& path);
    /// Bulk-load experts from a text manifest: one `id | path` per line (blank lines and lines starting with
    /// '#' are skipped; malformed lines are logged and skipped). Each entry goes through Load(), so it is
    /// idempotent and failure-isolated — one bad cart never aborts the rest. Returns the count SUCCESSFULLY
    /// loaded. Does NOT hold `mutex_` across the per-entry Load() calls (Urho Mutex is non-recursive); each
    /// Load() takes the lock itself. Uses engine File I/O (no raw platform calls), per project convention.
    unsigned LoadManifest(const String& manifestPath);
    /// Unload (free) the expert cart registered under `id`, reclaiming its GPU/CPU memory. No-op if absent.
    void Unload(const String& id);
    /// Return the loaded expert cart for `id` as a SharedPtr COPY (safe to hold across a concurrent
    /// Unload — the model stays alive until the caller's copy drops), or null if not registered.
    SharedPtr<YukiModel> Get(const String& id) const;
    /// True if an expert cart is registered AND loaded under `id`.
    bool IsLoaded(const String& id) const;
    /// Number of currently-loaded expert carts.
    unsigned Count() const;
    /// Human-readable ids of the currently-loaded experts (diagnostics / iteration).
    Vector<String> GetLoadedIds() const;
    /// Unload every expert cart (memory reclaim / teardown). Does NOT touch the core.
    void Clear();

    // --- Manifest catalog: the persistent id->path record of which experts EXIST, DECOUPLED from residency.
    //     `carts_` answers "what is loaded right now"; the manifest answers "which experts exist and where
    //     their .cart lives" — the source of truth P6 scale-out load-on-demand and P5 subcart production need.
    //     A cataloged id may be resident, or known-but-unloaded (costs no memory until LoadFromManifest). ---

    /// Record (or update) that an expert `id` lives at `path`, WITHOUT loading it. This is the producer API:
    /// P5 registers each subcart it trains here so the federation knows it exists. Idempotent; the catalog
    /// entry survives Unload (unloading frees memory but the expert still EXISTS on disk). False on empty args.
    bool Register(const String& id, const String& path);
    /// Look up a known expert's `.cart` path by id (pure catalog lookup, no load). Empty String if unknown.
    String GetManifestPath(const String& id) const;
    /// Ids of ALL known experts in the catalog (resident or not) — the "which experts exist" answer for P6.
    Vector<String> GetManifestIds() const;
    /// Number of known experts in the catalog (>= Count(), since not every known expert need be resident).
    unsigned ManifestCount() const;
    /// Load an expert by id using its cataloged path (load-on-demand — P6's scale-out entry point). Returns
    /// the loaded cart (SharedPtr COPY) or null if the id is not cataloged or the load fails.
    SharedPtr<YukiModel> LoadFromManifest(const String& id);
    /// Persist the catalog to `manifestPath` as sorted `id | path` lines (round-trips with LoadManifest via
    /// the same format). Snapshots under the lock, writes OUTSIDE it (no file I/O under the non-recursive
    /// mutex). Uses engine File I/O. Returns false on write failure.
    bool SaveManifest(const String& manifestPath);

private:
    /// Guards carts_. Mutable so const accessors (Get/IsLoaded/Count/GetLoadedIds) can lock.
    /// MUTEX DISCIPLINE: Urho3D Mutex is NON-RECURSIVE — no locked method may call another locked
    /// method (that would self-deadlock). Each public method takes the lock once and does its own work.
    mutable Mutex mutex_;
    /// id (the FULL String, not a hash) -> loaded expert cart. Keyed by the exact String so two distinct
    /// ids can NEVER collide into one entry (a 32-bit StringHash key silently merged hash-colliding ids —
    /// coder2's catch). SharedPtr so Unload()/Clear() frees the model and its buffers. The key IS the
    /// human-readable id, so no parallel id map is needed.
    HashMap<String, SharedPtr<YukiModel> > carts_;
    /// id (FULL String) -> `.cart` path. The catalog of KNOWN experts, independent of `carts_` residency.
    /// Guarded by the SAME mutex_. Populated by Register(), by a successful Load(), and by LoadManifest();
    /// NOT cleared by Unload()/Clear() (residency changes, existence does not — an unloaded expert still
    /// exists on disk and stays discoverable for load-on-demand).
    HashMap<String, String> manifest_;
};

}

// YukiTrainer — Consume memories, refine the cartridge.
// Copyright (c) 2026 Urho3D project. License: MIT.
//
// Memories come in through SQLite and are KEPT — never deleted. Each pass
// reinforces the memories that most need it (highest error, oldest untouched),
// so drifting memories are repaired and accurate ones are left alone. Survival
// is driven by error, never by access frequency: a rarely-recalled but accurate
// memory is left be; a quiet one that has drifted is reinforced.
//
// The optimizer is Adam (plain SGD was measured too weak to converge a
// vocab-4096 transformer in a sane number of passes).
//
// When the vocabulary overflows the trainer triggers an expand — bigger
// cartridge, old weights migrated. The brain grows but doesn't forget.

#pragma once

#include "../Core/Object.h"
#include "../ML/YukiModel.h"
#include "../ML/YukiInference.h"
#include "../ML/PagedFloatBuffer.h"   // paged (64-bit-addressable) weight/grad/Adam buffers
#include "../Container/HashMap.h"     // in-RAM priority/staleness ledger (id -> LedgerEntry)

#include <atomic>
#include <shared_mutex>
#include <mutex>

namespace Urho3D
{

class DbConnection;

/// Training statistics for one pass.
struct YukiTrainStats
{
    float loss{};           ///< Average loss over the reinforced batch.
    unsigned samples{};     ///< Number of memories reinforced this pass.
    unsigned unknowns{};    ///< Unknown tokens encountered.
    bool expanded{};        ///< Whether the cartridge was expanded this pass.
};

/// Trains a YukiModel from SQLite memories. Keeps every memory (never deletes);
/// each pass reinforces the highest-error / most-stale ones via Adam.
class URHO3D_API YukiTrainer : public Object
{
    URHO3D_OBJECT(YukiTrainer, Object);

public:
    explicit YukiTrainer(Context* context);

    /// Set the model to train.
    void SetModel(YukiModel* model);
    /// Set the memory database connection.
    void SetMemoryDb(DbConnection* db) { memoryDb_ = db; }

    /// Share the owner's weight reader/writer lock + DB mutex so TrainOnce can serialise its own
    /// short critical sections against UI inference / commands (the long compute stays SHARED, so
    /// inference runs concurrently). Null = standalone (the conditional lock helpers no-op).
    void SetLocks(std::shared_mutex* weight, std::mutex* db) { weightMutex_ = weight; dbMutex_ = db; }

    /// Run one training pass: select the memories that most need reinforcement
    /// (highest error + staleness), take an Adam step on each, persist their new
    /// error, save. Memories are KEPT, never deleted. Returns stats.
    YukiTrainStats TrainOnce();

    /// Check if the model needs expansion (too many unknowns, loss stagnant).
    bool NeedsExpansion() const;

    /// Expand the model — bigger vocab, optionally deeper/wider.
    /// Rebuilds vocabulary from remaining memories + existing tokens.
    bool Expand();

    /// Set learning rate.
    void SetLearningRate(float lr) { learningRate_ = lr; }
    float GetLearningRate() const { return learningRate_; }

    /// Set threshold: expand when unknown ratio exceeds this.
    void SetUnknownThreshold(float ratio) { unknownThreshold_ = ratio; }

    /// How many memories to reinforce per pass.
    void SetReinforceBatch(unsigned k) { reinforceBatch_ = k; }
    unsigned GetReinforceBatch() const { return reinforceBatch_; }
    /// Staleness weight: priority bonus per pass a memory has gone untouched.
    void SetStaleness(float a) { staleness_ = a; }
    float GetStaleness() const { return staleness_; }

    // ─── Plateau detection ───────────────────────────────────────────────────
    // The raw per-pass loss is noisy (it's the mean over a changing subset), so
    // we track a smoothed EMA + a stagnation counter rather than a raw slope.

    /// True once the smoothed loss has stopped improving for plateauPasses_.
    bool IsPlateaued() const { return stagnation_ >= plateauPasses_; }
    float GetLossEMA() const { return lossEMA_; }
    float GetBestEMA() const { return bestEMA_; }
    int GetStagnation() const { return stagnation_; }
    /// Minimum EMA improvement that counts as progress.
    void SetPlateauDelta(float d) { plateauDelta_ = d; }
    /// Stagnant passes before IsPlateaued() trips.
    void SetPlateauPasses(int n) { plateauPasses_ = n; }

    // ─── Elite (best-effort) caching ─────────────────────────────────────────
    // Quality is measured on a FIXED probe set (a sampled, frozen subset of
    // memories) via forward-only loss — a stable, comparable number whose cost is
    // constant as the corpus grows, unlike a full-corpus eval. Whenever the probe
    // loss sets a new best we snapshot the weights (the elite) and persist a
    // `.best` cartridge, so the best floor ever found is never overwritten by
    // later drift. RestoreBest() returns the live model to that elite.

    /// Load the persisted elite (.best) at startup: read its weights into the in-RAM
    /// elite and seed bestLoss_ by EVALUATING it on the current probe — so a worse
    /// state can never overwrite the best (the bug that lost the 2.x floor) and
    /// /restorebest works after a restart. Call after SetModel + SetMemoryDb. No-op
    /// if there's no .best or it doesn't match the current topology.
    void LoadElite();
    /// Forward-only probe loss for a GIVEN weight buffer (builds the probe on first
    /// use). Returns -1 if it couldn't score (no probe / nothing trainable).
    float EvalProbeLoss(const float* weights);
    /// Paged overload — scores a PagedFloatBuffer of weights (elite / PBT member /
    /// difference-move candidate) by building weight views once and running the
    /// view-taking loss over the probe. Same value the flat overload would return.
    float EvalProbeLoss(const PagedFloatBuffer& weights);
    /// DIAGNOSTIC: compute the SUMMED gradient over `seqs` on the live model_ weights into `out`
    /// (forward+backward, NO Adam step, NO weight change) — the CPU reference for comparing against
    /// the GPU resident Gacc on identical weights+batch. Configures `out` if needed. Returns the
    /// max |gradient| element (or -1 if it couldn't run).
    float ComputeBatchGradient(const Vector<Vector<unsigned> >& seqs, PagedFloatBuffer& out);
    /// The elite-ratchet DECISION on the live model_ weights: probe-eval, and if it beats
    /// the floor, capture+persist the new elite. The SINGLE decision source — TrainOnce calls
    /// it, and the GPU resident mode calls it on the synced weights, so both ratchet identically.
    /// Returns true iff a new best was captured. (Caller does the cadence + any probe resample.)
    bool RatchetProbe();
    /// RatchetProbe WITH the pawl (ceiling): the other half of a real ratchet. Probe-eval, then:
    /// improves the floor -> capture (+1); regresses PAST bestLoss*(1+tol) -> RestoreBest, i.e. yank
    /// the live model back to the elite (-1); within the band -> hold (0). RatchetProbe was capture-
    /// only, so the live/resident weights could climb with no ceiling — this clamps that runaway.
    /// tol < 0 disables the restore arm (== RatchetProbe). Caller does the cadence + probe resample,
    /// and (for the GPU pump) re-uploads model_ into the resident buffer when this returns -1.
    int RatchetGuard(float tol);
    /// Best probe loss seen so far (1e30 until the first eval).
    float GetBestLoss() const { return bestLoss_; }
    /// Drop the floor (bestLoss_ := 1e30) so the NEXT RatchetProbe captures unconditionally —
    /// forces a real capture through the production path (seed the elite from a freshly trained
    /// model). Discards the current floor; CaptureBest still snapshots + persists .best.
    void ResetBestLoss() { bestLoss_ = 1e30f; }
    bool HasElite() const { return bestWeights_.IsConfigured(); }
    /// Min seconds between .best DISK writes — throttles the early-training persist storm without ever
    /// losing the RAM elite. 0 = write every breakthrough (old behaviour). Baseline 30s; tune downward.
    void SetBestSaveInterval(float sec) { bestSaveIntervalSec_ = sec < 0.0f ? 0.0f : sec; }
    float GetBestSaveInterval() const { return bestSaveIntervalSec_; }
    /// Trailing flush: land a throttled-out best that would otherwise sit RAM-only until an unclean exit.
    /// Call from EVERY training pass cadence (CPU: TrainOnce tail; GPU: PumpGpuSolver) — independent of a new
    /// best, so it fires on convergence where CaptureBest never would. No-op unless dirty AND interval elapsed.
    void MaybeFlushBestToDisk();
    /// Restore the live weights to the elite snapshot; resets Adam; saves.
    void RestoreBest();
    /// Memories to evaluate in the probe (constant cost regardless of corpus size).
    void SetProbeCap(unsigned n) { probeCap_ = n; }
    /// Run the probe / capture-best every N training passes.
    void SetEvalEvery(int n) { evalEvery_ = n; }
    int GetEvalEvery() const { return evalEvery_; }   ///< Ratchet cadence — shared with the GPU trainer.

    // ─── Kick (perturb / elite-rollback) ─────────────────────────────────────
    // Escape a plateau by adding small noise scaled to weight RMS, then retrain.
    // The elite cache IS the safety net — a kick that fails to beat the elite is
    // rolled back to it. Diagnostic: a kick that never lowers the elite's floor
    // means the floor is capacity/regime-bound, not an optimization saddle.

    /// Add Gaussian noise (std = sigma·RMS(weights)) to every weight. Resets Adam
    /// state and re-prioritises every memory (stored errors are now stale).
    void Perturb(float sigma);
    /// Record the current elite floor, then Perturb(sigma). Caller runs TrainOnce
    /// passes (which keep capturing any new elite) then calls EvaluateKick().
    void Kick(float sigma);
    /// End of the kick window: returns true if the elite floor improved since the
    /// kick. Either way restores the live model to the elite (never leaves it adrift).
    bool EvaluateKick();

    // ─── PBT (population-based kick) ──────────────────────────────────────────
    // A kick can run a small population instead of one perturbation: K candidates
    // seeded off the elite, each refined by Adam under its OWN (lr, sigma), scored
    // by the probe loss. The winner promotes to the elite and hands its lr back to
    // the steady-state optimizer — so the population self-tunes the lr (the kick
    // "underwrites" the tuning). K=1 degenerates to a single-candidate kick.

    /// Set/get the PBT population size K.
    void SetPbtPop(unsigned k) { pbtPop_ = Max(1u, k); }
    unsigned GetPbtPop() const { return pbtPop_; }
    /// Run one PBT kick synchronously: seed K elite-derived candidates (spread lr &
    /// sigma), refine each `memberPasses` full-batch Adam passes, score by the probe,
    /// promote the winner + adopt its lr if it beats the persisted floor, else restore
    /// to the elite. Returns true iff the floor improved. Heavy — call from the worker.
    /// DORMANT: superseded by DifferenceMoveSweep (the kick was the "nuclear option").
    /// `progress` (optional): incremented once per refinement pass (0..pbtPop_*memberPasses) so a caller on
    /// another thread can render a live "k/total" counter — writes are plain int stores (a display race is benign).
    bool RunPbtKick(float baseSigma, int memberPasses, volatile int* progress = nullptr);

    // ─── Difference move (population-of-one GA) ───────────────────────────────
    // The graceful replacement for the kick. Instead of cosmic-radiation noise, take
    // the move from the trajectory of past bests: extrapolate along the improvement
    // direction  best + F·(best − prevBest)  and keep the candidate (a few F probed)
    // that beats the floor. Directed (follows where progress was going), self-scaling
    // (the best-to-best gap shrinks as it converges → moves shrink to fine-tuning),
    // and CHEAP — forward-only probe evals, no Adam grind, so it never freezes the
    // worker. N=1: the two operands are one lineage at two times (impossible in
    // nature, fine in math).

    /// Probe a few F along best+F·(best−prevBest); jump the live model to the best
    /// candidate that beats the floor (capturing it), else no-op. Needs >=2 checkpoints.
    /// Returns true iff the floor improved. Cheap; safe to call from the worker.
    bool DifferenceMoveSweep(float fbase);

    /// Tokenize every trainable (>=2 token) memory into `seqs`, exactly as TrainOnce does.
    /// Public so the GPU resident trainer shares the SINGLE tokenization source (vocab is
    /// immutable during training — const GetTokenIndex over a fixed buffer — so it's safe to
    /// call lock-free from the worker thread). Returns the count.
    unsigned BuildTokenSet(Vector<Vector<unsigned> >& seqs);

    /// Select the next priority training batch (= TrainOnce's selection, extracted verbatim):
    /// bumps the staleness clock, runs the priority query (last_loss + staleness·(pass−last_trained)
    /// DESC LIMIT reinforceBatch_), tokenizes each, retires <2-token memories, and fills ids/seqs
    /// with the trainable ones (+ unknowns count). Shared by TrainOnce and the GPU resident trainer.
    void SelectTrainingBatch(Vector<int>& ids, Vector<Vector<unsigned> >& seqs, unsigned& unknownsOut);
    /// Record one trained memory's result (= TrainOnce's writeback): updates the in-RAM priority
    /// ledger (last_loss + the staleness stamp last_trained + train_count). RAM only — NO disk write.
    /// The ledger is the scheduler's scratch state; it reaches disk only via FlushLedger on a floor
    /// breakthrough + teardown.
    void RecordTrainResult(int id, float loss);
    /// Batched RecordTrainResult: a whole pass's (id,loss) writebacks into the RAM ledger. RAM only —
    /// NO disk write per pass. This is what killed the per-pass WAL-fsync storm the GPU pump generated:
    /// the priority ledger (last_loss/last_trained/train_count) is ephemeral scheduler state, not
    /// checkpoint state, so it lives in RAM and is flushed to SQLite only on a floor breakthrough +
    /// teardown (FlushLedger). ids/losses are index-aligned.
    void RecordTrainResults(const Vector<int>& ids, const Vector<float>& losses);
    /// Persist the in-RAM priority ledger to SQLite in ONE transaction (one commit/fsync), iff dirty.
    /// The ONLY disk-write path for the ledger — call it on a floor breakthrough + at teardown, not
    /// in the hot pass loop. A crash between flushes loses only the last few passes of priority
    /// bookkeeping, which the next passes re-measure (nothing precious is lost).
    void FlushLedger();
    /// Feed one pass's mean loss into the smoothed plateau tracker (= TrainOnce's EMA/stagnation
    /// block, extracted verbatim): updates lastLoss_/lossEMA_/bestEMA_/stagnation_ so IsPlateaued()
    /// works for the GPU resident trainer too. Shared single source.
    void UpdatePlateau(float passLoss);
    /// Periodically re-sample the fixed probe set (cadence-limited inside). Public so the GPU
    /// resident trainer runs the SAME probe refresh as TrainOnce before RatchetProbe.
    void MaybeResampleProbe();

private:
    /// Forward pass, compute loss and gradients, update weights.
    float TrainSample(const Vector<unsigned>& tokens);

    /// Backpropagate through output projection.
    void BackwardOutput(const float* hidden, const float* logits,
                        const Vector<unsigned>& targets, float* gradHidden);

    /// Simple SGD weight update.
    void UpdateWeights(float* weights, const float* gradients, unsigned count);

    /// (Re)size gradient + Adam moment buffers to the current topology; zero Adam
    /// state and reset its timestep. Call on SetModel and after Expand.
    void ResizeBuffers();

    /// Zero the Adam moment buffers and reset the timestep without resizing.
    void ResetAdam();

    /// Idempotently ensure the reinforcement columns exist on the memories table. The columns are the
    /// durable backing store for the RAM ledger — written only by FlushLedger, never in the hot loop.
    void EnsureSchema();

    /// One row of the in-RAM priority/staleness ledger. Defaults match the DB column DEFAULTs, so a
    /// never-seen memory ranks at top priority (last_loss 1e30) until first reinforced.
    struct LedgerEntry
    {
        float lastLoss{1e30f};   ///< Last measured error (priority weight).
        int lastTrained{0};      ///< trainPass_ at last reinforcement (staleness stamp).
        unsigned trainCount{0};  ///< Times reinforced.
    };

    /// Lazily load the ledger from SQLite ONCE (id -> {last_loss,last_trained,train_count}) and seed
    /// trainPass_ from max(last_trained), so a restart resumes the last flushed priority state.
    void LoadLedger();
    /// Get (insert-default) the ledger row for a memory id. Default rows rank at top priority.
    LedgerEntry& LedgerFor(int id);
    /// Reset every ledger row's priority to top (last_loss=1e30) — the RAM analogue of the old
    /// "UPDATE memories SET last_loss=1e30" re-baseline after a kick / weight jump. Marks dirty.
    void ResetLedgerPriority();

    /// One pool worker's data-parallel share of a pass: atomically claim disjoint sequences
    /// (nextSeq_) and accumulate their forward+backward into this worker's OWN gradient buffer
    /// (workerGrads_[index]); write each claimed seq's loss into lossOut_[i]. Lock-free — workers
    /// only read the shared (pass-start) weights and write disjoint buffers, so none can perturb
    /// another's gradient. Reused by the coordinator (index 0) and every spawned helper.
    void WorkerProcessShare(unsigned index, const Vector<Vector<unsigned> >& seqs);

    /// One pool worker's data-parallel share of the update phase (phase B), partitioned over the
    /// PARAMETER axis (phase A partitions over sequences). Slice `sliceIdx` of `nSlices` owns the
    /// contiguous param range [sliceIdx*cnt/nSlices, (sliceIdx+1)*cnt/nSlices): it sums that range
    /// across the first `nWorkers` workerGrads_ buffers into gradients_ (the reduce), scales by invK
    /// (batch mean), then Adam-steps that range in place — reduce and Adam fused into one per-parameter
    /// pass. nSlices is the fan-out width (T parallel, 1 serial); nWorkers is the phase-A pool width
    /// (always T) — decoupled so the serial one-slice path still reduces across every worker's gradient.
    /// Lock-free: each worker touches only its own disjoint param slice of gradients_/weights/adamM_/
    /// adamV_, and adamStep_ is a scalar fixed for the whole pass, so bias correction is identical.
    void WorkerApplyUpdate(unsigned sliceIdx, unsigned nSlices, unsigned nWorkers,
                           unsigned long long cnt, float invK);

    WeakPtr<YukiModel> model_;
    DbConnection* memoryDb_{};

    float learningRate_{0.003f};    ///< Adam lr (plain-SGD 0.001 measured non-learning).
    float beta1_{0.9f};             ///< Adam first-moment decay.
    float beta2_{0.999f};           ///< Adam second-moment decay.
    float adamEps_{1e-8f};          ///< Adam epsilon.
    unsigned long long adamStep_{}; ///< Adam timestep (bias correction).

    unsigned reinforceBatch_{12};   ///< Memories reinforced per pass (K).
    float staleness_{0.01f};        ///< Priority bonus per idle pass (ALPHA).
    int trainPass_{-1};             ///< Monotonic staleness clock; seeded from DB.

    float unknownThreshold_{0.3f};  ///< Expand when 30%+ tokens are unknown.
    float lastLoss_{1e9f};
    unsigned totalUnknowns_{};
    unsigned totalTokens_{};
    bool schemaReady_{};            ///< EnsureSchema run once.

    // --- In-RAM priority/staleness ledger (was per-pass SQLite writes) ---
    // last_loss/last_trained/train_count used to be UPDATEd in SQLite every pass — one commit/fsync
    // per pass, a disk-write storm on a small corpus for state that never needed to be durable. The
    // ledger now lives here in RAM (SelectTrainingBatch ranks off it, RecordTrainResults mutates it)
    // and reaches disk only via FlushLedger on a floor breakthrough + at teardown.
    HashMap<int, LedgerEntry> ledger_;   ///< id -> {lastLoss,lastTrained,trainCount}.
    bool ledgerLoaded_{false};           ///< LoadLedger run once.
    bool ledgerDirty_{false};            ///< RAM changes not yet flushed to SQLite.

    /// Gradient + Adam moment buffers (parallel to the flat weight vector).
    /// gradients_ doubles as the master accumulator: per-worker grads are summed into it,
    /// averaged, then a single Adam step is applied.
    PagedFloatBuffer gradients_;    ///< Master grad accumulator (paged — 64-bit addressable).
    Vector<float> gradHidden_;      ///< embedDim-sized scratch (small — stays flat).
    PagedFloatBuffer adamM_;
    PagedFloatBuffer adamV_;

    // --- CPU data-parallel minibatch pool (Phase 1b) ---
    // (cores-1) total compute threads (coordinator + helpers), spawned per pass and joined before
    // the reduce/Adam — no persistent threads, so no lifecycle/teardown races. Each worker owns a
    // private gradient buffer; they meet only at the single-threaded reduce. See WorkerProcessShare.
    unsigned trainThreads_{1};            ///< max(1, logical cores - 1); set once in the ctor.
    Vector<PagedFloatBuffer> workerGrads_;  ///< Per-worker private gradient (TotalWeights each, paged), sized in ResizeBuffers.
    Vector<float> lossOut_;               ///< Per-seq loss, written by the claiming worker (disjoint indices).
    std::atomic<unsigned> nextSeq_{0};    ///< Atomic disjoint-claim cursor over the batch (lock-free).

    // --- Shared locks (owned by Yuki, pointers set via SetLocks; null in standalone tests) ---
    std::shared_mutex* weightMutex_{};    ///< Weight RW lock: SHARED for compute/inference, EXCLUSIVE for expand.
    std::mutex* dbMutex_{};               ///< Serialises SQLite access vs the UI command handler.
    /// Conditional lock helpers — acquire iff the pointer is set (movable RAII; no-op when null).
    std::shared_lock<std::shared_mutex> WeightShared()
    { return weightMutex_ ? std::shared_lock<std::shared_mutex>(*weightMutex_) : std::shared_lock<std::shared_mutex>{}; }
    std::unique_lock<std::shared_mutex> WeightExclusive()
    { return weightMutex_ ? std::unique_lock<std::shared_mutex>(*weightMutex_) : std::unique_lock<std::shared_mutex>{}; }
    std::unique_lock<std::mutex> DbLock()
    { return dbMutex_ ? std::unique_lock<std::mutex>(*dbMutex_) : std::unique_lock<std::mutex>{}; }

    // Plateau detection (noise-robust: smoothed loss + stagnation counter).
    float lossEMA_{0.0f};           ///< EMA of per-pass loss; 0 = uninitialised.
    float bestEMA_{1e9f};           ///< Best smoothed loss seen so far.
    int stagnation_{0};             ///< Passes since bestEMA_ last improved.
    float plateauDelta_{0.01f};     ///< Min EMA improvement counted as progress.
    int plateauPasses_{30};         ///< Stagnant passes before IsPlateaued().
    float emaAlpha_{0.1f};          ///< EMA smoothing factor.

    // Kick perturbation RNG.
    unsigned kickRng_{0x9e3779b9u}; ///< LCG state for perturbation noise.

    // Elite (best-effort) cache + fixed evaluation probe.
    PagedFloatBuffer bestWeights_;  ///< Snapshot of the best weights ever seen (paged).
    float bestLoss_{1e30f};         ///< Probe loss of bestWeights_ (1e30 = none yet).
    float lastBestDiskSave_{0.0f};  ///< Time::GetElapsedTime() of the last .best DISK write (0 = never).
    float bestSaveIntervalSec_{30.0f}; ///< Min seconds between .best disk writes; throttles the persist storm (tunable).
    bool  bestDirty_{false};        ///< A throttled-out best sits in bestWeights_ but not on disk; trailing flush lands it.
    bool  WriteBestToDisk(float nowSec); ///< Atomically persist bestWeights_ -> .best (temp+rename); stamps time, clears dirty.
    float kickStartBest_{1e30f};    ///< bestLoss_ at the moment of the last Kick().
    String bestPath_;               ///< Persisted elite cartridge ("<cartridge>.best").

    Vector<int> probeIds_;          ///< Memory ids in the frozen evaluation probe.
    bool probeReady_{false};        ///< Probe has been sampled.
    unsigned probeBuiltAtCount_{0}; ///< Corpus size when the probe was built (resample trigger).
    unsigned probeCap_{128};        ///< Max memories in the probe (constant eval cost).
    int evalEvery_{10};             ///< Run the probe / capture-best every N passes.

    /// Sample the fixed probe set once (or resample on large corpus growth).
    void BuildProbe();
    /// Collect the frozen probe as token sequences (>=2 tokens each) and fill `dims`. Shared
    /// by both EvalProbeLoss overloads so the DB read + tokenisation live in exactly one place.
    /// Returns the number of scorable sequences (0 if no probe / nothing trainable).
    unsigned CollectProbeSeqs(Vector<Vector<unsigned> >& seqs, YukiMath::YukiDims& dims);
    /// (MaybeResampleProbe declared in the public section — shared with the GPU resident trainer.
    /// Builds the probe if needed, and on large corpus growth resamples it AND re-baselines
    /// bestLoss_ by re-evaluating the elite on the new probe — never a blind reset to 1e30.)
    /// Snapshot the current weights as the elite and persist the `.best` cartridge.
    void CaptureBest(float loss);

    // ─── PBT internals ────────────────────────────────────────────────────────
    /// One PBT candidate: its own weights + Adam state + evolved hyperparameters.
    struct PbtMember
    {
        PagedFloatBuffer weights;
        PagedFloatBuffer m, v;          ///< Per-member Adam moments (paged).
        unsigned long long step{};      ///< Per-member Adam timestep.
        float lr{};                     ///< Evolved learning rate.
        float sigma{};                  ///< Seed perturbation scale.
        float fitness{1e30f};           ///< Probe loss (lower is better).
    };

    /// (BuildTokenSet declared in the public section — shared with the GPU resident trainer.)
    /// One full-batch Adam pass over `seqs` on a member's own buffers/lr. Reuses the
    /// shared gradient scratch (members run sequentially under the worker mutex). No
    /// DB writeback — members are ephemeral.
    void TrainMemberPass(PbtMember& mem, const Vector<Vector<unsigned> >& seqs,
                         unsigned long long count);

    unsigned pbtPop_{4};            ///< PBT population size K (1 = classic kick).
    Vector<PbtMember> pop_;         ///< Candidate pool, reused across kicks.
    unsigned pbtRng_{0x2545F491u};  ///< Separate LCG stream for member seed noise.

    // ─── Checkpoint ring (the temporal lineage for difference moves) ──────────
    /// Recent ELITE weight snapshots, oldest→newest. Pushed by CaptureBest, so it
    /// records the trajectory of bests. Difference moves extrapolate its newest two.
    Vector<PagedFloatBuffer> checkpointRing_;
    unsigned ringCap_{8};           ///< Max snapshots kept (drop oldest past this).
    /// Append a copy of `weights` to the ring, dropping the oldest beyond ringCap_.
    void PushCheckpoint(const PagedFloatBuffer& weights);
};

}

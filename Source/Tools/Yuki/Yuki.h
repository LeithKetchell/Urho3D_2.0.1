// Yuki — public domain LLM: data curation, training, and inference.
// UI for local interaction, reliable UDP for external clients.

#pragma once

#define YUKI_VERSION "0.4.05"  // app version; odd sub = beta, even = RC (cartridge-format version is YUKI_CART_VERSION)

#include <Urho3D/Engine/Application.h>
#include <Urho3D/Network/Network.h>
#include <Urho3D/Network/NetworkEvents.h>
#include <Urho3D/Network/Connection.h>
#include <Urho3D/Database/DbResult.h>
#include <Urho3D/Database/DbConnection.h>
#include <Urho3D/Database/Database.h>
#include <Urho3D/UI/UI.h>
#include <Urho3D/UI/UIElement.h>
#include <Urho3D/UI/Text.h>
#include <Urho3D/UI/ListView.h>
#include <Urho3D/UI/Window.h>
#include <Urho3D/UI/Font.h>
#include "ProgressGraph.h"
#include <Urho3D/UI/Button.h>
#include <Urho3D/UI/LineEdit.h>
#include <Urho3D/Scene/Scene.h>
#include <Urho3D/Resource/ResourceCache.h>
#include <Urho3D/Resource/XMLFile.h>
#include <Urho3D/IO/Log.h>
#include <Urho3D/ML/YukiModel.h>
#include <Urho3D/ML/YukiInference.h>
#include <Urho3D/ML/YukiTrainer.h>
#include <Urho3D/ML/YukiEars.h>
#include <Urho3D/ML/YukiCartRegistry.h>
#include <Urho3D/ML/YukiDispatch.h>
#include <Urho3D/ML/YukiExpertTrainer.h>
#include <Urho3D/ML/YukiExpertDataset.h>
#include <Urho3D/Core/Thread.h>
#include <Urho3D/Core/Mutex.h>
#include <Urho3D/Core/Timer.h>

#include <shared_mutex>   // weightMutex_ (reader/writer): compute+inference shared, RestoreBest/Expand exclusive
#include <mutex>          // dbMutex_ (SQLite single-writer serialisation)

using namespace Urho3D;

/// Background training worker — runs YukiTrainer::TrainOnce in a loop OFF the
/// render thread. The shared mutex serialises every model_/memoryDb_/trainer_
/// access between this thread and the main (UI) thread; the worker never touches
/// the UI, so the main thread reads passCount_/lastStats_ (under the mutex) to
/// drive logging, the kick window, and the status bar.
class YukiTrainWorker : public Thread
{
public:
    void Configure(YukiTrainer* trainer, YukiModel* model, std::shared_mutex* mutex)
    {
        trainer_ = trainer; model_ = model; mutex_ = mutex;
    }
    void ThreadFunction() override
    {
        unsigned sinceSave = 0;
        while (shouldRun_)
        {
            if (paused_)   // GPU/RUN mode: idle (no TrainOnce) but stay alive — avoids a
            {              // Stop()/join deadlock when GPU mode takes over.
                Time::Sleep(delayMs_);
                continue;
            }
            if (trainer_ && model_ && model_->IsLoaded())
            {
                // No outer lock: TrainOnce / DifferenceMoveSweep take trainMutex_ INTERNALLY —
                // SHARED around the parallel compute (the UI thread runs concurrently and stays
                // responsive), EXCLUSIVE only for a rare vocab expand. Save reads weights, so it
                // takes the shared lock too (excludes a concurrent RestoreBest / Expand).
                if (pbtRequested_)
                {
                    pbtSurpassed_ = trainer_->DifferenceMoveSweep(pbtSigma_);
                    pbtRequested_ = false;
                    ++pbtKickCount_;
                    if (pbtSurpassed_) { SaveLocked(); sinceSave = 0; }
                }
                else
                {
                    lastStats_ = trainer_->TrainOnce();
                    ++passCount_;
                    if (++sinceSave >= savePasses_) { SaveLocked(); sinceSave = 0; }
                }
            }
            Time::Sleep(delayMs_);   // inter-pass yield
        }
    }
    void SaveLocked()
    {
        if (mutex_ && model_) { std::shared_lock<std::shared_mutex> l(*mutex_); model_->Save(); }
    }
    /// Read by the main thread LOCK-FREE — these are display scalars, so a benign
    /// race is acceptable (no mutex on the per-frame path; that froze input).
    unsigned GetPassCount() const { return passCount_; }
    const YukiTrainStats& GetLastStats() const { return lastStats_; }
    void SetDelayMs(unsigned ms) { delayMs_ = ms; }
    unsigned GetDelayMs() const { return delayMs_; }
    /// Pause/resume CPU training WITHOUT stopping the thread — GPU-all-the-way pauses the CPU
    /// trainer (keeps it dormant + instantly revertible) instead of Stop()/join, which would
    /// deadlock if called while the UI thread holds trainMutex_ and the worker blocks on it.
    void SetPaused(bool p) { paused_ = p; }
    /// True when the CPU trainer is dormant (GPU or RUN mode). Lets the solver UI derive the live
    /// mode from real state instead of a separate flag that could desync from /gputrain.
    bool IsPaused() const { return paused_; }

    // ── PBT kick request (UI thread → worker) ────────────────────────────────
    // Plain scalars, polled every loop (~5 ms). A one-iteration delay is benign —
    // the same lock-free convention as the status reads. The UI fires only when
    // !PbtBusy() and detects completion via the kick counter changing.
    void RequestPbtKick(float sigma, int passes)
    {
        pbtSigma_ = sigma; pbtPasses_ = passes; pbtSurpassed_ = false; pbtRequested_ = true;
    }
    bool PbtBusy() const { return pbtRequested_; }
    unsigned GetPbtKickCount() const { return pbtKickCount_; }
    bool GetLastPbtSurpassed() const { return pbtSurpassed_; }

private:
    YukiTrainer* trainer_{};
    YukiModel* model_{};
    std::shared_mutex* mutex_{};
    YukiTrainStats lastStats_{};
    unsigned passCount_{};
    volatile bool paused_{false};       ///< GPU-all-the-way: CPU trainer idle but thread alive.
    volatile bool pbtRequested_{false}; ///< Set by UI, cleared by worker after the kick.
    float pbtSigma_{0.0f};              ///< Base sigma for the requested PBT kick.
    int pbtPasses_{0};                  ///< Adam passes per member for the requested kick.
    unsigned pbtKickCount_{0};          ///< Bumped each completed PBT kick (UI watches it).
    bool pbtSurpassed_{false};          ///< Last PBT kick beat the floor.
    unsigned delayMs_{5};        ///< Inter-pass yield. NOT zero: this is the window the UI thread
                                 ///< uses to take the mutex, so 0 would starve the render thread.
                                 ///< Tunable live via /rate.
    unsigned savePasses_{200};   ///< Persist the working cartridge ~ every 200 passes (rate is higher now).
};

/// Federation P5: background worker that runs the expert-subcart GA (Initialize + Steps + Emit) OFF the
/// render thread, so the naive-matmul fitness passes never freeze the UI. It touches ONLY the trainer, the
/// subcart seed, and READ-ONLY core vocab/topology (immutable during a run — never core weights), so no
/// lock is needed; the main thread reads the volatile progress scalars lock-free (benign display race, the
/// same convention as YukiTrainWorker) and does the registry/UI work on completion.
class ExpertTrainWorker : public Thread
{
public:
    void Configure(YukiExpertTrainer* trainer, YukiModel* core, YukiModel* seed, const String& cartPath)
    {
        trainer_ = trainer; core_ = core; seed_ = seed; cartPath_ = cartPath;
        finished_ = false; initFailed_ = false; emitOk_ = false; gen_ = 0; bestFit_ = 0.0f;
    }
    void ThreadFunction() override
    {
        if (!trainer_ || !core_ || !seed_) { initFailed_ = true; finished_ = true; return; }
        // Initialize seeds + scores the first tanks (heavy) — done here, not on the UI thread.
        if (!trainer_->Initialize(core_, seed_)) { initFailed_ = true; finished_ = true; return; }
        gen_ = trainer_->GetGeneration(); bestFit_ = trainer_->GetBestFitness();
        while (shouldRun_ && !trainer_->Graduated())
        {
            trainer_->Step();
            gen_ = trainer_->GetGeneration();
            bestTrain_ = trainer_->GetBestFitness();   // training fitness (memorisation)
            // Report the HELD-OUT validation score (generalisation) when a validator is wired; else train.
            bestFit_ = trainer_->HasValidatedChampion() ? trainer_->GetBestValFitness() : bestTrain_;
        }
        // Emit only on natural completion (a stop-early / cancel leaves no cart).
        if (trainer_->Graduated())
            emitOk_ = trainer_->Emit(cartPath_);
        finished_ = true;
    }
    /// Ask the worker to stop after the current generation (non-blocking; join later via Stop() once
    /// IsFinished()). Sets the base-class run flag so ThreadFunction's loop exits.
    void SignalStop() { shouldRun_ = false; }
    bool IsFinished() const { return finished_; }
    bool InitFailed() const { return initFailed_; }
    bool EmitOk() const { return emitOk_; }
    int GetGen() const { return gen_; }
    float GetBest() const { return bestFit_; }
    float GetBestTrain() const { return bestTrain_; }

private:
    YukiExpertTrainer* trainer_{};
    YukiModel* core_{};
    YukiModel* seed_{};
    String cartPath_;
    volatile bool finished_{false};
    volatile bool initFailed_{false};
    volatile bool emitOk_{false};
    volatile int gen_{0};
    volatile float bestFit_{0.0f};
    volatile float bestTrain_{0.0f};
};

/// Escape harness PBT rung, run OFF the render thread. RunPbtKick does pbtPop x memberPasses (~48) CPU
/// training passes — far too heavy for the main tick (it froze the UI). This runs it on its own thread
/// while the caller PAUSES the GPU pump (so the worker has exclusive access to the trainer/model — in GPU
/// mode the CPU worker is already paused too), and applies the result on the main thread when finished.
class EscapePbtWorker : public Thread
{
public:
    void Configure(YukiTrainer* trainer, float sigma, int memberPasses, unsigned pop)
    {
        trainer_ = trainer; sigma_ = sigma; memberPasses_ = memberPasses; pop_ = pop;
        finished_ = false; surpassed_ = false; progress_ = 0; total_ = (int)pop * memberPasses;
    }
    void ThreadFunction() override
    {
        if (trainer_)
        {
            trainer_->SetPbtPop(pop_);
            surpassed_ = trainer_->RunPbtKick(sigma_, memberPasses_, &progress_);
        }
        finished_ = true;
    }
    bool IsFinished() const { return finished_; }
    bool Surpassed() const { return surpassed_; }
    int  GetProgress() const { return progress_; }   ///< Refinement passes done (0..GetTotal()).
    int  GetTotal() const { return total_; }         ///< pop x memberPasses.
private:
    YukiTrainer* trainer_{};
    float sigma_{0.05f};
    int memberPasses_{8};
    unsigned pop_{6};
    volatile bool finished_{false};
    volatile bool surpassed_{false};
    volatile int progress_{0};
    int total_{0};
};

/// Resident GPU training state — the buffer set (W/G/m-v + activation/grad scratch),
/// topology, weight-slice offsets, cached shader variations and timestep, all defined
/// in Yuki.cpp. Held by raw pointer so this header needs no Graphics includes.
struct YukiResidentState;

namespace Urho3D { class VulkanGraphicsImpl; class VertexBuffer; class ShaderVariation; class HttpRequest; }
class Yuki;

// A2.3: YukiGpuTrainWorker (the off-render GPU training thread) is deleted. GPU production training
// now runs as a non-blocking main-tick state machine, Yuki::PumpGpuSolver() — no worker thread, no
// join at teardown. See Claude/SCOPE_poll_conversions.md (Conversion A); git history has the old class.

class Yuki : public Application
{
    URHO3D_OBJECT(Yuki, Application);

public:
    explicit Yuki(Context* context);

    void Setup() override;
    void Start() override;
    void Stop() override;

private:
    // --- Database ---
    void InitDatabase();
    void RunSchema();
    DbConnection* corpusDb_{};
    DbConnection* memoryDb_{};

    // --- Memory ---
    void InitMemory();
    /// Store a fact into yuki_memory.db
    void Remember(const String& text, const String& source);
    /// Search memories by keyword, return up to maxResults matches
    Vector<String> RecallMemories(const String& query, int maxResults = 5);
    /// Process user input — learn from it and respond
    void ProcessInput(const String& input);

    // --- UI ---
    void CreateUI();
    void LogMessage(const String& msg);
    void LogMessage(const String& msg, const Color& color);

    /// Chat-style input/output
    UIElement* rootLayout_{};
    SharedPtr<ListView> messageLog_;
    SharedPtr<LineEdit> inputField_;
    SharedPtr<Text>     statusText_;
    SharedPtr<ProgressGraph> progressGraph_;   ///< Live training-progress overlay (green loss curve).
    SharedPtr<ProgressGraph> leakGraph_;       ///< Red process-RSS overlay (leakage watch), same bounds as progressGraph_.

    /// Three-state learner control (top of the window). The proven CPU solver is never discarded —
    /// the button just switches which one is live so the GPU solver can be watched head-to-head.
    enum SolverMode { SOLVER_CPU, SOLVER_GPU, SOLVER_RUN };
    SharedPtr<Button> solverButton_;        ///< Colour-coded learner toggle: CPU / GPU / RUN.
    SharedPtr<Text>   solverButtonText_;    ///< Its label (current mode + next on click).
    /// Live mode DERIVED from real state (gpuWorkerRunning_ + CPU paused_), so it can never desync
    /// from /gputrain or /solver — there is no separate mode flag to drift.
    SolverMode CurrentSolverMode() const;
    /// Transition to a mode. SetSolverMode takes trainMutex_ (button handler path); SetSolverModeLocked
    /// assumes the caller already holds it (the /solver console command inside ProcessInput).
    void SetSolverMode(SolverMode mode);
    void SetSolverModeLocked(SolverMode mode);
    void UpdateSolverButton();              ///< Refresh label + colour from CurrentSolverMode().
    void HandleSolverButton(StringHash eventType, VariantMap& eventData);

    void HandleInput(StringHash eventType, VariantMap& eventData);
    void HandleUpdate(StringHash eventType, VariantMap& eventData);
    /// The window/engine signalled exit ("the app is dying") — save the best now.
    void HandleExitRequested(StringHash eventType, VariantMap& eventData);
    /// Persist the BEST (not the latest drift) exactly once, on the way out.
    void SaveOnExit();

    // --- Network ---
    void InitNetwork();
    unsigned short listenPort_{7879};

    void HandleClientConnected(StringHash eventType, VariantMap& eventData);
    void HandleClientDisconnected(StringHash eventType, VariantMap& eventData);
    void HandleNetworkMessage(StringHash eventType, VariantMap& eventData);

    // --- Corpus ---
    /// Query corpus stats (document count, source breakdown)
    String GetCorpusStats();

    // --- Brain ---
    void InitBrain();
    SharedPtr<YukiModel> model_;
    SharedPtr<YukiInference> inference_;
    SharedPtr<YukiTrainer> trainer_;
    bool hasBrain_{};

    // --- Federation P3: expert registry + output-layer trigger/dispatch (additive, off by default) ---
    /// Registry of auxiliary expert carts (P2). Owns the loaded experts; the CORE (model_) is never held
    /// here. Instantiated at brain init; experts are pinned only on explicit `/fed pin`.
    SharedPtr<YukiCartRegistry> cartRegistry_;
    /// Output-layer trigger + single-expert dispatch (P3). Attached to the CORE inference context only.
    /// Null-safe: with no expert pinned, MaybeRoute is a zero-cost no-op and inference is unchanged.
    SharedPtr<YukiDispatch> dispatch_;

    // --- Federation P5: expert-subcart training on a BACKGROUND thread (UI never blocks) ---
    /// Poll the background training worker each frame: log throttled progress, and on completion join it,
    /// register the emitted cart + save the manifest, and tear down. No-op when not training. Called from
    /// HandleUpdate — the heavy GA compute runs on expertWorker_, never on the render thread.
    void StepExpertTraining();
    ExpertTrainWorker expertWorker_;               ///< Background GA thread (see ExpertTrainWorker).
    SharedPtr<YukiExpertTrainer> expertTrainer_;   ///< Active GA (kept alive for the worker; null = idle).
    SharedPtr<YukiExpertDataset> expertDataset_;   ///< Held alive for the fitness seam during training.
    SharedPtr<YukiModel> expertSeed_;              ///< Seed/work model; held alive during training.
    String expertTrainId_;                         ///< Expert id being trained.
    String expertTrainCartPath_;                   ///< Output <id>.cart path.
    String expertTrainDir_;                        ///< experts/ dir (for the manifest).
    bool expertTraining_{false};                   ///< True while a GA run is in progress.
    int expertTrainLastLoggedGen_{-1};             ///< Throttles the per-generation progress log.

    // --- GPU resident trainer (STEP 3d: cross-step on-GPU training, main-thread) ---
    /// Allocate the resident buffer set once for the given topology and upload initial
    /// weights. Buffers persist across steps as a member so STEP 4's worker can drive
    /// ResidentStep() off the render thread with nothing else changed.
    bool ResidentInit(unsigned D, unsigned F, unsigned V, unsigned H, unsigned L, unsigned S,
                      const PagedFloatBuffer& initW, const unsigned* tokens);
    /// One on-GPU step on the resident buffers. train=true: zero G, fwd+bwd, in-place Adam, t++.
    /// train=false: FORWARD only (loss into ceB) — no backward/Adam, weights untouched, for eval.
    /// Fully GPU-resident; no readback. Returns false on a dispatch fault.
    bool ResidentStep(bool train = true);
    /// GPU minibatch "page": train count sequences (seqs[first..first+count), count <= pageP) as
    /// ONE minibatch in a SINGLE submit — per-seq grads summed into the page accumulator (Gacc) via
    /// the audited ELEM_ADD, then ONE Adam over the summed gradient. Per-seq mean CE loss is written
    /// to lossesOut[0..count) from one readback. P=1 reproduces ResidentStep exactly. The page is the
    /// GPU analogue of the CPU data-parallel minibatch (one averaged-update step, not count SGD steps).
    bool ResidentStepPage(const Vector<Vector<unsigned> >& seqs, unsigned first, unsigned count, float* lossesOut);
    /// A2 split of ResidentStepPage for the async / main-tick GPU pump. Record does the host uploads and
    /// records the whole page (zero → per-seq fwd+bwd → addCS → Adam) into an OPEN compute batch, but does
    /// NOT close it — the caller closes via EndComputeBatch (blocking) or EndComputeBatchAsync (async). The
    /// harvest context (keepAlive, per-seq curS/invCount, count) is parked in the resident state so it
    /// survives an async submit. Harvest does the single lossSlots readback + per-seq reduction into
    /// lossesOut and releases keepAlive; call it only after the batch has completed (blocking wait, or
    /// PollComputeBatch() true). ResidentStepPage itself is now Record → blocking close → Harvest.
    bool ResidentStepPageRecord(const Vector<Vector<unsigned> >& seqs, unsigned first, unsigned count);
    void ResidentStepPageHarvest(float* lossesOut);
    /// Record ONE sequence's forward (+optional backward into Gb) into the CURRENTLY-OPEN compute
    /// batch — the shared body of ResidentStep and ResidentStepPage. Caller owns Begin/EndComputeBatch,
    /// the Adam step, and keepAlive (the per-dispatch meta buffers must outlive the single submit).
    /// train=false: forward+loss only (no zero/backward). lossDst!=null: COPY the curS per-position
    /// nlls to lossDst[lossOff] (the page's per-seq loss siphon; single-seq reads ceB directly).
    void ResidentRecordSeq(VertexBuffer* tokB, VertexBuffer* tgtB, unsigned curS, float invCount,
                           bool train, VertexBuffer* lossDst, unsigned lossOff,
                           Vector<SharedPtr<VertexBuffer> >& keepAlive);
    /// Record the in-place Adam dispatch (W -= ... using gradP + the resident m/v) into the open compute
    /// batch: syncs the live tuned lr, ++t, bias-correct, then ONE dispatch per weight page. gradP is GbP
    /// (single step) or GaccP (page). Shared by ResidentStep and ResidentStepPage.
    void ResidentRecordAdam(Vector<SharedPtr<VertexBuffer> >& gradP, float gradScale = 1.0f);
    /// Choose the GPU page size P from descriptor-pool budget (the binding constraint — a page's
    /// sets must coexist until its submit) and a cap. Read-only; called once at ResidentInit.
    unsigned ComputeGpuPageSize(unsigned L) const;
    /// Forward-only mean cross-entropy loss for one sequence on the CURRENT resident weights
    /// (no training) — the stable eval metric. Sets the sequence, runs a forward, reads the loss.
    float ResidentEvalLoss(const unsigned* tokens, unsigned len);
    /// Swap in a new training sequence (variable length <= the allocated maxSeqLen): re-uploads
    /// tokens/targets + recomputes invCount + sets the current dispatch length. Lets ResidentStep
    /// run real corpus sequences (which vary in length) on a buffer set allocated once at maxSeqLen.
    void SetResidentSequence(const unsigned* tokens, unsigned len);
    /// Read back the current step's mean cross-entropy loss (the one amortized readback the
    /// resident loop is allowed). Returns 0 if no resident state.
    float ResidentReadLoss();
    /// Read the current resident weights back to host (TotalWeights floats, flat) — for verify and
    /// debug (small models); NOT called per step in the resident loop.
    bool ResidentReadWeights(float* out);
    /// Read the resident weights straight into a paged model buffer (page-by-page, no flat TW scratch) —
    /// the production GPU->model_ sync. `dst` is independently paged; ranges are scattered by global base.
    bool ResidentReadWeightsToModel(PagedFloatBuffer& dst);
    /// Upload host weights (TotalWeights floats) INTO the resident buffer — the model_->resident
    /// half of the sync (after the CPU meta-logic changes weights: kick/RestoreBest). Weights only;
    /// Adam moments are the caller's concern.
    bool ResidentUploadWeights(const PagedFloatBuffer& w);
    /// Zero the GPU-resident Adam moments (mvBuf, m/v interleaved). Must accompany any resident weight
    /// reset (RestoreBest/kick) on the GPU pump — else stale drift momentum steps the restored weights
    /// straight back off the floor. The GPU analogue of YukiTrainer::ResetAdam (which clears CPU Adam).
    bool ResidentZeroAdam();
    /// Soft Adam restart for the pawl yank: zero velocity m, KEEP curvature v + timestep (gentle,
    /// curvature-scaled re-entry at the floor instead of the cold-reset signSGD rebound).
    bool ResidentSoftRestart();
    /// Push resident weights back into model_ (the only model_ mutation in GPU production training).
    /// Readback OFF the lock; the host copy into model_ via TryAcquire (skip if busy — benign).
    void SyncResidentToModel();
    /// Same, but assumes the caller ALREADY holds trainMutex_ (or no concurrency) — the
    /// authoritative final sync in /gputrain stop after the worker has joined.
    void SyncResidentToModelLocked();
    /// Free the resident buffer set.
    void ResidentShutdown();
    /// Start GPU-all-the-way production training (the factored /gputrain-start body): resident init
    /// + start the off-render GPU worker, pausing the CPU trainWorker_ on success. Returns false
    /// (CPU worker untouched) if Vulkan/brain unavailable — the caller falls back to CPU.
    bool StartGpuTraining(bool announce);
    /// Stop the off-render GPU worker without losing progress (join, authoritative resident->model_
    /// sync, free resident). Used by the solver-mode switch when leaving GPU (->CPU or ->RUN). Returns
    /// true if the worker WAS running. MUST be called under trainMutex_ — SyncResidentToModelLocked
    /// needs it (the button path locks; /solver runs inside ProcessInput's lock).
    bool QuiesceGpuWorker();
    YukiResidentState* resident_{};
    // A2.3: GPU production training is now PUMPED from the main tick (PumpGpuSolver) — no worker thread.
    // Everything (record, poll, harvest, regime/kick, inference, RestoreBest/Expand) runs on main, so the
    // regime updates need NO trainMutex_ (the try_locks the worker used are gone). gpuPumpActive_ replaces
    // the old gpuWorkerRunning_ as the SOLVER_GPU discriminator.
    bool gpuPumpActive_{false};      ///< Whether the main-tick GPU solver pump is active.
    void PumpGpuSolver();            ///< Advance the GPU solver ONE non-blocking step per call (main tick).
    void RequestGpuKick(float fbase) { gpuKickFbase_ = fbase; gpuForceKick_ = true; }  ///< /kick in GPU mode.
    void StopGpuPump();              ///< Drain any in-flight page, clear the pool override, free the pools.
    enum GpuPumpPhase { GPU_IDLE, GPU_INFLIGHT };
    GpuPumpPhase gpuPhase_{GPU_IDLE};
    Vector<int> gpuBatchIds_;                 ///< Current priority batch (ids) being trained this pass.
    Vector<Vector<unsigned> > gpuBatchSeqs_;  ///< Current priority batch (token seqs).
    Vector<float> gpuBatchLosses_;            ///< Per-seq loss for the current pass (harvested per page).
    unsigned gpuPageStart_{0};                ///< Next page's first-seq index within the batch.
    unsigned gpuPassCount_{0};                ///< Completed GPU passes this run.
    // GPU telemetry (moved off the deleted worker; still read lock-free by HandleUpdate — all main now).
    unsigned gpuStepCount_{0};
    float    gpuLastEvalLoss_{1e30f};
    unsigned gpuKickCount_{0};
    bool     gpuKickSurpassed_{false};
    unsigned gpuRatchetRestores_{0};          ///< Times the pawl caught a ceiling breach + restored .best.
    unsigned gpuPassDone_{0};
    float    gpuLastPassLoss_{0.0f};
    unsigned gpuLastPassSamples_{0};
    // TEMP instrumentation (grad max-abs diagnosis): per-pass max |Gacc| driving the Adam step, and a
    // finiteness flag. Distinguishes bounded floor-walk (gradMax stays O(1)) from NaN/Inf poison
    // (gradMax spikes / non-finite) when the GPU solver diverges off a converged floor. gradScan_ is a
    // reused host scratch for the Gacc readback so the per-pass GetData doesn't churn a 7 MB alloc.
    float          gpuLastGradMax_{0.0f};
    bool           gpuLastGradFinite_{true};
    // Event-gated divergence detector over gpuLastGradMax_ (the machine-readable signal that replaced the
    // removed per-pass [Train] gradMax line). gpuGradEma_ is a slow EMA baseline; a spike is gradMax >
    // K×baseline, edge-triggered via gpuGradSpiking_ so sustained divergence logs once, not every pass.
    float          gpuGradEma_{0.0f};        ///< Slow EMA baseline of gradMax (0 = unseeded).
    bool           gpuGradSpiking_{false};   ///< Latch: true while gradMax stays above the spike/ceil threshold.
    // Live-tunable detector knobs (via /tune gradk|gradema|gradceil — dial against real traces, no recompile):
    float          gradSpikeK_{8.0f};        ///< /tune gradk: relative spike when gradMax > this × EMA baseline.
    float          gradEmaAlpha_{0.1f};      ///< /tune gradema: EMA smoothing of the gradMax baseline (lower = slower, resists ramp-tracking).
    float          gradCeil_{0.0f};          ///< /tune gradceil: absolute gradMax backstop (0 = off) — the slow-ramp catch the relative arm misses.
    Vector<float>  gradScan_;
    bool     gpuForceKick_{false};
    float    gpuKickFbase_{1.0f};
    unsigned gpuDelayMs_{5};                  ///< /rate readout; the pump advances per-tick so this no
                                              ///< longer throttles — kept for the command's get/set.
    /// Real corpus token sequences for GPU production training — built main-thread (BuildTokenSet)
    /// before the worker starts, then read-only/immutable during the run (worker reads lock-free).
    Vector<Vector<unsigned> > gpuSeqs_;

    // --- Background training worker (M2) ---
    std::shared_mutex trainMutex_; ///< Weight reader/writer lock: training compute + inference take
                                   ///< it SHARED (concurrent — keeps the UI responsive during a pass);
                                   ///< RestoreBest / Expand / GPU-sync take it EXCLUSIVE.
    std::mutex dbMutex_;           ///< Serialises SQLite access (worker batch select/record vs UI commands).
    YukiTrainWorker trainWorker_;  ///< Off-render-thread training loop.
    unsigned lastSeenPass_{0};     ///< Worker pass count seen by the previous HandleUpdate.
    unsigned lastSeenPbtKick_{0};  ///< Worker PBT-kick count seen by the previous HandleUpdate.
    unsigned lastSeenGpuKick_{0};  ///< GPU-worker difference-move count seen by the previous HandleUpdate.
    unsigned lastSeenGpuPass_{0};  ///< GPU-worker pass count seen by the previous HandleUpdate (graph feed).
    bool savedOnExit_{false};      ///< SaveOnExit() guard — the best is written once on exit.
    float statusTimer_{};          ///< Throttles the worker-observation/status block (~5 Hz), so the
                                   ///< UI thread isn't taking the shared mutex every frame.

    // --- Kick (plateau-escape diagnostic) ---
    int kickPassesLeft_{0};        ///< Train passes remaining in the active kick window (0 = none).
    float kickStartLoss_{0.0f};    ///< Smoothed loss captured at kick time, for reporting.
    int kickWindow_{50};           ///< Passes a kick gets to retrain before keep/rollback.
    bool autoKick_{false};         ///< Auto-fire one kick on plateau (one-shot; re-armed manually).
    bool ratchet_{true};           ///< Kick-until-first-rollback: keep kicking each plateau until one
                                   ///< fails to beat the elite (true floor found), then stop. DEFAULT ON:
                                   ///< with ceil 1.8 it bounds the GPU pump's per-batch noise + hard-batch
                                   ///< gradient spikes so the solver holds the converged floor.
    float ratchetSigma_{0.011f};   ///< Perturbation magnitude for auto/ratchet kicks (tuned default).
    float ratchetCeil_{1.8f};      ///< Ratchet pawl ceiling: on the GPU pump, restore .best when the probe
                                   ///< regresses past bestLoss*(1+ratchetCeil_). The missing "no ceiling"
                                   ///< clamp — capture alone let the resident weights drift up unbounded.
                                   ///< 0 disables the restore arm (pure capture). Tunable via /ceil.
    float gpuGradClip_{3.0f};      ///< Per-element gradient value-clip for the GPU Adam step (params[7]).
                                   ///< Tuned default 3 — above the honest ~0.7 baseline, below the hard-batch
                                   ///< spikes. 0 = disabled. Tames the intermittent hard-batch gradient
                                   ///< spikes that jolt the solver off the descent; clips before Adam. /gclip.

    // --- Escape harness: adaptive plateau-escape ladder (/escape). Semi-autonomous; opt-in; default OFF so
    //     normal training is untouched. On plateau it escalates through increasingly aggressive/diverse
    //     moves (all elite-protected — a failed move is rolled back to .best), banking any that lower the
    //     floor and declaring a HONEST hard-floor if the whole ladder fails N times. Drives the EXISTING
    //     primitives (DifferenceMoveSweep, LR restart, PBT); the ladder + escalation + verdict is the new part.
    void EscapeStep();                       ///< One plateau event: judge the last rung, escalate/bank, act.
    String EscapeExecuteRung(int rung);      ///< Run rung `rung`'s move; returns a human label for the log.
    bool escapeArmed_{false};                ///< /escape on. When armed it drives plateau-escape (over autokick).
    int  escapeRung_{-1};                    ///< Current ladder rung (-1 = ladder not started this cycle).
    int  escapeMaxRung_{4};                  ///< Top rung index (0..4: diffF1, diffF2, diffF3.5, LR-restart, PBT).
                                             ///< The PBT rung runs RunPbtKick (48 CPU passes) on a BACKGROUND
                                             ///< thread (EscapePbtWorker) — never the render thread — with the
                                             ///< GPU pump paused so the worker has exclusive model access.
    bool escapePbtRunning_{false};           ///< True while the background PBT rung is in flight (pump paused).
    int  escapeFailedLadders_{0};            ///< Consecutive full-ladder passes with no floor gain.
    int  escapeMaxFailedLadders_{2};         ///< Declare a hard floor after this many exhausted ladders.
    float escapeLastFloor_{1e30f};           ///< GetBestLoss() recorded before the last rung's move (to judge it).
    float escapeLrSaved_{0.0f};              ///< LR saved before an LR warm-restart rung, restored after the window.
    int  escapeLrRestoreIn_{0};              ///< Passes until the warm-restart LR is annealed back (0 = inactive).
    EscapePbtWorker escapePbtWorker_;        ///< Background thread for the PBT rung (off the render thread).
    int  escapePbtPollTicks_{0};             ///< Frames the PBT rung has been running (for a heartbeat log).

    // --- /ingest: bulk text importer (file / directory / http(s) URL) -> memories. RUN mode only (writes
    //     memories while the trainer is frozen). Files/dirs read synchronously; a URL is fetched by the
    //     `curl` CLI via SystemRunAsync (robust TLS + redirects, unlike civetweb), off-thread so network
    //     latency never blocks the render thread; completion arrives as an E_ASYNCEXECFINISHED event. ---
    bool IsSupportedTextExt(const String& path) const;       ///< .txt/.h/.cpp/... whitelist (HTML filtering later).
    void HandleAsyncExecFinished(StringHash eventType, VariantMap& eventData);  ///< curl-done: queue temp for ingest.
    void BeginIngest(const String& firstText, const Vector<String>& fileQueue, const String& label);  ///< start incremental ingest.
    void StepIngestProcess();                                ///< Chunk+INSERT a BOUNDED batch per frame (keeps UI responsive).
    unsigned ingestCurlReqId_{0};                            ///< SystemRunAsync request id of the in-flight curl (0 = none).
    String ingestTmpPath_;                                   ///< Temp file curl writes the response to.
    String ingestLabel_;                                     ///< memories.source tag for the current blob.
    bool ingesting_{false};                                  ///< True while a URL fetch is in progress (curl).
    bool asyncExecSubscribed_{false};                        ///< Subscribe to E_ASYNCEXECFINISHED once, lazily.
    // Incremental chunk+insert state (spread across frames so a big source never freezes the render thread).
    bool ingestProcessing_{false};                           ///< True while chunk+INSERT is in progress.
    String ingestText_;                                      ///< Current text blob being chunked.
    unsigned ingestPos_{0};                                  ///< Cursor into ingestText_.
    Vector<String> ingestFileQueue_;                         ///< Remaining files (directory ingest); one blob at a time.
    unsigned ingestChunkTotal_{0};                           ///< Running chunk count for the final report.

    // --- Karen telemetry (parasite on Yuki's existing 7879 server) ---
    float karenTimer_{};           ///< Throttles telemetry emission (~1 Hz); telemetry needs less than status.
    int memoryCount_{0};           ///< Live memory row count. Seeded pre-worker, kept current at Remember/forget.
                                   ///< All writers + the HandleUpdate reader are main-thread, so no lock needed.

    // --- Ears ---
    SharedPtr<YukiEars> ears_;

    // --- Input erosion ---
    String pendingInput_;       ///< Full input waiting to be consumed.
    String erodingText_;        ///< What's still visible in the input field.
    float erodeTimer_{};
    float erodeRate_{0.03f};    ///< Seconds per character eroded.
    bool eroding_{};
};

// YukiTrainer — Consume memories, refine the cartridge.
// Copyright (c) 2026 Urho3D project. License: MIT.

#include "../Precompiled.h"

#include "../ML/YukiTrainer.h"
#include "../ML/YukiTrainStep.h"
#include "../ML/YukiForwardCache.h"
#include "../ML/YukiLoss.h"
#include "../Database/DbConnection.h"
#include "../Database/DbResult.h"
#include "../Container/Sort.h"        // ledger priority ranking (was SQL ORDER BY)
#include "../Container/Pair.h"
#include "../IO/FileSystem.h"
#include "../IO/Log.h"
#include "../Core/Context.h"
#include "../Core/Timer.h"          // Time subsystem — throttle the .best disk write (persist-storm gate)
#include "../Core/ProcessUtils.h"   // GetNumLogicalCPUs

#include <cmath>
#include <cstring>
#include <thread>
#include <vector>

#include "../DebugNew.h"

namespace Urho3D
{

YukiTrainer::YukiTrainer(Context* context) :
    Object(context)
{
    // CPU pool size = logical cores minus one (leave a core for the render/UI thread). The
    // coordinator counts as one of these, so it + (trainThreads_-1) helpers = trainThreads_ total.
    const unsigned cores = GetNumLogicalCPUs();
    trainThreads_ = (cores > 1u) ? (cores - 1u) : 1u;
}

void YukiTrainer::SetModel(YukiModel* model)
{
    model_ = model;
    ResizeBuffers();
    if (model_ && model_->IsLoaded())
        bestPath_ = model_->GetPath() + ".best";
}

void YukiTrainer::ResizeBuffers()
{
    if (!model_ || !model_->IsLoaded())
        return;
    const YukiTopology& t = model_->GetTopology();
    // Paged buffers lift the 2^31 whole-buffer ceiling (see PagedFloatBuffer/YukiPaging). The
    // model store is paged too (same layout as these buffers), so the reduce/Adam address model
    // and buffers page-for-page. Configure fails ONLY if a SINGLE weight tensor exceeds 2^31
    // (would need intra-tensor paging — out of scope); guard that loudly.
    if (!gradients_.Configure(t.embedDim, t.nLayers, t.ffDim, t.vocabSize) ||
        !adamM_.Configure(t.embedDim, t.nLayers, t.ffDim, t.vocabSize) ||
        !adamV_.Configure(t.embedDim, t.nLayers, t.ffDim, t.vocabSize))
    {
        URHO3D_LOGERROR("YukiTrainer: a single weight tensor exceeds the 2^31 page ceiling — "
            "buffers NOT configured, training disabled");
        return;
    }
    gradHidden_.Resize(t.embedDim);
    // Per-worker private gradient buffers for the data-parallel pool (one per compute thread).
    workerGrads_.Resize(trainThreads_);
    for (unsigned w = 0; w < trainThreads_; ++w)
        workerGrads_[w].Configure(t.embedDim, t.nLayers, t.ffDim, t.vocabSize);
    // Configure already zeroes the pages; ResetAdam clears the Adam timestep too.
    ResetAdam();
}

void YukiTrainer::ResetAdam()
{
    if (adamM_.IsConfigured())
        adamM_.Zero();
    if (adamV_.IsConfigured())
        adamV_.Zero();
    adamStep_ = 0;
}

void YukiTrainer::EnsureSchema()
{
    if (!memoryDb_)
        return;
    // Canonical table (no-op if it already exists).
    memoryDb_->Execute(
        "CREATE TABLE IF NOT EXISTS memories ("
        " id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " text TEXT NOT NULL,"
        " source TEXT DEFAULT '',"
        " status TEXT DEFAULT 'pending',"
        " created INTEGER DEFAULT (strftime('%s','now')),"
        " last_loss REAL DEFAULT 1e30,"
        " last_trained INTEGER DEFAULT 0,"
        " train_count INTEGER DEFAULT 0)");

    // Older DBs: add the reinforcement columns if missing (ADD COLUMN has no
    // IF NOT EXISTS). Existing rows get the column DEFAULT — a fresh memory thus
    // starts at last_loss=1e30, i.e. top priority until first reinforced.
    bool hasLoss = false, hasTrained = false, hasCount = false;
    DbResult info = memoryDb_->Execute("PRAGMA table_info(memories)");
    const Vector<VariantVector>& cols = info.GetRows();
    for (unsigned i = 0; i < cols.Size(); ++i)
    {
        String name = cols[i][1].GetString();   // table_info column 1 = name
        if (name == "last_loss") hasLoss = true;
        else if (name == "last_trained") hasTrained = true;
        else if (name == "train_count") hasCount = true;
    }
    if (!hasLoss)    memoryDb_->Execute("ALTER TABLE memories ADD COLUMN last_loss REAL DEFAULT 1e30");
    if (!hasTrained) memoryDb_->Execute("ALTER TABLE memories ADD COLUMN last_trained INTEGER DEFAULT 0");
    if (!hasCount)   memoryDb_->Execute("ALTER TABLE memories ADD COLUMN train_count INTEGER DEFAULT 0");
}

// ─── Training Pass ───────────────────────────────────────────────────────────

void YukiTrainer::SelectTrainingBatch(Vector<int>& ids, Vector<Vector<unsigned> >& seqs, unsigned& unknownsOut)
{
    // Verbatim extraction of TrainOnce's selection — the SINGLE source the CPU TrainOnce and the
    // GPU resident trainer both call, so the priority/staleness regime is identical. Bumps the
    // staleness clock, runs the priority query, tokenizes, retires <2-token memories.
    ids.Clear(); seqs.Clear(); unknownsOut = 0;
    if (!model_ || !model_->IsLoaded() || !memoryDb_)
        return;
    if (!schemaReady_)
    {
        EnsureSchema();
        schemaReady_ = true;
    }

    // Seed the staleness clock from the ledger once (LoadLedger pulls the last flushed state), so a
    // restart resumes the last persisted priority ordering rather than starting cold.
    if (!ledgerLoaded_)
        LoadLedger();
    ++trainPass_;

    // Rank the corpus by RAM priority (last_loss + staleness·(pass − last_trained)) and take the top
    // reinforceBatch_ — the old "ORDER BY … DESC LIMIT", moved off SQLite so no priority state is
    // written per pass. Only ids are read from the DB (cheap, no fsync); a memory absent from the
    // ledger ranks at top priority via LedgerFor's default row. Text is then fetched for just the batch.
    DbResult idr = memoryDb_->Execute("SELECT id FROM memories");
    const Vector<VariantVector>& idrows = idr.GetRows();
    if (idrows.Empty())
        return;

    Vector<Pair<float, int> > ranked;   // (priority, id)
    ranked.Reserve(idrows.Size());
    for (unsigned i = 0; i < idrows.Size(); ++i)
    {
        int id = idrows[i][0].GetI32();
        const LedgerEntry& e = LedgerFor(id);
        float prio = e.lastLoss + staleness_ * (float)(trainPass_ - e.lastTrained);
        ranked.Push(MakePair(prio, id));
    }
    // Highest priority first (ties are irrelevant to a training pick).
    Sort(ranked.Begin(), ranked.End(),
        [](const Pair<float, int>& a, const Pair<float, int>& b) { return a.first_ > b.first_; });

    const unsigned take = reinforceBatch_ < ranked.Size() ? reinforceBatch_ : ranked.Size();
    String idList;
    for (unsigned i = 0; i < take; ++i)
    {
        if (i) idList += ",";
        idList += String(ranked[i].second_);
    }
    DbResult result = memoryDb_->Execute("SELECT id, text FROM memories WHERE id IN (" + idList + ")");
    const Vector<VariantVector>& rows = result.GetRows();
    if (rows.Empty())
        return;

    const YukiTopology& t = model_->GetTopology();
    for (unsigned r = 0; r < rows.Size(); ++r)
    {
        int id = rows[r][0].GetI32();
        String text = rows[r][1].GetString();

        // Tokenize
        Vector<String> words = YukiModel::Tokenize(text);
        Vector<unsigned> tokens;
        unsigned unknowns = 0;
        for (const String& word : words)
        {
            String clean = word.Trimmed();
            if (clean.Empty())
                continue;
            unsigned idx = model_->GetTokenIndex(clean);
            if (idx >= t.vocabSize)
            {
                idx = 1;  // <unk>
                unknowns++;
            }
            tokens.Push(idx);
        }
        if (tokens.Size() > t.maxSeqLen)
            tokens.Resize(t.maxSeqLen);
        if (tokens.Size() < 2)
        {
            // Untrainable (<2 tokens) — retire from priority without deleting. RAM ledger only
            // (last_loss=0 sinks it to the bottom of the ranking); persisted on the next FlushLedger.
            LedgerEntry& le = LedgerFor(id);
            le.lastLoss = 0.0f;
            le.lastTrained = trainPass_;
            ledgerDirty_ = true;
            continue;
        }
        unknownsOut += unknowns;
        totalUnknowns_ += unknowns;
        totalTokens_ += tokens.Size();
        ids.Push(id);
        seqs.Push(tokens);
    }
}

void YukiTrainer::RecordTrainResult(int id, float loss)
{
    // The freshly measured error + staleness stamp that drives which memories are picked next pass.
    // RAM ledger only — no disk write. Persisted in bulk by FlushLedger on the checkpoint cadence.
    LedgerEntry& e = LedgerFor(id);
    e.lastLoss = loss;
    e.lastTrained = trainPass_;
    ++e.trainCount;
    ledgerDirty_ = true;
}

void YukiTrainer::RecordTrainResults(const Vector<int>& ids, const Vector<float>& losses)
{
    // A whole pass's writebacks into the RAM ledger — the per-pass SQLite commit/fsync is gone. This
    // was the disk-write storm: one fsync per pass for state that is pure scheduler scratch. The
    // ledger now reaches disk only via FlushLedger (checkpoint cadence + teardown). Index-aligned.
    if (ids.Empty())
        return;
    const unsigned n = ids.Size() < losses.Size() ? ids.Size() : losses.Size();
    for (unsigned i = 0; i < n; ++i)
    {
        LedgerEntry& e = LedgerFor(ids[i]);
        e.lastLoss = losses[i];
        e.lastTrained = trainPass_;
        ++e.trainCount;
    }
    if (n)
        ledgerDirty_ = true;
}

// ─── In-RAM priority ledger (persisted only on cadence, never per pass) ──────────

YukiTrainer::LedgerEntry& YukiTrainer::LedgerFor(int id)
{
    return ledger_[id];   // HashMap::operator[] inserts a default LedgerEntry for a new id.
}

void YukiTrainer::LoadLedger()
{
    // One-time hydrate of the RAM ledger from the durable columns, and seed the staleness clock from
    // max(last_trained) so a restart resumes the last flushed priority ordering. Reads only (no fsync).
    ledgerLoaded_ = true;
    ledger_.Clear();
    int maxTrained = 0;
    if (memoryDb_)
    {
        if (!schemaReady_) { EnsureSchema(); schemaReady_ = true; }
        DbResult r = memoryDb_->Execute(
            "SELECT id, COALESCE(last_loss, 1e30), COALESCE(last_trained, 0), "
            "COALESCE(train_count, 0) FROM memories");
        const Vector<VariantVector>& rows = r.GetRows();
        for (unsigned i = 0; i < rows.Size(); ++i)
        {
            const int id = rows[i][0].GetI32();
            LedgerEntry& e = ledger_[id];
            e.lastLoss = rows[i][1].GetFloat();
            e.lastTrained = rows[i][2].GetI32();
            e.trainCount = (unsigned)rows[i][3].GetI32();
            if (e.lastTrained > maxTrained)
                maxTrained = e.lastTrained;
        }
    }
    if (trainPass_ < 0)
        trainPass_ = maxTrained;
    ledgerDirty_ = false;   // just loaded — RAM matches disk
}

void YukiTrainer::ResetLedgerPriority()
{
    // RAM analogue of "UPDATE memories SET last_loss = 1e30" — re-float every memory to top priority
    // after a kick / weight jump so the next passes re-measure. Flushed with the next FlushLedger.
    if (!ledgerLoaded_)
        LoadLedger();
    for (HashMap<int, LedgerEntry>::Iterator it = ledger_.Begin(); it != ledger_.End(); ++it)
        it->second_.lastLoss = 1e30f;
    ledgerDirty_ = true;
}

void YukiTrainer::FlushLedger()
{
    // The ONLY disk-write path for the ledger: RAM → SQLite in one BEGIN/COMMIT (one commit/fsync),
    // and only when something changed. Called on the checkpoint cadence + at teardown, never in the
    // hot loop. DbLock serialises the connection vs the UI command handler (matches other write paths).
    if (!memoryDb_ || !ledgerDirty_ || ledger_.Empty())
        return;
    auto dlock = DbLock();
    memoryDb_->Execute("BEGIN");
    for (HashMap<int, LedgerEntry>::ConstIterator it = ledger_.Begin(); it != ledger_.End(); ++it)
    {
        const LedgerEntry& e = it->second_;
        memoryDb_->Execute("UPDATE memories SET last_loss = " + String(e.lastLoss)
            + ", last_trained = " + String(e.lastTrained)
            + ", train_count = " + String(e.trainCount)
            + " WHERE id = " + String(it->first_));
    }
    memoryDb_->Execute("COMMIT");
    ledgerDirty_ = false;
}

void YukiTrainer::UpdatePlateau(float passLoss)
{
    // Verbatim extraction of TrainOnce's smoothed plateau tracking — the EMA + stagnation counter
    // that IsPlateaued() reads. Shared so the GPU resident trainer feeds the SAME tracker.
    lastLoss_ = passLoss;
    if (lossEMA_ <= 0.0f)
        lossEMA_ = passLoss;
    else
        lossEMA_ = emaAlpha_ * passLoss + (1.0f - emaAlpha_) * lossEMA_;

    if (lossEMA_ < bestEMA_ - plateauDelta_)
    {
        bestEMA_ = lossEMA_;
        stagnation_ = 0;
    }
    else
        ++stagnation_;
}

void YukiTrainer::WorkerProcessShare(unsigned index, const Vector<Vector<unsigned> >& seqs)
{
    // One pool thread's share of a pass. Build dims locally (cheap; keeps YukiDims out of the
    // header), then atomically claim disjoint sequences and accumulate each into THIS worker's
    // private gradient (zeroed by the coordinator before fan-out). Lock-free: only shared reads
    // are the pass-start weights; all writes are to disjoint buffers/indices.
    if (!model_ || !model_->IsLoaded())
        return;
    const YukiTopology& t = model_->GetTopology();
    YukiMath::YukiDims dims;
    dims.embedDim = t.embedDim; dims.nLayers = t.nLayers; dims.nHeads = t.nHeads;
    dims.ffDim = t.ffDim; dims.vocabSize = t.vocabSize; dims.maxSeqLen = t.maxSeqLen;

    // Build the views ONCE per share: weight views from the paged model store, grad views from
    // THIS worker's paged buffer. The backward accumulates through the grad views across every
    // claimed sequence.
    YukiMath::YukiModelPtrs w{};
    std::vector<YukiMath::YukiLayerPtrs> wl(t.nLayers);
    model_->GetWeights().BuildModelPtrs(dims, w, wl.data());

    PagedFloatBuffer& g = workerGrads_[index];
    YukiMath::YukiModelGrads gg{};
    std::vector<YukiMath::YukiLayerGrads> gl(t.nLayers);
    g.BuildModelGrads(dims, gg, gl.data());

    const unsigned count = seqs.Size();
    for (;;)
    {
        const unsigned i = nextSeq_.fetch_add(1u, std::memory_order_relaxed);
        if (i >= count)
            break;
        const Vector<unsigned>& tokens = seqs[i];
        const float loss = YukiMath::YukiSequenceForwardBackward(w, wl.data(), gg, gl.data(),
            dims, tokens.Buffer(), tokens.Size());
        lossOut_[i] = loss;   // disjoint index — no contention with other workers
    }
}

void YukiTrainer::WorkerApplyUpdate(unsigned sliceIdx, unsigned nSlices, unsigned nWorkers,
                                    unsigned long long cnt, float invK)
{
    // Phase B: this worker's param slice. Partition [0,cnt) into nSlices contiguous ranges — 64-bit
    // math so sliceIdx*cnt can't overflow. Empty slice (cnt < nSlices at the tail) is a no-op.
    // nSlices is the fan-out width (T when parallel, 1 when serial); nWorkers is the number of phase-A
    // gradient buffers to reduce (always the phase-A pool width) — decoupled so the serial path (one
    // slice) still sums across EVERY worker's gradient.
    const unsigned long long lo = (cnt * (unsigned long long)sliceIdx) / nSlices;
    const unsigned long long hi = (cnt * (unsigned long long)(sliceIdx + 1)) / nSlices;
    if (hi <= lo)
        return;

    // Fused reduce+Adam over the flat param range [lo,hi), processed in PAGE-CLIPPED chunks.
    // The model store and all paged buffers (gradients_/adamM_/adamV_/workerGrads_) share one page
    // directory (Configure'd from the same topology + stride), so a single (page p, offset off)
    // addresses them ALL identically — including the model weights now that it's paged too. Same
    // arithmetic as before, restricted to this worker's disjoint slice.
    PagedFloatBuffer& modelW = model_->GetWeights();
    for (unsigned p = 0; p < gradients_.PageCount(); ++p)
    {
        const unsigned long long pb = gradients_.PageBase(p);
        const unsigned long long pe = pb + gradients_.PageElems(p);
        const unsigned long long cs = lo > pb ? lo : pb;
        const unsigned long long ce = hi < pe ? hi : pe;
        if (ce <= cs)
            continue;   // this page doesn't intersect [lo,hi)
        const unsigned off = (unsigned)(cs - pb);
        const unsigned n = (unsigned)(ce - cs);

        // Reduce: seed from worker 0, sum the rest, scale to the batch mean.
        float* g = gradients_.PageData(p) + off;
        const float* g0 = workerGrads_[0].PageData(p) + off;
        for (unsigned i = 0; i < n; ++i)
            g[i] = g0[i];
        for (unsigned wkr = 1; wkr < nWorkers; ++wkr)
        {
            const float* gw = workerGrads_[wkr].PageData(p) + off;
            for (unsigned i = 0; i < n; ++i)
                g[i] += gw[i];
        }
        for (unsigned i = 0; i < n; ++i)
            g[i] *= invK;

        // Adam on this chunk. adamStep_ is the pass-wide scalar (bumped once before fan-out), so
        // the bias correction is identical to a single full-vector step.
        YukiMath::AdamStep(modelW.PageData(p) + off, g,
            adamM_.PageData(p) + off, adamV_.PageData(p) + off, adamStep_,
            learningRate_, beta1_, beta2_, adamEps_, n);
    }
}

YukiTrainStats YukiTrainer::TrainOnce()
{
    YukiTrainStats stats;

    if (!model_ || !model_->IsLoaded() || !memoryDb_)
        return stats;

    // Selection + staleness clock + tokenize + <2-token retire — the shared regime entry point
    // (also used by the GPU resident trainer). Returns the trainable batch in ids/seqs.
    Vector<int> ids;
    Vector<Vector<unsigned> > seqs;
    { auto dlock = DbLock(); SelectTrainingBatch(ids, seqs, stats.unknowns); }   // DB read — serialise vs UI commands
    if (ids.Empty())
        return stats;

    const YukiTopology& t = model_->GetTopology();
    const unsigned long long count = t.TotalWeights();
    float totalLoss = 0.0f;
    // DATA-PARALLEL MINIBATCH, two parallel phases over the SAME (cores-1) pool with one barrier:
    //   Phase A (sequence axis): each thread atomically claims disjoint sequences and accumulates
    //     forward+backward into its OWN private gradient buffer — all reading the SAME pass-start
    //     weights, so no worker can perturb another's gradient. Barrier (join) before phase B.
    //   Phase B (parameter axis): each thread owns a disjoint param slice and does the fused
    //     reduce+Adam on it — sum the slice across all workerGrads_, batch-mean, one Adam step.
    // Both the reduce and the Adam step are now parallel; no single-threaded remainder except the
    // cheap probe/ratchet. Threads are spawned per phase and joined within this scope — nothing
    // outlives it (no persistent pool, no teardown races).
    const unsigned K = ids.Size();
    const unsigned T = trainThreads_;
    lossOut_.Resize(K);
    nextSeq_.store(0u, std::memory_order_relaxed);
    for (unsigned w = 0; w < T; ++w)
        workerGrads_[w].Zero();

    // SHARED weight lock spans the whole compute + reduce + Adam + probe: inference (also SHARED)
    // runs concurrently so the UI stays responsive, while RestoreBest / Expand (EXCLUSIVE) wait
    // for the pass. The spawned helpers read weights under THIS lock (the coordinator holds it for
    // the pass's duration), so an exclusive writer can never race a mid-flight gradient.
    {
        auto wlock = WeightShared();

        std::vector<std::thread> helpers;
        helpers.reserve(T > 0 ? T - 1 : 0);
        for (unsigned w = 1; w < T; ++w)
            helpers.emplace_back([this, w, &seqs]() { WorkerProcessShare(w, seqs); });
        WorkerProcessShare(0, seqs);   // coordinator runs share 0 alongside the helpers
        for (std::thread& th : helpers)
            th.join();

        stats.samples = K;

        // PHASE B — fused reduce+Adam over the PARAMETER axis (phase A above ran over the SEQUENCE
        // axis). Bump the Adam timestep ONCE here so every slice shares the same scalar bias
        // correction, then fan the param slices across the same pool: each worker sums its slice
        // across all workerGrads_, batch-means it, and Adam-steps it in place. A real topology is
        // millions of weights, so parallel always pays; the only serial case is a single-thread pool.
        ++adamStep_;
        const float invK = 1.0f / (float)K;
        if (T <= 1)
        {
            WorkerApplyUpdate(0, 1, T, count, invK);   // one slice over [0,cnt), sums all T workers
        }
        else
        {
            std::vector<std::thread> upd;
            upd.reserve(T - 1);
            for (unsigned s = 1; s < T; ++s)
                upd.emplace_back([this, s, T, count, invK]() { WorkerApplyUpdate(s, T, T, count, invK); });
            WorkerApplyUpdate(0, T, T, count, invK);   // coordinator runs slice 0 alongside the helpers
            for (std::thread& th : upd)
                th.join();
        }

        // Loss aggregate + DB writeback — coordinator only; SQLite serialised by dbMutex_ (nested
        // weight->db ordering, matching the UI command handler, so the two can never deadlock).
        {
            auto dlock = DbLock();
            for (unsigned r = 0; r < K; ++r)
            {
                totalLoss += lossOut_[r];
                RecordTrainResult(ids[r], lossOut_[r]);
            }
        }
        stats.loss = totalLoss / (float)K;
        UpdatePlateau(stats.loss);   // EMA + stagnation tracking (shared with the GPU trainer)

        // Elite tracking on the fixed probe (reads weights — under the SHARED lock; cadence-limited).
        // bestLoss_ is seeded from the persisted elite at startup, so this compares against the REAL
        // best — a worse state can't overwrite it. (No eager-first-pass capture: that clobbered it.)
        if (evalEvery_ > 0 && (trainPass_ % evalEvery_) == 0)
        {
            MaybeResampleProbe();
            if (RatchetProbe())   // probe-eval + capture-if-beats-floor (single decision source)
                FlushLedger();    // breakthrough only: piggyback the RAM priority ledger on a new-best (.best) write
        }
    }

    // Trailing best-flush (CPU cadence): land a throttled-out .best that CaptureBest deferred. Runs every
    // pass, INDEPENDENT of a new best, so a CONVERGED best (no further CaptureBest) still reaches disk instead
    // of dying on an unclean exit. No-op unless dirty AND the throttle interval has elapsed, so it does NOT
    // thrash the disk. Same worker thread as CaptureBest (no new lock); GPU mode gets the analog in the pump.
    MaybeFlushBestToDisk();

    // Persistence is decoupled from the pass rate: the training worker saves the
    // working cartridge on its own cadence (and CaptureBest persists .best on a new
    // best). Saving every pass here would thrash the disk at the worker's rate.

    URHO3D_LOGDEBUG("YukiTrainer: reinforced " + String(stats.samples) + " memories, loss=" +
        String(stats.loss, 4));

    // Check if expansion is needed. Expand reallocates the model + grad/Adam buffers, so it takes
    // the weight lock EXCLUSIVE (outside the SHARED compute scope above) — no reader/inference can
    // be mid-flight on the old buffers while they're replaced.
    if (NeedsExpansion())
    {
        auto wexcl = WeightExclusive();
        URHO3D_LOGINFO("YukiTrainer: Expansion triggered");
        if (Expand())
        {
            stats.expanded = true;
            ResizeBuffers();   // topology changed → grad/Adam buffers are stale
        }
    }

    return stats;
}

// ─── Elite caching + kick (perturb / elite-rollback) ─────────────────────────

void YukiTrainer::BuildProbe()
{
    probeIds_.Clear();
    if (!memoryDb_)
        return;
    // Frozen random sample, capped — constant eval cost regardless of corpus size.
    DbResult r = memoryDb_->Execute("SELECT id FROM memories ORDER BY RANDOM() LIMIT "
        + String(probeCap_));
    const Vector<VariantVector>& rows = r.GetRows();
    for (unsigned i = 0; i < rows.Size(); ++i)
        probeIds_.Push(rows[i][0].GetI32());

    DbResult c = memoryDb_->Execute("SELECT COUNT(*) FROM memories");
    const Vector<VariantVector>& cr = c.GetRows();
    probeBuiltAtCount_ = (!cr.Empty()) ? (unsigned)cr[0][0].GetI32() : probeIds_.Size();
    probeReady_ = true;
}

bool YukiTrainer::RatchetProbe()
{
    // Verbatim extraction of the TrainOnce elite-ratchet decision — the SINGLE source both the
    // CPU TrainOnce and the GPU resident mode call, so the floor decision is identical. Caller
    // owns the cadence + any probe resample (MaybeResampleProbe).
    float probeLoss = EvalProbeLoss(model_->GetWeights());
    if (probeLoss >= 0.0f && probeLoss < bestLoss_)
    {
        CaptureBest(probeLoss);
        return true;
    }
    return false;
}

int YukiTrainer::RatchetGuard(float tol)
{
    // The pawl RatchetProbe never had. Capture on improvement (same decision), but ALSO restore when
    // the probe regresses past the ceiling bestLoss*(1+tol) — without this the resident/live weights
    // drift upward unbounded ("loss climbs with no ceiling") because capture-only does nothing on a
    // regression. bestLoss_ starts at 1e30 (no elite yet) so the ceiling is unreachable until a floor
    // exists, and RestoreBest is a no-op with an empty snapshot — both safe on a cold start.
    if (!model_)
        return 0;
    const float probeLoss = EvalProbeLoss(model_->GetWeights());
    if (probeLoss < 0.0f)
        return 0;   // couldn't score — make no decision
    if (probeLoss < bestLoss_)
    {
        CaptureBest(probeLoss);
        return 1;
    }
    if (tol >= 0.0f && probeLoss > bestLoss_ * (1.0f + tol))
    {
        RestoreBest();   // yank the live model back to the elite floor + reset Adam (RAM only — no disk write; elite already on .best)
        return -1;
    }
    return 0;            // within the tolerance band — allow the uphill exploration
}

unsigned YukiTrainer::CollectProbeSeqs(Vector<Vector<unsigned> >& seqs, YukiMath::YukiDims& dims)
{
    // The frozen probe as token sequences (model_ supplies vocab + topology only). No
    // bestLoss_ side-effects — the shared collection path for both EvalProbeLoss overloads.
    seqs.Clear();
    if (!model_ || !model_->IsLoaded() || !memoryDb_)
        return 0;
    if (!probeReady_)
        BuildProbe();
    if (probeIds_.Empty())
        return 0;

    const YukiTopology& t = model_->GetTopology();
    dims.embedDim = t.embedDim; dims.nLayers = t.nLayers; dims.nHeads = t.nHeads;
    dims.ffDim = t.ffDim; dims.vocabSize = t.vocabSize; dims.maxSeqLen = t.maxSeqLen;

    String idList;
    for (unsigned i = 0; i < probeIds_.Size(); ++i)
        idList += (i ? "," : "") + String(probeIds_[i]);
    DbResult r = memoryDb_->Execute("SELECT text FROM memories WHERE id IN (" + idList + ")");
    const Vector<VariantVector>& rows = r.GetRows();

    for (unsigned i = 0; i < rows.Size(); ++i)
    {
        Vector<String> words = YukiModel::Tokenize(rows[i][0].GetString());
        Vector<unsigned> tokens;
        for (const String& word : words)
        {
            String clean = word.Trimmed();
            if (clean.Empty())
                continue;
            unsigned idx = model_->GetTokenIndex(clean);
            if (idx >= t.vocabSize)
                idx = 1;   // <unk>
            tokens.Push(idx);
        }
        if (tokens.Size() > t.maxSeqLen)
            tokens.Resize(t.maxSeqLen);
        if (tokens.Size() < 2)
            continue;
        seqs.Push(tokens);
    }
    return seqs.Size();
}

float YukiTrainer::EvalProbeLoss(const float* weights)
{
    if (!weights)
        return -1.0f;
    Vector<Vector<unsigned> > seqs;
    YukiMath::YukiDims dims;
    if (CollectProbeSeqs(seqs, dims) == 0)
        return -1.0f;
    float total = 0.0f;
    for (unsigned i = 0; i < seqs.Size(); ++i)
        total += YukiMath::YukiSequenceLoss(weights, dims, seqs[i].Buffer(), seqs[i].Size());
    return total / (float)seqs.Size();
}

float YukiTrainer::EvalProbeLoss(const PagedFloatBuffer& weights)
{
    if (!weights.IsConfigured())
        return -1.0f;
    Vector<Vector<unsigned> > seqs;
    YukiMath::YukiDims dims;
    if (CollectProbeSeqs(seqs, dims) == 0)
        return -1.0f;
    // Build the weight views ONCE (weights fixed), then score every probe sequence through
    // the view-taking loss — the paged analogue of the flat overload above.
    YukiMath::YukiModelPtrs w{};
    std::vector<YukiMath::YukiLayerPtrs> wl(dims.nLayers);
    weights.BuildModelPtrs(dims, w, wl.data());
    float total = 0.0f;
    for (unsigned i = 0; i < seqs.Size(); ++i)
        total += YukiMath::YukiSequenceLoss(w, dims, seqs[i].Buffer(), seqs[i].Size());
    return total / (float)seqs.Size();
}

float YukiTrainer::ComputeBatchGradient(const Vector<Vector<unsigned> >& seqs, PagedFloatBuffer& out)
{
    if (!model_ || !model_->IsLoaded() || seqs.Empty())
        return -1.0f;
    const YukiTopology& t = model_->GetTopology();
    YukiMath::YukiDims dims;
    dims.embedDim = t.embedDim; dims.nLayers = t.nLayers; dims.nHeads = t.nHeads;
    dims.ffDim = t.ffDim; dims.vocabSize = t.vocabSize; dims.maxSeqLen = t.maxSeqLen;

    if (!out.IsConfigured() || out.Size() != t.TotalWeights())
        out.Configure(t.embedDim, t.nLayers, t.ffDim, t.vocabSize);
    out.Zero();

    // Weight views from the live model; SUMMED grad views into `out`. No Adam, no weight change —
    // this is exactly the CPU reference for the GPU Gacc (which is also a sum over the batch).
    YukiMath::YukiModelPtrs w{};
    std::vector<YukiMath::YukiLayerPtrs> wl(t.nLayers);
    model_->GetWeights().BuildModelPtrs(dims, w, wl.data());
    YukiMath::YukiModelGrads g{};
    std::vector<YukiMath::YukiLayerGrads> gl(t.nLayers);
    out.BuildModelGrads(dims, g, gl.data());

    for (unsigned s = 0; s < seqs.Size(); ++s)
        if (seqs[s].Size() >= 2)
            YukiMath::YukiSequenceForwardBackward(w, wl.data(), g, gl.data(),
                dims, seqs[s].Buffer(), seqs[s].Size());

    // Max |grad| element across all pages.
    float mx = 0.0f;
    for (unsigned p = 0; p < out.PageCount(); ++p)
    {
        const float* d = out.PageData(p);
        const unsigned n = out.PageElems(p);
        for (unsigned i = 0; i < n; ++i) { float a = d[i] < 0 ? -d[i] : d[i]; if (a > mx) mx = a; }
    }
    return mx;
}

void YukiTrainer::MaybeResampleProbe()
{
    if (!memoryDb_)
        return;
    if (!probeReady_)
    {
        BuildProbe();   // first build; bestLoss_ stays as LoadElite seeded it (or 1e30)
        return;
    }
    DbResult c = memoryDb_->Execute("SELECT COUNT(*) FROM memories");
    const Vector<VariantVector>& cr = c.GetRows();
    unsigned now = (!cr.Empty()) ? (unsigned)cr[0][0].GetI32() : probeBuiltAtCount_;
    if (probeBuiltAtCount_ > 0 && now > probeBuiltAtCount_ + probeBuiltAtCount_ / 2)
    {
        BuildProbe();   // new probe — old bestLoss_ isn't comparable to it
        // Re-baseline by RE-EVALUATING the elite on the new probe, NOT a blind reset
        // to 1e30 (that reset is what let a worse state overwrite the elite).
        if (bestWeights_.IsConfigured())
        {
            float v = EvalProbeLoss(bestWeights_);
            bestLoss_ = (v >= 0.0f) ? v : 1e30f;
        }
        else
            bestLoss_ = 1e30f;
    }
}

void YukiTrainer::LoadElite()
{
    // Read the persisted elite (.best) into the in-RAM elite and seed bestLoss_ by
    // EVALUATING it on the current probe. Without this, bestLoss_ starts at 1e30 and
    // the first capture overwrites .best with a worse state — the bug that lost the
    // hard-won floor. No-op if no .best or it doesn't match the current topology.
    if (!model_ || !model_->IsLoaded())
        return;
    if (bestPath_.Empty())
        bestPath_ = model_->GetPath() + ".best";

    auto* fs = GetSubsystem<FileSystem>();
    if (!fs || !fs->FileExists(bestPath_))
        return;

    SharedPtr<YukiModel> elite(new YukiModel(context_));
    if (!elite->Load(bestPath_) || !elite->IsLoaded())
        return;

    const unsigned long long n = model_->GetTopology().TotalWeights();
    if (elite->GetTopology().TotalWeights() != n)
    {
        URHO3D_LOGWARNING("YukiTrainer::LoadElite: .best topology differs — skipping (stale elite)");
        return;
    }

    const YukiTopology& t = model_->GetTopology();
    bestWeights_.Configure(t.embedDim, t.nLayers, t.ffDim, t.vocabSize);
    bestWeights_.CopyFrom(elite->GetWeights());   // both paged, same layout

    float v = EvalProbeLoss(bestWeights_);
    if (v >= 0.0f)
    {
        bestLoss_ = v;
        PushCheckpoint(bestWeights_);   // seed the lineage with the restored elite
        URHO3D_LOGINFOF("YukiTrainer::LoadElite: restored elite, floor=%.4f", bestLoss_);
    }
    else
    {
        // Couldn't establish a comparable loss → don't keep a half-loaded elite;
        // a loaded elite with bestLoss_=1e30 would just be clobbered again.
        bestWeights_.Clear();
    }
}

void YukiTrainer::CaptureBest(float loss)
{
    if (!model_ || !model_->IsLoaded())
        return;
    const YukiTopology& t = model_->GetTopology();
    bestWeights_.Configure(t.embedDim, t.nLayers, t.ffDim, t.vocabSize);
    bestWeights_.CopyFrom(model_->GetWeights());   // both paged, same layout
    bestLoss_ = loss;
    // Record this best in the lineage ring so difference moves can extrapolate the
    // trajectory of bests (the temporal "population of one").
    PushCheckpoint(bestWeights_);
    if (bestPath_.Empty())
        bestPath_ = model_->GetPath() + ".best";

    // Throttle the .best DISK write to kill the early-training persist storm: the floor breaks almost every
    // pass, and a full-model atomic write per break saturates disk I/O. The RAM elite above is ALWAYS current
    // (the floor is never lost in memory). On a throttle-skip we mark bestDirty_ so MaybeFlushBestToDisk (run
    // from the pass cadence, INDEPENDENT of a new best) still lands the write on CONVERGENCE — where no new
    // best arrives and the deferred write would otherwise never happen, losing that best on an unclean exit.
    auto* timeSub = GetSubsystem<Time>();
    const float nowSec = timeSub ? timeSub->GetElapsedTime() : 0.0f;
    // null timeSub -> write (don't throttle): a broken clock must never wedge the .best write off forever.
    if (timeSub && lastBestDiskSave_ > 0.0f && (nowSec - lastBestDiskSave_) < bestSaveIntervalSec_)
    {
        bestDirty_ = true;   // newer best in RAM, not yet on disk; the trailing flush will carry it
        return;
    }
    if (!WriteBestToDisk(nowSec))
        bestDirty_ = true;   // write failed -> keep dirty so the trailing flush retries
}

// Atomically write the RAM elite (bestWeights_) to .best: temp then rename, so a mid-write crash never
// corrupts the prior elite. NON-destructive (SaveWeightsAs, not RestoreBest+Save) so it is safe mid-training.
// On success stamps lastBestDiskSave_ and clears bestDirty_.
bool YukiTrainer::WriteBestToDisk(float nowSec)
{
    if (!model_ || bestPath_.Empty() || !bestWeights_.IsConfigured())
        return false;
    // Topology guard (mirrors RestoreBest): after an Expand, header_ is NEW-topology but bestWeights_ may
    // still hold OLD-topology pages until the next post-expand CaptureBest reconfigures the elite. SaveWeightsAs
    // writes the live header_ + these pages, so writing now pairs a new header with old pages -> truncated/
    // corrupt .best. Bail WITHOUT stamping (keep dirty, no backoff) so it lands the instant the elite re-syncs;
    // the size check is cheap, so retrying every pass through the expansion window costs nothing.
    if (bestWeights_.Size() != model_->GetTopology().TotalWeights())
        return false;
    // Stamp on ATTEMPT, not just success: on a persistently failing disk this bounds retries to 1/interval
    // instead of every pass (which would re-storm exactly when the disk is already sick). bestDirty_ stays
    // true on failure, so the write is still retried — just throttled (coder2 nit 1).
    lastBestDiskSave_ = nowSec;
    const String tmp = bestPath_ + ".tmp";
    auto* fs = GetSubsystem<FileSystem>();
    if (model_->SaveWeightsAs(bestWeights_, tmp) && fs && fs->Rename(tmp, bestPath_))
    {
        bestDirty_ = false;
        return true;
    }
    if (fs && fs->FileExists(tmp))   // save or rename failed -> leave the existing .best intact
        fs->Delete(tmp);
    return false;
}

// Trailing flush: land a throttled-out best that would otherwise sit RAM-only until (and be lost by) an
// unclean exit. Called from the training pass cadence — independent of a new best, so it fires on
// convergence/plateau where CaptureBest never would. Bounds .best staleness to bestSaveIntervalSec_ + one pass.
void YukiTrainer::MaybeFlushBestToDisk()
{
    if (!bestDirty_)
        return;
    auto* timeSub = GetSubsystem<Time>();
    const float nowSec = timeSub ? timeSub->GetElapsedTime() : 0.0f;
    if (timeSub && lastBestDiskSave_ > 0.0f && (nowSec - lastBestDiskSave_) < bestSaveIntervalSec_)
        return;   // not yet due
    WriteBestToDisk(nowSec);   // writes the RAM elite non-destructively; clears dirty on success
}

void YukiTrainer::RestoreBest()
{
    if (!model_ || !bestWeights_.IsConfigured())
        return;
    const unsigned long long n = model_->GetTopology().TotalWeights();
    if (bestWeights_.Size() != n)
        return;   // topology changed — snapshot no longer valid
    model_->GetWeights().CopyFrom(bestWeights_);   // both paged, same layout
    ResetAdam();
    // No disk write here: a restore is a RAM-only rollback to the elite after
    // floor noise, NOT a new floor. Persisting the whole model on every
    // noise-induced rollback was the GPU-mode disk storm — RestoreBest fires on
    // each ceiling breach, and the GPU pump is fast and noisy right at the floor.
    // Only an actual floor breakthrough writes to disk (CaptureBest -> .best),
    // which is already the durable elite a reload resumes from; the working
    // cartridge's recency is persisted on teardown via SaveOnExit.
}

void YukiTrainer::Perturb(float sigma)
{
    if (!model_ || !model_->IsLoaded() || sigma <= 0.0f)
        return;

    PagedFloatBuffer& weights = model_->GetWeights();
    const unsigned long long n = model_->GetTopology().TotalWeights();
    if (n == 0)
        return;

    // Scale the noise to the RMS of the weight vector so the perturbation is
    // relative, not absolute (small weights get small kicks).
    double sumsq = 0.0;
    for (unsigned p = 0; p < weights.PageCount(); ++p)
    {
        const float* wp = weights.PageData(p);
        const unsigned nn = weights.PageElems(p);
        for (unsigned i = 0; i < nn; ++i)
            sumsq += (double)wp[i] * (double)wp[i];
    }
    const float rms = (float)sqrt(sumsq / (double)n);
    const float scale = sigma * rms;

    // Gaussian noise via Box-Muller, driven by the same LCG family used to initialise a fresh
    // cartridge. Page-by-page — pages tile [0,n) in order, so the RNG stream maps to the same
    // flat indices as the old flat loop.
    for (unsigned p = 0; p < weights.PageCount(); ++p)
    {
        float* wp = weights.PageData(p);
        const unsigned nn = weights.PageElems(p);
        for (unsigned i = 0; i < nn; ++i)
        {
            kickRng_ = kickRng_ * 1664525u + 1013904223u;
            float u1 = ((kickRng_ >> 8) & 0xFFFFFFu) / (float)0x1000000;
            kickRng_ = kickRng_ * 1664525u + 1013904223u;
            float u2 = ((kickRng_ >> 8) & 0xFFFFFFu) / (float)0x1000000;
            if (u1 < 1e-7f)
                u1 = 1e-7f;
            const float g = sqrtf(-2.0f * logf(u1)) * cosf(6.2831853f * u2);
            wp[i] += scale * g;
        }
    }

    // Stale Adam momentum would smoothly undo the kick — clear it.
    ResetAdam();

    // Every stored error is pre-kick and now wrong; re-prioritise so the next
    // passes re-measure all memories rather than chasing stale errors.
    ResetLedgerPriority();

    // Re-baseline the plateau tracker on the post-kick trajectory.
    lossEMA_ = 0.0f;
    bestEMA_ = 1e9f;
    stagnation_ = 0;
}

void YukiTrainer::Kick(float sigma)
{
    // The elite cache is the safety net; record its floor, then perturb. Training
    // passes during the window keep capturing any new elite via EvalProbeLoss.
    kickStartBest_ = bestLoss_;
    Perturb(sigma);
}

bool YukiTrainer::EvaluateKick()
{
    // Surpassed iff the elite floor actually dropped since the kick. Either way,
    // restore the live model to the elite so it never ends a kick adrift above
    // its best (the old design left it wherever the window happened to stop).
    const bool surpassed = bestLoss_ < kickStartBest_ - 1e-4f;
    RestoreBest();
    return surpassed;
}

unsigned YukiTrainer::BuildTokenSet(Vector<Vector<unsigned> >& seqs)
{
    seqs.Clear();
    if (!model_ || !model_->IsLoaded() || !memoryDb_)
        return 0;

    const YukiTopology& t = model_->GetTopology();
    DbResult r = memoryDb_->Execute("SELECT text FROM memories");
    const Vector<VariantVector>& rows = r.GetRows();
    for (unsigned i = 0; i < rows.Size(); ++i)
    {
        String text = rows[i][0].GetString();
        // Tokenize EXACTLY as TrainOnce/EvalProbeLoss: lower, split ' ', trim, <unk>.
        Vector<String> words = YukiModel::Tokenize(text);
        Vector<unsigned> tokens;
        for (const String& word : words)
        {
            String clean = word.Trimmed();
            if (clean.Empty())
                continue;
            unsigned idx = model_->GetTokenIndex(clean);
            if (idx >= t.vocabSize)
                idx = 1;   // <unk>
            tokens.Push(idx);
        }
        if (tokens.Size() > t.maxSeqLen)
            tokens.Resize(t.maxSeqLen);
        if (tokens.Size() < 2)
            continue;      // untrainable (<2 tokens)
        seqs.Push(tokens);
    }
    return seqs.Size();
}

void YukiTrainer::TrainMemberPass(PbtMember& mem, const Vector<Vector<unsigned> >& seqs,
                                  unsigned long long count)
{
    (void)count;   // sizing now comes from the paged buffers' page directory
    const YukiTopology& t = model_->GetTopology();
    YukiMath::YukiDims dims;
    dims.embedDim = t.embedDim; dims.nLayers = t.nLayers; dims.nHeads = t.nHeads;
    dims.ffDim = t.ffDim; dims.vocabSize = t.vocabSize; dims.maxSeqLen = t.maxSeqLen;

    // Build views once: this member's paged weights, and the shared paged grad scratch. The
    // views point into the buffers, so in-place AdamStep updates are seen by the next forward.
    YukiMath::YukiModelPtrs mw{};
    std::vector<YukiMath::YukiLayerPtrs> mwl(t.nLayers);
    mem.weights.BuildModelPtrs(dims, mw, mwl.data());
    YukiMath::YukiModelGrads gg{};
    std::vector<YukiMath::YukiLayerGrads> gl(t.nLayers);
    gradients_.BuildModelGrads(dims, gg, gl.data());

    for (unsigned s = 0; s < seqs.Size(); ++s)
    {
        const Vector<unsigned>& tk = seqs[s];
        // Reuse the shared gradient scratch — members run sequentially under the worker mutex,
        // so there's no contention. YukiSequenceForwardBackward ACCUMULATES, so zero first.
        gradients_.Zero();
        YukiMath::YukiSequenceForwardBackward(mw, mwl.data(), gg, gl.data(),
            dims, tk.Buffer(), tk.Size());
        ++mem.step;
        // Adam over the whole member, page by page — mem.weights/gradients_/mem.m/mem.v all share
        // the same page directory (Configure'd from the same topology).
        for (unsigned p = 0; p < mem.weights.PageCount(); ++p)
            YukiMath::AdamStep(mem.weights.PageData(p), gradients_.PageData(p),
                mem.m.PageData(p), mem.v.PageData(p), mem.step,
                mem.lr, beta1_, beta2_, adamEps_, mem.weights.PageElems(p));
    }
}

bool YukiTrainer::RunPbtKick(float baseSigma, int memberPasses, volatile int* progress)
{
    if (!model_ || !model_->IsLoaded() || !memoryDb_ || baseSigma <= 0.0f)
        return false;
    const unsigned long long count = model_->GetTopology().TotalWeights();
    if (count == 0)
        return false;
    if (memberPasses < 1)
        memberPasses = 1;
    if (pbtPop_ < 1)
        pbtPop_ = 1;

    // Token set once; members train against it in RAM (no per-pass DB hit).
    Vector<Vector<unsigned> > seqs;
    if (BuildTokenSet(seqs) == 0)
        return false;

    // Seed off the ELITE if we have one (explore around the best ever, not wherever the live model
    // drifted); else off the live weights. Both stores are now paged with the same layout as the
    // members, so the seed reads page-for-page uniformly.
    const bool fromElite = HasElite();
    const PagedFloatBuffer& base = fromElite ? bestWeights_ : model_->GetWeights();

    // Relative noise scale = RMS of the base weights (mirrors Perturb).
    double sumsq = 0.0;
    for (unsigned p = 0; p < base.PageCount(); ++p)
    {
        const float* bp = base.PageData(p);
        const unsigned nn = base.PageElems(p);
        for (unsigned i = 0; i < nn; ++i)
            sumsq += (double)bp[i] * (double)bp[i];
    }
    const float rms = (float)sqrt(sumsq / (double)count);

    const float floorBefore = bestLoss_;

    // Spread sigma & lr multiplicatively across the population: one near-elite
    // refiner, the rest progressively bolder explorers / faster learners.
    static const float spread[4] = { 0.5f, 1.0f, 1.5f, 2.0f };

    const YukiTopology& t = model_->GetTopology();
    pop_.Resize(pbtPop_);
    for (unsigned k = 0; k < pbtPop_; ++k)
    {
        PbtMember& mem = pop_[k];
        mem.weights.Configure(t.embedDim, t.nLayers, t.ffDim, t.vocabSize);
        mem.m.Configure(t.embedDim, t.nLayers, t.ffDim, t.vocabSize);   // Configure zeroes the pages
        mem.v.Configure(t.embedDim, t.nLayers, t.ffDim, t.vocabSize);
        mem.step = 0;
        mem.sigma = baseSigma * spread[k % 4];
        mem.lr = learningRate_ * spread[k % 4];
        mem.fitness = 1e30f;

        // Seed weights = base + Gaussian(sigma·rms), Box-Muller via the PBT LCG. Page-by-page —
        // pages tile [0,count) in order, so the RNG stream maps to the same flat indices as the
        // old flat loop (identical seeds). base and mem.weights share the same page layout.
        const float scale = mem.sigma * rms;
        for (unsigned p = 0; p < mem.weights.PageCount(); ++p)
        {
            const unsigned nn = mem.weights.PageElems(p);
            const float* bp = base.PageData(p);
            float* wp = mem.weights.PageData(p);
            for (unsigned i = 0; i < nn; ++i)
            {
                pbtRng_ = pbtRng_ * 1664525u + 1013904223u;
                float u1 = ((pbtRng_ >> 8) & 0xFFFFFFu) / (float)0x1000000;
                pbtRng_ = pbtRng_ * 1664525u + 1013904223u;
                float u2 = ((pbtRng_ >> 8) & 0xFFFFFFu) / (float)0x1000000;
                if (u1 < 1e-7f) u1 = 1e-7f;
                const float gn = sqrtf(-2.0f * logf(u1)) * cosf(6.2831853f * u2);
                wp[i] = bp[i] + scale * gn;
            }
        }
    }

    // Refine + score each member (Adam under its own lr, then probe loss).
    for (unsigned k = 0; k < pbtPop_; ++k)
    {
        PbtMember& mem = pop_[k];
        for (int p = 0; p < memberPasses; ++p)
        {
            TrainMemberPass(mem, seqs, count);
            if (progress) ++(*progress);   // live "k/total" for an off-thread status display (benign race)
        }
        float f = EvalProbeLoss(mem.weights);
        mem.fitness = (f >= 0.0f) ? f : 1e30f;
    }

    // Select the winner (lowest probe loss).
    unsigned best = 0;
    for (unsigned k = 1; k < pbtPop_; ++k)
        if (pop_[k].fitness < pop_[best].fitness)
            best = k;
    PbtMember& win = pop_[best];

    if (win.fitness < bestLoss_)
    {
        // Winner beat the persisted floor → it becomes the live model AND the elite,
        // and we adopt its learning rate (the population self-tunes lr — the kick
        // underwrites the tuning). CaptureBest snapshots the live model, so set it first.
        model_->GetWeights().CopyFrom(win.weights);   // both paged, same layout
        learningRate_ = win.lr;
        CaptureBest(win.fitness);
        URHO3D_LOGINFOF("YukiTrainer::RunPbtKick: winner #%u  floor %.4f -> %.4f  adopted lr=%.5f",
            best, floorBefore, bestLoss_, learningRate_);
    }
    else
    {
        // No member beat the floor → restore the live model to the elite (never leave
        // it on a worse candidate). lr unchanged.
        RestoreBest();
    }

    // The live model changed; stored per-memory errors are stale. Re-prioritise
    // (mirrors Perturb) so steady-state passes re-measure, and reset Adam + the
    // plateau tracker onto the new state.
    ResetAdam();
    ResetLedgerPriority();
    lossEMA_ = 0.0f;
    bestEMA_ = 1e9f;
    stagnation_ = 0;

    return bestLoss_ < floorBefore - 1e-4f;
}

void YukiTrainer::PushCheckpoint(const PagedFloatBuffer& weights)
{
    if (!weights.IsConfigured())
        return;
    // Skip a duplicate of the newest — identical snapshots give a zero difference,
    // which would make a move a no-op.
    if (!checkpointRing_.Empty() && checkpointRing_.Back().ContentEquals(weights))
        return;
    checkpointRing_.Push(weights);   // deep copy into the ring
    while (checkpointRing_.Size() > ringCap_)
        checkpointRing_.Erase(0);    // drop oldest
}

bool YukiTrainer::DifferenceMoveSweep(float fbase)
{
    if (!model_ || !model_->IsLoaded() || !memoryDb_)
        return false;
    if (checkpointRing_.Size() < 2)
        return false;   // need two points in the lineage to form a difference
    const unsigned long long count = model_->GetTopology().TotalWeights();
    if (count == 0)
        return false;

    const PagedFloatBuffer& b0 = checkpointRing_[checkpointRing_.Size() - 1];  // newest best
    const PagedFloatBuffer& b1 = checkpointRing_[checkpointRing_.Size() - 2];  // prior best
    if (b0.Size() != count || b1.Size() != count)
        return false;

    const float floorBefore = bestLoss_;

    // Line search along the improvement direction d = b0 - b1 (self-scaling: as the
    // bests converge, d shrinks, so the moves shrink to fine-tuning). fbase scales it.
    const float baseF = (fbase > 0.0f) ? fbase : 1.0f;
    const float mult[3] = { 0.5f, 1.0f, 2.0f };

    const YukiTopology& t = model_->GetTopology();
    PagedFloatBuffer cand; cand.Configure(t.embedDim, t.nLayers, t.ffDim, t.vocabSize);
    PagedFloatBuffer bestCand;
    float bestScore = bestLoss_;
    float bestUsedF = 0.0f;
    bool found = false;

    for (unsigned s = 0; s < 3; ++s)
    {
        const float F = baseF * mult[s];
        // Extrapolate page-by-page: cand = b0 + F*(b0 - b1). b0/b1/cand share one layout.
        for (unsigned p = 0; p < cand.PageCount(); ++p)
        {
            const float* p0 = b0.PageData(p);
            const float* p1 = b1.PageData(p);
            float* c = cand.PageData(p);
            const unsigned nn = cand.PageElems(p);
            for (unsigned i = 0; i < nn; ++i)
                c[i] = p0[i] + F * (p0[i] - p1[i]);
        }

        const float score = EvalProbeLoss(cand);   // forward-only — cheap
        if (score >= 0.0f && score < bestScore)
        {
            bestScore = score;
            bestUsedF = F;
            bestCand = cand;   // deep copy of the winning extrapolation
            found = true;
        }
    }

    if (!found)
    {
        // No extrapolation beat the floor → leave the model untouched. The graceful
        // no-op: unlike the kick, a failed move costs nothing and disturbs nothing
        // (no perturb, no Adam reset, no re-prioritise) — training just carries on.
        URHO3D_LOGINFOF("YukiTrainer::DifferenceMoveSweep: no F beat floor %.4f (held)", floorBefore);
        return false;
    }

    // Jump the live model to the winning extrapolation and capture it as the new elite. The weight
    // write + elite snapshot take the SHARED lock (in-place, benign vs inference; excludes a
    // concurrent RestoreBest / Expand). DB re-baseline under dbMutex_ (weight->db order).
    {
        auto wlock = WeightShared();
        model_->GetWeights().CopyFrom(bestCand);   // both paged, same layout
        CaptureBest(bestScore);   // snapshots live + persists .best + pushes the ring

        // The live model moved; stored per-memory errors are stale. Re-baseline so the
        // steady-state passes re-measure and Adam restarts cleanly from the new point.
        ResetAdam();
        ResetLedgerPriority();   // RAM re-baseline; persisted on the next FlushLedger
        lossEMA_ = 0.0f;
        bestEMA_ = 1e9f;
        stagnation_ = 0;
    }

    URHO3D_LOGINFOF("YukiTrainer::DifferenceMoveSweep: F=%.2f  floor %.4f -> %.4f",
        bestUsedF, floorBefore, bestLoss_);
    return true;
}

float YukiTrainer::TrainSample(const Vector<unsigned>& tokens)
{
    if (!model_ || tokens.Size() < 2)
        return 0.0f;

    const YukiTopology& t = model_->GetTopology();
    unsigned dim = t.embedDim;
    float totalLoss = 0.0f;

    // Simple next-token prediction: for each position, predict the next token.
    // Use the embedding of the current token as input, predict via output projection.
    // This is a simplified training loop — single token context, not full sequence.
    // Full sequence training requires backprop through attention — future work.

    const float* embWeights = model_->GetEmbeddingWeights();
    const float* outWeights = model_->GetOutputWeights();

    // Mutable pointers for gradient updates — embedding (first tensor) and output projection
    // (last tensor). Both are contiguous spans in the paged store.
    PagedFloatBuffer& W = model_->GetWeights();
    float* embMut = W.Span(0, (unsigned long long)t.vocabSize * dim);
    float* outMut = W.Span(W.Size() - (unsigned long long)dim * t.vocabSize,
                           (unsigned long long)dim * t.vocabSize);

    Vector<float> logits(t.vocabSize);
    Vector<float> probs(t.vocabSize);

    for (unsigned pos = 0; pos + 1 < tokens.Size(); ++pos)
    {
        unsigned inputToken = tokens[pos];
        unsigned targetToken = tokens[pos + 1];

        if (inputToken >= t.vocabSize || targetToken >= t.vocabSize)
            continue;

        const float* inputEmb = embWeights + inputToken * dim;

        // Forward: logits = inputEmb × outWeights [dim × vocabSize]
        for (unsigned v = 0; v < t.vocabSize; ++v)
        {
            float sum = 0.0f;
            for (unsigned d = 0; d < dim; ++d)
                sum += inputEmb[d] * outWeights[d * t.vocabSize + v];
            logits[v] = sum;
        }

        // Softmax
        float maxLogit = logits[0];
        for (unsigned v = 1; v < t.vocabSize; ++v)
            if (logits[v] > maxLogit) maxLogit = logits[v];

        float expSum = 0.0f;
        for (unsigned v = 0; v < t.vocabSize; ++v)
        {
            probs[v] = expf(logits[v] - maxLogit);
            expSum += probs[v];
        }
        for (unsigned v = 0; v < t.vocabSize; ++v)
            probs[v] /= expSum;

        // Cross-entropy loss
        float p = probs[targetToken];
        if (p < 1e-10f) p = 1e-10f;
        totalLoss += -logf(p);

        // Gradient of loss w.r.t. logits: probs - one_hot(target)
        for (unsigned v = 0; v < t.vocabSize; ++v)
            probs[v] -= (v == targetToken) ? 1.0f : 0.0f;

        // Update output weights: grad = inputEmb^T × (probs - target)
        // W -= lr * grad
        for (unsigned d = 0; d < dim; ++d)
        {
            for (unsigned v = 0; v < t.vocabSize; ++v)
            {
                outMut[d * t.vocabSize + v] -= learningRate_ * inputEmb[d] * probs[v];
            }
        }

        // Update input embedding: grad = outWeights × (probs - target)
        for (unsigned d = 0; d < dim; ++d)
        {
            float grad = 0.0f;
            for (unsigned v = 0; v < t.vocabSize; ++v)
                grad += outWeights[d * t.vocabSize + v] * probs[v];
            embMut[inputToken * dim + d] -= learningRate_ * grad;
        }
    }

    return (tokens.Size() > 1) ? totalLoss / (float)(tokens.Size() - 1) : 0.0f;
}

// ─── Expansion ───────────────────────────────────────────────────────────────

bool YukiTrainer::NeedsExpansion() const
{
    if (totalTokens_ == 0)
        return false;

    float unknownRatio = (float)totalUnknowns_ / (float)totalTokens_;
    return unknownRatio > unknownThreshold_;
}

bool YukiTrainer::Expand()
{
    if (!model_ || !model_->IsLoaded() || !memoryDb_)
        return false;

    const YukiTopology& oldT = model_->GetTopology();
    String oldPath = model_->GetPath();
    String newPath = oldPath + ".expanded";

    // Build new vocabulary: old tokens + new words from remaining memories
    Vector<String> newVocab;
    for (unsigned i = 0; i < oldT.vocabSize; ++i)
        newVocab.Push(model_->GetToken(i));

    // Scan remaining memories for unknown words
    DbResult result = memoryDb_->Execute("SELECT text FROM memories");
    const Vector<VariantVector>& rows = result.GetRows();

    HashSet<String> existing;
    for (unsigned i = 0; i < newVocab.Size(); ++i)
        existing.Insert(newVocab[i]);

    for (unsigned r = 0; r < rows.Size(); ++r)
    {
        Vector<String> words = YukiModel::Tokenize(rows[r][0].GetString());
        for (const String& word : words)
        {
            String clean = word.Trimmed();
            if (clean.Length() >= 1 && !existing.Contains(clean))   // >=1: punctuation tokens count now
            {
                existing.Insert(clean);
                newVocab.Push(clean);
            }
        }
    }

    // New topology — same depth and width, bigger vocab
    // Grow width if vocab doubled
    YukiTopology newT = oldT;
    newT.vocabSize = newVocab.Size();

    if (newT.vocabSize > oldT.vocabSize * 2)
    {
        // Vocab doubled — grow the network too
        newT.embedDim = Min(oldT.embedDim * 2, 512u);
        newT.ffDim = newT.embedDim * 4;
        newT.nHeads = Min(oldT.nHeads * 2, 16u);
    }

    URHO3D_LOGINFOF("YukiTrainer: Expanding vocab %u → %u, dim %u → %u",
        oldT.vocabSize, newT.vocabSize, oldT.embedDim, newT.embedDim);

    if (!ExpandCartridge(context_, oldPath, newPath, newT, newVocab))
        return false;

    // Replace old cartridge
    auto* fs = GetSubsystem<FileSystem>();
    fs->Delete(oldPath);
    fs->Rename(newPath, oldPath);

    // Reload
    model_->Load(oldPath);

    // Reset counters
    totalUnknowns_ = 0;
    totalTokens_ = 0;

    return true;
}

void YukiTrainer::UpdateWeights(float* weights, const float* gradients, unsigned count)
{
    for (unsigned i = 0; i < count; ++i)
        weights[i] -= learningRate_ * gradients[i];
}

}

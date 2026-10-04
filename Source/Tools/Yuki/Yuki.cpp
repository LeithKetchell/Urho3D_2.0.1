// Yuki — public domain LLM tool implementation.

#include "Yuki.h"

#include <Urho3D/Core/CoreEvents.h>
#include <Urho3D/Core/KarenClient.h>
#include <Urho3D/Core/ProcessUtils.h>   // GetArguments() for the -noKaren startup opt-out
#include <Urho3D/Core/StringUtils.h>    // ToI32 — Urho's cross-platform string->int (replaces atoi)
#include <Urho3D/Core/Timer.h>          // HiresTimer — [gpuboot] freeze-localization markers
#include <Urho3D/Engine/EngineDefs.h>
#include <Urho3D/Graphics/Graphics.h>
#include <Urho3D/Graphics/Renderer.h>
#include <Urho3D/Graphics/Zone.h>
#include <Urho3D/Input/Input.h>
#include <Urho3D/Input/InputEvents.h>
#include <Urho3D/IO/FileSystem.h>
#include <Urho3D/IO/File.h>
#include <Urho3D/IO/MemoryBuffer.h>
#include <Urho3D/IO/VectorBuffer.h>
#include <Urho3D/Network/Protocol.h>
#include <Urho3D/IO/IOEvents.h>
#include <Urho3D/UI/UIEvents.h>
#include <Urho3D/GraphicsAPI/Shader.h>
#include <Urho3D/GraphicsAPI/ShaderVariation.h>
#include <Urho3D/GraphicsAPI/VertexBuffer.h>
#include <Urho3D/GraphicsAPI/GraphicsDefs.h>
#include <Urho3D/GraphicsAPI/Vulkan/VulkanGraphicsImpl.h>  // STEP 4: worker command pool + queue-submit mutex
#include <Urho3D/ML/YukiMath.h>          // YukiMath::TotalWeights / YukiDims (resident GPU training)
#include <Urho3D/ML/YukiTrainAdapter.h>  // BuildModelPtrs / YukiModelPtrs (resident GPU training)
#include <Urho3D/ML/PagedFloatBuffer.h>  // Phase 4: YukiMath::YukiPage / YukiPlanPages (paged GPU buffers)

#include <cstring>   // memcpy (Phase 4 page gather/scatter)

#include <cmath>     // powf/sinf/fabsf (GPU-vs-CPU compare)

using namespace Urho3D;

// ─── Single-instance guard ───────────────────────────────────────────────────
// Yuki is the authority on whether a Yuki is already running: it self-guards at
// startup. A second instance refuses to start — it never kills the incumbent.
// The OS releases the lock automatically when the holding process dies, so the
// guard can never go stale (the failure mode the old Manager bool latch had).
// Platform lock primitives have no Urho wrapper, so the OS difference is wrapped
// in a build-time switch, mirroring the existing AuthServer/Manager guards.
#ifdef _WIN32
#include <windows.h>
static HANDLE g_yukiMutex = nullptr;
static bool AcquireInstanceLock()
{
    g_yukiMutex = CreateMutexA(nullptr, TRUE, "Global\\urho3d_yuki_singleton");
    if (!g_yukiMutex)
        return false;
    if (GetLastError() == ERROR_ALREADY_EXISTS)
        return false;   // another instance owns it
    return true;
}
static void ReleaseInstanceLock()
{
    if (g_yukiMutex)
    {
        ReleaseMutex(g_yukiMutex);
        CloseHandle(g_yukiMutex);
        g_yukiMutex = nullptr;
    }
}
#else
#include <unistd.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
static int g_yukiLockFd = -1;
static const char* g_yukiLockPath = "/tmp/urho3d_yuki.lock";

// Current resident set size (RSS) in MB for the leakage-watch overlay. Reads /proc/self/statm field 2
// (resident pages) via a low-level read — the engine has no RSS API, and Urho3D File can't read procfs
// (it reports size 0, so File::Read yields nothing). Unlike peak RSS (getrusage ru_maxrss), current RSS can
// fall, so a real leak reads as a genuine upward drift and a plateau reads as held. Returns 0 on failure.
static double YukiCurrentRssMb()
{
    int fd = open("/proc/self/statm", O_RDONLY);
    if (fd < 0)
        return 0.0;
    char buf[128];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0)
        return 0.0;
    buf[n] = '\0';
    long totalPages = 0, residentPages = 0;
    if (sscanf(buf, "%ld %ld", &totalPages, &residentPages) != 2)
        return 0.0;
    static const long pageSz = sysconf(_SC_PAGESIZE);   // 4096 on x86_64; queried, not assumed
    return (double)residentPages * (double)pageSz / (1024.0 * 1024.0);
}
static bool AcquireInstanceLock()
{
    // flock-based guard with an inode re-check (guards the delete+recreate race
    // where another process could lock a fresh file at the same path). No PID
    // kill — a duplicate simply fails to acquire and exits.
    for (int attempt = 0; attempt < 3; ++attempt)
    {
        g_yukiLockFd = open(g_yukiLockPath, O_CREAT | O_RDWR, 0600);
        if (g_yukiLockFd < 0)
            return false;
        if (flock(g_yukiLockFd, LOCK_EX | LOCK_NB) != 0)
        {
            close(g_yukiLockFd);
            g_yukiLockFd = -1;
            return false;   // held by a live Yuki
        }
        struct stat fdStat = {};
        struct stat pathStat = {};
        if (fstat(g_yukiLockFd, &fdStat) == 0 && ::stat(g_yukiLockPath, &pathStat) == 0
            && fdStat.st_dev == pathStat.st_dev && fdStat.st_ino == pathStat.st_ino)
            break;   // we hold the real on-disk file
        close(g_yukiLockFd);
        g_yukiLockFd = -1;
    }
    if (g_yukiLockFd < 0)
        return false;
    // Record our PID for diagnostics (not used for locking). Format via Urho String
    // (cross-platform, no stdio); the lock write stays POSIX pwrite — a genuine
    // platform primitive with no Urho wrapper, already inside this POSIX branch.
    int rc = ftruncate(g_yukiLockFd, 0); (void)rc;
    String pidStr((int)getpid());
    ssize_t w = pwrite(g_yukiLockFd, pidStr.CString(), pidStr.Length(), 0); (void)w;
    return true;
}
static void ReleaseInstanceLock()
{
    if (g_yukiLockFd >= 0)
    {
        flock(g_yukiLockFd, LOCK_UN);
        close(g_yukiLockFd);
        // Do NOT unlink — keep the inode stable so the next instance contends on
        // the same file (unlinking would let a new inode be locked freely).
        g_yukiLockFd = -1;
    }
}
#endif

URHO3D_DEFINE_APPLICATION_MAIN(Yuki)

// Source tag for text Yuki generated itself (inference output remembered back into the corpus —
// see the conversational path in ProcessInput and the MSG_YUKI_SAY reply path in
// HandleNetworkMessage). One identity regardless of entry point, so /sources, /coverage, and
// /newcart's corpus-composition logic can all identify and prioritise self-authored material —
// self-improvement means reasoned output feeds back into training, not just Leith-authored or
// ingested text.
static const char* const YUKI_SELF_SOURCE = "Yuki (self)";

Yuki::Yuki(Context* context) :
    Application(context)
{
}

void Yuki::Setup()
{
    engineParameters_[EP_WINDOW_TITLE] = String("Yuki v") + YUKI_VERSION;
    engineParameters_[EP_WINDOW_WIDTH] = 800;
    engineParameters_[EP_WINDOW_HEIGHT] = 600;
    engineParameters_[EP_FULL_SCREEN] = false;
    engineParameters_[EP_WINDOW_RESIZABLE] = true;
    engineParameters_[EP_LOG_NAME] = "Yuki.log";
    engineParameters_[EP_RESOURCE_PATHS] = "CoreData;Data";
    engineParameters_[EP_FRAME_LIMITER] = true;
}

void Yuki::Start()
{
    // Single-instance guard — refuse to start if a Yuki is already running.
    if (!AcquireInstanceLock())
    {
        URHO3D_LOGERROR("Yuki is already running — this instance will exit.");
        engine_->Exit();
        return;
    }

    engine_->SetMaxFps(30);
    engine_->SetMaxInactiveFps(10);

    GetSubsystem<Input>()->SetMouseVisible(true);
    GetSubsystem<Input>()->SetMouseGrabbed(false);

    CreateUI();

    InitDatabase();
    InitMemory();
    InitBrain();
    URHO3D_LOGINFO("[gpuboot] Start: InitBrain returned");
    InitNetwork();
    URHO3D_LOGINFO("[gpuboot] Start: InitNetwork returned");

    // Karen telemetry — register once, then start AFTER InitNetwork so the host
    // server is already up (IsServerRunning()==true). KarenClient rides Yuki's
    // existing 7879 server (skips StartServer) and stamps {Karen:1,...} onto the
    // discovery beacon so the Manager listener finds this emitter. Empty map: Yuki
    // sets no beacon of its own, so there's nothing to merge-preserve.
    // `-noKaren` on the command line runs Yuki with ZERO telemetry: the subsystem is
    // never registered or started. (A runtime /karen toggle is planned separately.)
    if (!GetArguments().Contains("-noKaren"))
    {
        if (!GetSubsystem<KarenClient>())
            context_->RegisterSubsystem(new KarenClient(context_));
        GetSubsystem<KarenClient>()->Start("Yuki", VariantMap());
    }
    else
        URHO3D_LOGINFO("Yuki: -noKaren — telemetry disabled.");
    URHO3D_LOGINFO("[gpuboot] Start: Karen/telemetry stage returned");

    // Ears — DISABLED. Start() opens the mic and continuously captures PCM, but
    // nothing here ever calls BeginListening()/EndListeningAndTranscribe(), so the
    // transcription output goes nowhere — the capture only produced PCM-overflow
    // errors. Left the member in place so re-enabling is a one-line restore once
    // there's an actual consumer wired up.
    // ears_ = new YukiEars(context_);
    // if (ears_->Start(16000)) ...

    // Show what she knows on startup
    if (memoryDb_)
    {
        DbResult r = memoryDb_->Execute("SELECT COUNT(*) FROM memories");
        const Vector<VariantVector>& rows = r.GetRows();
        int count = rows.Empty() ? 0 : rows[0][0].GetI32();
        LogMessage("I have " + String(count) + " memories.", Color(0.6f, 0.8f, 1.0f));
        // Seed the lock-free telemetry counter here — runs before trainWorker_.Run(),
        // so this read is race-free and we never touch memoryDb_ on the per-frame path.
        memoryCount_ = count;
    }

    LogMessage("Talk to me. I'll remember.", Color(0.6f, 0.8f, 1.0f));

    SubscribeToEvent(E_UPDATE, URHO3D_HANDLER(Yuki, HandleUpdate));
    // "If the app is dying, save it." Window close / exit request fires before teardown.
    SubscribeToEvent(E_EXITREQUESTED, URHO3D_HANDLER(Yuki, HandleExitRequested));

    // Start the background training worker LAST — everything it touches is now
    // initialised, and from here on all model_/memoryDb_/trainer_ access is
    // serialised through trainMutex_ (worker loop + the UI thread below).
    if (hasBrain_ && trainer_)
    {
        URHO3D_LOGINFO("[gpuboot] Start: entering trainWorker block (memory query + subscribes passed)");
        trainer_->SetLocks(&trainMutex_, &dbMutex_);   // share the weight RW lock + DB mutex with TrainOnce
        trainWorker_.Configure(trainer_, model_, &trainMutex_);
        trainWorker_.Run();
        URHO3D_LOGINFO("[gpuboot] Start: trainWorker_.Run() returned — about to StartGpuTraining");
        // A4 — GPU-all-the-way default (restored 2026-07-09): the off-render GPU resident worker is the
        // production trainer. The async-compute-batch sync bug that had forced a temporary CPU-boot default
        // is FIXED — training now submits on a dedicated async-compute queue (VulkanGraphicsImpl), so a long
        // compute batch no longer sits in front of the frame Present and races the render loop into
        // VK_ERROR_DEVICE_LOST. StartGpuTraining pauses the CPU worker on success (kept alive but dormant, so
        // the solver toggle / /gputrain stop reverts to it). If Vulkan/brain is unavailable it returns false
        // and the legacy CPU trainer runs as the fallback. Pass -cpuboot to force the CPU trainer at boot.
        if (!GetArguments().Contains("-cpuboot") && StartGpuTraining(false))
            LogMessage("[Brain] GPU trainer engaged (CPU trainer dormant; switch with the solver toggle).", Color(0.6f, 0.8f, 1.0f));
        else
            LogMessage("[Brain] CPU trainer running (GPU unavailable or -cpuboot). Switch with the solver toggle / /gputrain.", Color(0.6f, 0.8f, 1.0f));
    }
}

void Yuki::HandleExitRequested(StringHash, VariantMap&)
{
    // The app is dying (window close / exit request) — persist the best before teardown.
    SaveOnExit();
}

void Yuki::SaveOnExit()
{
    if (savedOnExit_)
        return;
    savedOnExit_ = true;
    // Stop training so nothing races the write, then persist the BEST — not the latest
    // drifted state (saving latest on exit is exactly what lost the floor). RestoreBest
    // rolls the live model back to the elite in RAM only — it no longer writes to disk
    // (per the "only a floor breakthrough writes" rule that killed the GPU-mode storm),
    // so SaveOnExit does the ONE teardown write explicitly afterward. With no elite we
    // just save the live model.
    // GPU-all-the-way: the GPU worker is the live trainer — stop it FIRST or it keeps
    // syncing model_ while we roll back (join is safe: it only TryAcquires trainMutex_,
    // and SaveOnExit holds no lock). We discard its drifted resident state by design.
    if (gpuPumpActive_)
        StopGpuPump();   // stop the pump + free its pools; drifted resident state discarded
    trainWorker_.Stop();   // clears shouldRun_ and joins the thread
    expertWorker_.Stop();  // P5: join any background expert-training thread before its referenced members die
    if (escapePbtRunning_) escapePbtWorker_.Stop();   // join any in-flight escape PBT rung before teardown
    if (trainer_)
        trainer_->FlushLedger();   // clean exit: persist the RAM priority ledger one last time
    if (trainer_ && trainer_->HasElite())
        trainer_->RestoreBest();   // roll the live model back to the elite (RAM only, no disk write)
    if (model_ && model_->IsLoaded())
        model_->Save();            // the one teardown write: elite (post-restore) or live if no elite -> cart
}

void Yuki::Stop()
{
    SaveOnExit();          // best -> cart, exactly once (also covers the engine-teardown path)

    // Flush Yuki's training writes out of the WAL before we exit. yuki_memory.db
    // stays in WAL mode (WorkboardManager co-owns it and is its long-lived holder);
    // this is just a checkpoint, not a journal-mode change. TRUNCATE zeroes the -wal
    // when no reader is mid-read (e.g. Manager idle this instant); otherwise it
    // degrades to a passive flush so the latest writes still reach the main file.
    if (memoryDb_)
        memoryDb_->Execute("PRAGMA wal_checkpoint(TRUNCATE)");

    if (gpuPumpActive_) StopGpuPump();   // drain + free pools before ResidentShutdown frees the buffers
    ResidentShutdown();    // free the GPU resident-trainer buffer set if it was allocated

    auto* network = GetSubsystem<Network>();
    if (network)
        network->StopServer();
    ReleaseInstanceLock();
}

// ============================================================================
// Database
// ============================================================================

void Yuki::InitDatabase()
{
    auto* db = GetSubsystem<Database>();
    if (!db)
    {
        LogMessage("[ERROR] Database subsystem not available", Color::RED);
        return;
    }

    auto* fs = GetSubsystem<FileSystem>();
    String dbPath = fs->GetProgramDir() + "Data/GameDB/corpus.db";

    corpusDb_ = db->Connect("file:" + dbPath);
    if (!corpusDb_)
        LogMessage("[WARN] corpus.db not found — corpus features disabled", Color::YELLOW);
    else
    {
        corpusDb_->Execute("PRAGMA journal_mode=TRUNCATE");
        RunSchema();
        URHO3D_LOGINFO("[Yuki] Corpus DB connected");
    }
}

void Yuki::InitMemory()
{
    auto* db = GetSubsystem<Database>();
    if (!db)
        return;

    auto* fs = GetSubsystem<FileSystem>();
    String dbPath = fs->GetProgramDir() + "Data/GameDB/yuki_memory.db";

    memoryDb_ = db->Connect("file:" + dbPath);
    if (!memoryDb_)
    {
        LogMessage("[ERROR] Failed to open yuki_memory.db", Color::RED);
        return;
    }

    // yuki_memory.db stays in WAL mode: WorkboardManager co-owns this file and is
    // its long-lived holder, and SQLite refuses to leave WAL mode while a second
    // connection is attached — so a journal_mode=TRUNCATE here was a silent no-op.
    // Durability is kept instead by WAL checkpoints (Manager periodic + both sides
    // at shutdown); see Yuki::Stop and WorkboardManager's WAL upkeep.
    // synchronous=NORMAL (per-connection; does NOT affect Manager's connection): in WAL this is the
    // crash-safe setting — a commit no longer fsyncs the WAL every time, only at checkpoint. It can
    // lose the LAST few commits on an OS/power loss (never on an app crash, and WAL stays consistent);
    // those are just per-pass priority stamps, regenerated next pass. Cuts the training fsync rate hard
    // on top of the per-pass transaction batching (RecordTrainResults).
    memoryDb_->Execute("PRAGMA synchronous=NORMAL");
    URHO3D_LOGINFO("[Yuki] Memory DB connected: " + dbPath);
}

void Yuki::RunSchema()
{
    if (!corpusDb_)
        return;

    auto* fs = GetSubsystem<FileSystem>();
    String schemaPath = fs->GetProgramDir() + "Data/GameDB/corpus_schema.sql";

    File schemaFile(context_, schemaPath, FILE_READ);
    if (!schemaFile.IsOpen())
        return;

    unsigned size = schemaFile.GetSize();
    String sql;
    sql.Resize(size);
    schemaFile.Read(&sql[0], size);
    schemaFile.Close();

    Vector<String> statements = sql.Split(';');
    for (const String& stmt : statements)
    {
        String trimmed = stmt.Trimmed();
        if (trimmed.Empty() || trimmed.StartsWith("--"))
            continue;
        corpusDb_->Execute(trimmed);
    }
}

// ============================================================================
// Memory — read and write yuki_memory.db
// ============================================================================

void Yuki::Remember(const String& text, const String& source)
{
    if (!memoryDb_ || text.Trimmed().Empty())
        return;

    memoryDb_->Execute("INSERT INTO memories (text, source) VALUES ('" +
        text.Replaced("'", "''") + "', '" +
        source.Replaced("'", "''") + "')");
    ++memoryCount_;  // keep the lock-free telemetry counter current (main-thread writer)
}

Vector<String> Yuki::RecallMemories(const String& query, int maxResults)
{
    Vector<String> results;
    if (!memoryDb_ || query.Trimmed().Empty())
        return results;

    // Split query into words, search for any match
    Vector<String> words = YukiModel::Tokenize(query);

    // Build WHERE clause — match any word
    String where;
    for (unsigned i = 0; i < words.Size(); ++i)
    {
        String word = words[i].Trimmed();
        if (word.Length() < 3)  // skip tiny words
            continue;
        if (!where.Empty())
            where += " OR ";
        where += "LOWER(text) LIKE '%" + word.Replaced("'", "''") + "%'";
    }

    if (where.Empty())
        return results;

    String sql = "SELECT text FROM memories WHERE (" + where +
        ") ORDER BY id DESC LIMIT " + String(maxResults);

    DbResult r = memoryDb_->Execute(sql);
    const Vector<VariantVector>& rows = r.GetRows();
    for (unsigned i = 0; i < rows.Size(); ++i)
        results.Push(rows[i][0].GetString());

    return results;
}

// ============================================================================
// GPU resident trainer (STEP 3d) — the on-GPU train step on a persistent buffer
// set. ResidentStep() is the unit STEP 4's worker will dispatch off the render
// thread; here it runs main-thread. Weights/grads/moments + all activation and
// gradient scratch stay GPU-resident across steps; only the loss/weights are read
// back, and only for verify/checkpoint — never inside the step.
// ============================================================================

struct YukiResidentState
{
    // topology + derived sizes. S is the ALLOCATION length (maxSeqLen); curS is the current
    // sequence length the dispatches use (<= S) — one buffer set serves variable-length corpus
    // sequences. For the fixed-S verify commands curS == S.
    unsigned D{}, F{}, V{}, H{}, L{}, S{}, curS{}, hd{}, TW{}, N{}, NF{}, P{}, DD{};
    float scale{}, eps{};
    // weight-slice offsets (element indices into the single W / G buffers)
    unsigned offEmb{}, offOut{}, offFN{};
    Vector<unsigned> oQ, oK, oV, oO, oF1, oF2, oNorm;
    // Adam
    float lr{}, b1{}, b2{}, aeps{};
    unsigned long long t{};
    float invCount{};
    // cached shader variations
    ShaderVariation *moCS{}, *aoCS{}, *boCS{}, *smCS{}, *ctxCS{}, *gfCS{}, *gbCS{}, *lnCS{}, *ldxCS{}, *ldwCS{},
                    *dvCS{}, *dpCS{}, *smbCS{}, *dqCS{}, *dkCS{}, *efCS{}, *ebCS{}, *ceCS{}, *addCS{}, *zeroCS{}, *cpCS{}, *adamCS{};
    // resident buffers (allocated once, reused every step)
    // Phase 4 — 64-bit-addressable GPU weight/grad/Adam buffers, PAGED into tensor-aligned pages
    // (mirroring the CPU PagedFloatBuffer / YukiPaging plan) so no single VkBuffer exceeds the i32
    // element ceiling. With a <=2^24 page stride every per-tensor offset handed to a shader stays a
    // page-LOCAL index that is exactly float-representable (shaders read offsets as uint(dim[k]) from
    // a float meta). WbP/GbP/GaccP share one page directory (wgPages); mvP pages hold 2*elems (m,v
    // interleaved); pBufP is one 9-float Adam-param buffer per page (written once per Adam call, so it
    // survives the single submit). When the whole model fits ONE page this collapses, bit-identical, to
    // the pre-paging single-buffer path (page 0, base 0, local offset == global offset).
    Vector<YukiMath::YukiPage> wgPages;   ///< Shared page directory (base+elems) for W/G/Gacc.
    unsigned maxPageElems{};              ///< Largest page's float count (host-scratch sizing).
    Vector<SharedPtr<VertexBuffer> > WbP, GbP, GaccP, mvP, pBufP;
    SharedPtr<VertexBuffer> tokB, tgtB, mdxBuf;
    Vector<SharedPtr<VertexBuffer> > hin, qC, kC, vC, prC, cxC, aC, f1pC, f1qC, bC;
    SharedPtr<VertexBuffer> tmpN, gbB, finB, logB, ceB;
    SharedPtr<VertexBuffer> dOut, dX, dFin, dB, dff1g, dff1, dhFF, dA, dCtx, dVb, dPr, dSc, dQ, dK, dHq, dHk, dHv, dwdb, dEmb;
    // GPU minibatch page (P seqs per submit): pageP per-seq token/target buffers (each seq needs its
    // OWN tokB/tgtB since all of a page's dispatches coexist in one batch), a gradient accumulator
    // Gacc summed across the page, and lossSlots (pageP*S) holding each seq's per-position nll for
    // one readback. The heavy activation buffers above are REUSED across the page (serial-within-one-
    // submit; the engine's per-dispatch COMPUTE->COMPUTE barrier orders seq i's reads before i+1's writes).
    unsigned pageP{1};
    Vector<SharedPtr<VertexBuffer> > tokBP, tgtBP;
    SharedPtr<VertexBuffer> lossSlots;   // Gacc is paged now (GaccP, shares the wgPages directory)

    // A2 async page in-flight state. When the page is submitted via EndComputeBatchAsync (in
    // ResidentStepPageRecord) the harvest context must survive until the fence signals and
    // ResidentStepPageHarvest reduces the losses: keepAlive holds the per-dispatch meta buffers the GPU
    // still reads until then, and inflightCurS/inflightInvCount the per-seq reduction inputs. The blocking
    // ResidentStepPage clears them within the same call, so nothing outlives a blocking step.
    Vector<SharedPtr<VertexBuffer> > inflightKeepAlive;
    unsigned metaPoolIdx_ = 0;   // rolling cursor into inflightKeepAlive, reused as a persistent per-dispatch meta-buffer pool
    Vector<unsigned> inflightCurS;      ///< per-seq current length for the in-flight page (harvest reduction)
    Vector<float>    inflightInvCount;  ///< per-seq 1/valid-count for the in-flight page
    unsigned inflightCount{0};          ///< seqs recorded in the in-flight page
    bool     pageInFlight{false};       ///< a page is recorded/submitted and not yet harvested

    // A2.3 main-tick pump: the dedicated compute cmd + descriptor pools now live on the MAIN thread and
    // are registered via SetComputeBatchPools (the tid-independent override — safe now that no worker
    // thread races them). Created in StartGpuTraining, freed in StopGpuPump.
    VkCommandPool gpuCmdPool{};
    VkDescriptorPool gpuDescPool{};
};

// ── Phase 4 page (un)staging. The GPU pages and the CPU PagedFloatBuffer are BOTH tensor-aligned but
// need NOT share a stride, so a GPU page's logical [base, base+len) range may span several CPU pages
// (always on tensor boundaries). These walk the source/dest page directory (a handful of pages) and
// memcpy each overlap — used only at (un)stage time, never inside a training step. ──

/// Copy a logical [base, base+len) float range OUT of a paged buffer into a flat contiguous dst.
static void GatherPagedRange(const PagedFloatBuffer& src, unsigned long long base, unsigned len, float* dst)
{
    unsigned long long cur = base; const unsigned long long end = base + len;
    const unsigned np = src.PageCount();
    while (cur < end)
    {
        unsigned pg = np;
        for (unsigned p = 0; p < np; ++p)
        { const unsigned long long b = src.PageBase(p); if (cur >= b && cur < b + src.PageElems(p)) { pg = p; break; } }
        if (pg == np) break;   // gap (never for a configured buffer) — stop rather than read OOB
        const unsigned long long b = src.PageBase(pg), e = b + src.PageElems(pg);
        const unsigned long long stop = end < e ? end : e;
        const unsigned n = (unsigned)(stop - cur);
        memcpy(dst + (cur - base), src.PageData(pg) + (unsigned)(cur - b), (size_t)n * sizeof(float));
        cur = stop;
    }
}

/// Reverse of GatherPagedRange: copy a flat contiguous src INTO a paged buffer's [base, base+len) range.
static void ScatterPagedRange(PagedFloatBuffer& dst, unsigned long long base, unsigned len, const float* src)
{
    unsigned long long cur = base; const unsigned long long end = base + len;
    const unsigned np = dst.PageCount();
    while (cur < end)
    {
        unsigned pg = np;
        for (unsigned p = 0; p < np; ++p)
        { const unsigned long long b = dst.PageBase(p); if (cur >= b && cur < b + dst.PageElems(p)) { pg = p; break; } }
        if (pg == np) break;
        const unsigned long long b = dst.PageBase(pg), e = b + dst.PageElems(pg);
        const unsigned long long stop = end < e ? end : e;
        const unsigned n = (unsigned)(stop - cur);
        memcpy(dst.PageData(pg) + (unsigned)(cur - b), src + (cur - base), (size_t)n * sizeof(float));
        cur = stop;
    }
}

bool Yuki::ResidentInit(unsigned D, unsigned F, unsigned V, unsigned H, unsigned L, unsigned S,
                        const PagedFloatBuffer& initW, const unsigned* tokens)
{
    auto* graphics = GetSubsystem<Graphics>();
    auto* cache = GetSubsystem<ResourceCache>();
    if (!graphics || !initW.IsConfigured() || !tokens) return false;

    // [gpuboot] freeze-localization: time each phase of resident init to Yuki.log (grep '[gpuboot]').
    // The boot GPU path runs silently (announce=false), so if it wedges the log otherwise ends at the
    // brain load with no clue where. These markers bisect ResidentInit → BuildTokenSet → pool create.
    HiresTimer _riTimer;
    URHO3D_LOGINFO("[gpuboot] ResidentInit: ENTER");

    ResidentShutdown();
    YukiResidentState* st = new YukiResidentState();

    st->D = D; st->F = F; st->V = V; st->H = H; st->L = L; st->S = S;
    st->hd = D / H; st->scale = 1.0f / sqrtf((float)st->hd); st->eps = 1e-5f;
    st->N = S * D; st->NF = S * F; st->P = H * S * S; st->DD = D * D;

    YukiMath::YukiDims d{};
    d.embedDim = D; d.ffDim = F; d.vocabSize = V; d.nHeads = H; d.nLayers = L; d.maxSeqLen = S;
    st->TW = (unsigned)YukiMath::TotalWeights(d);

    // Flat element offsets of each tensor into the (contiguous) resident GPU buffer. Computed
    // directly from the layout — same order as YukiMath::BuildModelPtrs — so no flat weight
    // pointer is needed (the model store is paged now). normBias/finalNormBias occupy their D
    // slots in the walk but aren't tracked separately (they weren't before either).
    st->oQ.Resize(L); st->oK.Resize(L); st->oV.Resize(L); st->oO.Resize(L);
    st->oF1.Resize(L); st->oF2.Resize(L); st->oNorm.Resize(L);
    {
        unsigned long long o = 0;
        st->offEmb = (unsigned)o; o += (unsigned long long)V * D;
        for (unsigned l = 0; l < L; ++l)
        {
            st->oQ[l] = (unsigned)o; o += (unsigned long long)D * D;
            st->oK[l] = (unsigned)o; o += (unsigned long long)D * D;
            st->oV[l] = (unsigned)o; o += (unsigned long long)D * D;
            st->oO[l] = (unsigned)o; o += (unsigned long long)D * D;
            st->oF1[l] = (unsigned)o; o += (unsigned long long)D * F;
            st->oF2[l] = (unsigned)o; o += (unsigned long long)F * D;
            st->oNorm[l] = (unsigned)o; o += D;   // norm
            o += D;                               // normBias
        }
        st->offFN = (unsigned)o; o += D;          // finalNorm
        o += D;                                   // finalNormBias
        st->offOut = (unsigned)o;                 // outputProj
    }

    auto* opsRes = cache->GetResource<Shader>("Shaders/GLSL/YukiOps.glsl");
    auto* mmRes  = cache->GetResource<Shader>("Shaders/GLSL/YukiMatMul.glsl");
    auto* adRes  = cache->GetResource<Shader>("Shaders/GLSL/YukiAdam.glsl");
    if (opsRes && mmRes && adRes)
    {
        st->moCS = mmRes->GetVariation(CS, "MATMUL_OFF");   st->aoCS = mmRes->GetVariation(CS, "MATMUL_AT_OFF");
        st->boCS = mmRes->GetVariation(CS, "MATMUL_BT_OFF");
        st->smCS = opsRes->GetVariation(CS, "ATTN_SOFTMAX"); st->ctxCS = opsRes->GetVariation(CS, "ATTN_CTX_FWD");
        st->gfCS = opsRes->GetVariation(CS, "GELU_FWD");     st->gbCS = opsRes->GetVariation(CS, "GELU_BWD");
        st->lnCS = opsRes->GetVariation(CS, "LAYERNORM");    st->ldxCS = opsRes->GetVariation(CS, "LN_BWD_DX");
        st->ldwCS = opsRes->GetVariation(CS, "LN_BWD_DWDB");
        st->dvCS = opsRes->GetVariation(CS, "ATTN_DV");      st->dpCS = opsRes->GetVariation(CS, "ATTN_DPROBS");
        st->smbCS = opsRes->GetVariation(CS, "ATTN_SOFTMAX_BWD"); st->dqCS = opsRes->GetVariation(CS, "ATTN_DQ");
        st->dkCS = opsRes->GetVariation(CS, "ATTN_DK");      st->efCS = opsRes->GetVariation(CS, "EMB_FWD");
        st->ebCS = opsRes->GetVariation(CS, "EMB_BWD");      st->ceCS = opsRes->GetVariation(CS, "CROSS_ENTROPY");
        st->addCS = opsRes->GetVariation(CS, "ELEM_ADD");    st->zeroCS = opsRes->GetVariation(CS, "ELEM_ZERO");
        st->cpCS = opsRes->GetVariation(CS, "COPY_OFFSET");  st->adamCS = adRes->GetVariation(CS, "ADAM_STEP");
    }
    if (!st->moCS||!st->aoCS||!st->boCS||!st->smCS||!st->ctxCS||!st->gfCS||!st->gbCS||!st->lnCS||!st->ldxCS||
        !st->ldwCS||!st->dvCS||!st->dpCS||!st->smbCS||!st->dqCS||!st->dkCS||!st->efCS||!st->ebCS||!st->ceCS||
        !st->addCS||!st->zeroCS||!st->cpCS||!st->adamCS)
    { delete st; return false; }
    URHO3D_LOGINFO("[gpuboot] ResidentInit: shaders resolved (+" + String(_riTimer.GetUSec(false) / 1000) + " ms) — GetVariation only; glslang compile is deferred to first dispatch");

    Vector<VertexElement> f1; f1.Push(VertexElement(TYPE_FLOAT, SEM_POSITION));
    Context* ctx = context_;
    auto mk = [&](unsigned nfloats) -> SharedPtr<VertexBuffer>
    { SharedPtr<VertexBuffer> b(new VertexBuffer(ctx)); b->SetShadowed(false); b->SetSize(nfloats, f1, false); return b; };

    const unsigned N = st->N, NF = st->NF, P = st->P;
    // ── Phase 4: plan the GPU page layout (tensor-aligned; <=2^24 stride so every page-local tensor
    // offset stays exactly float-representable when a shader casts it uint(dim[k]) from a float meta —
    // a page-local base is always < stride, and every int < 2^24 is float-exact) and allocate one
    // VkBuffer per page. WbP/GbP/GaccP share the directory; mvP pages are 2*elems (m,v interleaved);
    // pBufP is one 9-float Adam-param buffer per page. Initial weights are gathered from the (possibly
    // differently-paged) CPU buffer one page at a time — no flat TW-float scratch. ──
    {
        const unsigned tc = YukiMath::YukiTensorCount(L);
        Vector<unsigned long long> lens(tc);
        if (YukiMath::YukiEnumerateTensors(D, L, F, V, lens.Buffer(), tc) != tc) { delete st; return false; }
        const unsigned GPU_PAGE_STRIDE = 1u << 24;   // <=2^24 floats/page → float-exact page-local offsets
        st->wgPages.Resize(tc);                       // page count never exceeds tensor count
        unsigned long long total = 0;
        const unsigned np = YukiMath::YukiPlanPages(lens.Buffer(), tc, GPU_PAGE_STRIDE,
                                                    st->wgPages.Buffer(), tc, &total);
        if (!np || total != (unsigned long long)st->TW) { delete st; return false; }
        st->wgPages.Resize(np);
        st->maxPageElems = 0;
        for (unsigned p = 0; p < np; ++p)
            if (st->wgPages[p].elems > st->maxPageElems) st->maxPageElems = st->wgPages[p].elems;

        st->WbP.Resize(np); st->GbP.Resize(np); st->GaccP.Resize(np); st->mvP.Resize(np); st->pBufP.Resize(np);
        Vector<float> scratch(st->maxPageElems ? st->maxPageElems : 1u);
        Vector<float> mvZero(st->maxPageElems ? 2u * st->maxPageElems : 2u);
        for (unsigned i = 0; i < mvZero.Size(); ++i) mvZero[i] = 0.0f;
        for (unsigned p = 0; p < np; ++p)
        {
            const unsigned e = st->wgPages[p].elems;
            st->WbP[p] = mk(e); st->GbP[p] = mk(e); st->GaccP[p] = mk(e);
            st->mvP[p] = mk(2u * e); st->pBufP[p] = mk(9);   // pBufP: Adam params [0..8], [8]=gscale
            GatherPagedRange(initW, st->wgPages[p].base, e, scratch.Buffer());
            st->WbP[p]->SetData(scratch.Buffer());   // SetData reads exactly this page's e floats
            st->mvP[p]->SetData(mvZero.Buffer());    // m,v = 0 (reads this page's 2*e floats)
        }
    }
    URHO3D_LOGINFO("[gpuboot] ResidentInit: weight pages uploaded (+" + String(_riTimer.GetUSec(false) / 1000) + " ms)");
    Vector<float> toksF(S); for (unsigned i = 0; i < S; ++i) toksF[i] = (float)tokens[i];
    st->tokB = mk(S); st->tokB->SetData(toksF.Buffer());
    Vector<unsigned> targets(S); for (unsigned p = 0; p + 1 < S; ++p) targets[p] = tokens[p + 1]; targets[S - 1] = V;
    Vector<float> tgtF(S); for (unsigned i = 0; i < S; ++i) tgtF[i] = (float)targets[i];
    st->tgtB = mk(S); st->tgtB->SetData(tgtF.Buffer());
    unsigned count = 0; for (unsigned p = 0; p < S; ++p) if (targets[p] < V) ++count;
    st->invCount = count ? 1.0f / (float)count : 0.0f;
    // mdxBuf = [S, D, eps, gamma0..gammaD]; head set once, gamma COPY'd from W each LN-bwd.
    Vector<float> mdx(3 + D); mdx[0] = (float)S; mdx[1] = (float)D; mdx[2] = st->eps; for (unsigned i = 0; i < D; ++i) mdx[3 + i] = 0.0f;
    st->mdxBuf = mk(3 + D); st->mdxBuf->SetData(mdx.Buffer());

    st->hin.Resize(L + 1); for (unsigned l = 0; l <= L; ++l) st->hin[l] = mk(N);
    st->qC.Resize(L); st->kC.Resize(L); st->vC.Resize(L); st->prC.Resize(L); st->cxC.Resize(L);
    st->aC.Resize(L); st->f1pC.Resize(L); st->f1qC.Resize(L); st->bC.Resize(L);
    for (unsigned l = 0; l < L; ++l)
    { st->qC[l]=mk(N); st->kC[l]=mk(N); st->vC[l]=mk(N); st->prC[l]=mk(P); st->cxC[l]=mk(N);
      st->aC[l]=mk(N); st->f1pC[l]=mk(NF); st->f1qC[l]=mk(NF); st->bC[l]=mk(N); }
    st->tmpN = mk(N); st->gbB = mk(2 * D); st->finB = mk(N); st->logB = mk(S * V); st->ceB = mk(S * V + S);
    st->dOut = mk(N); st->dX = mk(N); st->dFin = mk(N); st->dB = mk(N); st->dff1g = mk(NF); st->dff1 = mk(NF);
    st->dhFF = mk(N); st->dA = mk(N); st->dCtx = mk(N); st->dVb = mk(N); st->dPr = mk(P); st->dSc = mk(P);
    st->dQ = mk(N); st->dK = mk(N); st->dHq = mk(N); st->dHk = mk(N); st->dHv = mk(N); st->dwdb = mk(2 * D); st->dEmb = mk(V * D);

    // GPU minibatch page allocation. pageP from descriptor-pool budget (the page's sets must all
    // coexist until its single submit). tokBP/tgtBP are per-seq (S floats each, like tokB/tgtB);
    // Gacc accumulates the page's summed gradient (TW); lossSlots holds pageP seqs' per-position
    // nll for one readback. P=1 collapses to the proven single-seq path.
    st->pageP = ComputeGpuPageSize(L);
    st->tokBP.Resize(st->pageP); st->tgtBP.Resize(st->pageP);
    for (unsigned i = 0; i < st->pageP; ++i) { st->tokBP[i] = mk(S); st->tgtBP[i] = mk(S); }
    st->lossSlots = mk(st->pageP * S);   // GaccP allocated above (paged, shares wgPages)

    st->lr = 0.003f; st->b1 = 0.9f; st->b2 = 0.999f; st->aeps = 1e-8f; st->t = 0;
    st->curS = S;   // default: full allocated length (SetResidentSequence overrides per sequence)
    resident_ = st;
    URHO3D_LOGINFO("[gpuboot] ResidentInit: DONE — all activation/grad buffers allocated (+" + String(_riTimer.GetUSec(false) / 1000) + " ms total)");
    return true;
}

void Yuki::ResidentRecordSeq(VertexBuffer* tokB, VertexBuffer* tgtB, unsigned curS, float invCount,
                            bool train, VertexBuffer* lossDst, unsigned lossOff,
                            Vector<SharedPtr<VertexBuffer> >& keepAlive)
{
    // Records ONE sequence's forward (+ optional backward into Gb) into the CURRENTLY-OPEN compute
    // batch — the shared body of ResidentStep (single seq) and ResidentStepPage (P seqs/submit). The
    // caller owns Begin/EndComputeBatch, the Adam step, and keepAlive: the per-dispatch meta buffers
    // MUST outlive the single EndComputeBatch (the GPU reads them at submit), so they're parked in
    // the caller-scoped keepAlive and freed only AFTER the fence-wait. The engine inserts a
    // COMPUTE->COMPUTE barrier after every dispatch, so multiple seqs sharing the reused activation
    // buffers in one batch execute serially in submission order — seq i's reads complete before
    // seq i+1's writes. tokB/tgtB/curS/invCount are THIS seq's (so a page binds a different set each).
    YukiResidentState* st = resident_;
    auto* graphics = GetSubsystem<Graphics>();
    if (!st || !graphics) return;

    const unsigned D = st->D, F = st->F, V = st->V, H = st->H, L = st->L, S = curS;   // curS: this seq's length
    const unsigned N = S * D, NF = S * F, P = H * S * S, DD = st->DD;   // TW dropped: Gb zero is paged now
    const float scale = st->scale, eps = st->eps;

    Vector<VertexElement> f1; f1.Push(VertexElement(TYPE_FLOAT, SEM_POSITION));
    Context* ctx = context_;
    auto run = [&](ShaderVariation* cs, VertexBuffer* a, VertexBuffer* b, VertexBuffer* c, const Vector<float>& meta, unsigned threads)
    {
        // Pooled meta buffer: reuse a persistent slot in keepAlive instead of allocating one per dispatch.
        // Slot is sized EXACTLY to this dispatch's meta (width is fixed per dispatch-index); the cursor
        // resets per page AFTER the prior page's fence, so no in-flight slot is ever reused mid-page.
        VertexBuffer* dBuf;
        if (st->metaPoolIdx_ < keepAlive.Size())
        {
            dBuf = keepAlive[st->metaPoolIdx_];
            if (dBuf->GetVertexCount() != (int)meta.Size()) dBuf->SetSize(meta.Size(), f1, false);
        }
        else
        {
            SharedPtr<VertexBuffer> nb(new VertexBuffer(ctx)); nb->SetShadowed(false); nb->SetSize(meta.Size(), f1, false);
            keepAlive.Push(nb); dBuf = nb;
        }
        // SetData, NOT SetDataRange: these slots are non-shadowed, and SetDataRange_Vulkan has no no-shadow
        // upload path (it writes via the shadow copy only, then UpdateToGPU early-returns on null shadow) — it
        // would upload NOTHING. SetData_Vulkan takes the direct UploadDataToGPU path, so the meta reaches GPU.
        dBuf->SetData((void*)meta.Buffer());
        ++st->metaPoolIdx_;
        graphics->SetStorageBuffer(0, a); graphics->SetStorageBuffer(1, b ? b : a); graphics->SetStorageBuffer(2, c); graphics->SetStorageBuffer(3, dBuf);
        graphics->SetComputeShader(cs); graphics->DispatchCompute((threads + 255u) / 256u, 1, 1);
        graphics->SetComputeShader(nullptr);
        for (unsigned k = 0; k < 4; ++k) graphics->SetStorageBuffer(k, nullptr);
    };
    auto runD = [&](ShaderVariation* cs, VertexBuffer* a, VertexBuffer* b, VertexBuffer* c, VertexBuffer* dbuf, unsigned threads)
    {
        graphics->SetStorageBuffer(0, a); graphics->SetStorageBuffer(1, b ? b : a); graphics->SetStorageBuffer(2, c); graphics->SetStorageBuffer(3, dbuf);
        graphics->SetComputeShader(cs); graphics->DispatchCompute((threads + 255u) / 256u, 1, 1);
        graphics->SetComputeShader(nullptr);
        for (unsigned k = 0; k < 4; ++k) graphics->SetStorageBuffer(k, nullptr);
    };
    auto M = [](float a, float b, float c, float e) -> Vector<float> { Vector<float> m; m.Resize(4); m[0]=a; m[1]=b; m[2]=c; m[3]=e; return m; };
    auto M6 = [](unsigned mm, unsigned k, unsigned n, unsigned ao, unsigned bo, unsigned co) -> Vector<float>
    { Vector<float> m; m.Resize(6); m[0]=(float)mm; m[1]=(float)k; m[2]=(float)n; m[3]=(float)ao; m[4]=(float)bo; m[5]=(float)co; return m; };
    // Phase 4 resolvers: map a GLOBAL tensor offset -> its page buffer + page-local offset. No tensor is
    // split across pages, so each weight/grad access lands wholly in one page; single-page models collapse
    // to (page 0, offset unchanged), bit-identical to the pre-paging path. Wp/Gp bind, lc localizes.
    auto pgOf = [&](unsigned off) -> unsigned { return YukiMath::YukiPageOf(st->wgPages.Buffer(), st->wgPages.Size(), off); };
    auto Wp = [&](unsigned off) -> VertexBuffer* { return st->WbP[pgOf(off)]; };
    auto Gp = [&](unsigned off) -> VertexBuffer* { return st->GbP[pgOf(off)]; };
    auto lc = [&](unsigned off) -> unsigned { return off - (unsigned)st->wgPages[pgOf(off)].base; };
    // COPY current gamma (norm scale weights at woff in W) into mdxBuf[3..3+D] for LN_BWD_DX.
    auto loadGamma = [&](unsigned woff) { run(st->cpCS, Wp(woff), nullptr, st->mdxBuf, M((float)D, 3.0f, (float)lc(woff), 0), D); };

    // ── zero G (only needed for the backward; eval skips it) — per page ──
    if (train)
        for (unsigned p = 0; p < st->GbP.Size(); ++p)
        { const unsigned e = st->wgPages[p].elems; run(st->zeroCS, st->GbP[p], st->GbP[p], st->GbP[p], M((float)e, 0, 0, 0), e); }

    // ── FORWARD ──  (binds THIS seq's tokB)
    run(st->efCS, st->WbP[0], tokB, st->hin[0], M((float)S, (float)D, 0, 0), N);   // embedding (offEmb==0 → page 0)
    for (unsigned l = 0; l < L; ++l)
    {
        run(st->moCS, st->hin[l], Wp(st->oQ[l]), st->qC[l], M6(S, D, D, 0, lc(st->oQ[l]), 0), N);
        run(st->moCS, st->hin[l], Wp(st->oK[l]), st->kC[l], M6(S, D, D, 0, lc(st->oK[l]), 0), N);
        run(st->moCS, st->hin[l], Wp(st->oV[l]), st->vC[l], M6(S, D, D, 0, lc(st->oV[l]), 0), N);
        run(st->smCS, st->qC[l], st->kC[l], st->prC[l], M((float)S, (float)D, (float)H, scale), H * S);
        run(st->ctxCS, st->prC[l], st->vC[l], st->cxC[l], M((float)S, (float)D, (float)H, 0), H * S);
        run(st->moCS, st->cxC[l], Wp(st->oO[l]), st->tmpN, M6(S, D, D, 0, lc(st->oO[l]), 0), N);
        run(st->addCS, st->hin[l], st->tmpN, st->aC[l], M((float)N, 0, 0, 0), N);
        run(st->moCS, st->aC[l], Wp(st->oF1[l]), st->f1pC[l], M6(S, D, F, 0, lc(st->oF1[l]), 0), NF);
        run(st->gfCS, st->f1pC[l], st->f1pC[l], st->f1qC[l], M((float)NF, 0, 0, 0), NF);
        run(st->moCS, st->f1qC[l], Wp(st->oF2[l]), st->tmpN, M6(S, F, D, 0, lc(st->oF2[l]), 0), N);
        run(st->addCS, st->aC[l], st->tmpN, st->bC[l], M((float)N, 0, 0, 0), N);
        run(st->cpCS, Wp(st->oNorm[l]), nullptr, st->gbB, M((float)(2 * D), 0, (float)lc(st->oNorm[l]), 0), 2 * D);
        run(st->lnCS, st->bC[l], st->gbB, st->hin[l + 1], M((float)S, (float)D, eps, 0), S);
    }
    run(st->cpCS, Wp(st->offFN), nullptr, st->gbB, M((float)(2 * D), 0, (float)lc(st->offFN), 0), 2 * D);
    run(st->lnCS, st->hin[L], st->gbB, st->finB, M((float)S, (float)D, eps, 0), S);
    run(st->moCS, st->finB, Wp(st->offOut), st->logB, M6(S, D, V, 0, lc(st->offOut), 0), S * V);

    // ── LOSS seed (CROSS_ENTROPY writes dLogits + per-position nll into ceB) ── (binds THIS seq's tgtB + invCount)
    run(st->ceCS, st->logB, tgtB, st->ceB, M((float)S, (float)V, invCount, 0), S);

    // Page loss siphon: COPY this seq's curS per-position nlls (ceB[S*V .. S*V+S)) to lossDst[lossOff].
    // ceB is reused across the page, so the loss MUST be captured here, before the next seq overwrites
    // it (the per-dispatch barrier orders this copy before that overwrite). Single-seq passes lossDst=
    // null and reads ceB directly via ResidentReadLoss.
    if (lossDst)
        run(st->cpCS, st->ceB, nullptr, lossDst, M((float)S, (float)lossOff, (float)(S * V), 0), S);

    if (!train)
        return;   // eval: forward + loss only — weights untouched (caller closes the batch)

    // ── BACKWARD (grads into Gb) ──
    run(st->aoCS, st->finB, st->ceB, Gp(st->offOut), M6(D, S, V, 0, 0, lc(st->offOut)), D * V);
    run(st->boCS, st->ceB, Wp(st->offOut), st->dFin, M6(S, V, D, 0, lc(st->offOut), 0), N);
    loadGamma(st->offFN);
    runD(st->ldxCS, st->hin[L], st->dFin, st->dOut, st->mdxBuf, S);
    run(st->ldwCS, st->hin[L], st->dFin, st->dwdb, M((float)S, (float)D, eps, 0), D);
    run(st->cpCS, st->dwdb, nullptr, Gp(st->offFN), M((float)(2 * D), (float)lc(st->offFN), 0, 0), 2 * D);

    for (int li = (int)L - 1; li >= 0; --li)
    {
        const unsigned l = (unsigned)li;
        loadGamma(st->oNorm[l]);
        runD(st->ldxCS, st->bC[l], st->dOut, st->dB, st->mdxBuf, S);
        run(st->ldwCS, st->bC[l], st->dOut, st->dwdb, M((float)S, (float)D, eps, 0), D);
        run(st->cpCS, st->dwdb, nullptr, Gp(st->oNorm[l]), M((float)(2 * D), (float)lc(st->oNorm[l]), 0, 0), 2 * D);
        // FFN bwd
        run(st->boCS, st->dB, Wp(st->oF2[l]), st->dff1g, M6(S, D, F, 0, lc(st->oF2[l]), 0), NF);
        run(st->aoCS, st->f1qC[l], st->dB, Gp(st->oF2[l]), M6(F, S, D, 0, 0, lc(st->oF2[l])), F * D);
        run(st->gbCS, st->f1pC[l], st->dff1g, st->dff1, M((float)NF, 0, 0, 0), NF);
        run(st->boCS, st->dff1, Wp(st->oF1[l]), st->dhFF, M6(S, F, D, 0, lc(st->oF1[l]), 0), N);
        run(st->aoCS, st->aC[l], st->dff1, Gp(st->oF1[l]), M6(D, S, F, 0, 0, lc(st->oF1[l])), D * F);
        run(st->addCS, st->dB, st->dhFF, st->dA, M((float)N, 0, 0, 0), N);
        // Attn bwd
        run(st->boCS, st->dA, Wp(st->oO[l]), st->dCtx, M6(S, D, D, 0, lc(st->oO[l]), 0), N);
        run(st->aoCS, st->cxC[l], st->dA, Gp(st->oO[l]), M6(D, S, D, 0, 0, lc(st->oO[l])), DD);
        run(st->dvCS, st->prC[l], st->dCtx, st->dVb, M((float)S, (float)D, (float)H, 0), H * S);
        run(st->dpCS, st->dCtx, st->vC[l], st->dPr, M((float)S, (float)D, (float)H, 0), H * S);
        run(st->smbCS, st->prC[l], st->dPr, st->dSc, M((float)S, (float)H, 0, 0), H * S);
        run(st->dqCS, st->dSc, st->kC[l], st->dQ, M((float)S, (float)D, (float)H, scale), H * S);
        run(st->dkCS, st->dSc, st->qC[l], st->dK, M((float)S, (float)D, (float)H, scale), H * S);
        run(st->boCS, st->dQ, Wp(st->oQ[l]), st->dHq, M6(S, D, D, 0, lc(st->oQ[l]), 0), N);
        run(st->boCS, st->dK, Wp(st->oK[l]), st->dHk, M6(S, D, D, 0, lc(st->oK[l]), 0), N);
        run(st->boCS, st->dVb, Wp(st->oV[l]), st->dHv, M6(S, D, D, 0, lc(st->oV[l]), 0), N);
        run(st->aoCS, st->hin[l], st->dQ, Gp(st->oQ[l]), M6(D, S, D, 0, 0, lc(st->oQ[l])), DD);
        run(st->aoCS, st->hin[l], st->dK, Gp(st->oK[l]), M6(D, S, D, 0, 0, lc(st->oK[l])), DD);
        run(st->aoCS, st->hin[l], st->dVb, Gp(st->oV[l]), M6(D, S, D, 0, 0, lc(st->oV[l])), DD);
        run(st->addCS, st->dA, st->dHq, st->dX, M((float)N, 0, 0, 0), N);
        run(st->addCS, st->dX, st->dHk, st->dX, M((float)N, 0, 0, 0), N);
        run(st->addCS, st->dX, st->dHv, st->dX, M((float)N, 0, 0, 0), N);
        st->dOut.Swap(st->dX);
    }
    run(st->ebCS, st->dOut, tokB, st->dEmb, M((float)S, (float)D, (float)V, 0), V * D);
    run(st->cpCS, st->dEmb, nullptr, Gp(st->offEmb), M((float)(V * D), (float)lc(st->offEmb), 0, 0), V * D);
}

void Yuki::ResidentRecordAdam(Vector<SharedPtr<VertexBuffer> >& gradP, float gradScale)
{
    // Records the in-place Adam dispatch(es) into the open compute batch — ONE dispatch per weight page
    // (W -= lr*m_hat/(sqrt(v_hat)+eps) using gradP[p] + that page's m/v). gradP is GbP (single step) or
    // GaccP (page-summed gradient). gradScale pre-multiplies the gradient in the shader: 1/P for a page
    // (turns the SUM into a MEAN so Adam's v sees a constant scale regardless of page size — incl. the
    // partial last page), 1.0 for a single-seq step. Keeps the GPU's effective gradient scale identical
    // to the CPU minibatch. t/bias-correction are GLOBAL (one ++t per Adam call, shared across all pages);
    // each page's pBufP holds its own params (persistent, written once per call — survives the submit).
    YukiResidentState* st = resident_;
    auto* graphics = GetSubsystem<Graphics>();
    if (!st || !graphics) return;
    // Sync the live tuned learning rate: the resident lr was hardcoded at init (0.003) and never
    // tracked /tune, so the GPU ignored Leith's lr entirely (CPU/GPU divergence). betas/eps match
    // the CPU defaults and aren't tunable, so lr is the only sync needed. (Bug found by coder3.)
    if (trainer_)
        st->lr = trainer_->GetLearningRate();
    ++st->t;
    const float bc1 = 1.0f - powf(st->b1, (float)st->t);
    const float bc2 = 1.0f - powf(st->b2, (float)st->t);
    for (unsigned p = 0; p < st->WbP.Size(); ++p)
    {
        const unsigned e = st->wgPages[p].elems;   // this page's element count = Adam's n (params[6])
        Vector<float> params(9);
        params[0] = st->lr; params[1] = st->b1; params[2] = st->b2; params[3] = st->aeps;
        params[4] = bc1; params[5] = bc2; params[6] = (float)e; params[7] = gpuGradClip_;
        params[8] = gradScale;
        st->pBufP[p]->SetData(params.Buffer());
        graphics->SetStorageBuffer(0, st->WbP[p]); graphics->SetStorageBuffer(1, gradP[p]);
        graphics->SetStorageBuffer(2, st->mvP[p]); graphics->SetStorageBuffer(3, st->pBufP[p]);
        graphics->SetComputeShader(st->adamCS);
        graphics->DispatchCompute((e + 255u) / 256u, 1, 1);
        graphics->SetComputeShader(nullptr);
        for (unsigned k = 0; k < 4; ++k) graphics->SetStorageBuffer(k, nullptr);
    }
}

bool Yuki::ResidentStep(bool train)
{
    YukiResidentState* st = resident_;
    auto* graphics = GetSubsystem<Graphics>();
    if (!st || !graphics) return false;

    // P1.5: the WHOLE step is ONE compute batch (one submit + one fence-wait) instead of ~170
    // per-dispatch fence-waits. keepAlive holds the per-dispatch meta buffers until AFTER
    // EndComputeBatch's fence-wait (the GPU reads them at submit), then frees them at scope exit.
    Vector<SharedPtr<VertexBuffer> > keepAlive;
    graphics->BeginComputeBatch();
    ResidentRecordSeq(st->tokB, st->tgtB, st->curS, st->invCount, train, nullptr, 0, keepAlive);
    if (train)
        ResidentRecordAdam(st->GbP);   // single-seq: Adam straight off this seq's gradient (per page)
    graphics->EndComputeBatch();
    return true;
}

bool Yuki::ResidentStepPageRecord(const Vector<Vector<unsigned> >& seqs, unsigned first, unsigned count)
{
    YukiResidentState* st = resident_;
    auto* graphics = GetSubsystem<Graphics>();
    if (!st || !graphics || count == 0) return false;
    // Fail-closed fence guard: never record (this resets the pool cursor and reuses meta slots) while a page
    // is still in flight. Live callers always harvest first, but enforcing it HERE makes the pool's
    // in-flight-reuse safety a property of this function, not caller discipline (found by coder2's review).
    if (st->pageInFlight) return false;
    if (count > st->pageP) count = st->pageP;
    const unsigned S = st->S, V = st->V;   // TW dropped: Gacc zero/accumulate are paged now

    // Upload each seq's tokens/targets into its OWN page buffer (host writes, BEFORE the batch opens —
    // every seq in the page is read by the GPU at the single submit, so they cannot share one buffer).
    // The per-seq reduction inputs (curS, invCount) park in the resident state so the harvest can run
    // AFTER the (possibly async) submit completes.
    st->inflightCurS.Resize(count);
    st->inflightInvCount.Resize(count);
    for (unsigned i = 0; i < count; ++i)
    {
        const Vector<unsigned>& seq = seqs[first + i];
        unsigned len = seq.Size(); if (len > S) len = S; if (len < 1u) len = 1u;
        Vector<float> toksF(S), tgtF(S);
        for (unsigned p = 0; p < S; ++p) { toksF[p] = 0.0f; tgtF[p] = (float)V; }
        unsigned cnt = 0;
        for (unsigned p = 0; p < len; ++p)
        {
            toksF[p] = (float)seq[p];
            unsigned tgt = (p + 1 < len) ? seq[p + 1] : V;   // next-token; last position sentinel
            tgtF[p] = (float)tgt;
            if (tgt < V) ++cnt;
        }
        st->tokBP[i]->SetData(toksF.Buffer());
        st->tgtBP[i]->SetData(tgtF.Buffer());
        st->inflightCurS[i] = len;
        st->inflightInvCount[i] = cnt ? 1.0f / (float)cnt : 0.0f;
    }

    Vector<VertexElement> f1; f1.Push(VertexElement(TYPE_FLOAT, SEM_POSITION));
    Context* ctx = context_;
    // keepAlive lives in the resident state and is REUSED as a persistent meta-buffer pool: rather than
    // freeing the prior page's per-dispatch buffers, we reset the cursor and reuse them. Fence-safe because
    // the prior page always harvested (pageInFlight guard) before this records, so no slot is still in-flight.
    st->metaPoolIdx_ = 0;   // reset pool cursor; buffers persist and are reused (kills the per-dispatch churn)
    Vector<SharedPtr<VertexBuffer> >& keepAlive = st->inflightKeepAlive;
    auto run = [&](ShaderVariation* cs, VertexBuffer* a, VertexBuffer* b, VertexBuffer* c, const Vector<float>& meta, unsigned threads)
    {
        // Pooled meta buffer: reuse a persistent slot in keepAlive instead of allocating one per dispatch.
        // Slot is sized EXACTLY to this dispatch's meta (width is fixed per dispatch-index); the cursor
        // resets per page AFTER the prior page's fence, so no in-flight slot is ever reused mid-page.
        VertexBuffer* dBuf;
        if (st->metaPoolIdx_ < keepAlive.Size())
        {
            dBuf = keepAlive[st->metaPoolIdx_];
            if (dBuf->GetVertexCount() != (int)meta.Size()) dBuf->SetSize(meta.Size(), f1, false);
        }
        else
        {
            SharedPtr<VertexBuffer> nb(new VertexBuffer(ctx)); nb->SetShadowed(false); nb->SetSize(meta.Size(), f1, false);
            keepAlive.Push(nb); dBuf = nb;
        }
        // SetData, NOT SetDataRange: these slots are non-shadowed, and SetDataRange_Vulkan has no no-shadow
        // upload path (it writes via the shadow copy only, then UpdateToGPU early-returns on null shadow) — it
        // would upload NOTHING. SetData_Vulkan takes the direct UploadDataToGPU path, so the meta reaches GPU.
        dBuf->SetData((void*)meta.Buffer());
        ++st->metaPoolIdx_;
        graphics->SetStorageBuffer(0, a); graphics->SetStorageBuffer(1, b ? b : a); graphics->SetStorageBuffer(2, c); graphics->SetStorageBuffer(3, dBuf);
        graphics->SetComputeShader(cs); graphics->DispatchCompute((threads + 255u) / 256u, 1, 1);
        graphics->SetComputeShader(nullptr);
        for (unsigned k = 0; k < 4; ++k) graphics->SetStorageBuffer(k, nullptr);
    };
    auto M = [](float a, float b, float c, float e) -> Vector<float> { Vector<float> m; m.Resize(4); m[0]=a; m[1]=b; m[2]=c; m[3]=e; return m; };

    graphics->BeginComputeBatch();
    // Zero the page gradient accumulator ONCE; then for each seq: full fwd+bwd into Gb (the per-seq
    // scratch), then Gacc += Gb via the audited alias-safe ELEM_ADD (a==c is already used in the proven
    // attn-bwd path). ONE Adam over the SUMMED page gradient. Summing (not averaging by count) matches
    // the CPU minibatch's averaged path to ~1e-8: Adam is scale-invariant in the gradient (the count
    // cancels in m_hat/sqrt(v_hat); only the aeps term differs, negligibly) — well within the ~1e-5 bar.
    // At P=1, Gacc = 0 + Gb is bit-identical to Gb, so this reproduces ResidentStep exactly.
    for (unsigned p = 0; p < st->GaccP.Size(); ++p)
    { const unsigned e = st->wgPages[p].elems; run(st->zeroCS, st->GaccP[p], st->GaccP[p], st->GaccP[p], M((float)e, 0, 0, 0), e); }
    for (unsigned i = 0; i < count; ++i)
    {
        ResidentRecordSeq(st->tokBP[i], st->tgtBP[i], st->inflightCurS[i], st->inflightInvCount[i], true, st->lossSlots, i * S, keepAlive);
        for (unsigned p = 0; p < st->GaccP.Size(); ++p)
        { const unsigned e = st->wgPages[p].elems; run(st->addCS, st->GaccP[p], st->GbP[p], st->GaccP[p], M((float)e, 0, 0, 0), e); }
    }
    // Average the page: 1/count turns the SUMMED Gacc into a MEAN, so Adam's v sees a constant
    // gradient scale regardless of page size (the partial last page no longer poisons v).
    ResidentRecordAdam(st->GaccP, count > 0 ? 1.0f / (float)count : 1.0f);
    // Batch is left OPEN — the caller closes it: EndComputeBatch (blocking) or EndComputeBatchAsync (pump).

    st->inflightCount = count;
    st->pageInFlight = true;
    return true;
}

void Yuki::ResidentStepPageHarvest(float* lossesOut)
{
    YukiResidentState* st = resident_;
    if (!st || !st->pageInFlight) return;
    const unsigned S = st->S;
    const unsigned count = st->inflightCount;

    // ONE readback: lossSlots[i*S .. i*S+curS) holds seq i's per-position nll; mean = sum * invCount
    // (same reduction ResidentReadLoss does on ceB for the single-seq path). Safe only now — the batch
    // that wrote lossSlots has completed (blocking wait returned, or PollComputeBatch() reported done).
    if (lossesOut)
    {
        Vector<float> ls(st->pageP * S);
        if (st->lossSlots->GetData(ls.Buffer()))
        {
            for (unsigned i = 0; i < count; ++i)
            {
                float sum = 0.0f;
                for (unsigned p = 0; p < st->inflightCurS[i]; ++p) sum += ls[i * S + p];
                lossesOut[i] = sum * st->inflightInvCount[i];
            }
        }
        else
            for (unsigned i = 0; i < count; ++i) lossesOut[i] = 0.0f;
    }

    // The GPU is done reading the per-dispatch meta buffers, but they STAY resident as the pool — reused
    // next page, freed only at resident teardown. No per-page Clear: that free/realloc was the churn we killed.
    st->pageInFlight = false;
}

bool Yuki::ResidentStepPage(const Vector<Vector<unsigned> >& seqs, unsigned first, unsigned count, float* lossesOut)
{
    // Blocking page = record → blocking close (submit + bounded wait + reclaim) → harvest. Behaviour is
    // identical to the pre-A2 monolithic ResidentStepPage; the async main-tick pump uses the same halves
    // with EndComputeBatchAsync + PollComputeBatch between them.
    auto* graphics = GetSubsystem<Graphics>();
    if (!graphics || !lossesOut) return false;
    if (!ResidentStepPageRecord(seqs, first, count)) return false;
    graphics->EndComputeBatch();
    ResidentStepPageHarvest(lossesOut);
    return true;
}

unsigned Yuki::ComputeGpuPageSize(unsigned L) const
{
    // P is bounded by the worker descriptor pool: a whole page's descriptor sets must coexist until
    // its single submit completes (the pool is reset once per page). setsPerSeq = the per-seq fwd+bwd
    // dispatches (= the proven single step's 39L+15) + 1 ELEM_ADD accumulate. Activations are REUSED
    // across the page, so VRAM barely scales with P — the descriptor pool is the real constraint.
    // Cap at Pcap; floor at 1 (== the proven single-seq path).
    const unsigned setsPerSeq = 39u * L + 16u;
    const unsigned sharedSets = 8u;       // zero Gacc + Adam + slack
    const unsigned poolCeil = 8192u;      // descriptor sets we're willing to allocate per worker pool
    const unsigned Pcap = 16u;
    unsigned Pdesc = (poolCeil > sharedSets) ? (poolCeil - sharedSets) / (setsPerSeq ? setsPerSeq : 1u) : 1u;
    unsigned P = Pdesc; if (P > Pcap) P = Pcap; if (P < 1u) P = 1u;
    return P;
}

bool Yuki::ResidentReadWeights(float* out)
{
    // Flat readback: each page written back at its GLOBAL base. `out` must hold TW floats — for the
    // verify/debug paths only (small models). Production sync uses ResidentReadWeightsToModel (paged).
    if (!resident_ || !out) return false;
    YukiResidentState* st = resident_;
    for (unsigned p = 0; p < st->WbP.Size(); ++p)
        if (!st->WbP[p]->GetData(out + st->wgPages[p].base)) return false;
    return true;
}

bool Yuki::ResidentReadWeightsToModel(PagedFloatBuffer& dst)
{
    // GPU->paged-model readback WITHOUT a flat TW-float host buffer: read each GPU page into a page-sized
    // scratch and scatter it into the (independently-paged) model buffer. The production sync path.
    if (!resident_ || !dst.IsConfigured()) return false;
    YukiResidentState* st = resident_;
    Vector<float> scratch(st->maxPageElems ? st->maxPageElems : 1u);
    for (unsigned p = 0; p < st->WbP.Size(); ++p)
    {
        const unsigned e = st->wgPages[p].elems;
        if (!st->WbP[p]->GetData(scratch.Buffer())) return false;   // reads this page's e floats
        ScatterPagedRange(dst, st->wgPages[p].base, e, scratch.Buffer());
    }
    return true;
}

bool Yuki::ResidentUploadWeights(const PagedFloatBuffer& w)
{
    // Per-page upload: gather each GPU page's logical range from the (independently-paged) source and
    // SetData it into that page's resident buffer. No flat TW-float scratch — one page-sized buffer.
    // NOTE: SetDataRange can't be used — it only copies through a shadow buffer, and the resident
    // buffers are non-shadowed (SetShadowed(false)); a ranged write would upload nothing. SetData
    // uploads the whole page buffer (its own e floats), so a full per-page SetData is correct.
    if (!resident_ || !w.IsConfigured()) return false;
    YukiResidentState* st = resident_;
    Vector<float> scratch(st->maxPageElems ? st->maxPageElems : 1u);
    for (unsigned p = 0; p < st->WbP.Size(); ++p)
    {
        const unsigned e = st->wgPages[p].elems;
        GatherPagedRange(w, st->wgPages[p].base, e, scratch.Buffer());
        st->WbP[p]->SetData(scratch.Buffer());
    }
    return true;
}

bool Yuki::ResidentZeroAdam()
{
    // Zero the GPU-resident Adam moments (m,v interleaved = 2*elems per page). The CPU trainer's
    // ResetAdam() clears the WRONG Adam on the GPU pump — the live optimiser state lives in mvP here.
    // Without this, a restore/kick uploads elite W but the stale drift momentum survives resident and
    // the very next ResidentRecordAdam step carries the weights right back off the floor.
    if (!resident_ || resident_->mvP.Empty()) return false;
    YukiResidentState* st = resident_;
    Vector<float> zeros(st->maxPageElems ? 2u * st->maxPageElems : 2u);
    for (unsigned i = 0; i < zeros.Size(); ++i) zeros[i] = 0.0f;
    for (unsigned p = 0; p < st->mvP.Size(); ++p)
        st->mvP[p]->SetData(zeros.Buffer());   // SetData reads this page's 2*elems floats
    // Reset the Adam TIMESTEP too, or the bias correction (1/(1-b^t)) stays "warm" while m,v are
    // cold — the first post-restore step is taken as if Adam were converged (~3x an oversized step),
    // kicking the weights right back off the elite. A true cold restart needs t=0 (mirrors the CPU
    // ResetAdam, which zeros adamStep_). This is the upward spike after every yank-to-floor.
    st->t = 0;
    return true;
}

bool Yuki::ResidentSoftRestart()
{
    // Soft Adam restart for the pawl's yank-to-floor: zero the VELOCITY (m) but KEEP the curvature (v)
    // AND the timestep. Each mvP page is interleaved [m0,v0, m1,v1, ...], so m lives at 2i, v at 2i+1.
    //
    // A full cold reset (ResidentZeroAdam: m=v=0, t=0) makes the first re-entry step ~lr*sign(g) on
    // EVERY param — a coordinated signSGD kick off the elite. Then m re-accumulates those uphill
    // gradients into "false momentum" that keeps climbing. Keeping v means the re-entry step is
    // curvature-SCALED (small in steep directions) instead of a flat launch, and zeroing m means no
    // stale velocity is carried in — a gentle landing at the floor instead of a rebound off it.
    if (!resident_ || resident_->mvP.Empty()) return false;
    YukiResidentState* st = resident_;
    Vector<float> mv(st->maxPageElems ? 2u * st->maxPageElems : 2u);
    for (unsigned p = 0; p < st->mvP.Size(); ++p)
    {
        const unsigned e = st->wgPages[p].elems;
        if (!st->mvP[p]->GetData(mv.Buffer())) return false;   // reads this page's 2*e floats
        for (unsigned i = 0; i < e; ++i) mv[2u * i] = 0.0f;     // zero m; v (2i+1) and st->t left intact
        st->mvP[p]->SetData(mv.Buffer());
    }
    return true;
}

void Yuki::SyncResidentToModel()
{
    if (!resident_ || !model_ || !model_->IsLoaded()) return;
    // TryAcquire (NOT block): the pump must never block on trainMutex_, or /gputrain stop's join-under-
    // the-held-mutex would deadlock. A skipped periodic sync is benign — the next one catches up, and
    // /gputrain stop does the authoritative final sync main-thread after the join. (Pre-Phase-4 the
    // GPU->host readback ran OFF the lock into a flat TW scratch; the paged readback scatters straight
    // into the model buffer, so it runs inside the lock — safe now: the GPU pump is main-thread (A2.3),
    // no worker races trainMutex_, and the readback holds the lock only for its own duration.)
    if (trainMutex_.try_lock())   // EXCLUSIVE, non-blocking — never blocks the pump (join-safe)
    {
        ResidentReadWeightsToModel(model_->GetWeights());   // scatter the GPU readback into the paged store
        trainMutex_.unlock();
    }
}

void Yuki::SyncResidentToModelLocked()
{
    if (!resident_ || !model_ || !model_->IsLoaded()) return;
    ResidentReadWeightsToModel(model_->GetWeights());   // caller already holds trainMutex_ (no concurrency)
}

void Yuki::SetResidentSequence(const unsigned* tokens, unsigned len)
{
    YukiResidentState* st = resident_;
    if (!st || !tokens || len < 2) return;
    if (len > st->S) len = st->S;   // clamp to the allocated maxSeqLen
    const unsigned V = st->V;

    // tokB/tgtB were allocated at the max length; upload a max-length array with the first
    // `len` real and the rest padded (never read — dispatches use curS=len).
    Vector<float> toksF(st->S), tgtF(st->S);
    for (unsigned i = 0; i < st->S; ++i) { toksF[i] = 0.0f; tgtF[i] = (float)V; }
    unsigned count = 0;
    for (unsigned i = 0; i < len; ++i)
    {
        toksF[i] = (float)tokens[i];
        unsigned tgt = (i + 1 < len) ? tokens[i + 1] : V;   // next-token; last position sentinel
        tgtF[i] = (float)tgt;
        if (tgt < V) ++count;
    }
    st->tokB->SetData(toksF.Buffer());
    st->tgtB->SetData(tgtF.Buffer());
    st->invCount = count ? 1.0f / (float)count : 0.0f;
    st->curS = len;
}

float Yuki::ResidentReadLoss()
{
    YukiResidentState* st = resident_;
    if (!st) return 0.0f;
    const unsigned curS = st->curS, V = st->V, allocS = st->S;
    // ceB was allocated at the MAX length; GetData copies the WHOLE buffer, so the host vector
    // MUST be the full allocation size — sizing it to curS*V+curS overflows the heap when
    // curS < maxSeqLen (the real-corpus case). The nll sits at ceB[curS*V + p] (CE used S=curS).
    Vector<float> ce(allocS * V + allocS);
    if (!st->ceB->GetData(ce.Buffer())) return 0.0f;
    float sum = 0.0f; for (unsigned p = 0; p < curS; ++p) sum += ce[curS * V + p];
    return sum * st->invCount;
}

float Yuki::ResidentEvalLoss(const unsigned* tokens, unsigned len)
{
    if (!resident_ || !tokens || len < 2) return 0.0f;
    SetResidentSequence(tokens, len);
    ResidentStep(false);   // forward only — weights untouched
    return ResidentReadLoss();
}

void Yuki::ResidentShutdown()
{
    if (resident_) { delete resident_; resident_ = nullptr; }
}

bool Yuki::StartGpuTraining(bool announce)
{
    // Factored /gputrain-start body — also called at startup (A4: GPU-all-the-way default).
    if (gpuPumpActive_)
        return true;
    auto* graphics = GetSubsystem<Graphics>();
    VulkanGraphicsImpl* vkImpl = graphics ? graphics->GetImpl_Vulkan() : nullptr;
    if (!graphics || !vkImpl)
    { if (announce) LogMessage("GPUtrain needs the Vulkan backend.", Color::RED); return false; }
    if (!hasBrain_ || !model_ || !model_->IsLoaded() || !trainer_)
    { if (announce) LogMessage("GPUtrain needs a loaded brain + trainer.", Color::RED); return false; }

    // [gpuboot] freeze-localization: the boot path calls this with announce=false, so on success nothing
    // reaches the log — a wedge here (or in the first pump tick) leaves the log dead at the brain load.
    // Time the three heavy sub-steps to Yuki.log (grep '[gpuboot]') so the next boot pinpoints the stall.
    HiresTimer _gtTimer;
    URHO3D_LOGINFO("[gpuboot] StartGpuTraining: ENTER (announce=" + String(announce ? 1 : 0) + ")");

    const YukiTopology& t = model_->GetTopology();
    const unsigned D = t.embedDim, F = t.ffDim, V = t.vocabSize, H = t.nHeads, L = t.nLayers, maxS = t.maxSeqLen;
    Vector<unsigned> dummy(maxS); for (unsigned i = 0; i < maxS; ++i) dummy[i] = 0;
    if (!ResidentInit(D, F, V, H, L, maxS, model_->GetWeights(), dummy.Buffer()))
    { if (announce) LogMessage("[GPUtrain] ResidentInit failed.", Color::RED); return false; }
    URHO3D_LOGINFO("[gpuboot] StartGpuTraining: ResidentInit returned (+" + String(_gtTimer.GetUSec(false) / 1000) + " ms)");

    gpuSeqs_.Clear();
    trainer_->BuildTokenSet(gpuSeqs_);   // shared regime tokenization (main thread); upfront data check
    if (gpuSeqs_.Empty())
    { if (announce) LogMessage("[GPUtrain] no trainable sequences.", Color::RED); ResidentShutdown(); return false; }
    URHO3D_LOGINFO("[gpuboot] StartGpuTraining: BuildTokenSet done, " + String(gpuSeqs_.Size()) + " seqs (+" + String(_gtTimer.GetUSec(false) / 1000) + " ms)");

    // A2.3: own the compute cmd + descriptor pools on the MAIN thread and register them as the explicit
    // batch override (tid-independent — the pump runs on main where main tid == render tid, so tid routing
    // would hand back the shared frame pool that BeginFrame resets, destroying our in-flight sets). Size
    // the descriptor pool for a whole minibatch page. Phase 4: the Gb-zero, the Gacc-accumulate and the
    // Adam step are now ONE dispatch PER WEIGHT PAGE (np), not one each — so a page's set count is
    // np(zero Gacc) + pageP*(39L+14 fwd/bwd + np add) + np(Adam) = pageP*(39L+14+2np) + 2np. For a
    // single-page model (np==1) this is pageP*(39L+16)+2, matching the pre-paging budget. No worker
    // thread now, so the override can no longer collide (the A1 segfault cause).
    const unsigned pageP = resident_->pageP;
    const unsigned pageL = resident_->L;
    const unsigned np = resident_->wgPages.Size();
    const uint32_t maxSets = (uint32_t)(pageP * (39u * pageL + 14u + 2u * np) + 2u * np + 8u);
    resident_->gpuCmdPool = vkImpl->CreateThreadComputePool();
    resident_->gpuDescPool = (resident_->gpuCmdPool != VK_NULL_HANDLE)
        ? vkImpl->CreateThreadComputeDescriptorPool(maxSets, maxSets * 4u) : VK_NULL_HANDLE;
    if (resident_->gpuCmdPool == VK_NULL_HANDLE || resident_->gpuDescPool == VK_NULL_HANDLE)
    {
        if (resident_->gpuCmdPool != VK_NULL_HANDLE)
        { vkImpl->DestroyCommandPool(resident_->gpuCmdPool); resident_->gpuCmdPool = VK_NULL_HANDLE; }
        if (announce) LogMessage("[GPUtrain] failed to create compute pools.", Color::RED);
        ResidentShutdown();
        return false;
    }
    vkImpl->SetComputeBatchPools(resident_->gpuCmdPool, resident_->gpuDescPool);
    vkImpl->ClearComputeHang();   // fresh run: clear any hang latched by a previous run
    URHO3D_LOGINFO("[gpuboot] StartGpuTraining: compute pools created (+" + String(_gtTimer.GetUSec(false) / 1000) + " ms)");

    // Reset the pump state machine for a clean run, then arm it.
    gpuPhase_ = GPU_IDLE;
    gpuBatchIds_.Clear(); gpuBatchSeqs_.Clear(); gpuBatchLosses_.Clear();
    gpuPageStart_ = 0; gpuPassCount_ = 0; gpuStepCount_ = 0;
    gpuForceKick_ = false;
    gpuGradEma_ = 0.0f; gpuGradSpiking_ = false;   // fresh run: re-seed the gradMax spike baseline
    gpuPumpActive_ = true;
    // Commit: pause the CPU TrainOnce worker ONLY now that the GPU pump is live (flag, not Stop()/join —
    // that would deadlock under a held trainMutex_). Leaving GPU resumes it via the solver-mode switch.
    trainWorker_.SetPaused(true);
    URHO3D_LOGINFO("[gpuboot] StartGpuTraining: ARMED — pump active, returning to caller (+" + String(_gtTimer.GetUSec(false) / 1000) + " ms). Next marker is the first pump tick's dispatch (glslang compile).");
    if (announce)
        LogMessage(String("[GPUtrain] STARTED — off-render GPU training of the LIVE model (") + String(gpuSeqs_.Size()) +
            " seqs, minibatch page P=" + String(resident_ ? resident_->pageP : 1u) +
            "). CPU worker paused. Watch the loss graph + UI responsive.", Color(0.6f, 0.8f, 1.0f));
    return true;
}

bool Yuki::QuiesceGpuWorker()
{
    // Stop the main-tick GPU pump without losing progress: drain any in-flight page + free the pools,
    // authoritative resident->model_ sync, then free resident. Used by the solver-mode switch when leaving
    // GPU (->CPU or ->RUN); the caller decides whether to resume the CPU trainer. There is NO thread to
    // join now (the point of A2.3) — teardown is a bounded fence drain, so it cannot deadlock under a held
    // trainMutex_. (Name kept for call-site stability; it quiesces the pump, not a worker.)
    if (!gpuPumpActive_)
        return false;
    StopGpuPump();                 // drain in-flight + free pools — uses resident_, so BEFORE ResidentShutdown
    SyncResidentToModelLocked();   // capture the GPU-trained weights into model_ (caller holds trainMutex_)
    ResidentShutdown();
    return true;
}

void Yuki::StopGpuPump()
{
    // Bounded drain of the pump: if a page is in flight, poll its fence to completion so we never free the
    // cmd pool from under a live cmd buffer; then clear the override and destroy the pools. Safe to call
    // when idle. resident_ must still be alive (it holds the pool handles) — call BEFORE ResidentShutdown.
    gpuPumpActive_ = false;
    auto* graphics = GetSubsystem<Graphics>();
    VulkanGraphicsImpl* vkImpl = graphics ? graphics->GetImpl_Vulkan() : nullptr;
    if (graphics && gpuPhase_ == GPU_INFLIGHT)
    {
        // PollComputeBatch reclaims the cmd buffer + fence on completion; its own poll budget latches
        // computeHang_ and returns true if the GPU is wedged, so this loop is bounded even on a hang.
        while (!graphics->PollComputeBatch())
            Time::Sleep(1);
        ResidentStepPageHarvest(nullptr);   // release the page's keepAlive; losses discarded on teardown
    }
    gpuPhase_ = GPU_IDLE;
    if (vkImpl)
    {
        vkImpl->SetComputeBatchPools(VK_NULL_HANDLE, VK_NULL_HANDLE);   // stop routing compute onto our pools
        vkImpl->SetComputeBatchTimeoutScale(1u);
    }
    if (vkImpl && resident_)
    {
        if (resident_->gpuCmdPool != VK_NULL_HANDLE)
        { vkImpl->DestroyCommandPool(resident_->gpuCmdPool); resident_->gpuCmdPool = VK_NULL_HANDLE; }
        if (resident_->gpuDescPool != VK_NULL_HANDLE)
        { vkImpl->DestroyDescriptorPool(resident_->gpuDescPool); resident_->gpuDescPool = VK_NULL_HANDLE; }
    }
    if (trainer_)
        trainer_->FlushLedger();   // leaving GPU mode: persist any priority updates since the last cadence flush
}

String Yuki::EscapeExecuteRung(int rung)
{
    YukiTrainer* tr = trainer_;
    if (!tr)
        return "no-trainer";
    // All rungs are ELITE-PROTECTED: DifferenceMoveSweep is a forward-only probe that only jumps on a
    // surpass (never regresses); LR-restart just rescales the step; RunPbtKick keeps the best candidate. A
    // failed move costs compute, never the floor. Increasing-F diff-moves are the "small->large" magnitude
    // escalation (larger F extrapolates further along the elite-vs-prior difference = a bigger jump).
    switch (rung)
    {
    case 0: { bool s = tr->DifferenceMoveSweep(1.0f); if (s && model_) ResidentUploadWeights(model_->GetWeights()); return "diff-move F=1.0"; }
    case 1: { bool s = tr->DifferenceMoveSweep(2.0f); if (s && model_) ResidentUploadWeights(model_->GetWeights()); return "diff-move F=2.0"; }
    case 2: { bool s = tr->DifferenceMoveSweep(3.5f); if (s && model_) ResidentUploadWeights(model_->GetWeights()); return "diff-move F=3.5"; }
    case 3: { escapeLrSaved_ = tr->GetLearningRate(); tr->SetLearningRate(escapeLrSaved_ * 4.0f); escapeLrRestoreIn_ = 40; return "LR warm-restart x4 (40 passes)"; }
    case 4: {
        // PBT on a BACKGROUND thread (never the render thread). The GPU pump is paused while escapePbtRunning_
        // so the worker has exclusive access to trainer_/model_ (the CPU worker is already paused in GPU mode).
        // Kept SMALL — 4 members x 3 CPU passes = 12 — because CPU passes are slow and pause GPU progress; a
        // heartbeat logs while it runs so it's never mistaken for a hang. Result applied on completion.
        escapePbtRunning_ = true;
        escapePbtPollTicks_ = 0;
        escapePbtWorker_.Configure(tr, 0.05f, 3, 4);
        escapePbtWorker_.Run();
        return "PBT pop=4 x3 passes (background — pump paused; ~12 CPU passes)";
    }
    default: return "none";
    }
}

void Yuki::EscapeStep()
{
    if (!trainer_)
        return;
    const float floor = trainer_->GetBestLoss();

    // Ladder start: nothing prior to judge — record the floor and fire rung 0.
    if (escapeRung_ < 0)
    {
        escapeLastFloor_ = floor;
        escapeRung_ = 0;
        LogMessage("[escape] ladder start @ floor " + String(floor, 4) + " — rung 0 (" +
            EscapeExecuteRung(0) + ")", Color(0.9f, 0.85f, 0.4f));
        return;
    }

    // Judge the previous rung: did the floor drop meaningfully since we fired it?
    if (floor < escapeLastFloor_ - 0.0005f)
    {
        LogMessage("[escape] rung " + String(escapeRung_) + " PAID OFF: floor " + String(escapeLastFloor_, 4) +
            " -> " + String(floor, 4) + " — banked; back to rung 0.", Color(0.5f, 1.0f, 0.5f));
        escapeFailedLadders_ = 0;
        escapeRung_ = 0;
        escapeLastFloor_ = floor;
        EscapeExecuteRung(0);
        return;
    }

    // No gain — escalate, or (at the top) count a failed ladder and maybe declare a hard floor.
    if (escapeRung_ >= escapeMaxRung_)
    {
        ++escapeFailedLadders_;
        if (escapeFailedLadders_ >= escapeMaxFailedLadders_)
        {
            LogMessage("[escape] HARD FLOOR: " + String(floor, 4) + " survived the full ladder " +
                String(escapeFailedLadders_) + "x — looks like a capacity limit for this config (needs a "
                "bigger model or narrower data, not more knobs). Disarming /escape.", Color(1.0f, 0.5f, 0.5f));
            escapeArmed_ = false;
            escapeRung_ = -1;
            return;
        }
        LogMessage("[escape] ladder exhausted (" + String(escapeFailedLadders_) + "/" +
            String(escapeMaxFailedLadders_) + ") at floor " + String(floor, 4) + " — restarting ladder.",
            Color(0.9f, 0.6f, 0.3f));
        escapeRung_ = 0;
        escapeLastFloor_ = floor;
        EscapeExecuteRung(0);
        return;
    }

    ++escapeRung_;
    escapeLastFloor_ = floor;
    LogMessage("[escape] no gain @ " + String(floor, 4) + " — escalate to rung " + String(escapeRung_) +
        " (" + EscapeExecuteRung(escapeRung_) + ")", Color(0.9f, 0.85f, 0.4f));
}

void Yuki::PumpGpuSolver()
{
    // Main-tick GPU solver: advance ONE non-blocking step per call. Replaces YukiGpuTrainWorker's loop.
    // Because this, inference, and RestoreBest/Expand all run on the main thread, the regime updates the
    // worker guarded with try_lock(trainMutex_) run here with NO lock — main is the single coordinator
    // (SCOPE_poll_conversions.md, the lock-model simplification). One state transition per tick keeps
    // per-frame cost bounded; a P-page pass takes ~(pages+1) ticks, invisible at 60 fps on Yuki's render.
    YukiResidentState* st = resident_;
    auto* graphics = GetSubsystem<Graphics>();
    VulkanGraphicsImpl* vkImpl = graphics ? graphics->GetImpl_Vulkan() : nullptr;
    YukiTrainer* tr = trainer_;
    if (!st || !graphics || !vkImpl || !tr)
        return;

    // Escape harness PBT rung runs on a background thread — PAUSE the pump while it's in flight (the worker
    // needs exclusive access to trainer_/model_). Poll for completion; on finish, apply + resume. The UI
    // stays live because HandleUpdate keeps rendering; we simply don't advance training this tick.
    if (escapePbtRunning_)
    {
        if (escapePbtWorker_.IsFinished())
        {
            escapePbtWorker_.Stop();   // join (instant; thread already exited)
            const bool surpassed = escapePbtWorker_.Surpassed();
            if (surpassed && model_)
                ResidentUploadWeights(model_->GetWeights());
            escapePbtRunning_ = false;
            LogMessage(String("[escape] PBT rung finished (") + String(escapePbtPollTicks_ / 60) + "s) — " +
                (surpassed ? "surpassed the floor." : "held (no gain)."), Color(0.8f, 0.85f, 0.5f));
        }
        else if (++escapePbtPollTicks_ % 180 == 0)   // heartbeat ~every 3s so it's never mistaken for a hang
        {
            LogMessage("[escape] PBT running on worker… " + String(escapePbtPollTicks_ / 60) +
                "s (GPU pump paused; UI live)", Color(0.7f, 0.75f, 0.5f));
        }
        return;   // pump paused until PBT completes
    }

    if (vkImpl->ComputeHangDetected())
        return;   // wedged — stop advancing; StopGpuPump drains + frees on mode-leave / exit

    const unsigned pageP = st->pageP;
    const unsigned TW = st->TW;
    const int eval = tr->GetEvalEvery();

    // ── IN FLIGHT: poll the outstanding page. If the GPU is still busy, return (no core spent waiting).
    // If it just completed, harvest + advance and FALL THROUGH to submit the next page THIS SAME tick —
    // so a finished page is immediately refed instead of wasting a frame on a poll-only tick. We still
    // submit at most ONE page per tick (the record below returns), so the per-frame cost stays bounded. ──
    if (gpuPhase_ == GPU_INFLIGHT)
    {
        if (!graphics->PollComputeBatch())
            return;   // still running — try next tick (no core spent waiting)
        // A latched hang ALSO makes PollComputeBatch return true — it force-reclaims the wedged
        // page (Graphics_Vulkan.cpp:2693) so teardown stays clean. Guard before harvesting:
        // ResidentStepPageHarvest on a page whose GPU work never completed reads stale/garbage
        // Gacc and poisons training. The entry guard (ComputeHangDetected, above) only catches a
        // hang latched on a PRIOR tick; this catches one latched by THIS tick's poll. computeHang_
        // stays set, so the pump stops advancing from here on and StopGpuPump drains on mode-leave.
        if (vkImpl->ComputeHangDetected())
            return;
        // [gpuboot] freeze-localization: first fence signal of the run — proves the GPU actually executed
        // the first page (not just recorded it). If the FIRST-record marker logged but this never does, the
        // GPU is wedged on the first dispatch (device-lost / async-queue submit), not the CPU-side compile.
        if (gpuPassCount_ == 0 && gpuStepCount_ == 0)
            URHO3D_LOGINFO("[gpuboot] PumpGpuSolver: FIRST page fence SIGNALLED — GPU executed page 0; boot GPU path is live");
        ResidentStepPageHarvest(&gpuBatchLosses_[gpuPageStart_]);
        // TEMP instrumentation: read back the page's summed gradient (Gacc) NOW — the fence has signalled
        // (PollComputeBatch true) so the GPU is idle and Gacc holds exactly the gradient Adam just stepped
        // with (Adam reads g, writes W/mv; it never touches Gacc). Compute max|g| + finiteness on host to
        // tell floor-walk (bounded) from NaN/Inf poison (spike/non-finite). Reused scratch = no realloc.
        if (!st->GaccP.Empty())
        {
            if (gradScan_.Size() != st->maxPageElems) gradScan_.Resize(st->maxPageElems);   // page-sized scratch
            float gmax = 0.0f; bool finite = true; bool ok = true;
            for (unsigned pg = 0; pg < st->GaccP.Size() && ok; ++pg)
            {
                const unsigned e = st->wgPages[pg].elems;
                if (!st->GaccP[pg]->GetData(gradScan_.Buffer())) { ok = false; break; }
                for (unsigned gi = 0; gi < e; ++gi)
                {
                    const float a = Abs(gradScan_[gi]);
                    if (!IsNaN(a) && a != M_INFINITY) { if (a > gmax) gmax = a; }
                    else finite = false;
                }
            }
            if (ok)
            {
                gpuLastGradMax_ = gmax; gpuLastGradFinite_ = finite;

                // Event-gated divergence log — restores the machine-readable signal the removed per-pass
                // [Train] gradMax line carried, without the spam. Emits to Yuki.log (severity-tagged, so
                // telemetry / a blind coder can grep it) AND the UI console. Two arms:
                //   NON-FINITE → log every occurrence: unambiguous NaN/Inf poison; you want the ongoing signal.
                //   SPIKE      → gradMax > K× a slow EMA baseline, EDGE-triggered (log only on entry into
                //                spike) so sustained divergence logs once, not every pass. EMA seeds lazily.
                if (!finite)
                {
                    const String m = "Yuki GPU: gradMax NON-FINITE — training diverged (pass "
                        + String(gpuPassCount_) + ")";
                    URHO3D_LOGERROR(m);
                    LogMessage(m, Color(1.0f, 0.4f, 0.4f));
                }
                else
                {
                    if (gpuGradEma_ <= 0.0f)
                        gpuGradEma_ = gmax;      // seed the baseline; no decision on the first sample
                    else
                    {
                        // Relative arm: gradMax > gradSpikeK_ × EMA baseline (catches sudden jumps).
                        // Absolute arm: gradMax > gradCeil_ (0 = off) — catches a SLOW finite ramp the EMA
                        // tracks straight past the relative arm. Either trips; EDGE-triggered via the latch
                        // so a sustained divergence logs on entry, not every pass. Knobs are /tune-live.
                        const bool relSpike = (gmax > gradSpikeK_ * gpuGradEma_) && (gmax > 1e-6f);
                        const bool absOver  = (gradCeil_ > 0.0f) && (gmax > gradCeil_);
                        const bool over = relSpike || absOver;
                        if (over && !gpuGradSpiking_)   // edge: transition INTO the flagged state
                        {
                            String why = relSpike
                                ? (String(gmax / gpuGradEma_, 1) + "x baseline " + String(gpuGradEma_, 6))
                                : String::EMPTY;
                            if (absOver)
                                why += (why.Empty() ? String::EMPTY : String(", ")) + "over ceil " + String(gradCeil_, 6);
                            const String m = "Yuki GPU: gradMax " + String(gmax, 6) + " — divergence ("
                                + why + ", pass " + String(gpuPassCount_) + ")";
                            URHO3D_LOGWARNING(m);
                            LogMessage(m, Color(0.9f, 0.7f, 0.4f));
                        }
                        gpuGradSpiking_ = over;
                        gpuGradEma_ = gradEmaAlpha_ * gmax + (1.0f - gradEmaAlpha_) * gpuGradEma_;
                    }
                }
            }
        }
        gpuStepCount_ += st->inflightCount;
        gpuPageStart_ += pageP;
        gpuPhase_ = GPU_IDLE;
        // fall through
    }

    // ── IDLE: need a batch? select one (main thread — no lock) ──
    if (gpuBatchSeqs_.Empty())
    {
        unsigned unk = 0;
        tr->SelectTrainingBatch(gpuBatchIds_, gpuBatchSeqs_, unk);
        if (gpuBatchSeqs_.Empty())
            return;   // nothing trainable yet — idle this tick
        gpuBatchLosses_.Resize(gpuBatchSeqs_.Size());
        gpuPageStart_ = 0;
    }

    // ── more pages in this pass? record + submit the next one async ──
    if (gpuPageStart_ < gpuBatchSeqs_.Size())
    {
        unsigned cnt = gpuBatchSeqs_.Size() - gpuPageStart_; if (cnt > pageP) cnt = pageP;
        vkImpl->SetComputeBatchTimeoutScale(pageP);      // a P-seq page is ~P× longer than a single step
        vkImpl->ResetDescriptorPool(st->gpuDescPool);    // safe: only reached when IDLE (prev page's fence signalled)
        // [gpuboot] freeze-localization: the FIRST page record of the run creates every compute pipeline,
        // which is where glslang compiles all ~22 CS variations SYNCHRONOUSLY on this (render) thread — the
        // prime freeze suspect. Time it once (pass 0, page 0). If the log shows a long gap here, the fix is
        // to pre-warm/compile the pipelines off the render thread (or cache SPIR-V) before arming the pump.
        const bool _firstRec = (gpuPassCount_ == 0 && gpuPageStart_ == 0);
        HiresTimer _recTimer;
        if (_firstRec)
            URHO3D_LOGINFO("[gpuboot] PumpGpuSolver: FIRST page record BEGIN — creating compute pipelines (glslang compile, synchronous on render thread)");
        if (ResidentStepPageRecord(gpuBatchSeqs_, gpuPageStart_, cnt))
        {
            graphics->EndComputeBatchAsync();
            gpuPhase_ = GPU_INFLIGHT;
            if (_firstRec)
                URHO3D_LOGINFO("[gpuboot] PumpGpuSolver: FIRST page recorded + submitted async (+" + String(_recTimer.GetUSec(false) / 1000) + " ms) — pipelines compiled; awaiting fence");
        }
        else
        {
            gpuPageStart_ += pageP;   // record failed — skip this page rather than stall
            if (_firstRec)
                URHO3D_LOGWARNING("[gpuboot] PumpGpuSolver: FIRST page record FAILED (+" + String(_recTimer.GetUSec(false) / 1000) + " ms) — page skipped");
        }
        return;
    }

    // ── pass complete: regime update + kick, all inline on main (no lock) ──
    vkImpl->SetComputeBatchTimeoutScale(1u);
    ++gpuPassCount_;
    float passMeanLoss = 0.0f;
    for (unsigned i = 0; i < gpuBatchLosses_.Size(); ++i) passMeanLoss += gpuBatchLosses_[i];
    if (gpuBatchLosses_.Size()) passMeanLoss /= (float)gpuBatchLosses_.Size();
    gpuLastPassLoss_ = passMeanLoss;            // HandleUpdate feeds the progress graph off this
    gpuLastPassSamples_ = gpuBatchSeqs_.Size(); // reinforced count for this pass (status/graph)
    gpuPassDone_ = gpuPassCount_;               // per-new-pass trigger for the graph + UI

    // Throttled baseline readout: the HEALTHY gradMax scale the divergence detector rides on, sampled to
    // Yuki.log so it's readable off-process. The event-gated arms stay silent when nothing diverges, so
    // without this the baseline is invisible except on the UI status bar — leaving gradceil un-tunable
    // without eyes on the screen. Every 200 passes = low-rate (throttle the LOG), and it sits here at
    // pass-complete, off the per-step training path — so it costs the training loop nothing.
    if (gpuLastGradFinite_ && (gpuPassCount_ % 200u) == 0u)
        URHO3D_LOGINFO("Yuki GPU: gradMax baseline — last " + String(gpuLastGradMax_, 6) +
            ", EMA " + String(gpuGradEma_, 6) + " (pass " + String(gpuPassCount_) + ")");

    // VMA memory watch: sample total LIVE-allocation bytes under the pump so a run PROVES the
    // deferred-deletion fix instead of anyone eyeballing a graph — flat = solved, climbing = still leaking.
    // Throttled with the baseline (vmaCalculateStatistics is O(pools), trivial at 1/200 passes) and off the
    // per-step path. allocationCount is the direct tell: an undestroyed buffer stays a LIVE allocation, so a
    // starved deferred-deletion queue makes both the count and the bytes climb pass over pass.
    if (vkImpl && (gpuPassCount_ % 200u) == 0u)
    {
        VmaTotalStatistics vmaStats{};
        vmaCalculateStatistics(vkImpl->GetAllocator(), &vmaStats);
        const VmaStatistics& vt = vmaStats.total.statistics;
        URHO3D_LOGINFO("Yuki GPU: VMA watch — live " + String((unsigned)(vt.allocationBytes >> 20)) + " MB in "
            + String(vt.allocationCount) + " allocs, reserved " + String((unsigned)(vt.blockBytes >> 20))
            + " MB (pass " + String(gpuPassCount_) + ") — flat=solved, climbing=leak");
    }

    const bool ratchetThisPass = (eval > 0 && (gpuPassCount_ % (unsigned)eval) == 0);
    tr->RecordTrainResults(gpuBatchIds_, gpuBatchLosses_);   // RAM ledger only — NO per-pass disk write
    tr->UpdatePlateau(passMeanLoss);            // feed the plateau tracker (IsPlateaued)

    // Escape harness: anneal a warm-restart LR back to base after its window (rung 3). One tick per pass.
    if (escapeLrRestoreIn_ > 0 && --escapeLrRestoreIn_ == 0)
    {
        tr->SetLearningRate(escapeLrSaved_);
        LogMessage("[escape] LR warm-restart annealed back to " + String(escapeLrSaved_, 5) + ".",
            Color(0.8f, 0.8f, 0.5f));
    }

    if (ratchetThisPass && TW && model_)
    {
        ResidentReadWeightsToModel(model_->GetWeights());   // push GPU-trained weights into the paged model_ (paged, no flat TW scratch)
        tr->MaybeResampleProbe();
        // The pawl: capture on a new best, OR restore .best when the probe regresses past
        // bestLoss*(1+ratchetCeil_). Capture-only (RatchetProbe) let the resident weights climb with
        // no ceiling — on a restore we must also re-upload model_ back into the resident buffer, or
        // the GPU keeps training from its drifted state and the clamp never lands.
        const int rg = tr->RatchetGuard(ratchetCeil_);
        if (rg < 0 && TW && tr->HasElite())   // HasElite: rg==-1 with no snapshot means RestoreBest no-op'd
        {
            ResidentUploadWeights(model_->GetWeights());   // yank the GPU back to the floor
            ResidentSoftRestart();   // zero velocity m (kill false momentum) but KEEP curvature v — a
                                     // curvature-scaled re-entry, not a signSGD launch off the floor
            ++gpuRatchetRestores_;
        }
        gpuLastEvalLoss_ = tr->GetBestLoss();             // status = the live floor
        if (rg > 0)          // rg==1: RatchetGuard captured a new floor (breakthrough)
            tr->FlushLedger();   // breakthrough only: piggyback the RAM priority ledger on the new-best (.best) write
    }

    // Trailing best-flush (GPU cadence): land a throttled-out .best that RatchetGuard/CaptureBest deferred.
    // Independent of a new best, so a CONVERGED best still reaches disk instead of dying on an unclean exit.
    // No-op unless dirty AND the throttle interval elapsed. The CPU worker is paused in GPU mode, so this
    // never races the CPU-side flush on the shared dirty/timer state.
    tr->MaybeFlushBestToDisk();

    // Plateau escape (difference-move): FORCED by a manual /kick, or AUTO when armed + stagnated. Cheap
    // forward-only probe; on a surpass, jump model_ -> upload back into resident so the GPU continues
    // from the lower basin.
    const bool forced = gpuForceKick_;
    // When /escape is armed, the escalation LADDER drives plateau escape instead of the single fixed-F
    // auto-kick (they're mutually exclusive — escape supersedes autokick/ratchet). Manual /kick still works.
    const bool escapeNow = ratchetThisPass && escapeArmed_ && tr->IsPlateaued();
    const bool autoK = ratchetThisPass && !escapeArmed_ && (autoKick_ || ratchet_) && tr->IsPlateaued();
    const float kickF = forced ? gpuKickFbase_ : 1.0f;
    if (forced) gpuForceKick_ = false;
    if (forced || autoK)
    {
        bool surpassed = tr->DifferenceMoveSweep(kickF);
        if (surpassed && TW && model_)
            ResidentUploadWeights(model_->GetWeights());
        gpuKickSurpassed_ = surpassed;
        ++gpuKickCount_;
        if (!forced)   // manual kicks don't touch the arm flags; auto kicks disarm
        {
            if (autoKick_) autoKick_ = false;              // one-shot disarm
            if (ratchet_ && !surpassed) ratchet_ = false;   // held = true floor
        }
    }
    else if (escapeNow)
    {
        EscapeStep();   // adaptive ladder: judge the last rung, escalate/bank, act (all elite-protected)
    }

    // If EscapeStep just launched the background PBT rung, stop here — the worker now owns trainer_/model_
    // until it finishes (the pump is paused from the top next tick). Do NOT touch tr below this tick.
    if (escapePbtRunning_)
        return;

    // Re-select the next pass's priority batch and reset the page cursor.
    unsigned unk = 0;
    tr->SelectTrainingBatch(gpuBatchIds_, gpuBatchSeqs_, unk);
    gpuBatchLosses_.Resize(gpuBatchSeqs_.Size());
    gpuPageStart_ = 0;
}

// ── Three-state learner control (CPU learn / GPU learn / RUN frozen) ─────────────────────────────
// The mode is DERIVED from real worker state, never stored — so the button, the /solver command,
// and the legacy /gputrain command can never disagree. Leith's invariant: the proven CPU solver is
// never discarded; the GPU solver has to stand up head-to-head before it earns the default.

Yuki::SolverMode Yuki::CurrentSolverMode() const
{
    if (gpuPumpActive_)            return SOLVER_GPU;   // GPU resident worker is learning
    if (trainWorker_.IsPaused())      return SOLVER_RUN;   // neither learns — frozen, inference only
    return SOLVER_CPU;                                     // CPU trainer is learning
}

void Yuki::SetSolverMode(SolverMode mode)
{
    // Pause the CPU worker BEFORE blocking on the EXCLUSIVE lock when heading away from CPU learning.
    // The worker re-acquires trainMutex_ SHARED back-to-back every pass; glibc's std::shared_mutex is
    // reader-preferring, so a stream of re-acquiring readers can starve this writer. SetPaused is a
    // lock-free flag the worker checks BETWEEN passes (Yuki.h ThreadFunction), so setting it here lets
    // the in-flight pass drain and halts re-acquisition — the exclusive acquire then completes (bounded
    // by one pass instead of forever). GPU/RUN targets pause anyway in SetSolverModeLocked; a CPU target
    // is entered from GPU/RUN where the worker is already paused, so there is no reader stream to break.
    if (mode != SOLVER_CPU)
        trainWorker_.SetPaused(true);
    std::unique_lock<std::shared_mutex> lock(trainMutex_);   // button handler runs on the UI thread holding no lock
    SetSolverModeLocked(mode);
}

void Yuki::SetSolverModeLocked(SolverMode target)
{
    // Caller holds trainMutex_ (SetSolverMode locks; /solver runs inside ProcessInput's lock).
    if (CurrentSolverMode() == target)
        return;
    // Leaving GPU? QuiesceGpuWorker stops the worker, does the authoritative resident->model_ sync
    // (no progress lost), frees resident, and leaves the CPU worker PAUSED — exactly the seam the
    // RUN state needs (unlike /gputrain stop, which always resumes CPU).
    if (gpuPumpActive_ && target != SOLVER_GPU)
        QuiesceGpuWorker();
    switch (target)
    {
    case SOLVER_CPU:
        trainWorker_.SetPaused(false);   // CPU resumes learning
        LogMessage("[Solver] CPU — learning on the proven CPU trainer.", Color(0.5f, 0.8f, 0.5f));
        break;
    case SOLVER_GPU:
        if (!gpuPumpActive_ && !StartGpuTraining(true))
        {
            // Vulkan/brain missing — fall back honestly to CPU rather than show a GPU we didn't start.
            trainWorker_.SetPaused(false);
            LogMessage("[Solver] GPU unavailable — stayed on CPU.", Color(0.8f, 0.6f, 0.3f));
        }
        break;
    case SOLVER_RUN:
        trainWorker_.SetPaused(true);    // neither trainer learns; weights frozen, inference still live
        LogMessage("[Solver] RUN — frozen, inference only (no learning).", Color(0.6f, 0.7f, 0.85f));
        break;
    }
    UpdateSolverButton();
}

void Yuki::HandleSolverButton(StringHash, VariantMap&)
{
    // Cycle CPU → GPU → RUN → CPU.
    const SolverMode cur = CurrentSolverMode();
    const SolverMode next = (cur == SOLVER_CPU) ? SOLVER_GPU
                          : (cur == SOLVER_GPU) ? SOLVER_RUN
                                                : SOLVER_CPU;
    SetSolverMode(next);
}

void Yuki::UpdateSolverButton()
{
    if (!solverButton_ || !solverButtonText_)
        return;
    switch (CurrentSolverMode())
    {
    case SOLVER_CPU:
        solverButtonText_->SetText("LEARNING: CPU   (click -> GPU)");
        solverButton_->SetColor(Color(0.16f, 0.40f, 0.20f));   // steady green — the proven solver
        break;
    case SOLVER_GPU:
        solverButtonText_->SetText("LEARNING: GPU   (click -> RUN)");
        solverButton_->SetColor(Color(0.55f, 0.42f, 0.12f));   // amber — on trial, watch it stand up
        break;
    case SOLVER_RUN:
        solverButtonText_->SetText("RUN - frozen, not learning   (click -> CPU)");
        solverButton_->SetColor(Color(0.22f, 0.26f, 0.34f));   // grey-blue — idle, inference only
        break;
    }
}

// A2.3: YukiGpuTrainWorker is deleted — GPU production training now runs on the main-tick pump
// (Yuki::PumpGpuSolver). The old off-render worker loop lived here; it is gone with the thread.
// See Claude/SCOPE_poll_conversions.md (Conversion A) and the git history for the removed body.

void Yuki::ProcessInput(const String& input)
{
    // Runs on the UI thread alongside the training worker. The lock is now NARROW, not a
    // whole-handler sledgehammer: COMMANDS take the weight lock EXCLUSIVE + the DB mutex (they may
    // restore weights / write the corpus, and they're infrequent); INFERENCE (the conversation
    // path below) takes the weight lock SHARED, so it runs concurrently with a training pass and
    // the UI stays responsive. (weight->db acquisition order matches TrainOnce — no deadlock.)

    // Commands start with /
    if (input.StartsWith("/"))
    {
        // Bare-param aliases: every /tune knob also gets a direct command, like /sigma == /tune sigma.
        // "/lr 3e-4" -> "/tune lr 3e-4", re-dispatched so the /tune handler stays the ONE implementation.
        // (sigma/ratchet/autokick already have their own richer commands — deliberately not shadowed here.)
        if (input.Length() > 1)
        {
            const Vector<String> w = input.Substring(1).Split(' ');
            const String word = w.Empty() ? String::EMPTY : w[0].ToLower();
            static const char* kBareTune[] = { "lr", "batch", "staleness", "rate", "gradk", "gradema", "gradceil" };
            for (const char* t : kBareTune)
            {
                if (word == t)
                {
                    const String rest = input.Substring(1 + word.Length()).Trimmed();
                    ProcessInput(String("/tune ") + t + (rest.Empty() ? String::EMPTY : " " + rest));
                    return;
                }
            }
        }

        std::unique_lock<std::shared_mutex> wlock(trainMutex_);
        std::unique_lock<std::mutex> dlock(dbMutex_);
        if (input == "/stats" || input == "/status")
        {
            LogMessage(GetCorpusStats());
        }
        else if (input == "/memories")
        {
            if (memoryDb_)
            {
                DbResult r = memoryDb_->Execute("SELECT COUNT(*) FROM memories");
                const Vector<VariantVector>& rows = r.GetRows();
                int count = rows.Empty() ? 0 : rows[0][0].GetI32();
                LogMessage("I hold " + String(count) + " memories.", Color(0.6f, 0.8f, 1.0f));

                // Show last 5 with ids (ids are how /forget targets a memory).
                DbResult recent = memoryDb_->Execute(
                    "SELECT id, text FROM memories ORDER BY id DESC LIMIT 5");
                const Vector<VariantVector>& rRows = recent.GetRows();
                if (!rRows.Empty())
                {
                    LogMessage("Most recent:", Color(0.6f, 0.8f, 1.0f));
                    for (unsigned i = 0; i < rRows.Size(); ++i)
                    {
                        int id = rRows[i][0].GetI32();
                        String text = rRows[i][1].GetString();
                        if (text.Length() > 100)
                            text = text.Substring(0, 100) + "...";
                        LogMessage("  [" + String(id) + "] " + text, Color(0.7f, 0.7f, 0.7f));
                    }
                }
            }
        }
        else if (input.StartsWith("/remember "))
        {
            String fact = input.Substring(10).Trimmed();
            if (!fact.Empty())
            {
                Remember(fact, "Leith (direct)");
                LogMessage("Remembered.", Color(0.6f, 0.8f, 1.0f));
            }
        }
        else if (input.StartsWith("/recall "))
        {
            String query = input.Substring(8).Trimmed();
            Vector<String> matches = RecallMemories(query, 10);
            if (matches.Empty())
                LogMessage("Nothing comes to mind.", Color(0.6f, 0.8f, 1.0f));
            else
            {
                LogMessage("I remember " + String(matches.Size()) + " things about that:", Color(0.6f, 0.8f, 1.0f));
                for (unsigned i = 0; i < matches.Size(); ++i)
                {
                    String text = matches[i];
                    if (text.Length() > 120)
                        text = text.Substring(0, 120) + "...";
                    LogMessage("  " + text, Color(0.7f, 0.7f, 0.7f));
                }
            }
        }
        else if (input.StartsWith("/forget "))
        {
            // Manual, operator-only deletion — the ONLY delete path now (training
            // keeps every memory). Removes one memory by id; echoes what it dropped.
            if (!memoryDb_)
            {
                LogMessage("Memory not connected.", Color::RED);
                return;
            }
            String arg = input.Substring(8).Trimmed();
            int id = ToI32(arg);
            if (arg.Empty() || id <= 0)
                LogMessage("Usage: /forget <id>   (see ids with /memories)", Color(0.8f, 0.6f, 0.3f));
            else
            {
                DbResult ex = memoryDb_->Execute("SELECT text FROM memories WHERE id = " + String(id));
                const Vector<VariantVector>& exRows = ex.GetRows();
                if (exRows.Empty())
                    LogMessage("No memory with id " + String(id) + ".", Color(0.8f, 0.6f, 0.3f));
                else
                {
                    String text = exRows[0][0].GetString();
                    memoryDb_->Execute("DELETE FROM memories WHERE id = " + String(id));
                    if (memoryCount_ > 0)
                        --memoryCount_;  // keep the lock-free telemetry counter current
                    if (text.Length() > 80)
                        text = text.Substring(0, 80) + "...";
                    LogMessage("Forgotten [" + String(id) + "]: " + text, Color(0.8f, 0.6f, 0.3f));
                }
            }
        }
        else if (input == "/coverage")
        {
            // One-shot vocab-coverage scan. Tokens that miss the vocab map to
            // <unk> and can never be predicted — that fraction of the loss is a
            // vocab floor, removable only by growing the vocabulary, not training.
            if (!memoryDb_ || !model_ || !model_->IsLoaded())
            {
                LogMessage("Need a brain and memory to scan.", Color::RED);
                return;
            }
            const unsigned vocab = model_->GetTopology().vocabSize;
            DbResult all = memoryDb_->Execute("SELECT text FROM memories");
            const Vector<VariantVector>& rows = all.GetRows();
            unsigned total = 0, unknown = 0;
            Vector<String> sampleMisses;
            for (unsigned i = 0; i < rows.Size(); ++i)
            {
                Vector<String> words = YukiModel::Tokenize(rows[i][0].GetString());
                for (const String& word : words)
                {
                    String clean = word.Trimmed();
                    if (clean.Empty())
                        continue;
                    ++total;
                    if (model_->GetTokenIndex(clean) >= vocab)
                    {
                        ++unknown;
                        if (sampleMisses.Size() < 12 && !sampleMisses.Contains(clean))
                            sampleMisses.Push(clean);
                    }
                }
            }
            float ratio = total ? (100.0f * unknown / total) : 0.0f;
            LogMessage("Coverage: " + String(total) + " tokens, " + String(unknown) +
                " unknown (" + String(ratio, 1) + "%)", Color(0.6f, 0.8f, 1.0f));
            if (unknown > 0)
            {
                String top;
                for (unsigned i = 0; i < sampleMisses.Size(); ++i)
                    top += sampleMisses[i] + " ";
                LogMessage("  unknown incl: " + top, Color(0.7f, 0.7f, 0.7f));
                LogMessage("  that " + String(ratio, 1) + "% is a vocab floor — training can't remove it.",
                    Color(0.7f, 0.7f, 0.7f));
            }
        }
        else if (input.StartsWith("/kick") || input.StartsWith("/move"))
        {
            // Difference move (graceful escape): extrapolate along the lineage's
            // improvement direction best + F·(best − prevBest), probe a few F, jump to
            // the best candidate that beats the floor — else a clean no-op. Directed and
            // cheap (forward-only probe), unlike the old cosmic-radiation kick.
            if (!hasBrain_ || !trainer_)
            {
                LogMessage("No brain to move.", Color::RED);
                return;
            }
            float fbase = 1.0f;
            String arg = input.Substring(5).Trimmed();
            if (!arg.Empty())
                fbase = (float)atof(arg.CString());
            if (fbase <= 0.0f)
                fbase = 1.0f;
            kickStartLoss_ = trainer_->GetBestLoss();
            // GPU-all-the-way: the CPU worker is paused, so route the manual move to the GPU pump
            // (it runs the same DifferenceMoveSweep on the main tick). Falls back to the CPU worker.
            if (gpuPumpActive_)
            {
                RequestGpuKick(fbase);
            }
            else
            {
                if (trainWorker_.PbtBusy())
                {
                    LogMessage("A move is already running — wait for it to finish.", Color(0.9f, 0.7f, 0.4f));
                    return;
                }
                trainWorker_.RequestPbtKick(fbase, 0);   // fbase carried in the sigma slot
            }
            LogMessage("Difference move (F~" + String(fbase, 2) + ", floor was " +
                String(kickStartLoss_, 3) + ")…", Color(0.9f, 0.7f, 0.4f));
        }
        else if (input.StartsWith("/autokick"))
        {
            // "on [sigma]" — optional inline sigma, matching /kick.
            Vector<String> a = input.Substring(9).Trimmed().Split(' ');
            autoKick_ = (!a.Empty() && a[0] == "on");
            if (autoKick_ && a.Size() > 1)
            {
                float s = (float)atof(a[1].CString());
                if (s > 0.0f)
                    ratchetSigma_ = s;
            }
            LogMessage(String("Auto-kick ") + (autoKick_ ?
                "ON (sigma " + String(ratchetSigma_, 3) + ") — one kick on plateau, then disarm." : "OFF."),
                Color(0.6f, 0.8f, 1.0f));
        }
        else if (input.StartsWith("/escape"))
        {
            // Adaptive plateau-escape ladder (semi-autonomous). On each plateau it escalates through
            // increasingly aggressive/diverse ELITE-PROTECTED moves (diff-move F=1/2/3.5 -> LR warm-restart
            // -> PBT), banks any that lower the floor, and declares an HONEST hard floor if the full ladder
            // fails repeatedly. Supersedes autokick/ratchet while armed. Drives the GPU trainer (GPU mode).
            Vector<String> a = input.Substring(7).Trimmed().Split(' ');
            const String op = a.Empty() ? String::EMPTY : a[0];
            if (op == "on")
            {
                escapeArmed_ = true;
                escapeRung_ = -1;
                escapeFailedLadders_ = 0;
                escapeLastFloor_ = 1e30f;
                if (a.Size() > 1) { int m = ToI32(a[1]); if (m > 0) escapeMaxFailedLadders_ = m; }
                LogMessage("Escape ladder ARMED — on plateau it escalates diff-move F=1/2/3.5 -> LR-restart -> "
                    "PBT (all elite-protected), banks gains, declares a hard floor after " +
                    String(escapeMaxFailedLadders_) + " failed ladders. GPU mode. Watch [escape] lines.",
                    Color(0.6f, 1.0f, 0.6f));
                if (!gpuPumpActive_)
                    LogMessage("  (note: escape drives the GPU trainer — start /gputrain for it to act.)",
                        Color(0.8f, 0.8f, 0.5f));
            }
            else if (op == "off")
            {
                escapeArmed_ = false;
                escapeRung_ = -1;
                LogMessage("Escape ladder OFF.", Color(0.6f, 0.8f, 1.0f));
            }
            else
            {
                LogMessage(String("Escape ") + (escapeArmed_ ? "ARMED" : "off") + " — rung " +
                    String(escapeRung_) + ", failed-ladders " + String(escapeFailedLadders_) + "/" +
                    String(escapeMaxFailedLadders_) + ", floor " + String(trainer_ ? trainer_->GetBestLoss() : 0.0f, 4) +
                    ". Usage: /escape on [maxFailedLadders] | /escape off", Color(0.6f, 0.8f, 1.0f));
            }
        }
        else if (input.StartsWith("/ingest"))
        {
            // Bulk text importer: file | directory | http(s) URL -> chunked into `memories`, which the trainer
            // already consumes. RUN mode only (writes memories while the trainer is frozen — no DB contention).
            const String arg = input.Length() > 7 ? input.Substring(7).Trimmed() : String::EMPTY;
            if (arg.Empty())
            {
                LogMessage("Usage: /ingest <file | directory | http(s)://url>", Color(0.8f, 0.6f, 0.3f));
                return;
            }
            if (CurrentSolverMode() != SOLVER_RUN)
            {
                LogMessage("Switch to RUN mode first (click the solver button or /solver run) — ingest writes "
                    "memories while the trainer is frozen.", Color(0.8f, 0.6f, 0.3f));
                return;
            }
            if (!memoryDb_)
            {
                LogMessage("No memory DB.", Color::RED);
                return;
            }
            if (ingesting_ || ingestProcessing_)
            {
                LogMessage("An ingest is already in progress — wait for it to finish.", Color(0.8f, 0.6f, 0.3f));
                return;
            }

            if (arg.StartsWith("http://") || arg.StartsWith("https://"))
            {
                // Fetch via the `curl` CLI — robust TLS + redirect handling that civetweb's mg_download lacks.
                // Runs off-thread (SystemRunAsync) so network latency never blocks the render thread; the
                // exit code arrives as E_ASYNCEXECFINISHED, where we read the temp file and ingest it.
                auto* fsn = GetSubsystem<FileSystem>();
                if (!asyncExecSubscribed_)
                {
                    SubscribeToEvent(E_ASYNCEXECFINISHED, URHO3D_HANDLER(Yuki, HandleAsyncExecFinished));
                    asyncExecSubscribed_ = true;
                }
                ingestTmpPath_ = fsn->GetProgramDir() + "Data/GameDB/.ingest_download.tmp";
                Vector<String> cargs;
                cargs.Push("-sSL");                              // silent, show errors, follow redirects
                cargs.Push("--max-time"); cargs.Push("180");     // hard timeout
                cargs.Push("-A"); cargs.Push("Yuki/1.0 (+ingest)");
                cargs.Push("-o"); cargs.Push(ingestTmpPath_);
                cargs.Push(arg);
                ingestCurlReqId_ = fsn->SystemRunAsync("curl", cargs);
                if (ingestCurlReqId_ == M_MAX_UNSIGNED || ingestCurlReqId_ == 0)
                {
                    LogMessage("Couldn't launch curl (is it installed / are allowed-paths set?).", Color::RED);
                    ingestCurlReqId_ = 0;
                    return;
                }
                ingestLabel_ = "ingest:" + arg;
                ingesting_ = true;
                LogMessage("Fetching " + arg + " via curl … (ingests when complete; UI stays live)",
                    Color(0.6f, 0.8f, 1.0f));
                return;
            }

            auto* fs = GetSubsystem<FileSystem>();
            if (fs->DirExists(arg))
            {
                // Queue every supported file; StepIngestProcess streams them one blob at a time across frames.
                Vector<String> files, queue;
                fs->ScanDir(files, arg, "*", SCAN_FILES, true);   // recurse
                unsigned skipped = 0;
                for (const String& rel : files)
                {
                    if (IsSupportedTextExt(rel)) queue.Push(arg + "/" + rel);
                    else ++skipped;
                }
                if (queue.Empty())
                {
                    LogMessage("No supported text files under " + arg + " (" + String(skipped) + " skipped).",
                        Color(0.8f, 0.6f, 0.3f));
                    return;
                }
                LogMessage("Ingesting " + String(queue.Size()) + " file(s) from " + arg + " (" + String(skipped) +
                    " non-text skipped)…", Color(0.6f, 0.8f, 1.0f));
                BeginIngest(String::EMPTY, queue, "ingest:" + arg);   // empty first blob -> pulls the queue
                return;
            }
            if (fs->FileExists(arg))
            {
                File f(context_, arg, FILE_READ);
                if (!f.IsOpen()) { LogMessage("Can't open " + arg, Color::RED); return; }
                String text; text.Resize(f.GetSize());
                f.Read(&text[0], f.GetSize()); f.Close();
                BeginIngest(text, Vector<String>(), "ingest:" + arg);
                return;
            }
            LogMessage("Not a file, directory, or http(s) URL: " + arg, Color(0.8f, 0.6f, 0.3f));
        }
        else if (input.StartsWith("/ratchet"))
        {
            // Kick-until-first-rollback: keep kicking on each plateau until one
            // fails to beat the elite, i.e. the true floor. "on [sigma]" like /kick.
            Vector<String> a = input.Substring(8).Trimmed().Split(' ');
            ratchet_ = (!a.Empty() && a[0] == "on");
            if (ratchet_ && a.Size() > 1)
            {
                float s = (float)atof(a[1].CString());
                if (s > 0.0f)
                    ratchetSigma_ = s;
            }
            LogMessage(String("Ratchet ") + (ratchet_ ?
                "ON (sigma " + String(ratchetSigma_, 3) + ") — kick each plateau until the floor holds." : "OFF.") +
                " Pawl ceiling +" + String(ratchetCeil_ * 100.0f, 1) + "% (/ceil).",
                Color(0.6f, 0.8f, 1.0f));
        }
        else if (input == "/ceil" || input.StartsWith("/ceil "))
        {
            // The ratchet pawl: on the GPU pump, restore .best when the probe regresses past
            // bestLoss*(1+ceil). "/ceil 0.08" = 8% headroom; "/ceil 0" disables the restore arm.
            String a = input.Substring(5).Trimmed();
            if (!a.Empty())
            {
                float c = (float)atof(a.CString());
                if (c >= 0.0f)
                    ratchetCeil_ = c;
                else { LogMessage("ceil must be >= 0 (0 disables the pawl)", Color(1.0f, 0.5f, 0.5f)); return; }
            }
            LogMessage(ratchetCeil_ > 0.0f ?
                "Pawl ceiling = +" + String(ratchetCeil_ * 100.0f, 1) + "% over .best (restores on breach)." :
                "Pawl DISABLED (capture-only; loss can climb with no ceiling).",
                Color(0.6f, 0.8f, 1.0f));
        }
        else if (input == "/gclip" || input.StartsWith("/gclip "))
        {
            // Per-element gradient value-clip for the GPU Adam step (params[7]). Clamps each summed-page
            // gradient component to [-c, c] BEFORE the moment update, so the intermittent 300..6500 spikes
            // can't poison Adam's slow v and shove the weights uphill. "/gclip 0" disables (default).
            // Tune it against the gradMax readout: set it just above the honest baseline (a few), below the
            // spikes. Takes effect on the next Adam dispatch — no rebuild.
            String a = input.Substring(6).Trimmed();
            if (!a.Empty())
            {
                float c = (float)atof(a.CString());
                if (c >= 0.0f)
                    gpuGradClip_ = c;
                else { LogMessage("gclip must be >= 0 (0 disables)", Color(1.0f, 0.5f, 0.5f)); return; }
            }
            LogMessage(gpuGradClip_ > 0.0f ?
                "GPU grad clip = ±" + String(gpuGradClip_, 3) + " per element (spikes clamped before Adam)." :
                "GPU grad clip DISABLED (raw gradient — spikes can poison Adam's v).",
                Color(0.6f, 0.8f, 1.0f));
        }
        else if (input == "/gradcmp")
        {
            // DIAGNOSTIC: compute the gradient of ONE sequence on the CPU (reference) and on the GPU
            // (Gacc) from IDENTICAL weights, and compare per region. The forward already matches
            // (loss agrees), so a per-region divergence here pinpoints which backward op is wrong.
            auto* graphics = GetSubsystem<Graphics>();
            if (!graphics || !resident_ || !trainer_ || !model_ || !model_->IsLoaded())
            { LogMessage("gradcmp needs the GPU resident active (/gputrain) + a loaded brain.", Color(1.0f, 0.5f, 0.5f)); return; }

            // The blocking GPU step below opens its own compute batch — it CANNOT coexist with the
            // async pump's in-flight page (double BeginComputeBatch is what crashed). Drain the pump
            // to idle first (keeps resident_ alive; the CPU worker is already paused). One-shot: the
            // pump stays paused afterwards — re-run /gputrain to resume.
            if (gpuPumpActive_) StopGpuPump();

            Vector<Vector<unsigned> > seqs;
            if (trainer_->BuildTokenSet(seqs) == 0 || seqs.Empty())
            { LogMessage("gradcmp: no trainable sequences.", Color(1.0f, 0.5f, 0.5f)); return; }
            Vector<Vector<unsigned> > one; one.Push(seqs[0]);   // one seq = cleanest per-seq backward test

            const unsigned TW = resident_->TW;
            const YukiTopology& t = model_->GetTopology();
            const unsigned emb = (unsigned)((unsigned long long)t.vocabSize * t.embedDim);
            const unsigned outStart = TW - (unsigned)((unsigned long long)t.embedDim * t.vocabSize);

            ResidentUploadWeights(model_->GetWeights());   // GPU == CPU model

            PagedFloatBuffer cpuGrad;
            const float cpuMax = trainer_->ComputeBatchGradient(one, cpuGrad);
            Vector<float> cpuFlat(TW); cpuGrad.CopyToFlat(cpuFlat.Buffer());

            Vector<float> losses(1);
            if (!ResidentStepPage(one, 0, 1, losses.Buffer()))
            { LogMessage("gradcmp: GPU step failed.", Color(1.0f, 0.5f, 0.5f)); return; }
            Vector<float> gacc(TW);   // flat debug readback (small model): each page at its global base
            for (unsigned p = 0; p < resident_->GaccP.Size(); ++p)
                resident_->GaccP[p]->GetData(gacc.Buffer() + resident_->wgPages[p].base);

            ResidentUploadWeights(model_->GetWeights());   // undo the GPU Adam step
            ResidentZeroAdam();

            auto rmax = [](const Vector<float>& v, unsigned lo, unsigned hi) -> float
            { float m = 0.0f; for (unsigned i = lo; i < hi; ++i) { float a = v[i] < 0 ? -v[i] : v[i]; if (a > m) m = a; } return m; };
            const float gpuMax = rmax(gacc, 0, TW);
            LogMessage("[gradcmp] 1 seq  max|g|  CPU=" + String(cpuMax, 4) + "  GPU=" + String(gpuMax, 4), Color(0.7f, 0.9f, 1.0f));
            LogMessage("  embedding CPU=" + String(rmax(cpuFlat, 0, emb), 4) + "  GPU=" + String(rmax(gacc, 0, emb), 4), Color(0.6f, 0.8f, 1.0f));
            LogMessage("  layers    CPU=" + String(rmax(cpuFlat, emb, outStart), 4) + "  GPU=" + String(rmax(gacc, emb, outStart), 4), Color(0.6f, 0.8f, 1.0f));
            LogMessage("  outProj   CPU=" + String(rmax(cpuFlat, outStart, TW), 4) + "  GPU=" + String(rmax(gacc, outStart, TW), 4), Color(0.6f, 0.8f, 1.0f));
            LogMessage("  emb[0] CPU=" + String(cpuFlat[0], 5) + " GPU=" + String(gacc[0], 5) +
                       "   out[0] CPU=" + String(cpuFlat[outStart], 5) + " GPU=" + String(gacc[outStart], 5), Color(0.6f, 0.8f, 1.0f));
            LogMessage("gradcmp done — GPU pump paused. Re-run /gputrain to resume training.", Color(0.7f, 0.9f, 1.0f));
            return;
        }
        else if (input == "/tune" || input.StartsWith("/tune "))
        {
            // ONE consistent interface for every training knob:
            //   /tune                  — show all values + state
            //   /tune <param>          — show one  (lr | batch | staleness | sigma | rate | gradk | gradema | gradceil | ratchet | autokick)
            //   /tune <param> <value>  — set one, then echo the value that took
            if (!trainer_)
            {
                LogMessage("No trainer.", Color(0.8f, 0.6f, 0.3f));
                return;
            }
            const Color ok(0.6f, 0.8f, 1.0f), bad(0.8f, 0.6f, 0.3f);
            Vector<String> a = (input.Length() > 6 ? input.Substring(6).Trimmed() : String::EMPTY).Split(' ');

            if (a.Empty())
            {
                LogMessage("Tune: lr=" + String(trainer_->GetLearningRate(), 5) +
                    " batch=" + String(trainer_->GetReinforceBatch()) +
                    " staleness=" + String(trainer_->GetStaleness(), 4) +
                    " sigma=" + String(ratchetSigma_, 4) +
                    " rate=" + String(trainWorker_.GetDelayMs()) + "ms", ok);
                LogMessage("gradMax detector: gradk=" + String(gradSpikeK_, 2) +
                    " gradema=" + String(gradEmaAlpha_, 3) +
                    " gradceil=" + (gradCeil_ > 0.0f ? String(gradCeil_, 4) : String("off")), ok);
                String floorStr = trainer_->HasElite() ? String(trainer_->GetBestLoss(), 3) : String("—");
                LogMessage(String("State: ratchet=") + (ratchet_ ? "on" : "off") +
                    " autokick=" + (autoKick_ ? "armed" : "off") +
                    " floor=" + floorStr + (trainer_->IsPlateaued() ? " PLATEAU" : ""), ok);
            }
            else
            {
                const String p = a[0].ToLower();
                const bool set = a.Size() > 1;
                const String vs = set ? a[1] : String::EMPTY;

                if (p == "lr")
                {
                    if (set) { float v = (float)atof(vs.CString()); if (v > 0.0f) trainer_->SetLearningRate(v); else { LogMessage("lr must be > 0", bad); return; } }
                    LogMessage("lr = " + String(trainer_->GetLearningRate(), 5), ok);
                }
                else if (p == "batch")
                {
                    if (set) { int k = ToI32(vs); if (k > 0) trainer_->SetReinforceBatch((unsigned)k); else { LogMessage("batch must be > 0", bad); return; } }
                    LogMessage("batch = " + String(trainer_->GetReinforceBatch()), ok);
                }
                else if (p == "staleness")
                {
                    if (set) { float s = (float)atof(vs.CString()); if (s >= 0.0f) trainer_->SetStaleness(s); else { LogMessage("staleness must be >= 0", bad); return; } }
                    LogMessage("staleness = " + String(trainer_->GetStaleness(), 4), ok);
                }
                else if (p == "sigma")
                {
                    if (set) { float s = (float)atof(vs.CString()); if (s > 0.0f) ratchetSigma_ = s; else { LogMessage("sigma must be > 0", bad); return; } }
                    LogMessage("sigma = " + String(ratchetSigma_, 4), ok);
                }
                else if (p == "rate")
                {
                    // Route to the LIVE trainer: in GPU mode the CPU worker is paused. The main-tick pump
                    // advances once per frame (no inter-step sleep), so gpuDelayMs_ is only a stored readout
                    // now — it no longer throttles. Kept so /rate get/set doesn't error in GPU mode.
                    if (set) { int ms = ToI32(vs); if (ms >= 0) { if (gpuPumpActive_) gpuDelayMs_ = (unsigned)ms; else trainWorker_.SetDelayMs((unsigned)ms); } else { LogMessage("rate must be >= 0", bad); return; } }
                    LogMessage("rate = " + String(gpuPumpActive_ ? gpuDelayMs_ : trainWorker_.GetDelayMs()) + "ms", ok);
                }
                else if (p == "gradk")
                {
                    // GPU gradMax spike multiple: divergence flagged when gradMax > gradk × EMA baseline.
                    if (set) { float v = (float)atof(vs.CString()); if (v > 1.0f) gradSpikeK_ = v; else { LogMessage("gradk must be > 1", bad); return; } }
                    LogMessage("gradk = " + String(gradSpikeK_, 2), ok);
                }
                else if (p == "gradema")
                {
                    // GPU gradMax EMA smoothing (0,1]: lower = slower baseline, resists tracking a slow ramp.
                    if (set) { float v = (float)atof(vs.CString()); if (v > 0.0f && v <= 1.0f) gradEmaAlpha_ = v; else { LogMessage("gradema must be in (0,1]", bad); return; } }
                    LogMessage("gradema = " + String(gradEmaAlpha_, 3), ok);
                }
                else if (p == "gradceil")
                {
                    // GPU gradMax absolute backstop: flag when gradMax > gradceil (0 = off). The slow-ramp
                    // catch the relative arm misses — set once the real gradMax scale is known, not blind.
                    if (set) { float v = (float)atof(vs.CString()); if (v >= 0.0f) gradCeil_ = v; else { LogMessage("gradceil must be >= 0 (0 = off)", bad); return; } }
                    LogMessage("gradceil = " + (gradCeil_ > 0.0f ? String(gradCeil_, 4) : String("off")), ok);
                }
                else if (p == "ratchet")
                {
                    // Kick-until-first-rollback. Same knob as the /ratchet alias — settable here too so every
                    // knob /tune SHOWS, /tune can SET ("one consistent interface for every training knob").
                    if (set) { if (vs == "on" || vs == "off") ratchet_ = (vs == "on"); else { LogMessage("ratchet must be on|off", bad); return; } }
                    LogMessage(String("ratchet = ") + (ratchet_ ? "on" : "off"), ok);
                }
                else if (p == "autokick")
                {
                    // One-shot auto-kick on plateau. Same knob as the /autokick alias.
                    if (set) { if (vs == "on" || vs == "off") autoKick_ = (vs == "on"); else { LogMessage("autokick must be on|off", bad); return; } }
                    LogMessage(String("autokick = ") + (autoKick_ ? "armed" : "off"), ok);
                }
                else
                    LogMessage("Unknown tune param '" + p + "' (lr | batch | staleness | sigma | rate | gradk | gradema | gradceil | ratchet | autokick)", bad);
            }
        }
        else if (input == "/sigma" || input.StartsWith("/sigma "))
        {
            // Thin alias for `/tune sigma`: `/sigma` shows the kick perturbation
            // magnitude, `/sigma 0.03` sets it. Same knob as /tune, /kick, /ratchet.
            if (!trainer_)
            {
                LogMessage("No trainer.", Color(0.8f, 0.6f, 0.3f));
                return;
            }
            const Color ok(0.6f, 0.8f, 1.0f), bad(0.8f, 0.6f, 0.3f);
            const String vs = (input.Length() > 6 ? input.Substring(6).Trimmed() : String::EMPTY);
            if (!vs.Empty())
            {
                float s = (float)atof(vs.CString());
                if (s > 0.0f) ratchetSigma_ = s;
                else { LogMessage("sigma must be > 0", bad); return; }
            }
            LogMessage("sigma = " + String(ratchetSigma_, 4), ok);
        }
        else if (input == "/restorebest")
        {
            // Manually return the live model to the best-ever (elite) weights.
            if (!trainer_ || !trainer_->HasElite())
            {
                LogMessage("No elite captured yet — nothing to restore.", Color(0.8f, 0.6f, 0.3f));
                return;
            }
            trainer_->RestoreBest();
            LogMessage("Restored best-ever weights (floor " +
                String(trainer_->GetBestLoss(), 3) + ").", Color(0.6f, 0.8f, 1.0f));
        }
        else if (input == "/fed" || input.StartsWith("/fed "))
        {
            // Federation P3 control (single-expert spike). Additive; off until an expert is pinned.
            //   /fed                       — status
            //   /fed pin <id> <path>       — pin one expert cart at startup (vocab-identity checked vs core)
            //   /fed force on|off          — debug: always-fire the trigger (validate wiring end-to-end)
            //   /fed threshold <v>         — trigger fires when tanh(logit[channel]) > v
            //   /fed channel <i>           — output-neuron index used as the trigger channel
            //   /fed clear                 — unpin (Unload the expert; dispatch goes back to no-op)
            if (!dispatch_ || !cartRegistry_)
            {
                LogMessage("Federation not initialised (no brain?).", Color(0.8f, 0.6f, 0.3f));
                return;
            }
            Vector<String> a = input.Split(' ');
            const String sub = a.Size() > 1 ? a[1] : String::EMPTY;

            if (sub.Empty() || sub == "status")
            {
                // Summarise the routing table (channel -> expert id).
                String routeStr;
                const Vector<YukiRoute>& routes = dispatch_->Routes();
                for (unsigned i = 0; i < routes.Size(); ++i)
                    routeStr += (i ? ", " : "") + String(routes[i].channel_) + "->" + routes[i].expertId_;
                LogMessage(String("Federation: resident=") + String(dispatch_->PinnedCount()) + "/" +
                    String(dispatch_->ResidentCap()) + " [" + String::Joined(dispatch_->PinnedIds(), ",") + "]" +
                    ", manifest=" + String(cartRegistry_->ManifestCount()) +
                    ", routes=[" + routeStr + "]" +
                    ", depthCap=" + String(dispatch_->DepthCap()) +
                    ", callBudget=" + String(dispatch_->CallBudget()) +
                    ", fires=" + String(dispatch_->FireCount()),
                    Color(0.6f, 0.8f, 1.0f));
            }
            else if (sub == "pin")
            {
                if (a.Size() < 4)
                {
                    LogMessage("Usage: /fed pin <id> <path>", Color(0.8f, 0.6f, 0.3f));
                    return;
                }
                // Pin off any live pass (registry LIVE-PASS contract): drain a live GPU pass first.
                if (gpuPumpActive_) StopGpuPump();
                if (dispatch_->PinExpert(a[2], a[3], model_))
                    LogMessage("Pinned expert '" + a[2] + "' (auto-routed on default channel if free). " +
                        "Add more routes with /fed route <ch> <id>.", Color(0.6f, 1.0f, 0.6f));
                else
                    LogMessage("Pin failed for '" + a[2] + "' — see log (load error or vocab mismatch).",
                        Color::RED);
            }
            else if (sub == "route" && a.Size() > 3)
            {
                // Map an output-neuron trigger channel to a pinned expert (an edge in the call tree).
                if (dispatch_->SetRoute(ToU32(a[2]), a[3]))
                    LogMessage("Route set: channel " + a[2] + " -> expert '" + a[3] + "'.",
                        Color(0.6f, 0.8f, 1.0f));
                else
                    LogMessage("Usage: /fed route <channel> <expertId>", Color(0.8f, 0.6f, 0.3f));
            }
            else if (sub == "unroute" && a.Size() > 2)
            {
                dispatch_->ClearRoute(ToU32(a[2]));
                LogMessage("Route cleared on channel " + a[2] + ".", Color(0.6f, 0.8f, 1.0f));
            }
            else if (sub == "register" && a.Size() > 3)
            {
                // Catalog an expert's existence (id -> .cart path) WITHOUT loading it. Producer API: P5
                // records each subcart it trains; P6 resolves ids to paths for load-on-demand.
                if (cartRegistry_->Register(a[2], a[3]))
                    LogMessage("Cataloged expert '" + a[2] + "' -> " + a[3] + " (known, not loaded).",
                        Color(0.6f, 0.8f, 1.0f));
                else
                    LogMessage("Usage: /fed register <id> <path>", Color(0.8f, 0.6f, 0.3f));
            }
            else if (sub == "manifest")
            {
                // /fed manifest list|save <path>|load <path> — the id->path catalog of which experts exist.
                const String op = a.Size() > 2 ? a[2] : String::EMPTY;
                if (op == "list")
                {
                    Vector<String> ids = cartRegistry_->GetManifestIds();
                    LogMessage("Manifest: " + String(ids.Size()) + " known expert(s):", Color(0.6f, 0.8f, 1.0f));
                    for (const String& id : ids)
                        LogMessage("  " + id + " -> " + cartRegistry_->GetManifestPath(id));
                }
                else if (op == "save" && a.Size() > 3)
                {
                    if (cartRegistry_->SaveManifest(a[3]))
                        LogMessage("Manifest saved to " + a[3] + ".", Color(0.6f, 1.0f, 0.6f));
                    else
                        LogMessage("Manifest save failed — see log.", Color::RED);
                }
                else if (op == "load" && a.Size() > 3)
                {
                    if (gpuPumpActive_) StopGpuPump();   // may load carts -> off any live pass (LIVE-PASS contract)
                    unsigned n = cartRegistry_->LoadManifest(a[3]);
                    LogMessage("Manifest loaded " + String(n) + " expert(s) from " + a[3] +
                        " (catalog now " + String(cartRegistry_->ManifestCount()) + ").", Color(0.6f, 0.8f, 1.0f));
                }
                else
                    LogMessage("Usage: /fed manifest list | save <path> | load <path>", Color(0.8f, 0.6f, 0.3f));
            }
            else if (sub == "depth" && a.Size() > 2)
            {
                dispatch_->SetDepthCap((int)ToI32(a[2]));
                LogMessage("Call-tree depth cap = " + String(dispatch_->DepthCap()) +
                    " (core->expert->... ; guards keep the tree finite).", Color(0.6f, 0.8f, 1.0f));
            }
            else if (sub == "budget" && a.Size() > 2)
            {
                dispatch_->SetCallBudget((int)ToI32(a[2]));
                LogMessage("Per-token call budget = " + String(dispatch_->CallBudget()) +
                    " (max expert invocations across the whole tree per token).", Color(0.6f, 0.8f, 1.0f));
            }
            else if (sub == "cap" && a.Size() > 2)
            {
                // P6: resident-expert memory cap. Beyond it, the LRU non-pinned expert is evicted on load.
                dispatch_->SetResidentCap(ToU32(a[2]));
                LogMessage("Resident expert cap = " + String(dispatch_->ResidentCap()) +
                    " (0 = unlimited; load-on-demand evicts LRU beyond this).", Color(0.6f, 0.8f, 1.0f));
            }
            else if (sub == "autoroute")
            {
                // P6 scale-out: wire every cataloged expert (manifest) to a sequential trigger channel, so a
                // whole roster is "strung together" in one command. Experts load on demand when their route fires.
                const unsigned startCh = a.Size() > 2 ? ToU32(a[2]) : 0u;
                Vector<String> ids = cartRegistry_->GetManifestIds();
                unsigned n = 0;
                for (unsigned i = 0; i < ids.Size(); ++i)
                    if (dispatch_->SetRoute(startCh + i, ids[i])) ++n;
                LogMessage("Auto-routed " + String(n) + " cataloged expert(s) from channel " + String(startCh) +
                    " (load-on-demand; cap " + String(dispatch_->ResidentCap()) + ").", Color(0.6f, 1.0f, 0.6f));
            }
            else if (sub == "force")
            {
                const bool on = (a.Size() > 2 && (a[2] == "on" || a[2] == "1" || a[2] == "true"));
                dispatch_->SetForceMode(on);
                LogMessage(String("Federation force-fire ") + (on ? "ON" : "OFF") +
                    (on && !dispatch_->HasExpert() ? " (but no expert pinned — still a no-op)" : ""),
                    Color(0.6f, 0.8f, 1.0f));
            }
            else if (sub == "threshold" && a.Size() > 2)
            {
                dispatch_->SetThreshold(ToFloat(a[2]));
                LogMessage("Federation trigger threshold = " + a[2], Color(0.6f, 0.8f, 1.0f));
            }
            else if (sub == "channel" && a.Size() > 2)
            {
                dispatch_->SetTriggerChannel(ToU32(a[2]));
                LogMessage("Default trigger channel = " + a[2] + " (used by bare /fed pin auto-route).",
                    Color(0.6f, 0.8f, 1.0f));
            }
            else if (sub == "clear")
            {
                // Drop all pinned experts + routes (fresh orchestrator) and evict them from the registry.
                Vector<String> ids = dispatch_->PinnedIds();
                inference_->SetDispatch(nullptr);
                dispatch_ = new YukiDispatch(context_, cartRegistry_);
                dispatch_->SetCore(model_);   // P6: interface reference for load-on-demand
                inference_->SetDispatch(dispatch_);
                for (const String& id : ids)
                    cartRegistry_->Unload(id);
                LogMessage("Federation cleared — back to no-op.", Color(0.6f, 0.8f, 1.0f));
            }
            else
                LogMessage("Unknown /fed subcommand. Try: status | pin | route | unroute | register | "
                    "manifest | autoroute | cap | depth | budget | force | threshold | channel | clear",
                    Color(0.8f, 0.6f, 0.3f));
        }
        else if (input.StartsWith("/expertharvest"))
        {
            // P5 (§8): offline-harvest the training dataset for an expert from the corpus, tokenised through
            // the CORE's exact vocab (interface-lock). Writes bin/Data/GameDB/experts/<id>.dataset.
            Vector<String> a = input.Split(' ');
            if (a.Size() < 2)
            {
                LogMessage("Usage: /expertharvest <id> [maxSamples]", Color(0.8f, 0.6f, 0.3f));
                return;
            }
            if (!hasBrain_ || !model_)
            {
                LogMessage("No brain — can't harvest (need the core's vocab).", Color(0.8f, 0.6f, 0.3f));
                return;
            }
            const String id = a[1];
            const unsigned maxSamples = a.Size() > 2 ? ToU32(a[2]) : 500u;   // conservative default (scoring is O(samples))

            // Gather corpus text from memories (same source InitBrain builds the vocab from).
            String text;
            if (memoryDb_)
            {
                DbResult r = memoryDb_->Execute("SELECT text FROM memories ORDER BY id");
                const Vector<VariantVector>& rows = r.GetRows();
                for (unsigned i = 0; i < rows.Size(); ++i)
                    text += rows[i][0].GetString() + " ";
            }
            if (text.Empty())
            {
                LogMessage("No corpus text in memories to harvest.", Color(0.8f, 0.6f, 0.3f));
                return;
            }

            auto* fs = GetSubsystem<FileSystem>();
            const String dir = fs->GetProgramDir() + "Data/GameDB/experts/";
            fs->CreateDir(dir);
            SharedPtr<YukiExpertDataset> ds(new YukiExpertDataset(context_));
            const unsigned n = ds->BuildFromText(model_, text, model_->GetTopology().maxSeqLen, maxSamples);
            if (n == 0)
            {
                LogMessage("Harvest produced 0 samples (no known tokens?).", Color::RED);
                return;
            }
            const String dsPath = dir + id + ".dataset";
            if (ds->Save(dsPath))
                LogMessage("Harvested " + String(n) + " samples for '" + id + "' -> " + dsPath + ".",
                    Color(0.6f, 1.0f, 0.6f));
            else
                LogMessage("Harvest save failed — see log.", Color::RED);
        }
        else if (input.StartsWith("/experttrain"))
        {
            // P5: train an expert subcart by the two-tank GA (YukiExpertTrainer) against its harvested
            // dataset (task-success fitness). The GA is STEPPED off the main tick (StepExpertTraining, called
            // from HandleUpdate) so the UI stays responsive and shows progress — NOT a synchronous Run().
            Vector<String> a = input.Split(' ');
            if (a.Size() >= 2 && a[1] == "cancel")
            {
                if (expertTraining_)
                {
                    expertWorker_.SignalStop();   // stops after the current generation; teardown in StepExpertTraining
                    LogMessage("Cancelling expert training after the current generation…", Color(0.8f, 0.6f, 0.3f));
                }
                else
                    LogMessage("No expert training in progress.", Color(0.8f, 0.6f, 0.3f));
                return;
            }
            if (a.Size() < 2)
            {
                LogMessage("Usage: /experttrain <id> [nLayers=2] [gens=20] [batch=32] | /experttrain cancel  "
                    "(run /expertharvest <id> first)", Color(0.8f, 0.6f, 0.3f));
                return;
            }
            if (expertTraining_)
            {
                LogMessage("Already training '" + expertTrainId_ + "' (gen " + String(expertWorker_.GetGen()) +
                    "). /experttrain cancel to stop.", Color(0.8f, 0.6f, 0.3f));
                return;
            }
            if (!hasBrain_ || !model_)
            {
                LogMessage("No brain — can't train (need the core as interface reference).", Color(0.8f, 0.6f, 0.3f));
                return;
            }
            // NOTE: intentionally does NOT stop the GPU pump. This trains a SEPARATE subcart on CPU and only
            // READS the core's vocab/topology (which don't change during training); it never touches the
            // core's GPU/weight memory, so there is no live-pass contention to guard against.

            const String id = a[1];
            const unsigned nLayers = a.Size() > 2 ? Max(1u, ToU32(a[2])) : 2u;
            const unsigned gens = a.Size() > 3 ? Max(1u, ToU32(a[3])) : 20u;
            const unsigned batch = a.Size() > 4 ? Max(1u, ToU32(a[4])) : 128u;  // TRAIN fitness batch (fixed across gens)
            const float sigma = a.Size() > 5 ? ToFloat(a[5]) : 0.25f;           // bare-perturb scale (RMS-relative)

            auto* fs = GetSubsystem<FileSystem>();
            const String dir = fs->GetProgramDir() + "Data/GameDB/experts/";
            fs->CreateDir(dir);

            expertDataset_ = new YukiExpertDataset(context_);
            if (!expertDataset_->Load(dir + id + ".dataset") || expertDataset_->Size() == 0)
            {
                LogMessage("No dataset for '" + id + "' — run /expertharvest " + id + " first.", Color(0.8f, 0.6f, 0.3f));
                expertDataset_.Reset();
                return;
            }
            // Hold out the last 15% as a VALIDATION split so we emit the champion that GENERALISES, not the
            // one that memorised the training batch. Needs enough samples for a meaningful split.
            if (expertDataset_->Size() < 20)
            {
                LogMessage("Dataset too small (" + String(expertDataset_->Size()) +
                    ") for a train/val split — harvest more samples first.", Color(0.8f, 0.6f, 0.3f));
                expertDataset_.Reset();
                return;
            }
            expertDataset_->SetValidationFraction(0.15f);

            // Interface-locked seed subcart (§6): core's embedDim + EXACT vocab; "small" only via fewer
            // nLayers / smaller ffDim. nHeads kept = core so headDim divides identically.
            const YukiTopology& ct = model_->GetTopology();
            Vector<String> vocab;
            vocab.Reserve(ct.vocabSize);
            for (unsigned i = 0; i < ct.vocabSize; ++i)
                vocab.Push(model_->GetToken(i));
            YukiTopology st;
            st.embedDim = ct.embedDim;                    // FIXED to core
            st.nLayers = nLayers;                         // shrink depth
            st.nHeads = ct.nHeads;                        // keep (embedDim/nHeads unchanged)
            st.ffDim = Max(ct.embedDim, ct.ffDim / 2u);   // shrink width (>= embedDim)
            st.vocabSize = ct.vocabSize;                  // FIXED (identity via the same vocab list)
            st.maxSeqLen = ct.maxSeqLen;

            const String seedPath = dir + id + ".seed.cart";
            if (!CreateEmptyCartridge(context_, seedPath, st, vocab))
            {
                LogMessage("Seed cartridge creation failed — see log.", Color::RED);
                expertDataset_.Reset();
                return;
            }
            expertSeed_ = new YukiModel(context_);
            if (!expertSeed_->Load(seedPath))
            {
                LogMessage("Seed cartridge load failed — see log.", Color::RED);
                expertDataset_.Reset(); expertSeed_.Reset();
                return;
            }

            expertTrainer_ = new YukiExpertTrainer(context_);
            expertTrainer_->SetMaxGenerations(gens);
            expertTrainer_->SetPerturbSigma(sigma);
            // FIXED minibatch fitness: every candidate in EVERY generation is scored on the SAME `batch`
            // samples (constant seed). Comparability is what lets the GA climb — a per-generation rotating
            // batch made an immortal's stored score and a fresh challenger's score non-comparable, so the
            // monotonic best latched onto batch noise instead of real improvement. Fixed batch = a clean
            // "does the optimizer move off the uniform baseline" signal (overfits the batch, but that is the
            // point of the first-cut proof; raise `batch` toward the full dataset for generalisation later).
            YukiExpertDataset* dsp = expertDataset_.Get();      // members outlive the run (reset on completion)
            expertTrainer_->SetFitnessEvaluator([dsp, batch](YukiModel* sc)
                { return dsp->Score(sc, batch, 0u); });
            // HELD-OUT validator: judges the champion on the untouched val split each generation. The trainer
            // keeps the best-VALIDATING weights and emits THOSE — so the emitted expert generalises rather
            // than memorises the training batch. The reported fitness becomes this held-out number.
            expertTrainer_->SetValidator([dsp](YukiModel* sc) { return dsp->ScoreValidation(sc); });
            // NOTE: Initialize() (heavy — seeds + scores the first tanks) runs on the WORKER thread below, not
            // here, so the command returns immediately and the UI never blocks.

            expertTrainId_ = id;
            expertTrainCartPath_ = dir + id + ".cart";
            expertTrainDir_ = dir;
            expertTrainLastLoggedGen_ = -1;
            expertTraining_ = true;
            expertWorker_.Configure(expertTrainer_, model_, expertSeed_, expertTrainCartPath_);
            expertWorker_.Run();   // background: Initialize + Steps + Emit; main thread polls in StepExpertTraining
            LogMessage("Training '" + id + "' (background): train " + String(expertDataset_->TrainSize()) +
                " / val " + String(expertDataset_->ValSize()) + ", batch " + String(batch) + ", sigma " +
                String(sigma, 3) + ", " + String(nLayers) + " layers, up to " + String(gens) +
                " gens — reported fitness is HELD-OUT (val); baseline ~1/vocab; /experttrain cancel to stop.",
                Color(0.6f, 0.8f, 1.0f));
        }
        else if (input == "/sources")
        {
            if (corpusDb_)
            {
                DbResult result = corpusDb_->Execute("SELECT name, bulk_method, notes FROM sources ORDER BY id");
                const Vector<VariantVector>& rows = result.GetRows();
                LogMessage(rows.Empty() ? "No trusted sources." : "Trusted sources:", Color(0.6f, 0.8f, 1.0f));
                for (unsigned i = 0; i < rows.Size(); ++i)
                    LogMessage("  " + rows[i][0].GetString() + " [" + rows[i][1].GetString() + "]");
            }
        }
        else if (input.StartsWith("/trust "))
        {
            // Add a trusted source. Typing this at Yuki's console IS the trust decision —
            // only the operator can do it. This only opens the gate; it ingests nothing.
            if (!corpusDb_)
            {
                LogMessage("Corpus not connected.", Color::RED);
                return;
            }
            Vector<String> parts = input.Substring(7).Trimmed().Split('|');
            String name  = parts.Size() > 0 ? parts[0].Trimmed() : String::EMPTY;
            String url   = parts.Size() > 1 ? parts[1].Trimmed() : String::EMPTY;
            String notes = parts.Size() > 2 ? parts[2].Trimmed() : String::EMPTY;
            if (name.Empty())
                LogMessage("Usage: /trust <name> | <url> | <notes>", Color(0.8f, 0.6f, 0.3f));
            else
            {
                DbResult ex = corpusDb_->Execute("SELECT id FROM sources WHERE name = '" + name.Replaced("'", "''") + "'");
                if (!ex.GetRows().Empty())
                    LogMessage("'" + name + "' is already trusted.", Color(0.8f, 0.6f, 0.3f));
                else
                {
                    corpusDb_->Execute("INSERT INTO sources (name, url, bulk_method, notes) VALUES ('" +
                        name.Replaced("'", "''") + "', '" +
                        url.Replaced("'", "''") + "', 'manual', '" +
                        notes.Replaced("'", "''") + "')");
                    LogMessage("Trusted source added: " + name, Color(0.6f, 0.8f, 1.0f));
                }
            }
        }
        else if (input.StartsWith("/untrust "))
        {
            // Remove a source from the trust list (deletes the menu entry only).
            // Any documents already ingested from it are reported but left in place.
            if (!corpusDb_)
            {
                LogMessage("Corpus not connected.", Color::RED);
                return;
            }
            String name = input.Substring(9).Trimmed();
            if (name.Empty())
                LogMessage("Usage: /untrust <name> | /untrust *  (clear all)", Color(0.8f, 0.6f, 0.3f));
            else if (name == "*")
            {
                // Wildcard: clear the ENTIRE trust list in one shot. Same policy as a single removal — any
                // already-ingested documents are reported but left in place (purge separately).
                DbResult cnt = corpusDb_->Execute("SELECT COUNT(*) FROM sources");
                int srcCount = cnt.GetRows().Empty() ? 0 : cnt.GetRows()[0][0].GetI32();
                if (srcCount == 0)
                    LogMessage("No trusted sources to remove.", Color(0.6f, 0.8f, 1.0f));
                else
                {
                    DbResult dc = corpusDb_->Execute("SELECT COUNT(*) FROM documents WHERE source_id IS NOT NULL");
                    int docCount = dc.GetRows().Empty() ? 0 : dc.GetRows()[0][0].GetI32();
                    corpusDb_->Execute("DELETE FROM sources");
                    if (docCount > 0)
                        LogMessage("Cleared ALL " + String(srcCount) + " trusted source(s). Note: " + String(docCount) +
                            " already-ingested document(s) remain — purge separately.", Color(0.8f, 0.6f, 0.3f));
                    else
                        LogMessage("Cleared ALL " + String(srcCount) + " trusted source(s).", Color(0.6f, 0.8f, 1.0f));
                }
            }
            else
            {
                String esc = name.Replaced("'", "''");
                DbResult ex = corpusDb_->Execute("SELECT id FROM sources WHERE name = '" + esc + "'");
                const Vector<VariantVector>& exRows = ex.GetRows();
                if (exRows.Empty())
                    LogMessage("No trusted source named '" + name + "'.", Color(0.8f, 0.6f, 0.3f));
                else
                {
                    int sourceId = exRows[0][0].GetI32();
                    DbResult dc = corpusDb_->Execute("SELECT COUNT(*) FROM documents WHERE source_id = " + String(sourceId));
                    int docCount = dc.GetRows().Empty() ? 0 : dc.GetRows()[0][0].GetI32();
                    corpusDb_->Execute("DELETE FROM sources WHERE id = " + String(sourceId));
                    if (docCount > 0)
                        LogMessage("Removed source '" + name + "'. Note: " + String(docCount) +
                            " already-ingested document(s) remain — purge separately.", Color(0.8f, 0.6f, 0.3f));
                    else
                        LogMessage("Removed untrusted source: " + name, Color(0.6f, 0.8f, 1.0f));
                }
            }
        }
        else if (input == "/help")
        {
            LogMessage("Talk to me — I answer from what I know. Only /remember teaches me.", Color(0.6f, 0.8f, 1.0f));
            LogMessage("Commands:", Color(0.6f, 0.8f, 1.0f));
            LogMessage("  /remember <fact>  — store a fact (the ONLY thing that trains me)");
            LogMessage("  /recall <topic>   — search my memory");
            LogMessage("  /memories         — how much I know (shows ids)");
            LogMessage("  /forget <id>      — delete one memory (only delete path)");
            LogMessage("  /coverage         — unknown-token ratio (vocab floor)");
            LogMessage("  /kick [sigma]     — perturb weights to escape a plateau");
            LogMessage("  /autokick on|off [sigma]  — auto-kick once when plateaued");
            LogMessage("  /ratchet on|off [sigma]   — kick every plateau until the true floor");
            LogMessage("  /ceil [frac]              — pawl ceiling: restore .best past +frac over floor (0=off)");
            LogMessage("  /gclip [val]              — GPU grad value-clip ±val before Adam (0=off; tames spikes)");
            LogMessage("  /restorebest      — revert to best-ever weights");
            LogMessage("  /tune [param [val]]  — view/set lr|batch|staleness|sigma|rate|gradk|gradema|gradceil|ratchet|autokick");
            LogMessage("       gradk/gradema/gradceil — GPU gradMax divergence detector (spike mult / EMA / abs ceiling)");
            LogMessage("  /sigma [val]      — show/set kick sigma (alias for /tune sigma)");
            LogMessage("  /stats            — corpus statistics");
            LogMessage("  /sources          — trusted data sources");
            LogMessage("  /trust <name> | <url> | <notes>  — add a trusted source");
            LogMessage("  /untrust <name> | /untrust *     — remove a source (or * = clear all)");
        }
        else
        {
            LogMessage("I don't know that command. Try /help", Color(0.8f, 0.6f, 0.3f));
        }
        return;
    }

    // Not a command — this is conversation: respond, AND remember the response. Self-generated
    // output is the FIRST training material — reasoned output feeding back into the corpus is
    // what self-improvement means here, not a byproduct of it. Tagged distinctly (YUKI_SELF_SOURCE)
    // so /newcart and any corpus-composition logic can identify and prioritise it, and so it never
    // gets confused with Leith-authored or ingested text in /sources or /coverage. Inference reads
    // weights under the SHARED lock (runs concurrently with a training pass — responsive UI); recall
    // and the self-Remember are DB ops serialised by dbMutex_ (weight->db acquisition order,
    // matching TrainOnce).
    std::shared_lock<std::shared_mutex> rlock(trainMutex_);

    // Search for related memories as context
    Vector<String> related;
    { std::unique_lock<std::mutex> dlock(dbMutex_); related = RecallMemories(input, 3); }

    if (hasBrain_ && inference_)
    {
        // Build context: related memories + input
        String context;
        for (unsigned i = 0; i < related.Size(); ++i)
        {
            if (related[i] != input)
                context += related[i] + " ";
        }
        context += input;

        String response = inference_->GenerateText(context, 64);
        if (!response.Trimmed().Empty())
        {
            { std::unique_lock<std::mutex> dlock(dbMutex_); Remember(response.Trimmed(), YUKI_SELF_SOURCE); }
            LogMessage(response.Trimmed(), Color(0.6f, 0.8f, 1.0f));
        }
        else
        {
            // Brain produced nothing useful — fall back to memory
            if (related.Empty())
                LogMessage("I heard you. I'm still learning.", Color(0.6f, 0.8f, 1.0f));
            else
            {
                LogMessage("That reminds me of:", Color(0.6f, 0.8f, 1.0f));
                for (unsigned i = 0; i < related.Size(); ++i)
                {
                    if (related[i] == input) continue;
                    String text = related[i];
                    if (text.Length() > 120)
                        text = text.Substring(0, 120) + "...";
                    LogMessage("  " + text, Color(0.7f, 0.7f, 0.7f));
                }
            }
        }
    }
    else
    {
        // No brain — memory recall only
        if (related.Empty())
        {
            LogMessage("I've stored that. I don't know enough yet to say anything useful about it.",
                Color(0.6f, 0.8f, 1.0f));
        }
        else
        {
            LogMessage("Noted. That reminds me of:", Color(0.6f, 0.8f, 1.0f));
            for (unsigned i = 0; i < related.Size(); ++i)
            {
                if (related[i] == input) continue;
                String text = related[i];
                if (text.Length() > 120)
                    text = text.Substring(0, 120) + "...";
                LogMessage("  " + text, Color(0.7f, 0.7f, 0.7f));
            }
        }
    }
}

// ============================================================================
// UI
// ============================================================================

void Yuki::CreateUI()
{
    auto* cache = GetSubsystem<ResourceCache>();
    auto* ui = GetSubsystem<UI>();
    auto* uiRoot = ui->GetRoot();

    auto* style = cache->GetResource<XMLFile>("UI/DefaultStyle.xml");
    uiRoot->SetDefaultStyle(style);

    auto* font = cache->GetResource<Font>("Fonts/Anonymous Pro.ttf");
    if (!font)
        font = cache->GetResource<Font>("Fonts/BlueHighway.ttf");

    // Dark background
    GetSubsystem<Renderer>()->GetDefaultZone()->SetFogColor(Color(0.06f, 0.06f, 0.08f));

    // Root layout — fills window, vertical, clean margins
    auto* root = uiRoot->CreateChild<UIElement>();
    root->SetLayout(LM_VERTICAL, 2);
    root->SetSize(uiRoot->GetSize());
    root->SetLayoutBorder(IntRect(8, 8, 8, 8));
    rootLayout_ = root;

    // Learner control — top, three-state (CPU learn / GPU learn / RUN frozen), colour-coded so the
    // active solver reads at a glance. Reuses the proven /gputrain switch; CPU is never discarded.
    solverButton_ = root->CreateChild<Button>();
    solverButton_->SetStyleAuto();
    solverButton_->SetMinHeight(22);
    solverButton_->SetMaxHeight(22);
    solverButton_->SetLayoutFlexScale(Vector2(1.0f, 0.0f));
    solverButtonText_ = solverButton_->CreateChild<Text>();
    solverButtonText_->SetFont(font, 11);
    solverButtonText_->SetColor(Color(0.95f, 0.95f, 0.95f));
    solverButtonText_->SetAlignment(HA_CENTER, VA_CENTER);
    SubscribeToEvent(solverButton_, E_RELEASED, URHO3D_HANDLER(Yuki, HandleSolverButton));

    // Status — one line, small, top
    statusText_ = root->CreateChild<Text>();
    statusText_->SetFont(font, 10);
    statusText_->SetColor(Color(0.4f, 0.5f, 0.4f));
    statusText_->SetText(String("Yuki v") + YUKI_VERSION);
    statusText_->SetMinHeight(14);
    statusText_->SetLayoutFlexScale(Vector2(1.0f, 0.0f));

    // Message log — fills the space, no borders
    messageLog_ = root->CreateChild<ListView>();
    messageLog_->SetStyleAuto();
    messageLog_->SetScrollBarsVisible(false, true);
    messageLog_->SetLayoutFlexScale(Vector2(1.0f, 1.0f));

    // Input field — bottom, full width, clean
    inputField_ = root->CreateChild<LineEdit>();
    inputField_->SetStyle("LineEdit");
    inputField_->SetMinHeight(26);
    inputField_->SetMaxHeight(26);
    inputField_->SetLayoutFlexScale(Vector2(1.0f, 0.0f));

    auto* inputText = inputField_->GetTextElement();
    if (inputText)
        inputText->SetFont(font, 12);

    // Placeholder color
    inputField_->SetText("");

    // Live training-progress graph — translucent overlay spanning the full window width so the
    // 256-sample window stretches across the whole width: each scroll step is then ~(width/256) px
    // and visibly slides, instead of the sub-pixel step a narrow element gave (looked frozen/compressed).
    // Width is re-set to the window width every frame in HandleUpdate so it tracks a resize.
    progressGraph_ = new ProgressGraph(context_);
    GetSubsystem<UI>()->GetRoot()->AddChild(progressGraph_);
    auto* graphics0 = GetSubsystem<Graphics>();
    progressGraph_->SetSize(graphics0 ? graphics0->GetWidth() : 800, 140);
    progressGraph_->SetAlignment(HA_LEFT, VA_TOP);
    progressGraph_->SetPosition(0, 12);
    progressGraph_->SetMode(ProgressGraph::CATMULL_ROM);
    progressGraph_->SetLineColor(Color(0.4f, 0.9f, 0.5f, 1.0f));
    progressGraph_->SetBackgroundColor(Color(0.05f, 0.05f, 0.08f, 0.4f));
    progressGraph_->SetThickness(1.5f);

    // Second, overlaid graph: process RSS (leakage watch), drawn RED in the SAME bounds as the loss curve.
    // Added AFTER progressGraph_ so it renders on top. Transparent background (alpha 0) so it doesn't repaint
    // over the loss graph's translucent backing. Independently autoscaled, so its shape shows memory creep
    // regardless of the loss curve's scale — a climbing red line means we're still leaking.
    leakGraph_ = new ProgressGraph(context_);
    GetSubsystem<UI>()->GetRoot()->AddChild(leakGraph_);
    leakGraph_->SetSize(graphics0 ? graphics0->GetWidth() : 800, 140);
    leakGraph_->SetAlignment(HA_LEFT, VA_TOP);
    leakGraph_->SetPosition(0, 12);
    leakGraph_->SetMode(ProgressGraph::CATMULL_ROM);
    leakGraph_->SetLineColor(Color(0.95f, 0.25f, 0.25f, 1.0f));   // red = leakage series
    leakGraph_->SetBackgroundColor(Color(0.0f, 0.0f, 0.0f, 0.0f)); // no fill — overlay only
    leakGraph_->SetThickness(1.5f);
    leakGraph_->SetMinSpan(128.0f);   // anchor: <128 MB of wobble reads flat; a real leak of that order stands out

    SubscribeToEvent(inputField_, E_TEXTFINISHED, URHO3D_HANDLER(Yuki, HandleInput));

    ui->SetFocusElement(inputField_);
}

void Yuki::LogMessage(const String& msg)
{
    LogMessage(msg, Color(0.9f, 0.9f, 0.9f));
}

void Yuki::LogMessage(const String& msg, const Color& color)
{
    if (!messageLog_)
        return;

    auto* cache = GetSubsystem<ResourceCache>();
    auto* font = cache->GetResource<Font>("Fonts/Anonymous Pro.ttf");
    if (!font)
        font = cache->GetResource<Font>("Fonts/BlueHighway.ttf");

    auto* graphics = GetSubsystem<Graphics>();
    int maxW = graphics ? graphics->GetWidth() - 24 : 760;

    auto* text = new Text(context_);
    text->SetFont(font, 12);
    text->SetColor(color);
    text->SetText(msg);
    text->SetWordwrap(true);
    text->SetMaxWidth(maxW);
    messageLog_->AddItem(text);

    // Keep log manageable — trim old messages
    while (messageLog_->GetNumItems() > 200)
        messageLog_->RemoveItem(messageLog_->GetItem(0));

    messageLog_->EnsureItemVisibility(messageLog_->GetNumItems() - 1);

    URHO3D_LOGINFO("[Yuki] " + msg);
}

void Yuki::HandleInput(StringHash /*eventType*/, VariantMap& eventData)
{
    using namespace TextFinished;
    String input = eventData[P_TEXT].GetString().Trimmed();

    if (input.Empty())
        return;

    // Don't clear — start eroding. Yuki consumes visibly.
    pendingInput_ = input;
    erodingText_ = input;
    eroding_ = true;
    erodeTimer_ = 0.0f;
    inputField_->SetText(erodingText_);
}

bool Yuki::IsSupportedTextExt(const String& path) const
{
    String ext = GetExtension(path, true);   // lowercased, includes the leading dot
    if (ext.StartsWith("."))
        ext = ext.Substring(1);
    // The usual suspects — plain text, docs, config, and source. HTML is read raw for now (filtering later).
    static const char* ok[] = {
        "txt","text","md","markdown","rst","log","csv","tsv","json","xml","yaml","yml","ini","cfg","conf",
        "h","hpp","hh","hxx","inl","c","cc","cpp","cxx","py","js","ts","jsx","tsx","java","kt","rb","go","rs",
        "sh","bash","zsh","lua","sql","glsl","vert","frag","comp","cs","php","pl","r","m","mm","html","htm","css",
        nullptr };
    for (unsigned i = 0; ok[i]; ++i)
        if (ext == ok[i])
            return true;
    return false;
}

// Kick off an incremental chunk+insert. `firstText` is the first blob (or empty for a directory, whose
// files come from `fileQueue`). The heavy work (chunking + INSERTs) is then spread across frames by
// StepIngestProcess so a big source never freezes the render thread.
void Yuki::BeginIngest(const String& firstText, const Vector<String>& fileQueue, const String& label)
{
    ingestText_ = firstText;
    ingestPos_ = 0;
    ingestFileQueue_ = fileQueue;
    ingestLabel_ = label;
    ingestChunkTotal_ = 0;
    ingestProcessing_ = true;
    LogMessage("Ingesting into memories (streaming, UI stays live)…", Color(0.6f, 0.8f, 1.0f));
}

static inline bool YukiIsWs(char c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t'; }

void Yuki::StepIngestProcess()
{
    if (!ingestProcessing_ || !memoryDb_)
        return;

    // If the current blob is exhausted, pull the next queued file (directory ingest); else finish.
    while (ingestPos_ >= ingestText_.Length())
    {
        if (ingestFileQueue_.Empty())
        {
            LogMessage("Ingest complete: " + String(ingestChunkTotal_) + " chunk(s) into memories.",
                Color(0.6f, 1.0f, 0.6f));
            ingestProcessing_ = false; ingestText_.Clear(); ingestFileQueue_.Clear();
            return;
        }
        const String next = ingestFileQueue_.Back(); ingestFileQueue_.Pop();
        File f(context_, next, FILE_READ);
        ingestText_.Clear(); ingestPos_ = 0;
        if (f.IsOpen() && f.GetSize() > 0)
        {
            ingestText_.Resize(f.GetSize());
            f.Read(&ingestText_[0], f.GetSize());
            f.Close();
        }
        ingestLabel_ = "ingest:" + next;
    }

    // Chunk a BOUNDED number of pieces this frame, each ≤ ~context-window tokens (0.7 words/token headroom).
    const unsigned maxTok = (model_ && model_->IsLoaded()) ? model_->GetTopology().maxSeqLen : 128u;
    const unsigned chunkWords = Max(16u, maxTok * 7u / 10u);
    const unsigned BUDGET = 300;   // chunks per frame — bounded main-thread DB work
    const char* s = ingestText_.CString();
    const unsigned len = ingestText_.Length();
    const String esclabel = ingestLabel_.Replaced("'", "''");

    memoryDb_->Execute("BEGIN");
    unsigned done = 0;
    while (done < BUDGET && ingestPos_ < len)
    {
        while (ingestPos_ < len && YukiIsWs(s[ingestPos_])) ++ingestPos_;   // skip leading whitespace
        if (ingestPos_ >= len) break;
        const unsigned start = ingestPos_;
        unsigned words = 0;
        while (ingestPos_ < len && words < chunkWords)
        {
            while (ingestPos_ < len && !YukiIsWs(s[ingestPos_])) ++ingestPos_;   // consume a word
            ++words;
            while (ingestPos_ < len && YukiIsWs(s[ingestPos_])) ++ingestPos_;     // skip inter-word whitespace
        }
        String chunk(s + start, (i32)(ingestPos_ - start));
        chunk = chunk.Trimmed();
        if (!chunk.Empty())
        {
            memoryDb_->Execute("INSERT INTO memories (text, source) VALUES ('" + chunk.Replaced("'", "''") +
                "', '" + esclabel + "')");
            ++ingestChunkTotal_; ++done;
        }
    }
    memoryDb_->Execute("COMMIT");
}

void Yuki::HandleAsyncExecFinished(StringHash /*eventType*/, VariantMap& eventData)
{
    using namespace AsyncExecFinished;
    const unsigned id = eventData[P_REQUESTID].GetU32();
    if (!ingesting_ || id != ingestCurlReqId_)
        return;   // not our curl

    const int code = eventData[P_EXITCODE].GetI32();
    ingesting_ = false;
    ingestCurlReqId_ = 0;
    auto* fs = GetSubsystem<FileSystem>();

    if (code != 0)
    {
        LogMessage("curl fetch failed (exit " + String(code) + ") — bad URL / network / TLS?", Color::RED);
        if (fs) fs->Delete(ingestTmpPath_);
        return;
    }
    File f(context_, ingestTmpPath_, FILE_READ);
    if (!f.IsOpen() || f.GetSize() == 0)
    {
        LogMessage("Fetch produced no data (empty response?).", Color(0.8f, 0.6f, 0.3f));
        if (fs) fs->Delete(ingestTmpPath_);
        return;
    }
    const unsigned kb = f.GetSize() / 1024;
    String text; text.Resize(f.GetSize());
    f.Read(&text[0], f.GetSize());
    f.Close();
    if (fs) fs->Delete(ingestTmpPath_);
    LogMessage("Fetched " + String(kb) + " KB via curl.", Color(0.6f, 0.8f, 1.0f));
    BeginIngest(text, Vector<String>(), ingestLabel_);   // stream the chunk+insert across frames
}

void Yuki::StepExpertTraining()
{
    if (!expertTraining_)
        return;

    // Poll the background worker — the GA compute runs on expertWorker_, never here. Read its progress
    // scalars lock-free (benign display race) and log throttled.
    const int gen = expertWorker_.GetGen();
    if (gen - expertTrainLastLoggedGen_ >= 1)   // every generation while we watch it learn (6 s.f. shows small moves)
    {
        expertTrainLastLoggedGen_ = gen;
        LogMessage("  [experttrain '" + expertTrainId_ + "'] gen " + String(gen) + " train=" +
            String(expertWorker_.GetBestTrain(), 6) + " val=" + String(expertWorker_.GetBest(), 6),
            Color(0.55f, 0.7f, 0.9f));
    }

    if (!expertWorker_.IsFinished())
        return;

    // Worker done — join it (instant; the thread has already exited), then report + register on THIS thread
    // (registry mutation + UI are kept main-thread). Emit already happened on the worker.
    expertWorker_.Stop();
    if (expertWorker_.InitFailed())
        LogMessage("Expert training failed to initialise (interface mismatch?) — see log.", Color::RED);
    else if (expertWorker_.EmitOk())
    {
        cartRegistry_->Register(expertTrainId_, expertTrainCartPath_);
        cartRegistry_->SaveManifest(expertTrainDir_ + "manifest.txt");
        LogMessage("Trained '" + expertTrainId_ + "': held-out val=" + String(expertWorker_.GetBest(), 6) +
            " gen=" + String(expertWorker_.GetGen()) + " -> " + expertTrainCartPath_ +
            " (best-val champion emitted, registered + manifest saved).", Color(0.6f, 1.0f, 0.6f));
    }
    else
        LogMessage("Expert training stopped before completion (cancelled) — no cart emitted.",
            Color(0.8f, 0.6f, 0.3f));

    expertTraining_ = false;
    expertTrainer_.Reset();
    expertDataset_.Reset();
    expertSeed_.Reset();
}

void Yuki::HandleUpdate(StringHash /*eventType*/, VariantMap& eventData)
{
    using namespace Update;
    float timeStep = eventData[P_TIMESTEP].GetFloat();

    // A2.3: advance the main-tick GPU solver (one non-blocking step/frame) when GPU mode is active. Runs
    // BEFORE the telemetry/graph block below so a pass completed this frame is reported the same frame.
    if (gpuPumpActive_)
        PumpGpuSolver();

    // P5: advance expert-subcart training one GA generation per frame (stepped off the tick so the UI stays
    // responsive instead of freezing on a synchronous Run()). No-op when not training.
    if (expertTraining_)
        StepExpertTraining();

    // /ingest: chunk+insert a bounded batch per frame so a big source (a book, a source tree) never freezes.
    if (ingestProcessing_)
        StepIngestProcess();

    auto* graphics = GetSubsystem<Graphics>();
    if (graphics && rootLayout_)
        rootLayout_->SetSize(graphics->GetWidth(), graphics->GetHeight());
    if (graphics && progressGraph_)
        progressGraph_->SetWidth(graphics->GetWidth());   // keep the graph spanning the window width on resize
    if (graphics && leakGraph_)
        leakGraph_->SetWidth(graphics->GetWidth());       // leak overlay tracks the same width

    auto* ui = GetSubsystem<UI>();
    if (ui && inputField_ && !ui->GetFocusElement())
        ui->SetFocusElement(inputField_);

    // Input erosion — Yuki eats, text dissolves
    if (eroding_)
    {
        erodeTimer_ += timeStep;
        if (erodeTimer_ >= erodeRate_)
        {
            erodeTimer_ = 0.0f;
            if (erodingText_.Length() > 0)
            {
                erodingText_ = erodingText_.Substring(1);
                inputField_->SetText(erodingText_);
            }

            if (erodingText_.Empty())
            {
                // Fully consumed — process and respond
                eroding_ = false;
                inputField_->SetText("");
                GetSubsystem<UI>()->SetFocusElement(inputField_);

                LogMessage("> " + pendingInput_, Color(1.0f, 1.0f, 0.7f));
                ProcessInput(pendingInput_);
                pendingInput_.Clear();
            }
        }
    }

    // Observe the background training worker (M2). LOCK-FREE status read: these are
    // scalar display values, so a benign race is fine — no mutex on the per-frame
    // path (that contention is what froze the input field). The mutex is taken ONLY
    // around the rare kick mutations, which actually touch weights/DB. Throttled to
    // ~5 Hz just to avoid rebuilding the status string every frame.
    statusTimer_ += timeStep;
    if (hasBrain_ && trainer_ && statusTimer_ >= 0.2f)
    {
        statusTimer_ = 0.0f;

        // Source training telemetry from the ACTIVE trainer. Under GPU-all-the-way the CPU
        // trainWorker_ is paused (its pass count + stats frozen), so the [Train] line, loss and
        // graph must come from the GPU worker. Separate per-worker lastSeen counters avoid a
        // spurious newPasses spike at the CPU<->GPU handover.
        unsigned newPasses = 0;
        YukiTrainStats stats;
        if (gpuPumpActive_)
        {
            const unsigned gp = gpuPassDone_;
            newPasses = gp - lastSeenGpuPass_;
            lastSeenGpuPass_ = gp;
            stats.loss = gpuLastPassLoss_;
            stats.samples = gpuLastPassSamples_;
        }
        else
        {
            const unsigned cp = trainWorker_.GetPassCount();
            newPasses = cp - lastSeenPass_;
            lastSeenPass_ = cp;
            stats = trainWorker_.GetLastStats();
        }

        // PBT kick completion — checked independently of pass count: during a kick the
        // worker runs no TrainOnce, so newPasses stays 0. The kick counter is the signal.
        const unsigned pbtCount = trainWorker_.GetPbtKickCount();
        if (pbtCount != lastSeenPbtKick_)
        {
            lastSeenPbtKick_ = pbtCount;
            const bool surpassed = trainWorker_.GetLastPbtSurpassed();
            const float floorAfter = trainer_->GetBestLoss();   // lock-free scalar; benign race
            if (surpassed)
                LogMessage("[Move] Surpassed — new floor " + String(floorAfter, 3) +
                    " beats " + String(kickStartLoss_, 3) + ". Extrapolation found a lower basin.",
                    Color(0.5f, 0.9f, 0.5f));
            else
            {
                LogMessage("[Move] Held at " + String(floorAfter, 3) +
                    " — no extrapolation beat the floor (model untouched).", Color(0.9f, 0.6f, 0.4f));
                if (ratchet_)
                {
                    ratchet_ = false;   // first hold = true floor along this direction
                    LogMessage("[Ratchet] True floor at " + String(floorAfter, 3) +
                        " — a difference move could no longer beat it. Stopping.", Color(0.6f, 0.8f, 1.0f));
                }
            }
        }

        // GPU-mode plateau-escape (difference-move) completion — the pump doesn't log inline (keeps the
        // pass step cheap), so report its result here, like the CPU PBT block above.
        if (gpuPumpActive_)
        {
            const unsigned gk = gpuKickCount_;
            if (gk != lastSeenGpuKick_)
            {
                lastSeenGpuKick_ = gk;
                const float floorAfter = trainer_->GetBestLoss();
                if (gpuKickSurpassed_)
                    LogMessage("[Move] Surpassed — new floor " + String(floorAfter, 4) +
                        " (GPU difference-move found a lower basin).", Color(0.5f, 0.9f, 0.5f));
                else
                    LogMessage("[Move] Held at " + String(floorAfter, 4) +
                        " — no extrapolation beat the floor (GPU, model untouched).", Color(0.9f, 0.6f, 0.4f));
            }
        }

        if (newPasses > 0)
        {
            // (lastSeen advanced above, per active worker)
            // Per-pass [Train] reinforced/loss line removed — loss lives on the status bar + graph, and
            // gradMax (with its NONFINITE divergence flag) now rides the status bar between floor and the
            // plateau marker, so the log stays for events, not per-pass spam.
            if (stats.expanded)
                LogMessage("[Train] Brain expanded!", Color(0.6f, 1.0f, 0.6f));

            // Auto/ratchet: on plateau, fire a PBT kick (one-shot for autokick;
            // ratchet persists until a kick fails to beat the floor). Skip if one is
            // already running — the worker processes one at a time.
            // Not while the GPU trainer owns training — it fires its own difference-moves on the
            // worker thread (the CPU worker is paused, so a RequestPbtKick here would be a no-op latch).
            if (!gpuPumpActive_ && (autoKick_ || ratchet_) && !trainWorker_.PbtBusy() && trainer_->IsPlateaued())
            {
                kickStartLoss_ = trainer_->GetBestLoss();
                trainWorker_.RequestPbtKick(1.0f, 0);   // F base 1.0
                autoKick_ = false;   // one-shot disarms; ratchet_ persists across moves
                LogMessage("[Move] Auto-fired on plateau (floor " +
                    String(kickStartLoss_, 3) + ")…", Color(0.9f, 0.7f, 0.4f));
            }
        }

        // Status bar — all lock-free reads, no DB on this path.
        if (model_ && model_->IsLoaded())
        {
            const YukiTopology& t = model_->GetTopology();
            String state;
            if (trainWorker_.PbtBusy())
                state = " | MOVE";
            else if (trainer_->IsPlateaued())
                state = " | PLATEAU";
            // Escape harness PBT rung: live "k/total" refinement counter right after the plateau marker, so
            // the background PBT rung visibly advances instead of looking like a hang.
            if (escapePbtRunning_)
                state += " PBT " + String(escapePbtWorker_.GetProgress()) + "/" + String(escapePbtWorker_.GetTotal());
            String floorStr = trainer_->HasElite() ? String(trainer_->GetBestLoss(), 3) : String("—");
            // GPU pass max|grad| (+ NONFINITE flag) rides the status bar between floor and the plateau
            // marker — the divergence signature at a glance. GPU mode only; blank for CPU/RUN.
            String gradStr = gpuPumpActive_
                ? (" gradMax:" + String(gpuLastGradMax_, 6) + (gpuLastGradFinite_ ? "" : " NONFINITE!"))
                : String::EMPTY;
            statusText_->SetText(String("Yuki v") + YUKI_VERSION + " | " +
                String(t.vocabSize) + " words | " +
                String(t.nLayers) + " layers | " +
                String((unsigned)(t.TotalWeightBytes() / 1024)) + " KB | " +
                "loss:" + String(stats.loss, 3) +
                " floor:" + floorStr + gradStr + state);

            // Feed the live loss to the overlay graph (this block is throttled ~5 Hz).
            // Issue #1: freeze the graph in RUN — neither solver is learning, so there's nothing to
            // plot; advance only while CPU or GPU is actually training, else it shovels stale loss.
            if (progressGraph_ && (gpuPumpActive_ || !trainWorker_.IsPaused()))
                progressGraph_->AddValue(stats.loss);

            // Leakage watch (red overlay): CURRENT process RSS in MB. Fed UNCONDITIONALLY (unlike the loss curve,
            // which freezes when idle) so the red line keeps advancing even in RUN — a leak can grow while no
            // solver is learning. Current RSS (not peak) can fall, so a real leak reads as a genuine upward drift
            // and a plateau reads as held; the graph's SetMinSpan anchor keeps that flat instead of amplified noise.
            if (leakGraph_)
            {
                double rssMb = YukiCurrentRssMb();
                if (rssMb > 0.0)
                    leakGraph_->AddValue((float)rssMb);
            }

            // Keep the learner button in sync with real state — covers mode changes made via the
            // /gputrain or /solver console commands, not just button clicks. Cheap, lock-free reads.
            UpdateSolverButton();
        }
    }

    // Karen telemetry — parasite emit on Yuki's existing 7879 server. Throttled ~1 Hz
    // (telemetry needs far less than the 5 Hz status block). Both values stay off the
    // train-mutex path: memoryCount_ is a main-thread scalar, and the WAL byte size is
    // a filesystem stat independent of the memoryDb_ handle. Skip all work (incl. the
    // file open) unless a viewer is actually attached.
    karenTimer_ += timeStep;
    if (karenTimer_ >= 1.0f)
    {
        karenTimer_ = 0.0f;
        auto* k = GetSubsystem<KarenClient>();
        if (k && k->GetViewerCount() > 0)
        {
            // WAL size: filesystem read of yuki_memory.db-wal; 0 if checkpointed away.
            long long walBytes = 0;
            if (auto* fs = GetSubsystem<FileSystem>())
            {
                File f(context_);
                if (f.Open(fs->GetProgramDir() + "Data/GameDB/yuki_memory.db-wal", FILE_READ))
                {
                    walBytes = (long long)f.GetSize();
                    f.Close();
                }
            }
            k->Plot("mem/memories", (float)memoryCount_);
            k->Plot("mem/wal_bytes", (float)walBytes);
            // Training telemetry — only while the GPU pump is live, so a viewer sees real
            // loss/gradMax, not a stale value frozen from the last run. gradMax carries the
            // divergence signal (goes huge / NONFINITE on a blow-up); loss is the pass mean.
            if (gpuPumpActive_)
            {
                k->Plot("train/loss", gpuLastPassLoss_);
                k->Plot("train/gradmax", gpuLastGradMax_);
            }
        }
    }
}

// ============================================================================
// Network — reliable UDP via SLikeNet
// ============================================================================

void Yuki::InitNetwork()
{
    auto* network = GetSubsystem<Network>();
    if (!network)
    {
        LogMessage("[ERROR] Network subsystem not available", Color::RED);
        return;
    }

    if (!network->StartServer(listenPort_))
    {
        LogMessage("[ERROR] Failed to start server on port " + String(listenPort_), Color::RED);
        return;
    }

    LogMessage("Listening on UDP port " + String(listenPort_));

    SubscribeToEvent(E_CLIENTCONNECTED, URHO3D_HANDLER(Yuki, HandleClientConnected));
    SubscribeToEvent(E_CLIENTDISCONNECTED, URHO3D_HANDLER(Yuki, HandleClientDisconnected));
    SubscribeToEvent(E_NETWORKMESSAGE, URHO3D_HANDLER(Yuki, HandleNetworkMessage));
}

void Yuki::HandleClientConnected(StringHash /*eventType*/, VariantMap& eventData)
{
    using namespace ClientConnected;
    auto* conn = static_cast<Connection*>(eventData[P_CONNECTION].GetPtr());
    LogMessage("[Net] Client connected: " + conn->ToString());
}

void Yuki::HandleClientDisconnected(StringHash /*eventType*/, VariantMap& eventData)
{
    using namespace ClientDisconnected;
    auto* conn = static_cast<Connection*>(eventData[P_CONNECTION].GetPtr());
    LogMessage("[Net] Client disconnected: " + conn->ToString());
}

// Yuki network protocol — anyone can talk to her.
// MSG_YUKI_SAY:   peer sends text, Yuki remembers and responds.
// MSG_YUKI_REPLY: Yuki sends response back to the peer.
// MSG_YUKI_TEACH: peer sends a fact, Yuki stores it without responding.
// MSG_YUKI_RECALL: peer asks a question, Yuki searches memory and responds.
static const int MSG_YUKI_SAY    = MSG_USER + 400;
static const int MSG_YUKI_REPLY  = MSG_USER + 401;
static const int MSG_YUKI_TEACH  = MSG_USER + 402;
static const int MSG_YUKI_RECALL = MSG_USER + 403;

void Yuki::HandleNetworkMessage(StringHash /*eventType*/, VariantMap& eventData)
{
    using namespace NetworkMessage;
    int msgID = eventData[P_MESSAGEID].GetI32();
    auto* conn = static_cast<Connection*>(eventData[P_CONNECTION].GetPtr());
    const Vector<byte>& data = eventData[P_DATA].GetBuffer();
    MemoryBuffer msg(data);

    if (msgID == MSG_YUKI_SAY)
    {
        // Peer says something — remember it, think about it, reply
        String text = msg.ReadString();
        String source = msg.ReadString();

        LogMessage("[" + source + "] " + text, Color(1.0f, 1.0f, 0.7f));
        Remember(text, source);

        // Generate response
        String response;
        if (hasBrain_ && inference_)
        {
            Vector<String> related = RecallMemories(text, 3);
            String context;
            for (unsigned i = 0; i < related.Size(); ++i)
            {
                if (related[i] != text)
                    context += related[i] + " ";
            }
            context += text;
            response = inference_->GenerateText(context, 64).Trimmed();
        }

        if (response.Empty())
        {
            Vector<String> related = RecallMemories(text, 1);
            response = related.Empty() ? "I heard you." : related[0];
            if (response.Length() > 120)
                response = response.Substring(0, 120) + "...";
        }
        else
        {
            // Only remember GENUINELY generated output, not the memory-recall fallback above —
            // re-storing a retrieved memory as if it were newly reasoned would just duplicate an
            // existing row under a false source. Same YUKI_SELF_SOURCE tag as the local
            // conversation path (ProcessInput) — one identity for Yuki's own output regardless of
            // which entry point produced it.
            Remember(response, YUKI_SELF_SOURCE);
        }

        LogMessage("[Yuki] " + response, Color(0.6f, 0.8f, 1.0f));

        // Send reply back
        VectorBuffer reply;
        reply.WriteString(response);
        conn->SendMessage(MSG_YUKI_REPLY, true, true, reply);
    }
    else if (msgID == MSG_YUKI_TEACH)
    {
        // Peer teaches a fact — store silently
        String text = msg.ReadString();
        String source = msg.ReadString();

        Remember(text, source);
        LogMessage("[Teach:" + source + "] " + text, Color(0.5f, 0.7f, 0.5f));
    }
    else if (msgID == MSG_YUKI_RECALL)
    {
        // Peer asks a question — search memory, reply
        String query = msg.ReadString();

        Vector<String> results = RecallMemories(query, 5);

        VectorBuffer reply;
        reply.WriteU32(results.Size());
        for (unsigned i = 0; i < results.Size(); ++i)
            reply.WriteString(results[i]);

        conn->SendMessage(MSG_YUKI_REPLY, true, true, reply);

        LogMessage("[Recall] " + query + " → " + String(results.Size()) + " results",
            Color(0.7f, 0.7f, 0.9f));
    }
    else
    {
        LogMessage("[Net] Unknown message " + String(msgID) + " from " + conn->ToString());
    }
}

// ============================================================================
// Corpus queries
// ============================================================================

// ============================================================================
// Brain — load or create cartridge, wire up inference
// ============================================================================

void Yuki::InitBrain()
{
    auto* fs = GetSubsystem<FileSystem>();
    String cartridgePath = fs->GetProgramDir() + "Data/GameDB/yuki_brain.cart";

    model_ = new YukiModel(context_);
    inference_ = new YukiInference(context_);

    if (fs->FileExists(cartridgePath))
    {
        // Load existing brain
        if (model_->Load(cartridgePath))
        {
            inference_->SetModel(model_);
            hasBrain_ = true;
            LogMessage("Brain loaded (" +
                String((unsigned)(model_->GetTopology().TotalWeightBytes() / 1024)) +
                " KB, " + String(model_->GetTopology().nLayers) + " layers)",
                Color(0.6f, 1.0f, 0.6f));
        }
        else
            LogMessage("Brain file corrupt — starting fresh", Color::YELLOW);
    }

    if (!hasBrain_)
    {
        // No brain yet — create a tiny one from scratch
        LogMessage("No brain found. Creating one...", Color(0.6f, 0.8f, 1.0f));

        // Build vocabulary from memories
        Vector<String> vocab;
        vocab.Push("<pad>");
        vocab.Push("<unk>");
        vocab.Push("<start>");
        vocab.Push("<end>");

        // Pull words from memories to build a working vocabulary
        if (memoryDb_)
        {
            DbResult r = memoryDb_->Execute(
                "SELECT text FROM memories ORDER BY id");
            const Vector<VariantVector>& rows = r.GetRows();

            HashSet<String> seen;
            for (unsigned i = 0; i < rows.Size(); ++i)
            {
                Vector<String> words = YukiModel::Tokenize(rows[i][0].GetString());
                for (const String& word : words)
                {
                    String clean = word.Trimmed();
                    if (clean.Length() >= 1 && !seen.Contains(clean) && vocab.Size() < 4096)   // >=1: punctuation tokens count now
                    {
                        seen.Insert(clean);
                        vocab.Push(clean);
                    }
                }
            }
        }

        // Pad to minimum vocab size
        while (vocab.Size() < 256)
            vocab.Push("<unused_" + String(vocab.Size()) + ">");

        YukiTopology topology;
        topology.embedDim = 128;
        topology.nLayers = 4;
        topology.nHeads = 4;
        topology.ffDim = 512;
        topology.vocabSize = vocab.Size();
        topology.maxSeqLen = 128;

        if (CreateEmptyCartridge(context_, cartridgePath, topology, vocab))
        {
            if (model_->Load(cartridgePath))
            {
                inference_->SetModel(model_);
                hasBrain_ = true;
                LogMessage("Brain created: " + String(vocab.Size()) + " words, " +
                    String((unsigned)(topology.TotalWeightBytes() / 1024)) + " KB",
                    Color(0.6f, 1.0f, 0.6f));
                LogMessage("I'm new. Talk to me — I'll learn.", Color(0.6f, 0.8f, 1.0f));
            }
        }
        else
        {
            LogMessage("[ERROR] Failed to create brain", Color::RED);
        }
    }

    // Wire up trainer — consumes memories, refines weights
    if (hasBrain_ && memoryDb_)
    {
        trainer_ = new YukiTrainer(context_);
        trainer_->SetModel(model_);
        trainer_->SetMemoryDb(memoryDb_);
        // Load the persisted elite (.best) and resume from it: seeds bestLoss_ from
        // the real best (so a worse state can't overwrite it) and restores the live
        // model to the best-ever, instead of the drifted working cartridge.
        trainer_->LoadElite();
        if (trainer_->HasElite())
        {
            trainer_->RestoreBest();
            LogMessage("Resumed at best floor " + String(trainer_->GetBestLoss(), 3) + ".",
                Color(0.6f, 0.8f, 1.0f));
        }
        URHO3D_LOGINFO("[Yuki] Trainer ready — background worker reinforces continuously");
    }

    // Federation P3: create the expert registry + output-layer dispatch, and attach the dispatch to the
    // CORE inference context. Additive and OFF by default — no expert is pinned until `/fed pin`, so
    // MaybeRoute is a zero-cost no-op and the core inference path is byte-identical until then.
    if (hasBrain_)
    {
        cartRegistry_ = new YukiCartRegistry(context_);
        dispatch_ = new YukiDispatch(context_, cartRegistry_);
        dispatch_->SetCore(model_);   // P6: interface reference for experts loaded on demand
        inference_->SetDispatch(dispatch_);
        URHO3D_LOGINFO("[Yuki] Federation ready (no expert pinned — use /fed to pin one)");
    }
}

String Yuki::GetCorpusStats()
{
    if (!corpusDb_)
        return "Corpus database not connected.";

    String stats = "=== Corpus ===\n";

    DbResult r = corpusDb_->Execute("SELECT COUNT(*), COALESCE(SUM(word_count),0), COALESCE(SUM(byte_size),0) FROM documents");
    const Vector<VariantVector>& rows = r.GetRows();
    if (!rows.Empty())
    {
        int docs = rows[0][0].GetI32();
        int words = rows[0][1].GetI32();
        int bytes = rows[0][2].GetI32();
        stats += "Documents: " + String(docs) + "  Words: " + String(words) +
                 "  Size: " + String(bytes / 1048576) + " MB";
    }

    return stats;
}

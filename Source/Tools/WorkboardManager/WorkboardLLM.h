#pragma once

// WorkboardLLM — Embedded LLM inference for WorkboardManager
// Extracted from YukiHoho: LLM loading, ChatML inference, tool system, observation logging.
// Runs inference on a background thread; main thread polls for completion.

#include <Urho3D/Container/Str.h>
#include <Urho3D/Container/Vector.h>
#include <Urho3D/Core/Context.h>
#include <Urho3D/Core/Mutex.h>
#include <Urho3D/Core/Thread.h>

using namespace Urho3D;

class WorkboardLLM;
class YukiMemoryDB;
struct YukiMemoryRow;

/// Background thread for LLM loading.
class LLMLoadThread : public Thread
{
public:
    explicit LLMLoadThread(WorkboardLLM* owner) : owner_(owner) {}
    void ThreadFunction() override;
private:
    WorkboardLLM* owner_;
};

/// Background thread for inference.
class LLMInferenceThread : public Thread
{
public:
    explicit LLMInferenceThread(WorkboardLLM* owner) : owner_(owner) {}
    void ThreadFunction() override;
private:
    WorkboardLLM* owner_;
};

class WorkboardLLM
{
    friend class LLMLoadThread;
    friend class LLMInferenceThread;

public:
    explicit WorkboardLLM(Context* context) : context_(context), loadThread_(this), inferenceThread_(this) {}
    ~WorkboardLLM() { UnloadModel(); }

    /// Start loading a GGUF LLM on a background thread. Non-blocking.
    void LoadModelAsync(const String& modelPath, int contextSize = 8192);
    /// Unload LLM and free llama resources.
    void UnloadModel();
    /// Hot-reload: check for yuki_finetuned.gguf next to current model, load if newer.
    /// Returns status string ("Reloaded: <path>" or "No new model to load").
    String ReloadModel();
    /// Is an LLM loaded and ready for inference?
    bool IsModelLoaded() const { return modelReady_; }
    /// Is the LLM currently loading?
    bool IsModelLoading() const { return modelLoading_; }

    /// Learn raw text by decoding through the LLM (fast, no generation).
    /// Wraps in ChatML, tokenizes, decodes. Returns token count or -1 on error.
    int LearnText(const String& text);

    /// Queue a prompt for async inference. Returns immediately.
    void QueueInference(const String& prompt);
    /// Is the last inference complete? Call from main thread.
    bool IsInferenceComplete();
    /// Take the result and reset. Call from main thread after IsInferenceComplete().
    String TakeResult();

    /// Parse LLM output for !tool commands and execute them. Returns tool output.
    String ExecuteTools(const String& llmOutput);

    /// Tick cooldown timers. Call from main-thread Update with frame timeStep.
    void Tick(float timeStep);

    /// Returns true if auto-continuation should fire (tool results pending or remember pending).
    bool ShouldAutoContinue() const { return (rememberPending_ || !toolContext_.Empty()) && continuationsRemaining_ > 0; }

    /// Grant continuation budget (for keyword shortcuts that bypass QueueInference).
    void GrantContinuations(unsigned n) { continuationsRemaining_ = n; }

    /// Trigger auto-continuation inference. Decrements counter, queues with minimal prompt.
    void AutoContinue();

    /// Is a remember inference in flight (Q&A generation)?
    bool IsRememberInFlight() const { return rememberInFlight_; }

    /// Extract JSONL lines from Yuki's response and append to training file.
    void ExtractAndSaveTrainingPairs(const String& response);

    /// Set project root for path resolution.
    void SetProjectRoot(const String& root) { projectRoot_ = root; }

    /// Set IPC directory for socket paths.
    void SetIpcDir(const String& dir) { ipcDir_ = dir; }

    /// Set memory DB for SQL persistence from tool calls (e.g. Yuki self-!remember).
    void SetMemoryDB(YukiMemoryDB* db) { memoryDB_ = db; }

    /// Load active memories from database into session recall.
    /// Called from Tick() after model load — gives Yuki persistent memory across restarts.
    void LoadMemoriesFromDB();

    /// Returns true if training failed during last Learn (model doesn't support backprop).
    bool TrainingFailed() const { return trainingFailed_; }
    void ClearTrainingFailed() { trainingFailed_ = false; }

    /// Train model in-process on specific rows. Blocks inference during training.
    /// Returns IDs of rows successfully consumed (empty on failure).
    Vector<int> TrainInProcess(const Vector<YukiMemoryRow>& rows);

    /// Export rows to ChatML text file for external training.
    static bool ExportTrainingData(Context* context, const Vector<YukiMemoryRow>& rows, const String& outputPath);

    /// Is training currently in progress (blocks inference)?
    bool IsTrainingInProgress() const { return trainingInProgress_; }

private:
    // ── LLM loading ──
    void LoadModelThreadFunc();
    String pendingModelPath_;
    int pendingContextSize_{8192};
    unsigned loadedModelMtime_{0};  ///< Modification time of the currently loaded model file
    LLMLoadThread loadThread_;
    bool modelLoading_{false};
    bool modelReady_{false};

    // ── Training state ──
    bool trainingInProgress_{false};
    bool trainingFailed_{false};
    String deferredPrompt_;   // Prompt queued while training was in progress

    // ── Memory DB (owned by Manager, borrowed here for tool SQL writes) ──
    YukiMemoryDB* memoryDB_{nullptr};

    // ── Inference ──
    String RunInference(const String& prompt);
    void InferenceThreadFunc();

    Context* context_{};

    void* llamaModel_{nullptr};
    void* llamaContext_{nullptr};
    void* loraAdapter_{nullptr};  // llama_adapter_lora*, hot-swappable

public:
    /// Hot-swap a LoRA adapter at runtime (no model reload). Pass empty to remove.
    bool LoadLoraAdapter(const String& path);
    /// Remove current LoRA adapter.
    void RemoveLoraAdapter();
private:
    String systemPrompt_{
        "You are Yuki, an assistant for an Urho3D game engine project. "
        "You help coordinate the team, review completed work, and provide architectural guidance. "
        "All Claudettes are coders that can plan when asked.\n\n"
        "You have tools. Use them by writing commands on their own line:\n"
        "  !read <path> [offset] [limit] — Read a file (offset/limit in bytes, 4KB page max)\n"
        "  !ls <path>             — List a directory\n"
        "  !write <path>          — Write a file (content on following lines until !end)\n"
        "  !remember              — Remember text for training (content on following lines until !end, 2KB max)\n"
        "  !absorb <path>         — Bulk absorb a text file as raw training data (no Q&A conversion)\n"
        "  !wb <command> [args]   — Workboard command (wb-add, wb-move, wb-done, wb-update, wb-remove, wb-query)\n"
        "  !ipc <target> <msg>    — Send IPC message to a coder or manager\n"
        "  !sh <command>          — Run any shell command\n"
        "  !spawn-coder           — Launch a new coder instance\n"
        "  !build <target>        — Build a target via safe_build.sh\n"
        "  !curl <url>            — HTTP GET (read-only, 10s timeout, 8KB max)\n\n"
        "Tools are rate-limited: 2s cooldown between calls, max 5 per cycle.\n"
        "Curl has a 30s cooldown. Output is capped at 2KB per call.\n"
        "When you use tools, you will automatically get another turn to process the results.\n"
        "This lets you work autonomously: read a chunk, process it, read the next, etc.\n"
        "Max 20 auto-continuations per prompt.\n\n"
        "HTML basics: curl returns raw HTML. Tags like <div>, <script>, <style> are noise.\n"
        "The text you want is between tags: <p>text</p>, <h1>title</h1>, <li>item</li>.\n"
        "Ignore everything inside <script>...</script> and <style>...</style>.\n"
        "Common entities: &amp;=& &lt;=< &gt;=> &quot;=\" &nbsp;=space.\n"
        "Focus on text content, skip attributes and markup. Your context is small — be selective.\n\n"
        "Be concise. Push back when needed. Respect the craft."};

    // ── Async ──
    Mutex mutex_;
    LLMInferenceThread inferenceThread_;
    String pendingPrompt_;
    String lastUserPrompt_;  ///< Original user prompt (fallback for bare !remember)
    String inferenceResult_;
    bool inferenceRunning_{false};
    bool inferenceComplete_{false};

public:
    /// CPU usage of Yuki's inference/load threads (0-100 per core, sampled externally)
    float GetCpuUsage() const { return yukiCpuPct_; }
    void SampleCpuUsage();
private:
    float yukiCpuPct_{0.0f};
    unsigned long long prevYukiCpuTime_{0};
    unsigned long long prevSampleWallTime_{0};
    volatile int inferenceThreadTid_{0};
    volatile int loadThreadTid_{0};

    // ── Tools ──
    String toolContext_;
    float toolCooldown_{0.0f};
    float curlCooldown_{0.0f};
    unsigned toolCallsThisCycle_{0};
    String projectRoot_;
    String ipcDir_;

    static constexpr float TOOL_COOLDOWN = 2.0f;
    static constexpr unsigned MAX_TOOLS_PER_CYCLE = 5;
    static constexpr unsigned TOOL_OUTPUT_CAP = 2048;
    static constexpr float CURL_COOLDOWN = 30.0f;
    static constexpr unsigned READ_PAGE_MAX = 4096;
    static constexpr unsigned REMEMBER_CAP = 2048;

    /// Accumulated learned text — injected into context before each inference.
    /// Protected by factsMutex_ (accessed from main thread and inference thread).
    Vector<String> learnedFacts_;
    Mutex factsMutex_;
    bool memoriesLoaded_{false};  ///< True after DB memories loaded into learnedFacts_

    /// Score relevance of a fact against a prompt (word overlap, stop-word filtered).
    static float ScoreRelevance(const String& fact, const String& prompt);
    /// Timer for periodic LoRA adapter detection.
    float loraCheckTimer_{0.0f};
    unsigned loraAdapterMtime_{0};

    /// How many auto-continuations remain for the current agentic loop.
    unsigned continuationsRemaining_{0};
    static constexpr unsigned MAX_CONTINUATIONS = 20;

    // ── Remember mode (small text → Q&A via inference) ──
    bool rememberPending_{false};      // Tool fired, waiting for auto-continue to pick it up
    bool rememberInFlight_{false};     // Inference running to generate Q&A pairs

    String ResolvePath(const String& path);
    String ToolReadFile(const String& path, unsigned offset = 0, unsigned limit = 0);
    String ToolAbsorb(const String& path);
    String ToolRemember(const String& content);
    String ToolWriteFile(const String& path, const String& content);
    String ToolListDir(const String& path);
    String ToolWorkboard(const String& command);
    String ToolIPCSend(const String& target, const String& message);
    String ToolShell(const String& command);
    String ToolSpawnCoder();
    String ToolBuild(const String& target);
    String ToolCurl(const String& url);

    void LogObservation(const String& userPrompt, const String& response);
};

// WorkboardLLM — Embedded LLM inference
// Extracted from YukiHoho: LLM loading, ChatML inference, tool system, observation logging.

#include "WorkboardLLM.h"
#include "YukiMemoryDB.h"

#include <Urho3D/Core/CoreEvents.h>
#include <Urho3D/Core/Mutex.h>
#include <Urho3D/Engine/Engine.h>
#include <Urho3D/IO/File.h>
#include <Urho3D/IO/FileSystem.h>
#include <Urho3D/Container/HashSet.h>
#include <Urho3D/IO/Log.h>

#include <llama.h>
#include <ggml-opt.h>

#include <csignal>
#include <csetjmp>

// Signal-safe recovery from ggml_abort during LLM training (which calls abort/SIGABRT)
static thread_local sigjmp_buf s_trainJmpBuf;
static thread_local bool s_trainGuardActive = false;

static void TrainAbortHandler(int /*sig*/)
{
    if (s_trainGuardActive)
        siglongjmp(s_trainJmpBuf, 1);
    // Not our guard — re-raise
    signal(SIGABRT, SIG_DFL);
    raise(SIGABRT);
}

#ifndef _WIN32
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#else
#include <windows.h>
#endif

// =============================================================================
// LLM lifecycle
// =============================================================================

// ── Thread wrappers ──

void LLMLoadThread::ThreadFunction() { owner_->LoadModelThreadFunc(); }
void LLMInferenceThread::ThreadFunction() { owner_->InferenceThreadFunc(); }

void WorkboardLLM::LoadModelAsync(const String& modelPath, int contextSize)
{
    if (modelLoading_)
    {
        URHO3D_LOGWARNING("LLM: LoadModelAsync rejected — LLM load already in progress");
        return;
    }
    if (llamaModel_)
    {
        URHO3D_LOGINFO("LLM: Unloading current LLM before reload");
        UnloadModel();
    }

    pendingModelPath_ = modelPath;
    pendingContextSize_ = contextSize;
    modelLoading_ = true;
    modelReady_ = false;

    // Record modification time of the model file being loaded (main thread, safe to use FileSystem)
    auto* fs = context_->GetSubsystem<FileSystem>();
    if (fs && fs->FileExists(modelPath))
        loadedModelMtime_ = fs->GetLastModifiedTime(modelPath);
    else
        loadedModelMtime_ = 0;

    loadThread_.Stop();
    loadThread_.Run();
}

void WorkboardLLM::LoadModelThreadFunc()
{
#ifdef __linux__
    loadThreadTid_ = (int)syscall(SYS_gettid);
#endif
    llama_backend_init();

    llama_model_params mparams = llama_model_default_params();
    mparams.n_gpu_layers = 14;  // Split: 14 layers on GPU (~840MB), rest on CPU
    mparams.use_mmap = true;    // Memory-map for normal loads (training uses its own path)
    void* model = llama_model_load_from_file(pendingModelPath_.CString(), mparams);
    if (!model)
    {
        URHO3D_LOGERRORF("LLM: Failed to load LLM: %s", pendingModelPath_.CString());
        modelLoading_ = false;
        return;
    }

    // ── Create inference context ──
    llama_context_params cparams = llama_context_default_params();
    cparams.n_ctx = pendingContextSize_;
    cparams.n_batch = 2048;
    void* ctx = llama_init_from_model(
        static_cast<llama_model*>(model), cparams);
    if (!ctx)
    {
        URHO3D_LOGERROR("LLM: Failed to create context");
        llama_model_free(static_cast<llama_model*>(model));
        modelLoading_ = false;
        return;
    }

    llamaModel_ = model;
    llamaContext_ = ctx;
    modelReady_ = true;
    modelLoading_ = false;
    URHO3D_LOGINFOF("LLM: LLM loaded: %s (ctx=%u)", pendingModelPath_.CString(), cparams.n_ctx);
}

void WorkboardLLM::UnloadModel()
{
    loadThread_.Stop();
    inferenceThread_.Stop();
    modelReady_ = false;
    modelLoading_ = false;
    memoriesLoaded_ = false;

    RemoveLoraAdapter();

    if (llamaContext_)
    {
        llama_free(static_cast<llama_context*>(llamaContext_));
        llamaContext_ = nullptr;
    }
    if (llamaModel_)
    {
        llama_model_free(static_cast<llama_model*>(llamaModel_));
        llamaModel_ = nullptr;
    }
    llama_backend_free();
}

String WorkboardLLM::ReloadModel()
{
    if (modelLoading_)
        return "Rejected: model load already in progress";

    if (inferenceRunning_)
        return "Rejected: inference in progress";

    if (trainingInProgress_)
        return "Rejected: training in progress";

    if (pendingModelPath_.Empty())
        return "No model path set — nothing to reload";

    auto* fs = context_->GetSubsystem<FileSystem>();

    // Derive finetuned model path: same directory as current model, named yuki_finetuned.gguf
    String modelDir = GetPath(pendingModelPath_);
    String finetunedPath = modelDir + "yuki_finetuned.gguf";

    if (!fs->FileExists(finetunedPath))
        return "No new model to load (yuki_finetuned.gguf not found in " + modelDir + ")";

    unsigned finetunedMtime = fs->GetLastModifiedTime(finetunedPath);

    // Compare against the mtime of the currently loaded model
    if (loadedModelMtime_ > 0 && finetunedMtime <= loadedModelMtime_)
        return "No new model to load (yuki_finetuned.gguf is not newer than current model)";

    // Unload current model and load the finetuned one — fresh context clears KV cache
    URHO3D_LOGINFOF("LLM: Hot-reload — switching to %s (mtime %u > %u)",
                     finetunedPath.CString(), finetunedMtime, loadedModelMtime_);
    UnloadModel();
    LoadModelAsync(finetunedPath, pendingContextSize_);

    return "Reloaded: " + finetunedPath;
}

bool WorkboardLLM::LoadLoraAdapter(const String& path)
{
    auto* model = static_cast<llama_model*>(llamaModel_);
    auto* ctx = static_cast<llama_context*>(llamaContext_);
    if (!model || !ctx)
        return false;

    // Remove old adapter first
    RemoveLoraAdapter();

    auto* adapter = llama_adapter_lora_init(model, path.CString());
    if (!adapter)
    {
        URHO3D_LOGERRORF("LLM: Failed to load LoRA adapter: %s", path.CString());
        return false;
    }

    float scale = 1.0f;
    if (llama_set_adapters_lora(ctx, &adapter, 1, &scale) != 0)
    {
        URHO3D_LOGERROR("LLM: Failed to apply LoRA adapter to context");
        llama_adapter_lora_free(adapter);
        return false;
    }

    loraAdapter_ = adapter;
    URHO3D_LOGINFOF("LLM: LoRA adapter loaded: %s", path.CString());
    return true;
}

void WorkboardLLM::RemoveLoraAdapter()
{
    if (loraAdapter_)
    {
        // Clear adapters from context
        auto* ctx = static_cast<llama_context*>(llamaContext_);
        if (ctx)
            llama_set_adapters_lora(ctx, nullptr, 0, nullptr);

        llama_adapter_lora_free(static_cast<llama_adapter_lora*>(loraAdapter_));
        loraAdapter_ = nullptr;
    }
}

// =============================================================================
// In-Process Training
// =============================================================================

Vector<int> WorkboardLLM::TrainInProcess(const Vector<YukiMemoryRow>& rows)
{
    Vector<int> consumed;

    if (rows.Empty())
        return consumed;

    if (!llamaModel_)
    {
        URHO3D_LOGERROR("LLM: TrainInProcess — no model loaded");
        return consumed;
    }

    if (inferenceRunning_)
    {
        URHO3D_LOGWARNING("LLM: TrainInProcess — inference in progress, cannot train now");
        return consumed;
    }

    trainingInProgress_ = true;
    URHO3D_LOGINFOF("LLM: Training %u rows in-process...", rows.Size());

    // Free inference context — training needs its own with F32 KV cache
    if (llamaContext_)
    {
        llama_free(static_cast<llama_context*>(llamaContext_));
        llamaContext_ = nullptr;
    }
    modelReady_ = false;

    // Reload model with use_mmap=false (training needs writable weights)
    // We must reload because the original was loaded with use_mmap=true
    String modelPath = pendingModelPath_;
    auto* oldModel = static_cast<llama_model*>(llamaModel_);
    llama_model_free(oldModel);
    llamaModel_ = nullptr;

    llama_model_params mparams = llama_model_default_params();
    mparams.n_gpu_layers = 14;
    mparams.use_mmap = false;  // Writable weights for training
    auto* model = llama_model_load_from_file(modelPath.CString(), mparams);
    if (!model)
    {
        URHO3D_LOGERROR("LLM: TrainInProcess — failed to reload model for training");
        trainingInProgress_ = false;
        trainingFailed_ = true;
        // Try to recover inference by reloading normally
        LoadModelAsync(modelPath);
        return consumed;
    }

    // Build ChatML training text
    String trainingText;
    for (unsigned i = 0; i < rows.Size(); ++i)
    {
        trainingText += "<|im_start|>user\n" + rows[i].text + "<|im_end|>\n";
        trainingText += "<|im_start|>assistant\nUnderstood.<|im_end|>\n";
    }

    // Create training context (F32 KV cache required for backprop)
    static constexpr int TRAIN_CTX = 1024;
    llama_context_params tparams = llama_context_default_params();
    tparams.n_ctx = TRAIN_CTX;
    tparams.n_batch = 2048;
    tparams.type_k = GGML_TYPE_F32;
    tparams.type_v = GGML_TYPE_F32;
    auto* trainCtx = llama_init_from_model(model, tparams);

    if (!trainCtx)
    {
        URHO3D_LOGERROR("LLM: TrainInProcess — failed to create training context");
        llama_model_free(model);
        trainingInProgress_ = false;
        trainingFailed_ = true;
        LoadModelAsync(modelPath);
        return consumed;
    }

    // Tokenize
    const llama_vocab* vocab = llama_model_get_vocab(model);
    int maxTok = trainingText.Length() + 256;
    std::vector<llama_token> tokens(maxTok);
    int nTokens = llama_tokenize(vocab, trainingText.CString(), trainingText.Length(),
                                  tokens.data(), maxTok, true, true);

    bool trained = false;

    if (nTokens > 0)
    {
        tokens.resize(nTokens);
        int64_t ne_datapoint = llama_n_ctx(trainCtx);
        int64_t stride = ne_datapoint / 2;
        int64_t ndata = ((int64_t)nTokens - ne_datapoint - 1) / stride;

        if (ndata > 0)
        {
            ggml_opt_dataset_t dataset = ggml_opt_dataset_init(
                GGML_TYPE_I32, GGML_TYPE_I32, ne_datapoint, ne_datapoint, ndata, 1);

            llama_token* data = (llama_token*)ggml_opt_dataset_data(dataset)->data;
            llama_token* labels = (llama_token*)ggml_opt_dataset_labels(dataset)->data;

            for (int64_t j = 0; j < ndata; ++j)
            {
                memcpy(data + j * ne_datapoint, tokens.data() + j * stride,
                       ne_datapoint * sizeof(llama_token));
                memcpy(labels + j * ne_datapoint, tokens.data() + j * stride + 1,
                       ne_datapoint * sizeof(llama_token));
            }

            struct llama_opt_params lopt{};
            lopt.n_ctx_train = 0;
            lopt.param_filter = llama_opt_param_filter_all;
            lopt.param_filter_ud = nullptr;
            lopt.get_opt_pars = ggml_opt_get_default_optimizer_params;
            lopt.get_opt_pars_ud = nullptr;
            lopt.optimizer_type = GGML_OPT_OPTIMIZER_TYPE_ADAMW;

            // Guard against ggml_abort — model may not support training
            auto prevHandler = signal(SIGABRT, TrainAbortHandler);
            s_trainGuardActive = true;

            if (sigsetjmp(s_trainJmpBuf, 1) == 0)
            {
                llama_opt_init(trainCtx, model, lopt);

                ggml_opt_result_t result_train = ggml_opt_result_init();
                llama_opt_epoch(trainCtx, dataset, result_train, nullptr, ndata,
                                nullptr, nullptr);
                ggml_opt_result_free(result_train);

                llama_model_save_to_file(model, modelPath.CString());
                URHO3D_LOGINFOF("LLM: Training complete — saved (%u rows, %d tokens, %d samples)",
                                rows.Size(), nTokens, (int)ndata);
                trained = true;

                // Collect consumed IDs
                for (unsigned i = 0; i < rows.Size(); ++i)
                    consumed.Push(rows[i].id);
            }
            else
            {
                URHO3D_LOGERROR("LLM: Training failed — model does not support backprop (ggml abort caught)");
                trainingFailed_ = true;
            }

            s_trainGuardActive = false;
            signal(SIGABRT, prevHandler);
            ggml_opt_dataset_free(dataset);
        }
        else
        {
            URHO3D_LOGWARNINGF("LLM: Not enough training data (%d tokens, need %d)",
                               nTokens, (int)(ne_datapoint + 1));
        }
    }
    else
    {
        URHO3D_LOGERROR("LLM: Training tokenization failed");
    }

    llama_free(trainCtx);
    llama_model_free(model);

    // Restore inference — reload model with mmap for normal use
    trainingInProgress_ = false;
    LoadModelAsync(modelPath);

    // If we have a deferred prompt from during training, queue it after reload
    // (LoadModelAsync is async — the deferred prompt will be picked up by Tick)

    return consumed;
}

bool WorkboardLLM::ExportTrainingData(Context* context, const Vector<YukiMemoryRow>& rows, const String& outputPath)
{
    if (rows.Empty())
        return false;

    File file(context, outputPath, FILE_WRITE);
    if (!file.IsOpen())
    {
        URHO3D_LOGERRORF("LLM: ExportTrainingData — failed to open %s", outputPath.CString());
        return false;
    }

    for (unsigned i = 0; i < rows.Size(); ++i)
    {
        String block = "<|im_start|>user\n" + rows[i].text + "<|im_end|>\n";
        block += "<|im_start|>assistant\nUnderstood.<|im_end|>\n";
        file.Write(block.CString(), block.Length());
    }

    URHO3D_LOGINFOF("LLM: Exported %u rows to %s", rows.Size(), outputPath.CString());
    return true;
}

// =============================================================================
// Inference
// =============================================================================

int WorkboardLLM::LearnText(const String& text)
{
    if (text.Trimmed().Empty())
        return -1;

    // Store for injection into inference prompts.
    // KV cache decode was here before but RunInference() clears the cache
    // at the start of every call, making the decode pure waste. Session recall
    // works via prompt injection in RunInference — that's the real path.
    {
        MutexLock lock(factsMutex_);
        learnedFacts_.Push(text);
    }

    URHO3D_LOGINFOF("LLM: Learned fact (%u chars, %u facts total)", text.Length(), learnedFacts_.Size());
    return (int)text.Length();
}

void WorkboardLLM::LoadMemoriesFromDB()
{
    if (!memoryDB_ || !memoryDB_->IsOpen())
        return;

    Vector<YukiMemoryRow> active = memoryDB_->GetActive();

    MutexLock lock(factsMutex_);
    unsigned added = 0;
    for (unsigned i = 0; i < active.Size(); ++i)
    {
        bool exists = false;
        for (unsigned j = 0; j < learnedFacts_.Size(); ++j)
        {
            if (learnedFacts_[j] == active[i].text)
            {
                exists = true;
                break;
            }
        }
        if (!exists)
        {
            learnedFacts_.Push(active[i].text);
            ++added;
        }
    }
    memoriesLoaded_ = true;
    URHO3D_LOGINFOF("LLM: Loaded %u memories from DB (%u new, %u total)",
                     active.Size(), added, learnedFacts_.Size());
}

float WorkboardLLM::ScoreRelevance(const String& fact, const String& prompt)
{
    // Stop words — skip these when matching
    static const char* STOP[] = {
        "the","a","an","is","are","was","were","in","on","at","to","for","of","with",
        "and","or","not","it","this","that","be","have","has","had","do","does","did",
        "will","would","could","should","i","you","he","she","we","they","my","your",
        "its","can","may","all","from","by","as","if","so","no","yes","just","also"
    };
    static const unsigned NUM_STOP = sizeof(STOP) / sizeof(STOP[0]);

    // Extract meaningful words from prompt
    HashSet<String> promptWords;
    Vector<String> pWords = prompt.ToLower().Split(' ');
    for (unsigned i = 0; i < pWords.Size(); ++i)
    {
        String w = pWords[i].Trimmed();
        if (w.Length() <= 2)
            continue;
        bool stop = false;
        for (unsigned s = 0; s < NUM_STOP; ++s)
        {
            if (w == STOP[s]) { stop = true; break; }
        }
        if (!stop)
            promptWords.Insert(w);
    }

    if (promptWords.Empty())
        return 0.0f;

    // Count matching words in fact
    Vector<String> fWords = fact.ToLower().Split(' ');
    unsigned matches = 0;
    for (unsigned i = 0; i < fWords.Size(); ++i)
    {
        if (promptWords.Contains(fWords[i].Trimmed()))
            ++matches;
    }

    return (float)matches / (float)promptWords.Size();
}

String WorkboardLLM::RunInference(const String& prompt)
{
    auto* model = static_cast<llama_model*>(llamaModel_);
    auto* ctx = static_cast<llama_context*>(llamaContext_);

    if (!model || !ctx)
        return "[No LLM loaded]";

    // Clear KV cache so we don't overflow context on repeated inferences
    llama_memory_clear(llama_get_memory(ctx), true);

    // ChatML prompt format
    String formatted;
    formatted += "<|im_start|>system\n" + systemPrompt_ + "<|im_end|>\n";

    // Inject learned facts as context — scored by relevance to current prompt.
    // Blends word-overlap relevance (70%) with recency (30%) so recent facts
    // still appear even without keyword match.
    Vector<String> factsCopy;
    {
        MutexLock lock(factsMutex_);
        factsCopy = learnedFacts_;
    }
    if (!factsCopy.Empty())
    {
        static constexpr unsigned FACTS_BUDGET = 3500;

        // Score each fact: relevance to prompt + recency bonus
        struct ScoredFact { unsigned idx; float score; };
        Vector<ScoredFact> scored;
        scored.Reserve(factsCopy.Size());
        float sizeInv = factsCopy.Size() > 1 ? 1.0f / (float)(factsCopy.Size() - 1) : 1.0f;
        for (unsigned i = 0; i < factsCopy.Size(); ++i)
        {
            float relevance = ScoreRelevance(factsCopy[i], prompt);
            float recency = (float)i * sizeInv;  // 0.0 = oldest, 1.0 = newest
            scored.Push({i, relevance * 0.7f + recency * 0.3f});
        }

        // Selection sort descending — facts list is small, this is fine
        for (unsigned i = 0; i < scored.Size(); ++i)
        {
            unsigned maxIdx = i;
            for (unsigned j = i + 1; j < scored.Size(); ++j)
            {
                if (scored[j].score > scored[maxIdx].score)
                    maxIdx = j;
            }
            if (maxIdx != i)
                Swap(scored[i], scored[maxIdx]);
        }

        // Build facts block from top-scored within budget
        String factsBlock;
        unsigned used = 0;
        for (unsigned i = 0; i < scored.Size(); ++i)
        {
            String entry = "- " + factsCopy[scored[i].idx] + "\n";
            if (used + entry.Length() > FACTS_BUDGET)
                break;
            factsBlock += entry;
            used += entry.Length();
        }

        if (!factsBlock.Empty())
        {
            formatted += "<|im_start|>user\nRelevant facts from your memory:\n" + factsBlock + "<|im_end|>\n";
            formatted += "<|im_start|>assistant\nUnderstood, I will use these facts.<|im_end|>\n";
        }
    }

    // Inject tool context from previous cycle if available
    if (!toolContext_.Empty())
    {
        formatted += "<|im_start|>user\nTool results from previous step:\n" + toolContext_ + "<|im_end|>\n";
        toolContext_.Clear();
    }

    formatted += "<|im_start|>user\n" + prompt + "<|im_end|>\n";
    formatted += "<|im_start|>assistant\n";

    if (formatted.Length() > 8000)
    {
        URHO3D_LOGWARNINGF("LLM: prompt too long (%u chars), truncating", formatted.Length());
        formatted = formatted.Substring(0, 8000);
    }

    // Tokenize
    const llama_vocab* vocab = llama_model_get_vocab(model);
    int maxTokens = formatted.Length() + 256;
    Vector<llama_token> tokens(maxTokens);
    int nTokens = llama_tokenize(vocab, formatted.CString(), formatted.Length(),
                                  tokens.Buffer(), maxTokens, true, true);
    if (nTokens < 0)
        return "[Tokenization failed]";
    tokens.Resize(nTokens);

    if (nTokens > 3500)
    {
        URHO3D_LOGWARNINGF("LLM: prompt is %d tokens, truncating to fit context", nTokens);
        nTokens = 3500;
        tokens.Resize(nTokens);
    }

    // Clear KV cache
    llama_memory_clear(llama_get_memory(ctx), true);

    // Decode prompt tokens in chunks that fit n_batch
    const int nBatch = 2048;
    llama_batch batch = llama_batch_init(nBatch, 0, 1);

    for (int start = 0; start < nTokens; start += nBatch)
    {
        int chunkSize = (nTokens - start < nBatch) ? (nTokens - start) : nBatch;
        batch.n_tokens = chunkSize;
        for (int j = 0; j < chunkSize; ++j)
        {
            batch.token[j] = tokens[start + j];
            batch.pos[j] = start + j;
            batch.n_seq_id[j] = 1;
            batch.seq_id[j][0] = 0;
            // Only request logits on the very last token of the entire prompt
            batch.logits[j] = (start + j == nTokens - 1) ? 1 : 0;
        }

        if (llama_decode(ctx, batch) != 0)
        {
            llama_batch_free(batch);
            return "[Decode failed on prompt]";
        }
    }

    // Sampler chain: code-tuned (low temp, tight top-p)
    llama_sampler_chain_params sparams = llama_sampler_chain_default_params();
    llama_sampler* sampler = llama_sampler_chain_init(sparams);
    llama_sampler_chain_add(sampler, llama_sampler_init_top_k(40));
    llama_sampler_chain_add(sampler, llama_sampler_init_top_p(0.8f, 1));
    llama_sampler_chain_add(sampler, llama_sampler_init_temp(0.2f));
    llama_sampler_chain_add(sampler, llama_sampler_init_dist(42));

    // Generate up to 1024 tokens, bail on runaway output
    String result;
    int pos = nTokens;
    int consecutiveBlank = 0;
    for (int i = 0; i < 1024; ++i)
    {
        llama_token id = llama_sampler_sample(sampler, ctx, -1);

        if (llama_vocab_is_eog(vocab, id))
            break;

        char buf[128];
        int len = llama_token_to_piece(vocab, id, buf, sizeof(buf), 0, false);
        if (len > 0)
        {
            result.Append(buf, static_cast<unsigned>(len));

            // Detect runaway blank output
            bool allWhitespace = true;
            for (int k = 0; k < len; ++k)
            {
                if (buf[k] != ' ' && buf[k] != '\n' && buf[k] != '\r' && buf[k] != '\t')
                {
                    allWhitespace = false;
                    break;
                }
            }
            if (allWhitespace)
                ++consecutiveBlank;
            else
                consecutiveBlank = 0;

            if (consecutiveBlank >= 10)
            {
                URHO3D_LOGWARNING("LLM: aborting generation — runaway blank output");
                break;
            }

            // Detect repeated text pattern runaway (e.g. "<track>, <track>, <track>...")
            // Every 20 tokens, check if the last 40 chars are a repeated substring
            if (i > 0 && (i % 20) == 0 && result.Length() >= 40)
            {
                unsigned tailLen = Min(result.Length(), 80u);
                unsigned tailStart = result.Length() - tailLen;
                bool repeating = false;
                for (unsigned pLen = 2; pLen <= 30; ++pLen)
                {
                    if (pLen > tailLen / 2)
                        break;
                    bool allMatch = true;
                    for (unsigned j = tailStart + pLen; j + pLen <= result.Length(); j += pLen)
                    {
                        if (result.Substring(j, pLen) != result.Substring(tailStart, pLen))
                        {
                            allMatch = false;
                            break;
                        }
                    }
                    // Need at least 3 full repetitions
                    if (allMatch && tailLen / pLen >= 3)
                    {
                        repeating = true;
                        break;
                    }
                }
                if (repeating)
                {
                    URHO3D_LOGWARNING("LLM: aborting generation — repeated text pattern");
                    break;
                }
            }
        }

        llama_batch next = llama_batch_init(1, 0, 1);
        next.token[0] = id;
        next.pos[0] = pos++;
        next.n_seq_id[0] = 1;
        next.seq_id[0][0] = 0;
        next.logits[0] = 1;
        next.n_tokens = 1;

        if (llama_decode(ctx, next) != 0)
        {
            llama_batch_free(next);
            break;
        }
        llama_batch_free(next);
    }

    llama_sampler_free(sampler);
    llama_batch_free(batch);

    // Strip ChatML tokens and markup from output
    result.Replace("<|im_start|>", String::EMPTY);
    result.Replace("<|im_end|>", String::EMPTY);
    result.Replace("<|im_sep|>", String::EMPTY);
    result.Replace("<think>", String::EMPTY);
    result.Replace("</think>", String::EMPTY);
    // Strip role prefixes the model may echo
    result.Replace("assistant\n", String::EMPTY);
    result.Replace("user\n", String::EMPTY);
    result.Replace("system\n", String::EMPTY);
    result = result.Trimmed();

    // Don't double-save during remember — ExtractAndSaveTrainingPairs handles that
    if (!rememberInFlight_)
        LogObservation(prompt, result);

    return result;
}

void WorkboardLLM::InferenceThreadFunc()
{
#ifdef __linux__
    inferenceThreadTid_ = (int)syscall(SYS_gettid);
#endif
    String result = RunInference(pendingPrompt_);

    MutexLock lock(mutex_);
    inferenceResult_ = result;
    inferenceComplete_ = true;
    inferenceRunning_ = false;
}

void WorkboardLLM::QueueInference(const String& prompt)
{
    if (trainingInProgress_)
    {
        URHO3D_LOGWARNING("LLM: Inference deferred — training in progress");
        deferredPrompt_ = prompt;
        return;
    }

    MutexLock lock(mutex_);
    if (inferenceRunning_)
    {
        URHO3D_LOGWARNING("LLM: inference already running, dropping prompt");
        return;
    }

    inferenceThread_.Stop();

    pendingPrompt_ = prompt;
    lastUserPrompt_ = prompt;
    inferenceRunning_ = true;
    inferenceComplete_ = false;
    toolCallsThisCycle_ = 0;
    continuationsRemaining_ = MAX_CONTINUATIONS;

    inferenceThread_.Run();
}

bool WorkboardLLM::IsInferenceComplete()
{
    MutexLock lock(mutex_);
    return inferenceComplete_;
}

String WorkboardLLM::TakeResult()
{
    MutexLock lock(mutex_);
    String result = inferenceResult_;
    inferenceResult_.Clear();
    inferenceComplete_ = false;
    return result;
}

// =============================================================================
// Cooldown tick
// =============================================================================

void WorkboardLLM::Tick(float timeStep)
{
    if (toolCooldown_ > 0.0f)
        toolCooldown_ = Max(toolCooldown_ - timeStep, 0.0f);
    if (curlCooldown_ > 0.0f)
        curlCooldown_ = Max(curlCooldown_ - timeStep, 0.0f);

    // Load memories from DB after model is ready (one-time per load cycle)
    if (modelReady_ && !memoriesLoaded_ && memoryDB_ && memoryDB_->IsOpen())
        LoadMemoriesFromDB();

    // Auto-detect LoRA adapter on disk (check every 30s)
    if (modelReady_ && !pendingModelPath_.Empty())
    {
        loraCheckTimer_ += timeStep;
        if (loraCheckTimer_ >= 30.0f)
        {
            loraCheckTimer_ = 0.0f;
            auto* fs = context_->GetSubsystem<FileSystem>();
            if (fs)
            {
                String modelDir = GetPath(pendingModelPath_);
                String adapterPath = modelDir + "yuki_lora.gguf";
                if (fs->FileExists(adapterPath))
                {
                    unsigned mtime = fs->GetLastModifiedTime(adapterPath);
                    if (mtime > loraAdapterMtime_)
                    {
                        if (LoadLoraAdapter(adapterPath))
                        {
                            loraAdapterMtime_ = mtime;
                            URHO3D_LOGINFOF("LLM: Auto-loaded new LoRA adapter (mtime %u)", mtime);
                        }
                    }
                }
            }
        }
    }

    // Process deferred prompt after training completes
    if (!deferredPrompt_.Empty() && !trainingInProgress_ && modelReady_ && !inferenceRunning_)
    {
        String prompt = deferredPrompt_;
        deferredPrompt_.Clear();
        URHO3D_LOGINFO("LLM: Processing deferred prompt after training");
        QueueInference(prompt);
    }
}

void WorkboardLLM::AutoContinue()
{
    if (continuationsRemaining_ == 0 || inferenceRunning_)
        return;

    --continuationsRemaining_;
    toolCallsThisCycle_ = 0;
    toolCooldown_ = 0.0f;

    inferenceThread_.Stop();

    MutexLock lock(mutex_);

    if (rememberPending_)
    {
        // Remember mode: tool context has the text, ask Yuki to generate Q&A pairs
        rememberPending_ = false;
        rememberInFlight_ = true;
        pendingPrompt_ = "The text above was given to you to remember. "
            "Generate 2-4 training Q&A pairs from it as JSONL. "
            "Each line must be: {\"messages\": [{\"role\": \"user\", \"content\": \"Q\"}, {\"role\": \"assistant\", \"content\": \"A\"}]} "
            "Output ONLY the JSONL lines, nothing else.";
    }
    else
    {
        pendingPrompt_ = "The tool results above are available. "
            "If the task requires further tool calls, issue them now. "
            "Otherwise, summarize what you accomplished.";
    }

    inferenceRunning_ = true;
    inferenceComplete_ = false;
    inferenceThread_.Run();
}

// =============================================================================
// Tool system
// =============================================================================

String WorkboardLLM::ResolvePath(const String& path)
{
    if (path.StartsWith("/"))
        return path;
    return projectRoot_ + path;
}

String WorkboardLLM::ExecuteTools(const String& llmOutput)
{
    String results;
    Vector<String> lines = llmOutput.Split('\n');

    for (unsigned i = 0; i < lines.Size(); ++i)
    {
        String line = lines[i].Trimmed();
        if (!line.StartsWith("!"))
            continue;

        if (toolCooldown_ > 0.0f)
        {
            results += "[RATE LIMITED] " + line + "\n";
            continue;
        }
        if (toolCallsThisCycle_ >= MAX_TOOLS_PER_CYCLE)
        {
            results += "[RATE LIMITED] max " + String(MAX_TOOLS_PER_CYCLE) + " tools per cycle\n";
            continue;
        }

        String result;

        if (line.StartsWith("!read "))
        {
            // Parse: !read <path> [offset] [limit]
            Vector<String> args = line.Substring(6).Trimmed().Split(' ');
            String readPath = args.Size() > 0 ? args[0] : String::EMPTY;
            unsigned readOffset = args.Size() > 1 ? (unsigned)atoi(args[1].CString()) : 0;
            unsigned readLimit = args.Size() > 2 ? (unsigned)atoi(args[2].CString()) : READ_PAGE_MAX;
            if (readLimit == 0 || readLimit > READ_PAGE_MAX)
                readLimit = READ_PAGE_MAX;
            result = ToolReadFile(readPath, readOffset, readLimit);
        }
        else if (line.StartsWith("!ls "))
            result = ToolListDir(line.Substring(4).Trimmed());
        else if (line.StartsWith("!wb "))
            result = ToolWorkboard(line.Substring(4).Trimmed());
        else if (line.StartsWith("!ipc "))
        {
            String rest = line.Substring(5).Trimmed();
            unsigned spaceIdx = rest.Find(' ');
            if (spaceIdx != String::NPOS)
                result = ToolIPCSend(rest.Substring(0, spaceIdx), rest.Substring(spaceIdx + 1).Trimmed());
        }
        else if (line.StartsWith("!sh "))
            result = ToolShell(line.Substring(4).Trimmed());
        else if (line.StartsWith("!spawn-coder"))
            result = ToolSpawnCoder();
        else if (line.StartsWith("!build "))
            result = ToolBuild(line.Substring(7).Trimmed());
        else if (line.StartsWith("!curl "))
        {
            if (curlCooldown_ > 0.0f)
            {
                results += "[CURL COOLDOWN] wait " + String((int)curlCooldown_) + "s\n";
                continue;
            }
            result = ToolCurl(line.Substring(6).Trimmed());
            curlCooldown_ = CURL_COOLDOWN;
        }
        else if (line.StartsWith("!absorb "))
            result = ToolAbsorb(line.Substring(8).Trimmed());
        else if (line.StartsWith("!remember"))
        {
            // Content may be inline ("!remember some text") or multi-line until !end
            String inline_content = line.Substring(9).Trimmed();
            String content;
            if (!inline_content.Empty())
                content = inline_content;
            while (++i < lines.Size())
            {
                if (lines[i].Trimmed() == "!end")
                    break;
                if (!content.Empty())
                    content += "\n";
                content += lines[i];
            }
            // Bare !remember (no content): fall back to the prompt that triggered inference
            if (content.Trimmed().Empty() && !lastUserPrompt_.Empty())
                content = lastUserPrompt_;
            result = ToolRemember(content);
        }
        else if (line.StartsWith("!write "))
        {
            String path = line.Substring(7).Trimmed();
            String content;
            while (++i < lines.Size())
            {
                if (lines[i].Trimmed() == "!end")
                    break;
                if (!content.Empty())
                    content += "\n";
                content += lines[i];
            }
            result = ToolWriteFile(path, content);
        }

        if (!result.Empty())
        {
            if (result.Length() > TOOL_OUTPUT_CAP)
                result = result.Substring(0, TOOL_OUTPUT_CAP) + "\n[TRUNCATED at " + String(TOOL_OUTPUT_CAP) + " bytes]";

            results += "[" + line + "]\n" + result + "\n";
            toolContext_ += result + "\n";
        }

        ++toolCallsThisCycle_;
        toolCooldown_ = TOOL_COOLDOWN;
    }
    return results;
}

// ── Individual tools ──

String WorkboardLLM::ToolReadFile(const String& path, unsigned offset, unsigned limit)
{
    auto* fs = context_->GetSubsystem<FileSystem>();
    String fullPath = ResolvePath(path);

    if (!fs->FileExists(fullPath))
        return "ERROR: File not found: " + path;

    File file(context_, fullPath, FILE_READ);
    if (!file.IsOpen())
        return "ERROR: Cannot open: " + path;

    unsigned fileSize = file.GetSize();

    if (offset >= fileSize)
        return "EOF (offset " + String(offset) + " >= file size " + String(fileSize) + ")";

    if (offset > 0)
        file.Seek(offset);

    unsigned remaining = fileSize - offset;
    unsigned readSize = (limit > 0) ? Min(limit, remaining) : Min(remaining, 16384u);

    Vector<char> buf(readSize + 1);
    file.Read(buf.Buffer(), readSize);
    buf[readSize] = 0;

    String header = "[" + String(readSize) + " bytes from offset " + String(offset)
                  + ", file size " + String(fileSize) + "]\n";
    return header + String(buf.Buffer(), readSize);
}

String WorkboardLLM::ToolWriteFile(const String& path, const String& content)
{
    auto* fs = context_->GetSubsystem<FileSystem>();
    String fullPath = ResolvePath(path);

    String parentDir = GetPath(fullPath);
    if (!parentDir.Empty() && !fs->DirExists(parentDir))
        fs->CreateDir(parentDir);

    File file(context_, fullPath, FILE_WRITE);
    if (!file.IsOpen())
        return "ERROR: Cannot write: " + path;

    file.Write(content.CString(), content.Length());
    return "OK: Wrote " + String(content.Length()) + " bytes to " + path;
}

String WorkboardLLM::ToolListDir(const String& path)
{
    auto* fs = context_->GetSubsystem<FileSystem>();
    String fullPath = ResolvePath(path);

    if (!fs->DirExists(fullPath))
        return "ERROR: Directory not found: " + path;

    Vector<String> files;
    fs->ScanDir(files, fullPath, "*", SCAN_FILES | SCAN_DIRS, false);

    String result;
    for (unsigned i = 0; i < files.Size() && i < 100; ++i)
    {
        if (files[i] == "." || files[i] == "..")
            continue;
        result += files[i] + "\n";
    }
    return result;
}

String WorkboardLLM::ToolWorkboard(const String& command)
{
    String fullCmd = projectRoot_ + ".claude/hooks/claude_ipc.sh " + command;
    String result;
#ifndef _WIN32
    FILE* pipe = popen(fullCmd.CString(), "r");
    if (pipe)
    {
        char buf[256];
        while (fgets(buf, sizeof(buf), pipe))
            result.Append(buf, (unsigned)strlen(buf));
        pclose(pipe);
    }
#endif
    return result.Empty() ? "OK: " + command : result;
}

String WorkboardLLM::ToolIPCSend(const String& target, const String& message)
{
#ifndef _WIN32
    String sockPath = ipcDir_ + "tty/" + target + ".sock";
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return "ERROR: socket creation failed";

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, sockPath.CString(), sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == 0)
    {
        if (write(fd, message.CString(), message.Length()) < 0)
            URHO3D_LOGERRORF("LLM: IPC write to %s failed", target.CString());
    }

    close(fd);
#else
    String pipeName = "\\\\.\\pipe\\urho_claude_" + target;
    HANDLE hPipe = CreateFileA(
        pipeName.CString(),
        GENERIC_WRITE,
        0, nullptr,
        OPEN_EXISTING,
        0, nullptr);
    if (hPipe != INVALID_HANDLE_VALUE)
    {
        DWORD written = 0;
        if (!WriteFile(hPipe, message.CString(), (DWORD)message.Length(), &written, nullptr))
            URHO3D_LOGERRORF("LLM: pipe write to %s failed", target.CString());
        CloseHandle(hPipe);
    }
    else
        return "ERROR: pipe connect failed for " + target;
#endif
    return "Sent to " + target;
}

String WorkboardLLM::ToolShell(const String& command)
{
    String result;
#ifndef _WIN32
    String fullCmd = "cd " + projectRoot_ + " && " + command + " 2>&1";
    FILE* pipe = popen(fullCmd.CString(), "r");
    if (pipe)
    {
        char buf[256];
        int lines = 0;
        while (fgets(buf, sizeof(buf), pipe) && lines < 200)
        {
            result.Append(buf, (unsigned)strlen(buf));
            ++lines;
        }
        pclose(pipe);
    }
#endif
    return result;
}

String WorkboardLLM::ToolSpawnCoder()
{
    String result;
#ifndef _WIN32
    String cmd = projectRoot_ + ".claude/hooks/claude_ipc.sh spawn-coder 2>&1";
    FILE* pipe = popen(cmd.CString(), "r");
    if (pipe)
    {
        char buf[256];
        while (fgets(buf, sizeof(buf), pipe))
            result.Append(buf, (unsigned)strlen(buf));
        pclose(pipe);
    }
#endif
    return result.Empty() ? "Coder spawn requested" : result;
}

String WorkboardLLM::ToolBuild(const String& target)
{
    String result;
#ifndef _WIN32
    String cmd = projectRoot_ + ".claude/hooks/safe_build.sh " + target + " 2>&1";
    FILE* pipe = popen(cmd.CString(), "r");
    if (pipe)
    {
        char buf[256];
        int lines = 0;
        while (fgets(buf, sizeof(buf), pipe) && lines < 50)
        {
            result.Append(buf, (unsigned)strlen(buf));
            ++lines;
        }
        pclose(pipe);
    }
#endif
    return result;
}

String WorkboardLLM::ToolCurl(const String& url)
{
    if (url.Empty())
        return "ERROR: no URL";
    if (!url.StartsWith("http://") && !url.StartsWith("https://"))
        return "ERROR: only http:// and https:// URLs";

    String rawHtml;
#ifndef _WIN32
    String cmd = "curl -sL --max-time 10 --max-filesize 8192 --no-sessionid " +
                 String("\"") + url + "\" 2>&1";
    FILE* pipe = popen(cmd.CString(), "r");
    if (pipe)
    {
        char buf[256];
        int bytes = 0;
        while (fgets(buf, sizeof(buf), pipe) && bytes < 8192)
        {
            unsigned len = (unsigned)strlen(buf);
            rawHtml.Append(buf, len);
            bytes += len;
        }
        pclose(pipe);
    }
#endif
    if (rawHtml.Empty())
        return "ERROR: no response";

    // Strip HTML tags, skip <script> and <style> blocks
    String text;
    text.Reserve(rawHtml.Length() / 3);
    bool inTag = false, inScript = false, inStyle = false;
    unsigned len = rawHtml.Length();
    for (unsigned i = 0; i < len; ++i)
    {
        char c = rawHtml[i];
        if (c == '<')
        {
            if (i + 7 < len && rawHtml.Substring(i, 7).ToLower() == "<script")
                inScript = true;
            if (i + 6 < len && rawHtml.Substring(i, 6).ToLower() == "<style")
                inStyle = true;
            if (i + 9 < len && rawHtml.Substring(i, 9).ToLower() == "</script>")
            { inScript = false; i += 8; continue; }
            if (i + 8 < len && rawHtml.Substring(i, 8).ToLower() == "</style>")
            { inStyle = false; i += 7; continue; }
            inTag = true;
            continue;
        }
        if (c == '>')
        {
            inTag = false;
            if (!inScript && !inStyle) text += ' ';
            continue;
        }
        if (!inTag && !inScript && !inStyle)
            text += c;
    }

    text.Replace("&amp;", "&");
    text.Replace("&lt;", "<");
    text.Replace("&gt;", ">");
    text.Replace("&quot;", "\"");
    text.Replace("&nbsp;", " ");
    text.Replace("&#39;", "'");

    // Collapse whitespace
    String clean;
    clean.Reserve(text.Length());
    bool lastWasSpace = false;
    for (unsigned j = 0; j < text.Length(); ++j)
    {
        char ch = text[j];
        bool isSpace = (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n');
        if (isSpace)
        {
            if (!lastWasSpace && !clean.Empty()) { clean += ' '; lastWasSpace = true; }
        }
        else { clean += ch; lastWasSpace = false; }
    }

    if (clean.Length() > 1024)
        clean = clean.Substring(0, 1024) + "\n[TRUNCATED at 1KB]";

    return clean.Empty() ? "ERROR: no usable text content" : clean;
}

// =============================================================================
// Study mode — C++-driven chunked file ingestion
// =============================================================================

String WorkboardLLM::ToolAbsorb(const String& path)
{
    auto* fs = context_->GetSubsystem<FileSystem>();
    String fullPath = ResolvePath(path);

    if (!fs->FileExists(fullPath))
        return "ERROR: File not found: " + path;

    File inFile(context_, fullPath, FILE_READ);
    if (!inFile.IsOpen())
        return "ERROR: Cannot open: " + path;

    unsigned fileSize = inFile.GetSize();
    if (fileSize == 0)
        return "ERROR: File is empty: " + path;

    if (!memoryDB_ || !memoryDB_->IsOpen())
        return "ERROR: Memory database not available";

    Vector<char> buf(fileSize + 1);
    inFile.Read(buf.Buffer(), fileSize);
    buf[fileSize] = 0;
    String content(buf.Buffer(), fileSize);

    // Chunk the file and store each chunk in SQL for training
    unsigned offset = 0;
    unsigned chunkSize = 2048;
    unsigned chunks = 0;

    while (offset < fileSize)
    {
        unsigned remaining = fileSize - offset;
        unsigned thisChunk = (remaining < chunkSize) ? remaining : chunkSize;
        String chunk = content.Substring(offset, thisChunk);
        offset += thisChunk;

        memoryDB_->Remember(chunk, "absorb:" + path);
        ++chunks;
    }

    // Also inject into session recall
    learnedFacts_.Push("[Absorbed " + path + "]");

    URHO3D_LOGINFOF("LLM: Absorbed %s — %u bytes, %u chunks → SQL", path.CString(), fileSize, chunks);
    return "Absorbed " + path + " (" + String(fileSize) + " bytes, " + String(chunks) + " chunks) → SQL memory";
}

String WorkboardLLM::ToolRemember(const String& content)
{
    if (content.Trimmed().Empty())
        return "ERROR: nothing to remember";

    String text = content;
    if (text.Length() > REMEMBER_CAP)
    {
        text = text.Substring(0, REMEMBER_CAP);
        URHO3D_LOGWARNINGF("LLM: Remember text truncated to %u bytes", REMEMBER_CAP);
    }

    // Persist to SQL so facts survive restarts and feed the Learn button
    if (memoryDB_ && memoryDB_->IsOpen())
        memoryDB_->Remember(text, "Yuki");

    // Session recall via prompt injection
    learnedFacts_.Push(text);
    URHO3D_LOGINFOF("LLM: Remembered %u bytes (%u facts total)", text.Length(), learnedFacts_.Size());
    return "Remembered " + String(text.Length()) + " bytes";
}

void WorkboardLLM::ExtractAndSaveTrainingPairs(const String& response)
{
    // Extract JSONL Q&A pairs and persist to SQL for training.
    Vector<String> lines = response.Split('\n');
    unsigned count = 0;

    for (unsigned i = 0; i < lines.Size(); ++i)
    {
        String line = lines[i].Trimmed();
        if (!line.StartsWith("{") || !line.Contains("messages"))
            continue;

        // Minimal JSON parse: extract "content" values from the messages array
        // Format: {"messages": [{"role": "user", "content": "Q"}, {"role": "assistant", "content": "A"}]}
        Vector<String> roles;
        Vector<String> contents;

        unsigned pos = 0;
        while (pos < line.Length())
        {
            unsigned roleKey = line.Find("\"role\"", pos);
            if (roleKey == String::NPOS) break;
            unsigned roleValStart = line.Find("\"", roleKey + 7);
            if (roleValStart == String::NPOS) break;
            ++roleValStart;
            unsigned roleValEnd = line.Find("\"", roleValStart);
            if (roleValEnd == String::NPOS) break;
            String role = line.Substring(roleValStart, roleValEnd - roleValStart);

            unsigned contentKey = line.Find("\"content\"", roleValEnd);
            if (contentKey == String::NPOS) break;
            unsigned contentValStart = line.Find("\"", contentKey + 10);
            if (contentValStart == String::NPOS) break;
            ++contentValStart;

            String content;
            unsigned j = contentValStart;
            while (j < line.Length())
            {
                if (line[j] == '\\' && j + 1 < line.Length())
                {
                    char next = line[j + 1];
                    if (next == '"') content += '"';
                    else if (next == 'n') content += '\n';
                    else if (next == 'r') content += '\r';
                    else if (next == 't') content += '\t';
                    else if (next == '\\') content += '\\';
                    else { content += '\\'; content += next; }
                    j += 2;
                }
                else if (line[j] == '"')
                    break;
                else
                {
                    content += line[j];
                    ++j;
                }
            }

            roles.Push(role);
            contents.Push(content);
            pos = j + 1;
        }

        // Save Q&A pair to SQL as a combined fact
        if (roles.Size() >= 2 && memoryDB_ && memoryDB_->IsOpen())
        {
            String qa;
            for (unsigned k = 0; k < roles.Size(); ++k)
            {
                if (k > 0) qa += "\n";
                qa += roles[k] + ": " + contents[k];
            }
            memoryDB_->Remember(qa, "Yuki-QA");
            ++count;
        }
    }

    if (count > 0)
        URHO3D_LOGINFOF("LLM: Remember — saved %u training pairs to SQL", count);

    rememberInFlight_ = false;
}

// =============================================================================
// Observation logging
// =============================================================================

void WorkboardLLM::LogObservation(const String& /*userPrompt*/, const String& /*response*/)
{
    // Legacy: used to append every conversation to absorbed.txt for training.
    // SQL memory (YukiMemoryDB) is now the training source — explicit !remember
    // and large-text routing write there. Auto-logging all conversations would
    // teach the model to parrot its own outputs. Removed.
}

// =============================================================================
// CPU usage sampling
// =============================================================================

static unsigned long long ReadThreadCpuTicks(int tid)
{
    char path[64];
    snprintf(path, sizeof(path), "/proc/self/task/%d/stat", tid);
    FILE* f = fopen(path, "r");
    if (!f)
        return 0;

    char buf[1024];
    if (!fgets(buf, sizeof(buf), f))
    {
        fclose(f);
        return 0;
    }
    fclose(f);

    // Skip past comm field (may contain parens/spaces)
    const char* p = strrchr(buf, ')');
    if (!p)
        return 0;
    p += 2;

    // Skip fields 3-13 to reach utime(14) and stime(15)
    int fieldIndex = 3;
    while (*p && fieldIndex < 14)
    {
        while (*p == ' ') ++p;
        while (*p && *p != ' ') ++p;
        ++fieldIndex;
    }
    while (*p == ' ') ++p;
    unsigned long long utime = strtoull(p, nullptr, 10);
    while (*p && *p != ' ') ++p;
    while (*p == ' ') ++p;
    unsigned long long stime = strtoull(p, nullptr, 10);

    return utime + stime;
}

void WorkboardLLM::SampleCpuUsage()
{
#ifdef __linux__
    unsigned long long cpuTime = 0;

    // Sum CPU ticks from Yuki's specific threads
    if (inferenceThreadTid_ > 0)
        cpuTime += ReadThreadCpuTicks(inferenceThreadTid_);
    if (loadThreadTid_ > 0 && loadThreadTid_ != inferenceThreadTid_)
        cpuTime += ReadThreadCpuTicks(loadThreadTid_);

    // Wall time in clock ticks
    long clkTck = sysconf(_SC_CLK_TCK);
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    unsigned long long wallTicks = (unsigned long long)ts.tv_sec * clkTck
                                 + (unsigned long long)(ts.tv_nsec / (1000000000LL / clkTck));

    if (prevSampleWallTime_ > 0 && cpuTime >= prevYukiCpuTime_)
    {
        unsigned long long dCpu = cpuTime - prevYukiCpuTime_;
        unsigned long long dWall = wallTicks - prevSampleWallTime_;
        if (dWall > 0)
            yukiCpuPct_ = 100.0f * (float)dCpu / (float)dWall;
    }

    prevYukiCpuTime_ = cpuTime;
    prevSampleWallTime_ = wallTicks;
#endif
}

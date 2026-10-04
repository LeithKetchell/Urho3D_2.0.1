// Claudia v0.3.0 -- Anthropic API client built on Urho3D.
// Permission layer: every state-changing tool call is shown to the local user
// before execution. User grants (Once / Session / Always) are recorded; always-
// grants persist to disk. The AI receives no special treatment -- it submits a
// call and gets either a result or "BLOCKED".

#include "Claudia.h"

#include <Urho3D/Core/CoreEvents.h>
#include <Urho3D/Engine/EngineDefs.h>
#include <Urho3D/Graphics/Graphics.h>
#include <Urho3D/IO/File.h>
#include <Urho3D/IO/FileSystem.h>
#include <Urho3D/IO/Log.h>
#include <Urho3D/Input/Input.h>
#include <Urho3D/Input/InputEvents.h>
#include <Urho3D/Network/Network.h>
#include <Urho3D/Resource/JSONFile.h>
#include <Urho3D/Resource/ResourceCache.h>
#include <Urho3D/Resource/XMLFile.h>
#include <Urho3D/UI/Font.h>
#include <Urho3D/UI/UI.h>
#include <Urho3D/UI/UIEvents.h>

#include <cstdio>
#include <cstdlib>
#include <regex>

URHO3D_DEFINE_APPLICATION_MAIN(Claudia)

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

Claudia::Claudia(Context* context) : Application(context) {}

void Claudia::Setup()
{
    engineParameters_[EP_WINDOW_TITLE]          = "Claudia " CLAUDIA_VERSION;
    engineParameters_[EP_WINDOW_WIDTH]          = 1100;
    engineParameters_[EP_WINDOW_HEIGHT]         = 760;
    engineParameters_[EP_FULL_SCREEN]           = false;
    engineParameters_[EP_LOG_NAME]              = "claudia.log";
    engineParameters_[EP_RESOURCE_PREFIX_PATHS] = ";..;../..";

    if (const char* k = getenv("ANTHROPIC_API_KEY"))
        apiKey_ = k;

    const auto& args = GetArguments();
    for (unsigned i = 0; i < args.Size(); ++i)
    {
        if      (args[i] == "--model"  && i + 1 < args.Size()) model_     = args[++i];
        else if (args[i] == "--cwd"    && i + 1 < args.Size()) cwd_       = args[++i];
        else if (args[i] == "--tokens" && i + 1 < args.Size()) maxTokens_ = ToI32(args[++i]);
    }

    if (cwd_.Empty())
    {
        FileSystem fs(context_);
        cwd_ = fs.GetCurrentDir();
    }
    while (cwd_.Length() > 1 && (cwd_.Back() == '/' || cwd_.Back() == '\\'))
        cwd_.Erase(cwd_.Length() - 1);
}

void Claudia::Start()
{
    auto* cache = GetSubsystem<ResourceCache>();
    auto* ui    = GetSubsystem<UI>();

    auto* style = cache->GetResource<XMLFile>("UI/DefaultStyle.xml");
    if (style) ui->GetRoot()->SetDefaultStyle(style);

    font_ = cache->GetResource<Font>("Fonts/Anonymous Pro.ttf");
    if (!font_) font_ = cache->GetResource<Font>("Fonts/DejaVuSansMono.ttf");

    CreateUI();
    LoadGrants();

    SubscribeToEvent(E_UPDATE,  URHO3D_HANDLER(Claudia, HandleUpdate));
    SubscribeToEvent(E_KEYDOWN, URHO3D_HANDLER(Claudia, HandleKeyDown));

    AppendSystem("Claudia " CLAUDIA_VERSION " | " + model_);
    AppendSystem("CWD: " + cwd_);
    if (apiKey_.Empty())
        AppendSystem("WARNING: ANTHROPIC_API_KEY not set -- API calls will fail.");

    unsigned always = 0;
    for (const Grant& g : grants_) if (g.scope == GRANT_ALWAYS) ++always;
    if (always > 0)
        AppendSystem(String(always) + " always-grant(s) loaded from disk.");
}

void Claudia::Stop() { pendingRequest_.Reset(); }

// ─────────────────────────────────────────────────────────────────────────────
// UI construction
// ─────────────────────────────────────────────────────────────────────────────

void Claudia::CreateUI()
{
    auto* ui   = GetSubsystem<UI>();
    auto* root = ui->GetRoot();
    auto* gfx  = GetSubsystem<Graphics>();

    const int W        = gfx->GetWidth();
    const int H        = gfx->GetHeight();
    const int STATUS_H = 22;
    const int INPUT_H  = 34;
    const int CHAT_H   = H - STATUS_H - INPUT_H - 6;

    statusText_ = root->CreateChild<Text>();
    statusText_->SetFont(font_, FONT_SIZE);
    statusText_->SetPosition(4, 3);
    statusText_->SetSize(W - 8, STATUS_H);
    statusText_->SetColor(Color(0.55f, 0.85f, 0.55f));
    statusText_->SetText("Ready");

    chatScroll_ = root->CreateChild<ScrollView>();
    chatScroll_->SetPosition(0, STATUS_H + 2);
    chatScroll_->SetSize(W, CHAT_H);
    chatScroll_->SetStyleAuto();

    chatContent_ = new UIElement(context_);
    chatContent_->SetLayout(LM_VERTICAL, 3);
    chatContent_->SetMinWidth(W - 20);
    chatContent_->SetHeight(0);
    chatScroll_->SetContentElement(chatContent_);

    inputEdit_ = root->CreateChild<LineEdit>();
    inputEdit_->SetPosition(4, H - INPUT_H - 2);
    inputEdit_->SetSize(W - 100, INPUT_H);
    inputEdit_->SetStyleAuto();
    if (font_) inputEdit_->GetTextElement()->SetFont(font_, FONT_SIZE);

    sendBtn_ = root->CreateChild<Button>();
    sendBtn_->SetPosition(W - 94, H - INPUT_H - 2);
    sendBtn_->SetSize(90, INPUT_H);
    sendBtn_->SetStyleAuto();

    auto* label = sendBtn_->CreateChild<Text>();
    label->SetFont(font_, FONT_SIZE);
    label->SetText("Send");
    label->SetAlignment(HA_CENTER, VA_CENTER);

    SubscribeToEvent(sendBtn_, E_RELEASED, URHO3D_HANDLER(Claudia, HandleSendClicked));
}

// ─────────────────────────────────────────────────────────────────────────────
// Message display
// ─────────────────────────────────────────────────────────────────────────────

void Claudia::AppendMessage(const String& role, const String& text)
{
    auto* elem = chatContent_->CreateChild<Text>();
    elem->SetFont(font_, FONT_SIZE);
    elem->SetWordwrap(true);
    elem->SetMinWidth(chatContent_->GetWidth() - 10);
    if (role == "user")
    {
        elem->SetColor(Color(0.88f, 0.88f, 1.0f));
        elem->SetText("You: " + text);
    }
    else
    {
        elem->SetColor(Color(0.65f, 1.0f, 0.65f));
        elem->SetText("Claudia: " + text);
    }
    ScrollToBottom();
}

void Claudia::AppendSystem(const String& text)
{
    auto* elem = chatContent_->CreateChild<Text>();
    elem->SetFont(font_, FONT_SIZE);
    elem->SetWordwrap(true);
    elem->SetMinWidth(chatContent_->GetWidth() - 10);
    elem->SetColor(Color(0.45f, 0.45f, 0.55f));
    elem->SetText("  [" + text + "]");
    ScrollToBottom();
}

void Claudia::AppendStreamDelta(const String& delta)
{
    streamText_ += delta;
    if (!streamElem_)
    {
        streamElem_ = chatContent_->CreateChild<Text>();
        streamElem_->SetFont(font_, FONT_SIZE);
        streamElem_->SetWordwrap(true);
        streamElem_->SetMinWidth(chatContent_->GetWidth() - 10);
        streamElem_->SetColor(Color(0.65f, 1.0f, 0.65f));
    }
    streamElem_->SetText("Claudia: " + streamText_);
    ScrollToBottom();
}

void Claudia::FinaliseStream()
{
    streamElem_ = nullptr;
    if (!streamText_.Empty())
    {
        history_.Push({"assistant", streamText_});
        streamText_.Clear();
    }
}

void Claudia::SetBusy(bool busy)
{
    busy_ = busy;
    sendBtn_->SetEnabled(!busy);
    inputEdit_->SetEnabled(!busy);
    statusText_->SetText(busy ? String("Thinking... | ") + model_
                               : String("Ready | ") + model_);
}

void Claudia::ScrollToBottom()
{
    chatScroll_->SetViewPosition(IntVector2(0, chatContent_->GetHeight()));
}

// ─────────────────────────────────────────────────────────────────────────────
// Input handling
// ─────────────────────────────────────────────────────────────────────────────

void Claudia::Submit(const String& text)
{
    if (text.Empty() || busy_) return;
    history_.Push({"user", text});
    AppendMessage("user", text);
    inputEdit_->SetText(String::EMPTY);
    SetBusy(true);
    SendToAPI();
}

void Claudia::HandleSendClicked(StringHash, VariantMap&)
{
    Submit(inputEdit_->GetText().Trimmed());
}

void Claudia::HandleKeyDown(StringHash, VariantMap& eventData)
{
    using namespace KeyDown;
    int key = eventData[P_KEY].GetI32();
    if ((key == KEY_RETURN || key == KEY_KP_ENTER) && inputEdit_->HasFocus())
        Submit(inputEdit_->GetText().Trimmed());
}

// ─────────────────────────────────────────────────────────────────────────────
// Update: drain streaming bytes
// ─────────────────────────────────────────────────────────────────────────────

void Claudia::HandleUpdate(StringHash, VariantMap&)
{
    if (!pendingRequest_) return;

    unsigned avail = pendingRequest_->GetAvailableSize();
    if (avail > 0)
    {
        Vector<unsigned char> buf;
        buf.Resize((i32)avail);
        pendingRequest_->Read(buf.Buffer(), avail);
        FeedBytes((const char*)buf.Buffer(), avail);
    }

    HttpRequestState state = pendingRequest_->GetState();
    if (state == HTTP_CLOSED || state == HTTP_ERROR)
    {
        avail = pendingRequest_->GetAvailableSize();
        if (avail > 0)
        {
            Vector<unsigned char> buf;
            buf.Resize((i32)avail);
            pendingRequest_->Read(buf.Buffer(), avail);
            FeedBytes((const char*)buf.Buffer(), avail);
        }
        if (state == HTTP_ERROR)
            AppendSystem("API error: " + pendingRequest_->GetError());
        pendingRequest_.Reset();
        OnAPIFinished();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Anthropic API
// ─────────────────────────────────────────────────────────────────────────────

void Claudia::SendToAPI()
{
    sseBuf_.Clear();
    currentEventType_.Clear();
    currentEventData_.Clear();
    streamText_.Clear();
    streamElem_          = nullptr;
    pendingTools_.Clear();
    currentToolIdx_      = 0;
    pendingToolResults_.Clear();
    currentToolId_.Clear();
    currentToolName_.Clear();
    currentToolInputBuf_.Clear();
    inToolInput_         = false;
    messageStopReceived_ = false;
    awaitingPermission_  = false;

    String body = BuildRequestBody();

    Vector<String> headers;
    headers.Push("x-api-key: " + apiKey_);
    headers.Push("anthropic-version: 2023-06-01");
    headers.Push("content-type: application/json");
    headers.Push("Accept: text/event-stream");

    pendingRequest_ = GetSubsystem<Network>()->MakeHttpRequest(
        "https://api.anthropic.com/v1/messages", "POST", headers, body);
}

String Claudia::BuildRequestBody() const
{
    static const char* TOOLS =
        "["
        "{"
          "\"name\":\"read_file\","
          "\"description\":\"Read a file. Provide start_line/end_line (1-based, end_line -1=EOF) to read a window.\","
          "\"input_schema\":{"
            "\"type\":\"object\","
            "\"properties\":{"
              "\"path\":{\"type\":\"string\"},"
              "\"start_line\":{\"type\":\"integer\"},"
              "\"end_line\":{\"type\":\"integer\"}"
            "},"
            "\"required\":[\"path\"]"
          "}"
        "},"
        "{"
          "\"name\":\"write_file\","
          "\"description\":\"Create or overwrite a complete file. Use edit_file for targeted changes.\","
          "\"input_schema\":{"
            "\"type\":\"object\","
            "\"properties\":{"
              "\"path\":{\"type\":\"string\"},"
              "\"content\":{\"type\":\"string\"}"
            "},"
            "\"required\":[\"path\",\"content\"]"
          "}"
        "},"
        "{"
          "\"name\":\"edit_file\","
          "\"description\":\"Replace an exact string in a file. old_string must match byte-for-byte. "
              "Read the file first if you have not seen it this session.\","
          "\"input_schema\":{"
            "\"type\":\"object\","
            "\"properties\":{"
              "\"path\":{\"type\":\"string\"},"
              "\"old_string\":{\"type\":\"string\"},"
              "\"new_string\":{\"type\":\"string\"},"
              "\"replace_all\":{\"type\":\"boolean\"}"
            "},"
            "\"required\":[\"path\",\"old_string\",\"new_string\"]"
          "}"
        "},"
        "{"
          "\"name\":\"grep\","
          "\"description\":\"Search file contents by regular expression.\","
          "\"input_schema\":{"
            "\"type\":\"object\","
            "\"properties\":{"
              "\"pattern\":{\"type\":\"string\"},"
              "\"path\":{\"type\":\"string\"},"
              "\"file_pattern\":{\"type\":\"string\"},"
              "\"ignore_case\":{\"type\":\"boolean\"},"
              "\"context\":{\"type\":\"integer\"}"
            "},"
            "\"required\":[\"pattern\"]"
          "}"
        "},"
        "{"
          "\"name\":\"glob\","
          "\"description\":\"Find files by path pattern. Use * within a level and ** across levels.\","
          "\"input_schema\":{"
            "\"type\":\"object\","
            "\"properties\":{"
              "\"pattern\":{\"type\":\"string\"}"
            "},"
            "\"required\":[\"pattern\"]"
          "}"
        "},"
        "{"
          "\"name\":\"stat\","
          "\"description\":\"Report whether a path exists: type, size, line count.\","
          "\"input_schema\":{"
            "\"type\":\"object\","
            "\"properties\":{"
              "\"path\":{\"type\":\"string\"}"
            "},"
            "\"required\":[\"path\"]"
          "}"
        "},"
        "{"
          "\"name\":\"list_dir\","
          "\"description\":\"List directory contents.\","
          "\"input_schema\":{"
            "\"type\":\"object\","
            "\"properties\":{"
              "\"path\":{\"type\":\"string\"},"
              "\"recursive\":{\"type\":\"boolean\"}"
            "},"
            "\"required\":[\"path\"]"
          "}"
        "},"
        "{"
          "\"name\":\"shell\","
          "\"description\":\"Run a shell command. Subject to local security policy and user approval.\","
          "\"input_schema\":{"
            "\"type\":\"object\","
            "\"properties\":{"
              "\"command\":{\"type\":\"string\"}"
            "},"
            "\"required\":[\"command\"]"
          "}"
        "},"
        "{"
          "\"name\":\"git_status\","
          "\"description\":\"Show which files are modified, staged, or untracked.\","
          "\"input_schema\":{\"type\":\"object\",\"properties\":{}}"
        "},"
        "{"
          "\"name\":\"git_diff\","
          "\"description\":\"Show unstaged changes.\","
          "\"input_schema\":{"
            "\"type\":\"object\","
            "\"properties\":{"
              "\"path\":{\"type\":\"string\"}"
            "},"
            "\"required\":[]"
          "}"
        "},"
        "{"
          "\"name\":\"git_log\","
          "\"description\":\"Show recent commits.\","
          "\"input_schema\":{"
            "\"type\":\"object\","
            "\"properties\":{"
              "\"count\":{\"type\":\"integer\"}"
            "},"
            "\"required\":[]"
          "}"
        "},"
        "{"
          "\"name\":\"git_add\","
          "\"description\":\"Stage a path for the next commit.\","
          "\"input_schema\":{"
            "\"type\":\"object\","
            "\"properties\":{"
              "\"path\":{\"type\":\"string\"}"
            "},"
            "\"required\":[\"path\"]"
          "}"
        "},"
        "{"
          "\"name\":\"git_commit\","
          "\"description\":\"Commit staged changes.\","
          "\"input_schema\":{"
            "\"type\":\"object\","
            "\"properties\":{"
              "\"message\":{\"type\":\"string\"}"
            "},"
            "\"required\":[\"message\"]"
          "}"
        "}"
        "]";

    String msgs = "[";
    bool first = true;
    for (const ConvTurn& t : history_)
    {
        if (!first) msgs += ",";
        first = false;
        if (t.role == "user")
            msgs += "{\"role\":\"user\",\"content\":" + QuoteJson(t.content) + "}";
        else if (t.role == "assistant")
            msgs += "{\"role\":\"assistant\",\"content\":" + QuoteJson(t.content) + "}";
        else if (t.role == "assistant_raw")
            msgs += "{\"role\":\"assistant\",\"content\":" + t.content + "}";
        else if (t.role == "user_raw")
            msgs += "{\"role\":\"user\",\"content\":" + t.content + "}";
    }
    msgs += "]";

    String sys = "You are Claudia, an AI coding assistant. "
        "Working directory: " + cwd_ + ". "
        "All paths are relative to this directory. Never access paths above it. "
        "Read a file before editing it if you haven't already seen it this session. "
        "Prefer edit_file over write_file for files that exist. "
        "State-changing operations (write, edit, shell, git commit/add) require the "
        "local user's approval before they execute -- the user sees a dialog and may deny.";

    String body = "{";
    body += "\"model\":"      + QuoteJson(model_)  + ",";
    body += "\"max_tokens\":" + String(maxTokens_) + ",";
    body += "\"stream\":true,";
    body += "\"system\":"     + QuoteJson(sys)     + ",";
    body += "\"tools\":"      + String(TOOLS)      + ",";
    body += "\"messages\":"   + msgs;
    body += "}";
    return body;
}

// ─────────────────────────────────────────────────────────────────────────────
// SSE parser
// ─────────────────────────────────────────────────────────────────────────────

void Claudia::FeedBytes(const char* buf, unsigned len)
{
    sseBuf_.Append(buf, len);
    while (true)
    {
        unsigned nl = sseBuf_.Find('\n');
        if (nl == String::NPOS) break;
        String line = sseBuf_.Substring(0, nl);
        sseBuf_ = sseBuf_.Substring(nl + 1);
        if (!line.Empty() && line.Back() == '\r')
            line = line.Substring(0, line.Length() - 1);
        if (line.Empty())
        {
            if (!currentEventData_.Empty())
            {
                OnSSEEvent(currentEventType_, currentEventData_);
                currentEventType_.Clear();
                currentEventData_.Clear();
            }
        }
        else if (line.StartsWith("event: ")) currentEventType_ = line.Substring(7);
        else if (line.StartsWith("data: "))  currentEventData_ = line.Substring(6);
    }
}

void Claudia::OnSSEEvent(const String& /*event*/, const String& data)
{
    if (data == "[DONE]") return;
    JSONFile jf(context_);
    if (!jf.FromString(data)) return;
    const JSONValue& root = jf.GetRoot();
    String type = root["type"].GetString();

    if (type == "content_block_start")
    {
        const JSONValue& cb = root["content_block"];
        if (cb["type"].GetString() == "tool_use")
        {
            inToolInput_         = true;
            currentToolId_       = cb["id"].GetString();
            currentToolName_     = cb["name"].GetString();
            currentToolInputBuf_.Clear();
        }
        else inToolInput_ = false;
    }
    else if (type == "content_block_delta")
    {
        const JSONValue& delta = root["delta"];
        String dt = delta["type"].GetString();
        if (dt == "text_delta")
        {
            String text = delta["text"].GetString();
            if (!text.Empty()) AppendStreamDelta(text);
        }
        else if (dt == "input_json_delta")
            currentToolInputBuf_ += delta["partial_json"].GetString();
    }
    else if (type == "content_block_stop")
    {
        if (inToolInput_)
        {
            pendingTools_.Push({currentToolId_, currentToolName_, currentToolInputBuf_});
            inToolInput_ = false;
            currentToolId_.Clear();
            currentToolName_.Clear();
            currentToolInputBuf_.Clear();
        }
    }
    else if (type == "message_stop")
        messageStopReceived_ = true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Tool pipeline -- sequential, one tool at a time
// ─────────────────────────────────────────────────────────────────────────────

void Claudia::OnAPIFinished()
{
    FinaliseStream();
    if (pendingTools_.Empty()) { SetBusy(false); return; }

    currentToolIdx_ = 0;
    pendingToolResults_.Clear();
    ProcessNextTool();
}

void Claudia::ProcessNextTool()
{
    if (currentToolIdx_ >= (int)pendingTools_.Size())
    {
        FinishToolRound();
        return;
    }

    const PendingTool& pt = pendingTools_[currentToolIdx_];
    PolicyDecision decision = CheckPolicy(pt.name, pt.inputJson);

    if (decision == POLICY_DENY)
    {
        AppendSystem("Blocked (policy): " + pt.name);
        pendingToolResults_.Push("[BLOCKED by policy]");
        ++currentToolIdx_;
        ProcessNextTool();
        return;
    }

    if (decision == POLICY_ALLOW)
    {
        AppendSystem("Tool: " + pt.name);
        pendingToolResults_.Push(ExecuteTool(pt.name, pt.inputJson));
        ++currentToolIdx_;
        ProcessNextTool();
        return;
    }

    // POLICY_ASK -- pause and show dialog; resumes in OnPermissionResponse
    permCurrentKey_ = OpKey(pt.name, pt.inputJson);
    ShowPermissionDialog(pt.name, pt.inputJson);
}

void Claudia::OnPermissionResponse(GrantScope scope)
{
    HidePermissionDialog();

    const PendingTool& pt = pendingTools_[currentToolIdx_];
    AddGrant(permCurrentKey_, scope);
    AppendSystem("Tool (allowed): " + pt.name);
    pendingToolResults_.Push(ExecuteTool(pt.name, pt.inputJson));

    ++currentToolIdx_;
    ProcessNextTool();
}

void Claudia::FinishToolRound()
{
    // Build assistant_raw content array (text block + tool_use blocks)
    String aContent = "[";
    bool first = true;
    if (!history_.Empty() && history_.Back().role == "assistant")
    {
        String txt = history_.Back().content;
        history_.Pop();
        if (!txt.Empty())
        {
            aContent += "{\"type\":\"text\",\"text\":" + QuoteJson(txt) + "}";
            first = false;
        }
    }
    for (const PendingTool& pt : pendingTools_)
    {
        if (!first) aContent += ",";
        first = false;
        aContent += "{\"type\":\"tool_use\","
            "\"id\":"    + QuoteJson(pt.id)   + ","
            "\"name\":"  + QuoteJson(pt.name) + ","
            "\"input\":" + (pt.inputJson.Empty() ? String("{}") : pt.inputJson) + "}";
    }
    aContent += "]";
    history_.Push({"assistant_raw", aContent});

    // Build user_raw tool_result array
    String uContent = "[";
    for (unsigned i = 0; i < pendingTools_.Size(); ++i)
    {
        if (i > 0) uContent += ",";
        uContent += "{\"type\":\"tool_result\","
            "\"tool_use_id\":" + QuoteJson(pendingTools_[i].id) + ","
            "\"content\":"     + QuoteJson(pendingToolResults_[i]) + "}";
    }
    uContent += "]";
    history_.Push({"user_raw", uContent});

    pendingTools_.Clear();
    pendingToolResults_.Clear();
    currentToolIdx_ = 0;
    SendToAPI();
}

// ─────────────────────────────────────────────────────────────────────────────
// Permission dialog
// ─────────────────────────────────────────────────────────────────────────────

void Claudia::ShowPermissionDialog(const String& tool, const String& inputJson)
{
    awaitingPermission_ = true;
    auto* ui   = GetSubsystem<UI>();
    auto* root = ui->GetRoot();
    auto* gfx  = GetSubsystem<Graphics>();

    const int W     = gfx->GetWidth();
    const int H     = gfx->GetHeight();
    const int PW    = 620;
    const int PH    = 230;
    const int PX    = (W - PW) / 2;
    const int PY    = (H - PH) / 2;
    const int BTN_W = 132;
    const int BTN_H = 32;

    permPanel_ = root->CreateChild<UIElement>();
    permPanel_->SetPosition(PX, PY);
    permPanel_->SetSize(PW, PH);
    permPanel_->SetColor(Color(0.10f, 0.10f, 0.16f, 0.97f));

    auto* title = permPanel_->CreateChild<Text>();
    title->SetFont(font_, FONT_SIZE + 1);
    title->SetPosition(10, 10);
    title->SetSize(PW - 20, 22);
    title->SetColor(Color(1.0f, 0.75f, 0.2f));
    title->SetText("Allow tool: " + tool + "  --  approve before it runs");

    String display = inputJson.Length() > 320 ? inputJson.Substring(0, 320) + " ..." : inputJson;
    auto* params = permPanel_->CreateChild<Text>();
    params->SetFont(font_, FONT_SIZE);
    params->SetPosition(10, 38);
    params->SetSize(PW - 20, 130);
    params->SetWordwrap(true);
    params->SetColor(Color(0.82f, 0.82f, 0.82f));
    params->SetText(display);

    const int BY = PH - BTN_H - 12;
    int bx = 10;

    auto makeBtn = [&](const String& lbl, Color col) -> Button*
    {
        auto* btn = permPanel_->CreateChild<Button>();
        btn->SetPosition(bx, BY);
        btn->SetSize(BTN_W, BTN_H);
        btn->SetColor(col);
        btn->SetStyleAuto();
        auto* t = btn->CreateChild<Text>();
        t->SetFont(font_, FONT_SIZE);
        t->SetText(lbl);
        t->SetAlignment(HA_CENTER, VA_CENTER);
        t->SetColor(Color::WHITE);
        bx += BTN_W + 6;
        return btn;
    };

    auto* denyBtn    = makeBtn("Deny",          Color(0.55f, 0.12f, 0.12f));
    auto* onceBtn    = makeBtn("Allow Once",    Color(0.15f, 0.35f, 0.55f));
    auto* sessionBtn = makeBtn("Allow Session", Color(0.15f, 0.42f, 0.32f));
    auto* alwaysBtn  = makeBtn("Always Allow",  Color(0.32f, 0.22f, 0.52f));

    SubscribeToEvent(denyBtn,    E_RELEASED, URHO3D_HANDLER(Claudia, HandlePermDeny));
    SubscribeToEvent(onceBtn,    E_RELEASED, URHO3D_HANDLER(Claudia, HandlePermOnce));
    SubscribeToEvent(sessionBtn, E_RELEASED, URHO3D_HANDLER(Claudia, HandlePermSession));
    SubscribeToEvent(alwaysBtn,  E_RELEASED, URHO3D_HANDLER(Claudia, HandlePermAlways));

    statusText_->SetText("Waiting for your approval...");
}

void Claudia::HidePermissionDialog()
{
    awaitingPermission_ = false;
    if (permPanel_) { permPanel_->Remove(); permPanel_ = nullptr; }
    statusText_->SetText(String("Thinking... | ") + model_);
}

void Claudia::HandlePermDeny(StringHash, VariantMap&)
{
    HidePermissionDialog();
    const PendingTool& pt = pendingTools_[currentToolIdx_];
    AppendSystem("Tool (denied by user): " + pt.name);
    pendingToolResults_.Push("[DENIED by user]");
    ++currentToolIdx_;
    ProcessNextTool();
}

void Claudia::HandlePermOnce(StringHash, VariantMap&)    { OnPermissionResponse(GRANT_ONCE); }
void Claudia::HandlePermSession(StringHash, VariantMap&) { OnPermissionResponse(GRANT_SESSION); }
void Claudia::HandlePermAlways(StringHash, VariantMap&)  { OnPermissionResponse(GRANT_ALWAYS); }

// ─────────────────────────────────────────────────────────────────────────────
// Tool dispatch
// ─────────────────────────────────────────────────────────────────────────────

String Claudia::ExecuteTool(const String& name, const String& inputJson)
{
    JSONFile jf(context_);
    JSONValue args;
    if (!inputJson.Empty() && jf.FromString(inputJson))
        args = jf.GetRoot();

    if (name == "read_file")
    {
        int sl = args["start_line"].IsNull() ? 0 : args["start_line"].GetI32();
        int el = args["end_line"].IsNull()   ? 0 : args["end_line"].GetI32();
        return ToolReadFile(args["path"].GetString(), sl, el);
    }
    if (name == "write_file")
        return ToolWriteFile(args["path"].GetString(), args["content"].GetString());
    if (name == "edit_file")
    {
        bool replAll = !args["replace_all"].IsNull() && args["replace_all"].GetBool();
        return ToolEditFile(args["path"].GetString(),
                            args["old_string"].GetString(),
                            args["new_string"].GetString(),
                            replAll);
    }
    if (name == "grep")
    {
        int ctx = args["context"].IsNull() ? 0 : args["context"].GetI32();
        bool ic = !args["ignore_case"].IsNull() && args["ignore_case"].GetBool();
        return ToolGrep(args["pattern"].GetString(),
                        args["path"].IsNull() ? String(".") : args["path"].GetString(),
                        args["file_pattern"].IsNull() ? String("") : args["file_pattern"].GetString(),
                        ic, ctx);
    }
    if (name == "glob")
        return ToolGlob(args["pattern"].GetString());
    if (name == "stat")
        return ToolStat(args["path"].GetString());
    if (name == "list_dir")
    {
        bool rec = !args["recursive"].IsNull() && args["recursive"].GetBool();
        return ToolListDir(args["path"].GetString(), rec);
    }
    if (name == "shell")
        return ToolShell(args["command"].GetString());

    if (name == "git_status")
        return ToolShell("git -C " + cwd_ + " status --short --branch");
    if (name == "git_diff")
    {
        String path = args["path"].IsNull() ? String("") : args["path"].GetString();
        String cmd = "git -C " + cwd_ + " diff";
        if (!path.Empty())
        {
            RuleResult r = CheckPath(path);
            if (!r.allowed) return "[BLOCKED: " + r.reason + "]";
            cmd += " -- " + ResolvePath(path);
        }
        return ToolShell(cmd);
    }
    if (name == "git_log")
    {
        int count = args["count"].IsNull() ? 20 : args["count"].GetI32();
        if (count < 1 || count > 200) count = 20;
        char buf[64]; sprintf(buf, "%d", count);
        return ToolShell("git -C " + cwd_ + " log --oneline --decorate -n " + String(buf));
    }
    if (name == "git_add")
    {
        String path = args["path"].GetString();
        RuleResult r = CheckPath(path);
        if (!r.allowed) return "[BLOCKED: " + r.reason + "]";
        return ToolShell("git -C " + cwd_ + " add -- " + ResolvePath(path));
    }
    if (name == "git_commit")
        return ToolShell("git -C " + cwd_ + " commit -m " + QuoteJson(args["message"].GetString()));

    return "[Unknown tool: " + name + "]";
}

// ─────────────────────────────────────────────────────────────────────────────
// File tools
// ─────────────────────────────────────────────────────────────────────────────

String Claudia::ToolReadFile(const String& path, int startLine, int endLine)
{
    RuleResult rule = CheckPath(path);
    if (!rule.allowed) return "[BLOCKED: " + rule.reason + "]";

    File f(context_, ResolvePath(path), FILE_READ);
    if (!f.IsOpen()) return "[Error: cannot open " + path + "]";

    String out;
    int lineNum = 0;
    while (!f.IsEof())
    {
        String line = f.ReadLine();
        ++lineNum;
        if (startLine > 0 && lineNum < startLine) continue;
        if (endLine  > 0 && lineNum > endLine)    break;
        out += String(lineNum) + ": " + line + "\n";
    }
    return out.Empty() ? "(empty)" : out;
}

String Claudia::ToolWriteFile(const String& path, const String& content)
{
    RuleResult rule = CheckPath(path);
    if (!rule.allowed) return "[BLOCKED: " + rule.reason + "]";

    String resolved = ResolvePath(path);
    auto*  fs       = GetSubsystem<FileSystem>();
    String dir      = GetPath(resolved);
    if (!fs->DirExists(dir)) fs->CreateDir(dir);

    File f(context_, resolved, FILE_WRITE);
    if (!f.IsOpen()) return "[Error: cannot write " + path + "]";
    f.Write(content.CString(), content.Length());
    return "Written " + String(content.Length()) + " bytes to " + path;
}

String Claudia::ToolEditFile(const String& path, const String& oldString,
                              const String& newString, bool replaceAll)
{
    RuleResult rule = CheckPath(path);
    if (!rule.allowed) return "[BLOCKED: " + rule.reason + "]";
    if (oldString.Empty()) return "[Error: old_string must not be empty]";

    String content;
    {
        File f(context_, ResolvePath(path), FILE_READ);
        if (!f.IsOpen()) return "[Error: cannot open " + path + "]";
        content = f.ReadString();
    }

    unsigned count = 0, pos = 0;
    while (true)
    {
        pos = content.Find(oldString, pos);
        if (pos == String::NPOS) break;
        ++count;
        pos += oldString.Length();
    }

    if (count == 0)
        return "[Error: old_string was not found in " + path +
               ". It must match exactly including whitespace and indentation. "
               "Read the file again and copy the text verbatim.]";

    if (count > 1 && !replaceAll)
        return "[Error: old_string appears " + String(count) + " times in " + path +
               ". Add surrounding lines to make it unique, or set replace_all to true.]";

    String out;
    unsigned replaced = 0;
    pos = 0;
    while (true)
    {
        unsigned found = content.Find(oldString, pos);
        if (found == String::NPOS) { out += content.Substring(pos); break; }
        out += content.Substring(pos, found - pos);
        out += newString;
        pos = found + oldString.Length();
        ++replaced;
        if (!replaceAll) { out += content.Substring(pos); break; }
    }

    File f(context_, ResolvePath(path), FILE_WRITE);
    if (!f.IsOpen()) return "[Error: cannot write " + path + "]";
    f.Write(out.CString(), out.Length());
    return "Edited " + path + ": replaced " + String(replaced) + " occurrence(s).";
}

String Claudia::ToolListDir(const String& path, bool recursive)
{
    RuleResult rule = CheckPath(path);
    if (!rule.allowed) return "[BLOCKED: " + rule.reason + "]";

    String resolved = ResolvePath(path);
    auto*  fs       = GetSubsystem<FileSystem>();

    Vector<String> files, dirs;
    fs->ScanDir(files, resolved, "*", SCAN_FILES, recursive);
    fs->ScanDir(dirs,  resolved, "*", SCAN_DIRS,  recursive);

    String out;
    for (const String& d : dirs)  out += "d " + d + "\n";
    for (const String& f : files) out += "f " + f + "\n";
    return out.Empty() ? "(empty)" : out;
}

// ─────────────────────────────────────────────────────────────────────────────
// Search tools
// ─────────────────────────────────────────────────────────────────────────────

static std::string GlobToRegex(const std::string& pat)
{
    std::string re;
    re.reserve(pat.size() * 2);
    for (std::size_t i = 0; i < pat.size(); ++i)
    {
        char c = pat[i];
        if (c == '*')
        {
            if (i + 1 < pat.size() && pat[i + 1] == '*')
            {
                if (i + 2 < pat.size() && pat[i + 2] == '/') { re += "(?:.*/)?"; i += 2; }
                else { re += ".*"; ++i; }
            }
            else re += "[^/]*";
        }
        else if (c == '?') re += "[^/]";
        else if (std::string(".^$+()[]{}|\\").find(c) != std::string::npos)
            { re += '\\'; re += c; }
        else re += c;
    }
    return re;
}

String Claudia::ToolGrep(const String& pattern, const String& searchPath,
                          const String& filePattern, bool ignoreCase, int context)
{
    if (pattern.Empty()) return "[Error: pattern is required]";

    RuleResult rule = CheckPath(searchPath);
    if (!rule.allowed) return "[BLOCKED: " + rule.reason + "]";

    std::regex contentRe;
    try
    {
        auto flags = std::regex::ECMAScript | std::regex::optimize;
        if (ignoreCase) flags |= std::regex::icase;
        contentRe = std::regex(pattern.CString(), flags);
    }
    catch (const std::regex_error& e)
    { return String("[Error: invalid regex: ") + e.what() + "]"; }

    std::regex fileRe;
    bool hasFilePattern = !filePattern.Empty();
    if (hasFilePattern)
    {
        try { fileRe = std::regex(GlobToRegex(filePattern.CString())); }
        catch (...) { return "[Error: invalid file_pattern]"; }
    }

    context = (context < 0) ? 0 : (context > 5 ? 5 : context);

    String resolved = ResolvePath(searchPath);
    auto*  fs       = GetSubsystem<FileSystem>();

    Vector<String> files;
    if (fs->FileExists(resolved))
    {
        files.Push(resolved);
    }
    else
    {
        Vector<String> found;
        fs->ScanDir(found, resolved, "*", SCAN_FILES, true);
        for (const String& rel : found)
        {
            if (hasFilePattern && !std::regex_match(rel.CString(), fileRe)) continue;
            files.Push(resolved + "/" + rel);
        }
    }

    String out;
    unsigned totalHits = 0, filesHit = 0;

    for (const String& filePath : files)
    {
        if (totalHits >= 200) break;

        File f(context_, filePath, FILE_READ);
        if (!f.IsOpen()) continue;
        String content = f.ReadString();

        unsigned probe = content.Length() < 8192 ? content.Length() : 8192;
        bool binary = false;
        for (unsigned i = 0; i < probe; ++i)
            if (content[i] == '\0') { binary = true; break; }
        if (binary) continue;

        Vector<String> lines = content.Split('\n');
        String chunk;
        int lastPrinted = -1;

        for (int i = 0; i < (int)lines.Size(); ++i)
        {
            if (!std::regex_search(lines[i].CString(), contentRe)) continue;
            if (totalHits >= 200) break;
            ++totalHits;

            int lo = (i - context) > 0 ? i - context : 0;
            int hi = (i + context) < (int)lines.Size() - 1 ? i + context : (int)lines.Size() - 1;
            if (lastPrinted >= 0 && lo > lastPrinted + 1) chunk += "  --\n";
            for (int k = (lo > lastPrinted + 1 ? lo : lastPrinted + 1); k <= hi; ++k)
            {
                chunk += "  " + String(k + 1) + (k == i ? ": " : "  ") + lines[k] + "\n";
                lastPrinted = k;
            }
        }

        if (!chunk.Empty())
        {
            String rel = filePath;
            if (rel.StartsWith(cwd_ + "/")) rel = rel.Substring(cwd_.Length() + 1);
            out += rel + "\n" + chunk;
            ++filesHit;
        }

        if (out.Length() > 16384) { out += "[... truncated ...]"; break; }
    }

    if (out.Empty()) return "(no matches)";
    return String(totalHits) + " match(es) in " + String(filesHit) + " file(s)\n\n" + out;
}

String Claudia::ToolGlob(const String& pattern)
{
    if (pattern.Empty()) return "[Error: pattern is required]";

    std::regex re;
    try { re = std::regex(GlobToRegex(pattern.CString())); }
    catch (...) { return "[Error: invalid glob pattern]"; }

    auto* fs = GetSubsystem<FileSystem>();
    Vector<String> all;
    fs->ScanDir(all, cwd_, "*", SCAN_FILES, true);

    String out;
    unsigned count = 0;
    for (const String& rel : all)
    {
        if (!std::regex_match(rel.CString(), re)) continue;
        if (count++ >= 500) { out += "[... stopped at 500 results]\n"; break; }
        out += rel + "\n";
    }
    return out.Empty() ? "(no files matched)" : out;
}

String Claudia::ToolStat(const String& path)
{
    RuleResult rule = CheckPath(path);
    if (!rule.allowed) return "[BLOCKED: " + rule.reason + "]";

    String resolved = ResolvePath(path);
    auto*  fs       = GetSubsystem<FileSystem>();

    if (!fs->FileExists(resolved) && !fs->DirExists(resolved))
        return path + ": does not exist";

    if (fs->DirExists(resolved))
    {
        Vector<String> files;
        fs->ScanDir(files, resolved, "*", SCAN_FILES, true);
        return path + ": directory, " + String(files.Size()) + " file(s)";
    }

    File f(context_, resolved, FILE_READ);
    if (!f.IsOpen()) return "[Error: cannot stat " + path + "]";
    String content = f.ReadString();
    unsigned lines = content.Empty() ? 0 : 1;
    for (unsigned i = 0; i < content.Length(); ++i)
        if (content[i] == '\n') ++lines;

    return path + ": file, " + String(content.Length()) + " bytes, " + String(lines) + " lines";
}

// ─────────────────────────────────────────────────────────────────────────────
// Shell
// ─────────────────────────────────────────────────────────────────────────────

String Claudia::ToolShell(const String& command)
{
    RuleResult rule = CheckCommand(command);
    if (!rule.allowed) return "[BLOCKED: " + rule.reason + "]";

    auto* fs   = GetSubsystem<FileSystem>();
    String tmp = fs->GetTemporaryDir() + "claudia_shell.txt";
    int    ret = fs->SystemCommand(command + " > " + tmp + " 2>&1");

    String output;
    {
        File f(context_, tmp, FILE_READ);
        if (f.IsOpen()) output = f.ReadString();
    }
    fs->Delete(tmp);

    if (output.Length() > 16384)
        output = output.Substring(0, 16384) + "\n[...truncated]";

    return output + "[exit " + String(ret) + "]";
}

// ─────────────────────────────────────────────────────────────────────────────
// Policy engine
// ─────────────────────────────────────────────────────────────────────────────

static bool IsReadOnly(const String& tool)
{
    return tool == "read_file"  || tool == "grep"     || tool == "glob"    ||
           tool == "stat"       || tool == "list_dir" ||
           tool == "git_status" || tool == "git_diff" || tool == "git_log";
}

PolicyDecision Claudia::CheckPolicy(const String& tool, const String& inputJson) const
{
    // Static path check for file tools.
    if (tool == "read_file" || tool == "write_file" || tool == "edit_file" ||
        tool == "stat"      || tool == "list_dir"   || tool == "glob"      ||
        tool == "grep")
    {
        JSONFile jf(context_);
        JSONValue args;
        if (!inputJson.Empty() && jf.FromString(inputJson))
            args = jf.GetRoot();
        String path = args["path"].IsNull() ? String(".") : args["path"].GetString();
        if (!CheckPath(path).allowed) return POLICY_DENY;
    }

    // Static command check for shell.
    if (tool == "shell")
    {
        JSONFile jf(context_);
        JSONValue args;
        if (!inputJson.Empty() && jf.FromString(inputJson))
            args = jf.GetRoot();
        if (!CheckCommand(args["command"].GetString()).allowed) return POLICY_DENY;
    }

    // Read-only tools pass after static checks.
    if (IsReadOnly(tool)) return POLICY_ALLOW;

    // State-changing: check grant store.
    if (HasGrant(OpKey(tool, inputJson))) return POLICY_ALLOW;

    return POLICY_ASK;
}

String Claudia::OpKey(const String& tool, const String& inputJson) const
{
    JSONFile jf(context_);
    JSONValue args;
    if (!inputJson.Empty() && jf.FromString(inputJson))
        args = jf.GetRoot();

    if (tool == "shell")
    {
        String cmd = args["command"].IsNull() ? String("") : args["command"].GetString();
        return "shell|" + cmd;
    }
    if (tool == "write_file" || tool == "edit_file")
    {
        String path = args["path"].IsNull() ? String("") : args["path"].GetString();
        return tool + "|" + path;
    }
    if (tool == "git_add")
    {
        String path = args["path"].IsNull() ? String("") : args["path"].GetString();
        return "git_add|" + path;
    }
    return tool;  // git_commit, etc.
}

bool Claudia::HasGrant(const String& key) const
{
    for (const Grant& g : grants_)
        if (g.opKey == key) return true;
    return false;
}

void Claudia::AddGrant(const String& key, GrantScope scope)
{
    for (Grant& g : grants_)
    {
        if (g.opKey == key)
        {
            g.scope = scope;
            if (scope == GRANT_ALWAYS) SaveAlwaysGrant(key);
            return;
        }
    }
    grants_.Push({key, scope});
    if (scope == GRANT_ALWAYS) SaveAlwaysGrant(key);
}

String Claudia::GrantsFilePath() const
{
    auto* fs = GetSubsystem<FileSystem>();
    return fs->GetUserDocumentsDir() + ".claudia_grants";
}

void Claudia::LoadGrants()
{
    File f(context_, GrantsFilePath(), FILE_READ);
    if (!f.IsOpen()) return;
    while (!f.IsEof())
    {
        String line = f.ReadLine().Trimmed();
        if (!line.Empty() && !line.StartsWith("#"))
            grants_.Push({line, GRANT_ALWAYS});
    }
}

void Claudia::SaveAlwaysGrant(const String& key)
{
    String path = GrantsFilePath();

    String existing;
    {
        File r(context_, path, FILE_READ);
        if (r.IsOpen()) existing = r.ReadString();
    }
    if (existing.Contains(key + "\n") || existing == key) return;

    // Append to file.
    File f(context_, path, FILE_READWRITE);
    if (f.IsOpen())
    {
        f.Seek(f.GetSize());
        f.WriteLine(key);
    }
    else
    {
        // File doesn't exist yet.
        File fn(context_, path, FILE_WRITE);
        if (fn.IsOpen()) fn.WriteLine(key);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Rule engine (static deny)
// ─────────────────────────────────────────────────────────────────────────────

RuleResult Claudia::CheckPath(const String& path) const
{
    if (path.Empty())               return {false, "empty path"};
    if (path.Contains(".."))        return {false, "../ traversal not allowed"};
    if (path.StartsWith("/") || (path.Length() > 1 && path[1] == ':'))
    {
        if (!path.StartsWith(cwd_)) return {false, "absolute path outside working directory"};
    }
    if (!ResolvePath(path).StartsWith(cwd_)) return {false, "resolved path escapes working directory"};
    return {};
}

RuleResult Claudia::CheckCommand(const String& command) const
{
    String cmd = command.Trimmed().ToLower();
    if (cmd.Contains("rm -rf")  || cmd.Contains("rm -fr") ||
        cmd.Contains("rm -r")   || cmd.Contains("rm -f -r") ||
        cmd.Contains("rm -r -f"))
        return {false, "recursive force delete not allowed"};
    if (cmd == "cd /" || cmd.StartsWith("cd / ") ||
        cmd == "cd ~" || cmd.StartsWith("cd ~ ") ||
        cmd.StartsWith("cd .."))
        return {false, "cd outside working directory not allowed"};
    return {};
}

String Claudia::ResolvePath(const String& path) const
{
    if (path.StartsWith("/")) return path;
    if (path == ".")           return cwd_;
    return cwd_ + "/" + path;
}

// ─────────────────────────────────────────────────────────────────────────────
// JSON helpers
// ─────────────────────────────────────────────────────────────────────────────

String Claudia::QuoteJson(const String& s)
{
    String out = "\"";
    for (unsigned i = 0; i < s.Length(); ++i)
    {
        unsigned char c = (unsigned char)s[i];
        switch (c)
        {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n";  break;
        case '\r': out += "\\r";  break;
        case '\t': out += "\\t";  break;
        default:
            if (c < 0x20) { char hex[8]; sprintf(hex, "\\u%04x", c); out += String(hex); }
            else          out += (char)c;
        }
    }
    out += "\"";
    return out;
}

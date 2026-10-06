// Claudia -- open-source cross-platform Anthropic client.
// Urho3D UI replaces TUI. Local rule engine enforces policy before every tool.
// Rules live on the user's machine, not in prompts.

#pragma once

#define CLAUDIA_VERSION "0.3.0"

#include <Urho3D/Engine/Application.h>
#include <Urho3D/Network/HttpRequest.h>
#include <Urho3D/Network/Network.h>
#include <Urho3D/UI/Button.h>
#include <Urho3D/UI/Font.h>
#include <Urho3D/UI/LineEdit.h>
#include <Urho3D/UI/ScrollView.h>
#include <Urho3D/UI/Text.h>
#include <Urho3D/UI/UIElement.h>
#include <Urho3D/UI/Window.h>

using namespace Urho3D;

struct RuleResult { bool allowed{true}; String reason; };
struct ConvTurn   { String role; String content; };

enum PolicyDecision { POLICY_ALLOW, POLICY_DENY, POLICY_ASK };

enum GrantScope { GRANT_ONCE, GRANT_SESSION, GRANT_ALWAYS };

struct Grant
{
    String opKey;       // tool + "|" + canonical params
    GrantScope scope;
};

class Claudia : public Application
{
    URHO3D_OBJECT(Claudia, Application);

public:
    explicit Claudia(Context* context);
    void Setup() override;
    void Start()  override;
    void Stop()   override;

private:
    // UI
    void CreateUI();
    void AppendMessage(const String& role, const String& text);
    void AppendSystem(const String& text);
    void AppendStreamDelta(const String& delta);
    void FinaliseStream();
    void SetBusy(bool busy);
    void ScrollToBottom();

    // Input
    void Submit(const String& text);
    void HandleSendClicked(StringHash eventType, VariantMap& eventData);
    void HandleKeyDown(StringHash eventType, VariantMap& eventData);
    void HandleUpdate(StringHash eventType, VariantMap& eventData);
    void HandleInputFocus(StringHash eventType, VariantMap& eventData);

    // Anthropic API (streaming SSE)
    void   SendToAPI();
    String BuildRequestBody() const;
    void   FeedBytes(const char* buf, unsigned len);
    void   OnSSEEvent(const String& event, const String& data);
    void   OnAPIFinished();

    // Tool pipeline — sequential, one tool at a time
    void ProcessNextTool();
    void OnPermissionResponse(GrantScope scope);  // called by dialog buttons
    void FinishToolRound();

    // Permission dialog
    void ShowPermissionDialog(const String& tool, const String& inputJson);
    void HidePermissionDialog();
    void HandlePermDeny(StringHash, VariantMap&);
    void HandlePermOnce(StringHash, VariantMap&);
    void HandlePermSession(StringHash, VariantMap&);
    void HandlePermAlways(StringHash, VariantMap&);

    // Tool dispatch
    String ExecuteTool(const String& name, const String& inputJson);

    // File tools
    String ToolReadFile(const String& path, int startLine, int endLine);
    String ToolWriteFile(const String& path, const String& content);
    String ToolEditFile(const String& path, const String& oldString,
                        const String& newString, bool replaceAll);
    String ToolListDir(const String& path, bool recursive);

    // Search / navigation tools
    String ToolGrep(const String& pattern, const String& searchPath,
                    const String& filePattern, bool ignoreCase, int context);
    String ToolGlob(const String& pattern);
    String ToolStat(const String& path);

    // Shell
    String ToolShell(const String& command);

    // Policy engine
    PolicyDecision CheckPolicy(const String& tool, const String& inputJson) const;
    String         OpKey(const String& tool, const String& inputJson) const;
    bool           HasGrant(const String& key) const;
    void           AddGrant(const String& key, GrantScope scope);
    void           LoadGrants();
    void           SaveAlwaysGrant(const String& key);
    String         GrantsFilePath() const;

    // Rule engine (static deny / allow)
    RuleResult CheckPath(const String& path) const;
    RuleResult CheckCommand(const String& command) const;
    String     ResolvePath(const String& path) const;

    // JSON helpers
    static String QuoteJson(const String& s);

    // Config
    String apiKey_;
    String model_{"claude-sonnet-4-6"};
    String cwd_;
    int    maxTokens_{8192};

    // Conversation state
    Vector<ConvTurn> history_;
    bool             busy_{false};

    // Streaming state
    SharedPtr<HttpRequest> pendingRequest_;
    String   sseBuf_;
    String   currentEventType_;
    String   currentEventData_;
    bool     messageStopReceived_{false};
    String   streamText_;

    struct PendingTool { String id; String name; String inputJson; };
    Vector<PendingTool> pendingTools_;
    int      currentToolIdx_{0};        // which tool we're processing right now
    Vector<String> pendingToolResults_; // accumulates results as tools execute

    String   currentToolId_;
    String   currentToolName_;
    String   currentToolInputBuf_;
    bool     inToolInput_{false};

    // Permission state
    bool     awaitingPermission_{false};
    String   permCurrentKey_;   // opKey being asked about
    UIElement* permPanel_{};

    // Grant store
    Vector<Grant> grants_;     // session + always grants loaded from disk

    // UI elements
    SharedPtr<Font> font_;
    static const int FONT_SIZE = 11;

    Text*       statusText_{};
    UIElement*  chatContent_{};
    ScrollView* chatScroll_{};
    LineEdit*   inputEdit_{};
    Button*     sendBtn_{};
    Text*       streamElem_{};
};

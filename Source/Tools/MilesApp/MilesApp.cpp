// MilesApp.cpp
#include "MilesApp.h"
#include <Urho3D/Engine/Engine.h>
#include <Urho3D/IO/Log.h>
#include <Urho3D/Engine/Console.h>
#include <Urho3D/Input/Input.h>

// Include Miles' headers
#include "../../../../NewGuy/mistral_tools.hpp"

// Forward declare AISubsystem and AIBackend for now
// (We'll implement these in this file for simplicity)
namespace Urho3D {

class AIBackend {
public:
    virtual ~AIBackend() = default;
    virtual String Invoke(const String& command, const VariantMap& args) = 0;
    virtual String GetName() const = 0;
    virtual String GetDescription() const = 0;
    virtual bool IsLocal() const = 0;
};

class AISubsystem : public Subsystem {
    URHO3D_OBJECT(AISubsystem, Subsystem);

public:
    AISubsystem(Context* context) : Subsystem(context) {}
    ~AISubsystem() override {
        for (auto& pair : backends_) {
            delete pair.second_;
        }
    }

    void RegisterBackend(AIBackend* backend) {
        backends_[backend->GetName()] = backend;
    }

    void UnregisterBackend(const String& name) {
        auto it = backends_.Find(name);
        if (it != backends_.End()) {
            delete it->second_;
            backends_.Erase(it);
        }
    }

    String Invoke(const String& backendName, const String& command, const VariantMap& args) {
        auto it = backends_.Find(backendName);
        if (it == backends_.End()) {
            return String("ERROR: Backend not found: ") + backendName;
        }
        return it->second_->Invoke(command, args);
    }

    String Invoke(const String& command, const VariantMap& args) {
        if (defaultBackend_.Empty()) {
            return "ERROR: No default backend set.";
        }
        return Invoke(defaultBackend_, command, args);
    }

    void SetDefaultBackend(const String& name) {
        defaultBackend_ = name;
    }

    static void RegisterObject(Context* context) {
        context->AddSubsystem(new AISubsystem(context));
    }

private:
    HashMap<String, AIBackend*> backends_;
    String defaultBackend_;
};

// MistralBackend: Wraps Miles' tools
class MistralBackend : public AIBackend {
public:
    MistralBackend(Context* context) : context_(context) {
        // Initialize Miles' ToolRegistry
        registry_ = new mistral::ToolRegistry();

        // Register exec_tools
        mistral::ExecConfig exec_cfg;
        exec_cfg.root = context_->GetFileSystem()->GetProgramDir().ToStdString();
        exec_cfg.allow_shell = true;
        mistral::register_exec_tools(*registry_, exec_cfg);

        // Register fs_tools
        mistral::FsConfig fs_cfg;
        fs_cfg.root = context_->GetFileSystem()->GetProgramDir().ToStdString();
        fs_cfg.allow_mutation = true;
        mistral::register_fs_tools(*registry_, fs_cfg);
    }

    ~MistralBackend() override {
        delete registry_;
    }

    String Invoke(const String& command, const VariantMap& args) override {
        // Convert VariantMap to ToolArgs (std::map<std::string, std::string>)
        mistral::ToolArgs toolArgs;
        for (const auto& pair : args) {
            toolArgs[pair.first_] = pair.second_.ToString();
        }

        mistral::ToolCall call;
        call.name = command.ToStdString();

        std::string result = registry_->invoke(call);
        return String(result.c_str());
    }

    String GetName() const override { return "Mistral"; }
    String GetDescription() const override { return "Miles tool system"; }
    bool IsLocal() const override { return true; }

private:
    Context* context_;
    mistral::ToolRegistry* registry_;
};

} // namespace Urho3D

// MilesApp implementation
namespace Urho3D {

MilesApp::MilesApp(Context* context) : Application(context) {
}

MilesApp::~MilesApp() {
}

void MilesApp::Setup() {
    // Register AISubsystem
    AISubsystem::RegisterObject(context_);

    // Get AISubsystem and register MistralBackend
    AISubsystem* ai = GetSubsystem<AISubsystem>();
    MistralBackend* mistralBackend = new MistralBackend(context_);
    ai->RegisterBackend(mistralBackend);
    ai->SetDefaultBackend("Mistral");

    // Subscribe to console commands
    SubscribeToEvent(E_CONSOLECOMMAND, URHO3D_HANDLER(MilesApp, HandleMistralCommand));

    // Engine setup
    engineParameters_["WindowTitle"] = "Miles + Urho3D";
    engineParameters_["LogName"] = "MilesApp.log";
    engineParameters_["FullScreen"] = false;
    engineParameters_["WindowWidth"] = 1280;
    engineParameters_["WindowHeight"] = 720;
}

void MilesApp::Start() {
    // Show console
    Console* console = GetSubsystem<Console>();
    console->SetVisible(true);
    console->SetAutoVisibleOnError(true);

    URHO3D_LOGINFO("MilesApp started. Type 'mistral <command>' in the console.");
}

void MilesApp::Stop() {
    AISubsystem* ai = GetSubsystem<AISubsystem>();
    ai->UnregisterBackend("Mistral");
}

void MilesApp::HandleMistralCommand(StringHash eventType, VariantMap& eventData) {
    using namespace ConsoleCommand;
    String command = eventData[P_COMMAND].GetString();

    if (!command.StartsWith("mistral ")) {
        return;
    }

    // Extract the Miles command (e.g., "mistral git_log --path . --max 3")
    String milesCommand = command.SubString(8); // Skip "mistral "
    AISubsystem* ai = GetSubsystem<AISubsystem>();

    // Parse the command (simplified: assume first word is tool name, rest are args)
    Vector<String> tokens = milesCommand.Split(' ');
    if (tokens.Empty()) {
        URHO3D_LOGERROR("Usage: mistral <tool> [args]");
        return;
    }

    String toolName = tokens[0];
    VariantMap args;

    // Parse args (e.g., "--path", ".", "--max", "3")
    for (unsigned i = 1; i < tokens.Size(); i += 2) {
        if (i + 1 < tokens.Size()) {
            args[tokens[i]] = tokens[i + 1];
        }
    }

    // Invoke the tool
    String result = ai->Invoke("Mistral", toolName, args);
    URHO3D_LOGINFO(result);
}

} // namespace Urho3D

// Application entry point
URHO3D_DEFINE_APPLICATION_MAIN(MilesApp)

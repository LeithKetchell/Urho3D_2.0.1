// MelbourneClock — lower-right HH:MM display in Melbourne time (AEST/AEDT with auto DST).
// Uses OS timezone database (tzdata) — no manual DST math.
// Standalone widget: create, call Update() each frame, done.
// Client can add a scrub offset in hours via SetScrubOffset().

#pragma once

#include "../UI/Text.h"
#include "../UI/Font.h"
#include "../Core/Object.h"

namespace Urho3D
{

class URHO3D_API MelbourneClock : public Object
{
    URHO3D_OBJECT(MelbourneClock, Object);

public:
    explicit MelbourneClock(Context* context);

    /// Create the UI text element. Call once after UI subsystem is ready.
    void Initialize(UIElement* uiRoot, Font* font, int fontSize = 14);

    /// Update the display. Call from HandleUpdate.
    void Update();

    /// Set time scrub offset in hours (client time slider).
    void SetScrubOffset(float hours) { scrubOffsetSeconds_ = static_cast<int>(hours * 3600.0f); }
    float GetScrubOffset() const { return scrubOffsetSeconds_ / 3600.0f; }

    /// Show/hide the clock.
    void SetVisible(bool visible);
    bool IsVisible() const;

    /// Set display date alongside time (HH:MM → "Fri 20 Mar  17:28").
    void SetShowDate(bool show) { showDate_ = show; }

private:
    SharedPtr<Text> clockText_;
    int scrubOffsetSeconds_{};
    int lastMinute_{-1};
    bool showDate_{true};
};

} // namespace Urho3D

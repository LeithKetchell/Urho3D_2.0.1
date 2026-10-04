// MelbourneClock — Melbourne time display widget.
// Uses OS timezone database (tzdata) for correct DST handling.

#include "../Precompiled.h"
#include "MelbourneClock.h"
#include "../UI/UI.h"

#include <ctime>
#include <cstdlib>
#include <cstring>

namespace Urho3D
{

static const char* WEEKDAY_NAMES[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
static const char* MONTH_NAMES[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                     "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

MelbourneClock::MelbourneClock(Context* context) : Object(context) {}

void MelbourneClock::Initialize(UIElement* uiRoot, Font* font, int fontSize)
{
    if (!uiRoot || !font)
        return;

    clockText_ = uiRoot->CreateChild<Text>("MelbourneClock");
    clockText_->SetFont(font, fontSize);
    clockText_->SetColor(Color(0.9f, 0.9f, 0.8f));
    clockText_->SetAlignment(HA_RIGHT, VA_BOTTOM);
    clockText_->SetPosition(-8, -8);
    clockText_->SetTextEffect(TE_SHADOW);
    clockText_->SetEffectShadowOffset(IntVector2(1, 1));
    clockText_->SetText("--:--");
}

void MelbourneClock::Update()
{
    if (!clockText_)
        return;

    time_t now = time(nullptr);
    now += scrubOffsetSeconds_;

    // Use OS timezone database for Melbourne — handles DST correctly
    const char* oldTZ = getenv("TZ");
    char savedTZ[128];
    if (oldTZ)
    {
        strncpy(savedTZ, oldTZ, sizeof(savedTZ) - 1);
        savedTZ[sizeof(savedTZ) - 1] = '\0';
    }

    setenv("TZ", "Australia/Melbourne", 1);
    tzset();

    struct tm melbTm;
    localtime_r(&now, &melbTm);

    // Restore original TZ
    if (oldTZ)
        setenv("TZ", savedTZ, 1);
    else
        unsetenv("TZ");
    tzset();

    // Only update text when the minute changes
    int packed = melbTm.tm_min | (melbTm.tm_hour << 8);
    if (packed == lastMinute_)
        return;
    lastMinute_ = packed;

    char buf[64];
    if (showDate_)
    {
        snprintf(buf, sizeof(buf), "%s %d %s  %02d:%02d",
                 WEEKDAY_NAMES[melbTm.tm_wday],
                 melbTm.tm_mday,
                 MONTH_NAMES[melbTm.tm_mon],
                 melbTm.tm_hour,
                 melbTm.tm_min);
    }
    else
    {
        snprintf(buf, sizeof(buf), "%02d:%02d", melbTm.tm_hour, melbTm.tm_min);
    }

    clockText_->SetText(buf);
}

void MelbourneClock::SetVisible(bool visible)
{
    if (clockText_)
        clockText_->SetVisible(visible);
}

bool MelbourneClock::IsVisible() const
{
    return clockText_ && clockText_->IsVisible();
}

} // namespace Urho3D

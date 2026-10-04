// Copyright (c) 2008-2022 the Urho3D project
// License: MIT

#include "../Precompiled.h"

#include "../Core/Context.h"
#include "../Resource/ResourceCache.h"
#include "../UI/DockPanel.h"
#include "../UI/DockManager.h"
#include "../UI/Text.h"
#include "../UI/Button.h"
#include "../UI/Font.h"
#include "../UI/UIEvents.h"

#include "../DebugNew.h"

namespace Urho3D
{

extern const char* UI_CATEGORY;

DockPanel::DockPanel(Context* context) :
    Window(context)
{
    SetMovable(true);
    SetResizable(true);
    SetClipChildren(true);
    SetLayout(LM_VERTICAL, 0);

    CreateTitleBar();

    // Content container fills remaining space
    contentContainer_ = new UIElement(context_);
    contentContainer_->SetLayout(LM_VERTICAL, 0);
    Window::AddChild(contentContainer_);
}

DockPanel::~DockPanel() = default;

void DockPanel::RegisterObject(Context* context)
{
    context->RegisterFactory<DockPanel>(UI_CATEGORY);
    URHO3D_COPY_BASE_ATTRIBUTES(Window);
}

void DockPanel::CreateTitleBar()
{
    titleBar_ = new UIElement(context_);
    titleBar_->SetLayout(LM_HORIZONTAL, 2, IntRect(4, 2, 4, 2));
    titleBar_->SetFixedHeight(22);
    Window::AddChild(titleBar_);

    titleText_ = new Text(context_);
    titleText_->SetText("Panel");
    titleText_->SetColor(Color(0.9f, 0.9f, 0.9f));
    titleBar_->AddChild(titleText_);

    // Try to set font if resource cache is available
    auto* cache = GetSubsystem<ResourceCache>();
    if (cache)
    {
        auto* font = cache->GetResource<Font>("Fonts/Anonymous Pro.ttf");
        if (font)
            titleText_->SetFont(font, 11);
    }

    closeButton_ = new Button(context_);
    closeButton_->SetFixedSize(16, 16);
    closeButton_->SetHorizontalAlignment(HA_RIGHT);
    titleBar_->AddChild(closeButton_);

    // Close button label
    auto* closeText = new Text(context_);
    closeText->SetText("X");
    closeText->SetColor(Color(0.8f, 0.2f, 0.2f));
    closeText->SetAlignment(HA_CENTER, VA_CENTER);
    if (cache)
    {
        auto* font = cache->GetResource<Font>("Fonts/Anonymous Pro.ttf");
        if (font)
            closeText->SetFont(font, 10);
    }
    closeButton_->AddChild(closeText);

    SubscribeToEvent(closeButton_, E_RELEASED, URHO3D_HANDLER(DockPanel, HandleCloseButton));
}

void DockPanel::SetTitle(const String& title)
{
    title_ = title;
    if (titleText_)
        titleText_->SetText(title);
}

void DockPanel::SetContent(UIElement* content)
{
    if (content_)
        contentContainer_->RemoveChild(content_);
    content_ = content;
    if (content)
        contentContainer_->AddChild(content);
}

void DockPanel::SetDockState(DockState state)
{
    dockState_ = state;
    // When docked, disable window drag/resize — the DockSplit manages size
    SetMovable(state == DOCK_FLOATING);
    SetResizable(state == DOCK_FLOATING);
}

void DockPanel::SetDockManager(DockManager* manager)
{
    dockManager_ = manager;
}

void DockPanel::OnDragBegin(const IntVector2& position, const IntVector2& screenPosition,
    MouseButtonFlags buttons, QualifierFlags qualifiers, Cursor* cursor)
{
    // If docked, undock on drag and switch to floating
    if (dockState_ == DOCK_DOCKED && dockManager_)
    {
        // TODO: DockManager::UndockPanel(this) — remove from split tree
        SetDockState(DOCK_FLOATING);
    }

    Window::OnDragBegin(position, screenPosition, buttons, qualifiers, cursor);

    if (dockManager_)
        dockManager_->BeginDockPreview(this, screenPosition);
}

void DockPanel::OnDragMove(const IntVector2& position, const IntVector2& screenPosition,
    const IntVector2& deltaPos, MouseButtonFlags buttons, QualifierFlags qualifiers, Cursor* cursor)
{
    Window::OnDragMove(position, screenPosition, deltaPos, buttons, qualifiers, cursor);

    if (dockManager_)
        dockManager_->UpdateDockPreview(this, screenPosition);
}

void DockPanel::OnDragEnd(const IntVector2& position, const IntVector2& screenPosition,
    MouseButtonFlags dragButtons, MouseButtonFlags releaseButtons, Cursor* cursor)
{
    Window::OnDragEnd(position, screenPosition, dragButtons, releaseButtons, cursor);

    if (dockManager_)
        dockManager_->EndDockPreview(this, screenPosition);
}

void DockPanel::HandleCloseButton(StringHash /*eventType*/, VariantMap& /*eventData*/)
{
    SetVisible(false);
}

}

// Copyright (c) 2008-2022 the Urho3D project
// License: MIT

/// \file

#pragma once

#include "../UI/Window.h"

namespace Urho3D
{

class DockManager;
class Text;
class Button;

/// Dock state for a panel.
enum DockState
{
    DOCK_FLOATING = 0,
    DOCK_DOCKED
};

/// Dock-aware window wrapper that can be docked into a DockManager or float freely.
class URHO3D_API DockPanel : public Window
{
    URHO3D_OBJECT(DockPanel, Window);

public:
    explicit DockPanel(Context* context);
    ~DockPanel() override;
    static void RegisterObject(Context* context);

    /// Set the panel title shown in the title bar.
    void SetTitle(const String& title);
    /// Set the content element displayed in this panel.
    void SetContent(UIElement* content);
    /// Set dock state.
    void SetDockState(DockState state);
    /// Set the dock manager this panel belongs to.
    void SetDockManager(DockManager* manager);

    /// Get panel title.
    const String& GetTitle() const { return title_; }
    /// Get content element.
    UIElement* GetContent() const { return content_; }
    /// Get dock state.
    DockState GetDockState() const { return dockState_; }
    /// Get dock manager.
    DockManager* GetDockManager() const { return dockManager_; }
    /// Get title bar element.
    UIElement* GetTitleBar() const { return titleBar_; }

    /// React to drag begin — notify dock manager.
    void OnDragBegin(const IntVector2& position, const IntVector2& screenPosition, MouseButtonFlags buttons,
        QualifierFlags qualifiers, Cursor* cursor) override;
    /// React to drag move — notify dock manager for zone detection.
    void OnDragMove(const IntVector2& position, const IntVector2& screenPosition, const IntVector2& deltaPos,
        MouseButtonFlags buttons, QualifierFlags qualifiers, Cursor* cursor) override;
    /// React to drag end — dock or float.
    void OnDragEnd(const IntVector2& position, const IntVector2& screenPosition, MouseButtonFlags dragButtons,
        MouseButtonFlags releaseButtons, Cursor* cursor) override;

private:
    /// Create the title bar with label and close button.
    void CreateTitleBar();
    /// Handle close button click.
    void HandleCloseButton(StringHash eventType, VariantMap& eventData);

    /// Panel title.
    String title_;
    /// Dock state.
    DockState dockState_{DOCK_FLOATING};
    /// Dock manager (weak reference).
    DockManager* dockManager_{};
    /// Title bar container.
    SharedPtr<UIElement> titleBar_;
    /// Title text.
    SharedPtr<Text> titleText_;
    /// Close button.
    SharedPtr<Button> closeButton_;
    /// Content container.
    SharedPtr<UIElement> contentContainer_;
    /// Content element.
    SharedPtr<UIElement> content_;
};

}

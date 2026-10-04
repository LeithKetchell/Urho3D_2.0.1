// Copyright (c) 2008-2022 the Urho3D project
// License: MIT

/// \file

#pragma once

#include "../UI/UIElement.h"

namespace Urho3D
{

class DockSplit;
class DockPanel;
class BorderImage;

/// Dock zone where a panel can snap to.
enum DockZone
{
    DOCK_ZONE_NONE = 0,
    DOCK_ZONE_LEFT,
    DOCK_ZONE_RIGHT,
    DOCK_ZONE_TOP,
    DOCK_ZONE_BOTTOM,
    DOCK_ZONE_CENTER
};

/// Root container for dockable panels. Manages the tree of DockSplits.
class URHO3D_API DockManager : public UIElement
{
    URHO3D_OBJECT(DockManager, UIElement);

public:
    explicit DockManager(Context* context);
    ~DockManager() override;
    static void RegisterObject(Context* context);

    /// Dock a panel to the specified zone. Creates splits as needed.
    void Dock(DockPanel* panel, DockZone zone);
    /// Undock a panel, returning it to floating state.
    void UndockPanel(DockPanel* panel);

    /// Set the edge zone size as a fraction of total width/height (default 0.2).
    void SetZoneFraction(float fraction);
    /// Get the edge zone fraction.
    float GetZoneFraction() const { return zoneFraction_; }

    /// Called by DockPanel when drag begins.
    void BeginDockPreview(DockPanel* panel, const IntVector2& screenPos);
    /// Called by DockPanel during drag to update preview.
    void UpdateDockPreview(DockPanel* panel, const IntVector2& screenPos);
    /// Called by DockPanel when drag ends — dock if in zone, else float.
    void EndDockPreview(DockPanel* panel, const IntVector2& screenPos);

    /// Get the root split (may be null if nothing is docked).
    DockSplit* GetRootSplit() const { return rootSplit_; }

    /// Handle resize.
    void OnResize(const IntVector2& newSize, const IntVector2& delta) override;

private:
    /// Detect which dock zone the screen position falls in.
    DockZone HitTestZone(const IntVector2& screenPos);
    /// Show the dock preview overlay at the given zone.
    void ShowPreviewOverlay(DockZone zone);
    /// Hide the dock preview overlay.
    void HidePreviewOverlay();

    /// Root split of the dock tree.
    SharedPtr<DockSplit> rootSplit_;
    /// Dock preview overlay (semi-transparent rectangle).
    SharedPtr<BorderImage> previewOverlay_;
    /// Edge zone fraction (0.0-0.5).
    float zoneFraction_{0.2f};
    /// Panel currently being dragged.
    DockPanel* draggingPanel_{};
    /// Current preview zone.
    DockZone previewZone_{DOCK_ZONE_NONE};
};

}

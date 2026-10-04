// Copyright (c) 2008-2022 the Urho3D project
// License: MIT

/// \file

#pragma once

#include "../UI/BorderImage.h"

namespace Urho3D
{

/// Split orientation.
enum DockOrientation
{
    DOCK_HORIZONTAL = 0,
    DOCK_VERTICAL
};

/// Two-child split container with a draggable divider.
/// Each child can be a UIElement, DockSplit (for nesting), or DockPanel.
class URHO3D_API DockSplit : public UIElement
{
    URHO3D_OBJECT(DockSplit, UIElement);

public:
    explicit DockSplit(Context* context);
    ~DockSplit() override;
    static void RegisterObject(Context* context);

    /// Set split orientation (horizontal = side-by-side, vertical = top/bottom).
    void SetOrientation(DockOrientation orientation);
    /// Set split ratio (0.0-1.0, position of divider).
    void SetSplitRatio(float ratio);
    /// Set divider thickness in pixels.
    void SetDividerSize(int size);
    /// Set minimum size for each child pane in pixels.
    void SetMinPaneSize(int minSize);

    /// Set the first (left/top) child.
    void SetFirstChild(UIElement* child);
    /// Set the second (right/bottom) child.
    void SetSecondChild(UIElement* child);

    /// Get split orientation.
    DockOrientation GetOrientation() const { return orientation_; }
    /// Get split ratio.
    float GetSplitRatio() const { return splitRatio_; }
    /// Get divider size.
    int GetDividerSize() const { return dividerSize_; }
    /// Get minimum pane size.
    int GetMinPaneSize() const { return minPaneSize_; }
    /// Get the first child.
    UIElement* GetFirstChild() const { return firstChild_; }
    /// Get the second child.
    UIElement* GetSecondChild() const { return secondChild_; }
    /// Get the divider element.
    BorderImage* GetDivider() const { return divider_; }

    /// Handle resize — recalculate child sizes.
    void OnResize(const IntVector2& newSize, const IntVector2& delta) override;

private:
    /// Recalculate child positions and sizes from current ratio.
    void UpdateLayout();
    /// Handle divider drag begin.
    void HandleDividerDragBegin(StringHash eventType, VariantMap& eventData);
    /// Handle divider drag move.
    void HandleDividerDragMove(StringHash eventType, VariantMap& eventData);

    /// Split orientation.
    DockOrientation orientation_{DOCK_HORIZONTAL};
    /// Split ratio (0.0-1.0).
    float splitRatio_{0.5f};
    /// Divider thickness in pixels.
    int dividerSize_{6};
    /// Minimum pane size in pixels.
    int minPaneSize_{50};
    /// First child (left or top).
    SharedPtr<UIElement> firstChild_;
    /// Second child (right or bottom).
    SharedPtr<UIElement> secondChild_;
    /// Divider element.
    SharedPtr<BorderImage> divider_;
};

}

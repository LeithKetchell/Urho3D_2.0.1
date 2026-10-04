// Copyright (c) 2008-2022 the Urho3D project
// License: MIT

#include "../Precompiled.h"

#include "../Core/Context.h"
#include "../UI/DockManager.h"
#include "../UI/DockSplit.h"
#include "../UI/DockPanel.h"
#include "../UI/BorderImage.h"

#include "../DebugNew.h"

namespace Urho3D
{

extern const char* UI_CATEGORY;

DockManager::DockManager(Context* context) :
    UIElement(context)
{
    SetClipChildren(true);

    // Create the preview overlay (hidden by default)
    previewOverlay_ = new BorderImage(context_);
    previewOverlay_->SetColor(Color(0.2f, 0.4f, 0.8f, 0.3f));
    previewOverlay_->SetVisible(false);
    previewOverlay_->SetPriority(1000);  // Always on top within dock manager
    UIElement::AddChild(previewOverlay_);
}

DockManager::~DockManager() = default;

void DockManager::RegisterObject(Context* context)
{
    context->RegisterFactory<DockManager>(UI_CATEGORY);
    URHO3D_COPY_BASE_ATTRIBUTES(UIElement);
}

void DockManager::SetZoneFraction(float fraction)
{
    zoneFraction_ = Clamp(fraction, 0.05f, 0.5f);
}

void DockManager::Dock(Urho3D::DockPanel* panel, DockZone zone)
{
    if (!panel || zone == DOCK_ZONE_NONE)
        return;

    panel->SetDockManager(this);
    panel->SetDockState(DOCK_DOCKED);

    if (!rootSplit_)
    {
        // First panel docked — create root split and put panel as first child
        rootSplit_ = new DockSplit(context_);
        rootSplit_->SetOrientation(
            (zone == DOCK_ZONE_LEFT || zone == DOCK_ZONE_RIGHT) ? DOCK_HORIZONTAL : DOCK_VERTICAL);
        rootSplit_->SetSize(GetSize());
        UIElement::AddChild(rootSplit_);

        if (zone == DOCK_ZONE_RIGHT || zone == DOCK_ZONE_BOTTOM)
        {
            // Panel goes second; create a placeholder first
            auto* placeholder = new UIElement(context_);
            rootSplit_->SetFirstChild(placeholder);
            rootSplit_->SetSecondChild(panel);
            rootSplit_->SetSplitRatio(0.75f);
        }
        else
        {
            auto* placeholder = new UIElement(context_);
            rootSplit_->SetFirstChild(panel);
            rootSplit_->SetSecondChild(placeholder);
            rootSplit_->SetSplitRatio(0.25f);
        }
        return;
    }

    // Wrap existing root in a new split to add the panel
    auto* newRoot = new DockSplit(context_);
    newRoot->SetOrientation(
        (zone == DOCK_ZONE_LEFT || zone == DOCK_ZONE_RIGHT) ? DOCK_HORIZONTAL : DOCK_VERTICAL);
    newRoot->SetSize(GetSize());

    // Remove old root from our children, re-parent under new root
    UIElement::RemoveChild(rootSplit_);

    if (zone == DOCK_ZONE_LEFT || zone == DOCK_ZONE_TOP)
    {
        newRoot->SetFirstChild(panel);
        newRoot->SetSecondChild(rootSplit_);
        newRoot->SetSplitRatio(0.25f);
    }
    else
    {
        newRoot->SetFirstChild(rootSplit_);
        newRoot->SetSecondChild(panel);
        newRoot->SetSplitRatio(0.75f);
    }

    rootSplit_ = newRoot;
    UIElement::AddChild(rootSplit_);
}

void DockManager::UndockPanel(Urho3D::DockPanel* panel)
{
    if (!panel)
        return;

    panel->SetDockState(DOCK_FLOATING);
    panel->SetDockManager(nullptr);

    // TODO: Walk the split tree, find and remove the panel,
    // collapse unnecessary splits (e.g. split with one child becomes that child).
    // For Phase 1, panels are docked at edges and not yet dynamically undocked.
}

void DockManager::BeginDockPreview(Urho3D::DockPanel* panel, const IntVector2& screenPos)
{
    draggingPanel_ = panel;
    previewZone_ = DOCK_ZONE_NONE;
    UpdateDockPreview(panel, screenPos);
}

void DockManager::UpdateDockPreview(Urho3D::DockPanel* /*panel*/, const IntVector2& screenPos)
{
    DockZone zone = HitTestZone(screenPos);
    if (zone != previewZone_)
    {
        previewZone_ = zone;
        if (zone != DOCK_ZONE_NONE)
            ShowPreviewOverlay(zone);
        else
            HidePreviewOverlay();
    }
}

void DockManager::EndDockPreview(Urho3D::DockPanel* panel, const IntVector2& screenPos)
{
    HidePreviewOverlay();

    DockZone zone = HitTestZone(screenPos);
    if (zone != DOCK_ZONE_NONE && panel)
    {
        // Remove panel from its current parent (floating in UI root)
        if (panel->GetParent())
            panel->GetParent()->RemoveChild(panel);

        Dock(panel, zone);
    }

    draggingPanel_ = nullptr;
    previewZone_ = DOCK_ZONE_NONE;
}

DockZone DockManager::HitTestZone(const IntVector2& screenPos)
{
    IntVector2 pos = ScreenToElement(screenPos);
    IntVector2 size = GetSize();

    if (pos.x_ < 0 || pos.y_ < 0 || pos.x_ >= size.x_ || pos.y_ >= size.y_)
        return DOCK_ZONE_NONE;

    int zoneW = (int)(size.x_ * zoneFraction_);
    int zoneH = (int)(size.y_ * zoneFraction_);

    if (pos.x_ < zoneW) return DOCK_ZONE_LEFT;
    if (pos.x_ >= size.x_ - zoneW) return DOCK_ZONE_RIGHT;
    if (pos.y_ < zoneH) return DOCK_ZONE_TOP;
    if (pos.y_ >= size.y_ - zoneH) return DOCK_ZONE_BOTTOM;

    return DOCK_ZONE_CENTER;
}

void DockManager::ShowPreviewOverlay(DockZone zone)
{
    IntVector2 size = GetSize();
    int zoneW = (int)(size.x_ * zoneFraction_);
    int zoneH = (int)(size.y_ * zoneFraction_);

    IntVector2 pos(0, 0);
    IntVector2 overlaySize = size;

    switch (zone)
    {
    case DOCK_ZONE_LEFT:
        overlaySize.x_ = zoneW;
        break;
    case DOCK_ZONE_RIGHT:
        pos.x_ = size.x_ - zoneW;
        overlaySize.x_ = zoneW;
        break;
    case DOCK_ZONE_TOP:
        overlaySize.y_ = zoneH;
        break;
    case DOCK_ZONE_BOTTOM:
        pos.y_ = size.y_ - zoneH;
        overlaySize.y_ = zoneH;
        break;
    case DOCK_ZONE_CENTER:
        // Full area
        break;
    default:
        HidePreviewOverlay();
        return;
    }

    previewOverlay_->SetPosition(pos);
    previewOverlay_->SetSize(overlaySize);
    previewOverlay_->SetVisible(true);
    previewOverlay_->BringToFront();
}

void DockManager::HidePreviewOverlay()
{
    if (previewOverlay_)
        previewOverlay_->SetVisible(false);
}

void DockManager::OnResize(const IntVector2& newSize, const IntVector2& delta)
{
    UIElement::OnResize(newSize, delta);

    // Resize root split to fill the dock manager
    if (rootSplit_)
        rootSplit_->SetSize(newSize);
}

}

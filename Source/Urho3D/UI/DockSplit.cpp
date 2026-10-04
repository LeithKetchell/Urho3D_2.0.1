// Copyright (c) 2008-2022 the Urho3D project
// License: MIT

#include "../Precompiled.h"

#include "../Core/Context.h"
#include "../UI/DockSplit.h"
#include "../UI/UIEvents.h"

#include "../DebugNew.h"

namespace Urho3D
{

extern const char* UI_CATEGORY;

DockSplit::DockSplit(Context* context) :
    UIElement(context)
{
    SetClipChildren(true);

    // Create the divider bar
    divider_ = new BorderImage(context_);
    divider_->SetColor(Color(0.4f, 0.4f, 0.4f));
    divider_->SetEnabled(true);  // Must be enabled for drag events
    UIElement::AddChild(divider_);

    SubscribeToEvent(divider_, E_DRAGBEGIN, URHO3D_HANDLER(DockSplit, HandleDividerDragBegin));
    SubscribeToEvent(divider_, E_DRAGMOVE, URHO3D_HANDLER(DockSplit, HandleDividerDragMove));
}

DockSplit::~DockSplit() = default;

void DockSplit::RegisterObject(Context* context)
{
    context->RegisterFactory<DockSplit>(UI_CATEGORY);
    URHO3D_COPY_BASE_ATTRIBUTES(UIElement);
}

void DockSplit::SetOrientation(DockOrientation orientation)
{
    if (orientation_ == orientation)
        return;
    orientation_ = orientation;
    UpdateLayout();
}

void DockSplit::SetSplitRatio(float ratio)
{
    ratio = Clamp(ratio, 0.0f, 1.0f);
    if (splitRatio_ == ratio)
        return;
    splitRatio_ = ratio;
    UpdateLayout();
}

void DockSplit::SetDividerSize(int size)
{
    dividerSize_ = Max(size, 2);
    UpdateLayout();
}

void DockSplit::SetMinPaneSize(int minSize)
{
    minPaneSize_ = Max(minSize, 10);
    UpdateLayout();
}

void DockSplit::SetFirstChild(UIElement* child)
{
    if (firstChild_)
        RemoveChild(firstChild_);
    firstChild_ = child;
    if (child)
        UIElement::AddChild(child);
    UpdateLayout();
}

void DockSplit::SetSecondChild(UIElement* child)
{
    if (secondChild_)
        RemoveChild(secondChild_);
    secondChild_ = child;
    if (child)
        UIElement::AddChild(child);
    UpdateLayout();
}

void DockSplit::OnResize(const IntVector2& newSize, const IntVector2& delta)
{
    UIElement::OnResize(newSize, delta);
    UpdateLayout();
}

void DockSplit::UpdateLayout()
{
    IntVector2 size = GetSize();
    if (size.x_ <= 0 || size.y_ <= 0)
        return;

    int totalSize = (orientation_ == DOCK_HORIZONTAL) ? size.x_ : size.y_;
    int available = totalSize - dividerSize_;
    if (available < 2 * minPaneSize_)
        available = 2 * minPaneSize_;

    int firstSize = (int)(available * splitRatio_);
    firstSize = Clamp(firstSize, minPaneSize_, available - minPaneSize_);
    int secondSize = available - firstSize;

    if (orientation_ == DOCK_HORIZONTAL)
    {
        if (firstChild_)
        {
            firstChild_->SetPosition(0, 0);
            firstChild_->SetSize(firstSize, size.y_);
        }
        if (divider_)
        {
            divider_->SetPosition(firstSize, 0);
            divider_->SetSize(dividerSize_, size.y_);
        }
        if (secondChild_)
        {
            secondChild_->SetPosition(firstSize + dividerSize_, 0);
            secondChild_->SetSize(secondSize, size.y_);
        }
    }
    else
    {
        if (firstChild_)
        {
            firstChild_->SetPosition(0, 0);
            firstChild_->SetSize(size.x_, firstSize);
        }
        if (divider_)
        {
            divider_->SetPosition(0, firstSize);
            divider_->SetSize(size.x_, dividerSize_);
        }
        if (secondChild_)
        {
            secondChild_->SetPosition(0, firstSize + dividerSize_);
            secondChild_->SetSize(size.x_, secondSize);
        }
    }
}

void DockSplit::HandleDividerDragBegin(StringHash /*eventType*/, VariantMap& /*eventData*/)
{
    // Nothing to store — ratio computed on each drag move
}

void DockSplit::HandleDividerDragMove(StringHash /*eventType*/, VariantMap& eventData)
{
    using namespace DragMove;

    IntVector2 size = GetSize();
    int totalSize = (orientation_ == DOCK_HORIZONTAL) ? size.x_ : size.y_;
    if (totalSize <= dividerSize_)
        return;

    int delta = (orientation_ == DOCK_HORIZONTAL)
        ? eventData[P_DX].GetI32()
        : eventData[P_DY].GetI32();

    int available = totalSize - dividerSize_;
    float deltaRatio = (float)delta / (float)available;
    float newRatio = Clamp(splitRatio_ + deltaRatio, 0.0f, 1.0f);

    // Enforce minimum pane sizes
    int firstSize = (int)(available * newRatio);
    firstSize = Clamp(firstSize, minPaneSize_, available - minPaneSize_);
    newRatio = (float)firstSize / (float)available;

    splitRatio_ = newRatio;
    UpdateLayout();
}

}

// MultiLineEdit — multi-line text widget with selection and clipboard.

#include "../Precompiled.h"
#include "MultiLineEdit.h"
#include "../UI/Text.h"
#include "../UI/UI.h"
#include "../UI/UIEvents.h"
#include "../Input/Input.h"
#include "../Input/InputEvents.h"
#include "../Core/Context.h"

namespace Urho3D
{

extern const char* UI_CATEGORY;

MultiLineEdit::MultiLineEdit(Context* context) : BorderImage(context)
{
    SetEnabled(true);
    SetFocusMode(FM_FOCUSABLE_DEFOCUSABLE);
    SetClipChildren(true);

    textElement_ = CreateChild<Text>("MLE_Text");
    textElement_->SetInternal(true);
    textElement_->SetWordwrap(true);

    cursor_ = CreateChild<BorderImage>("MLE_Cursor");
    cursor_->SetInternal(true);
    cursor_->SetEnabled(false);
    cursor_->SetFixedSize(2, 0);
    cursor_->SetVisible(false);
    cursor_->SetColor(Color(0.9f, 1.0f, 0.9f));  // Visible cursor (light green)

    SubscribeToEvent(this, E_FOCUSED, URHO3D_HANDLER(MultiLineEdit, HandleFocused));
    SubscribeToEvent(this, E_DEFOCUSED, URHO3D_HANDLER(MultiLineEdit, HandleDefocused));
}

MultiLineEdit::~MultiLineEdit() = default;

void MultiLineEdit::RegisterObject(Context* context)
{
    context->RegisterFactory<MultiLineEdit>(UI_CATEGORY);
    URHO3D_COPY_BASE_ATTRIBUTES(BorderImage);
    URHO3D_UPDATE_ATTRIBUTE_DEFAULT_VALUE("Clip Children", true);
    URHO3D_UPDATE_ATTRIBUTE_DEFAULT_VALUE("Is Enabled", true);
    URHO3D_UPDATE_ATTRIBUTE_DEFAULT_VALUE("Focus Mode", FM_FOCUSABLE_DEFOCUSABLE);
}

void MultiLineEdit::ApplyAttributes()
{
    BorderImage::ApplyAttributes();
    UpdateText();
    UpdateCursor();
}

void MultiLineEdit::Update(float timeStep)
{
    if (cursorBlinkRate_ > 0.0f)
        cursorBlinkTimer_ += timeStep;

    // Toggle cursor visibility based on blink
    if (HasFocus() && !readOnly_)
    {
        float blinkPeriod = 1.0f / cursorBlinkRate_;
        bool visible = fmodf(cursorBlinkTimer_, blinkPeriod) < blinkPeriod * 0.5f;
        cursor_->SetVisible(visible);
    }
    else
    {
        cursor_->SetVisible(false);
    }

    // Auto-size height to fit text content (for ScrollView wrapping)
    if (textElement_)
    {
        int padding = 4;
        int textH = textElement_->GetHeight() + padding * 2;
        int minH = GetMinHeight();
        if (minH <= 0) minH = 28;
        int desiredH = Max(minH, textH);
        if (desiredH != GetHeight())
            SetHeight(desiredH);
    }
}

void MultiLineEdit::OnClickBegin(const IntVector2& position, const IntVector2& screenPosition,
    MouseButton button, MouseButtonFlags buttons, QualifierFlags qualifiers, Cursor* cursor)
{
    if (button == MOUSEB_LEFT)
    {
        i32 pos = GetCharIndex(position);
        if (pos != NINDEX)
        {
            SetCursorPosition(pos);
            textElement_->ClearSelection();
        }
    }
}

void MultiLineEdit::OnDoubleClick(const IntVector2& position, const IntVector2& screenPosition,
    MouseButton button, MouseButtonFlags buttons, QualifierFlags qualifiers, Cursor* cursor)
{
    if (button != MOUSEB_LEFT)
        return;

    // Double-click: select word under cursor
    i32 pos = GetCharIndex(position);
    if (pos == NINDEX)
        return;

    const String& text = text_;
    i32 wordStart = pos, wordEnd = pos;

    while (wordStart > 0)
    {
        char c = text[wordStart - 1];
        if (isalpha(c) || isdigit(c) || c == '_' || c == '-')
            --wordStart;
        else
            break;
    }
    while (wordEnd < (i32)text.Length())
    {
        char c = text[wordEnd];
        if (isalpha(c) || isdigit(c) || c == '_' || c == '-')
            ++wordEnd;
        else
            break;
    }

    if (wordEnd > wordStart)
    {
        // Toggle: if same word is already selected, deselect
        if (textElement_->GetSelectionStart() == wordStart &&
            textElement_->GetSelectionLength() == wordEnd - wordStart)
            textElement_->ClearSelection();
        else
            textElement_->SetSelection(wordStart, wordEnd - wordStart);
    }
}

void MultiLineEdit::OnTripleClick(const IntVector2& position, const IntVector2& screenPosition,
    MouseButton button, MouseButtonFlags buttons, QualifierFlags qualifiers, Cursor* cursor)
{
    if (button != MOUSEB_LEFT)
        return;

    // Triple-click: select/deselect entire line under cursor
    i32 pos = GetCharIndex(position);
    if (pos == NINDEX)
        return;

    const String& text = text_;
    i32 lineStart = pos, lineEnd = pos;

    while (lineStart > 0 && text[lineStart - 1] != '\n')
        --lineStart;
    while (lineEnd < (i32)text.Length() && text[lineEnd] != '\n')
        ++lineEnd;

    // Toggle: if same line is already selected, deselect
    if (textElement_->GetSelectionStart() == lineStart &&
        textElement_->GetSelectionLength() == lineEnd - lineStart)
        textElement_->ClearSelection();
    else
        textElement_->SetSelection(lineStart, lineEnd - lineStart);
}

void MultiLineEdit::OnDragBegin(const IntVector2& position, const IntVector2& screenPosition,
    MouseButtonFlags buttons, QualifierFlags qualifiers, Cursor* cursor)
{
    UIElement::OnDragBegin(position, screenPosition, buttons, qualifiers, cursor);
    dragBeginCursor_ = GetCharIndex(position);
}

void MultiLineEdit::OnDragMove(const IntVector2& position, const IntVector2& screenPosition,
    const IntVector2& deltaPos, MouseButtonFlags buttons, QualifierFlags qualifiers, Cursor* cursor)
{
    i32 start = dragBeginCursor_;
    i32 current = GetCharIndex(position);
    if (start != NINDEX && current != NINDEX)
    {
        if (start < current)
            textElement_->SetSelection(start, current - start);
        else
            textElement_->SetSelection(current, start - current);
        SetCursorPosition(current);
    }
}

void MultiLineEdit::OnKey(Key key, MouseButtonFlags buttons, QualifierFlags qualifiers)
{
    bool ctrl = (qualifiers & QUAL_CTRL) != 0;
    bool shift = (qualifiers & QUAL_SHIFT) != 0;

    // Clipboard shortcuts
    if (ctrl)
    {
        switch (key)
        {
        case KEY_C:
        {
            String sel = GetSelectedText();
            if (!sel.Empty())
                GetSubsystem<UI>()->SetClipboardText(sel);
            return;
        }
        case KEY_V:
        {
            if (readOnly_)
                return;
            const String& clip = GetSubsystem<UI>()->GetClipboardText();
            if (!clip.Empty())
                InsertText(clip);
            return;
        }
        case KEY_X:
        {
            String sel = GetSelectedText();
            if (!sel.Empty())
            {
                GetSubsystem<UI>()->SetClipboardText(sel);
                if (!readOnly_)
                {
                    i32 selStart = textElement_->GetSelectionStart();
                    i32 selLen = textElement_->GetSelectionLength();
                    text_ = text_.SubstringUTF8(0, selStart) + text_.SubstringUTF8(selStart + selLen);
                    textElement_->ClearSelection();
                    UpdateText();
                    SetCursorPosition(selStart);
                }
            }
            return;
        }
        case KEY_A:
            textElement_->SetSelection(0, text_.LengthUTF8());
            SetCursorPosition(text_.LengthUTF8());
            return;
        default:
            return;
        }
    }

    switch (key)
    {
    case KEY_RETURN:
    case KEY_RETURN2:
    case KEY_KP_ENTER:
        return;

    case KEY_LEFT:
        if (shift && !textElement_->GetSelectionLength())
            dragBeginCursor_ = cursorPosition_;
        if (cursorPosition_ > 0)
        {
            if (textElement_->GetSelectionLength() && !shift)
                SetCursorPosition(textElement_->GetSelectionStart());
            else
                SetCursorPosition(cursorPosition_ - 1);
        }
        if (shift)
        {
            i32 start = dragBeginCursor_;
            i32 current = cursorPosition_;
            if (start < current)
                textElement_->SetSelection(start, current - start);
            else
                textElement_->SetSelection(current, start - current);
        }
        else
            textElement_->ClearSelection();
        break;

    case KEY_RIGHT:
        if (shift && !textElement_->GetSelectionLength())
            dragBeginCursor_ = cursorPosition_;
        if (cursorPosition_ < text_.LengthUTF8())
        {
            if (textElement_->GetSelectionLength() && !shift)
                SetCursorPosition(textElement_->GetSelectionStart() + textElement_->GetSelectionLength());
            else
                SetCursorPosition(cursorPosition_ + 1);
        }
        if (shift)
        {
            i32 start = dragBeginCursor_;
            i32 current = cursorPosition_;
            if (start < current)
                textElement_->SetSelection(start, current - start);
            else
                textElement_->SetSelection(current, start - current);
        }
        else
            textElement_->ClearSelection();
        break;

    case KEY_UP:
    {
        if (shift && !textElement_->GetSelectionLength())
            dragBeginCursor_ = cursorPosition_;
        Vector2 curPos = textElement_->GetCharPosition(cursorPosition_);
        int rowHeight = textElement_->GetRowHeight();
        if (rowHeight > 0)
        {
            IntVector2 above(static_cast<int>(curPos.x_), static_cast<int>(curPos.y_) - rowHeight);
            IntVector2 screenPos = textElement_->ElementToScreen(above);
            IntVector2 localPos = ScreenToElement(screenPos);
            i32 idx = GetCharIndex(localPos);
            if (idx != NINDEX)
                SetCursorPosition(idx);
        }
        if (shift)
        {
            i32 start = dragBeginCursor_;
            i32 current = cursorPosition_;
            if (start < current)
                textElement_->SetSelection(start, current - start);
            else
                textElement_->SetSelection(current, start - current);
        }
        else
            textElement_->ClearSelection();
        break;
    }

    case KEY_DOWN:
    {
        if (shift && !textElement_->GetSelectionLength())
            dragBeginCursor_ = cursorPosition_;
        Vector2 curPos = textElement_->GetCharPosition(cursorPosition_);
        int rowHeight = textElement_->GetRowHeight();
        if (rowHeight > 0)
        {
            IntVector2 below(static_cast<int>(curPos.x_), static_cast<int>(curPos.y_) + rowHeight);
            IntVector2 screenPos = textElement_->ElementToScreen(below);
            IntVector2 localPos = ScreenToElement(screenPos);
            i32 idx = GetCharIndex(localPos);
            if (idx != NINDEX)
                SetCursorPosition(idx);
        }
        if (shift)
        {
            i32 start = dragBeginCursor_;
            i32 current = cursorPosition_;
            if (start < current)
                textElement_->SetSelection(start, current - start);
            else
                textElement_->SetSelection(current, start - current);
        }
        else
            textElement_->ClearSelection();
        break;
    }

    case KEY_HOME:
        if (shift && !textElement_->GetSelectionLength())
            dragBeginCursor_ = cursorPosition_;
        SetCursorPosition(0);
        if (shift)
        {
            i32 start = dragBeginCursor_;
            i32 current = cursorPosition_;
            if (start < current)
                textElement_->SetSelection(start, current - start);
            else
                textElement_->SetSelection(current, start - current);
        }
        else
            textElement_->ClearSelection();
        break;

    case KEY_END:
        if (shift && !textElement_->GetSelectionLength())
            dragBeginCursor_ = cursorPosition_;
        SetCursorPosition(text_.LengthUTF8());
        if (shift)
        {
            i32 start = dragBeginCursor_;
            i32 current = cursorPosition_;
            if (start < current)
                textElement_->SetSelection(start, current - start);
            else
                textElement_->SetSelection(current, start - current);
        }
        else
            textElement_->ClearSelection();
        break;

    case KEY_BACKSPACE:
        if (!readOnly_)
        {
            i32 selStart = textElement_->GetSelectionStart();
            i32 selLen = textElement_->GetSelectionLength();
            if (selLen > 0)
            {
                text_ = text_.SubstringUTF8(0, selStart) + text_.SubstringUTF8(selStart + selLen);
                textElement_->ClearSelection();
                UpdateText();
                SetCursorPosition(selStart);
            }
            else if (cursorPosition_ > 0)
            {
                i32 pos = cursorPosition_ - 1;
                text_ = text_.SubstringUTF8(0, pos) + text_.SubstringUTF8(pos + 1);
                UpdateText();
                SetCursorPosition(pos);
            }
        }
        break;

    case KEY_DELETE:
        if (!readOnly_)
        {
            i32 selStart = textElement_->GetSelectionStart();
            i32 selLen = textElement_->GetSelectionLength();
            if (selLen > 0)
            {
                text_ = text_.SubstringUTF8(0, selStart) + text_.SubstringUTF8(selStart + selLen);
                textElement_->ClearSelection();
                UpdateText();
                SetCursorPosition(selStart);
            }
            else if (cursorPosition_ < text_.LengthUTF8())
            {
                text_ = text_.SubstringUTF8(0, cursorPosition_) + text_.SubstringUTF8(cursorPosition_ + 1);
                UpdateText();
            }
        }
        break;

    default:
        break;
    }
}

void MultiLineEdit::OnTextInput(const String& input)
{
    if (readOnly_ || input.Empty())
        return;

    i32 selStart = textElement_->GetSelectionStart();
    i32 selLen = textElement_->GetSelectionLength();
    if (selLen > 0)
    {
        text_ = text_.SubstringUTF8(0, selStart) + input + text_.SubstringUTF8(selStart + selLen);
        textElement_->ClearSelection();
        UpdateText();
        SetCursorPosition(selStart + input.LengthUTF8());
    }
    else
    {
        i32 pos = cursorPosition_;
        text_ = text_.SubstringUTF8(0, pos) + input + text_.SubstringUTF8(pos);
        UpdateText();
        SetCursorPosition(pos + input.LengthUTF8());
    }
}

void MultiLineEdit::InsertNewline()
{
    if (readOnly_)
        return;

    i32 selStart = textElement_->GetSelectionStart();
    i32 selLen = textElement_->GetSelectionLength();
    if (selLen > 0)
    {
        text_ = text_.SubstringUTF8(0, selStart) + "\n" + text_.SubstringUTF8(selStart + selLen);
        textElement_->ClearSelection();
        UpdateText();
        SetCursorPosition(selStart + 1);
    }
    else
    {
        i32 pos = cursorPosition_;
        text_ = text_.SubstringUTF8(0, pos) + "\n" + text_.SubstringUTF8(pos);
        UpdateText();
        SetCursorPosition(pos + 1);
    }
}

void MultiLineEdit::InsertText(const String& input)
{
    if (readOnly_ || input.Empty())
        return;

    i32 selStart = textElement_->GetSelectionStart();
    i32 selLen = textElement_->GetSelectionLength();
    if (selLen > 0)
    {
        text_ = text_.SubstringUTF8(0, selStart) + input + text_.SubstringUTF8(selStart + selLen);
        textElement_->ClearSelection();
        UpdateText();
        SetCursorPosition(selStart + input.LengthUTF8());
    }
    else
    {
        i32 pos = cursorPosition_;
        text_ = text_.SubstringUTF8(0, pos) + input + text_.SubstringUTF8(pos);
        UpdateText();
        SetCursorPosition(pos + input.LengthUTF8());
    }
}

void MultiLineEdit::SetText(const String& text)
{
    text_ = text;
    UpdateText();
    SetCursorPosition(text_.LengthUTF8());
}

void MultiLineEdit::AppendLine(const String& line)
{
    if (!text_.Empty() && !text_.EndsWith("\n"))
        text_ += "\n";
    text_ += line;
    numLines_ = CountLines(text_);

    if (maxLines_ > 0 && numLines_ > maxLines_)
    {
        while (numLines_ > maxLines_)
        {
            unsigned pos = text_.Find('\n');
            if (pos != String::NPOS)
            {
                text_ = text_.Substring(pos + 1);
                --numLines_;
            }
            else
                break;
        }
    }

    UpdateText();

    if (autoScroll_)
        SetCursorPosition(text_.LengthUTF8());
}

void MultiLineEdit::OnResize(const IntVector2& newSize, const IntVector2& delta)
{
    if (textElement_)
    {
        int padding = 4;
        int textWidth = Max(newSize.x_ - padding * 2, 0);
        textElement_->SetPosition(padding, padding);
        textElement_->SetFixedWidth(textWidth);
    }
}

void MultiLineEdit::SetWordWrap(bool enable)
{
    if (textElement_)
        textElement_->SetWordwrap(enable);
}

void MultiLineEdit::SetCursorPosition(i32 position)
{
    i32 len = text_.LengthUTF8();
    if (position > len)
        position = len;
    if (position < 0)
        position = 0;

    cursorPosition_ = position;
    UpdateCursor();
}

String MultiLineEdit::GetSelectedText() const
{
    if (!textElement_)
        return String::EMPTY;

    i32 start = textElement_->GetSelectionStart();
    i32 length = textElement_->GetSelectionLength();
    if (length <= 0)
        return String::EMPTY;

    return text_.SubstringUTF8(start, length);
}

i32 MultiLineEdit::GetCharIndex(const IntVector2& position)
{
    if (!textElement_)
        return NINDEX;

    IntVector2 screenPosition = ElementToScreen(position);
    IntVector2 textPosition = textElement_->ScreenToElement(screenPosition);

    i32 numChars = textElement_->GetNumChars();
    if (numChars == 0)
        return 0;

    i32 bestIndex = 0;
    float bestDist = M_INFINITY;

    for (i32 i = 0; i <= numChars; ++i)
    {
        Vector2 charPos = textElement_->GetCharPosition(i);
        float dx = charPos.x_ - textPosition.x_;
        float dy = charPos.y_ - textPosition.y_;
        float dist = dx * dx + dy * dy;
        if (dist < bestDist)
        {
            bestDist = dist;
            bestIndex = i;
        }
    }

    return bestIndex;
}

void MultiLineEdit::UpdateCursor()
{
    if (!textElement_ || !cursor_)
        return;

    Vector2 charPos = textElement_->GetCharPosition(cursorPosition_);
    int rowHeight = textElement_->GetRowHeight();

    IntVector2 textPos = textElement_->GetPosition();
    cursor_->SetPosition(textPos.x_ + static_cast<int>(charPos.x_),
                         textPos.y_ + static_cast<int>(charPos.y_));
    cursor_->SetFixedSize(2, rowHeight);

    cursorBlinkTimer_ = 0.0f;
}

void MultiLineEdit::UpdateText()
{
    if (!textElement_)
        return;

    textElement_->SetText(text_);
    numLines_ = CountLines(text_);
}

unsigned MultiLineEdit::CountLines(const String& text)
{
    if (text.Empty())
        return 0;

    unsigned count = 1;
    for (unsigned i = 0; i < text.Length(); ++i)
    {
        if (text[i] == '\n')
            ++count;
    }
    return count;
}

bool MultiLineEdit::FilterImplicitAttributes(XMLElement& dest) const
{
    if (!BorderImage::FilterImplicitAttributes(dest))
        return false;

    XMLElement childElem = dest.GetChild("element");
    if (!childElem)
        return true;
    if (!RemoveChildXML(childElem, "Name", "MLE_Text"))
        return false;
    if (!RemoveChildXML(childElem, "Position"))
        return false;
    if (!RemoveChildXML(childElem, "Size"))
        return false;

    childElem = childElem.GetNext("element");
    if (!childElem)
        return true;
    if (!RemoveChildXML(childElem, "Name", "MLE_Cursor"))
        return false;

    return true;
}

void MultiLineEdit::HandleFocused(StringHash /*eventType*/, VariantMap& eventData)
{
    UpdateCursor();
}

void MultiLineEdit::HandleDefocused(StringHash /*eventType*/, VariantMap& eventData)
{
    cursor_->SetVisible(false);
}

} // namespace Urho3D

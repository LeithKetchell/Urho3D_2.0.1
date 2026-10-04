// MultiLineEdit — multi-line text widget with selection and clipboard support.
// Read-only mode for log viewers, editable mode for text editors.
// Inherits BorderImage (same pattern as LineEdit).

#pragma once

#include "../UI/BorderImage.h"
#include "../UI/ScrollView.h"

namespace Urho3D
{

class Font;
class Text;

class URHO3D_API MultiLineEdit : public BorderImage
{
    URHO3D_OBJECT(MultiLineEdit, BorderImage);

public:
    explicit MultiLineEdit(Context* context);
    ~MultiLineEdit() override;

    static void RegisterObject(Context* context);

    void ApplyAttributes() override;
    void Update(float timeStep) override;
    void OnClickBegin(const IntVector2& position, const IntVector2& screenPosition, MouseButton button,
        MouseButtonFlags buttons, QualifierFlags qualifiers, Cursor* cursor) override;
    void OnDoubleClick(const IntVector2& position, const IntVector2& screenPosition, MouseButton button,
        MouseButtonFlags buttons, QualifierFlags qualifiers, Cursor* cursor) override;
    void OnTripleClick(const IntVector2& position, const IntVector2& screenPosition, MouseButton button,
        MouseButtonFlags buttons, QualifierFlags qualifiers, Cursor* cursor) override;
    void OnDragBegin(const IntVector2& position, const IntVector2& screenPosition,
        MouseButtonFlags buttons, QualifierFlags qualifiers, Cursor* cursor) override;
    void OnDragMove(const IntVector2& position, const IntVector2& screenPosition, const IntVector2& deltaPos,
        MouseButtonFlags buttons, QualifierFlags qualifiers, Cursor* cursor) override;
    void OnKey(Key key, MouseButtonFlags buttons, QualifierFlags qualifiers) override;
    void OnTextInput(const String& text) override;
    void OnResize(const IntVector2& newSize, const IntVector2& delta) override;

    /// Set text content.
    void SetText(const String& text);
    /// Insert a newline at the cursor position. Application calls this explicitly (e.g. Shift+Enter).
    void InsertNewline();
    /// Insert arbitrary text at the cursor position.
    void InsertText(const String& input);
    /// Append text (for log use — trims from top if over max lines).
    void AppendLine(const String& line);
    /// Set read-only mode (no editing, selection+copy still works).
    void SetReadOnly(bool readOnly) { readOnly_ = readOnly; }
    /// Set word wrap.
    void SetWordWrap(bool enable);
    /// Set maximum number of lines (0 = unlimited). Oldest lines trimmed when exceeded.
    void SetMaxLines(unsigned maxLines) { maxLines_ = maxLines; }
    /// Set cursor position as character index.
    void SetCursorPosition(i32 position);
    /// Set cursor blink rate. 0 disables blinking.
    void SetCursorBlinkRate(float rate) { cursorBlinkRate_ = rate; }

    /// Return text content.
    const String& GetText() const { return text_; }
    /// Return selected text substring.
    String GetSelectedText() const;
    /// Return whether read-only.
    bool IsReadOnly() const { return readOnly_; }
    /// Return cursor position.
    i32 GetCursorPosition() const { return cursorPosition_; }
    /// Return text element.
    Text* GetTextElement() const { return textElement_; }
    /// Return cursor element.
    BorderImage* GetCursorElement() const { return cursor_; }
    /// Return number of lines.
    unsigned GetNumLines() const { return numLines_; }

protected:
    bool FilterImplicitAttributes(XMLElement& dest) const override;

private:
    /// Get character index nearest to element-space position.
    i32 GetCharIndex(const IntVector2& position);
    /// Update cursor visual position and blink.
    void UpdateCursor();
    /// Update the Text element content and line count.
    void UpdateText();
    /// Count lines in text.
    static unsigned CountLines(const String& text);

    void HandleFocused(StringHash eventType, VariantMap& eventData);
    void HandleDefocused(StringHash eventType, VariantMap& eventData);

    /// Text display element (child).
    SharedPtr<Text> textElement_;
    /// Cursor element (child).
    SharedPtr<BorderImage> cursor_;
    /// Raw text content.
    String text_;
    /// Cursor position (character index).
    i32 cursorPosition_{};
    /// Drag-start cursor position.
    i32 dragBeginCursor_{};
    /// Cursor blink rate.
    float cursorBlinkRate_{1.0f};
    /// Cursor blink timer.
    float cursorBlinkTimer_{};
    /// Maximum number of lines (0 = unlimited).
    unsigned maxLines_{};
    /// Current line count.
    unsigned numLines_{};
    /// Read-only flag.
    bool readOnly_{false};
    /// Auto-scroll to bottom on append.
    bool autoScroll_{true};
};

} // namespace Urho3D

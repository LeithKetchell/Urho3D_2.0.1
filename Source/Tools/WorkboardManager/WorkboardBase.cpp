// WorkboardBase — shared implementation for WorkboardManager and WorkboardClient.

#include "WorkboardBase.h"

#include <Urho3D/Container/Sort.h>
#include <Urho3D/IO/FileSystem.h>
#include <Urho3D/UI/BorderImage.h>
#include <Urho3D/UI/ScrollBar.h>
#include <Urho3D/UI/UIEvents.h>

WorkboardBase::WorkboardBase(Context* context) : Application(context) {}

// ============================================================================
// Helpers
// ============================================================================

String WorkboardBase::GetProjectRoot()
{
    auto* fs = GetSubsystem<FileSystem>();

    // CWD-based: walk upward looking for .claude/ marker
    String cwd = fs->GetCurrentDir();
    String walk = cwd;
    while (!walk.Empty() && walk != "/")
    {
        if (fs->DirExists(walk + ".claude/hooks"))
            return AddTrailingSlash(walk);
        // Strip trailing slash then go up one level
        if (walk.EndsWith("/"))
            walk = walk.Substring(0, walk.Length() - 1);
        unsigned pos = walk.FindLast('/');
        if (pos == String::NPOS)
            break;
        walk = walk.Substring(0, pos + 1);
    }

    // Fallback: assume binary lives in build/bin/
    String programDir = fs->GetProgramDir();
    if (programDir.EndsWith("/"))
        programDir = programDir.Substring(0, programDir.Length() - 1);
    unsigned pos = programDir.FindLast('/');
    if (pos != String::NPOS)
        programDir = programDir.Substring(0, pos);
    pos = programDir.FindLast('/');
    if (pos != String::NPOS)
        programDir = programDir.Substring(0, pos);
    return programDir + "/";
}

String WorkboardBase::GetClaudeDir()
{
    return projectRoot_ + "Claude/";
}

// ============================================================================
// UI Creation
// ============================================================================

void WorkboardBase::CreateWorkboardPanel(UIElement* parent, float minX, float minY, float maxX, float maxY)
{
    workboardPanel_ = parent->CreateChild<Window>("WorkboardPanel");
    workboardPanel_->SetStyle("Window");
    workboardPanel_->SetOpacity(0.6f);
    workboardPanel_->SetEnableAnchor(true);
    workboardPanel_->SetMinAnchor(minX, minY);
    workboardPanel_->SetMaxAnchor(maxX, maxY);
    workboardPanel_->SetMovable(false);
    workboardPanel_->SetResizable(false);
    workboardPanel_->SetLayout(LM_VERTICAL, 2, IntRect(4, 4, 4, 4));

    auto* titleText = workboardPanel_->CreateChild<Text>("WBTitle");
    titleText->SetFont(font_, fontSize_ + 3);
    titleText->SetText("WORKBOARD");
    titleText->SetColor(Color(1.0f, 0.9f, 0.5f));

    workboardScroll_ = workboardPanel_->CreateChild<ScrollView>("WBScroll");
    workboardScroll_->SetStyleAuto();

    workboardContent_ = new UIElement(context_);
    workboardContent_->SetLayout(LM_VERTICAL, 2);
    workboardScroll_->SetContentElement(workboardContent_);
}

void WorkboardBase::CreatePlanPanel(UIElement* parent, float minX, float minY, float maxX, float maxY)
{
    planPanel_ = parent->CreateChild<Window>("PlanPanel");
    planPanel_->SetStyle("Window");
    planPanel_->SetOpacity(0.6f);
    planPanel_->SetEnableAnchor(true);
    planPanel_->SetMinAnchor(minX, minY);
    planPanel_->SetMaxAnchor(maxX, maxY);
    planPanel_->SetMovable(false);
    planPanel_->SetResizable(false);
    planPanel_->SetLayout(LM_VERTICAL, 2, IntRect(4, 4, 4, 4));

    auto* titleText = planPanel_->CreateChild<Text>("PlanTitle");
    titleText->SetFont(font_, fontSize_ + 3);
    titleText->SetText("PLANS");
    titleText->SetColor(Color(0.5f, 0.8f, 1.0f));

    // Plan list (top ~30%)
    planListView_ = planPanel_->CreateChild<ListView>("PlanList");
    planListView_->SetStyleAuto();
    planListView_->SetMinHeight(120);
    planListView_->SetMaxHeight(180);
    SubscribeToEvent(planListView_, "ItemClicked", URHO3D_HANDLER(WorkboardBase, HandlePlanSelected));

    // Plan content (bottom ~70%)
    planContentScroll_ = planPanel_->CreateChild<ScrollView>("PlanScroll");
    planContentScroll_->SetStyleAuto();
    planContentScroll_->SetClipChildren(true);
    planContentScroll_->SetScrollBarsAutoVisible(true);
    auto* hBar = planContentScroll_->GetHorizontalScrollBar();
    if (hBar)
        hBar->SetVisible(false);

    planContentText_ = new Text(context_);
    planContentText_->SetFont(font_, fontSize_);
    planContentText_->SetColor(Color(0.85f, 0.85f, 0.85f));
    planContentText_->SetWordwrap(true);
    planContentText_->SetText("Select a plan file to view its contents.");
    planContentScroll_->SetContentElement(planContentText_);
}

// ============================================================================
// Workboard Parsing & Rendering
// ============================================================================

void WorkboardBase::ParseWorkboard(const String& content)
{
    sections_.Clear();

    Vector<String> lines = content.Split('\n');

    WorkboardSection currentSection;
    bool inSection = false;
    bool headersParsed = false;

    for (unsigned i = 0; i < lines.Size(); ++i)
    {
        String line = lines[i].Trimmed();

        if (line.StartsWith("## "))
        {
            if (inSection && !currentSection.headers.Empty())
                sections_.Push(currentSection);

            currentSection = WorkboardSection();
            currentSection.title = line.Substring(3).Trimmed();
            inSection = true;
            headersParsed = false;
            continue;
        }

        if (!inSection)
            continue;

        if (line.StartsWith("|") && line.EndsWith("|"))
        {
            if (line.Contains("---"))
                continue;

            Vector<String> cells;
            Vector<String> parts = line.Split('|');
            for (unsigned j = 0; j < parts.Size(); ++j)
            {
                String cell = parts[j].Trimmed();
                if (!cell.Empty())
                    cells.Push(cell);
            }

            if (!headersParsed)
            {
                currentSection.headers = cells;
                headersParsed = true;
            }
            else
            {
                WorkboardRow row;
                row.cells = cells;
                currentSection.rows.Push(row);
            }
        }
    }

    if (inSection && !currentSection.headers.Empty())
        sections_.Push(currentSection);
}

void WorkboardBase::RenderWorkboardUI()
{
    if (!workboardContent_)
        return;

    // Save scroll position before rebuild
    IntVector2 savedScroll = workboardScroll_ ? workboardScroll_->GetViewPosition() : IntVector2::ZERO;

    // Update content width to match current panel size
    if (workboardPanel_)
        workboardContent_->SetFixedWidth(workboardPanel_->GetWidth() - 20);

    workboardContent_->RemoveAllChildren();

    for (unsigned i = 0; i < sections_.Size(); ++i)
        AddSectionToUI(sections_[i]);

    // Restore scroll position after rebuild
    if (workboardScroll_)
        workboardScroll_->SetViewPosition(savedScroll);
}

void WorkboardBase::AddSectionToUI(const WorkboardSection& section)
{
    // VIBGYOR (reverse rainbow) evenly spaced top-to-bottom, Morgue is grey
    Color titleColor(0.6f, 0.6f, 0.6f);  // default grey
    if (section.title.Contains("Team"))
        titleColor = Color(0.58f, 0.0f, 0.83f);  // Violet        (hue 270)
    else if (section.title.Contains("Rules"))
        titleColor = Color(0.29f, 0.0f, 0.93f);  // Indigo        (hue 251)
    else if (section.title.Contains("Planned"))
        titleColor = Color(0.0f, 0.45f, 1.0f);   // Blue          (hue 213)
    else if (section.title.Contains("Ready"))
        titleColor = Color(0.0f, 0.85f, 0.45f);  // Green         (hue 152)
    else if (section.title.Contains("In Progress"))
        titleColor = Color(0.85f, 0.85f, 0.0f);  // Yellow        (hue  60)
    else if (section.title.Contains("Coder Status"))
        titleColor = Color(1.0f, 0.65f, 0.0f);   // Amber-Orange  (hue  39)
    else if (section.title.Contains("Unverified"))
        titleColor = Color(1.0f, 0.75f, 0.0f);   // Gold          (hue  45)
    else if (section.title.Contains("Done"))
        titleColor = Color(1.0f, 0.40f, 0.0f);   // Orange        (hue  24)
    else if (section.title.Contains("Archive"))
        titleColor = Color(1.0f, 0.20f, 0.0f);   // Red-Orange    (hue  12)
    else if (section.title.Contains("Morgue"))
        titleColor = Color(0.5f, 0.5f, 0.5f);    // Grey (dead)
    else if (section.title.Contains("Shared"))
        titleColor = Color(0.9f, 0.0f, 0.0f);    // Red           (hue   0)

    // Clickable section title
    auto* titleText = workboardContent_->CreateChild<Text>();
    titleText->SetFont(font_, fontSize_ + 2);
    titleText->SetText("> " + section.title);
    titleText->SetColor(titleColor);
    titleText->SetEnabled(true);

    // Content container — collapsed by default
    auto* content = workboardContent_->CreateChild<UIElement>();
    content->SetLayout(LM_VERTICAL, 2);
    content->SetVisible(false);

    titleText->SetVar("SectionContent", content);
    SubscribeToEvent(titleText, E_CLICK, URHO3D_HANDLER(WorkboardBase, HandleSectionToggle));

    // Determine available width for columns
    int totalW = workboardContent_->GetWidth();
    if (totalW < 100) totalW = 900;

    unsigned numCols = section.headers.Size();
    if (numCols == 0)
    {
        for (unsigned r = 0; r < section.rows.Size(); ++r)
        {
            const WorkboardRow& row = section.rows[r];
            String rowLine;
            for (unsigned c = 0; c < row.cells.Size(); ++c)
            {
                if (c > 0) rowLine += "  |  ";
                rowLine += row.cells[c];
            }
            auto* rowText = content->CreateChild<Text>();
            rowText->SetFont(font_, fontSize_ - 1);
            rowText->SetText(rowLine);
            rowText->SetColor(Color(0.8f, 0.8f, 0.8f));
            rowText->SetWordwrap(true);
        }
        return;
    }

    // Build column widths — smart sizing for known headers
    Vector<int> colWidths;
    int usedW = 0;
    int flexCount = 0;
    colWidths.Resize(numCols);

    for (unsigned h = 0; h < numCols; ++h)
    {
        String hdr = section.headers[h].ToLower().Trimmed();
        if (hdr == "pri")
            colWidths[h] = 30;
        else if (hdr == "owner")
            colWidths[h] = 60;
        else if (hdr == "started")
            colWidths[h] = 80;
        else if (hdr == "blocked by" || hdr == "outcome" || hdr == "learned")
            colWidths[h] = 80;
        else
        {
            colWidths[h] = 0;
            ++flexCount;
        }
        usedW += colWidths[h];
    }

    int remaining = totalW - usedW - 4;
    if (flexCount > 0 && remaining > 0)
    {
        int perFlex = remaining / flexCount;
        for (unsigned h = 0; h < numCols; ++h)
        {
            if (colWidths[h] == 0)
                colWidths[h] = perFlex;
        }
    }

    // Column header row
    auto* headerRow = content->CreateChild<UIElement>("HeaderRow");
    headerRow->SetLayout(LM_HORIZONTAL, 2);
    headerRow->SetFixedHeight(fontSize_ + 6);
    Color headerColor = titleColor * 0.7f + Color(0.3f, 0.3f, 0.3f);
    for (unsigned h = 0; h < numCols; ++h)
    {
        auto* cell = headerRow->CreateChild<Text>();
        cell->SetFont(font_, fontSize_ - 2);
        cell->SetText(section.headers[h]);
        cell->SetColor(headerColor);
        cell->SetFixedWidth(colWidths[h]);
    }

    // Identify which columns are "detail" (notes, summary, file) — these go in the dropdown
    Vector<bool> isDetail;
    isDetail.Resize(numCols, false);
    for (unsigned h = 0; h < numCols; ++h)
    {
        String hdr = section.headers[h].ToLower().Trimmed();
        if (hdr == "notes" || hdr == "summary" || hdr == "file" || hdr == "description")
            isDetail[h] = true;
    }

    // ROYGBIV row colors — cycle through rainbow for each data row
    static const Color roygbiv[] = {
        Color(1.0f, 0.4f, 0.4f),   // Red
        Color(1.0f, 0.65f, 0.25f), // Orange
        Color(1.0f, 0.95f, 0.35f), // Yellow
        Color(0.4f, 1.0f, 0.55f),  // Green
        Color(0.4f, 0.65f, 1.0f),  // Blue
        Color(0.55f, 0.35f, 0.9f), // Indigo
        Color(0.8f, 0.45f, 1.0f),  // Violet
    };

    // Data rows — title line is clickable, detail columns hidden in dropdown
    for (unsigned r = 0; r < section.rows.Size(); ++r)
    {
        const WorkboardRow& row = section.rows[r];
        Color rowColor = roygbiv[r % 7];

        // Collect detail text from hidden columns
        String detailText;
        for (unsigned c = 0; c < row.cells.Size() && c < numCols; ++c)
        {
            if (isDetail[c])
            {
                String val = row.cells[c].Trimmed();
                if (!val.Empty() && val != "\xe2\x80\x94" && val != "--")  // skip em-dash and double-dash placeholders
                {
                    if (!detailText.Empty()) detailText += "\n";
                    detailText += section.headers[c] + ": " + val;
                }
            }
        }
        bool hasDetail = !detailText.Empty();

        // Title row — shows non-detail columns inline
        auto* rowElem = content->CreateChild<UIElement>("DataRow");
        rowElem->SetLayout(LM_HORIZONTAL, 2);
        if (hasDetail)
            rowElem->SetEnabled(true);  // clickable

        for (unsigned c = 0; c < row.cells.Size() && c < numCols; ++c)
        {
            if (isDetail[c])
                continue;  // hidden — goes in dropdown

            auto* cell = rowElem->CreateChild<Text>();
            cell->SetFont(font_, fontSize_ - 1);
            cell->SetText(row.cells[c].Trimmed());
            cell->SetFixedWidth(colWidths[c]);
            cell->SetWordwrap(true);

            // Color blocked_by red if non-empty
            String hdr = section.headers[c].ToLower().Trimmed();
            if (hdr == "blocked by" && !row.cells[c].Trimmed().Empty())
                cell->SetColor(Color(1.0f, 0.4f, 0.3f));
            else
            {
                cell->SetColor(rowColor);
            }
        }

        // Detail dropdown — hidden by default, toggled on click
        if (hasDetail)
        {
            auto* detailElem = content->CreateChild<UIElement>("DetailDrop");
            detailElem->SetLayout(LM_VERTICAL, 1, IntRect(20, 2, 4, 2));
            detailElem->SetVisible(false);

            auto* detailLabel = detailElem->CreateChild<Text>();
            detailLabel->SetFont(font_, fontSize_ - 2);
            detailLabel->SetText(detailText);
            detailLabel->SetColor(Color(0.65f, 0.65f, 0.65f));
            detailLabel->SetWordwrap(true);

            rowElem->SetVar("SectionContent", detailElem);
            SubscribeToEvent(rowElem, E_CLICK, URHO3D_HANDLER(WorkboardBase, HandleSectionToggle));
        }
    }

    // Separator
    auto* sep = workboardContent_->CreateChild<BorderImage>();
    sep->SetFixedHeight(1);
    sep->SetColor(titleColor * 0.3f);
}

// ============================================================================
// Event Handlers
// ============================================================================

void WorkboardBase::HandleSectionToggle(StringHash /*eventType*/, VariantMap& eventData)
{
    using namespace Click;
    auto* titleText = static_cast<Text*>(eventData[P_ELEMENT].GetPtr());
    if (!titleText) return;

    auto* content = static_cast<UIElement*>(titleText->GetVar("SectionContent").GetPtr());
    if (!content) return;

    bool wasVisible = content->IsVisible();
    content->SetVisible(!wasVisible);

    // Toggle prefix: "v " (expanded) / "> " (collapsed)
    String text = titleText->GetText();
    if (wasVisible && text.StartsWith("v "))
        titleText->SetText("> " + text.Substring(2));
    else if (!wasVisible && text.StartsWith("> "))
        titleText->SetText("v " + text.Substring(2));

    // Force parent reflow
    workboardContent_->SetHeight(0);
    workboardContent_->UpdateLayout();
}

void WorkboardBase::HandlePlanSelected(StringHash /*eventType*/, VariantMap& eventData)
{
    using namespace ItemClicked;
    int index = eventData[P_SELECTION].GetI32();
    if (index >= 0 && (unsigned)index < planFiles_.Size())
        OnPlanSelected(planFiles_[index]);
}

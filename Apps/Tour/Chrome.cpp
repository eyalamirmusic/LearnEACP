#include "Chrome.h"

#include <ECodeWidgets/UIText.h>

using namespace eacp;
using namespace eacp::Graphics;

namespace Tour
{
namespace
{
constexpr auto buttonGap = 6.f;
constexpr auto groupGap = 18.f;
constexpr auto titleHeight = 34.f;

constexpr auto shortcuts = "\xe2\x8c\x98"
                           "1-4 layout   \xe2\x8c\x98[ \xe2\x8c\x98] chapter   "
                           "\xe2\x8c\x98"
                           "B chapters   \xe2\x8c\x98R reload";
} // namespace

// --- Button -----------------------------------------------------------------

Button::Button(std::string labelToShow, bool latches)
    : label(std::move(labelToShow))
    , latching(latches)
{
}

void Button::setOn(bool value)
{
    if (on == value)
        return;

    on = value;
    repaint();
}

void Button::mouseDown(const MouseEvent&)
{
    pressed = true;
    hovered = true;
    repaint();
}

void Button::mouseUp(const MouseEvent& event)
{
    pressed = false;
    hovered = bounds().contains(event.pos);
    repaint();

    // A press dragged off the button before release is a press abandoned.
    if (hovered)
        onClick();
}

void Button::mouseMoved(const MouseEvent&)
{
    if (hovered)
        return;

    hovered = true;
    repaint();
}

void Button::mouseExited()
{
    if (!hovered)
        return;

    hovered = false;
    repaint();
}

void Button::prepare(Text::GlyphAtlas& atlas, const Rect&)
{
    ecode::UIText::prepare(atlas, label);
}

void Button::paint(ecode::PaintContext& context)
{
    const auto area = bounds();
    const auto lit = latching && on;

    auto fill = lit ? Look::selected : Look::button;

    if (hovered)
        fill = fill.brighter(0.06f);

    if (pressed)
        fill = fill.brighter(0.12f);

    context.sprites().fillRect(area, fill);

    const auto width = ecode::UIText::width(context.atlas(), label);
    const auto baseline = ecode::UIText::centredBaseline(context.atlas(), area);

    ecode::UIText::draw(context,
                        label,
                        area.center().x - width / 2.f,
                        baseline,
                        lit || !latching ? Look::text : Look::dimText);
}

// --- Header -----------------------------------------------------------------

Header::Header(Button* actionToShow)
    : action(actionToShow)
{
    if (action != nullptr)
        addChild(*action);
}

void Header::setText(std::string leftText, std::string rightText, Color colour)
{
    left = std::move(leftText);
    right = std::move(rightText);
    rightColour = colour;
    repaint();
}

void Header::layout()
{
    if (action != nullptr)
        action->setBounds(bounds().fromRight(96.f, 4.f).inset(0.f, 3.f));
}

void Header::prepare(Text::GlyphAtlas& atlas, const Rect&)
{
    ecode::UIText::prepare(atlas, left);
    ecode::UIText::prepare(atlas, right);
}

void Header::paint(ecode::PaintContext& context)
{
    const auto area = bounds();

    context.sprites().fillRect(area, Look::panel);

    const auto baseline = ecode::UIText::centredBaseline(context.atlas(), area);

    ecode::UIText::draw(context, left, area.x + Look::margin, baseline, Look::text);

    auto rightEdge = area.right() - Look::margin;

    if (action != nullptr)
        rightEdge -= 104.f;

    const auto width = ecode::UIText::width(context.atlas(), right);
    ecode::UIText::draw(context, right, rightEdge - width, baseline, rightColour);

    context.sprites().fillRect(area.fromBottom(1.f), Look::edge);
}

HeaderView::HeaderView(Button* action)
    : header(action)
{
    setRoot(header);
}

// --- Toolbar ----------------------------------------------------------------

Toolbar::Root::Root(Toolbar& owner)
    : toolbar(owner)
{
    for (auto* button: {&toolbar.sidebar,
                        &toolbar.result,
                        &toolbar.sideBySide,
                        &toolbar.stacked,
                        &toolbar.code,
                        &toolbar.reload})
        addChild(*button);
}

void Toolbar::Root::layout()
{
    auto area = bounds().inset(8.f, 6.f);

    auto place = [&](Button& button, float width, float gapAfter)
    {
        button.setBounds(area.removeFromLeft(width));
        area.removeFromLeft(gapAfter);
    };

    place(toolbar.sidebar, 86.f, groupGap);
    place(toolbar.result, 70.f, buttonGap);
    place(toolbar.sideBySide, 96.f, buttonGap);
    place(toolbar.stacked, 74.f, buttonGap);
    place(toolbar.code, 60.f, groupGap);
    place(toolbar.reload, 70.f, 0.f);
}

void Toolbar::Root::prepare(Text::GlyphAtlas& atlas, const Rect&)
{
    ecode::UIText::prepare(atlas, shortcuts);
}

void Toolbar::Root::paint(ecode::PaintContext& context)
{
    const auto area = bounds();

    context.sprites().fillRect(area, Look::panel);
    context.sprites().fillRect(area.fromBottom(1.f), Look::edge);

    // Only where it fits beside the buttons.
    const auto width = ecode::UIText::width(context.atlas(), shortcuts);
    const auto x = area.right() - width - Look::margin;

    if (x > toolbar.reload.bounds().right() + groupGap)
        ecode::UIText::draw(context,
                            shortcuts,
                            x,
                            ecode::UIText::centredBaseline(context.atlas(), area),
                            Look::dimText);
}

Toolbar::Toolbar()
{
    setRoot(root);
}

void Toolbar::show(LayoutMode mode, bool sidebarShown)
{
    sidebar.setOn(sidebarShown);
    result.setOn(mode == LayoutMode::Result);
    sideBySide.setOn(mode == LayoutMode::SideBySide);
    stacked.setOn(mode == LayoutMode::Stacked);
    code.setOn(mode == LayoutMode::Code);
}

// --- Sidebar ----------------------------------------------------------------

Sidebar::Root::Root(Sidebar& owner)
    : sidebar(owner)
{
    addChild(sidebar.scroller);
}

void Sidebar::Root::layout()
{
    auto area = bounds();
    area.removeFromTop(titleHeight);
    area.removeFromRight(1.f);

    sidebar.scroller.setBounds(area);
}

void Sidebar::Root::prepare(Text::GlyphAtlas& atlas, const Rect&)
{
    ecode::UIText::prepare(atlas, "LearnEACP");
}

void Sidebar::Root::paint(ecode::PaintContext& context)
{
    const auto area = bounds();

    context.sprites().fillRect(area, Look::panel);

    const auto title = area.fromTop(titleHeight);
    ecode::UIText::draw(context,
                        "LearnEACP",
                        title.x + Look::margin,
                        ecode::UIText::centredBaseline(context.atlas(), title),
                        Look::dimText);

    context.sprites().fillRect(area.fromRight(1.f), Look::edge);
}

Sidebar::Sidebar(Vector<std::string> rowsToShow)
    : rows(std::move(rowsToShow))
{
    list.setRowCount((std::size_t) rows.size());
    list.setRowHeight(Look::rowHeight);

    // The list never takes the keyboard; the keys belong to the chapter.
    list.setFocusable(false);

    list.prepareRow = [this](Text::GlyphAtlas& atlas, std::size_t row)
    { ecode::UIText::prepareElided(atlas, rows[(int) row]); };

    list.paintRow =
        [this](
            ecode::PaintContext& context, std::size_t row, const Rect& area, bool on)
    {
        if (on)
            context.sprites().fillRect(area, Look::selected);

        const auto name = ecode::UIText::elide(
            context.atlas(), rows[(int) row], area.w - 2.f * Look::margin);

        ecode::UIText::draw(context,
                            name,
                            area.x + Look::margin,
                            ecode::UIText::centredBaseline(context.atlas(), area),
                            on ? Look::text : Look::theme().rowText);
    };

    list.onRowClicked = [this](std::size_t row, int) { onPicked((int) row); };

    list.onSelectionChanged = [this](int row)
    {
        if (row >= 0)
            scroller.scrollToShow(list.boundsOfRow((std::size_t) row).y
                                      - list.bounds().y,
                                  Look::rowHeight);
    };

    scroller.setContent(list);
    setRoot(root);
}

void Sidebar::setShown(int index)
{
    list.setSelectedRow(index);
}
} // namespace Tour

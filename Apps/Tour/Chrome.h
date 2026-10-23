#pragma once

#include "WidgetView.h"

#include <ECodeWidgets/ListView.h>
#include <ECodeWidgets/ScrollView.h>
#include <ECodeWidgets/Widget.h>

#include <functional>
#include <string>

// The Tour's chrome, as ECode widgets: a button, a one-line header, the
// toolbar and the chapter list.
namespace Tour
{
// A clickable label. A latching button stays lit while what it names is on.
class Button final : public ecode::Widget
{
public:
    explicit Button(std::string labelToShow, bool latches = false);

    void setOn(bool value);

    bool wantsMouse() const override { return true; }

    void prepare(eacp::Text::GlyphAtlas& atlas,
                 const eacp::Graphics::Rect& visible) override;
    void paint(ecode::PaintContext& context) override;

    void mouseDown(const eacp::Graphics::MouseEvent& event) override;
    void mouseUp(const eacp::Graphics::MouseEvent& event) override;
    void mouseMoved(const eacp::Graphics::MouseEvent& event) override;
    void mouseExited() override;

    std::function<void()> onClick = [] {};

private:
    std::string label;
    bool latching = false;
    bool on = false;
    bool pressed = false;
    bool hovered = false;
};

// One line over a pane: what it shows on the left, a note on the right, and
// optionally a button at the far right.
class Header final : public ecode::Widget
{
public:
    explicit Header(Button* actionToShow = nullptr);

    void setText(std::string leftText,
                 std::string rightText,
                 eacp::Graphics::Color rightColour = Look::dimText);

    void layout() override;

    void prepare(eacp::Text::GlyphAtlas& atlas,
                 const eacp::Graphics::Rect& visible) override;
    void paint(ecode::PaintContext& context) override;

private:
    Button* action;
    std::string left;
    std::string right;
    eacp::Graphics::Color rightColour = Look::dimText;
};

// A Header in a view of its own, for a pane to place above its content.
class HeaderView final : public WidgetView
{
public:
    explicit HeaderView(Button* action = nullptr);

    Header header;
};

// The layout modes, as the toolbar and the menu name them. The order is the
// order of the Cmd+1..4 shortcuts.
enum class LayoutMode
{
    Result,
    SideBySide,
    Stacked,
    Code
};

// The strip along the top: the sidebar toggle, the four layouts, reload, and a
// reminder of the shortcuts.
class Toolbar final : public WidgetView
{
public:
    Toolbar();

    void show(LayoutMode mode, bool sidebarShown);

    Button sidebar {"Chapters", true};
    Button result {"Result", true};
    Button sideBySide {"Side by side", true};
    Button stacked {"Stacked", true};
    Button code {"Code", true};
    Button reload {"Reload"};

private:
    class Root final : public ecode::Widget
    {
    public:
        explicit Root(Toolbar& owner);

        void layout() override;
        void prepare(eacp::Text::GlyphAtlas& atlas,
                     const eacp::Graphics::Rect& visible) override;
        void paint(ecode::PaintContext& context) override;

    private:
        Toolbar& toolbar;
    };

    Root root {*this};
};

// The chapters down the left, "NN. Title", one row each. Clicking a row picks
// that chapter.
class Sidebar final : public WidgetView
{
public:
    explicit Sidebar(eacp::Vector<std::string> rowsToShow);

    // Highlights the chapter on screen and scrolls it into view. Does not call
    // onPicked.
    void setShown(int index);

    std::function<void(int)> onPicked = [](int) {};

private:
    class Root final : public ecode::Widget
    {
    public:
        explicit Root(Sidebar& owner);

        void layout() override;
        void prepare(eacp::Text::GlyphAtlas& atlas,
                     const eacp::Graphics::Rect& visible) override;
        void paint(ecode::PaintContext& context) override;

    private:
        Sidebar& sidebar;
    };

    eacp::Vector<std::string> rows;
    ecode::ListView list;
    ecode::ScrollView scroller {Look::theme()};
    Root root {*this};
};
} // namespace Tour

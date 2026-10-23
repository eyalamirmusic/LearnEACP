#pragma once

#include "Chrome.h"
#include "Panes.h"

#include <LearnEACP/Chapter.h>

#include <memory>
#include <optional>
#include <string>

namespace Tour
{
// "01. Hello Window"
std::string chapterLabel(const LearnEACP::ChapterInfo& chapter);

eacp::Vector<std::string>
    chapterLabels(const std::vector<LearnEACP::ChapterInfo>& chapters);

eacp::Graphics::WindowOptions windowOptions();

// How the Tour opens, from the command line (see Main.cpp).
struct Options
{
    // Into LearnEACP::registeredChapters().
    int chapterIndex = 0;
    LayoutMode layout = LayoutMode::SideBySide;
    bool sidebar = true;

    // When set, the window is captured to this PNG `after` seconds after it
    // opens, and the app exits: 0 if the file was written, 1 if not.
    std::string screenshot;
    double after = 2.0;
};

// Where the chapter's result goes: its GPUView, or for a console chapter the
// pane with what it printed. Owns the chapter's view, and nothing else ever
// holds one, so the chapter that is not on screen is not running.
class ResultArea final : public eacp::Graphics::View
{
public:
    ResultArea();

    // Puts `next` on screen, then removes and destroys the view it replaces.
    // Null for a console chapter, which shows the console pane instead.
    void setChapterView(std::unique_ptr<eacp::GPU::GPUView> next);

    eacp::GPU::GPUView* chapterView() const { return view.get(); }

    void resized() override;

    ConsolePane console;

private:
    std::unique_ptr<eacp::GPU::GPUView> view;
};

// The window's content: toolbar along the top, chapter list down the left,
// and the code and the result arranged by the layout mode.
class Layout final : public eacp::Graphics::View
{
public:
    Layout(Toolbar& toolbarToPlace,
           Sidebar& sidebarToPlace,
           CodePane& codeToPlace,
           ResultArea& resultToPlace);

    void setMode(LayoutMode newMode);
    LayoutMode getMode() const { return mode; }

    void setSidebarShown(bool shown);
    bool isSidebarShown() const { return sidebarShown; }

    void resized() override;
    void paint(eacp::Graphics::Context& context) override;

    // The chapter gets the keyboard whenever the window becomes key, rather
    // than whichever view the window would pick.
    void* nativeFocusTarget() override;

private:
    void setShown(eacp::Graphics::View& pane, bool shown);

    Toolbar& toolbar;
    Sidebar& sidebar;
    CodePane& code;
    ResultArea& result;

    LayoutMode mode = LayoutMode::SideBySide;
    bool sidebarShown = true;
};

// The app: one window, every chapter, one at a time.
class App
{
public:
    explicit App(const Options& options);

private:
    // Builds chapter `index`'s view, swaps it in for the current one (which
    // is destroyed), shows its code, and gives it the keyboard.
    void show(int index);
    void reload() { show(current); }

    void runConsoleChapter();

    void setLayout(LayoutMode mode);
    void toggleSidebar();

    // Hands the keyboard back to the chapter after the chrome was used.
    void focusResult();

    void announce();
    void installMenu();
    void takeScreenshot(const std::string& path, int attemptsLeft = 50);

    std::vector<LearnEACP::ChapterInfo> chapters = LearnEACP::registeredChapters();
    int current = 0;

    Toolbar toolbar;
    Sidebar sidebar {chapterLabels(chapters)};
    CodePane code;
    ResultArea result;
    Layout layout {toolbar, sidebar, code, result};
    eacp::Graphics::Window window {windowOptions()};

    // Lets a delayed callback tell whether the app is still there.
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
};
} // namespace Tour

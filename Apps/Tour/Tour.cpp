#include "Tour.h"

#include <cstdio>

using namespace eacp;
using namespace eacp::Graphics;

namespace Tour
{
std::string chapterLabel(const LearnEACP::ChapterInfo& chapter)
{
    auto number = std::to_string(chapter.number);

    if (number.size() < 2)
        number.insert(0, "0");

    return number + ". " + std::string {chapter.title};
}

Vector<std::string>
    chapterLabels(const std::vector<LearnEACP::ChapterInfo>& chapters)
{
    auto labels = Vector<std::string> {};

    for (const auto& chapter: chapters)
        labels.add(chapterLabel(chapter));

    return labels;
}

WindowOptions windowOptions()
{
    auto options = WindowOptions {};

    options.width = 1440;
    options.height = 860;
    options.minWidth = 760;
    options.minHeight = 480;
    options.title = "LearnEACP Tour";
    options.backgroundColor = Look::panel;

    return options;
}

// --- ResultArea -------------------------------------------------------------

ResultArea::ResultArea() = default;

void ResultArea::setChapterView(std::unique_ptr<GPU::GPUView> next)
{
    if (next != nullptr)
    {
        addSubview(*next);
        next->setBounds(getLocalBounds());
    }
    else if (console.getParent() != this)
    {
        addSubview(console);
        console.setBounds(getLocalBounds());
    }

    auto previous = std::move(view);
    view = std::move(next);

    // Out of the tree rather than hidden, as Layout does with its panes.
    if (view != nullptr && console.getParent() == this)
        removeSubview(console);

    if (previous != nullptr)
        removeSubview(*previous);

    // `previous` is destroyed here, after its replacement is on screen.
}

void ResultArea::resized()
{
    console.setBounds(getLocalBounds());

    if (view != nullptr)
        view->setBounds(getLocalBounds());
}

// --- Layout -----------------------------------------------------------------

Layout::Layout(Toolbar& toolbarToPlace,
               Sidebar& sidebarToPlace,
               CodePane& codeToPlace,
               ResultArea& resultToPlace)
    : toolbar(toolbarToPlace)
    , sidebar(sidebarToPlace)
    , code(codeToPlace)
    , result(resultToPlace)
{
    addChildren({toolbar, sidebar, code, result});
}

void Layout::setMode(LayoutMode newMode)
{
    mode = newMode;
    resized();
}

void Layout::setSidebarShown(bool shown)
{
    sidebarShown = shown;
    resized();
}

// A pane that is not shown is taken out of the tree rather than hidden, because
// a window snapshot (renderToImage) draws hidden views too. A continuous
// chapter keeps ticking while out of the tree (its display link does not depend
// on the window) and shows again as soon as it is put back.
void Layout::setShown(View& pane, bool shown)
{
    if (shown && pane.getParent() != this)
        addSubview(pane);
    else if (!shown && pane.getParent() == this)
        removeSubview(pane);
}

void Layout::resized()
{
    auto bounds = getLocalBounds();

    toolbar.setBounds(bounds.removeFromTop(Look::toolbarHeight));

    setShown(sidebar, sidebarShown);

    if (sidebarShown)
        sidebar.setBounds(bounds.removeFromLeft(Look::sidebarWidth));

    setShown(code, mode != LayoutMode::Result);
    setShown(result, mode != LayoutMode::Code);

    // A one-point gap between code and result, in the window's background.
    switch (mode)
    {
        case LayoutMode::Result:
            result.setBounds(bounds);
            break;

        case LayoutMode::Code:
            code.setBounds(bounds);
            break;

        case LayoutMode::SideBySide:
            code.setBounds(bounds.removeFromLeft((bounds.w - 1.f) / 2.f));
            bounds.removeFromLeft(1.f);
            result.setBounds(bounds);
            break;

        case LayoutMode::Stacked:
            code.setBounds(bounds.removeFromTop((bounds.h - 1.f) / 2.f));
            bounds.removeFromTop(1.f);
            result.setBounds(bounds);
            break;
    }
}

// Only the gap between code and result shows it, but a window snapshot
// (renderToImage) leaves out the window's own background, so it is drawn here.
void Layout::paint(Context& context)
{
    context.setColor(Look::edge);
    context.fillRect(getLocalBounds());
}

void* Layout::nativeFocusTarget()
{
    if (auto* view = result.chapterView())
        return view->getHandle();

    return View::nativeFocusTarget();
}

// --- App --------------------------------------------------------------------

App::App(const Options& options)
{
    toolbar.sidebar.onClick = [this] { toggleSidebar(); };
    toolbar.result.onClick = [this] { setLayout(LayoutMode::Result); };
    toolbar.sideBySide.onClick = [this] { setLayout(LayoutMode::SideBySide); };
    toolbar.stacked.onClick = [this] { setLayout(LayoutMode::Stacked); };
    toolbar.code.onClick = [this] { setLayout(LayoutMode::Code); };
    toolbar.reload.onClick = [this] { reload(); };

    sidebar.onPicked = [this](int index) { show(index); };
    result.console.onRunAgain = [this] { reload(); };

    layout.setMode(options.layout);
    layout.setSidebarShown(options.sidebar);
    toolbar.show(options.layout, options.sidebar);

    window.setContentView(layout);
    installMenu();

    show(options.chapterIndex);

    if (!options.screenshot.empty())
    {
        auto delay = Time::MS {(std::int64_t) (options.after * 1000.0)};
        auto weak = std::weak_ptr<bool> {alive};

        Threads::callAfter(delay,
                           [this, weak, path = options.screenshot]
                           {
                               if (!weak.expired())
                                   takeScreenshot(path);
                           });
    }
}

void App::show(int index)
{
    const auto count = (int) chapters.size();
    index = ((index % count) + count) % count;

    const auto& chapter = chapters[(std::size_t) index];

    // Built before the old one goes, so the result is never empty, then
    // swapped in; the old chapter is destroyed rather than kept alive hidden,
    // since most chapters render continuously.
    auto next = std::unique_ptr<GPU::GPUView> {};

    if (chapter.createView)
    {
        next = chapter.createView();

        // So a click on the result takes the keyboard back from the code
        // view, whether or not the chapter itself reads the mouse.
        next->setHandlesMouseEvents(true);
        next->setGrabsFocusOnMouseDown(true);
    }

    result.setChapterView(std::move(next));
    current = index;

    code.show(LearnEACP::findChapterSource(chapter.name));
    sidebar.setShown(index);

    if (chapter.runConsole)
        runConsoleChapter();

    announce();
    focusResult();
}

void App::runConsoleChapter()
{
    const auto& chapter = chapters[(std::size_t) current];
    auto run = runCapturingOutput(chapter.runConsole);

    result.console.show(run.output, run.exitStatus);
}

void App::setLayout(LayoutMode mode)
{
    layout.setMode(mode);
    toolbar.show(mode, layout.isSidebarShown());
    focusResult();
}

void App::toggleSidebar()
{
    layout.setSidebarShown(!layout.isSidebarShown());
    toolbar.show(layout.getMode(), layout.isSidebarShown());
    focusResult();
}

void App::focusResult()
{
    if (auto* view = result.chapterView())
        view->focus();
}

void App::announce()
{
    window.setTitle("LearnEACP Tour - "
                    + chapterLabel(chapters[(std::size_t) current]));
}

void App::installMenu()
{
    auto view = Menu {"View"};

    auto addLayout = [&](const char* title, LayoutMode mode, const char* key)
    {
        view.add(MenuItem::withCheckableAction(
            title,
            [this, mode] { setLayout(mode); },
            [this, mode] { return layout.getMode() == mode; },
            commandKey(key)));
    };

    addLayout("Result Only", LayoutMode::Result, "1");
    addLayout("Code and Result Side by Side", LayoutMode::SideBySide, "2");
    addLayout("Code over Result", LayoutMode::Stacked, "3");
    addLayout("Code Only", LayoutMode::Code, "4");

    view.addSeparator();
    view.add(MenuItem::withCheckableAction(
        "Show Chapter List",
        [this] { toggleSidebar(); },
        [this] { return layout.isSidebarShown(); },
        commandKey("b")));

    auto chapterMenu = Menu {"Chapter"};

    chapterMenu.add(MenuItem::withAction(
        "Previous Chapter", [this] { show(current - 1); }, commandKey("[")));
    chapterMenu.add(MenuItem::withAction(
        "Next Chapter", [this] { show(current + 1); }, commandKey("]")));
    chapterMenu.add(MenuItem::withAction(
        "Reload Chapter", [this] { reload(); }, commandKey("r")));
    chapterMenu.addSeparator();

    for (auto index = 0; index < (int) chapters.size(); ++index)
        chapterMenu.add(MenuItem::withCheckableAction(
            chapterLabel(chapters[(std::size_t) index]),
            [this, index] { show(index); },
            [this, index] { return current == index; }));

    auto bar = MenuBar {};
    bar.add(standardApplicationMenu("Tour"));
    bar.add(std::move(view));
    bar.add(std::move(chapterMenu));

    setApplicationMenuBar(bar, window);
}

void App::takeScreenshot(const std::string& path, int attemptsLeft)
{
    // A shown code pane that is still being highlighted gets another 100 ms,
    // up to 5 s, so the capture has its colours.
    if (layout.getMode() != LayoutMode::Result && !code.isHighlighted()
        && attemptsLeft > 0)
    {
        auto weak = std::weak_ptr<bool> {alive};

        Threads::callAfter(Time::MS {100},
                           [this, weak, path, attemptsLeft]
                           {
                               if (!weak.expired())
                                   takeScreenshot(path, attemptsLeft - 1);
                           });
        return;
    }

    auto image = layout.renderToImage();

    if (!image.isValid())
    {
        std::fprintf(stderr, "Tour: the window rendered no image\n");
        Apps::quit(1);
        return;
    }

    try
    {
        image.save(FilePath {path});
    }
    catch (const std::exception& error)
    {
        std::fprintf(
            stderr, "Tour: cannot save %s: %s\n", path.c_str(), error.what());
        Apps::quit(1);
        return;
    }

    std::printf("Saved %s (%dx%d)\n", path.c_str(), image.width(), image.height());
    std::fflush(stdout);
    Apps::quit(0);
}
} // namespace Tour

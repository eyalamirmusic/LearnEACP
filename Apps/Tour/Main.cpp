// The Tour: every LearnEACP chapter in one window, each one's Main.cpp beside
// what it draws.
//
//   Tour                       opens the window on chapter 1
//   Tour --list                prints the chapters; exits 0 if all registered
//   Tour --chapter 6 --layout stacked --screenshot shot.png [--after 3]
//                              opens on a chapter, captures the window, exits
#include "Tour.h"

#include <eacp/Core/App/Args.h>

#include <cstdio>

using namespace eacp;

namespace Tour
{
namespace
{
// Every chapter LearnEACP has, which is how many learneacp_add_chapter() calls
// the build made: set by Apps/Tour/CMakeLists.txt.
constexpr auto expectedChapterCount = TOUR_EXPECTED_CHAPTER_COUNT;

// Prints the chapters in order and answers whether all of them registered,
// in order, exactly once, each with exactly one entry point.
int listChapters()
{
    auto chapters = LearnEACP::registeredChapters();
    auto ok = (int) chapters.size() == expectedChapterCount;

    for (auto index = 0; index < (int) chapters.size(); ++index)
    {
        const auto& chapter = chapters[(std::size_t) index];
        const auto* source = LearnEACP::findChapterSource(chapter.name);

        std::printf("%02d  %-20.*s %-22.*s %-8s %zu lines\n",
                    chapter.number,
                    (int) chapter.name.size(),
                    chapter.name.data(),
                    (int) chapter.title.size(),
                    chapter.title.data(),
                    chapter.createView ? "window" : "console",
                    source != nullptr ? source->lines.size() : 0);

        auto hasOneEntryPoint =
            (bool) chapter.createView != (bool) chapter.runConsole;
        ok = ok && chapter.number == index + 1 && hasOneEntryPoint
             && source != nullptr;
    }

    std::printf("%d of %d chapters registered: %s\n",
                (int) chapters.size(),
                expectedChapterCount,
                ok ? "OK" : "FAILED");

    return ok ? 0 : 1;
}

// A chapter by number ("6") or by target name ("Textures", any case).
std::optional<int> findChapter(std::string_view text)
{
    auto chapters = LearnEACP::registeredChapters();

    for (auto index = 0; index < (int) chapters.size(); ++index)
    {
        const auto& chapter = chapters[(std::size_t) index];

        if (Strings::equalsCaseInsensitive(chapter.name, text)
            || std::to_string(chapter.number) == text
            || (text.size() == 2 && text[0] == '0'
                && std::to_string(chapter.number) == text.substr(1)))
            return index;
    }

    return std::nullopt;
}

LayoutMode layoutNamed(const std::string& name)
{
    if (name == "result")
        return LayoutMode::Result;

    if (name == "stacked")
        return LayoutMode::Stacked;

    if (name == "code")
        return LayoutMode::Code;

    return LayoutMode::SideBySide;
}

int run()
{
    auto list = false;
    auto chapter = std::string {"1"};
    auto layout = std::string {"side-by-side"};
    auto hideSidebar = false;
    auto options = Options {};

    auto cli = Args::Parser {"Tour", "Every LearnEACP chapter in one window."};

    cli.flag("--list", list, "print the chapters and exit: 0 if all registered");
    cli.option("--chapter", chapter, "the chapter to open, by number or name")
        .hint("<n|name>");
    cli.option("--layout", layout, "how code and result share the window")
        .choices({"result", "side-by-side", "stacked", "code"});
    cli.flag("--no-sidebar", hideSidebar, "start with the chapter list hidden");
    cli.option("--screenshot",
               options.screenshot,
               "capture the window to this PNG, then exit")
        .hint("<path.png>");
    cli.option("--after", options.after, "seconds to wait before the screenshot")
        .atLeast(0.0);

    if (auto code = cli.exitCodeFor(cli.parse()))
        return *code;

    if (list)
        return listChapters();

    auto index = findChapter(chapter);

    if (!index)
        return cli.fail("no chapter '" + chapter + "' (try --list)");

    options.chapterIndex = *index;
    options.layout = layoutNamed(layout);
    options.sidebar = !hideSidebar;

    return Apps::run<App>(options);
}
} // namespace
} // namespace Tour

int main(int argc, char* argv[])
{
    Apps::setCommandLineArgs(argc, argv);
    return Tour::run();
}

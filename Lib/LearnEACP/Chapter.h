#pragma once

// The one line at the bottom of every chapter's Main.cpp.
//
// Built on its own, a chapter is a normal program, and LEARNEACP_CHAPTER(View)
// is its main(): it runs View in a window, exactly as
// `return Graphics::runWindowedApp<View>();` would.
//
// Built with LEARNEACP_CHAPTER_REGISTRY defined, the same line instead adds
// the chapter to a registry, so several chapters can be linked into one
// program that lists them and creates whichever one is wanted. The chapter's
// number, name and title then come from compile definitions set by the build.

#include <eacp/Core/App/App.h>
#include <eacp/GPU/GPU.h>

#include <algorithm>
#include <functional>
#include <memory>
#include <string_view>
#include <vector>

namespace LearnEACP
{
// One registered chapter. A windowed chapter fills createView, a console
// chapter fills runConsole; the other is empty.
struct ChapterInfo
{
    int number = 0;
    std::string_view name; // the build target, e.g. "HelloWindow"
    std::string_view title; // the README heading, e.g. "Hello Window"
    std::function<std::unique_ptr<eacp::GPU::GPUView>()> createView;
    std::function<int()> runConsole;
};

// Function-local, so it exists before the first registration whatever order
// the chapters' static initializers run in.
inline std::vector<ChapterInfo>& chapterRegistry()
{
    static std::vector<ChapterInfo> registry;
    return registry;
}

// Every registered chapter, in chapter order.
inline std::vector<ChapterInfo> registeredChapters()
{
    auto chapters = chapterRegistry();
    std::ranges::sort(chapters, {}, &ChapterInfo::number);
    return chapters;
}

struct ChapterRegistration
{
    explicit ChapterRegistration(ChapterInfo info)
    {
        chapterRegistry().push_back(std::move(info));
    }
};
} // namespace LearnEACP

#if defined(LEARNEACP_CHAPTER_REGISTRY)

#define LEARNEACP_REGISTER_CHAPTER(createViewFunction, runConsoleFunction)          \
    static const LearnEACP::ChapterRegistration learnEACPChapterRegistration {      \
        {LEARNEACP_CHAPTER_NUMBER,                                                  \
         LEARNEACP_CHAPTER_NAME,                                                    \
         LEARNEACP_CHAPTER_TITLE,                                                   \
         createViewFunction,                                                        \
         runConsoleFunction}};

#define LEARNEACP_CHAPTER(View)                                                     \
    LEARNEACP_REGISTER_CHAPTER([]() -> std::unique_ptr<eacp::GPU::GPUView>          \
                               { return std::make_unique<View>(); },                \
                               {})

#define LEARNEACP_CONSOLE_CHAPTER(function) LEARNEACP_REGISTER_CHAPTER({}, function)

#else

#define LEARNEACP_CHAPTER(View)                                                     \
    int main()                                                                      \
    {                                                                               \
        return eacp::Graphics::runWindowedApp<View>();                              \
    }

#define LEARNEACP_CONSOLE_CHAPTER(function)                                         \
    int main()                                                                      \
    {                                                                               \
        return eacp::Apps::runConsole(function);                                    \
    }

#endif

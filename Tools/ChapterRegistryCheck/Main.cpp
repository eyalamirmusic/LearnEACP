// Lists every chapter linked into this program through the chapter registry,
// and exits with 0 only if all of them registered, in order, exactly once.
#include <LearnEACP/Chapter.h>

#include <cstdio>

int main()
{
    auto chapters = LearnEACP::registeredChapters();
    auto ok = (int) chapters.size() == EXPECTED_CHAPTER_COUNT;

    for (auto index = 0; index < (int) chapters.size(); ++index)
    {
        auto& chapter = chapters[(size_t) index];
        auto kind = chapter.createView ? "window" : "console";

        std::printf("%02d  %-20.*s %-20.*s (%s)\n",
                    chapter.number,
                    (int) chapter.name.size(),
                    chapter.name.data(),
                    (int) chapter.title.size(),
                    chapter.title.data(),
                    kind);

        auto hasOneEntryPoint =
            (bool) chapter.createView != (bool) chapter.runConsole;
        ok = ok && chapter.number == index + 1 && hasOneEntryPoint;
    }

    std::printf("%d of %d chapters registered: %s\n",
                (int) chapters.size(),
                EXPECTED_CHAPTER_COUNT,
                ok ? "OK" : "FAILED");

    return ok ? 0 : 1;
}

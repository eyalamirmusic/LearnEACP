#include "Panes.h"

#include <ECodeSyntax/SyntaxHighlighter.h>

#include <cstdio>
#include <exception>

#if defined(_WIN32)
#include <io.h>
#define TOUR_DUP _dup
#define TOUR_DUP2 _dup2
#define TOUR_CLOSE _close
#define TOUR_FILENO _fileno
#else
#include <unistd.h>
#define TOUR_DUP dup
#define TOUR_DUP2 dup2
#define TOUR_CLOSE close
#define TOUR_FILENO fileno
#endif

using namespace eacp;
using namespace eacp::Graphics;

namespace Tour
{
namespace
{
eacp::OwningPointer<ecode::Highlighter> cppHighlighter()
{
    auto syntax = makeOwned<ecode::SyntaxHighlighter>(
        ecode::SyntaxHighlighter::frameParseBudget);

    if (!syntax->isValid())
        return {};

    return eacp::OwningPointer<ecode::Highlighter> {std::move(syntax)};
}

// ECode's own dark theme, without a caret: nothing can be typed here.
ecode::TextTheme readOnlyTheme()
{
    auto theme = ecode::TextTheme {};
    theme.caret = {0.f, 0.f, 0.f, 0.f};
    return theme;
}

void placeUnderHeader(View& pane, View& header, View& content)
{
    auto bounds = pane.getLocalBounds();

    header.setBounds(bounds.removeFromTop(Look::headerHeight));
    content.setBounds(bounds);
}
} // namespace

// --- CodePane ---------------------------------------------------------------

CodePane::CodePane()
{
    editor.setReadOnly(true);
    editor.setTheme(readOnlyTheme());
    editor.setHighlighter(cppHighlighter());

    addChildren({header, editor});
}

void CodePane::show(const LearnEACP::ChapterSource* source)
{
    if (source == nullptr)
    {
        header.header.setText("(no source embedded)", {});
        editor.setText({});
        return;
    }

    header.header.setText(std::string {source->path},
                          std::to_string(source->lines.size()) + " lines");
    editor.setText(source->text());

    // A new file opens at its first line, whatever the last one was showing.
    editor.editor().placeCaret(0);
    editor.editorWidget().scrollToTopLine(0);
    editor.editorWidget().scrollToLeftColumn(0);
}

bool CodePane::isHighlighted() const
{
    auto* highlighter = editor.highlighter();
    return highlighter == nullptr || !highlighter->hasPendingWork();
}

void CodePane::resized()
{
    placeUnderHeader(*this, header, editor);
}

// --- ConsolePane ------------------------------------------------------------

ConsolePane::ConsolePane()
{
    output.setReadOnly(true);
    output.setTheme(readOnlyTheme());
    runAgain.onClick = [this] { onRunAgain(); };

    addChildren({header, output});
}

void ConsolePane::show(const std::string& text, int exitStatus)
{
    header.header.setText("Console output",
                          "exit status " + std::to_string(exitStatus),
                          exitStatus == 0 ? Look::good : Look::bad);
    output.setText(text);
}

void ConsolePane::resized()
{
    placeUnderHeader(*this, header, output);
}

// --- Capturing a console chapter --------------------------------------------

ConsoleRun runCapturingOutput(const std::function<int()>& body)
{
    auto run = ConsoleRun {};

    std::fflush(stdout);
    std::fflush(stderr);

    auto* capture = std::tmpfile();

    if (capture == nullptr)
    {
        run.output = "Could not create a file to capture the output in.\n";
        run.exitStatus = 1;
        return run;
    }

    const auto savedOut = TOUR_DUP(1);
    const auto savedErr = TOUR_DUP(2);

    TOUR_DUP2(TOUR_FILENO(capture), 1);
    TOUR_DUP2(TOUR_FILENO(capture), 2);

    try
    {
        run.exitStatus = body();
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "Exception: %s\n", error.what());
        run.exitStatus = 1;
    }

    std::fflush(stdout);
    std::fflush(stderr);

    TOUR_DUP2(savedOut, 1);
    TOUR_DUP2(savedErr, 2);
    TOUR_CLOSE(savedOut);
    TOUR_CLOSE(savedErr);

    std::rewind(capture);

    char buffer[4096];

    while (auto count = std::fread(buffer, 1, sizeof(buffer), capture))
        run.output.append(buffer, count);

    std::fclose(capture);

    std::fwrite(run.output.data(), 1, run.output.size(), stdout);
    std::fflush(stdout);

    return run;
}
} // namespace Tour

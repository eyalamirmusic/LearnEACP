#pragma once

#include "Chrome.h"

#include <LearnEACP/ChapterSource.h>

#include <ECodeEditor/CodeEditorView.h>

#include <functional>
#include <string>

namespace Tour
{
// A chapter's Main.cpp: its path over ECode's read-only, C++-highlighted code
// view. Selecting and copying still work; typing does not.
class CodePane final : public eacp::Graphics::View
{
public:
    CodePane();

    void show(const LearnEACP::ChapterSource* source);

    // Whether the highlighter has coloured the whole file. It parses a slice
    // per frame, so a long file takes a moment; a screenshot waits for this.
    bool isHighlighted() const;

    void resized() override;

private:
    HeaderView header;
    ecode::CodeEditorView editor;
};

// What a console chapter shows in place of a result: the text it printed and
// the status it exited with, with a button to run it again.
class ConsolePane final : public eacp::Graphics::View
{
public:
    ConsolePane();

    void show(const std::string& output, int exitStatus);

    void resized() override;

    std::function<void()> onRunAgain = [] {};

private:
    Button runAgain {"Run again"};
    HeaderView header {&runAgain};
    ecode::CodeEditorView output;
};

// What a console chapter's run left behind.
struct ConsoleRun
{
    std::string output;
    int exitStatus = 0;
};

// Runs `body` with stdout and stderr redirected into a temporary file, and
// hands back what it printed and returned. Both streams are restored before
// this returns, and the captured text is then echoed to the real stdout, so a
// run from a terminal still shows it. An exception escaping `body` is reported
// in the output and counts as exit status 1, as Apps::runConsole does.
ConsoleRun runCapturingOutput(const std::function<int()>& body);
} // namespace Tour

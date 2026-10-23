#pragma once

#include <ECodeRender/FontSettings.h>
#include <ECodeWidgets/Theme.h>

#include <eacp/Core/Core.h>
#include <eacp/Graphics/Graphics.h>

// The Tour's chrome in one place: colours taken from ECode's own dark theme,
// so the sidebar and toolbar match the code view they sit beside.
namespace Tour::Look
{
inline const ecode::ChromeTheme& theme()
{
    static const auto chrome = ecode::ChromeTheme {};
    return chrome;
}

constexpr auto panel = eacp::Graphics::Color {0.094f, 0.098f, 0.118f};
constexpr auto edge = eacp::Graphics::Color {0.18f, 0.19f, 0.22f};
constexpr auto text = eacp::Graphics::Color {0.85f, 0.87f, 0.91f};
constexpr auto dimText = eacp::Graphics::Color {0.50f, 0.53f, 0.60f};
constexpr auto button = eacp::Graphics::Color {0.15f, 0.16f, 0.19f};
constexpr auto selected = eacp::Graphics::Color {0.20f, 0.33f, 0.55f};
constexpr auto good = eacp::Graphics::Color {0.45f, 0.78f, 0.50f};
constexpr auto bad = eacp::Graphics::Color {0.90f, 0.45f, 0.40f};

constexpr auto toolbarHeight = 36.f;
constexpr auto headerHeight = 26.f;
constexpr auto sidebarWidth = 236.f;
constexpr auto rowHeight = 24.f;
constexpr auto margin = 12.f;

// Proportional rather than the editor's monospace face: chapter titles and
// button labels are prose, not code. Named per platform because no family
// ships on all three.
inline ecode::FontSettings chromeFont()
{
    constexpr auto family = []
    {
        if constexpr (eacp::Platform::isWindows())
            return "Segoe UI";

        if constexpr (eacp::Platform::isLinux())
            return "DejaVu Sans";

        return "Helvetica";
    }();

    return {family, 13.f};
}
} // namespace Tour::Look

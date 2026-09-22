#pragma once
#include "WinUIDebug.h"
#include <winrt/Microsoft.UI.Composition.h>
#include <winrt/Microsoft.UI.Composition.SystemBackdrops.h>
#include <winrt/Microsoft.UI.Xaml.h>

namespace lightHostModern::ui
{
// Own the native controllers and their activation/theme policy together. This
// also supports desktops where ThemeSettings notifications are unavailable.
class WindowMaterial
{
public:
    ~WindowMaterial() { close(); }
    void activated(bool value) noexcept
    {
        active = value;
        try { if (configuration) configuration.IsInputActive(active); } catch (...) {}
    }
    bool apply(winrt::Microsoft::UI::Xaml::Window window, int requested,
               winrt::Microsoft::UI::Xaml::ElementTheme theme, bool highContrast)
    {
        using namespace winrt::Microsoft::UI::Composition;
        using namespace winrt::Microsoft::UI::Composition::SystemBackdrops;
        const auto targetTheme = theme == winrt::Microsoft::UI::Xaml::ElementTheme::Dark
            ? SystemBackdropTheme::Dark : SystemBackdropTheme::Light;
        if (requested == mode && configuration)
        {
            configuration.Theme(targetTheme);
            configuration.IsHighContrast(highContrast);
            return true;
        }
        close();
        if (requested == 3 || highContrast) return false;
        if ((requested < 2 && !MicaController::IsSupported())
            || (requested == 2 && !DesktopAcrylicController::IsSupported())) return false;
        configuration = SystemBackdropConfiguration();
        configuration.Theme(targetTheme);
        configuration.IsInputActive(active);
        configuration.IsHighContrast(highContrast);
        const auto target = window.as<ICompositionSupportsSystemBackdrop>();
        if (requested < 2)
        {
            mica = MicaController();
            mica.Kind(requested == 1 ? MicaKind::BaseAlt : MicaKind::Base);
            mica.StateChanged([requested](const auto& controller, const auto&) {
                winUILog("Mica state changed: mode=" + std::to_string(requested)
                    + " state=" + std::to_string(static_cast<int>(controller.State())));
            });
            if (!mica.AddSystemBackdropTarget(target)) { close(); return false; }
            mica.SetSystemBackdropConfiguration(configuration);
        }
        else
        {
            acrylic = DesktopAcrylicController();
            if (!acrylic.AddSystemBackdropTarget(target)) { close(); return false; }
            acrylic.SetSystemBackdropConfiguration(configuration);
        }
        mode = requested;
        winUILog("Native window material applied: mode=" + std::to_string(mode)
            + " state=" + std::to_string(static_cast<int>(mica ? mica.State() : acrylic.State()))
            + " active=" + std::to_string(active));
        return true;
    }
    void close() noexcept
    {
        try { if (mica) mica.Close(); } catch (...) {}
        try { if (acrylic) acrylic.Close(); } catch (...) {}
        mica = nullptr; acrylic = nullptr; configuration = nullptr; mode = -1;
    }
private:
    winrt::Microsoft::UI::Composition::SystemBackdrops::MicaController mica{nullptr};
    winrt::Microsoft::UI::Composition::SystemBackdrops::DesktopAcrylicController acrylic{nullptr};
    winrt::Microsoft::UI::Composition::SystemBackdrops::SystemBackdropConfiguration configuration{nullptr};
    bool active = true;
    int mode = -1;
};
}

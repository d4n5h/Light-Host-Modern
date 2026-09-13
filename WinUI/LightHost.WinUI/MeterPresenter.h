#pragma once
#include "Localization.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>

namespace lightHost::ui
{
// The v1.2.2 display: 28 segments, linear peak amplitude, green/yellow/red.
class MeterPresenter
{
public:
    void create(winrt::Microsoft::UI::Xaml::Controls::StackPanel host, const wchar_t* id,
                ::LightHostWinUI::LocalizationCatalog& catalog)
    {
        using namespace winrt;
        using namespace Microsoft::UI::Xaml;
        using namespace Microsoft::UI::Xaml::Controls;
        host.Children().Clear();
        const auto resources = Application::Current().Resources();
        inactive = resources.Lookup(box_value(L"MeterInactiveSegmentStyle")).as<Style>();
        safe = resources.Lookup(box_value(L"MeterSafeSegmentStyle")).as<Style>();
        warning = resources.Lookup(box_value(L"MeterWarningSegmentStyle")).as<Style>();
        critical = resources.Lookup(box_value(L"MeterCriticalSegmentStyle")).as<Style>();
        StackPanel row; row.Orientation(Orientation::Horizontal); row.Spacing(4);
        for (auto& segment : segments)
        {
            segment = Border(); segment.Width(7); segment.Height(26);
            segment.CornerRadius({3,3,3,3}); segment.Style(inactive);
            row.Children().Append(segment);
        }
        Viewbox scale; scale.Stretch(Media::Stretch::Uniform); scale.StretchDirection(StretchDirection::DownOnly);
        scale.HorizontalAlignment(HorizontalAlignment::Left); scale.Child(row);
        display.Content(scale);
        Automation::AutomationProperties::SetAutomationId(display, id);
        input = std::wstring_view(id) == L"InputMeter";
        localize(catalog); host.Children().Append(display); activeCount = -1;
    }
    void localize(::LightHostWinUI::LocalizationCatalog& catalog)
    {
        winrt::Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(display,
            catalog.text(input ? "meters.inputLevel" : "meters.outputLevel", input ? L"Input level" : L"Output level"));
    }
    void update(double amplitude)
    {
        const auto level = std::isfinite(amplitude) ? (std::clamp)(amplitude, 0.0, 1.0) : 0.0;
        const int count = static_cast<int>(std::round(level * segments.size()));
        if (count == activeCount) return;
        activeCount = count;
        for (int i = 0; i < static_cast<int>(segments.size()); ++i)
            segments[i].Style(i >= count ? inactive : i > 28 * 0.82 ? critical : i > 28 * 0.64 ? warning : safe);
        winrt::Microsoft::UI::Xaml::Automation::AutomationProperties::SetHelpText(display,
            winrt::to_hstring(static_cast<int>(std::round(level * 100))) + L"%");
    }
private:
    winrt::Microsoft::UI::Xaml::Controls::UserControl display;
    std::array<winrt::Microsoft::UI::Xaml::Controls::Border, 28> segments{};
    winrt::Microsoft::UI::Xaml::Style inactive{nullptr}, safe{nullptr}, warning{nullptr}, critical{nullptr};
    int activeCount = -1;
    bool input = false;
};
}

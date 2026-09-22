#pragma once
#include "Localization.h"
#include "../../Source/MeterScale.h"
#include <chrono>
#include <iomanip>
#include <sstream>
#include <array>
#include <algorithm>
#include <cmath>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>

namespace lightHostModern::ui
{
// 28 bars on a -60..0 dBFS scale, with an unclamped numeric reading.
class MeterPresenter
{
public:
    void create(winrt::Microsoft::UI::Xaml::Controls::StackPanel host, const wchar_t* id,
                ::LightHostModernWinUI::LocalizationCatalog& catalog)
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
        // Reserve the full numeric field so value changes cannot resize the row or its Viewbox.
        Border readingSlot; readingSlot.Width(112); readingSlot.Height(26);
        reading.VerticalAlignment(VerticalAlignment::Center);
        reading.TextWrapping(TextWrapping::NoWrap); reading.TextAlignment(TextAlignment::Right);
        readingSlot.Child(reading); row.Children().Append(readingSlot);
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
    void localize(::LightHostModernWinUI::LocalizationCatalog& catalog)
    {
        unavailableText = catalog.text("common.unavailable", L"Unavailable");
        winrt::Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(display,
            catalog.text(input ? "meters.inputLevel" : "meters.outputLevel", input ? L"Input level" : L"Output level"));
    }
    void update(double amplitude)
    {
        if (!std::isfinite(amplitude)) { unavailable(); return; }
        const auto now = std::chrono::steady_clock::now();
        const auto db = lightHostModern::amplitudeDb(amplitude);
        const double seconds = std::chrono::duration<double>(now - updated).count(); updated = now;
        displayedDb = (std::max)(db, displayedDb - (std::min)(seconds, 1.0) * 30.0);
        const int count = lightHostModern::meterSegments(displayedDb);
        if (count != activeCount)
        {
            activeCount = count;
            for (int i = 0; i < static_cast<int>(segments.size()); ++i)
            {
                const auto threshold = -60.0 + (i + 1) * 60.0 / 28.0;
                segments[i].Style(i >= count ? inactive : threshold > -3 ? critical : threshold > -12 ? warning : safe);
            }
        }
        std::wostringstream text; text.imbue(std::locale::classic());
        const auto shown = !std::isfinite(db) && displayedDb <= -60.0 ? db : displayedDb;
        if (std::isfinite(shown)) text << std::fixed << std::setprecision(1) << shown << L" dBFS";
        else text << L"−∞ dBFS";
        reading.Text(text.str());
        winrt::Microsoft::UI::Xaml::Automation::AutomationProperties::SetHelpText(display, text.str());
    }
    void unavailable()
    {
        displayedDb = -60; activeCount = 0;
        for (auto segment : segments) segment.Style(inactive);
        reading.Text(L"—");
        winrt::Microsoft::UI::Xaml::Automation::AutomationProperties::SetHelpText(display, unavailableText);
    }

private:
    winrt::Microsoft::UI::Xaml::Controls::UserControl display;
    std::array<winrt::Microsoft::UI::Xaml::Controls::Border, 28> segments{};
    winrt::Microsoft::UI::Xaml::Style inactive{nullptr}, safe{nullptr}, warning{nullptr}, critical{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::TextBlock reading;
    std::chrono::steady_clock::time_point updated = std::chrono::steady_clock::now();
    double displayedDb = -60;
    int activeCount = -1;
    bool input = false;
    winrt::hstring unavailableText = L"Unavailable";
};
}

#pragma once
#include "HostJson.h"
#include "Localization.h"
#include "../../Source/ProcessMetrics.h"
#include <iomanip>
#include <limits>
#include <sstream>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>

namespace lightHost::ui
{
class DiagnosticsPresenter
{
public:
    void create(winrt::Microsoft::UI::Xaml::Controls::StackPanel host, ::LightHostWinUI::LocalizationCatalog& catalog)
    {
        using namespace winrt;
        using namespace Microsoft::UI::Xaml;
        using namespace Microsoft::UI::Xaml::Controls;
        host.Children().Clear(); rows.clear(); groups.clear();
        const auto resources = Application::Current().Resources();
        const auto addGroup = [&](const char* key, const wchar_t* glyph, const wchar_t* title, const wchar_t* description,
                                  std::initializer_list<Definition> definitions)
        {
            Group group; group.key = key; group.fallbackTitle = title; group.fallbackDescription = description;
            Border card; card.Style(resources.Lookup(box_value(L"AppCardStyle")).as<Style>());
            Automation::AutomationProperties::SetAutomationId(card, to_hstring(std::string("DiagnosticsCard-") + key));
            group.layout.ColumnSpacing(16); group.layout.RowSpacing(16);
            for (const auto width : {GridLengthHelper::FromPixels(32), GridLengthHelper::FromValueAndType(1, GridUnitType::Star), GridLengthHelper::Auto()})
            { ColumnDefinition column; column.Width(width); group.layout.ColumnDefinitions().Append(column); }
            for (int index = 0; index < 2; ++index)
            { RowDefinition row; row.Height(GridLengthHelper::Auto()); group.layout.RowDefinitions().Append(row); }
            FontIcon icon; icon.Glyph(glyph); icon.VerticalAlignment(VerticalAlignment::Center);
            group.layout.Children().Append(icon);
            StackPanel text; text.Spacing(4); text.VerticalAlignment(VerticalAlignment::Center); Grid::SetColumn(text, 1);
            group.title.Style(resources.Lookup(box_value(L"BodyStrongTextBlockStyle")).as<Style>());
            group.title.TextWrapping(TextWrapping::Wrap);
            Automation::AutomationProperties::SetAutomationId(group.title, to_hstring(std::string("DiagnosticsTitle-") + key));
            Automation::AutomationProperties::SetHeadingLevel(group.title, Automation::Peers::AutomationHeadingLevel::Level2);
            group.description.Style(resources.Lookup(box_value(L"SecondaryCaptionStyle")).as<Style>());
            group.description.TextWrapping(TextWrapping::Wrap);
            text.Children().Append(group.title); text.Children().Append(group.description); group.layout.Children().Append(text);
            group.values.ColumnSpacing(16); group.values.RowSpacing(8); group.values.VerticalAlignment(VerticalAlignment::Center);
            ColumnDefinition label; label.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star)); group.values.ColumnDefinitions().Append(label);
            ColumnDefinition value; value.Width(GridLengthHelper::Auto()); group.values.ColumnDefinitions().Append(value);
            int index = 0;
            for (const auto& definition : definitions)
            {
                Row row; row.definition = definition;
                RowDefinition height; height.Height(GridLengthHelper::Auto()); group.values.RowDefinitions().Append(height);
                row.label.Style(resources.Lookup(box_value(L"CaptionTextBlockStyle")).as<Style>());
                row.value.Style(resources.Lookup(box_value(L"BodyStrongTextBlockStyle")).as<Style>());
                row.label.TextWrapping(TextWrapping::Wrap); row.value.TextWrapping(TextWrapping::Wrap);
                row.value.IsTextSelectionEnabled(true); row.value.HorizontalAlignment(HorizontalAlignment::Right);
                Grid::SetRow(row.label, index); Grid::SetRow(row.value, index++); Grid::SetColumn(row.value, 1);
                Automation::AutomationProperties::SetAutomationId(row.value, to_hstring(std::string("Diagnostic-") + definition.key));
                group.values.Children().Append(row.label); group.values.Children().Append(row.value); rows.push_back(row);
            }
            group.layout.Children().Append(group.values); card.Child(group.layout); host.Children().Append(card); groups.push_back(group);
        };
        addGroup("performance", L"\xE950", L"Performance", L"Audio processing load and CPU use by each part of the app.", {
            {"dspLoadPercent", "DSP load", 2, "%"}, {"hostCpuPercent", "Host CPU", 2, "%"},
            {"uiCpuPercent", "UI CPU", 2, "%"}, {"workerCpuPercent", "Worker CPU", 2, "%"}});
        addGroup("reliability", L"\xE9D9", L"Audio reliability", L"Interruptions, processing errors and MIDI events that exceeded capacity.", {
            {"xRunCount", "Xruns", 0, ""}, {"processFailures", "Processing failures", 0, ""}, {"midiOverflow", "Dropped MIDI events", 0, ""}});
        addGroup("format", L"\xE713", L"Stream format", L"Requested settings alongside the values reported by the audio driver.", {
            {"requestedSampleRate", "Requested sample rate", 0, " Hz"}, {"sampleRate", "Driver sample rate", 0, " Hz"},
            {"requestedBufferSize", "Requested buffer", 0, " samples"}, {"bufferSize", "Driver buffer", 0, " samples"}});
        addGroup("latency", L"\xE916", L"Latency", L"Delay introduced by the plugin chain and the audio device.", {
            {"chainLatencySamples", "Chain latency", 0, " samples"}, {"inputLatency", "Driver input latency", 0, " samples"},
            {"outputLatency", "Driver output latency", 0, " samples"}});
        addGroup("activity", L"\xE9D2", L"Processing activity", L"Audio and MIDI processed during this host session.", {
            {"processedBlocks", "Processed blocks", 0, ""}, {"processedSamples", "Processed samples", 0, ""},
            {"inputMidiEvents", "Input MIDI events", 0, ""}, {"outputMidiEvents", "Output MIDI events", 0, ""}});
        resize(780); update("{}", catalog);
    }
    void resize(double)
    {
        using namespace winrt::Microsoft::UI::Xaml;
        using namespace winrt::Microsoft::UI::Xaml::Controls;
        for (const auto& group : groups)
        {
            Grid::SetRow(group.values, 1);
            Grid::SetColumn(group.values, 1);
            Grid::SetColumnSpan(group.values, 2);
            group.values.Width(std::numeric_limits<double>::quiet_NaN());
        }
    }

    void update(const std::string& json, ::LightHostWinUI::LocalizationCatalog& catalog)
    {
        using namespace winrt;
        for (const auto& group : groups)
        {
            group.title.Text(catalog.text(std::string("diagnostics.group.") + group.key, group.fallbackTitle));
            group.description.Text(catalog.text(std::string("diagnostics.group.") + group.key + ".description", group.fallbackDescription));
        }
        const auto cpu = cpuSampler.sample(lightHost::processCpuTicks(), GetTickCount64(), lightHost::processorCount());
        for (const auto& row : rows)
        {
            const auto& definition = row.definition;
            row.label.Text(catalog.text(std::string("diagnostics.") + definition.key, to_hstring(definition.label).c_str()));
            auto value = ipc::field(json, definition.key);
            if (std::string(definition.key) == "uiCpuPercent") value = cpu ? ipc::JsonValue::CreateNumberValue(*cpu) : ipc::JsonValue::CreateNullValue();
            std::wstring text = catalog.text("common.unavailable", L"Unavailable").c_str();
            if (value.ValueType() == ipc::JsonValueType::Number)
            {
                std::wostringstream formatted; formatted.imbue(std::locale::classic()); formatted << std::fixed << std::setprecision(definition.precision) << value.GetNumber();
                text = formatted.str();
                if (std::string(definition.unit) == " samples") text += L" " + std::wstring(catalog.text("common.samples", L"samples"));
                else text += to_hstring(definition.unit);
            }
            row.value.Text(text);
            Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(row.value, row.label.Text() + L": " + text);
        }
    }
    void resetCpuSampler() { cpuSampler.reset(); }
private:
    struct Definition { const char* key; const char* label; int precision; const char* unit; };
    struct Row { Definition definition{}; winrt::Microsoft::UI::Xaml::Controls::TextBlock label, value; };
    struct Group
    {
        const char* key{}; const wchar_t* fallbackTitle{}; const wchar_t* fallbackDescription{};
        winrt::Microsoft::UI::Xaml::Controls::Grid layout, values;
        winrt::Microsoft::UI::Xaml::Controls::TextBlock title, description;
    };
    std::vector<Row> rows;
    std::vector<Group> groups;
    lightHost::CpuUsageSampler cpuSampler;
};
}

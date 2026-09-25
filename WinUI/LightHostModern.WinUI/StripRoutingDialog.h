#pragma once
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <string>
#include <utility>
#include <vector>

namespace lightHostModern::ui
{
struct RoutingChannel
{
    int index = 0;
    std::wstring label;
    bool selected = false;
};

struct StripRoutingEditor
{
    winrt::Microsoft::UI::Xaml::Controls::ContentDialog dialog{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::CheckBox allInputs{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::CheckBox allOutputs{nullptr};
    std::vector<std::pair<int, winrt::Microsoft::UI::Xaml::Controls::CheckBox>> inputs, outputs;

    void build(winrt::Microsoft::UI::Xaml::XamlRoot const& root,
        winrt::hstring const& title, winrt::hstring const& save, winrt::hstring const& cancel,
        winrt::hstring const& allInputsLabel, winrt::hstring const& allOutputsLabel,
        bool allIn, bool allOut, std::vector<RoutingChannel> const& inputChannels, std::vector<RoutingChannel> const& outputChannels)
    {
        using namespace winrt::Microsoft::UI::Xaml;
        using namespace winrt::Microsoft::UI::Xaml::Controls;
        dialog = ContentDialog();
        dialog.XamlRoot(root);
        dialog.Title(winrt::box_value(title));
        dialog.PrimaryButtonText(save);
        dialog.CloseButtonText(cancel);
        allInputs = CheckBox(); allInputs.Content(winrt::box_value(allInputsLabel)); allInputs.IsChecked(allIn);
        allOutputs = CheckBox(); allOutputs.Content(winrt::box_value(allOutputsLabel)); allOutputs.IsChecked(allOut);
        StackPanel stack; stack.Spacing(8);
        stack.Children().Append(allInputs);
        for (auto const& channel : inputChannels) append(stack, inputs, channel, allInputs);
        stack.Children().Append(allOutputs);
        for (auto const& channel : outputChannels) append(stack, outputs, channel, allOutputs);
        dialog.Content(stack);
        dialog.PrimaryButtonClick([this](auto const&, ContentDialogButtonClickEventArgs const& args) {
            if ((!allInputs.IsChecked().GetBoolean() && chosen(inputs).empty())
                || (!allOutputs.IsChecked().GetBoolean() && chosen(outputs).empty()))
                args.Cancel(true);
        });
    }

    static std::vector<int> chosen(std::vector<std::pair<int, winrt::Microsoft::UI::Xaml::Controls::CheckBox>> const& boxes)
    {
        std::vector<int> values;
        for (auto const& box : boxes)
            if (box.second.IsChecked().GetBoolean()) values.push_back(box.first);
        return values;
    }

private:
    static void append(winrt::Microsoft::UI::Xaml::Controls::StackPanel const& stack,
        std::vector<std::pair<int, winrt::Microsoft::UI::Xaml::Controls::CheckBox>>& boxes,
        RoutingChannel const& channel, winrt::Microsoft::UI::Xaml::Controls::CheckBox const& all)
    {
        winrt::Microsoft::UI::Xaml::Controls::CheckBox box;
        box.Content(winrt::box_value(channel.label));
        box.IsChecked(channel.selected);
        box.IsEnabled(!all.IsChecked().GetBoolean());
        all.Checked([box](auto const&, auto const&) { box.IsEnabled(false); });
        all.Unchecked([box](auto const&, auto const&) { box.IsEnabled(true); });
        boxes.push_back({channel.index, box});
        stack.Children().Append(box);
    }
};
}

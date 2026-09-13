#pragma once
#include "SettingsPageView.g.h"

namespace winrt::LightHostWinUI::implementation
{
struct SettingsPageView : SettingsPageViewT<SettingsPageView>
{
    SettingsPageView();
    winrt::weak_ref<winrt::Windows::Foundation::IInspectable> owner;
    void OriginalRepositoryButton_Click(winrt::Windows::Foundation::IInspectable const& arg0, Microsoft::UI::Xaml::RoutedEventArgs const& arg1);
    void PreferredDeviceButton_Click(winrt::Windows::Foundation::IInspectable const& arg0, Microsoft::UI::Xaml::RoutedEventArgs const& arg1);
    void RepositoryButton_Click(winrt::Windows::Foundation::IInspectable const& arg0, Microsoft::UI::Xaml::RoutedEventArgs const& arg1);
};
}
namespace winrt::LightHostWinUI::factory_implementation
{
struct SettingsPageView : SettingsPageViewT<SettingsPageView, implementation::SettingsPageView> {};
}

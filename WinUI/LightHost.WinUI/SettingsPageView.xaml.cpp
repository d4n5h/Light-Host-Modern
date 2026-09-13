#include "pch.h"
#include "SettingsPageView.xaml.h"
#include "SettingsPageView.g.cpp"
#include "MainWindow.xaml.h"

namespace winrt::LightHostWinUI::implementation
{
SettingsPageView::SettingsPageView() { InitializeComponent(); }

void SettingsPageView::OriginalRepositoryButton_Click(winrt::Windows::Foundation::IInspectable const& arg0, Microsoft::UI::Xaml::RoutedEventArgs const& arg1)
{
    if (auto target = owner.get())
        winrt::get_self<MainWindow>(target.as<winrt::LightHostWinUI::MainWindow>())->OriginalRepositoryButton_Click(arg0, arg1);
}

void SettingsPageView::PreferredDeviceButton_Click(winrt::Windows::Foundation::IInspectable const& arg0, Microsoft::UI::Xaml::RoutedEventArgs const& arg1)
{
    if (auto target = owner.get())
        winrt::get_self<MainWindow>(target.as<winrt::LightHostWinUI::MainWindow>())->PreferredDeviceButton_Click(arg0, arg1);
}

void SettingsPageView::RepositoryButton_Click(winrt::Windows::Foundation::IInspectable const& arg0, Microsoft::UI::Xaml::RoutedEventArgs const& arg1)
{
    if (auto target = owner.get())
        winrt::get_self<MainWindow>(target.as<winrt::LightHostWinUI::MainWindow>())->RepositoryButton_Click(arg0, arg1);
}
}

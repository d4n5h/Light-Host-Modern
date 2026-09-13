#include "pch.h"
#include "DatabasePageView.xaml.h"
#include "DatabasePageView.g.cpp"
#include "MainWindow.xaml.h"

namespace winrt::LightHostWinUI::implementation
{
DatabasePageView::DatabasePageView()
{
    InitializeComponent();
}

void DatabasePageView::RetryPluginScan_Click(winrt::Windows::Foundation::IInspectable arg0, Microsoft::UI::Xaml::RoutedEventArgs arg1)
{
    if (auto target = owner.get())
        winrt::get_self<MainWindow>(target.as<winrt::LightHostWinUI::MainWindow>())->RetryPluginScan_Click(arg0, arg1);
}

void DatabasePageView::ViewScanFailures_Click(winrt::Windows::Foundation::IInspectable arg0, Microsoft::UI::Xaml::RoutedEventArgs arg1)
{
    if (auto target = owner.get())
        winrt::get_self<MainWindow>(target.as<winrt::LightHostWinUI::MainWindow>())->ViewScanFailures_Click(arg0, arg1);
}
}

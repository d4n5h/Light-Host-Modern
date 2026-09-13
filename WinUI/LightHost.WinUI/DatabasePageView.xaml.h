#pragma once
#include "DatabasePageView.g.h"

namespace winrt::LightHostWinUI::implementation
{
struct DatabasePageView : DatabasePageViewT<DatabasePageView>
{
    DatabasePageView();
    winrt::weak_ref<winrt::Windows::Foundation::IInspectable> owner;
    void RetryPluginScan_Click(winrt::Windows::Foundation::IInspectable arg0, Microsoft::UI::Xaml::RoutedEventArgs arg1);
    void ViewScanFailures_Click(winrt::Windows::Foundation::IInspectable arg0, Microsoft::UI::Xaml::RoutedEventArgs arg1);
};
}
namespace winrt::LightHostWinUI::factory_implementation
{
struct DatabasePageView : DatabasePageViewT<DatabasePageView, implementation::DatabasePageView> {};
}

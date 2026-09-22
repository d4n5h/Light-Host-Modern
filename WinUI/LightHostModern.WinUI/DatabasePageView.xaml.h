#pragma once
#include "DatabasePageView.g.h"

namespace winrt::LightHostModernWinUI::implementation
{
struct DatabasePageView : DatabasePageViewT<DatabasePageView>
{
    DatabasePageView();
    winrt::weak_ref<winrt::Windows::Foundation::IInspectable> owner;
    void RetryPluginScan_Click(winrt::Windows::Foundation::IInspectable arg0, Microsoft::UI::Xaml::RoutedEventArgs arg1);
    void ViewScanFailures_Click(winrt::Windows::Foundation::IInspectable arg0, Microsoft::UI::Xaml::RoutedEventArgs arg1);
};
}
namespace winrt::LightHostModernWinUI::factory_implementation
{
struct DatabasePageView : DatabasePageViewT<DatabasePageView, implementation::DatabasePageView> {};
}

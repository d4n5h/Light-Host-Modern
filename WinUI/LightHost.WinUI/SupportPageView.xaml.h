#pragma once
#include "SupportPageView.g.h"

namespace winrt::LightHostWinUI::implementation
{
struct SupportPageView : SupportPageViewT<SupportPageView>
{
    SupportPageView();
    winrt::weak_ref<winrt::Windows::Foundation::IInspectable> owner;

};
}
namespace winrt::LightHostWinUI::factory_implementation
{
struct SupportPageView : SupportPageViewT<SupportPageView, implementation::SupportPageView> {};
}

#pragma once
#include "AudioPageView.g.h"

namespace winrt::LightHostModernWinUI::implementation
{
struct AudioPageView : AudioPageViewT<AudioPageView>
{
    AudioPageView();
    winrt::weak_ref<winrt::Windows::Foundation::IInspectable> owner;

};
}
namespace winrt::LightHostModernWinUI::factory_implementation
{
struct AudioPageView : AudioPageViewT<AudioPageView, implementation::AudioPageView> {};
}

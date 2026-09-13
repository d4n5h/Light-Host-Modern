#pragma once
#include "AudioPageView.g.h"

namespace winrt::LightHostWinUI::implementation
{
struct AudioPageView : AudioPageViewT<AudioPageView>
{
    AudioPageView();
    winrt::weak_ref<winrt::Windows::Foundation::IInspectable> owner;

};
}
namespace winrt::LightHostWinUI::factory_implementation
{
struct AudioPageView : AudioPageViewT<AudioPageView, implementation::AudioPageView> {};
}

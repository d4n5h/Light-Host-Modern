#pragma once

#include "App.xaml.g.h"

namespace winrt::LightHostModernWinUI::implementation
{
    struct App : AppT<App>
    {
        App();
        void OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const&);

    private:
        void loadNativeWinUIResources();

        Microsoft::UI::Xaml::Window window{ nullptr };
        bool nativeWinUIResourcesLoaded = false;
    };
}

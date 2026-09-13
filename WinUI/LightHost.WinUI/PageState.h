#pragma once
#include "PageState.g.h"

namespace winrt::LightHostWinUI::implementation
{
struct PageState : PageStateT<PageState>
{
    PageState() = default;
    event_token PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler) { return changed.add(handler); }
    void PropertyChanged(event_token const& token) noexcept { changed.remove(token); }
    bool AudioLoaded() const { return audio; }
    bool PluginsLoaded() const { return plugins; }
    bool SupportLoaded() const { return support; }
    bool SettingsLoaded() const { return settings; }
    void AudioLoaded(bool value) { update(audio, value, L"AudioLoaded"); }
    void PluginsLoaded(bool value) { update(plugins, value, L"PluginsLoaded"); }
    void SupportLoaded(bool value) { update(support, value, L"SupportLoaded"); }
    void SettingsLoaded(bool value) { update(settings, value, L"SettingsLoaded"); }
private:
    void update(bool& field, bool value, hstring const& name)
    { if (field != value) { field = value; changed(*this, Microsoft::UI::Xaml::Data::PropertyChangedEventArgs(name)); } }
    bool audio = false, plugins = false, support = false, settings = false;
    winrt::event<Microsoft::UI::Xaml::Data::PropertyChangedEventHandler> changed;
};
}
namespace winrt::LightHostWinUI::factory_implementation
{
struct PageState : PageStateT<PageState, implementation::PageState> {};
}

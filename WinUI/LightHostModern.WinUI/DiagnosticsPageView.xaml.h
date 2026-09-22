#pragma once
#include "DiagnosticsPageView.g.h"

namespace winrt::LightHostModernWinUI::implementation
{
struct DiagnosticsPageView : DiagnosticsPageViewT<DiagnosticsPageView>
{
    DiagnosticsPageView();
};
}
namespace winrt::LightHostModernWinUI::factory_implementation
{
struct DiagnosticsPageView : DiagnosticsPageViewT<DiagnosticsPageView, implementation::DiagnosticsPageView> {};
}

#pragma once
#include "DiagnosticsPageView.g.h"

namespace winrt::LightHostWinUI::implementation
{
struct DiagnosticsPageView : DiagnosticsPageViewT<DiagnosticsPageView>
{
    DiagnosticsPageView();
};
}
namespace winrt::LightHostWinUI::factory_implementation
{
struct DiagnosticsPageView : DiagnosticsPageViewT<DiagnosticsPageView, implementation::DiagnosticsPageView> {};
}

#include "pch.h"
#include "PluginsPageView.xaml.h"
#include "PluginsPageView.g.cpp"
#include "MainWindow.xaml.h"

namespace winrt::LightHostModernWinUI::implementation
{
namespace
{
    Microsoft::UI::Xaml::Controls::TextBox findSearchInput(Microsoft::UI::Xaml::DependencyObject const& root)
    {
        using namespace Microsoft::UI::Xaml;
        if (auto input = root.try_as<Controls::TextBox>()) return input;
        for (int index = 0; index < Media::VisualTreeHelper::GetChildrenCount(root); ++index)
            if (auto input = findSearchInput(Media::VisualTreeHelper::GetChild(root, index))) return input;
        return nullptr;
    }
    void updateSearchAccessibility(Microsoft::UI::Xaml::Controls::AutoSuggestBox const& box)
    {
        using namespace Microsoft::UI::Xaml;
        box.ApplyTemplate();
        if (auto input = findSearchInput(box))
        {
            Automation::AutomationProperties::SetAutomationId(input, Automation::AutomationProperties::GetAutomationId(box) + L"Input");
            Automation::AutomationProperties::SetName(input, Automation::AutomationProperties::GetName(box));
        }
    }
}
PluginsPageView::PluginsPageView()
{
    InitializeComponent();
    for (auto list : {RunningPluginsListView(), InstalledPluginsListView()})
        list.ContainerContentChanging([this](Microsoft::UI::Xaml::Controls::ListViewBase const&,
            Microsoft::UI::Xaml::Controls::ContainerContentChangingEventArgs const& args) {
            using namespace Microsoft::UI::Xaml;
            if (!args.InRecycleQueue())
                if (const auto item = args.Item().try_as<winrt::LightHostModernWinUI::PluginItem>())
                {
                    args.ItemContainer().Margin({contentInset, 0, contentInset, 0});
                    Automation::AutomationProperties::SetAutomationId(args.ItemContainer(), (item.IsGroupHeader() ? L"" : item.Running() ? L"running-" : L"installed-") + item.Id());
                    Automation::AutomationProperties::SetName(args.ItemContainer(), item.AccessibleName());
                    args.ItemContainer().IsTabStop(!item.IsGroupHeader());
                }
        });
    for (auto list : {RunningPluginsListView(), InstalledPluginsListView()})
        list.Loaded([this](const auto&, const auto&) { setContentInsets(contentInset); });
    for (auto search : {RunningPluginSearchBox(), InstalledPluginSearchBox()})
    {
        search.Loaded([](Windows::Foundation::IInspectable const& sender, auto const&) {
            updateSearchAccessibility(sender.as<Microsoft::UI::Xaml::Controls::AutoSuggestBox>());
        });
        // A collapsed tab can receive Loaded before its template is created.
        search.SizeChanged([](Windows::Foundation::IInspectable const& sender, auto const&) {
            updateSearchAccessibility(sender.as<Microsoft::UI::Xaml::Controls::AutoSuggestBox>());
        });
    }
}

void PluginsPageView::setContentInsets(double inset)
{
    using namespace Microsoft::UI::Xaml;
    contentInset = inset;
    for (auto card : {PluginSectionCard(), RunningToolbarCard(), InstalledToolbarCard()})
        card.Margin({inset, 0, inset, 0});
    for (auto list : {RunningPluginsListView(), InstalledPluginsListView()})
        if (auto panel = list.ItemsPanelRoot().try_as<Controls::ItemsStackPanel>())
            for (int index = (std::max)(0, panel.FirstCacheIndex()); index <= panel.LastCacheIndex(); ++index)
                if (auto container = list.ContainerFromIndex(index).try_as<FrameworkElement>())
                    container.Margin({inset, 0, inset, 0});
}

void PluginsPageView::Toolbar_SizeChanged(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::SizeChangedEventArgs const& args)
{
    const auto toolbar = sender.as<Microsoft::UI::Xaml::Controls::CommandBar>();
    const bool running = toolbar == RunningToolbar();
    const auto available = args.NewSize().Width;
    // Both tabs reserve the same search width, independent of their commands.
    (running ? RunningPluginSearchBox() : InstalledPluginSearchBox()).Width(
        (std::max)(160.0, (std::min)(420.0, available * 0.4)));
}

void PluginsPageView::refreshSearchAccessibility()
{
    updateSearchAccessibility(RunningPluginSearchBox());
    updateSearchAccessibility(InstalledPluginSearchBox());
}

void PluginsPageView::PluginCard_PointerEntered(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&)
{
    using namespace Microsoft::UI::Xaml;
    sender.as<Controls::Border>().Style(Resources().Lookup(box_value(L"PluginCardPointerOverStyle")).as<Microsoft::UI::Xaml::Style>());
}

void PluginsPageView::PluginCard_PointerExited(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&)
{
    using namespace Microsoft::UI::Xaml;
    sender.as<Controls::Border>().Style(Application::Current().Resources().Lookup(box_value(L"AppCardStyle")).as<Microsoft::UI::Xaml::Style>());
}



void PluginsPageView::GlobalAudioControl_Click(winrt::Windows::Foundation::IInspectable arg0, Microsoft::UI::Xaml::RoutedEventArgs arg1)
{
    if (auto target = owner.get())
        winrt::get_self<MainWindow>(target.as<winrt::LightHostModernWinUI::MainWindow>())->GlobalAudioControl_Click(arg0, arg1);
}

void PluginsPageView::PluginActions_Click(winrt::Windows::Foundation::IInspectable const& arg0, Microsoft::UI::Xaml::RoutedEventArgs const& arg1)
{
    if (auto target = owner.get())
        winrt::get_self<MainWindow>(target.as<winrt::LightHostModernWinUI::MainWindow>())->PluginActions_Click(arg0, arg1);
}



void PluginsPageView::RunningPluginItem_DragOver(winrt::Windows::Foundation::IInspectable const& arg0, Microsoft::UI::Xaml::DragEventArgs const& arg1)
{
    if (auto target = owner.get())
        winrt::get_self<MainWindow>(target.as<winrt::LightHostModernWinUI::MainWindow>())->RunningPluginItem_DragOver(arg0, arg1);
}

void PluginsPageView::RunningPluginItem_Drop(winrt::Windows::Foundation::IInspectable arg0, Microsoft::UI::Xaml::DragEventArgs arg1)
{
    if (auto target = owner.get())
        winrt::get_self<MainWindow>(target.as<winrt::LightHostModernWinUI::MainWindow>())->RunningPluginItem_Drop(arg0, arg1);
}

void PluginsPageView::RunningPluginsListView_DragItemsCompleted(winrt::Windows::Foundation::IInspectable const& arg0, Microsoft::UI::Xaml::Controls::DragItemsCompletedEventArgs const& arg1)
{
    if (auto target = owner.get())
        winrt::get_self<MainWindow>(target.as<winrt::LightHostModernWinUI::MainWindow>())->RunningPluginsListView_DragItemsCompleted(arg0, arg1);
}

void PluginsPageView::RunningPluginsListView_DragItemsStarting(winrt::Windows::Foundation::IInspectable const& arg0, Microsoft::UI::Xaml::Controls::DragItemsStartingEventArgs const& arg1)
{
    if (auto target = owner.get())
        winrt::get_self<MainWindow>(target.as<winrt::LightHostModernWinUI::MainWindow>())->RunningPluginsListView_DragItemsStarting(arg0, arg1);
}

void PluginsPageView::InstalledPluginsListView_DragItemsStarting(winrt::Windows::Foundation::IInspectable const& arg0, Microsoft::UI::Xaml::Controls::DragItemsStartingEventArgs const& arg1)
{
    if (auto target = owner.get())
        winrt::get_self<MainWindow>(target.as<winrt::LightHostModernWinUI::MainWindow>())->InstalledPluginsListView_DragItemsStarting(arg0, arg1);
}

void PluginsPageView::StripMixer_DragOver(winrt::Windows::Foundation::IInspectable const& arg0, Microsoft::UI::Xaml::DragEventArgs const& arg1)
{
    if (auto target = owner.get())
        winrt::get_self<MainWindow>(target.as<winrt::LightHostModernWinUI::MainWindow>())->StripMixer_DragOver(arg0, arg1);
}

void PluginsPageView::StripMixer_Drop(winrt::Windows::Foundation::IInspectable const& arg0, Microsoft::UI::Xaml::DragEventArgs const& arg1)
{
    if (auto target = owner.get())
        winrt::get_self<MainWindow>(target.as<winrt::LightHostModernWinUI::MainWindow>())->StripMixer_Drop(arg0, arg1);
}

void PluginsPageView::RunningPluginsListView_DragOver(winrt::Windows::Foundation::IInspectable const& arg0, Microsoft::UI::Xaml::DragEventArgs const& arg1)
{
    if (auto target = owner.get())
        winrt::get_self<MainWindow>(target.as<winrt::LightHostModernWinUI::MainWindow>())->RunningPluginsListView_DragOver(arg0, arg1);
}

void PluginsPageView::RunningPluginsListView_Drop(winrt::Windows::Foundation::IInspectable const& arg0, Microsoft::UI::Xaml::DragEventArgs const& arg1)
{
    if (auto target = owner.get())
        winrt::get_self<MainWindow>(target.as<winrt::LightHostModernWinUI::MainWindow>())->RunningPluginsListView_Drop(arg0, arg1);
}



}

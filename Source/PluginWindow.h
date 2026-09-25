#ifndef PluginWindow_h
#define PluginWindow_h

#include <juce_audio_utils/juce_audio_utils.h>

using namespace juce;

ApplicationProperties& getAppProperties();

class PluginWindow  : public DocumentWindow
{
public:
    enum WindowFormatType
    {
        Normal = 0,
        Generic,
        Programs,
        Parameters,
        NumTypes
    };

    PluginWindow (Component* pluginEditor, AudioProcessor&, NamedValueSet&, WindowFormatType);
    ~PluginWindow();

    static PluginWindow* getWindowFor (AudioProcessor&, NamedValueSet&, WindowFormatType);

    static bool isOpenFor (AudioProcessor&);
    static void closeCurrentlyOpenWindowsFor (AudioProcessor&);
    static void closeAllCurrentlyOpenWindows();
    static bool containsActiveWindows();

    void moved() override;
    void resized() override;
    void closeButtonPressed() override;

private:
    void storeBounds();
    AudioProcessor* owner;
    NamedValueSet* windowProperties;
    WindowFormatType type;
    bool tracking = false;

    float getDesktopScaleFactor() const override     { return 1.0f; }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginWindow)
};

inline String toString (PluginWindow::WindowFormatType type)
{
    switch (type)
    {
        case PluginWindow::Normal:     return "Normal";
        case PluginWindow::Generic:    return "Generic";
        case PluginWindow::Programs:   return "Programs";
        case PluginWindow::Parameters: return "Parameters";
        default:                       return String();
    }
}

inline String getLastXProp (PluginWindow::WindowFormatType type)    { return "uiLastX_" + toString (type); }
inline String getLastYProp (PluginWindow::WindowFormatType type)    { return "uiLastY_" + toString (type); }
inline String getLastWProp (PluginWindow::WindowFormatType type)    { return "uiLastW_" + toString (type); }
inline String getLastHProp (PluginWindow::WindowFormatType type)    { return "uiLastH_" + toString (type); }
inline String getOpenProp  (PluginWindow::WindowFormatType type)    { return "uiopen_"  + toString (type); }


#endif /* PluginWindow_hpp */

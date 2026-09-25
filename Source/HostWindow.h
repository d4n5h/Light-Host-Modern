#pragma once
#include "AudioEngine.h"
#include <functional>

class HostWindow : public juce::DocumentWindow
{
public:
    HostWindow(AudioEngine&, std::function<void()> refreshTray);
    void closeButtonPressed() override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HostWindow)
};

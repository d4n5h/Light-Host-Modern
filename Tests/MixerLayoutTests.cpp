#include "MixerView.h"
#include <iostream>

int main()
{
    const auto withPan = layoutChannelStrip({0, 0, 128, 900}, true);
    const auto master = layoutChannelStrip({0, 0, 128, 900}, false);
    if (withPan.fader.getBottom() != master.fader.getBottom())
    {
        std::cerr << "Faders do not share a bottom edge\n";
        return 1;
    }
    if (std::abs(withPan.fader.getBottom() - master.fader.getBottom()) > 2)
    {
        std::cerr << "Fader bottoms differ by more than 2 px\n";
        return 1;
    }
    float value = 1.0f;
    const auto expectPan = [&](const char* text, float wanted) {
        if (!parsePanText(text, value) || std::abs(value - wanted) > 0.001f)
        {
            std::cerr << "Pan parse failed for " << text << "\n";
            return false;
        }
        return true;
    };
    const auto expectGain = [&](const char* text, float wanted) {
        if (!parseGainText(text, value) || std::abs(value - wanted) > 0.001f)
        {
            std::cerr << "Gain parse failed for " << text << "\n";
            return false;
        }
        return true;
    };
    if (!expectPan("0", 0.0f) || !expectPan("C", 0.0f) || !expectPan("50L", -0.5f) || !expectPan("50R", 0.5f) || !expectPan("100L", -1.0f))
        return 1;
    if (!expectGain("-10", -10.0f) || !expectGain("3.5 dB", 3.5f) || parseGainText("", value))
    {
        std::cerr << "Gain parse failed\n";
        return 1;
    }
    std::cout << "Mixer strip faders align\n";
    return 0;
}

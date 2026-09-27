#pragma once
#include "AudioEngine.h"

struct StripLayout
{
    juce::Rectangle<int> name, route, inserts, pan, fader, gain;
};

inline StripLayout layoutChannelStrip(juce::Rectangle<int> area, bool showPan)
{
    area = area.reduced(8);
    StripLayout layout;
    layout.name = area.removeFromTop(28);
    layout.route = area.removeFromTop(32);
    auto faderArea = area.removeFromBottom(220);
    layout.gain = faderArea.removeFromBottom(20);
    layout.fader = faderArea;
    if (showPan)
        layout.pan = area.removeFromBottom(72);
    layout.inserts = area;
    return layout;
}

inline bool categoryMatches(const juce::String& category, const juce::String& group)
{
    if (group.isEmpty() || group == "All") return true;
    const char* names[] = { "Dynamics", "EQ", "Reverb", "Delay", "Modulation", "Distortion", "Filter", "Pitch", "Spatial", "Instrument", "Analyzer", "Restoration", "Tools" };
    const auto known = [&](const juce::String& text) {
        for (const auto* name : names) if (text.containsIgnoreCase(name)) return true;
        return false;
    };
    if (group == "Other") return category.isEmpty() || !known(category);
    return category.containsIgnoreCase(group);
}

inline bool pluginMatches(const juce::PluginDescription& plugin, const juce::String& query,
                          const juce::String& format, const juce::String& manufacturer,
                          const juce::String& typeGroup = {})
{
    if (format.isNotEmpty() && plugin.pluginFormatName != format) return false;
    if (manufacturer.isNotEmpty() && plugin.manufacturerName != manufacturer) return false;
    if (!categoryMatches(plugin.category, typeGroup)) return false;
    if (query.isEmpty()) return true;
    return plugin.name.containsIgnoreCase(query) || plugin.manufacturerName.containsIgnoreCase(query);
}

inline bool parseGainText(juce::String text, float& value)
{
    text = text.trim().toLowerCase().replace("db", "").trim();
    if (text.isEmpty() || !text.containsOnly("0123456789.-+")) return false;
    value = juce::jlimit(-60.0f, 12.0f, text.getFloatValue());
    return true;
}

inline bool parsePanText(juce::String text, float& value)
{
    text = text.trim().toLowerCase();
    if (text == "0" || text == "c" || text == "center") { value = 0.0f; return true; }
    const bool left = text.endsWithChar('l') || text.startsWithChar('l');
    const bool right = text.endsWithChar('r') || text.startsWithChar('r');
    auto number = text.removeCharacters("lr ");
    if ((left || right) && number.isEmpty()) { value = left ? -1.0f : 1.0f; return true; }
    if (number.isEmpty() || !number.containsOnly("0123456789.-+")) return false;
    const float amount = number.getFloatValue() / 100.0f;
    if (left) value = juce::jlimit(-1.0f, 1.0f, -std::abs(amount));
    else if (right) value = juce::jlimit(-1.0f, 1.0f, std::abs(amount));
    else value = juce::jlimit(-1.0f, 1.0f, amount);
    return true;
}

class ClickLabel : public juce::Label
{
public:
    std::function<void()> onClick;
    void mouseUp(const juce::MouseEvent& event) override
    {
        if (event.mouseWasClicked() && onClick) onClick();
    }
};

class MarkButton : public juce::Button
{
public:
    MarkButton(const juce::String& text, juce::Colour active) : juce::Button(text), onColour(active) {}
    void paintButton(juce::Graphics& graphics, bool over, bool down) override
    {
        auto colour = getToggleState() ? onColour : juce::Colour(0xff2c2c2c);
        if (down) colour = colour.darker(0.2f);
        else if (over) colour = colour.brighter(0.12f);
        graphics.setColour(colour);
        graphics.fillRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 3.0f);
        graphics.setColour(juce::Colours::white);
        graphics.drawText(getButtonText(), getLocalBounds(), juce::Justification::centred, false);
    }
private:
    juce::Colour onColour;
};

inline juce::Colour stripPalette(int color)
{
    switch (color)
    {
        case 1: return juce::Colour(0xff4da3ff);
        case 2: return juce::Colour(0xff3dcc7a);
        case 3: return juce::Colour(0xffff9f43);
        case 4: return juce::Colour(0xffff5d5d);
        case 5: return juce::Colour(0xffb07cff);
        case 6: return juce::Colour(0xff2ec4b6);
        case 7: return juce::Colour(0xffffd166);
        case 8: return juce::Colour(0xffff7ab6);
        default: return juce::Colour(0xff3a3a3a);
    }
}

inline juce::Colour stripFill(int index, const juce::String& hex)
{
    const auto dark = juce::Colour(0xff1e1e1e);
    if (hex.length() == 6) return dark.interpolatedWith(juce::Colour::fromString("ff" + hex), 0.45f);
    if (index > 0) return dark.interpolatedWith(stripPalette(index), 0.45f);
    return juce::Colour(0xff333333);
}

class StripNameButton : public juce::TextButton
{
public:
    std::function<juce::String()> dragDescription;
    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (!dragDescription || event.getDistanceFromDragStart() <= 8) return;
        const auto description = dragDescription();
        if (description.isEmpty()) return;
        if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor(this))
            container->startDragging(description, getParentComponent());
    }
};

class PluginListModel : public juce::ListBoxModel
{
public:
    std::vector<juce::PluginDescription> plugins;
    std::function<void(int)> onActivate;
    std::function<void(int)> onPopup;
    int getNumRows() override { return (int) plugins.size(); }
    void paintListBoxItem(int row, juce::Graphics& graphics, int width, int height, bool selected) override
    {
        if (row < 0 || row >= getNumRows()) return;
        graphics.fillAll(selected ? juce::Colour(0xff3d5a80) : juce::Colour(0xff2c2c2c));
        const auto& plugin = plugins[(size_t) row];
        graphics.setColour(juce::Colour(0xfff2f2f2));
        graphics.drawText(plugin.name, 8, 0, width / 2 - 8, height, juce::Justification::centredLeft, true);
        graphics.setColour(juce::Colour(0xffb8b8b8));
        graphics.drawText(plugin.category.isEmpty() ? "Other" : plugin.category, width / 2, 0, width / 4, height, juce::Justification::centredLeft, true);
        graphics.drawText(plugin.pluginFormatName, width * 3 / 4, 0, width / 4 - 8, height, juce::Justification::centredRight, true);
    }
    void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override { if (onActivate) onActivate(row); }
    void listBoxItemClicked(int row, const juce::MouseEvent& event) override
    {
        if (event.mods.isPopupMenu() && onPopup) onPopup(row);
    }
};

class ShellLookAndFeel : public juce::LookAndFeel_V4
{
public:
    ShellLookAndFeel()
    {
        const auto text = juce::Colour(0xfff2f2f2);
        const auto control = juce::Colour(0xff2c2c2c);
        const auto selected = juce::Colour(0xff3d5a80);
        setColour(juce::Label::textColourId, text);
        setColour(juce::ToggleButton::textColourId, text);
        setColour(juce::ToggleButton::tickColourId, juce::Colour(0xff4da3ff));
        setColour(juce::ToggleButton::tickDisabledColourId, juce::Colour(0xff6a6a6a));
        setColour(juce::ComboBox::textColourId, text);
        setColour(juce::ComboBox::backgroundColourId, control);
        setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff4a4a4a));
        setColour(juce::ComboBox::arrowColourId, text);
        setColour(juce::PopupMenu::backgroundColourId, control);
        setColour(juce::PopupMenu::textColourId, text);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, selected);
        setColour(juce::PopupMenu::highlightedTextColourId, text);
        setColour(juce::TextEditor::textColourId, text);
        setColour(juce::TextEditor::backgroundColourId, control);
        setColour(juce::TextEditor::highlightColourId, selected);
        setColour(juce::TextEditor::highlightedTextColourId, text);
        setColour(juce::TextEditor::outlineColourId, juce::Colour(0xff4a4a4a));
        setColour(juce::ListBox::textColourId, text);
        setColour(juce::ListBox::backgroundColourId, juce::Colour(0xff1e1e1e));
        setColour(juce::TextButton::buttonColourId, control);
        setColour(juce::TextButton::buttonOnColourId, selected);
        setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        setColour(juce::Slider::thumbColourId, juce::Colour(0xff4da3ff));
        setColour(juce::Slider::trackColourId, juce::Colour(0xff6a6a6a));
        setColour(juce::Slider::backgroundColourId, juce::Colour(0xff2a2a2a));
        setColour(juce::AlertWindow::backgroundColourId, control);
        setColour(juce::AlertWindow::textColourId, text);
        setColour(juce::AlertWindow::outlineColourId, juce::Colour(0xff4a4a4a));
    }

    void drawButtonBackground(juce::Graphics& graphics, juce::Button& button, const juce::Colour&, bool, bool) override
    {
        auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
        graphics.setColour(button.getToggleState() ? juce::Colour(0xff3d5a80) : juce::Colour(0xff2c2c2c));
        graphics.fillRoundedRectangle(bounds, 4.0f);
        if (button.getToggleState())
        {
            graphics.setColour(juce::Colour(0xff4da3ff));
            graphics.fillRect(button.getLocalBounds().removeFromLeft(3));
        }
    }
};

inline ShellLookAndFeel& shellLook()
{
    static ShellLookAndFeel look;
    return look;
}

class ChannelStripComponent : public juce::Component,
                              public juce::DragAndDropTarget,
                              private juce::Slider::Listener
{
public:
    ChannelStripComponent(AudioEngine&, bool master);
    ~ChannelStripComponent() override;
    void setStrip(const lightHostModern::ChainStrip&, const std::vector<lightHostModern::PluginInstanceRecord>&);
    void setMaster(float gainDb, const std::vector<lightHostModern::PluginInstanceRecord>& plugins);
    void setMeter(float peak);
    void applyLive(float gainDb, float panValue, bool muted, bool soloed);
    void applyMasterLive(float gainDb);
    const juce::String& group() const { return groupName; }
    const juce::String& id() const { return stripId; }
    bool isMaster() const { return master; }
    std::function<void(const juce::String& draggedId, bool after)> onStripDrop;
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDrag(const juce::MouseEvent&) override;
    bool isInterestedInDragSource(const SourceDetails&) override;
    void itemDragEnter(const SourceDetails&) override;
    void itemDragMove(const SourceDetails&) override;
    void itemDragExit(const SourceDetails&) override;
    void itemDropped(const SourceDetails&) override;

private:
    class InsertButton;
    void sliderValueChanged(juce::Slider*) override;
    void showStripMenu();
    void showPluginMenu(const juce::String& instanceId);
    void applyRouting();
    void editValue(juce::Label& label, bool gain);
    void pickColour();
    void updateDrag(const SourceDetails&);
    void showPluginGap(int y);
    void clearDrag();
    void dropPlugin(const juce::String& source);
    void showInputMenu();

    struct InputCombo : juce::ComboBox
    {
        std::function<void()> onMenu;
        void showPopup() override
        {
            hidePopup();
            if (onMenu) onMenu();
        }
    };

    AudioEngine& engine;
    bool master = false;
    bool applying = false;
    juce::String stripId;
    StripNameButton nameButton;
    ClickLabel gainLabel, panReadout;
    InputCombo inputBox;
    juce::ComboBox outputBox, outputBox2;
    MarkButton muteStrip { "M", juce::Colour(0xffc44545) }, soloStrip { "S", juce::Colour(0xffd4a017) };
    juce::TextButton plusButton;
    juce::Component insertList;
    juce::Viewport insertViewport;
    juce::Slider pan, fader;
    juce::OwnedArray<InsertButton> inserts;
    std::vector<juce::String> insertIds;
    juce::Rectangle<int> meterBounds, tickBounds;
    float meterPeak = 0;
    int color = 0;
    juce::String colourHex;
    int pluginDropY = -1;
    int stripEdge = -1;
    juce::String pluginInsertBefore;
    bool stereo = false;
    juce::String groupName, inputSignature, outputSignature;
    std::vector<int> inputChannels, outputChannels;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChannelStripComponent)
};

class MixerView : public juce::Component, private juce::Timer
{
public:
    explicit MixerView(AudioEngine&);
    void refresh();
    void resized() override;
    void paint(juce::Graphics&) override;

private:
    void timerCallback() override;
    void toggleGroup(const juce::String& name);
    void layoutStrips();
    void startSavedRecording();
    void startSavedStream();
    void moveStripTo(const juce::String& draggedId, ChannelStripComponent& target, bool after);
    AudioEngine& engine;
    juce::TextButton addButton, undoButton, redoButton, muteButton, bypassButton, newProfile;
    juce::TextButton recordButton, recordPause, recordStop, streamButton, streamPause, streamStop;
    juce::ComboBox profiles;
    juce::Viewport viewport;
    juce::Component row;
    juce::OwnedArray<ChannelStripComponent> strips;
    juce::OwnedArray<juce::TextButton> groupHeaders;
    juce::StringArray collapsed;
    juce::String layoutKey;
    uint64 seenSurface = 0;
    bool applying = false;
};

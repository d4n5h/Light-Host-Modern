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

namespace studio
{
    const juce::Colour background { 0xff12141a };
    const juce::Colour panel      { 0xff1a1d24 };
    const juce::Colour control    { 0xff262a33 };
    const juce::Colour line       { 0xff3a3f4b };
    const juce::Colour text       { 0xffe7e9ee };
    const juce::Colour muted      { 0xff9aa0ab };
    const juce::Colour accent     { 0xff4da3ff };
    inline juce::Colour selectedFill() { return control.interpolatedWith(accent, 0.45f); }
}

class MarkButton : public juce::Button
{
public:
    MarkButton(const juce::String& text, juce::Colour active) : juce::Button(text), onColour(active) {}
    void paintButton(juce::Graphics& graphics, bool over, bool down) override
    {
        auto bounds = getLocalBounds().toFloat().reduced(1.0f);
        auto colour = getToggleState() ? onColour : studio::control;
        if (down) colour = colour.darker(0.15f);
        else if (over) colour = colour.brighter(0.1f);
        graphics.setColour(colour);
        graphics.fillRoundedRectangle(bounds, 6.0f);
        if (!getToggleState())
        {
            graphics.setColour(studio::line);
            graphics.drawRoundedRectangle(bounds, 6.0f, 1.0f);
        }
        graphics.setColour(getToggleState() ? juce::Colours::white : studio::muted);
        graphics.setFont(juce::Font(juce::FontOptions(12.0f)));
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
    const auto dark = studio::panel;
    if (hex.length() == 6) return dark.interpolatedWith(juce::Colour::fromString("ff" + hex), 0.45f);
    if (index > 0) return dark.interpolatedWith(stripPalette(index), 0.45f);
    return studio::panel;
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
        graphics.fillAll(selected ? studio::selectedFill() : studio::control);
        const auto& plugin = plugins[(size_t) row];
        graphics.setColour(studio::text);
        graphics.drawText(plugin.name, 8, 0, width / 2 - 8, height, juce::Justification::centredLeft, true);
        graphics.setColour(studio::muted);
        graphics.drawText(plugin.category.isEmpty() ? "Other" : plugin.category, width / 2, 0, width / 4, height, juce::Justification::centredLeft, true);
        graphics.drawText(plugin.pluginFormatName, width * 3 / 4, 0, width / 4 - 8, height, juce::Justification::centredRight, true);
    }
    void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override { if (onActivate) onActivate(row); }
    void listBoxItemClicked(int row, const juce::MouseEvent& event) override
    {
        if (event.mods.isPopupMenu() && onPopup) onPopup(row);
    }
};

class CaptionButton : public juce::Button
{
public:
    explicit CaptionButton(int kindIn) : juce::Button({}), kind(kindIn) {}
    void paintButton(juce::Graphics& graphics, bool over, bool down) override
    {
        const bool close = kind == juce::DocumentWindow::closeButton;
        if (over || down)
        {
            graphics.setColour(close ? juce::Colour(0xffff5d5d) : studio::control.brighter(down ? 0.0f : 0.08f));
            graphics.fillRect(getLocalBounds());
        }
        auto ink = close && (over || down) ? juce::Colours::white : over ? studio::text : studio::muted;
        auto mark = getLocalBounds().toFloat().reduced(16.0f, 12.0f);
        graphics.setColour(ink);
        if (kind == juce::DocumentWindow::minimiseButton)
            graphics.drawLine(mark.getX(), mark.getBottom() - 1.0f, mark.getRight(), mark.getBottom() - 1.0f, 1.4f);
        else if (kind == juce::DocumentWindow::maximiseButton && getToggleState())
        {
            auto back = mark.reduced(0.5f).translated(2.0f, -2.0f);
            auto front = mark.reduced(0.5f).translated(-2.0f, 2.0f);
            graphics.drawRect(back, 1.3f);
            graphics.setColour(over || down ? studio::control.brighter(0.08f) : studio::panel);
            graphics.fillRect(front);
            graphics.setColour(ink);
            graphics.drawRect(front, 1.3f);
        }
        else if (kind == juce::DocumentWindow::maximiseButton)
            graphics.drawRect(mark, 1.3f);
        else
        {
            graphics.drawLine(mark.getX(), mark.getY(), mark.getRight(), mark.getBottom(), 1.4f);
            graphics.drawLine(mark.getRight(), mark.getY(), mark.getX(), mark.getBottom(), 1.4f);
        }
    }
private:
    int kind;
};

class ShellLookAndFeel : public juce::LookAndFeel_V4
{
public:
    ShellLookAndFeel()
    {
        const auto selected = studio::selectedFill();
        setColour(juce::Label::textColourId, studio::text);
        setColour(juce::ToggleButton::textColourId, studio::text);
        setColour(juce::ToggleButton::tickColourId, studio::accent);
        setColour(juce::ToggleButton::tickDisabledColourId, studio::muted);
        setColour(juce::ComboBox::textColourId, studio::text);
        setColour(juce::ComboBox::backgroundColourId, studio::control);
        setColour(juce::ComboBox::outlineColourId, studio::line);
        setColour(juce::ComboBox::arrowColourId, studio::text);
        setColour(juce::PopupMenu::backgroundColourId, studio::control);
        setColour(juce::PopupMenu::textColourId, studio::text);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, selected);
        setColour(juce::PopupMenu::highlightedTextColourId, studio::text);
        setColour(juce::TextEditor::textColourId, studio::text);
        setColour(juce::TextEditor::backgroundColourId, studio::control);
        setColour(juce::TextEditor::highlightColourId, selected);
        setColour(juce::TextEditor::highlightedTextColourId, studio::text);
        setColour(juce::TextEditor::outlineColourId, studio::line);
        setColour(juce::ListBox::textColourId, studio::text);
        setColour(juce::ListBox::backgroundColourId, studio::background);
        setColour(juce::TextButton::buttonColourId, studio::control);
        setColour(juce::TextButton::buttonOnColourId, selected);
        setColour(juce::TextButton::textColourOffId, studio::text);
        setColour(juce::TextButton::textColourOnId, studio::text);
        setColour(juce::Slider::thumbColourId, studio::accent);
        setColour(juce::Slider::trackColourId, studio::line);
        setColour(juce::Slider::backgroundColourId, studio::panel);
        setColour(juce::ScrollBar::thumbColourId, studio::line);
        setColour(juce::ScrollBar::backgroundColourId, studio::panel);
        setColour(juce::AlertWindow::backgroundColourId, studio::control);
        setColour(juce::AlertWindow::textColourId, studio::text);
        setColour(juce::AlertWindow::outlineColourId, studio::line);
    }

    juce::Font getTextButtonFont(juce::TextButton&, int) override { return juce::Font(juce::FontOptions(13.0f)); }
    juce::Font getLabelFont(juce::Label&) override { return juce::Font(juce::FontOptions(13.0f)); }

    void drawButtonBackground(juce::Graphics& graphics, juce::Button& button, const juce::Colour&, bool over, bool down) override
    {
        auto colour = button.getToggleState() ? studio::control.brighter(0.08f) : studio::control;
        if (down) colour = colour.darker(0.12f);
        else if (over) colour = colour.brighter(0.08f);
        graphics.setColour(colour);
        graphics.fillRoundedRectangle(button.getLocalBounds().toFloat().reduced(0.5f), 5.0f);
        if (button.getToggleState())
        {
            graphics.setColour(studio::accent);
            graphics.fillRect(button.getLocalBounds().removeFromLeft(3));
        }
    }

    void drawComboBox(juce::Graphics& graphics, int width, int height, bool, int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox& box) override
    {
        auto bounds = juce::Rectangle<float>(0.5f, 0.5f, (float) width - 1.0f, (float) height - 1.0f);
        graphics.setColour(studio::control);
        graphics.fillRoundedRectangle(bounds, 6.0f);
        graphics.setColour(studio::line);
        graphics.drawRoundedRectangle(bounds, 6.0f, 1.0f);
        juce::Path arrow;
        const float centreX = (float) buttonX + buttonW * 0.5f;
        const float centreY = (float) buttonY + buttonH * 0.5f;
        arrow.addTriangle(centreX - 4.0f, centreY - 2.0f, centreX + 4.0f, centreY - 2.0f, centreX, centreY + 3.0f);
        graphics.setColour(box.findColour(juce::ComboBox::arrowColourId));
        graphics.fillPath(arrow);
    }

    int getSliderThumbRadius(juce::Slider& slider) override
    {
        return slider.isVertical() ? 8 : juce::LookAndFeel_V4::getSliderThumbRadius(slider);
    }

    void drawLinearSlider(juce::Graphics& graphics, int x, int y, int width, int height,
                          float sliderPos, float minSliderPos, float maxSliderPos,
                          juce::Slider::SliderStyle style, juce::Slider& slider) override
    {
        if (style != juce::Slider::LinearVertical && style != juce::Slider::LinearBarVertical)
        {
            juce::LookAndFeel_V4::drawLinearSlider(graphics, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
            return;
        }
        const float radius = (float) getSliderThumbRadius(slider);
        const float centreX = x + width * 0.5f;
        juce::Rectangle<float> track(centreX - 1.5f, (float) y + radius, 3.0f, juce::jmax(0.0f, (float) height - radius * 2.0f));
        graphics.setColour(studio::line);
        graphics.fillRoundedRectangle(track, 1.5f);
        graphics.setColour(studio::accent);
        graphics.fillRoundedRectangle({ track.getX(), sliderPos, track.getWidth(), juce::jmax(0.0f, track.getBottom() - sliderPos) }, 1.5f);
        const float capWidth = juce::jmin(16.0f, (float) width - 4.0f);
        juce::Rectangle<float> cap(centreX - capWidth * 0.5f, sliderPos - 6.0f, capWidth, 12.0f);
        graphics.setColour(studio::text);
        graphics.fillRoundedRectangle(cap, 3.0f);
        graphics.setColour(studio::background);
        graphics.fillRect(cap.getX() + 4.0f, cap.getCentreY() - 0.5f, cap.getWidth() - 8.0f, 1.0f);
    }

    void drawRotarySlider(juce::Graphics& graphics, int x, int y, int width, int height,
                          float sliderPos, float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override
    {
        const float radius = juce::jmin(width, height) * 0.5f - 3.0f;
        const float centreX = x + width * 0.5f;
        const float centreY = y + height * 0.5f;
        const float angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
        graphics.setColour(studio::control);
        graphics.fillEllipse(centreX - radius, centreY - radius, radius * 2.0f, radius * 2.0f);
        graphics.setColour(studio::line);
        graphics.drawEllipse(centreX - radius, centreY - radius, radius * 2.0f, radius * 2.0f, 1.0f);
        const float arc = radius - 4.0f;
        juce::Path track;
        track.addCentredArc(centreX, centreY, arc, arc, 0.0f, rotaryStartAngle, rotaryEndAngle, true);
        graphics.strokePath(track, juce::PathStrokeType(2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        juce::Path value;
        value.addCentredArc(centreX, centreY, arc, arc, 0.0f, rotaryStartAngle, angle, true);
        graphics.setColour(studio::accent);
        graphics.strokePath(value, juce::PathStrokeType(2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        graphics.setColour(studio::text);
        graphics.drawLine(centreX + std::sin(angle) * radius * 0.28f, centreY - std::cos(angle) * radius * 0.28f,
                          centreX + std::sin(angle) * radius * 0.72f, centreY - std::cos(angle) * radius * 0.72f, 2.0f);
        graphics.setColour(studio::accent);
        graphics.fillEllipse(centreX - 2.5f, centreY - 2.5f, 5.0f, 5.0f);
    }

    void drawDocumentWindowTitleBar(juce::DocumentWindow& window, juce::Graphics& graphics, int width, int height,
                                    int titleSpaceX, int titleSpaceW, const juce::Image*, bool) override
    {
        graphics.setColour(studio::panel);
        graphics.fillAll();
        graphics.setColour(studio::line);
        graphics.fillRect(0, height - 1, width, 1);
        graphics.setColour(studio::text);
        graphics.setFont(juce::Font(juce::FontOptions(13.0f)));
        graphics.drawText(window.getName(), titleSpaceX + 8, 0, titleSpaceW - 8, height, juce::Justification::centredLeft, true);
    }

    juce::Button* createDocumentWindowButton(int buttonType) override { return new CaptionButton(buttonType); }

    void positionDocumentWindowButtons(juce::DocumentWindow&, int titleBarX, int titleBarY, int titleBarW, int titleBarH,
                                       juce::Button* minimiseButton, juce::Button* maximiseButton, juce::Button* closeButton, bool) override
    {
        const int buttonWidth = 46;
        int x = titleBarX + titleBarW - buttonWidth;
        for (auto* button : { closeButton, maximiseButton, minimiseButton })
        {
            if (button == nullptr) continue;
            button->setBounds(x, titleBarY, buttonWidth, titleBarH);
            x -= buttonWidth;
        }
    }

    void drawResizableWindowBorder(juce::Graphics& graphics, int width, int height, const juce::BorderSize<int>& border, juce::ResizableWindow&) override
    {
        if (border.isEmpty()) return;
        graphics.setColour(juce::Colour(0xff0b0d12));
        graphics.drawRect(0, 0, width, height, juce::jmax(1, border.getTop()));
        graphics.setColour(studio::line);
        graphics.drawRect(0, 0, width, height, 1);
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

class TransportButton : public juce::TextButton
{
public:
    juce::Colour live { studio::accent };
    bool armed = false;
    void paintButton(juce::Graphics& graphics, bool over, bool down) override
    {
        auto colour = armed ? live : studio::control;
        if (down) colour = colour.darker(0.16f);
        else if (over) colour = colour.brighter(0.1f);
        auto bounds = getLocalBounds().toFloat().reduced(1.0f);
        graphics.setColour(colour);
        graphics.fillRoundedRectangle(bounds, 7.0f);
        if (!armed)
        {
            graphics.setColour(studio::line);
            graphics.drawRoundedRectangle(bounds, 7.0f, 1.0f);
        }
        graphics.setColour(armed ? live.brighter(0.35f) : studio::muted);
        graphics.fillEllipse(8.0f, bounds.getCentreY() - 3.0f, 6.0f, 6.0f);
        graphics.setColour(juce::Colours::white);
        graphics.setFont(juce::Font(juce::FontOptions(12.0f)));
        graphics.drawText(getButtonText(), getLocalBounds().withTrimmedLeft(16), juce::Justification::centred, false);
    }
};

class MixerView : public juce::Component, private juce::Timer, private juce::KeyListener
{
public:
    explicit MixerView(AudioEngine&);
    ~MixerView() override;
    void refresh();
    void resized() override;
    void paint(juce::Graphics&) override;
    void parentHierarchyChanged() override;

private:
    bool keyPressed(const juce::KeyPress&, juce::Component*) override;
    void timerCallback() override;
    void toggleGroup(const juce::String& name);
    void layoutStrips();
    void startSavedRecording();
    void startSavedStream();
    void moveStripTo(const juce::String& draggedId, ChannelStripComponent& target, bool after);
    AudioEngine& engine;
    juce::Label profileLabel;
    juce::TextButton addButton, newProfile;
    TransportButton recordButton, recordPause, recordStop, streamButton, streamPause, streamStop;
    juce::ComboBox profiles;
    juce::Component* keyHost = nullptr;
    juce::String transportState;
    juce::Viewport viewport;
    juce::Component row;
    juce::OwnedArray<ChannelStripComponent> strips;
    juce::OwnedArray<juce::TextButton> groupHeaders;
    juce::StringArray collapsed;
    juce::String layoutKey;
    uint64 seenSurface = 0;
    bool applying = false;
};

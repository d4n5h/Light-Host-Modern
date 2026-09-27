#include "MixerView.h"
#include <juce_gui_extra/juce_gui_extra.h>

namespace
{
juce::String panText(double value)
{
    const int amount = (int) std::lround(std::abs(value) * 100.0);
    if (amount == 0) return "Center";
    return juce::String(amount) + (value < 0 ? "L" : "R");
}

class PluginBrowser : public juce::Component
{
public:
    static void open(AudioEngine& engine, const juce::String& stripId)
    {
        auto* browser = new PluginBrowser(engine, stripId);
        juce::DialogWindow::LaunchOptions options;
        options.content.setOwned(browser);
        options.dialogTitle = "Add plugin";
        options.dialogBackgroundColour = studio::background;
        options.escapeKeyTriggersCloseButton = true;
        options.useNativeTitleBar = true;
        options.resizable = true;
        options.launchAsync();
    }

    PluginBrowser(AudioEngine& engineIn, juce::String strip) : engine(engineIn), stripId(std::move(strip))
    {
        setLookAndFeel(&shellLook());
        setSize(680, 480);
        addAndMakeVisible(search);
        addAndMakeVisible(format);
        addAndMakeVisible(type);
        addAndMakeVisible(manufacturer);
        addAndMakeVisible(list);
        addAndMakeVisible(add);
        search.setTextToShowWhenEmpty("Search", studio::muted);
        add.setButtonText("Add");
        format.addItem("All formats", 1);
        format.addItem("VST3", 2);
        format.addItem("VST", 3);
        format.setSelectedId(1, juce::dontSendNotification);
        const char* types[] = { "All", "Dynamics", "EQ", "Reverb", "Delay", "Modulation", "Distortion", "Filter", "Pitch", "Spatial", "Instrument", "Analyzer", "Restoration", "Tools", "Other" };
        for (int i = 0; i < 15; ++i) type.addItem(types[i], i + 1);
        type.setSelectedId(1, juce::dontSendNotification);
        manufacturer.addItem("All manufacturers", 1);
        juce::StringArray names;
        for (const auto& plugin : engine.getKnownPluginsSorted())
            if (plugin.manufacturerName.isNotEmpty()) names.addIfNotAlreadyThere(plugin.manufacturerName);
        names.sort(true);
        for (int i = 0; i < names.size(); ++i) manufacturer.addItem(names[i], i + 2);
        manufacturer.setSelectedId(1, juce::dontSendNotification);
        list.setModel(&model);
        list.setRowHeight(28);
        model.onActivate = [this](int) { addSelected(); };
        auto refill = [this] { applyFilter(); };
        search.onTextChange = refill;
        format.onChange = refill;
        type.onChange = refill;
        manufacturer.onChange = refill;
        add.onClick = [this] { addSelected(); };
        applyFilter();
    }

    ~PluginBrowser() override { setLookAndFeel(nullptr); }

    void resized() override
    {
        auto area = getLocalBounds().reduced(8);
        auto bar = area.removeFromTop(28);
        search.setBounds(bar.removeFromLeft(160));
        bar.removeFromLeft(6);
        format.setBounds(bar.removeFromLeft(100));
        bar.removeFromLeft(6);
        type.setBounds(bar.removeFromLeft(130));
        bar.removeFromLeft(6);
        manufacturer.setBounds(bar);
        add.setBounds(area.removeFromBottom(32).removeFromRight(100));
        area.removeFromBottom(8);
        list.setBounds(area);
    }

    void paint(juce::Graphics& graphics) override { graphics.fillAll(studio::background); }

private:
    void applyFilter()
    {
        const auto query = search.getText();
        const auto chosenFormat = format.getSelectedId() <= 1 ? juce::String() : format.getText();
        const auto chosenType = type.getSelectedId() <= 1 ? juce::String() : type.getText();
        const auto chosenMaker = manufacturer.getSelectedId() <= 1 ? juce::String() : manufacturer.getText();
        model.plugins.clear();
        for (const auto& plugin : engine.getKnownPluginsSorted())
            if (pluginMatches(plugin, query, chosenFormat, chosenMaker, chosenType)) model.plugins.push_back(plugin);
        list.updateContent();
        list.repaint();
    }

    void addSelected()
    {
        const int row = list.getSelectedRow();
        if (row < 0 || row >= (int) model.plugins.size()) return;
        engine.addKnownPluginAt(engine.findKnownPluginIndexById(lightHostModern::knownPluginId(model.plugins[(size_t) row])), stripId, {});
        if (auto* window = findParentComponentOfClass<juce::DialogWindow>()) window->exitModalState(0);
    }

    AudioEngine& engine;
    juce::String stripId;
    juce::TextEditor search;
    juce::ComboBox format, type, manufacturer;
    juce::TextButton add;
    PluginListModel model;
    juce::ListBox list { "plugins", nullptr };
};
}

class ChannelStripComponent::InsertButton : public juce::Component, public juce::DragAndDropTarget
{
public:
    InsertButton(juce::String id, juce::String drag) : instanceId(std::move(id)), dragId(std::move(drag))
    {
        addAndMakeVisible(name);
        addAndMakeVisible(dots);
        name.row = this;
        dots.row = this;
        name.onClick = [this] { if (onOpen) onOpen(); };
        dots.onClick = [this] { if (onMenu) onMenu(); };
    }
    void setPlugin(const juce::String& text, bool active)
    {
        name.setButtonText(text);
        engaged = active;
        repaint();
    }
    void resized() override
    {
        auto area = getLocalBounds();
        dots.setBounds(area.removeFromRight(18).reduced(1, 1));
        name.setBounds(area);
    }
    bool isInterestedInDragSource(const SourceDetails& details) override
    {
        const auto source = details.description.toString();
        return source.startsWith("known:") || source.startsWith("instance:");
    }
    void itemDragEnter(const SourceDetails& details) override { hover(details); }
    void itemDragMove(const SourceDetails& details) override { hover(details); }
    void itemDragExit(const SourceDetails&) override { if (onHover) onHover(-1); dropEdge = 0; }
    void itemDropped(const SourceDetails& details) override
    {
        hover(details);
        if (onDrop) onDrop(details.description.toString());
        if (onHover) onHover(-1);
    }
    void setDropEdge(int edge)
    {
        if (dropEdge == edge) return;
        dropEdge = edge;
        repaint();
    }
    std::function<void()> onOpen, onMenu;
    std::function<void(const juce::String&)> onDrop;
    std::function<void(int)> onHover;
    juce::String instanceId, dragId;
    int dropEdge = 0;
    bool engaged = true;

private:
    struct RowButton : juce::TextButton
    {
        InsertButton* row = nullptr;
        void paintButton(juce::Graphics& graphics, bool over, bool down) override
        {
            auto colour = row != nullptr && row->engaged ? studio::accent : studio::line;
            if (down) colour = colour.darker(0.15f);
            else if (over) colour = colour.brighter(0.12f);
            graphics.setColour(colour);
            graphics.fillRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 3.0f);
            graphics.setColour(juce::Colours::white);
            if (getButtonText().isNotEmpty())
                graphics.drawFittedText(getButtonText(), getLocalBounds().reduced(4, 0), juce::Justification::centredLeft, 1);
            if (row == nullptr || row->dropEdge == 0) return;
            graphics.setColour(juce::Colours::white);
            graphics.fillRect(row->dropEdge == 1 ? getLocalBounds().removeFromTop(3) : getLocalBounds().removeFromBottom(3));
        }
        void mouseUp(const juce::MouseEvent& event) override
        {
            if (event.getDistanceFromDragStart() > 8)
            {
                setState(buttonNormal);
                return;
            }
            juce::TextButton::mouseUp(event);
        }
    };
    struct NameButton : RowButton
    {
        bool dragged = false;
        void mouseDown(const juce::MouseEvent& event) override
        {
            dragged = false;
            juce::TextButton::mouseDown(event);
        }
        void mouseDrag(const juce::MouseEvent& event) override
        {
            if (row->dragId.isEmpty() || event.getDistanceFromDragStart() <= 8) return;
            dragged = true;
            if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor(this))
                container->startDragging(row->dragId, row);
        }
        void mouseUp(const juce::MouseEvent& event) override
        {
            if (dragged)
            {
                dragged = false;
                setState(buttonNormal);
                return;
            }
            RowButton::mouseUp(event);
        }
    };
    struct DotsButton : RowButton
    {
        void paintButton(juce::Graphics& graphics, bool over, bool down) override
        {
            RowButton::paintButton(graphics, over, down);
            graphics.setColour(juce::Colours::white);
            const auto centre = getLocalBounds().getCentre().toFloat();
            for (int i = -1; i <= 1; ++i)
                graphics.fillEllipse(centre.x - 1.5f, centre.y + (float) i * 4.0f - 1.5f, 3.0f, 3.0f);
        }
    };
    void hover(const SourceDetails& details) { if (onHover) onHover(getY() + details.localPosition.y); }
    NameButton name;
    DotsButton dots;
};

ChannelStripComponent::ChannelStripComponent(AudioEngine& engineIn, bool masterIn)
    : engine(engineIn), master(masterIn)
{
    addAndMakeVisible(nameButton);
    addAndMakeVisible(gainLabel);
    addAndMakeVisible(fader);
    addAndMakeVisible(muteStrip);
    nameButton.onClick = [this] { showStripMenu(); };
    nameButton.dragDescription = [this] { return master ? juce::String() : "strip:" + stripId; };
    gainLabel.setJustificationType(juce::Justification::centred);
    gainLabel.onClick = [this] { editValue(gainLabel, true); };
    fader.setSliderStyle(juce::Slider::LinearVertical);
    fader.setRange(-60.0, 12.0, 0.1);
    fader.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    fader.setDoubleClickReturnValue(true, 0.0, juce::ModifierKeys());
    fader.addListener(this);
    muteStrip.setClickingTogglesState(true);
    muteStrip.onClick = [this] {
        if (applying) return;
        if (master) engine.setGlobalMuted(muteStrip.getToggleState());
        else engine.setStripMuted(stripId, muteStrip.getToggleState());
    };
    insertViewport.setViewedComponent(&insertList, false);
    insertViewport.setScrollBarsShown(true, false);
    insertViewport.setScrollOnDragMode(juce::Viewport::ScrollOnDragMode::never);
    addAndMakeVisible(insertViewport);
    addAndMakeVisible(plusButton);
    plusButton.setButtonText("+");
    plusButton.onClick = [this] { PluginBrowser::open(engine, master ? juce::String(lightHostModern::masterStripId) : stripId); };
    if (!master)
    {
        addAndMakeVisible(inputBox);
        addAndMakeVisible(outputBox);
        addAndMakeVisible(outputBox2);
        addAndMakeVisible(soloStrip);
        addAndMakeVisible(pan);
        addAndMakeVisible(panReadout);
        soloStrip.setClickingTogglesState(true);
        soloStrip.onClick = [this] { if (!applying) engine.setStripSolo(stripId, soloStrip.getToggleState()); };
        inputBox.setTextWhenNothingSelected("Inputs");
        inputBox.onMenu = [this] { showInputMenu(); };
        const auto chosen = [](const juce::ComboBox& box, const std::vector<int>& channels, int firstId) {
            const int index = box.getSelectedId() - firstId;
            return index >= 0 && index < (int) channels.size() ? channels[(size_t) index] : -1;
        };
        outputBox.onChange = outputBox2.onChange = [this, chosen] {
            if (applying) return;
            auto strip = std::find_if(engine.chainStrips().begin(), engine.chainStrips().end(), [&](const auto& item) { return item.id == stripId; });
            if (strip == engine.chainStrips().end()) return;
            if (outputBox.getSelectedId() <= 1)
            {
                engine.setStripRouting(stripId, strip->allInputs, true, strip->inputs, {});
                return;
            }
            const int left = chosen(outputBox, outputChannels, 2);
            const int right = chosen(outputBox2, outputChannels, 1);
            if (left < 0) return;
            if (right < 0 || right == left)
                engine.setStripRouting(stripId, strip->allInputs, false, strip->inputs, { left });
            else
                engine.setStripRouting(stripId, strip->allInputs, false, strip->inputs, { left, right });
        };
        pan.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        pan.setRange(-1.0, 1.0, 0.01);
        pan.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        pan.setDoubleClickReturnValue(true, 0.0, juce::ModifierKeys());
        pan.addListener(this);
        panReadout.setJustificationType(juce::Justification::centred);
        panReadout.onClick = [this] { editValue(panReadout, false); };
    }
}

ChannelStripComponent::~ChannelStripComponent() = default;

void ChannelStripComponent::applyLive(float gainDb, float panValue, bool muted, bool soloed)
{
    applying = true;
    if (!fader.isMouseButtonDown()) fader.setValue(gainDb, juce::dontSendNotification);
    if (!pan.isMouseButtonDown()) pan.setValue(panValue, juce::dontSendNotification);
    muteStrip.setToggleState(muted, juce::dontSendNotification);
    soloStrip.setToggleState(soloed, juce::dontSendNotification);
    applying = false;
    gainLabel.setText(juce::String(gainDb, 1) + " dB", juce::dontSendNotification);
    panReadout.setText(panText(panValue), juce::dontSendNotification);
}

void ChannelStripComponent::applyMasterLive(float gainDb)
{
    applying = true;
    if (!fader.isMouseButtonDown()) fader.setValue(gainDb, juce::dontSendNotification);
    applying = false;
    gainLabel.setText(juce::String(gainDb, 1) + " dB", juce::dontSendNotification);
}

void ChannelStripComponent::setMeter(float peak)
{
    if (std::abs(peak - meterPeak) < 0.001f) return;
    meterPeak = peak;
    repaint(meterBounds);
}

void ChannelStripComponent::setMaster(float gainDb, const std::vector<lightHostModern::PluginInstanceRecord>& plugins)
{
    stripId = lightHostModern::masterStripId;
    groupName.clear();
    color = 0;
    colourHex.clear();
    nameButton.setButtonText("Master");
    applying = true;
    muteStrip.setToggleState(engine.isGlobalMuted(), juce::dontSendNotification);
    if (!fader.isMouseButtonDown()) fader.setValue(gainDb, juce::dontSendNotification);
    applying = false;
    gainLabel.setText(juce::String(gainDb, 1) + " dB", juce::dontSendNotification);
    std::vector<juce::String> ids;
    for (const auto& plugin : plugins) ids.push_back(plugin.id);
    if (ids == insertIds)
    {
        for (int i = 0; i < inserts.size(); ++i)
            inserts[i]->setPlugin(plugins[(size_t) i].displayName(), !plugins[(size_t) i].bypassed);
        return;
    }
    insertIds = ids;
    inserts.clear();
    for (const auto& plugin : plugins)
    {
        auto* button = inserts.add(new InsertButton(plugin.id, "instance:" + plugin.id));
        button->setPlugin(plugin.displayName(), !plugin.bypassed);
        button->onOpen = [this, id = plugin.id] {
            const int index = engine.findPluginIndexById(id);
            if (index >= 0) engine.showPluginEditor(index);
        };
        button->onMenu = [this, id = plugin.id] { showPluginMenu(id); };
        button->onHover = [this](int y) { if (y < 0) clearDrag(); else showPluginGap(y); };
        button->onDrop = [this](const juce::String& source) { dropPlugin(source); };
        insertList.addAndMakeVisible(button);
    }
    resized();
}

void ChannelStripComponent::setStrip(const lightHostModern::ChainStrip& strip, const std::vector<lightHostModern::PluginInstanceRecord>& plugins)
{
    stripId = strip.id;
    groupName = strip.group;
    color = strip.color;
    colourHex = strip.colour;
    nameButton.setButtonText(strip.name);
    const bool widthChanged = stereo != strip.stereo;
    stereo = strip.stereo;
    const auto config = engine.getAudioDeviceConfiguration();
    juce::String signature = "inputs";
    std::vector<int> channels;
    for (int i = 0; i < (int) config.activeInputChannels.size(); ++i)
        if (config.activeInputChannels[(size_t) i])
        {
            channels.push_back(i);
            const auto label = i < (int) config.inputChannelNames.size() && config.inputChannelNames[(size_t) i].isNotEmpty()
                ? config.inputChannelNames[(size_t) i] : "In " + juce::String(i + 1);
            signature += "|" + label;
        }
    if (signature != inputSignature)
    {
        inputSignature = signature;
        inputChannels = channels;
    }
    const auto channelLabel = [&](int channel, const std::vector<juce::String>& names, const char* prefix) {
        return channel < names.size() && names[channel].isNotEmpty() ? names[channel] : juce::String(prefix) + juce::String(channel + 1);
    };
    const auto selectChannel = [](juce::ComboBox& box, const std::vector<int>& choices, int channel, int firstId) {
        const auto found = std::find(choices.begin(), choices.end(), channel);
        if (found != choices.end()) box.setSelectedId((int) std::distance(choices.begin(), found) + firstId, juce::dontSendNotification);
    };
    applying = true;
    if (strip.allInputs) inputBox.setText("All inputs", juce::dontSendNotification);
    else
    {
        juce::StringArray picked;
        for (int channel : strip.inputs) picked.add(channelLabel(channel, config.inputChannelNames, "In "));
        inputBox.setText(picked.isEmpty() ? "All inputs" : picked.joinIntoString(", "), juce::dontSendNotification);
    }
    juce::String outputKey = "outputs";
    std::vector<int> outputs;
    for (int i = 0; i < (int) config.activeOutputChannels.size(); ++i)
        if (config.activeOutputChannels[(size_t) i])
        {
            outputs.push_back(i);
            const auto label = i < (int) config.outputChannelNames.size() && config.outputChannelNames[(size_t) i].isNotEmpty()
                ? config.outputChannelNames[(size_t) i] : "Out " + juce::String(i + 1);
            outputKey += "|" + label;
        }
    if (outputKey != outputSignature)
    {
        outputSignature = outputKey;
        outputChannels = outputs;
        applying = true;
        outputBox.clear(juce::dontSendNotification);
        outputBox2.clear(juce::dontSendNotification);
        outputBox.addItem("All outputs", 1);
        for (int i = 0; i < (int) outputChannels.size(); ++i)
        {
            const auto channel = outputChannels[(size_t) i];
            const auto label = channelLabel(channel, config.outputChannelNames, "Out ");
            outputBox.addItem(label, i + 2);
            outputBox2.addItem(label, i + 1);
        }
        applying = false;
    }
    applying = true;
    muteStrip.setToggleState(strip.muted, juce::dontSendNotification);
    soloStrip.setToggleState(strip.solo, juce::dontSendNotification);
    const bool showPair = !strip.allOutputs;
    const bool layoutChanged = widthChanged || outputBox2.isVisible() != showPair;
    outputBox2.setVisible(showPair);
    if (strip.allOutputs) outputBox.setSelectedId(1, juce::dontSendNotification);
    else if (!strip.outputs.empty()) selectChannel(outputBox, outputChannels, strip.outputs[0], 2);
    if (strip.outputs.size() > 1) selectChannel(outputBox2, outputChannels, strip.outputs[1], 1);
    else if (!strip.outputs.empty())
    {
        const int selected = strip.outputs[0];
        const int other = selected % 2 == 0 ? selected + 1 : selected - 1;
        selectChannel(outputBox2, outputChannels, other, 1);
    }
    if (!fader.isMouseButtonDown()) fader.setValue(strip.gainDb, juce::dontSendNotification);
    if (!pan.isMouseButtonDown()) pan.setValue(strip.pan, juce::dontSendNotification);
    applying = false;
    gainLabel.setText(juce::String(strip.gainDb, 1) + " dB", juce::dontSendNotification);
    panReadout.setText(panText(strip.pan), juce::dontSendNotification);
    std::vector<juce::String> ids;
    for (const auto& plugin : plugins) ids.push_back(plugin.id);
    if (ids == insertIds)
    {
        for (int i = 0; i < inserts.size(); ++i)
            inserts[i]->setPlugin(plugins[(size_t) i].displayName(), !plugins[(size_t) i].bypassed);
        if (layoutChanged) resized();
        repaint();
        return;
    }
    insertIds = ids;
    inserts.clear();
    for (const auto& plugin : plugins)
    {
        auto* button = inserts.add(new InsertButton(plugin.id, "instance:" + plugin.id));
        button->setPlugin(plugin.displayName(), !plugin.bypassed);
        button->onOpen = [this, id = plugin.id] {
            const int index = engine.findPluginIndexById(id);
            if (index >= 0) engine.showPluginEditor(index);
        };
        button->onMenu = [this, id = plugin.id] { showPluginMenu(id); };
        button->onHover = [this](int y) { if (y < 0) clearDrag(); else showPluginGap(y); };
        button->onDrop = [this](const juce::String& source) { dropPlugin(source); };
        insertList.addAndMakeVisible(button);
    }
    resized();
}

void ChannelStripComponent::showInputMenu()
{
    auto strip = std::find_if(engine.chainStrips().begin(), engine.chainStrips().end(), [&](const auto& item) { return item.id == stripId; });
    if (strip == engine.chainStrips().end()) return;
    const auto config = engine.getAudioDeviceConfiguration();
    struct Choice { bool all = true; std::vector<int> inputs; };
    auto choice = std::make_shared<Choice>();
    choice->all = strip->allInputs;
    choice->inputs = strip->inputs;
    struct Row : juce::PopupMenu::CustomComponent
    {
        int channel = -1;
        juce::String text;
        bool tick = false;
        std::function<void()> toggle;
        Row(int channel, juce::String text, bool tick, std::function<void()> toggle)
            : CustomComponent(false), channel(channel), text(std::move(text)), tick(tick), toggle(std::move(toggle)) {}
        void getIdealSize(int& width, int& height) override { width = 220; height = 24; }
        void paint(juce::Graphics& graphics) override
        {
            if (isItemHighlighted()) graphics.fillAll(studio::selectedFill());
            auto box = juce::Rectangle<float>(8.0f, (getHeight() - 12.0f) / 2.0f, 12.0f, 12.0f);
            graphics.setColour(studio::text);
            graphics.drawRoundedRectangle(box, 2.0f, 1.0f);
            if (tick) graphics.fillRoundedRectangle(box.reduced(3.0f), 1.0f);
            graphics.drawText(text, getLocalBounds().withTrimmedLeft(28), juce::Justification::centredLeft);
        }
        void mouseUp(const juce::MouseEvent& event) override
        {
            if (event.mouseWasClicked() && toggle) toggle();
        }
    };
    auto rows = std::make_shared<std::vector<Row*>>();
    auto toggle = [this, choice, rows](int channel) {
        if (channel < 0)
        {
            if (choice->all) return;
            choice->all = true;
            choice->inputs.clear();
        }
        else
        {
            choice->all = false;
            auto found = std::find(choice->inputs.begin(), choice->inputs.end(), channel);
            if (found != choice->inputs.end()) choice->inputs.erase(found);
            else
            {
                if (choice->inputs.size() == 2) choice->inputs.erase(choice->inputs.begin());
                choice->inputs.push_back(channel);
            }
            if (choice->inputs.empty()) choice->all = true;
        }
        for (auto* row : *rows)
            row->tick = row->channel < 0 ? choice->all
                : !choice->all && std::find(choice->inputs.begin(), choice->inputs.end(), row->channel) != choice->inputs.end();
        for (auto* row : *rows) row->repaint();
        auto current = std::find_if(engine.chainStrips().begin(), engine.chainStrips().end(), [&](const auto& item) { return item.id == stripId; });
        if (current == engine.chainStrips().end()) return;
        engine.setStripRouting(stripId, choice->all, current->allOutputs, choice->inputs, current->outputs);
    };
    juce::PopupMenu menu;
    auto add = [&](int channel, juce::String text, bool tick) {
        auto row = std::make_unique<Row>(channel, text, tick, [toggle, channel] { toggle(channel); });
        rows->push_back(row.get());
        menu.addCustomItem(channel + 2, std::move(row), nullptr, text);
    };
    add(-1, "All inputs", choice->all);
    for (int channel : inputChannels)
    {
        const auto text = channel < (int) config.inputChannelNames.size() && config.inputChannelNames[(size_t) channel].isNotEmpty()
            ? config.inputChannelNames[(size_t) channel] : "In " + juce::String(channel + 1);
        const bool tick = !choice->all && std::find(choice->inputs.begin(), choice->inputs.end(), channel) != choice->inputs.end();
        add(channel, text, tick);
    }
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&inputBox));
}

void ChannelStripComponent::paint(juce::Graphics& graphics)
{
    auto card = getLocalBounds().toFloat().reduced(2);
    graphics.setColour(stripFill(color, master ? juce::String() : colourHex));
    graphics.fillRoundedRectangle(card, 8.0f);
    graphics.setColour(studio::line);
    graphics.drawRoundedRectangle(card, 8.0f, 1.0f);
    if (!meterBounds.isEmpty())
    {
        graphics.setColour(studio::background);
        graphics.fillRoundedRectangle(meterBounds.toFloat(), 2.0f);
        const float db = juce::Decibels::gainToDecibels(meterPeak, -60.0f);
        const float amount = juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 60.0f);
        auto filled = meterBounds;
        filled.setTop(meterBounds.getBottom() - juce::roundToInt(meterBounds.getHeight() * amount));
        graphics.setColour(db > -3.0f ? juce::Colour(0xffff5d5d) : db > -12.0f ? juce::Colour(0xffffd166) : juce::Colour(0xff3dcc7a));
        graphics.fillRoundedRectangle(filled.toFloat(), 2.0f);
        graphics.setColour(studio::muted);
        graphics.setFont(10.0f);
        for (float mark : { 0.0f, -6.0f, -12.0f, -24.0f, -48.0f })
        {
            const int y = meterBounds.getBottom() - juce::roundToInt((float) meterBounds.getHeight() * ((mark + 60.0f) / 60.0f));
            graphics.fillRect(meterBounds.getX() - 4, y, 3, 1);
            graphics.drawText(juce::String(mark, 0), tickBounds.withY(y - 6).withHeight(12), juce::Justification::centredRight, false);
        }
    }
    if (pluginDropY >= 0)
    {
        graphics.setColour(studio::accent);
        graphics.fillRect(8, pluginDropY - 1, getWidth() - 16, 3);
    }
    if (stripEdge >= 0)
    {
        graphics.setColour(studio::accent);
        graphics.fillRect(stripEdge == 0 ? 2 : getWidth() - 5, 6, 3, getHeight() - 12);
    }
}

void ChannelStripComponent::mouseDrag(const juce::MouseEvent& event)
{
    if (master || event.eventComponent != this || event.getDistanceFromDragStart() <= 8) return;
    if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor(this))
        container->startDragging("strip:" + stripId, this);
}

void ChannelStripComponent::resized()
{
    auto layout = layoutChannelStrip(getLocalBounds(), !master);
    nameButton.setBounds(layout.name);
    auto faderArea = layout.fader;
    meterBounds = faderArea.removeFromRight(8).reduced(1, 0);
    tickBounds = faderArea.removeFromRight(22);
    auto marks = faderArea.removeFromTop(26);
    if (master) muteStrip.setBounds(marks.reduced(1, 1));
    else
    {
        muteStrip.setBounds(marks.removeFromLeft(marks.getWidth() / 2).reduced(1, 1));
        soloStrip.setBounds(marks.reduced(1, 1));
    }
    fader.setBounds(faderArea);
    gainLabel.setBounds(layout.gain);
    if (!master)
    {
        inputBox.setBounds(layout.route);
        pan.setBounds(layout.pan.withTrimmedBottom(16));
        panReadout.setBounds(layout.pan.removeFromBottom(16));
    }
    auto insertsArea = layout.inserts;
    if (!master)
    {
        outputBox.setBounds(insertsArea.removeFromTop(26).reduced(0, 1));
        if (outputBox2.isVisible()) outputBox2.setBounds(insertsArea.removeFromTop(26).reduced(0, 1));
    }
    const int rowHeight = 26;
    plusButton.setBounds(insertsArea.removeFromBottom(rowHeight).reduced(0, 1));
    insertViewport.setBounds(insertsArea);
    const int contentHeight = inserts.size() * rowHeight;
    const bool scroll = contentHeight > insertsArea.getHeight();
    const int width = juce::jmax(1, insertsArea.getWidth() - (scroll ? insertViewport.getScrollBarThickness() : 0));
    insertList.setSize(width, juce::jmax(insertsArea.getHeight(), contentHeight));
    auto list = insertList.getLocalBounds();
    for (auto* button : inserts)
        button->setBounds(list.removeFromTop(rowHeight).reduced(0, 1));
}

bool ChannelStripComponent::isInterestedInDragSource(const SourceDetails& details)
{
    const auto source = details.description.toString();
    if (source.startsWith("strip:")) return !master;
    return source.startsWith("known:") || source.startsWith("instance:");
}

void ChannelStripComponent::itemDragEnter(const SourceDetails& details) { updateDrag(details); }
void ChannelStripComponent::itemDragMove(const SourceDetails& details) { updateDrag(details); }
void ChannelStripComponent::itemDragExit(const SourceDetails&) { clearDrag(); }

void ChannelStripComponent::updateDrag(const SourceDetails& details)
{
    const auto source = details.description.toString();
    if (source.startsWith("strip:"))
    {
        for (auto* button : inserts) button->setDropEdge(0);
        pluginDropY = -1;
        const int edge = master || details.localPosition.x < getWidth() / 2 ? 0 : 1;
        if (stripEdge != edge) { stripEdge = edge; repaint(); }
        return;
    }
    stripEdge = -1;
    showPluginGap(details.localPosition.y - insertViewport.getY() + insertViewport.getViewPositionY());
}

void ChannelStripComponent::showPluginGap(int y)
{
    pluginInsertBefore.clear();
    pluginDropY = -1;
    for (auto* button : inserts)
    {
        if (y < button->getBounds().getCentreY())
        {
            button->setDropEdge(1);
            pluginInsertBefore = button->instanceId;
            for (auto* other : inserts) if (other != button) other->setDropEdge(0);
            repaint();
            return;
        }
    }
    for (auto* button : inserts) button->setDropEdge(0);
    if (auto* last = inserts.getLast()) last->setDropEdge(2);
    else pluginDropY = plusButton.getY();
    repaint();
}

void ChannelStripComponent::clearDrag()
{
    for (auto* button : inserts) button->setDropEdge(0);
    pluginDropY = -1;
    stripEdge = -1;
    pluginInsertBefore.clear();
    repaint();
}

void ChannelStripComponent::dropPlugin(const juce::String& source)
{
    const auto before = pluginInsertBefore;
    clearDrag();
    if (source.startsWith("known:"))
    {
        engine.addKnownPluginAt(engine.findKnownPluginIndexById(source.fromFirstOccurrenceOf("known:", false, false)), stripId, before);
        return;
    }
    if (!source.startsWith("instance:")) return;
    const auto instance = source.fromFirstOccurrenceOf("instance:", false, false);
    if (instance == before) return;
    const auto found = std::find(insertIds.begin(), insertIds.end(), instance);
    if (found != insertIds.end())
    {
        const auto next = found + 1 == insertIds.end() ? juce::String() : *(found + 1);
        if (next == before) return;
    }
    engine.movePluginToStrip(instance, stripId, before);
}

void ChannelStripComponent::itemDropped(const SourceDetails& details)
{
    const auto source = details.description.toString();
    if (source.startsWith("strip:"))
    {
        const bool after = stripEdge == 1;
        clearDrag();
        if (onStripDrop) onStripDrop(source.fromFirstOccurrenceOf("strip:", false, false), after);
        return;
    }
    showPluginGap(details.localPosition.y);
    dropPlugin(source);
}

void ChannelStripComponent::sliderValueChanged(juce::Slider* slider)
{
    if (applying) return;
    if (slider == &fader)
    {
        const auto gain = (float) fader.getValue();
        gainLabel.setText(juce::String(gain, 1) + " dB", juce::dontSendNotification);
        if (master) engine.setMasterGain(gain);
        else engine.setStripGain(stripId, gain);
    }
    else
    {
        panReadout.setText(panText(pan.getValue()), juce::dontSendNotification);
        engine.setStripPan(stripId, (float) pan.getValue());
    }
}

void ChannelStripComponent::showStripMenu()
{
    if (master) return;
    juce::PopupMenu menu;
    menu.addItem(1, "Rename");
    menu.addItem(2, "Outputs");
    juce::PopupMenu colors;
    const char* names[] = { "None", "Blue", "Green", "Orange", "Red", "Purple", "Teal", "Yellow", "Pink" };
    for (int i = 0; i < 9; ++i) colors.addItem(10 + i, names[i], true, colourHex.isEmpty() && color == i);
    colors.addItem(19, "Custom...");
    menu.addSubMenu("Color", colors);
    menu.addItem(4, "Group...");
    if (groupName.isNotEmpty()) menu.addItem(5, "Remove from group");
    menu.addItem(3, "Remove");
    menu.showMenuAsync(juce::PopupMenu::Options(), [this](int result) {
        if (result == 1)
        {
            auto editor = std::make_shared<juce::AlertWindow>("Rename", "Strip name", juce::AlertWindow::NoIcon);
            editor->addTextEditor("name", nameButton.getButtonText());
            editor->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
            editor->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
            editor->enterModalState(true, juce::ModalCallbackFunction::create([this, editor](int choice) {
                if (choice == 1) engine.renameStrip(stripId, editor->getTextEditorContents("name"));
            }), false);
        }
        else if (result == 2) applyRouting();
        else if (result == 3) engine.removeStrip(stripId);
        else if (result == 4)
        {
            auto editor = std::make_shared<juce::AlertWindow>("Group", "Group name", juce::AlertWindow::NoIcon);
            editor->addTextEditor("name", groupName);
            editor->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
            editor->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
            editor->enterModalState(true, juce::ModalCallbackFunction::create([this, editor](int choice) {
                if (choice == 1) engine.setStripGroup(stripId, editor->getTextEditorContents("name"));
            }), false);
        }
        else if (result == 5) engine.setStripGroup(stripId, {});
        else if (result >= 10 && result <= 18) engine.setStripColor(stripId, result - 10);
        else if (result == 19) pickColour();
    });
}

void ChannelStripComponent::applyRouting()
{
    auto strip = std::find_if(engine.chainStrips().begin(), engine.chainStrips().end(), [&](const auto& item) { return item.id == stripId; });
    if (strip == engine.chainStrips().end()) return;
    auto dialog = std::make_shared<juce::AlertWindow>("Outputs", juce::String(), juce::AlertWindow::NoIcon);
    const auto list = [](const std::vector<int>& values) {
        juce::String text;
        for (size_t i = 0; i < values.size(); ++i) text += (i ? "," : "") + juce::String(values[i]);
        return text;
    };
    const auto allInputs = strip->allInputs;
    const auto inputs = strip->inputs;
    dialog->addTextEditor("outputs", list(strip->outputs), "Output channels");
    dialog->addButton("All outputs", 2);
    dialog->addButton("Save", 1);
    dialog->addButton("Cancel", 0);
    dialog->enterModalState(true, juce::ModalCallbackFunction::create([this, dialog, allInputs, inputs](int choice) {
        if (choice == 2) engine.setStripRouting(stripId, allInputs, true, inputs, {});
        if (choice != 1) return;
        std::vector<int> outputs;
        for (auto part : juce::StringArray::fromTokens(dialog->getTextEditorContents("outputs"), ",", ""))
        {
            part = part.trim();
            if (part.isNotEmpty()) outputs.push_back(part.getIntValue());
        }
        engine.setStripRouting(stripId, allInputs, outputs.empty(), inputs, outputs);
    }), false);
}

void ChannelStripComponent::editValue(juce::Label& label, bool gain)
{
    auto* editor = new juce::TextEditor();
    editor->setBounds(label.getBounds());
    editor->setText(gain ? juce::String(fader.getValue(), 1) : panText(pan.getValue()), false);
    editor->setSelectAllWhenFocused(true);
    addAndMakeVisible(editor);
    editor->grabKeyboardFocus();
    auto commit = std::make_shared<bool>(false);
    editor->onReturnKey = editor->onFocusLost = [this, editor, gain, commit] {
        if (*commit) return;
        *commit = true;
        float value = 0.0f;
        const auto ok = gain ? parseGainText(editor->getText(), value) : parsePanText(editor->getText(), value);
        if (ok)
        {
            if (gain) fader.setValue(value, juce::sendNotificationSync);
            else pan.setValue(value, juce::sendNotificationSync);
        }
        juce::MessageManager::callAsync([editor] { delete editor; });
    };
}

void ChannelStripComponent::pickColour()
{
    struct Picker : juce::Component
    {
        Picker(AudioEngine& engineIn, juce::String id, juce::Colour current) : engine(engineIn), stripId(std::move(id)), start(current.toDisplayString(false).toLowerCase())
        {
            addAndMakeVisible(selector);
            selector.setCurrentColour(current);
            setSize(300, 360);
        }
        ~Picker() override
        {
            const auto hex = selector.getCurrentColour().toDisplayString(false).toLowerCase();
            if (hex != start) engine.setStripColour(stripId, hex);
        }
        void resized() override { selector.setBounds(getLocalBounds()); }
        AudioEngine& engine;
        juce::String stripId, start;
        juce::ColourSelector selector { juce::ColourSelector::showColourAtTop | juce::ColourSelector::showSliders | juce::ColourSelector::showColourspace };
    };
    const auto current = colourHex.length() == 6 ? juce::Colour::fromString("ff" + colourHex) : color > 0 ? stripPalette(color) : studio::accent;
    juce::CallOutBox::launchAsynchronously(std::make_unique<Picker>(engine, stripId, current), nameButton.getScreenBounds(), nullptr);
}

void ChannelStripComponent::showPluginMenu(const juce::String& instanceId)
{
    const int index = engine.findPluginIndexById(instanceId);
    if (index < 0) return;
    juce::PopupMenu menu;
    menu.addItem(1, engine.isPluginBypassed(index) ? "Enable" : "Bypass");
    menu.addItem(2, "Delete");
    menu.showMenuAsync(juce::PopupMenu::Options(), [this, index](int result) {
        if (result == 1) engine.setPluginBypassed(index, !engine.isPluginBypassed(index));
        else if (result == 2) engine.removePlugin(index);
    });
}

MixerView::MixerView(AudioEngine& engineIn)
    : engine(engineIn)
{
    addButton.setButtonText("+ Add channel");
    newProfile.setButtonText("New");
    recordButton.setButtonText("Record");
    recordButton.live = juce::Colour(0xffff5d5d);
    recordPause.setButtonText("Pause");
    recordStop.setButtonText("Stop");
    streamButton.setButtonText("Stream");
    streamPause.setButtonText("Pause");
    streamStop.setButtonText("Stop");
    recordPause.setVisible(false);
    recordStop.setVisible(false);
    streamPause.setVisible(false);
    streamStop.setVisible(false);
    for (juce::Component* button : { (juce::Component*) &addButton, (juce::Component*) &newProfile, (juce::Component*) &recordButton, (juce::Component*) &recordPause, (juce::Component*) &recordStop, (juce::Component*) &streamButton, (juce::Component*) &streamPause, (juce::Component*) &streamStop })
        addAndMakeVisible(button);
    recordButton.onClick = [this] { startSavedRecording(); };
    recordPause.onClick = [this] { if (engine.isRecordingPaused()) engine.resumeRecording(); else engine.pauseRecording(); };
    recordStop.onClick = [this] { engine.stopRecording(); };
    streamButton.onClick = [this] { startSavedStream(); };
    streamPause.onClick = [this] { if (engine.isStreamPaused()) engine.resumeStream(); else engine.pauseStream(); };
    streamStop.onClick = [this] { engine.stopStream(); };
    profileLabel.setText("Profile", juce::dontSendNotification);
    profileLabel.setColour(juce::Label::textColourId, studio::muted);
    profileLabel.setFont(juce::Font(juce::FontOptions(12.0f)));
    addAndMakeVisible(profileLabel);
    addAndMakeVisible(profiles);
    addAndMakeVisible(viewport);
    viewport.setViewedComponent(&row, false);
    viewport.setScrollBarsShown(false, true);
    addButton.onClick = [this] { engine.addStrip("Strip"); };
    newProfile.onClick = [this] {
        auto editor = std::make_shared<juce::AlertWindow>("New profile", "Profile name", juce::AlertWindow::NoIcon);
        editor->addTextEditor("name", {});
        editor->addButton("Create", 1, juce::KeyPress(juce::KeyPress::returnKey));
        editor->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
        editor->enterModalState(true, juce::ModalCallbackFunction::create([this, editor](int choice) {
            if (choice != 1) return;
            if (const auto error = engine.createChainProfile(editor->getTextEditorContents("name")); error.isNotEmpty())
                juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Profile", error);
        }), false);
    };
    profiles.onChange = [this] {
        if (applying || profiles.getSelectedId() <= 0) return;
        const auto catalog = engine.chainProfileCatalog();
        const auto index = profiles.getSelectedId() - 1;
        if (index >= 0 && index < (int) catalog.profiles.size())
            engine.switchChainProfile(catalog.profiles[(size_t) index].id);
    };
    startTimerHz(30);
}

MixerView::~MixerView()
{
    if (keyHost != nullptr) keyHost->removeKeyListener(this);
}

void MixerView::parentHierarchyChanged()
{
    if (keyHost != nullptr) keyHost->removeKeyListener(this);
    keyHost = getTopLevelComponent();
    if (keyHost != nullptr && keyHost != this) keyHost->addKeyListener(this);
}

bool MixerView::keyPressed(const juce::KeyPress& key, juce::Component* originating)
{
    if (dynamic_cast<juce::TextEditor*>(originating) != nullptr) return false;
    const auto mods = key.getModifiers();
    if (!mods.isCommandDown() || mods.isPopupMenu()) return false;
    const auto code = key.getKeyCode();
    const bool undo = code == 'Z' && !mods.isShiftDown();
    const bool redo = code == 'Y' || (code == 'Z' && mods.isShiftDown());
    if (undo) { engine.undoChain(); return true; }
    if (redo) { engine.redoChain(); return true; }
    return false;
}

void MixerView::startSavedRecording()
{
    if (engine.isRecording()) return;
    auto* settings = getAppProperties().getUserSettings();
    const auto folder = juce::File(settings->getValue("recordFolder", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("LightHostModern").getChildFile("Recordings").getFullPathName()));
    const auto error = engine.startRecording(folder, settings->getBoolValue("recordMp3", false), settings->getIntValue("recordBitrate", 192) * 1000,
        settings->getBoolValue("recordMixdown", true), settings->getBoolValue("recordMulti", false), settings->getBoolValue("recordInterleaved", false), settings->getBoolValue("recordRaw", false),
        settings->getValue("icecastHost"), settings->getIntValue("icecastPort", 8000), settings->getValue("icecastMount", "/live"), settings->getValue("icecastUser", "source"), settings->getValue("icecastPassword"), settings->getValue("icecastName", "LightHostModern"));
    if (error.isNotEmpty()) juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Record", error);
}

void MixerView::startSavedStream()
{
    if (engine.isStreaming()) return;
    const auto device = getAppProperties().getUserSettings()->getValue("streamDevice");
    if (device.isEmpty())
    {
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Stream", "Choose a stream device on the Record page. Install a virtual cable so other apps can select it.");
        return;
    }
    if (const auto error = engine.startStream(device); error.isNotEmpty())
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Stream", error);
}

void MixerView::paint(juce::Graphics& graphics)
{
    graphics.fillAll(studio::background);
}

void MixerView::refresh()
{
    const auto catalog = engine.chainProfileCatalog();
    applying = true;
    profiles.clear(juce::dontSendNotification);
    for (int i = 0; i < (int) catalog.profiles.size(); ++i)
    {
        profiles.addItem(catalog.profiles[(size_t) i].name, i + 1);
        if (catalog.profiles[(size_t) i].id == catalog.activeId) profiles.setSelectedId(i + 1, juce::dontSendNotification);
    }
    if (profiles.getSelectedId() == 0) profiles.setText("Unsaved", juce::dontSendNotification);
    applying = false;
    const auto& chain = engine.chainStrips();
    const auto& records = engine.getPluginInstances();
    if (strips.size() != (int) chain.size() + 1)
    {
        row.removeAllChildren();
        strips.clear();
        groupHeaders.clear();
        layoutKey.clear();
        for (size_t i = 0; i < chain.size(); ++i)
            strips.add(new ChannelStripComponent(engine, false));
        strips.add(new ChannelStripComponent(engine, true));
    }
    for (int i = 0; i < (int) chain.size(); ++i)
    {
        std::vector<lightHostModern::PluginInstanceRecord> plugins;
        for (const auto& record : records)
            if (record.stripId == chain[(size_t) i].id) plugins.push_back(record);
        strips[i]->setStrip(chain[(size_t) i], plugins);
    }
    if (auto* masterStrip = strips.getLast())
    {
        std::vector<lightHostModern::PluginInstanceRecord> masterPlugins;
        for (const auto& record : records)
            if (record.stripId == lightHostModern::masterStripId) masterPlugins.push_back(record);
        masterStrip->setMaster(engine.masterGainDb(), masterPlugins);
    }
    for (auto* strip : strips)
        strip->onStripDrop = [this, strip](const juce::String& dragged, bool after) { moveStripTo(dragged, *strip, after); };
    layoutStrips();
}

void MixerView::timerCallback()
{
    if (!isShowing()) return;
    const auto& chain = engine.chainStrips();
    const int count = juce::jmin((int) chain.size(), strips.size());
    for (int i = 0; i < count; ++i)
        strips[i]->setMeter(engine.getStripLevel(chain[(size_t) i].id));
    if (strips.size() == (int) chain.size() + 1)
        if (auto* master = strips.getLast()) master->setMeter(engine.getMasterLevel());
    if (engine.surfaceGeneration() != seenSurface)
    {
        seenSurface = engine.surfaceGeneration();
        for (int i = 0; i < count; ++i)
            strips[i]->applyLive(chain[(size_t) i].gainDb, chain[(size_t) i].pan, chain[(size_t) i].muted, chain[(size_t) i].solo);
        if (strips.size() == (int) chain.size() + 1)
            if (auto* master = strips.getLast()) master->applyMasterLive(engine.masterGainDb());
    }
    recordPause.setVisible(engine.isRecording());
    recordStop.setVisible(engine.isRecording());
    recordPause.setButtonText(engine.isRecordingPaused() ? "Resume" : "Pause");
    recordButton.armed = engine.isRecording() && !engine.isRecordingPaused();
    streamPause.setVisible(engine.isStreaming());
    streamStop.setVisible(engine.isStreaming());
    streamPause.setButtonText(engine.isStreamPaused() ? "Resume" : "Pause");
    streamButton.armed = engine.isStreaming() && !engine.isStreamPaused();
    const auto state = juce::String((int) engine.isRecording()) + juce::String((int) engine.isRecordingPaused()) + juce::String((int) engine.isStreaming()) + juce::String((int) engine.isStreamPaused());
    if (state != transportState)
    {
        transportState = state;
        resized();
    }
}

void MixerView::moveStripTo(const juce::String& draggedId, ChannelStripComponent& target, bool after)
{
    struct Item
    {
        juce::String id, group;
        bool operator==(const Item& other) const { return id == other.id && group == other.group; }
    };
    std::vector<Item> order;
    const int channels = strips.size() > 0 ? strips.size() - 1 : 0;
    const auto add = [&](int index) { order.push_back({ strips[index]->id(), strips[index]->group() }); };
    for (int i = 0; i < channels; ++i)
        if (strips[i]->group().isEmpty()) add(i);
    juce::StringArray seen;
    for (int i = 0; i < channels; ++i)
    {
        const auto name = strips[i]->group();
        if (name.isEmpty() || seen.contains(name, true)) continue;
        seen.add(name);
        for (int member = 0; member < channels; ++member)
            if (strips[member]->group().equalsIgnoreCase(name)) add(member);
    }
    const auto previous = order;
    Item dragged;
    order.erase(std::remove_if(order.begin(), order.end(), [&](const Item& item) {
        if (item.id != draggedId) return false;
        dragged = item;
        return true;
    }), order.end());
    if (dragged.id.isEmpty()) return;
    auto insertAt = order.end();
    auto group = dragged.group;
    if (!target.isMaster())
    {
        insertAt = std::find_if(order.begin(), order.end(), [&](const Item& item) { return item.id == target.id(); });
        if (insertAt == order.end()) return;
        group = insertAt->group;
        if (after) ++insertAt;
    }
    else if (!order.empty())
        group = order.back().group;
    dragged.group = group;
    order.insert(insertAt, dragged);
    if (order == previous) return;
    std::vector<std::pair<juce::String, juce::String>> next;
    for (const auto& item : order) next.emplace_back(item.id, item.group);
    engine.orderStrips(next);
}

void MixerView::toggleGroup(const juce::String& name)
{
        if (collapsed.contains(name, true)) collapsed.removeString(name, true);
        else collapsed.add(name);
    layoutStrips();
}

void MixerView::layoutStrips()
{
    const int height = juce::jmax(viewport.getHeight(), 480);
    const int channels = strips.size() > 0 ? strips.size() - 1 : 0;
    juce::String key = juce::String(height) + ":" + juce::String(channels) + ":" + collapsed.joinIntoString("|").toLowerCase();
    for (int i = 0; i < channels; ++i) key += "\n" + strips[i]->group().toLowerCase();
    if (key == layoutKey) return;
    layoutKey = key;
    groupHeaders.clear();
    int x = 0;
    for (int i = 0; i < channels; ++i) strips[i]->setVisible(false);
    auto place = [&](juce::Component& component, int width) {
        row.addAndMakeVisible(component);
        component.setBounds(x, 0, width, height);
        component.setVisible(true);
        x += width;
    };
    for (int i = 0; i < channels; ++i)
        if (strips[i]->group().isEmpty()) place(*strips[i], 128);
    juce::StringArray seen;
    for (int i = 0; i < channels; ++i)
    {
        const auto name = strips[i]->group();
        if (name.isEmpty() || seen.contains(name, true)) continue;
        seen.add(name);
        const bool open = !collapsed.contains(name, true);
        auto* header = groupHeaders.add(new juce::TextButton((open ? "v " : "> ") + name));
        header->onClick = [this, name] { toggleGroup(name); };
        place(*header, 36);
        for (int member = 0; member < channels; ++member)
        {
            if (!strips[member]->group().equalsIgnoreCase(name)) continue;
            strips[member]->setVisible(open);
            if (open) place(*strips[member], 128);
        }
    }
    row.setSize(juce::jmax(1, x), height);
}

void MixerView::resized()
{
    auto area = getLocalBounds().reduced(8);
    auto transport = area.removeFromTop(36);
    profileLabel.setBounds(transport.removeFromLeft(52));
    profiles.setBounds(transport.removeFromLeft(200).reduced(0, 4));
    transport.removeFromLeft(6);
    newProfile.setBounds(transport.removeFromLeft(64).reduced(0, 4));
    auto place = [&](juce::Button& button, int width) {
        if (!button.isVisible()) return;
        button.setBounds(transport.removeFromRight(width).reduced(3, 4));
    };
    place(streamStop, 68);
    place(streamPause, 76);
    place(streamButton, 86);
    if (streamButton.isVisible()) transport.removeFromRight(10);
    place(recordStop, 68);
    place(recordPause, 76);
    place(recordButton, 86);
    transport.removeFromRight(10);
    addButton.setBounds(transport.removeFromRight(140).reduced(0, 4));
    auto masterSlot = area.removeFromRight(136);
    viewport.setBounds(area);
    layoutStrips();
    if (auto* master = strips.getLast())
    {
        addAndMakeVisible(master);
        master->setVisible(true);
        master->setBounds(masterSlot.getX() + 8, viewport.getY(), 128, juce::jmax(viewport.getHeight(), 480));
    }
}

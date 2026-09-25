#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginWindow.h"

class PluginWindow;
static Array <PluginWindow*> activePluginWindows;

PluginWindow::PluginWindow (Component* const pluginEditor,
                            AudioProcessor& o,
                            NamedValueSet& properties,
                            WindowFormatType t)
    : DocumentWindow (pluginEditor->getName(), Colours::lightgrey,
                      DocumentWindow::minimiseButton | DocumentWindow::closeButton),
      owner (&o),
      windowProperties (&properties),
      type (t)
{
    setUsingNativeTitleBar (true);
    setContentOwned (pluginEditor, true);
    if (auto* editor = dynamic_cast<AudioProcessorEditor*>(pluginEditor))
        setResizable (editor->isResizable(), false);

    const int savedWidth = windowProperties->getWithDefault (getLastWProp (type), 0);
    const int savedHeight = windowProperties->getWithDefault (getLastHProp (type), 0);
    if (savedWidth >= 80 && savedHeight >= 40)
        setSize (savedWidth, savedHeight);

    setTopLeftPosition (windowProperties->getWithDefault (getLastXProp (type), Random::getSystemRandom().nextInt (500)),
                        windowProperties->getWithDefault (getLastYProp (type), Random::getSystemRandom().nextInt (500)));
    if (const auto* display = Desktop::getInstance().getDisplays().getDisplayForPoint(getBounds().getCentre().toFloat()))
    {
        const auto area = display->userBounds.toNearestInt();
        if (!area.contains(getBounds().getCentre()))
            setTopLeftPosition(jlimit(area.getX(), area.getRight() - getWidth(), getX()),
                jlimit(area.getY(), area.getBottom() - getHeight(), getY()));
    }

    windowProperties->set (getOpenProp (type), true);

    setVisible (true);
    tracking = true;
    storeBounds();

    activePluginWindows.add (this);
    
}

bool PluginWindow::isOpenFor (AudioProcessor& processor)
{
    for (auto* window : activePluginWindows)
        if (window->owner == &processor) return true;
    return false;
}

void PluginWindow::closeCurrentlyOpenWindowsFor (AudioProcessor& processor)
{
    for (int i = activePluginWindows.size(); --i >= 0;)
        if (activePluginWindows.getUnchecked(i)->owner == &processor)
            delete activePluginWindows.getUnchecked (i);
}

void PluginWindow::closeAllCurrentlyOpenWindows()
{
    for (int i = activePluginWindows.size(); --i >= 0;)
        delete activePluginWindows.getUnchecked (i);
}

bool PluginWindow::containsActiveWindows()
{
    return activePluginWindows.size() > 0;
}

//==============================================================================
class ProcessorProgramPropertyComp : public PropertyComponent,
                                     private AudioProcessorListener
{
public:
    ProcessorProgramPropertyComp (const String& name, AudioProcessor& p, int index_)
        : PropertyComponent (name),
          owner (p),
          index (index_)
    {
        owner.addListener (this);
    }

    ~ProcessorProgramPropertyComp()
    {
        owner.removeListener (this);
    }

    void refresh() { }
    void audioProcessorChanged (AudioProcessor*, const ChangeDetails&) override { }
    void audioProcessorParameterChanged(AudioProcessor*, int, float) override { }

private:
    AudioProcessor& owner;
    const int index;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ProcessorProgramPropertyComp)
};

class ProgramAudioProcessorEditor : public AudioProcessorEditor
{
public:
    ProgramAudioProcessorEditor (AudioProcessor* const p)
        : AudioProcessorEditor (p)
    {
        jassert (p != nullptr);
        setOpaque (true);

        addAndMakeVisible (panel);

        Array<PropertyComponent*> programs;

        const int numPrograms = p->getNumPrograms();
        int totalHeight = 0;

        for (int i = 0; i < numPrograms; ++i)
        {
            String name (p->getProgramName (i).trim());

            if (name.isEmpty())
                name = "Unnamed";

            ProcessorProgramPropertyComp* const pc = new ProcessorProgramPropertyComp (name, *p, i);
            programs.add (pc);
            totalHeight += pc->getPreferredHeight();
        }

        panel.addProperties (programs);

        setSize (400, jlimit (25, 400, totalHeight));
    }

    void paint (Graphics& g)
    {
        g.fillAll (Colours::grey);
    }

    void resized()
    {
        panel.setBounds (getLocalBounds());
    }

private:
    PropertyPanel panel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ProgramAudioProcessorEditor)
};

//==============================================================================
PluginWindow* PluginWindow::getWindowFor (AudioProcessor& processor,
                                          NamedValueSet& windowProperties,
                                          WindowFormatType type)
{
    // Resolve the normal-editor fallback before searching. Otherwise a plugin
    // without its own editor opens another generic window on every request.
    if (type == Normal && !processor.hasEditor()) type = Generic;
    const auto requestedType = type;
    for (int i = activePluginWindows.size(); --i >= 0;)
        if (activePluginWindows.getUnchecked(i)->owner == &processor
             && activePluginWindows.getUnchecked(i)->type == type)
            return activePluginWindows.getUnchecked(i);

    AudioProcessorEditor* ui = nullptr;

    if (type == Normal)
    {
        ui = processor.createEditorAndMakeActive();

        if (ui == nullptr)
            type = Generic;
    }

    if (ui == nullptr)
    {
        if (type == Generic || type == Parameters)
            ui = new GenericAudioProcessorEditor (processor);
        else if (type == Programs)
            ui = new ProgramAudioProcessorEditor (&processor);
    }

    if (ui != nullptr)
    {
        if (AudioPluginInstance* const plugin = dynamic_cast<AudioPluginInstance*> (&processor))
            ui->setName (plugin->getName());

        return new PluginWindow (ui, processor, windowProperties, requestedType);
    }

    return nullptr;
}

PluginWindow::~PluginWindow()
{
    storeBounds();
    tracking = false;
    activePluginWindows.removeFirstMatchingValue (this);
    clearContentComponent();
}

void PluginWindow::storeBounds()
{
    if (!tracking || windowProperties == nullptr || getWidth() < 80 || getHeight() < 40) return;
    windowProperties->set (getLastXProp (type), getX());
    windowProperties->set (getLastYProp (type), getY());
    windowProperties->set (getLastWProp (type), getWidth());
    windowProperties->set (getLastHProp (type), getHeight());
}

void PluginWindow::moved()
{
    storeBounds();
}

void PluginWindow::resized()
{
    DocumentWindow::resized();
    storeBounds();
}

void PluginWindow::closeButtonPressed()
{
    storeBounds();
    if (windowProperties != nullptr)
        windowProperties->set (getOpenProp (type), false);

    delete this;
}

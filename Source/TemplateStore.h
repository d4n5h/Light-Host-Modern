#pragma once
#include <juce_core/juce_core.h>
#include <memory>
#include <vector>

namespace lightHostModern
{
struct TemplateEntry
{
    juce::String id, name;
};

struct TemplateSnapshot
{
    std::unique_ptr<juce::XmlElement> session;
    std::unique_ptr<juce::XmlElement> device;
    std::unique_ptr<juce::XmlElement> channels;
    bool monoInputs = false;
    bool monoOutput = false;
    bool muted = false;
    bool bypassed = false;
    juce::String persistence = "disabled";
};

class TemplateStore
{
public:
    static constexpr int maximumTemplates = 64;
    explicit TemplateStore(const juce::File& preferencesFile);
    const juce::File& root() const { return directory; }
    std::vector<TemplateEntry> list() const;
    juce::String create(const juce::String& name, const TemplateSnapshot& snapshot, juce::String& id) const;
    juce::String update(const juce::String& id, const TemplateSnapshot& snapshot) const;
    juce::String read(const juce::String& id, TemplateSnapshot& snapshot) const;
    juce::String rename(const juce::String& id, const juce::String& name) const;
    juce::String remove(const juce::String& id) const;

private:
    juce::File fileFor(const juce::String& id) const { return directory.getChildFile(id + ".xml"); }
    juce::String validateName(const juce::String& name, const juce::String& exceptId, juce::String& normalized) const;
    juce::String writeFile(const juce::String& id, const juce::String& name, const TemplateSnapshot& snapshot) const;
    juce::File directory;
};
}

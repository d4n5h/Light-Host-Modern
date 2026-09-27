#include "TemplateStore.h"
#include "PluginInstances.h"
#include <algorithm>

namespace lightHostModern
{
namespace
{
bool validId(const juce::String& id)
{
    return id.length() == 32 && id.containsOnly("0123456789abcdef");
}

std::unique_ptr<juce::XmlElement> childCopy(const juce::XmlElement& root, const juce::String& tag)
{
    if (auto* child = root.getChildByName(tag)) return std::make_unique<juce::XmlElement>(*child);
    return {};
}
}

TemplateStore::TemplateStore(const juce::File& preferencesFile)
    : directory(preferencesFile.getSiblingFile(preferencesFile.getFileName() + ".templates"))
{}

juce::String TemplateStore::validateName(const juce::String& name, const juce::String& exceptId, juce::String& normalized) const
{
    if (!normalizeInstanceName(name, normalized) || normalized.isEmpty()) return "template_name_invalid";
    for (const auto& entry : list())
        if (entry.id != exceptId && entry.name.compareIgnoreCase(normalized) == 0) return "template_name_taken";
    return {};
}

std::vector<TemplateEntry> TemplateStore::list() const
{
    std::vector<TemplateEntry> entries;
    for (const auto& file : directory.findChildFiles(juce::File::findFiles, false, "*.xml"))
    {
        const auto parsed = juce::XmlDocument::parse(file);
        if (parsed == nullptr || !parsed->hasTagName("LIGHTHOSTTEMPLATE") || parsed->getIntAttribute("version") != 1)
            continue;
        const auto id = file.getFileNameWithoutExtension().toLowerCase();
        if (!validId(id)) continue;
        TemplateEntry entry;
        entry.id = id;
        entry.name = parsed->getStringAttribute("name");
        if (entry.name.isNotEmpty()) entries.push_back(std::move(entry));
    }
    std::sort(entries.begin(), entries.end(), [](const auto& left, const auto& right) {
        return left.name.compareIgnoreCase(right.name) < 0;
    });
    return entries;
}

juce::String TemplateStore::writeFile(const juce::String& id, const juce::String& name, const TemplateSnapshot& snapshot) const
{
    if (snapshot.session == nullptr || !snapshot.session->hasTagName("LIGHTHOSTSESSION")) return "template_session_invalid";
    directory.createDirectory();
    juce::XmlElement root("LIGHTHOSTTEMPLATE");
    root.setAttribute("version", 1);
    root.setAttribute("name", name);
    root.addChildElement(new juce::XmlElement(*snapshot.session));
    if (snapshot.device != nullptr)
        root.createNewChildElement("AUDIOSTATE")->addChildElement(new juce::XmlElement(*snapshot.device));
    if (snapshot.channels != nullptr) root.addChildElement(new juce::XmlElement(*snapshot.channels));
    auto* host = root.createNewChildElement("HOST");
    host->setAttribute("monoInputs", snapshot.monoInputs);
    host->setAttribute("monoOutput", snapshot.monoOutput);
    host->setAttribute("persistence", snapshot.persistence);
    host->setAttribute("muted", snapshot.muted);
    host->setAttribute("bypassed", snapshot.bypassed);
    if (!fileFor(id).replaceWithText(root.toString())) return "template_write_failed";
    return {};
}

juce::String TemplateStore::create(const juce::String& name, const TemplateSnapshot& snapshot, juce::String& id) const
{
    juce::String normalized;
    if (const auto error = validateName(name, {}, normalized); error.isNotEmpty()) return error;
    if ((int) list().size() >= maximumTemplates) return "template_limit";
    for (int attempt = 0; attempt < 8; ++attempt)
    {
        const auto candidate = juce::Uuid().toString().removeCharacters("-").toLowerCase();
        if (validId(candidate) && !fileFor(candidate).existsAsFile())
        {
            if (const auto error = writeFile(candidate, normalized, snapshot); error.isNotEmpty()) return error;
            id = candidate;
            return {};
        }
    }
    return "template_write_failed";
}

juce::String TemplateStore::update(const juce::String& id, const TemplateSnapshot& snapshot) const
{
    if (!validId(id) || !fileFor(id).existsAsFile()) return "template_not_found";
    TemplateSnapshot existing;
    if (const auto error = read(id, existing); error.isNotEmpty()) return error;
    const auto parsed = juce::XmlDocument::parse(fileFor(id));
    if (parsed == nullptr) return "template_not_found";
    return writeFile(id, parsed->getStringAttribute("name"), snapshot);
}

juce::String TemplateStore::read(const juce::String& id, TemplateSnapshot& snapshot) const
{
    if (!validId(id)) return "template_not_found";
    const auto parsed = juce::XmlDocument::parse(fileFor(id));
    if (parsed == nullptr || !parsed->hasTagName("LIGHTHOSTTEMPLATE") || parsed->getIntAttribute("version") != 1)
        return "template_not_found";
    snapshot.session = childCopy(*parsed, "LIGHTHOSTSESSION");
    if (auto* wrap = parsed->getChildByName("AUDIOSTATE"))
        if (auto* child = wrap->getFirstChildElement()) snapshot.device = std::make_unique<juce::XmlElement>(*child);
    snapshot.channels = childCopy(*parsed, "CHANNELS");
    if (auto* host = parsed->getChildByName("HOST"))
    {
        snapshot.monoInputs = host->getBoolAttribute("monoInputs");
        snapshot.monoOutput = host->getBoolAttribute("monoOutput");
        snapshot.persistence = host->getStringAttribute("persistence", "disabled");
        snapshot.muted = host->getBoolAttribute("muted");
        snapshot.bypassed = host->getBoolAttribute("bypassed");
    }
    if (snapshot.session == nullptr) return "template_session_invalid";
    return {};
}

juce::String TemplateStore::rename(const juce::String& id, const juce::String& name) const
{
    juce::String normalized;
    if (const auto error = validateName(name, id, normalized); error.isNotEmpty()) return error;
    TemplateSnapshot snapshot;
    if (const auto error = read(id, snapshot); error.isNotEmpty()) return error;
    return writeFile(id, normalized, snapshot);
}

juce::String TemplateStore::remove(const juce::String& id) const
{
    if (!validId(id) || !fileFor(id).existsAsFile()) return "template_not_found";
    return fileFor(id).deleteFile() ? juce::String() : juce::String("template_write_failed");
}

juce::String TemplateStore::exportFile(const juce::String& id, const juce::File& destination) const
{
    if (!validId(id) || !fileFor(id).existsAsFile()) return "template_not_found";
    auto target = destination.getFullPathName().isEmpty() ? juce::File() : destination;
    if (target == juce::File()) return "template_write_failed";
    if (!target.hasFileExtension("xml")) target = target.withFileExtension("xml");
    target.getParentDirectory().createDirectory();
    return fileFor(id).copyFileTo(target) ? juce::String() : juce::String("template_write_failed");
}

juce::String TemplateStore::importFile(const juce::File& source, juce::String& id) const
{
    const auto parsed = juce::XmlDocument::parse(source);
    if (parsed == nullptr || !parsed->hasTagName("LIGHTHOSTTEMPLATE") || parsed->getIntAttribute("version") != 1)
        return "template_session_invalid";
    TemplateSnapshot snapshot;
    snapshot.session = childCopy(*parsed, "LIGHTHOSTSESSION");
    if (auto* wrap = parsed->getChildByName("AUDIOSTATE"))
        if (auto* child = wrap->getFirstChildElement()) snapshot.device = std::make_unique<juce::XmlElement>(*child);
    snapshot.channels = childCopy(*parsed, "CHANNELS");
    if (auto* host = parsed->getChildByName("HOST"))
    {
        snapshot.monoInputs = host->getBoolAttribute("monoInputs");
        snapshot.monoOutput = host->getBoolAttribute("monoOutput");
        snapshot.persistence = host->getStringAttribute("persistence", "disabled");
        snapshot.muted = host->getBoolAttribute("muted");
        snapshot.bypassed = host->getBoolAttribute("bypassed");
    }
    if (snapshot.session == nullptr) return "template_session_invalid";
    auto name = parsed->getStringAttribute("name");
    if (name.isEmpty()) name = source.getFileNameWithoutExtension();
    for (int extra = 2; extra < 100; ++extra)
    {
        const auto error = create(name, snapshot, id);
        if (error != "template_name_taken") return error;
        name = parsed->getStringAttribute("name") + " " + juce::String(extra);
        if (parsed->getStringAttribute("name").isEmpty()) name = source.getFileNameWithoutExtension() + " " + juce::String(extra);
    }
    return "template_name_taken";
}
}

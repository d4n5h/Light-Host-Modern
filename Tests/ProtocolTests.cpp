#include "IpcProtocol.h"
#include "PluginInstanceId.h"
#include <iostream>

using namespace lightHostModern::ipc;
static void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}
int main()
{
    try
    {
        const auto parse = [](const char* command, const char* args) {
            return parseRequest(juce::String("{\"version\":") + juce::String(protocolVersion) + ",\"id\":\"test\",\"command\":\"" + command + "\",\"args\":" + args + "}");
        };
        require(static_cast<bool>(parse("snapshot", "[]")), "snapshot schema");
        require(static_cast<bool>(parse("chain-profiles", "[]")), "chain profile list");
        require(static_cast<bool>(parse("create-chain-profile", R"(["Stage"])")), "create profile name");
        require(static_cast<bool>(parse("switch-chain-profile", R"(["0123456789abcdef0123456789abcdef"])")), "switch profile id");
        require(static_cast<bool>(parse("duplicate-chain-profile", R"(["0123456789abcdef0123456789abcdef"])")), "duplicate profile id");
        require(static_cast<bool>(parse("delete-chain-profile", R"(["0123456789abcdef0123456789abcdef"])")), "delete profile id");
        require(static_cast<bool>(parse("rename-chain-profile", R"(["0123456789abcdef0123456789abcdef","Stage"])")), "rename profile");
        require(!parse("create-chain-profile", "[1]"), "numeric profile name rejected");
        require(!parse("switch-chain-profile", "[]"), "missing profile id rejected");
        require(!parse("rename-chain-profile", R"(["only-one"])"), "rename requires two strings");
        require(!parse("delete-chain-profile", "[0]"), "numeric profile id rejected");
        require(!parse("chain-profiles", R"(["extra"])"), "profile list rejects arguments");
        require(static_cast<bool>(parse("set-global-mute", "[true]")), "typed global mute");
        require(static_cast<bool>(parse("set-global-bypass", "[false]")), "typed global bypass");
        require(!parse("set-global-mute", "[1]"), "numeric global mute rejected");
        require(static_cast<bool>(parse("remove-plugin", "[\"0123456789abcdef0123456789abcdef\"]")), "instance ID accepted");
        require(!parse("remove-plugin", "[0]"), "legacy instance index rejected");
        require(static_cast<bool>(parse("swap-plugin-with", "[\"first\",\"second\"]")), "two instance IDs accepted");
        require(!parse("swap-plugin-with", "[\"first\",1]"), "mixed index and ID rejected");
        require(static_cast<bool>(parse("set-input-channel", "[31,true]")), "typed channel arguments");
        require(!parse("set-input-channel", "[31,1]"), "boolean coercion forbidden");
        require(!parse("set-buffer-size", "[\"64\"]"), "string coercion forbidden");
        require(static_cast<bool>(parse("remove-known-plugin", "[\"stable-class-id\"]")), "installed ID accepted");
        require(!parse("remove-known-plugin", "[0]"), "installed index rejected");
        require(static_cast<bool>(parse("plugin-scan-failures", R"([{"scanId":"scan","revision":4,"offset":100,"limit":100}])")), "failure page object accepted");
        require(!parse("plugin-scan-failures", R"(["scan",100])"), "unversioned failure page rejected");
        require(static_cast<bool>(parse("retry-plugin-scan-selection", R"([{"scanId":"scan","revision":4,"ids":["failure"]}])")), "selected retry object accepted");
        require(!parse("remove-plugin", "[2147483648]"), "integer overflow forbidden");
        require(!parse("remove-plugin", "[0.5]"), "fractional indices forbidden");
        require(!parse("quit-host", "[0]"), "unexpected args forbidden");
        require(static_cast<bool>(parse("add-strip", R"(["Mic"])")), "add strip");
        require(static_cast<bool>(parse("remove-strip", R"(["0123456789abcdef0123456789abcdef"])")), "remove strip");
        require(static_cast<bool>(parse("rename-strip", R"(["0123456789abcdef0123456789abcdef","Mic"])")), "rename strip");
        require(static_cast<bool>(parse("set-strip-gain", R"([{"stripId":"0123456789abcdef0123456789abcdef","gainDb":-6}])")), "strip gain");
        require(static_cast<bool>(parse("set-strip-pan", R"([{"stripId":"0123456789abcdef0123456789abcdef","pan":-1}])")), "strip pan");
        require(!parse("set-strip-pan", R"(["left"])"), "pan rejects text");
        require(static_cast<bool>(parse("set-master-gain", "[-3.5]")), "master gain");
        require(static_cast<bool>(parse("set-strip-routing", R"([{"stripId":"0123456789abcdef0123456789abcdef","allInputs":false,"allOutputs":true,"inputs":[0,1],"outputs":[]}])")), "strip routing");
        require(static_cast<bool>(parse("add-known-plugin-at", R"([{"stripId":"0123456789abcdef0123456789abcdef","knownId":"class","beforeInstanceId":""}])")), "insert plugin");
        require(static_cast<bool>(parse("move-plugin-to-strip", R"([{"stripId":"0123456789abcdef0123456789abcdef","instanceId":"11111111111111111111111111111111","beforeInstanceId":""}])")), "move to strip");
        require(static_cast<bool>(parse("undo-chain", "[]")) && static_cast<bool>(parse("redo-chain", "[]")), "undo commands");
        require(!parse("set-strip-gain", R"(["0123456789abcdef0123456789abcdef"])"), "gain rejects text");
        require(!parse("set-master-gain", R"(["0"])"), "master gain rejects text");
        require(!parse("add-known-plugin-at", "[]"), "insert requires an object");
        require(!parse("undo-chain", "[1]"), "undo rejects arguments");
        require(!parse("undefined-command", "[]"), "unknown command rejected");
        const auto text = parse("scan-plugin-path", R"(["C:\\Plugins\\\u65e5\u672c [x] {y} : \"z\""])");
        require(static_cast<bool>(text) && text.args[0].toString().contains(juce::String::fromUTF8(u8"\u65e5\u672c")), "JSON Unicode and delimiters preserved");
        auto incompatible = parseRequest(R"({"version":1,"id":"test","command":"snapshot","args":[]})");
        require(incompatible.errorCode == "incompatible_version", "version rejected before dispatch");
        require(!parseRequest("remove-plugin:0"), "legacy mutations rejected");
        require(!parseRequest("{invalid"), "malformed JSON rejected");
        const auto response = juce::JSON::parse(errorResponse(incompatible));
        require(response["id"].toString() == "test" && response["error"]["code"].toString() == "incompatible_version", "structured error retains id");
        juce::PropertySet settings;
        settings.setValue("legacy-state-first", "distinct-state-a");
        settings.setValue("legacy-state-second", "distinct-state-b");
        juce::StringArray assigned;
        const auto first = ensurePluginInstanceId(settings, "instance-first", assigned);
        const auto second = ensurePluginInstanceId(settings, "instance-second", assigned);
        require(first != second && first.length() == 32, "duplicate plugins receive independent IDs");
        auto saved = settings.createXml("SESSION");
        juce::PropertySet restarted;
        restarted.restoreFromXml(*saved);
        assigned.clear();
        require(ensurePluginInstanceId(restarted, "instance-second", assigned) == second
            && ensurePluginInstanceId(restarted, "instance-first", assigned) == first, "IDs survive restart and reordering");
        restarted.setValue("instance-collision", first);
        require(ensurePluginInstanceId(restarted, "instance-collision", assigned) != first, "duplicate stored ID repaired");
        require(restarted.getValue("legacy-state-first") == "distinct-state-a"
            && restarted.getValue("legacy-state-second") == "distinct-state-b", "identity migration preserves legacy state");
        std::cout << "Protocol regressions passed\n";
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}

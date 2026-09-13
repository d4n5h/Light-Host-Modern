#include "RuntimeProfile.h"
#include "ScenarioRunner.h"

int main()
{
    using lightHost::RuntimeProfile;
    using scenarios::require;
    scenarios::Runner runner;
    runner.run("production paths and explicit no-audio", [] {
        require(!RuntimeProfile::parse({}, L"C:\\Local").test, "Production changed");
        require(!RuntimeProfile::parse({}, L"C:\\Local").noAudio, "Production audio changed");
        require(RuntimeProfile::parse({L"--no-audio"}, L"C:\\Local").noAudio, "No-audio ignored");
    });
    runner.run("host and UI agree across argument syntax and working directory", [] {
        const auto host = RuntimeProfile::parse({L"--test-profile=audio", L"--profile-root=C:\\Tests"}, L"C:\\Local");
        const auto ui = RuntimeProfile::parse({L"--test-profile", L"audio", L"--profile-root", L"C:\\Tests"}, L"C:\\Other");
        require(host.noAudio && host.test, "Test audio opened automatically");
        require(host.key == ui.key && host.pipeName() == ui.pipeName(), "Profile identity differs");
        require(host.uiSettings() == ui.uiSettings(), "Preferences differ");
        const auto other = RuntimeProfile::parse({L"--test-profile=audio", L"--profile-root=C:\\Other"}, L"C:\\Local");
        require(host.key != other.key && host.windowTitle() != other.windowTitle(), "Profiles collide");
    });
    runner.run("malformed profiles cannot fall back to production", [] {
        for (const auto& args : std::vector<std::vector<std::wstring>>{
            {L"--test-profile"}, {L"--test-profile="}, {L"--test-profile=../main"},
            {L"--profile-root=C:\\Tests"}, {L"--test-profile=ok", L"--profile-root=relative"},
            {L"--test-profile=ok", L"--test-profile=other"}})
        {
            bool rejected = false;
            try { RuntimeProfile::parse(args, L"C:\\Local"); } catch (const std::exception&) { rejected = true; }
            require(rejected, "Invalid profile was accepted");
        }
    });
    return runner.result();
}

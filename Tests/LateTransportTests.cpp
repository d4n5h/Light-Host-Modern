#include "HostTransport.h"
#include "ScenarioRunner.h"

using namespace lightHost::ipc;

static std::string reply(const std::string& wire, const wchar_t* status, const wchar_t* session = L"host-one")
{
    auto response = parseObject(wire);
    response.SetNamedValue(L"status", JsonValue::CreateStringValue(status));
    response.SetNamedValue(L"hostSession", JsonValue::CreateStringValue(session));
    return winrt::to_string(response.Stringify());
}

int main()
{
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    scenarios::Runner tests;
    using scenarios::require;
    tests.run("acceptance is followed by status queries, with no mutation replay", [] {
        auto state = std::make_shared<ClientState>();
        int effects = 0, queries = 0;
        std::string operationId;
        state->transport = [&](const auto&, const std::string& wire, DWORD) {
            const auto command = extractString(wire, "command");
            if (command == "hello") return reply(wire, L"ok");
            auto result = parseObject(reply(wire, L"operation"));
            if (command == "duplicate-plugin")
            {
                ++effects; operationId = extractString(wire, "id");
                result.SetNamedValue(L"operationState", JsonValue::CreateStringValue(L"queued"));
            }
            else
            {
                require(command == "operation-status", "Transport repeated a mutation");
                require(extractArray(wire, "args").GetStringAt(0) == winrt::to_hstring(operationId), "Lookup lost original request ID");
                ++queries;
                result.SetNamedValue(L"operationState", JsonValue::CreateStringValue(queries == 1 ? L"running" : L"completed"));
                result.SetNamedValue(L"result", parseObject(reply(wire, L"ok")));
            }
            return winrt::to_string(result.Stringify());
        };
        auto result = winrt::to_string(requestAsync(state, L"simulated", "duplicate-plugin:instance").get());
        require(extractString(result, "status") == "ok" && effects == 1 && queries == 2, "Late operation did not complete exactly once");
        require(state->pending.empty(), "Completed request remained pending");
    });
    tests.run("lost acknowledgement is reconciled after reconnection", [] {
        auto state = std::make_shared<ClientState>();
        state->hostSession = "host-one";
        int effects = 0, queries = 0;
        state->transport = [&](const auto&, const std::string& wire, DWORD) {
            const auto command = extractString(wire, "command");
            if (command == "remove-plugin") { ++effects; return std::string{}; }
            if (command == "operation-status") { ++queries; return reply(wire, L"ok"); }
            require(command == "snapshot", "Unexpected automatic mutation replay");
            return reply(wire, L"online");
        };
        require(requestAsync(state, L"simulated", "remove-plugin:instance").get().empty(), "Unknown result fabricated success");
        requestAsync(state, L"simulated", "snapshot").get();
        require(effects == 1 && queries == 1 && state->takeLateResults().size() == 1, "Pending operation was not reconciled");
    });
    tests.run("a restarted host cannot authorize replay of a lost operation", [] {
        auto state = std::make_shared<ClientState>();
        state->hostSession = "host-one";
        int effects = 0;
        state->transport = [&](const auto&, const std::string& wire, DWORD) {
            const auto command = extractString(wire, "command");
            if (command == "add-known-plugin") { ++effects; return std::string{}; }
            if (command == "operation-status")
            {
                require(extractArray(wire, "args").GetStringAt(1) == L"host-one", "Lookup used the restarted session");
                auto result = parseObject(reply(wire, L"error", L"host-two"));
                result.SetNamedValue(L"code", JsonValue::CreateStringValue(L"host_restarted"));
                return winrt::to_string(result.Stringify());
            }
            return reply(wire, L"online", L"host-two");
        };
        requestAsync(state, L"simulated", "add-known-plugin:0").get();
        requestAsync(state, L"simulated", "snapshot").get();
        const auto late = state->takeLateResults();
        require(effects == 1 && late.size() == 1 && extractString(late.front(), "code") == "host_restarted", "Restart silently repeated an action");
        require(state->hostSession == "host-two", "Snapshot did not establish the new session");
    });
    return tests.result();
}

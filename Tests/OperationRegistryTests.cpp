#include "OperationRegistry.h"
#include "ScenarioRunner.h"
#include <future>

int main()
{
    using namespace lightHost::ipc;
    using scenarios::require;
    scenarios::Runner runner;
    auto clock = OperationRegistry::Clock::time_point{};
    runner.run("acceptance, late result, duplicate and conflicting content", [&] {
        OperationRegistry registry([&] { return clock; });
        require(registry.accept("add", "plugin-a").inserted, "First request not queued");
        require(!registry.accept("add", "plugin-a").inserted, "Duplicate queued twice");
        require(registry.accept("add", "plugin-b").error == "request_id_conflict", "ID reused for different effect");
        require(registry.start("add") && !registry.start("add"), "Execution is not exactly once");
        clock += std::chrono::hours(1);
        require(registry.find("add")->state == OperationState::running, "In-flight operation expired");
        registry.finish("add", "result", true);
        require(registry.find("add")->response == "result", "Lost late result");
        clock += std::chrono::seconds(599);
        require(registry.find("add").has_value(), "Result expired early");
        clock += std::chrono::seconds(1);
        require(!registry.find("add"), "Result exceeded retention");
    });
    runner.run("bounded completed retention preserves ongoing work", [&] {
        OperationRegistry::Limits limits;
        limits.completed = 2; limits.bytes = 100;
        OperationRegistry registry(limits, [&] { return clock; });
        registry.accept("live", "content"); registry.start("live");
        for (int i = 0; i < 10; ++i)
        {
            auto id = std::to_string(i);
            registry.accept(id, "c"); registry.start(id); registry.finish(id, "r", true);
        }
        require(!registry.find("7") && registry.find("8") && registry.find("9"), "Wrong eviction order");
        require(registry.find("live").has_value(), "Ongoing result evicted by capacity");
        registry.accept("large", "c"); registry.start("large"); registry.finish("large", std::string(90, 'r'), true);
        require(!registry.find("8") && registry.find("large"), "Byte limit not enforced");
    });
    runner.run("shutdown cancels queued actions and retains running outcome", [&] {
        OperationRegistry registry;
        registry.accept("queued", "a"); registry.accept("running", "b"); registry.start("running");
        registry.close([](const std::string&) { return "shutdown"; });
        require(!registry.start("queued"), "Shutdown executed pending mutation");
        require(registry.find("queued")->state == OperationState::cancelled, "Missing cancellation status");
        registry.finish("running", "late", false);
        require(registry.find("running")->state == OperationState::failed, "Shutdown lost running result");
        require(registry.accept("new", "a").error == "shutting_down", "Shutdown accepted new effect");
    });
    return runner.result();
}

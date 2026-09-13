#pragma once
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>

namespace scenarios
{
inline void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}
class Runner
{
public:
    template<class Work> void run(const char* name, Work&& work)
    {
        try { work(); ++passed; std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& error) { ++failed; std::cerr << "FAIL " << name << ": " << error.what() << '\n'; }
        catch (...) { ++failed; std::cerr << "FAIL " << name << ": unknown exception\n"; }
    }
    int result() const { return failed == 0 ? 0 : 1; }
private:
    unsigned passed = 0, failed = 0;
};
}

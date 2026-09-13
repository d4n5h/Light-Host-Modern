#include "RealtimeAudit.h"
#include <windows.h>
#include <cstdlib>
#include <new>
#include <cstring>
#include <malloc.h>

namespace
{
thread_local bool cppAllocation = false;
using Malloc = void* (__cdecl*)(size_t);
using Calloc = void* (__cdecl*)(size_t, size_t);
using Realloc = void* (__cdecl*)(void*, size_t);
using Free = void (__cdecl*)(void*);
Malloc realMalloc = nullptr;
Calloc realCalloc = nullptr;
Realloc realRealloc = nullptr;
Free realFree = nullptr;
void* __cdecl auditedMalloc(size_t bytes)
{
    if (!cppAllocation) lightHost::realtimeAudit::allocation();
    return realMalloc(bytes);
}
void* __cdecl auditedCalloc(size_t count, size_t bytes)
{
    if (!cppAllocation) lightHost::realtimeAudit::allocation();
    return realCalloc(count, bytes);
}
void* __cdecl auditedRealloc(void* value, size_t bytes)
{
    if (!cppAllocation)
    {
        if (value) lightHost::realtimeAudit::release();
        if (bytes) lightHost::realtimeAudit::allocation();
    }
    return realRealloc(value, bytes);
}
void __cdecl auditedFree(void* value)
{
    if (value && !cppAllocation) lightHost::realtimeAudit::release();
    realFree(value);
}
struct CppScope { bool previous = cppAllocation; CppScope() { cppAllocation = true; } ~CppScope() { cppAllocation = previous; } };
}

// Only the calling executable is changed; no plugin DLL or system module is patched.
// JUCE's C allocations and C++ allocations are observed separately from plugin
// processBlock scope, including Release builds where CRT debug hooks don't work.
bool installRealtimeAllocationAudit()
{
    if (lightHost::realtimeAudit::available.load()) return true;
    auto* base = reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
    const auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    const auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    auto* imports = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
    unsigned installed = 0;
    for (; imports->Name != 0; ++imports)
    {
        if (!imports->OriginalFirstThunk) continue;
        auto* names = reinterpret_cast<IMAGE_THUNK_DATA*>(base + imports->OriginalFirstThunk);
        auto* addresses = reinterpret_cast<IMAGE_THUNK_DATA*>(base + imports->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++addresses)
        {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            const auto* name = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData)->Name;
            void* replacement = nullptr;
            const auto current = reinterpret_cast<void*>(addresses->u1.Function);
            if (std::strcmp(name, "malloc") == 0) { realMalloc = reinterpret_cast<Malloc>(current); replacement = reinterpret_cast<void*>(auditedMalloc); }
            if (std::strcmp(name, "calloc") == 0) { realCalloc = reinterpret_cast<Calloc>(current); replacement = reinterpret_cast<void*>(auditedCalloc); }
            if (std::strcmp(name, "realloc") == 0) { realRealloc = reinterpret_cast<Realloc>(current); replacement = reinterpret_cast<void*>(auditedRealloc); }
            if (std::strcmp(name, "free") == 0) { realFree = reinterpret_cast<Free>(current); replacement = reinterpret_cast<void*>(auditedFree); }
            if (!replacement) continue;
            DWORD previous = 0;
            if (!VirtualProtect(&addresses->u1.Function, sizeof(addresses->u1.Function), PAGE_READWRITE, &previous)) return false;
            addresses->u1.Function = reinterpret_cast<ULONG_PTR>(replacement);
            DWORD ignored = 0;
            VirtualProtect(&addresses->u1.Function, sizeof(addresses->u1.Function), previous, &ignored);
            ++installed;
        }
    }
    const bool complete = installed >= 3 && realMalloc && realRealloc && realFree;
    lightHost::realtimeAudit::available.store(complete);
    return complete;
}

void* operator new(size_t bytes)
{
    lightHost::realtimeAudit::allocation();
    CppScope scope;
    for (;;)
    {
        if (auto* result = std::malloc(bytes ? bytes : 1)) return result;
        const auto handler = std::get_new_handler();
        if (!handler) throw std::bad_alloc();
        handler();
    }
}
void* operator new[](size_t bytes) { return ::operator new(bytes); }
void operator delete(void* value) noexcept
{
    if (value) lightHost::realtimeAudit::release();
    CppScope scope;
    std::free(value);
}
void operator delete[](void* value) noexcept { ::operator delete(value); }
void operator delete(void* value, size_t) noexcept { ::operator delete(value); }
void operator delete[](void* value, size_t) noexcept { ::operator delete(value); }
void* operator new(size_t bytes, const std::nothrow_t&) noexcept { try { return ::operator new(bytes); } catch (...) { return nullptr; } }
void* operator new[](size_t bytes, const std::nothrow_t&) noexcept { return ::operator new(bytes, std::nothrow); }
void operator delete(void* value, const std::nothrow_t&) noexcept { ::operator delete(value); }
void operator delete[](void* value, const std::nothrow_t&) noexcept { ::operator delete(value); }
void* operator new(size_t bytes, std::align_val_t alignment)
{
    lightHost::realtimeAudit::allocation();
    CppScope scope;
    for (;;)
    {
        if (auto* value = _aligned_malloc(bytes ? bytes : 1, static_cast<size_t>(alignment))) return value;
        const auto handler = std::get_new_handler();
        if (!handler) throw std::bad_alloc();
        handler();
    }
}
void* operator new[](size_t bytes, std::align_val_t alignment) { return ::operator new(bytes, alignment); }
void operator delete(void* value, std::align_val_t) noexcept
{
    if (value) lightHost::realtimeAudit::release();
    CppScope scope;
    _aligned_free(value);
}
void operator delete[](void* value, std::align_val_t alignment) noexcept { ::operator delete(value, alignment); }
void operator delete(void* value, size_t, std::align_val_t alignment) noexcept { ::operator delete(value, alignment); }
void operator delete[](void* value, size_t, std::align_val_t alignment) noexcept { ::operator delete(value, alignment); }

void* operator new(size_t bytes, std::align_val_t alignment, const std::nothrow_t&) noexcept { try { return ::operator new(bytes, alignment); } catch (...) { return nullptr; } }
void* operator new[](size_t bytes, std::align_val_t alignment, const std::nothrow_t&) noexcept { return ::operator new(bytes, alignment, std::nothrow); }
void operator delete(void* value, std::align_val_t alignment, const std::nothrow_t&) noexcept { ::operator delete(value, alignment); }
void operator delete[](void* value, std::align_val_t alignment, const std::nothrow_t&) noexcept { ::operator delete(value, alignment); }


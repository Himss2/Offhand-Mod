#pragma once
namespace pl::memory {
enum class HookPriority { Normal };
class HookHandle {
public:
    HookHandle(void* target, void*, void** original, HookPriority) { *original = target; }
    bool installed() const { return true; }
    void reset() {}
};
}

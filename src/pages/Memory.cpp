#include <Geode/utils/cocos.hpp>
#include "../DevTools.hpp"
#include "../ImGui.hpp"
#include <chrono>

using namespace geode::prelude;

#include <Windows.h>
struct SafePtr {
    uintptr_t addr = 0;
    SafePtr(uintptr_t addr) : addr(addr) {}
    SafePtr(const void* addr) : addr(reinterpret_cast<uintptr_t>(addr)) {}

    bool operator==(void* ptr) const { return as_ptr() == ptr; }
    operator bool() const { return addr != 0; }

    void* as_ptr() const { return reinterpret_cast<void*>(addr); }

    bool is_safe(int size) {
        return !IsBadReadPtr(as_ptr(), size);
    }

    bool read_into(void* buffer, int size) {
        if (!is_safe(size)) return false;

        std::memcpy(buffer, as_ptr(), size);
        return true;
    }

    template <class T>
    T read() {
        T result{};
        read_into(&result, sizeof(result));
        return result;
    }

    SafePtr read_ptr() {
        return SafePtr(this->read<uintptr_t>());
    }

    SafePtr operator+(intptr_t offset) const {
        return SafePtr(addr + offset);
    }
    SafePtr operator-(intptr_t offset) const {
        return SafePtr(addr - offset);
    }

    std::string_view read_c_str(int max_size = 512) {
        if (!is_safe(max_size)) return "";
        auto* c_str = reinterpret_cast<const char*>(as_ptr());
        for (int i = 0; i < max_size; ++i) {
            if (c_str[i] == 0)
                return std::string_view(c_str, i);
        }
        return "";
    }
};

std::string_view demangle(std::string_view mangled) {
    static std::unordered_map<std::string_view, std::string> cached;
    auto it = cached.find(mangled);
    if (it != cached.end()) {
        return it->second;
    }
    auto parts = utils::string::split(std::string(mangled.substr(4)), "@");
    std::string result;
    for (const auto& part : utils::ranges::reverse(parts)) {
        if (part.empty()) continue;
        if (!result.empty())
            result += "::";
        result += part;
    }
    return cached[mangled] = result;
}

struct RttiInfo {
    SafePtr ptr;
    RttiInfo(SafePtr ptr) : ptr(ptr) {}

    std::optional<std::string_view> class_name() {
        auto vtable = ptr.read_ptr();
        if (!vtable) return {};
        auto rttiObj = (vtable - 4).read_ptr();
        if (!rttiObj) return {};
        // always 0
        auto signature = rttiObj.read<int>();
        if (signature != 0) return {};
        auto rttiDescriptor = (rttiObj + 12).read_ptr();
        if (!rttiDescriptor) return {};
        return demangle((rttiDescriptor + 8).read_c_str());
    }
};

void DevTools::drawMemory() {
    using namespace std::chrono_literals;
    static auto lastRender = std::chrono::high_resolution_clock::now();

    static char buffer[256] = {'0', '\0'};
    bool changed = ImGui::InputText("Addr", buffer, sizeof(buffer));
    static int size = 0x100;
    changed |= ImGui::DragInt("Size", &size, 16.f, 0, 0, "%x");
    if (size < 4) {
        size = 4;
    }

    if (ImGui::Button("Selected Node")) {
        auto str = fmt::format("{}", fmt::ptr(m_selectedNode.data()));
        std::memcpy(buffer, str.c_str(), str.size() + 1);
        changed = true;
    }

    auto const timeNow = std::chrono::high_resolution_clock::now();

    // if (timeNow - lastRender < 0.5s) return;

    uintptr_t addr = 0;
    try {
        addr = std::stoull(buffer, nullptr, 16);
    } catch (...) {}

    static std::vector<std::string> texts;
    if (changed) {
        texts.clear();
        for (int i = 0; i < size; i += 4) {
            SafePtr ptr = addr + i;
            RttiInfo info(ptr.read_ptr());
            auto name = info.class_name();
            if (name) {
                texts.push_back(fmt::format("[{:03x}] {}", i, *name));
            } else {
                // scan for std::string (msvc)
                auto size = (ptr + 16).read<size_t>();
                auto capacity = (ptr + 20).read<size_t>();
                // log::debug("Looking for string at {:x} - {} {}", ptr);
                if (size > capacity || capacity < 15) continue;
                // dont care about ridiculous sizes (> 100mb)
                if (capacity > 1e8) continue;
                char* data = nullptr;
                if (capacity == 15) {
                    data = reinterpret_cast<char*>(ptr.as_ptr());
                } else {
                    data = reinterpret_cast<char*>(ptr.read_ptr().as_ptr());
                }
                if (data == nullptr || !SafePtr(data).is_safe(capacity)) continue;
                // quick null term check
                if (data[size] != 0) continue;
                if (strlen(data) != size) continue;
                auto str = std::string_view(data).substr(0, 30);
                auto fmted = matjson::Value(std::string(str)).dump(0);
                texts.push_back(fmt::format("[{:03x}] maybe std::string {} > {}, {}", i, size, capacity, fmted));
            }
        }
    }

    ImGui::PushFont(m_monoFont);
    for (const auto& text : texts) {
        ImGui::TextUnformatted(text.data(), text.data() + text.size());
        // ImGui::Text("%s", text);
    }
    ImGui::PopFont();
}
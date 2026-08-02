#pragma once
#include <flowforge/plugin_v1.h>
#include <string>
#include <vector>

struct LoadedPlugin {
    void*                     handle;
    const flowforge_plugin_t* vtable;
};

/** Manages dlopen/dlclose lifecycle for plugin shared libraries. */
class Loader {
public:
    Loader() = default;
    ~Loader();
    Loader(const Loader&) = delete;
    Loader& operator=(const Loader&) = delete;

    /**
     * Load and initialise a plugin from @p path.
     * Performs: dlopen → ABI version pre-flight (data symbol) → entry fn → init.
     * @return 0 on success, -1 on any failure (error logged to stderr).
     */
    [[nodiscard]] int load(const std::string& path);

    const std::vector<LoadedPlugin>& plugins() const noexcept { return plugins_; }

private:
    std::vector<LoadedPlugin> plugins_;
};

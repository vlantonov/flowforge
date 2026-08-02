#include "loader.hpp"

#include <cstdio>
#include <cstring>
#include <dlfcn.h>

Loader::~Loader() {
    /* Destroy and unload in reverse load order (LIFO). */
    for (auto it = plugins_.rbegin(); it != plugins_.rend(); ++it) {
        if (it->vtable->destroy) {
            it->vtable->destroy();
        }
        dlclose(it->handle);
    }
}

int Loader::load(const std::string& path) {
    void* handle = dlopen(path.c_str(), RTLD_LOCAL | RTLD_NOW);
    if (!handle) {
        fprintf(stderr, "flowforge: dlopen(%s): %s\n", path.c_str(), dlerror());
        return -1;
    }

    /* Pre-flight: check ABI version data symbol before touching any vtable bytes. */
    void* abi_sym = dlsym(handle, "flowforge_plugin_abi_version");
    if (!abi_sym) {
        fprintf(stderr, "flowforge: %s: missing flowforge_plugin_abi_version: %s\n",
                path.c_str(), dlerror());
        dlclose(handle);
        return -1;
    }
    uint32_t plugin_abi = *static_cast<const uint32_t*>(abi_sym);
    if (plugin_abi != FLOWFORGE_PLUGIN_ABI_VERSION) {
        fprintf(stderr, "flowforge: %s: incompatible ABI version: host=%u, plugin=%u\n",
                path.c_str(), FLOWFORGE_PLUGIN_ABI_VERSION, plugin_abi);
        dlclose(handle);
        return -1;
    }

    /* Resolve and call entry point to obtain the vtable. */
    void* entry_sym = dlsym(handle, "flowforge_plugin_entry");
    if (!entry_sym) {
        fprintf(stderr, "flowforge: %s: missing flowforge_plugin_entry: %s\n",
                path.c_str(), dlerror());
        dlclose(handle);
        return -1;
    }

    /* Cast via memcpy to avoid strict-aliasing and -Wpedantic warnings. */
    flowforge_plugin_entry_fn entry_fn = nullptr;
    static_assert(sizeof(entry_fn) == sizeof(entry_sym), "function pointer size mismatch");
    std::memcpy(&entry_fn, &entry_sym, sizeof(entry_fn));

    const flowforge_plugin_t* vtable = entry_fn();
    if (!vtable) {
        fprintf(stderr, "flowforge: %s: flowforge_plugin_entry returned NULL\n", path.c_str());
        dlclose(handle);
        return -1;
    }

    /* Optionally call init(). */
    if (vtable->init) {
        int rc = vtable->init();
        if (rc != 0) {
            fprintf(stderr, "flowforge: %s: init() failed: %s\n",
                    path.c_str(), strerror(-rc));
            dlclose(handle);
            return -1;
        }
    }

    plugins_.push_back({handle, vtable});
    return 0;
}

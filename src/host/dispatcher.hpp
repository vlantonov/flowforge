#pragma once
#include <flowforge/plugin_v1.h>
#include "loader.hpp"
#include <vector>

/** Holds the name of the producing plugin alongside the result it wrote. */
struct DispatchResult {
    const char*        plugin_name; /**< statically-allocated; valid for process lifetime */
    flowforge_result_t result;
};

/** Dispatches a buffer to every loaded plugin and collects successful results. */
class Dispatcher {
public:
    /**
     * Call process() on each plugin in load order.
     * Plugins that return a negative error code are logged and skipped.
     * All other results (including NOT_APPLICABLE) are collected.
     */
    [[nodiscard]] static std::vector<DispatchResult> dispatch(
        flowforge_buf_t buf,
        const std::vector<LoadedPlugin>& plugins
    );
};

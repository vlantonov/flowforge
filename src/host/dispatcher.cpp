#include "dispatcher.hpp"

#include <cstdio>
#include <cstring>

std::vector<DispatchResult> Dispatcher::dispatch(
    flowforge_buf_t buf,
    const std::vector<LoadedPlugin>& plugins)
{
    std::vector<DispatchResult> results;
    results.reserve(plugins.size());

    for (const auto& p : plugins) {
        flowforge_result_t result{};
        int rc = p.vtable->process(buf, &result);
        if (rc < 0) {
            fprintf(stderr, "flowforge: plugin %s process() error: %s\n",
                    p.vtable->name(), strerror(-rc));
            continue;
        }
        results.push_back({p.vtable->name(), result});
    }

    return results;
}

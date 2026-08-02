#include "loader.hpp"
#include "dispatcher.hpp"
#include "output.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <getopt.h>
#include <vector>
#include <string>

static constexpr size_t READ_CHUNK_BYTES = 65535;

static void usage(const char* prog) {
    fprintf(stderr,
        "Usage: %s --plugin <path.so> [--plugin <path2.so> ...] [--input <file>|-]\n",
        prog);
}

int main(int argc, char* argv[]) {
    std::vector<std::string> plugin_paths;
    const char* input_path = nullptr;

    static const struct option long_opts[] = {
        {"plugin", required_argument, nullptr, 'p'},
        {"input",  required_argument, nullptr, 'i'},
        {nullptr,  0,                 nullptr,  0 }
    };

    int c;
    while ((c = getopt_long(argc, argv, "p:i:", long_opts, nullptr)) != -1) {
        switch (c) {
            case 'p': plugin_paths.emplace_back(optarg); break;
            case 'i': input_path = optarg;               break;
            default:
                usage(argv[0]);
                return 2;
        }
    }

    if (plugin_paths.empty()) {
        usage(argv[0]);
        fprintf(stderr, "error: at least one --plugin is required\n");
        return 2;
    }

    Loader loader;
    bool any_failed = false;
    for (const auto& path : plugin_paths) {
        if (loader.load(path) != 0) {
            any_failed = true;
        }
    }

    if (loader.plugins().empty()) {
        fprintf(stderr, "flowforge: no plugins loaded successfully\n");
        return 2;
    }

    /* Open input source. */
    FILE* input = stdin;
    bool owns_file = false;
    if (input_path && std::strcmp(input_path, "-") != 0) {
        input = std::fopen(input_path, "rb");
        if (!input) {
            std::perror(input_path);
            return 3;
        }
        owns_file = true;
    }

    /* Dispatch loop — no heap allocation per chunk. */
    static uint8_t chunk_buf[READ_CHUNK_BYTES];
    size_t bytes_read;
    while ((bytes_read = std::fread(chunk_buf, 1, READ_CHUNK_BYTES, input)) > 0) {
        flowforge_buf_t buf = {chunk_buf, bytes_read};
        auto results = Dispatcher::dispatch(buf, loader.plugins());
        for (const auto& r : results) {
            write_ndjson(r.plugin_name, r.result);
        }
    }

    if (owns_file) {
        std::fclose(input);
    }

    return any_failed ? 1 : 0;
}

#include <gtest/gtest.h>
#include <flowforge/plugin_v1.h>
#include "../src/host/loader.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>

// Build a tiny mock .so with a wrong ABI version at test-time, then verify
// that Loader rejects it.
TEST(LoaderVersionMismatchTest, WrongVersionIsRejected) {
    auto tmp = std::filesystem::temp_directory_path() / "ff_version_test";
    std::filesystem::create_directories(tmp);
    auto src = tmp / "mock_plugin.c";
    auto so  = tmp / "mock_plugin.so";

    const uint32_t wrong = FLOWFORGE_PLUGIN_ABI_VERSION + 1u;
    {
        std::ofstream f(src);
        f << "#include <stdint.h>\n"
          << "__attribute__((used,visibility(\"default\")))\n"
          << "const uint32_t flowforge_plugin_abi_version = " << wrong << "u;\n"
          << "__attribute__((visibility(\"default\")))\n"
          << "void* flowforge_plugin_entry(void) { return (void*)0; }\n";
    }

    // Try compilers in preference order; skip if none available.
    std::string cmd;
    auto try_compile = [&](const char* compiler) -> bool {
        cmd = std::string(compiler) + " -shared -fPIC -o " +
              so.string() + " " + src.string() + " 2>/dev/null";
        return system(cmd.c_str()) == 0;
    };

    if (!try_compile("clang-18") && !try_compile("clang") && !try_compile("gcc")) {
        GTEST_SKIP() << "No C compiler found — skipping version-mismatch test";
    }

    Loader loader;
    int rc = loader.load(so.string());
    EXPECT_EQ(-1, rc) << "Loader must reject a plugin with wrong ABI version";
    EXPECT_TRUE(loader.plugins().empty());

    std::filesystem::remove_all(tmp);
}

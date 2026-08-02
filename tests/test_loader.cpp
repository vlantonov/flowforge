#include <gtest/gtest.h>
#include <flowforge/plugin_v1.h>
#include "../src/host/loader.hpp"

#include <cstdio>
#include <cstring>

TEST(LoaderTest, NonExistentPathReturnsError) {
    Loader loader;
    int rc = loader.load("/tmp/flowforge_nonexistent_plugin_xyz.so");
    EXPECT_EQ(-1, rc);
    EXPECT_TRUE(loader.plugins().empty());
}

TEST(LoaderTest, NotAnElfFileReturnsError) {
    /* /dev/null is not an ELF shared library. */
    Loader loader;
    int rc = loader.load("/dev/null");
    EXPECT_EQ(-1, rc);
    EXPECT_TRUE(loader.plugins().empty());
}

TEST(LoaderTest, LoadingTextFileReturnsError) {
    /* Write a small text file and try to dlopen it. */
    const char* path = "/tmp/flowforge_test_not_elf.txt";
    {
        FILE* f = std::fopen(path, "wb");
        if (f) {
            std::fputs("this is not a shared library\n", f);
            std::fclose(f);
        }
    }
    Loader loader;
    int rc = loader.load(path);
    EXPECT_EQ(-1, rc);
    EXPECT_TRUE(loader.plugins().empty());
    std::remove(path);
}

TEST(LoaderTest, MultipleFailuresDontCrash) {
    Loader loader;
    EXPECT_EQ(-1, loader.load("/no/such/path/a.so"));
    EXPECT_EQ(-1, loader.load("/no/such/path/b.so"));
    EXPECT_TRUE(loader.plugins().empty());
}

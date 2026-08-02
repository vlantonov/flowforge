cmake_minimum_required(VERSION 3.21)
project(flowforge VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(GTest REQUIRED)

# Header-only interface library for the public ABI
add_library(flowforge_plugin_api INTERFACE)
target_include_directories(flowforge_plugin_api INTERFACE include)

# Host binary
add_executable(flowforge
    src/host/main.cpp
    src/host/loader.cpp
    src/host/dispatcher.cpp
    src/host/output.cpp
)
target_link_libraries(flowforge PRIVATE flowforge_plugin_api ${CMAKE_DL_LIBS})
target_compile_options(flowforge PRIVATE -Wall -Wextra -Wpedantic)

# Plugins (MODULE so they can only be dlopen'd, not linked)
foreach(plugin http dns tls_ja3)
    add_library(plugin_${plugin} MODULE src/plugins/${plugin}/plugin_${plugin}.cpp)
    target_link_libraries(plugin_${plugin} PRIVATE flowforge_plugin_api)
    target_compile_options(plugin_${plugin} PRIVATE -Wall -Wextra -Wpedantic)
    set_target_properties(plugin_${plugin} PROPERTIES PREFIX "")
endforeach()
# TLS plugin also needs the vendored md5 files
target_sources(plugin_tls_ja3 PRIVATE src/plugins/tls_ja3/vendor/md5/md5.c)
target_include_directories(plugin_tls_ja3 PRIVATE src/plugins/tls_ja3/vendor/md5)

# Tests
enable_testing()

add_executable(flowforge_test_http
    tests/test_http.cpp
    src/plugins/http/plugin_http.cpp
)
target_link_libraries(flowforge_test_http PRIVATE flowforge_plugin_api GTest::gtest_main)
target_compile_options(flowforge_test_http PRIVATE -Wall -Wextra -Wpedantic)

add_executable(flowforge_test_dns
    tests/test_dns.cpp
    src/plugins/dns/plugin_dns.cpp
)
target_link_libraries(flowforge_test_dns PRIVATE flowforge_plugin_api GTest::gtest_main)
target_compile_options(flowforge_test_dns PRIVATE -Wall -Wextra -Wpedantic)

add_executable(flowforge_test_tls
    tests/test_tls.cpp
    src/plugins/tls_ja3/plugin_tls_ja3.cpp
    src/plugins/tls_ja3/vendor/md5/md5.c
)
target_link_libraries(flowforge_test_tls PRIVATE flowforge_plugin_api GTest::gtest_main)
target_compile_options(flowforge_test_tls PRIVATE -Wall -Wextra -Wpedantic)
target_include_directories(flowforge_test_tls PRIVATE src/plugins/tls_ja3/vendor/md5)

add_executable(flowforge_test_loader
    tests/test_loader.cpp
    src/host/loader.cpp
)
target_link_libraries(flowforge_test_loader PRIVATE flowforge_plugin_api GTest::gtest_main ${CMAKE_DL_LIBS})
target_compile_options(flowforge_test_loader PRIVATE -Wall -Wextra -Wpedantic)

include(GoogleTest)
gtest_discover_tests(flowforge_test_http)
gtest_discover_tests(flowforge_test_dns)
gtest_discover_tests(flowforge_test_tls)
gtest_discover_tests(flowforge_test_loader)

# Install rules
install(TARGETS flowforge DESTINATION bin)
install(TARGETS plugin_http plugin_dns plugin_tls_ja3 DESTINATION lib/flowforge/plugins)
install(DIRECTORY include/flowforge DESTINATION include)

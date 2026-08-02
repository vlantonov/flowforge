from conan import ConanFile
from conan.tools.cmake import cmake_layout


class FlowforgeConan(ConanFile):
    name = "flowforge"
    version = "0.1.0"
    settings = "os", "compiler", "build_type", "arch"
    generators = ["CMakeToolchain", "CMakeDeps"]

    def requirements(self):
        self.requires("gtest/1.14.0")

    def layout(self):
        cmake_layout(self)

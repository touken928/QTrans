from conan import ConanFile
from conan.tools.cmake import CMake, CMakeToolchain, cmake_layout


class SentbreakConan(ConanFile):
    name = "sentbreak"
    version = "0.1.0"
    required_conan_version = ">=2.28"
    package_type = "library"
    settings = "os", "arch", "compiler", "build_type"
    options = {"shared": [True, False], "fPIC": [True, False]}
    default_options = {"shared": False, "fPIC": True}
    exports_sources = "CMakeLists.txt", "cmake/*", "include/*", "src/*", "tests/*"

    def configure(self):
        if self.options.shared:
            self.options.rm_safe("fPIC")

    def generate(self):
        toolchain = CMakeToolchain(self)
        toolchain.variables["BUILD_SHARED_LIBS"] = self.options.shared
        toolchain.generate()

    def layout(self):
        cmake_layout(self)

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        cmake = CMake(self)
        cmake.install()

    def package_info(self):
        self.cpp_info.libs = ["sentbreak"]
        self.cpp_info.set_property("cmake_file_name", "sentbreak")
        self.cpp_info.set_property("cmake_target_name", "sentbreak::sentbreak")

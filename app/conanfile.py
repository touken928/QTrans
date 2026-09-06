from conan import ConanFile
from conan.errors import ConanInvalidConfiguration


class QTransDependencies(ConanFile):
    name = "qtrans-dependencies"
    version = "0.1.0"
    required_conan_version = ">=2.28"
    package_type = "application"
    settings = "os", "arch", "compiler", "build_type"
    generators = "CMakeDeps", "CMakeToolchain"

    def requirements(self):
        self.requires("llama-cpp/b6565")
        self.requires("libcurl/8.21.0")
        self.requires("qt/6.10.3")
        self.requires("simdutf/8.2.0")
        self.requires("spdlog/1.17.0")
        self.requires("gtest/1.17.0")
        if self.settings.os == "Macos":
            self.requires("icu/78.2")

    def configure(self):
        self.options["llama-cpp"].shared = False
        self.options["llama-cpp"].with_curl = False
        self.options["llama-cpp"].with_examples = False
        self.options["llama-cpp"].with_vulkan = self.settings.os == "Windows"
        self.options["libcurl"].shared = False
        self.options["qt"].shared = False
        self.options["qt"].with_icu = False
        self.options["qt"].with_sqlite3 = False
        self.options["qt"].with_pq = False
        self.options["qt"].with_odbc = False
        self.options["qt"].with_brotli = False
        self.options["simdutf"].shared = False
        self.options["spdlog"].shared = False
        self.options["gtest"].shared = False
        if self.settings.os == "Macos":
            self.options["icu"].shared = False

    def validate(self):
        if self.settings.os == "Macos" and self.settings.arch != "armv8":
            raise ConanInvalidConfiguration("QTrans supports macOS ARM64 only")
        if self.settings.os == "Windows" and self.settings.arch != "x86_64":
            raise ConanInvalidConfiguration("QTrans supports Windows x64 only")
        if self.settings.os not in ("Macos", "Windows"):
            raise ConanInvalidConfiguration(
                "QTrans supports macOS ARM64 and Windows x64 only"
            )

"""The Conan recipe of the corvus-regex-ecma262 C++ library: the header from this repository, over the C library's
release archive for the platform (its shared or its static library, as the `shared` option asks; on Windows the static
library built for the C runtime the profile links). The C library is Rust, built and tested in its own repository's CI
for each platform; the archive is checked against the hash recorded here.

    conan create . --build=missing

and in a consumer, `corvus-regex-ecma262-cpp/0.1.1` as a requirement, with
`target_link_libraries(app PRIVATE corvus_regex_ecma262_cpp::corvus_regex_ecma262_cpp)`.
"""
import os

from conan import ConanFile
from conan.errors import ConanInvalidConfiguration
from conan.tools.build import check_min_cppstd
from conan.tools.files import copy, get

C_VERSION = "0.1.1"
C_SHA256 = {
    "aarch64-apple-darwin": "d4f20640c0254d2e1d31986405df9ea1398ed1e2c31fed35ff7a317e768eee69",
    "aarch64-pc-windows-msvc": "9fc1e333ec2bb406c6b1b42ccd5689396f3972302d0c9e2634385e2aa886e485",
    "aarch64-unknown-linux-gnu": "c1435cd5829ed4fae7f7435d6f09f4ea55f53fb7068cbef360ae316b3508a2e5",
    "aarch64-unknown-linux-musl": "f27d10a4a3af3117cb082918018f5342cae39f1de8689c3d44307d42c58298fe",
    "x86_64-apple-darwin": "c77d8fbd3bf02458c7c7b73f6019e5a57b2cdd728185ae885a109bc1f5e02a9a",
    "x86_64-pc-windows-msvc": "52df9b267b1a4310eeb25e0d8e195686e47dc9cde05e1cd84230ffc5f1d9bfb5",
    "x86_64-unknown-linux-gnu": "2a910ffe9a96ae471da3c28bd144b31572878b9df9d0b7492f5988de5e9d3971",
    "x86_64-unknown-linux-musl": "24e27ba057a564bad55b32e3f9239bf7bd16b40348a48385c2661816afe83a4f",
}


class CorvusRegexEcma262CppConan(ConanFile):
    name = "corvus-regex-ecma262-cpp"
    version = "0.1.1"
    description = ("ECMA-262 (JavaScript) regular expressions for C++20: a header-only library over the "
                   "corvus-regex-ecma262 C library, UTF-8 and UTF-16, every flag.")
    license = "Apache-2.0"
    url = homepage = "https://github.com/corvus-dotnet/corvus-regex-ecma262-cpp"
    topics = ("regex", "ecmascript", "javascript", "regexp", "ecma262", "utf-16")
    settings = "os", "arch", "compiler", "build_type"
    options = {"shared": [True, False], "musl": [True, False]}
    default_options = {"shared": False, "musl": False}
    exports_sources = "include/*", "LICENSE"
    package_type = "library"

    def validate(self):
        check_min_cppstd(self, 20)
        if str(self.settings.arch) not in ("x86_64", "armv8"):
            raise ConanInvalidConfiguration("the C library's archives are for x86_64 and armv8")
        if str(self.settings.os) not in ("Linux", "Macos", "Windows"):
            raise ConanInvalidConfiguration("the C library's archives are for Linux, macOS and Windows")
        if self.settings.os == "Windows" and self.settings.compiler != "msvc":
            raise ConanInvalidConfiguration("the C library's Windows archives are built for MSVC")

    def _target(self):
        arch = "x86_64" if self.settings.arch == "x86_64" else "aarch64"
        if self.settings.os == "Windows":
            return f"{arch}-pc-windows-msvc", "zip"
        if self.settings.os == "Macos":
            return f"{arch}-apple-darwin", "tar.gz"
        return f"{arch}-unknown-linux-{'musl' if self.options.musl else 'gnu'}", "tar.gz"

    def build(self):
        target, extension = self._target()
        get(self, f"https://github.com/corvus-dotnet/corvus-regex-ecma262-rs/releases/download/v{C_VERSION}/"
                  f"corvus-regex-ecma262-{C_VERSION}-{target}.{extension}",
            sha256=C_SHA256[target], destination="c", strip_root=True)

    def _static_runtime(self):
        return self.settings.os == "Windows" and str(self.settings.get_safe("compiler.runtime")) == "static"

    def package(self):
        copy(self, "LICENSE", self.source_folder, os.path.join(self.package_folder, "licenses"))
        copy(self, "*", os.path.join(self.source_folder, "include"), os.path.join(self.package_folder, "include"))
        c = os.path.join(self.build_folder, "c")
        copy(self, "corvus_regex_ecma262.h", os.path.join(c, "include"), os.path.join(self.package_folder, "include"))
        lib = os.path.join(self.package_folder, "lib")
        if self.settings.os == "Windows":
            if self.options.shared:
                copy(self, "corvus_regex_ecma262.dll", os.path.join(c, "bin"), os.path.join(self.package_folder, "bin"))
                copy(self, "corvus_regex_ecma262.dll.lib", os.path.join(c, "lib"), lib)
            else:
                copy(self, "corvus_regex_ecma262_mt.lib" if self._static_runtime() else "corvus_regex_ecma262.lib",
                     os.path.join(c, "lib"), lib)
        else:
            if self.options.shared:
                copy(self, "libcorvus_regex_ecma262.dylib" if self.settings.os == "Macos" else "libcorvus_regex_ecma262.so",
                     os.path.join(c, "lib"), lib)
            else:
                copy(self, "libcorvus_regex_ecma262.a", os.path.join(c, "lib"), lib)

    def package_info(self):
        self.cpp_info.set_property("cmake_file_name", "corvus_regex_ecma262_cpp")
        self.cpp_info.set_property("cmake_target_name", "corvus_regex_ecma262_cpp::corvus_regex_ecma262_cpp")
        self.cpp_info.set_property("cmake_target_aliases", ["corvus_regex_ecma262_cpp::corvus_regex_ecma262_cpp_static"])
        if self.settings.os == "Windows":
            if self.options.shared:
                self.cpp_info.libs = ["corvus_regex_ecma262.dll"]
            else:
                self.cpp_info.libs = ["corvus_regex_ecma262_mt" if self._static_runtime() else "corvus_regex_ecma262"]
                self.cpp_info.system_libs = ["kernel32", "ntdll", "userenv", "ws2_32", "dbghelp"]
        else:
            self.cpp_info.libs = ["corvus_regex_ecma262"]
            if not self.options.shared:
                self.cpp_info.system_libs = (["m"] if self.settings.os == "Macos"
                                             else ["pthread", "m", "dl", "rt", "util"])

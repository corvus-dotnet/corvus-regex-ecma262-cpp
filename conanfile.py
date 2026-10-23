"""The Conan recipe of the corvus-regex-ecma262 C++ library: the header from this repository, over the C library's
release archive for the platform (its shared or its static library, as the `shared` option asks; on Windows the static
library built for the C runtime the profile links). The C library is Rust, built and tested in its own repository's CI
for each platform; the archive is checked against the hash recorded here.

    conan create . --build=missing

and in a consumer, `corvus-regex-ecma262-cpp/0.2.1` as a requirement, with
`target_link_libraries(app PRIVATE corvus_regex_ecma262_cpp::corvus_regex_ecma262_cpp)`.
"""
import os

from conan import ConanFile
from conan.errors import ConanInvalidConfiguration
from conan.tools.build import check_min_cppstd
from conan.tools.files import copy, get

C_VERSION = "0.2.1"
C_SHA256 = {
    "aarch64-apple-darwin": "d44b8baa59db5a13a045c0b0ce480b6e434a3d8b1cc9053625373ffc80b5bbaa",
    "aarch64-pc-windows-msvc": "08a9411b94ffbe93cef256c539898ef8f309be32e48035fd2ab2d84385e7d768",
    "aarch64-unknown-linux-gnu": "bebc0379f23754b1674eed3b079d98c4d825dcda85b0af15f432a11802e2f332",
    "aarch64-unknown-linux-musl": "b53960e72efb64692816789f98d1ed657d742ff57ee01f6bf10b502cb51d0766",
    "x86_64-apple-darwin": "568d56da2f2a20a91a70b7054bcd5200d6583534c961cc18abdfe98419a0cc5d",
    "x86_64-pc-windows-msvc": "5201c899b6a28b30184accdae6f3900bf9ddd9332a5b750f33751190b655f274",
    "x86_64-unknown-linux-gnu": "3b292ba2dac7828b84a0102bdf8463b5c04093f7849efab2b606e5756c39ac49",
    "x86_64-unknown-linux-musl": "b77dbc5247693a24771978140b61d42ba3107507901e6e81d61e2e2721f6f3e3",
}


class CorvusRegexEcma262CppConan(ConanFile):
    name = "corvus-regex-ecma262-cpp"
    version = "0.2.1"
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

#!/usr/bin/env python3
import argparse
import logging
import os
import subprocess
import sys

platforms = {
  "linux": ["x86_64"],
  "android": ["arm64-v8a", "armeabi-v7a"],
}


class PlatformBuilder:
  cmake_dir = "/usr"
  llvm_dir = "/usr"
  cmake_build_type = "Release"

  def __init__(self, project_dir, platform, arch):
    self.project_dir = project_dir
    self.platform = platform
    self.arch = arch
    self.cmake_build_dir = f"{project_dir}/build/cmake-build-{platform}-{arch}"
    self.output_dir = f"{project_dir}/build/{platform}/{arch}"
    self.cmake = "cmake" if not self.cmake_dir else f"{self.cmake_dir}/bin/cmake"
    self.clang = "clang" if not self.llvm_dir else f"{self.llvm_dir}/bin/clang"
    self.clangxx = "clang++" if not self.llvm_dir else f"{self.llvm_dir}/bin/clang++"
    self.cmake_args = [
      f"-DCMAKE_BUILD_TYPE={self.cmake_build_type}",
      "-DDOBBY_BUILD_EXAMPLE=OFF",
      "-DDOBBY_BUILD_TEST=OFF",
    ]

  def run(self, cmd, cwd=None):
    logging.info("run: %s", " ".join(cmd) if isinstance(cmd, list) else cmd)
    subprocess.run(cmd, cwd=cwd, check=True)

  def build(self):
    os.makedirs(self.output_dir, exist_ok=True)
    self.configure()
    self.run([self.cmake, "--build", self.cmake_build_dir, "--clean-first", "--target", "dobby", "--target", "dobby_static", "--", "-j8"])
    self.copy_outputs()

  def copy_outputs(self):
    for name in ["libdobby.so", "libdobby.a"]:
      src = os.path.join(self.cmake_build_dir, name)
      if os.path.exists(src):
        self.run(["cp", src, self.output_dir])
    self.run(["cp", os.path.join(self.project_dir, "include/dobby.h"), os.path.join(self.output_dir, "dobby.h")])


class LinuxPlatformBuilder(PlatformBuilder):
  def __init__(self, project_dir, arch):
    super().__init__(project_dir, "linux", arch)
    self.cmake_args += [
      f"-DCMAKE_C_COMPILER={self.clang}",
      f"-DCMAKE_CXX_COMPILER={self.clangxx}",
      "-DCMAKE_SYSTEM_NAME=Linux",
      f"-DCMAKE_SYSTEM_PROCESSOR={arch}",
    ]

  def configure(self):
    self.run([self.cmake, "-S", self.project_dir, "-B", self.cmake_build_dir] + self.cmake_args)


class AndroidPlatformBuilder(PlatformBuilder):
  def __init__(self, android_ndk_dir, project_dir, arch):
    super().__init__(project_dir, "android", arch)
    self.android_ndk_dir = android_ndk_dir
    self.api_level = 21
    self.cmake_args += [
      f"-DCMAKE_TOOLCHAIN_FILE={android_ndk_dir}/build/cmake/android.toolchain.cmake",
      f"-DANDROID_ABI={arch}",
      f"-DANDROID_PLATFORM=android-{self.api_level}",
      "-DDOBBY_ANDROID_USE_XDL=ON",
    ]

  def configure(self):
    self.run([self.cmake, "-S", self.project_dir, "-B", self.cmake_build_dir, "-G", "Ninja"] + self.cmake_args)


if __name__ == "__main__":
  parser = argparse.ArgumentParser()
  parser.add_argument("--platform", type=str, required=True, choices=platforms.keys())
  parser.add_argument("--arch", type=str, required=True)
  parser.add_argument("--library_build_type", type=str, default="static")
  parser.add_argument("--android_ndk_dir", type=str)
  parser.add_argument("--cmake_dir", type=str, default="/usr")
  parser.add_argument("--llvm_dir", type=str, default="/usr")
  args = parser.parse_args()

  logging.basicConfig(level=logging.INFO, format="%(asctime)s - %(levelname)s - %(message)s")
  PlatformBuilder.cmake_dir = args.cmake_dir
  PlatformBuilder.llvm_dir = args.llvm_dir

  project_dir = os.path.dirname(os.path.dirname(os.path.realpath(__file__)))
  if not os.path.exists(f"{project_dir}/CMakeLists.txt"):
    logging.error("Please run this script in Dobby project root directory")
    sys.exit(1)

  archs = platforms[args.platform] if args.arch == "all" else [args.arch]
  for arch in archs:
    if arch not in platforms[args.platform]:
      logging.error("invalid arch %s for platform %s. Supported: %s", arch, args.platform, platforms[args.platform])
      sys.exit(1)
    if args.platform == "android":
      if not args.android_ndk_dir:
        logging.error("ndk dir is required for android platform")
        sys.exit(1)
      builder = AndroidPlatformBuilder(args.android_ndk_dir, project_dir, arch)
    else:
      builder = LinuxPlatformBuilder(project_dir, arch)
    logging.info("build platform: %s, arch: %s", args.platform, arch)
    builder.build()

# Build

This fork is Linux/Android-only. Windows, macOS and iOS build support has been removed.

## CMake build options

```cmake
option(DOBBY_DEBUG "Enable debug logging" OFF)
option(NearBranch "Enable near branch trampoline" ON)
option(FullFloatingPointRegisterPack "Save and pack all floating-point registers" OFF)
option(Plugin.SymbolResolver "Enable symbol resolver" ON)
option(Plugin.Android.BionicLinkerUtil "Enable android bionic linker util" OFF)
option(DOBBY_ANDROID_USE_XDL "Use bundled xDL for enhanced Android ELF symbol resolving" ON)
option(DOBBY_BUILD_EXAMPLE "Build example" OFF)
option(DOBBY_BUILD_TEST "Build test" OFF)
```

## Build Linux

```shell
bash scripts/build_linux.sh
```

Output:

```text
prebuilt/linux-x86_64/libdobby.so
prebuilt/linux-x86_64/libdobby.a
prebuilt/linux-x86_64/dobby.h
```

## Build Android

```shell
export ANDROID_NDK_HOME=/path/to/android-ndk-r25b
bash scripts/build_android.sh
```

The default Android ABI is `arm64-v8a`.

To build explicit ABIs:

```shell
bash scripts/build_android.sh arm64-v8a x86_64
```

Output:

```text
prebuilt/android/<abi>/libdobby.so
prebuilt/android/<abi>/libdobby.a
prebuilt/android/<abi>/dobby.h
prebuilt/android/include/dobby.h
```

## Build with CMake directly

```shell
cmake -S . -B build/linux-x86_64 -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DDOBBY_BUILD_EXAMPLE=ON -DDOBBY_BUILD_TEST=OFF
cmake --build build/linux-x86_64 -j$(nproc)
```

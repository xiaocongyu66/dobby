# Bundled xDL for Dobby

This directory vendors the official xDL source from the supplied `xDL-master` archive:

- upstream source tree: `xDL-master/xdl/src/main/cpp`
- xDL version: 2.3.0
- license: MIT, see `LICENSE`

Dobby builds these xDL sources directly into the Android `libdobby.so` / `libdobby.a`
when `DOBBY_ANDROID_USE_XDL=ON`, so applications only need to package Dobby and can
include `dobby.h` to access both Dobby and xDL APIs.

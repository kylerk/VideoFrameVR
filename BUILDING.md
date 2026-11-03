# Building from source

These steps build the app for development on a desktop. Releases are built with
[packaging/release](packaging/release/README.md) instead, which pins and builds
every dependency itself.

## Dependencies (Ubuntu 26.04)

```sh
sudo apt install build-essential cmake pkg-config curl libopenxr-dev libmpv-dev libglfw3-dev libgl-dev \
  libvulkan-dev libpango1.0-dev libcairo2-dev libcurl4-openssl-dev libgdk-pixbuf-2.0-dev \
  nlohmann-json3-dev libglib2.0-dev libavformat-dev libavcodec-dev libavutil-dev libswscale-dev \
  libsqlite3-dev librsvg2-dev glslang-tools fonts-noto-extra
```

The app also needs **libsmb2 6.3 or newer**. Ubuntu 26.04 has 6.2, so build the
pinned version first, outside this checkout:

```sh
curl -fL https://codeload.github.com/sahlberg/libsmb2/tar.gz/2bcb014b23a8c8cccf534eea8bda2fe5df453cfd -o libsmb2.tar.gz
echo '72727c2b6bb19367b4224e655cd41e1d80b453bf910e967cec5704cbbc40b589  libsmb2.tar.gz' | sha256sum -c -
tar -xf libsmb2.tar.gz
cmake -S libsmb2-2bcb014b23a8c8cccf534eea8bda2fe5df453cfd -B libsmb2-build \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
  -DBUILD_SHARED_LIBS=ON -DENABLE_LIBKRB5=OFF -DENABLE_GSSAPI=OFF -DENABLE_EXAMPLES=OFF
cmake --build libsmb2-build -j2
sudo cmake --install libsmb2-build
sudo ldconfig
pkg-config --modversion libsmb2
```

If you install it somewhere else, point `PKG_CONFIG_PATH` at its `lib/pkgconfig`
before the first build.

## Build and run

```sh
./build.sh                       # builds into build/
./build/videoframevr [video]     # needs SteamVR (or another OpenXR runtime) running
```

You need OpenGL 4.5, Vulkan 1.2 and an active OpenXR runtime. Desktop hardware
decoding uses NVIDIA's NVDEC; other GPUs decode in software.

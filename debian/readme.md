## building deb package
```bash
mkdir build
cd build
cmake ..
make
../debian/make-deb-pkg.sh 1.0.1
```

## building arm64 deb package
Requires `g++-aarch64-linux-gnu`.
```bash
cmake -S . -B build-arm64 -DCMAKE_TOOLCHAIN_FILE=toolchain-aarch64-linux-gnu.cmake \
    -DCMAKE_BUILD_TYPE=Release -DRUN_TESTS=OFF
cmake --build build-arm64
cd build-arm64
../debian/make-deb-pkg.sh 1.0.1 arm64
```

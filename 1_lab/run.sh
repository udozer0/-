rm -rf build

conan install . --output-folder=build --build=missing

cmake -S . -B build \
-DCMAKE_TOOLCHAIN_FILE=build/conan_toolchain.cmake \
-DCMAKE_BUILD_TYPE=Release

cmake --build build
./build/loop config.conf

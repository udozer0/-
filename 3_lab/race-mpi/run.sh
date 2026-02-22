rm -rf build

conan install . --output-folder=build --build=missing

cmake -S . -B build

cmake --build build -j
mpirun -np 6 ./build/race-mpi

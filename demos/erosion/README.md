# Colony Compute Erosion (Windows / D3D11)

This optional sample runs a **compute-shader thermal diffusion step** over a heightfield and writes a grayscale **PGM** image. It is **Windows-only** (D3D11).

## Build

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 ^
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake" ^
  -DVCPKG_MANIFEST_MODE=ON -DBUILD_TESTING=ON -DCOLONY_LTO=OFF ^
  -DCOLONY_BUILD_EROSION_DEMO=ON

cmake --build build --config Release --target ColonyComputeErosion

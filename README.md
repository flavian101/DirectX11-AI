# DirectX11-AI

## Dependencies

Assimp is included as a Git submodule at `extern/assimp`.

Initialize all submodules before configuring:

```powershell
git submodule update --init --recursive
```

To build using the checked-out Assimp source instead of downloading it with FetchContent, pass the source directory when configuring CMake:

```powershell
cmake -S . -B out/build -G Ninja `
  -DFETCHCONTENT_SOURCE_DIR_ASSIMP="$PWD/extern/assimp"
```

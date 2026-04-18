# native/README.md

## Configure
`cmake -S native -B build/native`

## Build
`cmake --build build/native --config Debug`

## Test
`ctest --test-dir build/native -C Debug --output-on-failure`

## Run
`build\\native\\Debug\\ASnap.exe`

## Package (portable ZIP)
`.\scripts\package-windows.ps1 -Configuration Release -RunTests -Version local`

## Notes
- Qt Widgets only; no QML
- CMake only; no qmake
- Non-UI services/models use smart pointers
- UI/network flow honors busy-state locking

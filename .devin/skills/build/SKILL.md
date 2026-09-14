---
name: build
description: Build the ParamotorSim editor and game DLL.
allowed-tools:
  - exec
  - read
permissions:
  allow:
    - Exec(C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat)
    - Exec(taskkill)
---

Build the ParamotorSim project for Win64 Development using UnrealBuildTool.

1. First kill any running UnrealEditor or UnrealEditor-Cmd processes with `taskkill /F /IM UnrealEditor.exe` and `taskkill /F /IM UnrealEditor-Cmd.exe`.
2. Then run:
   `C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat ParamotorSimEditor Win64 Development "C:\Users\sdean\Documents\Unreal Projects\ParamotorSim\ParamotorSim.uproject" -NoLiveCoding`
3. Capture and return the build result (succeeded/failed) and any relevant errors.

---
name: run-game
description: Run ParamotorSim in a windowed game instance and capture a screenshot.
allowed-tools:
  - exec
  - read
permissions:
  allow:
    - Exec(C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe)
    - Exec(python)
---

Launch ParamotorSim in game mode.

1. Start the game with:
   `C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe "C:\Users\sdean\Documents\Unreal Projects\ParamotorSim\ParamotorSim.uproject" -game -NoLiveCoding -ResX=1280 -ResY=720 -Windowed -NoSplash -log`
2. Wait about 6 seconds for the level to load.
3. Use a Python script with `mss` and `pygetwindow` to focus the game window and capture one screenshot to `C:\DevinTools\screenshot.png`.
4. Read the screenshot and show it.

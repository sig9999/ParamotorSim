---
name: inspect-asset
description: Inspect an Unreal animation asset in the ParamotorSim project.
allowed-tools:
  - exec
  - write
  - read
argument-hint: "<AssetPath>"
permissions:
  allow:
    - Exec(C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe)
    - Write(C:\Users\sdean\Documents\Unreal Projects\ParamotorSim\inspect_tmp.py)
    - Exec(python)
---

Inspect a single Unreal asset. The argument is the asset's full Unreal path, e.g. `/Game/Animations/MH_Walk` or `/MetaHumanCharacter/.../AS_MH_Neutral_Walk_Loop_F`.

1. Write a temporary Python commandlet script that loads the asset and prints:
   - `get_path_name()`
   - `get_editor_property('skeleton')` (if available)
   - `get_editor_property('SequenceLength')` (if available)
   - any other useful editor properties.
2. Run the script via `UnrealEditor-Cmd.exe ... -run=pythonscript -script=...`
3. Return the parsed output and clean up the temp script.

"""
spawn_grass_actors.py
=====================
Places GrassWindManager in the active level.
The GrassField is auto-spawned by UGrassFieldSpawner (C++ subsystem),
so it doesn't need level placement.

Run from the UE5.8 editor Python console:
    py "/Game/Python/spawn_grass_actors.py"

This is idempotent — safe to run multiple times.
"""

import unreal

WIND_MANAGER_CLASS = "/Script/ParamotorSim.GrassWindManager"


def log(msg):
    unreal.log("[spawn_grass_actors] " + msg)


def main():
    eal = unreal.EditorAssetLibrary
    editor_subsystem = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    world = editor_subsystem.get_editor_world()

    if not world:
        log("ERROR: No active world")
        return

    # Check if GrassWindManager already exists in the level
    actors = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.GrassWindManager)
    if actors and len(actors) > 0:
        log("GrassWindManager already exists in level — skipping")
        return

    # Spawn GrassWindManager at origin
    spawn_params = unreal.ActorSpawnParameters()
    spawn_params.spawn_collision_handling_override = unreal.SpawnActorCollisionHandlingMethod.ALWAYS_SPAWN

    wind_manager = world.spawn_actor_from_class(
        unreal.GrassWindManager,
        unreal.Vector(0, 0, 0),
        unreal.Rotator(0, 0, 0),
        spawn_params,
    )

    if wind_manager:
        log("SUCCESS: Spawned GrassWindManager at origin")
        # Save the level
        eal.save_asset(world.get_path_name())
        log("Saved level")
    else:
        log("ERROR: Failed to spawn GrassWindManager")


main()

"""Run via UnrealEditor -EnablePlugins=PythonScriptPlugin,EditorScriptingUtilities -ExecutePythonScript=...
Creates only missing maps. Existing user maps are inspected, never overwritten.
"""
import unreal

levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
specs = [
    ("/Game/FPS/Maps/MainMenu_Map", "/Script/FPS.FPSMenuGameMode"),
    ("/Game/FPS/Maps/Lobby_Map", "/Script/FPS.FPSLobbyGameMode"),
    ("/Game/FPS/Maps/Transition_Map", "/Script/Engine.GameModeBase"),
]
for package, mode_path in specs:
    if unreal.EditorAssetLibrary.does_asset_exist(package):
        unreal.log("[FPSSessionAssets] Exists (left unchanged): " + package)
        continue
    if not levels.new_level(package):
        raise RuntimeError("Could not create " + package)
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    mode = unreal.load_class(None, mode_path)
    if not mode:
        raise RuntimeError("Missing native class: " + mode_path)
    world.get_world_settings().set_editor_property("default_game_mode", mode)
    if not levels.save_current_level():
        raise RuntimeError("Could not save " + package)
    unreal.log("[FPSSessionAssets] Created " + package + " mode=" + mode_path)

unreal.log("[FPSSessionAssets] Complete")
unreal.SystemLibrary.quit_editor()

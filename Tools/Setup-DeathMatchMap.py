"""Add owned spawn points to Demo; existing scene actors remain untouched.
Back up Demo.umap before invoking. Do not run while the user is editing FPS.
"""
import math
import unreal

package = '/Game/LP_FPSLite_JC/Maps/Demo'
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
if not levels.load_level(package):
    raise RuntimeError('Cannot load ' + package)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
existing = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.PlayerStart)
if not existing:
    # Inspected scene: 100m square floor at z=0; buildings occupy roughly +/-7m.
    # Capsule centers at 1.1m; ring radius 14m leaves room around the buildings.
    for index in range(16):
        angle = index * math.tau / 16
        x, y = math.cos(angle) * 1400, math.sin(angle) * 1400
        rotation = unreal.Rotator(0, math.degrees(math.atan2(-y, -x)), 0)
        start = actors.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(x, y, 110), rotation)
        if not start:
            raise RuntimeError('Cannot create spawn point ' + str(index))
        start.set_actor_label('FPS_DM_Spawn_' + str(index).zfill(2))
else:
    unreal.log('[FPSMapSetup] Existing player starts preserved: ' + str(len(existing)))
mode = unreal.load_class(None, '/Game/FPS/Blueprint/BP_FPSGameMode.BP_FPSGameMode_C')
base_mode = unreal.load_class(None, '/Script/FPS.FPSGameMode')
if not mode or not unreal.MathLibrary.class_is_child_of(mode, base_mode):
    raise RuntimeError('The playable FPS GameMode must derive from FPSGameMode')
defaults = unreal.get_default_object(mode)
for field in ('default_pawn_class', 'player_controller_class'):
    value = defaults.get_editor_property(field)
    unreal.log('[FPSMapSetup] ' + field + '=' + value.get_path_name())
unreal.log('[FPSMapSetup] seamless=' + str(defaults.get_editor_property('use_seamless_travel')))
world.get_world_settings().set_editor_property('default_game_mode', mode)
if not levels.save_current_level():
    raise RuntimeError('Cannot save ' + package)
unreal.log('[FPSMapSetup] Saved Demo with playable BP_FPSGameMode and player starts')
unreal.SystemLibrary.quit_editor()

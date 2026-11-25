"""Reparent only the Test sandbox mode, preserving its configured classes and graph."""
import json
import shutil
from pathlib import Path
import unreal

package = '/Game/FPS/Test/BP_TestGameMode'
project = Path(unreal.Paths.project_dir())
evidence_dir = project / 'Saved' / 'Diagnostics' / 'TestRecording-20261125'
evidence_dir.mkdir(parents=True, exist_ok=True)
source = project / 'Content' / 'FPS' / 'Test' / 'BP_TestGameMode.uasset'
backup = evidence_dir / 'BP_TestGameMode.before.uasset'
if not backup.exists():
    shutil.copy2(source, backup)
blueprint = unreal.load_asset(package)
parent = unreal.load_class(None, '/Script/FPS.FPSRecordingTestGameMode')
if not blueprint or not parent:
    raise RuntimeError('Missing Test blueprint or compiled recording parent')

def defaults():
    cdo = unreal.get_default_object(blueprint.generated_class())
    result = {}
    for key in ('default_pawn_class', 'player_controller_class', 'hud_class', 'game_state_class', 'player_state_class'):
        obj = cdo.get_editor_property(key)
        result[key] = obj.get_path_name() if obj else None
    return result

before = defaults()
unreal.BlueprintEditorLibrary.reparent_blueprint(blueprint, parent)
unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
after = defaults()
if before != after:
    raise RuntimeError('Reparent changed configured classes; asset not saved: ' + repr((before, after)))
unreal.get_default_object(blueprint.generated_class()).set_editor_property('hide_listen_host_first_person', True)
if not unreal.EditorAssetLibrary.save_loaded_asset(blueprint):
    raise RuntimeError('Failed to save Test GameMode')
report = {'asset': package, 'new_parent': parent.get_path_name(), 'before': before, 'after': after,
          'saved': True, 'backup': str(backup)}
(evidence_dir / 'configuration.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
unreal.log('[TestRecording] ' + json.dumps(report, ensure_ascii=False))

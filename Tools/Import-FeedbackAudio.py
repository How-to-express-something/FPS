"""Import only missing procedural feedback sounds; preserve user assets."""
from pathlib import Path
import unreal

root = Path(unreal.Paths.project_dir()).resolve()
tasks = []
for name in ('S_HitBody', 'S_HitHead', 'S_HitKill'):
    if unreal.EditorAssetLibrary.does_asset_exist('/Game/FPS/Audio/' + name):
        unreal.log('[FPSAudio] Existing sound preserved: ' + name)
        continue
    task = unreal.AssetImportTask()
    task.filename = str(root / 'SourceAssets' / 'Audio' / (name + '.wav'))
    task.destination_path = '/Game/FPS/Audio'
    task.destination_name = name
    task.automated = True
    task.replace_existing = False
    task.save = True
    tasks.append(task)
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)
for name in ('S_HitBody', 'S_HitHead', 'S_HitKill'):
    if not unreal.EditorAssetLibrary.does_asset_exist('/Game/FPS/Audio/' + name):
        raise RuntimeError('Import failed: ' + name)
    asset = unreal.load_asset('/Game/FPS/Audio/' + name)
    # Only alter assets imported from this generator, never an unrelated user replacement.
    sources = asset.get_editor_property('asset_import_data').extract_filenames()
    if any(str(root / 'SourceAssets' / 'Audio').lower() in str(Path(p).resolve()).lower() for p in sources):
        asset.set_editor_property('sound_asset_compression_type', unreal.SoundAssetCompressionType.PCM)
        unreal.EditorAssetLibrary.save_loaded_asset(asset)
    duration = asset.get_editor_property('duration')
    if duration <= 0:
        raise RuntimeError('Invalid duration: ' + name)
    unreal.log('[FPSAudio] ' + name + ' duration=' + str(duration))
unreal.log('[FPSAudio] All three feedback sound assets available')
unreal.SystemLibrary.quit_editor()

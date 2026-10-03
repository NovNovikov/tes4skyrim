"""convert_meshes: a --mesh-subdirs run converts the named NIFs and nothing else."""
import pytest

from asset_convert import asset_pipeline
from pathlib import Path

POST_PASSES = ('_profile_hair_and_grass', '_split_magic_art',
               '_copy_and_fix_textures')


@pytest.fixture
def calls(tmp_path, monkeypatch):
    """Stub every step of convert_meshes; return the list of steps that ran."""
    ran = []
    (tmp_path / 'export' / 'Test.esm' / 'meshes').mkdir(parents=True)
    monkeypatch.chdir(tmp_path)
    monkeypatch.setattr(asset_pipeline, '_activate_namespace', lambda _d: 'tes4')
    monkeypatch.setattr(asset_pipeline, 'assemble_armor', lambda *_a: 0)
    monkeypatch.setattr(asset_pipeline, '_persist_mesh_manifests',
                        lambda *_a: None)
    monkeypatch.setattr(asset_pipeline, '_convert_mesh_tree',
                        lambda *_a: ran.append('batch') or {})
    for name in POST_PASSES:
        monkeypatch.setattr(asset_pipeline, name,
                            lambda *_a, _n=name: ran.append(_n))
    monkeypatch.setattr(asset_pipeline.landscape_normals, 'ensure_ltex_normals',
                        lambda *_a: ran.append('ltex_normals') or (0, 0))
    return ran


def test_filtered_run_converts_only_the_meshes(calls):
    """A --mesh-subdirs run stops after the NIF batch: no whole-tree pass runs."""
    asset_pipeline.convert_meshes('Test.esm', mesh_subdirs=['dungeons/a.nif'])
    assert calls == ['batch']


def test_unfiltered_run_runs_every_pass(calls):
    """A full run still runs the hair/grass, magic art, texture and LTEX passes."""
    asset_pipeline.convert_meshes('Test.esm')
    assert calls == ['batch', *POST_PASSES, 'ltex_normals']


def _dump(folder, sig, *records):
    folder.mkdir(parents=True, exist_ok=True)
    (folder / f'{sig}.txt').write_text(''.join(
        '---RECORD_BEGIN---\n' + '\n'.join(f'{k}={v}' for k, v in rec.items())
        + '\n---RECORD_END---\n' for rec in records), encoding='utf-8')


def test_plugin_mesh_scope_converts_own_models_and_placed_master_only(tmp_path, monkeypatch):
    """An unrelated master's same local FormID and unused meshes stay untouched."""
    from asset_convert.nif import nif_batch

    exp = tmp_path / 'export'
    a, b, patch = [exp / name for name in ('A.esm', 'B.esm', 'Patch.esp')]
    _dump(a, 'STAT', {'FormID': '00000007', 'Model.MODL': 'unused.nif'})
    _dump(b, 'STAT', {'FormID': '01000007', 'Model.MODL': 'placed.nif'})
    (b / '_HEADER.txt').write_text('Master[0]=A.esm\n', encoding='utf-8')
    _dump(patch, 'ARMO', {'FormID': '02000001',
                          'Male.BipedModel.MODL': 'Meshes\\\\Armor\\\\Worn.nif',
                          'Female.WorldModel.MODL': 'armor/dropped.nif'})
    _dump(patch, 'REFR', {'FormID': '02000002', 'NAME': '00000007'})
    (patch / '_HEADER.txt').write_text('Master[0]=B.esm\nMaster[1]=A.esm\n',
                                       encoding='utf-8')
    meshes, out = patch / 'meshes', tmp_path / 'output'
    for rel in ['unused.nif', 'placed.nif', 'armor/worn.nif', 'armor/dropped.nif']:
        path = meshes / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(b'source')
    fragments = patch / 'mesh_scan_fragments'
    fragments.mkdir()
    (fragments / 'previous.jsonl').write_bytes(b'keep other meshes')

    def convert_selected(work, stats, *_args, **_kwargs):
        for args in work:
            target = Path(args[1])
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(b'converted')
            stats['converted'] += 1

    monkeypatch.setattr(nif_batch, '_run_batch', convert_selected)
    result = asset_pipeline._convert_mesh_tree(
        meshes, out, patch, exp, 'Patch.esp', None, False, False, True)
    assert result['converted'] == 3
    assert sorted(p.relative_to(out).as_posix() for p in out.rglob('*.nif')) == [
        'armor/dropped.nif', 'armor/worn.nif', 'placed.nif']
    assert (fragments / 'previous.jsonl').read_bytes() == b'keep other meshes'


def test_empty_plugin_mesh_scope_does_not_rebuild_shared_assets(tmp_path):
    from asset_convert.sources.plugin_assets import model_paths
    from asset_convert.nif.nif_batch import _collect_nifs

    meshes = tmp_path / 'meshes'
    meshes.mkdir()
    (meshes / 'unrelated.nif').write_bytes(b'source')
    assert _collect_nifs(meshes, None, model_filter=model_paths(tmp_path))[0] == []
    assert _collect_nifs(meshes, None)[0] == [meshes / 'unrelated.nif']


def test_plugin_creature_scope_keeps_only_own_and_placed_creatures(tmp_path):
    from asset_convert.havok.creature_pipeline import _creature_folders, _pick_creature_dirs

    exp = tmp_path / 'export'
    master, patch = exp / 'A.esm', exp / 'Patch.esp'
    _dump(master, 'CREA', {'FormID': '00000001',
                           'Model.MODL': 'creatures/wolf/skeleton.nif'},
                          {'FormID': '00000002',
                           'Model.MODL': 'creatures/unused/skeleton.nif'})
    _dump(patch, 'CREA', {'FormID': '01000003',
                          'Model.MODL': 'Meshes\\\\creatures\\\\rat\\\\skeleton.nif'})
    _dump(patch, 'ACRE', {'FormID': '01000004', 'NAME': '00000001'})
    (patch / '_HEADER.txt').write_text('Master[0]=A.esm\n', encoding='utf-8')
    meshes = patch / 'meshes'
    for rel in ('creatures/rat', 'creatures/wolf', 'creatures/unused', 'characters/rat'):
        folder = meshes / rel
        folder.mkdir(parents=True)
        (folder / 'skeleton.nif').write_bytes(b'skeleton')
        (folder / 'idle.kf').write_bytes(b'animation')
    selected = _pick_creature_dirs(_creature_folders(
        str(patch), str(meshes), None, lambda *_: None, True), lambda *_: None)
    assert [Path(path).relative_to(meshes).as_posix() for path, name in selected] == [
        'creatures/rat', 'creatures/wolf']
    narrowed = _creature_folders(str(patch), str(meshes), ['wolf'],
                                  lambda *_: None, True)
    assert [name for path, name, referenced in narrowed] == ['wolf']
    (patch / 'CREA.txt').unlink()
    (patch / 'ACRE.txt').unlink()
    assert _creature_folders(str(patch), str(meshes), None,
                              lambda *_: None, True) == []


def test_plugin_creature_run_preserves_other_registered_projects(tmp_path, monkeypatch):
    import json
    from asset_convert.havok import creature_pipeline as cp, animation_data

    export = tmp_path / 'export' / 'Patch.esp'
    _dump(export, 'CREA', {'FormID': '00000001',
                           'Model.MODL': 'creatures/rat/skeleton.nif'})
    folder = export / 'meshes' / 'creatures' / 'rat'
    folder.mkdir(parents=True)
    (folder / 'skeleton.nif').write_bytes(b'skeleton')
    (folder / 'idle.kf').write_bytes(b'animation')
    out = tmp_path / 'output' / 'Patch.esp' / 'meshes'
    out.mkdir(parents=True)
    def manifest(name):
        return dict(name=name, namespace='patch', project_hkx=f'{name}/project.hkx',
                    behavior_hkx=f'{name}/behavior.hkx', body_dir=name,
                    skeleton_nif=f'{name}/skeleton.nif')
    converted, registered = [], []
    def convert(dirs, *_args):
        converted.extend(name for folder, name in dirs)
        return {name: manifest(name) for folder, name in dirs}, {}
    def register(projects, out_meshes, source, appends, plugin_out):
        registered.extend(m['name'] for m in projects)
        assert appends == {'animdata_appends': ['existing gun']}
        return str(out / 'fragment.json')
    monkeypatch.setattr(cp, 'split_creatures', lambda *_: 0)
    monkeypatch.setattr(cp, '_convert_pool', convert)
    monkeypatch.setattr(cp, 'manifests_under', lambda *_: {'wolf': manifest('wolf')})
    monkeypatch.setattr(cp, '_kept_appends',
                        lambda *_: {'animdata_appends': ['existing gun']})
    monkeypatch.setattr(cp, 'convert_guns', lambda *_: {})
    monkeypatch.setattr(animation_data, 'write_fragment', register)
    result = cp.convert_creatures(str(export), str(out), log=lambda *_: None,
                                   plugin_assets_only=True)
    assert converted == ['rat'] and result['errors'] == {}
    assert set(registered) == {'rat', 'wolf'}
    artifact = json.loads((export / 'creature_projects.json').read_text(encoding='utf-8'))
    assert set(artifact['data']) == {'rat', 'wolf'}

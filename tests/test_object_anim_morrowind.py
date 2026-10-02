"""A Morrowind object's timeline becomes the named sequences Skyrim plays."""

from asset_convert.nif.pyffi_monkey_patch import apply_patches
apply_patches()
from pyffi.formats.nif import NifFormat

from asset_convert.nif.object_anim_morrowind import (animate_timeline,
                                                     drop_unplayed_keyframes)
from asset_convert.nif.sequences import CYCLE_CLAMP, CYCLE_LOOP

#: (time, scale) keys on the animated node: 1 at 0, 3 at 2, 5 at 4.
_SCALE_KEYS = ((0.0, 1.0), (2.0, 3.0), (4.0, 5.0))

#: Morrowind's group markers: Idle covers 0..2, Idle2 covers 2..4.
_GROUP_KEYS = ((0.0, 'Idle: Start'), (2.0, 'Idle: Stop\r\nIdle2: Start'),
               (4.0, 'Idle2: Stop'))


def _keyframe_controller(node):
    """A 4.0.0.2-style NiKeyframeController scaling `node` over 0..4."""
    data = NifFormat.NiKeyframeData()
    group = data.scales
    group.interpolation = 1
    group.num_keys = len(_SCALE_KEYS)
    group.keys.update_size()
    for key, (time, value) in zip(group.keys, _SCALE_KEYS):
        key.time, key.value = time, value
    ctrl = NifFormat.NiKeyframeController()
    ctrl.flags, ctrl.frequency, ctrl.start_time, ctrl.stop_time = 0x0C, 1.0, 0.0, 4.0
    ctrl.data, ctrl.target = data, node
    node.controller = ctrl
    return ctrl, data


def _mesh(with_keys=True):
    """(data, root, gear): a root over one keyframed node, text keys optional."""
    root, gear = NifFormat.NiNode(), NifFormat.NiNode()
    root.name, gear.name = b'Root', b'Gear'
    root.add_child(gear)
    ctrl, kd = _keyframe_controller(gear)
    blocks = [root, gear, ctrl, kd]
    if with_keys:
        tk = NifFormat.NiTextKeyExtraData()
        tk.num_text_keys = len(_GROUP_KEYS)
        tk.text_keys.update_size()
        for key, (time, text) in zip(tk.text_keys, _GROUP_KEYS):
            key.time, key.value = time, text.encode('ascii')
        blocks.append(tk)
    data = NifFormat.Data()
    data.roots = [root]
    data.blocks = blocks
    return data, root, gear


def _sequences(root):
    """{name: sequence} off the root's manager."""
    return {bytes(s.name): s for s in root.controller.controller_sequences}


def _scale_keys(seq):
    """(time, value) scale keys of the sequence's first controlled block."""
    data = seq.controlled_blocks[0].interpolator.data
    return [(round(k.time, 3), round(k.value, 3)) for k in data.scales.keys]


def test_each_group_becomes_a_sequence_retimed_from_zero():
    """Idle loops and Idle2 clamps, each keyed from 0 with the timeline's values."""
    data, root, gear = _mesh()
    assert animate_timeline(data, root, keyed=True) == 2
    seqs = _sequences(root)
    assert seqs[b'Idle'].cycle_type == CYCLE_LOOP
    assert seqs[b'Idle2'].cycle_type == CYCLE_CLAMP
    assert round(seqs[b'Idle2'].stop_time, 3) == 2.0
    keys = _scale_keys(seqs[b'Idle2'])
    assert keys[0] == (0.0, 3.0) and keys[-1] == (2.0, 5.0)
    assert bytes(seqs[b'Idle'].controlled_blocks[0].node_name) == b'Gear'
    assert gear.controller is None


def test_unplayed_groups_leave_the_mesh_still():
    """Without the x-kf Morrowind plays no group, so nothing is sequenced."""
    data, root, gear = _mesh()
    assert animate_timeline(data, root, keyed=False) == 0
    assert not isinstance(root.controller, NifFormat.NiControllerManager)


def test_autoplay_node_loops_its_controllers_as_idle():
    """An AutoPlay node runs its controllers forever, with or without text keys."""
    data, root, gear = _mesh(with_keys=False)
    gear._mw_autoplay = True
    assert animate_timeline(data, root, keyed=False) == 1
    idle = _sequences(root)[b'Idle']
    assert idle.cycle_type == CYCLE_LOOP
    assert round(idle.stop_time, 3) == 4.0


def test_unplayed_keyframe_controllers_are_dropped():
    """A keyframe controller nothing plays is unlinked, keys and all."""
    data, root, gear = _mesh(with_keys=False)
    assert drop_unplayed_keyframes(data) == 1
    assert gear.controller is None

"""A TES4 source's character-sheet tables, in the Morrowind sidecar's formats.

See: docs/commentary/morrowind_runtime.md#tes4-tables
"""

from tes4_export.record_types.morrowind import MW_SKILL_TO_TES4
from tes5_import.actors import attribute_tables as tables
from tes5_import.base import owned_records


def _race(fid: str, playable: int, male: int, female: int) -> dict:
    """A RACE export record with every attribute at `male` / `female`."""
    rec = {'FormID': fid, 'EditorID': f'Race{fid}', 'DATA.Flags': str(playable)}
    for name in owned_records.TES4_ATTRIBUTE_NAMES:
        rec[f'ATTR.Male.{name}'] = str(male)
        rec[f'ATTR.Female.{name}'] = str(female)
    return rec


def test_namesakes_agree_with_the_morrowind_fold():
    """Each TES4 skill's namesake folds back into that same TES4 skill."""
    assert len(tables.TES4_SKILL_TO_MW) == 21
    assert len(set(tables.TES4_SKILL_TO_MW.values())) == 21
    for tes4, mw in tables.TES4_SKILL_TO_MW.items():
        assert MW_SKILL_TO_TES4[mw] == tes4


def test_skill_row_is_keyed_by_the_namesake():
    """Oblivion's Blade (actor value 14) is Long Blade's row, 5."""
    blade = {'DATA.Action': '14', 'DATA.Attribute': '0', 'DATA.Specialization': '0',
             'DATA.UseValue1': '1.5', 'DATA.UseValue2': '0.0'}
    specs, lines = tables.skill_lines([blade])
    assert lines == ['5=0|0|1.5,0'] and specs == {2: 0}


def test_playable_race_lands_on_its_skyrim_race_and_vampire():
    """Dark Elf keys DarkElfRace and its vampire; Dremora is skipped."""
    lines = tables.race_lines([_race('000191C1', 1, 40, 30), _race('00038010', 0, 50, 50)])
    assert lines == ['00013742=Race000191C1|40,40,40,40,40,40,40,40|30,30,30,30,30,30,30,30',
                     '0008883D=Race000191C1|40,40,40,40,40,40,40,40|30,30,30,30,30,30,30,30']


def test_player_row_carries_its_authored_attributes():
    """Oblivion's Player record is the start the race moves."""
    player = {'EditorID': 'Player', 'FULL': 'Bendu Olo', 'RNAM.Race': '00000907',
              'ACBS.Flags': '0', 'ACBS.Level': '1', 'DATA.Strength': '50',
              'DATA.Luck': '50', 'DATA.Personality': '50', 'DATA.Speechcraft': '5'}
    line, = tables.actor_lines([player], [], {})
    key, value = line.split('=', 1)
    fields = value.split('|')
    assert key == 'Player' and fields[0] == 'Imperial' and fields[6] == 'Bendu Olo'
    assert fields[19] == '50,0,0,0,0,0,50,50'
    assert fields[20].split(',')[25] == '5'


def test_only_the_plugins_own_globals_are_listed(monkeypatch):
    """A global adopted from a master is the master's sidecar's row."""
    monkeypatch.setitem(owned_records.WELL_KNOWN_PROPERTIES, 'TES4PlayerStrength', 0x01000A01)
    monkeypatch.setitem(owned_records.WELL_KNOWN_PROPERTIES, 'TES4PlayerLuck', 0x00000A08)
    assert tables.global_lines('Mod.esp', ['Oblivion.esm']) == ['strength=Mod.esp|01000A01']


class _Writer:
    """Just enough PluginWriter for `_emit_global`."""

    def __init__(self):
        self.added = []

    def derive_formid(self, sig: str, edid: str) -> int:
        """A stand-in id per EditorID."""
        return 0x01000000 + len(self.added)

    def add_record(self, sig: str, data: bytes) -> None:
        """Keep what was written."""
        self.added.append(data)


def test_missing_globals_are_created_in_a_dependent(monkeypatch):
    """A master built before the attribute globals existed leaves them to the plugin."""
    monkeypatch.setattr(owned_records, 'WELL_KNOWN_PROPERTIES', {'TES4Fame': 1, 'TES4Infamy': 2,
                                                                 'TES4GoldFenced': 3,
                                                                 'TES4ControlsDisabled': 4})
    writer = _Writer()
    assert owned_records.create_missing_globals(writer) == 8
    assert all(edid in owned_records.WELL_KNOWN_PROPERTIES
               for edid in owned_records.PLAYER_ATTRIBUTE_GLOBALS)

"""Replace Luigi's Balloon World prompt with one "not supported" message.

Balloon World needs Nintendo's online service, which this port does not have.
In Luigi's standby event flow (EventData/BalloonGame.szs: NpcStandby.byml) node
43 sends a 2P player to node 44, the A-only TimeBalloon_Separate message, and
everyone else on to the Play/Quit prompts that start a connection. Both cases
now take node 44, which ends the talk directly (node 50) instead of adding
"Play again sometime!", so no flow entry can reach StartConnect. Node 44's
label, used by no other flow, gets the replacement text in every language,
keeping Luigi's voice tag. The supplied files are verified and never modified.

The same pass renames the Pro Controller for touch players: the English
(USen, EUen) pad-style names and system glossary entry read "Controller /
Device" instead, keeping their leading pad icon tags.

Touch pictures: while touch is in use the host draws the handheld variant of
every message pad picture (hle_odyssey_ui.c). Handheld and controller variants
are the same pictures except the controller names, so the handheld JoyCon,
JoyConR and JoyConIconOnly entries take the controller's content in every
language. The moped tutorial's accelerate and jump pictures (Tutorial.msbt
Motorcycle) name LeftButtonL and LeftButtonR as their handheld variants: those
entries show the SL and SR glyphs, which prepare_odyssey_glyphs.py redraws as a
pedal and an up arrow. In English the cutscene guide ("{+} Skip") takes a + pad
picture whose handheld variant, LeftButtonPlus (single left Joy-Con only), now
reads "Tap anywhere to": touch shows "Tap anywhere to Skip".
"""
from __future__ import annotations

from pathlib import Path
import struct

import prepare_odyssey_aspect as common
import prepare_odyssey_phone as phone

MESSAGE = ('Hey, Bro! Sorry, but Balloon World is not\n'
           'supported in this port yet. Come back\n'
           'some time soon?')
FLOW = Path('EventData/BalloonGame.szs')
FLOW_MEMBER = 'NpcStandby.byml'
MESSAGES_MEMBER = 'TimeBalloon.msbt'
LABEL = 'TimeBalloon_Separate'
LANGUAGES = 14  # Every language folder of the exact 1.4.1 RomFS.
DEVICE = ('Pro Controller', 'Controller / Device')
DEVICE_LANGUAGES = ('USen', 'EUen')
DEVICE_LABELS = {'PadStyle.msbt': ('FullKeyJoyCon', 'FullKeyJoyConR'), 'GlossarySystem.msbt': ('JoyCon_R_00',)}
TOUCH_NAMES = ('JoyCon', 'JoyConR', 'JoyConIconOnly')     # PadStyle Handheld<name> = FullKey<name>
MOPED = ('Tutorial.msbt', 'Motorcycle')
MOPED_PICTURES = {5: ('HandheldButtonY', 'LeftButtonL'), 3: ('HandheldButtonB', 'LeftButtonR')}   # PadStyle tag type: handheld name
PAD_TAG_GROUP, PAD_TAG_HANDHELD = 6, 1                    # ProjectTag PadStyle*: names for Dual, Handheld, FullKey, Left, Right
SKIP = ('PlayGuideSkip.msbt', 'PlayGuideDemoSkip')
SKIP_PAIR = (29, ['MenuButton', 'MenuPlayerL', 'MenuPlayerR'])  # PadPairMenu: the guide's + picture
SKIP_PICTURE = (11, ['DualButtonPlus', 'LeftButtonPlus', 'FullKeyButtonPlus', 'LeftButtonPlus', 'RightButtonPlus'])  # PadStyleButtonPlus
SKIP_TOUCH = ('LeftButtonPlus', 'Tap anywhere to')


class Byml:
    """Read-only BYML v2/v3 locator for in-place scalar edits."""

    def __init__(self, data: bytes):
        if len(data) < 16 or data[:2] not in (b'YB', b'BY'):
            raise ValueError('invalid BYML')
        self.data, self.order = data, '<' if data[:2] == b'YB' else '>'
        self.version, keys, strings, self.root = struct.unpack_from(self.order + 'HIII', data, 2)
        if self.version not in (2, 3):
            raise ValueError(f'unsupported BYML version {self.version}')
        self.keys, self.strings = self._table(keys), self._table(strings)

    def _u32(self, pos: int) -> int:
        return struct.unpack_from(self.order + 'I', self.data, pos)[0]

    def _count(self, pos: int) -> int:
        return int.from_bytes(self.data[pos + 1:pos + 4], 'little' if self.order == '<' else 'big')

    def _table(self, pos: int) -> list[str]:
        if not pos:
            return []
        if self.data[pos] != 0xC2:
            raise ValueError('invalid BYML string table')
        n = self._count(pos)
        offsets = [self._u32(pos + 4 + 4 * i) for i in range(n + 1)]
        return [self.data[pos + offsets[i]:self.data.index(b'\0', pos + offsets[i])].decode() for i in range(n)]

    def entries(self, pos: int):
        """Yield (key or index, type, raw value, value slot) for one container."""
        kind, n = self.data[pos], self._count(pos)
        if kind == 0xC0:
            types, base = self.data[pos + 4:pos + 4 + n], pos + 4 + ((n + 3) & ~3)
            for i in range(n):
                yield i, types[i], self._u32(base + 4 * i), base + 4 * i
        elif kind == 0xC1:
            for i in range(n):
                q = pos + 4 + 8 * i
                key = int.from_bytes(self.data[q:q + 3], 'little' if self.order == '<' else 'big')
                yield self.keys[key], self.data[q + 3], self._u32(q + 4), q + 4
        else:
            raise ValueError(f'BYML container expected at {pos:#x}')

    def get(self, pos: int) -> dict:
        return {key: (kind, raw, slot) for key, kind, raw, slot in self.entries(pos)}

    def value(self, kind: int, raw: int):
        if kind == 0xA0:
            return self.strings[raw]
        if kind == 0xD1:
            return struct.unpack('<i', struct.pack('<I', raw))[0]
        if kind == 0xD0:
            return bool(raw)
        if kind in (0xC0, 0xC1):
            return {key: self.value(t, r) for key, t, r, _ in self.entries(raw)}
        return raw


def _flow_nodes(b: Byml) -> tuple[dict[int, int], dict[str, int]]:
    root = b.get(b.root)
    nodes, entries = {}, {}
    for _, kind, raw, _ in b.entries(root['NodeList'][1]):
        node = b.get(raw)
        if kind != 0xC1 or node['Id'][0] != 0xD1 or b.value(*node['Id'][:2]) in nodes:
            raise ValueError('unexpected or duplicate event flow node')
        nodes[b.value(*node['Id'][:2])] = raw
    for _, _, raw, _ in b.entries(root['EntryList'][1]):
        entry = b.value(0xC1, raw)
        entries[entry['Name']] = entry['NodeId']
    return nodes, entries


def _reachable_types(b: Byml, nodes: dict[int, int], entries: dict[str, int]) -> set[str]:
    types, seen, todo = set(), set(), list(entries.values())
    while todo:
        node_id = todo.pop()
        if node_id in seen or node_id not in nodes:
            continue
        seen.add(node_id)
        node = b.value(0xC1, nodes[node_id])
        types.add(node['Type'])
        todo += [node['NextId']] if 'NextId' in node else []
        todo += [case['NextId'] for case in node.get('CaseEventList', {}).values() if 'NextId' in case]
        if 'JumpEntryName' in node:
            todo.append(entries[node['JumpEntryName']])
    return types


def patch_flow(data: bytes) -> bytes:
    """Route every Talk to the single A-only message; same size as the input."""
    b = Byml(data)
    nodes, entries = _flow_nodes(b)
    want = {
        43: {'Type': 'JudgeSeparatePlay', 'Base': 'QueryJudge',
             'CaseEventList': {0: {'Index': 0, 'Name': 'Yes', 'NextId': 44}, 1: {'Index': 1, 'Name': 'No', 'NextId': 2}}},
        44: {'Type': 'MessageTalk', 'NextId': 45, 'Param': {'Text': {'MessageData': {
             'LabelName': LABEL, 'MessageFileName': 'TimeBalloon', 'MessageType': 'SystemMessage'}}}},
        45: {'Type': 'JumpEntry', 'JumpEntryName': 'TalkEnd'},
        50: {'Type': 'CameraEnd', 'NextId': 8},
    }
    for node_id, expected in want.items():
        actual = b.value(0xC1, nodes[node_id]) if node_id in nodes else None
        if actual is None or any(actual.get(k) != v for k, v in expected.items()):
            raise ValueError(f'{FLOW_MEMBER}: node {node_id} differs from the verified Balloon World flow')
    if entries.get('Talk') != 71 or 'StartConnect' not in _reachable_types(b, nodes, entries):
        raise ValueError(f'{FLOW_MEMBER}: unexpected Talk entry')
    output = bytearray(data)
    no_case = b.get(b.get(nodes[43])['CaseEventList'][1])[1][1]
    for slot, value in ((b.get(no_case)['NextId'][2], 44), (b.get(nodes[44])['NextId'][2], 50)):
        struct.pack_into(b.order + 'i', output, slot, value)
    result = Byml(bytes(output))
    if 'StartConnect' in _reachable_types(result, *_flow_nodes(result)):
        raise ValueError(f'{FLOW_MEMBER}: StartConnect is still reachable')
    return bytes(output)


def replace_message(data: bytes, label: str, text: str) -> bytes:
    """Replace one UTF-16 MSBT message, keeping its leading voice tag."""
    def change(old: bytes, order: str) -> bytes:
        if len(old) < 8 or struct.unpack_from(order + '3H', old, 0) != (0x0E, 4, 0):
            raise ValueError(f'{label}: expected a leading voice tag')
        voice = old[:8 + struct.unpack_from(order + 'H', old, 6)[0]]
        return voice + text.encode('utf-16-le' if order == '<' else 'utf-16-be') + b'\0\0'
    return _rewrite(data, label, change)


def replace_text(data: bytes, label: str, old: str, new: str) -> bytes:
    """Replace the one occurrence of old in a UTF-16 MSBT message; its tags stay as they are."""
    def change(message: bytes, order: str) -> bytes:
        codec = 'utf-16-le' if order == '<' else 'utf-16-be'
        units, found, at = old.encode(codec), [], 0
        while (at := message.find(units, at)) >= 0:
            if at % 2 == 0:
                found.append(at)
            at += 2
        if len(found) != 1:
            raise ValueError(f'{label}: expected one {old!r}')
        return message[:found[0]] + new.encode(codec) + message[found[0] + len(units):]
    return _rewrite(data, label, change)


def copy_message(data: bytes, label: str, source: str) -> bytes:
    """Give label the content of source (same MSBT)."""
    found = {}
    _rewrite(data, source, lambda message, order: found.setdefault('raw', message))
    return _rewrite(data, label, lambda message, order: found['raw'])


def _tags(message: bytes, order: str):
    """Yield (offset, group, type, payload) for each tag of a UTF-16 message."""
    at = 0
    while at + 2 <= len(message):
        unit = struct.unpack_from(order + 'H', message, at)[0]
        if unit == 0x0E:
            group, kind, size = struct.unpack_from(order + '3H', message, at + 2)
            yield at, group, kind, message[at + 8:at + 8 + size]
            at += 8 + size
        elif unit == 0x0F:
            at += 6
        else:
            at += 2


def rename_pad_picture(data: bytes, label: str, kind: int, old: str, new: str) -> bytes:
    """In label's one PadStyle tag of this type, name new as the handheld variant (it was old)."""
    def change(message: bytes, order: str) -> bytes:
        codec = 'utf-16-le' if order == '<' else 'utf-16-be'
        found = [(at, payload) for at, group, k, payload in _tags(message, order) if group == PAD_TAG_GROUP and k == kind]
        if len(found) != 1:
            raise ValueError(f'{label}: expected one pad picture of type {kind}')
        at, payload = found[0]
        names, pos = [], 0
        while pos < len(payload):
            length = struct.unpack_from(order + 'H', payload, pos)[0]
            names.append(payload[pos + 2:pos + 2 + length].decode(codec))
            pos += 2 + length
        if len(names) != 5 or names[PAD_TAG_HANDHELD] != old:
            raise ValueError(f'{label}: unexpected pad picture names {names}')
        names[PAD_TAG_HANDHELD] = new
        body = b''.join(struct.pack(order + 'H', 2 * len(n)) + n.encode(codec) for n in names)
        tag = struct.pack(order + '4H', 0x0E, PAD_TAG_GROUP, kind, len(body)) + body
        return message[:at] + tag + message[at + 8 + len(payload):]
    return _rewrite(data, label, change)


def _pad_tag(kind: int, names, order: str) -> bytes:
    codec = 'utf-16-le' if order == '<' else 'utf-16-be'
    body = b''.join(struct.pack(order + 'H', 2 * len(n)) + n.encode(codec) for n in names)
    return struct.pack(order + '4H', 0x0E, PAD_TAG_GROUP, kind, len(body)) + body


def _touch_skip(data: bytes) -> bytes:
    """The guide's leading pad-pair picture becomes a + pad picture with a touch (handheld) variant."""
    def change(message: bytes, order: str) -> bytes:
        tags = list(_tags(message, order))
        expected = _pad_tag(SKIP_PAIR[0], SKIP_PAIR[1], order)
        if not tags or tags[0][0] != 0 or message[:len(expected)] != expected:
            raise ValueError(f'{SKIP[1]}: unexpected skip guide')
        return _pad_tag(SKIP_PICTURE[0], SKIP_PICTURE[1], order) + message[len(expected):]
    return _rewrite(data, SKIP[1], change)


def _touch_skip_label(data: bytes) -> bytes:
    codec = lambda order: 'utf-16-le' if order == '<' else 'utf-16-be'
    return _rewrite(data, SKIP_TOUCH[0], lambda message, order: SKIP_TOUCH[1].encode(codec(order)) + b'\0\0')


def _touch_pictures(data: bytes) -> bytes:
    for name in TOUCH_NAMES:
        data = copy_message(data, 'Handheld' + name, 'FullKey' + name)
    return data


def _moped(data: bytes) -> bytes:
    for kind, (old, new) in MOPED_PICTURES.items():
        data = rename_pad_picture(data, MOPED[1], kind, old, new)
    return data


def _rewrite(data: bytes, label: str, change) -> bytes:
    if len(data) < 0x20 or data[:8] != b'MsgStdBn' or data[8:10] not in (b'\xff\xfe', b'\xfe\xff') or data[0x0c] != 1:
        raise ValueError('expected a UTF-16 MSBT')
    order = '<' if data[8:10] == b'\xff\xfe' else '>'
    count, size = struct.unpack_from(order + 'H', data, 0x0e)[0], struct.unpack_from(order + 'I', data, 0x12)[0]
    if size != len(data):
        raise ValueError('MSBT size mismatch')
    sections, pos = [], 0x20
    for _ in range(count):
        tag, length = struct.unpack_from(order + '4sI', data, pos)
        if pos + 16 + length > len(data):
            raise ValueError('truncated MSBT section')
        sections.append([tag, data[pos:pos + 16], data[pos + 16:pos + 16 + length]])
        pos = (pos + 16 + length + 15) & ~15
    found = {tag: body for tag, _, body in sections}
    if b'LBL1' not in found or b'TXT2' not in found:
        raise ValueError('MSBT without labels/text')
    labels, lbl = {}, found[b'LBL1']
    for i in range(struct.unpack_from(order + 'I', lbl, 0)[0]):
        n, offset = struct.unpack_from(order + 'II', lbl, 4 + 8 * i)
        for _ in range(n):
            length = lbl[offset]
            labels[lbl[offset + 1:offset + 1 + length].decode()] = struct.unpack_from(order + 'I', lbl, offset + 1 + length)[0]
            offset += 1 + length + 4
    txt = found[b'TXT2']
    n = struct.unpack_from(order + 'I', txt, 0)[0]
    offsets = [struct.unpack_from(order + 'I', txt, 4 + 4 * i)[0] for i in range(n)] + [len(txt)]
    strings = [txt[offsets[i]:offsets[i + 1]] for i in range(n)]
    index = labels.get(label)
    if index is None or index >= n:
        raise ValueError(f'MSBT has no {label}')
    strings[index] = change(strings[index], order)
    table = bytearray(struct.pack(order + 'I', n))
    at = 4 + 4 * n
    for s in strings:
        table += struct.pack(order + 'I', at)
        at += len(s)
    body = bytes(table) + b''.join(strings)
    out = bytearray(data[:0x20])
    for tag, head, section in sections:
        if tag == b'TXT2':
            head, section = head[:4] + struct.pack(order + 'I', len(body)) + head[8:], body
        out += head + section
        out += b'\xab' * (-len(out) % 16)
    struct.pack_into(order + 'I', out, 0x12, len(out))
    return bytes(out)


def _alignment(start: int) -> int:
    return min(start & -start, 0x2000) if start else 0x2000


def replace_member(sarc: bytes, name: str, body: bytes) -> bytes:
    """Replace one top-level SARC member, keeping every other member byte-exact
    and every following member's original start alignment."""
    ranges = phone.members(sarc, name)
    order = '<' if sarc[6:8] == b'\xff\xfe' else '>'
    header = struct.unpack_from(order + 'H', sarc, 4)[0]
    data_start = struct.unpack_from(order + 'I', sarc, 12)[0]
    nodes = header + 12
    target = [i for i, (member, _, _) in enumerate(ranges) if member == name]
    if len(target) != 1:
        raise ValueError(f'expected one {name}')
    t = target[0]
    _, lo, hi = ranges[t]
    growth = len(body) - (hi - lo)
    after = [lo2 - data_start for i, (_, lo2, _) in enumerate(ranges) if i != t and lo2 >= hi]
    alignment = max([_alignment(start) for start in after] or [1])
    delta = max(0, -(-growth // alignment) * alignment)
    out = bytearray(sarc[:lo]) + body + bytes(hi - lo + delta - len(body)) + sarc[hi:]
    for i, (_, start, end) in enumerate(ranges):
        node = nodes + 16 * i
        if i == t:
            struct.pack_into(order + 'I', out, node + 12, start - data_start + len(body))
        elif start >= hi:
            struct.pack_into(order + '2I', out, node + 8, start - data_start + delta, end - data_start + delta)
    struct.pack_into(order + 'I', out, 8, len(out))
    result = bytes(out)
    check = phone.members(result, name)
    for (member, a, b), (_, c, d) in zip(ranges, check):
        expected = body if member == name else sarc[a:b]
        if result[c:d] != expected or (c - data_start) % _alignment(a - data_start):
            raise ValueError(f'{name}: SARC repack changed member {member}')
    return result


def _member(sarc: bytes, name: str, label: str) -> tuple[int, int]:
    found = [(lo, hi) for member, lo, hi in phone.members(sarc, label) if member == name]
    if len(found) != 1:
        raise ValueError(f'{label}: expected one top-level {name}')
    return found[0]


def _repack(source: Path, changes) -> tuple[bytes, bytes]:
    before = source.read_bytes()
    if before[:4] != b'Yaz0':
        raise ValueError(f'{source.name}: expected Yaz0')
    patched = sarc = common.yaz0_decode(before)
    for member, change in changes:
        lo, hi = _member(patched, member, source.name)
        new = change(patched[lo:hi])
        patched = patched[:lo] + new + patched[hi:] if len(new) == hi - lo else replace_member(patched, member, new)
    if patched == sarc:
        raise ValueError(f'{source.name}: nothing changed')
    after = common.yaz0_literal(patched, before[:16])
    if common.yaz0_decode(after) != patched:
        raise ValueError(f'{source.name}: Yaz0 round-trip failed')
    return before, after


def _device_names(data: bytes, labels) -> bytes:
    for label in labels:
        data = replace_text(data, label, *DEVICE)
    return data


def prepare(romfs: Path, overlay: Path) -> list[dict]:
    """Write the changed flow and message archives under overlay; return one record per archive."""
    jobs = {FLOW: [(FLOW_MEMBER, patch_flow)]}
    messages = sorted(romfs.glob('LocalizedData/*/MessageData/SystemMessage.szs'))
    if len(messages) != LANGUAGES:
        raise ValueError(f'expected {LANGUAGES} SystemMessage archives, found {len(messages)}')
    renamed = 0
    for path in messages:
        changes = [(MESSAGES_MEMBER, lambda m: replace_message(m, LABEL, MESSAGE))]
        if path.parts[-3] in DEVICE_LANGUAGES:
            changes += [(member, lambda m, labels=labels: _device_names(m, labels)) for member, labels in DEVICE_LABELS.items()]
            changes += [('PadStyle.msbt', _touch_skip_label), (SKIP[0], _touch_skip)]
            renamed += 1
        changes += [('PadStyle.msbt', _touch_pictures), (MOPED[0], _moped)]   # after the renames: touch shows the new names
        jobs[path.relative_to(romfs)] = changes
    if renamed != len(DEVICE_LANGUAGES):
        raise ValueError(f'expected the {", ".join(DEVICE_LANGUAGES)} SystemMessage archives')
    records = []
    for relative, changes in jobs.items():
        before, after = _repack(romfs / relative, changes)
        target = overlay / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(after)
        records.append({'relative_path': str(relative), 'members': [member for member, _ in changes],
                        'source_sha256': common.sha(before), 'output_sha256': common.sha(after)})
    return records

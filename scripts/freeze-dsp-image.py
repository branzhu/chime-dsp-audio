from pathlib import Path
import argparse
import hashlib
import json
import math
import os
import re
import subprocess

os.chdir(Path(__file__).resolve().parents[1])
parser = argparse.ArgumentParser(description='Import SS compiler memory and explicit ESP32 control bindings; never connects to hardware.')
parser.add_argument('--update', action='store_true', help='validate all inputs, then regenerate the frozen image, map and policy')
parser.add_argument('--source-ref', help='read a manifest-matching Git snapshot; check only, never update')
args = parser.parse_args()
if args.source_ref and args.update:
    parser.error('--source-ref cannot be combined with --update')
source_ref = subprocess.check_output(['git', 'rev-parse', '--verify', args.source_ref + '^{commit}'], text=True).strip() if args.source_ref else None


def fail(message):
    raise SystemExit('DSP import: ' + message)


def require(condition, message):
    if not condition:
        fail(message)


source_cache = {}


def source_bytes(path):
    key = Path(path).as_posix()
    if key not in source_cache:
        source_cache[key] = (subprocess.check_output(['git', 'show', source_ref + ':' + key])
                             if source_ref else Path(path).read_bytes())
    return source_cache[key]


def source_text(path):
    return source_bytes(path).decode('utf-8-sig')


def load_json(path):
    return json.loads(source_text(path))


def collect(tree):
    nodes, links = {}, []

    def walk(obj):
        if isinstance(obj, dict):
            if 'Uid' in obj and 'WritableProperties' in obj:
                uid = obj['Uid']
                require(uid not in nodes, 'duplicate node UID: ' + uid)
                nodes[uid] = obj
            if obj.get('LinkType') == 'SampleAnalog':
                links.append(obj)
            for value in obj.values():
                walk(value)
        elif isinstance(obj, list):
            for value in obj:
                walk(value)
    walk(tree)
    return nodes, links


def node_properties(nodes, uid):
    require(uid in nodes, 'missing bound node ' + uid + '; restore it or review esp32-bindings.json')
    return nodes[uid]['WritableProperties']


out = Path('software/main/generated')
binding_path = Path('dsp/esp32-bindings.json')
binding = load_json(binding_path)
require(binding['schema'] == 1, 'unsupported binding schema')
root = Path(binding['compiler_dir'])
project_path = Path(binding['project'])
# Paths are repository-local even when reviewing a user-edited binding.
for path in (root, project_path):
    require(not path.is_absolute() and '..' not in path.parts, 'binding paths must be repository-relative')
inputs = [binding_path, project_path] + [root / n for n in
          ['Program.dat', 'Program.bin', 'DMem0.dat', 'DMem1.dat', 'PMem.dat', 'ParamAddress.dat', 'compiler_output.log']]
if not args.update:
    manifest = json.loads((out / 'manifest.json').read_text(encoding='utf-8'))
    for file, digest in manifest['source_sha256'].items():
        require(hashlib.sha256(source_bytes(file)).hexdigest() == digest,
                'Frozen DSP source changed: ' + file + '; save/recompile and review, then run --update')

# This board's RAM access policy deliberately remains conservative: 8K 32-bit
# words per bank. Compiler-exported instructions are loaded verbatim.
segments = []
for name, address in [('Program.dat', 0xc000), ('DMem0.dat', 0), ('DMem1.dat', 0x6000)]:
    text = ''.join(source_text(root / name).split())
    require(re.fullmatch('[0-9a-fA-F]*', text) is not None and len(text) % 8 == 0,
            name + ': expected complete hexadecimal 32-bit words')
    data = bytes.fromhex(text)
    require(len(data) <= 8192 * 4, name + ': exceeds board 8K-word memory window')
    require(bool(data) or name == 'DMem1.dat', name + ': empty compiler segment')
    segments.append((name, address, data))
require(not source_text(root / 'PMem.dat').strip(),
        'nonempty PMem.dat layout not supported; review program-data export before importing')
binary = source_text(root / 'Program.bin').split()
require(all(re.fullmatch('[01]{32}', word) for word in binary), 'Program.bin: invalid binary word')
require(b''.join(int(word, 2).to_bytes(4, 'big') for word in binary) == segments[0][2], 'Program.bin/dat mismatch')
log = source_text(root / 'compiler_output.log')
require(re.search(r'Compile succeeded:\s+0 errors, 0 warnings', log) is not None,
        'compiler log does not confirm a clean compile')
parameters = {}
for line in source_text(root / 'ParamAddress.dat').splitlines():
    if not line.strip():
        continue
    fields = line.split()
    require(len(fields) == 2 and fields[1].isdigit(), 'malformed ParamAddress.dat line: ' + line)
    name, address = fields[0], int(fields[1])
    require(name not in parameters, 'duplicate parameter name: ' + name)
    require(0 <= address <= 0xffff, name + ': invalid address')
    parameters[name] = address
banks = [(address, data) for _, address, data in segments if address < 0xc000]


def memory_word(address, label):
    for base, data in banks:
        offset = (address - base) * 4
        if 0 <= offset and offset + 4 <= len(data):
            return int.from_bytes(data[offset:offset + 4], 'big', signed=True)
    fail(label + ': address outside loaded DM0/DM1 segments: ' + hex(address))


def parameter(name):
    require(name in parameters, 'missing bound parameter ' + name + '; restore name or review binding')
    address = parameters[name]
    memory_word(address, name)
    # Aliasing other named parameters is not safe for host writes.
    require([key for key, value in parameters.items() if value == address] == [name],
            name + ': parameter address aliases another symbol')
    return address


project = load_json(project_path)
nodes, _ = collect(project)
processor = node_properties(nodes, binding['processor_uid'])
require(processor.get('ProcessorName') == 'ADAU1451', 'only this board ADAU1451 is supported')
require(node_properties(nodes, binding['core_uid']).get('CoreName') == 'ADAU1451', 'incompatible DSP core')
for key, value in binding['board_properties'].items():
    require(processor.get(key) == value, 'board interface property changed: ' + key + '; review hardware-init compatibility')
require(binding['sample_rate_hz'] == 48000, 'firmware only supports 48kHz')
require(binding['schematic_uid'] in nodes, 'missing bound schematic')
audio_nodes, links = collect(nodes[binding['schematic_uid']])
for uid, node in audio_nodes.items():
    props = node['WritableProperties']
    for key in ('FS', 'FSOut'):
        if key in props:
            require(props[key] == 48000, uid + ': non-48kHz sample processing requires firmware review')
    require(not props.get('ExcludeExport', False), uid + ': excluded audio node cannot be audited')
require(set(binding['outputs']) == {'L', 'R', 'SUB'}, 'binding must declare L/R/SUB')
require({ch: spec['channels'] for ch, spec in binding['outputs'].items()} ==
        {'L': [16], 'R': [17], 'SUB': [32, 33]}, 'output channel assignment differs from board wiring')
input_uid = binding['input']['uid']
input_props = node_properties(audio_nodes, input_uid)
require('ASRCInput' in audio_nodes[input_uid].get('PluginName', ''), 'input must use board ASRC0')
require(binding['input']['pins'] == ['Ch0_out', 'Ch1_out'], 'board USB input is ASRC0 channels 0/1')
require(input_props.get('InUseCh0') and input_props.get('InUseCh1') and
        not any(input_props.get('InUseCh' + str(i), False) for i in range(2, 16)), 'incompatible ASRC input channels')
predecessors = {}
for link in links:
    src, dst = link['SourceUid'], link['TargetUid']
    require(src in audio_nodes and dst in audio_nodes, 'unresolved audio link')
    if src == input_uid:
        require(link['SourcePinName'] in binding['input']['pins'], 'unbound ASRC channel in use')
    predecessors.setdefault(dst, set()).add(src)
# Verify final physical output gates, not a specific EQ/crossover graph. Each
# output is directly fed by its bound scalar Gain. Arbitrary processing can be
# added upstream, but another branch's gate may not affect this branch.
output_nodes = {}
for uid, node in audio_nodes.items():
    channel = node['WritableProperties'].get('SelectedOPChannel')
    if channel is not None:
        require(re.fullmatch(r'Ch\d+', channel) is not None, uid + ': invalid output channel')
        ch = int(channel[2:])
        require(ch not in output_nodes, 'duplicate physical output channel')
        output_nodes[ch] = uid
require(set(output_nodes) == {16, 17, 32, 33}, 'physical outputs must be CH16/17/32/33')
gains, words, saved_gain, saved_is_db, saved_linear = {}, {}, {}, {}, {}
gate_uids = {spec['uid'] for spec in binding['outputs'].values()}
require(len(gate_uids) == 3, 'three output gains must be independent')
for ch, spec in binding['outputs'].items():
    uid = spec['uid']
    props = node_properties(audio_nodes, uid)
    require('Gain' in audio_nodes[uid].get('PluginName', '') and spec['parameter'] == uid + '.Gain',
            ch + ': binding must reference its scalar Gain parameter')
    require(props.get('NumChannels') == 1 and props.get('SlewType') == 'No Slew', ch + ': scalar No Slew Gain required')
    for output in spec['channels']:
        output_links = [link for link in links if link['TargetUid'] == output_nodes[output]]
        require(len(output_links) == 1 and output_links[0]['SourcePinName'] == 'Output' and
                output_links[0]['TargetPinName'] == 'Input', ch + ': invalid physical gate pins or multiple drivers')
        require(predecessors.get(output_nodes[output]) == {uid},
                ch + ': final output must be fed directly by its bound Gain; move new processing upstream of gate')
    # Follow only upstream edges. Feedback is allowed; visited prevents loops.
    pending, seen = list(predecessors.get(uid, ())), set()
    require(bool(pending), ch + ': disconnected output Gain')
    while pending:
        ancestor = pending.pop()
        if ancestor in seen:
            continue
        seen.add(ancestor)
        require(ancestor not in gate_uids, ch + ': another output gate or feedback bypass affects branch isolation')
        upstream = predecessors.get(ancestor, set())
        if not upstream:
            require(ancestor == input_uid, ch + ': audio source is not the bound USB ASRC input')
        pending.extend(upstream)
    require(input_uid in seen, ch + ': no path from USB ASRC input')
    value, is_db = props.get('Gain'), props.get('IsDB')
    require(type(value) in (int, float) and math.isfinite(value) and type(is_db) == bool, ch + ': invalid saved Gain/IsDB')
    # Import the project's gain verbatim, including amplification and polarity
    # inversion. Only the signed 8.24 encoding range is enforced here.
    try:
        linear = 10**(value / 20) if is_db else value
    except OverflowError:
        fail(ch + ': gain outside signed 8.24 encoding range')
    require(math.isfinite(linear) and -128 <= linear <= 128 - 2**-24,
            ch + ': gain outside signed 8.24 encoding range')
    address = parameter(spec['parameter'])
    word = memory_word(address, ch)
    require(abs(word - linear * (1 << 24)) <= 1.01,
            f'{ch} gain mismatch at 0x{address:04x}: saved {value} '+('dB' if is_db else 'linear')+
            f', compiled 0x{word & 0xffffffff:08x}; save and recompile the same project')
    gains[ch], words[ch], saved_gain[ch], saved_is_db[ch], saved_linear[ch] = address, word, value, is_db, linear
require(len(set(gains.values())) == 3, 'output Gain addresses overlap')
# Only explicitly bound scalar parameters may be changed at runtime. Optional
# mixer controls disappear from the whitelist if that block is removed/renamed.
targets = set(gains.values())
optional = {}
for name in binding.get('optional_live_parameters', []):
    if name in parameters:
        address = parameter(name)
        require(address not in targets, 'live parameter aliases a Gain')
        optional[name] = address
        targets.add(address)
safe_names = binding['safeload_parameters']
present = [name in parameters for name in safe_names.values()]
require(all(present) or not any(present), 'partial software Safeload map')
safe = {key: parameter(name) for key, name in safe_names.items()} if all(present) else None
if safe:
    staging = set(range(safe['data'], safe['data'] + 5)) | {safe['address'], safe['count']}
    require(len(staging) == 7, 'Safeload staging slots overlap')
    for address in staging:
        memory_word(address, 'Safeload staging')
    require(any(base <= safe['data'] and safe['data'] + 5 <= base + len(data)//4 for base, data in banks),
            'Safeload data slots must occupy one loaded bank')
    require(not staging & targets, 'Safeload staging overlaps writable scalar parameters')
    require(not any(a in staging and name not in safe_names.values() for name, a in parameters.items()),
            'Safeload staging overlaps another compiler parameter')
else:
    staging = set()

# Generate sorted disjoint permission ranges, independent of L/R/Sub order,
# adjacency and DM0/DM1 bank. Never authorize the holes between parameters.
def runs(first, last, permission):
    result = []
    start, prev = first, permission(first)
    for address in range(first + 1, last + 1):
        current = permission(address)
        if current != prev:
            result.append((start, address - 1, prev))
            start, prev = address, current
    result.append((start, last, prev))
    return result


ram_regions = []
for first, last in [(0, 0x1fff), (0x6000, 0x7fff), (0xc000, 0xdfff)]:
    ram_regions += runs(first, last, lambda address: 'DSP_REGION_READ|DSP_REGION_WRITE|' +
                        ('DSP_REGION_SAFELOAD_TARGET' if address in targets else 'DSP_REGION_STOPPED'))
safe_regions = []
if safe:
    for first, last in [(0, 0x1fff), (0x6000, 0x7fff)]:
        safe_regions += [r for r in runs(first, last, lambda a:
                         'DSP_REGION_READ|DSP_REGION_WRITE|DSP_REGION_SAFELOAD_TARGET' if a in targets else
                         'DSP_REGION_READ|DSP_REGION_WRITE' if a in staging else '') if r[2]]


# 64 fixed register regions + generated RAM regions must fit transfer policy.
require(len(ram_regions) <= 32 and len(safe_regions) <= 96, 'too many bound live ranges for firmware policy')


def macro_regions(name, ranges):
    text = '#define ' + name + ' \\\n'
    text += ', \\\n'.join(f'    {{0x{a:04x}U,0x{b:04x}U,4,{perm}}}' for a, b, perm in ranges)
    return text + '\n'


image = '/* Generated from SS compiler output; no parameter overrides. */\n'
for name, address, data in segments:
    key = name.split('.')[0].lower()
    image += f'#define IMAGE_{key.upper()}_BYTES {len(data)}U\n'
    image += f'static const uint8_t image_{key}[]={{\n'
    payload = data or b'\x00'  # C has no portable zero-length arrays; size macro remains 0.
    for offset in range(0, len(payload), 16):
        image += '    ' + ','.join(f'0x{byte:02x}' for byte in payload[offset:offset+16]) + ',\n'
    image += '};\n'
header = '/* Generated from esp32-bindings.json + compiler symbols; do not hand edit. */\n#pragma once\n'
header += '#define CHIME_DSP_PROFILE ' + json.dumps(binding['profile']) + '\n'
for ch in ('L', 'R', 'SUB'):
    header += f'#define CHIME_DSP_GAIN_{ch} 0x{gains[ch]:04x}U\n#define CHIME_DSP_GAIN_{ch}_WORD 0x{words[ch] & 0xffffffff:08x}U\n'
header += f'#define CHIME_DSP_HAS_SAFELOAD {int(safe is not None)}\n'
for key, name in [('data', 'DATA'), ('address', 'ADDRESS'), ('count', 'COUNT')]:
    header += f'#define CHIME_DSP_SAFE_{name} 0x{safe[key] if safe else 0:04x}U\n'
header += macro_regions('CHIME_DSP_RAM_REGIONS', ram_regions)
if safe:
    header += macro_regions('CHIME_DSP_SAFE_REGIONS', safe_regions)
manifest_new = {
    'schema': 4, 'project': project_path.as_posix(), 'binding': binding_path.as_posix(),
    'tool': project.get('ToolVersionSaved'), 'boot_profile': binding['profile'],
    'source_sha256': {p.as_posix(): hashlib.sha256(source_bytes(p)).hexdigest() for p in inputs},
    'segments': [{'source': n, 'address': f'{a:04x}', 'bytes': len(b), 'sha256': hashlib.sha256(b).hexdigest()} for n, a, b in segments],
    'register_profile': 'reviewed board 48k I2S/ASRC hardware-init subset; NOT vendor register import',
    'eeprom_written': False, 'boot_parameter_overrides': [],
    'saved_project_gain': saved_gain, 'saved_project_gain_is_db': saved_is_db, 'saved_project_gain_linear': saved_linear,
    'compiled_gain': {ch: word/(1 << 24) for ch, word in words.items()},
    'output_gain_addresses': {ch: f'{a:04x}' for ch, a in gains.items()},
    'output_gain_words': {ch: f'{w & 0xffffffff:08x}' for ch, w in words.items()},
    'optional_live_parameters': optional, 'safeload': safe,
    'ram_regions': [{'first': a, 'last': b, 'permissions': p} for a, b, p in ram_regions],
    'source_note': 'Bound output gains match compiler 8.24 within 1 LSB; routing restores exact compiled values. Upstream processing is not topology-pinned or acoustically certified.',
    'board_contract': {'sample_rate_hz': 48000, 'input': binding['input'], 'outputs': binding['outputs']},
}
outputs = {'chime_dsp_image.inc': image, 'chime_dsp_parameters.h': header,
           'manifest.json': json.dumps(manifest_new, indent=2) + '\n'}
# Refuse an input changed by a concurrent SS save/compile while importing.
if not source_ref:
    for path in inputs:
        require(path.read_bytes() == source_bytes(path),
                'input changed during import: ' + path.as_posix() + '; wait for SS compilation and retry')

# All source validations complete before any durable output edit. On a failed
# import existing outputs remain unchanged. Interrupted writes fail strict check.
if args.update:
    out.mkdir(exist_ok=True)
    for name, content in outputs.items():
        temp = out / (name + '.tmp')
        temp.write_text(content, encoding='utf-8', newline='\n')
        temp.replace(out / name)
else:
    for name, content in outputs.items():
        require((out / name).read_text(encoding='utf-8') == content, 'generated ' + name + ' differs; refusing build')
print('PASS frozen DSP image and bindings:', [(n, hex(a), len(b)) for n, a, b in segments])
print('GAINS', {ch: {'address': hex(gains[ch]), 'saved': saved_gain[ch], 'unit': 'dB' if saved_is_db[ch] else 'linear', 'word': f'{words[ch] & 0xffffffff:08x}'} for ch in gains})
print('SAFELOAD', safe if safe else 'absent: flag=1 disabled')
if source_ref:
    print('SOURCE SNAPSHOT:', source_ref, '(manifest-matching; working files unchanged)')

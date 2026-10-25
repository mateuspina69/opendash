"""Structural audit of test-only actor layouts; does not compile or run the game."""
from pathlib import Path
import re
from collections import Counter

ROOT = Path(__file__).resolve().parents[2]
TRANSLATOR = ROOT / "tests" / "translator"


def structs(source):
    source = re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"', '', source, flags=re.S)
    found = {}
    stack = []
    pattern = r'namespace\s*([\w:]*)\s*\{|\bstruct\s+(\w+)\s*(?:\:\s*([^\{;]+))?\s*\{|[{}]'
    for match in re.finditer(pattern, source):
        if match[0] == '}':
            if stack:
                stack.pop()
            continue
        if match[1] is not None:
            stack.append(match[1])
            continue
        if match[2]:
            namespace = '::'.join(part for part in stack if part)
            depth, end = 1, match.end()
            while depth and end < len(source):
                depth += (source[end] == '{') - (source[end] == '}')
                end += 1
            found[namespace + '::' + match[2]] = (match[3] or '', source[match.end():end - 1])
        stack.append('')
    return found


def main():
    native = {}
    for header in (ROOT / 'src').rglob('*.hpp'):
        native.update(structs(header.read_text(encoding='utf-8')))

    def bases(name):
        namespace = name.rsplit('::', 1)[0]
        for base in native[name][0].split(','):
            for candidate in (namespace + '::' + base.strip(), 'od::port::' + base.strip(), base.strip()):
                if candidate in native:
                    yield candidate
                    break

    classes = {name for name, (base, _) in native.items() if 'ActorClassState' in base}
    while True:
        expanded = classes | {name for name in native if any(base in classes for base in bases(name))}
        if expanded == classes:
            break
        classes = expanded

    sources = list(TRANSLATOR.glob('classes_*_layout.cpp')) + [TRANSLATOR / 'player_layout.cpp']
    code = '\n'.join(path.read_text(encoding='utf-8') for path in sources)
    dispatch = Counter(re.findall(r'if \(typeid\(s\) == typeid\(([\w:]+)\)\)', code))
    assert set(dispatch) == classes, ('class dispatch mismatch', sorted(classes - dispatch.keys()), sorted(dispatch.keys() - classes))
    assert all(count == 1 for count in dispatch.values()), 'duplicate class dispatch'

    def members(name, seen=()):
        if name in seen or name not in native:
            return ''
        return native[name][1] + '\n' + '\n'.join(members(base, seen + (name,)) for base in bases(name))

    definitions = Counter()
    for match in re.finditer(r'void layoutValue\(LayoutVisitor& v, uint32_t a, ([\w:]+)& s\) \{\n(.*?)\n}', code, re.S):
        name, body = match.groups()
        definitions[name] += 1
        assert name in native, ('missing native record', name)
        declarations = members(name)
        for field in set(re.findall(r'\bs\.(\w+)', body)):
            assert re.search(r'\b' + field + r'\b', declarations), ('missing native member', name, field)

    header = (TRANSLATOR / 'class_layouts.hpp').read_text(encoding='utf-8')
    declared = Counter(re.findall(r'void layoutValue\(LayoutVisitor&, uint32_t, ([\w:]+)&\);', header))
    assert declared == definitions, 'record declarations and definitions differ'
    assert all(count == 1 for count in definitions.values()), 'duplicate record layout'
    for path in TRANSLATOR.glob('*.cpp'):
        for include in re.findall(r'#include "([^"]+)"', path.read_text(encoding='utf-8')):
            assert any((root / include).is_file() for root in (ROOT / 'src', ROOT / 'tests')), (path.name, include)
    print(f'{len(classes)} class dispatches; {len(definitions)} record layouts; native members and includes accounted for')
    print('Structural checks only; compile, round-trip and golden tests remain the controller\'s checks.')


if __name__ == '__main__':
    main()

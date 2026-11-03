#!/usr/bin/env python3
"""Move fixed C/Python values into subsystem catalogs without changing expressions.

The catalogs are editable source, not generated game data. Reruns preserve names
and add new values. --check reports uncataloged literals. Syntax, documentation,
dynamic f-string fragments, 0/1 and shared data tables remain in their own form.
"""
import argparse
import ast
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
ALIAS = "K"
SKIP = {"tools/centralize_literals.py", "tools/release_entry.py", "runtime/version.h",
        "runtime/lan_protocol.h", "runtime/network_hooks.inc"}
TOKEN = re.compile(r'''//[^\n]*|/\*[\s\S]*?\*/|(?:u8|[LuU])?"(?:\\[\s\S]|[^"\\])*"|(?:[LuU])?'(?:\\[\s\S]|[^'\\])*'|(?:UINT|INT)(?:8|16|32|64)_C\(\s*(?:0[xX][\da-fA-F]+|\d+)\s*\)|[A-Za-z_]\w*|(?:0[xX][\da-fA-F]+(?:\.[\da-fA-F]*)?(?:[pP][+-]?\d+)?|\d+(?:\.\d*)?(?:[eE][+-]?\d+)?|\.\d+(?:[eE][+-]?\d+)?)[uUlLfF]*''')


def sources():
    for directory in (ROOT / "runtime", ROOT / "tools", ROOT / "android/app/src/main/java"):
        for path in sorted(directory.rglob("*")):
            relative = path.relative_to(ROOT).as_posix()
            if (not path.is_file() or relative in SKIP or "constants" in path.parts
                    or any(part in path.parts for part in ("third_party", "bin", "obj", "__pycache__"))
                    or path.name.startswith("_sf2_") or path.name in ("GameConstants.java", "Sf2Constants.cs")):
                continue
            if path.suffix in (".c", ".h", ".cpp", ".inc", ".py", ".java", ".cs"):
                yield path


def key(prefix, token):
    # The readable value helps find a string/mask; the digest distinguishes
    # different escape spellings and long strings without unstable line numbers.
    words = re.sub(r"[^A-Za-z0-9]+", "_", token).strip("_").upper()[:28] or "EMPTY"
    return prefix + "_" + words + "_" + hashlib.sha256(token.encode()).hexdigest()[:4].upper()


def python_values(path, text):
    tree = ast.parse(text)
    parents = {child: node for node in ast.walk(tree) for child in ast.iter_child_nodes(node)}
    offsets = [0]
    for line in text.splitlines(keepends=True):
        offsets.append(offsets[-1] + len(line.encode("utf-8")))
    encoded = text.encode("utf-8")
    values = []
    for node in ast.walk(tree):
        if not isinstance(node, ast.Constant) or type(node.value) not in (str, bytes, int, float, complex):
            continue
        parent = parents.get(node)
        if isinstance(parent, ast.JoinedStr):
            continue
        if isinstance(parent, ast.Expr):
            owner = parents.get(parent)
            if isinstance(owner, (ast.Module, ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef)) and owner.body[0] is parent:
                continue  # Docstrings are part of the Python object, not constants.
        if isinstance(parent, (ast.MatchValue, ast.MatchSingleton)):
            continue  # Pattern syntax requires a literal.
        if type(node.value) is int and node.value in (0, 1):
            continue
        if type(node.value) in (float, complex) and node.value in (0, 1):
            continue
        start = offsets[node.lineno - 1] + node.col_offset
        end = offsets[node.end_lineno - 1] + node.end_col_offset
        token = encoded[start:end].decode("utf-8")
        kind = "S" if type(node.value) is str else "B" if type(node.value) is bytes else "N"
        values.append((start, end, key(kind, token), repr(node.value), node.lineno))
    return tree, values


def pasted_arguments(text):
    """Literal spellings used for token pasting/stringification are syntax."""
    macros = {}
    for source in (ROOT / "runtime").rglob("*"):
        if source.suffix not in (".h", ".c", ".inc", ".cpp") or "constants" in source.parts:
            continue
        normalized = source.read_text(encoding="utf-8").replace("\\\n", " ")
        for name, args, body in re.findall(r"^\s*#define\s+(\w+)\(([^)]*)\)([^\n]*)", normalized, re.MULTILINE):
            indices = {i for i, arg in enumerate(args.split(",")) if re.search(r"#\s*"+re.escape(arg.strip())+r"\b|\b"+re.escape(arg.strip())+r"\s*##", body)}
            if indices: macros[name]=indices
    intervals = []
    for name, indices in macros.items():
        for invocation in re.finditer(r"\b"+re.escape(name)+r"\s*\(", text):
            start=base=invocation.end(); depth=0; index=0
            for token in re.finditer(r'"(?:\\.|[^"\\])*"|\(|\)|,',text[base:]):
                value=token[0];position=base+token.start()
                if value=='(': depth+=1
                elif value==')':
                    if depth: depth-=1
                    else:
                        if index in indices: intervals.append((start,position))
                        break
                elif value==',' and not depth:
                    if index in indices: intervals.append((start,position))
                    index+=1;start=position+1
    return intervals


def c_values(path, text):
    values = []
    pasted = pasted_arguments(text)
    for match in TOKEN.finditer(text):
        token = match[0]
        if token.startswith(("//", "/*")):
            continue
        start, end = match.span()
        if any(lo <= start < hi for lo,hi in pasted):
            continue
        line_start = text.rfind("\n", 0, start) + 1
        line = text[line_start:start]
        if re.match(r"\s*#\s*(?:include|include_next|error|warning|line|pragma)\b", line):
            continue  # Preprocessor syntax and diagnostics need literal tokens.
        if re.match(r"(?:u8|[LuU])?[\"']", token):
            kind = "S" if '"' in token[:3] else "CH"
        elif token[0].isdigit() or token.startswith(".") or re.match(r"(?:UINT|INT)\d+_C\(", token):
            if token in ('0', '1', '0.0', '1.0'):
                continue
            kind = "N"
        else:
            continue
        # Join physical continuation lines, preserving C preprocessing semantics.
        literal = token.replace("\\\n", "")
        values.append((start, end, key("SF2C_" + path.stem.upper() + "_" + kind, literal), literal, text.count("\n", 0, start) + 1))
    return values


def c_group(path):
    parent = path.parent.relative_to(ROOT / "runtime").parts
    return "_".join(parent) if parent else "core"


def expanded(path):
    """Readable projection for source-analysis tools and migration verification."""
    path = Path(path).resolve()
    text = path.read_text(encoding="utf-8")
    if path.suffix == ".py":
        match = re.search(r"^from (_sf2_\w+_constants) import (\w+) as (\w+)\n", text, re.MULTILINE)
        if not match:
            return text
        catalog = path.parent / (match[1] + ".py")
        if not catalog.exists():
            catalog = ROOT / "tools" / (match[1] + ".py")
        section = next(n for n in ast.parse(catalog.read_text()).body if isinstance(n, ast.ClassDef) and n.name == match[2])
        values = {n.targets[0].id: ast.literal_eval(n.value) for group in ast.parse(catalog.read_text()).body
                  if isinstance(group, ast.ClassDef) for n in group.body if isinstance(n, ast.Assign)}
        text = text.replace(match[0], "")
        # Use AST spans so aliases inside literal strings are never rewritten.
        tree = ast.parse(text)
        lines = text.encode().splitlines(keepends=True)
        offsets = [0]
        for line in lines: offsets.append(offsets[-1]+len(line))
        replacements = []
        for node in ast.walk(tree):
            if isinstance(node, ast.Attribute) and isinstance(node.value, ast.Name) and node.value.id == match[3] and node.attr in values:
                replacements.append((offsets[node.lineno-1]+node.col_offset, offsets[node.end_lineno-1]+node.end_col_offset, repr(values[node.attr])))
        encoded = text.encode()
        for start,end,value in sorted(replacements,reverse=True): encoded=encoded[:start]+value.encode()+encoded[end:]
        return encoded.decode()
    includes = re.findall(r'^#include "([^"\n]*constants/[^"\n]+)"\n', text, re.MULTILINE)
    values = {}
    for name in includes:
        catalog = (path.parent/name).resolve()
        values.update(re.findall(r'^#define (SF2C_\w+) (.*)$', catalog.read_text(), re.MULTILINE))
        text = text.replace(f'#include "{name}"\n', "")
    return re.sub(r"\bSF2C_[A-Z0-9_]+\b",lambda m:values.get(m[0],m[0]),text)


def migrate_python(path, write):
    text = path.read_text(encoding="utf-8")
    original, values = python_values(path, text)
    if not values:
        return 0
    recompiler = "recompr" in path.parts
    module = "_sf2_recompiler_constants" if recompiler else "_sf2_tool_constants"
    catalog = path.parent / (module + ".py") if recompiler else ROOT / "tools" / (module + ".py")
    section = path.stem.upper()
    if not write:
        return len(values)
    encoded = text.encode("utf-8")
    for start, end, name, literal, line in sorted(values, reverse=True):
        encoded = encoded[:start] + (ALIAS + "." + name).encode() + encoded[end:]
    result = encoded.decode("utf-8")
    # Import after the module docstring and future imports.
    anchor = 0
    for node in original.body:
        if isinstance(node, ast.Expr) and isinstance(node.value, ast.Constant) and isinstance(node.value.value, str) and anchor == 0:
            anchor = node.end_lineno
        elif isinstance(node, ast.ImportFrom) and node.module == "__future__":
            anchor = node.end_lineno
        else:
            break
    lines = result.splitlines(keepends=True)
    if anchor == 0 and lines and lines[0].startswith("#!"):
        anchor = 1
    declaration = f"from {module} import {section} as {ALIAS}\n"
    if declaration not in result:
        lines.insert(anchor, declaration)
    result = "".join(lines)
    # Expand the aliases back into constants and compare complete Python trees.
    replacement = {}
    if catalog.exists():
        for group in ast.parse(catalog.read_text()).body:
            if isinstance(group, ast.ClassDef) and group.name == section:
                replacement.update({n.targets[0].id: ast.literal_eval(n.value)
                                    for n in group.body if isinstance(n, ast.Assign)})
    for _, _, name, literal, _ in values:
        value = ast.literal_eval(literal)
        if name in replacement and replacement[name] != value:
            raise ValueError(f'Literal key collision in {path}: {name}; choose a semantic name')
        replacement[name] = value
    class Restore(ast.NodeTransformer):
        def visit_ImportFrom(self, node):
            return None if node.module == module else node
        def visit_Attribute(self, node):
            if isinstance(node.value, ast.Name) and node.value.id == ALIAS and node.attr in replacement:
                return ast.copy_location(ast.Constant(replacement[node.attr]), node)
            return self.generic_visit(node)
    expected = ast.dump(Restore().visit(original), include_attributes=False)
    actual = ast.dump(Restore().visit(ast.parse(result)), include_attributes=False)
    if actual != expected:
        raise ValueError(f"Literal migration changed the Python AST: {path}")
    existing = catalog.read_text() if catalog.exists() else '"""Fixed values, grouped by source subsystem. Edit values here."""\n'
    class_marker = f"class {section}:\n"
    if class_marker not in existing:
        existing += "\n\n" + class_marker
    for _, _, name, literal, line in sorted(set(values)):
        section_body = existing[existing.index(class_marker):].split("\nclass ", 1)[0]
        if re.search(rf"^    {re.escape(name)} =", section_body, re.MULTILINE):
            continue
        # Insert into the correct class even after other sections exist.
        at = existing.index(class_marker) + len(class_marker)
        existing = existing[:at] + f"    {name} = {literal}  # {path.relative_to(ROOT)}:{line}\n" + existing[at:]
    catalog.write_text(existing, encoding="utf-8")
    path.write_text(result, encoding="utf-8")
    return len(values)


def migrate_c(path, write):
    # Tools' native harnesses keep their own fixtures. The production C is runtime/.
    if not path.is_relative_to(ROOT / "runtime"):
        return 0
    text = path.read_text(encoding="utf-8")
    values = c_values(path, text)
    if not values or not write:
        return len(values)
    catalog = ROOT / "runtime/constants" / (c_group(path) + ".h")
    existing = catalog.read_text() if catalog.exists() else "// Fixed values for this subsystem. Edit definitions here.\n#pragma once\n"
    for _, _, name, literal, line in values:
        if re.search(rf"^#define {re.escape(name)} ", existing, re.MULTILINE):
            continue
        existing += f"// {path.relative_to(ROOT)}:{line}\n#define {name} {literal}\n"
    result = text
    for start, end, name, _, _ in sorted(values, reverse=True):
        result = result[:start] + name + result[end:]
    # Re-expansion proves the exact original C token text is retained.
    lookup = dict(re.findall(r'^#define (SF2C_\w+) (.*)$', existing, re.MULTILINE))
    def restore(source):
        return re.sub(r"\bSF2C_\w+\b", lambda m: lookup.get(m[0], m[0]), source)
    if restore(result) != restore(text):
        raise ValueError(f"Literal migration changed C text: {path}")
    include = '#include "' + os.path.relpath(catalog, path.parent).replace(os.sep, "/") + '"\n'
    if include not in result:
        result = include + result
    catalog.parent.mkdir(parents=True, exist_ok=True)
    catalog.write_text(existing, encoding="utf-8")
    path.write_text(result, encoding="utf-8")
    return len(values)


def migrate_managed(path, write):
    text = path.read_text(encoding="utf-8-sig")
    values = []
    for match in TOKEN.finditer(text):
        token = match[0]
        if token.startswith(('//','/*')) or not (token[0].isdigit() or token[0] in "\"'"):
            continue
        if token in ('0', '1'):
            continue
        kind = 'S' if token[0]=='"' else 'CH' if token[0]=="'" else 'N'
        if kind=='S': type_name='String' if path.suffix=='.java' else 'string'
        elif kind=='CH': type_name='char'
        elif token.lower().endswith('f'): type_name='float'
        elif token.lower().endswith('d') or '.' in token or ('e' in token.lower() and not token.startswith('0x')): type_name='double'
        elif token.lower().endswith('ul'): type_name='ulong'
        elif token.lower().endswith('l'): type_name='long'
        elif token.lower().endswith('u'): type_name='uint'
        else: type_name='int'
        name=key(path.stem.upper()+'_'+kind,token)
        values.append((*match.span(),name,token,type_name))
    if not values or not write: return len(values)
    classname='GameConstants' if path.suffix=='.java' else 'Sf2Constants'
    catalog=path.parent/(classname+path.suffix)
    existing=catalog.read_text() if catalog.exists() else ('package org.sf2recomp.app;\n\nfinal class GameConstants {\n}\n' if path.suffix=='.java' else 'static class Sf2Constants {\n}\n')
    result=text
    for start,end,name,token,type_name in sorted(values,reverse=True):
        result=result[:start]+classname+'.'+name+result[end:]
        if re.search(r'\b'+name+r'\s*=',existing): continue
        declaration=('static final ' if path.suffix=='.java' else 'public const ')+type_name+' '+name+' = '+token+';'
        at=existing.rfind('}')
        existing=existing[:at]+'    '+declaration+'\n'+existing[at:]
    # The rewrite leaves operators, overload selection and literal types intact.
    lookup=dict(re.findall(r'^\s*(?:static final|public const)\s+\w+\s+(\w+) = (.*);$',existing,re.MULTILINE))
    def restore(source):
        return re.sub(r'\b'+classname+r'\.(\w+)',lambda m:lookup.get(m[1],m[0]),source)
    if restore(result)!=restore(text): raise ValueError(f'Managed literal migration changed tokens: {path}')
    catalog.write_text(existing,encoding='utf-8')
    path.write_text(result,encoding='utf-8')
    return len(values)


def name_declarations():
    """Keep semantic names such as OFF_SP and BUILD_ID in the value catalogs."""
    for path in sources():
        text=path.read_text(encoding='utf-8')
        if path.suffix=='.py':
            match=re.search(r'^from (_sf2_\w+_constants) import (\w+) as (\w+)\n',text,re.MULTILINE)
            if not match: continue
            catalog=path.parent/(match[1]+'.py')
            if not catalog.exists(): catalog=ROOT/'tools'/(match[1]+'.py')
            body=catalog.read_text();tree=ast.parse(text)
            group=next(n for n in ast.parse(body).body if isinstance(n,ast.ClassDef) and n.name==match[2])
            values={n.targets[0].id:ast.literal_eval(n.value) for n in group.body if isinstance(n,ast.Assign)}
            assignments=[n for n in ast.walk(tree) if isinstance(n,ast.Assign) and len(n.targets)==1 and isinstance(n.targets[0],ast.Name)]
            counts={n.targets[0].id:sum(m.targets[0].id==n.targets[0].id for m in assignments) for n in assignments}
            lines=text.encode().splitlines(keepends=True);offsets=[0]
            for line in lines:offsets.append(offsets[-1]+len(line))
            edits=[]
            for node in assignments:
                name=node.targets[0].id;value=node.value
                if (not name.isupper() or counts[name]!=1 or not isinstance(value,ast.Attribute)
                        or not isinstance(value.value,ast.Name) or value.value.id!=match[3] or value.attr==name):continue
                literal=values[value.attr]
                if name in values and values[name]!=literal:raise ValueError(f'Conflicting semantic constant {name}')
                if name not in values:
                    anchor=body.index('class '+match[2]+':\n')+len('class '+match[2]+':\n')
                    body=body[:anchor]+f'    {name} = {literal!r}  # {path.relative_to(ROOT)}\n'+body[anchor:]
                    values[name]=literal
                edits.append((offsets[value.lineno-1]+value.col_offset,offsets[value.end_lineno-1]+value.end_col_offset,match[3]+'.'+name))
            encoded=text.encode()
            for start,end,value in sorted(edits,reverse=True):encoded=encoded[:start]+value.encode()+encoded[end:]
            if edits:path.write_text(encoded.decode());catalog.write_text(body)
        elif path.is_relative_to(ROOT/'runtime') and path.suffix in ('.c','.h','.cpp','.inc'):
            catalog=ROOT/'runtime/constants'/(c_group(path)+'.h')
            if not catalog.exists():continue
            body=catalog.read_text()
            definitions=re.findall(r'^#define (\w+) (SF2C_\w+)\s*$',text,re.MULTILINE)
            counts={name:sum(n==name for n,_ in definitions) for name,_ in definitions}
            original_text,original_body=text,body
            for name,old in definitions:
                if counts[name]!=1:continue  # Conditional platform variants must stay distinct.
                new='SF2C_'+path.stem.upper()+'_'+name
                if old==new:continue
                value=re.search(r'^#define '+re.escape(old)+r' (.*)$',body,re.MULTILINE)
                if not value:continue
                if not re.search(r'^#define '+re.escape(new)+r' ',body,re.MULTILINE):body+='\n#define '+new+' '+value[1]+'\n'
                text=re.sub(r'^(#define '+re.escape(name)+r' )'+re.escape(old)+r'\s*$',r'\g<1>'+new,text,flags=re.MULTILINE)
            def restore(source,definitions):
                values=dict(re.findall(r'^#define (SF2C_\w+) (.*)$',definitions,re.MULTILINE))
                return re.sub(r'\bSF2C_\w+\b',lambda m:values.get(m[0],m[0]),source)
            if restore(text,body)!=restore(original_text,original_body):
                raise ValueError(f'Semantic naming changed a C definition: {path}')
            path.write_text(text);catalog.write_text(body)


def prune():
    """Remove obsolete definitions after a repeat migration or a source edit."""
    references={}
    c_names=set()
    for path in sources():
        text=path.read_text(encoding='utf-8')
        c_names.update(re.findall(r'\bSF2C_\w+\b',text))
        match=re.search(r'^from (_sf2_\w+_constants) import (\w+) as (\w+)\n',text,re.MULTILINE)
        if match:
            references.setdefault((match[1],match[2]),set()).update(re.findall(r'\b'+re.escape(match[3])+r'\.(\w+)',text))
    for catalog in (ROOT/'tools/_sf2_tool_constants.py',ROOT/'tools/recompr/_sf2_recompiler_constants.py'):
        if not catalog.exists():continue
        text=catalog.read_text();tree=ast.parse(text);lines=text.splitlines(keepends=True);remove=set()
        for group in tree.body:
            if not isinstance(group,ast.ClassDef):continue
            keep=references.get((catalog.stem,group.name),set())
            if not keep:remove.update(range(group.lineno-1,group.end_lineno));continue
            for node in group.body:
                if isinstance(node,ast.Assign) and node.targets[0].id not in keep:remove.update(range(node.lineno-1,node.end_lineno))
        catalog.write_text(''.join(line for i,line in enumerate(lines) if i not in remove))
    for catalog in (ROOT/'runtime/constants').glob('*.h'):
        lines=catalog.read_text().splitlines(keepends=True);result=[]
        for line in lines:
            match=re.match(r'#define (SF2C_\w+) ',line)
            if match and match[1] not in c_names:
                if result and result[-1].startswith('// runtime/'):result.pop()
                continue
            result.append(line)
        catalog.write_text(''.join(result))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--write", action="store_true")
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--paths", nargs="*", type=Path)
    parser.add_argument("--report", type=Path)
    parser.add_argument("--expand", action="store_true", help="restore catalog references to literals")
    parser.add_argument('--name-declarations',action='store_true')
    parser.add_argument('--prune',action='store_true')
    args = parser.parse_args()
    if args.name_declarations:name_declarations()
    if args.prune:prune()
    counts = {}
    for path in args.paths or list(sources()):
        path = path.resolve()
        if args.expand:
            text = expanded(path)
            if args.write: path.write_text(text, encoding="utf-8")
            else: print(text)
            continue
        count = (migrate_python(path, args.write) if path.suffix == ".py" else
                 migrate_managed(path,args.write) if path.suffix in ('.java','.cs') else migrate_c(path, args.write))
        if count:
            counts[path.relative_to(ROOT).as_posix()] = count
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(counts, indent=2) + "\n")
    print(f"{'Cataloged' if args.write else 'Uncataloged'} {sum(counts.values())} literals in {len(counts)} files")
    return 1 if args.check and counts else 0


if __name__ == "__main__":
    raise SystemExit(main())

"""Check that a freshly built MO2 plugin DLL can bind to the DLLs shipped with MO2.

usage: python verify_imports.py <built plugin.dll> <extracted MO2 folder> [report.txt]

For every DLL the plugin imports from the MO2 distribution (uibase.dll, Qt6*.dll, and
any C++ runtime DLL MO2 ships its own copy of) each imported symbol must be exported by
MO2's copy of that DLL. It also compares the
Qt version recorded in the plugin metadata with MO2's own game_starfield.dll, since
QPluginLoader rejects plugins built against a newer Qt minor version.
"""

import pathlib
import sys

import pefile

SYSTEM_DLLS = {
    "kernel32.dll", "user32.dll", "advapi32.dll", "shell32.dll", "ole32.dll",
    "oleaut32.dll", "version.dll", "dbghelp.dll", "shlwapi.dll", "ucrtbase.dll",
    "msvcp140.dll", "msvcp140_1.dll", "msvcp140_2.dll", "msvcp140_atomic_wait.dll",
    "vcruntime140.dll", "vcruntime140_1.dll",
}


def load(path, directory):
    pe = pefile.PE(str(path), fast_load=True)
    pe.parse_data_directories(
        directories=[pefile.DIRECTORY_ENTRY[directory]])
    return pe


def exports(path):
    pe = load(path, "IMAGE_DIRECTORY_ENTRY_EXPORT")
    if not hasattr(pe, "DIRECTORY_ENTRY_EXPORT"):
        return set()
    return {s.name.decode() for s in pe.DIRECTORY_ENTRY_EXPORT.symbols if s.name}


def imports(path):
    pe = load(path, "IMAGE_DIRECTORY_ENTRY_IMPORT")
    result = {}
    for entry in getattr(pe, "DIRECTORY_ENTRY_IMPORT", []):
        result[entry.dll.decode()] = [
            imp.name.decode() if imp.name else f"#ordinal {imp.ordinal}"
            for imp in entry.imports
        ]
    return result


def qt_metadata(path):
    data = pathlib.Path(path).read_bytes()
    i = data.find(b"QTMETADATA !")
    if i < 0:
        return None
    version, major, minor, flags = data[i + 12:i + 16]
    return f"metadata v{version}, Qt {major}.{minor}, arch flags {flags:#x}"


def main():
    built = pathlib.Path(sys.argv[1])
    mo2 = pathlib.Path(sys.argv[2])
    report_path = pathlib.Path(sys.argv[3]) if len(sys.argv) > 3 else None

    lines, problems = [], []
    out = lines.append

    # index the DLLs MO2 itself loads from: its root, dlls\ and plugins\ (not
    # instance/mod folders, which can hold thousands of unrelated DLLs)
    candidates = list(mo2.glob("*.dll")) + list(mo2.glob("dlls/**/*.dll")) + list(
        mo2.glob("plugins/*.dll"))
    shipped = {}
    for dll in sorted(candidates, key=lambda p: len(p.parts)):
        shipped.setdefault(dll.name.lower(), dll)

    official = mo2 / "plugins" / "game_starfield.dll"
    if not official.exists() or official.resolve() == built.resolve():
        official = None

    out(f"built plugin : {built}")
    out(f"MO2 folder   : {mo2} ({len(shipped)} DLLs indexed)")
    out(f"official dll : {official}")
    out("")

    built_meta = qt_metadata(built)
    out(f"Qt metadata (built)   : {built_meta}")
    if official:
        official_meta = qt_metadata(official)
        out(f"Qt metadata (official): {official_meta}")
        if built_meta != official_meta:
            problems.append("Qt plugin metadata differs from MO2's own plugin")
    out("")

    for dll, names in sorted(imports(built).items(), key=lambda kv: kv[0].lower()):
        key = dll.lower()
        if key.startswith(("api-ms-win-", "ext-ms-")) or (
                key in SYSTEM_DLLS and key not in shipped):
            out(f"[system ] {dll}: {len(names)} symbols")
            continue
        if key not in shipped:
            problems.append(f"{dll} is imported but not shipped with MO2")
            out(f"[MISSING] {dll}: not found in the MO2 folder")
            continue
        available = exports(shipped[key])
        missing = [n for n in names if not n.startswith("#") and n not in available]
        status = "OK     " if not missing else "BROKEN "
        out(f"[{status}] {dll}: {len(names)} symbols, {len(missing)} unresolved "
            f"(checked against {shipped[key].relative_to(mo2)})")
        for name in missing:
            out(f"           unresolved: {name}")
        if missing:
            problems.append(f"{dll}: {len(missing)} unresolved symbols")

    if official:
        out("")
        ours = {d.lower() for d in imports(built)}
        theirs = {d.lower() for d in imports(official)}
        out(f"imported only by built plugin   : {sorted(ours - theirs) or '-'}")
        out(f"imported only by official plugin: {sorted(theirs - ours) or '-'}")

    out("")
    out("RESULT: " + ("OK" if not problems else "FAILED - " + "; ".join(problems)))

    text = "\n".join(lines)
    print(text)
    if report_path:
        report_path.write_text(text + "\n", encoding="utf-8")
    sys.exit(1 if problems else 0)


if __name__ == "__main__":
    main()

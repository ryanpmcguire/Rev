#!/usr/bin/env python3
import os, re, sys, pathlib, hashlib

# -------------------------------------------------------------------
#   CONFIGURATION
# -------------------------------------------------------------------

PROJECT_ROOT = pathlib.Path(__file__).resolve().parent.parent
SRC_EXTS = {".cpp", ".h", ".hpp", ".ixx", ".mxx", ".cppm"}

# Where the final module goes:
OUTPUT_DIR = PROJECT_ROOT / "Resources"
OUTPUT_DIR.mkdir(exist_ok=True)

ATLAS_FILE = OUTPUT_DIR / "Files.ixx"

# Regex for matching Resource("path") or File("path")
RESOURCE_REGEX = re.compile(
    r'\b(?:Resource|File)\s*\(\s*"([^"]+)"\s*\)', re.MULTILINE
)

# -------------------------------------------------------------------
#   UTILITIES
# -------------------------------------------------------------------

def to_virtual_path(src_file: pathlib.Path, ref: str) -> str:
    """
    Resolve a referenced path (e.g. "./Arial.ttf") relative to the source file.
    Produce a normalized project-relative virtual path.
    """
    abs_src_dir = src_file.parent
    abs_target = (abs_src_dir / ref).resolve()

    try:
        rel = abs_target.relative_to(PROJECT_ROOT)
    except ValueError:
        print(f"[WARN] Referenced file is outside project: {abs_target}")
        return None

    return rel.as_posix()


def load_bytes(path: pathlib.Path) -> bytes:
    with open(path, "rb") as f:
        return f.read()


def mangle_symbol(path: str) -> str:
    """
    Convert "Graphics/Text/Arial.ttf" → Graphics_Text_Arial_ttf_data
    """
    return re.sub(r'[^a-zA-Z0-9]', '_', path) + "_data"


# -------------------------------------------------------------------
#   MAIN SCAN
# -------------------------------------------------------------------

print("Scanning project for Resource(...) and File(...) references...")

found_files = {}  # virtualPath -> absolutePath

for root, dirs, files in os.walk(PROJECT_ROOT):
    for fname in files:
        p = pathlib.Path(root) / fname
        if p.suffix not in SRC_EXTS:
            continue

        text = p.read_text(errors="ignore")

        # Look for Resource("path") or File("path")
        for match in RESOURCE_REGEX.finditer(text):
            rel_path = match.group(1).strip()
            virt_path = to_virtual_path(p, rel_path)
            if not virt_path:
                continue

            abs_target = (PROJECT_ROOT / virt_path).resolve()

            if abs_target.exists():
                print(f"  [+] {virt_path}")
                found_files[virt_path] = abs_target
            else:
                print(f"  [!] Missing file: {virt_path} (referenced from {p})")

print("")
print(f"Total resource files found: {len(found_files)}")
print("Generating atlas module...")

# -------------------------------------------------------------------
#   GENERATE Files.ixx
# -------------------------------------------------------------------

with open(ATLAS_FILE, "w") as out:

    out.write("export module Files;\n\n")
    out.write("export namespace FilesDB {\n\n")

    out.write("    export struct FileEntry {\n")
    out.write("        std::string_view virtualPath;\n")
    out.write("        const unsigned char* data;\n")
    out.write("        size_t size;\n")
    out.write("    };\n\n")

    # Write byte arrays
    for virt, absfile in found_files.items():
        data = load_bytes(absfile)
        sym = mangle_symbol(virt)

        out.write(f"    inline constexpr unsigned char {sym}[] = {{\n        ")
        hexbytes = ", ".join(f"0x{b:02X}" for b in data)
        out.write(hexbytes)
        out.write("\n    };\n\n")

    # Write atlas table
    out.write("    export constinit FileEntry Atlas[] = {\n")
    for virt, absfile in found_files.items():
        sym = mangle_symbol(virt)
        out.write(f'        {{ "{virt}", {sym}, sizeof({sym}) }},\n')
    out.write("    };\n\n")

    out.write(f"    export constinit size_t Count = {len(found_files)};\n")

    out.write("}\n")

print(f"Done. Wrote atlas to: {ATLAS_FILE}")

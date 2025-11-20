#!/usr/bin/env python3
import argparse, os, re, sys, pathlib

# -------------------------------------------------------------------
#   CONFIGURATION
# -------------------------------------------------------------------

parser = argparse.ArgumentParser()
parser.add_argument("--project-root", required=True)
args = parser.parse_args()

PROJECT_ROOT = pathlib.Path(args.project_root).resolve()

SRC_EXTS = {".cpp", ".h", ".hpp", ".ixx", ".mxx", ".cppm"}

# Output target: Rev/Resources/.modules/Files.ixx
OUTPUT_DIR = PROJECT_ROOT / "Rev/Resources/.modules"
OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

ATLAS_FILE = OUTPUT_DIR / "Files.ixx"

# Match: File("path")
RESOURCE_REGEX = re.compile(
    r'\bFile\s*\(\s*"([^"]+)"\s*\)',
    re.MULTILINE
)

# -------------------------------------------------------------------
#   UTILITIES
# -------------------------------------------------------------------

def resolve_resource_path(src_file: pathlib.Path, ref: str) -> pathlib.Path:
    """
    Implements the SAME rule as C++:
      - If ref starts with "./"  -> anchor-relative
      - Else                     -> project-root absolute

    Returns a fully resolved absolute path on disk.
    """

    ref = ref.replace("\\", "/")  # normalize

    # CASE 1: Anchor-relative ("./")
    if ref.startswith("./"):
        stripped = ref[2:]  # remove "./"
        src_dir = src_file.parent
        return (src_dir / stripped).resolve()

    # CASE 2: Project-root absolute
    return (PROJECT_ROOT / ref).resolve()


def load_bytes(path: pathlib.Path) -> bytes:
    with open(path, "rb") as f:
        return f.read()


def mangle_symbol(path: str) -> str:
    """
    Convert a virtual path into a legal C++ symbol.
       Example: "src/Scene/UI/Test.png" ->
       src_Scene_UI_Test_png_data
    """
    return re.sub(r'[^a-zA-Z0-9]', '_', path) + "_data"


# -------------------------------------------------------------------
#   MAIN SCAN
# -------------------------------------------------------------------

print(f"Scanning project for File(\"...\") references inside: {PROJECT_ROOT}\n")

found_files = {}  # virtualPath -> absolutePath

for root, dirs, files in os.walk(PROJECT_ROOT):
    for fname in files:
        p = pathlib.Path(root) / fname
        if p.suffix not in SRC_EXTS:
            continue

        try:
            text = p.read_text(errors="ignore")
        except Exception:
            continue

        for match in RESOURCE_REGEX.finditer(text):
            ref = match.group(1).strip()

            # 1. Resolve absolute file path on disk
            abs_target = resolve_resource_path(p, ref)

            # 2. Compute project-relative "virtual path"
            try:
                virt = abs_target.relative_to(PROJECT_ROOT).as_posix()
            except ValueError:
                print(f"  [WARN] File outside project root: {abs_target}")
                continue

            # 3. Store if exists
            if abs_target.exists():
                if virt not in found_files:
                    print(f"  [+] {virt}")
                    found_files[virt] = abs_target
            else:
                print(f"  [!] Missing file: {virt}  (from {p})")

print("\n------------------------------------------------------------")
print(f"Total resource files found: {len(found_files)}")
print("Generating Files.ixx...\n")

# -------------------------------------------------------------------
#   GENERATE Files.ixx ATLAS
# -------------------------------------------------------------------

with open(ATLAS_FILE, "w", encoding="utf-8") as out:

    out.write("module;\n")
    out.write("#include <string>\n\n")
    out.write("export module Rev.Managed.Files;\n\n")
    out.write("export namespace FilesDB {\n\n")

    out.write("    struct FileEntry {\n")
    out.write("        std::string_view virtualPath;\n")
    out.write("        const unsigned char* data;\n")
    out.write("        size_t size;\n")
    out.write("    };\n\n")

    # Emit byte arrays
    for virt, absfile in found_files.items():
        data = load_bytes(absfile)
        sym = mangle_symbol(virt)

        out.write(f"    inline constexpr unsigned char {sym}[] = {{\n        ")
        out.write(", ".join(f"0x{b:02X}" for b in data))
        out.write("\n    };\n\n")

    # Emit atlas table
    out.write("    constinit FileEntry Atlas[] = {\n")
    for virt, absfile in found_files.items():
        sym = mangle_symbol(virt)
        out.write(f'        {{ "{virt}", {sym}, sizeof({sym}) }},\n')
    out.write("    };\n\n")

    out.write(f"    constinit size_t Count = {len(found_files)};\n")
    out.write("};\n")

print(f"Done. Atlas written to: {ATLAS_FILE}\n")

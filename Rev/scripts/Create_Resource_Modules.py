#!/usr/bin/env python3
import argparse, os, re, sys, pathlib, json, hashlib, time

# -------------------------------------------------------------------
#   CONFIGURATION
# -------------------------------------------------------------------

parser = argparse.ArgumentParser()
parser.add_argument("--project-root", required=True)
args = parser.parse_args()

PROJECT_ROOT = pathlib.Path(args.project_root).resolve()

SRC_EXTS = {".cpp", ".h", ".hpp", ".ixx", ".mxx", ".cppm"}

OUTPUT_DIR = PROJECT_ROOT / "Rev/resources/.modules"
OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

ATLAS_FILE = OUTPUT_DIR / "Files.ixx"
CACHE_FILE = OUTPUT_DIR / "Files.cache.json"

RESOURCE_REGEX = re.compile(
    r'(?:[\w:]+::)*File\s*\(\s*"([^"]+)"\s*\)',
    re.MULTILINE
)

# -------------------------------------------------------------------
#   HELPERS
# -------------------------------------------------------------------

def hash_bytes(file: pathlib.Path) -> str:
    h = hashlib.sha256()
    with open(file, "rb") as f:
        while chunk := f.read(65536):
            h.update(chunk)
    return h.hexdigest()

def load_cache():
    if CACHE_FILE.exists():
        try:
            return json.loads(CACHE_FILE.read_text())
        except Exception:
            return {}
    return {}

def save_cache(cache):
    CACHE_FILE.write_text(json.dumps(cache, indent=2))

def resolve_resource(src_file: pathlib.Path, ref: str) -> pathlib.Path:
    ref = ref.replace("\\", "/")

    # anchor-relative
    if ref.startswith("./"):
        stripped = ref[2:]
        return (src_file.parent / stripped).resolve()

    # project-root absolute
    return (PROJECT_ROOT / ref).resolve()

def virtualize(abs_path: pathlib.Path) -> str:
    try:
        return abs_path.resolve().relative_to(PROJECT_ROOT).as_posix()
    except ValueError:
        return None

# -------------------------------------------------------------------
#   SCAN PROJECT
# -------------------------------------------------------------------

print(f"Scanning project for File(\"...\") in: {PROJECT_ROOT}\n")

new_map = {}  # virtual -> {abs, hash, mtime}

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

            abs_target = resolve_resource(p, ref)
            virt = virtualize(abs_target)

            if virt is None:
                print(f"[WARN] File {abs_target} outside project root")
                continue

            if not abs_target.exists():
                print(f"[WARN] Missing file: {virt} from {p}")
                continue

            h = hash_bytes(abs_target)
            m = abs_target.stat().st_mtime

            if virt not in new_map:
                print(f"  [+] {virt}")
                new_map[virt] = {
                    "absolute": abs_target.as_posix(),
                    "hash": h,
                    "mtime": m
                }

print("\n------------------------------------------------------------")
print(f"Total resources found: {len(new_map)}")

# -------------------------------------------------------------------
#   LOAD OLD CACHE AND COMPARE
# -------------------------------------------------------------------

old_cache = load_cache()
old_files = old_cache.get("files", {})

if old_files == new_map:
    print("No changes detected — skipping regeneration.")
    sys.exit(0)

print("Changes detected — regenerating Files.ixx...")

# -------------------------------------------------------------------
#   GENERATE Files.ixx
# -------------------------------------------------------------------

def mangle_symbol(path: str) -> str:
    return re.sub(r'[^a-zA-Z0-9]', '_', path) + "_data"

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
    for virt, info in new_map.items():
        abs_path = pathlib.Path(info["absolute"])
        data = abs_path.read_bytes()
        sym = mangle_symbol(virt)

        out.write(f"    inline constexpr unsigned char {sym}[] = {{\n        ")
        out.write(", ".join(f"0x{b:02X}" for b in data))
        out.write("\n    };\n\n")

    # Emit atlas
    out.write("    constinit FileEntry Atlas[] = {\n")
    for virt, info in new_map.items():
        sym = mangle_symbol(virt)
        out.write(f'        {{ "{virt}", {sym}, sizeof({sym}) }},\n')
    out.write("    };\n\n")

    out.write(f"    constinit size_t Count = {len(new_map)};\n")
    out.write("};\n")

print(f"Atlas written to: {ATLAS_FILE}")

# -------------------------------------------------------------------
#   SAVE CACHE
# -------------------------------------------------------------------

save_cache({"files": new_map, "project_root": PROJECT_ROOT.as_posix()})

print("Cache updated.")

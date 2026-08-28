#!/bin/bash

export PATH="/usr/bin:$PATH"

# Script root
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Unified input/output root
RESOURCE_DIR="$SCRIPT_DIR/../resources"
INPUT_DIR="$RESOURCE_DIR"
OUTPUT_DIR="$RESOURCE_DIR/.modules"

# Create output directory if needed
mkdir -p "$OUTPUT_DIR"

# Clean up stale modules
echo ""
echo "CLEANING STALE MODULES"
echo "--------------------------------------------------"
echo ""

find "$OUTPUT_DIR" -type f -name '*.ixx' | while read -r OUT_FILE; do
    REL_OUT="${OUT_FILE#$OUTPUT_DIR/}"
    OUT_DIRNAME=$(dirname "$REL_OUT")
    OUT_BASENAME=$(basename "$REL_OUT" .ixx)

    FILE_BASE="${OUT_BASENAME%_*}"
    FILE_EXT="${OUT_BASENAME##*_}"
    EXPECTED_FILE="${INPUT_DIR}/${OUT_DIRNAME}/${FILE_BASE}.${FILE_EXT}"

    if [[ ! -f "$EXPECTED_FILE" ]]; then
        echo "🗑️ Removing stale module: $REL_OUT"
        rm "$OUT_FILE"
    fi
done

echo ""
echo "CREATING RESOURCE MODULES"
echo "--------------------------------------------------"
echo ""

RESOURCE_MODULE_NAME="Rev.Core.Resource"

# Find and embed all files in Resources/
find "$INPUT_DIR" -type f ! -path "$OUTPUT_DIR/*" | while read -r FILE; do
    REL_PATH="${FILE#$INPUT_DIR/}"
    FILE_DIR="${REL_PATH%/*}"
    FILE_NAME="${REL_PATH##*/}"
    FILE_BASE="${FILE_NAME%.*}"
    FILE_EXT="${FILE_NAME##*.}"
    BASE_NAME="${FILE_BASE}_${FILE_EXT}"
    OUT_FILE="$OUTPUT_DIR/$FILE_DIR/$BASE_NAME.ixx"

    # 🟢 Skip unchanged files quickly
    if [[ -f "$OUT_FILE" && "$FILE" -ot "$OUT_FILE" ]]; then
        #echo "✅ Skipping unchanged: $REL_PATH"
        continue
    fi

    echo "🔧 Embedding: $REL_PATH"

    SYMBOL_NAME="${BASE_NAME//[^a-zA-Z0-9_]/_}"
    MODULE_NAME=$(echo "Resources/$FILE_DIR/$BASE_NAME" | sed 's/[^a-zA-Z0-9/]/_/g' | tr '/' '.')

    mkdir -p "$OUTPUT_DIR/$FILE_DIR"

    {
        echo "export module $MODULE_NAME;"
        echo
        echo "import $RESOURCE_MODULE_NAME;"
        echo
        echo "inline const unsigned char ${SYMBOL_NAME}_data[] = {"
        xxd -i "$FILE" | grep -vE '^(unsigned char|unsigned int)'
        echo
        echo "export constinit Rev::Core::Resource ${SYMBOL_NAME} = { &${SYMBOL_NAME}_data[0], sizeof(${SYMBOL_NAME}_data) };"
    } > "$OUT_FILE"
done

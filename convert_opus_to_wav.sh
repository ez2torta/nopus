#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
INPUT_DIR="${SCRIPT_DIR}/xvs"
OUTPUT_DIR="${SCRIPT_DIR}/output_wavs"
NOPUS_BIN="${SCRIPT_DIR}/nopus"
LOOP_MODE="auto"

print_usage() {
    echo "Uso: $(basename "$0") [--no-loop|-no-loop]"
    echo
    echo "Opciones:"
    echo "  --no-loop, -no-loop   Genera WAV sin loop points"
    echo "  -h, --help            Muestra esta ayuda"
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --no-loop|-no-loop)
            LOOP_MODE="none"
            ;;
        -h|--help)
            print_usage
            exit 0
            ;;
        *)
            echo "Error: opción desconocida '$1'"
            print_usage
            exit 1
            ;;
    esac
    shift
done

if [[ ! -x "${NOPUS_BIN}" && ! -x "${NOPUS_BIN}.exe" ]]; then
    echo "Error: nopus no encontrado en '${NOPUS_BIN}'."
    echo "Compila el proyecto primero con 'make'."
    exit 1
fi

# Prefer .exe on Windows/MinGW if it exists
[[ -x "${NOPUS_BIN}.exe" ]] && NOPUS_BIN="${NOPUS_BIN}.exe"

mkdir -p "${OUTPUT_DIR}"

for opus_file in "${INPUT_DIR}"/*.opus; do
    if [[ -f "$opus_file" ]]; then
        filename=$(basename "$opus_file" .opus)
        wav_file="${OUTPUT_DIR}/${filename}.wav"
        echo "Convirtiendo '$opus_file' a '$wav_file'..."
        "${NOPUS_BIN}" make_wav "$opus_file" "$wav_file"
    else
        echo "No se encontraron archivos .opus en '${INPUT_DIR}'."
    fi
done
#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
INPUT_DIR="${SCRIPT_DIR}/mp3s"
OUTPUT_DIR="${SCRIPT_DIR}/output_opus"
TEMP_DIR="${OUTPUT_DIR}/.tmp_wav"
ENCODER_BIN="${SCRIPT_DIR}/create_capcom_opus"

if ! command -v ffmpeg >/dev/null 2>&1; then
    echo "Error: ffmpeg no está instalado en el sistema o no está en PATH."
    exit 1
fi

if [[ ! -x "${ENCODER_BIN}" ]]; then
    echo "Error: no se encontró el binario ejecutable '${ENCODER_BIN}'."
    echo "Compila primero con: make"
    exit 1
fi

if [[ ! -d "${INPUT_DIR}" ]]; then
    echo "Error: no existe la carpeta de entrada '${INPUT_DIR}'."
    exit 1
fi

mkdir -p "${OUTPUT_DIR}" "${TEMP_DIR}"

cleanup() {
    rm -rf "${TEMP_DIR}"
}
trap cleanup EXIT

find_mp3_files() {
    find "${INPUT_DIR}" -maxdepth 1 -type f \( -iname "*.mp3" \) -print0 | sort -z
}

mapfile -d '' mp3_files < <(find_mp3_files)

if [[ ${#mp3_files[@]} -eq 0 ]]; then
    echo "No se encontraron archivos .mp3 en '${INPUT_DIR}'."
    exit 0
fi

echo "Procesando ${#mp3_files[@]} archivo(s) de MP3..."

for mp3_file in "${mp3_files[@]}"; do
    file_name="$(basename "${mp3_file}")"
    base_name="${file_name%.*}"

    wav_file="${TEMP_DIR}/${base_name}.wav"
    opus_file="${OUTPUT_DIR}/${base_name}.opus"

    echo "[1/2] MP3 -> WAV: ${file_name}"
    ffmpeg -hide_banner -loglevel error -y -i "${mp3_file}" "${wav_file}"

    echo "[2/2] WAV -> OPUS: ${base_name}.opus"
    "${ENCODER_BIN}" "${wav_file}" "${opus_file}" 0 0

done

echo "Listo. Archivos OPUS generados en '${OUTPUT_DIR}'."
echo "WAV temporal eliminado automáticamente."

# nopus

A tool for decoding and encoding Nintendo Switch OPUS audio files, including the Capcom variant used in games such as *Monster Hunter Rise*.

Nintendo Switch OPUS files store raw Opus packets inside a proprietary container. The Capcom variant adds an extra wrapper with loop-point metadata that vgmstream identifies as **"Nintendo Switch OPUS header (Ogg)"**.

This program is licensed under the MIT license — conhlee 2025.

---

## Dependencies

| Dependency | Purpose |
|---|---|
| C compiler (`gcc` or `clang`) | compiles the source |
| `make` | build system |
| `libopus` | Opus encoding/decoding |

The Makefile uses `CC ?= cc`, so it picks up whatever `cc` points to on your system (clang on macOS, gcc on most Linux distros). You can always override it: `make CC=gcc`.

### Install build tools

**Ubuntu / Debian**
```bash
sudo apt-get install build-essential
```

**Fedora / RHEL**
```bash
sudo dnf install gcc make
```

**macOS** — install Xcode Command Line Tools (ships clang as `cc`):
```bash
xcode-select --install
```

### Install libopus

**Ubuntu / Debian**
```bash
sudo apt-get install libopus-dev
```

**Fedora / RHEL**
```bash
sudo dnf install opus-devel
```

**macOS (Homebrew)**
```bash
brew install opus
```

---

## Build

```bash
git clone https://github.com/conhlee/nopus
cd nopus
make
```

This produces two binaries in the repository root:

| Binary | Description |
|---|---|
| `nopus` | Main converter (decode/encode Nintendo & Capcom OPUS) |
| `create_capcom_opus` | Standalone Capcom OPUS encoder (legacy) |

### Optional: build the browser/WASM bundle

If you want a pure client-side HTML frontend, you can compile the converter to WebAssembly and keep all audio processing inside the browser:

```bash
make wasm
```

This target expects an Emscripten toolchain (`emcc`) plus a `libopus` build that is available to that toolchain.

That target writes `web/nopus-web.js`, a single-file Emscripten bundle with the WASM payload embedded. After that, open `web/index.html` and the page will process WAV/OPUS files locally in the browser without a processing backend.

To clean build artifacts:
```bash
make clean
```

---

## Usage

```
./nopus <command> <input> <output> [options]
```

## Frontend HTML + WASM (sin backend de procesamiento)

Sí: este proyecto ya tiene la parte más importante para eso, porque la lógica de conversión trabaja sobre buffers en memoria. Con el target `make wasm` se expone un wrapper WebAssembly y `web/index.html` ofrece una interfaz estática para:

- WAV → Nintendo OPUS
- WAV → Capcom OPUS
- Nintendo OPUS → WAV
- Capcom OPUS → WAV

Flujo esperado:

1. Compilar el bundle:
   ```bash
   make wasm
   ```
2. Abrir `web/index.html`
3. Subir el archivo desde el navegador
4. Convertir y descargar el resultado

Eso evita un servidor de procesamiento: el audio entra al navegador, se convierte en WASM y vuelve a descargarse del lado del cliente.

### Commands

#### `make_wav` — Nintendo OPUS → WAV
Decodes a standard Nintendo Switch OPUS file to PCM WAV.

```bash
./nopus make_wav input.opus output.wav
```

#### `make_opus` — WAV → Nintendo OPUS
Encodes a WAV file to standard Nintendo Switch OPUS format.

```bash
./nopus make_opus input.wav output.opus
```

#### `make_capcom_opus` — WAV → Capcom OPUS
Encodes a WAV file to the Capcom variant of Nintendo Switch OPUS, with loop-point metadata embedded in the header. Loop points are specified in **samples** (not seconds).

```bash
# With explicit loop points
./nopus make_capcom_opus input.wav output.opus <loop_start> <loop_end>

# Auto loop (0 → end of file)
./nopus make_capcom_opus input.wav output.opus auto
```

**Example** using one of the included samples:
```bash
./nopus make_capcom_opus samples/wav/BGM_0000_bin.wav out.opus 0 3743454
```

#### `make_capcom_wav` — Capcom OPUS → WAV
Decodes a Capcom OPUS file (single pass, loop ignored) to PCM WAV.

```bash
./nopus make_capcom_wav input.opus output.wav
```

---

## Verification with vgmstream

The repository includes `vgmstream-linux-cli.zip` (vgmstream r2083), which can decode both original and re-encoded Capcom OPUS files. Use it to verify the round-trip.

### Extract vgmstream

```bash
unzip vgmstream-linux-cli.zip -d /tmp/vgmstream
chmod +x /tmp/vgmstream/vgmstream-cli
```

### Decode an original Capcom OPUS file

The `-i` flag decodes a single pass without looping.

```bash
/tmp/vgmstream/vgmstream-cli -i -o decoded.wav samples/opus/BGM_0000_bin.opus
```

### Full round-trip workflow

```bash
# 1. Decode original → WAV with vgmstream
/tmp/vgmstream/vgmstream-cli -i -o original.wav samples/opus/BGM_0000_bin.opus

# 2. Re-encode WAV → Capcom OPUS with nopus
./nopus make_capcom_opus original.wav reencoded.opus 0 3743454

# 3. Verify re-encoded file with vgmstream
/tmp/vgmstream/vgmstream-cli -i -o roundtrip.wav reencoded.opus
```

Expected vgmstream output for both the original and re-encoded file:
```
sample rate: 48000 Hz
channels: 2
encoding: libopus Opus
metadata from: Nintendo Switch OPUS header (Ogg)
bitrate: 99 kbps
```

### Loop points for included samples

| File | loop_start | loop_end |
|---|---|---|
| `BGM_0000_bin.opus` | 0 | 3743454 |
| `BGM_0A00_bin.opus` | 0 | 3071479 |
| `BGM_0B00_bin.opus` | 800949 | 4808309 |
| `BGM_0B01_bin.opus` | 0 | 3161522 |
| `BGM_0C00_bin.opus` | 0 | 3894074 |

---

## Repository layout

```
nopus/
├── src/                        C source files
│   ├── main.c                  nopus entry point (all commands)
│   ├── opusProcess.h/.c        Opus encode/decode (Nintendo & Capcom)
│   ├── wavProcess.h/.c         WAV read/write helpers
│   ├── files.h/.c              File I/O helpers
│   ├── list.h/.c               Dynamic array helper
│   ├── common.h/.c             Shared utilities
│   └── type.h                  Primitive type aliases
├── samples/
│   ├── opus/                   Original Capcom OPUS samples
│   └── wav/                    WAVs decoded from originals via vgmstream -i
├── docs/                       Analysis scripts and hex dumps
├── web/                        Static HTML frontend + generated WASM bundle
├── vgmstream-linux-cli.zip     vgmstream r2083 Linux CLI binary
└── Makefile
```

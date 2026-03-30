(() => {
    const elements = {};
    // Short delay to let the generated bundle finish loading before showing a missing-build hint.
    const WASM_LOAD_TIMEOUT_MS = 1500;
    let runtimeReady = false;
    let downloadUrl = null;

    function outputExtension(command) {
        if (command === 'make_wav' || command === 'make_capcom_wav')
            return 'wav';
        return 'opus';
    }

    function outputName(inputName, command) {
        const stem = inputName.replace(/\.[^.]+$/, '') || 'converted';
        return `${stem}.${outputExtension(command)}`;
    }

    function setStatus(message) {
        elements.status.textContent = message;
    }

    function updateLoopInputs() {
        const enabled = elements.command.value === 'make_capcom_opus';
        elements.loopStart.disabled = !enabled;
        elements.loopEnd.disabled = !enabled;
        elements.autoLoop.disabled = !enabled;
    }

    async function convert() {
        const file = elements.inputFile.files[0];
        if (!file) {
            setStatus('Elegí un archivo antes de convertir.');
            return;
        }

        const command = elements.command.value;
        const inputBytes = new Uint8Array(await file.arrayBuffer());
        if (!inputBytes.length) {
            setStatus('El archivo está vacío.');
            return;
        }

        const inputPtr = Module._malloc(inputBytes.length);
        if (!inputPtr) {
            setStatus('No se pudo reservar memoria en el módulo WASM.');
            return;
        }
        Module.HEAPU8.set(inputBytes, inputPtr);

        elements.convert.disabled = true;
        elements.download.hidden = true;
        setStatus('Procesando en el navegador…');

        try {
            let resultCode = 1;

            switch (command) {
            case 'make_wav':
                resultCode = Module._nopus_make_wav(inputPtr, inputBytes.length);
                break;
            case 'make_opus':
                resultCode = Module._nopus_make_opus(inputPtr, inputBytes.length);
                break;
            case 'make_capcom_opus':
                resultCode = Module._nopus_make_capcom_opus(
                    inputPtr,
                    inputBytes.length,
                    Number(elements.loopStart.value || 0),
                    Number(elements.loopEnd.value || 0),
                    elements.autoLoop.checked ? 1 : 0
                );
                break;
            case 'make_capcom_wav':
                resultCode = Module._nopus_make_capcom_wav(inputPtr, inputBytes.length);
                break;
            }

            if (resultCode !== 0) {
                setStatus('La conversión falló. Verificá que el archivo coincida con el formato elegido.');
                return;
            }

            const resultPtr = Module._nopus_result_ptr();
            const resultSize = Module._nopus_result_size();
            if (!resultPtr || !resultSize) {
                setStatus('La conversión no devolvió datos.');
                return;
            }
            const resultBytes = Module.HEAPU8.subarray(resultPtr, resultPtr + resultSize);
            const blob = new Blob([resultBytes], { type: 'application/octet-stream' });
            if (downloadUrl)
                URL.revokeObjectURL(downloadUrl);
            const url = URL.createObjectURL(blob);
            downloadUrl = url;

            elements.download.href = url;
            elements.download.download = outputName(file.name, command);
            elements.download.hidden = false;
            setStatus(`Conversión lista. Todo se procesó localmente en tu navegador (${resultSize} bytes).`);
        } finally {
            Module._nopus_result_free();
            Module._free(inputPtr);
            elements.convert.disabled = !runtimeReady;
        }
    }

    function bindUi() {
        elements.command = document.getElementById('command');
        elements.inputFile = document.getElementById('input-file');
        elements.loopStart = document.getElementById('loop-start');
        elements.loopEnd = document.getElementById('loop-end');
        elements.autoLoop = document.getElementById('auto-loop');
        elements.convert = document.getElementById('convert');
        elements.status = document.getElementById('status');
        elements.download = document.getElementById('download');

        elements.command.addEventListener('change', updateLoopInputs);
        elements.convert.addEventListener('click', () => {
            void convert();
        });

        updateLoopInputs();
        setStatus('Esperando el bundle nopus-web.js generado por `make wasm`.');

        window.setTimeout(() => {
            if (!runtimeReady)
                setStatus('No se encontró `web/nopus-web.js`. Ejecutá `make wasm` antes de abrir esta página.');
        }, WASM_LOAD_TIMEOUT_MS);
    }

    window.Module = window.Module || {};
    window.Module.onRuntimeInitialized = () => {
        runtimeReady = true;
        elements.convert.disabled = false;
        elements.convert.textContent = 'Convertir';
        setStatus('Módulo listo. Elegí un archivo para procesarlo localmente.');
    };

    window.addEventListener('DOMContentLoaded', bindUi);
})();

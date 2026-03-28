# 🛠️ Compilación en Windows (MSYS2)

Este proyecto puede compilarse en Windows usando **MSYS2** y el toolchain de MinGW.

## 📦 Requisitos

1. Instalar MSYS2:
   https://www.msys2.org/

2. Abrir la terminal correcta:

   ```
   MSYS2 MinGW64
   ```

---

## 🔧 Instalación de dependencias

Dentro de la terminal **MINGW64**, ejecutar:

```bash
pacman -S mingw-w64-x86_64-gcc make mingw-w64-x86_64-opus
```

Esto instalará:

* `gcc` (compilador)
* `make`
* `libopus` (dependencia del proyecto)

---

## 📂 Navegar al proyecto

Los discos de Windows están montados así:

| Windows | MSYS2 |
| ------- | ----- |
| C:\     | /c/   |

Ejemplo:

```bash
cd /c/Users/TuUsuario/ruta/al/proyecto
```

---

## 🚀 Compilar

```bash
make
```

Esto generará los ejecutables:

```
nopus.exe
create_capcom_opus.exe
```

---

## ▶️ Ejecutar

```bash
./nopus.exe
```

---

## ⚠️ Dependencias en runtime (DLL)

El ejecutable depende de:

```
libopus-0.dll
```

Ubicada en:

```
C:\msys64\mingw64\bin
```

### ✔️ Solución recomendada

Copiar la DLL junto al ejecutable:

```bash
cp /mingw64/bin/libopus-0.dll .
```

Estructura final:

```
nopus.exe
libopus-0.dll
```

---

## 🧪 Verificar dependencias

```bash
ldd nopus.exe
```

---

## 📝 Notas

* Es importante usar **MINGW64**, no la terminal MSYS.
* El binario generado es un `.exe` nativo de Windows.
* Si faltan DLLs, el programa no ejecutará fuera de MSYS2.

---

# IC Lab 1

Enunciado: [`trab1.pdf`](trab1.pdf).

## Dependencias

- Compilador de C17 (GCC/Clang)
- Meson >= 1.3.0
- Ninja

## Build

```sh
meson setup build
meson compile -C build
./build/src/wav_hist
```

Para entrega/avaliação: 

```sh
meson setup build-release --buildtype=release
meson compile -C build-release
```

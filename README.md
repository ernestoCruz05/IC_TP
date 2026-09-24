# IC Lab 1

Enunciado: [`trab1.pdf`](trab1.pdf).

## Dependências

- Compilador de C17 (GCC/Clang)
- Meson >= 1.3.0
- Ninja

## Build

```sh
meson setup build
meson compile -C build
```

## wav_hist

O programa gera um SVG com um painel por histograma e uma escala Y comum. A opção `-c`
pode ser repetida e `-k` define que cada bin agrupa `2^k` valores.

```sh
./build/src/wav_hist dataset/sample01.wav
./build/src/wav_hist -c L -c MID -k 4 -o out dataset/sample01.wav
```

Para entrega/avaliação:

```sh
meson setup build-release --buildtype=release
meson compile -C build-release
```

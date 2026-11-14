# Trabalho 2 — Imagens

Enunciado: [trab2.pdf](trab2.pdf).

Esta pasta fica dedicada ao segundo trabalho, sobre análise, quantização e
compressão de imagens RAW, PGM e PPM. O Trabalho 1, sobre áudio, está em
[`../trab1/`](../trab1/README.md).

O código, os datasets, os scripts de teste e os relatórios do Trabalho 2 ficam
organizados nesta pasta.

## `img_hist`

A primeira versão de `img_hist` lê imagens PGM binárias P5 de 8 ou 16 bits. No
caso de 16 bits, os pixels são lidos em big-endian, como definido pelo formato
PGM. O programa apresenta o histograma e a entropia dos pixels, calcula as
entropias das diferenças entre vizinhos horizontais e verticais e cria um
gráfico SVG em `out/`.

Também pode analisar apenas um intervalo de bits dos pixels. Por exemplo,
`0-5` seleciona os seis bits menos significativos. O histograma é calculado
com os valores selecionados antes das diferenças.

```sh
meson setup build-trab2 trab2  # só na primeira configuração
meson compile -C build-trab2

./build-trab2/src/img_hist trab2/dataset/imgs-pgm8/lena.pgm
./build-trab2/src/img_hist --bit-range 0-5 \
    trab2/dataset/imgs-pgm8/lena.pgm
```

Os gráficos são guardados, por exemplo, em `out/lena_hist.svg` e
`out/lena_hist_bits0-5.svg`. Esta versão ainda não lê PGM ASCII P2.

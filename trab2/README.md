# Trabalho 2 — Imagens

Enunciado: [trab2.pdf](trab2.pdf).

Esta pasta fica dedicada ao segundo trabalho, sobre análise, quantização e
compressão de imagens RAW, PGM e PPM. 

O código, os datasets, os scripts de teste e os relatórios do Trabalho 2 ficam
organizados nesta pasta.

## `img_hist`

A primeira versão de `img_hist` lê PGM binário P5 de 8 ou 16 bits e RAW de 8 ou
16 bits. Em PGM, os pixels de 16 bits são lidos em big-endian. Para RAW, é
preciso indicar dimensões e profundidade; para RAW de 16 bits também se indica
a endianess (big endian ou little endian).

O programa apresenta o histograma e a entropia dos pixels, calcula as
entropias das diferenças entre vizinhos horizontais e verticais e cria um
gráfico SVG em `out/`. Para histogramas com mais de 256 valores, o gráfico
agrupa valores em até 256 colunas; as contagens e entropias continuam a usar os
valores exatos.

Também pode analisar apenas um intervalo de bits dos pixels. Por exemplo,
`0-5` seleciona os seis bits menos significativos. O histograma é calculado
com os valores selecionados antes das diferenças.

```sh
meson setup build-trab2 trab2  # só na primeira configuração
meson compile -C build-trab2

./build-trab2/src/img_hist trab2/dataset/imgs-pgm8/lena.pgm
./build-trab2/src/img_hist --bit-range 0-5 \
    trab2/dataset/imgs-pgm8/lena.pgm

# RAW de 16 bits, big-endian; --rows e --cols são as dimensões da imagem
./build-trab2/src/img_hist --format raw --rows 2046 --cols 2048 \
    --bits 16 --endian big \
    trab2/dataset/imgs-raw16/ERIS.2024-01-01T06:15:12.592_u16be-1x2046x2048.raw

# RAW de 8 bits: não é necessário indicar endianess
./build-trab2/src/img_hist --format raw --rows 480 --cols 640 \
    --bits 8 imagem.raw
```

Para um intervalo de bits RAW, acrescenta `--bit-range`, por exemplo,
`--bit-range 0-5`. Os gráficos incluem `_raw_` no nome, como
`out/ERIS.2024-01-01T06:15:12.592_u16be-1x2046x2048_raw_hist.svg`; os gráficos
PGM são guardados, por exemplo, em `out/lena_hist.svg` e
`out/lena_hist_bits0-5.svg`. 

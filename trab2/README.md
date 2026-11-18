# Trabalho 2 — Imagens

Enunciado: [trab2.pdf](trab2.pdf).

Esta pasta fica dedicada ao segundo trabalho, sobre análise, quantização e
compressão de imagens RAW, PGM e PPM. 

O código, os datasets, os scripts de teste e os relatórios do Trabalho 2 ficam
organizados nesta pasta.

## `img_hist`

A versão atual de `img_hist` lê PGM binário P5, PPM binário P6 e RAW. PGM e PPM
aceitam `Maxval` até 65535; amostras acima de 255 são lidas em dois bytes
big-endian. Para RAW, é preciso indicar dimensões e profundidade; para RAW de 16 bits também se indica a endianess (big-endian ou little-endian). PPM é analisado separadamente nos canais R, G e B.

O programa espera uma imagem por ficheiro e rejeita dados adicionais depois
dos pixels.

O código do programa está dividido: `img_hist.c` trata dos argumentos e coordena a análise; `img_hist_io.c` lê os formatos; `histogram_stats.c` calcula histogramas, diferenças e entropias; e `histogram_svg.c` gera os gráficos. Os ficheiros `.h` correspondentes declaram as funções partilhadas.

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

# PPM: cria gráficos separados e sobrepostos para R, G e B
./build-trab2/src/img_hist trab2/dataset/imgs-ppm/lena.ppm
./build-trab2/src/img_hist --bit-range 0-5 \
    trab2/dataset/imgs-ppm/lena.ppm

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
`out/lena_hist_bits0-5.svg`. Para PPM são criados `out/lena_ppm_hist_channels.svg`
e `out/lena_ppm_hist_overlay.svg`, com `bits0-5` acrescentado ao nome quando
se usa `--bit-range`. PPM P6 com `Maxval` entre 256 e 65535 usa dois bytes
big-endian por componente.

## `img_quant`

Quantização escalar uniforme: reduz cada pixel (ou canal, em PPM) a `BITS`
bits e escreve a imagem no formato da entrada. Cada amostra fica com o valor
central do seu intervalo. Para RAW usam-se as mesmas opções de `img_hist`.

```sh
./build-trab2/src/img_quant -b 3 trab2/dataset/imgs-pgm8/lena.pgm lena_q3.pgm
./build-trab2/src/img_quant -b 4 --format raw --rows 480 --cols 640 \
    --bits 8 imagem.raw imagem_q4.raw
```

## `img_cmp`

Compara duas imagens com o mesmo tipo, dimensões e `Maxval`, calculando o erro
máximo absoluto, o MSE e o PSNR em dB (por canal e global no caso PPM). Para
imagens RAW usam-se as mesmas opções de `img_hist`, podendo usar `--orig-raw` ou
`--test-raw` quando apenas uma das imagens é RAW.

```sh
./build-trab2/src/img_cmp trab2/dataset/imgs-pgm8/lena.pgm lena_q3.pgm
./build-trab2/src/img_cmp --format raw --rows 480 --cols 640 --bits 8 \
    imagem.raw imagem_q4.raw
./build-trab2/src/img_cmp --test-raw --rows 512 --cols 512 --bits 8 \
    trab2/dataset/imgs-pgm8/lena.pgm lena.raw
```

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

O programa gera `out/<nome>_histograms.svg`, com um painel por histograma e
uma escala Y comum. A opção `-c` pode ser repetida e `-k` define que cada bin
agrupa `2^k` valores. Sem `-k`, são usados 256 bins; sem `-c`, são incluídos
L, R, MID e DIFF para estéreo, ou apenas MONO para mono.

MID é `(L+R)/2` e DIFF é `(L-R)/2`, com divisão inteira truncada para zero.
DIFF não é SIDE (`L-R`).

```sh
./build/src/wav_hist dataset/sample01.wav
./build/src/wav_hist -c L -c MID -k 4 -o out dataset/sample01.wav
```

### Formatos aceites

- RIFF/WAVE little-endian com PCM inteiro, mono ou estéreo.
- Amostras de 8, 16, 24 ou 32 bits. PCM de 8 bits é unsigned no ficheiro e
  convertido para `-128..127`; as restantes profundidades são signed.
- PCM clássico (formato 1) e WAVE_FORMAT_EXTENSIBLE com o GUID de PCM, desde
  que os bits válidos coincidam com a largura da amostra. Por exemplo,
  24 bits válidos num contentor de 32 bits não são aceites.

Não são aceites WAV com float, codecs comprimidos, mais de dois canais,
RIFX ou RF64. Em estéreo, as amostras são tratadas como L/R; a máscara de
canais do formato extensible não é interpretada.

`wav_parse()` valida o ficheiro e preenche os metadados, mas não posiciona
para leitura de áudio. É necessário chamar `wav_seek_data()` antes de
`wav_read_sample()` e limitar a leitura a `data_size / block_align` frames.

### Leitura dos gráficos

Os bins vazios antes da primeira e depois da última ocorrência são omitidos.
Os bins vazios no meio mantêm-se. Cada painel tem o seu próprio intervalo X;
a linha vermelha tracejada marca zero quando este está dentro do intervalo.

Se houver mais de 1060 bins no intervalo visível, apenas o desenho é
agregado em até 1060 colunas, somando as contagens dos bins de cada coluna.
A indicação `display aggregated into N columns` assinala esse caso. O
histograma calculado continua a ter a largura de bin pedida com `-k`; os
seus dados não são alterados. Nesses gráficos, Y representa a contagem por
coluna de apresentação, não por bin original.

Os SVG podem ser abertos num navegador, sem dependências de plotting.
`--debug` compara a soma dos bins de cada histograma com o número de frames
e termina com erro se houver diferenças. Para histogramas muito grandes,
o programa pede um `-k` maior em vez de ultrapassar 16 777 216 bins por canal.

Para entrega/avaliação:

```sh
meson setup build-release --buildtype=release
meson compile -C build-release
```

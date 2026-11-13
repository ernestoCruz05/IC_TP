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

## wav_quant

Aplica quantização escalar uniforme a um ficheiro WAV, reduzindo a resolução
para `quant_bits` bits (`1 <= quant_bits < source_bits`):

```sh
./build/src/wav_quant -b BITS input.wav output.wav
```

### Contentor e níveis

A saída é um WAV PCM. A largura do contentor (`storage_bits`, `K`) depende
dos bits de quantização (`Q`):

- 1 a 8 bits: contentor de 8 bits
- 9 a 16 bits: contentor de 16 bits
- 17 a 24 bits: contentor de 24 bits
- 25 a 32 bits: contentor de 32 bits

A quantização determina o índice de intervalo `k` em `[0, 2^Q - 1]` a partir da
amostra de entrada. A reconstrução no contentor de saída depende da relação entre
`K` e `Q`:

- Quando `K > Q`: o ponto médio teórico `l_k = A_min + Δ/2 + kΔ` é
  exatamente representável como inteiro no contentor, pois `Δ/2 = 2^(K-Q-1) >= 1`.
- Quando `K = Q`: cada amostra de entrada é dividida por
  `2^(source_bits-storage_bits)`, arredondada ao código PCM mais próximo e limitada
  à gama válida `[-2^(K-1), 2^(K-1)-1]`.

O cabeçalho WAV de saída reflete os `storage_bits` escolhidos, recalculando
`block_align`, `byte_rate` e `data_size`.

## wav_cmp

Compara dois ficheiros WAV (original e processado/quantizado), imprimindo para
cada canal e para o sinal médio (MID):

- Erro quadrático médio (MSE / L2) e RMSE
- Erro máximo absoluto por amostra (L∞)
- Relação sinal-ruído (SNR em dB)

```sh
./build/src/wav_cmp original.wav test.wav
```

Para entrega/avaliação:

```sh
meson setup build-release --buildtype=release
meson compile -C build-release
```

## wav_effects

Aplica efeitos a um WAV e escreve outro WAV PCM com o mesmo `sample_rate`,
número de canais e `bits_per_sample`. Carrega todas as amostras para memória
antes de aplicar o efeito.

```sh
./build/src/wav_effects -e reverse input.wav output.wav
./build/src/wav_effects -e echo -d SECONDS -g GAIN [-r REPEATS] input.wav output.wav
./build/src/wav_effects -e amod -f HZ -p DEPTH input.wav output.wav
```

### Efeitos disponíveis

- reverse: inverte a ordem dos frames, mantendo os valores das amostras.
- echo: soma ao sinal original uma ou mais cópias atrasadas e
  progressivamente atenuadas.
  - `-d SECONDS` (default `0.3`): atraso de cada eco, em segundos.
  - `-g GAIN` (default `0.5`): fator de atenuação de cada repetição
    (`0 < gain < 1`); cada eco sucessivo é atenuado por `gain^r`.
  - `-r REPEATS` (default `1`): número de ecos somados. `1` corresponde a
    um único eco.
  - O eco é calculado a partir do sinal original.
- amod: modulação de amplitude, multiplicando cada amostra por
  `1 + depth * sin(2π * freq_hz * t)`.
  - `-f HZ` (default `5.0`): frequência da modulação, em Hz.
  - `-p DEPTH` (default `0.5`): profundidade do efeito (`0 <= depth <= 1` para evitar inversão de fase).

Todas as amostras resultantes são limitadas (`clipping`) à gama válida do
`bits_per_sample` do ficheiro de entrada antes de serem escritas.

## wav_lossless

`wav_lossless_enc` comprime um ficheiro WAV sem perdas e `wav_lossless_dec`
reconstrói o ficheiro original, byte a byte.

```sh
./build/src/wav_lossless_enc [-b BLOCK_SIZE] [-l LPC_ORDER] [-q PRECISION] \
    [-p PARTITION_ORDER] [-j THREADS] input.wav compressed_file
./build/src/wav_lossless_dec [-j THREADS] compressed_file output.wav
```

- `-b` define o número de frames por bloco, de 16 a 65535 (4096 por omissão).
- `-l` define a ordem máxima do preditor LPC, de 0 a 32 (12 por omissão). Com
  0, só são usados os preditores fixos. Ordens maiores podem melhorar a
  compressão, com mais tempo de processamento.
- `-q` define a precisão dos coeficientes LPC, de 5 a 15 bits (12 por
  omissão).
- `-p` define a ordem máxima de partição do código Rice, de 0 a 8 (4 por
  omissão). Com ordem p, os resíduos de um stream podem ser divididos em até
  2^p partições, cada uma com o seu parâmetro.
- `-j` define o número de threads, de 1 a 64, nos dois programas. Por
  omissão, usa o número de processadores disponíveis, limitado a 64. O
  ficheiro comprimido é o mesmo com qualquer número de threads.

### O que é comprimido

São comprimidos os ficheiros que o `wav_parse()` aceita e que tenham de 1 a 8
canais: PCM inteiro de 8, 16, 24 ou 32 bits, no formato clássico ou
extensible. Só o payload do chunk `data` é comprimido. Os bytes antes e depois
dele, incluindo outros chunks, o byte de padding e bytes para lá do fim do
RIFF, são copiados sem alterações.

Os restantes ficheiros, como WAV com float, RF64 ou mais de 8 canais, são
guardados sem compressão. O codificador avisa no stderr e o descodificador
reconstrói-os da mesma forma.

Em estéreo, cada bloco escolhe o par de streams com menor custo entre L/R, L/S,
S/R e M/S, com S = L - R e M = ⌊(L + R) / 2⌋. Cada stream é guardado como um
valor constante, com as amostras sem compressão, ou como os resíduos de um
preditor, codificados com Rice. O preditor é fixo, de ordem 0 a 4, ou linear
(LPC), com coeficientes calculados por bloco: autocorrelação do stream com
janela de Tukey, Levinson-Durbin e quantização dos coeficientes. A ordem LPC é
estimada a partir do erro de predição de cada ordem, e só essa é avaliada com
o custo exato. Antes da codificação, os resíduos são mapeados para inteiros
não negativos (0, -1, 1, -2, ... passam a 0, 1, 2, 3, ...). O codificador
escolhe a ordem de partição e o parâmetro Rice de cada partição a partir das
somas desses valores.

O ficheiro comprimido termina com o CRC-32 do ficheiro original. O
descodificador falha se o CRC não coincidir, se o ficheiro estiver truncado ou
se algum bloco for inválido; nesse caso, apaga o ficheiro de saída.

### Comparação com outros compressores

```sh
meson setup build-rel --buildtype=release -Db_lto=true
ninja -C build-rel
python3 scripts/bench_lossless.py --runs 3 --threads 12 dataset/*.wav
```

O `setup` só é necessário ao criar o build. Para usar outros executáveis,
indique os caminhos com `--enc` e `--dec`.

O script compara o codec com flac, xz, zstd, bzip2, gzip e WavPack. Varia
`-b`, `-l`, `-q` e `-p` e compara uma e várias threads. Mostra o tamanho, os
tempos de compressão e descompressão e o pico de memória. Soma o melhor
tempo de cada ficheiro nas três execuções e verifica cada reconstrução byte
a byte, fora da medição. A memória é medida em MiB com GNU time (pacote
`time`); sem ele, aparece `n/a`.

O flac usa `--keep-foreign-metadata` para preservar chunks extra; xz e zstd
comprimem o ficheiro inteiro. Ferramentas em falta são ignoradas. Falhas ou
diferenças na reconstrução são indicadas no fim e dão código de saída 1.

Resultados nos 7 ficheiros de `dataset/` (25 300 772 bytes), num Ryzen 5 7535HS:

| Configuração | Tamanho/WAV | Compressão (s) | Descompressão (s) |
|---|---|---|---|
| wav_lossless -j1 | 55,85% | 0,661 | 0,285 |
| wav_lossless -j12 | 55,85% | 0,150 | 0,095 |
| wav_lossless -l 32 -p 8 -j12 | 55,45% | 0,232 | 0,116 |
| flac -5 -j1 | 56,30% | 0,306 | 0,187 |
| flac -5 -j12 | 56,30% | 0,089 | 0,183 |
| flac -8 -j1 | 55,99% | 0,732 | 0,201 |
| flac -8 -j12 | 55,99% | 0,130 | 0,196 |
| xz -9e -T1 | 83,36% | 5,545 | 0,706 |
| zstd -19 -T1 | 89,35% | 3,465 | 0,076 |
| wavpack | 56,56% | 0,236 | 0,144 |
| wavpack -hh | 55,27% | 0,441 | 0,231 |
| bzip2 -9 | 85,36% | 2,092 | 1,398 |
| gzip -9 | 89,68% | 0,843 | 0,148 |

O flac descodifica sempre com uma thread.

## wav_lossy

Aplica DCT-II a blocos de áudio e guarda os coeficientes quantizados com Rice.
A descodificação mantém a frequência, os canais, a profundidade PCM e a duração.

```sh
./build/src/wav_lossy_enc [-n BLOCK_SIZE] [-q QUALITY] input.wav compressed_file
./build/src/wav_lossy_dec compressed_file output.wav
```

- `-n`: tamanho do bloco, potência de dois entre 256 e 4096 (default 2048).
- `-q`: índice do passo de quantização, entre 0 e 240 (default 64). Valores
  maiores usam um passo menor e tendem a preservar melhor a qualidade.

O formato ainda pode mudar. São aceites WAV PCM inteiro de 8, 16, 24 ou 32 bits
e de 1 a 8 canais. Outros chunks WAV não são preservados.

### Comparação com MP3

O script `scripts/bench_lossy.py` compara o codec com MP3 CBR através do FFmpeg
com `libmp3lame`. Verifica os WAV descodificados e mede tamanho, SNR, tempos e
memória.

```sh
meson setup build-rel --buildtype=release -Db_lto=true
meson compile -C build-rel
python3 scripts/bench_lossy.py --runs 1 --n 256 \
    --qualities 32,48,64 \
    --bitrates 160k,224k,320k dataset/*.wav
```

Cada valor de `--qualities` é comparado com uma taxa MP3 próxima da taxa média
resultante. O FFmpeg usa uma thread e o tamanho MP3 inclui as tags. É necessário
ter `libmp3lame` disponível no FFmpeg.

Medição de referência nos 7 WAVs de `dataset/` (25 300 772 bytes, 143,43 s),
Intel Core i7-10750H, FFmpeg 6.1.1, uma execução por ficheiro:

| Codec | Tamanho | Ratio | Taxa média | Codificação | Descodificação | RSS cod./desc. | SNR L/R/MID |
|---|---:|---:|---:|---:|---:|---:|---:|
| wav_lossy DCT-II N=256 q=32 | 2 723 847 B | 10,77% | 151,9 kbps | 0,924 s | 0,923 s | 2,2 / 2,1 MiB | 23,23 / 22,19 / 24,63 dB |
| MP3 CBR 160 kbps | 2 877 962 B | 11,37% | 160,5 kbps | 3,006 s | 0,453 s | 51,7 / 51,2 MiB | 20,54 / 20,10 / 22,02 dB |
| wav_lossy DCT-II N=256 q=48 | 3 864 452 B | 15,27% | 215,5 kbps | 0,875 s | 0,935 s | 2,2 / 2,1 MiB | 33,42 / 32,35 / 34,93 dB |
| MP3 CBR 224 kbps | 4 029 020 B | 15,92% | 224,7 kbps | 2,787 s | 0,477 s | 51,7 / 51,2 MiB | 26,12 / 25,53 / 27,93 dB |
| wav_lossy DCT-II N=256 q=64 | 5 499 424 B | 21,74% | 306,7 kbps | 0,965 s | 0,967 s | 2,2 / 2,2 MiB | 44,27 / 43,21 / 45,84 dB |
| MP3 CBR 320 kbps | 5 755 604 B | 22,75% | 321,0 kbps | 2,633 s | 0,491 s | 51,8 / 51,2 MiB | 33,29 / 32,27 / 35,20 dB |

### Efeito do tamanho do bloco

Teste dos cinco tamanhos de bloco com `q=64` nos mesmos sete WAVs. Os tempos
são a soma de uma execução por ficheiro; o RSS é o pico máximo. O SNR é agregado.

| N | Tamanho | Ratio | Taxa média | Codificação | Descodificação | RSS cod./desc. | SNR L/R/MID |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 256 | 5 499 424 B | 21,74% | 306,7 kbps | 0,917 s | 0,934 s | 2,2 / 2,1 MiB | 44,27 / 43,21 / 45,84 dB |
| 512 | 5 262 174 B | 20,80% | 293,5 kbps | 0,942 s | 0,971 s | 2,2 / 2,2 MiB | 44,32 / 43,26 / 45,89 dB |
| 1024 | 5 117 699 B | 20,23% | 285,5 kbps | 1,001 s | 0,956 s | 2,2 / 2,1 MiB | 44,34 / 43,29 / 45,92 dB |
| 2048 | 5 048 234 B | 19,95% | 281,6 kbps | 1,028 s | 0,981 s | 2,2 / 2,1 MiB | 44,34 / 43,28 / 45,92 dB |
| 4096 | 5 060 699 B | 20,00% | 282,3 kbps | 1,127 s | 1,119 s | 2,3 / 2,2 MiB | 44,29 / 43,24 / 45,88 dB |

`N=2048` deu o ficheiro mais pequeno e o melhor SNR MID (empatado com `N=1024`).
Face a `N=256`, reduziu o tamanho cerca de 8,2%; a codificação demorou mais
0,111 s e a descodificação mais 0,047 s. `N=4096` não trouxe melhoria. Escolhi
`N=2048` como valor por omissão com base nestes testes.

As taxas dos pares são próximas. O SNR MID do `wav_lossy` foi superior em 2,61,
7,00 e 10,64 dB, respetivamente. Nestas medições, codificou mais depressa e
descodificou mais devagar que o MP3. O RSS do MP3 inclui o processo FFmpeg.

Com `N=2048` e `q=64`, a taxa foi 281,6 kbps e o SNR MID 45,92 dB. O parâmetro
`-q` permite trocar tamanho por qualidade: nos testes com `N=256`, q=80 deu
57,06 dB a 426,1 kbps e q=96 deu 68,54 dB a 571,8 kbps. Os resultados são de
uma execução nos ficheiros usados e podem variar com outros áudios. A DCT-II
usa uma FFT, com custo `O(N log N)` por bloco.

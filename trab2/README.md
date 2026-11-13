# Information and Coding — Lab work 2

Enunciado: [`trab2.pdf`](trab2.pdf). O Trabalho 1 continua na raiz do
repositório; o código, os dados e os resultados deste trabalho ficam nesta
pasta.

## Tema

O trabalho aplica análise, quantização e compressão a imagens nos formatos
RAW, PGM e PPM. A estrutura é semelhante à do Trabalho 1, mas os dados são
imagens em vez de áudio.

### Parte I — ferramentas de imagem

- `img_hist`: histogramas e entropia dos valores dos pixels/canais, além das
  diferenças entre pixels vizinhos na horizontal e na vertical. Deve aceitar
  RAW, PGM e PPM; para RAW é preciso fornecer dimensões e profundidade, e para
  RAW de 16 bits também a ordem dos bytes. O programa deve ainda poder analisar
  um intervalo de bits dos pixels.
- `img_quant`: quantização escalar uniforme, produzindo outra imagem.
- `img_cmp`: erro absoluto máximo por pixel/canal e PSNR relativamente ao
  original.

### Parte II — codec lossless

O encoder deve aceitar RAW, PGM e PPM. O decoder deve reconstruir exatamente o
ficheiro original. A avaliação compara razão de compressão em bits por pixel,
velocidade de codificação/descodificação e memória, com codecs lossless de
imagem e compressores de uso geral.

### Parte III — codec lossy

O codec deve permitir controlar a qualidade com `-q`, de 1 a 100. As decisões
de projeto devem ser justificadas por experiências. A comparação com codecs
lossy da família JPEG deve incluir bits por pixel, PSNR, tempos e memória.

### Relatórios

- Relatório técnico: até 12 páginas, com decisões, experiências e manual de
  utilização; não deve incluir código.
- Relatório sobre ferramentas de IA: até 6 páginas, identificando as
  ferramentas usadas e avaliando criticamente as propostas e a forma como
  foram verificadas.

## Organização

```text
trab2/
├── dataset/   # imagens de teste
├── report/    # relatórios do Trabalho 2
├── scripts/  # benchmarks e utilitários de teste
├── src/      # programas e módulos do Trabalho 2
├── meson.build
└── trab2.pdf
```

O Trabalho 2 tem um projeto Meson separado do Trabalho 1. O build terá o seu
próprio diretório na raiz do repositório:

```sh
meson setup build-trab2 trab2
meson compile -C build-trab2
```

Neste momento foi preparada a separação e a configuração inicial. Os programas
e os comandos de execução serão documentados à medida que forem implementados.

## Requisitos gerais do enunciado

O software deve ser fácil de instalar e preparado para correr pelo menos em
Linux. O enunciado permite escolher a linguagem, mas indica que tempo e memória
serão avaliados; por isso, a escolha deve considerar também o desempenho.
Existem datasets na plataforma da unidade curricular, e a avaliação pode usar
imagens privadas, pelo que os programas não devem ser ajustados apenas aos
ficheiros públicos de teste.

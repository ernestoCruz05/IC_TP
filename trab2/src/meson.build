# Os programas do Trabalho 2 serão adicionados aqui.

executable(
  'histogram_demo',
  'histogram_demo.c',
  dependencies: m_dep,
  install: false,
)

executable(
  'img_hist',
  [
    'img_hist.c',
    'img_hist_io.c',
    'histogram_stats.c',
    'histogram_svg.c',
  ],
  dependencies: m_dep,
  install: false,
)

executable(
  'img_quant',
  [
    'img_quant.c',
    'img_hist_io.c',
  ],
  install: false,
)

executable(
  'img_cmp',
  [
    'img_cmp.c',
    'img_hist_io.c',
  ],
  dependencies: m_dep,
  install: false,
)

executable(
  'img_lossless_enc',
  [
    'img_lossless_enc.c',
    'img_hist_io.c',
    'bitstream.c',
  ],
  install: false,
)

executable(
  'img_lossless_dec',
  [
    'img_lossless_dec.c',
    'bitstream.c',
  ],
  install: false,
)

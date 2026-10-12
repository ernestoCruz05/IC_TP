wav_hist = executable(
  'wav_hist',
  'wav_hist.c',
  'hist_plot.c',
  'wav.c',
  install: true,
)

wav_quant = executable(
  'wav_quant',
  'wav_quant.c',
  'wav.c',
  install: true,
)

wav_cmp = executable(
  'wav_cmp',
  'wav_cmp.c',
  'wav.c',
  dependencies: m_dep,
  install: true,
)

wav_effects = executable(
  'wav_effects',
  'wav_effects.c',
  'wav.c',
  dependencies: m_dep,
  install: true,
)

subdir('lossless')

wav_lossless_enc = executable(
  'wav_lossless_enc',
  'wav_lossless_enc.c',
  'wav.c',
  c_args: lossless_args,
  dependencies: lossless_dep,
  override_options: ['werror=true'],
  install: true,
)

wav_lossless_dec = executable(
  'wav_lossless_dec',
  'wav_lossless_dec.c',
  c_args: lossless_args,
  dependencies: lossless_dep,
  override_options: ['werror=true'],
  install: true,
)

lossless_args = cc.get_supported_arguments(
  '-Wconversion',
  '-Wsign-conversion',
  '-Wshadow',
  '-Wvla',
  '-Wcast-qual',
  '-Wpointer-arith',
  '-Wmissing-prototypes',
  '-Wdouble-promotion',
  '-Wwrite-strings',
  '-Wstrict-prototypes',
)

threads_dep = dependency('threads')

lossless_lib = static_library(
  'lossless',
  'bitstream.c',
  'block.c',
  'crc32.c',
  'lossless.c',
  'lpc.c',
  'pcm.c',
  'predict.c',
  'rice.c',
  'stereo.c',
  c_args: lossless_args,
  dependencies: [m_dep, threads_dep],
  override_options: ['werror=true'],
)

lossless_dep = declare_dependency(
  link_with: lossless_lib,
  dependencies: [m_dep, threads_dep],
  include_directories: include_directories('.'),
)

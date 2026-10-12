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

lossless_lib = static_library(
  'lossless',
  'bitstream.c',
  'block.c',
  'crc32.c',
  'lossless.c',
  'pcm.c',
  c_args: lossless_args,
  override_options: ['werror=true'],
)

lossless_dep = declare_dependency(
  link_with: lossless_lib,
  include_directories: include_directories('.'),
)

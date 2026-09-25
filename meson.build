project(
  'ic-tp',
  'c',
  version: '0.1.0',
  meson_version: '>=1.3.0',
  default_options: [
    'c_std=c17',
    'warning_level=3',
    'buildtype=debug',
  ],
)

cc = meson.get_compiler('c')
m_dep = cc.find_library('m', required: false)

subdir('src')

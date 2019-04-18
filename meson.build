project('all-pairs-edit-distance', 'cpp', version: '0.1.0',
  meson_version: '>= 0.50.0',
  default_options : ['c_std=c11', 'cpp_std=c++17']
)


config_h = configuration_data()
config_h.set_quoted('PACKAGE_VERSION', meson.project_version())
configure_file(
  output: 'all-pairs-edit-distance-config.h',
  configuration: config_h,
)
add_project_arguments([
  '--std=c++17',
  '-I' + meson.build_root(),
], language: 'cpp')

subdir('src')


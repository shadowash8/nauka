project(
    'nauka',
    'c',
    version: '0.1.0',
    default_options: [
        'warning_level=3',
    ],
)

cc = meson.get_compiler('c')

wayland_server = dependency('wayland-server')
scenefx = dependency('scenefx-0.5')
wlroots = dependency('wlroots-0.20')
xkbcommon = dependency('xkbcommon')
wayland_scanner = find_program('wayland-scanner')
wayland_protocols = dependency('wayland-protocols')
pixman = dependency('pixman-1')
protocol_dir = wayland_protocols.get_variable('pkgdatadir')
inc = include_directories('include')

add_project_arguments(
    '-DWLR_USE_UNSTABLE',
    '-Wno-unused-parameter',
    '-Wno-pedantic',
    language: 'c',
)

protocol_files = [
  'protocols/wlr-layer-shell-unstable-v1.xml',
]

generated = []

foreach f : protocol_files
  filename = f.split('/')[-1]
  dest = filename.replace('.xml', '-protocol.h')

  generated += custom_target(
    output: dest,
    input: f,
    command: [
      wayland_scanner,
      'server-header',
      '@INPUT@',
      '@OUTPUT@',
    ],
    build_by_default: true,
  )
endforeach

executable(
    'nauka',
    generated + [
        'src/nauka.c',
        'src/config.c',
        'src/decoration.c',
        'src/keyboard.c',
        'src/layer_shell.c',
        'src/layout.c',
        'src/output.c',
        'src/pointer.c',
        'src/rendering.c',
        'src/tags.c',
        'src/toplevel.c',
    ],
    include_directories: inc,
    dependencies: [
        wayland_server,
        scenefx,
        wlroots,
        xkbcommon,
        pixman,
    ],
    install: true,
)

install_data('default.conf', install_dir: get_option('datadir') / 'ashwc')
install_data('LICENSE', install_dir: get_option('datadir') / 'licenses/ashwc')
install_data('nauka.desktop', install_dir: get_option('datadir') / 'wayland-sessions')
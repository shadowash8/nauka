project(
    'nauka',
    'c',
    version: '0.2.0',
    meson_version:  '>=1.1.0',
    default_options: [
        'warning_level=3',
    ],
)

cc = meson.get_compiler('c')

prefix  = get_option('prefix')
bindir  = prefix / get_option('bindir')
datadir = get_option('datadir')
init    = get_option('init')

wayland_server = dependency('wayland-server')
scenefx = dependency('scenefx-0.5')
wlroots = dependency('wlroots-0.20')
xkbcommon = dependency('xkbcommon')
wayland_scanner = find_program('wayland-scanner')
wayland_protocols = dependency('wayland-protocols')
pixman = dependency('pixman-1')
libinput = dependency('libinput')
protocol_dir = wayland_protocols.get_variable('pkgdatadir')
inc = include_directories('include')

if 'systemd' in init
  add_project_arguments('-DUSE_SYSTEMD=1', language: 'c')
endif
if 'dinit' in init
  add_project_arguments('-DUSE_DINIT=1', language: 'c')
endif

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
        'src/animation.c',
        'src/config.c',
        'src/decoration.c',
        'src/idle.c',
        'src/keyboard.c',
        'src/layer_shell.c',
        'src/layout.c',
        'src/output.c',
        'src/pointer.c',
        'src/rendering.c',
        'src/rules.c',
        'src/session_lock.c',
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
        libinput,
    ],
    install: true,
)

install_data('default.conf', install_dir: datadir / 'nauka')
install_data('LICENSE',      install_dir: datadir / 'licenses/nauka')

install_data('resources/start-nauka', install_dir: get_option('bindir'),
             install_mode: 'rwxr-xr-x')
install_data('resources/nauka.desktop',
             install_dir: datadir / 'wayland-sessions')
install_data('resources/nauka-portals.conf',
             install_dir: datadir / 'xdg-desktop-portal')


conf = configuration_data()
conf.set('bindir', bindir)

if 'systemd' in init
  systemd_dir =  prefix / 'lib/systemd/user'

  install_data(
    configure_file(input: 'resources/nauka.service',
                   output: 'nauka.service', configuration: conf),
    'resources/nauka-session.target',
    'resources/nauka-shutdown.target',
    install_dir: systemd_dir,
  )
endif

if 'dinit' in init
  dinit_dir = prefix / 'lib/dinit.d/user'

  install_data(
    configure_file(input: 'resources/dinit/nauka',
                   output: 'nauka.dinit', configuration: conf),
    rename: 'nauka',
    install_dir: dinit_dir,
  )
  install_data('resources/dinit/nauka.target', install_dir: dinit_dir)
endif

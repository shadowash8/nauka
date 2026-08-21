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
wlroots = dependency('wlroots-0.20')
xkbcommon = dependency('xkbcommon')

inc = include_directories('include')

add_project_arguments(
    '-DWLR_USE_UNSTABLE',
    '-Wno-unused-parameter',
    language: 'c',
)
executable(
    'nauka',
    [
        'src/nauka.c',
        'src/keyboard.c',
        'src/output.c',
        'src/pointer.c',
        'src/toplevel.c',
    ],
    include_directories: inc,
    dependencies: [
        wayland_server,
        wlroots,
        xkbcommon,
    ],
    install: true,
)

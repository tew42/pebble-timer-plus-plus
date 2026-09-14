#
# This file is the default set of rules to compile a Pebble application.
#

import os.path

top = '.'
out = 'build'


def options(ctx):
    ctx.load('pebble_sdk')


def configure(ctx):
    """
    This method is used to configure your build. ctx.load(`pebble_sdk`) automatically configures
    a build for each valid platform in `targetPlatforms`. Platform-specific configuration: add your
    change after calling ctx.load('pebble_sdk') and make sure to set the correct environment first.
    Universal configuration: add your change prior to calling ctx.load('pebble_sdk').
    """
    ctx.load('pebble_sdk')


# Platforms with a touch surface, which is what the SDK's own PBL_TOUCH define marks. Read off
# sdk-core/pebble/common/tools/pebble_sdk_platform.py in 4.33.1, where exactly two platforms carry
# it: emery and gabbro. Flint was listed here and is not one of them -- being a Core Devices watch
# is not the same as having a digitizer.
#
# main.c keys APP_TOUCH_CONTROLS off PBL_TOUCH and compiles out every reference to RotaryKit
# without it, so dropping the file here is about not compiling it rather than not linking it: the
# linker discards the unreferenced code either way, and flint's footprint was already identical to
# aplite's, which never had the file at all. A disagreement between this list and PBL_TOUCH is
# therefore quiet in this direction, and a link failure only in the other.
TOUCH_PLATFORMS = ('emery', 'gabbro')


def build(ctx):
    ctx.load('pebble_sdk')

    build_worker = os.path.exists('worker_src')
    binaries = []

    cached_env = ctx.env
    for platform in ctx.env.TARGET_PLATFORMS:
        ctx.env = ctx.all_envs[platform]
        ctx.set_group(ctx.env.PLATFORM_NAME)
        app_elf = '{}/pebble-app.elf'.format(ctx.env.BUILD_DIR)
        sources = ctx.path.ant_glob('src/c/**/*.c')
        if platform not in TOUCH_PLATFORMS:
            sources = [s for s in sources if s.name != 'rotary_kit.c']
        ctx.pbl_build(source=sources, target=app_elf, bin_type='app')

        if build_worker:
            worker_elf = '{}/pebble-worker.elf'.format(ctx.env.BUILD_DIR)
            binaries.append({'platform': platform, 'app_elf': app_elf, 'worker_elf': worker_elf})
            ctx.pbl_build(source=ctx.path.ant_glob('worker_src/c/**/*.c'),
                          target=worker_elf,
                          bin_type='worker')
        else:
            binaries.append({'platform': platform, 'app_elf': app_elf})
    ctx.env = cached_env

    ctx.set_group('bundle')
    ctx.pbl_bundle(binaries=binaries,
                   js=ctx.path.ant_glob(['src/pkjs/**/*.js',
                                         'src/pkjs/**/*.json',
                                         'src/common/**/*.js']),
                   js_entry_file='src/pkjs/index.js')

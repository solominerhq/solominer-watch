"""
Pre-build script: Patch GFX Library for Arduino for ESP32 Arduino Core 2.x compatibility.

GFX Library 1.6.4 uses headers/APIs from Arduino Core 3.x (esp32-hal-periman.h,
esp_rgb_panel_t, etc.) that don't exist in Core 2.x. This project only needs the
QSPI databus and CO5300 display driver, so we can safely stub out the incompatible
source files and guard the problematic header includes.

This script runs at the pre: stage (before every build) and patches the library
in .pio/libdeps/ if needed. Patches are idempotent (marker-checked).
"""
import os

Import("env")

MARKER = "PATCHED_FOR_CORE2X"

def patch_gfx_library():
    project_dir = env.subst("$PROJECT_DIR")
    libdeps = os.path.join(project_dir, ".pio", "libdeps")
    if not os.path.isdir(libdeps):
        return

    # Patch only the active environment. Patching the first directory found
    # left other environments unpatched, making multi-environment builds fail.
    gfx_dir = os.path.join(env.subst("$PROJECT_LIBDEPS_DIR"), env.subst("$PIOENV"), "GFX Library for Arduino")
    if not os.path.isdir(gfx_dir):
        raise RuntimeError("GFX dependency missing; run pio pkg install before building")

    databus_dir = os.path.join(gfx_dir, "src", "databus")
    if not os.path.isdir(databus_dir):
        return

    # ── Stub .cpp files that use Core 3.x-only APIs ──
    for fname in ["Arduino_ESP32RGBPanel.cpp", "Arduino_ESP32SPI.cpp",
                  "Arduino_ESP32SPIDMA.cpp", "Arduino_ESP32DSIPanel.cpp"]:
        fpath = os.path.join(databus_dir, fname)
        if not os.path.isfile(fpath):
            continue
        with open(fpath, "r") as f:
            content = f.read()
        if MARKER in content:
            continue
        with open(fpath, "w") as f:
            f.write("// {}\n#if 0  // Disabled for ESP32 Arduino Core 2.x compatibility\n{}\n#endif\n".format(
                MARKER, content))
        print("[patch_gfx] Stubbed " + fname)

    # ── Guard problematic #includes in Arduino_ESP32SPI.h ──
    spi_h = os.path.join(databus_dir, "Arduino_ESP32SPI.h")
    if os.path.isfile(spi_h):
        with open(spi_h, "r") as f:
            content = f.read()
        if MARKER not in content:
            for inc in ["esp32-hal-periman.h", "esp_private/periph_ctrl.h"]:
                old = '#include "{}"'.format(inc)
                if old in content and '__has_include("{}")'.format(inc) not in content:
                    content = content.replace(old,
                        '#if __has_include("{inc}")\n#include "{inc}"\n#endif'.format(inc=inc))
            with open(spi_h, "w") as f:
                f.write("// {}\n{}".format(MARKER, content))
            print("[patch_gfx] Patched Arduino_ESP32SPI.h includes")

patch_gfx_library()

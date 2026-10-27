"""Keep developer-specific paths out of compiled assertions and debug metadata."""
from pathlib import Path
Import("env")

# GCC uses the last matching prefix. Keep project and PlatformIO filenames
# useful for troubleshooting without publishing the builder's home directory.
mappings = [
    (str(Path.home()), "/build-user"),
    (env.subst("$PROJECT_CORE_DIR"), "/platformio"),
    (env.subst("$PROJECT_DIR"), "/project"),
]
env.Append(CCFLAGS=["-ffile-prefix-map=" + source + "=" + target
                    for source, target in mappings if source and "$" not in source])

# SPDX-License-Identifier: GPL-3.0-or-later

# PlatformIO treats -flto in build_flags as a compiler flag. The final GCC
# invocation also needs LTO and its archive plugin to optimize the fat objects
# produced for Heltec. Apply this after the framework configures its linker.
Import("env")

env.AppendUnique(LINKFLAGS=["-flto", "-fuse-linker-plugin"])

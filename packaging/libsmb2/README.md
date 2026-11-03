# libsmb2 packaging

Both release architectures and the older native Frame builder use libsmb2-6.3,
commit `2bcb014b23a8c8cccf534eea8bda2fe5df453cfd`. The source archive's SHA-256
is pinned in both recipes. The owner approved this dependency and waived its
10-day cooldown on 2026-10-28.

Build a shared library in `lib/`, matching mpv/FFmpeg and allowing replacement
under LGPL-2.1-or-later. Kerberos, GSSAPI and examples are disabled.

Add future patches here as `*.patch`, with `a/` and `b/` paths. Both builders
apply them in filename order with `patch -p1` before CMake configuration, and
include their hashes in the dependency cache key and build records. Include
upstream-compatible licence grants and dated modification notices in patches.
`0001-hardware-aes.patch` reuses expanded AES-128 keys within CMAC/CCM and selects
ARMv8 Crypto Extensions or x86_64 AES-NI at runtime, with the original software
cipher as fallback. It adds no dependency or global CPU compiler flags.

`NOTICE` preserves the library's per-file copyright and crypto notices, alongside
upstream's `COPYING` and `LICENCE-LGPL-2.1.txt`. Matching release source archives
include the full upstream archive, local patches and recipe.

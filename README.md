# ShadowHeap

ShadowHeap is a mitigation layer that reliably prevents most heap-based attacks against glibc.

The `malloc()`/`free()` allocator functions in the C standard library
are easy to use incorrectly.
In vulnerable applications,
attackers can use various methods
as collected in the [how2heap](https://github.com/shellphish/how2heap) repository
to control future allocations,
and e.g. inject fake data or access arbitrary memory.
While there are hardened allocators
such as [DieHard by Emery Berger](https://github.com/emeryberger/DieHard)
or the OpenBSD allocator,
the GNU standard library (glibc) allocator
tends to prioritize performance over strong security.

ShadowHeap can be injected via `LD_PRELOAD` as a mitigation layer between the application and glibc.
It will take snapshots of the internal glibc allocator data structures
and verifies integrity of these snapshots before forwarding allocator function calls to glibc.
Due to injecting the mitigation via dynamic linking,
it is not necessary to recompile the target application.
The overhead of this approach depends on the application, and on enabled mitigation components.

## Mitigation levels

| Level | Abbrev | Name                    |
|-------|--------|-------------------------|
|     1 | PTR    | free protection         |
|     2 | TOP    | topchunk protection     |
|     3 | USB    | unsorted bin protection |
|     4 | TCA    | tcache protection       |

Each level includes the mitigations from the previous level,
so that all levels include free protection.
Higher mitigation levels incur a larger performance penalty,
thus allowing the user to strike their own balance between security and performance.

The mitigations must be enabled during compilation (e.g. `TOP_CHECK=1`),
but can be disabled at runtime
by setting an environment variable (e.g. `SHADOWHEAP_DISABLE_TOPCHECKS=1`).

Performance characteristics of the free protection can be adjusted
by setting the initial size of the hash table that holds the shadow copy
of all in-use memory chunks.
This can be adjusted e.g. as `SHADOWHEAP_SIZE_INITIAL=1024`.

Mitigations for fastbin, smallbin, largebin have not been implemented.

## Usage

Running `make` will compile an assortment of ShadowHeap configurations into the `bin` folder.
This requires the GCC C++ compiler (`g++`).

Then, just preload the desired mitigation configuration
when launching any compatible application:

```bash
$ LD_PRELOAD=$PWD/bin/malloc-shadow-prod.so the-target-application
```

If there is an error such as
“ShadowHeap: Error: ArenaLeak was not initialized correctly! Aborting...”,
this indicates that the layout of internal glibc data structures has changed,
or that the glibc has added build-in mitigations
that make it difficult to take a snapshot of heap metadata.
Please refer to the compatibility section for known-good glibc versions.

Available configurations:

* malloc-shadow-prod.so: production build with all mitigations enabled
* malloc-shadow-prod-level-1.so: production build with level 1 mitigations
* malloc-shadow-prod-level-2.so: production build with level 2 mitigations
* malloc-shadow-prod-level-3.so: production build with level 3 mitigations
* malloc-shadow-prod-level-4.so: production build with level 4 mitigations
  (equivalent to malloc-shadow-prod)
* malloc-shadow-debug.so: build with all mitigations enabled,
  with debugging symbols
* malloc-shadow-verbose.so: build with all mitigations enabled,
  with debugging symbols,
  with logging messages (extremely slow)
* malloc-shadow.so, malloc-hooked.so: builds without any mitigations

## Compatibility

ShadowHeap was tested under glibc versions 2.25, 2.26, 2.27, 2.28, 2.30
and works exclusively on AMD64 (x86-64) systems.

The following allocator functions are wrapped:
`malloc()`, `calloc()`, `realloc()`, `free()`.
The following functions are NOT available:
`posix_memalign()`, `memalign()`, `aligned_alloc()`, `valloc()`, `pvalloc()`.

ShadowHeap is a proof of concept and is not threadsafe.
Furthermore, access to glibc internals necessarily involves undefined behavior (UB).

For glibc versions 2.26 and 2.27, the contents of tcache can only be protected
if a patch to glibc is applied:

Global definitions in `malloc.c`:

```diff
+#ifndef M_GET_RVA_TCACHE_LOWER
+#define M_GET_RVA_TCACHE_LOWER -11
+#endif
+
+#ifndef M_GET_RVA_TCACHE_UPPER
+#define M_GET_RVA_TCACHE_UPPER -12
+#endif

+static int getTcacheRva(int pos) {
+#if USE_TCACHE
+    long ptr = tcache;
+    if (pos == 0) return ptr << 32 >> 32;
+    if (pos == 1) return ptr >> 32;
+#else
+    return 1;
+#endif
+}
```

New cases in `__libc_mallopt()` in `malloc.c`:

```diff
         res = 0;
       break;

+   case M_GET_RVA_TCACHE_LOWER:
+     res = getTcacheRva(0);
+     break;
+
+   case M_GET_RVA_TCACHE_UPPER:
+     res = getTcacheRva(1);
+     break;
+
     case M_TRIM_THRESHOLD:
       do_set_trim_threshold (value);
       break;
```

## Citation

J. Bouché, L. Atkinson, and M. Kappes.
2020.
**Shadow-Heap: Preventing Heap-based Memory Corruptions by Metadata Validation.**
*Accepted for European Interdisciplinary Cybersecurity Conference (EICC 2020)*.
[doi:10.1145/3424954.3424956](https://doi.org/10.1145/3424954.3424956)

## License

ShadowHeap - A mitigation layer to prevent heap-based attacks against glibc

Copyright (C) 2020 Johannes Bouche, Lukas Atkinson

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 2.1 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA

## Acknowledgments

ShadowHeap was developed
by the [Research Group for Network and Information Security](https://www.fg-itsec.de/)
at the Frankfurt University of Applied Sciences.

This work was supported by the German Federal Ministry for Economic Affairs and Energy grant no ZF4131805MS9.

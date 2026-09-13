# Offline snapshot

Project name: `RE3-3DSPort-Complete`

This working snapshot is stored locally and is not configured for publication.
It has no Git remote and must not be pushed until its ownership, history,
licensing, and release policy have been reviewed deliberately.

The offline project copy includes:

- the completed GTA III and Vice City 3DS source trees;
- shared renderer, audio, decoder, libctru, and citro3d sources;
- both maintained `gamefiles` override directories, including the modified GXT,
  particle, controller, texture, and startup-movie files;
- the locked devkitARM r55 / GCC 10.2 toolchain;
- reproducible 3DSX/CIA packaging scripts, tools, banner sources, and assets;
- the final verified CIA and 3DSX files under `release/`.

It deliberately excludes copied PC game installations, converted commercial
archives, build objects, obsolete test packages, and reverse-extraction scratch
directories. Legally owned PC game data must still be supplied during
installation as described in `README.md`.

Generated objects and release binaries are ignored by the local Git repository.
They remain present in the offline folder so the snapshot can be built and used
without relying on a network connection.

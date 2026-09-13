# Finished HOME Menu artwork

Each game has three packaging inputs:

- `.cgfx`: the finished animated banner, with its textures included;
- `.bcwav`: the existing encoded banner sound, copied without re-encoding;
- `-icon.png`: the final 48×48 HOME Menu icon.

The packager combines these with the game ELF files. Blender, pycgfx, raw game
models and the original artwork scenes are not required.

These inputs are published as explicit exceptions to the generated-file
ignore rules. Generated BNR, SMDH, CIA and 3DSX files stay in the ignored
`production_cia/output` directory.

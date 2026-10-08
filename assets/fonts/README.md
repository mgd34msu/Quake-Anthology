# Engine font

`DejaVuSans.ttf` is the unmodified DejaVu Sans 2.37 font from the
[official 2.37 release](https://github.com/dejavu-fonts/dejavu-fonts/releases/tag/version_2_37),
archive `dejavu-sans-ttf-2.37.zip`, entry
`dejavu-sans-ttf-2.37/ttf/DejaVuSans.ttf`.

`LICENSE-DejaVu.txt` contains the archive's complete, unmodified license.
Ship both files in `engine-data/fonts` beside the executable. The frontend
mounts this directory through the shared VFS. `--font-directory` selects an
explicit alternative directory.

# Old Director XMED Text Renderer

This is a standalone rendering probe for old Director XMED text chunks. It is
intentionally kept out of the C++ runtime so font parsing and rasterization can
be tested before changing the root project.

The parser extracts the text payload, font table, style runs, and Packer-encoded
style records. Text color comes from the selected XMED style record's authored
foreground color values. When movie sprite context is supplied, the renderer
then applies the same final color rule used by `SpriteBaker`: background
transparent Volter-style XMED with authored white text uses the sprite
foreground color.

Render the GoldFish private-login label fixtures:

```sh
python3 tools/render/old_director/xmed/render_xmed_text.py --preset gf_private_login_labels
```

Render a single XMED chunk:

```sh
python3 tools/render/old_director/xmed/render_xmed_text.py \
  --xmed /path/to/member_XMED.bin \
  --width 206 \
  --height 12 \
  --ink 36 \
  --fore-color 000000
```

Outputs are written to `tools/render/old_director/xmed/out/` as `.png` files
with matching `.json` metadata. `parsed` records the authored XMED color;
`resolvedColor` records the final movie-rendered color. Use `--background`
only for preview contrast.

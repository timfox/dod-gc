# Exporting DoD assets to USD (Maya)

`tools/goldsrc_to_usd.py` converts retail Day of Defeat (GoldSrc) assets to USD so they can be opened and edited in Maya. It only reads the source files.

Needs: `pip install usd-core numpy pillow`

```
# everything in a dod folder (models, weapons, players, props, sprites, maps)
python3 tools/goldsrc_to_usd.py all /path/to/Half-Life/dod -o ~/dod-usd

# or piece by piece
python3 tools/goldsrc_to_usd.py mdl models/player/us-inf/us-inf.mdl -o out
python3 tools/goldsrc_to_usd.py spr sprites/explosion1.spr -o out
python3 tools/goldsrc_to_usd.py bsp maps/dod_charlie.bsp --wad dod_charlie.wad -o out
```

## What you get

| Source | Output |
| --- | --- |
| MDL (soldiers, v_/p_/w_ weapons, props) | `models/<path>/<name>/<name>.usdc` plus `textures/*.png` |
| SPR | `sprites/<name>/<name>.usdc` plus `frames/frame_NN.png` |
| BSP | `maps/<map>/<map>.usdc` plus `textures/*.png` (embedded + all WADs in the dod folder) |

MDL details: UsdSkel skeleton (bind and rest transforms), one Mesh per body-part sub-model with GeomSubsets per texture and a UsdPreviewSurface material, single-joint (rigid) skin weights as GoldSrc stores them, one SkelAnimation per sequence under `Animations/` (blend sequences get `_bN` suffixes; the first sequence is bound as the default animation). Alternate sub-models (extra helmets, heads) are present but hidden; unhide them in Maya. Texture flags, bone controllers, skin families and animation events are kept as `goldsrc:*` customData on the prims.

Units are GoldSrc units (about one inch, stage `metersPerUnit` 0.0254) and the stage is Z-up; Maya's import converts the up axis. Player animations are authored around the hull centre, so a player animations are shifted per sequence so the feet stand on the origin like the rest pose.

## Opening in Maya

1. Enable the plugin: Windows > Settings/Preferences > Plug-in Manager > `mayaUsdPlugin`.
2. File > Import (or Open) and choose the `.usdc`; turn on skeletons and animation in the import options. To keep editing as USD, use Create > Universal Scene Description (USD) > Stage From File instead.
3. If textures do not show, check the viewport is in textured mode with Viewport 2.0.

Limits: external sequence group files (`name01.mdl`) are read when present; brush entities are placed from their `origin`/`angles` (pivots of origin-brush rotating doors are not applied) and `env_model`-style entities reference the converted MDL; lightmaps and sprite blend modes are not converted. Map textures are searched in the dod WADs and the sibling `valve` folder.

## Licensing

The converted files are derived from retail DoD assets. Keep the output local and private; do not commit it to a public repo.

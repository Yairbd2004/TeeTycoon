# TeeTycoon map edits

The server ships with two versions of the supplied block maps:

- `blmapV3ROYAL-TT` is the default.
- `Copy Love Box-TT` is available as an alternate map.

Each map keeps its original race and block layout. The existing freeze, teleporter, front, and speedup layers are preserved; the new rooms sit in empty areas of the original maps. Each one has five private houses, from a compact starter home through a large palace, plus a separate VIP room. The house shells grow in size and detail as the house level rises, and their art uses the map's own tilesets. The farm strips grow with the house level as well.

The house exits use tele-out number 254. TeeTycoon reserves that number so it won't mix with the map's existing race or event teleports. The five house exits are ordered left to right, matching `/home 0` through `/home 4`.

The edited maps are committed under `TeeTycoon/data/maps`; their DDNet 0.7 conversions are under `TeeTycoon/data/maps7`. CMake stages both maps into the server and client data folders.

`edit_reference_maps.py` can recreate the edits if the original files are present at `map_examples/blmapV3ROYAL.map` and `map_examples/Copy Love Box.map`. It preserves the source maps and writes the TeeTycoon copies. It needs Python 3, NumPy, and the small DATA-v4 helper in `ddmap.py`. After editing, use DDNet's `map_convert_07` to refresh the two 0.7 copies.

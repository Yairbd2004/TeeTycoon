# TeeTycoonV1 map

`TeeTycoon/data/maps/TeeTycoonV1.map` is the editable DDNet map. Its artwork is embedded, so the server and clients do not need a separate tileset file. The server config selects this map by default. The `maps7` copy is for 0.7 clients.

The map has a lobby, an open block field with side and floor freeze pockets, a race route below it, a VIP lounge, five event rooms, and five private houses. The block field uses the broad spacing and short platforms seen in the supplied reference maps; its layout and artwork are original.

| House level | Room | Farm strip |
| --- | --- | ---: |
| 0 | Shack | 8 tiles |
| 1 | Loft | 13 tiles |
| 2 | Suite | 19 tiles |
| 3 | Court | 25 tiles |
| 4 | Palace | 37 tiles |

House exits all use teleporter number 2, in left to right order on the same row. The server's `/home` command indexes these exits by house level. Teleporter 1 returns the public race to the lobby; 3 enters the race from the lobby. The five event rooms use teleporter numbers 254 through 250 for Survival, Race, Deathmatch, Freeze Race, and FNG.

To rebuild the map, run `python map_design/build_teetycoon_v1.py` from the repository root with Pillow installed. The script writes the map, embedded atlas source, and two design previews. Run DDNet's `map_convert_07` on the result to update `TeeTycoon/data/maps7/TeeTycoonV1.map`.

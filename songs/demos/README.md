# Demo songs

The demo songs built into the player's firmware. Every MIDI file here comes from the
[Mutopia Project](https://www.mutopiaproject.org), and its page there marks it **Public Domain**
(most also carry the "CC0 – no rights reserved" mark). No attribution is required; this list is
here so the source of each file stays known.

| Song on the player | Piece | Mutopia page |
|---|---|---|
| Fur Elise | Beethoven – Für Elise, WoO 59 | [piece 931](https://www.mutopiaproject.org/cgibin/piece-info.cgi?id=931) |
| Rondo alla Turca | Mozart – Piano Sonata K. 331, 3rd mvt. | [piece 108](https://www.mutopiaproject.org/cgibin/piece-info.cgi?id=108) |
| The Entertainer | Joplin – The Entertainer | [piece 263](https://www.mutopiaproject.org/cgibin/piece-info.cgi?id=263) |
| Maple Leaf Rag | Joplin – Maple Leaf Rag | [piece 23](https://www.mutopiaproject.org/cgibin/piece-info.cgi?id=23) |
| Mountain King | Grieg – In the Hall of the Mountain King, Op. 46 (piano) | [piece 1888](https://www.mutopiaproject.org/cgibin/piece-info.cgi?id=1888) |
| Minuet in G | Petzold (attr. Bach) – Minuet in G, BWV Anh. 114 | [piece 75](https://www.mutopiaproject.org/cgibin/piece-info.cgi?id=75) |
| Invention No 8 | Bach – Invention No. 8 in F, BWV 779 | [piece 61](https://www.mutopiaproject.org/cgibin/piece-info.cgi?id=61) |
| Ode to Joy | Beethoven – Ode to Joy (SATB) | [piece 528](https://www.mutopiaproject.org/cgibin/piece-info.cgi?id=528) |
| Gymnopedie No 1 | Satie – Gymnopédie No. 1 | [piece 37](https://www.mutopiaproject.org/cgibin/piece-info.cgi?id=37) |
| Eine kleine Nachtmusik | Mozart – Serenade K. 525, 1st mvt. | [piece 900](https://www.mutopiaproject.org/cgibin/piece-info.cgi?id=900) |
| Greensleeves | Traditional – Greensleeves (accordion) | [piece 1265](https://www.mutopiaproject.org/cgibin/piece-info.cgi?id=1265) |

## Changing the demos

1. Put or remove `.mid` files here. The file name becomes the song name on the player
   (1–23 characters: letters, digits, space, `- _ ( ) .`). Only add files you may publish.
2. `tools\convert_demos.ps1` converts each `.mid` to a `.stepper` next to it, with the web
   page's converter and the Convert card's default settings.
3. `tools\build_demos.ps1` packs the `.stepper` files into
   `esp8266_player/src/music/EmbeddedSongs.cpp`.
4. Compile and flash the ESP8266, then press "Restore demo songs" on the page (new demos are
   only copied automatically on the very first start).

Your own songs that may not be published belong in `songs/private/`, which git ignores.

# Roadie room artwork

`iron-medallion.png` is original artwork generated with the built-in ImageGen
tool on September 27, 2026. The full prompt is in `generation-prompt.txt`.
It is used as a mipmapped, world-anchored circular stone-floor inlay in the
opening room. The room geometry, lighting and materials are implemented in
`src/openxr_host/roadie_room.h` and `eddie_lobby.cpp`. The artwork contains no text.

`basalt.png` is an original ground/stone material generated with the same
built-in tool and date. Its prompt is in `basalt-prompt.txt`. Both images use
sRGB sampling, generated mipmaps and anisotropic filtering in the room renderer.

# Amalthea horn of plenty

Amalthea nursed Zeus; in the tradition where she is the goat herself, one of her
horns became the cornucopia, the horn of plenty. The mark is that horn, stylised:
a ribbed, curling horn whose growth rings read as rows and whose curve reads as a
crescent (Amalthea is also Jupiter's red moon), pouring from its ember mouth a fan
of uneven rays, a burst of plenty that is also a spectrum. The system offers plenty.

Appliances are identified by what they do: Tracker by a stylised pattern screen
(hex columns with a highlighted cursor row), FX by a pedalboard of three stomp
boxes joined by patch cables. Mint and violet distinguish them on the home screen;
the Tracker top bar draws its icon in the current track colour.

The angular Oxanium SemiBold typography is the runtime atlas, including the instrument UI.
Source: https://github.com/google/fonts/tree/main/ofl/oxanium (OFL license in
`assets/Oxanium-OFL.txt`). The atlas is generated at build-development time by
`tools/make_font.py`; the appliance needs no font renderer or UI framework.

`src/brand.cpp` draws the native geometry with anti-aliased polygon fills. The SVG files
are exported from that same geometry; `ready.png` is the matching 1024×768 boot image.
`render_identity OUTPUT_DIRECTORY` recreates splash and home PPMs and the three SVGs.
Artwork is original to this project under its license. The application draws its
identity without loading PNGs.

;
; The names for city tiles are not free and must follow the following rules.
; The names consists of 'style name' + '_' + 'index'. The style name is as
; specified in cities.ruleset file and the index only defines the read order
; of the images. The definitions are read starting with index 0 till the first
; missing value The index is checked against the city bonus of effect
; City_Image and the resulting image is used to draw the city on the tile.
;
; Obviously the first tile must be 'style_name'_city_0 and the sizes must be
; in ascending order. There must also be a 'style_name'_wall_0 tile used
; for the default wall graphics and an occupied tile to indicate
; a military units in a city.
; For providing multiple walls buildings (as requested by the "Visible_Walls"
; effect value) tags are 'style_name'_bldg_'effect_value'_'index'.
; The maximum number of images is only limited by the maximum size of a city
; (currently MAX_CITY_SIZE = 255).
;
; For providing custom citizen icons for the city style, use tags of the form
; 'citizen.<tag>.<citizen_type>_<index>'
; where <tag> is citizens_graphic tag from the styles.ruleset,
; <citizen_type> is type like 'content', same ones as
; misc/small.spec has for the default citizen icons, and
; <index> is a running number for alternative sprites.
;

[spec]

; Format and options of this spec file:
options = "+Freeciv-3.1-spec"

[info]

artists = "
    Jerzy Klek <jekl@altavista.net>

    european style based on trident tileset by
    Tatu Rissanen <tatu.rissanen@hut.fi>
    Marco Saupe <msaupe@saale-net.de> (reworked classic, industrial and modern)
"

[file]
gfx = "isotrident/morecities"

[grid_main]

x_top_left = 0
y_top_left = 0
dx = 64
dy = 48

tiles = { "row", "column", "tag"
;
; city tiles
;

 0,  0, "city.asian_city_0"
 0,  1, "city.asian_city_1"
 0,  2, "city.asian_city_2"
 ;1,  3, "city.asian_wall"
 0,  4, "city.asian_occupied_0"
 0,  5, "city.asian_wall_0"
 0,  6, "city.asian_wall_1"
 0,  7, "city.asian_wall_2"


 1,  0, "city.tropical_city_0"
 1,  1, "city.tropical_city_1"
 1,  2, "city.tropical_city_2"
 ;5,  3, "city.tropical_wall"
 1,  4, "city.tropical_occupied_0"
 1,  5, "city.tropical_wall_0"
 1,  6, "city.tropical_wall_1"
 1,  7, "city.tropical_wall_2"

}

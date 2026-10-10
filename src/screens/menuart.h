/* menuart.h -- the menu's art: its nine textures and the panel quad the
 * score overlay shares.  Menu_BuildMenuGeometry loads and builds them once per
 * device (menuscreens.cpp); app owns the one MenuArt and passes it down to
 * the screens that draw it. */
#pragma once

#include "texture.h"
#include "renderdevice.h"

struct MenuArt {
    Texture tex1;             // menu_1.tga: the main menu and level-select panel
    Texture tex2;             // menu_2.tga: the options page and the high-score panel
    Texture tex3;             // menu_3.tga: the video options and the controls page
    Texture tex4;             // menu_4.tga: the save-slot list and the sound options
    Texture knob;             // drehknopf.tga
    Texture off;              // knopf_aus.tga
    Texture on;               // knopf_ein.tga
    Texture scale;            // scale.tga
    Texture selector;         // selector.tga: the cursor markers
    ScreenVertex panelQuad[4]{};
};

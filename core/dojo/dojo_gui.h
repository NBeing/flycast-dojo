#pragma once

#include "emulator.h"

#include "rend/gui.h"
#include "imgui/imgui.h"

class DojoGui
{
public:
    void gui_display_disconnected();
};

extern DojoGui dojo_gui;

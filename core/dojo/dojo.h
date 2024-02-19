#pragma once

#include <iostream>
#include <string>

#include "cfg/option.h"
#include "emulator.h"

class Dojo
{
public:
    void AssignPlayerNames();

    bool hosting;
    std::string player_1;
    std::string player_2;
};

extern Dojo dojo;

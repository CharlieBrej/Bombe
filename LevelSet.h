#pragma once
#include "Misc.h"
#include "SaveState.h"

#include <vector>

class LevelSet
{
public:
    std::string name;
    std::vector<std::string> levels;

    LevelSet(){};
    LevelSet(SaveObjectMap* omap);
    SaveObject* save();

    static void init_global();
    static void save_global();
};

extern std::vector<LevelSet*> global_level_sets;

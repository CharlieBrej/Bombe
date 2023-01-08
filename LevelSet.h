#pragma once
#include "Misc.h"
#include "SaveState.h"
#include <vector>

#define GLBAL_LEVEL_SETS 3

class LevelSet
{
public:
    std::vector<std::string> levels;

    LevelSet(){};
    LevelSet(SaveObjectMap* omap);
    SaveObject* save();

    static void init_global();
    static void save_global();
    static void delete_global();
};

extern std::vector<LevelSet*> global_level_sets[GLBAL_LEVEL_SETS];

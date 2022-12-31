#include "LevelSet.h"
#include "SaveState.h"
#include <iostream>
#include <fstream>
#include <algorithm>

std::vector<LevelSet*> global_level_sets;

LevelSet::LevelSet(SaveObjectMap* omap)
{
    SaveObjectList* rlist = omap->get_item("levels")->get_list();

    for (int i = 0; i < rlist->get_count(); i++)
    {
        std::string s = rlist->get_string(i);
        levels.push_back(rlist->get_string(i));
    }
}

SaveObject* LevelSet::save()
{
    SaveObjectMap* omap = new SaveObjectMap;

    SaveObjectList* rlist = new SaveObjectList;
    for (const std::string& level : levels)
    {
        rlist->add_string(level);
    }
    omap->add_item("levels", rlist);

    return omap;
}

void LevelSet::init_global()
{
    if (!global_level_sets.empty())
        return;
    std::ifstream loadfile("levels.json");
    SaveObjectMap* omap = SaveObject::load(loadfile)->get_map();
    SaveObjectList* rlist = omap->get_item("level_sets")->get_list();

    delete_global();
    for (int i = 0; i < rlist->get_count(); i++)
    {
        LevelSet *lset = new LevelSet(rlist->get_item(i)->get_map());
        global_level_sets.push_back(lset);
    }
    delete omap;
}
void LevelSet::delete_global()
{
    for (LevelSet* level_set : global_level_sets)
    {
        delete level_set;
    }
    global_level_sets.clear();
}

void LevelSet::save_global()
{
    SaveObjectMap* omap = new SaveObjectMap;

    SaveObjectList* rlist = new SaveObjectList;
    for (LevelSet* level_set : global_level_sets)
    {
        rlist->add_item(level_set->save());
    }
    omap->add_item("level_sets", rlist);

    std::ofstream outfile ("levels.json");
    omap->save(outfile);
    delete omap;
}

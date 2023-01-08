#include "LevelSet.h"
#include "Grid.h"
#include "SaveState.h"
#include <iostream>
#include <fstream>
#include <algorithm>

std::vector<LevelSet*> global_level_sets[GLBAL_LEVEL_SETS];

LevelSet::LevelSet(SaveObjectMap* omap)
{
    SaveObjectList* rlist = omap->get_item("levels")->get_list();

    for (int i = 0; i < rlist->get_count(); i++)
    {
        std::string s = rlist->get_string(i);
        Grid* grid = Grid::Load(s);
        s = grid->to_string();
        levels.push_back(s);
        delete grid;
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
    if (!global_level_sets[1].empty())
        return;
    std::ifstream loadfile("levels.json");
    SaveObjectMap* omap = SaveObject::load(loadfile)->get_map();
    SaveObjectList* llist = omap->get_item("level_sets")->get_list();
    delete_global();

    for (int j = 0; (j < llist->get_count()) && (j < GLBAL_LEVEL_SETS); j++)
    {
        SaveObjectList* rlist = llist->get_item(j)->get_list();
        for (int i = 0; i < rlist->get_count(); i++)
        {
            LevelSet *lset = new LevelSet(rlist->get_item(i)->get_map());
            global_level_sets[j].push_back(lset);
        }
    }
    delete omap;
}
void LevelSet::delete_global()
{
    for (int i = 0; i < GLBAL_LEVEL_SETS; i++)
    {
        for (LevelSet* level_set : global_level_sets[i])
        {
            delete level_set;
        }
        global_level_sets[i].clear();
    }
}

void LevelSet::save_global()
{
    SaveObjectMap* omap = new SaveObjectMap;
    SaveObjectList* llist = new SaveObjectList;

    for (int i = 0; i < GLBAL_LEVEL_SETS; i++)
    {
        SaveObjectList* rlist = new SaveObjectList;
        for (LevelSet* level_set : global_level_sets[i])
        {
            rlist->add_item(level_set->save());
        }
        llist->add_item(rlist);
    }

    omap->add_item("level_sets", llist);
    std::ofstream outfile ("levels.json");
    omap->save(outfile);
    delete omap;
}

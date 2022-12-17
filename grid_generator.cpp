#include "Grid.h"
#include "LevelSet.h"

int main( int argc, char* argv[] )
{
    LevelSet::init_global();

    int params[][6] = {
      // cnt  x  y  +- x/y
       { 100,   3, 3, 0, 0,  0},
       { 100,   3, 3, 1, 0,  0},
       { 100,   3, 3, 1, 1,  0},
       { 100,   3, 3, 1, 1,  1},
       { 000,   3, 3, 1, 1,  2},
       { 100,   4, 4, 0, 0,  0},
       { 100,   4, 4, 1, 0,  0},
       { 100,   4, 4, 1, 1,  0},
       { 100,   4, 4, 1, 1,  1},
       { 000,   4, 4, 1, 1,  2},
       { 100,   5, 5, 0, 0,  0},
       { 100,   5, 5, 1, 0,  0},
       { 100,   5, 5, 1, 1,  0},
       { 100,   5, 5, 1, 1,  1},
       { 000,   5, 5, 1, 1,  2},
       { 100,   6, 6, 0, 0,  0},
       { 100,   6, 6, 1, 0,  0},
       { 100,   6, 6, 1, 1,  0},
       { 100,   6, 6, 1, 1,  1},
       { 000,   6, 6, 1, 1,  2},
       { 100,   7, 7, 0, 0,  0},
       { 100,   7, 7, 1, 0,  0},
       { 100,   7, 7, 1, 0,  0},
       { 100,   7, 7, 1, 1,  1},
       { 000,   7, 7, 1, 1,  2},
       { 100,   8, 8, 0, 0,  0},
       { 100,   8, 8, 1, 0,  0},
       { 100,   8, 8, 1, 1,  0},
       { 100,   8, 8, 1, 1,  1},
       { 000,   8, 8, 1, 1,  2},
       { 100,   9, 9, 0, 0,  0},
       { 100,   9, 9, 1, 0,  0},
       { 100,   9, 9, 1, 1,  0},
       { 100,   9, 9, 1, 1,  1},
       { 000,   9, 9, 1, 1,  2},
       { -1,    3, 3, 0, 0,  0}
   };

    for (int i = 0; params[i][0] >= 0; i++)
    {
        if (global_level_sets.size() <= i)
            global_level_sets.push_back(new LevelSet());
        while (global_level_sets[i]->levels.size() < params[i][0])
        {
            Grid* g = new Grid (XYPos(params[i][1], params[i][2]));
            g->make_harder(params[i][3], params[i][4], params[i][5]);
            std::string s = g->to_string();
            std::vector<std::string> &levels = global_level_sets[i]->levels;
            if(std::find(levels.begin(), levels.end(), s) == levels.end())
                global_level_sets[i]->levels.push_back(s);
            delete g;
            LevelSet::save_global();
        }

    }
    LevelSet::save_global();

    return 0;
}

#include "Grid.h"
#include "LevelSet.h"

int main( int argc, char* argv[] )
{
    LevelSet::init_global();

    int params[][7] = {
      // cnt    x  y  +- x/y
       { 200,   3, 3, 0, 0,  0, 0},
       { 200,   3, 3, 1, 0,  0, 0},
       { 200,   3, 3, 1, 1,  0, 0},
       { 200,   3, 3, 1, 1,  1, 0},
       { 200,   3, 3, 1, 1,  1, 50},
       { 200,   4, 4, 0, 0,  0, 0},
       { 200,   4, 4, 1, 0,  0, 0},
       { 200,   4, 4, 1, 1,  0, 0},
       { 200,   4, 4, 1, 1,  1, 0},
       { 200,   4, 4, 1, 1,  1, 50},
       { 200,   5, 5, 0, 0,  0, 0},
       { 200,   5, 5, 1, 0,  0, 0},
       { 200,   5, 5, 1, 1,  0, 0},
       { 200,   5, 5, 1, 1,  1, 0},
       { 200,   5, 5, 1, 1,  1, 50},
       { 200,   6, 6, 0, 0,  0, 0},
       { 200,   6, 6, 1, 0,  0, 0},
       { 200,   6, 6, 1, 1,  0, 0},
       { 200,   6, 6, 1, 1,  1, 0},
       { 200,   6, 6, 1, 1,  1, 50},
       { 50,   7, 7, 0, 0,  0, 0},
       { 50,   7, 7, 1, 0,  0, 0},
       { 50,   7, 7, 1, 0,  0, 0},
       { 50,   7, 7, 1, 1,  1, 0},
       { 50,   7, 7, 1, 1,  1, 50},
       { 50,   8, 8, 0, 0,  0, 0},
       { 50,   8, 8, 1, 0,  0, 0},
       { 50,   8, 8, 1, 1,  0, 0},
       { 50,   8, 8, 1, 1,  1, 0},
       { 50,   8, 8, 1, 1,  1, 50},
       { 50,   9, 9, 0, 0,  0, 0},
       { 50,   9, 9, 1, 0,  0, 0},
       { 50,   9, 9, 1, 1,  0, 0},
       { 50,   9, 9, 1, 1,  1, 0},
       { 50,   9, 9, 1, 1,  1, 100},
       { -1,    3, 3, 0, 0,  0}
   };

    for (int i = 0; params[i][0] >= 0; i++)
    {
        if (global_level_sets.size() <= i)
            global_level_sets.push_back(new LevelSet());
        while (global_level_sets[i]->levels.size() < params[i][0])
        {
            Grid* g = new Grid (XYPos(params[i][1], params[i][2]));
            g->make_harder(params[i][3], params[i][4], params[i][5], params[i][6]);
            std::string s = g->to_string();
            std::vector<std::string> &levels = global_level_sets[i]->levels;
            if(std::find(levels.begin(), levels.end(), s) == levels.end())
                global_level_sets[i]->levels.push_back(s);
            delete g;
            LevelSet::save_global();
            printf("got\n");
        }

    }
    LevelSet::save_global();

    return 0;
}

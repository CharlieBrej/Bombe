#include "Grid.h"
#include "LevelSet.h"

int main( int argc, char* argv[] )
{
    LevelSet::init_global();

    int params[][8] = {
      // cnt    x  y  0,  +- x/y
       { 200,   3, 3, 0,  1, 0,  0, 0},
       { 200,   3, 3, 0,  1, 1,  0, 0},
       { 200,   3, 3, 0,  0, 0,  0, 0},
       { 200,   3, 3, 0,  1, 1,  1, 0},
       { 200,   3, 3, 0,  1, 1,  1, 50},
       { 200,   4, 4, 0,  0, 0,  0, 0},
       { 200,   4, 4, 0,  1, 0,  0, 0},
       { 200,   4, 4, 0,  1, 1,  0, 0},
       { 200,   4, 4, 0,  1, 1,  1, 0},
       { 200,   4, 4, 0,  1, 1,  1, 50},
       { 200,   5, 5, 0,  0, 0,  0, 0},
       { 200,   5, 5, 0,  1, 0,  0, 0},
       { 200,   5, 5, 0,  1, 1,  0, 0},
       { 200,   5, 5, 0,  1, 1,  1, 0},
       { 200,   5, 5, 0,  1, 1,  1, 50},
       { 200,   6, 6, 0,  0, 0,  0, 0},
       { 200,   6, 6, 0,  1, 0,  0, 0},
       { 200,   6, 6, 0,  1, 1,  0, 0},
       { 200,   6, 6, 0,  1, 1,  1, 0},
       { 200,   6, 6, 0,  1, 1,  1, 50},
       { 200,   7, 7, 2,  0, 0,  0, 0},
       { 200,   7, 7, 2,  1, 0,  0, 0},
       { 200,   7, 7, 2,  1, 0,  0, 0},
       { 200,   7, 7, 2,  1, 1,  1, 0},
       { 200,   7, 7, 2,  1, 1,  1, 50},
       { 200,   8, 8, 3,  0, 0,  0, 0},
       { 200,   8, 8, 3,  1, 0,  0, 0},
       { 200,   8, 8, 3,  1, 1,  0, 0},
       { 200,   8, 8, 3,  1, 1,  1, 0},
       { 200,   8, 8, 3,  1, 1,  1, 50},
//          { 200,  6, 4, 0,  0, 0,  0, 100},
          // { 200,   4, 3, 0,  1, 0,  0, 0},
          // { 200,   4, 3, 0,  1, 1,  0, 0},
          // { 200,   4, 3, 0,  1, 1,  1, 0},
          // { 200,   4, 3, 0,  1, 1,  1, 0},
          // { 200,   6, 4, 0,  0, 0,  0, 0},
          // { 200,   6, 4, 0,  1, 0,  0, 0},
          // { 200,   6, 4, 0,  1, 1,  0, 0},
          // { 200,   6, 4, 0,  1, 1,  1, 0},
          // { 200,   6, 4, 0,  1, 1,  1, 0},
          // { 200,   8, 5, 0,  0, 0,  0, 0},
          // { 200,   8, 5, 0,  1, 0,  0, 0},
          // { 200,   8, 5, 0,  1, 1,  0, 0},
          // { 200,   8, 5, 0,  1, 1,  1, 0},
          // { 200,   8, 5, 0,  1, 1,  1, 0},
           { -1,    3, 3, 0,  0, 0,  0, 0}
   };
    int i;
    for (i = 0; params[i][0] >= 0; i++)
    {
        if (global_level_sets.size() <= i)
            global_level_sets.push_back(new LevelSet());
        while (global_level_sets[i]->levels.size() < params[i][0])
        {
            printf("%d of %d\n", global_level_sets[i]->levels.size(), params[i][0]);
            Grid* g = new SquareGrid ();
            g->randomize(XYPos(params[i][1], params[i][2]), params[i][3], params[i][7]);

            g->make_harder(params[i][4], params[i][5], params[i][6]);
            std::string s = g->to_string();
            std::vector<std::string> &levels = global_level_sets[i]->levels;
            if(std::find(levels.begin(), levels.end(), s) == levels.end())
                global_level_sets[i]->levels.push_back(s);
            delete g;
            LevelSet::save_global();
            printf("got\n");
        }
        global_level_sets[i]->levels.resize(params[i][0]);
    }
    global_level_sets.resize(i);
    LevelSet::save_global();

    return 0;
}

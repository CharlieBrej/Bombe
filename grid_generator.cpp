#include "Grid.h"
#include "LevelSet.h"

int main( int argc, char* argv[] )
{
    LevelSet::init_global();

    int params[][9] = {
      // cnt shp   x  y  mrg +- x/y msc rows
       {   0, 0,   3, 3, 0,  1, 0,  0, 0},

       { 200, 1,   3, 3, 0,  1, 0,  0, 0},
       { 200, 1,   3, 3, 0,  1, 1,  0, 0},
       { 200, 1,   3, 3, 0,  0, 0,  0, 0},
       { 200, 1,   3, 3, 0,  1, 1,  1, 0},
       { 200, 1,   3, 3, 0,  1, 1,  1, 50},
       { 200, 1,   4, 4, 0,  0, 0,  0, 0},
       { 200, 1,   4, 4, 0,  1, 0,  0, 0},
       { 200, 1,   4, 4, 0,  1, 1,  0, 0},
       { 200, 1,   4, 4, 0,  1, 1,  1, 0},
       { 200, 1,   4, 4, 0,  1, 1,  1, 50},
       { 200, 1,   5, 5, 0,  0, 0,  0, 0},
       { 200, 1,   5, 5, 0,  1, 0,  0, 0},
       { 200, 1,   5, 5, 0,  1, 1,  0, 0},
       { 200, 1,   5, 5, 0,  1, 1,  1, 0},
       { 200, 1,   5, 5, 0,  1, 1,  1, 50},
       { 200, 1,   6, 6, 0,  0, 0,  0, 0},
       { 200, 1,   6, 6, 0,  1, 0,  0, 0},
       { 200, 1,   6, 6, 0,  1, 1,  0, 0},
       { 200, 1,   6, 6, 0,  1, 1,  1, 0},
       { 200, 1,   6, 6, 0,  1, 1,  1, 50},
       { 200, 1,   7, 7, 2,  0, 0,  0, 0},
       { 200, 1,   7, 7, 2,  1, 0,  0, 0},
       { 200, 1,   7, 7, 2,  1, 0,  0, 0},
       { 200, 1,   7, 7, 2,  1, 1,  1, 0},
       { 200, 1,   7, 7, 2,  1, 1,  1, 50},
       { 200, 1,   8, 8, 3,  0, 0,  0, 0},
       { 200, 1,   8, 8, 3,  1, 0,  0, 0},
       { 200, 1,   8, 8, 3,  1, 1,  0, 0},
       { 200, 1,   8, 8, 3,  1, 1,  1, 0},
       { 200, 1,   8, 8, 3,  1, 1,  1, 50},

       {   0, 2,   3, 3, 0,  0, 0,  0, 0},

       {  -1, -1,  3, 3, 0,  0, 0,  0, 0}
   };
    int i;


    for (int j = 0; j < GLBAL_LEVEL_SETS; j++)
    {
        int cnt = 0;
        for (i = 0; params[i][0] >= 0; i++)
        {
            if (params[i][1] != j)
                continue;
            if (global_level_sets[j].size() <= i)
                global_level_sets[j].push_back(new LevelSet());
            while (global_level_sets[j][cnt]->levels.size() < params[i][0])
            {
                printf("%d of %d\n", global_level_sets[j][cnt]->levels.size(), params[i][0]);
                Grid* g = new SquareGrid ();
                g->randomize(XYPos(params[i][2], params[i][3]), params[i][4], params[i][8]);

                g->make_harder(params[i][5], params[i][6], params[i][7]);
                std::string s = g->to_string();
                std::vector<std::string> &levels = global_level_sets[j][cnt]->levels;
                if(std::find(levels.begin(), levels.end(), s) == levels.end())
                    global_level_sets[j][cnt]->levels.push_back(s);
                delete g;
                LevelSet::save_global();
                printf("got\n");
            }
            global_level_sets[j][cnt]->levels.resize(params[i][0]);
            cnt++;
        }
        global_level_sets[j].resize(cnt);
        LevelSet::save_global();
    }
    LevelSet::save_global();

    return 0;
}

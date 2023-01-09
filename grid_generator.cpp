#include "Grid.h"
#include "LevelSet.h"

pthread_mutex_t glob_mutex;

void* exec(void* dummy)
{
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

       { 200, 2,   3, 2, 0,  0, 0,  0, 0},
       { 200, 2,   3, 2, 0,  1, 0,  0, 0},
       { 200, 2,   3, 2, 0,  1, 1,  0, 0},
       { 200, 2,   3, 2, 0,  1, 1,  1, 0},
       { 200, 2,   3, 2, 0,  1, 1,  1, 50},

       { 200, 2,   4, 3, 0,  0, 0,  0, 0},
       { 200, 2,   4, 3, 0,  1, 0,  0, 0},
       { 200, 2,   4, 3, 0,  1, 1,  0, 0},
       { 200, 2,   4, 3, 0,  1, 1,  1, 0},
       { 200, 2,   4, 3, 0,  1, 1,  1, 50},

       { 200, 2,   6, 4, 0,  0, 0,  0, 0},
       { 200, 2,   6, 4, 0,  1, 0,  0, 0},
       { 200, 2,   6, 4, 0,  1, 1,  0, 0},
       { 200, 2,   6, 4, 0,  1, 1,  1, 0},
       { 200, 2,   6, 4, 0,  1, 1,  1, 50},

       { 200, 2,   8, 5, 0,  0, 0,  0, 0},
       { 200, 2,   8, 5, 0,  1, 0,  0, 0},
       { 200, 2,   8, 5, 0,  1, 1,  0, 0},
       { 200, 2,   8, 5, 0,  1, 1,  1, 0},
       { 200, 2,   8, 5, 0,  1, 1,  1, 50},

       { 200, 2,   9, 6, 0,  0, 0,  0, 0},
       { 200, 2,   9, 6, 0,  1, 0,  0, 0},
       { 200, 2,   9, 6, 0,  1, 1,  0, 0},
       { 200, 2,   9, 6, 0,  1, 1,  1, 0},
       { 200, 2,   9, 6, 0,  1, 1,  1, 50},

       { 200, 2,  11, 7, 0,  0, 0,  0, 0},
       { 200, 2,  11, 7, 0,  1, 0,  0, 0},
       { 200, 2,  11, 7, 0,  1, 1,  0, 0},
       { 200, 2,  11, 7, 0,  1, 1,  1, 0},
       { 200, 2,  11, 7, 0,  1, 1,  1, 50},

       {  -1, -1,  3, 3, 0,  0, 0,  0, 0}
    };
    int i;
    pthread_mutex_lock(&glob_mutex);



    for (int j = 0; j < GLBAL_LEVEL_SETS; j++)
    {
        int cnt = 0;
        for (i = 0; params[i][0] >= 0; i++)
        {
            if (params[i][1] != j)
                continue;
            if (global_level_sets[j].size() <= cnt)
                global_level_sets[j].push_back(new LevelSet());
            while (global_level_sets[j][cnt]->levels.size() < params[i][0])
            {
                pthread_mutex_unlock(&glob_mutex);
                printf("%d of %d\n", global_level_sets[j][cnt]->levels.size(), params[i][0]);
                Grid* g;
                if (j == 0)
                    assert(0);
                else if (j == 1)
                    g = new SquareGrid ();
                else if (j == 2)
                    g = new TriangleGrid ();
                else
                    assert(0);
                g->randomize(XYPos(params[i][2], params[i][3]), params[i][4], params[i][8]);

                g->make_harder(params[i][5], params[i][6], params[i][7]);
                std::string s = g->to_string();
                pthread_mutex_lock(&glob_mutex);

                std::vector<std::string> &levels = global_level_sets[j][cnt]->levels;
                if(std::find(levels.begin(), levels.end(), s) == levels.end())
                    levels.push_back(s);
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

    pthread_mutex_unlock(&glob_mutex);
    return NULL;
}


int main( int argc, char* argv[] )
{
    int TNUM = 8;
    pthread_t thread[TNUM];
    void* dummy;

    LevelSet::init_global();
    for (int i = 0; i < TNUM; i++)
        pthread_create(&thread[i], NULL, exec, NULL);
    for (int i = 0; i < TNUM; i++)
        pthread_join(thread[i], &dummy);
    return 0;
}

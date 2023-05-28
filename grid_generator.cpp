#include "Grid.h"
#include "LevelSet.h"

static pthread_mutex_t glob_mutex;

void* exec(void* dummy)
{


    struct Param
    {
        int cnt;
        int group;
        const char* pars;
    };
    Param params[] = {
    // count, group, (hex/sqr/tri)(x)(y)(wrap)(merged)(rows)(+-)(x_y)(x_y3)(x_y_z)(exc)(parity)(xor1)(xor11)
    //                0            1  2  3     4       5    6    7       8    9     10    11    12     13
        // { 100, 0,  "A4300000000000"},
        // {-1, -1, ""},


        {200, 0,  "A4300000000000"},
        {200, 0,  "A4300080000000"},
        {200, 0,  "A4300044400040"},
        {200, 0,  "A4300044444444"},
        {200, 0,  "A4300244444444"},

        {200, 0,  "A5400000000000"},
        {200, 0,  "A5400080000000"},
        {200, 0,  "A5400044400040"},
        {200, 0,  "A5400044444444"},
        {200, 0,  "A5400244444444"},

        {200, 0,  "A6500000000000"},
        {200, 0,  "A6500080000000"},
        {200, 0,  "A6500044400040"},
        {200, 0,  "A6500044444444"},
        {200, 0,  "A6500244444444"},

        {200, 0,  "A7600000000000"},
        {200, 0,  "A7600080000000"},
        {200, 0,  "A7600044400040"},
        {200, 0,  "A7600044444444"},
        {200, 0,  "A7600244444444"},

        {200, 0,  "A8700000000000"},
        {200, 0,  "A8700080000000"},
        {200, 0,  "A8700044400040"},
        {200, 0,  "A8700044444444"},
        {200, 0,  "A8700244444444"},

        {200, 0,  "A9800000000000"},
        {200, 0,  "A9800080000000"},
        {200, 0,  "A9800044400040"},
        {200, 0,  "A9800044444444"},
        {200, 0,  "A9800244444444"},




        {200, 1,  "B3300000000000"},
        {200, 1,  "B3300080000000"},
        {200, 1,  "B3300044400040"},
        {200, 1,  "B3300044444444"},
        {200, 1,  "B3300244444444"},

        {200, 1,  "B4400000000000"},
        {200, 1,  "B4400080000000"},
        {200, 1,  "B4400044400040"},
        {200, 1,  "B4400044444444"},
        {200, 1,  "B4400244444444"},

        {200, 1,  "B5500000000000"},
        {200, 1,  "B5500080000000"},
        {200, 1,  "B5500044400040"},
        {200, 1,  "B5500044444444"},
        {200, 1,  "B5500244444444"},

        {200, 1,  "B6601000000000"},
        {200, 1,  "B6601080000000"},
        {200, 1,  "B6601044400040"},
        {200, 1,  "B6601044444444"},
        {200, 1,  "B6601244444444"},

        {200, 1,  "B7702000000000"},
        {200, 1,  "B7702080000000"},
        {200, 1,  "B7702044400040"},
        {200, 1,  "B7702044444444"},
        {200, 1,  "B7702244444444"},

        {200, 1,  "B8803000000000"},
        {200, 1,  "B8803080000000"},
        {200, 1,  "B8803044400040"},
        {200, 1,  "B8803044444444"},
        {200, 1,  "B8803244444444"},




        {200, 2,  "C4300000000000"},
        {200, 2,  "C4300080000000"},
        {200, 2,  "C4300044400040"},
        {200, 2,  "C4300044444444"},
        {200, 2,  "C4300244444444"},

        {200, 2,  "C6400000000000"},
        {200, 2,  "C6400080000000"},
        {200, 2,  "C6400044400040"},
        {200, 2,  "C6400044444444"},
        {200, 2,  "C6400244444444"},

        {200, 2,  "C8500000000000"},
        {200, 2,  "C8500080000000"},
        {200, 2,  "C8500044400040"},
        {200, 2,  "C8500044444444"},
        {200, 2,  "C8500144444444"},

        {200, 2,  "C9601000000000"},
        {200, 2,  "C9601080000000"},
        {200, 2,  "C9601044400040"},
        {200, 2,  "C9601044444444"},
        {200, 2,  "C9601144444444"},

        {200, 2,  "CC702000000000"},
        {200, 2,  "CC702080000000"},
        {200, 2,  "CC702044400040"},
        {200, 2,  "CC702044444444"},
        {200, 2,  "CC702144444444"},

        {200, 2,  "CD805000000000"},
        {200, 2,  "CD805080000000"},
        {200, 2,  "CD805044400040"},
        {200, 2,  "CD805044444444"},
        {200, 2,  "CD805144444444"},




        {200, 3,  "A4410044444444"},
        {200, 3,  "B4410044444444"},
        {200, 3,  "C4410044444444"},
        {200, 3,  "B4421044444444"},
        {200, 3,  "C8421044444444"},

        {200, 3,  "A6610044444444"},
        {200, 3,  "B6610044444444"},
        {200, 3,  "C6610044444444"},
        {200, 3,  "B6621044444444"},
        {200, 3,  "CC621044444444"},

        {200, 3,  "A8810044444444"},
        {200, 3,  "B8810044444444"},
        {200, 3,  "C8810044444444"},
        {200, 3,  "B8822044444444"},
        {200, 3,  "CG823044444444"},

        {200, 3,  "AAA10044444444"},
        {200, 3,  "BAA10044444444"},
        {200, 3,  "CAA10044444444"},
        {200, 3,  "BAA22044444444"},
        {200, 3,  "CKA23044444444"},

        {200, 3,  "ACC10044444444"},
        {200, 3,  "BCC10044444444"},
        {200, 3,  "CCC10044444444"},
        {200, 3,  "BCC24044444444"},
        {200, 3,  "COC26044444444"},

        {200, 3,  "AEE10044444444"},
        {200, 3,  "BEE10044444444"},
        {200, 3,  "CEE10044444444"},
        {200, 3,  "BEE24044444444"},
        {200, 3,  "CSE26044444444"},


        {  -1, -1,  ""}

     };
     int i;
     pthread_mutex_lock(&glob_mutex);



    for (int j = 0; j < GLBAL_LEVEL_SETS; j++)
    {
        int cnt = 0;
        for (i = 0; params[i].cnt >= 0; i++)
        {
            if (params[i].group != j)
                continue;
            if (global_level_sets[j].size() <= cnt)
                global_level_sets[j].push_back(new LevelSet());
            while (global_level_sets[j][cnt]->levels.size() < params[i].cnt)
            {
                printf("%d of %d\n", global_level_sets[j][cnt]->levels.size(), params[i].cnt);
                pthread_mutex_unlock(&glob_mutex);
                const char* req = params[i].pars;
                Grid* g;

                char c = req[0];
                if (c == 'A')
                    g = new HexagonGrid ();
                else if (c == 'B')
                    g = new SquareGrid ();
                else if (c == 'C')
                    g = new TriangleGrid ();
                else
                {
                    assert(0);
                    return 0;
                }
                XYPos siz;
                c = req[1];
                if (c >= 'A' && c <= 'Z')
                    siz.x = (c - 'A') + 10;
                else
                    siz.x = c - '0';
                c = req[2];
                int y;
                if (c >= 'A' && c <= 'Z')
                    siz.y = (c - 'A') + 10;
                else
                    siz.y = c - '0';
                int wrap = req[3] - '0';
                int merged = req[4] - '0';
                int rows = req[5] - '0';
                int pm = req[6] - '0';
                int xy = req[7] - '0';
                int xy3 = req[8] - '0';
                int xyz = req[9] - '0';
                int exc = req[10] - '0';
                int parity = req[11] - '0';
                int xor1 = req[12] - '0';
                int xor11 = req[13] - '0';

                g->randomize(siz, Grid::WrapType(wrap), merged, rows);
                g->make_harder(pm, xy, xy3, xyz, exc, parity, xor1, xor11);

                std::string s = g->to_string();
                {
                    Grid* gt = Grid::Load(s);
                    assert(gt->is_solveable());
                    delete gt;
                }
                pthread_mutex_lock(&glob_mutex);

                std::vector<std::string> &levels = global_level_sets[j][cnt]->levels;
                if(std::find(levels.begin(), levels.end(), s) == levels.end())
                    levels.push_back(s);
                delete g;
                LevelSet::save_global();
                printf("got\n");
            }
            global_level_sets[j][cnt]->levels.resize(params[i].cnt);
            cnt++;
        }
        global_level_sets[j].resize(cnt);
        LevelSet::save_global();
    }
    LevelSet::save_global();

    pthread_mutex_unlock(&glob_mutex);
    return NULL;
}


void global_mutex_lock()
{
    pthread_mutex_lock(&glob_mutex);
}

void global_mutex_unlock()
{
    pthread_mutex_unlock(&glob_mutex);
}


int main( int argc, char* argv[] )
{
    //grid_set_rnd(1);
    int TNUM = 16 ;
    pthread_t thread[TNUM];
    void* dummy;

    LevelSet::init_global();

    // for (int j = 0; j < GLBAL_LEVEL_SETS; j++)
    // {
    //     for (auto a : global_level_sets[j])
    //     {
    //         for (std::string& s : a->levels)
    //         {
    //             std::cout << s << std::endl;
    //             {
    //                 Grid* gt = Grid::Load(s);
    //                 assert(gt->is_solveable());
    //                 delete gt;
    //             }
    //         }
    //     }
    // }

    for (int i = 0; i < TNUM; i++)
        pthread_create(&thread[i], NULL, exec, NULL);
    for (int i = 0; i < TNUM; i++)
        pthread_join(thread[i], &dummy);
    return 0;
}

#include "Grid.h"
#include "LevelSet.h"
#include <algorithm>

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


        {100, 0,  "A43000000000000"},
        {100, 0,  "A43000800000000"},
        {100, 0,  "A43000444000400"},
        {100, 0,  "A43000444444440"},
        {100, 0,  "A43002444444440"},

        {100, 0,  "A54000000000000"},
        {100, 0,  "A54000800000000"},
        {100, 0,  "A54000444000400"},
        {100, 0,  "A54000444444440"},
        {100, 0,  "A54002444444440"},

        {100, 0,  "A65000000000000"},
        {100, 0,  "A65000800000000"},
        {100, 0,  "A65000444000400"},
        {100, 0,  "A65000444444440"},
        {100, 0,  "A65002444444440"},

        {100, 0,  "A76000000000000"},
        {100, 0,  "A76000800000000"},
        {100, 0,  "A76000444000400"},
        {100, 0,  "A76000444444440"},
        {100, 0,  "A76002444444440"},

        {100, 0,  "A87000000000000"},
        {100, 0,  "A87000800000000"},
        {100, 0,  "A87000444000400"},
        {100, 0,  "A87000444444440"},
        {100, 0,  "A87002444444440"},

        {100, 0,  "A98000000000000"},
        {100, 0,  "A98000800000000"},
        {100, 0,  "A98000444000400"},
        {100, 0,  "A98000444444440"},
        {100, 0,  "A98002444444440"},




        {100, 1,  "B33000000000000"},
        {100, 1,  "B33000800000000"},
        {100, 1,  "B33000444000400"},
        {100, 1,  "B33000444444440"},
        {100, 1,  "B33002444444440"},

        {100, 1,  "B44000000000000"},
        {100, 1,  "B44000800000000"},
        {100, 1,  "B44000444000400"},
        {100, 1,  "B44000444444440"},
        {100, 1,  "B44002444444440"},

        {100, 1,  "B55000000000000"},
        {100, 1,  "B55000800000000"},
        {100, 1,  "B55000444000400"},
        {100, 1,  "B55000444444440"},
        {100, 1,  "B55002444444440"},

        {100, 1,  "B66010000000000"},
        {100, 1,  "B66010800000000"},
        {100, 1,  "B66010444000400"},
        {100, 1,  "B66010444444440"},
        {100, 1,  "B66012444444440"},

        {100, 1,  "B77020000000000"},
        {100, 1,  "B77020800000000"},
        {100, 1,  "B77020444000400"},
        {100, 1,  "B77020444444440"},
        {100, 1,  "B77022444444440"},

        {100, 1,  "B88030000000000"},
        {100, 1,  "B88030800000000"},
        {100, 1,  "B88030444000400"},
        {100, 1,  "B88030444444440"},
        {100, 1,  "B88032444444440"},




        {100, 2,  "C43000000000000"},
        {100, 2,  "C43000800000000"},
        {100, 2,  "C43000444000400"},
        {100, 2,  "C43000444444440"},
        {100, 2,  "C43002444444440"},

        {100, 2,  "C64000000000000"},
        {100, 2,  "C64000800000000"},
        {100, 2,  "C64000444000400"},
        {100, 2,  "C64000444444440"},
        {100, 2,  "C64002444444440"},

        {100, 2,  "C85000000000000"},
        {100, 2,  "C85000800000000"},
        {100, 2,  "C85000444000400"},
        {100, 2,  "C85000444444440"},
        {100, 2,  "C85001444444440"},

        {100, 2,  "C96010000000000"},
        {100, 2,  "C96010800000000"},
        {100, 2,  "C96010444000400"},
        {100, 2,  "C96010444444440"},
        {100, 2,  "C96011444444440"},

        {100, 2,  "CC7020000000000"},
        {100, 2,  "CC7020800000000"},
        {100, 2,  "CC7020444000400"},
        {100, 2,  "CC7020444444440"},
        {100, 2,  "CC7021444444440"},

        {100, 2,  "CD8050000000000"},
        {100, 2,  "CD8050800000000"},
        {100, 2,  "CD8050444000400"},
        {100, 2,  "CD8050444444440"},
        {100, 2,  "CD8051444444440"},




        {100, 3,  "A44100444444440"},
        {100, 3,  "B44100444444440"},
        {100, 3,  "C44100444444440"},
        {100, 3,  "B44210444444440"},
        {100, 3,  "C84210444444440"},

        {100, 3,  "A66100444444440"},
        {100, 3,  "B66100444444440"},
        {100, 3,  "C66100444444440"},
        {100, 3,  "B66210444444440"},
        {100, 3,  "CC6210444444440"},

        {100, 3,  "A88100444444440"},
        {100, 3,  "B88100444444440"},
        {100, 3,  "C88100444444440"},
        {100, 3,  "B88220444444440"},
        {100, 3,  "CG8230444444440"},

        {100, 3,  "AAA100444444440"},
        {100, 3,  "BAA100444444440"},
        {100, 3,  "CAA100444444440"},
        {100, 3,  "BAA220444444440"},
        {100, 3,  "CKA230444444440"},

        {100, 3,  "ACC100444444440"},
        {100, 3,  "BCC100444444440"},
        {100, 3,  "CCC100444444440"},
        {100, 3,  "BCC240444444440"},
        {100, 3,  "COC260444444440"},

        {100, 3,  "AEE100444444440"},
        {100, 3,  "BEE100444444440"},
        {100, 3,  "CEE100444444440"},
        {100, 3,  "BEE240444444440"},
        {100, 3,  "CSE260444444440"},


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
            if ((int)second_global_level_sets[j].size() <= cnt)
                second_global_level_sets[j].push_back(new LevelSet());
            while ((int)second_global_level_sets[j][cnt]->levels.size() < params[i].cnt)
            {
                printf("%lu of %d\n", second_global_level_sets[j][cnt]->levels.size(), params[i].cnt);
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
                int prime = req[14] - '0';

                g->randomize(siz, Grid::WrapType(wrap), merged, rows, 20);
                g->make_harder(pm, xy, xy3, xyz, exc, parity, xor1, xor11, prime);

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
            second_global_level_sets[j][cnt]->levels.resize(params[i].cnt);
            cnt++;
        }
        second_global_level_sets[j].resize(cnt);
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
    const int TNUM = 16 ;
    pthread_t thread[TNUM];
    void* dummy;

    LevelSet::init_global();
//    exec(&dummy);

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

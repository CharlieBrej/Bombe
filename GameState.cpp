#define _USE_MATH_DEFINES
#include <cmath>

#include "GameState.h"
#include "SaveState.h"
#include "Misc.h"
#include "LevelSet.h"
#include "Compress.h"
#include "ImgClipBoard.h"

#include <cassert>
#include <SDL.h>
#include <SDL_mixer.h>
#include <SDL_net.h>
#include <SDL_ttf.h>

#include <iostream>
#include <iomanip>
#include <fstream>
#include <algorithm>
#include <limits>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <sstream>
#include <codecvt>

#ifdef _WIN32
    #define NOMINMAX
    #include <windows.h>
    #include <shellapi.h>
    #include <filesystem>
#endif

static void DisplayWebsite(const char* url)
{
#ifdef __linux__
    char buf[1024];
    snprintf(buf, sizeof(buf), "xdg-open %s", url);
    system(buf);
#else
    SDL_OpenURL(url);
#endif

}

static SDL_mutex* global_mutex;

void global_mutex_lock()
{
    SDL_LockMutex(global_mutex);
}

void global_mutex_unlock()
{
    SDL_UnlockMutex(global_mutex);
}

static Rand rnd(1);

struct RobotThread
{
    GameState* state;
    int index;
};

static int robot_thread_func(void *ptr)
{
    RobotThread* rt = (RobotThread*)ptr;
    rt->state->robot_thread(rt->index);
    delete rt;
    return 0;
}

GameState::GameState(std::string& load_data, bool json)
{
    global_mutex = SDL_CreateMutex();
    level_gen_mutex = SDL_CreateMutex();
    LevelSet::init_global();

    for (int k = 0; k < GAME_MODES; k++)
    for (int j = 0; j < GLBAL_LEVEL_SETS; j++)
    {
        level_progress[k][j].resize(global_level_sets[j].size());
        for (unsigned i = 0; i < global_level_sets[j].size(); i++)
        {
            level_progress[k][j][i].level_status.resize(global_level_sets[j][i]->levels.size());
            level_progress[k][j][i].count_todo = global_level_sets[j][i]->levels.size();
        }
    }
    {
        server_levels.resize(1);
        server_levels[0].push_back("ABBA!");
        for (int m = 0; m < GAME_MODES; m++)
            level_progress[m][GLBAL_LEVEL_SETS].resize(1);

    }
    {
        std::ifstream loadfile("lang.json");
        lang_data = SaveObject::load(loadfile)->get_map();
    }
    window_size = XYPos(1920/2, 1080/2);
    try
    {
        if (!load_data.empty())
        {
            SaveObjectMap* omap;
            omap = SaveObject::load(load_data)->get_map();
            int version = omap->get_num("version");
            if (json && version > 6) 
                version = 6;
            if (version < 2)
                throw(std::runtime_error("Bad Version"));
            if (omap->has_key("language"))
                language = omap->get_string("language");
            if (omap->has_key("level_group_index"))
                current_level_group_index = omap->get_num("level_group_index");
            if (omap->has_key("level_set_index"))
                current_level_set_index = omap->get_num("level_set_index");
            if (omap->has_key("show_row_clues"))
                show_row_clues = omap->get_num("show_row_clues");
            if (omap->has_key("low_contrast"))
                low_contrast = omap->get_num("low_contrast");
            if(low_contrast)
                contrast = 128;
            if (omap->has_key("speed_dial"))
                speed_dial = double(omap->get_num("speed_dial")) / 1000;
            if (omap->has_key("volume"))
                volume = double(omap->get_num("volume")) / 1000;
            if (omap->has_key("music_volume"))
                music_volume = double(omap->get_num("music_volume")) / 1000;
            if (omap->has_key("colors"))
                colors = double(omap->get_num("colors")) / 1000;
            if (omap->has_key("rule_limit"))
                rule_limit_slider = double(omap->get_num("rule_limit")) / 1000000000;
            if (omap->has_key("game_mode"))
                game_mode = omap->get_num("game_mode");
            if (omap->has_key("full_screen"))
                full_screen = omap->get_num("full_screen");
            if (!full_screen && omap->has_key("window_size"))
            {
                window_size.y = omap->get_num("window_size");
                window_size.x = std::ceil(double(window_size.y) * 16 / 9);
            }
            if (omap->has_key("max_stars"))
                max_stars = omap->get_num("max_stars");
            if (omap->has_key("server_levels"))
            {
                SaveObjectList* lvl_sets = omap->get_item("server_levels")->get_list();
                server_levels.resize(lvl_sets->get_count());
                for (int m = 0; m < GAME_MODES; m++)
                    level_progress[m][GLBAL_LEVEL_SETS].resize(lvl_sets->get_count());
                for (unsigned k = 0; k < lvl_sets->get_count(); k++)
                {
                    SaveObjectList* plist = lvl_sets->get_item(k)->get_list();
                    server_levels[k].clear();
                    for (int m = 0; m < GAME_MODES; m++)
                    {
                        level_progress[m][GLBAL_LEVEL_SETS][k].level_status.resize(plist->get_count());
                        level_progress[m][GLBAL_LEVEL_SETS][k].count_todo = plist->get_count();
                    }
                    for (unsigned i = 0; i < plist->get_count(); i++)
                    {
                        std::string s = plist->get_string(i);
                        server_levels[k].push_back(s);
                    }
                }
                server_levels_version = omap->get_num("server_levels_version");
            }

            if (omap->has_key("rule_del_count"))
            {
                SaveObjectList* dlist = omap->get_item("rule_del_count")->get_list();
                for (unsigned k = 0; k < dlist->get_count() && k < GAME_MODES; k++)
                    rule_del_count[k] = dlist->get_item(k)->get_num();
            }

            if (omap->has_key("key_codes"))
            {
                SaveObjectList* dlist = omap->get_item("key_codes")->get_list();
                for (unsigned k = 0; k < dlist->get_count() && k < KEY_CODE_TOTAL; k++)
                    key_codes[k] = dlist->get_item(k)->get_num();
            }

            std::list<SaveObjectMap*> modes;
            if (omap->has_key("modes"))
            {
                SaveObjectList* mode_lists = omap->get_item("modes")->get_list();
                for (unsigned k = 0; k < mode_lists->get_count(); k++)
                    modes.push_back(mode_lists->get_item(k)->get_map());
            }
            else
                modes.push_back(omap);
            int mode = 0;
            for (SaveObjectMap* omap : modes)
            {
                SaveObjectList* rlist = omap->get_item("rules")->get_list();
                for (unsigned i = 0; i < rlist->get_count(); i++)
                {
                    GridRule r(rlist->get_item(i));
                    if (rule_is_permitted(r, mode))
                        rules[mode].push_back(r);
                }

                if (version == game_version)
                {
                    SaveObjectList* pplist = omap->get_item("level_progress")->get_list();
                    for (unsigned k = 0; k <= GLBAL_LEVEL_SETS && k < pplist->get_count(); k++)
                    {
                        SaveObjectList* plist = pplist->get_item(k)->get_list();
                        for (unsigned i = 0; i < plist->get_count() && i < level_progress[mode][k].size(); i++)
                        {
                            std::string s = plist->get_string(i);
                            int lim = std::min(s.size(), level_progress[mode][k][i].level_status.size());
                            for (int j = 0; j < lim; j++)
                            {
                                char c = s[j];
                                int stat = c - '0';
                                level_progress[mode][k][i].level_status[j].done = stat;
                                if (stat)
                                    level_progress[mode][k][i].count_todo--;
                            }
                            if (level_progress[mode][k][i].count_todo == 0)
                                level_progress[mode][k][i].star_anim_prog = PROG_ANIM_MAX;
                            if (level_is_accessible(mode,k,i))
                                level_progress[mode][k][i].unlock_anim_prog = PROG_ANIM_MAX;
                        }
                    }
                }
                mode++;
            }
            delete omap;
        }
    }
    catch (const std::runtime_error& error)
    {
        std::cerr << error.what() << "\n";
    }

    if (!max_stars)
    {
        display_help = true;
        display_language_chooser = true;
        walkthrough = true;
    }
    if (current_level_group_index >= GLBAL_LEVEL_SETS)
        current_level_group_index = 0;
    if (current_level_set_index >= global_level_sets[current_level_group_index].size())
        current_level_set_index = 0;

    rule_limit_count = pow(100, 1 + rule_limit_slider * 1.6) / 10;
    if (rule_limit_slider >= 1.0)
        rule_limit_count = -1;

    sdl_window = SDL_CreateWindow( "Bombe", 0, 32, window_size.x, window_size.y, SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | (full_screen ? SDL_WINDOW_FULLSCREEN_DESKTOP  | SDL_WINDOW_BORDERLESS : 0));
    sdl_renderer = SDL_CreateRenderer(sdl_window, -1, SDL_RENDERER_ACCELERATED);
    if (full_screen)
    {
        SDL_SetWindowFullscreen(sdl_window, full_screen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
        SDL_SetWindowBordered(sdl_window, full_screen ? SDL_FALSE : SDL_TRUE);
        SDL_SetWindowResizable(sdl_window, full_screen ? SDL_FALSE : SDL_TRUE);
        SDL_SetWindowInputFocus(sdl_window);
    }

    SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 255);
	sdl_texture = loadTexture("texture.png");

    tutorial_texture[0] = loadTexture("tutorial/tut0.png");
    tutorial_texture[1] = loadTexture("tutorial/tut1.png");
    tutorial_texture[2] = loadTexture("tutorial/tut2.png");
    tutorial_texture[3] = loadTexture("tutorial/tut3.png");
    tutorial_texture[4] = loadTexture("tutorial/tut4.png");
    tutorial_texture[5] = loadTexture("tutorial/tut5.png");
    tutorial_texture[6] = loadTexture("tutorial/tut6.png");
    tutorial_texture[7] = loadTexture("tutorial/tut7.png");

    {
        SDL_Surface* icon_surface = IMG_Load("icon.png");
        SDL_SetWindowIcon(sdl_window, icon_surface);
	    SDL_FreeSurface(icon_surface);
    }
    overlay_texture = SDL_CreateTexture(sdl_renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, 2048, 2048);
    SDL_SetTextureBlendMode(overlay_texture, SDL_BLENDMODE_BLEND);
    clear_overlay();

    for (std::map<std::string, SaveObject*>::iterator it = lang_data->omap.begin(); it != lang_data->omap.end(); ++it)
    {
        std::string s = it->first;
        std::string filename = lang_data->get_item(s)->get_map()->get_string("font");
        if (!fonts.count(filename))
            fonts[filename] = TTF_OpenFont(filename.c_str(), 32);
    }
    set_language(language);
    score_font = TTF_OpenFont("font-fixed.ttf", 19*4);

    if (Mix_OpenAudio(48000, MIX_DEFAULT_FORMAT, 2, 2048) != 0)
        has_sound = false;
    else if (Mix_AllocateChannels(32) != 32)
        has_sound = false;
    if (has_sound)
    {
        sounds[0] = Mix_LoadWAV( "snd/plop0.wav" );
        sounds[1] = Mix_LoadWAV( "snd/plop1.wav" );
        sounds[2] = Mix_LoadWAV( "snd/plop2.wav" );
        sounds[3] = Mix_LoadWAV( "snd/plop3.wav" );
        sounds[4] = Mix_LoadWAV( "snd/plop4.wav" );
        sounds[5] = Mix_LoadWAV( "snd/plop5.wav" );
        sounds[6] = Mix_LoadWAV( "snd/plop6.wav" );
        sounds[7] = Mix_LoadWAV( "snd/plop7.wav" );
        sounds[8] = Mix_LoadWAV( "snd/success1.wav" );
        sounds[9] = Mix_LoadWAV( "snd/success2.wav" );
        for (int i = 0; i < 10; i++)
            assert(sounds[i]);
        Mix_Volume(-1, volume * volume * SDL_MIX_MAXVOLUME);
        Mix_VolumeMusic(music_volume * music_volume * SDL_MIX_MAXVOLUME);
        music = Mix_LoadMUS("music.ogg");
        assert(music);
        assert(Mix_PlayMusic(music, -1) == 0);
    }

    grid = Grid::Load("ABBA!");

    prog_stars[PROG_LOCK_HEX] = 0;
    prog_stars[PROG_LOCK_SQUARE] = 500;
    prog_stars[PROG_LOCK_TRIANGLE] = 5000;
    prog_stars[PROG_LOCK_GRID] = 10000;
    prog_stars[PROG_LOCK_SERVER] = 12000;
    prog_stars[PROG_LOCK_DONT_CARE] = 19;
    prog_stars[PROG_LOCK_NUMBER_TYPES] = 30;
    prog_stars[PROG_LOCK_LEVELS_AND_LOCKS] = 40;
    prog_stars[PROG_LOCK_GEN_REGIONS] = 395;
    prog_stars[PROG_LOCK_VISIBILITY] = 2000;
    prog_stars[PROG_LOCK_VISIBILITY2] = 2000;
    prog_stars[PROG_LOCK_VISIBILITY3] = 2000;
    prog_stars[PROG_LOCK_VISIBILITY4] = 2000;
    prog_stars[PROG_LOCK_GAME_MODE] = 13000;
    prog_stars[PROG_LOCK_VARS1] = 6000;
    prog_stars[PROG_LOCK_VARS2] = 10000;
    prog_stars[PROG_LOCK_VARS3] = 14000;
    prog_stars[PROG_LOCK_VARS4] = 17000;
    prog_stars[PROG_LOCK_VARS5] = 20000;
    // prog_stars[PROG_LOCK_VARS3] = 10000;
    // prog_stars[PROG_LOCK_VARS4] = 10000;
    // prog_stars[PROG_LOCK_VARS5] = 10000;
    prog_stars[PROG_LOCK_FILTER] = 7000;
    prog_stars[PROG_LOCK_PRIORITY] = 16000;
    prog_stars[PROG_LOCK_PRIORITY2] = 16000;
    prog_stars[PROG_LOCK_PAUSE] = 9000;
    prog_stars[PROG_LOCK_COLORS] = 15000;
    prog_stars[PROG_LOCK_TABLES] = 200;
    prog_stars[PROG_LOCK_SPEED] = 3;

    prog_stars[PROG_LOCK_USE_DONT_CARE] = 100;
    prog_stars[PROG_LOCK_REGION_HINT] = 300;
    prog_stars[PROG_LOCK_DOUBLE_CLICK_HINT] = 600;
    prog_stars[PROG_LOCK_REGION_LIMIT] = 8000;
    prog_stars[PROG_LOCK_ROBOTS] = 9000;


    for (int i = 0; i < PROG_LOCK_TOTAL; i++)
        if (prog_stars[i] <= max_stars)
            prog_seen[i] = PROG_ANIM_MAX;
    if (!prog_seen[PROG_LOCK_GAME_MODE])
        game_mode = 0;
    ImgClipBoard::init();

    robot_count = SDL_GetCPUCount();
    if (robot_count > max_robot_count)
        robot_count = max_robot_count;
    run_robot_count = robot_count;
    for (int i = 0; i < robot_count; i++)
    {
        robot_lock[i] = SDL_CreateMutex();
        RobotThread* rt = new RobotThread{this, i};
        robot_threads[i] = SDL_CreateThread(robot_thread_func, "Robot", (void *)rt);
    }
    level_progress_lock = SDL_CreateMutex();
}

SaveObject* GameState::save(bool lite)
{
    SaveObjectMap* omap = new SaveObjectMap;
    omap->add_num("version", game_version);

    SaveObjectList* m_list = new SaveObjectList;

    for (int mode = 0; mode < GAME_MODES; mode++)
    {
        SaveObjectMap* omap = new SaveObjectMap;
        SaveObjectList* rlist = new SaveObjectList;
        for (GridRule& rule : rules[mode])
        {
            if (!rule.deleted)
                rlist->add_item(rule.save());
        }
        omap->add_item("rules", rlist);

        SaveObjectList* pplist = new SaveObjectList;
        for (int j = 0; j <= GLBAL_LEVEL_SETS; j++)
        {
            SaveObjectList* plist = new SaveObjectList;
            for (LevelProgress& prog : level_progress[mode][j])
            {
                std::string sstr;
                for (LevelStatus& stat : prog.level_status)
                {
                    char c = '0' + stat.done;
                    sstr += c;
                }
                plist->add_item(new SaveObjectString(sstr));
            }
            pplist->add_item(plist);
        }
        omap->add_item("level_progress", pplist);
        m_list->add_item(omap);
    }
    omap->add_item("modes", m_list);
    omap->add_num("game_mode", game_mode);

    omap->add_string("language", language);
    omap->add_num("level_group_index", current_level_group_index);
    omap->add_num("level_set_index", current_level_set_index);
    omap->add_num("show_row_clues", show_row_clues);
    omap->add_num("low_contrast", low_contrast);
    omap->add_num("speed_dial", speed_dial * 1000);
    omap->add_num("volume", volume * 1000);
    omap->add_num("music_volume", music_volume * 1000);
    omap->add_num("colors", colors * 1000);
    omap->add_num("rule_limit", rule_limit_slider * 1000000000);
    {
        if (window_size.x * 9 > window_size.y * 16)
            omap->add_num("window_size", window_size.y);
        else
            omap->add_num("window_size", std::ceil(double(window_size.x) * 9) / 16);
    }
    omap->add_num("full_screen", full_screen);
    omap->add_num("max_stars", max_stars);

    SaveObjectList* dc_list = new SaveObjectList;
    for (int i = 0; i < GAME_MODES; i++)
        dc_list->add_num(rule_del_count[i]);
    omap->add_item("rule_del_count", dc_list);

    dc_list = new SaveObjectList;
    for (int i = 0; i < KEY_CODE_TOTAL; i++)
        dc_list->add_num(key_codes[i]);
    omap->add_item("key_codes", dc_list);

    SaveObjectList* sl_list = new SaveObjectList;
    for (std::vector<std::string>& lvl_set : server_levels)
    {
        SaveObjectList* ssl_list = new SaveObjectList;
        for (std::string& lvl : lvl_set)
        {
            ssl_list->add_string(lvl);
        }
        sl_list->add_item(ssl_list);
    }
    omap->add_item("server_levels", sl_list);
    omap->add_num("server_levels_version", server_levels_version);

    return omap;
}

void GameState::save(std::ostream& outfile, bool lite)
{
    SaveObject* omap = save(lite);
    omap->save(outfile);
    delete omap;
}


GameState::~GameState()
{
    SHUTDOWN = true;
    run_robots = false;
    for (int i = 0; i < robot_count; i++)
    {
        SDL_WaitThread(robot_threads[i], NULL);
    }

    delete grid;
    delete lang_data;
    for (const auto& [key, value] : fonts)
        TTF_CloseFont(value);
    fonts.clear();
    TTF_CloseFont(score_font);
    ImgClipBoard::shutdown();

    if (has_sound)
        for (int i = 0; i < 16; i++)
            if (sounds[i])
                Mix_FreeChunk(sounds[i]);
    Mix_FreeMusic(music);

    SDL_DestroyTexture(sdl_texture);
    for (int i = 0; i < tut_texture_count; i++)
        SDL_DestroyTexture(tutorial_texture[i]);
    SDL_DestroyRenderer(sdl_renderer);
    SDL_DestroyWindow(sdl_window);
    LevelSet::delete_global();
    if (level_gen_thread)
    {
        SDL_WaitThread(level_gen_thread, NULL);
        level_gen_thread = NULL;
    }

    Z3_finalize_memory();
    SDL_DestroyMutex(level_progress_lock);
}
void GameState::reset_levels()
{
    rule_del_count[game_mode] = 0;
    current_level_group_index = 0;
    current_level_set_index = 0;
    current_level_index = 0;
    load_level = true;
    force_load_level = true;

    for (GridRule& rule : rules[game_mode])
    {
        rule.used_count = 0;
        rule.clear_count = 0;
    }
    for (int j = 0; j < GLBAL_LEVEL_SETS; j++)
    {
        level_progress[game_mode][j].resize(global_level_sets[j].size());
        for (unsigned i = 0; i < global_level_sets[j].size(); i++)
        {
            level_progress[game_mode][j][i].level_status.clear();
            level_progress[game_mode][j][i].level_status.resize(global_level_sets[j][i]->levels.size());
            level_progress[game_mode][j][i].count_todo = global_level_sets[j][i]->levels.size();
            level_progress[game_mode][j][i].star_anim_prog = 0;
            level_progress[game_mode][j][i].unlock_anim_prog = 0;
        }
    }
    for (unsigned i = 0; i < server_levels.size(); i++)
    {
        level_progress[game_mode][GLBAL_LEVEL_SETS][i].level_status.clear();
        level_progress[game_mode][GLBAL_LEVEL_SETS][i].level_status.resize(server_levels[i].size());
        level_progress[game_mode][GLBAL_LEVEL_SETS][i].count_todo = server_levels[i].size();
        level_progress[game_mode][GLBAL_LEVEL_SETS][i].star_anim_prog = 0;
        level_progress[game_mode][GLBAL_LEVEL_SETS][i].unlock_anim_prog = 0;
    }
}

bool GameState::level_is_accessible(int mode, int group_index, int  set)
{
    if (IS_DEMO)
    {
        int r = set % 5 + set / 5;
        if (r > 3)
            return false;
        if (group_index > 2)
            return false;
    }
    if (set == 0)
        return true;
    if (set >= 5 && level_progress[mode][group_index][set - 5].count_todo <= 100)
        return true;
    if ((set % 5 >= 1) && level_progress[mode][group_index][set - 1].count_todo <= 100)
        return true;
    return false;
}

class ServerComms
{
public:
    SaveObject* send;
    ServerResp* resp;

    ServerComms(SaveObject* send_, ServerResp* resp_ = NULL):
        send(send_),
        resp(resp_)
    {}
};

    // (hex/sqr/tri)(x)(y)(wrap)(merged)(rows)(+-)(x_y)(x_y3)(x_y_z)(exc)(parity)(xor1)(xor11)
    //  0            1  2  3     4       5    6    7       8    9     10    11    12     13
static int level_gen_thread_func(void *ptr)
{
    GameState* game_state = (GameState*) ptr;
    std::string req = game_state->level_gen_req;
    char c = req[0];

    Grid* g;

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

    g->randomize(siz, Grid::WrapType(wrap), merged, rows * 10);
    g->make_harder(pm, xy, xy3, xyz, exc, parity, xor1, xor11);
    std::string s = g->to_string();
    SDL_LockMutex(game_state->level_gen_mutex);
    game_state->level_gen_resp = g->to_string();
    SDL_UnlockMutex(game_state->level_gen_mutex);
    return 0;
}

static int fetch_from_server_thread(void *ptr)
{
    IPaddress ip;
    TCPsocket tcpsock;
    ServerComms* comms = (ServerComms*)ptr;
    if (SDLNet_ResolveHost(&ip, "brej.org", 42071) == -1)
//    if (SDLNet_ResolveHost(&ip, "127.0.0.1", 42071) == -1)
    {
        printf("SDLNet_ResolveHost: %s\n", SDLNet_GetError());
        if (comms->resp)
        {
            comms->resp->error = true;
            comms->resp->done = true;
            SDL_AtomicUnlock(&comms->resp->working);
        }
        delete comms->send;
        delete comms;
        return 0;
    }

    tcpsock = SDLNet_TCP_Open(&ip);
    if (!tcpsock)
    {
        printf("SDLNet_TCP_Open: %s\n", SDLNet_GetError());
        if (comms->resp)
        {
            comms->resp->error = true;
            comms->resp->done = true;
            SDL_AtomicUnlock(&comms->resp->working);
        }
        delete comms->send;
        delete comms;
        return 0;
    }

    try
    {
        std::ostringstream stream;
        comms->send->save(stream);
        std::string comp = compress_string(stream.str());

        uint32_t length = comp.length();
        SDLNet_TCP_Send(tcpsock, (char*)&length, 4);
        SDLNet_TCP_Send(tcpsock, comp.c_str(), length);

        if (comms->resp)
        {
            unsigned got = SDLNet_TCP_Recv(tcpsock, (char*)&length, 4);
            if (got != 4)
                throw(std::runtime_error("Connection closed early"));
            char* data = (char*)malloc(length);
            got = 0;
            while (got != length)
            {
                int n = SDLNet_TCP_Recv(tcpsock, &data[got], length - got);
                got += n;
                if (!n)
                {
                    free (data);
                    throw(std::runtime_error("Connection closed early"));
                }
            }
            std::string in_str(data, length);
            free (data);
            std::string decomp = decompress_string(in_str);
            std::istringstream decomp_stream(decomp);
            comms->resp->resp = SaveObject::load(decomp_stream);
        }
    }
    catch (const std::runtime_error& error)
    {
        std::cerr << error.what() << "\n";
        if (comms->resp)
        {
            comms->resp->error = true;
            comms->resp->done = true;
            SDL_AtomicUnlock(&comms->resp->working);
        }
    }
    SDLNet_TCP_Close(tcpsock);
    if (comms->resp)
    {
        comms->resp->done = true;
        SDL_AtomicUnlock(&comms->resp->working);
    }
    delete comms->send;
    delete comms;
    return 0;
}

void GameState::post_to_server(SaveObject* send, bool sync)
{
    if (server_timeout)
        return;
    SDL_Thread *thread = SDL_CreateThread(fetch_from_server_thread, "PostToServer", (void *)new ServerComms(send));
    if (sync)
        SDL_WaitThread(thread, NULL);
    else
        SDL_DetachThread(thread);
}

void GameState::fetch_from_server(SaveObject* send, ServerResp* resp)
{
    if (server_timeout)
        return;
    SDL_AtomicLock(&resp->working);
    resp->done = false;
    resp->error = false;
    delete resp->resp;
    resp->resp = NULL;
    SDL_Thread *thread = SDL_CreateThread(fetch_from_server_thread, "FetchFromServer", (void *)new ServerComms(send, resp));
    SDL_DetachThread(thread);
}

void GameState::fetch_scores()
{
    if (scores_from_server.working || scores_from_server.done)
        return;
    if (steam_session_string.empty())
        return;
    SaveObjectMap* omap = new SaveObjectMap;
    omap->add_string("command", "scores");
    omap->add_num("steam_id", steam_id);
    omap->add_string("steam_username", steam_username);
    omap->add_string("steam_session", steam_session_string);
    omap->add_num("demo", IS_DEMO);
    omap->add_num("playtest", IS_PLAYTEST);
    omap->add_num("version", game_version);
    omap->add_num("game_mode", game_mode);
    SaveObjectList* pplist = new SaveObjectList;
    for (int j = 0; j <= GLBAL_LEVEL_SETS; j++)
    {
        SaveObjectList* plist = new SaveObjectList;
        for (LevelProgress& prog : level_progress[game_mode][j])
        {
            std::string sstr;
            for (LevelStatus& stat : prog.level_status)
            {
                char c = '0' + stat.done;
                sstr += c;
            }
            plist->add_item(new SaveObjectString(sstr));
        }
        pplist->add_item(plist);
    }
    omap->add_item("level_progress", pplist);

    SaveObjectList* slist = new SaveObjectList;
    for (uint64_t f : steam_friends)
        slist->add_num(f);
    omap->add_item("friends", slist);
    omap->add_num("server_levels_version", server_levels_version);

    if (SDL_TryLockMutex(level_gen_mutex) == 0)
    {
        if (level_gen_resp != "")
        {
            omap->add_string("level_gen_req", level_gen_req);
            omap->add_string("level_gen_resp", level_gen_resp);
            level_gen_req = "";
            level_gen_resp = "";
        }
        SDL_UnlockMutex(level_gen_mutex);
    }
    fetch_from_server(omap, &scores_from_server);
}

void GameState::clear_overlay()
{
    if (overlay_texture_is_clean)
        return;
    uint32_t* pixels;
    int pitch;
    int r = SDL_LockTexture(overlay_texture, NULL, (void**)&pixels, &pitch);
    assert(r == 0);
    pitch /= 4;
    FOR_XY(p, XYPos(0, 0), XYPos(2048, 2048))
    {
        pixels[p.y * pitch + p.x] = 0;
    }
    SDL_UnlockTexture(overlay_texture);
    overlay_texture_is_clean = true;
}

SDL_Texture* GameState::loadTexture(const char* filename)
{
    SDL_Surface* loadedSurface = IMG_Load(filename);
	assert(loadedSurface);
    SDL_Texture* new_texture = SDL_CreateTextureFromSurface(sdl_renderer, loadedSurface);
	assert(new_texture);
	SDL_FreeSurface(loadedSurface);
	return new_texture;
}

bool GameState::rule_is_permitted(GridRule& rule, int mode)
{   
    // GridRule why;
    // if (rule.is_legal(why) != GridRule::OK)
    //     return false;
    // GridRule::IsLogicalRep rep = rule.is_legal(why);
    // if (rep == GridRule::OK || rep == GridRule::LOSES_DATA)
    // {}
    // else
    //     assert(0);

    if (mode == 1 && rule.region_count == 4)
        return false;
    if (mode == 3)
    {
        for (int i = 0; i < rule.region_count; i++)
            if (rule.region_type[i].var)
                return false;
        for (int i = 0; i < (1 << constructed_rule.region_count); i++)
            if (constructed_rule.square_counts[i].var)
                return false;
        if (constructed_rule.apply_region_bitmap && constructed_rule.apply_region_type.var)
            return false;
    }
    if (rule.apply_region_type.type != RegionType::VISIBILITY && (mode == 2 || mode == 3))
    {
        int rule_cnt = 0;
        for (GridRule& rule : rules[mode])
            if (!rule.deleted && rule.apply_region_type.type != RegionType::VISIBILITY)
                rule_cnt++;
        rule_cnt += rule_del_count[mode];
        if (mode == 2 && rule_cnt >= 60)
            return false;
        if (mode == 3 && rule_cnt >= 300)
            return false;
    }
    return true;
}

void GameState::load_grid(std::string s)
{
    filter_pos.clear();
    grid->commit_level_counts();
    delete grid;
    grid = Grid::Load(s);
    rule_gen_region[0] = NULL;
    rule_gen_region[1] = NULL;
    rule_gen_region[2] = NULL;
    rule_gen_region[3] = NULL;

    for (ConstructedRuleState& s : constructed_rule_undo)
        for (int i = 0; i < 4; i++)
            s.regions[i] = NULL;
    for (ConstructedRuleState& s : constructed_rule_redo)
        for (int i = 0; i < 4; i++)
            s.regions[i] = NULL;

    inspected_rule.regions[0] = NULL;
    inspected_rule.regions[1] = NULL;
    inspected_rule.regions[2] = NULL;
    inspected_rule.regions[3] = NULL;
    inspected_region = NULL;
    mouse_hover_region = NULL;
    grid_cells_animation.clear();
    grid_regions_animation.clear();
    grid_regions_fade.clear();
    current_level_is_temp = false;
    grid_zoom = 1;
    target_grid_zoom = 1;
    scaled_grid_offset = XYPos(0,0);
    scaled_grid_size = grid_size;
    display_levels_center_current = true;
    if (right_panel_mode == RIGHT_MENU_REGION)
        right_panel_mode = RIGHT_MENU_NONE;

}
static int advance_grid(Grid* grid, std::list<GridRule> &rules, GridRegion *inspected_region);

void GameState::pause_robots()
{
    if (!run_robots)
        return;
    run_robots = false;
    for (int i = 0; i < robot_count; i++)
        SDL_LockMutex(robot_lock[i]);

    for (int i = 0; i < robot_count; i++)
        SDL_UnlockMutex(robot_lock[i]);
}

void GameState::robot_thread(int thread_index)
{
    Rand rnd;
    struct RobotJob
    {
        unsigned level_group_index;
        unsigned level_set_index;
        unsigned level_index;
    };

    
    SDL_LockMutex(robot_lock[thread_index]);

    while (true)
    {
        if (!run_robots || run_robot_count <= thread_index)
        {
            SDL_UnlockMutex(robot_lock[thread_index]);
            while (!run_robots || run_robot_count <= thread_index)
            {
                if (SHUTDOWN)
                    return;
                SDL_Delay(100);
            }
            SDL_LockMutex(robot_lock[thread_index]);
        }
        RobotJob job;
        {
            SDL_LockMutex(level_progress_lock);
            std::vector<RobotJob> jobs_todo;


                    
            for (unsigned i = 0; i < level_progress[game_mode][current_level_group_index][current_level_set_index].level_status.size(); i++)
            {
                LevelStatus p = level_progress[game_mode][current_level_group_index][current_level_set_index].level_status[i];
                if (!p.done && !p.robot_done)
                {
                    jobs_todo.push_back(RobotJob{current_level_group_index, current_level_set_index, i});
                }
            }
            if (jobs_todo.empty())
            {
                for (unsigned s = 0; s < level_progress[game_mode][current_level_group_index].size(); s++)
                {
                    if (!level_is_accessible(game_mode, current_level_group_index, s))
                        continue;
                    for (unsigned i = 0; i < level_progress[game_mode][current_level_group_index][s].level_status.size(); i++)
                    {
                        LevelStatus p = level_progress[game_mode][current_level_group_index][s].level_status[i];
                        if (!p.done && !p.robot_done)
                        {
                            jobs_todo.push_back(RobotJob{current_level_group_index, s, i});
                        }
                    }
                }
            }
            if (jobs_todo.empty())
            {
                for (unsigned g = 0; g < (GLBAL_LEVEL_SETS + 1); g++)
                {
                    for (unsigned s = 0; s < level_progress[game_mode][g].size(); s++)
                    {
                        if (!level_is_accessible(game_mode, g, s))
                            continue;
                        for (unsigned i = 0; i < level_progress[game_mode][g][s].level_status.size(); i++)
                        {
                            LevelStatus p = level_progress[game_mode][g][s].level_status[i];
                            if (!p.done && !p.robot_done)
                            {
                                jobs_todo.push_back(RobotJob{g, s, i});
                            }
                        }
                    }
                }
            }
            if (jobs_todo.empty())
            {
                SDL_UnlockMutex(level_progress_lock);
                SDL_Delay(100);
                continue;
            }
            job = jobs_todo[rnd % jobs_todo.size()];
            level_progress[game_mode][job.level_group_index][job.level_set_index].level_status[job.level_index].robot_done = 1;
            level_progress[game_mode][job.level_group_index][job.level_set_index].level_status[job.level_index].robot_regions = 0;
            SDL_UnlockMutex(level_progress_lock);
        }

        Grid* grid = Grid::Load((job.level_group_index == GLBAL_LEVEL_SETS) ?
                        server_levels[job.level_set_index][job.level_index] :
                        global_level_sets[job.level_group_index][job.level_set_index]->levels[job.level_index]);

        while (true)
        {
            int rep = advance_grid(grid, rules[game_mode], NULL);

            if (rep == 0)
            {
                if (grid->is_solved())
                {
                    SDL_LockMutex(level_progress_lock);
                    if (!level_progress[game_mode][job.level_group_index][job.level_set_index].level_status[job.level_index].done)
                    {
                        level_progress[game_mode][job.level_group_index][job.level_set_index].count_todo--;
                        level_progress[game_mode][job.level_group_index][job.level_set_index].level_status[job.level_index].done = true;
                    }
                    SDL_UnlockMutex(level_progress_lock);
                }
                level_progress[game_mode][job.level_group_index][job.level_set_index].level_status[job.level_index].robot_done = 2;
                break;
            }
            {
                int region_count = grid->regions.size();
                if(rule_limit_count >= 0 && region_count > rule_limit_count)
                {
                    level_progress[game_mode][job.level_group_index][job.level_set_index].level_status[job.level_index].robot_done = 2;
                    break;
                }
                else
                    level_progress[game_mode][job.level_group_index][job.level_set_index].level_status[job.level_index].robot_regions = region_count;
            }
            if (!run_robots)
                break;
        }
        grid->commit_level_counts();
        delete grid;
    }
}

void GameState::advance(int steps)
{
    if (!run_robots && should_run_robots)
    {
        for (unsigned g = 0; g < GLBAL_LEVEL_SETS + 1; g++)
        {
            for (unsigned s = 0; s < level_progress[game_mode][g].size(); s++)
            {
                for (unsigned i = 0; i < level_progress[game_mode][g][s].level_status.size(); i++)
                {
                    level_progress[game_mode][g][s].level_status[i].robot_done = 0;
                }
            }
        }
        run_robots = true;
    }
    if (display_help || display_menu)
        return;
    // if(grid->regions.size() > 700)
    //     _exit(1);
    if (rule_limit_count >= 0)
        if(int(grid->regions.size()) > rule_limit_count)
            if (auto_progress && !grid->is_solved())
                skip_level = true;
    frame = frame + steps;
    frame_step = steps;
    sound_frame_index = std::min(sound_frame_index + steps, 500);
    clipboard_check -= steps;
    if (clipboard_check < 0)
    {
        clipboard_check = 1000;
        check_clipboard();
    }
    deal_with_scores();
    if (server_timeout)
        server_timeout -= std::min(unsigned(steps), server_timeout);
    

    const std::string ach_set_names[] = {"HEXAGON", "SQUARE", "TRIANGLE", "GRID"};
    int totcount = 0;
    for (int s = 0; s < GLBAL_LEVEL_SETS; s++)
    {
        int ccount = 0;
        int count = 0;
        for (unsigned i = 0; i < level_progress[0][s].size(); i++)
        {
            for (unsigned j = 0; j < level_progress[0][s][i].level_status.size(); j++)
            {
                if (level_progress[game_mode][s][i].level_status[j].done)
                    ccount++;
                for (unsigned m = 0; m < GAME_MODES; m++)
                {
                    if (level_progress[m][s][i].level_status[j].done)
                    {
                        count++;
                        break;
                    }
                }
            }
        }
        int ach_sizes[] = {2000, 3000, 4000, 5000, 5500};
        int ach_cnt = *(&ach_sizes + 1) - ach_sizes;
        for (int i = 0; i < ach_cnt; i++)
        {
            if (count >= ach_sizes[i])
            {
                std::string name = ach_set_names[s] + std::to_string(ach_sizes[i]);
                achievements.insert(name);
            }
        }
        totcount += count;
        if (ccount >= 5500 && game_mode)
        {
            const std::string gm_ach_set_names[] = {"THREE", "SIXTY", "NOVAR"};
            achievements.insert(gm_ach_set_names[game_mode - 1]);
        }
    }
    if(max_stars >= prog_stars[PROG_LOCK_VARS5])
        achievements.insert("ALPHABET");
    if(grid->regions.size() > 1000)
        achievements.insert("EXPLOSION");
    for (GridRegion& r : grid->regions)
    {
        if (r.type == RegionType(RegionType::EQUAL, 20))
        {
            achievements.insert("ONE_SCORE");
            break;
        }
    }

    cur_stars = totcount;
    if (max_stars < totcount)
        max_stars = totcount;

    if (!(load_level || skip_level) && !force_load_level && grid->is_solved() && !current_level_is_temp)
    {
        skip_level = 1;
        SDL_LockMutex(level_progress_lock);
        if (!level_progress[game_mode][current_level_group_index][current_level_set_index].level_status[current_level_index].done)
        {
            level_progress[game_mode][current_level_group_index][current_level_set_index].count_todo--;
            level_progress[game_mode][current_level_group_index][current_level_set_index].level_status[current_level_index].done = true;
            {
                if (level_progress[game_mode][current_level_group_index][current_level_set_index].count_todo)
                {
                    if (sound_frame_index > 0)
                    {
                        sound_success_round_robin = (sound_success_round_robin + 1) % 32;
                        if (has_sound)
                            Mix_PlayChannel(sound_success_round_robin, sounds[8], 0);
                        sound_frame_index -= 100;
                    }
                }
                else
                {
                    sound_success_round_robin = (sound_success_round_robin + 1) % 32;
                    if (has_sound)
                        Mix_PlayChannel(16, sounds[9], 0);
                }
            }
        }
        SDL_UnlockMutex(level_progress_lock);
    }

    if (load_level || skip_level)
    {
        clear_overlay();
        clue_solves.clear();
        if (force_load_level || level_progress[game_mode][current_level_group_index][current_level_set_index].count_todo)
        {
            if (!force_load_level)
            {
                do
                {
                    if (load_level)
                    {
                        load_level = false;
                        continue;
                    }
                    else if (skip_level < 0)
                    {
                        if (current_level_index == 0)
                            current_level_index = level_progress[game_mode][current_level_group_index][current_level_set_index].level_status.size();
                        current_level_index--;
                    }
                    else
                    {
                        current_level_index++;
                        if (current_level_index >= level_progress[game_mode][current_level_group_index][current_level_set_index].level_status.size())
                        {
                            current_level_index = 0;
                            auto_progress = false;
                        }
                    }
                }
                while (level_progress[game_mode][current_level_group_index][current_level_set_index].level_status[current_level_index].done);
            }

            std::string& s = (current_level_group_index == GLBAL_LEVEL_SETS) ?
                        server_levels[current_level_set_index][current_level_index] :
                        global_level_sets[current_level_group_index][current_level_set_index]->levels[current_level_index];
            load_grid(s);
        }
        else
            auto_progress = false;
        skip_level = 0;
        load_level = false;
        force_load_level = false;
    }

    if (auto_progress_all && !auto_progress)
    {
        while (true)
        {
            current_level_set_index += 5;
            if (level_progress[game_mode][current_level_group_index].size() <= current_level_set_index)
            {
                current_level_set_index = current_level_set_index % 5 + 1;
                if (current_level_set_index == 5)
                {
                    current_level_set_index = 0;
                    current_level_index = 0;
                    auto_progress_all = false;
                    auto_progress = false;
                    load_level = true;
                    break;
                }
            }
            if (level_is_accessible(game_mode, current_level_group_index, current_level_set_index))
            {
                load_level = true;
                current_level_index = 0;
                auto_progress = true;
                break;
            }
        }
    }



    if(clue_solves.empty())
        get_hint = false;
    steps_had += steps;

    if (get_hint)
    {
        if (steps_had < 250)
            return;
        steps_had -= 250;
        get_hint = false;
        std::vector<GridRegion*> my_regions;
        for (GridRegion& r : grid->regions)
            my_regions.push_back(&r);

        std::random_device rd;
        std::mt19937 rnd_gen(rd());
        std::shuffle(my_regions.begin(), my_regions.end(), rnd_gen);

        for (GridRegion* r_ptr : my_regions)
        {
            GridRegion& r = *r_ptr;
            if (r.visibility_force == GridRegion::VIS_FORCE_NONE && r.vis_level == GRID_VIS_LEVEL_SHOW)
            {
                get_hint = true;
                r.visibility_force = GridRegion::VIS_FORCE_HINT;
                r.vis_cause.rule = NULL;
                r.vis_level = GRID_VIS_LEVEL_HIDE;

                std::set<XYPos> new_clue_solves;

                for (const XYPos& pos : clue_solves)
                {
                    if (grid->is_determinable_using_regions(pos, true))
                        new_clue_solves.insert(pos);
                }
                if (new_clue_solves.empty())
                {
                    r.vis_level = GRID_VIS_LEVEL_SHOW;
                }
                else
                {
                    clue_solves = new_clue_solves;
                    return;
                }
            }
        }
    }

    if (speed_dial == 0)
    {
        steps_had = 0;
        return;
    }

    unsigned oldtime = SDL_GetTicks();
    for (GridRule& rule : rules[game_mode])
    {
        if (rule.deleted)
            continue;
        if (rule.stale)
            continue;
        if (rule.paused)
            continue;
        unsigned oldtime = SDL_GetTicks();
        Grid::ApplyRuleResp resp  = grid->apply_rule(rule, (GridRegion*) NULL);
        unsigned newtime = SDL_GetTicks();
        if (newtime - oldtime)
            rule.cpu_time += newtime - oldtime;
        if (resp == Grid::APPLY_RULE_RESP_HIT)
            return;
        rule.stale = true;
        
    }

    while (true)
    {
        unsigned diff = SDL_GetTicks() - oldtime;
        if (diff > 20)
        {
            steps_had = 0;
            return;
        }
        double steps_needed = 1 * std::pow(100, ((1 - speed_dial) * 2));
        if (steps_had < steps_needed)
            return;
        steps_had -= steps_needed;

        int rep = advance_grid(grid, rules[game_mode], inspected_region);

        if (rep == 0)
        {
            if (auto_progress && !grid->is_solved())
                skip_level = 1;
        }

        if (rep == 1)
        {
            if (sound_frame_index > 100)
            {
                sound_success_round_robin = (sound_success_round_robin + 1) % 32;
                if (has_sound)
                    Mix_PlayChannel(sound_success_round_robin, sounds[rnd % 8], 0);
                sound_frame_index -= 100;
            }
        }

        if (rep == 2)
        {
            clue_solves.clear();
        }
    }
}

static int advance_grid(Grid* grid, std::list<GridRule> &rules, GridRegion *inspected_region)
{
    grid->add_base_regions();
    // for (GridRegion& r : grid->regions)
    //     r.stale = true;
    
    GridRegion* new_region = grid->add_one_new_region(inspected_region);

    if (!new_region)
        return 0;

    if (!new_region->stale)
    {
        for (int i = 1; i < 3; i++)
        {
            for (GridRule& rule : rules)
            {
                if (rule.deleted)
                    continue;
                if (rule.paused)
                    continue;
                if (rule.apply_region_type.type == RegionType::VISIBILITY && rule.apply_region_type.value == i)
                {
                    unsigned oldtime = SDL_GetTicks();
                    Grid::ApplyRuleResp resp  = grid->apply_rule(rule, new_region, false);
                    unsigned newtime = SDL_GetTicks();
                    if (newtime - oldtime)
                        rule.cpu_time += newtime - oldtime;
                    if (resp == Grid::APPLY_RULE_RESP_HIT)
                        break;
                }
            }
        }
        return 2;
    }

    if (new_region->vis_level != GRID_VIS_LEVEL_BIN)
    {
        for (GridRule& rule : rules)
        {
            if (rule.paused)
                continue;
            if (rule.deleted)
                continue;
            if (rule.apply_region_type.type == RegionType::VISIBILITY)
                continue;
            if (rule.apply_region_type.type == RegionType::SET)
                continue;
            {
                unsigned oldtime = SDL_GetTicks();
                grid->apply_rule(rule, new_region);
                unsigned newtime = SDL_GetTicks();
                if (newtime - oldtime)
                    rule.cpu_time += newtime - oldtime;
            }
        }
    }

    if (!new_region->deleted)
    {
        for (GridRule& rule : rules)
        {
            if (rule.deleted)
                continue;
            if (rule.paused)
                continue;
            if (rule.apply_region_type.type != RegionType::SET)
                continue;
            unsigned oldtime = SDL_GetTicks();
            Grid::ApplyRuleResp resp  = grid->apply_rule(rule, new_region);
            unsigned newtime = SDL_GetTicks();
            if (newtime - oldtime)
                rule.cpu_time += newtime - oldtime;
            if (resp == Grid::APPLY_RULE_RESP_HIT)
            {
                new_region->stale = false;
                for (GridRegion& r : grid->regions)
                {
                    if (r.vis_cause.rule && (
                        (r.vis_cause.regions[0] && r.vis_cause.regions[0]->deleted) ||
                        (r.vis_cause.regions[1] && r.vis_cause.regions[1]->deleted) ||
                        (r.vis_cause.regions[2] && r.vis_cause.regions[2]->deleted) ||
                        (r.vis_cause.regions[3] && r.vis_cause.regions[3]->deleted) ))
                    {
                        r.vis_cause = GridRegionCause();
                        GridVisLevel prev = r.vis_level;
                        r.vis_level = GRID_VIS_LEVEL_SHOW;
                        for (int i = 1; i < 3; i++)
                        {
                            for (GridRule& rule : rules)
                            {
                                if (rule.deleted)
                                    continue;
                                if (rule.paused)
                                    continue;
                                if (rule.apply_region_type.type == RegionType::VISIBILITY && rule.apply_region_type.value == i)
                                {
                                    unsigned oldtime = SDL_GetTicks();
                                    grid->apply_rule(rule, &r, false);
                                    unsigned newtime = SDL_GetTicks();
                                    if (newtime - oldtime)
                                        rule.cpu_time += newtime - oldtime;
                                }
                            }
                        }
                        if ((r.vis_level != prev) && (prev == GRID_VIS_LEVEL_BIN))
                            r.stale = false;
                    }
                }
                return 1;
            }
        }
    }
    return 2;
}

void GameState::audio()
{
}

void GameState::set_language(std::string lang)
{
    language = lang;
    if (!lang_data->has_key(language))
        language = "English";
    std::string filename = lang_data->get_item(language)->get_map()->get_string("font");
    font = fonts[filename];
}

void GameState::rule_gen_undo()
{
    if (right_panel_mode != RIGHT_MENU_RULE_GEN)
    {
        right_panel_mode = RIGHT_MENU_RULE_GEN;
    }
    else if (!constructed_rule_undo.empty())
    {
        constructed_rule_redo.push_front(ConstructedRuleState(constructed_rule, rule_gen_region));
        ConstructedRuleState& s = constructed_rule_undo.front();
        constructed_rule = s.rule;
        std::copy(s.regions, s.regions + 4, rule_gen_region);
        constructed_rule_undo.pop_front();
    }
    update_constructed_rule();
}

void GameState::rule_gen_redo()
{
    if (right_panel_mode != RIGHT_MENU_RULE_GEN)
    {
        right_panel_mode = RIGHT_MENU_RULE_GEN;
        if (constructed_rule.region_count)
            return;
    }
    if (!constructed_rule_redo.empty())
    {
        constructed_rule_undo.push_front(ConstructedRuleState(constructed_rule, rule_gen_region));
        ConstructedRuleState& s = constructed_rule_redo.front();
        constructed_rule = s.rule;
        std::copy(s.regions, s.regions + 4, rule_gen_region);
        constructed_rule_redo.pop_front();
    }
    update_constructed_rule();
}

void GameState::reset_rule_gen_region()
{
    rule_gen_region[0] = NULL;
    rule_gen_region[1] = NULL;
    rule_gen_region[2] = NULL;
    rule_gen_region[3] = NULL;
    replace_rule = NULL;
    duplicate_rule = false;

//    rule_gen_region_count = 0;
//    rule_gen_region_undef_num = 0;
    constructed_rule.apply_region_bitmap = 0;
    constructed_rule.priority = 0;
    constructed_rule.paused = false;

    constructed_rule.paused = false;
    constructed_rule.import_rule_gen_regions(rule_gen_region[0], rule_gen_region[1], rule_gen_region[2], rule_gen_region[3]);
    constructed_rule_undo.clear();
    constructed_rule_redo.clear();
    update_constructed_rule();
    if (right_panel_mode == RIGHT_MENU_RULE_GEN || right_panel_mode == RIGHT_MENU_REGION)
        right_panel_mode = RIGHT_MENU_NONE;
}

void GameState::update_constructed_rule_pre()
{
    constructed_rule_redo.clear();
    constructed_rule_undo.push_front(ConstructedRuleState(constructed_rule, rule_gen_region));
}

void GameState::update_constructed_rule()
{

    if (game_mode == 1 && constructed_rule.region_count == 4)
        reset_rule_gen_region();
    if (game_mode == 3)
    {
        for (int i = 0; i < constructed_rule.region_count; i++)
            if (constructed_rule.region_type[i].var)
                reset_rule_gen_region();
        for (int i = 0; i < (1 << constructed_rule.region_count); i++)
            if (constructed_rule.square_counts[i].var)
                reset_rule_gen_region();
        if (constructed_rule.apply_region_bitmap && constructed_rule.apply_region_type.var)
            reset_rule_gen_region();
    } 
    if (!constructed_rule.apply_region_bitmap)
        constructed_rule_is_logical = GridRule::OK;
    else
        constructed_rule_is_logical = constructed_rule.is_legal(rule_illogical_reason);
    constructed_rule_is_already_present = NULL;

    std::vector<int> order;
    for(int i = 0; i < 4; i++)
        if (i >= constructed_rule.region_count)
            rule_gen_region[i] = NULL;

    for(int i = 0; i < constructed_rule.region_count; i++)
        order.push_back(i);
    do{

        GridRule prule = constructed_rule.permute(order);
        for (GridRule& rule : rules[game_mode])
        {
            if (!rule.deleted && rule.covers(prule))
            {
                constructed_rule_is_already_present = &rule;
            }
        }
    }
    while(std::next_permutation(order.begin(),order.end()));

    if (constructed_rule_is_logical == GridRule::OK && constructed_rule.apply_region_type.type != RegionType::VISIBILITY)
    {
        int rule_cnt = 0;
        for (GridRule& rule : rules[game_mode])
        {
            if (!rule.deleted && rule.apply_region_type.type != RegionType::VISIBILITY)
                rule_cnt++;
        }
        rule_cnt += rule_del_count[game_mode];

        if (game_mode == 2 && rule_cnt >= 60)
            constructed_rule_is_logical = GridRule::LIMIT;
        if (game_mode == 3 && rule_cnt >= 300)
            constructed_rule_is_logical = GridRule::LIMIT;
    }
}

static void set_region_colour(SDL_Texture* sdl_texture, unsigned type, unsigned col, unsigned fade)
{
    uint8_t r = (type & 1) ? 128 : 255;
    uint8_t g = (type & 2) ? 128 : 255;
    uint8_t b = (type & 4) ? 128 : 255;

    r -= (col & 0x1) ? 60 : 0;
    g -= (col & 0x2) ? 60 : 0;
    b -= (col & 0x4) ? 60 : 0;
    r -= (col & 0x8) ? 30 : 0;
    g -= (col & 0x10) ? 30 : 0;
    b -= (col & 0x20) ? 30 : 0;

    r = (int(r) * (fade)) / 255;
    g = (int(g) * (fade)) / 255;
    b = (int(b) * (fade)) / 255;

    SDL_SetTextureColorMod(sdl_texture, r, g, b);
}

void GameState::render_region_bg(GridRegion& region, std::map<XYPos, int>& taken, std::map<XYPos, int>& total_taken, std::vector<WrapPos>& wraps, int disp_type)
{
    std::vector<XYPos> elements;
    std::vector<int> element_sizes;

    unsigned anim_prog = grid_regions_animation[&region];
    unsigned max_anim_frame = 500;
    double fade = 1.0 - (double(anim_prog) / double(max_anim_frame));
    int opac = std::min(int(contrast * (1.0 - pow(fade, 0.5))), int(contrast / 5 + ((grid_regions_fade[&region] * contrast * 4 / 5) / max_anim_frame)));

    FOR_XY_SET(pos, region.elements)
    {
        XYRect d = grid->get_bubble_pos(pos, grid_pitch, taken[pos], total_taken[pos]);
        XYPos n = d.pos + d.size / 2;
        elements.push_back(n);
        element_sizes.push_back(std::min(d.size.x, d.size.y));
        taken[pos]++;
        for(WrapPos r : wraps)
        {
            if (XYPosFloat(mouse - grid_offset - r.pos - (d.pos + d.size / 2) * r.size).distance() <= (d.size.x / 2 * r.size))
            {
                if ((mouse - grid_offset).inside(XYPos(grid_size,grid_size)))
                {
                    if (!display_menu)
                    {
                        mouse_hover_region = &region;
                        if (!grid_dragging)
                            mouse_cursor = SDL_SYSTEM_CURSOR_HAND;
                    }
                }
            }
        }
    }
    if (region.elements.count() <= 1)
        return;

    bool selected = (&region == mouse_hover_region);
    if (disp_type >= 1 && !selected)
        return;
    int siz = elements.size();

    std::vector<int> group;
    std::vector<std::vector<std::pair<double, WrapPos*>>> distances;
    distances.resize(siz);
    group.resize(siz);


    for (int i = 0; i < siz; i++)
    {
        group[i] = i;
        distances[i].resize(siz);
        for (int j = 0; j < siz; j++)
        {
            if (i == j)
                continue;
            distances[i][j] = std::make_pair(std::numeric_limits<double>::infinity(), (WrapPos*)NULL);
            for(WrapPos& r : wraps)
            {
                // if (&r != &wraps[0])
                //     continue;
                if (r.size > 1)
                    continue;
                double dist = (XYPosFloat(elements[j] * r.size + r.pos) - (elements[i] + wraps[0].pos)).distance() / sqrt(r.size);
                if (dist < distances[i][j].first)
                {
                    distances[i][j] = std::make_pair(dist, &r);
                }

            }
        }
    }

    while (true)
    {
        XYPos best_con;
        WrapPos* best_wrap = NULL;
        double best_distance = std::numeric_limits<double>::infinity();
        for (int i = 0; i < siz; i++)
        {
            for (int j = 0; j < siz; j++)
            {
                if (group[i] == group[j])
                    continue;
                if (distances[i][j].first < best_distance)
                {
                    best_distance = distances[i][j].first;
                    best_con = XYPos(i,j);
                    best_wrap = distances[i][j].second;
                }
            }
        }
//        best_wrap = &wraps[0];
        {
            assert(best_wrap);
            XYPos pos = elements[best_con.x];
            XYPos last = elements[best_con.y] * best_wrap->size + best_wrap->pos - wraps[0].pos;
            double dist = XYPosFloat(pos - last).distance();
            double angle = XYPosFloat(pos).angle(XYPosFloat(last));
            int s = std::min(element_sizes[best_con.x], int(element_sizes[best_con.y] * best_wrap->size));
            int line_thickness = std::min(s/4, (scaled_grid_size / std::min(grid->size.x, grid->size.y)) / 32);


            if ((disp_type == 1) && selected)
            {
                line_thickness *= 2;
                set_region_colour(sdl_texture, region.type.value, region.colour, opac);
                double f = (frame / 5 + pos.x);
                SDL_Rect src_rect1 = {int(f)% 1024, 2528, std::min(int(dist / line_thickness * 10), 1024), 32};
                f = (frame / 8 + pos.x);
                SDL_Rect src_rect2 = {1024 - int(f) % 1024, 2528, std::min(int(dist / line_thickness * 10), 1024), 32};
                for(WrapPos r : wraps)
                {
                    XYPos s = r.pos + pos * r.size;
                    XYPos e = r.pos + last * r.size;
                    if (s.x < -line_thickness && e.x < -line_thickness) continue;
                    if (s.y < -line_thickness && e.y < -line_thickness) continue;
                    if (s.x > grid_size + line_thickness && e.x > grid_size + line_thickness) continue;
                    if (s.y > grid_size + line_thickness && e.y > grid_size + line_thickness) continue;
                    s += grid_offset;
                    SDL_Point rot_center = {0, int(line_thickness * r.size)};
                    SDL_Rect dst_rect = {s.x, s.y - int(line_thickness * r.size), int(dist * r.size), int(line_thickness * 2 * r.size)};
                    SDL_RenderCopyEx(sdl_renderer, sdl_texture, &src_rect1, &dst_rect, degrees(angle), &rot_center, SDL_FLIP_NONE);
                    SDL_RenderCopyEx(sdl_renderer, sdl_texture, &src_rect2, &dst_rect, degrees(angle), &rot_center, SDL_FLIP_NONE);
                }
            }

            if ((disp_type == 0) || ((disp_type == 2) && selected))
            {
                set_region_colour(sdl_texture, region.type.value, region.colour, opac);
                SDL_Rect src_rect = {160, 608, 1, 1};
                for(WrapPos r : wraps)
                {
                    XYPos s = r.pos + pos * r.size;
                    XYPos e = r.pos + last * r.size;
                    if (s.x < -line_thickness && e.x < -line_thickness) continue;
                    if (s.y < -line_thickness && e.y < -line_thickness) continue;
                    if (s.x > grid_size + line_thickness && e.x > grid_size + line_thickness) continue;
                    if (s.y > grid_size + line_thickness && e.y > grid_size + line_thickness) continue;
                    s += grid_offset;

                    SDL_Point rot_center = {0, int(line_thickness * r.size)};
                    SDL_Rect dst_rect = {s.x, s.y - int(line_thickness * r.size), int(dist * r.size), int(line_thickness * 2 * r.size)};
                    SDL_RenderCopyEx(sdl_renderer, sdl_texture, &src_rect, &dst_rect, degrees(angle), &rot_center, SDL_FLIP_NONE);
                }
            }
        }

        int from = group[best_con.x];
        int to = group[best_con.y];
        bool diff = false;
        for (int i = 0; i < siz; i++)
        {
            if (group[i] == from)
                group[i] = to;
            if (group[i] != to)
                diff = true;
        }

        if (!diff)
            break;
    }

    SDL_SetTextureColorMod(sdl_texture, contrast, contrast, contrast);
}

void GameState::render_region_fg(GridRegion& region, std::map<XYPos, int>& taken, std::map<XYPos, int>& total_taken, std::vector<WrapPos>& wraps, int disp_type)
{
    bool selected = (&region == mouse_hover_region);

    unsigned anim_prog = grid_regions_animation[&region];
    unsigned max_anim_frame = 500;
    double fade = 1.0 - (double(anim_prog) / double(max_anim_frame));
    double wob = -sin((10 / (fade + 0.3))) * (fade * fade) / 2;
    int opac = std::min(int(contrast * (1.0 - pow(fade, 0.5))), int(contrast / 5 + ((grid_regions_fade[&region] * contrast * 4 / 5) / max_anim_frame)));

    // sq_pos.pos -= XYPosFloat(sq_pos.size) * (wob / 2);
    // sq_pos.size += XYPosFloat(sq_pos.size) * (wob);

    FOR_XY_SET(pos, region.elements)
    {
        XYRect d = grid->get_bubble_pos(pos, grid_pitch, taken[pos], total_taken[pos]);
        d.pos -= XYPosFloat(d.size) * (wob / 2);
        d.size += XYPosFloat(d.size) * (wob);

        taken[pos]++;
        if (!selected && disp_type)
            continue;
        if ((disp_type == 1) && selected)
        {
            set_region_colour(sdl_texture, region.type.value, region.colour, opac);
            XYPos margin = d.size / 8;
            SDL_Rect src_rect = {512, 1728, 192, 192};
            int fr = frame + pos.x * 1040 + pos.y * 100;
            for(WrapPos r : wraps)
            {
                for (int i = 0; i < 8; i++)
                {
                    XYPos blobs[8] = {{200,200},{-233,-310},{-190,-410},{210,-309},{230,210},{-273,340},{-390,-370},{313,-319}};
                    XYPos p = r.pos + (d.pos - (margin / 2) + XYPosFloat(margin) * XYPosFloat(sin(float(fr) / blobs[i].x), cos(float(fr) / blobs[i].y))) * r.size;
                    XYPos s = (d.size + margin)* r.size;
                    p += grid_offset;
                    SDL_Point rot_center = {s.x / 2, s.y / 2};
                    SDL_Rect dst_rect = {p.x, p.y, s.x, s.y};
                    SDL_RenderCopyEx(sdl_renderer, sdl_texture, &src_rect, &dst_rect, (double(frame) / (blobs[i].x * blobs[i].y)) * 3600, &rot_center, SDL_FLIP_NONE);
                }
            }
        }
        if ((disp_type == 0) || ((disp_type == 2) && selected))
        {
            set_region_colour(sdl_texture, region.type.value, region.colour, opac);
            SDL_Rect src_rect = {64, 512, 192, 192};
            for(WrapPos r : wraps)
            {
                XYPos p = r.pos + d.pos * r.size;
                XYPos s = d.size * r.size;
                if(p.x + s.x < 0) continue;
                if(p.y + s.y < 0) continue;
                if(p.x > grid_size) continue;
                if(p.y > grid_size) continue;
                p += grid_offset;
                SDL_Rect dst_rect = {p.x, p.y, s.x, s.y};
                SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            }

            SDL_SetTextureColorMod(sdl_texture, 0,0,0);
            for(WrapPos r : wraps)
            {
                XYPos p = r.pos + d.pos * r.size;
                XYPos s = d.size * r.size;
                if(p.x + s.x < 0) continue;
                if(p.y + s.y < 0) continue;
                if(p.x > grid_size) continue;
                if(p.y > grid_size) continue;
                p+= grid_offset;
                render_region_type(region.type, p, s.x);
            }
        }
    }

    SDL_SetTextureColorMod(sdl_texture, contrast, contrast, contrast);

}

void GameState::render_text_box(XYPos pos, std::string& s, bool left, int force_width)
{
    SDL_Color color = {contrast, contrast, contrast};
    std::vector<SDL_Surface*> text_surfaces;
    std::vector<SDL_Texture*> textures;

    std::string text = s;
    std::string::size_type start = 0;
    XYPos text_box_size;

    while (true)
    {
        std::string::size_type eol = text.find("\n", start);
        std::string line = text.substr(start, eol - start);

        SDL_Surface* text_surface = TTF_RenderUTF8_Blended(font, line.c_str(), color);
        text_surfaces.push_back(text_surface);
        SDL_Texture* new_texture = SDL_CreateTextureFromSurface(sdl_renderer, text_surface);
        textures.push_back(new_texture);
        SDL_Rect txt_rect;
        SDL_GetClipRect(text_surface, &txt_rect);
        text_box_size.x = std::max(text_box_size.x, txt_rect.w * button_size / 64);
        text_box_size.y += txt_rect.h * button_size / 64;
        if (eol == std::string::npos)
            break;
        start = eol + 1;
    }

    int border = button_size / 16;
    XYPos box_pos = pos;
    pos += XYPos(border, border);
    XYPos box_size = text_box_size + XYPos(border * 4, border * 2);

    if (left)
        box_pos.x -= box_size.x;
    if (box_pos.x < 0)
        box_pos.x = 0;
    if (force_width > 0)
        box_size.x = force_width;

    render_box(box_pos, box_size, button_size / 4, 1);
    XYPos txt_pos = box_pos + XYPos(border * 2, border);

    for (unsigned i = 0; i < textures.size(); i++)
    {
        SDL_Rect txt_rect;
        SDL_GetClipRect(text_surfaces[i], &txt_rect);
        SDL_Rect dst_rect;
        dst_rect.w = txt_rect.w * button_size / 64;
        dst_rect.h = txt_rect.h * button_size / 64;
        dst_rect.x = txt_pos.x;
        dst_rect.y = txt_pos.y;
        SDL_RenderCopy(sdl_renderer, textures[i], &txt_rect, &dst_rect);
        txt_pos.y += dst_rect.h;

        SDL_DestroyTexture(textures[i]);
        SDL_FreeSurface(text_surfaces[i]);
    }

}
std::string GameState::translate(std::string s)
{
    SaveObjectMap* lang = lang_data->get_item(language)->get_map();
    SaveObjectMap* trans = lang->get_item("translate")->get_map();
    if (trans->has_key(s))
        return trans->get_string(s);
    std::cout << s << std::endl;
    return s;
}
void GameState::render_tooltip()
{
    if (tooltip_rect.pos.x >= 0)
    {
        mouse_cursor = SDL_SYSTEM_CURSOR_HAND;
        render_box(tooltip_rect.pos, tooltip_rect.size, button_size / 4, 3);
    }
    if (tooltip_string != "")
    {
        std::string t = translate(tooltip_string);
        render_text_box(mouse + XYPos(-button_size / 4, button_size / 4), t, true);
    }
    if (mouse_cursor != prev_mouse_cursor)
    {
        prev_mouse_cursor = mouse_cursor;
        SDL_Cursor* cursor;
        cursor = SDL_CreateSystemCursor(mouse_cursor);
        SDL_SetCursor(cursor);
    }
}

void GameState::add_clickable_highlight(SDL_Rect& dst_rect)
{
    if ((mouse.x >= dst_rect.x) &&
        (mouse.x < (dst_rect.x + dst_rect.w)) &&
        (mouse.y >= dst_rect.y) &&
        (mouse.y < (dst_rect.y + dst_rect.h)))
        tooltip_rect = XYRect(dst_rect.x, dst_rect.y, dst_rect.w, dst_rect.h);
}

bool GameState::add_tooltip(SDL_Rect& dst_rect, const char* text, bool clickable)
{
    if ((mouse.x >= dst_rect.x) &&
        (mouse.x < (dst_rect.x + dst_rect.w)) &&
        (mouse.y >= dst_rect.y) &&
        (mouse.y < (dst_rect.y + dst_rect.h)))
    {
        tooltip_string = text;
        if (clickable)
            tooltip_rect = XYRect(dst_rect.x, dst_rect.y, dst_rect.w, dst_rect.h);
        return true;
    }
    return false;
}

void GameState::render_box(XYPos pos, XYPos size, int corner_size, int style)
{
        XYPos p = XYPos(320 + (style % 4) * 96, 416 + (style / 4) * 96);
        SDL_Rect src_rect = {p.x, p.y, 32, 32};
        SDL_Rect dst_rect = {pos.x, pos.y, corner_size, corner_size};
        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);

        src_rect = {p.x + 32, p.y, 1, 32};
        dst_rect = {pos.x + corner_size, pos.y, (size.x - corner_size * 2 ), corner_size};
        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);     //  Top

        src_rect = {p.x + 32, p.y, 32, 32};
        dst_rect = {pos.x + (size.x - corner_size), pos.y , corner_size, corner_size};
        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);     //  Top Right

        src_rect = {p.x, p.y + 32, 32, 1};
        dst_rect = {pos.x, pos.y + corner_size, corner_size, size.y - corner_size * 2};
        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect); // Left

        src_rect = {p.x + 32, p.y + 32, 1, 1};
        dst_rect = {pos.x + corner_size, pos.y + corner_size, size.x - corner_size * 2, size.y - corner_size * 2};
        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect); // Middle

        src_rect = {p.x + 32, p.y + 32, 32, 1};
        dst_rect = {pos.x + (size.x - corner_size), pos.y + corner_size, corner_size, size.y - corner_size * 2};
        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect); // Right

        src_rect = {p.x, p.y + 32, 32, 32};
        dst_rect = {pos.x, pos.y + (size.y - corner_size), corner_size, corner_size};
        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);    // Bottom left

        src_rect = {p.x + 32, p.y + 32, 1, 32};
        dst_rect = {pos.x + corner_size, pos.y + (size.y - corner_size), size.x - corner_size * 2, corner_size};
        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect); // Bottom

        src_rect = {p.x + 32, p.y + 32, 32, 32};
        dst_rect = {pos.x + (size.x - corner_size), pos.y + (size.y - corner_size), corner_size, corner_size};
        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect); // Bottom right
}

void GameState::render_number(unsigned num, XYPos pos, XYPos siz)
{
    std::string digits = std::to_string(num);
    render_number_string(digits, pos, siz);
}

// style 0 - normal, 1 - unclickable, 2 - warning, 3 - red
bool GameState::render_button(XYPos tpos, XYPos pos, const char* tooltip, int style, int size) 
{
    bool pressed = false;
    if (size == 0)
        size = button_size;
    int ds = 0;

    SDL_Rect src_rect = {192, 0, 192, 192};
    SDL_Rect dst_rect = {pos.x + ds, pos.y + ds, size - ds * 2, size - ds * 2};

    if (style == 0)
        SDL_SetTextureColorMod(sdl_texture, contrast * 0x2e / 255, contrast * 0xc7 / 255, contrast * 0x72 / 255);
    else if (style == 1)
        SDL_SetTextureColorMod(sdl_texture, contrast * 0x50 / 255, contrast * 0x50 / 255, contrast * 0x50 / 255);
    else if (style == 2)
        SDL_SetTextureColorMod(sdl_texture, contrast * 0xf0 / 255, contrast * 0xC0 / 255, contrast * 0x10 / 255);
    else if (style == 3)
        SDL_SetTextureColorMod(sdl_texture, contrast * 0xff / 255, contrast * 0x00 / 255, contrast * 0x00 / 255);

    bool hover =   ((mouse.x >= dst_rect.x) &&
                    (mouse.x < (dst_rect.x + dst_rect.w)) &&
                    (mouse.y >= dst_rect.y) &&
                    (mouse.y < (dst_rect.y + dst_rect.h)));

    if (hover)
    {
        mouse_cursor = SDL_SYSTEM_CURSOR_HAND;
        if (mouse_button_pressed)
        {
            if (tooltip == last_button_hovered)
                pressed = true;
        }
        else
            last_button_hovered = tooltip;
        SDL_SetTextureColorMod(sdl_texture, contrast * 0xf0 / 255, contrast * 0x90 / 255, contrast * 0x20 / 255);
    }

    if (pressed)
        src_rect.x = 0;
    SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
    SDL_SetTextureColorMod(sdl_texture, contrast, contrast, contrast);
    src_rect = {tpos.x, tpos.y, 192, 192};
    if (pressed)
        dst_rect.y += size / 16;

    SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
    dst_rect = {pos.x, pos.y, size, size};
    add_tooltip(dst_rect, tooltip, false);
    return hover;
}

static void get_char_texture(char c, char pc, int& pos, int &width, int &cwidth)
{
    cwidth = -1;
    if (c == '0')
    {
        pos = 0; width = 2;
    }
    else if (c == '1')
    {
        pos = 2;
        if (pc != '+')
        {
            width = 2; cwidth = 1;
        }
        else
        {
            width = 1; cwidth = 1;
        }
    }
    else if (c >= '2' && c <= '9')
    {
        pos = (c - '2') * 2 + 3; width = 2;
    }
    else if (c == '.' )
    {
        pos = 19; width = 1;
    }
    else if (c == '%' )
    {
        pos = 20; width = 3;
    }
    else if (c == '!' )
    {
        pos = 23; width = 1;
    }
    else if (c == '+' )
    {
        pos = 24; width = 2;
    }
    else if (c == '-' )
    {
        pos = 26; width = 2;
    }
    else if (c >= 'a' && c <= 'z')
    {
        pos = 28 + (c - 'a') * 2; width = 2;
    }
    else
        assert(0);
    if (cwidth < 0)
        cwidth = width;
}

void GameState::render_number_string(std::string digits, XYPos pos, XYPos siz, XYPos style)
{
    int width = 0;
    char pc = 0;
    for (unsigned i = 0; i < digits.size(); i++)
    {
        if (digits[i] == '^')
        {
            do i++;
            while (digits[i] != '^');
            continue;
        }
        char c = digits[i];
        int p;
        int w;
        int cw;
        get_char_texture(c, pc, p, w, cw);
        width += w;
        pc = c;
    }

    double s = std::min(double(siz.x) / width, siz.y / 3.0);
    XYPos t_size(s * width, s * 3);


    if (style.x > 0)
        pos.x += (siz.x - t_size.x);
    else if (style.x == 0)
        pos.x += (siz.x - t_size.x) / 2;
    if (style.y > 0)
        pos.y += (siz.y - t_size.y);
    else if (style.y == 0)
        pos.y += (siz.y - t_size.y) / 2;

    int texture_char_width = 64;
    int texture_char_pos = 2560;

    double cs = s;
    if (s < 0.5)
        return;
    else if (s <= 1)
    {
        cs = 1;
        s = 1;
        texture_char_width = 1;
        texture_char_pos = 2880;
    }
    else if (s <= 2)
    {
        cs = 2;
        s = 2;
        texture_char_width = 2;
        texture_char_pos = 2874;
    }
    else if (s <= 3)
    {
        cs = 3;
        s = 3;
        texture_char_width = 3;
        texture_char_pos = 2862;
    }
    // else if (s < 5)
    // {
    //     cs = 4;
    //     s = 4;
    //     texture_char_width = 4;
    //     texture_char_pos = 2848;
    // }
    else if (s < 8)
    {
        texture_char_width = 8;
        texture_char_pos = 2816;
    }
    else if (s < 24)
    {
        texture_char_width = 16;
        texture_char_pos = 2752;
    }

    pc = 0;
    for (unsigned i = 0; i < digits.size(); i++)
    {
        char c = digits[i];
        int p;
        int w;
        int cw;
        get_char_texture(c, pc, p, w, cw);
        pc = c;
        
        SDL_Rect src_rect = {p * texture_char_width, texture_char_pos, cw * texture_char_width, 3 * texture_char_width};
        SDL_Rect dst_rect = {pos.x + int((w - cw) * s / 2), pos.y, int(cw * cs), int(3 * cs)};

        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);

        if ((i + 1) < digits.size() && digits[i+1] == '^')
        {
            i+=2;
            std::string substr = "";
            while (i < digits.size() && digits[i] != '^')
            {
                substr += digits[i];
                i++;
            }
            render_number_string(substr, XYPos(pos.x, pos.y), XYPos(w * s, s * 1), XYPos(1, -1));
        }
        pos.x += int(w * s);
    }

}

void GameState::render_region_bubble(RegionType type, unsigned colour, XYPos pos, int siz, bool selected)
{
    set_region_colour(sdl_texture, type.value, colour, contrast);
    if (selected)
    {
        XYPos margin(siz / 8, siz / 8);
        SDL_Rect src_rect = {512, 1728, 192, 192};
        // SDL_Point rot_center = {siz / 2 + margin.y, siz / 2 + margin.x};
        // SDL_Rect dst_rect = {pos.x - margin.x, pos.y - margin.y, siz + margin.y * 2, siz + margin.y * 2};
        // SDL_RenderCopyEx(sdl_renderer, sdl_texture, &src_rect, &dst_rect, (double(frame) / 10000) * 360, &rot_center, SDL_FLIP_NONE);
        for (int i = 0; i < 8; i++)
        {
            static const XYPos blobs[8] = {{200,200},{-233,-310},{-190,-410},{210,-309},{230,210},{-273,340},{-390,-370},{313,-319}};
            XYPos p = pos + -(margin / 2) + XYPosFloat(margin) * XYPosFloat(sin(float(frame) / blobs[i].x), cos(float(frame) / blobs[i].y));
            XYPos s = XYPos(siz, siz) + margin;
            SDL_Point rot_center = {s.x / 2, s.y / 2};
            SDL_Rect dst_rect = {p.x, p.y, s.x, s.y};
            SDL_RenderCopyEx(sdl_renderer, sdl_texture, &src_rect, &dst_rect, (double(frame) / (blobs[i].x * blobs[i].y)) * 3600, &rot_center, SDL_FLIP_NONE);
        }



    }   
    SDL_Rect src_rect = {64, 512, 192, 192};
    SDL_Rect dst_rect = {pos.x, pos.y, int(siz), int(siz)};
    SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);

    SDL_SetTextureColorMod(sdl_texture, 0,0,0);
    render_region_type(type, pos, siz);
    SDL_SetTextureColorMod(sdl_texture, contrast, contrast, contrast);
}

void GameState::render_region_type(RegionType reg, XYPos pos, unsigned siz)
{
    if (reg.type == RegionType::XOR11 || reg.type == RegionType::XOR22)
    {
        XYPos numsiz = XYPos(siz * 0.9 * 2 / 8, siz * 0.9 * 3 / 8);
        render_number_string(reg.val_as_str(0),     pos + XYPos(int(siz) / 8,int(siz) / 8), numsiz);
        render_number_string(reg.val_as_str((reg.type == RegionType::XOR11) ? 1 : 2), pos - (numsiz / 2) + XYPos(int(siz) * 4 / 8,int(siz) * 4 / 8), numsiz);
        render_number_string(reg.val_as_str((reg.type == RegionType::XOR11) ? 2 : 4), pos - numsiz + XYPos(int(siz) * 7 / 8,int(siz) * 7 / 8), numsiz);
        SDL_Rect src_rect = {384, 736, 128, 128};
        SDL_Rect dst_rect = {pos.x + int(siz) / 4 + int(siz) / 20, pos.y + int(siz) * 2 / 8,  int(siz / 6), int(siz / 3)};
        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);

        dst_rect = {pos.x + int(siz) / 2 + int(siz) / 20, pos.y + int(siz) * 7 / 16,  int(siz / 6), int(siz / 3)};
        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
    }
    else if (reg.type == RegionType::XOR222)
    {
        XYPos numsiz = XYPos(siz * 5 / 16, siz * 5 / 16);
        render_number_string(reg.val_as_str(0), pos + XYPos(int(siz) / 8,int(siz) / 8), numsiz);
        render_number_string(reg.val_as_str(2), pos + XYPos(int(siz) * 9 / 16,int(siz) / 8), numsiz);
        render_number_string(reg.val_as_str(4), pos + XYPos(int(siz) / 8,int(siz) * 9 / 16), numsiz);
        render_number_string(reg.val_as_str(6), pos + XYPos(int(siz) * 9 / 16,int(siz) * 9 / 16), numsiz);
        SDL_Rect src_rect = {1280, 768, 192, 192};
        SDL_Rect dst_rect = {pos.x + int(siz) / 8, pos.y + int(siz) / 8,  int(siz * 6 / 8), int(siz * 6 / 8)};
        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
    }
    else if (reg.type == RegionType::XOR1 || reg.type == RegionType::XOR2 || reg.type == RegionType::XOR3)
    {
        XYPos numsiz = XYPos(siz * 2 * 1.35 / 8, siz * 3 * 1.35 / 8);

        render_number_string(reg.val_as_str(0), pos + XYPos(int(siz) / 8,int(siz) / 8), numsiz);
        render_number_string(reg.val_as_str(reg.type == RegionType::XOR3 ? 3 : (reg.type == RegionType::XOR2 ? 2 : 1)), pos - numsiz + XYPos(int(siz) * 7 / 8,int(siz) * 7 / 8), numsiz);

        SDL_Rect src_rect = {384, 736, 128, 128};
        SDL_Rect dst_rect = {pos.x + int(siz) / 3, pos.y + int(siz) / 4,  int(siz / 3), int(siz / 2)};
        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
    }
    else if (reg.type == RegionType::MORE || reg.type == RegionType::LESS)
    {
        XYPos numsiz = XYPos(siz * 6 / 8, siz * 6 / 8);

        std::string digits = reg.val_as_str();
        if (reg.type == RegionType::MORE)
            digits += '+';
        if (reg.type == RegionType::LESS)
            digits += '-';

        render_number_string(digits, pos + XYPos(int(siz) / 8, int(siz) / 8), numsiz);
    }
    else if (reg.type == RegionType::EQUAL)
    {
        XYPos numsiz = XYPos(siz * 6 / 8, siz * 6 / 8);
        render_number_string(reg.val_as_str(0), pos + XYPos(int(siz) / 8,int(siz) / 8), numsiz);

    }
    else if (reg.type == RegionType::NONE)
    {
        int border = siz / 8;
        SDL_Rect src_rect = {896, 192, 192, 192};
        SDL_Rect dst_rect = {pos.x + border / 2, pos.y + border / 2, (int)(siz - border), (int)(siz - border)};

        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
    }
    else if (reg.type == RegionType::NOTEQUAL)
    {
        XYPos numsiz = XYPos(siz * 6 / 8, siz * 6 / 8);
        std::string digits = "!" + reg.val_as_str(0);
        render_number_string(digits, pos + XYPos(int(siz) / 8,int(siz) / 8), numsiz);
    }
    else if (reg.type == RegionType::PARITY)
    {
        XYPos numsiz = XYPos(siz * 6 / 8, siz * 6 / 8);
        std::string digits = reg.val_as_str(0) + "+2f";
        if (!reg.var && !reg.value)
            digits = "2f";
        render_number_string(digits, pos + XYPos(int(siz) / 8,int(siz) / 8), numsiz);
    }
    else if (reg.type == RegionType::VISIBILITY)
    {
        if (reg.value > 2)
            return;
        SDL_Rect src_rect = {(reg.value == 1) ? 896 : 1088, 384, 192, 192};
        if (reg.value == 2)
        {
            src_rect.x = 512;
            src_rect.y = 768;
        }
        int border = siz / 8;
        SDL_Rect dst_rect = {pos.x + border, pos.y + border, (int)(siz - border * 2), (int)(siz - border * 2)};
        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
    }
    else if (reg.type == RegionType::SET)
    {
        SDL_Rect src_rect = {(reg.value == 1) ? 320 : 512, 192, 192, 192};
        int border = siz / 8;
        SDL_Rect dst_rect = {pos.x + border / 2, pos.y + border / 2, (int)(siz - border), (int)(siz - border)};
        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
    }
    else
    {
        assert(0);
    }


}
void GameState::render_star_burst(XYPos pos, XYPos size, int progress, bool lock)
{
    if (progress < 1000 && lock)
    {
        int s = size.x;
        SDL_Rect src_rect = {1088, 192, 192, 192};
        SDL_Rect dst_rect = {pos.x, pos.y + progress * size.x / 500, s, s};
        SDL_Point rot_center = {s / 2, s / 2};
        double star_angle = progress / 1000.0;
        SDL_SetTextureAlphaMod(sdl_texture, std::min (255, 1000 - progress));

        SDL_RenderCopyEx(sdl_renderer, sdl_texture, &src_rect, &dst_rect, degrees(star_angle), &rot_center, SDL_FLIP_NONE);
    }

    if (progress < 1000)
    {
        for (int i = 0; i < 10; i++)
        {
            int s = size.x * progress / 500;
            double angle = i;
            XYPosFloat p (Angle (angle), s);
            p.y += progress * progress * s / 1000000;
            SDL_Rect src_rect = {512, 960, 192, 192};
            SDL_Rect dst_rect = {pos.x + size.x / 2 - s / 2 + int(p.x), pos.y + size.y / 2 - s / 2 + int(p.y), s, s};
            SDL_Point rot_center = {s / 2, s / 2};
            double star_angle = progress / 100.0;
            SDL_SetTextureAlphaMod(sdl_texture, 250 - progress / 4);

            SDL_RenderCopyEx(sdl_renderer, sdl_texture, &src_rect, &dst_rect, degrees(star_angle), &rot_center, SDL_FLIP_NONE);
        }
    }

    for (int i = 0; i < 8; i++)
    {
        int s = size.x / 10;
        int p = int(i * 1000 / 4 + progress) % 1000;
        SDL_Rect src_rect = {512, 960, 192, 192};
        SDL_Rect dst_rect = {0, 0, s, s};
        SDL_Point rot_center = {s / 2, s / 2};
        double star_angle = progress / 300.0;
        XYPos ps(pos.x - s / 2, pos.y - s / 2);

        double t = (sin((i * 77) % 100 + progress / 100) + 1) / 2;
        if (progress > 4000)
            t *= 1.0 - (progress - 4000.0) / 1000;
        SDL_SetTextureAlphaMod(sdl_texture, t * 255);

        dst_rect.x = ps.x + (p * size.x) / 1000;
        dst_rect.y = ps.y;
        SDL_RenderCopyEx(sdl_renderer, sdl_texture, &src_rect, &dst_rect, degrees(star_angle), &rot_center, SDL_FLIP_NONE);
        dst_rect.x = ps.x + size.x;
        dst_rect.y = ps.y + (p * size.y) / 1000;
        SDL_RenderCopyEx(sdl_renderer, sdl_texture, &src_rect, &dst_rect, degrees(star_angle), &rot_center, SDL_FLIP_NONE);
        dst_rect.x = ps.x + size.x - (p * size.x) / 1000;
        dst_rect.y = ps.y + size.y;
        SDL_RenderCopyEx(sdl_renderer, sdl_texture, &src_rect, &dst_rect, degrees(star_angle), &rot_center, SDL_FLIP_NONE);
        dst_rect.x = ps.x;
        dst_rect.y = ps.y + size.y - (p * size.y) / 1000;
        SDL_RenderCopyEx(sdl_renderer, sdl_texture, &src_rect, &dst_rect, degrees(star_angle), &rot_center, SDL_FLIP_NONE);
    }

    SDL_SetTextureAlphaMod(sdl_texture, 255);
}

bool GameState::render_lock(int lock_type, XYPos pos, XYPos size)
{
    if (prog_seen[lock_type] >= PROG_ANIM_MAX)
        return true;

    if (prog_stars[lock_type] <= max_stars)
    {
        if (prog_seen[lock_type] < PROG_ANIM_MAX)
        {
            if (prog_seen[lock_type] == 0 && sound_frame_index > 0)
            {
                sound_success_round_robin = (sound_success_round_robin + 1) % 32;
                if (has_sound)
                    Mix_PlayChannel(sound_success_round_robin, sounds[9], 0);
                sound_frame_index -= 100;
            }
            star_burst_animations.push_back(AnimationStarBurst(pos, size, prog_seen[lock_type], true));
            prog_seen[lock_type] += frame_step;
        }
        return true;
    }
    if (max_stars < prog_stars[PROG_LOCK_LEVELS_AND_LOCKS])
        return false;
    int togo = prog_stars[lock_type] - cur_stars;
    int min_siz = std::min(size.x, size.y);
    XYPos offset = XYPos((size.x - min_siz) / 2, (size.y - min_siz) / 2);

    SDL_Rect src_rect = {1088, 192, 192, 192};
    SDL_Rect dst_rect = {pos.x + offset.x, pos.y + offset.y, min_siz, min_siz};
    SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);

    SDL_SetTextureColorMod(sdl_texture, 0,0,0);
    render_number(togo, pos + offset + XYPos(min_siz * 45 / 192 , min_siz * 77 / 192), XYPos(min_siz * 102 / 192, min_siz * 98 / 192));
    SDL_SetTextureColorMod(sdl_texture, contrast, contrast, contrast);
    return false;
}

void GameState::render_rule(GridRule& rule, XYPos base_pos, int size, int hover_rulemaker_region_base_index, bool reason)
{
        if (rule.region_count >= 1)
        {
            XYPos siz = XYPos(1,2);
            if (rule.region_count >= 2) siz.x = 2;
            if (rule.region_count >= 3) siz.y = 3;
            if (rule.region_count >= 4) siz.y = 5;

            unsigned colour = (right_panel_mode == RIGHT_MENU_RULE_GEN && rule_gen_region[0]) ? rule_gen_region[0]->colour : 0;
            set_region_colour(sdl_texture, rule.region_type[0].value, colour, contrast);
            render_box(base_pos + XYPos(0 * size, 0 * size), XYPos(siz.x * size, siz.y * size), size / 2, 8);
            render_region_bubble(rule.region_type[0], colour, base_pos + XYPos(0 * size, 0 * size), size * 2 / 3, hover_rulemaker_region_base_index == 0);

        }
        if (rule.region_count >= 2)
        {
            XYPosFloat siz = XYPos(2,2);
            if (rule.region_count >= 3) siz.y = 3;
            if (rule.region_count >= 4) siz.y = 5 - 1.0 / 12;

            unsigned colour = (right_panel_mode == RIGHT_MENU_RULE_GEN && rule_gen_region[1]) ? rule_gen_region[1]->colour : 0;
            set_region_colour(sdl_texture, rule.region_type[1].value, colour, contrast);
            render_box(base_pos + XYPos(1 * size, 0 * size + size / 12), XYPos(siz.x * size, siz.y * size), size / 2, 8);
            render_region_bubble(rule.region_type[1], colour, base_pos + XYPos(2 * size + size / 3, 0 * size +size / 12), size * 2 / 3, hover_rulemaker_region_base_index == 1);

        }

        if (rule.region_count >= 3)
        {
            XYPos siz = XYPos(5,1);
            if (rule.region_count >= 4) siz.y = 2;
            unsigned colour = (right_panel_mode == RIGHT_MENU_RULE_GEN && rule_gen_region[2]) ? rule_gen_region[2]->colour : 0;
            set_region_colour(sdl_texture, rule.region_type[2].value, colour, contrast);
            render_box(base_pos + XYPos(0 * size, 2 * size), XYPos(siz.x * size, siz.y * size), size / 2, 8);
            render_region_bubble(rule.region_type[2], colour, base_pos + XYPos(4 * size + size / 3, 2 * size), size * 2 / 3, hover_rulemaker_region_base_index == 2);
        }

        if (rule.region_count >= 4)
        {
            unsigned colour = (right_panel_mode == RIGHT_MENU_RULE_GEN && rule_gen_region[3]) ? rule_gen_region[3]->colour : 0;
            set_region_colour(sdl_texture, rule.region_type[3].value, colour, contrast);
            render_box(base_pos + XYPos(size / 12, 3 * size), XYPos(5 * size - size / 12, 2 * size), size / 2, 8);
            render_region_bubble(rule.region_type[3], colour, base_pos + XYPos(size / 12 + 4 * size + size / 3, 4 * size + size / 3), size * 2 / 3, hover_rulemaker_region_base_index == 3);
        }

        if (rule.apply_region_type.type == RegionType::VISIBILITY)
        {
            SDL_SetTextureColorMod(sdl_texture, contrast, contrast, contrast);
            SDL_Rect src_rect = {(rule.apply_region_type.value == 0) ? 1088 : 896, 384, 192, 192};
            if (rule.apply_region_type.value == 2)
            {
                src_rect.x = 512;
                src_rect.y = 768;
            }
            if (rule.apply_region_bitmap & 1)
            {
                SDL_Rect dst_rect = {base_pos.x + size / 3, base_pos.y + 0 * size + size / 3, size * 2 / 3, size * 2 / 3};
                SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            }
            if (rule.apply_region_bitmap & 2)
            {
                SDL_Rect dst_rect = {base_pos.x + size * 2, base_pos.y + 0 * size + size / 3, size * 2 / 3, size * 2 / 3};
                SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            }
            if (rule.apply_region_bitmap & 4)
            {
                SDL_Rect dst_rect = {base_pos.x + size * 4, base_pos.y + 2 * size + size / 3, size * 2 / 3, size * 2 / 3};
                SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            }
            if (rule.apply_region_bitmap & 8)
            {
                SDL_Rect dst_rect = {base_pos.x + size * 4, base_pos.y + 4 * size, size * 2 / 3, size * 2 / 3};
                SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            }
        }
        std::vector<XYPos> reg_pos;
        std::vector<int> reg_grp;

        for (int i = 1; i < (1 << rule.region_count); i++)
        {
            int x = i & 3;
            if (x == 0)
                x = 3;
            else if (x == 1)
                x = 0;
            else if (x == 2)
                x = 2;
            else if (x == 3)
                x = 1;

            int y = i >> 2;
            y ^= y >> 1;
            y += 1;

            XYPos p = XYPos(x,y);

            RegionType r_type = rule.square_counts[i];
            if (reason)
            {
                XYPos sp = base_pos + p * size;
                if (r_type.value == 0)
                {
                    if (r_type.type == RegionType::MORE)
                    {
                        SDL_Rect src_rect = {2304, 384, 192, 192};
                        SDL_Rect dst_rect = {sp.x + size * 1 / 8, sp.y + size * 1 / 8, (int)(size * 6 / 8), (int)(size * 6 / 8)};
                        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                    }
                }
                else if (r_type.value == 1)
                {
                    SDL_Rect src_rect = {320, 192, 192, 192};
                    SDL_Rect dst_rect = {sp.x + size * 1 / 8, sp.y + size * 1 / 8, (int)(size * 6 / 8), (int)(size * 6 / 8)};
                    SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);

                }
                else if (r_type.value <= 3)
                {
                    SDL_Rect src_rect = {320, 192, 192, 192};
                    SDL_Rect dst_rect = {sp.x + size * 1 / 8, sp.y + int(size * 1 / 8), (int)(size * 3 / 8), (int)(size * 3 / 8)};
                    SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                    dst_rect = {sp.x + size * 4 / 8, sp.y + int(size * 1 / 8), (int)(size * 3 / 8), (int)(size * 3 / 8)};
                    SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                    if (r_type.value == 3)
                    {
                        dst_rect = {sp.x + int(size * 2.5 / 8), sp.y + int(size * 4 / 8), (int)(size * 3 / 8), (int)(size * 3 / 8)};
                        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                    }
                }
                else
                {
                    render_number(r_type.value, sp + XYPos(size / 8,size / 8), XYPos(size * 3 / 8, size * 6 / 8));

                    SDL_Rect src_rect = {320, 192, 192, 192};
                    SDL_Rect dst_rect = {sp.x + size * 4 / 8, sp.y + int(size * 2 / 8), (int)(size * 3 / 8), (int)(size * 3 / 8)};
                    SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);

                }
            }
            else
                render_region_type(r_type, base_pos + p * size, size);

            if (((rule.apply_region_bitmap >> i) & 1) && rule.apply_region_type.type < 50)
            {
                reg_pos.push_back(p);
                reg_grp.push_back(reg_grp.size());
            }
        }

        while(true)
        {
            double best_dist = INFINITY;
            int best_i, best_j;
            for (unsigned i = 0; i < reg_pos.size(); i++)
            {
                for (unsigned j = 0; j < reg_pos.size(); j++)
                {
                    if (i == j)
                        continue;
                    if (reg_grp[i] == reg_grp[j])
                        continue;
                    double d = XYPosFloat(reg_pos[i] - reg_pos[j]).distance();
                    if (d < best_dist)
                    {
                        best_dist = d;
                        best_i = i;
                        best_j = j;
                    }
                }
            }
            if (best_dist == INFINITY)
                break;
            int o = reg_grp[best_i];
            int n = reg_grp[best_j];
            for (unsigned i = 0; i < reg_pos.size(); i++)
            {
                if (reg_grp[i] == o)
                    reg_grp[i] = n;
            }
            {
                XYPos pos = reg_pos[best_i] * size;
                XYPos last = reg_pos[best_j] * size;
                double dist = XYPosFloat(pos - last).distance();
                double angle = XYPosFloat(pos).angle(XYPosFloat(last));
                int line_thickness = size / 16;

                XYPos r_pos = base_pos + XYPos(size * 3 / 4, size * 3 / 4);
                {
                    set_region_colour(sdl_texture, rule.apply_region_type.value, 0, contrast);
                    double f = (frame / 5 + pos.x);
                    SDL_Rect src_rect1 = {int(f)% 1024, 2528, std::min(int(dist / line_thickness * 10), 1024), 32};
                    f = (frame / 8 + pos.x);
                    SDL_Rect src_rect2 = {1024 - int(f) % 1024, 2528, std::min(int(dist / line_thickness * 10), 1024), 32};
                    SDL_Point rot_center = {0, int(line_thickness * 2)};
                    SDL_Rect dst_rect = {r_pos.x + int(pos.x), r_pos.y + int((pos.y - line_thickness * 2)), int(dist), int(line_thickness * 4)};
                    SDL_RenderCopyEx(sdl_renderer, sdl_texture, &src_rect1, &dst_rect, degrees(angle), &rot_center, SDL_FLIP_NONE);
                    SDL_RenderCopyEx(sdl_renderer, sdl_texture, &src_rect2, &dst_rect, degrees(angle), &rot_center, SDL_FLIP_NONE);
                }

                {
                    SDL_Rect src_rect = {160, 608, 1, 1};
                    SDL_Point rot_center = {0, int(line_thickness)};
                    SDL_Rect dst_rect = {r_pos.x + int(pos.x), r_pos.y + int((pos.y - line_thickness)), int(dist), int(line_thickness * 2)};
                    SDL_RenderCopyEx(sdl_renderer, sdl_texture, &src_rect, &dst_rect, degrees(angle), &rot_center, SDL_FLIP_NONE);
                }
            }
        }
        for (int i = 1; i < (1 << rule.region_count); i++)
        {
            int x = i & 3;
            if (x == 0)
                x = 3;
            else if (x == 1)
                x = 0;
            else if (x == 2)
                x = 2;
            else if (x == 3)
                x = 1;

            int y = i >> 2;
            y ^= y >> 1;
            y += 1;

            XYPos p = XYPos(x,y);

            if ((rule.apply_region_bitmap >> i) & 1)
            {
                if (rule.apply_region_type.type < 50)
                {
                    render_region_bubble(rule.apply_region_type, 0, base_pos + XYPos(size / 2, size / 2) + p * size, size / 2);
                }
                else if (rule.apply_region_type.type == RegionType::SET)
                {
                    render_region_type(rule.apply_region_type, base_pos + XYPos(size / 2, size / 2) + p * size, size / 2);
                    if (reason)
                        render_box(base_pos + XYPos(size / 2, size / 2) + p * size, XYPos(size / 2, size / 2), size / 4, 9);

                }
            }
        }

}

void GameState::render(bool saving)
{
    if (score_tables[game_mode][0].size() == 0)
        fetch_scores();
    if (pirate)
        speed_dial = std::clamp(speed_dial, 0.0, 0.5);
    if(low_contrast && contrast > 128)
        contrast--;
    if(!low_contrast && contrast < 255)
        contrast++;
    if (prog_stars[PROG_LOCK_REGION_LIMIT] > max_stars)
        rule_limit_slider = 1.00;

    if (prog_stars[PROG_LOCK_DONT_CARE] <= max_stars)
        tut_page_count = 4;
    if (prog_stars[PROG_LOCK_NUMBER_TYPES] <= max_stars)
        tut_page_count = 5;
    if (prog_stars[PROG_LOCK_LEVELS_AND_LOCKS] <= max_stars)
        tut_page_count = 6;
    if (prog_stars[PROG_LOCK_GEN_REGIONS] <= max_stars)
        tut_page_count = 7;
    if (prog_stars[PROG_LOCK_VISIBILITY] <= max_stars)
        tut_page_count = 8;

    if (prog_seen[PROG_LOCK_DONT_CARE] == 0)
    {
        if (prog_stars[PROG_LOCK_DONT_CARE] <= max_stars)
        {
            display_help = true;
            tutorial_index = 3;
            prog_seen[PROG_LOCK_DONT_CARE]++;
        }
    }
    if (prog_seen[PROG_LOCK_NUMBER_TYPES] == 0)
    {
        if (prog_stars[PROG_LOCK_NUMBER_TYPES] <= max_stars)
        {
            display_help = true;
            tutorial_index = 4;
            prog_seen[PROG_LOCK_NUMBER_TYPES]++;
        }
    }
    if (prog_seen[PROG_LOCK_LEVELS_AND_LOCKS] == 0)
    {
        if (prog_stars[PROG_LOCK_LEVELS_AND_LOCKS] <= max_stars)
        {
            display_help = true;
            tutorial_index = 5;
            prog_seen[PROG_LOCK_LEVELS_AND_LOCKS]++;
        }
    }

    if (prog_seen[PROG_LOCK_GEN_REGIONS] == 0)
    {
        if (prog_stars[PROG_LOCK_GEN_REGIONS] <= max_stars)
        {
            display_help = true;
            tutorial_index = 6;
            prog_seen[PROG_LOCK_GEN_REGIONS]++;
        }
    }

    if (prog_seen[PROG_LOCK_VISIBILITY] == 0)
    {
        if (prog_stars[PROG_LOCK_VISIBILITY] <= max_stars)
        {
            display_help = true;
            tutorial_index = 7;
            prog_seen[PROG_LOCK_VISIBILITY]++;
        }
    }

    mouse_cursor = SDL_SYSTEM_CURSOR_ARROW;
    if (grid_dragging)
        mouse_cursor = SDL_SYSTEM_CURSOR_SIZEALL;

    bool row_col_clues = !grid->edges.empty() && show_row_clues;
    XYPos wsize;
    SDL_GetWindowSize(sdl_window, &wsize.x, &wsize.y);
    SDL_GetRendererOutputSize(sdl_renderer, &window_size.x, &window_size.y);
    mouse_scale = XYPosFloat(double(window_size.x) / double(wsize.x), double(window_size.y) / double(wsize.y));
    SDL_RenderClear(sdl_renderer);

    {
        SDL_Rect src_rect = {15, 426, 1, 1};
        SDL_Rect dst_rect = {0, 0, window_size.x, window_size.y};
        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
    }

    int edge_clue_border = 0;
    {
        if (window_size.x * 9 > window_size.y * 16)
            grid_size = window_size.y;
        else
            grid_size = (window_size.x * 9) / 16;

        panel_size = XYPos((grid_size * 7) / 18, grid_size);
        grid_offset = (window_size - XYPos(grid_size,grid_size)) / 2;
        left_panel_offset = grid_offset - XYPos(panel_size.x, 0);
        right_panel_offset = grid_offset + XYPos(grid_size, 0);
        button_size = (grid_size * 7) / (18 * 5);

        if (row_col_clues)
        {
            edge_clue_border = grid_size / 8;
            grid_offset += XYPos(edge_clue_border, edge_clue_border);
            grid_size -= edge_clue_border * 2;
        }
        if ((grid->wrapped == Grid::WRAPPED_NOT) && target_grid_zoom < 1)
            target_grid_zoom = 1;
        {
            grid_zoom = std::clamp(grid_zoom, 0.4, 12.0);
            target_grid_zoom = std::clamp(target_grid_zoom, 0.4, 12.0);

            double mag = (target_grid_zoom / grid_zoom);
            mag = std::clamp(mag, 0.97, 1.03);
            grid_zoom *= mag;

            int new_scaled_grid_size = grid_size * grid_zoom;
            scaled_grid_offset -= (mouse - grid_offset - scaled_grid_offset) * (new_scaled_grid_size - scaled_grid_size) / scaled_grid_size;
            scaled_grid_size = new_scaled_grid_size;
        }

        if ((grid->wrapped == Grid::WRAPPED_IN) && grid_zoom < 1.5)
        {
            XYRect sq_pos = grid->get_square_pos(grid->innie_pos, grid->get_grid_pitch(XYPos(scaled_grid_size, scaled_grid_size)));
            double mag = double(scaled_grid_size) / double(sq_pos.size.x);
            grid_zoom *= mag;
            target_grid_zoom *= mag;
            scaled_grid_offset = scaled_grid_offset - sq_pos.pos * mag;
        }
        if ((grid->wrapped == Grid::WRAPPED_IN) && grid_zoom >= 10)
        {
            XYRect sq_pos = grid->get_square_pos(grid->innie_pos, grid->get_grid_pitch(XYPos(scaled_grid_size, scaled_grid_size)));
            sq_pos.pos += scaled_grid_offset;
            if (sq_pos.pos.x < 0 && sq_pos.pos.y < 0 && (sq_pos.pos.x + sq_pos.size.x) >= grid_size && (sq_pos.pos.y + sq_pos.size.y) >= grid_size)
            {
                double mag = double(sq_pos.size.x) / double(scaled_grid_size);
                scaled_grid_offset = sq_pos.pos;
                grid_zoom *= mag;
                target_grid_zoom *= mag;
            }
        }
        scaled_grid_size = grid_size * grid_zoom;
        grid_pitch = grid->get_grid_pitch(XYPos(scaled_grid_size, scaled_grid_size));

        if (grid->wrapped == Grid::WRAPPED_NOT)
        {
            scaled_grid_offset.x = std::min(scaled_grid_offset.x, 0);
            scaled_grid_offset.y = std::min(scaled_grid_offset.y, 0);
            int d = (scaled_grid_offset.x + scaled_grid_size) - grid_size;
            if (d < 0)
                scaled_grid_offset.x -= d;
            d = (scaled_grid_offset.y + scaled_grid_size) - grid_size;
            if (d < 0)
                scaled_grid_offset.y -= d;
        }
        else if (grid->wrapped == Grid::WRAPPED_IN)
        {
            scaled_grid_offset.x = std::min(scaled_grid_offset.x, 0);
            scaled_grid_offset.y = std::min(scaled_grid_offset.y, 0);
            int d = (scaled_grid_offset.x + scaled_grid_size) - grid_size;
            if (d < 0)
                scaled_grid_offset.x -= d;
            d = (scaled_grid_offset.y + scaled_grid_size) - grid_size;
            if (d < 0)
                scaled_grid_offset.y -= d;
        }
        else
        {
            XYPos wrap_size = grid->get_wrapped_size(grid_pitch);
            while (scaled_grid_offset.x > wrap_size.x)
                scaled_grid_offset.x -= wrap_size.x;
            while (scaled_grid_offset.y > wrap_size.y)
                scaled_grid_offset.y -= wrap_size.y;
            while (scaled_grid_offset.x < 0 )
                scaled_grid_offset.x += wrap_size.x;
            while (scaled_grid_offset.y < 0)
                scaled_grid_offset.y += wrap_size.y;
        }

    }

    tooltip_string = "";
    tooltip_rect = XYRect(-1,-1,-1,-1);
    bool hover_rulemaker = false;
    XYSet hover_squares_highlight;
    int hover_rulemaker_bits = 0;
    bool hover_rulemaker_lower_right = false;

    int hover_rulemaker_region_base_index = -1;
    bool hover_rulemaker_region_base = false;

    mouse_hover_region = NULL;

    if (right_panel_mode == RIGHT_MENU_REGION)
    {
        if ((mouse - (right_panel_offset + XYPos(0, button_size))).inside(XYPos(button_size, button_size)))
            mouse_hover_region = inspected_region;
        if ((mouse - (right_panel_offset + XYPos(0, 2 * button_size))).inside(XYPos(button_size, button_size)))
        {
            hover_rulemaker = true;
            hover_squares_highlight = inspected_region->elements;
        }
    }

    if (right_panel_mode == RIGHT_MENU_RULE_GEN || right_panel_mode == RIGHT_MENU_RULE_INSPECT)
    {
        GridRegionCause rule_cause = (right_panel_mode == RIGHT_MENU_RULE_GEN) ? GridRegionCause(&constructed_rule, rule_gen_region[0], rule_gen_region[1], rule_gen_region[2], rule_gen_region[3]) : inspected_rule;

        if (XYPosFloat(mouse - (right_panel_offset + XYPos(0 * button_size, 1 * button_size) + XYPos(button_size / 3, button_size / 3))).distance() < (button_size / 3) && rule_cause.rule->region_count >= 1)
            hover_rulemaker_region_base_index = 0;
        if (XYPosFloat(mouse - (right_panel_offset + XYPos(2 * button_size, 1 * button_size) + XYPos((button_size * 2) / 3, button_size / 3 + button_size / 12))).distance() < (button_size / 3) && rule_cause.rule->region_count >= 2)
            hover_rulemaker_region_base_index = 1;
        if (XYPosFloat(mouse - (right_panel_offset + XYPos(4 * button_size, 3 * button_size) + XYPos((button_size * 2) / 3, button_size / 3))).distance() < (button_size / 3) && rule_cause.rule->region_count >= 3)
            hover_rulemaker_region_base_index = 2;
        if (XYPosFloat(mouse - (right_panel_offset + XYPos(4 * button_size, 5 * button_size) + XYPos((button_size * 2) / 3 + button_size / 12, (button_size * 2) / 3))).distance() < (button_size / 3) && rule_cause.rule->region_count >= 4)
            hover_rulemaker_region_base_index = 3;

        if (hover_rulemaker_region_base_index >= 0)
        {
            hover_rulemaker_region_base = true;
            mouse_hover_region = rule_cause.regions[hover_rulemaker_region_base_index];
        }

        if ((mouse - (right_panel_offset + XYPos(0, button_size * 2))).inside(XYPos(button_size*4, button_size * 6)))
        {
            XYPos pos = mouse - right_panel_offset;
            XYPos gpos = pos / button_size;
            XYPos ipos = (pos - (gpos * button_size));
            hover_rulemaker_lower_right = ((ipos.x > button_size/2) && (ipos.y > button_size/2));

            int x = gpos.x;
            if (x == 0) x = 1;
            else if (x == 1) x = 3;
            else if (x == 2) x = 2;
            else if (x == 3) x = 0;
            else assert(0);

            int y = gpos.y;
            y -= 2;
            y ^= y >> 1;

            hover_rulemaker_bits = x + y * 4;
            if (hover_rulemaker_bits >= 1 && hover_rulemaker_bits < (1 << rule_cause.rule->region_count))
            {
                hover_rulemaker = true;
                hover_squares_highlight = ~hover_squares_highlight;
                for (int i = 0; i < rule_cause.rule->region_count; i++)
                {
                    if (!rule_cause.regions[i])
                    {
                        hover_squares_highlight.clear();
                        break;
                    }
                    hover_squares_highlight = hover_squares_highlight & (((hover_rulemaker_bits >> i) & 1) ? rule_cause.regions[i]->elements : ~rule_cause.regions[i]->elements);
                }
            }
        }
    }

    if (display_scores)
    {
        unsigned row_count = 16;
        int rules_list_size = panel_size.y;
        int cell_width = rules_list_size / 7.5;
        int cell_height = (rules_list_size - cell_width) / row_count;
        XYPos list_pos = left_panel_offset + XYPos(panel_size.x, 0);
        {
            SDL_Rect src_rect = {704 + 0 * 192, 2144, 192, 192};
            SDL_Rect dst_rect = {list_pos.x + 0 * cell_width, list_pos.y, cell_width, cell_width};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            add_tooltip(dst_rect, "Position");
        }
        {
            SDL_Rect src_rect = {896, 2336, 192, 192};
            SDL_Rect dst_rect = {list_pos.x + 1 * cell_width, list_pos.y, cell_width, cell_width};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            add_tooltip(dst_rect, "Player Name");
        }
        if (current_level_group_index == GLBAL_LEVEL_SETS)
        {
            SDL_Rect src_rect = {!display_scores_global ? 1408 : 1600, 2144, 192, 192};
            SDL_Rect dst_rect = {list_pos.x + 5 * cell_width, list_pos.y, cell_width, cell_width};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            add_tooltip(dst_rect, !display_scores_global ? "Level" : "Global");
            if (display_rules_click && ((display_rules_click_pos - XYPos(dst_rect.x, dst_rect.y)).inside(XYPos(dst_rect.w, dst_rect.h))))
                display_scores_global = !display_scores_global;
        }
        {
            SDL_Rect src_rect = {512, 960, 192, 192};
            SDL_Rect dst_rect = {list_pos.x + 6 * cell_width, list_pos.y, cell_width, cell_width};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            add_tooltip(dst_rect, "Score");
        }
        {
            SDL_Rect src_rect = {704 + 3 * 192, 2144, 96, 96};
            SDL_Rect dst_rect = {list_pos.x + 7 * cell_width, list_pos.y, cell_width / 2, cell_width/2};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            add_tooltip(dst_rect, "Close");
            if (display_rules_click && ((display_rules_click_pos - XYPos(dst_rect.x, dst_rect.y)).inside(XYPos(dst_rect.w, dst_rect.h))))
                display_scores = false;
        }

        int grp_index = current_level_group_index;
        if (current_level_group_index == GLBAL_LEVEL_SETS && display_scores_global)
                grp_index = current_level_group_index + 1;
        if (rules_list_offset < 0)
            rules_list_offset = 0;
        if (rules_list_offset + row_count > score_tables[game_mode][grp_index].size())
            rules_list_offset = score_tables[game_mode][grp_index].size() - row_count;
        rules_list_offset = std::max(rules_list_offset, 0);

        for (unsigned score_index = 0; score_index < row_count; score_index++)
        {

            if (score_index + rules_list_offset >= score_tables[game_mode][grp_index].size())
                break;
            PlayerScore& s = score_tables[game_mode][grp_index][score_index + rules_list_offset];
            if (s.hidden)
                continue;

            render_number(s.pos, list_pos + XYPos(0 * cell_width, cell_width + score_index * cell_height + cell_height/10), XYPos(cell_width, cell_height*8/10));

            {
                SDL_Color color = {contrast, contrast, contrast};
                if (s.is_friend == 1)
                    color = {uint8_t(contrast / 2), contrast, uint8_t(contrast / 2)};
                if (s.is_friend == 2)
                    color = {contrast, uint8_t(contrast / 2), uint8_t(contrast / 2)};
                SDL_Surface* text_surface = TTF_RenderUTF8_Blended(score_font, s.name.c_str(), color);
                SDL_Texture* new_texture = SDL_CreateTextureFromSurface(sdl_renderer, text_surface);
                SDL_Rect src_rect;
                SDL_GetClipRect(text_surface, &src_rect);
                int width = (src_rect.w * cell_height) / src_rect.h;
                int height = cell_height;
                if (width > cell_width * 5)
                {
                    height = (height * cell_width * 5) / width;
                    width = cell_width * 5;
                }
                SDL_Rect dst_rect = {list_pos.x + 1 * cell_width, int(list_pos.y + cell_width + score_index * cell_height + (cell_height - height) / 2), width, height};
                SDL_RenderCopy(sdl_renderer, new_texture, &src_rect, &dst_rect);
                SDL_DestroyTexture(new_texture);
                SDL_FreeSurface(text_surface);

            }
            render_number(s.score, list_pos + XYPos(6 * cell_width, cell_width + score_index * cell_height + cell_height/10), XYPos(cell_width, cell_height*8/10));
        }
        {
            SDL_Rect src_rect = {1664, 1344, 64, 64};
            SDL_Rect dst_rect = {list_pos.x + cell_width * 7, list_pos.y + cell_width * 1, cell_width/2, cell_width/2};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            if (display_rules_click && ((display_rules_click_pos - XYPos(dst_rect.x, dst_rect.y)).inside(XYPos(dst_rect.w, dst_rect.h))))
                rules_list_offset--;
        }
        {
            SDL_Rect src_rect = {1664, 1408, 64, 64};
            SDL_Rect dst_rect = {list_pos.x + cell_width * 7, list_pos.y + cell_width * 7, cell_width/2, cell_width/2};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            if (display_rules_click && ((display_rules_click_pos - XYPos(dst_rect.x, dst_rect.y)).inside(XYPos(dst_rect.w, dst_rect.h))))
                rules_list_offset++;
        }

        if (rules_list_offset + row_count > score_tables[game_mode][grp_index].size())
            rules_list_offset = score_tables[game_mode][grp_index].size() - row_count;
        rules_list_offset = std::max(rules_list_offset, 0);

        {
            int full_size = cell_width * 5.5;
            unsigned all_count = score_tables[game_mode][grp_index].size();
            int box_height = full_size;
            int box_pos = 0;

            if (all_count > row_count)
            {
                box_height = (row_count * full_size) / all_count;
                box_height = std::max(cell_width / 4, box_height);
                box_pos = (rules_list_offset * (full_size - box_height)) / (all_count - row_count);
                if (display_rules_click_drag && ((display_rules_click_pos - list_pos - XYPos(cell_width * 7, cell_width * 1.5)).inside(XYPos(cell_width/2, cell_width * 5.5))))
                {
                    int p = mouse.y - list_pos.y - cell_width * 1.5 - box_height / 2;
                    p = std::max(p, 0);
                    p = (p * (all_count - row_count)) / (full_size - box_height);
                    rules_list_offset = p;

                }
            }

            render_box(list_pos + XYPos(cell_width * 7, cell_width * 1.5 + box_pos), XYPos(cell_width / 2, box_height), std::min(box_height/2, cell_width / 4));
        }
        if (rules_list_offset + row_count > score_tables[game_mode][grp_index].size())
            rules_list_offset = score_tables[game_mode][grp_index].size() - row_count;
        rules_list_offset = std::max(rules_list_offset, 0);
        display_rules_click = false;

    }
    else if (display_levels)
    {
        int row_count = 16;
        int rules_list_size = panel_size.y;
        int cell_width = rules_list_size / 7.5;
        int cell_height = (rules_list_size - cell_width) / row_count;
        int col_click = -1;
        XYPos list_pos = left_panel_offset + XYPos(panel_size.x, 0);
        {
            SDL_Rect src_rect = {704 + 0 * 192, 2144, 192, 192};
            SDL_Rect dst_rect = {list_pos.x + 0 * cell_width, list_pos.y, cell_width, cell_width};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            add_tooltip(dst_rect, "Level ID");
            if (display_rules_click && ((display_rules_click_pos - XYPos(dst_rect.x, dst_rect.y)).inside(XYPos(dst_rect.w, dst_rect.h))))
                col_click = 0;
        }
        {
            SDL_Rect src_rect = {1792, 2144, 192, 192};
            SDL_Rect dst_rect = {list_pos.x + 1 * cell_width, list_pos.y, cell_width, cell_width};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            add_tooltip(dst_rect, "Pass Rate");
            if (display_rules_click && ((display_rules_click_pos - XYPos(dst_rect.x, dst_rect.y)).inside(XYPos(dst_rect.w, dst_rect.h))))
                col_click = 1;
        }
        {
            SDL_Rect src_rect = {1280, 2240, 96, 96};
            SDL_Rect dst_rect = {list_pos.x + 2 * cell_width, list_pos.y, cell_width, cell_width};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            add_tooltip(dst_rect, "Solved");
            if (display_rules_click && ((display_rules_click_pos - XYPos(dst_rect.x, dst_rect.y)).inside(XYPos(dst_rect.w, dst_rect.h))))
                col_click = 2;
        }
        {
            SDL_Rect src_rect = {1984, 2336, 192, 192};
            SDL_Rect dst_rect = {list_pos.x + 3 * cell_width, list_pos.y, cell_width, cell_width};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            add_tooltip(dst_rect, "Robots");
            if (display_rules_click && ((display_rules_click_pos - XYPos(dst_rect.x, dst_rect.y)).inside(XYPos(dst_rect.w, dst_rect.h))))
                col_click = 3;
        }
        {
            SDL_Rect src_rect = {512, 192, 192, 192};
            SDL_Rect dst_rect = {list_pos.x + 4 * cell_width, list_pos.y, cell_width, cell_width};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            add_tooltip(dst_rect, "Clear");
            if (display_rules_click && ((display_rules_click_pos - XYPos(dst_rect.x, dst_rect.y)).inside(XYPos(dst_rect.w, dst_rect.h))))
            {
                current_level_index = 0;
                load_level = true;
                force_load_level = false;
                SDL_LockMutex(level_progress_lock);
                for (unsigned i = 0; i < level_progress[game_mode][current_level_group_index][current_level_set_index].level_status.size(); i++)
                    level_progress[game_mode][current_level_group_index][current_level_set_index].level_status[i].done = false;
                level_progress[game_mode][current_level_group_index][current_level_set_index].count_todo = level_progress[game_mode][current_level_group_index][current_level_set_index].level_status.size();
                level_progress[game_mode][current_level_group_index][current_level_set_index].star_anim_prog = 0;
                level_progress[game_mode][current_level_group_index][current_level_set_index].unlock_anim_prog = 0;
                SDL_UnlockMutex(level_progress_lock);
            }
        }

        {
            SDL_Rect src_rect = {704 + 3 * 192, 2144, 96, 96};
            SDL_Rect dst_rect = {list_pos.x + 7 * cell_width, list_pos.y, cell_width / 2, cell_width / 2};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            add_tooltip(dst_rect, "Close");
            if (display_rules_click && ((display_rules_click_pos - XYPos(dst_rect.x, dst_rect.y)).inside(XYPos(dst_rect.w, dst_rect.h))))
            {
                display_levels = false;
            }
        }

        if (col_click >= 0)
        {
            if (display_levels_sort_col == col_click)
                display_levels_sort_dir = !display_levels_sort_dir;
            else
            {
                display_levels_sort_col_2nd = display_levels_sort_col;
                display_levels_sort_dir_2nd = display_levels_sort_dir;
                display_levels_sort_col = col_click;
                display_levels_sort_dir = true;
            }
        }
        {
            SDL_Rect src_rect = {1088 + (192 * 2) + (display_levels_sort_dir_2nd ? 0 : 192), 2336, 192, 192};
            SDL_Rect dst_rect = {list_pos.x + display_levels_sort_col_2nd * cell_width, list_pos.y, cell_width, cell_width};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
        }
        {
            SDL_Rect src_rect = {1088 + (display_levels_sort_dir ? 0 : 192), 2336, 192, 192};
            SDL_Rect dst_rect = {list_pos.x + display_levels_sort_col * cell_width, list_pos.y, cell_width, cell_width};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
        }

        struct RuleDiplaySort
        {
            GameState& state;
            int col = 0;
            bool descend = false;
            RuleDiplaySort(GameState& state_, int col_, bool descend_):
                state(state_), col(col_), descend(descend_)
            {};
            bool operator() (unsigned a_,unsigned b_)
            {
                unsigned a = descend ? a_ : b_;
                unsigned b = descend ? b_ : a_;
                if (col == 0)
                    return (a < b);
                if (col == 1)
                    return (state.level_progress[state.game_mode][state.current_level_group_index][state.current_level_set_index].level_status[a].stats < 
                            state.level_progress[state.game_mode][state.current_level_group_index][state.current_level_set_index].level_status[b].stats);
                if (col == 2)
                    return (state.level_progress[state.game_mode][state.current_level_group_index][state.current_level_set_index].level_status[a].done < 
                            state.level_progress[state.game_mode][state.current_level_group_index][state.current_level_set_index].level_status[b].done);
                if (col == 3)
                {
                    if (state.level_progress[state.game_mode][state.current_level_group_index][state.current_level_set_index].level_status[a].done ==
                        state.level_progress[state.game_mode][state.current_level_group_index][state.current_level_set_index].level_status[b].done)
                        return (state.level_progress[state.game_mode][state.current_level_group_index][state.current_level_set_index].level_status[a].robot_done < 
                                state.level_progress[state.game_mode][state.current_level_group_index][state.current_level_set_index].level_status[b].robot_done);
                    return (state.level_progress[state.game_mode][state.current_level_group_index][state.current_level_set_index].level_status[a].done < 
                            state.level_progress[state.game_mode][state.current_level_group_index][state.current_level_set_index].level_status[b].done);
                }
                assert(0);
                return false;
            }
        };

        std::vector<unsigned> levels_list;

        for (unsigned i = 0; i < level_progress[game_mode][current_level_group_index][current_level_set_index].level_status.size(); i++)
            levels_list.push_back(i);

        std::stable_sort (levels_list.begin(), levels_list.end(), RuleDiplaySort(*this, display_levels_sort_col_2nd, display_levels_sort_dir_2nd));
        std::stable_sort (levels_list.begin(), levels_list.end(), RuleDiplaySort(*this, display_levels_sort_col, display_levels_sort_dir));

        if (display_levels_center_current)
            for (unsigned i = 0; i < levels_list.size(); i++)
                if (levels_list[i] == current_level_index)
                {
                    rules_list_offset = i - 8;
                    break;
                }
        display_levels_center_current = false;
        if (rules_list_offset + row_count > (int)levels_list.size())
            rules_list_offset = levels_list.size() - row_count;
        rules_list_offset = std::max(rules_list_offset, 0);

        for (int level_index = 0; level_index < row_count; level_index++)
        {
            if (level_index + rules_list_offset >= (int)levels_list.size())
                break;
            unsigned index = levels_list[level_index + rules_list_offset];

            if (index == current_level_index)
            {
                render_box(list_pos + XYPos(0, cell_width + level_index * cell_height), XYPos(cell_width * 7, cell_height), cell_height / 4);
            }
            render_number(index, list_pos + XYPos(0 * cell_width, cell_width + level_index * cell_height + cell_height/10), XYPos(cell_width, cell_height*8/10));

            {
                unsigned rep = level_progress[game_mode][current_level_group_index][current_level_set_index].level_status[index].stats;

                std::string digits;
                if (rep == 0)
                {
                    digits = "0%";
                }
                else if (rep < 100)
                {
                    digits = "0." + std::to_string((rep / 10) % 10) + std::to_string((rep / 1) % 10) + "%";
                }
                else if (rep < 1000)
                {
                    digits = std::to_string((rep / 100) % 10) + "." + std::to_string((rep / 10) % 10) + std::to_string((rep / 1) % 10) + "%";
                }
                else 
                {
                    digits = std::to_string((rep / 1000) % 10) + std::to_string((rep / 100) % 10) + "." + std::to_string((rep / 10) % 10) + std::to_string((rep / 1) % 10) + "%";
                }

                render_number_string(digits, list_pos + XYPos(1 * cell_width, cell_width + level_index * cell_height + cell_height/10), XYPos(cell_width, cell_height*8/10));
            }
            {
                bool rep = level_progress[game_mode][current_level_group_index][current_level_set_index].level_status[index].done;
                SDL_Rect src_rect = {1280, rep ? 2240 : 2144, 96, 96};
                SDL_Rect dst_rect = {list_pos.x + cell_width * 2 + (cell_width - cell_height) / 2, list_pos.y + cell_width * 1 + level_index * cell_height, cell_height, cell_height};
                SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            }
            {
                LevelStatus& status = level_progress[game_mode][current_level_group_index][current_level_set_index].level_status[index];
                if (!status.done && run_robots)
                {
                    if (status.robot_done == 1)
                    {
                        SDL_Rect src_rect = {704, 2336, 192, 192};
                        SDL_Rect dst_rect = {list_pos.x + cell_width * 3, list_pos.y + cell_width * 1 + level_index * cell_height, cell_height, cell_height};
                        SDL_Point rot_center = {cell_height / 2, cell_height / 2};
                        SDL_RenderCopyEx(sdl_renderer, sdl_texture, &src_rect, &dst_rect, level_index + double(frame) * 0.01, &rot_center, SDL_FLIP_NONE);
                        render_number(status.robot_regions, list_pos + XYPos(3 * cell_width + cell_height, cell_width + level_index * cell_height + cell_height/10), XYPos(cell_width - cell_height, cell_height*8/10));
                    }
                    else
                    {
                        SDL_Rect src_rect = {3008, 1344, 192, 192};
                        SDL_Rect dst_rect = {list_pos.x + cell_width * 3 + (cell_width - cell_height) / 2, list_pos.y + cell_width * 1 + level_index * cell_height, cell_height, cell_height};
                        if (status.robot_done == 2)
                            src_rect = {1280, 2240, 96, 96};
                        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                    }
                }
            }


            if (display_rules_click && ((display_rules_click_pos - list_pos - XYPos(0, cell_width + level_index * cell_height)).inside(XYPos(cell_width * 7, cell_height))))
            {
                current_level_index = index;
                force_load_level = level_progress[game_mode][current_level_group_index][current_level_set_index].level_status[index].done;
                load_level = true;
            }

        }



        {
            SDL_Rect src_rect = {1664, 1344, 64, 64};
            SDL_Rect dst_rect = {list_pos.x + cell_width * 7, list_pos.y + cell_width * 1, cell_width/2, cell_width/2};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            if (display_rules_click && ((display_rules_click_pos - XYPos(dst_rect.x, dst_rect.y)).inside(XYPos(dst_rect.w, dst_rect.h))))
                rules_list_offset--;
        }
        {
            SDL_Rect src_rect = {1664, 1408, 64, 64};
            SDL_Rect dst_rect = {list_pos.x + cell_width * 7, list_pos.y + cell_width * 7, cell_width/2, cell_width/2};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            if (display_rules_click && ((display_rules_click_pos - XYPos(dst_rect.x, dst_rect.y)).inside(XYPos(dst_rect.w, dst_rect.h))))
                rules_list_offset++;
        }

        if (rules_list_offset + row_count > (int)levels_list.size())
            rules_list_offset = levels_list.size() - row_count;
        rules_list_offset = std::max(rules_list_offset, 0);

        {
            int full_size = cell_width * 5.5;
            int all_count = levels_list.size();
            int box_height = full_size;
            int box_pos = 0;

            if (all_count > row_count)
            {
                box_height = (row_count * full_size) / all_count;
                box_height = std::max(cell_width / 4, box_height);
                box_pos = (rules_list_offset * (full_size - box_height)) / (all_count - row_count);
                if (display_rules_click_drag && ((display_rules_click_pos - list_pos - XYPos(cell_width * 7, cell_width * 1.5)).inside(XYPos(cell_width/2, cell_width * 5.5))))
                {
                    int p = mouse.y - list_pos.y - cell_width * 1.5 - box_height / 2;
                    p = (p * (all_count - row_count)) / (full_size - box_height);
                    rules_list_offset = p;
                }
            }

            render_box(list_pos + XYPos(cell_width * 7, cell_width * 1.5 + box_pos), XYPos(cell_width / 2, box_height), std::min(box_height/2, cell_width / 4));
        }

        if (rules_list_offset + row_count > (int)levels_list.size())
            rules_list_offset = levels_list.size() - row_count;
        rules_list_offset = std::max(rules_list_offset, 0);
        display_rules_click = false;
    }
    else if (display_rules)
    {
        int row_count = 16;
        int rules_list_size = panel_size.y;
        int cell_width = rules_list_size / 7.5;
        int cell_height = (rules_list_size - cell_width) / row_count;

        int col_click = -1;
        XYPos list_pos = left_panel_offset + XYPos(panel_size.x, 0);
        {
            SDL_Rect src_rect = {704 + 0 * 192, 2144, 192, 192};
            SDL_Rect dst_rect = {list_pos.x + 0 * cell_width, list_pos.y, cell_width, cell_width};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            add_tooltip(dst_rect, "Rule ID");
            if (display_rules_click && ((display_rules_click_pos - XYPos(dst_rect.x, dst_rect.y)).inside(XYPos(dst_rect.w, dst_rect.h))))
                col_click = 0;
        }
        {
            SDL_Rect src_rect = {1984, 2144, 192, 192};
            SDL_Rect dst_rect = {list_pos.x + 1 * cell_width, list_pos.y, cell_width, cell_width};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            add_tooltip(dst_rect, "Priority");
            if (display_rules_click && ((display_rules_click_pos - XYPos(dst_rect.x, dst_rect.y)).inside(XYPos(dst_rect.w, dst_rect.h))))
                col_click = 1;
        }
        {
            SDL_Rect src_rect = {704 + 1 * 192, 2144, 192, 192};
            SDL_Rect dst_rect = {list_pos.x + 2 * cell_width, list_pos.y, cell_width, cell_width};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            add_tooltip(dst_rect, "Action");
            if (display_rules_click && ((display_rules_click_pos - XYPos(dst_rect.x, dst_rect.y)).inside(XYPos(dst_rect.w, dst_rect.h))))
                col_click = 2;
        }
        {
            SDL_Rect src_rect = {704 + 2 * 192, 2144, 192, 192};
            SDL_Rect dst_rect = {list_pos.x + 3 * cell_width, list_pos.y, cell_width, cell_width};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            add_tooltip(dst_rect, "Regions");
            if (display_rules_click && ((display_rules_click_pos - XYPos(dst_rect.x, dst_rect.y)).inside(XYPos(dst_rect.w, dst_rect.h))))
                col_click = 3;
        }
        if (display_clipboard_rules)
        {
            {
                SDL_Rect src_rect = {1856, 1728, 192, 192};
                SDL_Rect dst_rect = {list_pos.x + 6 * cell_width, list_pos.y, cell_width, cell_width};
                SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                add_tooltip(dst_rect, "Import all Rules");
                if (display_rules_click && ((display_rules_click_pos - XYPos(dst_rect.x, dst_rect.y)).inside(XYPos(dst_rect.w, dst_rect.h))))
                {
                    pause_robots();
                    import_all_rules();
                }
            }
        }
        else
        {
            {
                SDL_Rect src_rect = {display_rules_level ? 1408 : 1600, 2144, 192, 192};
                SDL_Rect dst_rect = {list_pos.x + 4 * cell_width, list_pos.y, cell_width, cell_width};
                SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                add_tooltip(dst_rect, display_rules_level ? "Level" : "Global");
                if (display_rules_click && ((display_rules_click_pos - XYPos(dst_rect.x, dst_rect.y)).inside(XYPos(dst_rect.w, dst_rect.h))))
                    display_rules_level = !display_rules_level;
            }
            {
                SDL_Rect src_rect = {704 + 0 * 192, 2336, 192, 192};
                SDL_Rect dst_rect = {list_pos.x + 5 * cell_width, list_pos.y, cell_width, cell_width};
                SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                add_tooltip(dst_rect, "Times Applied");
                if (display_rules_click && ((display_rules_click_pos - XYPos(dst_rect.x, dst_rect.y)).inside(XYPos(dst_rect.w, dst_rect.h))))
                    col_click = 5;
            }
            {
                SDL_Rect src_rect = {512, 192, 192, 192};
                SDL_Rect dst_rect = {list_pos.x + 6 * cell_width, list_pos.y, cell_width, cell_width};
                SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                add_tooltip(dst_rect, "Cells Cleared");
                if (display_rules_click && ((display_rules_click_pos - XYPos(dst_rect.x, dst_rect.y)).inside(XYPos(dst_rect.w, dst_rect.h))))
                    col_click = 6;
            }
        }
        {
            SDL_Rect src_rect = {704 + 3 * 192, 2144, 96, 96};
            SDL_Rect dst_rect = {list_pos.x + 7 * cell_width, list_pos.y, cell_width / 2, cell_width / 2};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            add_tooltip(dst_rect, "Close");
            if (display_rules_click && ((display_rules_click_pos - XYPos(dst_rect.x, dst_rect.y)).inside(XYPos(dst_rect.w, dst_rect.h))))
            {
                display_rules = false;
                display_clipboard_rules = false;
            }
        }
        if (col_click >= 0)
        {
            if (display_rules_sort_col == col_click)
                display_rules_sort_dir = !display_rules_sort_dir;
            else
            {
                display_rules_sort_col_2nd = display_rules_sort_col;
                display_rules_sort_dir_2nd = display_rules_sort_dir;
                display_rules_sort_col = col_click;
                display_rules_sort_dir = true;
            }
        }


        {
            SDL_Rect src_rect = {1088 + (192 * 2) + (display_rules_sort_dir_2nd ? 0 : 192), 2336, 192, 192};
            SDL_Rect dst_rect = {list_pos.x + display_rules_sort_col_2nd * cell_width, list_pos.y, cell_width, cell_width};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
        }
        {
            SDL_Rect src_rect = {1088 + (display_rules_sort_dir ? 0 : 192), 2336, 192, 192};
            SDL_Rect dst_rect = {list_pos.x + display_rules_sort_col * cell_width, list_pos.y, cell_width, cell_width};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
        }

        struct RuleDiplay
        {
            unsigned index;
            GridRule* rule;
        };
        struct RuleDiplaySort
        {
            int col = 0;
            bool descend = false;
            bool cur_level = false;
            Grid* grid;
            bool cpu_debug = false;
            RuleDiplaySort(int col_, bool descend_, bool cur_level_, Grid* grid_, bool cpu_debug_):
                col(col_), descend(descend_), cur_level(cur_level_), grid(grid_), cpu_debug(cpu_debug_)
            {};
            bool operator() (RuleDiplay a_,RuleDiplay b_)
            {
                RuleDiplay &a = descend ? a_ : b_;
                RuleDiplay &b = descend ? b_ : a_;
                if (col == 0)
                    return (a.index < b.index);
                if (col == 1)
                {
                    int pa = a.rule->paused ? -10 : a.rule->priority;
                    int pb = b.rule->paused ? -10 : b.rule->priority;
                    if (!a.rule->paused && (a.rule->apply_region_type.type == RegionType::VISIBILITY || a.rule->apply_region_type.type == RegionType::SET))
                        pa = 3;
                    if (!b.rule->paused && (b.rule->apply_region_type.type == RegionType::VISIBILITY || b.rule->apply_region_type.type == RegionType::SET))
                        pb = 3;
                    return pa < pb;
                }
                if (col == 2)
                    return (a.rule->apply_region_type < b.rule->apply_region_type);
                if (col == 3)
                {
                    for (int i = 0; i < a.rule->region_count && i < b.rule->region_count; i++)
                    {
                        if (a.rule->get_region_sorted(i) < b.rule->get_region_sorted(i))
                            return true;
                        if (b.rule->get_region_sorted(i) < a.rule->get_region_sorted(i))
                            return false;
                    }
                    return (a.rule->region_count < b.rule->region_count);
                }
                if (cpu_debug && (col == 5))
                {
                    if (a.rule->cpu_time < b.rule->cpu_time)
                        return true;
                    if (b.rule->cpu_time < a.rule->cpu_time)
                        return false;
                }
                if (col == 5)
                {
                    if (a_.rule->apply_region_type.type == RegionType::VISIBILITY &&
                        b_.rule->apply_region_type.type != RegionType::VISIBILITY)
                            return false;
                    if (b_.rule->apply_region_type.type == RegionType::VISIBILITY &&
                        a_.rule->apply_region_type.type != RegionType::VISIBILITY)
                            return true;
                    if (cur_level)
                        return (grid->level_used_count[a.rule] < grid->level_used_count[b.rule]);
                    else
                        return (a.rule->used_count < b.rule->used_count);
                }
                if (col == 6)
                {
                    if (a_.rule->apply_region_type.type == RegionType::VISIBILITY &&
                        b_.rule->apply_region_type.type != RegionType::VISIBILITY)
                            return false;
                    if (b_.rule->apply_region_type.type == RegionType::VISIBILITY &&
                        a_.rule->apply_region_type.type != RegionType::VISIBILITY)
                            return true;
                    if (cur_level)
                        return (grid->level_clear_count[a.rule] < grid->level_clear_count[b.rule]);
                    else
                        return (a.rule->clear_count < b.rule->clear_count);
                }
                assert(0);
                return (a.index < b.index);
            }
        };
        std::vector<RuleDiplay> rules_list;
        unsigned i = 0;
        for (GridRule& r : display_clipboard_rules ? clipboard_rule_set : rules[game_mode])
        {
            if (r.deleted)
                continue;
            r.resort_region();
            rules_list.push_back(RuleDiplay{i, &r});
            i++;
        }

        std::stable_sort (rules_list.begin(), rules_list.end(), RuleDiplaySort(display_rules_sort_col_2nd, display_rules_sort_dir_2nd, display_rules_level, grid, debug_bits[0]));
        std::stable_sort (rules_list.begin(), rules_list.end(), RuleDiplaySort(display_rules_sort_col, display_rules_sort_dir, display_rules_level, grid, debug_bits[0]));

        if (rules_list_offset + row_count > (int)rules_list.size())
            rules_list_offset = rules_list.size() - row_count;
        rules_list_offset = std::max(rules_list_offset, 0);

        for (int rule_index = 0; rule_index < row_count; rule_index++)
        {
            if (rule_index + rules_list_offset >= (int)rules_list.size())
                break;
            RuleDiplay& rd = rules_list[rule_index + rules_list_offset];
            GridRule& rule = *rd.rule;
            if (((right_panel_mode == RIGHT_MENU_RULE_INSPECT) && ((&rule == inspected_rule.rule) || selected_rules.count(&rule))) ||
                ((right_panel_mode == RIGHT_MENU_RULE_GEN) && (&rule == replace_rule)))
            {
                render_box(list_pos + XYPos(0, cell_width + rule_index * cell_height), XYPos(cell_width * 7, cell_height), cell_height / 4, 9);
            }

            render_number(rd.index, list_pos + XYPos(0 * cell_width, cell_width + rule_index * cell_height + cell_height/10), XYPos(cell_width, cell_height*8/10));
            {
                int priority = std::clamp(int(rule.priority), -2, 2);
                if (rule.paused)
                    priority = -3;
                if (rule.apply_region_type.type == RegionType::VISIBILITY || rule.apply_region_type.type == RegionType::SET)
                    if (!rule.paused)
                        priority = -4;
                SDL_Rect src_rect = {2432, 1344 + (2 - priority) * 128, 128, 128};
                SDL_Rect dst_rect = {list_pos.x + cell_width + cell_width / 2 - cell_height / 2, list_pos.y + cell_width + rule_index * cell_height, cell_height, cell_height};
                SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            }
            if (rule.apply_region_type.type >= RegionType::Type::SET)
                render_region_type(rule.apply_region_type, list_pos + XYPos(2 * cell_width + (cell_width - cell_height) / 2, cell_width + rule_index * cell_height), cell_height);
            else
                render_region_bubble(rule.apply_region_type, 0, list_pos + XYPos(2 * cell_width + (cell_width - cell_height) / 2, cell_width + rule_index * cell_height), cell_height);
           
            for (int i = 0; i <rule.region_count; i++)
            {
                render_region_bubble(rule.get_region_sorted(i), 0, list_pos + XYPos(3 * cell_width + i * cell_height, cell_width + rule_index * cell_height), cell_height);
            }
            if(debug_bits[0])
            {
                render_number(rule.cpu_time, list_pos + XYPos(5 * cell_width, cell_width + rule_index * cell_height + cell_height/10), XYPos(cell_width * 9 / 10, cell_height*8/10));
            }
            else if(!display_clipboard_rules && rule.apply_region_type.type != RegionType::VISIBILITY)
            {
                int num_used = display_rules_level ? grid->level_used_count[&rule] : rule.used_count;
                int num_clear = display_rules_level ? grid->level_clear_count[&rule] : rule.clear_count;
                render_number(num_used, list_pos + XYPos(5 * cell_width, cell_width + rule_index * cell_height + cell_height/10), XYPos(cell_width * 9 / 10, cell_height*8/10));
                render_number(num_clear, list_pos + XYPos(6 * cell_width, cell_width + rule_index * cell_height + cell_height/10), XYPos(cell_width * 9 / 10, cell_height*8/10));
            }

            if (!display_clipboard_rules && display_rules_click_drag && display_rules_click_line && !display_rules_click && ((mouse - list_pos - XYPos(0, cell_width + rule_index * cell_height)).inside(XYPos(cell_width * 7, cell_height))))
            {
                // if (selected_rules.empty())
                // {
                //     if (inspected_rule.rule != &rule && (right_panel_mode == RIGHT_MENU_RULE_INSPECT))
                //     {
                //         if ((display_rules_sort_col != 0) || (display_rules_sort_dir != true))
                //         {
                //             display_rules_sort_col = 0;
                //             display_rules_sort_dir = true;
                //             display_rules_sort_col_2nd = 1;
                //             display_rules_sort_dir_2nd = true;
                //         }
                //         else
                //         {
                //             std::list<GridRule>::iterator from, to = rules[game_mode].end();

                //             for (std::list<GridRule>::iterator it = rules[game_mode].begin(); it != rules[game_mode].end(); ++it)
                //             {
                //                 if (&(*it) == &rule)
                //                 {
                //                     to = it;
                //                     to++;
                //                 }
                //                 if (&(*it) == inspected_rule.rule)
                //                 {
                //                     from = it;
                //                     to--;
                //                 }
                //             }
                //             rules[game_mode].splice(to, rules[game_mode], from);
                //         }
                //     }
                // }
                // else
                {
                    if (!selected_rules.count(&rule) && (right_panel_mode == RIGHT_MENU_RULE_INSPECT))
                    {
                        if ((display_rules_sort_col != 0) || (display_rules_sort_dir != true))
                        {
                            display_rules_sort_col = 0;
                            display_rules_sort_dir = true;
                            display_rules_sort_col_2nd = 1;
                            display_rules_sort_dir_2nd = true;
                        }
                        else
                        {
                            for (GridRule* s_rule : selected_rules)
                            {
                                std::list<GridRule>::iterator from, to = rules[game_mode].end();

                                for (std::list<GridRule>::iterator it = rules[game_mode].begin(); it != rules[game_mode].end(); ++it)
                                {
                                    if (&(*it) == &rule)
                                    {
                                        to = it;
                                        to++;
                                    }
                                    if (&(*it) == s_rule)
                                    {
                                        from = it;
                                        to--;
                                    }
                                }
                                rules[game_mode].splice(to, rules[game_mode], from);
                            }
                        }
                    }
                }
            }

            if (display_rules_click && ((display_rules_click_pos - list_pos - XYPos(0, cell_width + rule_index * cell_height)).inside(XYPos(cell_width * 7, cell_height))))
            {
                if (!display_clipboard_rules)
                {
                    if (right_panel_mode != RIGHT_MENU_RULE_INSPECT)
                    {
                        right_panel_mode = RIGHT_MENU_RULE_INSPECT;
                        inspected_rule = GridRegionCause(&rule, NULL, NULL, NULL, NULL);
                        selected_rules.clear();
                        selected_rules.insert(&rule);
                    }
                    else
                    {
                        if (ctrl_held)
                        {
                            if (selected_rules.count(&rule))
                            {
                                selected_rules.erase(&rule);
                                if (selected_rules.empty())
                                {
                                    right_panel_mode = RIGHT_MENU_NONE;
                                }
                                else
                                {
                                    inspected_rule.rule = *selected_rules.begin();
                                }
                                display_rules_click_drag = false;
                            }
                            else
                            {
                                inspected_rule = GridRegionCause(&rule, NULL, NULL, NULL, NULL);
                                selected_rules.insert(&rule);
                            }
                        }
                        else if (shift_held)
                        {
                            bool setting = false;
                            for (RuleDiplay& rdisp : rules_list)
                            {
                                GridRule* r = rdisp.rule;
                                if (setting)
                                    selected_rules.insert(r);
                                if (r == inspected_rule.rule)
                                    setting = !setting;
                                if (r == &rule)
                                    setting = !setting;
                                if (setting)
                                    selected_rules.insert(r);
                            }
                        }
                        else
                        {
                            inspected_rule = GridRegionCause(&rule, NULL, NULL, NULL, NULL);
                            selected_rules.clear();
                            selected_rules.insert(&rule);
                        }
                    }
                }
                else
                {
                    reset_rule_gen_region();
                    constructed_rule = rule;
                    right_panel_mode = RIGHT_MENU_RULE_GEN;
                    update_constructed_rule();
                }
                display_rules_click_line = true;

            }
                

        }
        {
            SDL_Rect src_rect = {1664, 1344, 64, 64};
            SDL_Rect dst_rect = {list_pos.x + cell_width * 7, list_pos.y + cell_width * 1, cell_width/2, cell_width/2};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            if (display_rules_click && ((display_rules_click_pos - XYPos(dst_rect.x, dst_rect.y)).inside(XYPos(dst_rect.w, dst_rect.h))))
                rules_list_offset--;
        }
        {
            SDL_Rect src_rect = {1664, 1408, 64, 64};
            SDL_Rect dst_rect = {list_pos.x + cell_width * 7, list_pos.y + cell_width * 7, cell_width/2, cell_width/2};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            if (display_rules_click && ((display_rules_click_pos - XYPos(dst_rect.x, dst_rect.y)).inside(XYPos(dst_rect.w, dst_rect.h))))
                rules_list_offset++;
        }

        if (rules_list_offset + row_count > (int)rules_list.size())
            rules_list_offset = rules_list.size() - row_count;
        rules_list_offset = std::max(rules_list_offset, 0);

        {
            int full_size = cell_width * 5.5;
            int all_count = rules_list.size();
            int box_height = full_size;
            int box_pos = 0;

            if (all_count > row_count)
            {
                box_height = (row_count * full_size) / all_count;
                box_height = std::max(cell_width / 4, box_height);
                box_pos = (rules_list_offset * (full_size - box_height)) / (all_count - row_count);
                if (display_rules_click_drag && ((display_rules_click_pos - list_pos - XYPos(cell_width * 7, cell_width * 1.5)).inside(XYPos(cell_width/2, cell_width * 5.5))))
                {
                    int p = mouse.y - list_pos.y - cell_width * 1.5 - box_height / 2;
                    p = (p * (all_count - row_count)) / (full_size - box_height);
                    rules_list_offset = p;
                }
            }

            render_box(list_pos + XYPos(cell_width * 7, cell_width * 1.5 + box_pos), XYPos(cell_width / 2, box_height), std::min(box_height/2, cell_width / 4));
        }

        if (rules_list_offset + row_count > (int)rules_list.size())
            rules_list_offset = rules_list.size() - row_count;
        rules_list_offset = std::max(rules_list_offset, 0);
        display_rules_click = false;
        if (!display_rules_click_drag)
            display_rules_click_line = false;
    }
    else
    {
        // {
        //     unsigned anim_length = 10000;
        //     double anim = double(frame % (anim_length * 2)) / anim_length - 1.0;
        //     double fade = std::max(0.7 - anim * anim, 0.0);
        //     SDL_SetTextureAlphaMod(sdl_texture, 255.0 * fade);
        //     anim = ((anim < 0) ? -1 : 1) * anim * anim;
        //     double siz = 0.05;
        //     int bg_size = panel_size.y;
        //     XYPos bg_pos = left_panel_offset + XYPos(panel_size.x, 0);

        //     Rand r(0);
        //     XYPosFloat b(double(r % 1000000000) / 1000000000, double(r % 1000000000) / 1000000000);
        //     Angle angle = double(r % 1000000000) / 10000000;
        //     FOR_XY(p, XYPos(0, 0), XYPos(5, 5))
        //     {
        //         Angle myangle = angle + (double(r % 1000000000) / 100000000) * anim;
        //         XYPosFloat sp = XYPosFloat(p) * siz;
        //         sp = sp.rotate(angle);
        //         XYPosFloat mov (Angle(double(r % 1000000000) / 10000000), anim);
        //         sp += mov;
        //         sp += b;
        //         sp *= bg_size;
        //         SDL_Rect src_rect = {512, 416, 64, 64};
        //         SDL_Rect dst_rect = {int(bg_pos.x + sp.x) - int(ceil(bg_size * siz / 2)), int(bg_pos.y + sp.y) - int(ceil(bg_size * siz / 2)), int(ceil(bg_size * siz)), int(ceil(bg_size * siz))};
        //         SDL_Point rot_center = {int(ceil(bg_size * siz / 2)), int(ceil(bg_size * siz / 2))};
        //         SDL_RenderCopyEx(sdl_renderer, sdl_texture, &src_rect, &dst_rect, degrees(myangle), &rot_center, SDL_FLIP_NONE);
        //     }
        //     SDL_SetTextureAlphaMod(sdl_texture, 255);
        // }
        XYSet grid_squares = grid->get_squares();

        std::vector<WrapPos> wraps;

        wraps.push_back(WrapPos{scaled_grid_offset, 1.0});
        if (grid->wrapped == Grid::WRAPPED_IN)
        {
            XYRect sq_pos = grid->get_square_pos(grid->innie_pos, grid->get_grid_pitch(XYPos(scaled_grid_size, scaled_grid_size)));
            double mag = double(scaled_grid_size) / double(sq_pos.size.x);

            wraps.push_back(WrapPos{scaled_grid_offset - sq_pos.pos * mag, 1.0 * mag});

            double z = 1.0;
            XYPos p;
            for (int i = 0; i < 10; i++)
            {
                p += sq_pos.pos * z;
                z /= mag;
                wraps.push_back(WrapPos{scaled_grid_offset + p, z});
            }
        }
        else if (grid->wrapped == Grid::WRAPPED_SIDE)
        {
            XYPos wrap_size = grid->get_wrapped_size(grid_pitch);
            FOR_XY(r, XYPos(-2, -2), XYPos(5, 5))
            if (r != XYPos(0, 0))
                wraps.push_back(WrapPos{scaled_grid_offset + wrap_size * r, 1.0});
        }



        FOR_XY_SET(pos, grid_squares)
        {
            Colour base(contrast * 0.6,contrast * 0.6,contrast * 0.6);
            if (colors > 0)
            {
                double p = (double)pos.y / grid->size.y * radians(360) + (double)frame / 2000;
                base.r += sin(p) * contrast * 0.4 * colors;
                base.g += sin(p + radians(120)) * contrast * 0.4 * colors;
                base.b += sin(p + radians(230)) * contrast * 0.4 * colors;
            }

            Colour bg_col(0,0,0);
            {
                if (clue_solves.count(pos))
                {
                    bg_col = Colour(0, contrast, 0);
                }
                if (filter_pos.get(pos))
                {
                    bg_col = Colour(contrast ,0, 0);
                }
                if (hover_rulemaker && hover_squares_highlight.get(pos))
                {
                    bg_col = Colour(contrast, contrast, 0);
                }
                // if (grid->get(pos).bomb && grid->get(pos).revealed)
                // {
                //     bg_col = Colour(contrast, contrast, contrast);
                // }
            }
            std::vector<RenderCmd> cmds;
            grid->render_square(pos, grid_pitch, cmds);
            for (RenderCmd& cmd : cmds)
            {
                for(WrapPos r : wraps)
                {
                    SDL_SetTextureColorMod(sdl_texture, base.r,  base.g, base.b);

                    if (cmd.bg)
                    {
                        if (bg_col == Colour(0, 0, 0))
                            continue;
                        SDL_SetTextureColorMod(sdl_texture, bg_col.r,  bg_col.g, bg_col.b);
                    }
                    else if (grid->wrapped == Grid::WRAPPED_IN)
                    {
                        double d = log(grid_pitch.y * r.size / grid_size) * 1.2;
                        uint8_t r = ((((sin(d) + 1) / 2) * 100 + 155) * base.r) / 255;
                        uint8_t g = ((((sin(d+2) + 1) / 2) * 100 + 155) * base.g) / 255;
                        uint8_t b = ((((sin(d+4) + 1) / 2) * 100 + 155) * base.b) / 255;
                        SDL_SetTextureColorMod(sdl_texture, r, g, b);
                    }
                    SDL_Rect src_rect = {cmd.src.pos.x, cmd.src.pos.y, cmd.src.size.x, cmd.src.size.y};
                    SDL_Rect dst_rect = {grid_offset.x + r.pos.x + int(cmd.dst.pos.x * r.size), grid_offset.y + r.pos.y + int(cmd.dst.pos.y * r.size), int(cmd.dst.size.x * r.size), int(ceil(cmd.dst.size.y * r.size))};
                    SDL_Point rot_center = {int(cmd.center.x * r.size), int(cmd.center.y)};
                    SDL_RenderCopyEx(sdl_renderer, sdl_texture, &src_rect, &dst_rect, cmd.angle, &rot_center, SDL_FLIP_NONE);
                }
            }
        }
        SDL_SetTextureColorMod(sdl_texture, contrast, contrast, contrast);

        std::map<XYPos, int> total_taken;
        std::list<GridRegion*> display_regions;

        XYPos mouse_filter_pos(-1,-1);
        if ((mouse - grid_offset).inside(XYPos(grid_size,grid_size)))
        {
            XYPos mouse_square = grid->get_square_from_mouse_pos(mouse - grid_offset - scaled_grid_offset, grid_pitch);
            if (mouse_square != XYPos(-1,-1))
            {
                mouse_square = grid->get_base_square(mouse_square);
                if (mouse_mode == MOUSE_MODE_FILTER)
                {
                    mouse_filter_pos = mouse_square;
                }
                else
                {
                    if (grid->cell_causes.count(mouse_square))
                    {
                        mouse_cursor = SDL_SYSTEM_CURSOR_HAND;
                    }

                    for (GridRegion& region : grid->regions)
                    {
                        if (!region.gen_cause.rule && (region.gen_cause_pos == mouse_square))
                        {
                            if (!display_menu)
                            {
                                mouse_hover_region = &region;
                                if (!grid_dragging)
                                    mouse_cursor = SDL_SYSTEM_CURSOR_HAND;
                            }
                        }
                    }
                }
            }
        }
        {
            bool has_hover = false;
            for (GridRegion& region : grid->regions)
            {
                if (!filter_pos.none() && !region.elements.contains(filter_pos))
                    continue;
                if (mouse_filter_pos.x >= 0 && !filter_pos.empty() && !region.elements.contains(mouse_filter_pos))
                    continue;
                if (region.vis_level == vis_level)
                {
                    display_regions.push_back(&region);
                    if (&region == mouse_hover_region)
                        has_hover = true;
                }
            }
            if (mouse_hover_region && !has_hover)
                display_regions.push_back(mouse_hover_region);
        }

        for (GridRegion* region : display_regions)
        {
            FOR_XY_SET(pos, region->elements)
            {
                total_taken[pos]++;
            }
        }

        for (int i = 0; i < 3; i++)
        {
            {
                std::map<XYPos, int> taken;
                for (GridRegion* region : display_regions)
                    render_region_bg(*region, taken, total_taken, wraps, i);
            }
            {
                std::map<XYPos, int> taken;
                for (GridRegion* region : display_regions)
                    render_region_fg(*region, taken, total_taken, wraps, i);
            }
        }

        FOR_XY_SET(pos, grid_squares)
        {
            GridPlace place = grid->get(pos);
            if (place.revealed)
            {
                XYRect sq_pos = grid->get_icon_pos(pos, grid_pitch);
                unsigned anim_prog = grid_cells_animation[pos];
                unsigned max_anim_frame = 500;
                grid_cells_animation[pos] = std::min(anim_prog + frame_step, max_anim_frame);
                double fade = 1.0 - (double(anim_prog) / double(max_anim_frame));
                double wob = -sin((10 / (fade + 0.3))) * (fade * fade);
                if (grid->last_cleared_regions.get(pos))
                {
                    sq_pos.pos.x += cos(double(frame) / 300) * (sq_pos.size.x / 100 + 2);
                    sq_pos.pos.y += sin(double(frame) / 473) * (sq_pos.size.x / 100 + 2);
                }

                sq_pos.pos -= XYPosFloat(sq_pos.size) * (wob / 2);
                sq_pos.size += XYPosFloat(sq_pos.size) * (wob);

                int icon_width = std::min(sq_pos.size.x, sq_pos.size.y);
                XYPos gpos = sq_pos.pos + (sq_pos.size - XYPos(icon_width,icon_width)) / 2;
                SDL_SetTextureAlphaMod(sdl_texture, 255.0 * (1.0 - pow(fade, 0.5)));
                for(WrapPos r : wraps)
                {
                    XYPos mgpos = grid_offset + gpos * r.size + r.pos;
                    int siz = icon_width * r.size;
                    if (!XYRect(mgpos, XYPos(siz, siz)).overlaps(XYRect(XYPos(0,0), window_size)))
                        continue;


                    if (place.bomb)
                    {
                        SDL_Rect src_rect = {320, 192, 192, 192};
                        SDL_Rect dst_rect = {mgpos.x, mgpos.y, siz, siz};
//                        SDL_SetTextureColorMod(sdl_texture, 0,0,0);
                        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
//                        SDL_SetTextureColorMod(sdl_texture, contrast, contrast, contrast);
                    }
                    else
                    {
                        render_region_type(place.clue, mgpos, icon_width * r.size);
                    }
                }
                SDL_SetTextureAlphaMod(sdl_texture, 255);
            }
        }
        if (!overlay_texture_is_clean)
            for(WrapPos r : wraps)
            {
                XYPos mgpos = grid_offset + r.pos;
                XYPos mgsize = grid->get_wrapped_size(grid_pitch);
                {
                    SDL_Rect src_rect = {0, 0, 2048, 2048};
                    SDL_Rect dst_rect = {mgpos.x, mgpos.y, int(mgsize.x * r.size), int(mgsize.y * r.size)};
                    SDL_RenderCopy(sdl_renderer, overlay_texture, &src_rect, &dst_rect);
                }
            }

        if (row_col_clues)
        {
            EdgePos* edge_hover_pos = NULL;
            std::vector<EdgePos> edges;
            grid->get_edges(edges, grid_pitch);
            {
                SDL_Rect src_rect = {10, 421, 1, 1};
                SDL_Rect dst_rect = {left_panel_offset.x + panel_size.x, left_panel_offset.y, edge_clue_border, panel_size.y};
                SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                dst_rect = {left_panel_offset.x + panel_size.x, left_panel_offset.y, right_panel_offset.x - left_panel_offset.x + panel_size.x, edge_clue_border};
                SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                dst_rect = {right_panel_offset.x - edge_clue_border, right_panel_offset.y, edge_clue_border, panel_size.y};
                SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                dst_rect = {left_panel_offset.x + panel_size.x, left_panel_offset.y + panel_size.y - edge_clue_border, right_panel_offset.x - left_panel_offset.x + panel_size.x, edge_clue_border};
                SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);

            }
            double best_distance = 1000000000;
            for (EdgePos& edge : edges)
            {
                if (edge == best_edge_pos)
                {
                    edges.push_back(best_edge_pos);
                    break;
                }
            }

            for (EdgePos& edge : edges)
            {
                int arrow_size = edge_clue_border * 82 / 100;
                int bubble_margin = arrow_size / 16;
                XYPosFloat epos = -scaled_grid_offset + XYPosFloat(0.1,0.1);

                double p = edge.pos / std::cos(edge.angle) + std::tan(edge.angle) * (epos.x) - epos.y;
                if (p >= 0 && p < grid_size && edge.angle != XYPosFloat(0,1).angle())
                {
                    double angle = edge.angle;
                    if (XYPosFloat(Angle(angle), 1).x < 0)
                        angle += M_PI;
                    XYPos gpos = XYPos(-arrow_size, -arrow_size + p);
                    SDL_Rect src_rect = {1664, 192, 192, 192};
                    SDL_Rect dst_rect = {grid_offset.x + gpos.x, grid_offset.y + gpos.y, arrow_size, arrow_size};
                    SDL_Point rot_center = {arrow_size, arrow_size};
                    SDL_RenderCopyEx(sdl_renderer, sdl_texture, &src_rect, &dst_rect, degrees(angle) - 45.0, &rot_center, SDL_FLIP_NONE);
                    XYPos t = XYPosFloat(-arrow_size / 2.0, -arrow_size / 2.0).rotate(angle - (M_PI / 4)) - XYPos(arrow_size / 2.0, arrow_size / 2.0);
                    render_region_type(edge.type, grid_offset + t + XYPos(0 + bubble_margin, p + bubble_margin), arrow_size - bubble_margin * 2);
                    double distance = XYPosFloat(mouse - (grid_offset + t + XYPos(0 + bubble_margin, p + bubble_margin) + XYPos(arrow_size / 2.0, arrow_size / 2.0))).distance();
                    if (distance < (arrow_size / 2.0))
                        edge_hover_pos = &edge;
                    if (distance < best_distance)
                    {
                        best_distance = distance;
                        best_edge_pos = edge;
                    }
                }
                p = -edge.pos / std::sin(edge.angle) + (epos.y) / std::tan(edge.angle) - epos.x;
                if (p >= 0 && p < grid_size && edge.angle != XYPosFloat(1,0).angle())
                {
                    double angle = edge.angle;
                    if (XYPosFloat(Angle(angle), 1).y < 0)
                        angle += M_PI;
                    XYPos gpos = XYPos(-arrow_size + p, -arrow_size);
                    SDL_Rect src_rect = {1664, 192, 192, 192};
                    SDL_Rect dst_rect = {grid_offset.x + gpos.x, grid_offset.y + gpos.y, arrow_size, arrow_size};
                    SDL_Point rot_center = {arrow_size, arrow_size};
                    SDL_RenderCopyEx(sdl_renderer, sdl_texture, &src_rect, &dst_rect, degrees(angle) - 45.0, &rot_center, SDL_FLIP_NONE);
                    XYPos t = XYPosFloat(-arrow_size / 2.0, -arrow_size / 2.0).rotate(angle - (M_PI / 4)) - XYPos(arrow_size / 2.0, arrow_size / 2.0);
                    render_region_type(edge.type, grid_offset + t + XYPos(p + bubble_margin, 0 + bubble_margin), arrow_size - bubble_margin * 2);
                    double distance = XYPosFloat(mouse - (grid_offset + t + XYPos(p + bubble_margin, 0 + bubble_margin) + XYPos(arrow_size / 2.0, arrow_size / 2.0))).distance();
                    if (distance < (arrow_size / 2.0))
                        edge_hover_pos = &edge;
                    if (distance < best_distance)
                    {
                        best_distance = distance;
                        best_edge_pos = edge;
                    }
                }
                p = edge.pos / std::cos(edge.angle) + std::tan(edge.angle) * (grid_size + epos.x) - epos.y;
                if (p >= 0 && p < grid_size && edge.angle != XYPosFloat(0,1).angle())
                {
                    double angle = edge.angle;
                    if (XYPosFloat(Angle(angle), 1).x > 0)
                        angle += M_PI;
                    XYPos gpos = XYPos(-arrow_size, -arrow_size + p);
                    SDL_Rect src_rect = {1664, 192, 192, 192};
                    SDL_Rect dst_rect = {grid_offset.x + grid_size + gpos.x, grid_offset.y + gpos.y, arrow_size, arrow_size};
                    SDL_Point rot_center = {arrow_size, arrow_size};
                    SDL_RenderCopyEx(sdl_renderer, sdl_texture, &src_rect, &dst_rect, degrees(angle) - 45.0, &rot_center, SDL_FLIP_NONE);
                    XYPos t = XYPosFloat(-arrow_size / 2.0, -arrow_size / 2.0).rotate(angle - (M_PI / 4)) - XYPos(arrow_size / 2.0, arrow_size / 2.0);
                    render_region_type(edge.type, grid_offset + t + XYPos(grid_size + bubble_margin, p + bubble_margin), arrow_size - bubble_margin * 2);
                    double distance = XYPosFloat(mouse - (grid_offset + t + XYPos(grid_size + bubble_margin, p + bubble_margin) + XYPos(arrow_size / 2.0, arrow_size / 2.0))).distance();
                    if (distance < (arrow_size / 2.0))
                        edge_hover_pos = &edge;
                    if (distance < best_distance)
                    {
                        best_distance = distance;
                        best_edge_pos = edge;
                    }
                }

                p = -edge.pos / std::sin(edge.angle) + (grid_size + epos.y) / std::tan(edge.angle) - epos.x;
                if (p >= 0 && p < grid_size && edge.angle != XYPosFloat(1,0).angle())
                {
                    double angle = edge.angle;
                    if (XYPosFloat(Angle(angle), 1).y > 0)
                        angle += M_PI;
                    XYPos gpos = XYPos(-arrow_size + p, -arrow_size);
                    SDL_Rect src_rect = {1664, 192, 192, 192};
                    SDL_Rect dst_rect = {grid_offset.x + gpos.x, grid_offset.y + grid_size + gpos.y, arrow_size, arrow_size};
                    SDL_Point rot_center = {arrow_size, arrow_size};
                    SDL_RenderCopyEx(sdl_renderer, sdl_texture, &src_rect, &dst_rect, degrees(angle) - 45.0, &rot_center, SDL_FLIP_NONE);
                    XYPos t = XYPosFloat(-arrow_size / 2.0, -arrow_size / 2.0).rotate(angle - (M_PI / 4)) - XYPos(arrow_size / 2.0, arrow_size / 2.0);
                    render_region_type(edge.type, grid_offset + t + XYPos(p + bubble_margin, grid_size + bubble_margin), arrow_size - bubble_margin * 2);
                    double distance = XYPosFloat(mouse - (grid_offset + t + XYPos(p + bubble_margin, grid_size + bubble_margin) + XYPos(arrow_size / 2.0, arrow_size / 2.0))).distance();
                    if (distance < (arrow_size / 2.0))
                        edge_hover_pos = &edge;
                    if (distance < best_distance)
                    {
                        best_distance = distance;
                        best_edge_pos = edge;
                    }
                }
            }
            if (edge_hover_pos)
            {
                XYPos p = edge_hover_pos->rule_pos;
                p.x += 1000;
                for (GridRegion& region : grid->regions)
                {
                    if (!region.gen_cause.rule && (region.gen_cause_pos == p))
                    {
                        if (!display_menu)
                        {
                            mouse_hover_region = &region;
                            if (!grid_dragging)
                                mouse_cursor = SDL_SYSTEM_CURSOR_HAND;
                        }
                    }
                }
            }
        }
        for (GridRegion* region : display_regions)
        {
            int r = grid_regions_fade[region];
            if (!mouse_hover_region || (region == mouse_hover_region))
                r = std::min(r + frame_step, 500);
            else
                r = std::max(r - frame_step, 0);
            grid_regions_fade[region] = r;

            grid_regions_animation[region] = std::min(grid_regions_animation[region] + frame_step, 500);
        }


        {
            SDL_Rect src_rect = {1, 417, 1, 1};
            SDL_Rect dst_rect = {0, 0, left_panel_offset.x + panel_size.x, window_size.y};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            dst_rect = {right_panel_offset.x, 0, window_size.x - right_panel_offset.x, window_size.y};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            dst_rect = {left_panel_offset.x + panel_size.x, 0, right_panel_offset.x - (left_panel_offset.x + panel_size.x), right_panel_offset.y};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            dst_rect = {left_panel_offset.x + panel_size.x, right_panel_offset.y + panel_size.y, right_panel_offset.x - (left_panel_offset.x + panel_size.x), window_size.y - (right_panel_offset.y + panel_size.y)};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
        }
    }
    {
        render_box(XYPos(0,0), XYPos(left_panel_offset.x + panel_size.x, window_size.y), button_size/2, 5);
        render_box(XYPos(right_panel_offset.x,0), XYPos(window_size.x - right_panel_offset.x, window_size.y), button_size/2, 6);
    }
    


    render_button(XYPos(1280, 1536), XYPos(left_panel_offset.x + 0 * button_size, left_panel_offset.y + button_size * 0), "Main Menu");
    render_button(XYPos(704, 960), XYPos(left_panel_offset.x + 1 * button_size, left_panel_offset.y + button_size * 0), "Help");
    render_button(XYPos(1472, 960), XYPos(left_panel_offset.x + 2 * button_size, left_panel_offset.y + button_size * 0), "Hint");
    render_button(XYPos(704 + 192 * 3, 960), XYPos(left_panel_offset.x + 3 * button_size, left_panel_offset.y + button_size * 0), "Next Level");
    render_button(XYPos(704 + 192 * 2, 960), XYPos(left_panel_offset.x + 4 * button_size, left_panel_offset.y + button_size * 0), "Refresh Regions");
    if (render_lock(PROG_LOCK_SPEED, XYPos(left_panel_offset.x + 0 * button_size, left_panel_offset.y + button_size * 1), XYPos(button_size * 3, button_size)))
    {
        SDL_Rect src_rect = {704, 1920, 576, 192};
        SDL_Rect dst_rect = {left_panel_offset.x + 0 * button_size, left_panel_offset.y + button_size * 1, button_size * 3, button_size};
        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
        add_tooltip(dst_rect, "Speed", false);

        src_rect = {1280, 1920, 64, 192};
        dst_rect = {left_panel_offset.x + int(speed_dial * 2.6666 * button_size), left_panel_offset.y + button_size * 1, button_size / 3, button_size};
        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
    }

    if (render_lock(PROG_LOCK_FILTER, XYPos(left_panel_offset.x + 4 * button_size, left_panel_offset.y + 1 * button_size), XYPos(button_size, button_size)))
    {
        render_box(left_panel_offset + XYPos(button_size * 3, button_size * 1), XYPos(button_size * 2, button_size), button_size/4, 4);
        if (!filter_pos.empty())
        render_button(XYPos(1472, 192), XYPos(left_panel_offset.x + 3 * button_size, right_panel_offset.y + button_size * 1), "Clear Filter");

        {
            render_button(XYPos(1280, 192), XYPos(left_panel_offset.x + 4 * button_size, right_panel_offset.y + button_size * 1), "Filter", (mouse_mode == MOUSE_MODE_FILTER) ? 3 : 0);
            int s = filter_pos.count();
            if (s)
            {
                render_number(s, left_panel_offset + XYPos(button_size * 4 + button_size / 4, button_size * 1 + button_size / 4.3), XYPos(button_size/2, button_size/4));
            }
        }
    }
    if (render_lock(PROG_LOCK_TABLES, XYPos(left_panel_offset.x + 0 * button_size, left_panel_offset.y + 2 * button_size), XYPos(button_size * 2, button_size * 3)))
    {
        render_box(left_panel_offset + XYPos(button_size * 0, button_size * 2), XYPos(button_size * 2, button_size * 3), button_size/4, 4);

        {
            SDL_Rect src_rect = {1344, 384, 192, 192};
            SDL_Rect dst_rect = {left_panel_offset.x + 0 * button_size, left_panel_offset.y + button_size * 2, button_size, button_size};
            int rule_count = 0;
            int vis_rule_count = 0;
            for (GridRule& rule : rules[game_mode])
            {
                if (!rule.deleted)
                {
                    if (rule.apply_region_type.type == RegionType::VISIBILITY)
                        vis_rule_count++;
                    else
                        rule_count++;
                }
            }

            std::string digits = std::to_string(rule_count);
            if ((game_mode ==  2 || game_mode == 3) && rule_del_count[game_mode])
            {
                digits += "+" + std::to_string(rule_del_count[game_mode]);
            }

            if (display_rules)
            {
                render_box(left_panel_offset + XYPos(button_size * 0, button_size * 2), XYPos(button_size * 2, button_size), button_size/4, 10);
                src_rect.x = 1536;
            }
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            render_number_string(digits, left_panel_offset + XYPos(button_size * 1, button_size * 2 + button_size / 20), XYPos(button_size, button_size * 8 / 20));
            render_number(vis_rule_count, left_panel_offset + XYPos(button_size * 1, button_size * 2 + button_size / 2 + button_size / 20), XYPos(button_size, button_size * 8 / 20));
            dst_rect.w *= 2;
            add_tooltip(dst_rect, "Rules", !display_rules);
        }
        {
            SDL_Rect src_rect = {1408, 2144, 192, 192};
            SDL_Rect dst_rect = {left_panel_offset.x + 0 * button_size, left_panel_offset.y + button_size * 3, button_size, button_size};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            unsigned rep = level_progress[game_mode][current_level_group_index][current_level_set_index].level_status[current_level_index].stats;

            std::string digits;
            if (rep == 0)
            {
                digits = "0%";
            }
            else if (rep < 100)
            {
                digits = "0." + std::to_string((rep / 10) % 10) + std::to_string((rep / 1) % 10) + "%";
            }
            else if (rep < 1000)
            {
                digits = std::to_string((rep / 100) % 10) + "." + std::to_string((rep / 10) % 10) + "%";
            }
            else 
            {
                digits = std::to_string((rep / 1000) % 10) + std::to_string((rep / 100) % 10) + "%";
            }
            if (display_levels)
            {
                render_box(left_panel_offset + XYPos(button_size * 0, button_size * 3), XYPos(button_size * 2, button_size), button_size/4, 10);
            }
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            render_number_string(digits, left_panel_offset + XYPos(button_size * 1 + button_size / 8, button_size * 3 + button_size / 4), XYPos(button_size * 3 / 4, button_size / 2));
            dst_rect.w *= 2;
            add_tooltip(dst_rect, "Current Level", !display_levels);

        }

        {
            SDL_Rect src_rect = {1728, 384, 192, 192};
            SDL_Rect dst_rect = {left_panel_offset.x + 0 * button_size, left_panel_offset.y + button_size * 4, button_size, button_size};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            int count = 0;
            for (unsigned i = 0; i < level_progress[game_mode][current_level_group_index].size(); i++)
            {
                LevelProgress& prog = level_progress[game_mode][current_level_group_index][i];
                for (LevelStatus& stat : prog.level_status)
                    if (stat.done)
                        count++;
            }

            if (display_scores)
            {
                render_box(left_panel_offset + XYPos(button_size * 0, button_size * 4), XYPos(button_size * 2, button_size), button_size/4, 10);
            }
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            render_number(count, left_panel_offset + XYPos(button_size * 1 + button_size / 8, button_size * 4 + button_size / 4), XYPos(button_size * 3 / 4, button_size / 2));
            if (display_scores)
                SDL_SetTextureColorMod(sdl_texture, contrast, contrast, contrast);
            dst_rect.w *= 2;
            add_tooltip(dst_rect, "Scores",  !display_scores);
        }
    }

    if (render_lock(PROG_LOCK_GAME_MODE, XYPos(left_panel_offset.x + 2 * button_size, left_panel_offset.y + 2 * button_size), XYPos(button_size, button_size)))
    {
        render_button(XYPos(2240, 576 + game_mode * 192), XYPos(left_panel_offset.x + 2 * button_size, left_panel_offset.y + button_size * 2), "Game Mode");
    }
    if (clipboard_has_item != CLIPBOARD_HAS_NONE && max_stars >= prog_stars[PROG_LOCK_LEVELS_AND_LOCKS])
    render_button(XYPos(2048, 1536), XYPos(left_panel_offset.x + 2 * button_size, left_panel_offset.y + button_size * 3), "Import Clipboard");

    if (render_lock(PROG_LOCK_VISIBILITY, XYPos(left_panel_offset.x + 3 * button_size, left_panel_offset.y + 2 * button_size), XYPos(2 * button_size, 3 * button_size)))
    {
        render_box(left_panel_offset + XYPos(button_size * 3, button_size * 2), XYPos(button_size * 2, button_size * 3), button_size/4, 4);
        int region_vis_counts[3] = {0,0,0};
        for (GridRegion& region : grid->regions)
            region_vis_counts[int(region.vis_level)]++;
        {
            SDL_Rect src_rect = {1088, 384, 192, 192};
            SDL_Rect dst_rect = {left_panel_offset.x + 4 * button_size, left_panel_offset.y + button_size * 2, button_size, button_size};
            if (vis_level == GRID_VIS_LEVEL_SHOW)
            {
                render_box(left_panel_offset+ XYPos(button_size * 3, button_size * 2), XYPos(button_size * 2, button_size), button_size/4, 10);
            }
            render_number(region_vis_counts[0], left_panel_offset + XYPos(button_size * 3 + button_size / 8, button_size * 2 + button_size / 4), XYPos(button_size * 3 / 4, button_size / 2));
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            dst_rect = {left_panel_offset.x + 3 * button_size, left_panel_offset.y + button_size * 2, button_size * 2, button_size};
            add_tooltip(dst_rect, "Visible", vis_level != GRID_VIS_LEVEL_SHOW);
        }
        {
            SDL_Rect src_rect = {896, 384, 192, 192};
            SDL_Rect dst_rect = {left_panel_offset.x + 4 * button_size, left_panel_offset.y + button_size * 3, button_size, button_size};
            if (vis_level == GRID_VIS_LEVEL_HIDE)
            {
                render_box(left_panel_offset+ XYPos(button_size * 3, button_size * 3), XYPos(button_size * 2, button_size), button_size/4, 10);
            }
            render_number(region_vis_counts[1], left_panel_offset + XYPos(button_size * 3 + button_size / 8, button_size * 3 + button_size / 4), XYPos(button_size * 3 / 4, button_size / 2));
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            dst_rect = {left_panel_offset.x + 3 * button_size, left_panel_offset.y + button_size * 3, button_size * 2, button_size};
            add_tooltip(dst_rect, "Hidden", vis_level != GRID_VIS_LEVEL_HIDE);
        }
        {
            SDL_Rect src_rect = {512, 768, 192, 192};
            SDL_Rect dst_rect = {left_panel_offset.x + 4 * button_size, left_panel_offset.y + button_size * 4, button_size, button_size};
            if (vis_level == GRID_VIS_LEVEL_BIN)
            {
                render_box(left_panel_offset+ XYPos(button_size * 3, button_size * 4), XYPos(button_size * 2, button_size), button_size/4, 10);
            }
            render_number(region_vis_counts[2], left_panel_offset + XYPos(button_size * 3 + button_size / 8, button_size * 4 + button_size / 4), XYPos(button_size * 3 / 4, button_size / 2));
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            dst_rect = {left_panel_offset.x + 3 * button_size, left_panel_offset.y + button_size * 4, button_size * 2, button_size};
            add_tooltip(dst_rect, "Trash", vis_level != GRID_VIS_LEVEL_BIN);
        }
    }

    if (max_stars >= prog_stars[PROG_LOCK_LEVELS_AND_LOCKS])
    {
        for (unsigned i = 0; i < 5; i++)
        {
            const static char* grid_names[] = {"Hexagon", "Square", "Triangle", "Infinite", "Weekly Levels"};
            if (render_lock(PROG_LOCK_HEX + i, XYPos(left_panel_offset.x + i * button_size, left_panel_offset.y + int(button_size * 5.5)), XYPos(button_size, button_size)))
            {
                if (auto_progress_all && (current_level_group_index == i))
                {
                    SDL_Rect src_rect = {1344, 1920, 128, 128};
                    SDL_Rect dst_rect = {left_panel_offset.x + (int)i * button_size, left_panel_offset.y + int(button_size * 5.5), button_size, button_size};
                    SDL_Point rot_center = {button_size / 2, button_size / 2};
                    double angle = frame * 0.05;
                    SDL_RenderCopyEx(sdl_renderer, sdl_texture, &src_rect, &dst_rect, angle, &rot_center, SDL_FLIP_NONE);
                }
                SDL_Rect src_rect = {1472, 1344 + (int)i * 192, 192, 192};
                SDL_Rect dst_rect = {left_panel_offset.x + (int)i * button_size, left_panel_offset.y + int(button_size * 5.5), button_size, button_size};
                if (i == 4)
                    src_rect = {1664, 1536, 192, 192};
                SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                add_tooltip(dst_rect, grid_names[i]);
            }
            {
                SDL_Rect src_rect = {512, (current_level_group_index == i) ? 1152 : 1344, 192, 192};
                SDL_Rect dst_rect = {left_panel_offset.x + (int)i * button_size, right_panel_offset.y + int(button_size * 5.5), button_size, button_size};
                SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            }
        }
        if (server_level_anim < PROG_ANIM_MAX)
        {
            star_burst_animations.push_back(AnimationStarBurst(XYPos(left_panel_offset.x + 4 * button_size, right_panel_offset.y + int(button_size * 5.5)), XYPos(button_size,button_size), server_level_anim, false));
            server_level_anim += frame_step;
        }

        for (unsigned i = 0; i < level_progress[game_mode][current_level_group_index].size(); i++)
        {
            XYPos pos = left_panel_offset + XYPos(button_size * (i % 5), button_size * (i / 5 + 6.5));
            if (i == current_level_set_index)
            {
                render_box(pos, XYPos(button_size, button_size), button_size/4, 10);
                if (auto_progress)
                {
                    SDL_Rect src_rect = {1344, 1920, 128, 128};
                    SDL_Rect dst_rect = {pos.x, pos.y, button_size, button_size};
                    SDL_Point rot_center = {button_size / 2, button_size / 2};
                    double angle = frame * 0.05;
                    SDL_RenderCopyEx(sdl_renderer, sdl_texture, &src_rect, &dst_rect, angle, &rot_center, SDL_FLIP_NONE);
                }
            }
            int cnt = (current_level_group_index == GLBAL_LEVEL_SETS) ?
                            server_levels[i].size() :
                            global_level_sets[current_level_group_index][i]->levels.size();

            if (!cnt || (IS_DEMO && (i % 5 + i / 5) > 3))
            {
                SDL_Rect src_rect = {1088, 192, 192, 192};
                SDL_Rect dst_rect = {pos.x, pos.y, button_size, button_size};
                SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                continue;
            }

            int need = 100;
            if (i >= 5)
                need = std::min(need, int(level_progress[game_mode][current_level_group_index][i - 5].count_todo) - 100);
            if (i % 5 >= 1)
                need = std::min(need, int(level_progress[game_mode][current_level_group_index][i - 1].count_todo) - 100);
            if (i && need > 0)
            {
                SDL_Rect src_rect = {1088, 192, 192, 192};
                SDL_Rect dst_rect = {pos.x , pos.y, button_size, button_size};
                SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                SDL_SetTextureColorMod(sdl_texture, 0, 0, 0);
                render_number(need, pos + XYPos(button_size * 45 / 192 , button_size * 77 / 192), XYPos(button_size * 102 / 192, button_size * 98 / 192));
                SDL_SetTextureColorMod(sdl_texture, contrast, contrast, contrast);
                continue;
            }
            else
            {
                if (level_progress[game_mode][current_level_group_index][i].unlock_anim_prog < PROG_ANIM_MAX)
                {
                    star_burst_animations.push_back(AnimationStarBurst(pos, XYPos(button_size,button_size), level_progress[game_mode][current_level_group_index][i].unlock_anim_prog, true));
                    level_progress[game_mode][current_level_group_index][i].unlock_anim_prog += frame_step;
                }
                
            }

            int c = level_progress[game_mode][current_level_group_index][i].count_todo;
            if (c)
            {
                render_number(level_progress[game_mode][current_level_group_index][i].count_todo, pos + XYPos(button_size / 32, button_size / 4), XYPos(button_size * 15 / 16 , button_size / 2));
                SDL_Rect dst_rect = {pos.x, pos.y, button_size, button_size};
                add_clickable_highlight(dst_rect);
            }
            else
            {
                if (level_progress[game_mode][current_level_group_index][i].star_anim_prog < PROG_ANIM_MAX)
                {
                    star_burst_animations.push_back(AnimationStarBurst(pos, XYPos(button_size,button_size), level_progress[game_mode][current_level_group_index][i].star_anim_prog, false));
                    level_progress[game_mode][current_level_group_index][i].star_anim_prog += frame_step;
                }
                SDL_Rect src_rect = {512, 960, 192, 192};
                SDL_Rect dst_rect = {pos.x, pos.y, button_size, button_size};
                SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            }
            SDL_SetTextureColorMod(sdl_texture, contrast, contrast, contrast);
        }
        {
            {
                SDL_Rect src_rect = {512, 1183, 96, 1};
                SDL_Rect dst_rect = {left_panel_offset.x, right_panel_offset.y + int(button_size * 6.5), button_size / 2, button_size * 6};
                SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            }
            {
                SDL_Rect src_rect = {512 + 96, 1183, 96, 1};
                SDL_Rect dst_rect = {left_panel_offset.x + button_size * 5 - button_size / 2, right_panel_offset.y + int(button_size * 6.5), button_size / 2, button_size * 6};
                SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            }
            {
                SDL_Rect src_rect = {520, 1504, 1, 32};
                SDL_Rect dst_rect = {left_panel_offset.x, right_panel_offset.y + int(button_size * 12.5) - button_size / 6, button_size * 5, button_size / 6};
                SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            }
        }
    }

    if (right_panel_mode == RIGHT_MENU_RULE_GEN)
    {
        {
            std::string t = translate("Rule Constructor");
            render_text_box(right_panel_offset + XYPos(0 * button_size, 0 * button_size), t);
        }


        if (render_lock(PROG_LOCK_NUMBER_TYPES, right_panel_offset + XYPos(0, button_size * 7.4), XYPos(button_size * 5, button_size * 5)))
        {
            render_box(right_panel_offset + XYPos(button_size * 0, button_size * 7.4), XYPos(button_size * 5, button_size * 2), button_size/4, 4);
            FOR_XY(pos, XYPos(), XYPos(5, 2))
            {
                RegionType r_type = select_region_type;
                if (r_type.type == RegionType::NONE || r_type.type == RegionType::SET || r_type.type == RegionType::VISIBILITY)
                    r_type.value = 0;
                XYPos bpos = right_panel_offset + pos * button_size + XYPos(0, button_size * 7.4);
                r_type.type = menu_region_types[pos.y][pos.x];
                if (r_type.type == RegionType::NONE)
                {
                    continue;
                }
                if (region_type == r_type)
                    render_box(bpos, XYPos(button_size, button_size), button_size/4, 10);
                render_region_type(r_type, bpos, button_size);
                SDL_Rect dst_rect = {bpos.x, bpos.y, button_size, button_size};
                add_clickable_highlight(dst_rect);
            }

            render_box(right_panel_offset + XYPos(button_size * 0, button_size * 9.6), XYPos(button_size * 5, button_size * 2), button_size/4, 4);
            FOR_XY(pos, XYPos(), XYPos(5, 2))
            {
                RegionType r_type = select_region_type;
                if (r_type.type == RegionType::NONE || r_type.type == RegionType::SET || r_type.type == RegionType::VISIBILITY)
                    r_type.type = RegionType::EQUAL;

                XYPos bpos = right_panel_offset + pos * button_size + XYPos(0, button_size * 9.6);
                r_type.value = pos.y * 5 + pos.x;
                if (region_type == r_type)
                    render_box(bpos, XYPos(button_size, button_size), button_size/4, 10);
                render_region_type(r_type, bpos, button_size);
                SDL_Rect dst_rect = {bpos.x, bpos.y, button_size, button_size};
                add_clickable_highlight(dst_rect);
            }

            if (game_mode != 3)
            {
                render_box(right_panel_offset + XYPos(button_size * 0, button_size * 11.8), XYPos(button_size * 5, button_size * 1), button_size/4, 4);
                for (int i = 0; i < 5; i++)
                {
                    XYPos bpos = right_panel_offset + XYPos(i * button_size, button_size * 11.8);
                    if (!render_lock(PROG_LOCK_VARS1 + i, bpos, XYPos(button_size, button_size)))
                        continue;
                    RegionType r_type = RegionType(RegionType::EQUAL, 0);
                    r_type.var = (1 << i);
                    if (select_region_type.var & (1 << i))
                        render_box(bpos, XYPos(button_size, button_size), button_size/4, 10);
                    render_region_type(r_type, bpos, button_size);

                    SDL_Rect dst_rect = {bpos.x, bpos.y, button_size, button_size};
                    add_clickable_highlight(dst_rect);
                }
            }

        }


        render_box(right_panel_offset + XYPos(button_size * 0, button_size * 6.2), XYPos(button_size * 5, button_size), button_size/4, 4);

        if (render_lock(PROG_LOCK_DONT_CARE, XYPos(right_panel_offset.x + 0 * button_size, left_panel_offset.y + 6.2 * button_size), XYPos(button_size, button_size)))
        {
            SDL_Rect src_rect = {896, 192, 192, 192};
            SDL_Rect dst_rect = {right_panel_offset.x + button_size * 0, right_panel_offset.y + int(button_size * 6.2), button_size, button_size};
            if (region_type.type == RegionType::NONE)
                render_box(XYPos(dst_rect.x, dst_rect.y), XYPos(button_size, button_size), button_size/4, 10);
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            add_tooltip(dst_rect, "Don't Care");
        }

        {
            SDL_Rect src_rect = {512, 192, 192, 192};
            SDL_Rect dst_rect = {right_panel_offset.x + button_size * 1, right_panel_offset.y + int(button_size * 6.2), button_size, button_size};
            if (region_type == RegionType(RegionType::SET, 0))
                render_box(XYPos(dst_rect.x, dst_rect.y), XYPos(button_size, button_size), button_size/4, 10);
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            add_tooltip(dst_rect, "Clear", region_type != RegionType(RegionType::SET, 0));
        }
        {
            SDL_Rect src_rect = {320, 192, 192, 192};
            SDL_Rect dst_rect = {right_panel_offset.x + button_size * 2, right_panel_offset.y + int(button_size * 6.2), button_size, button_size};
            if (region_type == RegionType(RegionType::SET, 1))
                render_box(XYPos(dst_rect.x, dst_rect.y), XYPos(button_size, button_size), button_size/4, 10);
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            add_tooltip(dst_rect, "Bomb", region_type != RegionType(RegionType::SET, 1));
        }

        if (render_lock(PROG_LOCK_VISIBILITY2, XYPos(right_panel_offset.x + 3 * button_size, left_panel_offset.y + 6.2 * button_size), XYPos(button_size, button_size)))
        {
            SDL_Rect src_rect = {896, 384, 192, 192};
            SDL_Rect dst_rect = {right_panel_offset.x + button_size * 3, right_panel_offset.y + int(button_size * 6.2), button_size, button_size};
            if (region_type == RegionType(RegionType::VISIBILITY, 1))
                render_box(XYPos(dst_rect.x, dst_rect.y), XYPos(button_size, button_size), button_size/4, 10);
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            add_tooltip(dst_rect, "Hidden", region_type != RegionType(RegionType::VISIBILITY, 1));
        }
        if (render_lock(PROG_LOCK_VISIBILITY3, XYPos(right_panel_offset.x + 4 * button_size, left_panel_offset.y + 6.2 * button_size), XYPos(button_size, button_size)))
        {
            SDL_Rect src_rect = {512, 768, 192, 192};
            SDL_Rect dst_rect = {right_panel_offset.x + button_size * 4, right_panel_offset.y + int(button_size * 6.2), button_size, button_size};
            if (region_type == RegionType(RegionType::VISIBILITY, 2))
                render_box(XYPos(dst_rect.x, dst_rect.y), XYPos(button_size, button_size), button_size/4, 10);
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            add_tooltip(dst_rect, "Trash", region_type != RegionType(RegionType::VISIBILITY, 2));
        }
    }

    // for (int i = 0; i < deaths; i++)
    // {
    //     SDL_Rect src_rect = {704, 192, 192, 192};
    //     SDL_Rect dst_rect = {left_panel_offset.x + (i % 4) * button_size, left_panel_offset.y + button_size * 8 + (i / 4) * button_size, button_size, button_size};
    //     SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
    // }

    if (right_panel_mode == RIGHT_MENU_REGION)
    {
        {
            std::string t = translate("Region Inspector");
            render_text_box(right_panel_offset + XYPos(0 * button_size, 0 * button_size), t);
        }

        {
            set_region_colour(sdl_texture, inspected_region->type.value, inspected_region->colour, contrast);
            render_box(right_panel_offset + XYPos(0 * button_size, 1 * button_size), XYPos(1 * button_size, 2 * button_size), button_size / 2, 8);
            render_region_bubble(inspected_region->type, inspected_region->colour, right_panel_offset + XYPos(0 * button_size, 1 * button_size), button_size * 2 / 3, hover_rulemaker_region_base_index == 0);
        }
        SDL_SetTextureColorMod(sdl_texture, contrast, contrast, contrast);
        if (hover_rulemaker)
            render_box(right_panel_offset + XYPos(0 * button_size, 2 * button_size), XYPos(button_size, button_size), button_size / 4);
        {
            // int count = inspected_region->elements.size();
            // SDL_Rect src_rect = {192 * count, 0, 192, 192};
            // SDL_Rect dst_rect = {right_panel_offset.x + button_size / 2 + button_size/8, right_panel_offset.y + 2 * button_size + button_size/8, button_size*6/8, button_size*6/8};
            // SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            //
            RegionType r_type(RegionType::EQUAL, (uint8_t) inspected_region->elements.count());
            render_region_type(r_type, right_panel_offset + XYPos(0, 2 * button_size), button_size);
        }
        render_button(XYPos( 192*3+128, 192*3), XYPos( right_panel_offset.x + button_size * 3, right_panel_offset.y + button_size), "Cancel");

        if (constructed_rule.region_count < (game_mode == 1 ? 3 : 4))
            render_button(XYPos( 1088, 576), XYPos( right_panel_offset.x + button_size * 4, right_panel_offset.y + 1 * button_size), "Add to Rule Constructor");

        if (inspected_region->gen_cause.rule)
            render_button(XYPos(704, 768), XYPos(right_panel_offset.x + button_size * 0, right_panel_offset.y + 4 * button_size), "Show Creation Rule");

        if (inspected_region->visibility_force == GridRegion::VIS_FORCE_USER)
            render_button(XYPos(1472, 768), XYPos(right_panel_offset.x + button_size * 1, right_panel_offset.y + 4 * button_size), "Visibility set by User");
        else if (inspected_region->visibility_force == GridRegion::VIS_FORCE_HINT)
            render_button(XYPos(1280, 1152), XYPos(right_panel_offset.x + button_size * 1, right_panel_offset.y + 4 * button_size), "Hint");
        else if (inspected_region->vis_cause.rule)
            render_button(XYPos(896, 768), XYPos(right_panel_offset.x + button_size * 1, right_panel_offset.y + 4 * button_size), "Show Visibility Rule");

        render_button(XYPos(1856, 576), XYPos(right_panel_offset.x + button_size * 0, right_panel_offset.y + 5 * button_size), "Change Colour");

        if (render_lock(PROG_LOCK_VISIBILITY4, XYPos(right_panel_offset.x + 3 * button_size, left_panel_offset.y + 3 * button_size), XYPos(button_size, button_size * 3)))
        {
            render_box(right_panel_offset + XYPos(button_size * 3, button_size * 3), XYPos(button_size, button_size * 3), button_size/4, 4);
            {
                SDL_Rect src_rect = {1088, 384, 192, 192};
                SDL_Rect dst_rect = {right_panel_offset.x + 3 * button_size, right_panel_offset.y + button_size * 3, button_size, button_size};
                if (inspected_region->vis_level == GRID_VIS_LEVEL_SHOW)
                    render_box(XYPos(dst_rect.x, dst_rect.y), XYPos(button_size, button_size), button_size/4, 10);
                SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                add_tooltip(dst_rect, "Visible", inspected_region->vis_level != GRID_VIS_LEVEL_SHOW);
            }
            {
                SDL_Rect src_rect = {896, 384, 192, 192};
                SDL_Rect dst_rect = {right_panel_offset.x + 3 * button_size, right_panel_offset.y + button_size * 4, button_size, button_size};
                if (inspected_region->vis_level == GRID_VIS_LEVEL_HIDE)
                    render_box(XYPos(dst_rect.x, dst_rect.y), XYPos(button_size, button_size), button_size/4, 10);
                SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                add_tooltip(dst_rect, "Hidden", inspected_region->vis_level != GRID_VIS_LEVEL_HIDE);
            }
            {
                SDL_Rect src_rect = {512, 768, 192, 192};
                SDL_Rect dst_rect = {right_panel_offset.x + 3 * button_size, right_panel_offset.y + button_size * 5, button_size, button_size};
                if (inspected_region->vis_level == GRID_VIS_LEVEL_BIN)
                    render_box(XYPos(dst_rect.x, dst_rect.y), XYPos(button_size, button_size), button_size/4, 10);
                SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                add_tooltip(dst_rect, "Trash", inspected_region->vis_level != GRID_VIS_LEVEL_BIN);
            }
        }
        if (render_lock(PROG_LOCK_PRIORITY2, right_panel_offset + XYPos(button_size * 0, button_size * 8), XYPos(button_size * 2, button_size)))
        {
            std::ostringstream out;
            out.setf(std::ios::fixed);
            out.precision(2);
            out << inspected_region->priority;
            std::string s = out.str();
            render_number_string(s, right_panel_offset + XYPos(button_size * 1 + button_size / 8, button_size * 8 + button_size / 8), XYPos(button_size * 7 / 4, button_size * 3 / 4));
            int p = std::round(inspected_region->priority);
            if (p < -2) p = -2;
            if (p >  2) p =  2;
            {
                SDL_Rect src_rect = {2432, 1344 + (2 - p) * 128, 128, 128};
                SDL_Rect dst_rect = {right_panel_offset.x + 0 * button_size, right_panel_offset.y + button_size * 8, button_size, button_size};
                SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                dst_rect.w = button_size * 2;
                add_tooltip(dst_rect, "Priority", false);
            }

        }
    }

    if (right_panel_mode != RIGHT_MENU_RULE_GEN)
    {
        {
            render_button(XYPos( 704, 1152), XYPos( right_panel_offset.x + button_size * 1, right_panel_offset.y + 6 * button_size), "Go to Rule Constructor");
            if (constructed_rule.region_count)
            {
                render_number(constructed_rule.region_count, XYPos(right_panel_offset.x + button_size * 1.6, right_panel_offset.y + 6.2 * button_size), XYPos(button_size * 0.3, button_size * 0.3));
            }
        }
    }
    if (((right_panel_mode == RIGHT_MENU_NONE) || (right_panel_mode == RIGHT_MENU_REGION)) &&  max_stars >= prog_stars[PROG_LOCK_LEVELS_AND_LOCKS])
    render_button(XYPos(1856, 1536), XYPos( right_panel_offset.x + button_size * 3, right_panel_offset.y + button_size * 6), "Copy Level to Clipboard");

    if (right_panel_mode == RIGHT_MENU_RULE_GEN || right_panel_mode == RIGHT_MENU_RULE_INSPECT)
    {
        GridRule* rule_ptr = NULL;
        if (right_panel_mode == RIGHT_MENU_RULE_GEN)
            rule_ptr = &constructed_rule;
        else
            rule_ptr = inspected_rule.rule;
        if (rule_ptr)
        {
            GridRule& rule = *rule_ptr;
            render_rule(rule, right_panel_offset + XYPos(0, button_size), button_size, hover_rulemaker_region_base_index);

            for (int i = 1; i < (1 << rule.region_count); i++)
            {
                int x = i & 3;
                if (x == 0)
                    x = 3;
                else if (x == 1)
                    x = 0;
                else if (x == 2)
                    x = 2;
                else if (x == 3)
                    x = 1;

                int y = i >> 2;
                y ^= y >> 1;
                y += 2;

                XYPos p = XYPos(x,y);

                if (hover_rulemaker && hover_rulemaker_bits == i)
                {
                    if (hover_rulemaker_lower_right && right_panel_mode == RIGHT_MENU_RULE_GEN)
                        SDL_SetTextureColorMod(sdl_texture, contrast / 2, contrast / 2, contrast / 2);
                    render_box(right_panel_offset + p * button_size, XYPos(button_size, button_size), button_size / 4, 9);
                    SDL_SetTextureColorMod(sdl_texture, contrast, contrast, contrast);
                    if (right_panel_mode == RIGHT_MENU_RULE_GEN)
                    {
                        if (!hover_rulemaker_lower_right)
                            SDL_SetTextureColorMod(sdl_texture, contrast / 2, contrast / 2, contrast / 2);
                        render_box(right_panel_offset + p * button_size + XYPos(button_size / 2, button_size / 2), XYPos(button_size / 2, button_size / 2), button_size / 4, 9);
                        SDL_SetTextureColorMod(sdl_texture, contrast, contrast, contrast);
                    }
                }
            }

            if (hover_rulemaker && right_panel_mode == RIGHT_MENU_RULE_GEN)
            {
                if (!hover_rulemaker_lower_right)
                {
                    XYPos p = mouse - XYPos(button_size + button_size / 4, button_size / 4);
                    RegionType new_region_type = region_type;
                    uint8_t square_counts[16];
                    GridRule::get_square_counts(square_counts, rule_gen_region[0], rule_gen_region[1], rule_gen_region[2], rule_gen_region[3]);
                    if (region_type.type >= 50)
                    {
                        new_region_type = RegionType(RegionType::NONE, 0);
                        // if ((rule.square_counts[hover_rulemaker_bits] == RegionType(RegionType::NONE, 0)) ||
                        //     (rule.square_counts[hover_rulemaker_bits] == RegionType(RegionType::EQUAL, 0) && square_counts[hover_rulemaker_bits]))
                        //     new_region_type = RegionType(RegionType::EQUAL, 0);
                    }
                    if (new_region_type == rule.square_counts[hover_rulemaker_bits])
                    {
                        new_region_type = RegionType(RegionType::EQUAL, square_counts[hover_rulemaker_bits]);
                    }
                    {
                        SDL_Rect src_rect = {256, 992, 128, 128};
                        SDL_Rect dst_rect = {p.x, p.y, button_size, button_size};
                        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                    }
                    {
                        SDL_Rect src_rect = {384, 992, 64, 64};
                        SDL_Rect dst_rect = {p.x + button_size * 3 / 4 , p.y, button_size / 2, button_size / 2};
                        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                    }

                    render_region_type(new_region_type, p, button_size);
                    mouse_cursor = SDL_SYSTEM_CURSOR_HAND;
                }
                else if (region_type.type != RegionType::VISIBILITY && region_type.type != RegionType::NONE)
                {
                    XYPos p = mouse - XYPos(button_size + button_size / 4, button_size / 4);

                    if (region_type.type > 50)
                    {
                        SDL_Rect src_rect = {256, 992, 128, 128};
                        SDL_Rect dst_rect = {p.x, p.y, button_size, button_size};
                        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                    }
                    {
                        SDL_Rect src_rect = {384, 992, 64, 64};
                        SDL_Rect dst_rect = {p.x + button_size * 3 / 4 , p.y, button_size / 2, button_size / 2};
                        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                    }
                    if (region_type.type < 50)
                    {
                        render_region_bubble(region_type, 0, p, button_size);
                    }
                    else
                    {
                        render_region_type(region_type, p, button_size);
                    }
                    mouse_cursor = SDL_SYSTEM_CURSOR_HAND;
                }
            }
            if (hover_rulemaker_region_base && right_panel_mode == RIGHT_MENU_RULE_GEN)
            {
                XYPos p = mouse - XYPos(button_size + button_size / 4, button_size / 4);
                if (region_type.type == RegionType::VISIBILITY)
                {
                    {
                        SDL_Rect src_rect = {384, 992, 64, 64};
                        SDL_Rect dst_rect = {p.x + button_size * 3 / 4 , p.y, button_size / 2, button_size / 2};
                        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                        render_region_type(region_type, p, button_size);
                        mouse_cursor = SDL_SYSTEM_CURSOR_HAND;
                    }
                }
                else if (region_type.type < 50)
                {
                    {
                        SDL_Rect src_rect = {384, 992, 64, 64};
                        SDL_Rect dst_rect = {p.x + button_size * 3 / 4 , p.y, button_size / 2, button_size / 2};
                        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                        render_region_bubble(region_type, 0, p, button_size);
                        mouse_cursor = SDL_SYSTEM_CURSOR_HAND;
                    }
                }
            }
        }

        if (right_panel_mode == RIGHT_MENU_RULE_GEN)
        {
              render_button(XYPos( 192*3+128, 192*3), XYPos( right_panel_offset.x + button_size * 3, right_panel_offset.y + button_size), "Cancel");

            if (constructed_rule.region_count >= 1)
            {
                if (constructed_rule_is_logical == GridRule::IMPOSSIBLE)
                {
                    render_button(XYPos(1856, 1152), XYPos(right_panel_offset.x + button_size * 4, right_panel_offset.y + button_size), "Impossible", 1);
                }
                else if (constructed_rule.apply_region_bitmap)
                {
                    if (constructed_rule_is_logical == GridRule::ILLOGICAL)
                    {
                        if (render_button(XYPos(896, 576), XYPos(right_panel_offset.x + button_size * 4, right_panel_offset.y + button_size), "Illogical", 1))
                        {
                            render_box(right_panel_offset + XYPos(-6 * button_size, 0), XYPos(6 * button_size, 6.5 * button_size), button_size/2, 1);
                            std::string t = translate("Why Illogical");
                            render_text_box(right_panel_offset + XYPos(-6 * button_size, 0 * button_size), t);
                            render_rule(rule_illogical_reason, right_panel_offset + XYPos(-5.5 * button_size, button_size), button_size, -1, true);
                        }
                    }
                    else if (constructed_rule_is_logical == GridRule::LOSES_DATA)
                    {
                        if (render_button(XYPos(896, 1152), XYPos(right_panel_offset.x + button_size * 4, right_panel_offset.y + button_size), "Loses Information", 2))
                        {
                            render_box(right_panel_offset + XYPos(-6 * button_size, 0), XYPos(6 * button_size, 6.5 * button_size), button_size/2, 1);
                            std::string t = translate("Why Loses Information");
                            render_text_box(right_panel_offset + XYPos(-6 * button_size, 0 * button_size), t);
                            render_rule(rule_illogical_reason, right_panel_offset + XYPos(-5.5 * button_size, button_size), button_size, -1, true);
                        }
                    }
                    else if (constructed_rule_is_logical == GridRule::UNBOUNDED)
                    {
                        render_button(XYPos(2048, 1344), XYPos(right_panel_offset.x + button_size * 4, right_panel_offset.y + button_size), "Unbounded", 1);
                    }
                    else if (constructed_rule_is_logical == GridRule::LIMIT)
                    {
                        render_button(XYPos(1856, 1344), XYPos(right_panel_offset.x + button_size * 4, right_panel_offset.y + button_size), "Rule Count Limit", 1);
                    }
                    else if (constructed_rule_is_already_present)
                    {
                        render_button(XYPos(1088, 768), XYPos(right_panel_offset.x + button_size * 4, right_panel_offset.y + button_size), "Rule Already Present", 1);
                    }
                    else if (replace_rule && !duplicate_rule)
                    {
                        render_button(XYPos(1088, 960), XYPos(right_panel_offset.x + button_size * 4, right_panel_offset.y + button_size), "Update Rule");
                    }
                    else
                    {
                        render_button(XYPos(704, 384), XYPos(right_panel_offset.x + button_size * 4, right_panel_offset.y + button_size), "OK");
                    }
                }
            }
            if (!constructed_rule_undo.empty())
                render_button(XYPos(1856, 768), XYPos(right_panel_offset.x + button_size * 3, right_panel_offset.y + button_size * 2), "Undo");
            if (!constructed_rule_redo.empty())
                render_button(XYPos(1856, 960), XYPos(right_panel_offset.x + button_size * 4, right_panel_offset.y + button_size * 2), "Redo");
        
        }
        if (right_panel_mode == RIGHT_MENU_RULE_INSPECT)
        {
            {
                std::string t = translate("Rule Inspector");
                render_text_box(right_panel_offset + XYPos(0 * button_size, 0 * button_size), t);
            }
            render_button(XYPos( 192*3+128, 192*3), XYPos( right_panel_offset.x + button_size * 3, right_panel_offset.y + button_size), "Cancel");
            if (!inspected_rule.rule->deleted)
                render_button(XYPos(1664, 960), XYPos( right_panel_offset.x + button_size * 4, right_panel_offset.y + button_size), "Remove Rule");
            render_button(XYPos(1280, 576), XYPos( right_panel_offset.x + button_size * 3, right_panel_offset.y + button_size * 2), "Edit Rule");
            render_button(XYPos(1472, 576), XYPos( right_panel_offset.x + button_size * 4, right_panel_offset.y + button_size * 2), "Duplicate Rule");
            if (max_stars >= prog_stars[PROG_LOCK_LEVELS_AND_LOCKS])
                render_button(XYPos(1856, 1536), XYPos( right_panel_offset.x + button_size * 3, right_panel_offset.y + button_size * 6), "Copy Rule to Clipboard");
            if (max_stars >= prog_stars[PROG_LOCK_LEVELS_AND_LOCKS] && selected_rules.size() == 1)
                render_button(XYPos(2048, 1728), XYPos( right_panel_offset.x + button_size * 4, right_panel_offset.y + button_size * 6), "Copy Rule to Clipboard Image");

            bool pri_consistant = true;
            bool pri_relevant = false;
            bool pri_paused = true;
            int priority;
            bool paused;

            assert(!selected_rules.empty());
            for (GridRule* rule : selected_rules)
            {
                if (rule->apply_region_type.type < RegionType::SET)
                {
                    if (!pri_relevant)
                    {
                        pri_relevant = true;
                        priority = rule->priority;
                        paused = rule->paused;
                    }
                    if (priority != rule->priority || paused != rule->paused)
                        pri_consistant = false;
                }
                if (!rule->paused)
                    pri_paused = false;
            }

            if (pri_relevant)
            {
                if (render_lock(PROG_LOCK_PRIORITY, right_panel_offset + XYPos(0, 7 * button_size), XYPos(button_size, 5 * button_size)))
                {
                    render_box(XYPos(right_panel_offset.x + button_size * 0, right_panel_offset.y + button_size * 6), XYPos(button_size, button_size * 6), button_size/4, 4);
                    {
                        SDL_Rect src_rect = {2240, 1344, 192, 192};
                        SDL_Rect dst_rect = {right_panel_offset.x + button_size * 0, right_panel_offset.y + button_size * 6, button_size, button_size};
                        if (!pri_paused && pri_consistant && priority == 2)
                            render_box(XYPos(dst_rect.x, dst_rect.y), XYPos(button_size, button_size), button_size/4, 10);
                        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                        add_tooltip(dst_rect, "Maximum Priority");
                    }
                    {
                        SDL_Rect src_rect = {2240, 1344 + 192, 192, 192};
                        SDL_Rect dst_rect = {right_panel_offset.x + button_size * 0, right_panel_offset.y + button_size * 7, button_size, button_size};
                        if (!pri_paused && pri_consistant && priority == 1)
                            render_box(XYPos(dst_rect.x, dst_rect.y), XYPos(button_size, button_size), button_size/4, 10);
                        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                        add_tooltip(dst_rect, "High Priority");
                    }
                    {
                        SDL_Rect src_rect = {2240, 1344 + 2 * 192, 192, 192};
                        SDL_Rect dst_rect = {right_panel_offset.x + button_size * 0, right_panel_offset.y + button_size * 8, button_size, button_size};
                        if (!pri_paused && pri_consistant && priority == 0)
                            render_box(XYPos(dst_rect.x, dst_rect.y), XYPos(button_size, button_size), button_size/4, 10);
                        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                        add_tooltip(dst_rect, "Medium Priority");
                    }
                    {
                        SDL_Rect src_rect = {2240, 1344 + 3 * 192, 192, 192};
                        SDL_Rect dst_rect = {right_panel_offset.x + button_size * 0, right_panel_offset.y + button_size * 9, button_size, button_size};
                        if (!pri_paused && pri_consistant && priority == -1)
                            render_box(XYPos(dst_rect.x, dst_rect.y), XYPos(button_size, button_size), button_size/4, 10);
                        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                        add_tooltip(dst_rect, "Low Priority");
                    }
                    {
                        SDL_Rect src_rect = {2240, 1344 + 4 * 192, 192, 192};
                        SDL_Rect dst_rect = {right_panel_offset.x + button_size * 0, right_panel_offset.y + button_size * 10, button_size, button_size};
                        if (!pri_paused && pri_consistant && priority == -2)
                            render_box(XYPos(dst_rect.x, dst_rect.y), XYPos(button_size, button_size), button_size/4, 10);
                        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                        add_tooltip(dst_rect, "Minimum Priority");
                    }
                }
            }
            if (render_lock(PROG_LOCK_PAUSE, right_panel_offset + XYPos(0, 11 * button_size), XYPos(button_size, button_size)))
            {
                if (!pri_relevant || prog_stars[PROG_LOCK_PRIORITY] > max_stars)
                    render_box(XYPos(right_panel_offset.x + button_size * 0, right_panel_offset.y + button_size * 11), XYPos(button_size, button_size), button_size/4, 4);
                SDL_Rect src_rect = {2240, 1344 + 5 * 192, 192, 192};
                SDL_Rect dst_rect = {right_panel_offset.x + button_size * 0, right_panel_offset.y + button_size * 11, button_size, button_size};
                if (pri_paused)
                    render_box(XYPos(dst_rect.x, dst_rect.y), XYPos(button_size, button_size), button_size/4, 10);
                SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                add_tooltip(dst_rect, "Paused");
            }
            if (inspected_rule.rule->apply_region_type.type != RegionType::VISIBILITY && (max_stars >= prog_stars[PROG_LOCK_TABLES]))
            {
                render_box(right_panel_offset + XYPos(button_size * 2, button_size * 10), XYPos(3 * button_size, button_size), button_size/4, 4);
                render_box(right_panel_offset + XYPos(button_size * 2, button_size * 11), XYPos(3 * button_size, button_size), button_size/4, 4);

                render_box(right_panel_offset + XYPos(button_size * 3, button_size * 9), XYPos(button_size, 3 * button_size), button_size/4, 0);
                render_box(right_panel_offset + XYPos(button_size * 4, button_size * 9), XYPos(button_size, 3 * button_size), button_size/4, 0);

                {
                    SDL_Rect src_rect = {1408, 2144, 192, 192};
                    SDL_Rect dst_rect = {right_panel_offset.x + button_size * 3, right_panel_offset.y + button_size * 9, button_size, button_size};
                    SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                    add_tooltip(dst_rect, "Level", false);
                }
                {
                    SDL_Rect src_rect = {1600, 2144, 192, 192};
                    SDL_Rect dst_rect = {right_panel_offset.x + button_size * 4, right_panel_offset.y + button_size * 9, button_size, button_size};
                    SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                    add_tooltip(dst_rect, "Global", false);
                }

                {
                    SDL_Rect src_rect = {704, 2336, 192, 192};
                    SDL_Rect dst_rect = {right_panel_offset.x + button_size * 2, right_panel_offset.y + button_size * 10, button_size, button_size};
                    SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                    add_tooltip(dst_rect, "Times Applied", false);
                }
                {
                    SDL_Rect src_rect = {512, 192, 192, 192};
                    SDL_Rect dst_rect = {right_panel_offset.x + button_size * 2, right_panel_offset.y + button_size * 11, button_size, button_size};
                    SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                    add_tooltip(dst_rect, "Cells Cleared", false);
                }

                render_number(grid->level_used_count[inspected_rule.rule], right_panel_offset + XYPos(button_size * 3 + button_size / 8, button_size * 10 + button_size / 4), XYPos(button_size * 3 / 4, button_size / 2));
                render_number(inspected_rule.rule->used_count, right_panel_offset + XYPos(button_size * 4 + button_size / 8, button_size * 10 + button_size / 4), XYPos(button_size * 3 / 4, button_size / 2));
                render_number(grid->level_clear_count[inspected_rule.rule], right_panel_offset + XYPos(button_size * 3 + button_size / 8, button_size * 11 + button_size / 4), XYPos(button_size * 3 / 4, button_size / 2));
                render_number(inspected_rule.rule->clear_count, right_panel_offset + XYPos(button_size * 4 + button_size / 8, button_size * 11 + button_size / 4), XYPos(button_size * 3 / 4, button_size / 2));
            }
        }
    }
    if (right_panel_mode == RIGHT_MENU_NONE)
    {
        {
            std::string t = translate("Current Level");
            render_text_box(right_panel_offset + XYPos(0 * button_size, 0 * button_size), t);
        }
        if (display_rules && !display_clipboard_rules)
        render_button(XYPos(1856, 1536), XYPos( right_panel_offset.x + button_size * 3, right_panel_offset.y + button_size * 6), "Copy All Rules to Clipboard");
        if (!last_deleted_rules[game_mode].empty())
        render_button(XYPos(1088, 1152), XYPos( right_panel_offset.x + button_size * 4, right_panel_offset.y + button_size), "Undelete Rules");
        if (!constructed_rule_undo.empty())
        render_button(XYPos(1856, 768), XYPos(right_panel_offset.x + button_size * 3, right_panel_offset.y + button_size * 2), "Undo");
        if (!constructed_rule_redo.empty())
        render_button(XYPos(1856, 960), XYPos(right_panel_offset.x + button_size * 4, right_panel_offset.y + button_size * 2), "Redo");

        if (render_lock(PROG_LOCK_REGION_LIMIT, XYPos(right_panel_offset.x + 0 * button_size, right_panel_offset.y + button_size * 2), XYPos(button_size, button_size * 4)))
        {
            {
                SDL_Rect src_rect = {3008, 576, 192, 576};
                SDL_Rect dst_rect = {right_panel_offset.x + 0 * button_size, right_panel_offset.y + button_size * 3, button_size, button_size * 3};
                SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                add_tooltip(dst_rect, "Auto-Solve Maximum Regions", false);

                src_rect = {2048, 1152, 192, 64};
                dst_rect = {right_panel_offset.x + 0 * button_size, right_panel_offset.y + button_size * 3 + int((1 - rule_limit_slider) * 2.6666 * button_size), button_size, button_size / 3};
                SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            }
            {
                SDL_Rect src_rect = {2624, 1536, 192, 192};
                SDL_Rect dst_rect = {right_panel_offset.x + 0 * button_size, right_panel_offset.y + button_size * 2, button_size, button_size};
                SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                add_tooltip(dst_rect, "Auto-Solve Maximum Regions", false);

                SDL_SetTextureColorMod(sdl_texture, 0,0,0);
                if (rule_limit_count >= 0)
                {
                    render_number(rule_limit_count, XYPos(right_panel_offset.x + 0.15 * button_size, right_panel_offset.y + button_size * 2.25), XYPos(button_size * 0.7, button_size/2));
                }
                else
                {
                    SDL_Rect src_rect = {2304, 384, 192, 192};
                    SDL_Rect dst_rect = {right_panel_offset.x + int(0.1 * button_size), right_panel_offset.y + int(button_size * 2.1), int(button_size * 0.8), int(button_size * 0.8)};
                    SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);

                }
                SDL_SetTextureColorMod(sdl_texture, contrast, contrast, contrast);
            }
        }
        if (render_lock(PROG_LOCK_ROBOTS, XYPos(right_panel_offset.x + 1 * button_size, right_panel_offset.y + button_size * 2), XYPos(button_size * 2, button_size * 2)))
        {
            int todo_count = 0;
            int done_count = 0;
            {
                for (unsigned g = 0; g < GLBAL_LEVEL_SETS + 1; g++)
                {
                    for (unsigned s = 0; s < level_progress[game_mode][g].size(); s++)
                    {
                        if (!level_is_accessible(game_mode, g, s))
                            continue;
                        for (unsigned i = 0; i < level_progress[game_mode][g][s].level_status.size(); i++)
                        {
                            if (!level_progress[game_mode][g][s].level_status[i].done)
                            {
                                if (run_robots && level_progress[game_mode][g][s].level_status[i].robot_done)
                                    done_count++;
                                else
                                    todo_count++;
                            }
                        }
                    }
                }
            }

            render_button(XYPos(should_run_robots ? 2816 : 3008, 1152), XYPos(right_panel_offset.x + button_size * 1, right_panel_offset.y + button_size * 2), "Robots", should_run_robots ? 0 : 1);
            render_box(right_panel_offset + XYPos(button_size * 1, button_size * 3), XYPos(2 * button_size, button_size), button_size/4, 4);
            render_box(right_panel_offset + XYPos(button_size * 1, button_size * 4), XYPos(2 * button_size, button_size), button_size/4, 4);
            {
                SDL_Rect src_rect = {1280, 2240, 96, 96};
                SDL_Rect dst_rect = {right_panel_offset.x + button_size * 1, right_panel_offset.y + button_size * 3, button_size, button_size};
                SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                dst_rect.w = 2 * button_size;
                add_tooltip(dst_rect, "Done", false);
            }
            {
                SDL_Rect src_rect = {3008, 1344, 192, 192};
                SDL_Rect dst_rect = {right_panel_offset.x + button_size * 1, right_panel_offset.y + button_size * 4, button_size, button_size};
                SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                dst_rect.w = 2 * button_size;
                add_tooltip(dst_rect, "To Do", false);
            }
            {
                render_number(done_count, XYPos(right_panel_offset.x + 2 * button_size, right_panel_offset.y + button_size * 3.25), XYPos(button_size * 0.9, button_size/2));
                render_number(todo_count, XYPos(right_panel_offset.x + 2 * button_size, right_panel_offset.y + button_size * 4.25), XYPos(button_size * 0.9, button_size/2));
            }

        }

    }
    {
        std::list<AnimationStarBurst>::iterator it = star_burst_animations.begin();
        while (it != star_burst_animations.end())
        {
            AnimationStarBurst& burst = *it;
            render_star_burst(burst.pos, burst.size, burst.progress, burst.lock);
            it++;
        }
        star_burst_animations.clear();
    }

    if (display_modes)
    {
        tooltip_string = "";
        tooltip_rect = XYRect(-1,-1,-1,-1);
        render_box(left_panel_offset + XYPos(button_size, button_size), XYPos(12 * button_size, (GAME_MODES + 2) * button_size), button_size/4, 1);
        for (int i = 0; i < GAME_MODES; i++)
        {
            static const char* mode_names[4] = {"Regular", "Three region rules", "Max 60 rules", "No variables, max 300 rules"};
            std::string name = mode_names[i];
            std::string tname = translate(name);
            {
                render_button(XYPos(2240, 576 + (i * 192)), XYPos(left_panel_offset.x + 2 * button_size, left_panel_offset.y + button_size * (2 + i)), name.c_str());

            }

            if (i == game_mode)
                SDL_SetTextureColorMod(sdl_texture, 0, contrast, 0);
            render_text_box(left_panel_offset + XYPos(button_size * 3, int(button_size * (2.14 + i))), tname);
            SDL_SetTextureColorMod(sdl_texture, contrast, contrast, contrast);
        }
    }

    if (display_help)
    {
        tooltip_string = "";
        tooltip_rect = XYRect(-1,-1,-1,-1);
        int sq_size = panel_size.y / 9;
        XYPos help_image_size = XYPos(right_panel_offset.x + panel_size.x - left_panel_offset.x, panel_size.y);
        XYPos help_image_offset = left_panel_offset;

        {
            SDL_Rect src_rect = {0, 0, 1920, 1080};
            SDL_Rect dst_rect = {help_image_offset.x, help_image_offset.y, help_image_size.x, help_image_size.y};
            SDL_RenderCopy(sdl_renderer, tutorial_texture[tutorial_index], &src_rect, &dst_rect);
            SaveObjectMap* lang = lang_data->get_item(language)->get_map();
            SaveObjectList* tutorial = lang->get_item("tutorial")->get_list();
            SaveObjectList* tutorial_page = tutorial->get_item(tutorial_index)->get_list();
            for (int i = 0; i < (int)tutorial_page->get_count(); i++)
            {
                SaveObjectMap* text_box = tutorial_page->get_item(i)->get_map();
                XYPos p;
                p.x = text_box->get_num("x");
                p.y = text_box->get_num("y");
                std::string text = text_box->get_string("text");
                bool left = text_box->has_key("left");
                p.x = p.x * help_image_size.x / 1920;
                p.y = p.y * help_image_size.y / 1080;
                render_text_box(help_image_offset + p, text, left);
            }
        }
        {
            if (tutorial_index)
                render_button(XYPos(704, 1344), XYPos(help_image_offset.x + help_image_size.x - sq_size * 3, help_image_offset.y + help_image_size.y - sq_size), "Previous Page", 0, sq_size);

            if (!walkthrough || (tutorial_index == (tut_page_count - 1)))
                render_button(XYPos(704 + 192, 1344), XYPos(help_image_offset.x + help_image_size.x - sq_size * 2, help_image_offset.y + help_image_size.y - sq_size), "OK", 0, sq_size);

            if (tutorial_index < (tut_page_count - 1))
                render_button(XYPos(704 + 192 * 2, 1344), XYPos(help_image_offset.x + help_image_size.x - sq_size * 1, help_image_offset.y + help_image_size.y - sq_size), "Next Page", 0, sq_size);
        }
    }
    if (prog_seen[PROG_LOCK_REGION_HINT] < PROG_ANIM_MAX)
    {
        if (prog_stars[PROG_LOCK_REGION_HINT] <= max_stars)
        {
            if (prog_seen[PROG_LOCK_REGION_HINT] == 0)
            {
                bool seen = false;
                for (GridRule& rule : rules[game_mode])
                {
                    if (rule.apply_region_type.type != RegionType::SET)
                        seen = true;
                }
                if (!seen)
                {
                    display_help = true;
                    tutorial_index = 4;
                }
                else
                {
                    prog_seen[PROG_LOCK_REGION_HINT] = PROG_ANIM_MAX;
                }
            }
            if (display_help)
                render_star_burst(right_panel_offset + XYPos(-button_size * 7.6, button_size * 1) , XYPos(button_size * 7, button_size * 11.5), prog_seen[PROG_LOCK_REGION_HINT], false);
            prog_seen[PROG_LOCK_REGION_HINT] += frame_step;
        }
    }
    if (prog_seen[PROG_LOCK_USE_DONT_CARE] < PROG_ANIM_MAX)
    {
        if (prog_stars[PROG_LOCK_USE_DONT_CARE] <= max_stars)
        {
            if (prog_seen[PROG_LOCK_USE_DONT_CARE] == 0)
            {
                bool seen = false;
                for (GridRule& rule : rules[game_mode])
                {
                    if (rule.square_counts[1].type == RegionType::NONE)
                        seen = true;
                }
                if (!seen)
                {
                    display_help = true;
                    tutorial_index = 3;
                }
                else
                {
                    prog_seen[PROG_LOCK_USE_DONT_CARE] = PROG_ANIM_MAX;
                }

            }
            if (display_help)
                render_star_burst(left_panel_offset + XYPos(button_size * 1, button_size * 1) , XYPos(button_size * 5, button_size * 8), prog_seen[PROG_LOCK_USE_DONT_CARE], false);
            prog_seen[PROG_LOCK_USE_DONT_CARE] += frame_step;
        }
    }
    if (prog_seen[PROG_LOCK_DOUBLE_CLICK_HINT] < PROG_ANIM_MAX)
    {
        if (prog_stars[PROG_LOCK_DOUBLE_CLICK_HINT] <= max_stars)
        {
            if (prog_seen[PROG_LOCK_DOUBLE_CLICK_HINT] == 0)
            {
                if (!seen_ff)
                {
                    display_help = true;
                    tutorial_index = 5;
                }
                else
                {
                    prog_seen[PROG_LOCK_DOUBLE_CLICK_HINT] = PROG_ANIM_MAX;
                }
            }
            if (display_help)
                render_star_burst(right_panel_offset + XYPos(button_size * 0, button_size * 1) , XYPos(button_size * 5, button_size * 4), prog_seen[PROG_LOCK_DOUBLE_CLICK_HINT], false);
            prog_seen[PROG_LOCK_DOUBLE_CLICK_HINT] += frame_step;
        }
    }
    if (display_menu)
    {
        {
            SDL_SetTextureAlphaMod(sdl_texture, 200);
            SDL_Rect src_rect = {15, 426, 1, 1};
            SDL_Rect dst_rect = {0, 0, window_size.x, window_size.y};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            SDL_SetTextureAlphaMod(sdl_texture, 255);
        }
        tooltip_string = "";
        tooltip_rect = XYRect(-1,-1,-1,-1);
        render_box(left_panel_offset + XYPos(button_size, button_size), XYPos(16 * button_size, 11.8 * button_size), button_size/4, 1);
        {
            SDL_Rect src_rect = {full_screen ? 1472 : 1664, 1152, 192, 192};
            SDL_Rect dst_rect = {left_panel_offset.x + 2 * button_size, left_panel_offset.y + button_size * 2, button_size, button_size};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            dst_rect.w = 8.84 * button_size;
            add_clickable_highlight(dst_rect);
            std::string t = translate("Full Screen");
            render_text_box(left_panel_offset + XYPos(3.2 * button_size, 2.14 * button_size), t, false, 7.5 * button_size);
        }
        {
            SDL_Rect src_rect = {1280, 1344, 192, 192};
            SDL_Rect dst_rect = {left_panel_offset.x + 2 * button_size, left_panel_offset.y + button_size * 3, button_size, button_size};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            dst_rect.w = 8.84 * button_size;
            add_clickable_highlight(dst_rect);
            std::string t = translate("Select Language");
            render_text_box(left_panel_offset + XYPos(3.2 * button_size, 3.14 * button_size), t, false, 7.5 * button_size);
        }
        {
            SDL_Rect src_rect = {1664, show_row_clues ? 576 : 768, 192, 192};
            SDL_Rect dst_rect = {left_panel_offset.x + 2 * button_size, left_panel_offset.y + button_size * 4, button_size, button_size};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            dst_rect.w = 8.84 * button_size;
            add_clickable_highlight(dst_rect);
            std::string t = translate("Show Row Clues");
            render_text_box(left_panel_offset + XYPos(3.2 * button_size, 4.14 * button_size), t, false, 7.5 * button_size);
        }
        {
            SDL_Rect src_rect = {low_contrast ? 1856 : 2048, 1920, 192, 192};
            SDL_Rect dst_rect = {left_panel_offset.x + 2 * button_size, left_panel_offset.y + button_size * 5, button_size, button_size};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            dst_rect.w = 8.84 * button_size;
            add_clickable_highlight(dst_rect);
            std::string t = translate("Low Contrast");
            render_text_box(left_panel_offset + XYPos(3.2 * button_size, 5.14 * button_size), t, false, 7.5 * button_size);
        }
        {
            SDL_Rect src_rect = {2432, 1152, 192, 192};
            SDL_Rect dst_rect = {left_panel_offset.x + 2 * button_size, left_panel_offset.y + button_size * 6, button_size, button_size};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            dst_rect.w = 8.84 * button_size;
            add_clickable_highlight(dst_rect);
            std::string t = translate("Remap Keys");
            render_text_box(left_panel_offset + XYPos(3.2 * button_size, 6.14 * button_size), t, false, 7.5 * button_size);
        }
        {
            SDL_Rect src_rect = {2624, 769, 192, 192};
            SDL_Rect dst_rect = {left_panel_offset.x + 2 * button_size, left_panel_offset.y + button_size * 7, button_size, button_size};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            dst_rect.w = 8.84 * button_size;
            add_clickable_highlight(dst_rect);
            std::string t = translate("About");
            render_text_box(left_panel_offset + XYPos(3.2 * button_size, 7.14 * button_size), t, false, 7.5 * button_size);
        }
        {
            SDL_Rect src_rect = {2624, 1728, 192, 192};
            SDL_Rect dst_rect = {left_panel_offset.x + 2 * button_size, left_panel_offset.y + button_size * 8, button_size, button_size};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            dst_rect.w = 8.84 * button_size;
            add_clickable_highlight(dst_rect);
            std::string t = translate("Join Discord");
            render_text_box(left_panel_offset + XYPos(3.2 * button_size, 8.14 * button_size), t, false, 7.5 * button_size);
        }
        {
            SDL_Rect src_rect = {2048, 576, 192, 576};
            SDL_Rect dst_rect = {left_panel_offset.x + 11 * button_size, left_panel_offset.y + button_size * 3, button_size, button_size * 3};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            add_tooltip(dst_rect, "Volume", false);

            src_rect = {2048, 1152, 192, 64};
            dst_rect = {left_panel_offset.x + 11 * button_size, left_panel_offset.y + button_size * 3 + int((1 - volume) * 2.6666 * button_size), button_size, button_size / 3};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
        }
        {
            SDL_Rect src_rect = {2624, 960, 192, 192};
            SDL_Rect dst_rect = {left_panel_offset.x + 11 * button_size, left_panel_offset.y + button_size * 2, button_size, button_size};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
        }
        {
            SDL_Rect src_rect = {2048, 576, 192, 576};
            SDL_Rect dst_rect = {left_panel_offset.x + 13 * button_size, left_panel_offset.y + button_size * 3, button_size, button_size * 3};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            add_tooltip(dst_rect, "Music Volume", false);

            src_rect = {2048, 1152, 192, 64};
            dst_rect = {left_panel_offset.x + 13 * button_size, left_panel_offset.y + button_size * 3 + int((1 - music_volume) * 2.6666 * button_size), button_size, button_size / 3};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
        }
        {
            SDL_Rect src_rect = {2624, 1152, 192, 192};
            SDL_Rect dst_rect = {left_panel_offset.x + 13 * button_size, left_panel_offset.y + button_size * 2, button_size, button_size};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
        }

        if (render_lock(PROG_LOCK_COLORS, XYPos(left_panel_offset.x + 15 * button_size, left_panel_offset.y + button_size * 3), XYPos(button_size, button_size * 3)))
        {
            {
                SDL_Rect src_rect = {2816, 576, 192, 576};
                SDL_Rect dst_rect = {left_panel_offset.x + 15 * button_size, left_panel_offset.y + button_size * 3, button_size, button_size * 3};
                SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                add_tooltip(dst_rect, "Colors", false);

                src_rect = {2048, 1152, 192, 64};
                dst_rect = {left_panel_offset.x + 15 * button_size, left_panel_offset.y + button_size * 3 + int((1 - colors) * 2.6666 * button_size), button_size, button_size / 3};
                SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            }
            {
                SDL_Rect src_rect = {2624, 1344, 192, 192};
                SDL_Rect dst_rect = {left_panel_offset.x + 15 * button_size, left_panel_offset.y + button_size * 2, button_size, button_size};
                SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            }
        }

        {
            SDL_SetTextureColorMod(sdl_texture, contrast, 0, 0);
            SDL_Rect src_rect = {1664, 960, 192, 192};
            SDL_Rect dst_rect = {left_panel_offset.x + 2 * button_size, left_panel_offset.y + button_size * 9, button_size, button_size};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            dst_rect.w = 8.84 * button_size;
            add_clickable_highlight(dst_rect);
            std::string t = translate("Reset Levels");
            render_text_box(left_panel_offset + XYPos(3.2 * button_size, 9.14 * button_size), t, false, 7.5 * button_size);
        }
        {
            SDL_Rect src_rect = {1664, 960, 192, 192};
            SDL_Rect dst_rect = {left_panel_offset.x + 2 * button_size, left_panel_offset.y + button_size * 10, button_size, button_size};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            dst_rect.w = 8.84 * button_size;
            add_clickable_highlight(dst_rect);
            std::string t = translate("Reset Rules");
            render_text_box(left_panel_offset + XYPos(3.2 * button_size, 10.14 * button_size), t, false, 7.5 * button_size);
            SDL_SetTextureColorMod(sdl_texture, contrast, contrast, contrast);
        }
        {
            SDL_Rect src_rect = {1280, 1728, 192, 192};
            SDL_Rect dst_rect = {left_panel_offset.x + 2 * button_size, left_panel_offset.y + button_size * 11, button_size, button_size};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            dst_rect.w = 8.84 * button_size;
            add_clickable_highlight(dst_rect);
            std::string t = translate("Quit");
            render_text_box(left_panel_offset + XYPos(3.2 * button_size, 11.14 * button_size), t, false, 7.5 * button_size);
        }
        render_button(XYPos(704, 384), XYPos(left_panel_offset.x + 15 * button_size, left_panel_offset.y + button_size * 11), "OK");
    }
    if (display_reset_confirm)
    {
        {
            SDL_SetTextureAlphaMod(sdl_texture, 200);
            SDL_Rect src_rect = {15, 426, 1, 1};
            SDL_Rect dst_rect = {0, 0, window_size.x, window_size.y};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            SDL_SetTextureAlphaMod(sdl_texture, 255);
        }
        tooltip_string = "";
        tooltip_rect = XYRect(-1,-1,-1,-1);
        render_box(left_panel_offset + XYPos(button_size, button_size), XYPos(10 * button_size, 10 * button_size), button_size/4, 1);
        {
            std::string t = display_reset_confirm_levels_only ? translate("Reset Levels") : translate("Reset Rules");
            render_text_box(left_panel_offset + XYPos(4 * button_size, 3 * button_size), t);
            render_button(XYPos(704, 384), XYPos(left_panel_offset.x + 3 * button_size, left_panel_offset.y + button_size * 6), "OK");
            render_button(XYPos( 704, 576), XYPos(left_panel_offset.x + 7 * button_size, left_panel_offset.y + button_size * 6), "Cancel");

        }
    }

    if (display_key_select)
    {
        {
            SDL_SetTextureAlphaMod(sdl_texture, 200);
            SDL_Rect src_rect = {15, 426, 1, 1};
            SDL_Rect dst_rect = {0, 0, window_size.x, window_size.y};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            SDL_SetTextureAlphaMod(sdl_texture, 255);
        }
        tooltip_string = "";
        tooltip_rect = XYRect(-1,-1,-1,-1);
        render_box(left_panel_offset + XYPos(button_size, button_size), XYPos(10 * button_size, 10.5 * button_size), button_size/4, 1);
        {
            std::string t = translate("Remap Keys");
            render_text_box(left_panel_offset + XYPos(1.5 * button_size, 1.5 * button_size), t);
            if (key_remap_page_index == 0)
            {
                {
                    SDL_Rect src_rect = {704, 960, 192, 192};
                    SDL_Rect dst_rect = {left_panel_offset.x + 2 * button_size, left_panel_offset.y + button_size * 3, button_size, button_size};
                    SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                    dst_rect.w = 3.9 * button_size;
                    add_tooltip(dst_rect, "Help");
                }
                {
                    SDL_Rect src_rect = {1472, 960, 192, 192};
                    SDL_Rect dst_rect = {left_panel_offset.x + 2 * button_size, left_panel_offset.y + button_size * 4, button_size, button_size};
                    SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                    dst_rect.w = 3.9 * button_size;
                    add_tooltip(dst_rect, "Hint");
                }
                {
                    SDL_Rect src_rect = {704 + 192 * 3, 960, 192, 192};
                    SDL_Rect dst_rect = {left_panel_offset.x + 2 * button_size, left_panel_offset.y + button_size * 5, button_size, button_size};
                    SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                    dst_rect.w = 3.9 * button_size;
                    add_tooltip(dst_rect, "Next Level");
                }
                {
                    SDL_Rect src_rect = {704 + 192 * 2, 960, 192, 192};
                    SDL_Rect dst_rect = {left_panel_offset.x + 2 * button_size, left_panel_offset.y + button_size * 6, button_size, button_size};
                    SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                    dst_rect.w = 3.9 * button_size;
                    add_tooltip(dst_rect, "Refresh Regions");
                }
                {
                    SDL_Rect src_rect = {1472, 1152, 192, 192};
                    SDL_Rect dst_rect = {left_panel_offset.x + 2 * button_size, left_panel_offset.y + button_size * 7, button_size, button_size};
                    SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                    dst_rect.w = 3.9 * button_size;
                    add_tooltip(dst_rect, "Full Screen");
                }
                {
                    SDL_Rect src_rect = {2432, 576, 192, 192};
                    SDL_Rect dst_rect = {left_panel_offset.x + 6 * button_size, left_panel_offset.y + button_size * 3, button_size, button_size};
                    SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                    dst_rect.w = 3.9 * button_size;
                    add_tooltip(dst_rect, "Visible");
                }
                {
                    SDL_Rect src_rect = {2432, 576 + 192, 192, 192};
                    SDL_Rect dst_rect = {left_panel_offset.x + 6 * button_size, left_panel_offset.y + button_size * 4, button_size, button_size};
                    SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                    dst_rect.w = 3.9 * button_size;
                    add_tooltip(dst_rect, "Hidden");
                }
                {
                    SDL_Rect src_rect = {2432, 576 + 192 * 2, 192, 192};
                    SDL_Rect dst_rect = {left_panel_offset.x + 6 * button_size, left_panel_offset.y + button_size * 5, button_size, button_size};
                    SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                    dst_rect.w = 3.9 * button_size;
                    add_tooltip(dst_rect, "Trash");
                }
                {
                    SDL_Rect src_rect = {1856, 768, 192, 192};
                    SDL_Rect dst_rect = {left_panel_offset.x + 6 * button_size, left_panel_offset.y + button_size * 6, button_size, button_size};
                    SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                    dst_rect.w = 3.9 * button_size;
                    add_tooltip(dst_rect, "Undo");
                }
                {
                    SDL_Rect src_rect = {1856, 960, 192, 192};
                    SDL_Rect dst_rect = {left_panel_offset.x + 6 * button_size, left_panel_offset.y + button_size * 7, button_size, button_size};
                    SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                    dst_rect.w = 3.9 * button_size;
                    add_tooltip(dst_rect, "Redo");
                }
            }
            else
            {
                for (int i = 0; i < 10; i++)
                {
                    RegionType reg = RegionType(RegionType::EQUAL, 0);
                    const char* tip = NULL;
                    if (key_remap_page_index == 1)
                    {
                        switch (i)
                        {
                            case 0:
                                reg = RegionType(RegionType::NONE, 0);
                                tip = "Don't Care";
                                break;
                            case 1:
                                reg = RegionType(RegionType::SET, 0);
                                tip = "Clear";
                                break;
                            case 2:
                                reg = RegionType(RegionType::SET, 1);
                                tip = "Bomb";
                                break;
                            case 3:
                                reg = RegionType(RegionType::VISIBILITY, 1);
                                tip = "Hidden";
                                break;
                            case 4:
                                reg = RegionType(RegionType::VISIBILITY, 2);
                                tip = "Trash";
                                break;
                            default:
                                reg.var = 1 << (i - 5);
                                break;
                        }
                    }
                    if (key_remap_page_index == 2)
                        reg = RegionType(RegionType::EQUAL, i);
                    if (key_remap_page_index == 3)
                    {
                        static const RegionType::Type types[10] = {RegionType::EQUAL, RegionType::NOTEQUAL, RegionType::MORE, RegionType::LESS, RegionType::XOR3, RegionType::XOR2, RegionType::XOR22, RegionType::PARITY, RegionType::XOR1, RegionType::XOR11};
                        reg.type = types[i];
                    }
                    SDL_Rect src_rect = {2624, 576, 192, 192};
                    SDL_Rect dst_rect = {left_panel_offset.x + (2 + (i / 5) * 4) * button_size, left_panel_offset.y + button_size * (3 + i % 5) , button_size, button_size};
                    SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                    render_region_type(reg, left_panel_offset + XYPos((2 + (i / 5) * 4) * button_size, button_size * (3 + i % 5)), button_size);
                    dst_rect.w = 3.9 * button_size;
                    if (tip)
                        add_tooltip(dst_rect, tip);
                    else
                        add_clickable_highlight(dst_rect);
                }
            }

            for (int i = 0; i < 10; i++)
            {
                int a = key_remap_page_index * 10 + i;
                std::string s = SDL_GetKeyName(key_codes[a]);
                if (a == capturing_key)
                    s = "[?]";
                
                render_text_box(left_panel_offset + XYPos((3.2 + (i / 5) * 4) * button_size, (3.14 + (i % 5)) * button_size), s, false, button_size * 2.5);
            }


            if (key_remap_page_index > 0)
                render_button(XYPos(704, 1344), XYPos(left_panel_offset.x + 4 * button_size, left_panel_offset.y + button_size * 9), "Previous Page");

            if (key_remap_page_index < 3)
                render_button(XYPos(1088, 1344), XYPos(left_panel_offset.x + 5 * button_size, left_panel_offset.y + button_size * 9), "Next Page");

            render_button(XYPos(704, 384), XYPos(left_panel_offset.x + 9 * button_size, left_panel_offset.y + button_size * 9), "OK");


        }
    }

    if (display_about)
    {
        tooltip_string = "";
        tooltip_rect = XYRect(-1,-1,-1,-1);
        std::string s = "Bombe version:" + version_text;
        s +=            "\nGame by Charlie Brej\n\nMusic by Amurich\n\nThank you to\n"
                        "All the players and testers especially: 3^3=7, AndyY, artless, Autoskip,\n"
                        "baltazar, bearb, Detros, Elgel, Fadaja, GuiltyBystander, Host,\n"
                        "hyperphysin, icely, Leaving Leaves, Mage6019, Miri Mayhem, Nif, notgreat,\n"
                        "npinsker, Nyphakosi, Olie, Orio Prisco, Orioo, Phos/Nyaki, piepie62,\n"
                        "rolamni, romain22222, Sinom, Snoresville, Skyhawk, ThunderClawShocktrix,\n"
                        "transcendental guy, Tsumiki Miniwa, vpumeyyv, yuval keshet\n    and many many others";
        render_text_box(left_panel_offset + XYPos(button_size * 2, button_size * 2), s);
    }

    if (display_language_chooser)
    {
        {
            SDL_SetTextureAlphaMod(sdl_texture, 200);
            SDL_Rect src_rect = {15, 426, 1, 1};
            SDL_Rect dst_rect = {0, 0, window_size.x, window_size.y};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            SDL_SetTextureAlphaMod(sdl_texture, 255);
        }
        tooltip_string = "";
        tooltip_rect = XYRect(-1,-1,-1,-1);
        render_box(left_panel_offset + XYPos(button_size, button_size), XYPos(9 * button_size, 10 * button_size), button_size/4, 1);
        int index = 0;
        std::string orig_lang = language;
        for (std::map<std::string, SaveObject*>::iterator it = lang_data->omap.begin(); it != lang_data->omap.end(); ++it)
        {
            std::string s = it->first;
            set_language(s);
            if (s == orig_lang)
                SDL_SetTextureColorMod(sdl_texture, 0, contrast, 0);
            XYPos p(index / 8, index % 8);
            render_text_box(left_panel_offset + XYPos(button_size * (2 + p.x * 4), button_size * (2 + p.y)), s);
            SDL_SetTextureColorMod(sdl_texture, contrast, contrast, contrast);
            index++;
        }
        set_language(orig_lang);
    }

    render_tooltip();
    if (pirate)
    {
        XYPos pos = grid_offset + XYPos(grid_size / 2, grid_size / 2);
        XYPos siz = XYPos(grid_size / 8, grid_size / 8);
        pos -= siz / 2;
        XYPosFloat cyc(Angle(float(frame) / 1000), grid_size / 3);
        pos += cyc;
        SDL_Rect src_rect = {2112, 384, 192, 192};
        SDL_Rect dst_rect = {pos.x, pos.y, siz.x , siz.y};
        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
    }

    if (walkthrough && !display_help)
    {
        walkthrough_double_click = false;
        if (walkthrough_step == 0)
        {
            walkthrough_region = XYRect(grid_offset + XYPos(button_size * 0.93, button_size * 3.65), XYPos(button_size * 1, button_size  * 1));
            walkthrough_double_click = true;
            region_type = RegionType(RegionType::NONE, 0);
        }
        else if (walkthrough_step == 1)
        {
            walkthrough_region = XYRect(right_panel_offset + XYPos(button_size * 1, button_size * 6.2), XYPos(button_size * 1, button_size  * 1));
        }
        else if (walkthrough_step == 2)
        {
            walkthrough_region = XYRect(right_panel_offset + XYPos(button_size * 0.5, button_size * 2.5), XYPos(button_size / 2, button_size  / 2));
        }
        else if (walkthrough_step == 3)
        {
            walkthrough_region = XYRect(right_panel_offset + XYPos(button_size * 4, button_size * 1), XYPos(button_size, button_size));
        }
        {
            SDL_Rect src_rect = {2624, 1920, 192, 192};
            SDL_Rect dst_rect = {walkthrough_region.pos.x - walkthrough_region.size.x * 3, walkthrough_region.pos.y - walkthrough_region.size.y * 3, walkthrough_region.size.x * 7, walkthrough_region.size.y * 7};
            SDL_Point rot_center = {dst_rect.w / 2, dst_rect.h / 2};
            SDL_RenderCopyEx(sdl_renderer, sdl_texture, &src_rect, &dst_rect, double(frame) / 10, &rot_center, SDL_FLIP_NONE);

            XYPos p = mouse - walkthrough_region.pos - walkthrough_region.size / 2;
            if (XYPosFloat(p).distance()  < XYPosFloat(walkthrough_region.size).distance() / 2)
            {
                src_rect = {2624, walkthrough_double_click ? 2112 + 192 : 2112, 192, 192};
                dst_rect = {walkthrough_region.pos.x, walkthrough_region.pos.y + walkthrough_region.size.y, walkthrough_region.size.x, walkthrough_region.size.y};
                SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            }
        }

    }

    if (display_debug)
    {
        static int debug_region_index = 0;
        static unsigned times[10] = {};
        static unsigned regions[10] = {};

        unsigned regs = grid->regions.size();
        int old_time = times[debug_region_index];

        double rate = double(regs - regions[debug_region_index]) / double(frame - old_time);
        times[debug_region_index] = frame;
        regions[debug_region_index] = regs;
        debug_region_index = (debug_region_index + 1) % 10;


        

        std::string debug_text = "DEBUG:\n";
        debug_text += "regions size: " + std::to_string(grid->regions.size()) + "\n";
        debug_text += "regions_to_add size: " + std::to_string(grid->regions_to_add.size()) + "\n";
        debug_text += "deleted_regions size: " + std::to_string(grid->deleted_regions.size()) + "\n";
        debug_text += "Regions per sec: " + std::to_string(rate * 1000) + "\n";
        debug_text += "FPS: " + std::to_string(10000.0 / (frame - old_time)) + "\n";
        debug_text += "DEBUG_BITS: ";
        for (int i = 0; i < 10; i++)
            debug_text += debug_bits[i] ? '1' : '0';
        debug_text += "\n";
        render_text_box(XYPos(0, grid_size/2), debug_text);
    }

    SDL_RenderPresent(sdl_renderer);
}

void GameState::grid_click(XYPos pos, int clicks, int btn)
{
    if (display_scores || display_rules || display_levels)
    {
        display_rules_click = true;
        display_rules_click_drag = true;
        display_rules_click_pos = mouse;
    }
    else if (mouse_mode == MOUSE_MODE_FILTER)
    {
        XYPos gpos = grid->get_square_from_mouse_pos(pos - grid_offset - scaled_grid_offset, grid_pitch);
        if (gpos.x >= 0)
            filter_pos.flip(gpos);
    }
    else
    {
        if (mouse_hover_region)
        {
            {
                if ((!key_held) && (clicks >= 2))
                {
                    right_panel_mode = RIGHT_MENU_RULE_GEN;
                    if (constructed_rule.region_count < (game_mode == 1 ? 3 : 4))
                    {
                        bool found = false;
                        for (int i = 0; i < constructed_rule.region_count; i++)
                        {
                            if (rule_gen_region[i] == mouse_hover_region)
                                found = true;
                        }
                        for (int i = constructed_rule.region_count; i < 4; i++)
                        {
                            assert(!rule_gen_region[i]);
                        }
                        if (!found)
                        {
                            update_constructed_rule_pre();
                            rule_gen_region[constructed_rule.region_count] = mouse_hover_region;
                            constructed_rule.import_rule_gen_regions(rule_gen_region[0], rule_gen_region[1], rule_gen_region[2], rule_gen_region[3]);
                            update_constructed_rule();
                        }
                    }
                }
                else
                {
                    right_panel_mode = RIGHT_MENU_REGION;
                    inspected_region = mouse_hover_region;
                }
            }
        }
        else
        {
            XYPos gpos = grid->get_square_from_mouse_pos(pos - grid_offset - scaled_grid_offset, grid_pitch);
            if (gpos.x >= 0)
            {
                gpos = grid->get_base_square(gpos);
                if (grid->cell_causes.count(gpos))
                {
                    right_panel_mode = RIGHT_MENU_RULE_INSPECT;
                    inspected_rule = grid->cell_causes[gpos];
                    selected_rules.insert(inspected_rule.rule);
                }
            }
        }
        grid_dragging = true;
        grid_dragging_btn = btn;
        grid_dragging_last_pos = mouse;
    }
}

void GameState::left_panel_click(XYPos pos, int clicks, int btn)
{
    if (prog_seen[PROG_LOCK_FILTER])
    {
        if ((pos - XYPos(button_size * 3, button_size * 1)).inside(XYPos(button_size,button_size)))
            filter_pos.clear();
        if ((pos - XYPos(button_size * 4, button_size * 1)).inside(XYPos(button_size,button_size)))
        {
            if(mouse_mode == MOUSE_MODE_FILTER)
                mouse_mode = MOUSE_MODE_NONE;
            else
                mouse_mode = MOUSE_MODE_FILTER;
        }
    }
    if (prog_seen[PROG_LOCK_VISIBILITY])
    {
        if ((pos - XYPos(button_size * 3, button_size * 2)).inside(XYPos(button_size * 2,button_size)))
            vis_level = GRID_VIS_LEVEL_SHOW;
        if ((pos - XYPos(button_size * 3, button_size * 3)).inside(XYPos(button_size * 2,button_size)))
            vis_level = GRID_VIS_LEVEL_HIDE;
        if ((pos - XYPos(button_size * 3, button_size * 4)).inside(XYPos(button_size * 2,button_size)))
            vis_level = GRID_VIS_LEVEL_BIN;
    }

    if ((pos - XYPos(button_size * 0, button_size * 0)).inside(XYPos(button_size,button_size)))
        display_menu = true;
    if ((pos - XYPos(button_size * 1, button_size * 0)).inside(XYPos(button_size,button_size)))
        display_help = true;
    if ((pos - XYPos(button_size * 2, button_size * 0)).inside(XYPos(button_size,button_size)))
    {
        if (btn == 2)
        {
            for (GridRegion& r : grid->regions)
                if (r.visibility_force == GridRegion::VIS_FORCE_HINT)
                {
                    r.visibility_force = GridRegion::VIS_FORCE_NONE;
                    r.vis_level = GRID_VIS_LEVEL_SHOW;
                }
            get_hint = false;
            return;
        }
        if (get_hint)
        {
            get_hint = false;
            return;
        }
        for (GridRegion& r : grid->regions)
            if (r.visibility_force == GridRegion::VIS_FORCE_HINT && r.vis_level == GRID_VIS_LEVEL_SHOW)
                r.visibility_force = GridRegion::VIS_FORCE_NONE;

        clue_solves.clear();
        XYSet grid_squares = grid->get_squares();
        FOR_XY_SET(pos, grid_squares)
        {
            if (grid->is_determinable_using_regions(pos, true))
                clue_solves.insert(pos);
        }
        get_hint = true;

    }
    if ((pos - XYPos(button_size * 3, button_size * 0)).inside(XYPos(button_size,button_size)))
    {
        skip_level = (btn == 2) ? -1 : 1;
        force_load_level = false;
    }
    if ((pos - XYPos(button_size * 4, button_size * 0)).inside(XYPos(button_size,button_size)))
    {
        clue_solves.clear();
        grid_regions_animation.clear();
        grid_regions_fade.clear();
        grid->clear_regions();
        reset_rule_gen_region();
        inspected_rule.regions[0] = NULL;
        inspected_rule.regions[1] = NULL;
        inspected_rule.regions[2] = NULL;
        inspected_rule.regions[3] = NULL;

        get_hint = false;
    }

    if (prog_seen[PROG_LOCK_SPEED] && (pos - XYPos(button_size * 0, button_size * 1)).inside(XYPos(button_size * 3, button_size)))
    {
        dragging_speed = true;
        double p = double(mouse.x - left_panel_offset.x - (button_size / 6)) / (button_size * 2.6666);
        speed_dial = std::clamp(p, 0.0, 1.0);
    }

    if (prog_seen[PROG_LOCK_TABLES])
    {
        if ((pos - XYPos(button_size * 0, button_size * 2)).inside(XYPos(button_size * 2, button_size)))
        {
            display_rules = !display_rules;
            display_clipboard_rules = false;
            if (display_rules)
            {
                display_scores = false;
                display_levels = false;
            }
        }
        if ((pos - XYPos(button_size * 0, button_size * 3)).inside(XYPos(button_size * 2, button_size)))
        {
            display_levels = !display_levels;
            display_clipboard_rules = false;
            if (display_levels)
            {
                display_rules = false;
                display_scores = false;
            }
        }
        if ((pos - XYPos(button_size * 0, button_size * 4)).inside(XYPos(button_size * 2, button_size)))
        {
            display_scores = !display_scores;
            display_clipboard_rules = false;
            if (display_scores)
            {
                display_rules = false;
                display_levels = false;
            }
        }
    }

    if ((pos - XYPos(button_size * 2, button_size * 2)).inside(XYPos(button_size, button_size)) && prog_seen[PROG_LOCK_GAME_MODE])
    {
        display_modes = true;
    }
    if ((pos - XYPos(button_size * 2, button_size * 3)).inside(XYPos(button_size, button_size)))
    {
        if (clipboard_has_item == CLIPBOARD_HAS_RULE)
        {
            reset_rule_gen_region();
            constructed_rule = clipboard_rule;
            right_panel_mode = RIGHT_MENU_RULE_GEN;
            update_constructed_rule();
        }
        if (clipboard_has_item == CLIPBOARD_HAS_RULE_SET)
        {
            display_rules = true;
            display_clipboard_rules = true;
            display_rules_sort_col = 0;
            display_rules_sort_dir = true;
            display_rules_sort_col_2nd = 1;
            display_rules_sort_dir_2nd = true;
        }
         if (clipboard_has_item == CLIPBOARD_HAS_LEVEL)
        {
            load_grid(clipboard_level);
            current_level_is_temp = true;
        }
   }

    if ((pos - XYPos(button_size * 0, button_size * 5.5)).inside(XYPos(button_size * (GLBAL_LEVEL_SETS + 1), button_size)))
    {
        int x = ((pos - XYPos(button_size * 0, button_size * 5.5)) / button_size).x;
        if (x >= 0 && x <= GLBAL_LEVEL_SETS && prog_seen[PROG_LOCK_HEX + x])
        {
            current_level_group_index = x;
            current_level_set_index = 0;
            current_level_index = 0;
            load_level = true;
            force_load_level = false;
            auto_progress = false;

            auto_progress_all = (clicks > 1);
            auto_progress = (clicks > 1);
            if (auto_progress)
                seen_ff = true;
            
        }
        return;
    }

    XYPos gpos = (pos - XYPos(0, int(button_size * 6.5)))/ button_size;
    unsigned idx = gpos.x + gpos.y * 5;

    if ((idx >= 0) && (idx < level_progress[game_mode][current_level_group_index].size()))
    {
        if (level_progress[game_mode][current_level_group_index][idx].level_status.size() && level_is_accessible(game_mode, current_level_group_index, idx))
        {
            current_level_set_index = idx;
            current_level_index = 0;
            load_level = true;
            force_load_level = false;
            auto_progress =  (clicks > 1);
            auto_progress_all = false;
            if (auto_progress)
                seen_ff = true;
        }
    }
}

void GameState::right_panel_click(XYPos pos, int clicks, int btn)
{
    if (right_panel_mode == RIGHT_MENU_REGION)
    {
        if ((pos - XYPos(button_size * 3, button_size)).inside(XYPos(button_size * 2, button_size)))
        {
            right_panel_mode = RIGHT_MENU_NONE;
        }
        if ((pos - XYPos(button_size * 4, button_size)).inside(XYPos(button_size, button_size)))
        {
            if (constructed_rule.region_count < (game_mode == 1 ? 3 : 4))
            {
                right_panel_mode = RIGHT_MENU_RULE_GEN;
                bool found = false;
                for (int i = 0; i < constructed_rule.region_count; i++)
                {
                    if (rule_gen_region[i] == inspected_region)
                        found = true;
                }
                if (!found)
                {
                    update_constructed_rule_pre();
                    rule_gen_region[constructed_rule.region_count] = inspected_region;
                    constructed_rule.import_rule_gen_regions(rule_gen_region[0], rule_gen_region[1], rule_gen_region[2], rule_gen_region[3]);
                    update_constructed_rule();
                }
            }
            return;
        }
        if ((pos - XYPos(button_size * 0, button_size * 4)).inside(XYPos(button_size, button_size)))
        {
            if(inspected_region->gen_cause.rule)
            {
                right_panel_mode = RIGHT_MENU_RULE_INSPECT;
                inspected_rule = inspected_region->gen_cause;
                selected_rules.clear();
                selected_rules.insert(inspected_rule.rule);
            }
            return;
        }
        if ((pos - XYPos(button_size * 1, button_size * 4)).inside(XYPos(button_size, button_size)))
        {
            if(inspected_region->visibility_force == GridRegion::VIS_FORCE_USER || inspected_region->visibility_force == GridRegion::VIS_FORCE_HINT)
            {
                inspected_region->visibility_force = GridRegion::VIS_FORCE_NONE;
                inspected_region->vis_cause.rule = NULL;
            }
            else if(inspected_region->vis_cause.rule)
            {
                right_panel_mode = RIGHT_MENU_RULE_INSPECT;
                inspected_rule = inspected_region->vis_cause;
                selected_rules.clear();
                selected_rules.insert(inspected_rule.rule);
            }
            return;
        }
        if ((pos - XYPos(button_size * 0, button_size * 5)).inside(XYPos(button_size, button_size)))
        {
            inspected_region->next_colour();
            return;
        }
        if (prog_seen[PROG_LOCK_VISIBILITY])
        {
            if ((pos - XYPos(button_size * 3, button_size * 3)).inside(XYPos(button_size, button_size)))
            {
                inspected_region->vis_level = GRID_VIS_LEVEL_SHOW;
                if (!ctrl_held)
                    inspected_region->visibility_force = GridRegion::VIS_FORCE_USER;
                inspected_region->vis_cause.rule = NULL;
                inspected_region->stale = false;
            }
            if ((pos - XYPos(button_size * 3, button_size * 4)).inside(XYPos(button_size, button_size)))
            {
                inspected_region->vis_level = GRID_VIS_LEVEL_HIDE;
                if (!ctrl_held)
                    inspected_region->visibility_force = GridRegion::VIS_FORCE_USER;
                inspected_region->vis_cause.rule = NULL;
                inspected_region->stale = false;
            }
            if ((pos - XYPos(button_size * 3, button_size * 5)).inside(XYPos(button_size, button_size)))
            {
                inspected_region->vis_level = GRID_VIS_LEVEL_BIN;
                if (!ctrl_held)
                    inspected_region->visibility_force = GridRegion::VIS_FORCE_USER;
                inspected_region->vis_cause.rule = NULL;
                inspected_region->stale = false;
            }
        }

    }

    if (right_panel_mode == RIGHT_MENU_RULE_INSPECT)
    {
        if ((pos - XYPos(button_size * 3, button_size)).inside(XYPos(button_size, button_size)))
        {
            right_panel_mode = RIGHT_MENU_NONE;
            return;
        }
        if ((pos - XYPos(button_size * 4, button_size)).inside(XYPos(button_size, button_size)) && !inspected_rule.rule->deleted)
        {
            // if (selected_rules.empty())
            // {
            //     inspected_rule.rule->deleted = true;
            //     if (inspected_rule.rule->apply_region_type.type != RegionType::VISIBILITY && (game_mode == 2 || game_mode == 3))
            //         rule_del_count[game_mode]++;
            // }
            // else
            {
                pause_robots();
                last_deleted_rules[game_mode].clear();
                for (GridRule* rule : selected_rules)
                {
                    rule->deleted = true;
                    if (rule->apply_region_type.type != RegionType::VISIBILITY && (game_mode == 2 || game_mode == 3))
                        rule_del_count[game_mode]++;
                    last_deleted_rules[game_mode].insert(rule);
                }
            }
            right_panel_mode = RIGHT_MENU_NONE;
            return;
        }
        if ((pos - XYPos(button_size * 3, button_size * 2)).inside(XYPos(button_size * 2, button_size)))
        {
            reset_rule_gen_region();
            constructed_rule = *inspected_rule.rule;
            constructed_rule.used_count = 0;
            constructed_rule.clear_count = 0;

            rule_gen_region[0] = inspected_rule.regions[0];
            rule_gen_region[1] = inspected_rule.regions[1];
            rule_gen_region[2] = inspected_rule.regions[2];
            rule_gen_region[3] = inspected_rule.regions[3];
            constructed_rule.deleted = false;
            constructed_rule.stale = false;
            if ((pos - XYPos(button_size * 3, button_size * 2)).inside(XYPos(button_size, button_size)))
                duplicate_rule = false;
            else
                duplicate_rule = true;
            replace_rule = inspected_rule.rule;
            right_panel_mode = RIGHT_MENU_RULE_GEN;
            update_constructed_rule();
            return;
        }
        if ((pos - XYPos(button_size * 3, button_size * 6)).inside(XYPos(button_size, button_size)))
        {
            if (selected_rules.size() == 1)
            {
                SaveObjectMap* omap = new SaveObjectMap;
                omap->add_item("rule", inspected_rule.rule->save(true));
                send_to_clipboard("Rule", omap);
                delete omap;
            }
            else
            {
                SaveObjectMap* omap = new SaveObjectMap;
                SaveObjectList* rlist = new SaveObjectList;
                for (GridRule* rule : selected_rules)
                {
                    if (!rule->deleted)
                        rlist->add_item(rule->save(true));
                }
                omap->add_item("rules", rlist);
                std::string title = "Rules (" + std::to_string(rlist->get_count()) + ")";
                send_to_clipboard(title, omap);
                delete omap;
            }
            return;
        }
        if ((pos - XYPos(button_size * 4, button_size * 6)).inside(XYPos(button_size, button_size)) && selected_rules.size() == 1)
        {
            send_rule_to_img_clipboard(*inspected_rule.rule);
            return;
        }

        // if (inspected_rule.rule->apply_region_type.type < RegionType::SET)
        {
            if (prog_seen[PROG_LOCK_PRIORITY])
            {
                bool has_reg = false;
                for (GridRule* rule : selected_rules)
                    if (rule->apply_region_type.type < RegionType::SET)
                        has_reg = true;
                if (has_reg && (pos - XYPos(button_size * 0, button_size * 6)).inside(XYPos(button_size, button_size * 5)))
                {
                    int y = ((pos - XYPos(button_size * 0, button_size * 6)) / button_size).y;
                    int np = 2 - y;
                    np = std::clamp(np, -2, 2);
                    for (GridRule* rule : selected_rules)
                    {
                        if (rule->paused)
                        {
                            rule->paused = false;
                            rule->stale = false;
                        }
                        if (rule->apply_region_type.type < RegionType::SET)
                            rule->priority = np;
                        else
                            rule->priority = 0;
                    }
                    
                }
            }
        }
        {
            if (prog_seen[PROG_LOCK_PAUSE])
            {
                if ((pos - XYPos(button_size * 0, button_size * 11)).inside(XYPos(button_size, button_size)))
                {
                    pause_robots();
                    bool all_paused = true;
                    for (GridRule* rule : selected_rules)
                        if (!rule->paused)
                            all_paused = false;
                    for (GridRule* rule : selected_rules)
                    {
                        if (all_paused)
                        {
                            rule->stale = false;
                            rule->paused = false;
                        }
                        else
                        {
                            rule->paused = true;
                        }
                    }
                }
            }
        }
    }

    if (right_panel_mode == RIGHT_MENU_NONE)
    {
        if (display_rules && !display_clipboard_rules)
        {
            if ((pos - XYPos(button_size * 3, button_size * 6)).inside(XYPos(button_size, button_size)))
            {
                export_all_rules_to_clipboard();
                return;
            }
        }

        if ((pos - XYPos(button_size * 4, button_size * 1)).inside(XYPos(button_size, button_size)) && !last_deleted_rules[game_mode].empty())
        {
            pause_robots();
            selected_rules.clear();
            for (GridRule* rule : last_deleted_rules[game_mode])
                rule->deleted = false;
            selected_rules = last_deleted_rules[game_mode];
            last_deleted_rules[game_mode].clear();
            right_panel_mode = RIGHT_MENU_RULE_INSPECT;
            inspected_rule = GridRegionCause(*selected_rules.begin(), NULL, NULL, NULL, NULL);
            return;
        }

        if ((pos - XYPos(button_size * 3, button_size * 2)).inside(XYPos(button_size, button_size)))
        {
            rule_gen_undo();
            return;
        }
        if ((pos - XYPos(button_size * 4, button_size * 2)).inside(XYPos(button_size, button_size)))
        {
            rule_gen_redo();
            return;
        }
        if ((pos - XYPos(button_size * 1, button_size * 2)).inside(XYPos(button_size, button_size)))
        {
            pause_robots();
            should_run_robots = !should_run_robots;
            return;
        }


        if ((pos - XYPos(button_size * 0, button_size * 3)).inside(XYPos(button_size, button_size * 3)))
        {
            dragging_scroller = true;
            dragging_scroller_type = DRAGGING_SCROLLER_RULES;
            double p = 1.0 - double(mouse.y - left_panel_offset.y - (button_size * 3) - (button_size / 6)) / (button_size * 2.6666);
            rule_limit_slider = std::clamp(p, 0.0, 1.0);
            int new_rule_limit_count = pow(100, 1 + rule_limit_slider * 1.6) / 10;
            if (rule_limit_slider >= 1.0)
                new_rule_limit_count = -1;
            if (new_rule_limit_count > rule_limit_count)
                pause_robots();
            rule_limit_count = new_rule_limit_count;
            return;
        }


    }

    if (right_panel_mode != RIGHT_MENU_RULE_GEN)
    {
        if ((pos - XYPos(button_size * 1, button_size * 6)).inside(XYPos(button_size, button_size)))
        {
            right_panel_mode = RIGHT_MENU_RULE_GEN;
            return;
        }
    }
    if (((right_panel_mode == RIGHT_MENU_NONE) || (right_panel_mode == RIGHT_MENU_REGION)) && !display_rules)
    {
        if ((pos - XYPos(button_size * 3, button_size * 6)).inside(XYPos(button_size, button_size)))
        {
            SaveObjectMap* omap = new SaveObjectMap;
            omap->add_string("level", grid->to_string());

            std::string title = "Level (" + grid->text_desciption() + ")";
            send_to_clipboard(title, omap);
            delete omap;
            return;
        }
    }

    if (right_panel_mode == RIGHT_MENU_RULE_GEN || right_panel_mode == RIGHT_MENU_RULE_INSPECT)
    {
        GridRegionCause rule_cause = (right_panel_mode == RIGHT_MENU_RULE_GEN) ? GridRegionCause(&constructed_rule, rule_gen_region[0], rule_gen_region[1], rule_gen_region[2], rule_gen_region[3]) : inspected_rule;

        int region_index = -1;
        if (XYPosFloat(mouse - (right_panel_offset + XYPos(0 * button_size, 1 * button_size) + XYPos(button_size / 3, button_size / 3))).distance() < (button_size / 3) && rule_cause.rule->region_count >= 1)
            region_index = 0;
        if (XYPosFloat(mouse - (right_panel_offset + XYPos(2 * button_size, 1 * button_size) + XYPos((button_size * 2) / 3, button_size / 3 + button_size / 12))).distance() < (button_size / 3) && rule_cause.rule->region_count >= 2)
            region_index = 1;
        if (XYPosFloat(mouse - (right_panel_offset + XYPos(4 * button_size, 3 * button_size) + XYPos((button_size * 2) / 3, button_size / 3))).distance() < (button_size / 3) && rule_cause.rule->region_count >= 3)
            region_index = 2;
        if (XYPosFloat(mouse - (right_panel_offset + XYPos(4 * button_size, 5 * button_size) + XYPos((button_size * 2) / 3 + button_size / 12, (button_size * 2) / 3))).distance() < (button_size / 3) && rule_cause.rule->region_count >= 4)
            region_index = 3;

        if (region_index >= 0)
        {
            if (right_panel_mode == RIGHT_MENU_RULE_GEN)
            {
                if (btn == 2)
                {
                    update_constructed_rule_pre();
                    int new_count = constructed_rule.region_count - 1;
                    for (int i = region_index; i < new_count; i++)
                        rule_gen_region[i] = rule_gen_region[i+1];
                    rule_gen_region[new_count] = NULL;
                    constructed_rule.remove_region(region_index);
                    update_constructed_rule();
                    return;
                }
                if (region_type.type == RegionType::VISIBILITY)
                {
                    update_constructed_rule_pre();
                    constructed_rule.apply_region_bitmap ^= 1 << region_index;
                    if (constructed_rule.apply_region_type != region_type)
                    {
                        constructed_rule.apply_region_type = region_type;
                        constructed_rule.apply_region_bitmap = 1 << region_index;
                    }
                    update_constructed_rule();
                    return;
                } 
                if (region_type != constructed_rule.region_type[region_index])
                {
                    if (region_type.type > 50)
                        return;
                    update_constructed_rule_pre();
                    constructed_rule.region_type[region_index] = region_type;
                    rule_gen_region[region_index] = NULL;
                    update_constructed_rule();
                    return;
                }
            }
            else if (rule_cause.regions[region_index])
            {
                right_panel_mode = RIGHT_MENU_REGION;
                inspected_region = rule_cause.regions[region_index];
            }
            return;
        }
    }
    if (btn)
        return;

    if (right_panel_mode == RIGHT_MENU_RULE_GEN)
    {
        if (prog_seen[PROG_LOCK_DONT_CARE])
        {
            if ((pos - XYPos(button_size * 0, button_size * 6.2)).inside(XYPos(button_size,button_size)))
            {
                region_type = RegionType(RegionType::NONE, 0);
                if (clicks >= 2 && constructed_rule.region_count < (game_mode == 1 ? 3 : 4))
                {
                    update_constructed_rule_pre();
                    rule_gen_region[constructed_rule.region_count] = NULL;
                    constructed_rule.add_region(region_type);
                    update_constructed_rule();
                }
            }
        }
        if ((pos - XYPos(button_size * 1, button_size * 6.2)).inside(XYPos(button_size,button_size)))
            region_type = RegionType(RegionType::SET, 0);
        if ((pos - XYPos(button_size * 2, button_size * 6.2)).inside(XYPos(button_size,button_size)))
            region_type = RegionType(RegionType::SET, 1);
        if (prog_seen[PROG_LOCK_VISIBILITY])
        {
            if ((pos - XYPos(button_size * 3, button_size * 6.2)).inside(XYPos(button_size,button_size)))
                region_type = RegionType(RegionType::VISIBILITY, 1);
            if ((pos - XYPos(button_size * 4, button_size * 6.2)).inside(XYPos(button_size,button_size)))
                region_type = RegionType(RegionType::VISIBILITY, 2);
        }
        if(prog_seen[PROG_LOCK_NUMBER_TYPES])
        {
            if ((pos - XYPos(button_size * 0, button_size * 7.4)).inside(XYPos(5 * button_size, 2 * button_size)))
            {
                XYPos region_item_selected = (pos - XYPos(0, button_size * 7.4)) / button_size;
                RegionType::Type t = menu_region_types[region_item_selected.y][region_item_selected.x];
                if (t == RegionType::NONE)
                {
                }
                else
                {
                    select_region_type.type = t;
                    region_type = select_region_type;
                    if (clicks >= 2 && constructed_rule.region_count < (game_mode == 1 ? 3 : 4))
                    {
                        update_constructed_rule_pre();
                        rule_gen_region[constructed_rule.region_count] = NULL;
                        constructed_rule.add_region(region_type);
                        update_constructed_rule();
                    }
                }
            }

            if ((pos - XYPos(button_size * 0, button_size * 9.6)).inside(XYPos(5 * button_size, 2 * button_size)))
            {
                XYPos region_item_selected = (pos - XYPos(0, button_size * 9.6)) / button_size;
                select_region_type.value = region_item_selected.x + (region_item_selected.y) * 5;
                region_type = select_region_type;
                if (clicks >= 2 && constructed_rule.region_count < (game_mode == 1 ? 3 : 4))
                {
                    update_constructed_rule_pre();
                    rule_gen_region[constructed_rule.region_count] = NULL;
                    constructed_rule.add_region(region_type);
                    update_constructed_rule();
                }
            }
            if ((pos - XYPos(button_size * 0, button_size * 11.8)).inside(XYPos(5 * button_size, 1 * button_size)))
            {
                int i =  pos.x / button_size;
                if (game_mode != 3 && prog_seen[PROG_LOCK_VARS1 + i])
                {
                    select_region_type.var ^= (1 << i);
                    region_type = select_region_type;
                }
            }
        }

        // if ((pos - XYPos(button_size * 0, button_size * 10 + button_size / 2)).inside(XYPos(5 * button_size, 2 * button_size)))
        // {
        //     XYPos region_item_selected = (pos - XYPos(0, button_size * 10 + button_size / 2)) / button_size;
        //     if (menu_region_types2[region_menu].type == RegionType::NONE)
        //     {
        //         region_type_var_value = region_item_selected.x + (region_item_selected.y) * 5;
        //         if (region_type.var)
        //             region_type.value = region_type_var_value;
        //     }
        //     else
        //     {
        //         region_type = menu_region_types2[region_menu];
        //         region_type.value += region_item_selected.x + (region_item_selected.y) * 5;
        //     }
        // }
        if ((pos - XYPos(0, button_size * 2)).inside(XYPos(button_size * 4, button_size * 4)))
        {
            XYPos gpos = pos / button_size;

            XYPos ipos = (pos - (gpos * button_size));
            bool hover_rulemaker_lower_right = ((ipos.x > button_size/2) && (ipos.y > button_size/2));


            bool hover_rulemaker = false;
            int x = gpos.x;
            if (x == 0) x = 1;
            else if (x == 1) x = 3;
            else if (x == 2) x = 2;
            else if (x == 3) x = 0;
            else assert(0);

            int y = gpos.y;
            y -= 2;
            y ^= y >> 1;

            unsigned hover_rulemaker_bits = x + y * 4;
            if (hover_rulemaker_bits >= 1 && hover_rulemaker_bits < (1u << constructed_rule.region_count))
            {
                hover_rulemaker = true;
            }

            if (hover_rulemaker)
            {
                if (!hover_rulemaker_lower_right)
                {
                    update_constructed_rule_pre();
                    uint8_t square_counts[16];
                    GridRule::get_square_counts(square_counts, rule_gen_region[0], rule_gen_region[1], rule_gen_region[2], rule_gen_region[3]);
                    RegionType new_region_type = region_type;
                    if (region_type.type >= 50)
                    {
                        new_region_type = RegionType(RegionType::NONE, 0);
                        // if ((constructed_rule.square_counts[hover_rulemaker_bits] == RegionType(RegionType::NONE, 0)) ||
                        //     (constructed_rule.square_counts[hover_rulemaker_bits] == RegionType(RegionType::EQUAL, 0) && square_counts[hover_rulemaker_bits]))
                        //     new_region_type = RegionType(RegionType::EQUAL, 0);
                    }
                    if (constructed_rule.square_counts[hover_rulemaker_bits] == new_region_type)
                    {
                        constructed_rule.square_counts[hover_rulemaker_bits] = RegionType(RegionType::EQUAL, square_counts[hover_rulemaker_bits]);
                    }
                    else //if (new_region_type.type == RegionType::NONE || new_region_type.apply_int_rule(square_counts[hover_rulemaker_bits]))
                    {
                        constructed_rule.square_counts[hover_rulemaker_bits] = new_region_type;
                    }
//                    rule_gen_region_undef_num ^= 1 << hover_rulemaker_bits;
                    update_constructed_rule();
                }
                else if (region_type.type != RegionType::VISIBILITY && region_type.type != RegionType::NONE)
                {
                    update_constructed_rule_pre();
                    constructed_rule.apply_region_bitmap ^= 1 << hover_rulemaker_bits;
                    {
                        if (constructed_rule.apply_region_type != region_type)
                            constructed_rule.apply_region_bitmap = 1 << hover_rulemaker_bits;
                        constructed_rule.apply_region_type = region_type;
                    }
                    update_constructed_rule();
                }
            }
        }

        if ((pos - XYPos(button_size * 4 , button_size * 1)).inside(XYPos(button_size, button_size)))
        {
            if (constructed_rule.region_count && constructed_rule.apply_region_bitmap)
            {
                if ((constructed_rule_is_logical == GridRule::OK) || (constructed_rule_is_logical == GridRule::LOSES_DATA))
                {
                    if (constructed_rule_is_already_present)
                    {
                        inspected_rule = GridRegionCause(constructed_rule_is_already_present, NULL, NULL, NULL, NULL);
                        selected_rules.clear();
                        selected_rules.insert(inspected_rule.rule);
                        reset_rule_gen_region();
                        right_panel_mode = RIGHT_MENU_RULE_INSPECT;
                    }
                    else
                    {
                        if (rule_is_permitted(constructed_rule, game_mode))
                        {
                            pause_robots();
                            std::list<GridRule>::iterator it;
                            for (it = rules[game_mode].begin(); it != rules[game_mode].end(); it++)
                            {
                                if (&*it == replace_rule)
                                {
                                    it++;
                                    break;
                                }
                            }

                            if (replace_rule && duplicate_rule == false)
                            {
                                if (game_mode == 2 || game_mode == 3)
                                {
                                    if (replace_rule->apply_region_type.type != RegionType::VISIBILITY)
                                        rule_del_count[game_mode]++;
                                }
                                replace_rule->deleted = true;
                            }
                            it = rules[game_mode].insert(it, constructed_rule);
                            inspected_rule = GridRegionCause(&*it, rule_gen_region[0], rule_gen_region[1], rule_gen_region[2], rule_gen_region[3]);
                            selected_rules.clear();
                            selected_rules.insert(inspected_rule.rule);
                            reset_rule_gen_region();
                            right_panel_mode = RIGHT_MENU_RULE_INSPECT;
                        }
                    }
                }
            }
        }

        if ((pos - XYPos(button_size * 3, button_size * 1)).inside(XYPos(button_size, button_size)))
        {
//            reset_rule_gen_region();
            update_constructed_rule_pre();
            for (int i = 0; i < 4; i++)
                rule_gen_region[i] = NULL;
            while (constructed_rule.region_count)
                constructed_rule.remove_region(constructed_rule.region_count - 1);
            update_constructed_rule();
            right_panel_mode = RIGHT_MENU_NONE;
            replace_rule  = NULL;

        }
        if ((pos - XYPos(button_size * 3, button_size * 2)).inside(XYPos(button_size, button_size)))
        {
            rule_gen_undo();
        }
        if ((pos - XYPos(button_size * 4, button_size * 2)).inside(XYPos(button_size, button_size)))
        {
            rule_gen_redo();
        }
    }
}

bool GameState::events()
{
    bool quit = false;
    // if (grid->regions.size() > 500)
    //     quit = true;
    SDL_Event e;
    while(SDL_PollEvent(&e))
    {
	    switch (e.type)
        {
            case SDL_WINDOWEVENT:
                switch (e.window.event)
                {
                    case SDL_WINDOWEVENT_LEAVE:
                    {
                        display_rules_click_drag = false;
                        grid_dragging = false;
                        dragging_speed = false;
                        dragging_scroller = false;
                    }

                    default:
                    {
//                        printf("window event:0x%x\n", e.window.event);
                        break;
                    }
                }
            break;

            case SDL_QUIT:
		        quit = true;
                break;
            case SDL_KEYDOWN:
            {
                int key = e.key.keysym.sym;
                if (key == SDLK_LCTRL)
                {
                    ctrl_held |= 1;
                    break;
                }
                if (key == SDLK_RCTRL)
                {
                    ctrl_held |= 2;
                    break;
                }
                if (key == SDLK_LSHIFT)
                {
                    shift_held |= 1;
                    break;
                }
                if (key == SDLK_RSHIFT)
                {
                    shift_held |= 2;
                    break;
                }
                if (key == SDLK_F12 && ctrl_held)
                {
                    display_debug = !display_debug;
                    break;
                }
                if (capturing_key >= 0)
                {
                    key_codes[capturing_key] = e.key.keysym.sym;
                    capturing_key = -1;
                    break;
                }
                if (key == SDLK_PAGEUP)
                {
                    if (display_rules || display_scores || display_levels)
                    {
                        rules_list_offset -= 16;
                        break;
                    }
                }
                if (key == SDLK_PAGEDOWN)
                {
                    if (display_rules || display_scores || display_levels)
                    {
                        rules_list_offset += 16;
                        break;
                    }
                }
                if (display_help || display_language_chooser || display_menu || display_key_select || display_about)
                {
                    if ((e.key.keysym.sym == key_codes[KEY_CODE_HELP]) ||
                        (e.key.keysym.sym == SDLK_ESCAPE))
                    {
                        display_help = false;    
                        display_language_chooser = false;
                        display_key_select = false;
                        display_menu = false;
                        display_about = false;
                    }
                    if (e.key.keysym.sym == key_codes[KEY_CODE_FULL_SCREEN])
                    {
                        full_screen = !full_screen;
                        SDL_SetWindowFullscreen(sdl_window, full_screen? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
                        SDL_SetWindowBordered(sdl_window, full_screen ? SDL_FALSE : SDL_TRUE);
                        SDL_SetWindowResizable(sdl_window, full_screen ? SDL_FALSE : SDL_TRUE);
                        SDL_SetWindowInputFocus(sdl_window);
                    }
                    break;
                }
                {
                    if (key == key_codes[KEY_CODE_UNDO])
                        rule_gen_undo();
                    else if (key == key_codes[KEY_CODE_REDO])
                        rule_gen_redo();
                    else if (key == key_codes[KEY_CODE_G_VISIBLE])
                        key_held = 'Q';
                    else if (key == key_codes[KEY_CODE_G_HIDDEN])
                        key_held = 'W';
                    else if (key == key_codes[KEY_CODE_G_TRASH])
                        key_held = 'E';
                    else if (key == SDLK_ESCAPE)
                        display_menu = true;
                    else if (key == key_codes[KEY_CODE_HELP])
                        display_help = true;
                    else if (key == key_codes[KEY_CODE_HINT])
                    {
                        if (shift_held)
                        {
                            for (GridRegion& r : grid->regions)
                                if (r.visibility_force == GridRegion::VIS_FORCE_HINT)
                                {
                                    r.visibility_force = GridRegion::VIS_FORCE_NONE;
                                    r.vis_level = GRID_VIS_LEVEL_SHOW;
                                }
                            get_hint = false;
                            break;
                        }
                        if (get_hint)
                        {
                            get_hint = false;
                            break;
                        }
                        clue_solves.clear();
                        XYSet grid_squares = grid->get_squares();
                        FOR_XY_SET(pos, grid_squares)
                        {
                            if (grid->is_determinable_using_regions(pos, true))
                                clue_solves.insert(pos);
                        }
                        get_hint = true;
                    }
                    else if (key == key_codes[KEY_CODE_SKIP])
                    {
                        skip_level = (shift_held) ? -1 : 1;
                        force_load_level = false;
                        break;
                    }
                    else if (key == key_codes[KEY_CODE_REFRESH])
                    {
                        clue_solves.clear();
                        grid_regions_animation.clear();
                        grid_regions_fade.clear();
                        grid->clear_regions();
                        reset_rule_gen_region();
                        get_hint = false;
                        break;
                    }
                    else if (key == SDLK_F5)
                    {
                        if (mouse_mode != MOUSE_MODE_PAINT)
                            mouse_mode = MOUSE_MODE_PAINT;
                        else
                            mouse_mode = MOUSE_MODE_NONE;
                        break;
                    }
                    else if (key == SDLK_F6)
                    {
                        debug_bits[0] = !debug_bits[0];
                        break;
                    }
                    else if (key == key_codes[KEY_CODE_FULL_SCREEN])
                    {
                        full_screen = !full_screen;
                        SDL_SetWindowFullscreen(sdl_window, full_screen? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
                        SDL_SetWindowBordered(sdl_window, full_screen ? SDL_FALSE : SDL_TRUE);
                        SDL_SetWindowResizable(sdl_window, full_screen ? SDL_FALSE : SDL_TRUE);
                        SDL_SetWindowInputFocus(sdl_window);
                        break;
                    }
                    else if (key == key_codes[KEY_CODE_DONT_CARE])
                    {
                        region_type = RegionType(RegionType::NONE, 0);
                        break;
                    }
                    else if (key == key_codes[KEY_CODE_CLEAR])
                    {
                        region_type = RegionType(RegionType::SET, 0);
                        break;
                    }
                    else if (key == key_codes[KEY_CODE_BOMB])
                    {
                        region_type = RegionType(RegionType::SET, 1);
                        break;
                    }
                    else if (key == key_codes[KEY_CODE_HIDE])
                    {
                        region_type = RegionType(RegionType::VISIBILITY, 1);
                        break;
                    }
                    else if (key == key_codes[KEY_CODE_TRASH])
                    {
                        region_type = RegionType(RegionType::VISIBILITY, 2);
                        break;
                    }
                    else if(prog_seen[PROG_LOCK_NUMBER_TYPES])
                    {
                        if (key == key_codes[KEY_CODE_VAR1])
                        {
                            if (game_mode != 3 && prog_seen[PROG_LOCK_VARS1])
                            {
                                select_region_type.var ^= (1 << 0);
                                region_type = select_region_type;
                            }
                            break;
                        }
                        else if (key == key_codes[KEY_CODE_VAR2])
                        {
                            if (game_mode != 3 && prog_seen[PROG_LOCK_VARS2])
                            {
                                select_region_type.var ^= (1 << 1);
                                region_type = select_region_type;
                            }
                            break;
                        }
                        else if (key == key_codes[KEY_CODE_VAR3])
                        {
                            if (game_mode != 3 && prog_seen[PROG_LOCK_VARS3])
                            {
                                select_region_type.var ^= (1 << 2);
                                region_type = select_region_type;
                            }
                            break;
                        }
                        else if (key == key_codes[KEY_CODE_VAR4])
                        {
                            if (game_mode != 3 && prog_seen[PROG_LOCK_VARS4])
                            {
                                select_region_type.var ^= (1 << 3);
                                region_type = select_region_type;
                            }
                            break;
                        }
                        else if (key == key_codes[KEY_CODE_VAR5])
                        {
                            if (game_mode != 3 && prog_seen[PROG_LOCK_VARS5])
                            {
                                select_region_type.var ^= (1 << 4);
                                region_type = select_region_type;
                            }
                            break;
                        }
                        else if (key == key_codes[KEY_CODE_0])
                        {
                            select_region_type.value = 0;
                            region_type = select_region_type;
                            break;
                        }
                        else if (key == key_codes[KEY_CODE_1])
                        {
                            select_region_type.value = 1;
                            region_type = select_region_type;
                            break;
                        }
                        else if (key == key_codes[KEY_CODE_2])
                        {
                            select_region_type.value = 2;
                            region_type = select_region_type;
                            break;
                        }
                        else if (key == key_codes[KEY_CODE_3])
                        {
                            select_region_type.value = 3;
                            region_type = select_region_type;
                            break;
                        }
                        else if (key == key_codes[KEY_CODE_4])
                        {
                            select_region_type.value = 4;
                            region_type = select_region_type;
                            break;
                        }
                        else if (key == key_codes[KEY_CODE_5])
                        {
                            select_region_type.value = 5;
                            region_type = select_region_type;
                            break;
                        }
                        else if (key == key_codes[KEY_CODE_6])
                        {
                            select_region_type.value = 6;
                            region_type = select_region_type;
                            break;
                        }
                        else if (key == key_codes[KEY_CODE_7])
                        {
                            select_region_type.value = 7;
                            region_type = select_region_type;
                            break;
                        }
                        else if (key == key_codes[KEY_CODE_8])
                        {
                            select_region_type.value = 8;
                            region_type = select_region_type;
                            break;
                        }
                        else if (key == key_codes[KEY_CODE_9])
                        {
                            select_region_type.value = 9;
                            region_type = select_region_type;
                            break;
                        }
                        else if (key == key_codes[KEY_CODE_EQUAL])
                        {
                            select_region_type.type = RegionType::EQUAL;
                            region_type = select_region_type;
                            break;
                        }
                        else if (key == key_codes[KEY_CODE_NOTEQUAL])
                        {
                            select_region_type.type = RegionType::NOTEQUAL;
                            region_type = select_region_type;
                            break;
                        }
                        else if (key == key_codes[KEY_CODE_PLUS])
                        {
                            select_region_type.type = RegionType::MORE;
                            region_type = select_region_type;
                            break;
                        }
                        else if (key == key_codes[KEY_CODE_MINUS])
                        {
                            select_region_type.type = RegionType::LESS;
                            region_type = select_region_type;
                            break;
                        }
                        else if (key == key_codes[KEY_CODE_XOR3])
                        {
                            select_region_type.type = RegionType::XOR3;
                            region_type = select_region_type;
                            break;
                        }
                        else if (key == key_codes[KEY_CODE_XOR2])
                        {
                            select_region_type.type = RegionType::XOR2;
                            region_type = select_region_type;
                            break;
                        }
                        else if (key == key_codes[KEY_CODE_XOR22])
                        {
                            select_region_type.type = RegionType::XOR22;
                            region_type = select_region_type;
                            break;
                        }
                        else if (key == key_codes[KEY_CODE_PARITY])
                        {
                            select_region_type.type = RegionType::PARITY;
                            region_type = select_region_type;
                            break;
                        }
                        else if (key == key_codes[KEY_CODE_XOR1])
                        {
                            select_region_type.type = RegionType::XOR1;
                            region_type = select_region_type;
                            break;
                        }
                        else if (key == key_codes[KEY_CODE_XOR11])
                        {
                            select_region_type.type = RegionType::XOR11;
                            region_type = select_region_type;
                            break;
                        }
                        else
                        {
                            printf("Uncaught key: %d\n", key);
                        }
                    }
                }
                break;
            }
            case SDL_KEYUP:
            {
                int key = e.key.keysym.sym;
                if (key == SDLK_LCTRL)
                    ctrl_held &= ~1;
                if (key == SDLK_RCTRL)
                    ctrl_held &= ~2;
                if (key == SDLK_LSHIFT)
                    shift_held &= ~1;
                if (key == SDLK_RSHIFT)
                    shift_held &= ~2;
                if (key == key_codes[KEY_CODE_G_VISIBLE] || 
                    key == key_codes[KEY_CODE_G_HIDDEN] ||
                    key == key_codes[KEY_CODE_G_TRASH])
                        key_held = 0;
                break;
            }
            case SDL_MOUSEMOTION:
            {
                mouse.x = e.motion.x * mouse_scale.x;
                mouse.y = e.motion.y * mouse_scale.y;
                if (grid_dragging)
                {
                    if (mouse_mode == MOUSE_MODE_PAINT)
                    {
                        XYPos pos = mouse - grid_offset - scaled_grid_offset;
                        XYPos siz = grid->get_wrapped_size(grid_pitch);
                        XYPos ppos = (pos * 2048) / siz;
                        XYPos opos = grid_dragging_last_pos - grid_offset - scaled_grid_offset;
                        XYPos oppos = (opos * 2048) / siz;

                        uint32_t* pixels;
                        int pitch;
                        int r = SDL_LockTexture(overlay_texture, NULL, (void**)&pixels, &pitch);
                        assert(r == 0);
                        pitch /= 4;
                        int s = 10;
                        if (grid_dragging_btn)
                            s = 30;
                        uint32_t nv = grid_dragging_btn ? 0 : 0xFFFFFFFF;
                        for (int i = 0; i < 10; i++)
                        {
                            XYPos lp = oppos / 10 * i + ppos / 10 * (10 - i);
                            FOR_XY(p, lp - XYPos(s, s), lp + XYPos(s, s))
                            {
                                if (!p.inside(XYPos(2048,2048)))
                                    continue;
                                pixels[p.y * pitch + p.x] = nv;
                            }
                        }
                        if (grid_dragging_btn == 0)
                            overlay_texture_is_clean = false;
                        SDL_UnlockTexture(overlay_texture);
                    }
                    else
                    {
                        scaled_grid_offset += (mouse - grid_dragging_last_pos);
                    }
                    grid_dragging_last_pos = mouse;
                }
                if (dragging_speed)
                {
                    double p = double(mouse.x - left_panel_offset.x - (button_size / 6)) / (button_size * 2.6666);
                    speed_dial = std::clamp(p, 0.0, 1.0);
                }
                if (dragging_scroller)
                {
                    double p = 1.0 - double(mouse.y - left_panel_offset.y - (button_size * 3) - (button_size / 6)) / (button_size * 2.6666);
                    if (dragging_scroller_type == DRAGGING_SCROLLER_COLOUR)
                    {
                        colors = std::clamp(p, 0.0, 1.0);
                    }
                    else if (dragging_scroller_type == DRAGGING_SCROLLER_MUSIC)
                    {
                        music_volume = std::clamp(p, 0.0, 1.0);
                        if (has_sound)
                            Mix_VolumeMusic(music_volume * music_volume * SDL_MIX_MAXVOLUME);
                    }
                    else if (dragging_scroller_type == DRAGGING_SCROLLER_VOLUME)
                    {
                        volume = std::clamp(p, 0.0, 1.0);
                        if (has_sound)
                            Mix_Volume(-1, volume * volume * SDL_MIX_MAXVOLUME);
                    }
                    else if (dragging_scroller_type == DRAGGING_SCROLLER_RULES)
                    {
                        rule_limit_slider = std::clamp(p, 0.0, 1.0);
                        int new_rule_limit_count = pow(100, 1 + rule_limit_slider * 1.6) / 10;
                        if (rule_limit_slider >= 1.0)
                            new_rule_limit_count = -1;
                        if (new_rule_limit_count < 0 || (rule_limit_count > 0 && new_rule_limit_count > rule_limit_count))
                            pause_robots();
                        rule_limit_count = new_rule_limit_count;
                    }
                }
                break;
            }
            case SDL_MOUSEBUTTONUP:
            {
                display_rules_click_drag = false;
                grid_dragging = false;
                dragging_speed = false;
                dragging_scroller = false;
                mouse_button_pressed = false;
                mouse.x = e.button.x * mouse_scale.x;
                mouse.y = e.button.y * mouse_scale.y;
                break;
            }
            case SDL_MOUSEBUTTONDOWN:
            {
                mouse_button_pressed = true;
                mouse.x = e.button.x * mouse_scale.x;
                mouse.y = e.button.y * mouse_scale.y;
                if (display_about)
                {
                    display_about = false;
                    break;
                }
                if (display_language_chooser)
                {
                    XYPos p = (mouse - left_panel_offset) / button_size;
                    p -= XYPos(2,2);
                    if (p.x >= 0 && p.y >= 0 && p.x < 8)
                    {
                        int want = p.y + (p.x / 4) * 8;
                        int index = 0;
                        for (std::map<std::string, SaveObject*>::iterator it = lang_data->omap.begin(); it != lang_data->omap.end(); ++it)
                        {
                            if (index == want)
                            {
                                set_language(it->first);
                            }
                            index++;
                        }
                        display_language_chooser = false;
                    }
                    break;
                }
                if (display_key_select)
                {
                    XYPos p = (mouse - left_panel_offset) / button_size;
                    p -= XYPos(2,3);
                    if (p.x >= 0 && p.y >= 0 && p.y < 5 && p.x <= 6)
                    {
                        int index = p.y + (p.x / 4) * 5 + key_remap_page_index * 10;

                        if (index < KEY_CODE_TOTAL)
                            capturing_key = index;
                    }
                    if (p == XYPos(2,6))
                        if (key_remap_page_index > 0)
                            key_remap_page_index--;
                    if (p == XYPos(3,6))
                        if (key_remap_page_index < 3)
                            key_remap_page_index++;
                    if (p == XYPos(7,6))
                    {
                        capturing_key = -1;
                        display_key_select = false;
                    }
                    break;
                }
               if (display_reset_confirm)
                {
                    XYPos p = (mouse - left_panel_offset) / button_size;
                    p -= XYPos(2,2);
                    if (p.x >= 0 && p.y >= 0 && p.x < 8)
                    {
                        if (p == XYPos(1, 4))
                        {
                            pause_robots();
                            if (display_reset_confirm_levels_only)
                                reset_levels();
                            else
                            {
                                rules[game_mode].clear();
                                if (game_mode == 2 || game_mode == 3)
                                    reset_levels();
                                force_load_level = true;
                                load_level = true;
                            }
                            display_reset_confirm = false;
                            right_panel_mode = RIGHT_MENU_NONE;
                        }
                        if (p == XYPos(5, 4))
                            display_reset_confirm = false;

                    }
                    break;
                }
                if (display_menu)
                {
                    XYPos p = (mouse - left_panel_offset) / button_size;
                    p -= XYPos(2,2);
                    if (p.x >= 0 && p.x <= 8 && p.y >= 0)
                    {
                        if (p.y == 0)
                        {
                            full_screen = !full_screen;
                            SDL_SetWindowFullscreen(sdl_window, full_screen? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
                            SDL_SetWindowBordered(sdl_window, full_screen ? SDL_FALSE : SDL_TRUE);
                            SDL_SetWindowResizable(sdl_window, full_screen ? SDL_FALSE : SDL_TRUE);
                            SDL_SetWindowInputFocus(sdl_window);
                        }
                        if (p.y == 1)
                            display_language_chooser = true;
                        if (p.y == 2)
                            show_row_clues = !show_row_clues;
                        if (p.y == 3)
                            low_contrast = !low_contrast;
                        if (p.y == 4)
                            display_key_select = true;
                        if (p.y == 5)
                            display_about = true;
                        if (p.y == 6)
                            DisplayWebsite("https://discord.gg/MXHyWsRkeu");
                        if (p.y == 7)
                        {
                            display_reset_confirm = true;
                            display_reset_confirm_levels_only = true;
                        }
                        if (p.y == 8)
                        {
                            display_reset_confirm = true;
                            display_reset_confirm_levels_only = false;
                        }
                        if (p.y == 9)
                            quit = true;
                    }
                    if (p.x == 9 && p.y >= 0 && p.y <= 3)
                    {
                        dragging_scroller = true;
                        dragging_scroller_type = DRAGGING_SCROLLER_VOLUME;
                        double p = 1.0 - double(mouse.y - left_panel_offset.y - (button_size * 3) - (button_size / 6)) / (button_size * 2.6666);
                        volume = std::clamp(p, 0.0, 1.0);
                        if (has_sound)
                            Mix_Volume(-1, volume * volume * SDL_MIX_MAXVOLUME);
                    }
                    if (p.x == 11 && p.y >= 0 && p.y <= 3)
                    {
                        dragging_scroller = true;
                        dragging_scroller_type = DRAGGING_SCROLLER_MUSIC;
                        double p = 1.0 - double(mouse.y - left_panel_offset.y - (button_size * 3) - (button_size / 6)) / (button_size * 2.6666);
                        music_volume = std::clamp(p, 0.0, 1.0);
                        if (has_sound)
                            Mix_VolumeMusic(music_volume * music_volume * SDL_MIX_MAXVOLUME);
                    }
                    if (p.x == 13 && p.y >= 0 && p.y <= 3 && prog_seen[PROG_LOCK_COLORS])
                    {
                        dragging_scroller = true;
                        dragging_scroller_type = DRAGGING_SCROLLER_COLOUR;
                        double p = 1.0 - double(mouse.y - left_panel_offset.y - (button_size * 3) - (button_size / 6)) / (button_size * 2.6666);
                        colors = std::clamp(p, 0.0, 1.0);
                    }
                    if (p == XYPos(13,9))
                        display_menu = false;
                    break;
                }
                if (display_help)
                {
                    int sq_size = std::min(window_size.y / 9, window_size.x / 16);
                    XYPos help_image_size = XYPos(16 * sq_size, 9 * sq_size);
                    XYPos help_image_offset = (window_size - help_image_size) / 2;
                    if ((mouse - help_image_offset - help_image_size + XYPos(sq_size * 3, sq_size)).inside(XYPos(sq_size, sq_size)))
                        if (tutorial_index)
                            tutorial_index--;
                    if ((!walkthrough || (tutorial_index == (tut_page_count - 1))) && (mouse - help_image_offset - help_image_size + XYPos(sq_size * 2, sq_size)).inside(XYPos(sq_size, sq_size)))
                        display_help = false;
                    if ((mouse - help_image_offset - help_image_size + XYPos(sq_size * 1, sq_size)).inside(XYPos(sq_size, sq_size)))
                        if (tutorial_index < (tut_page_count - 1))
                            tutorial_index++;
                    break;
                }
                if (display_modes)
                {
                    XYPos p = (mouse - left_panel_offset) / button_size;
                    p -= XYPos(2,2);
                    if (p.x >= 0 && p.y >= 0 && p.x < 5 && p.y < GAME_MODES)
                    {
                        int index = p.y;
                        if (index != game_mode)
                        {
                            pause_robots();
                            current_level_group_index = 0;
                            current_level_set_index = 0;
                            current_level_index = 0;
                            load_level = true;
                            force_load_level = true;
                            region_type = RegionType(RegionType::SET, 0);
                            select_region_type = RegionType(RegionType::EQUAL, 0);
                            game_mode = index;
                        }
                        display_modes = false;
                    }
                    break;
                }

                if(e.button.button == SDL_BUTTON_X1)
                {
                    rule_gen_undo();
                    break;
                }
                if(e.button.button == SDL_BUTTON_X2)
                {
                    rule_gen_redo();
                    break;
                }

                int btn = 0;
                if(e.button.button == SDL_BUTTON_RIGHT)
                {
                    btn = 2;
                }
                if (walkthrough && !display_help)
                {
                    XYPos p = mouse - walkthrough_region.pos - walkthrough_region.size / 2;
                    if ((XYPosFloat(p).distance()  < XYPosFloat(walkthrough_region.size).distance() / 2) && ((e.button.clicks > 1) || !walkthrough_double_click))
                    {
                        walkthrough_step++;
                        if (walkthrough_step >= 4)
                            walkthrough = false;
                    }
                    else
                        break;
                }


                if ((mouse - left_panel_offset).inside(panel_size))
                {
                    left_panel_click(mouse - left_panel_offset, e.button.clicks, btn);
                }
                else if ((mouse - right_panel_offset).inside(panel_size))
                {
                    right_panel_click(mouse - right_panel_offset, e.button.clicks, btn);
                }
                else if ((mouse - left_panel_offset - XYPos(panel_size.x, 0)).inside(XYPos(panel_size.y, panel_size.y)))
                {
                    grid_click(mouse, e.button.clicks, btn);
                }
                break;
            }

            case SDL_MOUSEWHEEL:
            {
                if (display_rules || display_scores || display_levels)
                {
                    rules_list_offset -= e.wheel.y;
                    break;
                }
                if (e.wheel.y > 0)
                    target_grid_zoom *= 1.1;
                else
                    target_grid_zoom /= 1.1;
                break;
            }

            case SDL_RENDER_TARGETS_RESET:
            {
                break;
            }
            default:
            {
                printf("event:0x%x\n", e.type);
                break;
            }
        }
    }

    if (right_panel_mode == RIGHT_MENU_REGION && key_held)
    {
        if (key_held == 'Q')
            inspected_region->vis_level = GRID_VIS_LEVEL_SHOW;
        else if (key_held == 'W')
            inspected_region->vis_level = GRID_VIS_LEVEL_HIDE;
        else if (key_held == 'E')
            inspected_region->vis_level = GRID_VIS_LEVEL_BIN;
        if (!ctrl_held)
            inspected_region->visibility_force = GridRegion::VIS_FORCE_USER;
        inspected_region->vis_cause.rule = NULL;
        inspected_region->stale = false;
    }


    return quit;
}

void GameState::deal_with_scores()
{
    if (scores_from_server.done)
    {
        scores_from_server.done = false;
        if (!scores_from_server.error)
        {
            try 
            {
                SaveObjectMap* omap = scores_from_server.resp->get_map();
                if (omap->has_key("pirate"))
                {
                    if (pirate)
                        server_timeout = 1000 * 60 * 60;
                    pirate = true;
                    steam_session_string = "";
                }
                SaveObjectList* lvls = omap->get_item("scores")->get_list();
                for (int i = 0; i < GLBAL_LEVEL_SETS + 2; i++)
                {
                    SaveObjectList* scores = lvls->get_item(i)->get_list();
                    int mode = omap->get_num("game_mode");
                    score_tables[mode][i].clear();
                    for (unsigned j = 0; j < scores->get_count(); j++)
                    {
                        SaveObjectMap* score = scores->get_item(j)->get_map();
                        unsigned is_friend = 0;
                        if (score->has_key("friend"))
                            is_friend = score->get_num("friend");
                        unsigned hidden = 0;
                        if (score->has_key("hidden"))
                            hidden = score->get_num("hidden");
                        score_tables[mode][i].push_back(PlayerScore(unsigned(score->get_num("pos")), score->get_string("name"), unsigned(score->get_num("score")), is_friend, hidden));
                    }
                }
                lvls = omap->get_item("stats")->get_list();
                for (unsigned i = 0; i <= GLBAL_LEVEL_SETS; i++)
                {
                    SaveObjectList* stats1 = lvls->get_item(i)->get_list();
                    for (unsigned j = 0; j < stats1->get_count() && j < level_progress[game_mode][i].size(); j++)
                    {
                        SaveObjectList* stats2 = stats1->get_item(j)->get_list();
                        for (unsigned k = 0; k < stats2->get_count() && k < level_progress[game_mode][i][j].level_status.size(); k++)
                        {
                            int64_t s = stats2->get_item(k)->get_num();
                            level_progress[game_mode][i][j].level_status[k].stats = s;
                        }
                    }
                }
                if (omap->has_key("level_gen_req"))
                {
                    if ((level_gen_req == "") && (SDL_TryLockMutex(level_gen_mutex) == 0))
                    {
                        if (level_gen_thread)
                            SDL_WaitThread(level_gen_thread, NULL);
                        level_gen_req = omap->get_string("level_gen_req");
                        level_gen_resp = "";
                        level_gen_thread = SDL_CreateThread(level_gen_thread_func, "GenerateLevel", (void *)this);
                        SDL_UnlockMutex(level_gen_mutex);
                    }
                }
                if (omap->has_key("server_levels"))
                {
                    server_level_anim = 0;
                    server_levels_version = omap->get_num("server_levels_version");
                    SaveObjectList* lvl_sets = omap->get_item("server_levels")->get_list();
                    server_levels.clear();
                    server_levels.resize(lvl_sets->get_count());
                    for (unsigned m = 0; m < GAME_MODES; m++)
                    {
                        level_progress[m][GLBAL_LEVEL_SETS].clear();
                        level_progress[m][GLBAL_LEVEL_SETS].resize(lvl_sets->get_count());
                    }
                    for (unsigned k = 0; k < lvl_sets->get_count(); k++)
                    {
                        SaveObjectList* plist = lvl_sets->get_item(k)->get_list();
                        for (int m = 0; m < GAME_MODES; m++)
                        {
                            level_progress[m][GLBAL_LEVEL_SETS][k].level_status.resize(plist->get_count());
                            level_progress[m][GLBAL_LEVEL_SETS][k].count_todo = plist->get_count();
                        }

                        for (unsigned i = 0; i < plist->get_count(); i++)
                        {
                            std::string s = plist->get_string(i);
                            server_levels[k].push_back(s);
                        }
                    }
                    if (current_level_group_index == GLBAL_LEVEL_SETS)
                        current_level_is_temp = true;
                }
            }
            catch (const std::runtime_error& error)
            {
                std::cerr << error.what() << "\n";
            }
            if (scores_from_server.resp)
                delete scores_from_server.resp;
            scores_from_server.resp = NULL;
        }
        else
            server_timeout = 1000;
    }
}

void GameState::export_all_rules_to_clipboard()
{
    SaveObjectMap* omap = new SaveObjectMap;
    SaveObjectList* rlist = new SaveObjectList;
    for (GridRule& rule : rules[game_mode])
    {
        if (!rule.deleted)
            rlist->add_item(rule.save(true));
    }
    omap->add_item("rules", rlist);
    std::string title = "Rules (" + std::to_string(rlist->get_count()) + ")";
    send_to_clipboard(title, omap);
    delete omap;
}

void GameState::send_to_clipboard(std::string title, SaveObject* obj)
{
    std::ostringstream stream;
    obj->save(stream);
    std::string str = stream.str();

    std::string reply = "Bombe " + title;
    reply += ":\n";
    if (ctrl_held)
    {
        reply += str;
    }
    else
    {
        std::string comp = compress_string_zstd(str);
        std::u32string s32;
        s32 += 0x1F4A3;                 // unicode Bomb 
        unsigned spaces = 2;
        for(char& c : comp)
        {
            if (spaces >= 80)
            {
                s32 += '\n';
                spaces = 0;
            }
            spaces++;
            s32 += uint32_t(0x2800 + (unsigned char)(c));

        } 
        s32 += 0x1F6D1;                 // stop sign
        std::wstring_convert<std::codecvt_utf8<char32_t>, char32_t> conv;
        reply += conv.to_bytes(s32);
    }
    reply += "\n";
    SDL_SetClipboardText(reply.c_str());
}

void GameState::send_rule_to_img_clipboard(GridRule& rule)
{
    SaveObjectMap* omap = new SaveObjectMap;
    omap->add_item("rule", rule.save(true));
    std::ostringstream stream;
    omap->save(stream);
    delete omap;
    std::string comp = compress_string(stream.str());

    XYPos siz;
    if (rule.region_count == 1)
        siz = XYPos(100, 200);
    else if (rule.region_count == 2)
        siz = XYPos(300, 208);
    else if (rule.region_count == 3)
        siz = XYPos(500, 300);
    else
        siz = XYPos(508, 500);

    SDL_Texture* my_canvas = SDL_CreateTexture(sdl_renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, siz.x, siz.y);
    SDL_SetTextureBlendMode(my_canvas, SDL_BLENDMODE_BLEND);
    SDL_SetRenderTarget(sdl_renderer, my_canvas);
    SDL_RenderClear(sdl_renderer);

    render_rule(rule, XYPos(0, 0), 100, -1);

    uint32_t* pixel_data = new uint32_t[siz.x * siz.y];
    SDL_Rect dst_rect = {0, 0, siz.x, siz.y};
    SDL_RenderReadPixels(sdl_renderer, &dst_rect, SDL_PIXELFORMAT_BGRA8888, (void*)pixel_data, siz.x * 4);

    uint32_t comp_size = comp.size();
    uint32_t comp_size2 = comp_size ^ 0x55555555;

    std::string siz_str = std::string(1, char(comp_size)) + std::string(1, char(comp_size>>8)) + std::string(1, char(comp_size>>16)) + std::string(1, char(comp_size>>24));
    std::string siz_str2 = std::string(1, char(comp_size2)) + std::string(1, char(comp_size2>>8)) + std::string(1, char(comp_size2>>16)) + std::string(1, char(comp_size2>>24));
    comp = siz_str + siz_str2 + comp;
    int offset = 32;
    comp_size = comp.size();
    for (unsigned i = 0; i < comp_size; i++)
    {
        uint8_t c = comp[i];
        for (int j = 0; j < 3; j++)
        {
            for(int sub = 0; sub < 3; sub++)
            {
                pixel_data[offset] = pixel_data[offset] & ~(7 << ((sub + 1) * 8));
                if (c & 1)
                    pixel_data[offset] = pixel_data[offset] | (0x6 << ((sub + 1) * 8));
                else
                    pixel_data[offset] = pixel_data[offset] | (0x2 << ((sub + 1) * 8));
                c >>= 1;
            }
            offset++;
        }
    }

    ImgClipBoard::send(pixel_data, XYPos(siz.x, siz.y));

    SDL_DestroyTexture(my_canvas);
    SDL_SetRenderTarget(sdl_renderer, NULL);
    delete[] pixel_data;
}

static uint32_t get_hidden_val(uint32_t** dat, unsigned roff, unsigned goff, unsigned boff)
{
    uint8_t bitpos = 1;
    uint32_t val = 0;
    for (int j = 0; j < 3; j++)
    {
        if ((**dat >> (roff + 2)) & 1) val |= bitpos;
        bitpos <<= 1;
        if ((**dat >> (goff + 2)) & 1) val |= bitpos;
        bitpos <<= 1;
        if ((**dat >> (boff + 2)) & 1) val |= bitpos;
        bitpos <<= 1;
        (*dat)++;
    }
    return val;
}

void GameState::check_clipboard()
{
    std::string comp;
    std::string new_value;
    std::vector<uint32_t> pix_dat;
    XYPos siz = ImgClipBoard::recieve(pix_dat);
    if (siz.x >= 100 && siz.y >= 200)
    {
        uint32_t* dat = &pix_dat[0];
        dat += 32;
        uint32_t comp_size = get_hidden_val(&dat, 8, 16, 24);
        comp_size += uint32_t(get_hidden_val(&dat, 8, 16, 24)) << 8;
        comp_size += uint32_t(get_hidden_val(&dat, 8, 16, 24)) << 16;
        comp_size += uint32_t(get_hidden_val(&dat, 8, 16, 24)) << 24;

        uint32_t comp_size2 = get_hidden_val(&dat, 8, 16, 24);
        comp_size2 += uint32_t(get_hidden_val(&dat, 8, 16, 24)) << 8;
        comp_size2 += uint32_t(get_hidden_val(&dat, 8, 16, 24)) << 16;
        comp_size2 += uint32_t(get_hidden_val(&dat, 8, 16, 24)) << 24;
        if ((comp_size == (comp_size2 ^ 0x55555555)) && ((int)comp_size < (siz.x * siz.y / 3)))
        {
            for (unsigned i = 0; i < comp_size; i++)
            {
                comp += get_hidden_val(&dat, 8, 16, 24);
            }
        }
    }
    else
    {
        if (!SDL_HasClipboardText())
        {
            clipboard_has_item = CLIPBOARD_HAS_NONE;
            return;
        }
        char* new_clip = SDL_GetClipboardText();
        new_value = new_clip;
        SDL_free(new_clip);
        if (new_value == clipboard_last)
            return;
        clipboard_last = new_value;
        std::wstring_convert<std::codecvt_utf8<char32_t>, char32_t> conv;
        std::u32string s32 = conv.from_bytes(std::string(new_value));
        for(uint32_t c : s32)
        {
            if ((c & 0xFF00) == 0x2800)
            {
                char asc = c & 0xFF;
                comp += asc;
            }
        }
    }

    clipboard_has_item = CLIPBOARD_HAS_NONE;
    SaveObjectMap* omap = NULL;
    try 
    {
        std::string decomp;
        if (!comp.empty())
        {
            decomp = decompress_string(comp);
        }
        else
        {
            decomp = new_value;
            while (!decomp.empty() && decomp[0] != '{')
                decomp.erase(0, 1);
        }
        if (!decomp.empty())
        {
            std::istringstream decomp_stream(decomp);
            omap = SaveObject::load(decomp_stream)->get_map();
            if (omap->has_key("rule"))
            {
                clipboard_rule = GridRule(omap->get_item("rule"));
                clipboard_has_item = CLIPBOARD_HAS_RULE;
            }
            if (omap->has_key("rules"))
            {
                clipboard_rule_set.clear();
                SaveObjectList* rlist = omap->get_item("rules")->get_list();
                for (unsigned i = 0; i < rlist->get_count(); i++)
                {
                    GridRule r(rlist->get_item(i));
                    clipboard_rule_set.push_back(r);
                }
                clipboard_has_item = CLIPBOARD_HAS_RULE_SET;
            }
            if (omap->has_key("level"))
            {
                clipboard_level = omap->get_string("level");
                clipboard_has_item = CLIPBOARD_HAS_LEVEL;
            }
        }
    }
    catch (const std::runtime_error& error)
    {
        std::cerr << error.what() << "\n";
    }
    delete omap;

}

void GameState::import_all_rules()
{
    for (GridRule& new_rule : clipboard_rule_set)
    {
        bool seen = false;
        std::vector<int> order;
        for(int i = 0; i < new_rule.region_count; i++)
            order.push_back(i);
        do{

            GridRule prule = new_rule.permute(order);
            for (GridRule& rule : rules[game_mode])
            {
                if (!rule.deleted && rule.covers(prule))
                {
                    seen = true;
                    break;
                }
            }
            if (seen)
                break;
        }
        while(std::next_permutation(order.begin(),order.end()));
        if (!seen && rule_is_permitted(new_rule, game_mode))
        {
            rules[game_mode].push_back(new_rule);
        }
    }
}

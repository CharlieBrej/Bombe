#define _USE_MATH_DEFINES
#include <cmath>

#include "GameState.h"
#include "SaveState.h"
#include "Misc.h"
#include "LevelSet.h"
#include "Compress.h"
#include "clip/clip.h"

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

GameState::GameState(std::string& load_data, bool json)
{
    global_mutex = SDL_CreateMutex();
    level_gen_mutex = SDL_CreateMutex();
    LevelSet::init_global();
    bool load_was_good = false;

    for (int k = 0; k < GAME_MODES; k++)
    for (int j = 0; j < GLBAL_LEVEL_SETS; j++)
    {
        level_progress[k][j].resize(global_level_sets[j].size());
        for (int i = 0; i < global_level_sets[j].size(); i++)
        {
            level_progress[k][j][i].level_status.resize(global_level_sets[j][i]->levels.size());
            level_progress[k][j][i].count_todo = global_level_sets[j][i]->levels.size();
            level_progress[k][j][i].level_stats.resize(global_level_sets[j][i]->levels.size());
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
            if (omap->has_key("game_mode"))
                game_mode = omap->get_num("game_mode");
            if (omap->has_key("full_screen"))
                full_screen = omap->get_num("full_screen");
            if (omap->has_key("max_stars"))
                max_stars = omap->get_num("max_stars");
            if (omap->has_key("server_levels"))
            {
                SaveObjectList* lvl_sets = omap->get_item("server_levels")->get_list();
                server_levels.resize(lvl_sets->get_count());
                for (int m = 0; m < GAME_MODES; m++)
                    level_progress[m][GLBAL_LEVEL_SETS].resize(lvl_sets->get_count());
                for (int k = 0; k < lvl_sets->get_count(); k++)
                {
                    SaveObjectList* plist = lvl_sets->get_item(k)->get_list();
                    server_levels[k].clear();
                    for (int m = 0; m < GAME_MODES; m++)
                    {
                        level_progress[m][GLBAL_LEVEL_SETS][k].level_status.resize(plist->get_count());
                        level_progress[m][GLBAL_LEVEL_SETS][k].count_todo = plist->get_count();
                        level_progress[m][GLBAL_LEVEL_SETS][k].level_stats.resize(plist->get_count());
                    }
                    for (int i = 0; i < plist->get_count(); i++)
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
                for (int k = 0; k < dlist->get_count() && k < GAME_MODES; k++)
                    rule_del_count[k] = dlist->get_item(k)->get_num();
            }

            if (omap->has_key("key_codes"))
            {
                SaveObjectList* dlist = omap->get_item("key_codes")->get_list();
                for (int k = 0; k < dlist->get_count() && k < KEY_CODE_TOTAL; k++)
                    key_codes[k] = dlist->get_item(k)->get_num();
            }

            std::list<SaveObjectMap*> modes;
            if (omap->has_key("modes"))
            {
                SaveObjectList* mode_lists = omap->get_item("modes")->get_list();
                for (int k = 0; k < mode_lists->get_count(); k++)
                    modes.push_back(mode_lists->get_item(k)->get_map());
            }
            else
                modes.push_back(omap);
            int mode = 0;
            for (SaveObjectMap* omap : modes)
            {
                SaveObjectList* rlist = omap->get_item("rules")->get_list();
                for (int i = 0; i < rlist->get_count(); i++)
                {
                    GridRule r(rlist->get_item(i));
                    if (rule_is_permitted(r, mode))
                        rules[mode].push_back(r);
                }

                {
                    SaveObjectList* pplist = omap->get_item("level_progress")->get_list();
                    for (int k = 0; k <= GLBAL_LEVEL_SETS && k < pplist->get_count(); k++)
                    {
                        SaveObjectList* plist = pplist->get_item(k)->get_list();
                        for (int i = 0; i < plist->get_count() && i < level_progress[mode][k].size(); i++)
                        {
                            std::string s = plist->get_string(i);
                            int lim = std::min(s.size(), level_progress[mode][k][i].level_status.size());
                            for (int j = 0; j < lim; j++)
                            {
                                char c = s[j];
                                int stat = c - '0';
                                level_progress[mode][k][i].level_status[j] = stat;
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
            load_was_good = true;
        }
    }
    catch (const std::runtime_error& error)
    {
        std::cerr << error.what() << "\n";
    }

    if (!load_was_good)
    {
        display_help = true;
        display_language_chooser = true;
    }

    sdl_window = SDL_CreateWindow( "Bombe", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1920/2, 1080/2, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | (full_screen? SDL_WINDOW_FULLSCREEN_DESKTOP  | SDL_WINDOW_BORDERLESS : 0));
    sdl_renderer = SDL_CreateRenderer(sdl_window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (full_screen)
    {
        SDL_SetWindowFullscreen(sdl_window, full_screen? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
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
        fonts[filename] = TTF_OpenFont(filename.c_str(), 32);
    }
    set_language(language);
    score_font = TTF_OpenFont("font-fixed.ttf", 19*4);

    assert(Mix_OpenAudio(48000, MIX_DEFAULT_FORMAT, 2, 2048) == 0);
    assert(Mix_AllocateChannels(32) == 32);

    sounds[0] = Mix_LoadWAV( "snd/plop0.wav" );
    sounds[1] = Mix_LoadWAV( "snd/plop1.wav" );
    sounds[2] = Mix_LoadWAV( "snd/plop2.wav" );
    sounds[3] = Mix_LoadWAV( "snd/plop3.wav" );
    sounds[4] = Mix_LoadWAV( "snd/plop4.wav" );
    sounds[5] = Mix_LoadWAV( "snd/plop5.wav" );
    sounds[6] = Mix_LoadWAV( "snd/plop6.wav" );
    sounds[7] = Mix_LoadWAV( "snd/plop7.wav" );
    assert(sounds[0]);
    assert(sounds[1]);
    assert(sounds[2]);
    assert(sounds[3]);
    assert(sounds[4]);
    assert(sounds[5]);
    assert(sounds[6]);
    assert(sounds[7]);
    Mix_Volume(-1, volume * volume * SDL_MIX_MAXVOLUME);

    grid = Grid::Load("ABBA!");

    prog_stars[PROG_LOCK_HEX] = 0;
    prog_stars[PROG_LOCK_SQUARE] = 500;
    prog_stars[PROG_LOCK_TRIANGLE] = 5000;
    prog_stars[PROG_LOCK_GRID] = 10000;
    prog_stars[PROG_LOCK_SERVER] = 12000;
    prog_stars[PROG_LOCK_NUMBER_TYPES] = 100;
    prog_stars[PROG_LOCK_VISIBILITY] = 2000;
    prog_stars[PROG_LOCK_VISIBILITY2] = 2000;
    prog_stars[PROG_LOCK_VISIBILITY3] = 2000;
    prog_stars[PROG_LOCK_VISIBILITY4] = 2000;
    prog_stars[PROG_LOCK_GAME_MODE] = 13000;
    prog_stars[PROG_LOCK_VARS1] = 6000;
    prog_stars[PROG_LOCK_VARS2] = 10000;
    prog_stars[PROG_LOCK_VARS3] = 14000;
    prog_stars[PROG_LOCK_VARS4] = 18000;
    prog_stars[PROG_LOCK_VARS5] = 22000;
    // prog_stars[PROG_LOCK_VARS3] = 10000;
    // prog_stars[PROG_LOCK_VARS4] = 10000;
    // prog_stars[PROG_LOCK_VARS5] = 10000;
    prog_stars[PROG_LOCK_FILTER] = 7000;
    prog_stars[PROG_LOCK_PRIORITY] = 16000;
    prog_stars[PROG_LOCK_PRIORITY2] = 16000;

    for (int i = 0; i < PROG_LOCK_TOTAL; i++)
        if (prog_stars[i] <= max_stars)
            prog_seen[i] = PROG_ANIM_MAX;
    if (!prog_seen[PROG_LOCK_GAME_MODE])
        game_mode = 0;
    clip::set_error_handler(NULL);
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
                for (bool stat : prog.level_status)
                {
                    char c = '0' + stat;
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
//    Mix_FreeMusic(music);
    delete grid;
    delete lang_data;
    for (const auto& [key, value] : fonts)
        TTF_CloseFont(value);
    fonts.clear();
    TTF_CloseFont(score_font);

    for (int i = 0; i < 8; i++)
        Mix_FreeChunk(sounds[i]);

    SDL_DestroyTexture(sdl_texture);
    for (int i = 0; i < tut_texture_count; i++)
        SDL_DestroyTexture(tutorial_texture[i]);
    SDL_DestroyRenderer(sdl_renderer);
    SDL_DestroyWindow(sdl_window);
    LevelSet::delete_global();
    if (level_gen_thread)
    {
        SHUTDOWN = true;
        SDL_WaitThread(level_gen_thread, NULL);
        level_gen_thread = NULL;
    }
    Z3_finalize_memory();
}
void GameState::reset_levels()
{
    rule_del_count[game_mode] = 0;
    current_level_group_index = 0;
    current_level_set_index = 0;
    current_level_index = 0;
    load_level = true;
    force_load_level = false;

    for (GridRule& rule : rules[game_mode])
    {
        rule.used_count = 0;
        rule.clear_count = 0;
        rule.level_used_count = 0;
        rule.level_clear_count = 0;
    }
    for (int j = 0; j < GLBAL_LEVEL_SETS; j++)
    {
        level_progress[game_mode][j].resize(global_level_sets[j].size());
        for (int i = 0; i < global_level_sets[j].size(); i++)
        {
            level_progress[game_mode][j][i].level_status.clear();
            level_progress[game_mode][j][i].level_status.resize(global_level_sets[j][i]->levels.size());
            level_progress[game_mode][j][i].count_todo = global_level_sets[j][i]->levels.size();
            level_progress[game_mode][j][i].star_anim_prog = 0;
            level_progress[game_mode][j][i].unlock_anim_prog = 0;
        }
    }
    for (int i = 0; i < server_levels.size(); i++)
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

    // (hex/sqr/tri)(x)(y)(wrap)(merged)(rows)(+-)(x_y)(x_y_z)(exc)
    //  0            1  2  3     4       5    6    7    8      9
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

    g->randomize(siz, Grid::WrapType(wrap), merged, rows * 10);
    g->make_harder(pm, xy, xy3, xyz, exc);
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
            int got = SDLNet_TCP_Recv(tcpsock, (char*)&length, 4);
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
    SaveObjectList* plist = new SaveObjectList;

    SaveObjectList* pplist = new SaveObjectList;
    for (int j = 0; j <= GLBAL_LEVEL_SETS; j++)
    {
        SaveObjectList* plist = new SaveObjectList;
        for (LevelProgress& prog : level_progress[game_mode][j])
        {
            std::string sstr;
            for (bool stat : prog.level_status)
            {
                char c = '0' + stat;
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
    GridRule why;
//    assert(rule.is_legal(why) == GridRule::OK);
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
        for (GridRule& rule : rules[game_mode])
            if (!rule.deleted && rule.apply_region_type.type != RegionType::VISIBILITY)
                rule_cnt++;
        rule_cnt += rule_del_count[game_mode];
        if (game_mode == 2 && rule_cnt >= 60)
            return false;
        if (game_mode == 3 && rule_cnt >= 300)
            return false;
    }
    return true;
}

void GameState::advance(int steps)
{
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
        int count = 0;
        for (int i = 0; i < level_progress[0][s].size(); i++)
        {
            LevelProgress& prog = level_progress[0][s][i];
            for (bool b : prog.level_status)
                if (b)
                    count++;
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
    }

    cur_stars = totcount;
    if (max_stars < totcount)
        max_stars = totcount;

    if (!(load_level || skip_level) && !force_load_level && grid->is_solved() && !current_level_is_temp)
    {
        if (!level_progress[game_mode][current_level_group_index][current_level_set_index].level_status[current_level_index])
        {
            // uint64_t prev = 0;
            // for (int i = 0; i < level_progress[game_mode][current_level_group_index].size(); i++)
            //     if (level_is_accessible(i))
            //         prev |= 1 << i;

            level_progress[game_mode][current_level_group_index][current_level_set_index].count_todo--;
            level_progress[game_mode][current_level_group_index][current_level_set_index].level_status[current_level_index] = true;
            skip_level = 1;
            // if (level_progress[game_mode][current_level_group_index][current_level_set_index].count_todo == 0)
            // {
            //     XYPos pos = left_panel_offset + XYPos(button_size * (current_level_set_index % 5), button_size * (current_level_set_index / 5 + 6));
            //     star_burst_animations.push_back(AnimationStarBurst(pos, XYPos(button_size,button_size), 0, false));
            // }
            // for (int i = 0; i < level_progress[game_mode][current_level_group_index].size(); i++)
            // {
            //     if (!((prev >> i) & 1) && level_is_accessible(i))
            //     {
            //         XYPos pos = left_panel_offset + XYPos(button_size * (i % 5), button_size * (i / 5 + 6));
            //         star_burst_animations.push_back(AnimationStarBurst(pos, XYPos(button_size,button_size), 0, true));
            //     }
            // }
        }
    }

    if (load_level || skip_level)
    {
        clear_overlay();
        clue_solves.clear();
        if (force_load_level || level_progress[game_mode][current_level_group_index][current_level_set_index].count_todo)
        {
            if (!force_load_level)
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
                while (level_progress[game_mode][current_level_group_index][current_level_set_index].level_status[current_level_index]);

            std::string& s = (current_level_group_index == GLBAL_LEVEL_SETS) ?
                        server_levels[current_level_set_index][current_level_index] :
                        global_level_sets[current_level_group_index][current_level_set_index]->levels[current_level_index];
            delete grid;
            grid = Grid::Load(s);
            reset_rule_gen_region();
            inspected_rule.regions[0] = NULL;
            inspected_rule.regions[1] = NULL;
            inspected_rule.regions[2] = NULL;
            inspected_rule.regions[3] = NULL;
            grid_cells_animation.clear();
            grid_regions_animation.clear();
            grid_regions_fade.clear();
            current_level_is_temp = false;
            grid_zoom = 1;
            target_grid_zoom = 1;
            scaled_grid_offset = XYPos(0,0);
            scaled_grid_size = grid_size;
            for (GridRule& rule : rules[game_mode])
            {
                rule.used_count += rule.level_used_count;
                rule.clear_count += rule.level_clear_count;
                rule.level_used_count = 0;
                rule.level_clear_count = 0;
            }
            display_levels_center_current = true;
        }
        else
            auto_progress = false;
        skip_level = 0;
        load_level = false;
    }

    if (auto_progress_all && !auto_progress)
    {
        while (true)
        {
            current_level_set_index++;
            if (level_progress[game_mode][current_level_group_index].size() <= current_level_set_index)
            {
                current_level_set_index = 0;
                current_level_index = 0;
                auto_progress_all = false;
                auto_progress = false;
                load_level = true;
                break;
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
    bool cleared_cell = false;

    for (GridRule& rule : rules[game_mode])
    {
        if (rule.deleted)
            continue;
        if (rule.stale)
            continue;
        if (rule.priority < -100)
            continue;
        Grid::ApplyRuleResp resp  = grid->apply_rule(rule, (GridRegion*) NULL);
        if (resp == Grid::APPLY_RULE_RESP_HIT)
            return;
        if (resp == Grid::APPLY_RULE_RESP_NONE)
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


        bool hit = false;
        bool rpt = true;
        while (rpt)
        {
            rpt = false;
            for (GridRegion& region : grid->regions)
            {
                if (!region.stale && region.priority > -100)
                {
                    for (GridRule& rule : rules[game_mode])
                    {
                        if (rule.deleted)
                            continue;
                        if (rule.apply_region_type.type != RegionType::SET)
                            continue;

                        while (true)
                        {
                            Grid::ApplyRuleResp resp  = grid->apply_rule(rule, &region);
                            if (resp == Grid::APPLY_RULE_RESP_HIT)
                            {
                                hit = true;
                                rpt = true;
                                break;
                            }
                            if (resp == Grid::APPLY_RULE_RESP_ERROR)
                                assert(0);
                            if (resp == Grid::APPLY_RULE_RESP_NONE)
                            {
                                break;
                            }
                        }
                        if (rpt)
                            break;
                    }
                    if (rpt)
                        break;
                }
            }
        }
        if (hit)
        {
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
                        for (GridRule& rule : rules[game_mode])
                        {
                            if (rule.deleted)
                                continue;
                            if (rule.apply_region_type.type == RegionType::VISIBILITY && rule.apply_region_type.value == i)
                            {
                                grid->apply_rule(rule, &r);
                            }
                        }
                    }
                    if ((r.vis_level != prev) && (prev == GRID_VIS_LEVEL_BIN))
                        r.stale = false;
                }
            }
            if (sound_frame_index > 50)
            {
                assert(Mix_PlayChannel(rnd % 32, sounds[rnd % 8], 0) != -1);
                sound_frame_index -= 50;
            }
            continue;
        }

        while (grid->add_regions(-1)) {}
        for (GridRegion& region : grid->regions)
        {
            if (!region.stale)
            {
                if (region.vis_level == GRID_VIS_LEVEL_BIN)
                        continue;
                for (GridRule& rule : rules[game_mode])
                {
                    if (rule.deleted)
                        continue;
                    if (rule.apply_region_type.type == RegionType::VISIBILITY)
                        continue;
                    if (rule.apply_region_type.type == RegionType::SET)
                        continue;

                    while (true)
                    {
                        Grid::ApplyRuleResp resp  = grid->apply_rule(rule, &region);
                        if (resp == Grid::APPLY_RULE_RESP_HIT)
                        {
                            //grid->apply_rule(rule, region);
                            hit = true;
                            break;
                        }
                        if (resp == Grid::APPLY_RULE_RESP_ERROR)
                            assert(0);
                        if (resp == Grid::APPLY_RULE_RESP_NONE)
                        {
                            rule.stale = true;
                            break;
                        }
                    }
                }
            }
        }
        for (GridRegion& r : grid->regions)
            r.stale = true;
        
        if (grid->add_one_new_region(right_panel_mode == RIGHT_MENU_REGION ? inspected_region : NULL))
        {
            for (GridRegion& region : grid->regions)
            {
                if (!region.stale)
                {
                    for (int i = 1; i < 3; i++)
                    {
                        for (GridRule& rule : rules[game_mode])
                        {
                            if (rule.deleted)
                                continue;
                            if (region.priority < -100)
                                continue;
                            if (rule.apply_region_type.type == RegionType::VISIBILITY && rule.apply_region_type.value == i)
                            {
                                Grid::ApplyRuleResp resp  = grid->apply_rule(rule, &region);
                                if (resp == Grid::APPLY_RULE_RESP_NONE)
                                    rule.stale = true;
                            }
                        }
                    }
                }
            }
            clue_solves.clear();
        }
        else
        {
            if (auto_progress && !grid->is_solved())
                skip_level = 1;
        }
    }
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
    if (!constructed_rule.region_count)
        right_panel_mode = RIGHT_MENU_NONE;
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
//    rule_gen_region_count = 0;
//    rule_gen_region_undef_num = 0;
    constructed_rule.apply_region_bitmap = 0;
    constructed_rule.import_rule_gen_regions(rule_gen_region[0], rule_gen_region[1], rule_gen_region[2], rule_gen_region[3]);
    constructed_rule_undo.clear();
    constructed_rule_redo.clear();
    update_constructed_rule();
    if (right_panel_mode == RIGHT_MENU_RULE_GEN || right_panel_mode == RIGHT_MENU_REGION)
        right_panel_mode = RIGHT_MENU_NONE;
    filter_pos.clear();
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
                    mouse_hover_region = &region;
                    if (!grid_dragging)
                        mouse_cursor = SDL_SYSTEM_CURSOR_HAND;
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

void GameState::render_text_box(XYPos pos, std::string& s, bool left)
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

    render_box(box_pos, box_size, button_size / 4, 1);
    XYPos txt_pos = box_pos + XYPos(border * 2, border);

    for (int i = 0; i < textures.size(); i++)
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
        XYPos p = XYPos(320 + style * 96, 416);
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


static void get_char_texture(char c, int& pos, int &width)
{
    if (c == '0')
    {
        pos = 0; width = 2;
    }
    else if (c == '1')
    {
        pos = 2; width = 1;
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
}

void GameState::render_number_string(std::string digits, XYPos pos, XYPos siz, XYPos style)
{
    int width = 0;

    for (int i = 0; i < digits.size(); i++)
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
        get_char_texture(c, p, w);
        width += w;
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
    if (s < 1)
        return;
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


    for (int i = 0; i < digits.size(); i++)
    {
        char c = digits[i];
        int p;
        int w;
        get_char_texture(c, p, w);
        
        SDL_Rect src_rect = {p * texture_char_width, texture_char_pos, w * texture_char_width, 3 * texture_char_width};
        SDL_Rect dst_rect = {pos.x, pos.y, int(w * cs), int(3 * cs)};

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
    if (reg.type == RegionType::XOR22)
    {
        XYPos numsiz = XYPos(siz * 0.9 * 2 / 8, siz * 0.9 * 3 / 8);
        render_number_string(reg.val_as_str(0),     pos + XYPos(int(siz) / 8,int(siz) / 8), numsiz);
        render_number_string(reg.val_as_str(2), pos - (numsiz / 2) + XYPos(int(siz) * 4 / 8,int(siz) * 4 / 8), numsiz);
        render_number_string(reg.val_as_str(4), pos - numsiz + XYPos(int(siz) * 7 / 8,int(siz) * 7 / 8), numsiz);
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
        SDL_Rect src_rect = {1344, 384, 192, 192};
        SDL_Rect dst_rect = {pos.x + int(siz) / 8, pos.y + int(siz) / 8,  int(siz * 6 / 8), int(siz * 6 / 8)};
        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
    }
    else if (reg.type == RegionType::XOR2 || reg.type == RegionType::XOR3)
    {
        XYPos numsiz = XYPos(siz * 2 * 1.35 / 8, siz * 3 * 1.35 / 8);

        render_number_string(reg.val_as_str(0), pos + XYPos(int(siz) / 8,int(siz) / 8), numsiz);
        render_number_string(reg.val_as_str(reg.type == RegionType::XOR3 ? 3 : 2), pos - numsiz + XYPos(int(siz) * 7 / 8,int(siz) * 7 / 8), numsiz);

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
        SDL_Rect src_rect = {928, 192, 128, 192};
        SDL_Rect dst_rect = {pos.x + border * 2, pos.y + border, (int)(siz - border * 2) * 3 / 4, (int)siz - border * 2};

        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
    }
    else if (reg.type == RegionType::NOTEQUAL)
    {
        XYPos numsiz = XYPos(siz * 6 / 8, siz * 6 / 8);
        std::string digits = "!" + reg.val_as_str(0);
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
        SDL_Rect dst_rect = {pos.x + border, pos.y + border, (int)(siz - border * 2), (int)(siz - border * 2)};
        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
    }
    else
    {
        assert(0);
    }


}

bool GameState::render_lock(int lock_type, XYPos pos, XYPos size)
{
    if (prog_seen[lock_type] >= PROG_ANIM_MAX)
        return true;

    if (prog_stars[lock_type] <= max_stars)
    {
        if (lock_type < PROG_ANIM_MAX)
        {
            star_burst_animations.push_back(AnimationStarBurst(pos, size, prog_seen[lock_type], true));
            prog_seen[lock_type] += frame_step;
        }
        return true;
    }
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
            render_box(base_pos + XYPos(0 * size, 0 * size), XYPos(siz.x * size, siz.y * size), size / 2);
            render_region_bubble(rule.region_type[0], colour, base_pos + XYPos(0 * size, 0 * size), size * 2 / 3, hover_rulemaker_region_base_index == 0);

        }
        if (rule.region_count >= 2)
        {
            XYPosFloat siz = XYPos(2,2);
            if (rule.region_count >= 3) siz.y = 3;
            if (rule.region_count >= 4) siz.y = 5 - 1.0 / 12;

            unsigned colour = (right_panel_mode == RIGHT_MENU_RULE_GEN && rule_gen_region[1]) ? rule_gen_region[1]->colour : 0;
            set_region_colour(sdl_texture, rule.region_type[1].value, colour, contrast);
            render_box(base_pos + XYPos(1 * size, 0 * size + size / 12), XYPos(siz.x * size, siz.y * size), size / 2);
            render_region_bubble(rule.region_type[1], colour, base_pos + XYPos(2 * size + size / 3, 0 * size +size / 12), size * 2 / 3, hover_rulemaker_region_base_index == 1);

        }

        if (rule.region_count >= 3)
        {
            XYPos siz = XYPos(5,1);
            if (rule.region_count >= 4) siz.y = 2;
            unsigned colour = (right_panel_mode == RIGHT_MENU_RULE_GEN && rule_gen_region[2]) ? rule_gen_region[2]->colour : 0;
            set_region_colour(sdl_texture, rule.region_type[2].value, colour, contrast);
            render_box(base_pos + XYPos(0 * size, 2 * size), XYPos(siz.x * size, siz.y * size), size / 2);
            render_region_bubble(rule.region_type[2], colour, base_pos + XYPos(4 * size + size / 3, 2 * size), size * 2 / 3, hover_rulemaker_region_base_index == 2);
        }

        if (rule.region_count >= 4)
        {
            unsigned colour = (right_panel_mode == RIGHT_MENU_RULE_GEN && rule_gen_region[3]) ? rule_gen_region[3]->colour : 0;
            set_region_colour(sdl_texture, rule.region_type[3].value, colour, contrast);
            render_box(base_pos + XYPos(size / 12, 3 * size), XYPos(5 * size, 2 * size), size / 2);
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
                    if (r_type.type == RegionType::SET)
                    {
                        SDL_Rect src_rect = {512, 192, 192, 192};
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
            for (int i = 0; i < reg_pos.size(); i++)
            {
                for (int j = 0; j < reg_pos.size(); j++)
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
            for (int i = 0; i < reg_pos.size(); i++)
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
                }
            }
        }

}

void GameState::render(bool saving)
{
    if (score_tables[game_mode][0].size() == 0)
        fetch_scores();

    if(low_contrast && contrast > 128)
        contrast--;
    if(!low_contrast && contrast < 255)
        contrast++;

    mouse_cursor = SDL_SYSTEM_CURSOR_ARROW;
    if (grid_dragging)
        mouse_cursor = SDL_SYSTEM_CURSOR_SIZEALL;

    XYPos window_size;
    bool row_col_clues = !grid->edges.empty() && show_row_clues;
    SDL_GetWindowSize(sdl_window, &window_size.x, &window_size.y);
    SDL_RenderClear(sdl_renderer);
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
        int row_count = 16;
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

        if (rules_list_offset + row_count > (int)score_tables[game_mode][current_level_group_index].size())
            rules_list_offset = score_tables[game_mode][current_level_group_index].size() - row_count;
        rules_list_offset = std::max(rules_list_offset, 0);

        for (int score_index = 0; score_index < row_count; score_index++)
        {
            if (score_index + rules_list_offset >= score_tables[game_mode][current_level_group_index].size())
                break;
            PlayerScore& s = score_tables[game_mode][current_level_group_index][score_index + rules_list_offset];
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
                SDL_Rect dst_rect = {list_pos.x + 1 * cell_width, list_pos.y + cell_width + score_index * cell_height, width, cell_height};
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

        if (rules_list_offset + row_count > score_tables[game_mode][current_level_group_index].size())
            rules_list_offset = score_tables[game_mode][current_level_group_index].size() - row_count;
        rules_list_offset = std::max(rules_list_offset, 0);

        {
            int full_size = cell_width * 5.5;
            int all_count = score_tables[game_mode][current_level_group_index].size();
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
        if (rules_list_offset + row_count > (int)score_tables[game_mode][current_level_group_index].size())
            rules_list_offset = score_tables[game_mode][current_level_group_index].size() - row_count;
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
            SDL_Rect src_rect = {896, 2336, 192, 192};
            SDL_Rect dst_rect = {list_pos.x + 2 * cell_width, list_pos.y, cell_width, cell_width};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            add_tooltip(dst_rect, "Solved");
            if (display_rules_click && ((display_rules_click_pos - XYPos(dst_rect.x, dst_rect.y)).inside(XYPos(dst_rect.w, dst_rect.h))))
                col_click = 2;
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
                    return (state.level_progress[state.game_mode][state.current_level_group_index][state.current_level_set_index].level_stats[a] < 
                            state.level_progress[state.game_mode][state.current_level_group_index][state.current_level_set_index].level_stats[b]);
                if (col == 2)
                    return (state.level_progress[state.game_mode][state.current_level_group_index][state.current_level_set_index].level_status[a] < 
                            state.level_progress[state.game_mode][state.current_level_group_index][state.current_level_set_index].level_status[b]);
                assert(0);
                return false;
            }
        };

        std::vector<unsigned> levels_list;

        for (int i = 0; i < level_progress[game_mode][current_level_group_index][current_level_set_index].level_status.size(); i++)
            levels_list.push_back(i);

        std::stable_sort (levels_list.begin(), levels_list.end(), RuleDiplaySort(*this, display_levels_sort_col_2nd, display_levels_sort_dir_2nd));
        std::stable_sort (levels_list.begin(), levels_list.end(), RuleDiplaySort(*this, display_levels_sort_col, display_levels_sort_dir));

        if (display_levels_center_current)
            for (int i = 0; i < levels_list.size(); i++)
                if (levels_list[i] == current_level_index)
                {
                    rules_list_offset = i - 8;
                    break;
                }
        display_levels_center_current = false;
        if (rules_list_offset + row_count > levels_list.size())
            rules_list_offset = levels_list.size() - row_count;
        rules_list_offset = std::max(rules_list_offset, 0);

        for (int level_index = 0; level_index < row_count; level_index++)
        {
            if (level_index + rules_list_offset >= levels_list.size())
                break;
            unsigned index = levels_list[level_index + rules_list_offset];

            if (index == current_level_index)
            {
                render_box(list_pos + XYPos(0, cell_width + level_index * cell_height), XYPos(cell_width * 7, cell_height), cell_height / 4);
            }
            render_number(index, list_pos + XYPos(0 * cell_width, cell_width + level_index * cell_height + cell_height/10), XYPos(cell_width, cell_height*8/10));

            {
                unsigned rep = level_progress[game_mode][current_level_group_index][current_level_set_index].level_stats[index];

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
                bool rep = level_progress[game_mode][current_level_group_index][current_level_set_index].level_status[index];
                SDL_Rect src_rect = {1280, rep ? 2240 : 2144, 96, 96};
                SDL_Rect dst_rect = {list_pos.x + cell_width * 2 + (cell_width - cell_height) / 2, list_pos.y + cell_width * 1 + level_index * cell_height, cell_height, cell_height};
                SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            }

            if (display_rules_click && ((display_rules_click_pos - list_pos - XYPos(0, cell_width + level_index * cell_height)).inside(XYPos(cell_width * 7, cell_height))))
            {
                current_level_index = index;
                force_load_level = level_progress[game_mode][current_level_group_index][current_level_set_index].level_status[index];
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

        if (rules_list_offset + row_count > levels_list.size())
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
            RuleDiplaySort(int col_, bool descend_, bool cur_level_):
                col(col_), descend(descend_), cur_level(cur_level_)
            {};
            bool operator() (RuleDiplay a_,RuleDiplay b_)
            {
                RuleDiplay &a = descend ? a_ : b_;
                RuleDiplay &b = descend ? b_ : a_;
                if (col == 0)
                    return (a.index < b.index);
                if (col == 1)
                    return a.rule->priority < b.rule->priority;
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
                if (col == 5)
                {
                    if (a.rule->apply_region_type.type == RegionType::VISIBILITY &&
                        b.rule->apply_region_type.type != RegionType::VISIBILITY)
                            return true;
                    if (cur_level)
                        return (a.rule->level_used_count < b.rule->level_used_count);
                    else
                        return (a.rule->used_count < b.rule->used_count);
                }
                if (col == 6)
                    if (a.rule->apply_region_type.type == RegionType::VISIBILITY &&
                        b.rule->apply_region_type.type != RegionType::VISIBILITY)
                            return true;
                    if (cur_level)
                        return (a.rule->level_clear_count < b.rule->level_clear_count);
                    else
                        return (a.rule->clear_count < b.rule->clear_count);
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

        std::stable_sort (rules_list.begin(), rules_list.end(), RuleDiplaySort(display_rules_sort_col_2nd, display_rules_sort_dir_2nd, display_rules_level));
        std::stable_sort (rules_list.begin(), rules_list.end(), RuleDiplaySort(display_rules_sort_col, display_rules_sort_dir, display_rules_level));

        if (rules_list_offset + row_count > rules_list.size())
            rules_list_offset = rules_list.size() - row_count;
        rules_list_offset = std::max(rules_list_offset, 0);

        for (int rule_index = 0; rule_index < row_count; rule_index++)
        {
            if (rule_index + rules_list_offset >= rules_list.size())
                break;
            RuleDiplay& rd = rules_list[rule_index + rules_list_offset];
            GridRule& rule = *rd.rule;
            if (((right_panel_mode == RIGHT_MENU_RULE_INSPECT) && (&rule == inspected_rule.rule)) ||
                ((right_panel_mode == RIGHT_MENU_RULE_GEN) && (&rule == replace_rule)))
            {
                render_box(list_pos + XYPos(0, cell_width + rule_index * cell_height), XYPos(cell_width * 7, cell_height), cell_height / 4);
            }

            render_number(rd.index, list_pos + XYPos(0 * cell_width, cell_width + rule_index * cell_height + cell_height/10), XYPos(cell_width, cell_height*8/10));
            {
                int priority = std::clamp(int(rule.priority), -3, 2);
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
            if(!display_clipboard_rules && rule.apply_region_type.type != RegionType::VISIBILITY)
            {
                render_number(display_rules_level ? rule.level_used_count : rule.used_count, list_pos + XYPos(5 * cell_width, cell_width + rule_index * cell_height + cell_height/10), XYPos(cell_width * 9 / 10, cell_height*8/10));
                render_number(display_rules_level ? rule.level_clear_count : rule.clear_count, list_pos + XYPos(6 * cell_width, cell_width + rule_index * cell_height + cell_height/10), XYPos(cell_width * 9 / 10, cell_height*8/10));
            }

            if (!display_clipboard_rules && display_rules_click_drag && display_rules_click_line && !display_rules_click && ((mouse - list_pos - XYPos(0, cell_width + rule_index * cell_height)).inside(XYPos(cell_width * 7, cell_height))))
            {
                if (inspected_rule.rule != &rule && (right_panel_mode == RIGHT_MENU_RULE_INSPECT))
                {
                    if ((display_rules_sort_col != 0) || (display_rules_sort_dir != true))
                    {
                        display_rules_sort_col = 0;
                        display_rules_sort_dir = true;
                    }
                    else
                    {
                        std::list<GridRule>::iterator from, to = rules[game_mode].end();

                        for (std::list<GridRule>::iterator it = rules[game_mode].begin(); it != rules[game_mode].end(); ++it)
                        {
                            if (&(*it) == &rule)
                            {
                                to = it;
                                to++;
                            }
                            if (&(*it) == inspected_rule.rule)
                            {
                                from = it;
                                to--;
                            }
                        }
                        rules[game_mode].splice(to, rules[game_mode], from);
                    }
                }
                

            }

            if (display_rules_click && ((display_rules_click_pos - list_pos - XYPos(0, cell_width + rule_index * cell_height)).inside(XYPos(cell_width * 7, cell_height))))
            {
                if (!display_clipboard_rules)
                {
                    right_panel_mode = RIGHT_MENU_RULE_INSPECT;
                    inspected_rule = GridRegionCause(&rule, NULL, NULL, NULL, NULL);
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

        if (rules_list_offset + row_count > rules_list.size())
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
            Colour bg_col(0,0,0);
            bool hl = false;
            {
                if (clue_solves.count(pos))
                {
                    bg_col = Colour(0, contrast, 0);
                }
                if (filter_pos.get(pos))
                {
                    bg_col = Colour(contrast,0, 0);
                }
                if (hover_rulemaker && hover_squares_highlight.get(pos))
                {
                    bg_col = Colour(contrast, contrast, 0);
                }
            }
            std::vector<RenderCmd> cmds;
            grid->render_square(pos, grid_pitch, cmds);
            for (RenderCmd& cmd : cmds)
            {
                for(WrapPos r : wraps)
                {
                    if (cmd.bg)
                    {
                        if (bg_col == Colour(0, 0, 0))
                            continue;
                        SDL_SetTextureColorMod(sdl_texture, bg_col.r,  bg_col.g, bg_col.b);
                    }
                    else if (grid->wrapped == Grid::WRAPPED_IN)
                    {
                        double d = log(grid_pitch.y * r.size / grid_size) * 1.2;
                        uint8_t r = ((sin(d) + 1) / 2) * 100 + 155;
                        uint8_t g = ((sin(d+2) + 1) / 2) * 100 + 155;
                        uint8_t b = ((sin(d+4) + 1) / 2) * 100 + 155;
                        SDL_SetTextureColorMod(sdl_texture, r, g, b);
                    }
                    SDL_Rect src_rect = {cmd.src.pos.x, cmd.src.pos.y, cmd.src.size.x, cmd.src.size.y};
                    SDL_Rect dst_rect = {grid_offset.x + r.pos.x + int(cmd.dst.pos.x * r.size), grid_offset.y + r.pos.y + int(cmd.dst.pos.y * r.size), int(cmd.dst.size.x * r.size), int(ceil(cmd.dst.size.y * r.size))};
                    SDL_Point rot_center = {int(cmd.center.x * r.size), int(cmd.center.y)};
                    SDL_RenderCopyEx(sdl_renderer, sdl_texture, &src_rect, &dst_rect, cmd.angle, &rot_center, SDL_FLIP_NONE);
                    SDL_SetTextureColorMod(sdl_texture, contrast, contrast, contrast);


                }
            }
        }

        std::map<XYPos, int> total_taken;
        std::list<GridRegion*> display_regions;

        XYPos mouse_filter_pos(-1,-1);
        if ((mouse_mode == MOUSE_MODE_FILTER) && (mouse - grid_offset - scaled_grid_offset).inside(XYPos(grid_size,grid_size)))
        {
            mouse_filter_pos = grid->get_square_from_mouse_pos(mouse - grid_offset - scaled_grid_offset, grid_pitch);
            if (mouse_filter_pos != XYPos(-1,-1))
                mouse_filter_pos = grid->get_base_square(mouse_filter_pos);
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

                sq_pos.pos -= XYPosFloat(sq_pos.size) * (wob / 2);
                sq_pos.size += XYPosFloat(sq_pos.size) * (wob);

                int icon_width = std::min(sq_pos.size.x, sq_pos.size.y);
                XYPos gpos = sq_pos.pos + (sq_pos.size - XYPos(icon_width,icon_width)) / 2;
                SDL_SetTextureAlphaMod(sdl_texture, 255.0 * (1.0 - pow(fade, 0.5)));
                for(WrapPos r : wraps)
                {
                    XYPos mgpos = grid_offset + gpos * r.size + r.pos;
                    if (place.bomb)
                    {
                        SDL_Rect src_rect = {320, 192, 192, 192};
                        SDL_Rect dst_rect = {mgpos.x, mgpos.y, int(icon_width * r.size), int(icon_width * r.size)};
                        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
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
            std::vector<EdgePos> edges;
            grid->get_edges(edges, grid_pitch);
            {
                SDL_Rect src_rect = {448, 448, 1, 1};
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
                    if (distance < best_distance)
                    {
                        best_distance = distance;
                        best_edge_pos = edge;
                    }
                }
            }
        }

        {
            SDL_Rect src_rect = {448, 448, 1, 1};
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
        SDL_Rect src_rect = {1280, 1536, 192, 192};
        SDL_Rect dst_rect = {left_panel_offset.x + 0 * button_size, left_panel_offset.y + button_size * 0, button_size, button_size};
        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
        add_tooltip(dst_rect, "Main Menu");
    }
    {
        SDL_Rect src_rect = {704, 960, 192, 192};
        SDL_Rect dst_rect = {left_panel_offset.x + 1 * button_size, left_panel_offset.y + button_size * 0, button_size, button_size};
        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
        add_tooltip(dst_rect, "Help");
    }
    {
        SDL_Rect src_rect = {1472, 960, 192, 192};
        SDL_Rect dst_rect = {left_panel_offset.x + 2 * button_size, left_panel_offset.y + button_size * 0, button_size, button_size};
        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
        add_tooltip(dst_rect, "Hint");
    }
    {
        SDL_Rect src_rect = {704 + 192 * 3, 960, 192, 192};
        SDL_Rect dst_rect = {left_panel_offset.x + 3 * button_size, left_panel_offset.y + button_size * 0, button_size, button_size};
        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
        add_tooltip(dst_rect, "Next Level");
    }
    {
        SDL_Rect src_rect = {704 + 192 * 2, 960, 192, 192};
        SDL_Rect dst_rect = {left_panel_offset.x + 4 * button_size, left_panel_offset.y + button_size * 0, button_size, button_size};
        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
        add_tooltip(dst_rect, "Refresh Regions");
    }
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
        if (!filter_pos.empty())
        {
            SDL_Rect src_rect = {1472, 192, 192, 192};
            SDL_Rect dst_rect = {left_panel_offset.x + 3 * button_size, right_panel_offset.y + button_size * 1, button_size, button_size};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            add_tooltip(dst_rect, "Clear Filter");
        }

        {
            if (mouse_mode == MOUSE_MODE_FILTER)
                render_box(left_panel_offset + XYPos(button_size * 4, button_size * 1), XYPos(button_size, button_size), button_size/4);
            SDL_Rect src_rect = {1280, 192, 192, 192};
            SDL_Rect dst_rect = {left_panel_offset.x + 4 * button_size, right_panel_offset.y + button_size * 1, button_size, button_size};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            add_tooltip(dst_rect, "Filter");
            int s = filter_pos.count();
            if (s)
            {
                render_number(s, left_panel_offset + XYPos(button_size * 4 + button_size / 4, button_size * 1 + button_size / 6), XYPos(button_size/2, button_size/4));
            }
        }
    }
    {
        if (display_rules)
            render_box(left_panel_offset + XYPos(button_size * 0, button_size * 2), XYPos(button_size * 2, button_size), button_size/4);
        SDL_Rect src_rect = {1536, 384, 192, 192};
        SDL_Rect dst_rect = {left_panel_offset.x + 0 * button_size, left_panel_offset.y + button_size * 2, button_size, button_size};
        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
        dst_rect.w *= 2;
        add_tooltip(dst_rect, "Rules");
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

        render_number_string(digits, left_panel_offset + XYPos(button_size * 1, button_size * 2 + button_size / 20), XYPos(button_size, button_size * 8 / 20));
        render_number(vis_rule_count, left_panel_offset + XYPos(button_size * 1, button_size * 2 + button_size / 2 + button_size / 20), XYPos(button_size, button_size * 8 / 20));
    }
    if (render_lock(PROG_LOCK_GAME_MODE, XYPos(left_panel_offset.x + 2 * button_size, left_panel_offset.y + 2 * button_size), XYPos(button_size, button_size)))
    {
        SDL_Rect src_rect = {2240, 576 + game_mode * 192, 192, 192};
        SDL_Rect dst_rect = {left_panel_offset.x + 2 * button_size, left_panel_offset.y + button_size * 2, button_size, button_size};
        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
        add_tooltip(dst_rect, "Game Mode");
    }
    if (clipboard_has_item != CLIPBOARD_HAS_NONE)
    {
        SDL_Rect src_rect = {2048, 1536, 192, 192};
        SDL_Rect dst_rect = {left_panel_offset.x + 2 * button_size, left_panel_offset.y + button_size * 3, button_size, button_size};
        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
        add_tooltip(dst_rect, "Import Clipboard");
    }
    {
        if (display_levels)
            render_box(left_panel_offset + XYPos(button_size * 0, button_size * 3), XYPos(button_size * 2, button_size), button_size/4);
        SDL_Rect src_rect = {1408, 2144, 192, 192};
        SDL_Rect dst_rect = {left_panel_offset.x + 0 * button_size, left_panel_offset.y + button_size * 3, button_size, button_size};
        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
        dst_rect.w *= 2;
        add_tooltip(dst_rect, "Current Level");
        unsigned rep = level_progress[game_mode][current_level_group_index][current_level_set_index].level_stats[current_level_index];

        std::string digits;
        if (rep == 0)
        {
            digits = "0%";
        }
        else if (rep < 100)
        {
            digits = "." + std::to_string((rep / 10) % 10) + std::to_string((rep / 1) % 10) + "%";
        }
        else if (rep < 1000)
        {
            digits = std::to_string((rep / 100) % 10) + "." + std::to_string((rep / 10) % 10) + "%";
        }
        else 
        {
            digits = std::to_string((rep / 1000) % 10) + std::to_string((rep / 100) % 10) + "%";
        }

        render_number_string(digits, left_panel_offset + XYPos(button_size * 1 + button_size / 8, button_size * 3 + button_size / 4), XYPos(button_size * 3 / 4, button_size / 2));
    }

    {
        if (display_scores)
            render_box(left_panel_offset + XYPos(button_size * 0, button_size * 4), XYPos(button_size * 2, button_size), button_size/4);
        SDL_Rect src_rect = {1728, 384, 192, 192};
        SDL_Rect dst_rect = {left_panel_offset.x + 0 * button_size, left_panel_offset.y + button_size * 4, button_size, button_size};
        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
        dst_rect.w *= 2;
        add_tooltip(dst_rect, "Scores");
        int count = 0;
        for (int i = 0; i < level_progress[game_mode][current_level_group_index].size(); i++)
        {
            LevelProgress& prog = level_progress[game_mode][current_level_group_index][i];
            for (bool b : prog.level_status)
                if (b)
                    count++;
        }
        render_number(count, left_panel_offset + XYPos(button_size * 1 + button_size / 8, button_size * 4 + button_size / 4), XYPos(button_size * 3 / 4, button_size / 2));
    }

    if (render_lock(PROG_LOCK_VISIBILITY, XYPos(left_panel_offset.x + 3 * button_size, left_panel_offset.y + 2 * button_size), XYPos(2 * button_size, 3 * button_size)))
    {
        int region_vis_counts[3] = {0,0,0};
        for (GridRegion& region : grid->regions)
            region_vis_counts[int(region.vis_level)]++;
        {
            if (vis_level == GRID_VIS_LEVEL_SHOW)
            render_box(left_panel_offset+ XYPos(button_size * 3, button_size * 2), XYPos(button_size * 2, button_size), button_size/4);
            render_number(region_vis_counts[0], left_panel_offset + XYPos(button_size * 3 + button_size / 8, button_size * 2 + button_size / 4), XYPos(button_size * 3 / 4, button_size / 2));
            SDL_Rect src_rect = {1088, 384, 192, 192};
            SDL_Rect dst_rect = {left_panel_offset.x + 4 * button_size, left_panel_offset.y + button_size * 2, button_size, button_size};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            dst_rect = {left_panel_offset.x + 3 * button_size, left_panel_offset.y + button_size * 2, button_size * 2, button_size};
            add_tooltip(dst_rect, "Visible");
        }
        {
            if (vis_level == GRID_VIS_LEVEL_HIDE)
                render_box(left_panel_offset+ XYPos(button_size * 3, button_size * 3), XYPos(button_size * 2, button_size), button_size/4);
            render_number(region_vis_counts[1], left_panel_offset + XYPos(button_size * 3 + button_size / 8, button_size * 3 + button_size / 4), XYPos(button_size * 3 / 4, button_size / 2));
            SDL_Rect src_rect = {896, 384, 192, 192};
            SDL_Rect dst_rect = {left_panel_offset.x + 4 * button_size, left_panel_offset.y + button_size * 3, button_size, button_size};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            dst_rect = {left_panel_offset.x + 3 * button_size, left_panel_offset.y + button_size * 3, button_size * 2, button_size};
            add_tooltip(dst_rect, "Hidden");
        }
        {
            if (vis_level == GRID_VIS_LEVEL_BIN)
            render_box(left_panel_offset+ XYPos(button_size * 3, button_size * 4), XYPos(button_size * 2, button_size), button_size/4);
            render_number(region_vis_counts[2], left_panel_offset + XYPos(button_size * 3 + button_size / 8, button_size * 4 + button_size / 4), XYPos(button_size * 3 / 4, button_size / 2));
            SDL_Rect src_rect = {512, 768, 192, 192};
            SDL_Rect dst_rect = {left_panel_offset.x + 4 * button_size, left_panel_offset.y + button_size * 4, button_size, button_size};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            dst_rect = {left_panel_offset.x + 3 * button_size, left_panel_offset.y + button_size * 4, button_size * 2, button_size};
            add_tooltip(dst_rect, "Trash");
        }
    }

    for (int i = 0; i < 5; i++)
    {
        const static char* grid_names[] = {"Hexagon", "Square", "Triangle", "Infinite", "Server"};
        if (render_lock(PROG_LOCK_HEX + i, XYPos(left_panel_offset.x + i * button_size, left_panel_offset.y + button_size * 5), XYPos(button_size, button_size)))
        {
            SDL_Rect src_rect = {1472, 1344 + i * 192, 192, 192};
            SDL_Rect dst_rect = {left_panel_offset.x + i * button_size, left_panel_offset.y + button_size * 5, button_size, button_size};
            if (i == 4)
                src_rect = {1664, 1536, 192, 192};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            add_tooltip(dst_rect, grid_names[i]);
        }
        {
            SDL_Rect src_rect = {512, (current_level_group_index == i) ? 1152 : 1344, 192, 192};
            SDL_Rect dst_rect = {left_panel_offset.x + i * button_size, right_panel_offset.y + button_size * 5, button_size, button_size};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
        }
        
    }


    XYPos p = XYPos(0,0);
    for (int i = 0; i < level_progress[game_mode][current_level_group_index].size(); i++)
    {
        p.x = i % 5;
        p.y = i / 5;

        XYPos pos = left_panel_offset + XYPos(button_size * (i % 5), button_size * (i / 5 + 6));
        if (i == current_level_set_index)
        {
            render_box(pos, XYPos(button_size, button_size), button_size/4);
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

            SDL_SetTextureColorMod(sdl_texture, 0,0,0);
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
            render_number(level_progress[game_mode][current_level_group_index][i].count_todo, pos + XYPos(button_size / 8, button_size / 8), XYPos(button_size * 3 / 4 , button_size * 3 / 4));
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

    }

    if (right_panel_mode == RIGHT_MENU_RULE_GEN)
    {
        {
            std::string t = translate("Rule Constructor");
            render_text_box(right_panel_offset + XYPos(0 * button_size, 0 * button_size), t);
        }


        if (render_lock(PROG_LOCK_NUMBER_TYPES, right_panel_offset + XYPos(0, button_size * 7.3), XYPos(button_size * 5, button_size * 5)))
        {
            FOR_XY(pos, XYPos(), XYPos(5, 2))
            {
                RegionType r_type = select_region_type;
                if (r_type.type == RegionType::NONE || r_type.type == RegionType::SET || r_type.type == RegionType::VISIBILITY)
                    r_type.value = 0;
                XYPos bpos = right_panel_offset + pos * button_size + XYPos(0, button_size * 7.3);
                r_type.type = menu_region_types[pos.y][pos.x];
                if (r_type.type == RegionType::NONE)
                {
                    continue;
                }
                if (region_type == r_type)
                    render_box(bpos, XYPos(button_size, button_size), button_size/4);
                render_region_type(r_type, bpos, button_size);
                SDL_Rect dst_rect = {bpos.x, bpos.y, button_size, button_size};
                add_clickable_highlight(dst_rect);
            }

            FOR_XY(pos, XYPos(), XYPos(5, 2))
            {
                RegionType r_type = select_region_type;
                if (r_type.type == RegionType::NONE || r_type.type == RegionType::SET || r_type.type == RegionType::VISIBILITY)
                    r_type.type = RegionType::EQUAL;

                XYPos bpos = right_panel_offset + pos * button_size + XYPos(0, button_size * 9.6);
                r_type.value = pos.y * 5 + pos.x;
                if (region_type == r_type)
                    render_box(bpos, XYPos(button_size, button_size), button_size/4);
                render_region_type(r_type, bpos, button_size);
                SDL_Rect dst_rect = {bpos.x, bpos.y, button_size, button_size};
                add_clickable_highlight(dst_rect);
            }

            for (int i = 0; i < 5; i++)
            {
                XYPos bpos = right_panel_offset + XYPos(i * button_size, button_size * 11.8);
                if (game_mode == 3)
                    continue;
                if (!render_lock(PROG_LOCK_VARS1 + i, bpos, XYPos(button_size, button_size)))
                    continue;
                RegionType r_type = RegionType(RegionType::EQUAL, 0);
                r_type.var = (1 << i);
                if (select_region_type.var & (1 << i))
                    render_box(bpos, XYPos(button_size, button_size), button_size/4);

                render_region_type(r_type, bpos, button_size);
                SDL_Rect dst_rect = {bpos.x, bpos.y, button_size, button_size};
                add_clickable_highlight(dst_rect);
            }

        }



        {
            if (region_type.type == RegionType::NONE)
                render_box(right_panel_offset + XYPos(button_size * 0, button_size * 6), XYPos(button_size, button_size), button_size/4);
            SDL_Rect src_rect = {896, 192, 192, 192};
            SDL_Rect dst_rect = {right_panel_offset.x + button_size * 0, right_panel_offset.y + button_size * 6, button_size, button_size};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            add_tooltip(dst_rect, "Don't Care");
        }

        {
            if (region_type == RegionType(RegionType::SET, 0))
                render_box(right_panel_offset + XYPos(button_size * 1, button_size * 6), XYPos(button_size, button_size), button_size/4);
            SDL_Rect src_rect = {512, 192, 192, 192};
            SDL_Rect dst_rect = {right_panel_offset.x + button_size * 1, right_panel_offset.y + button_size * 6, button_size, button_size};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            add_tooltip(dst_rect, "Clear");
        }
        {
            if (region_type == RegionType(RegionType::SET, 1))
                render_box(right_panel_offset + XYPos(button_size * 2, button_size * 6), XYPos(button_size, button_size), button_size/4);
            SDL_Rect src_rect = {320, 192, 192, 192};
            SDL_Rect dst_rect = {right_panel_offset.x + button_size * 2, right_panel_offset.y + button_size * 6, button_size, button_size};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            add_tooltip(dst_rect, "Bomb");
        }


        if (render_lock(PROG_LOCK_VISIBILITY2, XYPos(right_panel_offset.x + 3 * button_size, left_panel_offset.y + 6 * button_size), XYPos(button_size, button_size)))
        {
            if (region_type == RegionType(RegionType::VISIBILITY, 1))
                render_box(right_panel_offset + XYPos(button_size * 3, button_size * 6), XYPos(button_size, button_size), button_size/4);
            SDL_Rect src_rect = {896, 384, 192, 192};
            SDL_Rect dst_rect = {right_panel_offset.x + button_size * 3, right_panel_offset.y + button_size * 6, button_size, button_size};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            add_tooltip(dst_rect, "Hidden");
        }
        if (render_lock(PROG_LOCK_VISIBILITY3, XYPos(right_panel_offset.x + 4 * button_size, left_panel_offset.y + 6 * button_size), XYPos(button_size, button_size)))
        {
            if (region_type == RegionType(RegionType::VISIBILITY, 2))
                render_box(right_panel_offset + XYPos(button_size * 4, button_size * 6), XYPos(button_size, button_size), button_size/4);
            SDL_Rect src_rect = {512, 768, 192, 192};
            SDL_Rect dst_rect = {right_panel_offset.x + button_size * 4, right_panel_offset.y + button_size * 6, button_size, button_size};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            add_tooltip(dst_rect, "Trash");
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
            render_box(right_panel_offset + XYPos(0 * button_size, 1 * button_size), XYPos(1 * button_size, 2 * button_size), button_size / 2);
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
        {
            SDL_Rect src_rect = { 192*3+128, 192*3, 192, 192 };
            SDL_Rect dst_rect = { right_panel_offset.x + button_size * 3, right_panel_offset.y + button_size, button_size, button_size};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            add_tooltip(dst_rect, "Cancel");
        }
        if (constructed_rule.region_count < (game_mode == 1 ? 3 : 4))
        {
            SDL_Rect src_rect = { 1088, 576, 192, 192 };
            SDL_Rect dst_rect = { right_panel_offset.x + button_size * 4, right_panel_offset.y + 1 * button_size, button_size, button_size};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            add_tooltip(dst_rect, "Add to Rule Constructor");
        }
        if (inspected_region->gen_cause.rule)
        {
            SDL_Rect src_rect = { 704, 768, 192, 192 };
            SDL_Rect dst_rect = { right_panel_offset.x + button_size * 0, right_panel_offset.y + 4 * button_size, button_size, button_size };
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            add_tooltip(dst_rect, "Show Creation Rule");
        }

        if (inspected_region->visibility_force == GridRegion::VIS_FORCE_USER)
        {
            SDL_Rect src_rect = { 1472, 768, 192, 192};
            SDL_Rect dst_rect = { right_panel_offset.x + button_size * 1, right_panel_offset.y + 4 * button_size, button_size, button_size };
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            add_tooltip(dst_rect, "Visibility set by User");
        }
        else if (inspected_region->vis_cause.rule)
        {
            SDL_Rect src_rect = { 896, 768, 192, 192 };
            SDL_Rect dst_rect = { right_panel_offset.x + button_size * 1, right_panel_offset.y + 4 * button_size, button_size, button_size };
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            add_tooltip(dst_rect, "Show Visibility Rule");
        }
        {
            SDL_Rect src_rect = { 1856, 576, 192, 192};
            SDL_Rect dst_rect = { right_panel_offset.x + button_size * 0, right_panel_offset.y + 5 * button_size, button_size, button_size };
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            add_tooltip(dst_rect, "Change Colour");
        }

        if (render_lock(PROG_LOCK_VISIBILITY4, XYPos(right_panel_offset.x + 3 * button_size, left_panel_offset.y + 3 * button_size), XYPos(button_size, button_size * 3)))
        {
            {
                if (inspected_region->vis_level == GRID_VIS_LEVEL_SHOW)
                render_box(right_panel_offset+ XYPos(button_size * 3, button_size * 3), XYPos(button_size, button_size), button_size/4);

                SDL_Rect src_rect = {1088, 384, 192, 192};
                SDL_Rect dst_rect = {right_panel_offset.x + 3 * button_size, right_panel_offset.y + button_size * 3, button_size, button_size};
                SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                add_tooltip(dst_rect, "Visible");
            }
            {
                if (inspected_region->vis_level == GRID_VIS_LEVEL_HIDE)
                render_box(right_panel_offset+ XYPos(button_size * 3, button_size * 4), XYPos(button_size, button_size), button_size/4);
                SDL_Rect src_rect = {896, 384, 192, 192};
                SDL_Rect dst_rect = {right_panel_offset.x + 3 * button_size, right_panel_offset.y + button_size * 4, button_size, button_size};
                SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                add_tooltip(dst_rect, "Hidden");
            }
            {
                if (inspected_region->vis_level == GRID_VIS_LEVEL_BIN)
                render_box(right_panel_offset+ XYPos(button_size * 3, button_size * 5), XYPos(button_size, button_size), button_size/4);
                SDL_Rect src_rect = {512, 768, 192, 192};
                SDL_Rect dst_rect = {right_panel_offset.x + 3 * button_size, right_panel_offset.y + button_size * 5, button_size, button_size};
                SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                add_tooltip(dst_rect, "Trash");
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
        if (constructed_rule.region_count)
        {
            SDL_Rect src_rect = { 704 + (constructed_rule.region_count - 1) * 192, 1152, 192, 192 };
            SDL_Rect dst_rect = { right_panel_offset.x + button_size * 1, right_panel_offset.y + 6 * button_size, button_size, button_size};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            add_tooltip(dst_rect, "Go to Rule Constructor");
        }
    }

    if (right_panel_mode == RIGHT_MENU_RULE_GEN || right_panel_mode == RIGHT_MENU_RULE_INSPECT)
    {
        GridRule& rule = (right_panel_mode == RIGHT_MENU_RULE_GEN) ? constructed_rule : *inspected_rule.rule;
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
                render_box(right_panel_offset + p * button_size, XYPos(button_size, button_size), button_size / 4);
                SDL_SetTextureColorMod(sdl_texture, contrast, contrast, contrast);
                if (right_panel_mode == RIGHT_MENU_RULE_GEN)
                {
                    if (!hover_rulemaker_lower_right)
                        SDL_SetTextureColorMod(sdl_texture, contrast / 2, contrast / 2, contrast / 2);
                    render_box(right_panel_offset + p * button_size + XYPos(button_size / 2, button_size / 2), XYPos(button_size / 2, button_size / 2), button_size / 4);
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
                    if ((rule.square_counts[hover_rulemaker_bits] == RegionType(RegionType::NONE, 0)) ||
                        (rule.square_counts[hover_rulemaker_bits] == RegionType(RegionType::EQUAL, 0) && square_counts[hover_rulemaker_bits]))
                        new_region_type = RegionType(RegionType::EQUAL, 0);
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

        if (right_panel_mode == RIGHT_MENU_RULE_GEN)
        {
            if (rule.region_count >= 1)
            {
                SDL_Rect src_rect = { 192*3+128, 192*3, 192, 192 };
                SDL_Rect dst_rect = { right_panel_offset.x + button_size * 3, right_panel_offset.y + button_size, button_size, button_size};
                SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                add_tooltip(dst_rect, "Cancel");
            }

            if (rule.region_count >= 1)
            {
                if (constructed_rule_is_logical == GridRule::IMPOSSIBLE)
                {
                    SDL_Rect src_rect = {1856, 1152, 192, 192};
                    SDL_Rect dst_rect = {right_panel_offset.x + button_size * 4, right_panel_offset.y + button_size, button_size, button_size };
                    SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                    add_tooltip(dst_rect, "Impossible");
                }
                else if (constructed_rule.apply_region_bitmap)
                {
                    if (constructed_rule_is_logical == GridRule::ILLOGICAL)
                    {
                        SDL_Rect src_rect = {896, 576, 192, 192};
                        SDL_Rect dst_rect = {right_panel_offset.x + button_size * 4, right_panel_offset.y + button_size, button_size, button_size };
                        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                        if (add_tooltip(dst_rect, "Illogical"))
                        {
                            render_box(right_panel_offset + XYPos(-6 * button_size, 0), XYPos(6 * button_size, 6.5 * button_size), button_size/2, 1);
                            std::string t = translate("Why Illogical");
                            render_text_box(right_panel_offset + XYPos(-6 * button_size, 0 * button_size), t);
                            render_rule(rule_illogical_reason, right_panel_offset + XYPos(-5.5 * button_size, button_size), button_size, -1, true);
                        }
                    }
                    else if (constructed_rule_is_logical == GridRule::UNBOUNDED)
                    {
                        SDL_Rect src_rect = {2048, 1344, 192, 192};
                        SDL_Rect dst_rect = {right_panel_offset.x + button_size * 4, right_panel_offset.y + button_size, button_size, button_size };
                        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                        add_tooltip(dst_rect, "Unbounded");
                    }
                    else if (constructed_rule_is_logical == GridRule::LIMIT)
                    {
                        SDL_Rect src_rect = {1856, 1344, 192, 192};
                        SDL_Rect dst_rect = {right_panel_offset.x + button_size * 4, right_panel_offset.y + button_size, button_size, button_size };
                        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                        add_tooltip(dst_rect, "Rule Count Limit");
                    }
                    else if (constructed_rule_is_already_present)
                    {
                        SDL_Rect src_rect = {1088, 768, 192, 192};
                        SDL_Rect dst_rect = {right_panel_offset.x + button_size * 4, right_panel_offset.y + button_size, button_size, button_size };
                        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                        add_tooltip(dst_rect, "Rule Already Present");
                    }
                    else if (replace_rule)
                    {
                        SDL_Rect src_rect = {1088, 960, 192, 192};
                        SDL_Rect dst_rect = {right_panel_offset.x + button_size * 4, right_panel_offset.y + button_size, button_size, button_size };
                        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                        add_tooltip(dst_rect, "Update Rule");
                    }
                    else
                    {
                        SDL_Rect src_rect = {704, 384, 192, 192};
                        SDL_Rect dst_rect = {right_panel_offset.x + button_size * 4, right_panel_offset.y + button_size, button_size, button_size };
                        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                        add_tooltip(dst_rect, "OK");
                    }
                }
            }
            if (rule.region_count >= 1 && !constructed_rule_undo.empty())
            {
                SDL_Rect src_rect = { 1856, 768, 192, 192 };
                SDL_Rect dst_rect = { right_panel_offset.x + button_size * 3, right_panel_offset.y + button_size * 2, button_size, button_size};
                SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                add_tooltip(dst_rect, "Undo");
            }
            if (rule.region_count >= 1 && !constructed_rule_redo.empty())
            {
                SDL_Rect src_rect = { 1856, 960, 192, 192 };
                SDL_Rect dst_rect = { right_panel_offset.x + button_size * 4, right_panel_offset.y + button_size * 2, button_size, button_size};
                SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                add_tooltip(dst_rect, "Redo");
            }
        
        }
        if (right_panel_mode == RIGHT_MENU_RULE_INSPECT)
        {
            {
                std::string t = translate("Rule Inspector");
                render_text_box(right_panel_offset + XYPos(0 * button_size, 0 * button_size), t);
            }
            {
                SDL_Rect src_rect = { 192*3+128, 192*3, 192, 192 };
                SDL_Rect dst_rect = { right_panel_offset.x + button_size * 3, right_panel_offset.y + button_size, button_size, button_size};
                SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                add_tooltip(dst_rect, "Cancel");
            }
            if (!inspected_rule.rule->deleted)
            {
                SDL_Rect src_rect = {1664, 960, 192, 192};
                SDL_Rect dst_rect = { right_panel_offset.x + button_size * 4, right_panel_offset.y + button_size, button_size, button_size};
                SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                add_tooltip(dst_rect, "Remove Rule");
            }
            {
                SDL_Rect src_rect = {1280, 576, 192, 192};
                SDL_Rect dst_rect = { right_panel_offset.x + button_size * 3, right_panel_offset.y + button_size * 2, button_size, button_size};
                SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                add_tooltip(dst_rect, "Edit Rule");
            }
            {
                SDL_Rect src_rect = {1472, 576, 192, 192};
                SDL_Rect dst_rect = { right_panel_offset.x + button_size * 4, right_panel_offset.y + button_size * 2, button_size, button_size};
                SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                add_tooltip(dst_rect, "Duplicate Rule");
            }
            {
                SDL_Rect src_rect = {1856, 1536, 192, 192};
                SDL_Rect dst_rect = { right_panel_offset.x + button_size * 3, right_panel_offset.y + button_size * 6, button_size, button_size};
                SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                add_tooltip(dst_rect, "Copy Rule to Clipboard");
            }
            {
                SDL_Rect src_rect = {2048, 1728, 192, 192};
                SDL_Rect dst_rect = { right_panel_offset.x + button_size * 4, right_panel_offset.y + button_size * 6, button_size, button_size};
                SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                add_tooltip(dst_rect, "Copy Rule to Clipboard Image");
            }

            if (inspected_rule.rule->apply_region_type.type != RegionType::VISIBILITY)
            {
                if (render_lock(PROG_LOCK_PRIORITY, right_panel_offset + XYPos(0, 7 * button_size), XYPos(button_size, 5 * button_size)))
                {
                    {
                        SDL_Rect src_rect = {2240, 1344, 192, 192};
                        SDL_Rect dst_rect = {right_panel_offset.x + button_size * 0, right_panel_offset.y + button_size * 6, button_size, button_size};
                        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                        add_tooltip(dst_rect, "Maximum Priority");
                    }
                    {
                        SDL_Rect src_rect = {2240, 1344 + 192, 192, 192};
                        SDL_Rect dst_rect = {right_panel_offset.x + button_size * 0, right_panel_offset.y + button_size * 7, button_size, button_size};
                        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                        add_tooltip(dst_rect, "High Priority");
                    }
                    {
                        SDL_Rect src_rect = {2240, 1344 + 2 * 192, 192, 192};
                        SDL_Rect dst_rect = {right_panel_offset.x + button_size * 0, right_panel_offset.y + button_size * 8, button_size, button_size};
                        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                        add_tooltip(dst_rect, "Medium Priority");
                    }
                    {
                        SDL_Rect src_rect = {2240, 1344 + 3 * 192, 192, 192};
                        SDL_Rect dst_rect = {right_panel_offset.x + button_size * 0, right_panel_offset.y + button_size * 9, button_size, button_size};
                        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                        add_tooltip(dst_rect, "Low Priority");
                    }
                    {
                        SDL_Rect src_rect = {2240, 1344 + 4 * 192, 192, 192};
                        SDL_Rect dst_rect = {right_panel_offset.x + button_size * 0, right_panel_offset.y + button_size * 10, button_size, button_size};
                        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                        add_tooltip(dst_rect, "Minimum Priority");
                    }
                    {
                        SDL_Rect src_rect = {896, 960, 192, 192};
                        SDL_Rect dst_rect = {right_panel_offset.x + button_size * 0, right_panel_offset.y + button_size * 11, button_size, button_size};
                        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                        add_tooltip(dst_rect, "Paused");
                    }
                    int p = std::clamp((int)inspected_rule.rule->priority, -3, 2);
                    render_box(right_panel_offset + XYPos(button_size * 0, button_size * (8 - p)), XYPos(button_size, button_size), button_size/4);
                }

                render_box(right_panel_offset + XYPos(button_size * 2, button_size * 10), XYPos(3 * button_size, button_size), button_size/4);
                render_box(right_panel_offset + XYPos(button_size * 2, button_size * 11), XYPos(3 * button_size, button_size), button_size/4);

                render_box(right_panel_offset + XYPos(button_size * 3, button_size * 9), XYPos(button_size, 3 * button_size), button_size/4);
                render_box(right_panel_offset + XYPos(button_size * 4, button_size * 9), XYPos(button_size, 3 * button_size), button_size/4);

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


                render_number(inspected_rule.rule->level_used_count, right_panel_offset + XYPos(button_size * 3 + button_size / 8, button_size * 10 + button_size / 4), XYPos(button_size * 3 / 4, button_size / 2));
                render_number(inspected_rule.rule->used_count, right_panel_offset + XYPos(button_size * 4 + button_size / 8, button_size * 10 + button_size / 4), XYPos(button_size * 3 / 4, button_size / 2));
                render_number(inspected_rule.rule->level_clear_count, right_panel_offset + XYPos(button_size * 3 + button_size / 8, button_size * 11 + button_size / 4), XYPos(button_size * 3 / 4, button_size / 2));
                render_number(inspected_rule.rule->clear_count, right_panel_offset + XYPos(button_size * 4 + button_size / 8, button_size * 11 + button_size / 4), XYPos(button_size * 3 / 4, button_size / 2));
            }
            else
            {
                if (render_lock(PROG_LOCK_PRIORITY, right_panel_offset + XYPos(0, 11 * button_size), XYPos(button_size, button_size)))
                {
                    SDL_Rect src_rect = {896, 960, 192, 192};
                    SDL_Rect dst_rect = {right_panel_offset.x + button_size * 0, right_panel_offset.y + button_size * 11, button_size, button_size};
                    SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                    add_tooltip(dst_rect, "Paused");
                    if (inspected_rule.rule->priority < -100)
                        render_box(right_panel_offset + XYPos(button_size * 0, button_size * 11), XYPos(button_size, button_size), button_size/4);
                }
            }
        }
    }
    if (right_panel_mode == RIGHT_MENU_NONE)
    {
        if (display_rules && !display_clipboard_rules)
        {
            {
                SDL_Rect src_rect = {1856, 1536, 192, 192};
                SDL_Rect dst_rect = { right_panel_offset.x + button_size * 3, right_panel_offset.y + button_size * 6, button_size, button_size};
                SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                add_tooltip(dst_rect, "Copy All Rules to Clipboard");
            }
        }

    }
    {
        std::list<AnimationStarBurst>::iterator it = star_burst_animations.begin();
        while (it != star_burst_animations.end())
        {
            AnimationStarBurst& burst = *it;
            if (burst.progress < 1000 && burst.lock)
            {
                int size = burst.size.x;
                SDL_Rect src_rect = {1088, 192, 192, 192};
                SDL_Rect dst_rect = {burst.pos.x, burst.pos.y + burst.progress * burst.size.x / 500, size, size};
                SDL_Point rot_center = {size / 2, size / 2};
                double star_angle = burst.progress / 1000.0;
                SDL_SetTextureAlphaMod(sdl_texture, std::min (255, 1000 - burst.progress));

                SDL_RenderCopyEx(sdl_renderer, sdl_texture, &src_rect, &dst_rect, degrees(star_angle), &rot_center, SDL_FLIP_NONE);
            }

            if (burst.progress < 1000)
            {
                for (int i = 0; i < 10; i++)
                {
                    int size = burst.size.x * burst.progress / 500;
                    double angle = i;
                    XYPosFloat pos (Angle (angle), size);
                    pos.y += burst.progress * burst.progress * size / 1000000;
                    SDL_Rect src_rect = {512, 960, 192, 192};
                    SDL_Rect dst_rect = {burst.pos.x + burst.size.x / 2 - size / 2 + int(pos.x), burst.pos.y + burst.size.y / 2 - size / 2 + int(pos.y), size, size};
                    SDL_Point rot_center = {size / 2, size / 2};
                    double star_angle = burst.progress / 100.0;
                    SDL_SetTextureAlphaMod(sdl_texture, 250 - burst.progress / 4);

                    SDL_RenderCopyEx(sdl_renderer, sdl_texture, &src_rect, &dst_rect, degrees(star_angle), &rot_center, SDL_FLIP_NONE);
                }
            }
            for (int i = 0; i < 8; i++)
            {
                int size = burst.size.x / 10;
                int p = int(i * 1000 / 4 + burst.progress) % 1000;
                SDL_Rect src_rect = {512, 960, 192, 192};
                SDL_Rect dst_rect = {0, 0, size, size};
                SDL_Point rot_center = {size / 2, size / 2};
                double star_angle = burst.progress / 300.0;
                XYPos pos(burst.pos.x - size / 2, burst.pos.y - size / 2);

                double t = (sin((i * 77) % 100 + burst.progress / 100) + 1) / 2;
                if (burst.progress > 4000)
                    t *= 1.0 - (burst.progress - 4000.0) / 1000;
                SDL_SetTextureAlphaMod(sdl_texture, t * 255);

                dst_rect.x = pos.x + (p * burst.size.x) / 1000;
                dst_rect.y = pos.y;
                SDL_RenderCopyEx(sdl_renderer, sdl_texture, &src_rect, &dst_rect, degrees(star_angle), &rot_center, SDL_FLIP_NONE);
                dst_rect.x = pos.x + burst.size.x;
                dst_rect.y = pos.y + (p * burst.size.y) / 1000;
                SDL_RenderCopyEx(sdl_renderer, sdl_texture, &src_rect, &dst_rect, degrees(star_angle), &rot_center, SDL_FLIP_NONE);
                dst_rect.x = pos.x + burst.size.x - (p * burst.size.x) / 1000;
                dst_rect.y = pos.y + burst.size.y;
                SDL_RenderCopyEx(sdl_renderer, sdl_texture, &src_rect, &dst_rect, degrees(star_angle), &rot_center, SDL_FLIP_NONE);
                dst_rect.x = pos.x;
                dst_rect.y = pos.y + burst.size.y - (p * burst.size.y) / 1000;
                SDL_RenderCopyEx(sdl_renderer, sdl_texture, &src_rect, &dst_rect, degrees(star_angle), &rot_center, SDL_FLIP_NONE);
            }

            it++;
        }
        star_burst_animations.clear();
        SDL_SetTextureAlphaMod(sdl_texture, 255);
    }

    if (display_modes)
    {
        tooltip_string = "";
        tooltip_rect = XYRect(-1,-1,-1,-1);
        render_box(left_panel_offset + XYPos(button_size, button_size), XYPos(10 * button_size, (GAME_MODES + 2) * button_size), button_size/4, 1);
        for (int i = 0; i < GAME_MODES; i++)
        {
            static const char* mode_names[4] = {"Regular", "Three region rules", "Max 60 rules", "No variables, max 300 rules"};
            std::string name = mode_names[i];
            {
                SDL_Rect src_rect = {2240, 576 + (i * 192), 192, 192};
                SDL_Rect dst_rect = {left_panel_offset.x + 2 * button_size, left_panel_offset.y + button_size * (2 + i), button_size, button_size};
                SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                add_tooltip(dst_rect, name.c_str());

            }

            if (i == game_mode)
                SDL_SetTextureColorMod(sdl_texture, 0, contrast, 0);
            render_text_box(left_panel_offset + XYPos(button_size * 3, button_size * (2 + i)), name);
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
            for (int i = 0; i < tutorial_page->get_count(); i++)
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
            SDL_Rect src_rect = {704, 1344, 192, 192};
            SDL_Rect dst_rect = {help_image_offset.x + help_image_size.x - sq_size * 3, help_image_offset.y + help_image_size.y - sq_size, sq_size, sq_size};
            if (!tutorial_index)
                src_rect.y += 192;
            else
                add_clickable_highlight(dst_rect);
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);

            dst_rect.x += sq_size;
            src_rect = {704 + 192, 1344, 192, 192};
            add_clickable_highlight(dst_rect);
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);

            dst_rect.x += sq_size;
            src_rect = {704 + 192 * 2, 1344, 192, 192};
            if (tutorial_index >= (tut_texture_count - 1))
                src_rect.y += 192;
            else
                add_clickable_highlight(dst_rect);
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
        }
    }
    if (display_menu)
    {
        tooltip_string = "";
        tooltip_rect = XYRect(-1,-1,-1,-1);
        render_box(left_panel_offset + XYPos(button_size, button_size), XYPos(10 * button_size, 11.8 * button_size), button_size/4, 1);
        {
            SDL_Rect src_rect = {full_screen ? 1472 : 1664, 1152, 192, 192};
            SDL_Rect dst_rect = {left_panel_offset.x + 2 * button_size, left_panel_offset.y + button_size * 2, button_size, button_size};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            add_clickable_highlight(dst_rect);
            std::string t = translate("Full Screen");
            render_text_box(left_panel_offset + XYPos(3.2 * button_size, 2.2 * button_size), t);
        }
        {
            SDL_Rect src_rect = {1280, 1344, 192, 192};
            SDL_Rect dst_rect = {left_panel_offset.x + 2 * button_size, left_panel_offset.y + button_size * 3, button_size, button_size};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            add_clickable_highlight(dst_rect);
            std::string t = translate("Select Language");
            render_text_box(left_panel_offset + XYPos(3.2 * button_size, 3.2 * button_size), t);
        }
        {
            SDL_Rect src_rect = {1664, show_row_clues ? 576 : 768, 192, 192};
            SDL_Rect dst_rect = {left_panel_offset.x + 2 * button_size, left_panel_offset.y + button_size * 4, button_size, button_size};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            add_clickable_highlight(dst_rect);
            std::string t = translate("Show Row Clues");
            render_text_box(left_panel_offset + XYPos(3.2 * button_size, 4.2 * button_size), t);
        }
        {
            SDL_Rect src_rect = {low_contrast ? 1856 : 2048, 1920, 192, 192};
            SDL_Rect dst_rect = {left_panel_offset.x + 2 * button_size, left_panel_offset.y + button_size * 5, button_size, button_size};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            add_clickable_highlight(dst_rect);
            std::string t = translate("Low Contrast");
            render_text_box(left_panel_offset + XYPos(3.2 * button_size, 5.2 * button_size), t);
        }
        {
            SDL_Rect src_rect = {2432, 1152, 192, 192};
            SDL_Rect dst_rect = {left_panel_offset.x + 2 * button_size, left_panel_offset.y + button_size * 6, button_size, button_size};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            add_clickable_highlight(dst_rect);
            std::string t = translate("Remap Keys");
            render_text_box(left_panel_offset + XYPos(3.2 * button_size, 6.2 * button_size), t);
        }
        {
            SDL_Rect src_rect = {2048, 576, 192, 576};
            SDL_Rect dst_rect = {left_panel_offset.x + 9 * button_size, left_panel_offset.y + button_size * 2, button_size, button_size * 3};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            add_tooltip(dst_rect, "Volume", false);

            src_rect = {2048, 1152, 192, 64};
            dst_rect = {left_panel_offset.x + 9 * button_size, left_panel_offset.y + button_size * 2 + int((1 - volume) * 2.6666 * button_size), button_size, button_size / 3};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
        }

        {
            SDL_Rect src_rect = {1664, 960, 192, 192};
            SDL_Rect dst_rect = {left_panel_offset.x + 2 * button_size, left_panel_offset.y + button_size * 9, button_size, button_size};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            add_clickable_highlight(dst_rect);
            std::string t = translate("Reset Levels");
            render_text_box(left_panel_offset + XYPos(3.2 * button_size, 9.2 * button_size), t);
        }
        {
            SDL_Rect src_rect = {1664, 960, 192, 192};
            SDL_Rect dst_rect = {left_panel_offset.x + 2 * button_size, left_panel_offset.y + button_size * 10, button_size, button_size};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            add_clickable_highlight(dst_rect);
            std::string t = translate("Reset Rules");
            render_text_box(left_panel_offset + XYPos(3.2 * button_size, 10.2 * button_size), t);
        }
        {
            SDL_Rect src_rect = {1280, 1728, 192, 192};
            SDL_Rect dst_rect = {left_panel_offset.x + 2 * button_size, left_panel_offset.y + button_size * 11, button_size, button_size};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            add_clickable_highlight(dst_rect);
            std::string t = translate("Quit");
            render_text_box(left_panel_offset + XYPos(3.2 * button_size, 11.2 * button_size), t);
        }
        {
            SDL_Rect src_rect = {704, 384, 192, 192};
            SDL_Rect dst_rect = {left_panel_offset.x + 9 * button_size, left_panel_offset.y + button_size * 11, button_size, button_size};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            add_tooltip(dst_rect, "OK");
        }
    }
    if (display_reset_confirm)
    {
        tooltip_string = "";
        tooltip_rect = XYRect(-1,-1,-1,-1);
        render_box(left_panel_offset + XYPos(button_size, button_size), XYPos(10 * button_size, 10 * button_size), button_size/4, 1);
        {
            std::string t = display_reset_confirm_levels_only ? translate("Reset Levels") : translate("Reset Rules");
            render_text_box(left_panel_offset + XYPos(4 * button_size, 3 * button_size), t);
            {
                SDL_Rect src_rect = {704, 384, 192, 192};
                SDL_Rect dst_rect = {left_panel_offset.x + 3 * button_size, left_panel_offset.y + button_size * 6, button_size, button_size};
                SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                add_tooltip(dst_rect, "OK");
            }
            {
                SDL_Rect src_rect = { 704, 576, 192, 192 };
                SDL_Rect dst_rect = {left_panel_offset.x + 7 * button_size, left_panel_offset.y + button_size * 6, button_size, button_size};
                SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                add_tooltip(dst_rect, "Cancel");
            }

        }
    }

    if (display_key_select)
    {
        tooltip_string = "";
        tooltip_rect = XYRect(-1,-1,-1,-1);
        render_box(left_panel_offset + XYPos(button_size, button_size), XYPos(10 * button_size, 10.5 * button_size), button_size/4, 1);
        {
            std::string t = "Remap Keys";
            render_text_box(left_panel_offset + XYPos(1.5 * button_size, 1.5 * button_size), t);
            {
                SDL_Rect src_rect = {704, 960, 192, 192};
                SDL_Rect dst_rect = {left_panel_offset.x + 2 * button_size, left_panel_offset.y + button_size * 3, button_size, button_size};
                SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                add_tooltip(dst_rect, "Help");
            }
            {
                SDL_Rect src_rect = {1472, 960, 192, 192};
                SDL_Rect dst_rect = {left_panel_offset.x + 2 * button_size, left_panel_offset.y + button_size * 4, button_size, button_size};
                SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                add_tooltip(dst_rect, "Hint");
            }
            {
                SDL_Rect src_rect = {704 + 192 * 3, 960, 192, 192};
                SDL_Rect dst_rect = {left_panel_offset.x + 2 * button_size, left_panel_offset.y + button_size * 5, button_size, button_size};
                SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                add_tooltip(dst_rect, "Next Level");
            }
            {
                SDL_Rect src_rect = {704 + 192 * 2, 960, 192, 192};
                SDL_Rect dst_rect = {left_panel_offset.x + 2 * button_size, left_panel_offset.y + button_size * 6, button_size, button_size};
                SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                add_tooltip(dst_rect, "Refresh Regions");
            }
            {
                SDL_Rect src_rect = {1472, 1152, 192, 192};
                SDL_Rect dst_rect = {left_panel_offset.x + 2 * button_size, left_panel_offset.y + button_size * 7, button_size, button_size};
                SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                add_tooltip(dst_rect, "Full Screen");
            }
            {
                SDL_Rect src_rect = {2432, 576, 192, 192};
                SDL_Rect dst_rect = {left_panel_offset.x + 6 * button_size, left_panel_offset.y + button_size * 3, button_size, button_size};
                SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                add_tooltip(dst_rect, "Visible");
            }
            {
                SDL_Rect src_rect = {2432, 576 + 192, 192, 192};
                SDL_Rect dst_rect = {left_panel_offset.x + 6 * button_size, left_panel_offset.y + button_size * 4, button_size, button_size};
                SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                add_tooltip(dst_rect, "Hidden");
            }
            {
                SDL_Rect src_rect = {2432, 576 + 192 * 2, 192, 192};
                SDL_Rect dst_rect = {left_panel_offset.x + 6 * button_size, left_panel_offset.y + button_size * 5, button_size, button_size};
                SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                add_tooltip(dst_rect, "Trash");
            }
            {
                SDL_Rect src_rect = {1856, 768, 192, 192};
                SDL_Rect dst_rect = {left_panel_offset.x + 6 * button_size, left_panel_offset.y + button_size * 6, button_size, button_size};
                SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                add_tooltip(dst_rect, "Undo");
            }
            {
                SDL_Rect src_rect = {1856, 960, 192, 192};
                SDL_Rect dst_rect = {left_panel_offset.x + 6 * button_size, left_panel_offset.y + button_size * 7, button_size, button_size};
                SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                add_tooltip(dst_rect, "Redo");
            }



            {
                SDL_Rect src_rect = {704, 384, 192, 192};
                SDL_Rect dst_rect = {left_panel_offset.x + 9 * button_size, left_panel_offset.y + button_size * 9, button_size, button_size};
                SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                add_tooltip(dst_rect, "OK");
            }

            for (int i = 0; i < KEY_CODE_TOTAL; i++)
            {
                std::string s = SDL_GetKeyName(key_codes[i]);
                if (i == capturing_key)
                    s = "?";
                
                render_text_box(left_panel_offset + XYPos((3.2 + (i / 5) * 4) * button_size, (3 + (i % 5)) * button_size), s);
            }

        }
    }

    if (display_language_chooser)
    {
        tooltip_string = "";
        tooltip_rect = XYRect(-1,-1,-1,-1);
        render_box(left_panel_offset + XYPos(button_size, button_size), XYPos(5 * button_size, 10 * button_size), button_size/4, 1);
        int index = 0;
        std::string orig_lang = language;
        for (std::map<std::string, SaveObject*>::iterator it = lang_data->omap.begin(); it != lang_data->omap.end(); ++it)
        {
            std::string s = it->first;
            set_language(s);
            if (s == orig_lang)
                SDL_SetTextureColorMod(sdl_texture, 0, contrast, 0);
            render_text_box(left_panel_offset + XYPos(button_size * 2, button_size * (2 + index)), s);
            SDL_SetTextureColorMod(sdl_texture, contrast, contrast, contrast);
            index++;
        }
        set_language(orig_lang);
    }


    render_tooltip();
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
        grid_dragging = true;
        grid_dragging_btn = btn;
        grid_dragging_last_pos = mouse;
    }
}

void GameState::left_panel_click(XYPos pos, int clicks, int btn)
{
    const int trim = 27;
    int digit_height = button_size * 0.75;
    int digit_width = (digit_height * (192 - trim * 2)) / 192;

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
        get_hint = false;
    }
    if ((pos - XYPos(button_size * 0, button_size * 1)).inside(XYPos(button_size * 3, button_size)))
    {
        dragging_speed = true;
        double p = double(mouse.x - left_panel_offset.x - (button_size / 6)) / (button_size * 2.6666);
        speed_dial = std::clamp(p, 0.0, 1.0);
    }
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
        }
    }

    if ((pos - XYPos(button_size * 0, button_size * 5)).inside(XYPos(button_size * (GLBAL_LEVEL_SETS + 1), button_size)))
    {
        int x = ((pos - XYPos(button_size * 0, button_size * 5)) / button_size).x;
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
        }
    }

    XYPos gpos = pos / button_size;
    gpos.y -= 6;
    int idx = gpos.x + gpos.y * 5;

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
        }
    }
}

void GameState::right_panel_click(XYPos pos, int clicks, int btn)
{
    XYPos bpos = pos / button_size;

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
            }
            return;
        }
        if ((pos - XYPos(button_size * 1, button_size * 4)).inside(XYPos(button_size, button_size)))
        {
            if(inspected_region->visibility_force == GridRegion::VIS_FORCE_USER)
            {
                inspected_region->visibility_force = GridRegion::VIS_FORCE_NONE;
            }
            else if(inspected_region->vis_cause.rule)
            {
                right_panel_mode = RIGHT_MENU_RULE_INSPECT;
                inspected_rule = inspected_region->vis_cause;
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
                inspected_region->visibility_force = GridRegion::VIS_FORCE_USER;
                inspected_region->vis_cause.rule = NULL;
                inspected_region->stale = false;
            }
            if ((pos - XYPos(button_size * 3, button_size * 4)).inside(XYPos(button_size, button_size)))
            {
                inspected_region->vis_level = GRID_VIS_LEVEL_HIDE;
                inspected_region->visibility_force = GridRegion::VIS_FORCE_USER;
                inspected_region->vis_cause.rule = NULL;
                inspected_region->stale = false;
            }
            if ((pos - XYPos(button_size * 3, button_size * 5)).inside(XYPos(button_size, button_size)))
            {
                inspected_region->vis_level = GRID_VIS_LEVEL_BIN;
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
        }
        if ((pos - XYPos(button_size * 4, button_size)).inside(XYPos(button_size, button_size)) && !inspected_rule.rule->deleted)
        {
            inspected_rule.rule->deleted = true;
            right_panel_mode = RIGHT_MENU_NONE;
            if (inspected_rule.rule->apply_region_type.type != RegionType::VISIBILITY && (game_mode == 2 || game_mode == 3))
                rule_del_count[game_mode]++;
        }
        if ((pos - XYPos(button_size * 3, button_size * 2)).inside(XYPos(button_size * 2, button_size)))
        {
            reset_rule_gen_region();
            constructed_rule = *inspected_rule.rule;
            constructed_rule.used_count = 0;
            constructed_rule.clear_count = 0;
            constructed_rule.level_used_count = 0;
            constructed_rule.level_clear_count = 0;

            rule_gen_region[0] = inspected_rule.regions[0];
            rule_gen_region[1] = inspected_rule.regions[1];
            rule_gen_region[2] = inspected_rule.regions[2];
            rule_gen_region[3] = inspected_rule.regions[3];
            constructed_rule.deleted = false;
            constructed_rule.stale = false;
            if ((pos - XYPos(button_size * 3, button_size * 2)).inside(XYPos(button_size, button_size)))
            {
                replace_rule = inspected_rule.rule;
            }
            right_panel_mode = RIGHT_MENU_RULE_GEN;
            update_constructed_rule();
        }
        if ((pos - XYPos(button_size * 3, button_size * 6)).inside(XYPos(button_size, button_size)))
        {
            SaveObjectMap* omap = new SaveObjectMap;
            omap->add_item("rule", inspected_rule.rule->save(true));
            send_to_clipboard(omap);
            delete omap;
        }
        if ((pos - XYPos(button_size * 4, button_size * 6)).inside(XYPos(button_size, button_size)))
        {
            send_rule_to_img_clipboard(*inspected_rule.rule);
        }
        if (inspected_rule.rule->apply_region_type.type != RegionType::VISIBILITY)
        {
            if (prog_seen[PROG_LOCK_PRIORITY])
            {
                if ((pos - XYPos(button_size * 0, button_size * 6)).inside(XYPos(button_size, button_size * 6)))
                {
                    int y = ((pos - XYPos(button_size * 0, button_size * 6)) / button_size).y;
                    int np = 2 - y;
                    np = std::clamp(np, -3, 2);
                    if (inspected_rule.rule->priority < -100)
                    {
                        inspected_rule.rule->stale = false;
                        if (np == -3)
                            inspected_rule.rule->priority = 0;
                        else
                            inspected_rule.rule->priority = np;
                    }
                    else
                    {
                        if (np == -3)
                            inspected_rule.rule->priority = -128;
                        else
                            inspected_rule.rule->priority = np;
                    }
                    
                }
            }
        }
        else
        {
            if (prog_seen[PROG_LOCK_PRIORITY])
            {
                if ((pos - XYPos(button_size * 0, button_size * 11)).inside(XYPos(button_size, button_size)))
                {
                    if (inspected_rule.rule->priority < -100)
                    {
                        inspected_rule.rule->stale = false;
                        inspected_rule.rule->priority = 0;
                    }
                    else
                    {
                        inspected_rule.rule->priority = -128;
                    }
                }
            }
        }
    }

    if (right_panel_mode == RIGHT_MENU_NONE)
    {
         if ((pos - XYPos(button_size * 3, button_size * 6)).inside(XYPos(button_size, button_size)))
            export_all_rules_to_clipboard();
    }

    if (right_panel_mode != RIGHT_MENU_RULE_GEN)
    {
        if ((pos - XYPos(button_size * 1, button_size * 6)).inside(XYPos(button_size, button_size)))
        {
            if(constructed_rule.region_count)
            {
                right_panel_mode = RIGHT_MENU_RULE_GEN;
                return;
            }
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
        }
    }
    if (btn)
        return;

    if (right_panel_mode == RIGHT_MENU_RULE_GEN)
    {
        if ((pos - XYPos(button_size * 0, button_size * 6)).inside(XYPos(button_size,button_size)))
            region_type = RegionType(RegionType::NONE, 0);
        if ((pos - XYPos(button_size * 1, button_size * 6)).inside(XYPos(button_size,button_size)))
            region_type = RegionType(RegionType::SET, 0);
        if ((pos - XYPos(button_size * 2, button_size * 6)).inside(XYPos(button_size,button_size)))
            region_type = RegionType(RegionType::SET, 1);
        if (prog_seen[PROG_LOCK_VISIBILITY])
        {
            if ((pos - XYPos(button_size * 3, button_size * 6)).inside(XYPos(button_size,button_size)))
                region_type = RegionType(RegionType::VISIBILITY, 1);
            if ((pos - XYPos(button_size * 4, button_size * 6)).inside(XYPos(button_size,button_size)))
                region_type = RegionType(RegionType::VISIBILITY, 2);
        }
        if(prog_seen[PROG_LOCK_NUMBER_TYPES])
        {
            if ((pos - XYPos(button_size * 0, button_size * 7.3)).inside(XYPos(5 * button_size, 2 * button_size)))
            {
                XYPos region_item_selected = (pos - XYPos(0, button_size * 7.3)) / button_size;
                RegionType::Type t = menu_region_types[region_item_selected.y][region_item_selected.x];
                if (t == RegionType::NONE)
                {
                }
                else
                {
                    select_region_type.type = t;
                    region_type = select_region_type;
                }
            }

            if ((pos - XYPos(button_size * 0, button_size * 9.6)).inside(XYPos(5 * button_size, 2 * button_size)))
            {
                XYPos region_item_selected = (pos - XYPos(0, button_size * 9.6)) / button_size;
                select_region_type.value = region_item_selected.x + (region_item_selected.y) * 5;
                region_type = select_region_type;
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
            if (hover_rulemaker_bits >= 1 && hover_rulemaker_bits < (1 << constructed_rule.region_count))
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
                        if ((constructed_rule.square_counts[hover_rulemaker_bits] == RegionType(RegionType::NONE, 0)) ||
                            (constructed_rule.square_counts[hover_rulemaker_bits] == RegionType(RegionType::EQUAL, 0) && square_counts[hover_rulemaker_bits]))
                            new_region_type = RegionType(RegionType::EQUAL, 0);
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
                if (constructed_rule_is_logical == GridRule::OK)
                {
                    if (constructed_rule_is_already_present)
                    {
                        inspected_rule = GridRegionCause(constructed_rule_is_already_present, NULL, NULL, NULL, NULL);
                        reset_rule_gen_region();
                        right_panel_mode = RIGHT_MENU_RULE_INSPECT;
                    }
                    else
                    {
                        if (replace_rule)
                        {
                            if (game_mode == 2 || game_mode == 3)
                            {
                                if (replace_rule->apply_region_type.type != RegionType::VISIBILITY)
                                    rule_del_count[game_mode]++;
                            }
                            *replace_rule = constructed_rule;
                            inspected_rule = GridRegionCause(replace_rule, rule_gen_region[0], rule_gen_region[1], rule_gen_region[2], rule_gen_region[3]);
                            reset_rule_gen_region();
                            right_panel_mode = RIGHT_MENU_RULE_INSPECT;
                        }
                        else if (rule_is_permitted(constructed_rule, game_mode))
                        {
                            rules[game_mode].push_back(constructed_rule);
                            inspected_rule = GridRegionCause(&rules[game_mode].back(), rule_gen_region[0], rule_gen_region[1], rule_gen_region[2], rule_gen_region[3]);
                            reset_rule_gen_region();
                            right_panel_mode = RIGHT_MENU_RULE_INSPECT;
                        }
                    }
                }
            }
        }

        if ((pos - XYPos(button_size * 3, button_size * 1)).inside(XYPos(button_size, button_size)))
        {
            reset_rule_gen_region();
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
                        dragging_volume = false;
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
                if (capturing_key >= 0)
                {
                    key_codes[capturing_key] = e.key.keysym.sym;
                    capturing_key = -1;
                    break;
                }
                if (display_help || display_language_chooser || display_menu || display_key_select)
                {
                    if ((e.key.keysym.sym == key_codes[KEY_CODE_F1]) ||
                        (e.key.keysym.sym == SDLK_ESCAPE))
                    {
                        display_help = false;
                        display_language_chooser = false;
                        display_key_select = false;
                        display_menu = false;
                    }
                    if (e.key.keysym.sym == key_codes[KEY_CODE_F11])
                    {
                        full_screen = !full_screen;
                        SDL_SetWindowFullscreen(sdl_window, full_screen? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
                        SDL_SetWindowBordered(sdl_window, full_screen ? SDL_FALSE : SDL_TRUE);
                        SDL_SetWindowResizable(sdl_window, full_screen ? SDL_FALSE : SDL_TRUE);
                        SDL_SetWindowInputFocus(sdl_window);
                    }
                    break;
                }
                int key = e.key.keysym.sym;
                {
                    if (key == key_codes[KEY_CODE_Z])
                        rule_gen_undo();
                    else if (key == key_codes[KEY_CODE_Y])
                        rule_gen_redo();
                    else if (key == key_codes[KEY_CODE_Q])
                        key_held = 'Q';
                    else if (key == key_codes[KEY_CODE_W])
                        key_held = 'W';
                    else if (key == key_codes[KEY_CODE_E])
                        key_held = 'E';
                    else if (key == SDLK_ESCAPE)
                        display_menu = true;
                    else if (key == key_codes[KEY_CODE_F1])
                        display_help = true;
                    else if (key == key_codes[KEY_CODE_F2])
                    {
                        clue_solves.clear();
                        XYSet grid_squares = grid->get_squares();
                        FOR_XY_SET(pos, grid_squares)
                        {
                            if (grid->is_determinable_using_regions(pos, true))
                                clue_solves.insert(pos);
                        }
                        get_hint = true;
                    }
                    else if (key == key_codes[KEY_CODE_F3])
                    {
                        skip_level = 1;
                        force_load_level = false;
                        break;
                    }
                    else if (key == key_codes[KEY_CODE_F4])
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
                    else if (key == key_codes[KEY_CODE_F11])
                    {
                        full_screen = !full_screen;
                        SDL_SetWindowFullscreen(sdl_window, full_screen? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
                        SDL_SetWindowBordered(sdl_window, full_screen ? SDL_FALSE : SDL_TRUE);
                        SDL_SetWindowResizable(sdl_window, full_screen ? SDL_FALSE : SDL_TRUE);
                        SDL_SetWindowInputFocus(sdl_window);
                        break;
                    }
                    else
                    {
                        printf("Uncaught key: %d\n", key);
                    }
                }
                break;
            }
            case SDL_KEYUP:
                if (e.key.keysym.sym == key_codes[KEY_CODE_Q] || 
                    e.key.keysym.sym == key_codes[KEY_CODE_W] |
                    e.key.keysym.sym == key_codes[KEY_CODE_E])
                        key_held = 0;
                break;
            case SDL_MOUSEMOTION:
            {
                mouse.x = e.motion.x;
                mouse.y = e.motion.y;
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
                if (dragging_volume)
                {
                    double p = 1.0 - double(mouse.y - left_panel_offset.y - (button_size * 2) - (button_size / 6)) / (button_size * 2.6666);
                    volume = std::clamp(p, 0.0, 1.0);
                    Mix_Volume(-1, volume * volume * SDL_MIX_MAXVOLUME);
                }
                break;
            }
            case SDL_MOUSEBUTTONUP:
            {
                display_rules_click_drag = false;
                grid_dragging = false;
                dragging_speed = false;
                dragging_volume = false;
                mouse.x = e.button.x;
                mouse.y = e.button.y;
                break;
            }
            case SDL_MOUSEBUTTONDOWN:
            {
                mouse.x = e.button.x;
                mouse.y = e.button.y;
                if (display_language_chooser)
                {
                    XYPos p = (mouse - left_panel_offset) / button_size;
                    p -= XYPos(2,2);
                    if (p.x >= 0 && p.y >= 0 && p.x < 5)
                    {
                        int index = 0;
                        for (std::map<std::string, SaveObject*>::iterator it = lang_data->omap.begin(); it != lang_data->omap.end(); ++it)
                        {
                            if (index == p.y)
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
                        int index = p.y + (p.x / 3) * 5;

                        if (index < KEY_CODE_TOTAL)
                            capturing_key = index;
                    }
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
                            if (display_reset_confirm_levels_only)
                                reset_levels();
                            else
                            {
                                rules[game_mode].clear();
                                if (game_mode == 2 || game_mode == 3)
                                    reset_levels();
                            }
                            display_reset_confirm = false;
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
                    if (p.x >= 0 && p.x <= 6 && p.y >= 0)
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
                    if (p.x == 7 && p.y >= 0 && p.y <= 3)
                    {
                        dragging_volume = true;
                        double p = 1.0 - double(mouse.y - left_panel_offset.y - (button_size * 2) - (button_size / 6)) / (button_size * 2.6666);
                        volume = std::clamp(p, 0.0, 1.0);
                        Mix_Volume(-1, volume * volume * SDL_MIX_MAXVOLUME);
                    }
                    if (p == XYPos(7,9))
                        display_menu = false;
                    break;
                }
                if (display_help)
                {
                    XYPos window_size;
                    SDL_GetWindowSize(sdl_window, &window_size.x, &window_size.y);

                    int sq_size = std::min(window_size.y / 9, window_size.x / 16);
                    XYPos help_image_size = XYPos(16 * sq_size, 9 * sq_size);
                    XYPos help_image_offset = (window_size - help_image_size) / 2;
                    if ((mouse - help_image_offset - help_image_size + XYPos(sq_size * 3, sq_size)).inside(XYPos(sq_size, sq_size)))
                        if (tutorial_index)
                            tutorial_index--;
                    if ((mouse - help_image_offset - help_image_size + XYPos(sq_size * 2, sq_size)).inside(XYPos(sq_size, sq_size)))
                        display_help = false;
                    if ((mouse - help_image_offset - help_image_size + XYPos(sq_size * 1, sq_size)).inside(XYPos(sq_size, sq_size)))
                        if (tutorial_index < (tut_texture_count - 1))
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
                            current_level_group_index = 0;
                            current_level_set_index = 0;
                            current_level_index = 0;
                            load_level = true;
                            force_load_level = false;
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
        inspected_region->visibility_force = GridRegion::VIS_FORCE_USER;
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
                SaveObjectList* lvls = omap->get_item("scores")->get_list();
                for (int i = 0; i <= GLBAL_LEVEL_SETS; i++)
                {
                    SaveObjectList* scores = lvls->get_item(i)->get_list();
                    int mode = omap->get_num("game_mode");
                    score_tables[mode][i].clear();
                    for (int j = 0; j < scores->get_count(); j++)
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
                for (int i = 0; i <= GLBAL_LEVEL_SETS; i++)
                {
                    SaveObjectList* stats1 = lvls->get_item(i)->get_list();
                    for (int j = 0; j < stats1->get_count() && j < level_progress[game_mode][i].size(); j++)
                    {
                        SaveObjectList* stats2 = stats1->get_item(j)->get_list();
                        for (int k = 0; k < stats2->get_count() && k < level_progress[game_mode][i][j].level_stats.size(); k++)
                        {
                            int64_t s = stats2->get_item(k)->get_num();
                            level_progress[game_mode][i][j].level_stats[k] = s;
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
                    server_levels_version = omap->get_num("server_levels_version");
                    SaveObjectList* lvl_sets = omap->get_item("server_levels")->get_list();
                    server_levels.clear();
                    server_levels.resize(lvl_sets->get_count());
                    for (int m = 0; m < GAME_MODES; m++)
                    {
                        level_progress[m][GLBAL_LEVEL_SETS].clear();
                        level_progress[m][GLBAL_LEVEL_SETS].resize(lvl_sets->get_count());
                    }
                    for (int k = 0; k < lvl_sets->get_count(); k++)
                    {
                        SaveObjectList* plist = lvl_sets->get_item(k)->get_list();
                        for (int m = 0; m < GAME_MODES; m++)
                        {
                            level_progress[m][GLBAL_LEVEL_SETS][k].level_status.resize(plist->get_count());
                            level_progress[m][GLBAL_LEVEL_SETS][k].count_todo = plist->get_count();
                            level_progress[m][GLBAL_LEVEL_SETS][k].level_stats.resize(plist->get_count());
                        }

                        for (int i = 0; i < plist->get_count(); i++)
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
    send_to_clipboard(omap);
    delete omap;
}

void GameState::send_to_clipboard(SaveObject* obj)
{
    std::ostringstream stream;
    obj->save(stream);
    std::string str = stream.str();

    std::string comp = compress_string_zstd(str);
    std::u32string s32;
    std::string reply = "";

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

    SDL_Texture* my_canvas = SDL_CreateTexture(sdl_renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, 500, 500);
    SDL_SetTextureBlendMode(my_canvas, SDL_BLENDMODE_BLEND);
    SDL_SetRenderTarget(sdl_renderer, my_canvas);
    SDL_RenderClear(sdl_renderer);

    render_rule(rule, XYPos(0, 0), 100, -1);

    uint32_t pixel_data[500 * 500];
    SDL_Rect dst_rect = {0, 0, 500, 500};
    SDL_RenderReadPixels(sdl_renderer, &dst_rect, SDL_PIXELFORMAT_BGRA8888, (void*)pixel_data, 500 * 4);


    uint32_t comp_size = comp.size();

    std::string siz_str = std::string(1, char(comp_size)) + std::string(1, char(comp_size>>8)) + std::string(1, char(comp_size>>16)) + std::string(1, char(comp_size>>24));
    comp = siz_str + comp;
    int offset = 32;
    comp_size = comp.size();
    for (int i = 0; i < comp_size; i++)
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

    clip::image_spec spec;
    spec.width = 500;
    spec.height = 500;
    spec.bits_per_pixel = 32;
    spec.bytes_per_row = spec.width*4;
    spec.red_mask = 0xff00;
    spec.green_mask = 0xff0000;
    spec.blue_mask = 0xff000000;
    spec.alpha_mask = 0xff;
    spec.red_shift = 8;
    spec.green_shift = 16;
    spec.blue_shift = 24;
    spec.alpha_shift = 0;
    clip::image img(pixel_data, spec);
    clip::set_image(img);

    SDL_DestroyTexture(my_canvas);
    SDL_SetRenderTarget(sdl_renderer, NULL);
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
//#ifndef TARGET_LINUX
    bool has_image = false;
    if (clip::has(clip::image_format()))
    {
        clip::image_spec spec;
        if (clip::get_image_spec(spec))
        {
            if (spec.width == 500 && spec.height == 500)
                has_image = true;
        }
    }

    if (has_image)
    {
        clip::image img;
        if (!clip::get_image(img))
            return;
        clip::image_spec spec = img.spec();
        uint32_t* dat = (uint32_t*) img.data();
        dat += 32;
        uint32_t comp_size = get_hidden_val(&dat, spec.red_shift, spec.green_shift, spec.blue_shift);
        comp_size += uint32_t(get_hidden_val(&dat, spec.red_shift, spec.green_shift, spec.blue_shift)) << 8;
        comp_size += uint32_t(get_hidden_val(&dat, spec.red_shift, spec.green_shift, spec.blue_shift)) << 16;
        comp_size += uint32_t(get_hidden_val(&dat, spec.red_shift, spec.green_shift, spec.blue_shift)) << 24;
        if (comp_size > (500*500/3))
            return;
        for (int i = 0; i < comp_size; i++)
        {
            comp += get_hidden_val(&dat, spec.red_shift, spec.green_shift, spec.blue_shift);
        }
    }
    else
//#endif
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
                for (int i = 0; i < rlist->get_count(); i++)
                {
                    GridRule r(rlist->get_item(i));
                    clipboard_rule_set.push_back(r);
                }
                clipboard_has_item = CLIPBOARD_HAS_RULE_SET;
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

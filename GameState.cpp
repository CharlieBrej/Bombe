#define _USE_MATH_DEFINES
#include <cmath>

#include "GameState.h"
#include "SaveState.h"
#include "Misc.h"
#include "LevelSet.h"
#include "Compress.h"


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

static Rand rnd(1);

GameState::GameState(std::string& load_data, bool json)
{
    LevelSet::init_global();
    bool load_was_good = false;

    for (int j = 0; j < GLBAL_LEVEL_SETS; j++)
    {
        level_progress[j].resize(global_level_sets[j].size());
        for (int i = 0; i < global_level_sets[j].size(); i++)
        {
            level_progress[j][i].level_status.resize(global_level_sets[j][i]->levels.size());
            level_progress[j][i].count_todo = global_level_sets[j][i]->levels.size();
        }
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
            if (omap->has_key("speed_dial"))
                speed_dial = double(omap->get_num("speed_dial")) / 1000;


            SaveObjectList* rlist = omap->get_item("rules")->get_list();
            for (int i = 0; i < rlist->get_count(); i++)
            {
                GridRule r(rlist->get_item(i), version);
//                if (r.is_legal())
                rules.push_back(r);
            }

            if (version == 4)
            {
                SaveObjectList* plist = omap->get_item("level_progress")->get_list();
                for (int i = 0; i < plist->get_count() && i < level_progress[1].size(); i++)
                {
                    std::string s = plist->get_string(i);
                    int lim = std::min(s.size(), level_progress[1][i].level_status.size());
                    for (int j = 0; j < lim; j++)
                    {
                        char c = s[j];
                        int stat = c - '0';
                        level_progress[1][i].level_status[j] = stat;
                        if (stat)
                            level_progress[1][i].count_todo--;
                    }
                }
            }
            if (version >= 5)
            {
                SaveObjectList* pplist = omap->get_item("level_progress")->get_list();
                for (int k = 0; k < GLBAL_LEVEL_SETS; k++)
                {
                    SaveObjectList* plist = pplist->get_item(k)->get_list();
                    for (int i = 0; i < plist->get_count() && i < level_progress[k].size(); i++)
                    {
                        if (version == 5 && k == 0 && i >= 20)
                            continue;
                        std::string s = plist->get_string(i);
                        int lim = std::min(s.size(), level_progress[k][i].level_status.size());
                        for (int j = 0; j < lim; j++)
                        {
                            char c = s[j];
                            int stat = c - '0';
                            level_progress[k][i].level_status[j] = stat;
                            if (stat)
                                level_progress[k][i].count_todo--;
                        }
                    }
                }
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
    SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 0);
	sdl_texture = loadTexture("texture.png");

    tutorial_texture[0] = loadTexture("tutorial/tut0.png");
    tutorial_texture[1] = loadTexture("tutorial/tut1.png");
    tutorial_texture[2] = loadTexture("tutorial/tut2.png");
    tutorial_texture[3] = loadTexture("tutorial/tut3.png");
    tutorial_texture[4] = loadTexture("tutorial/tut4.png");

    {
        SDL_Surface* icon_surface = IMG_Load("icon.png");
        SDL_SetWindowIcon(sdl_window, icon_surface);
	    SDL_FreeSurface(icon_surface);
    }


    for (std::map<std::string, SaveObject*>::iterator it = lang_data->omap.begin(); it != lang_data->omap.end(); ++it)
    {
        std::string s = it->first;
        std::string filename = lang_data->get_item(s)->get_map()->get_string("font");
        fonts[filename] = TTF_OpenFont(filename.c_str(), 32);
    }
    set_language(language);
    score_font = TTF_OpenFont("font-fixed.ttf", 19*4);

    Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048);
    Mix_AllocateChannels(32);

    sounds[0] = Mix_LoadWAV( "snd/plop0.wav" );
    sounds[1] = Mix_LoadWAV( "snd/plop1.wav" );
    sounds[2] = Mix_LoadWAV( "snd/plop2.wav" );
    sounds[3] = Mix_LoadWAV( "snd/plop3.wav" );
    sounds[4] = Mix_LoadWAV( "snd/plop4.wav" );
    sounds[5] = Mix_LoadWAV( "snd/plop5.wav" );
    sounds[6] = Mix_LoadWAV( "snd/plop6.wav" );
    sounds[7] = Mix_LoadWAV( "snd/plop7.wav" );
    Mix_Volume(-1, 40);


//    font = TTF_OpenFont("font-en.ttf", 32);

//    Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048);
//    Mix_AllocateChannels(32);

//    Mix_VolumeMusic(music_volume);
//    music = Mix_LoadMUS("music.ogg");
//    Mix_PlayMusic(music, -1);
    grid = Grid::Load("ABBA!");
//    grid->make_harder(true, true);

//    grid->print();

//     {
//         Grid t = *grid;
//         t.solve(1000000);
//     }

}

SaveObject* GameState::save(bool lite)
{
    SaveObjectMap* omap = new SaveObjectMap;
    omap->add_num("version", game_version);

    SaveObjectList* rlist = new SaveObjectList;
    for (GridRule& rule : rules)
    {
        if (!rule.deleted)
            rlist->add_item(rule.save());
    }
    omap->add_item("rules", rlist);

    SaveObjectList* pplist = new SaveObjectList;
    for (int j = 0; j < GLBAL_LEVEL_SETS; j++)
    {
        SaveObjectList* plist = new SaveObjectList;
        for (LevelProgress& prog : level_progress[j])
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
    omap->add_string("language", language);
    omap->add_num("level_group_index", current_level_group_index);
    omap->add_num("level_set_index", current_level_set_index);
    omap->add_num("show_row_clues", show_row_clues);
    omap->add_num("speed_dial", speed_dial * 1000);
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
}

bool GameState::level_is_accessible(unsigned set)
{
    if (IS_DEMO && set > 5 && (set % 5))
        return false;
    if (set == 0)
        return true;
    if (set >= 5 && level_progress[current_level_group_index][set - 5].count_todo < 150)
        return true;
    if ((set % 5 >= 1) && level_progress[current_level_group_index][set - 1].count_todo < 150)
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
    SaveObjectList* plist = new SaveObjectList;

    SaveObjectList* pplist = new SaveObjectList;
    for (int j = 0; j < GLBAL_LEVEL_SETS; j++)
    {
        int count = 0;
        for (int i = 0; i < level_progress[j].size(); i++)
        {
            LevelProgress& prog = level_progress[j][i];
            for (bool b : prog.level_status)
                if (b)
                    count++;
        }
        pplist->add_num(count);
    }
    omap->add_item("scores", pplist);
    SaveObjectList* slist = new SaveObjectList;
    for (uint64_t f : steam_friends)
        slist->add_num(f);
    omap->add_item("friends", slist);
    fetch_from_server(omap, &scores_from_server);
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


void GameState::advance(int steps)
{
    frame = frame + steps;
    frame_step = steps;
    sound_frame_index = std::min(sound_frame_index + steps, 500);
    deal_with_scores();
    if (server_timeout)
        server_timeout-=steps;

    if (grid->is_solved() || skip_level)
    {
        clue_solves.clear();
        if (!skip_level && !current_level_is_temp)
        {
            bool o = level_progress[current_level_group_index][current_level_set_index].level_status[current_level_index];
            if (!o)
            {
                level_progress[current_level_group_index][current_level_set_index].count_todo--;
                level_progress[current_level_group_index][current_level_set_index].level_status[current_level_index] = true;
            }
        }

        if (level_progress[current_level_group_index][current_level_set_index].count_todo)
        {
            if (!current_level_is_temp || level_progress[current_level_group_index][current_level_set_index].level_status[current_level_index])
            {
                do
                {
                    if (skip_level < 0)
                    {
                        if (current_level_index == 0)
                        {
                            current_level_index = level_progress[current_level_group_index][current_level_set_index].level_status.size();
                        }
                        current_level_index--;
                    }
                    else
                    {
                        current_level_index++;
                        if (current_level_index >= level_progress[current_level_group_index][current_level_set_index].level_status.size())
                        {
                            auto_progress = false;
                            current_level_index = 0;
                        }
                    }
                }
                while (level_progress[current_level_group_index][current_level_set_index].level_status[current_level_index]);
            }

            std::string& s = global_level_sets[current_level_group_index][current_level_set_index]->levels[current_level_index];
            delete grid;
            grid = Grid::Load(s);
            reset_rule_gen_region();
            grid_cells_animation.clear();
            grid_regions_animation.clear();
            grid_regions_fade.clear();
            current_level_is_temp = false;
        }
        skip_level = 0;
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
        if (!get_hint)
        {
            for (GridRegion& r : grid->regions)
            {
                if (r.visibility_force == GridRegion::VIS_FORCE_HINT)
                    r.visibility_force = GridRegion::VIS_FORCE_NONE;
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

    for (GridRule& rule : rules)
    {
        if (rule.deleted)
            continue;
        if (rule.stale)
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
                if (!region.stale)
                {
                    for (GridRule& rule : rules)
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
                        for (GridRule& rule : rules)
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
                Mix_PlayChannel(rnd % 32, sounds[rnd % 8], 0);
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
                for (GridRule& rule : rules)
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
        
        if (grid->add_one_new_region())
        {
            for (GridRegion& region : grid->regions)
            {
                if (!region.stale)
                {
                    for (int i = 1; i < 3; i++)
                    {
                        for (GridRule& rule : rules)
                        {
                            if (rule.deleted)
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
    constructed_rule_is_logical = constructed_rule.is_legal();
    constructed_rule_is_already_present = NULL;

    std::vector<int> order;
    for(int i = 0; i < constructed_rule.region_count; i++)
        order.push_back(i);
    do{

        GridRule prule = constructed_rule.permute(order);
        for (GridRule& rule : rules)
        {
            if (rule.covers(prule))
            {
                constructed_rule_is_already_present = &rule;
            }
        }
    }
    while(std::next_permutation(order.begin(),order.end()));
}

static void set_region_colour(SDL_Texture* sdl_texture, unsigned type, unsigned col, unsigned fade)
{
    uint8_t r = (type & 1) ? 127 : 255;
    uint8_t g = (type & 2) ? 127 : 255;
    uint8_t b = (type & 4) ? 127 : 255;

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

void GameState::render_region_bg(GridRegion& region, std::map<XYPos, int>& taken, std::map<XYPos, int>& total_taken, XYPos wrap_size, int disp_type)
{
    std::vector<XYPos> elements;
    std::vector<int> element_sizes;
    XYPos wrap_start = (wrap_size == XYPos()) ? XYPos(0, 0) : XYPos(-2, -2);
    XYPos wrap_end = (wrap_size == XYPos()) ? XYPos(1, 1) : XYPos(5, 5);

    unsigned anim_prog = grid_regions_animation[&region];
    unsigned max_anim_frame = 500;
    double fade = 1.0 - (double(anim_prog) / double(max_anim_frame));
    int opac = std::min(int(255.0 * (1.0 - pow(fade, 0.5))), int(50 + (grid_regions_fade[&region] * 205 / max_anim_frame)));

    FOR_XY_SET(pos, region.elements)
    {
        XYRect d = grid->get_bubble_pos(pos, grid_pitch, taken[pos], total_taken[pos]);
        XYPos n = d.pos + d.size / 2;
        elements.push_back(n);
        element_sizes.push_back(std::min(d.size.x, d.size.y));
        taken[pos]++;
        FOR_XY(r, wrap_start, wrap_end)
        {
            if (XYPosFloat(mouse - wrap_size * r - scaled_grid_offset - d.pos - d.size / 2).distance() <= (d.size.x / 2))
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

    bool selected = (&region == mouse_hover_region);
    int siz = elements.size();

    std::vector<int> group;
    std::vector<std::vector<std::pair<double, XYPos>>> distances;
    distances.resize(siz);
    group.resize(siz);


    for (int i = 0; i < siz; i++)
    {
        group[i] = i;
        distances[i].resize(siz);
        for (int j = 0; j < siz; j++)
        {
            distances[i][j] = std::make_pair(std::numeric_limits<double>::infinity(), XYPos());
            FOR_XY(p, wrap_start, wrap_end)
            {
                double dist = XYPosFloat(elements[i]).distance(XYPosFloat(elements[j]) + p * wrap_size);
                if (dist < distances[i][j].first)
                    distances[i][j] = std::make_pair(dist, p);

            }
        }
    }

    while (true)
    {
        XYPos best_con;
        XYPos best_grid;
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
                    best_grid = distances[i][j].second;
                }
            }
        }
        {
            XYPos pos = elements[best_con.x];
            XYPos last = elements[best_con.y] + best_grid * wrap_size;
            double dist = XYPosFloat(pos).distance(XYPosFloat(last));
            double angle = XYPosFloat(pos).angle(XYPosFloat(last));
            int s = std::min(element_sizes[best_con.x], element_sizes[best_con.y]);
            int line_thickness = std::min(s/4, (scaled_grid_size / std::min(grid->size.x, grid->size.y)) / 32);


            if ((disp_type == 1) && selected)
            {
                set_region_colour(sdl_texture, region.type.value, region.colour, opac);
                SDL_Point rot_center = {0, line_thickness * 2};
                double f = (frame / 5 + pos.x);
                SDL_Rect src_rect1 = {int(f)% 1024, 2688, std::min(int(dist / line_thickness * 10), 1024), 32};
                f = (frame / 8 + pos.x);
                SDL_Rect src_rect2 = {1024 - int(f) % 1024, 2688, std::min(int(dist / line_thickness * 10), 1024), 32};
                FOR_XY(r, wrap_start, wrap_end)
                {
                    SDL_Rect dst_rect = {wrap_size.x * r.x + scaled_grid_offset.x + pos.x, wrap_size.y * r.y + scaled_grid_offset.y + pos.y - line_thickness * 2, int(dist), line_thickness * 4};
                    SDL_RenderCopyEx(sdl_renderer, sdl_texture, &src_rect1, &dst_rect, degrees(angle), &rot_center, SDL_FLIP_NONE);
                    SDL_RenderCopyEx(sdl_renderer, sdl_texture, &src_rect2, &dst_rect, degrees(angle), &rot_center, SDL_FLIP_NONE);
                }
            }

            if ((disp_type == 0) || ((disp_type == 2) && selected))
            {
                set_region_colour(sdl_texture, region.type.value, region.colour, opac);
                SDL_Point rot_center = {0, line_thickness};
                SDL_Rect src_rect = {160, 608, 1, 1};
                FOR_XY(r, wrap_start, wrap_end)
                {
                    SDL_Rect dst_rect = {wrap_size.x * r.x + scaled_grid_offset.x + pos.x, wrap_size.y * r.y + scaled_grid_offset.y + pos.y - line_thickness, int(dist), line_thickness * 2};
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

    SDL_SetTextureColorMod(sdl_texture, 255, 255, 255);
}

void GameState::render_region_fg(GridRegion& region, std::map<XYPos, int>& taken, std::map<XYPos, int>& total_taken, XYPos wrap_size, int disp_type)
{
    XYPos wrap_start = (wrap_size == XYPos()) ? XYPos(0, 0) : XYPos(-2, -2);
    XYPos wrap_end = (wrap_size == XYPos()) ? XYPos(1, 1) : XYPos(5, 5);
    bool selected = (&region == mouse_hover_region);

    unsigned anim_prog = grid_regions_animation[&region];
    unsigned max_anim_frame = 500;
    double fade = 1.0 - (double(anim_prog) / double(max_anim_frame));
    double wob = -sin((10 / (fade + 0.3))) * (fade * fade) / 2;
    int opac = std::min(int(255.0 * (1.0 - pow(fade, 0.5))), int(50 + (grid_regions_fade[&region] * 205 / max_anim_frame)));

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
            SDL_Point rot_center = {d.size.x / 2 + margin.x, d.size.y / 2 + margin.x};
            FOR_XY(r, wrap_start, wrap_end)
            {
                SDL_Rect dst_rect = {wrap_size.x * r.x + scaled_grid_offset.x + d.pos.x - margin.x, wrap_size.y * r.y + scaled_grid_offset.y + d.pos.y - margin.y, d.size.x + margin.x * 2, d.size.y + margin.x * 2};
                SDL_RenderCopyEx(sdl_renderer, sdl_texture, &src_rect, &dst_rect, (double(frame) / 10000) * 360, &rot_center, SDL_FLIP_NONE);
            }
        }
        if ((disp_type == 0) || ((disp_type == 2) && selected))
        {
            set_region_colour(sdl_texture, region.type.value, region.colour, opac);
            SDL_Rect src_rect = {64, 512, 192, 192};
            FOR_XY(r, wrap_start, wrap_end)
            {
                SDL_Rect dst_rect = {wrap_size.x * r.x + scaled_grid_offset.x + d.pos.x, wrap_size.y * r.y + scaled_grid_offset.y + d.pos.y, d.size.x, d.size.y};
                SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            }

            SDL_SetTextureColorMod(sdl_texture, 0,0,0);
            FOR_XY(r, wrap_start, wrap_end)
            {
                render_region_type(region.type, wrap_size * r + scaled_grid_offset + d.pos, d.size.x);
            }
        }
    }

    SDL_SetTextureColorMod(sdl_texture, 255, 255, 255);

}

void GameState::render_text_box(XYPos pos, std::string& s, bool left)
{
    SDL_Color color = {0xFF, 0xFF, 0xFF};
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

void GameState::add_tooltip(SDL_Rect& dst_rect, const char* text, bool clickable)
{
    if ((mouse.x >= dst_rect.x) &&
        (mouse.x < (dst_rect.x + dst_rect.w)) &&
        (mouse.y >= dst_rect.y) &&
        (mouse.y < (dst_rect.y + dst_rect.h)))
    {
        tooltip_string = text;
        if (clickable)
            tooltip_rect = XYRect(dst_rect.x, dst_rect.y, dst_rect.w, dst_rect.h);
    }
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
void GameState::render_number_string(std::string digits, XYPos pos, XYPos siz)
{
    int width = 0;
    bool first_dig = true;

    for(char& c : digits)
    {
        if (c == '+' || c == '-')
        {
            width += 3;
        }
        else if (c == '!' )
        {
            width += 2;
        }
        else
        {
            if (first_dig && c == '1')
                width += 2;
            else
                width += 4;
        }
        first_dig = false;
    }

    XYPos t_size;
    if ((width * siz.y / 6) > siz.x)
        t_size = XYPos(siz.x, (siz.x * 6) / width);
    else
        t_size = XYPos(width * siz.y / 6, siz.y);

    pos += (siz - t_size) / 2;
    XYPos digit_size = XYPos((t_size.y * 2) / 3, t_size.y);
    first_dig = true;
    for(char& c : digits)
    {
        int digit = c - '0';
        SDL_Rect src_rect = {32 + (digit) * 192, 0, 128, 192};
        SDL_Rect dst_rect = {pos.x, pos.y, digit_size.x, digit_size.y};
        if (c == '!')
        {
            dst_rect.w = digit_size.x / 2;
            src_rect = {1280, 384, 64, 192};
        }
        if (c == '-')
        {
            dst_rect.w = digit_size.x * 3 / 4;
            src_rect = {304, 512, 96, 192};
        }
        if (c == '+')
        {
            dst_rect.w = digit_size.x * 3 / 4;
            src_rect = {432, 512, 96, 192};
        }
        if (first_dig && c == '1')
        {
            dst_rect.w = digit_size.x / 2;
            src_rect = {256, 0, 64, 192};
        }
        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
        pos.x += dst_rect.w;
        first_dig = false;
    }

}

void GameState::render_region_bubble(RegionType type, unsigned colour, XYPos pos, int siz, bool selected)
{
    set_region_colour(sdl_texture, type.value, colour, 255);
    if (selected)
    {
        int margin = siz / 8;
        SDL_Rect src_rect = {512, 1728, 192, 192};
        SDL_Point rot_center = {siz / 2 + margin, siz / 2 + margin};
        SDL_Rect dst_rect = {pos.x - margin, pos.y - margin, siz + margin * 2, siz + margin * 2};
        SDL_RenderCopyEx(sdl_renderer, sdl_texture, &src_rect, &dst_rect, (double(frame) / 10000) * 360, &rot_center, SDL_FLIP_NONE);
    }   
    SDL_Rect src_rect = {64, 512, 192, 192};
    SDL_Rect dst_rect = {pos.x, pos.y, int(siz), int(siz)};
    SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);

    SDL_SetTextureColorMod(sdl_texture, 0,0,0);
    render_region_type(type, pos, siz);
    SDL_SetTextureColorMod(sdl_texture, 255, 255, 255);
}

void GameState::render_region_type(RegionType reg, XYPos pos, unsigned siz)
{
    if (reg.type == RegionType::XOR22)
    {
        XYPos numsiz = XYPos(siz * 0.9 * 2 / 8, siz * 0.9 * 3 / 8);
        render_number(reg.value,     pos + XYPos(int(siz) / 8,int(siz) / 8), numsiz);
        render_number(reg.value + 2, pos - (numsiz / 2) + XYPos(int(siz) * 4 / 8,int(siz) * 4 / 8), numsiz);
        render_number(reg.value + 4, pos - numsiz + XYPos(int(siz) * 7 / 8,int(siz) * 7 / 8), numsiz);
        SDL_Rect src_rect = {384, 736, 128, 128};
        SDL_Rect dst_rect = {pos.x + int(siz) / 4 + int(siz) / 20, pos.y + int(siz) * 2 / 8,  int(siz / 6), int(siz / 3)};
        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);

        dst_rect = {pos.x + int(siz) / 2 + int(siz) / 20, pos.y + int(siz) * 7 / 16,  int(siz / 6), int(siz / 3)};
        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
    }
    else if (reg.type == RegionType::XOR222)
    {
        XYPos numsiz = XYPos(siz * 5 / 16, siz * 5 / 16);
        render_number(reg.value,     pos + XYPos(int(siz) / 8,int(siz) / 8), numsiz);
        render_number(reg.value + 2, pos + XYPos(int(siz) * 9 / 16,int(siz) / 8), numsiz);
        render_number(reg.value + 4, pos + XYPos(int(siz) / 8,int(siz) * 9 / 16), numsiz);
        render_number(reg.value + 6, pos + XYPos(int(siz) * 9 / 16,int(siz) * 9 / 16), numsiz);
        SDL_Rect src_rect = {1344, 384, 192, 192};
        SDL_Rect dst_rect = {pos.x + int(siz) / 8, pos.y + int(siz) / 8,  int(siz * 6 / 8), int(siz * 6 / 8)};
        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
    }
    else if (reg.type == RegionType::XOR2 || reg.type == RegionType::XOR3)
    {
        XYPos numsiz = XYPos(siz * 2 * 1.35 / 8, siz * 3 * 1.35 / 8);

        render_number(reg.value, pos + XYPos(int(siz) / 8,int(siz) / 8), numsiz);
        render_number(reg.value + (reg.type == RegionType::XOR3 ? 3 : 2), pos - numsiz + XYPos(int(siz) * 7 / 8,int(siz) * 7 / 8), numsiz);

        SDL_Rect src_rect = {384, 736, 128, 128};
        SDL_Rect dst_rect = {pos.x + int(siz) / 3, pos.y + int(siz) / 4,  int(siz / 3), int(siz / 2)};
        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);


    }
    else if (reg.type == RegionType::MORE || reg.type == RegionType::LESS)
    {
        XYPos numsiz = XYPos(siz * 6 / 8, siz * 6 / 8);

        std::string digits = std::to_string(reg.value);
        if (reg.type == RegionType::MORE)
            digits += '+';
        if (reg.type == RegionType::LESS)
            digits += '-';

        render_number_string(digits, pos + XYPos(int(siz) / 8, int(siz) / 8), numsiz);
    }
    else if (reg.type == RegionType::EQUAL)
    {
        XYPos numsiz = XYPos(siz * 6 / 8, siz * 6 / 8);
        render_number(reg.value, pos + XYPos(int(siz) / 8,int(siz) / 8), numsiz);

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
        std::string digits = "!" + std::to_string(reg.value);
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

void GameState::render(bool saving)
{
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
        if (!grid->wrapped && grid_zoom < 0)
            grid_zoom = 0;
        scaled_grid_size = grid_size * std::pow(1.1, grid_zoom);
        grid_pitch = grid->get_grid_pitch(XYPos(scaled_grid_size, scaled_grid_size));

        if (!grid->wrapped)
        {
            scaled_grid_offset.x = std::min(scaled_grid_offset.x, grid_offset.x);
            scaled_grid_offset.y = std::min(scaled_grid_offset.y, grid_offset.y);
            int d = (scaled_grid_offset.x + scaled_grid_size) - (grid_offset.x + grid_size);
            if (d < 0)
                scaled_grid_offset.x -= d;
            d = (scaled_grid_offset.y + scaled_grid_size) - (grid_offset.y + grid_size);
            if (d < 0)
                scaled_grid_offset.y -= d;
        }
        else
        {
            XYPos wrap_size = grid->get_wrapped_size(grid_pitch);
            while ((scaled_grid_offset.x - grid_offset.x) > wrap_size.x)
                scaled_grid_offset.x -= wrap_size.x;
            while ((scaled_grid_offset.y - grid_offset.y) > wrap_size.y)
                scaled_grid_offset.y -= wrap_size.y;
            while ((scaled_grid_offset.x - grid_offset.x) < 0 )
                scaled_grid_offset.x += wrap_size.x;
            while ((scaled_grid_offset.y - grid_offset.y) < 0)
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
        if (XYPosFloat(mouse - (right_panel_offset + XYPos(2 * button_size, 1 * button_size) + XYPos((button_size * 2) / 3, button_size / 3 + button_size / 12))).distance() < (button_size / 3) && rule_cause.rule->region_count >= 1)
            hover_rulemaker_region_base_index = 1;
        if (XYPosFloat(mouse - (right_panel_offset + XYPos(4 * button_size, 3 * button_size) + XYPos((button_size * 2) / 3, button_size / 3))).distance() < (button_size / 3) && rule_cause.rule->region_count >= 1)
            hover_rulemaker_region_base_index = 2;
        if (XYPosFloat(mouse - (right_panel_offset + XYPos(4 * button_size, 5 * button_size) + XYPos((button_size * 2) / 3 + button_size / 12, (button_size * 2) / 3))).distance() < (button_size / 3) && rule_cause.rule->region_count >= 1)
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

        if (rules_list_offset + row_count > (int)score_tables[current_level_group_index].size())
            rules_list_offset = score_tables[current_level_group_index].size() - row_count;
        rules_list_offset = std::max(rules_list_offset, 0);

        for (int score_index = 0; score_index < row_count; score_index++)
        {
            if (score_index + rules_list_offset >= score_tables[current_level_group_index].size())
                break;
            PlayerScore& s = score_tables[current_level_group_index][score_index + rules_list_offset];
            if (s.hidden)
                continue;

            render_number(s.pos, list_pos + XYPos(0 * cell_width, cell_width + score_index * cell_height + cell_height/10), XYPos(cell_width, cell_height*8/10));

            {
                SDL_Color color = {0xFF, 0xFF, 0xFF};
                if (s.is_friend == 1)
                    color = {0x80, 0xFF, 0x80};
                if (s.is_friend == 2)
                    color = {0xFF, 0x80, 0x80};
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

        if (rules_list_offset + row_count > score_tables[current_level_group_index].size())
            rules_list_offset = score_tables[current_level_group_index].size() - row_count;
        rules_list_offset = std::max(rules_list_offset, 0);

        {
            int full_size = cell_width * 5.5;
            int all_count = score_tables[current_level_group_index].size();
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
        if (rules_list_offset + row_count > (int)score_tables[current_level_group_index].size())
            rules_list_offset = score_tables[current_level_group_index].size() - row_count;
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
            SDL_Rect src_rect = {704 + 1 * 192, 2144, 192, 192};
            SDL_Rect dst_rect = {list_pos.x + 1 * cell_width, list_pos.y, cell_width, cell_width};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            add_tooltip(dst_rect, "Action");
            if (display_rules_click && ((display_rules_click_pos - XYPos(dst_rect.x, dst_rect.y)).inside(XYPos(dst_rect.w, dst_rect.h))))
                col_click = 1;
        }
        {
            SDL_Rect src_rect = {704 + 2 * 192, 2144, 192, 192};
            SDL_Rect dst_rect = {list_pos.x + 2 * cell_width, list_pos.y, cell_width, cell_width};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            add_tooltip(dst_rect, "Regions");
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
        {
            SDL_Rect src_rect = {704 + 3 * 192, 2144, 96, 96};
            SDL_Rect dst_rect = {list_pos.x + 7 * cell_width, list_pos.y, cell_width / 2, cell_width / 2};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            add_tooltip(dst_rect, "Close");
            if (display_rules_click && ((display_rules_click_pos - XYPos(dst_rect.x, dst_rect.y)).inside(XYPos(dst_rect.w, dst_rect.h))))
                display_rules = false;
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
            RuleDiplaySort(int col_, bool descend_):
                col(col_), descend(descend_)
            {};
            bool operator() (RuleDiplay a_,RuleDiplay b_)
            {
                RuleDiplay &a = descend ? a_ : b_;
                RuleDiplay &b = descend ? b_ : a_;
                if (col == 0)
                    return (a.index < b.index);
                if (col == 1)
                    return (a.rule->apply_region_type < b.rule->apply_region_type);
                if (col == 2)
                    return a.rule->region_count < b.rule->region_count;
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
                    return (a.rule->used_count < b.rule->used_count);
                if (col == 6)
                    return (a.rule->clear_count < b.rule->clear_count);
                return (a.index < b.index);
            }
        };
        std::vector<RuleDiplay> rules_list;
        unsigned i = 0;
        for (GridRule& r : rules)
        {
            if (r.deleted)
                continue;
            r.resort_region();
            rules_list.push_back(RuleDiplay{i, &r});
            i++;
        }

        std::stable_sort (rules_list.begin(), rules_list.end(), RuleDiplaySort(display_rules_sort_col_2nd, display_rules_sort_dir_2nd));
        std::stable_sort (rules_list.begin(), rules_list.end(), RuleDiplaySort(display_rules_sort_col, display_rules_sort_dir));

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
            if (rule.apply_region_type.type >= RegionType::Type::SET)
                render_region_type(rule.apply_region_type, list_pos + XYPos(1 * cell_width + (cell_width - cell_height) / 2, cell_width + rule_index * cell_height), cell_height);
            else
                render_region_bubble(rule.apply_region_type, 0, list_pos + XYPos(1 * cell_width + (cell_width - cell_height) / 2, cell_width + rule_index * cell_height), cell_height);
           
            render_number(rule.region_count, list_pos + XYPos(2 * cell_width, cell_width + rule_index * cell_height + cell_height/10), XYPos(cell_width, cell_height*8/10));
            for (int i = 0; i <rule.region_count; i++)
            {
                render_region_bubble(rule.get_region_sorted(i), 0, list_pos + XYPos(3 * cell_width + i * cell_height, cell_width + rule_index * cell_height), cell_height);
            }

            render_number(rule.used_count, list_pos + XYPos(5 * cell_width, cell_width + rule_index * cell_height + cell_height/10), XYPos(cell_width, cell_height*8/10));
            render_number(rule.clear_count, list_pos + XYPos(6 * cell_width, cell_width + rule_index * cell_height + cell_height/10), XYPos(cell_width, cell_height*8/10));


            if (display_rules_click && ((display_rules_click_pos - list_pos - XYPos(0, cell_width + rule_index * cell_height)).inside(XYPos(cell_width * 7, cell_height))))
            {
                right_panel_mode = RIGHT_MENU_RULE_INSPECT;
                inspected_rule = GridRegionCause(&rule, NULL, NULL, NULL, NULL);
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
    }
    else
    {
        XYSet grid_squares = grid->get_squares();
        XYPos wrap_size = grid->get_wrapped_size(grid_pitch);
        XYPos wrap_start = (wrap_size == XYPos()) ? XYPos(0, 0) : XYPos(-2, -2);
        XYPos wrap_end = (wrap_size == XYPos()) ? XYPos(1, 1) : XYPos(5, 5);

        FOR_XY_SET(pos, grid_squares)
        {
            Colour bg_col(0,0,0);
            bool hl = false;
            {
                if (clue_solves.count(pos))
                {
                    bg_col = Colour(0, 255, 0);
                }
                if (filter_pos.get(pos))
                {
                    bg_col = Colour(255,0, 0);
                }
                if (hover_rulemaker && hover_squares_highlight.get(pos))
                {
                    bg_col = Colour(255,255, 0);
                }
            }
            std::vector<RenderCmd> cmds;
            grid->render_square(pos, grid_pitch, cmds);
            for (RenderCmd& cmd : cmds)
            {
                FOR_XY(r, wrap_start, wrap_end)
                {
                    if (cmd.bg)
                    {
                        if (bg_col == Colour(0, 0, 0))
                            continue;
                        SDL_SetTextureColorMod(sdl_texture, bg_col.r,  bg_col.g, bg_col.b);

                    }
                    SDL_Rect src_rect = {cmd.src.pos.x, cmd.src.pos.y, cmd.src.size.x, cmd.src.size.y};
                    SDL_Rect dst_rect = {wrap_size.x * r.x + scaled_grid_offset.x + cmd.dst.pos.x, wrap_size.y * r.y + scaled_grid_offset.y + cmd.dst.pos.y, cmd.dst.size.x, cmd.dst.size.y};
                    SDL_Point rot_center = {cmd.center.x, cmd.center.y};
                    SDL_RenderCopyEx(sdl_renderer, sdl_texture, &src_rect, &dst_rect, cmd.angle, &rot_center, SDL_FLIP_NONE);
                    if (cmd.bg)
                        SDL_SetTextureColorMod(sdl_texture, 255, 255, 255);


                }
            }
        }

        std::map<XYPos, int> total_taken;
        std::list<GridRegion*> display_regions;

        XYPos mouse_filter_pos(-1,-1);
        if ((mouse_mode == MOUSE_MODE_FILTER) && (mouse - scaled_grid_offset).inside(XYPos(grid_size,grid_size)))
        {
            mouse_filter_pos = grid->get_square_from_mouse_pos(mouse - scaled_grid_offset, grid_pitch);
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
                    render_region_bg(*region, taken, total_taken, wrap_size, i);
            }
            {
                std::map<XYPos, int> taken;
                for (GridRegion* region : display_regions)
                    render_region_fg(*region, taken, total_taken, wrap_size, i);
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
                XYRect sq_pos = grid->get_square_pos(pos, grid_pitch);
                unsigned anim_prog = grid_cells_animation[pos];
                unsigned max_anim_frame = 500;
                grid_cells_animation[pos] = std::min(anim_prog + frame_step, max_anim_frame);
                double fade = 1.0 - (double(anim_prog) / double(max_anim_frame));
                double wob = -sin((10 / (fade + 0.3))) * (fade * fade);

                sq_pos.pos -= XYPosFloat(sq_pos.size) * (wob / 2);
                sq_pos.size += XYPosFloat(sq_pos.size) * (wob);

                int icon_width = std::min(sq_pos.size.x, sq_pos.size.y);
                XYPos gpos = scaled_grid_offset + sq_pos.pos + (sq_pos.size - XYPos(icon_width,icon_width)) / 2;
                SDL_SetTextureAlphaMod(sdl_texture, 255.0 * (1.0 - pow(fade, 0.5)));
                FOR_XY(r, wrap_start, wrap_end)
                {
                    XYPos mgpos = gpos + wrap_size * r;
                    if (place.bomb)
                    {
                        SDL_Rect src_rect = {320, 192, 192, 192};
                        SDL_Rect dst_rect = {mgpos.x, mgpos.y, icon_width, icon_width};
                        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                    }
                    else
                    {
                        render_region_type(place.clue, mgpos, icon_width);
                    }
                }
                SDL_SetTextureAlphaMod(sdl_texture, 255);
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
                XYPosFloat epos = grid_offset - scaled_grid_offset + XYPosFloat(0.1,0.1);

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
    {
        if (display_rules)
            render_box(left_panel_offset + XYPos(button_size * 0, button_size * 2), XYPos(button_size * 2, button_size), button_size/4);
        SDL_Rect src_rect = {1536, 384, 192, 192};
        SDL_Rect dst_rect = {left_panel_offset.x + 0 * button_size, left_panel_offset.y + button_size * 2, button_size, button_size};
        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
        dst_rect.w *= 2;
        add_tooltip(dst_rect, "Rules");
        int rule_count = 0;
        for (GridRule& rule : rules)
        {
            if (!rule.deleted)
                rule_count++;
        }
        render_number(rule_count, left_panel_offset + XYPos(button_size * 1 + button_size / 8, button_size * 2 + button_size / 4), XYPos(button_size * 3 / 4, button_size / 2));
    }
    {
        SDL_Rect src_rect = {1920, 384, 192, 192};
        SDL_Rect dst_rect = {left_panel_offset.x + 0 * button_size, left_panel_offset.y + button_size * 3, button_size, button_size};
        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
        dst_rect.w *= 2;
        add_tooltip(dst_rect, "Current Level", false);
        render_number(current_level_index, left_panel_offset + XYPos(button_size * 1 + button_size / 8, button_size * 3 + button_size / 4), XYPos(button_size * 3 / 4, button_size / 2));
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
        for (int i = 0; i < level_progress[current_level_group_index].size(); i++)
        {
            LevelProgress& prog = level_progress[current_level_group_index][i];
            for (bool b : prog.level_status)
                if (b)
                    count++;
        }
        render_number(count, left_panel_offset + XYPos(button_size * 1 + button_size / 8, button_size * 4 + button_size / 4), XYPos(button_size * 3 / 4, button_size / 2));
    }


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

    for (int i = 0; i < 4; i++)
    {
        SDL_Rect src_rect = {1472, 1344 + i * 192, 192, 192};
        SDL_Rect dst_rect = {left_panel_offset.x + i * button_size, left_panel_offset.y + button_size * 5, button_size, button_size};
        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
        add_clickable_highlight(dst_rect);
    }

    for (int i = 0; i < 5; i++)
    {
        SDL_Rect src_rect = {512, (current_level_group_index == i) ? 1152 : 1344, 192, 192};
        SDL_Rect dst_rect = {left_panel_offset.x + i * button_size, right_panel_offset.y + button_size * 5, button_size, button_size};
        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
    }

    XYPos p = XYPos(0,0);
    for (int i = 0; i < level_progress[current_level_group_index].size(); i++)
    {
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
        int c = level_progress[current_level_group_index][i].count_todo;
        if (!global_level_sets[current_level_group_index][i]->levels.size() || !level_is_accessible(i))
        {
            SDL_Rect src_rect = {1088, 192, 192, 192};
            SDL_Rect dst_rect = {pos.x, pos.y, button_size, button_size};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
        }
        else if (c)
        {
            render_number(level_progress[current_level_group_index][i].count_todo, pos + XYPos(button_size / 8, button_size / 8), XYPos(button_size * 3 / 4 , button_size * 3 / 4));
            SDL_Rect dst_rect = {pos.x, pos.y, button_size, button_size};
            add_clickable_highlight(dst_rect);
        }
        else
        {
            SDL_Rect src_rect = {512, 960, 192, 192};
            SDL_Rect dst_rect = {pos.x, pos.y, button_size, button_size};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
        }

//        render_number(level_progress[i].counts[1], pos + XYPos(button_size /2, button_size / 12), XYPos(button_size /2, button_size / 3));
//        render_number(level_progress[i].counts[2], pos + XYPos(0, button_size / 2 + button_size / 12), XYPos(button_size /2, button_size / 3));
//        render_number(level_progress[i].counts[3], pos + XYPos(0, button_size / 2 + button_size / 12), XYPos(button_size, button_size / 3));

        p.x++;
        if (p.x >= 5)
        {
            p.x = 0;
            p.y++;
        }
    }

    if (right_panel_mode == RIGHT_MENU_RULE_GEN)
    {
        {
            std::string t = translate("Rule Constructor");
            render_text_box(right_panel_offset + XYPos(0 * button_size, 0 * button_size), t);
        }

        {
            SDL_Rect src_rect = {544, 544, 128, 128};
            SDL_Rect dst_rect = {right_panel_offset.x + 0 * button_size, right_panel_offset.y + button_size * 7, button_size, button_size};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
        }
        {
            SDL_Rect src_rect = {128, 864, 128, 128};
            SDL_Rect dst_rect = {right_panel_offset.x + 1 * button_size, right_panel_offset.y + button_size * 7, button_size, button_size};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
        }
        {
            SDL_Rect src_rect = {128, 736, 128, 128};
            SDL_Rect dst_rect = {right_panel_offset.x + 2 * button_size, right_panel_offset.y + button_size * 7, button_size, button_size};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
        }
        {
            SDL_Rect src_rect = {256, 864, 128, 128};
            SDL_Rect dst_rect = {right_panel_offset.x + 3 * button_size, right_panel_offset.y + button_size * 7, button_size, button_size};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
        }
        for (int i = 0; i < 5; i++)
        {
            SDL_Rect src_rect = {512, (region_menu == i) ? 1152 : 1344, 192, 192};
            SDL_Rect dst_rect = {right_panel_offset.x + i * button_size, right_panel_offset.y + button_size * 7, button_size, button_size};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            if (i < 4)
                add_clickable_highlight(dst_rect);
        }

        RegionType r_type = menu_region_types1[region_menu];
        if (r_type.type != RegionType::NONE)
        {
            FOR_XY(pos, XYPos(), XYPos(5, 2))
            {
                XYPos bpos = right_panel_offset+ pos * button_size + XYPos(0, button_size * 8);
                if (region_type == r_type)
                    render_box(bpos, XYPos(button_size, button_size), button_size/4);
                render_region_type(r_type, bpos, button_size);
                r_type.value ++;
                SDL_Rect dst_rect = {bpos.x, bpos.y, button_size, button_size};
                add_clickable_highlight(dst_rect);

            }
        }

        r_type = menu_region_types2[region_menu];
        if (r_type.type != RegionType::NONE)
        {
            FOR_XY(pos, XYPos(), XYPos(5, 2))
            {
                XYPos bpos = right_panel_offset+ pos * button_size + XYPos(0, button_size * 10 + button_size/2);
                if (region_type == r_type)
                    render_box(bpos, XYPos(button_size, button_size), button_size/4);
                render_region_type(r_type, bpos, button_size);
                r_type.value ++;
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


        {
            if (region_type == RegionType(RegionType::VISIBILITY, 1))
                render_box(right_panel_offset + XYPos(button_size * 3, button_size * 6), XYPos(button_size, button_size), button_size/4);
            SDL_Rect src_rect = {896, 384, 192, 192};
            SDL_Rect dst_rect = {right_panel_offset.x + button_size * 3, right_panel_offset.y + button_size * 6, button_size, button_size};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            add_tooltip(dst_rect, "Hidden");
        }

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
            set_region_colour(sdl_texture, inspected_region->type.value, inspected_region->colour, 255);
            render_box(right_panel_offset + XYPos(0 * button_size, 1 * button_size), XYPos(1 * button_size, 2 * button_size), button_size / 2);
            render_region_bubble(inspected_region->type, inspected_region->colour, right_panel_offset + XYPos(0 * button_size, 1 * button_size), button_size * 2 / 3, hover_rulemaker_region_base_index == 0);
        }
        SDL_SetTextureColorMod(sdl_texture, 255, 255, 255);
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
            SDL_Rect src_rect = { 1088, 576, 192, 192 };
            SDL_Rect dst_rect = { right_panel_offset.x + button_size * 3, right_panel_offset.y + 1 * button_size, button_size, button_size};
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

    if (right_panel_mode != RIGHT_MENU_RULE_GEN)
    {
        if (constructed_rule.region_count)
        {
            SDL_Rect src_rect = { 704 + (constructed_rule.region_count - 1) * 192, 1152, 192, 192 };
            SDL_Rect dst_rect = { right_panel_offset.x + button_size * 0, right_panel_offset.y + 6 * button_size, button_size, button_size};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            add_tooltip(dst_rect, "Go to Rule Constructor");
        }
    }

    if (right_panel_mode == RIGHT_MENU_RULE_GEN || right_panel_mode == RIGHT_MENU_RULE_INSPECT)
    {
        GridRule& rule = (right_panel_mode == RIGHT_MENU_RULE_GEN) ? constructed_rule : *inspected_rule.rule;

        if (rule.region_count >= 1)
        {
            XYPos siz = XYPos(1,2);
            if (rule.region_count >= 2) siz.x = 2;
            if (rule.region_count >= 3) siz.y = 3;
            if (rule.region_count >= 4) siz.y = 5;

            unsigned colour = (right_panel_mode == RIGHT_MENU_RULE_GEN && rule_gen_region[0]) ? rule_gen_region[0]->colour : 0;
            set_region_colour(sdl_texture, rule.region_type[0].value, colour, 255);
            render_box(right_panel_offset + XYPos(0 * button_size, 1 * button_size), XYPos(siz.x * button_size, siz.y * button_size), button_size / 2);
            render_region_bubble(rule.region_type[0], colour, right_panel_offset + XYPos(0 * button_size, 1 * button_size), button_size * 2 / 3, hover_rulemaker_region_base_index == 0);

        }
        if (rule.region_count >= 2)
        {
            XYPosFloat siz = XYPos(2,2);
            if (rule.region_count >= 3) siz.y = 3;
            if (rule.region_count >= 4) siz.y = 5 - 1.0 / 12;

            unsigned colour = (right_panel_mode == RIGHT_MENU_RULE_GEN && rule_gen_region[1]) ? rule_gen_region[1]->colour : 0;
            set_region_colour(sdl_texture, rule.region_type[1].value, colour, 255);
            render_box(right_panel_offset + XYPos(1 * button_size, 1 * button_size + button_size / 12), XYPos(siz.x * button_size, siz.y * button_size), button_size / 2);
            render_region_bubble(rule.region_type[1], colour, right_panel_offset + XYPos(2 * button_size + button_size / 3, 1 * button_size +button_size / 12), button_size * 2 / 3, hover_rulemaker_region_base_index == 1);

        }

        if (rule.region_count >= 3)
        {
            XYPos siz = XYPos(5,1);
            if (rule.region_count >= 4) siz.y = 2;
            unsigned colour = (right_panel_mode == RIGHT_MENU_RULE_GEN && rule_gen_region[2]) ? rule_gen_region[2]->colour : 0;
            set_region_colour(sdl_texture, rule.region_type[2].value, colour, 255);
            render_box(right_panel_offset + XYPos(0 * button_size, 3 * button_size), XYPos(siz.x * button_size, siz.y * button_size), button_size / 2);
            render_region_bubble(rule.region_type[2], colour, right_panel_offset + XYPos(4 * button_size + button_size / 3, 3 * button_size), button_size * 2 / 3, hover_rulemaker_region_base_index == 2);
        }

        if (rule.region_count >= 4)
        {
            unsigned colour = (right_panel_mode == RIGHT_MENU_RULE_GEN && rule_gen_region[3]) ? rule_gen_region[3]->colour : 0;
            set_region_colour(sdl_texture, rule.region_type[3].value, colour, 255);
            render_box(right_panel_offset + XYPos(button_size / 12, 4 * button_size), XYPos(5 * button_size, 2 * button_size), button_size / 2);
            render_region_bubble(rule.region_type[3], colour, right_panel_offset + XYPos(button_size / 12 + 4 * button_size + button_size / 3, 5 * button_size + button_size / 3), button_size * 2 / 3, hover_rulemaker_region_base_index == 3);
        }

        if (rule.apply_region_type.type == RegionType::VISIBILITY)
        {
            SDL_SetTextureColorMod(sdl_texture, 255, 255, 255);
            SDL_Rect src_rect = {(rule.apply_region_type.value == 0) ? 1088 : 896, 384, 192, 192};
            if (rule.apply_region_type.value == 2)
            {
                src_rect.x = 512;
                src_rect.y = 768;
            }
            if (rule.apply_region_bitmap & 1)
            {
                SDL_Rect dst_rect = {right_panel_offset.x + button_size / 3, right_panel_offset.y + 1 * button_size + button_size / 3, button_size * 2 / 3, button_size * 2 / 3};
                SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            }
            if (rule.apply_region_bitmap & 2)
            {
                SDL_Rect dst_rect = {right_panel_offset.x + button_size * 2, right_panel_offset.y + 1 * button_size + button_size / 3, button_size * 2 / 3, button_size * 2 / 3};
                SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            }
            if (rule.apply_region_bitmap & 4)
            {
                SDL_Rect dst_rect = {right_panel_offset.x + button_size * 4, right_panel_offset.y + 3 * button_size + button_size / 3, button_size * 2 / 3, button_size * 2 / 3};
                SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            }
            if (rule.apply_region_bitmap & 8)
            {
                SDL_Rect dst_rect = {right_panel_offset.x + button_size * 4, right_panel_offset.y + 5 * button_size, button_size * 2 / 3, button_size * 2 / 3};
                SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
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
            y += 2;

            XYPos p = XYPos(x,y);

            RegionType r_type = rule.square_counts[i];

            render_region_type(r_type, right_panel_offset + p * button_size, button_size);

            if (hover_rulemaker && hover_rulemaker_bits == i)
            {
                if (hover_rulemaker_lower_right && right_panel_mode == RIGHT_MENU_RULE_GEN)
                    SDL_SetTextureColorMod(sdl_texture, 128, 128, 128);
                render_box(right_panel_offset + p * button_size, XYPos(button_size, button_size), button_size / 4);
                SDL_SetTextureColorMod(sdl_texture, 255, 255, 255);

            }

            if ((rule.apply_region_bitmap >> i) & 1)
            {
                if (rule.apply_region_type.type < 50)
                {
                    render_region_bubble(rule.apply_region_type, 0, right_panel_offset + XYPos(button_size / 2, button_size / 2) + p * button_size, button_size / 2);
                }
                else if (rule.apply_region_type.type == RegionType::SET)
                {
                    render_region_type(rule.apply_region_type, right_panel_offset + XYPos(button_size / 2, button_size / 2) + p * button_size, button_size / 2);
                }
            }
            if (hover_rulemaker && hover_rulemaker_bits == i && right_panel_mode == RIGHT_MENU_RULE_GEN)
            {
                if (!hover_rulemaker_lower_right)
                    SDL_SetTextureColorMod(sdl_texture, 128, 128, 128);
                render_box(right_panel_offset + p * button_size + XYPos(button_size / 2, button_size / 2), XYPos(button_size / 2, button_size / 2), button_size / 4);
                SDL_SetTextureColorMod(sdl_texture, 255, 255, 255);
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
                }
            }
            else if ((region_type.type != RegionType::NONE) && (region_type.type < 50))
            {
                {
                    SDL_Rect src_rect = {384, 992, 64, 64};
                    SDL_Rect dst_rect = {p.x + button_size * 3 / 4 , p.y, button_size / 2, button_size / 2};
                    SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                }
                    render_region_bubble(region_type, 0, p, button_size);
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

            if (rule.region_count >= 1 && constructed_rule.apply_region_bitmap)
            {
                if (!constructed_rule_is_logical)
                {
                    SDL_Rect src_rect = {896, 576, 192, 192};
                    SDL_Rect dst_rect = {right_panel_offset.x + button_size * 4, right_panel_offset.y + button_size, button_size, button_size };
                    SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                    add_tooltip(dst_rect, "Illogical");
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
        if (right_panel_mode == RIGHT_MENU_RULE_INSPECT)
        {
            {
                std::string t = translate("Rule Inspector");
                render_text_box(right_panel_offset + XYPos(0 * button_size, 0 * button_size), t);
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
            SDL_Rect src_rect = {704, 1344, 192*3, 192};
            SDL_Rect dst_rect = {help_image_offset.x + help_image_size.x - sq_size * 3, help_image_offset.y + help_image_size.y - sq_size, sq_size * 3, sq_size};
            if (tutorial_index)
                src_rect.y += 192;
            if (tutorial_index >= (tut_texture_count - 1))
                src_rect.y += 192;
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
        }
    }
    if (display_menu)
    {
        tooltip_string = "";
        tooltip_rect = XYRect(-1,-1,-1,-1);
        render_box(left_panel_offset + XYPos(button_size, button_size), XYPos(10 * button_size, 10 * button_size), button_size/4, 1);
        {
            SDL_Rect src_rect = {full_screen ? 1472 : 1664, 1152, 192, 192};
            SDL_Rect dst_rect = {left_panel_offset.x + 2 * button_size, left_panel_offset.y + button_size * 2, button_size, button_size};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            std::string t = translate("Full Screen");
            render_text_box(left_panel_offset + XYPos(3.2 * button_size, 2.2 * button_size), t);
        }
        {
            SDL_Rect src_rect = {1280, 1344, 192, 192};
            SDL_Rect dst_rect = {left_panel_offset.x + 2 * button_size, left_panel_offset.y + button_size * 3, button_size, button_size};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            std::string t = translate("Select Language");
            render_text_box(left_panel_offset + XYPos(3.2 * button_size, 3.2 * button_size), t);
        }
        {
            SDL_Rect src_rect = {1664, show_row_clues ? 576 : 768, 192, 192};
            SDL_Rect dst_rect = {left_panel_offset.x + 2 * button_size, left_panel_offset.y + button_size * 4, button_size, button_size};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            std::string t = translate("Show Row Clues");
            render_text_box(left_panel_offset + XYPos(3.2 * button_size, 4.2 * button_size), t);
        }
        {
            SDL_Rect src_rect = {1664, 960, 192, 192};
            SDL_Rect dst_rect = {left_panel_offset.x + 2 * button_size, left_panel_offset.y + button_size * 6, button_size, button_size};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            std::string t = translate("Reset Levels");
            render_text_box(left_panel_offset + XYPos(3.2 * button_size, 6.2 * button_size), t);
        }
        {
            SDL_Rect src_rect = {1664, 960, 192, 192};
            SDL_Rect dst_rect = {left_panel_offset.x + 2 * button_size, left_panel_offset.y + button_size * 7, button_size, button_size};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            std::string t = translate("Reset Game");
            render_text_box(left_panel_offset + XYPos(3.2 * button_size, 7.2 * button_size), t);
        }
        {
            SDL_Rect src_rect = {1280, 1728, 192, 192};
            SDL_Rect dst_rect = {left_panel_offset.x + 2 * button_size, left_panel_offset.y + button_size * 8, button_size, button_size};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            std::string t = translate("Quit");
            render_text_box(left_panel_offset + XYPos(3.2 * button_size, 8.2 * button_size), t);
        }
        {
            SDL_Rect src_rect = {704, 384, 192, 192};
            SDL_Rect dst_rect = {left_panel_offset.x + 9 * button_size, left_panel_offset.y + button_size * 9, button_size, button_size};
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
            std::string t = display_reset_confirm_levels_only ? translate("Reset Levels") : translate("Reset Game");
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
                SDL_SetTextureColorMod(sdl_texture, 0,255, 0);
            render_text_box(left_panel_offset + XYPos(button_size * 2, button_size * (2 + index)), s);
            SDL_SetTextureColorMod(sdl_texture, 255,255, 255);
            index++;
        }
        set_language(orig_lang);
    }

    render_tooltip();
    SDL_RenderPresent(sdl_renderer);
}

void GameState::grid_click(XYPos pos, int clicks, int btn)
{
    grid_dragging = true;
    grid_dragging_last_pos = mouse;

    if (display_scores || display_rules)
    {
        display_rules_click = true;
        display_rules_click_drag = true;
        display_rules_click_pos = mouse;
    }
    else if (mouse_mode == MOUSE_MODE_FILTER)
    {
        XYPos gpos = grid->get_square_from_mouse_pos(pos - scaled_grid_offset, grid_pitch);
        gpos = grid->get_base_square(gpos);
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
                    if (constructed_rule.region_count < 4)
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

                // if (rule_gen_region_count < 3)
                // {
                //     rule_gen_region[rule_gen_region_count] = mouse_hover_region;
                //     rule_gen_region_count++;
                //     update_constructed_rule();
                // }
            }
        }
    }
}

void GameState::left_panel_click(XYPos pos, int clicks, int btn)
{
    const int trim = 27;
    int digit_height = button_size * 0.75;
    int digit_width = (digit_height * (192 - trim * 2)) / 192;

    if ((pos - XYPos(button_size * 3, button_size * 1)).inside(XYPos(button_size,button_size)))
        filter_pos.clear();
    if ((pos - XYPos(button_size * 4, button_size * 1)).inside(XYPos(button_size,button_size)))
    {
        if(mouse_mode == MOUSE_MODE_FILTER)
            mouse_mode = MOUSE_MODE_NONE;
        else
            mouse_mode = MOUSE_MODE_FILTER;
    }
    if ((pos - XYPos(button_size * 3, button_size * 2)).inside(XYPos(button_size * 2,button_size)))
        vis_level = GRID_VIS_LEVEL_SHOW;
    if ((pos - XYPos(button_size * 3, button_size * 3)).inside(XYPos(button_size * 2,button_size)))
        vis_level = GRID_VIS_LEVEL_HIDE;
    if ((pos - XYPos(button_size * 3, button_size * 4)).inside(XYPos(button_size * 2,button_size)))
        vis_level = GRID_VIS_LEVEL_BIN;


    if ((pos - XYPos(button_size * 0, button_size * 0)).inside(XYPos(button_size,button_size)))
        display_menu = true;
    if ((pos - XYPos(button_size * 1, button_size * 0)).inside(XYPos(button_size,button_size)))
        display_help = true;
    if ((pos - XYPos(button_size * 2, button_size * 0)).inside(XYPos(button_size,button_size)))
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
    if ((pos - XYPos(button_size * 3, button_size * 0)).inside(XYPos(button_size,button_size)))
    {
        skip_level = (btn == 2) ? -1 : 1;
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
        if (display_rules) display_scores = false;
    }
    if ((pos - XYPos(button_size * 0, button_size * 4)).inside(XYPos(button_size * 2, button_size)))
    {
        display_scores = !display_scores;
        if (display_scores) display_rules = false;
    }

    if ((pos - XYPos(button_size * 0, button_size * 5)).inside(XYPos(button_size * GLBAL_LEVEL_SETS, button_size)))
    {
        int x = ((pos - XYPos(button_size * 0, button_size * 5)) / button_size).x;
        if (x >= 0 && x < GLBAL_LEVEL_SETS)
        {
            current_level_group_index = x;
            current_level_set_index = 0;
            current_level_index = 0;
            skip_level = 1;
        }
    }

    XYPos gpos = pos / button_size;
    gpos.y -= 6;
    int idx = gpos.x + gpos.y * 5;

    if ((idx >= 0) && (idx < level_progress[current_level_group_index].size()))
    {
        if (level_progress[current_level_group_index][idx].level_status.size() && level_is_accessible(idx))
        {
            current_level_set_index = idx;
            current_level_index = 0;
            skip_level = 1;
            auto_progress =  (clicks > 1);
        }
    }
}

void GameState::right_panel_click(XYPos pos, int clicks, int btn)
{
    XYPos bpos = pos / button_size;

    if (right_panel_mode == RIGHT_MENU_REGION)
    {
        if ((pos - XYPos(button_size * 3, button_size)).inside(XYPos(button_size, button_size)))
        {
            right_panel_mode = RIGHT_MENU_RULE_GEN;
            if (constructed_rule.region_count < 4)
            {
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
            if(inspected_region->vis_cause.rule)
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

        if ((pos - XYPos(button_size * 3, button_size * 3)).inside(XYPos(button_size, button_size)))
        {
            inspected_region->vis_level = GRID_VIS_LEVEL_SHOW;
            inspected_region->visibility_force = GridRegion::VIS_FORCE_USER;
            inspected_region->stale = false;
        }
        if ((pos - XYPos(button_size * 3, button_size * 4)).inside(XYPos(button_size, button_size)))
        {
            inspected_region->vis_level = GRID_VIS_LEVEL_HIDE;
            inspected_region->visibility_force = GridRegion::VIS_FORCE_USER;
            inspected_region->stale = false;
        }
        if ((pos - XYPos(button_size * 3, button_size * 5)).inside(XYPos(button_size, button_size)))
        {
            inspected_region->vis_level = GRID_VIS_LEVEL_BIN;
            inspected_region->visibility_force = GridRegion::VIS_FORCE_USER;
            inspected_region->stale = false;
        }

    }

    if (right_panel_mode == RIGHT_MENU_RULE_INSPECT)
    {
        if ((pos - XYPos(button_size * 4, button_size)).inside(XYPos(button_size, button_size)) && !inspected_rule.rule->deleted)
        {
            inspected_rule.rule->deleted = true;
            right_panel_mode = RIGHT_MENU_NONE;
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
            {
                replace_rule = inspected_rule.rule;
            }
            right_panel_mode = RIGHT_MENU_RULE_GEN;
            update_constructed_rule();
        }
    }
    if (right_panel_mode != RIGHT_MENU_RULE_GEN)
    {
        if ((pos - XYPos(button_size * 0, button_size * 6)).inside(XYPos(button_size, button_size)))
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
        if ((mouse - (right_panel_offset + XYPos(0, button_size))).inside(XYPos(button_size, button_size)) && rule_cause.rule->region_count >= 1)
            region_index = 0;
        if ((mouse - (right_panel_offset + XYPos(2 * button_size, button_size))).inside(XYPos(button_size, button_size)) && rule_cause.rule->region_count >= 2)
            region_index = 1;
        if ((mouse - (right_panel_offset + XYPos(4 * button_size, 3 * button_size))).inside(XYPos(button_size, button_size)) && rule_cause.rule->region_count >= 3)
            region_index = 2;
        if ((mouse - (right_panel_offset + XYPos(4 * button_size, 5 * button_size))).inside(XYPos(button_size, button_size)) && rule_cause.rule->region_count >= 4)
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
                    if ((region_type.type == RegionType::NONE) || (region_type.type > 50))
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
        if ((pos - XYPos(button_size * 3, button_size * 6)).inside(XYPos(button_size,button_size)))
            region_type = RegionType(RegionType::VISIBILITY, 1);
        if ((pos - XYPos(button_size * 4, button_size * 6)).inside(XYPos(button_size,button_size)))
            region_type = RegionType(RegionType::VISIBILITY, 2);

        if ((pos - XYPos(button_size * 0, button_size * 7)).inside(XYPos(4 * button_size,1 * button_size)))
        {
            region_menu = pos.x / button_size;
        }

        if ((pos - XYPos(button_size * 0, button_size * 8)).inside(XYPos(5 * button_size,2 * button_size)))
        {
            XYPos region_item_selected = (pos - XYPos(0, button_size * 8)) / button_size;
            region_type = menu_region_types1[region_menu];
            region_type.value += region_item_selected.x + region_item_selected.y * 5;
        }

        if ((pos - XYPos(button_size * 0, button_size * 10 + button_size / 2)).inside(XYPos(5 * button_size, 2 * button_size)))
        {
            XYPos region_item_selected = (pos - XYPos(0, button_size * 10 + button_size / 2)) / button_size;
            region_type = menu_region_types2[region_menu];
            region_type.value += region_item_selected.x + (region_item_selected.y) * 5;
        }
        if (region_type.type == RegionType::VISIBILITY && region_type.value > 2)
            region_type = RegionType(RegionType::VISIBILITY, 0);
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
                if (constructed_rule.is_legal())
                {
                    if (constructed_rule_is_already_present)
                    {
                        inspected_rule = GridRegionCause(constructed_rule_is_already_present, NULL, NULL, NULL, NULL);
                        right_panel_mode = RIGHT_MENU_RULE_INSPECT;
                    }
                    else
                    {
                        if (replace_rule)
                            *replace_rule = constructed_rule;
                        else
                            rules.push_back(constructed_rule);

                        reset_rule_gen_region();
                    }
                }
            }
        }

        if ((pos - XYPos(button_size * 3, button_size * 1)).inside(XYPos(button_size, button_size)))
        {
            reset_rule_gen_region();
        }
    }
}

bool GameState::events()
{
    bool quit = false;
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
                if (display_help || display_language_chooser || display_menu)
                {
                    switch (e.key.keysym.scancode)
                    {
                        case SDL_SCANCODE_F1:
                        case SDL_SCANCODE_ESCAPE:
                            display_help = false;
                            display_language_chooser = false;
                            display_menu = false;
                            break;
                        case SDL_SCANCODE_F11:
                        {
                            full_screen = !full_screen;
                            SDL_SetWindowFullscreen(sdl_window, full_screen? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
                            SDL_SetWindowBordered(sdl_window, full_screen ? SDL_FALSE : SDL_TRUE);
                            SDL_SetWindowResizable(sdl_window, full_screen ? SDL_FALSE : SDL_TRUE);
                            SDL_SetWindowInputFocus(sdl_window);
                            break;
                        }
                    }
                    break;
                }
                switch (e.key.keysym.scancode)
                {
                    case SDL_SCANCODE_Z:
                        rule_gen_undo();
                        break;
                    case SDL_SCANCODE_Y:
                        rule_gen_redo();
                        break;
                    case SDL_SCANCODE_Q:
                        key_held = 'Q';
                        break;
                    case SDL_SCANCODE_W:
                        key_held = 'W';
                        break;
                    case SDL_SCANCODE_E:
                        key_held = 'E';
                        break;
                    case SDL_SCANCODE_C:
                    {
                        std::string s = grid->to_string();
                        SDL_SetClipboardText(s.c_str());
                        break;
                    }
                    case SDL_SCANCODE_V:
                    {
                        char* s = SDL_GetClipboardText();
                        clue_solves.clear();
                        reset_rule_gen_region();
                        delete grid;
                        grid = Grid::Load(s);
                        SDL_free(s);
                        grid_cells_animation.clear();
                        grid_regions_animation.clear();
                        grid_regions_fade.clear();
                        current_level_is_temp = true;
                        break;
                    }
                    case SDL_SCANCODE_ESCAPE:
                        display_menu = true;
                        break;
                    case SDL_SCANCODE_F1:
                        display_help = true;
                        break;
                    case SDL_SCANCODE_F2:
                    {
                        clue_solves.clear();
                        XYSet grid_squares = grid->get_squares();
                        FOR_XY_SET(pos, grid_squares)
                        {
                            if (grid->is_determinable_using_regions(pos, true))
                                clue_solves.insert(pos);
                        }
                        get_hint = true;
                        break;
                    }
                    case SDL_SCANCODE_F3:
                    {
                        skip_level = 1;
                        break;
                    }
                    case SDL_SCANCODE_F4:
                    {
                        clue_solves.clear();
                        grid_regions_animation.clear();
                        grid_regions_fade.clear();
                        grid->clear_regions();
                        reset_rule_gen_region();
                        get_hint = false;
                        break;
                    }
                    case SDL_SCANCODE_F11:
                    {
                        full_screen = !full_screen;
                        SDL_SetWindowFullscreen(sdl_window, full_screen? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
                        SDL_SetWindowBordered(sdl_window, full_screen ? SDL_FALSE : SDL_TRUE);
                        SDL_SetWindowResizable(sdl_window, full_screen ? SDL_FALSE : SDL_TRUE);
                        SDL_SetWindowInputFocus(sdl_window);
                        break;
                    }
                    default:
                        printf("Uncaught key: %d\n", e.key.keysym.scancode);
                        break;
                }
                break;
            }
            case SDL_KEYUP:
                switch (e.key.keysym.scancode)
                {
                    case SDL_SCANCODE_Q:
                    case SDL_SCANCODE_W:
                    case SDL_SCANCODE_E:
                        key_held = 0;
                }
                break;
            case SDL_MOUSEMOTION:
            {
                mouse.x = e.motion.x;
                mouse.y = e.motion.y;
                if (grid_dragging)
                {
                    scaled_grid_offset += (mouse - grid_dragging_last_pos);
                    grid_dragging_last_pos = mouse;
                }
                if (dragging_speed)
                {
                    double p = double(mouse.x - left_panel_offset.x - (button_size / 6)) / (button_size * 2.6666);
                    speed_dial = std::clamp(p, 0.0, 1.0);
                }
                break;
            }
            case SDL_MOUSEBUTTONUP:
            {
                display_rules_click_drag = false;
                grid_dragging = false;
                dragging_speed = false;
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
                if (display_reset_confirm)
                {
                    XYPos p = (mouse - left_panel_offset) / button_size;
                    p -= XYPos(2,2);
                    if (p.x >= 0 && p.y >= 0 && p.x < 8)
                    {
                        if (p == XYPos(1, 4))
                        {
                            current_level_group_index = 1;
                            current_level_set_index = 0;
                            current_level_index = 0;
                            skip_level = 1;
                            if (!display_reset_confirm_levels_only)
                                rules.clear();
                            else
                            {
                                for (GridRule& rule : rules)
                                {
                                    rule.used_count = 0;
                                    rule.clear_count = 0;
                                }
                            }
                            for (int j = 0; j < GLBAL_LEVEL_SETS; j++)
                            {
                                level_progress[j].resize(global_level_sets[j].size());
                                for (int i = 0; i < global_level_sets[j].size(); i++)
                                {
                                    level_progress[j][i].level_status.clear();
                                    level_progress[j][i].level_status.resize(global_level_sets[j][i]->levels.size());
                                    level_progress[j][i].count_todo = global_level_sets[j][i]->levels.size();
                                }
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
                    if (p.x >= 0 && p.y >= 0 && p.x < 8)
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
                        if (p.y == 4)
                        {
                            display_reset_confirm = true;
                            display_reset_confirm_levels_only = true;
                        }
                        if (p.y == 5)
                        {
                            display_reset_confirm = true;
                            display_reset_confirm_levels_only = false;
                        }
                        if (p.y == 6)
                            quit = true;
                        if (p.y == 7 && p.x == 7)
                            display_menu = false;
                    }
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
                if (display_rules || display_scores)
                {
                    rules_list_offset -= e.wheel.y;
                    break;
                }
                grid_zoom += e.wheel.y;
                grid_zoom = std::clamp(grid_zoom, -10, 20);
                int new_scaled_grid_size = grid_size * std::pow(1.1, grid_zoom);
                scaled_grid_offset -= (mouse - scaled_grid_offset) * (new_scaled_grid_size - scaled_grid_size) / scaled_grid_size;
                scaled_grid_size = new_scaled_grid_size;

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
                for (int i = 0; i < GLBAL_LEVEL_SETS; i++)
                {
                    SaveObjectList* scores = lvls->get_item(i)->get_list();
                    score_tables[i].clear();
                    for (int j = 0; j < scores->get_count(); j++)
                    {
                        SaveObjectMap* score = scores->get_item(j)->get_map();
                        unsigned is_friend = 0;
                        if (score->has_key("friend"))
                            is_friend = score->get_num("friend");
                        unsigned hidden = 0;
                        if (score->has_key("hidden"))
                            hidden = score->get_num("hidden");
                        score_tables[i].push_back(PlayerScore(unsigned(score->get_num("pos")), score->get_string("name"), unsigned(score->get_num("score")), is_friend, hidden));
                    }
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
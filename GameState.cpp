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


GameState::GameState(std::ifstream& loadfile)
{
    LevelSet::init_global();
    bool load_was_good = false;

    level_progress.resize(global_level_sets.size());
    for (int i = 0; i < global_level_sets.size(); i++)
    {
        level_progress[i].level_status.resize(global_level_sets[i]->levels.size());
        level_progress[i].count_todo = global_level_sets[i]->levels.size();
    }
    {
        std::ifstream loadfile("lang.json");
        lang_data = SaveObject::load(loadfile)->get_map();
    }

    try
    {
        if (!loadfile.fail() && !loadfile.eof())
        {
            SaveObjectMap* omap;
            omap = SaveObject::load(loadfile)->get_map();
            int version = omap->get_num("version");
            if (version < 2)
                throw(std::runtime_error("Bad Version"));
            if (omap->has_key("language"))
                language = omap->get_string("language");


            SaveObjectList* rlist = omap->get_item("rules")->get_list();
            for (int i = 0; i < rlist->get_count(); i++)
            {
                GridRule r(rlist->get_item(i), version);
//                if (r.is_legal())
                rules.push_back(r);
            }

            if (version >= 4)
            {
                SaveObjectList* plist = omap->get_item("level_progress")->get_list();
                for (int i = 0; i < plist->get_count() && i < level_progress.size(); i++)
                {
                    std::string s = plist->get_string(i);
                    int lim = std::min(s.size(), level_progress[i].level_status.size());
                    for (int j = 0; j < lim; j++)
                    {
                        char c = s[j];
                        int stat = c - '0';
                        if (stat)
                            completed_count++;
                        level_progress[i].level_status[j] = stat;
                        if (stat)
                            level_progress[i].count_todo--;
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
        display_mode = DISPLAY_MODE_HELP;
    }

    sdl_window = SDL_CreateWindow( "Bombe", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1920/2, 1080/2, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | (full_screen? SDL_WINDOW_FULLSCREEN_DESKTOP  | SDL_WINDOW_BORDERLESS : 0));
    sdl_renderer = SDL_CreateRenderer(sdl_window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 0);
	sdl_texture = loadTexture("texture.png");

    tutorial_texture[0] = loadTexture("tutorial/tut0.png");
    tutorial_texture[1] = loadTexture("tutorial/tut1.png");
    tutorial_texture[2] = loadTexture("tutorial/tut2.png");
    tutorial_texture[3] = loadTexture("tutorial/tut3.png");

    {
        SDL_Surface* icon_surface = IMG_Load("icon.png");
        SDL_SetWindowIcon(sdl_window, icon_surface);
	    SDL_FreeSurface(icon_surface);
    }

    font = TTF_OpenFont("font.ttf", 32);

    Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048);
    Mix_AllocateChannels(32);

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
    omap->add_num("version", 4);

    SaveObjectList* rlist = new SaveObjectList;
    for (GridRule& rule : rules)
    {
        if (!rule.deleted)
            rlist->add_item(rule.save());
    }
    omap->add_item("rules", rlist);

    SaveObjectList* plist = new SaveObjectList;
    for (LevelProgress& prog : level_progress)
    {
        std::string sstr;
        for (bool stat : prog.level_status)
        {
            char c = '0' + stat;
            sstr += c;
        }
        plist->add_item(new SaveObjectString(sstr));
    }
    omap->add_item("level_progress", plist);
    omap->add_string("language", language);
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
    TTF_CloseFont(font);

    SDL_DestroyTexture(sdl_texture);
    for (int i = 0; i < tut_texture_count; i++)
        SDL_DestroyTexture(tutorial_texture[i]);
    SDL_DestroyRenderer(sdl_renderer);
    SDL_DestroyWindow(sdl_window);
    LevelSet::delete_global();
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
//    if (SDLNet_ResolveHost(&ip, "127.0.0.1", 42070) == -1)
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

void GameState::save_to_server(bool sync)
{
    if (steam_session_string.empty())
        return;
    SaveObjectMap* omap = new SaveObjectMap;
    omap->add_string("command", "save");
    omap->add_num("steam_id", steam_id);
    omap->add_string("steam_username", steam_username);
    omap->add_string("steam_session", steam_session_string);
    omap->add_num("demo", IS_DEMO);
    omap->add_num("playtest", IS_PLAYTEST);
    SaveObjectList* plist = new SaveObjectList;
    for (LevelProgress& prog : level_progress)
    {
        std::string sstr;
        for (bool stat : prog.level_status)
        {
            char c = '0' + stat;
            sstr += c;
        }
        plist->add_item(new SaveObjectString(sstr));
    }
    omap->add_item("level_progress", plist);
    post_to_server(omap, sync);
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
    if (server_timeout)
        server_timeout--;

    if (grid->is_solved() || skip_level)
    {
        clue_solves.clear();
        if (grid->is_solved() && !current_level_is_temp)
        {
            bool o = level_progress[current_level_set_index].level_status[current_level_index];
            if (!o)
            {
                completed_count++;
                level_progress[current_level_set_index].count_todo--;
                level_progress[current_level_set_index].level_status[current_level_index] = true;
            }
        }
        skip_level = false;

        if (level_progress[current_level_set_index].count_todo)
        {
            if (!current_level_is_temp || level_progress[current_level_set_index].level_status[current_level_index]);
                do
                {
                    current_level_index++;
                    if (current_level_index >= level_progress[current_level_set_index].level_status.size())
                        current_level_index = 0;
                }
                while (level_progress[current_level_set_index].level_status[current_level_index]);

            std::string& s = global_level_sets[current_level_set_index]->levels[current_level_index];
            delete grid;
            grid = Grid::Load(s);
            reset_rule_gen_region();
            current_level_is_temp = false;
        }
    }

    if(clue_solves.empty())
        get_hint = false;
    while (steps)
    {
        bool done_summit = false;
        if (cooldown)
        {
            if (steps < cooldown)
            {
                cooldown -= steps;
                return;
            }
            cooldown = 0;
        }

        if (get_hint)
        {
            for (GridRegion& r : grid->regions)
            {
                if (r.visibility_force == GridRegion::VIS_FORCE_NONE && r.vis_level == GRID_VIS_LEVEL_SHOW)
                {
                    r.visibility_force = GridRegion::VIS_FORCE_HINT;
                    r.vis_level = GRID_VIS_LEVEL_HIDE;

                    for (const XYPos& pos : clue_solves)
                    {
                        if (!grid->is_determinable_using_regions(pos, true))
                        {
                            r.vis_level = GRID_VIS_LEVEL_SHOW;
                            break;
                        }
                    }
                    if (r.vis_level == GRID_VIS_LEVEL_HIDE)
                    {
                        cooldown = 300;
                        return;
                    }
                }
            }
            if (cooldown)
                continue;
        }

        if (auto_region)
        {
            bool hit = false;
            bool check_vis = false;
            while (grid->add_regions(-1)) {check_vis = true;  last_active_was_hit = true; break;}

            if (!last_active_was_hit)
            {
                grid->add_one_new_region();
                check_vis = true;
                last_active_was_hit = true;
            }
            if (check_vis)
            {
                for (std::list<GridRule>::iterator it=rules.begin(); it != rules.end(); ++it)
                {
                    GridRule& rule = *it;
                    if (rule.deleted)
                        continue;
                    if (rule.apply_region_type.type == RegionType::VISIBILITY)
                    {
                        Grid::ApplyRuleResp resp  = grid->apply_rule(rule);
                        if (resp == Grid::APPLY_RULE_RESP_NONE)
                            rule.stale = true;
                    }
                }
                cooldown = 50000 / (completed_count + 50);
                continue;
            }
            last_active_was_hit = false;
            for (std::list<GridRule>::iterator it=rules.begin(); it != rules.end(); ++it)
            {
                GridRule& rule = *it;
                if (rule.deleted)
                    continue;
                if (hit) break;
                if (rule.apply_region_type.type == RegionType::VISIBILITY)
                    continue;

                Grid::ApplyRuleResp resp  = grid->apply_rule(rule);
                if (resp == Grid::APPLY_RULE_RESP_HIT)
                {
                    hit = true;
                    last_active_was_hit = true;
                    clue_solves.clear();
                    break;
                }
                if (resp == Grid::APPLY_RULE_RESP_ERROR)
                {
//                    rules.erase(it);
                    assert(0);
                    break;
                }
                if (resp == Grid::APPLY_RULE_RESP_NONE)
                {
                    rule.stale = true;
                }
            }
            if (hit)
            {
                done_summit = true;
                continue;
            }
            else
            {
                for (GridRegion& r : grid->regions)
                {
                    r.stale = true;
                }
            }
        }
        break;
    }
}

void GameState::audio()
{
}

void GameState::reset_rule_gen_region()
{
    rule_gen_region[0] = NULL;
    rule_gen_region[1] = NULL;
    rule_gen_region[2] = NULL;
    rule_gen_region[3] = NULL;
//    rule_gen_region_count = 0;
//    rule_gen_region_undef_num = 0;
    constructed_rule.apply_region_bitmap = 0;
    constructed_rule.import_rule_gen_regions(rule_gen_region[0], rule_gen_region[1], rule_gen_region[2], rule_gen_region[3]);
    update_constructed_rule();
    right_panel_mode = RIGHT_MENU_NONE;
    filter_pos.clear();
}

void GameState::update_constructed_rule()
{
    constructed_rule_is_logical = constructed_rule.is_legal();
    constructed_rule_is_already_present = false;
    for (GridRule& rule : rules)
    {
        if (constructed_rule.matches(rule))
            constructed_rule_is_already_present = true;
    }
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

void GameState::render_region_bg(GridRegion& region, std::map<XYPos, int>& taken, std::map<XYPos, int>& total_taken)
{
    std::vector<XYPos> elements;

    FOR_XY_SET(pos, region.elements)
    {
        XYRect d = grid->get_bubble_pos(pos, grid_pitch, taken[pos], total_taken[pos]);
        XYPos n = d.pos + d.size / 2;
        elements.push_back(n);
        taken[pos]++;
    }
    int siz = elements.size();

    std::vector<int> group;
    std::vector<std::vector<double>> distances;
    distances.resize(siz);
    group.resize(siz);


    for (int i = 0; i < siz; i++)
    {
        group[i] = i;
        distances[i].resize(siz);
        for (int j = 0; j < siz; j++)
            distances[i][j] = XYPosFloat(elements[i]).distance(XYPosFloat(elements[j]));
    }

    set_region_colour(sdl_texture, region.type.value, region.colour, region.fade);
    while (true)
    {
        XYPos best_con;
        double best_distance = std::numeric_limits<double>::infinity();
        for (int i = 0; i < siz; i++)
        {
            for (int j = 0; j < siz; j++)
            {
                if (group[i] == group[j])
                    continue;
                if (distances[i][j] < best_distance)
                {
                    best_distance = distances[i][j];
                    best_con = XYPos(i,j);
                }
            }
        }
        {
            XYPos pos = elements[best_con.x];
            XYPos last = elements[best_con.y];
            double dist = XYPosFloat(pos).distance(XYPosFloat(last));
            double angle = XYPosFloat(pos).angle(XYPosFloat(last));

            SDL_Rect src_rect = {160, 608, 1, 1};
            SDL_Rect dst_rect = {grid_offset.x + pos.x, grid_offset.y + pos.y - (grid_pitch.y / 32), int(dist), grid_pitch.y / 16};

            SDL_Point rot_center;
            rot_center.x = 0;
            rot_center.y = grid_pitch.y / 32;

            SDL_RenderCopyEx(sdl_renderer, sdl_texture, &src_rect, &dst_rect, degrees(angle), &rot_center, SDL_FLIP_NONE);
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

void GameState::render_region_fg(GridRegion& region, std::map<XYPos, int>& taken, std::map<XYPos, int>& total_taken)
{
    FOR_XY_SET(pos, region.elements)
    {
        XYRect d = grid->get_bubble_pos(pos, grid_pitch, taken[pos], total_taken[pos]);
        XYPos n = d.pos;
        taken[pos]++;

        set_region_colour(sdl_texture, region.type.value, region.colour, region.fade);
        SDL_Rect src_rect = {64, 512, 192, 192};
        SDL_Rect dst_rect = {grid_offset.x + d.pos.x, grid_offset.y + d.pos.y, d.size.x, d.size.y};
        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);

        SDL_SetTextureColorMod(sdl_texture, 0,0,0);
        render_region_type(region.type, grid_offset + d.pos, d.size.x);
        if (XYPosFloat(mouse - grid_offset - d.pos - d.size / 2).distance() <= (d.size.x / 2))
        {
            mouse_hover_region = &region;
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
    XYPos box_pos = pos - XYPos(border, border);
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

void GameState::render_tooltip()
{
    if (tooltip_string != "")
    {
        SaveObjectMap* lang = lang_data->get_item(language)->get_map();
        SaveObjectMap* trans = lang->get_item("translate")->get_map();
        if (trans->has_key(tooltip_string))
        {
            std::string n = trans->get_string(tooltip_string);
            render_text_box(mouse + XYPos(-button_size / 4, button_size / 4), n, true);
        }
        else
        {
            render_text_box(mouse + XYPos(-button_size / 4, button_size / 4), tooltip_string, true);
        }
    }
}

void GameState::add_tooltip(SDL_Rect& dst_rect, const char* text)
{
    if ((mouse.x >= dst_rect.x) &&
        (mouse.x < (dst_rect.x + dst_rect.w)) &&
        (mouse.y >= dst_rect.y) &&
        (mouse.y < (dst_rect.y + dst_rect.h)))
    {
        tooltip_string = text;
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

void GameState::render_region_bubble(RegionType type, unsigned colour, XYPos pos, unsigned siz)
{
    set_region_colour(sdl_texture, type.value, colour, 255);
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
    XYPos window_size;
    bool row_col_clues = !grid->edges.empty();
    SDL_GetWindowSize(sdl_window, &window_size.x, &window_size.y);
    SDL_RenderClear(sdl_renderer);

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
            int border = grid_size / 8;
            grid_offset += XYPos(border, border);
            grid_size -= border;
        }

        grid_pitch = grid->get_grid_pitch(XYPos(grid_size, grid_size));
    }

    if (display_mode == DISPLAY_MODE_HELP)
    {
        int sq_size = std::min(window_size.y / 9, window_size.x / 16);
        XYPos help_image_size = XYPos(16 * sq_size, 9 * sq_size);
        XYPos help_image_offset = (window_size - help_image_size) / 2;

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


        SDL_RenderPresent(sdl_renderer);
        return;
    }

    tooltip_string = "";
    bool hover_rulemaker = false;
    XYSet hover_squares_highlight;
    int hover_rulemaker_bits = 0;
    bool hover_rulemaker_lower_right = false;

    int hover_rulemaker_region_base_index = -1;
    bool hover_rulemaker_region_base = false;

    mouse_hover_region = NULL;

    if (right_panel_mode == RIGHT_MENU_REGION)
    {
        if ((mouse - (right_panel_offset + XYPos(0, 0))).inside(XYPos(button_size, button_size)))
            mouse_hover_region = inspected_region;
        if ((mouse - (right_panel_offset + XYPos(0, button_size))).inside(XYPos(button_size, button_size)))
        {
            hover_rulemaker = true;
            hover_squares_highlight = inspected_region->elements;
        }
    }

    if (right_panel_mode == RIGHT_MENU_RULE_GEN || right_panel_mode == RIGHT_MENU_RULE_INSPECT)
    {
        GridRegionCause rule_cause = (right_panel_mode == RIGHT_MENU_RULE_GEN) ? GridRegionCause(&constructed_rule, rule_gen_region[0], rule_gen_region[1], rule_gen_region[2], rule_gen_region[3]) : inspected_rule;

        if ((mouse - (right_panel_offset + XYPos(0, 0))).inside(XYPos(button_size, button_size)) && rule_cause.rule->region_count >= 1)
            hover_rulemaker_region_base_index = 0;
        if ((mouse - (right_panel_offset + XYPos(2 * button_size, 0))).inside(XYPos(button_size, button_size)) && rule_cause.rule->region_count >= 2)
            hover_rulemaker_region_base_index = 1;
        if ((mouse - (right_panel_offset + XYPos(4 * button_size, 2 * button_size))).inside(XYPos(button_size, button_size)) && rule_cause.rule->region_count >= 3)
            hover_rulemaker_region_base_index = 2;
        if ((mouse - (right_panel_offset + XYPos(4 * button_size, 4 * button_size))).inside(XYPos(button_size, button_size)) && rule_cause.rule->region_count >= 4)
            hover_rulemaker_region_base_index = 3;

        if (hover_rulemaker_region_base_index >= 0)
        {
            hover_rulemaker_region_base = true;
            mouse_hover_region = rule_cause.regions[hover_rulemaker_region_base_index];
        }

        if ((mouse - (right_panel_offset + XYPos(0, button_size))).inside(XYPos(button_size*4, button_size * 6)))
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
            y--;
            y ^= y >> 1;

            hover_rulemaker_bits = x + y * 4;
            if (hover_rulemaker_bits >= 1 && hover_rulemaker_bits < (1 << rule_cause.rule->region_count))
            {
                hover_rulemaker = true;
                hover_squares_highlight = ~hover_squares_highlight;
                for (int i = 0; i < rule_cause.rule->region_count; i++)
                {
                    hover_squares_highlight = hover_squares_highlight & (((hover_rulemaker_bits >> i) & 1) ? rule_cause.regions[i]->elements : ~rule_cause.regions[i]->elements);
                }
            }
        }
    }

    XYSet grid_squares = grid->get_squares();
    FOR_XY_SET(pos, grid_squares)
    {
        {
            if (clue_solves.count(pos))
                SDL_SetTextureColorMod(sdl_texture, 0, 255, 0);
            if (filter_pos.get(pos))
                SDL_SetTextureColorMod(sdl_texture, 255,0, 0);
        }
        std::vector<RenderCmd> cmds;
        grid->render_square(pos, grid_pitch, cmds, hover_rulemaker && hover_squares_highlight.get(pos));
        for (RenderCmd& cmd : cmds)
        {
            SDL_Rect src_rect = {cmd.src.pos.x, cmd.src.pos.y, cmd.src.size.x, cmd.src.size.y};
            SDL_Rect dst_rect = {grid_offset.x + cmd.dst.pos.x, grid_offset.y + cmd.dst.pos.y, cmd.dst.size.x, cmd.dst.size.y};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
        }

        // if (hover_rulemaker)
        // {
        //     if (hover_squares_highlight.get(pos))
        //     {
        //         SDL_Rect src_rect = {793, 250, 1, 1};
        //         SDL_Rect dst_rect = {grid_offset.x + sq_pos.pos.x, grid_offset.y + sq_pos.pos.y, sq_pos.size.x, sq_pos.size.y};
        //         SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
        //     }
        // }
        //
//        render_box(grid_offset + sq_pos.pos, sq_pos.size, grid_pitch.x / 8, 2);

        // SDL_Rect src_rect = {64, 256, 192, 192};
        // SDL_Rect dst_rect = {grid_offset.x + sq_pos.pos.x, grid_offset.y + sq_pos.pos.y, sq_pos.size.x, sq_pos.size.y};
        // SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
        //
        SDL_SetTextureColorMod(sdl_texture, 255, 255, 255);
    }

    std::map<XYPos, int> total_taken;
    std::list<GridRegion*> display_regions;

    XYPos mouse_filter_pos(-1,-1);
    if ((mouse_mode == MOUSE_MODE_FILTER) && (mouse - grid_offset).inside(XYPos(grid_size,grid_size)))
    {
        mouse_filter_pos = grid->get_square_from_mouse_pos(mouse - grid_offset, grid_pitch);
        mouse_filter_pos = grid->get_base_square(mouse_filter_pos);
    }
    int region_vis_counts[3] = {0,0,0};
    {
        bool has_hover = false;
        for (GridRegion& region : grid->regions)
        {
            region_vis_counts[int(region.vis_level)]++;
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

    {
        std::map<XYPos, int> taken;

        for (GridRegion* region : display_regions)
        {
            render_region_bg(*region, taken, total_taken);
        }
    }
    {
        std::map<XYPos, int> taken;
        for (GridRegion* region : display_regions)
        {
            render_region_fg(*region, taken, total_taken);
        }
    }

    for (GridRegion* region : display_regions)
    {
        if (!mouse_hover_region || (region == mouse_hover_region))
        {
            region->fade += std::min(10, 255 - int(region->fade));
        }
        else
        {
            region->fade -= std::min(10, int(region->fade - 100));
            if (region->fade < 100)
            {
                region->fade += std::min(10, 100 - int(region->fade));
            }
        }
    }

    FOR_XY_SET(pos, grid_squares)
    {
        XYRect sq_pos = grid->get_square_pos(pos, grid_pitch);
        int icon_width = std::min(sq_pos.size.x, sq_pos.size.y);
        GridPlace place = grid->get(pos);
        XYPos gpos = grid_offset + sq_pos.pos + (sq_pos.size - XYPos(icon_width,icon_width)) / 2;

        if (place.revealed)
        {
            if (place.bomb)
            {
                SDL_Rect src_rect = {320, 192, 192, 192};
                SDL_Rect dst_rect = {gpos.x, gpos.y, icon_width, icon_width};
                SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            }
            else
            {
                render_region_type(place.clue, gpos, icon_width);
            }
        }
    }
    {
        std::vector<EdgePos> edges;
        grid->get_edges(edges, grid_pitch);
        for (EdgePos& edge : edges)
        {
            int arrow_size = grid_size / 9;
            int bubble_margin = arrow_size / 16;

            double p = edge.pos / std::cos(edge.angle);
            if (p >= 0 && p < grid_size)
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
            }
            p = -edge.pos / std::sin(edge.angle);
            if (p >= 0 && p < grid_size)
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
            }
        }

    }
    {
        SDL_Rect src_rect = {704, 960, 192, 192};
        SDL_Rect dst_rect = {left_panel_offset.x + 0 * button_size, left_panel_offset.y + button_size * 0, button_size, button_size};
        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
        add_tooltip(dst_rect, "Help");
    }
    {
        SDL_Rect src_rect = {1472, 960, 192, 192};
        SDL_Rect dst_rect = {left_panel_offset.x + 1 * button_size, left_panel_offset.y + button_size * 0, button_size, button_size};
        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
        add_tooltip(dst_rect, "Hint");
    }
    {
        SDL_Rect src_rect = {704 + 192, 960, 192, 192};
        SDL_Rect dst_rect = {left_panel_offset.x + 2 * button_size, left_panel_offset.y + button_size * 0, button_size, button_size};
        if (auto_region)
            src_rect = {1280, 768, 192, 192};
        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
        add_tooltip(dst_rect, "Auto Region");
    }
    {
        SDL_Rect src_rect = {704 + 192 * 3, 960, 192, 192};
        SDL_Rect dst_rect = {left_panel_offset.x + 3 * button_size, left_panel_offset.y + button_size * 0, button_size, button_size};
        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
        add_tooltip(dst_rect, "Skip Level");
    }
    {
        SDL_Rect src_rect = {704 + 192 * 2, 960, 192, 192};
        SDL_Rect dst_rect = {left_panel_offset.x + 4 * button_size, left_panel_offset.y + button_size * 0, button_size, button_size};
        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
        add_tooltip(dst_rect, "Refresh Regions");
    }
    {
        SDL_Rect src_rect = {1472, 1152, 192, 192};
        SDL_Rect dst_rect = {left_panel_offset.x + 0 * button_size, left_panel_offset.y + button_size * 1, button_size, button_size};
        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
        add_tooltip(dst_rect, "Full Screen");
    }
    {
        SDL_Rect src_rect = {1280, 1344, 192, 192};
        SDL_Rect dst_rect = {left_panel_offset.x + 1 * button_size, left_panel_offset.y + button_size * 1, button_size, button_size};
        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
        add_tooltip(dst_rect, "Select Language");
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
        SDL_Rect src_rect = {1536, 384, 192, 192};
        SDL_Rect dst_rect = {left_panel_offset.x + 0 * button_size, left_panel_offset.y + button_size * 2, button_size, button_size};
        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
        add_tooltip(dst_rect, "Rules");
        int rule_count = rules.size();
        render_number(rule_count, left_panel_offset + XYPos(button_size * 1 + button_size / 8, button_size * 2 + button_size / 4), XYPos(button_size * 3 / 4, button_size / 2));
    }


    {
        if (vis_level == GRID_VIS_LEVEL_SHOW)
           render_box(left_panel_offset+ XYPos(button_size * 3, button_size * 2), XYPos(button_size * 2, button_size), button_size/4);
        render_number(region_vis_counts[0], left_panel_offset + XYPos(button_size * 3 + button_size / 8, button_size * 2 + button_size / 4), XYPos(button_size * 3 / 4, button_size / 2));
        SDL_Rect src_rect = {1088, 384, 192, 192};
        SDL_Rect dst_rect = {left_panel_offset.x + 4 * button_size, left_panel_offset.y + button_size * 2, button_size, button_size};
        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
        add_tooltip(dst_rect, "Visible");
    }
    {
        if (vis_level == GRID_VIS_LEVEL_HIDE)
            render_box(left_panel_offset+ XYPos(button_size * 3, button_size * 3), XYPos(button_size * 2, button_size), button_size/4);
        render_number(region_vis_counts[1], left_panel_offset + XYPos(button_size * 3 + button_size / 8, button_size * 3 + button_size / 4), XYPos(button_size * 3 / 4, button_size / 2));
        SDL_Rect src_rect = {896, 384, 192, 192};
        SDL_Rect dst_rect = {left_panel_offset.x + 4 * button_size, left_panel_offset.y + button_size * 3, button_size, button_size};
        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
        add_tooltip(dst_rect, "Hidden");
    }
    {
        if (vis_level == GRID_VIS_LEVEL_BIN)
           render_box(left_panel_offset+ XYPos(button_size * 3, button_size * 4), XYPos(button_size * 2, button_size), button_size/4);
        render_number(region_vis_counts[2], left_panel_offset + XYPos(button_size * 3 + button_size / 8, button_size * 4 + button_size / 4), XYPos(button_size * 3 / 4, button_size / 2));
        SDL_Rect src_rect = {512, 768, 192, 192};
        SDL_Rect dst_rect = {left_panel_offset.x + 4 * button_size, left_panel_offset.y + button_size * 4, button_size, button_size};
        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
        add_tooltip(dst_rect, "Trash");
    }


    XYPos p = XYPos(0,0);
    for (int i = 0; i < level_progress.size(); i++)
    {
        XYPos pos = left_panel_offset + XYPos(button_size * (i % 5), button_size * (i / 5 + 6));
        if (i == current_level_set_index)
            render_box(pos, XYPos(button_size, button_size), button_size/4);
        int c = level_progress[i].count_todo;
        if (c)
            render_number(level_progress[i].count_todo, pos + XYPos(button_size / 8, button_size / 8), XYPos(button_size * 3 / 4 , button_size * 3 / 4));
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
        {
            SDL_Rect src_rect = {384, 864, 128, 128};
            SDL_Rect dst_rect = {right_panel_offset.x + 4 * button_size, right_panel_offset.y + button_size * 7, button_size, button_size};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
        }
        for (int i = 0; i < 5; i++)
        {
            SDL_Rect src_rect = {512, (region_menu == i) ? 1152 : 1344, 192, 192};
            SDL_Rect dst_rect = {right_panel_offset.x + i * button_size, right_panel_offset.y + button_size * 7, button_size, button_size};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
        }

        RegionType r_type = menu_region_types1[region_menu];
        if (r_type.type != RegionType::NONE)
        {
            FOR_XY(pos, XYPos(), XYPos(5, 2))
            {
                if (region_type == r_type)
                    render_box(right_panel_offset+ pos * button_size + XYPos(0, button_size * 8), XYPos(button_size, button_size), button_size/4);
                render_region_type(r_type, right_panel_offset + pos * button_size + XYPos(0, button_size * 8), button_size);
                r_type.value ++;
            }
        }

        r_type = menu_region_types2[region_menu];
        if (r_type.type != RegionType::NONE)
        {
            FOR_XY(pos, XYPos(), XYPos(5, 2))
            {
                if (region_type == r_type)
                    render_box(right_panel_offset+ pos * button_size + XYPos(0, button_size * 10 + button_size/2), XYPos(button_size, button_size), button_size/4);
                render_region_type(r_type, right_panel_offset + pos * button_size + XYPos(0, button_size * 10 + button_size/2), button_size);
                r_type.value ++;
            }
        }

        {
            if (region_type == RegionType(RegionType::SET, 0))
                render_box(right_panel_offset + XYPos(button_size * 1, button_size * 6), XYPos(button_size, button_size), button_size/4);
            SDL_Rect src_rect = {512, 192, 192, 192};
            SDL_Rect dst_rect = {right_panel_offset.x + button_size * 1, right_panel_offset.y + button_size * 6, button_size, button_size};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            add_tooltip(dst_rect, "Clear");

            if (region_type == RegionType(RegionType::SET, 1))
                render_box(right_panel_offset + XYPos(button_size * 2, button_size * 6), XYPos(button_size, button_size), button_size/4);
            src_rect = {320, 192, 192, 192};
            dst_rect = {right_panel_offset.x + button_size * 2, right_panel_offset.y + button_size * 6, button_size, button_size};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            add_tooltip(dst_rect, "Bomb");
        }

        {
            if (region_type.type == RegionType::NONE)
                render_box(right_panel_offset + XYPos(0, button_size * 6), XYPos(button_size, button_size), button_size/4);
            SDL_Rect src_rect = {896, 192, 192, 192};
            SDL_Rect dst_rect = {right_panel_offset.x, right_panel_offset.y + button_size * 6, button_size, button_size};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            add_tooltip(dst_rect, "Don't Care");
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
        set_region_colour(sdl_texture, inspected_region->type.value, inspected_region->colour, 255);
        {
            render_box(right_panel_offset + XYPos(0 * button_size, 0 * button_size), XYPos(1 * button_size, 2 * button_size), button_size / 2);
            render_region_bubble(inspected_region->type, inspected_region->colour, right_panel_offset + XYPos(0 * button_size, 0 * button_size), button_size * 2 / 3);
        }
        SDL_SetTextureColorMod(sdl_texture, 255, 255, 255);
        if (hover_rulemaker)
            render_box(right_panel_offset + XYPos(0 * button_size, 1 * button_size), XYPos(button_size, button_size), button_size / 4);
        {
            // int count = inspected_region->elements.size();
            // SDL_Rect src_rect = {192 * count, 0, 192, 192};
            // SDL_Rect dst_rect = {right_panel_offset.x + button_size / 2 + button_size/8, right_panel_offset.y + 2 * button_size + button_size/8, button_size*6/8, button_size*6/8};
            // SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            //
            RegionType r_type(RegionType::EQUAL, (uint8_t) inspected_region->elements.count());
            render_region_type(r_type, right_panel_offset + XYPos(0, 1 * button_size), button_size);

        }
        {
            SDL_Rect src_rect = { 1088, 576, 192, 192 };
            SDL_Rect dst_rect = { right_panel_offset.x + button_size * 3, right_panel_offset.y, button_size, button_size};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            add_tooltip(dst_rect, "Add to Rule Generator");
        }
        if (inspected_region->gen_cause.rule)
        {
            SDL_Rect src_rect = { 704, 768, 192, 192 };
            SDL_Rect dst_rect = { right_panel_offset.x + button_size * 0, right_panel_offset.y + 4 * button_size, button_size, button_size };
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            add_tooltip(dst_rect, "Show Creation Rule");
        }

        if (inspected_region->vis_cause.rule)
        {
            SDL_Rect src_rect = { 896, 768, 192, 192 };
            SDL_Rect dst_rect = { right_panel_offset.x + button_size * 1, right_panel_offset.y + 4 * button_size, button_size, button_size };
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            add_tooltip(dst_rect, "Show Visibility Rule");
        }

        {
            if (inspected_region->vis_level == GRID_VIS_LEVEL_SHOW)
               render_box(right_panel_offset+ XYPos(button_size * 3, button_size * 2), XYPos(button_size, button_size), button_size/4);

            SDL_Rect src_rect = {1088, 384, 192, 192};
            SDL_Rect dst_rect = {right_panel_offset.x + 3 * button_size, right_panel_offset.y + button_size * 2, button_size, button_size};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            add_tooltip(dst_rect, "Visible");
        }
        {
            if (inspected_region->vis_level == GRID_VIS_LEVEL_HIDE)
               render_box(right_panel_offset+ XYPos(button_size * 3, button_size * 3), XYPos(button_size, button_size), button_size/4);
            SDL_Rect src_rect = {896, 384, 192, 192};
            SDL_Rect dst_rect = {right_panel_offset.x + 3 * button_size, right_panel_offset.y + button_size * 3, button_size, button_size};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            add_tooltip(dst_rect, "Hidden");
        }
        {
            if (inspected_region->vis_level == GRID_VIS_LEVEL_BIN)
               render_box(right_panel_offset+ XYPos(button_size * 3, button_size * 4), XYPos(button_size, button_size), button_size/4);
            SDL_Rect src_rect = {512, 768, 192, 192};
            SDL_Rect dst_rect = {right_panel_offset.x + 3 * button_size, right_panel_offset.y + button_size * 4, button_size, button_size};
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

            unsigned colour = (right_panel_mode == RIGHT_MENU_RULE_GEN) ? rule_gen_region[0]->colour : 0;
            set_region_colour(sdl_texture, rule.region_type[0].value, colour, 255);
            render_box(right_panel_offset + XYPos(0 * button_size, 0 * button_size), XYPos(siz.x * button_size, siz.y * button_size), button_size / 2);
            render_region_bubble(rule.region_type[0], colour, right_panel_offset + XYPos(0 * button_size, 0 * button_size), button_size * 2 / 3);

        }
        if (rule.region_count >= 2)
        {
            XYPos siz = XYPos(2,2);
            if (rule.region_count >= 3) siz.y = 3;
            if (rule.region_count >= 4) siz.y = 5;

            unsigned colour = (right_panel_mode == RIGHT_MENU_RULE_GEN) ? rule_gen_region[1]->colour : 0;
            set_region_colour(sdl_texture, rule.region_type[1].value, colour, 255);
            render_box(right_panel_offset + XYPos(1 * button_size, button_size / 12), XYPos(siz.x * button_size, siz.y * button_size), button_size / 2);
            render_region_bubble(rule.region_type[1], colour, right_panel_offset + XYPos(2 * button_size + button_size / 3, button_size / 12), button_size * 2 / 3);

        }

        if (rule.region_count >= 3)
        {
            XYPos siz = XYPos(5,1);
            if (rule.region_count >= 4) siz.y = 2;
            unsigned colour = (right_panel_mode == RIGHT_MENU_RULE_GEN) ? rule_gen_region[2]->colour : 0;
            set_region_colour(sdl_texture, rule.region_type[2].value, colour, 255);
            render_box(right_panel_offset + XYPos(0 * button_size, 2 * button_size), XYPos(siz.x * button_size, siz.y * button_size), button_size / 2);
            render_region_bubble(rule.region_type[2], colour, right_panel_offset + XYPos(4 * button_size + button_size / 3, 2 * button_size), button_size * 2 / 3);
        }

        if (rule.region_count >= 4)
        {
            unsigned colour = (right_panel_mode == RIGHT_MENU_RULE_GEN) ? rule_gen_region[3]->colour : 0;
            set_region_colour(sdl_texture, rule.region_type[3].value, colour, 255);
            render_box(right_panel_offset + XYPos(button_size / 12, 3 * button_size), XYPos(5 * button_size, 2 * button_size), button_size / 2);
            render_region_bubble(rule.region_type[3], colour, right_panel_offset + XYPos(button_size / 12 + 4 * button_size + button_size / 3, 4 * button_size + button_size / 3), button_size * 2 / 3);
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
                SDL_Rect dst_rect = {right_panel_offset.x + button_size / 3, right_panel_offset.y + button_size / 3, button_size * 2 / 3, button_size * 2 / 3};
                SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            }
            if (rule.apply_region_bitmap & 2)
            {
                SDL_Rect dst_rect = {right_panel_offset.x + button_size * 2, right_panel_offset.y + button_size / 3, button_size * 2 / 3, button_size * 2 / 3};
                SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            }
            if (rule.apply_region_bitmap & 4)
            {
                SDL_Rect dst_rect = {right_panel_offset.x + button_size * 4, right_panel_offset.y + 2 * button_size + button_size / 3, button_size * 2 / 3, button_size * 2 / 3};
                SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            }
            if (rule.apply_region_bitmap & 8)
            {
                SDL_Rect dst_rect = {right_panel_offset.x + button_size * 4, right_panel_offset.y + 4 * button_size, button_size * 2 / 3, button_size * 2 / 3};
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
            y++;

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
                if (region_type.type >= 50)
                    new_region_type = RegionType(RegionType::NONE, 0);
                if (new_region_type == rule.square_counts[hover_rulemaker_bits])
                {
                    uint8_t square_counts[16];
                    GridRule::get_square_counts(square_counts, rule_gen_region[0], rule_gen_region[1], rule_gen_region[2], rule_gen_region[3]);
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
            else
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
        }

        if (right_panel_mode == RIGHT_MENU_RULE_GEN)
        {
            if (rule.region_count >= 1)
            {
                SDL_Rect src_rect = { 192*3+128, 192*3, 192, 192 };
                SDL_Rect dst_rect = { right_panel_offset.x + button_size * 3, right_panel_offset.y, button_size, button_size};
                SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                add_tooltip(dst_rect, "Cancel");

            }

            if (rule.region_count >= 1 && constructed_rule.apply_region_bitmap)
            {
                if (!constructed_rule_is_logical)
                {
                    SDL_Rect src_rect = {896, 576, 192, 192};
                    SDL_Rect dst_rect = {right_panel_offset.x + button_size * 4, right_panel_offset.y, button_size, button_size };
                    SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                    add_tooltip(dst_rect, "Illogical");
                }
                else if (constructed_rule_is_already_present)
                {
                    SDL_Rect src_rect = {1088, 768, 192, 192};
                    SDL_Rect dst_rect = {right_panel_offset.x + button_size * 4, right_panel_offset.y, button_size, button_size };
                    SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                    add_tooltip(dst_rect, "Rule Already Present");
                }
                else

                {
                    SDL_Rect src_rect = {704, 384, 192, 192};
                    SDL_Rect dst_rect = {right_panel_offset.x + button_size * 4, right_panel_offset.y, button_size, button_size };
                    SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                    add_tooltip(dst_rect, "OK");
                }
            }
        }
        if (right_panel_mode == RIGHT_MENU_RULE_INSPECT)
        {
            {
                SDL_Rect src_rect = { inspected_rule.rule->deleted ? 1280 + 192 : 1280, 768, 192, 192};
                SDL_Rect dst_rect = { right_panel_offset.x + button_size * 4, right_panel_offset.y, button_size, button_size};
                SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                add_tooltip(dst_rect, "Remove Rule");
            }
            {
                SDL_Rect src_rect = {1280, 576, 192, 192};
                SDL_Rect dst_rect = { right_panel_offset.x + button_size * 3, right_panel_offset.y + button_size, button_size, button_size};
                SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                add_tooltip(dst_rect, "Edit Rule");
            }
            {
                SDL_Rect src_rect = {1472, 576, 192, 192};
                SDL_Rect dst_rect = { right_panel_offset.x + button_size * 4, right_panel_offset.y + button_size, button_size, button_size};
                SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                add_tooltip(dst_rect, "Duplicate Rule");
            }
        }
    }
    if (display_mode == DISPLAY_MODE_LANGUAGE)
    {
        render_box(left_panel_offset + XYPos(button_size, button_size), XYPos(5 * button_size, 10 * button_size), button_size/4, 1);
        int index = 0;
        for (std::map<std::string, SaveObject*>::iterator it = lang_data->omap.begin(); it != lang_data->omap.end(); ++it)
        {
            std::string s = it->first;
            if (s == language)
                SDL_SetTextureColorMod(sdl_texture, 0,255, 0);
            render_text_box(left_panel_offset + XYPos(button_size * 2, button_size * (2 + index)), s);
            SDL_SetTextureColorMod(sdl_texture, 255,255, 255);
            index++;
        }
    }
    else
    {
        render_tooltip();
    }
    SDL_RenderPresent(sdl_renderer);
}

void GameState::grid_click(XYPos pos, bool right)
{
    if (right)
    {
        reset_rule_gen_region();
        return;
    }

    if (mouse_mode == MOUSE_MODE_FILTER)
    {
        XYPos gpos = grid->get_square_from_mouse_pos(pos, grid_pitch);
        gpos = grid->get_base_square(gpos);
        if (gpos.x >= 0)
            filter_pos.flip(gpos);
    }
    else
    {
        if (mouse_hover_region)
        {
            {
                if (inspected_region == mouse_hover_region && right_panel_mode == RIGHT_MENU_REGION)
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
                            rule_gen_region[constructed_rule.region_count] = mouse_hover_region;
                        constructed_rule.import_rule_gen_regions(rule_gen_region[0], rule_gen_region[1], rule_gen_region[2], rule_gen_region[3]);
                        update_constructed_rule();
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

void GameState::left_panel_click(XYPos pos, bool right)
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
        display_mode = DISPLAY_MODE_HELP;
    if ((pos - XYPos(button_size * 1, button_size * 0)).inside(XYPos(button_size,button_size)))
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
    if ((pos - XYPos(button_size * 2, button_size * 0)).inside(XYPos(button_size,button_size)))
        auto_region = !auto_region;
    if ((pos - XYPos(button_size * 3, button_size * 0)).inside(XYPos(button_size,button_size)))
        skip_level = true;
    if ((pos - XYPos(button_size * 4, button_size * 0)).inside(XYPos(button_size,button_size)))
    {
        clue_solves.clear();
        grid->clear_regions();
        reset_rule_gen_region();
    }
    if ((pos - XYPos(button_size * 0, button_size * 1)).inside(XYPos(button_size,button_size)))
    {
        full_screen = !full_screen;
        SDL_SetWindowFullscreen(sdl_window, full_screen? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
        SDL_SetWindowBordered(sdl_window, full_screen ? SDL_FALSE : SDL_TRUE);
        SDL_SetWindowResizable(sdl_window, full_screen ? SDL_FALSE : SDL_TRUE);
        SDL_SetWindowInputFocus(sdl_window);
    }
    if ((pos - XYPos(button_size * 1, button_size * 1)).inside(XYPos(button_size,button_size)))
        display_mode = DISPLAY_MODE_LANGUAGE;

    XYPos gpos = pos / button_size;
    gpos.y -= 6;
    int idx = gpos.x + gpos.y * 5;

    if ((idx >= 0) && (idx < level_progress.size()))
    {
        if (level_progress[idx].level_status.size())
        {
            current_level_set_index = idx;
            current_level_index = 0;
            skip_level = true;
        }
    }
}

void GameState::right_panel_click(XYPos pos, bool right)
{
    XYPos bpos = pos / button_size;

    if (right_panel_mode == RIGHT_MENU_REGION)
    {
        if ((pos - XYPos(button_size * 3, 0)).inside(XYPos(button_size, button_size)))
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
                    rule_gen_region[constructed_rule.region_count] = inspected_region;
                constructed_rule.import_rule_gen_regions(rule_gen_region[0], rule_gen_region[1], rule_gen_region[2], rule_gen_region[3]);
                update_constructed_rule();
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

        if ((pos - XYPos(button_size * 3, button_size * 2)).inside(XYPos(button_size, button_size)))
        {
            inspected_region->vis_level = GRID_VIS_LEVEL_SHOW;
            inspected_region->visibility_force = GridRegion::VIS_FORCE_USER;
            inspected_region->stale = false;
        }
        if ((pos - XYPos(button_size * 3, button_size * 3)).inside(XYPos(button_size, button_size)))
        {
            inspected_region->vis_level = GRID_VIS_LEVEL_HIDE;
            inspected_region->visibility_force = GridRegion::VIS_FORCE_USER;
            inspected_region->stale = false;
        }
        if ((pos - XYPos(button_size * 3, button_size * 4)).inside(XYPos(button_size, button_size)))
        {
            inspected_region->vis_level = GRID_VIS_LEVEL_BIN;
            inspected_region->visibility_force = GridRegion::VIS_FORCE_USER;
            inspected_region->stale = false;
        }

    }

    if (right_panel_mode == RIGHT_MENU_RULE_INSPECT)
    {
        if ((pos - XYPos(button_size * 4, 0)).inside(XYPos(button_size, button_size)))
        {
            inspected_rule.rule->deleted = !inspected_rule.rule->deleted;
            inspected_rule.rule->stale = false;
        }
        if ((pos - XYPos(button_size * 3, button_size * 1)).inside(XYPos(button_size * 2, button_size)))
        {
            constructed_rule = *inspected_rule.rule;
            rule_gen_region[0] = inspected_rule.regions[0];
            rule_gen_region[1] = inspected_rule.regions[1];
            rule_gen_region[2] = inspected_rule.regions[2];
            rule_gen_region[3] = inspected_rule.regions[3];
            constructed_rule.deleted = false;
            constructed_rule.stale = false;
            if ((pos - XYPos(button_size * 3, button_size * 1)).inside(XYPos(button_size, button_size)))
            {
                inspected_rule.rule->deleted = true;
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
        if ((mouse - (right_panel_offset + XYPos(0, 0))).inside(XYPos(button_size, button_size)) && rule_cause.rule->region_count >= 1)
            region_index = 0;
        if ((mouse - (right_panel_offset + XYPos(2 * button_size, 0))).inside(XYPos(button_size, button_size)) && rule_cause.rule->region_count >= 2)
            region_index = 1;
        if ((mouse - (right_panel_offset + XYPos(4 * button_size, 2 * button_size))).inside(XYPos(button_size, button_size)) && rule_cause.rule->region_count >= 3)
            region_index = 2;
        if ((mouse - (right_panel_offset + XYPos(4 * button_size, 4 * button_size))).inside(XYPos(button_size, button_size)) && rule_cause.rule->region_count >= 4)
            region_index = 3;

        if (region_index >= 0)
        {
            if ((right_panel_mode == RIGHT_MENU_RULE_GEN) && (region_type.type == RegionType::VISIBILITY))
            {
                constructed_rule.apply_region_bitmap ^= 1 << region_index;
                if (constructed_rule.apply_region_type != region_type)
                {
                    constructed_rule.apply_region_type = region_type;
                    constructed_rule.apply_region_bitmap = 1 << region_index;
                }
                update_constructed_rule();
            }
            else
            {
                right_panel_mode = RIGHT_MENU_REGION;
                inspected_region = rule_cause.regions[region_index];
            }
        }
    }

    if (right_panel_mode == RIGHT_MENU_RULE_GEN)
    {
        if ((pos - XYPos(button_size * 1, button_size * 6)).inside(XYPos(button_size,button_size)))
            region_type = RegionType(RegionType::SET, 0);
        if ((pos - XYPos(button_size * 2, button_size * 6)).inside(XYPos(button_size,button_size)))
            region_type = RegionType(RegionType::SET, 1);
        if ((pos - XYPos(button_size * 0, button_size * 6)).inside(XYPos(button_size,button_size)))
            region_type = RegionType(RegionType::NONE, 0);

        if ((pos - XYPos(button_size * 0, button_size * 7)).inside(XYPos(5 * button_size,1 * button_size)))
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
        if ((pos - XYPos(0, button_size)).inside(XYPos(button_size * 4, button_size * 6)))
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
            y--;
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
                    uint8_t square_counts[16];
                    RegionType new_region_type = region_type;
                    if (region_type.type >= 50)
                        new_region_type = RegionType(RegionType::NONE, 0);
                    GridRule::get_square_counts(square_counts, rule_gen_region[0], rule_gen_region[1], rule_gen_region[2], rule_gen_region[3]);
                    if (constructed_rule.square_counts[hover_rulemaker_bits] == new_region_type)
                    {
                        constructed_rule.square_counts[hover_rulemaker_bits] = RegionType(RegionType::EQUAL, square_counts[hover_rulemaker_bits]);
                    }
                    else if (new_region_type.type == RegionType::NONE || new_region_type.apply_int_rule(square_counts[hover_rulemaker_bits]))
                    {
                        constructed_rule.square_counts[hover_rulemaker_bits] = new_region_type;
                    }
//                    rule_gen_region_undef_num ^= 1 << hover_rulemaker_bits;
                    update_constructed_rule();
                }
                else if (region_type.type != RegionType::VISIBILITY && region_type.type != RegionType::NONE)
                {
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

        if ((pos - XYPos(button_size * 4 , button_size * 0)).inside(XYPos(button_size, button_size)))
        {
            if (constructed_rule.region_count && constructed_rule.apply_region_bitmap)
            {
                if (constructed_rule.is_legal())
                {
                    rules.push_back(constructed_rule);
                    reset_rule_gen_region();
                }
            }
        }

        if ((pos - XYPos(button_size * 3, 0)).inside(XYPos(button_size, button_size)))
        {
            if (constructed_rule.region_count)
            {

                rule_gen_region[constructed_rule.region_count - 1] = NULL;
                constructed_rule.import_rule_gen_regions(rule_gen_region[0], rule_gen_region[1], rule_gen_region[2], rule_gen_region[3]);
                if (constructed_rule.region_count == 0)
                    right_panel_mode = RIGHT_MENU_NONE;
                update_constructed_rule();
            }

        }
    }




    // if ((pos - XYPos(0,button_size * 6)).inside(XYPos(button_size*4, button_size * 4)))
    // {
    //     pos = (pos - XYPos(0,button_size * 6)) / (button_size/2);
    //     int index = pos.y + pos.x * 8;
    //     std::list<GridRule>::iterator it = rules.begin();
    //     std::advance(it,index);
    //     if (it != rules.end())
    //     {
    //         GridRule& rule = *it;
    //         rule.halted = true;
    //     }
    // }

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
                break;

            case SDL_QUIT:
		        quit = true;
                break;
            case SDL_KEYDOWN:
            {
                if (display_mode == DISPLAY_MODE_HELP || display_mode == DISPLAY_MODE_LANGUAGE)
                {
                    switch (e.key.keysym.scancode)
                    {
                        case SDL_SCANCODE_F1:
                        case SDL_SCANCODE_ESCAPE:
                            display_mode = DISPLAY_MODE_NORMAL;
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
                        break;
                    }
                    // case SDL_SCANCODE_ESCAPE:
                    //     quit = true;
                    //     break;
                    case SDL_SCANCODE_F1:
                        display_mode = DISPLAY_MODE_HELP;
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
                        auto_region = !auto_region;
                        break;
                    }
                    case SDL_SCANCODE_F4:
                    {
                        skip_level = true;
                        break;
                    }
                    case SDL_SCANCODE_F5:
                    {
                        clue_solves.clear();
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
                    case SDL_SCANCODE_LSHIFT:
                        break;
                }
                break;
            case SDL_MOUSEMOTION:
            {
                mouse.x = e.motion.x;
                mouse.y = e.motion.y;
                break;
            }
            case SDL_MOUSEBUTTONUP:
            {
                mouse.x = e.button.x;
                mouse.y = e.button.y;
                break;
            }
            case SDL_MOUSEBUTTONDOWN:
            {
                mouse.x = e.button.x;
                mouse.y = e.button.y;
                bool right = (e.button.button != SDL_BUTTON_LEFT);

                if (display_mode == DISPLAY_MODE_HELP)
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
                        display_mode = DISPLAY_MODE_NORMAL;
                    if ((mouse - help_image_offset - help_image_size + XYPos(sq_size * 1, sq_size)).inside(XYPos(sq_size, sq_size)))
                        if (tutorial_index < (tut_texture_count - 1))
                            tutorial_index++;
                    break;
                }
                if (display_mode == DISPLAY_MODE_LANGUAGE)
                {
                    XYPos p = (mouse - left_panel_offset) / button_size;
                    p -= XYPos(2,2);
                    if (p.x >= 0 && p.y >= 0)
                    {
                        int index = 0;
                        for (std::map<std::string, SaveObject*>::iterator it = lang_data->omap.begin(); it != lang_data->omap.end(); ++it)
                        {
                            if (index == p.y)
                            {
                                language = it->first;
                            }
                            index++;
                        }
                        display_mode = DISPLAY_MODE_NORMAL;
                    }
                    break;
                }

                if ((mouse - left_panel_offset).inside(panel_size))
                {
                    left_panel_click(mouse - left_panel_offset, right);
                }
                else if ((mouse - right_panel_offset).inside(panel_size))
                {
                    right_panel_click(mouse - right_panel_offset, right);
                }
                else if ((mouse - grid_offset).inside(XYPos(grid_size,grid_size)))
                {
                    grid_click(mouse - grid_offset, right);
                }
                break;
            }

            case SDL_MOUSEWHEEL:
            {
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
    return quit;
}

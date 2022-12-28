#define _USE_MATH_DEFINES
#include <cmath>

#include "GameState.h"
#include "SaveState.h"
#include "Misc.h"
#include "LevelSet.h"

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
        level_progress[i].counts[0] = global_level_sets[i]->levels.size();
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
                        uint8_t stat = std::min(c - '0', 4);
                        level_progress[i].level_status[j] = stat;
                        level_progress[i].counts[stat]++;
                        level_progress[i].counts[0]--;
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

    }
    region_type = RegionType(RegionType::EQUAL, 1);

    sdl_window = SDL_CreateWindow( "Bombe", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1920/2, 1080/2, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | (full_screen? SDL_WINDOW_FULLSCREEN_DESKTOP  | SDL_WINDOW_BORDERLESS : 0));
    sdl_renderer = SDL_CreateRenderer(sdl_window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 0);
	sdl_texture = loadTexture("texture.png");

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
    grid = new  Grid(XYPos(1,1));
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
        for (uint8_t& stat : prog.level_status)
        {
            char c = '0' + stat;
            sstr += c;
        }
        plist->add_item(new SaveObjectString(sstr));
    }
    omap->add_item("level_progress", plist);
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

    TTF_CloseFont(font);

	SDL_DestroyTexture(sdl_texture);
	SDL_DestroyRenderer(sdl_renderer);
	SDL_DestroyWindow(sdl_window);
    LevelSet::delete_global();
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
    if (grid->is_solved() || skip_level)
    {
        clue_solves.clear();
        if (grid->is_solved() && !current_level_is_temp)
        {
            int o = level_progress[current_level_set_index].level_status[current_level_index];
            int n = 3;
            if (current_level_manual)
            {
                n = 2;
                if (current_level_hinted)
                    n = 1;
            }

            if (o < n)
            {
                level_progress[current_level_set_index].counts[o]--;
                level_progress[current_level_set_index].counts[n]++;
                level_progress[current_level_set_index].level_status[current_level_index] = n;
            }
        }
        skip_level = false;

        int want = 0;
        while (level_progress[current_level_set_index].counts[want] == 0)
            want++;
        if (want < 3)
        {
            do
            {
                current_level_index++;
                if (current_level_index >= level_progress[current_level_set_index].level_status.size())
                    current_level_index = 0;
            }
            while (level_progress[current_level_set_index].level_status[current_level_index] > want);
            std::string& s = global_level_sets[current_level_set_index]->levels[current_level_index];
            delete grid;
            grid = new  Grid(s);
            reset_rule_gen_region();
            current_level_is_temp = false;
            current_level_hinted = false;
            current_level_manual = false;
        }
    }

    if(clue_solves.empty())
        get_hint = false;

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
                    cooldown = 200;
                    return;
                }
            }
        }

    }

    if (auto_region)
    {
        {
            bool hit = false;
            bool check_vis = false;
            while (grid->add_regions(-1)) {check_vis = true; cooldown = 100;}

            if (!last_active_was_hit)
            {
                grid->add_one_new_region();
                check_vis = true;
                cooldown = 50;
            }
            if (check_vis)
            {
                for (std::list<GridRule>::iterator it=rules.begin(); it != rules.end(); ++it)
                {
                    GridRule& rule = *it;
                    if (rule.deleted)
                        continue;
                    if ((rule.apply_type == GridRule::HIDE) || (rule.apply_type == GridRule::SHOW) || (rule.apply_type == GridRule::BIN))
                    {
                        Grid::ApplyRuleResp resp  = grid->apply_rule(rule);
                        if (resp == Grid::APPLY_RULE_RESP_NONE)
                            rule.stale = true;
                    }

                }
            }
            last_active_was_hit = false;
            for (std::list<GridRule>::iterator it=rules.begin(); it != rules.end(); ++it)
            {
                GridRule& rule = *it;
                if (rule.deleted)
                    continue;
//                if (hit) break;
                if ((rule.apply_type == GridRule::HIDE) || (rule.apply_type == GridRule::SHOW) || (rule.apply_type == GridRule::BIN))
                    continue;

                Grid::ApplyRuleResp resp  = grid->apply_rule(rule);
                if (resp == Grid::APPLY_RULE_RESP_HIT)
                {
                    hit = true;
                    last_active_was_hit = true;
                    clue_solves.clear();
//                    break;
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
                // for (GridRegion& r : grid->regions)
                // {
                //     if (r.visibility_force == GridRegion::VIS_FORCE_NONE && r.vis_level == GRID_VIS_LEVEL_BIN)
                //     {
                //         r.vis_level = GRID_VIS_LEVEL_SHOW;
                //         r.stale = false;
                //     }
                // }
                // for (GridRule& rule : rules)
                // {
                //     if (rule.deleted)
                //         continue;
                //     if ((rule.apply_type == GridRule::HIDE) || (rule.apply_type == GridRule::SHOW) || (rule.apply_type == GridRule::BIN))
                //     {
                //         Grid::ApplyRuleResp resp  = grid->apply_rule(rule);
                //         assert(resp == Grid::APPLY_RULE_RESP_NONE);
                //     }
                //
                // }
            }
            else
            {
                for (GridRegion& r : grid->regions)
                {
                    r.stale = true;
                }
            }
        }
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

unsigned GameState::taken_to_size(unsigned total)
{
    unsigned size = 3;
    while (total > (size * size))
        size++;
    return size;
}

XYPos GameState::taken_to_pos(unsigned count, unsigned total)
{
    int size = taken_to_size(total);

    return (XYPos((count / size + count % size) % size, count % size) * square_size) / size + XYPos(square_size/(size*2), square_size/(size*2));

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
    XYPosFloat avg;
    int count = 0;
    std::list<XYPos> elements;

    FOR_XY_SET(pos, region.elements)
    {
        XYPos n = pos * square_size + taken_to_pos(taken[pos], total_taken[pos]);
        avg += n;
        count++;
        elements.push_back(n);
        taken[pos]++;
    }
    avg /= count;

    struct Local
    {
        XYPosFloat avg;
        Local(XYPosFloat avg_) :
            avg(avg_)
        {}
        bool operator () (const XYPos i, const XYPos j)
        {
            if (avg.angle(i) == avg.angle(j))
                return avg.distance(i) < avg.distance(j);
            return avg.angle(i) < avg.angle(j);
        }
    };
    avg += XYPosFloat(0.001,0.001);

    elements.sort(Local(avg));

    set_region_colour(sdl_texture, region.type.value, region.colour, region.fade);

    double longest = 0;
    XYPos longest_tgt;
    XYPos last = elements.back();
    for (XYPos pos : elements)
    {
        double dist = XYPosFloat(pos).distance(XYPosFloat(last));
        if (dist > longest)
        {
            longest = dist;
            longest_tgt = pos;
        }
        last = pos;
    }

    last = elements.back();
    for (XYPos pos : elements)
    {
        if (pos != longest_tgt)
        {
            double dist = XYPosFloat(pos).distance(XYPosFloat(last));
            double angle = XYPosFloat(pos).angle(XYPosFloat(last));

            SDL_Rect src_rect = {64, 256, 1, 1};
            SDL_Rect dst_rect = {grid_offset.x + pos.x, grid_offset.y + pos.y - (square_size / 32), int(dist), square_size / 16};

            SDL_Point rot_center;
            rot_center.x = 0;
            rot_center.y = square_size / 32;

            SDL_RenderCopyEx(sdl_renderer, sdl_texture, &src_rect, &dst_rect, degrees(angle), &rot_center, SDL_FLIP_NONE);
        }
        last = pos;
    }
    SDL_SetTextureColorMod(sdl_texture, 255, 255, 255);
}

void GameState::render_region_fg(GridRegion& region, std::map<XYPos, int>& taken, std::map<XYPos, int>& total_taken)
{
    FOR_XY_SET(pos, region.elements)
    {
        unsigned size = taken_to_size(total_taken[pos]);
        XYPos n = pos * square_size + taken_to_pos(taken[pos], total_taken[pos]);
        taken[pos]++;

        set_region_colour(sdl_texture, region.type.value, region.colour, region.fade);
        SDL_Rect src_rect = {64, 512, 192, 192};
        SDL_Rect dst_rect = {(int)(grid_offset.x + n.x - square_size / (size * 2)), (int)(grid_offset.y + n.y - square_size / (size * 2)), (int)(square_size / size), (int)(square_size / size)};
        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);

        SDL_SetTextureColorMod(sdl_texture, 0,0,0);
        render_region_type(region.type, XYPos(grid_offset.x + n.x - square_size / (size * 2), grid_offset.y + n.y - square_size / (size * 2)), square_size / size);
    }

    SDL_SetTextureColorMod(sdl_texture, 255, 255, 255);

}

void GameState::render_tooltip()
{
    if (tooltip_string != "")
    {
        SDL_Color color = {0xFF, 0xFF, 0xFF};
        SDL_Surface* text_surface = TTF_RenderUTF8_Blended(font, tooltip_string.c_str(), color);
        SDL_Texture* new_texture = SDL_CreateTextureFromSurface(sdl_renderer, text_surface);
        SDL_Rect txt_rect;
        SDL_GetClipRect(text_surface, &txt_rect);

        SDL_Rect dst_rect;
        dst_rect.w = txt_rect.w * button_size / 64;
        dst_rect.h = txt_rect.h * button_size / 64;
        dst_rect.x = mouse.x - dst_rect.w - button_size / 4;
        dst_rect.y = mouse.y + button_size / 4;
        if (dst_rect.x < 0)
            dst_rect.x = 0;
        SDL_Rect bg_rect = {160, 608, 1, 1};

//        SDL_RenderCopy(sdl_renderer, sdl_texture, &bg_rect, &dst_rect);
        int border = button_size / 16;
        render_box(XYPos(dst_rect.x - border, dst_rect.y - border), XYPos(dst_rect.w + border * 2, dst_rect.h + border * 2), button_size / 4, 1);
        SDL_RenderCopy(sdl_renderer, new_texture, &txt_rect, &dst_rect);

        SDL_DestroyTexture(new_texture);
        SDL_FreeSurface(text_surface);
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
    else
    {
        assert(0);
    }


}

void GameState::render(bool saving)
{
    tooltip_string = "";
    XYPos window_size;
    bool row_col_clues = !grid->edges.empty();
    SDL_GetWindowSize(sdl_window, &window_size.x, &window_size.y);

    {
        if (window_size.x * 9 > window_size.y * 16)
        {
            grid_size = window_size.y;
        }
        else
        {
            grid_size = (window_size.x * 9) / 16;
        }

        panel_size = XYPos((grid_size * 7) / 18, grid_size);
        grid_offset = (window_size - XYPos(grid_size,grid_size)) / 2;
        left_panel_offset = grid_offset - XYPos(panel_size.x, 0);
        right_panel_offset = grid_offset + XYPos(grid_size, 0);

        square_size = grid_size / (std::max(grid->size.x,grid->size.y) + row_col_clues);
        if (row_col_clues)
        {
            grid_offset += XYPos(square_size,square_size);
        }
    }

    SDL_RenderClear(sdl_renderer);

    bool hover_rulemaker = false;
    XYSet hover_squares_highlight;
    int hover_rulemaker_bits = 0;
    bool hover_rulemaker_lower_right = false;


    button_size = (grid_size * 7) / (18 * 5);

    GridRegion* hover = NULL;

    if (right_panel_mode == RIGHT_MENU_RULE_GEN || right_panel_mode == RIGHT_MENU_RULE_INSPECT)
    {
        GridRegionCause rule_cause = (right_panel_mode == RIGHT_MENU_RULE_GEN) ? GridRegionCause(&constructed_rule, rule_gen_region[0], rule_gen_region[1], rule_gen_region[2], rule_gen_region[3]) : inspected_rule;

        if ((mouse - (right_panel_offset + XYPos(0, 0))).inside(XYPos(button_size, button_size)) && rule_cause.rule->region_count >= 1)
            hover = rule_cause.regions[0];
        if ((mouse - (right_panel_offset + XYPos(2 * button_size, 0))).inside(XYPos(button_size, button_size)) && rule_cause.rule->region_count >= 2)
            hover = rule_cause.regions[1];
        if ((mouse - (right_panel_offset + XYPos(4 * button_size, 2 * button_size))).inside(XYPos(button_size, button_size)) && rule_cause.rule->region_count >= 3)
            hover = rule_cause.regions[2];
        if ((mouse - (right_panel_offset + XYPos(4 * button_size, 4 * button_size))).inside(XYPos(button_size, button_size)) && rule_cause.rule->region_count >= 4)
            hover = rule_cause.regions[3];

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

    XYPos pos;
    for (pos.y = 0; pos.y < grid->size.y; pos.y++)
    for (pos.x = 0; pos.x < grid->size.x; pos.x++)
    {
        if (hover_rulemaker)
        {
            if (hover_squares_highlight.get(pos))
            {
                SDL_Rect src_rect = {793, 250, 1, 1};
                SDL_Rect dst_rect = {grid_offset.x + pos.x * square_size, grid_offset.y + pos.y * square_size, square_size, square_size};
                SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            }
        }

        {
            if (clue_solves.count(pos))
                SDL_SetTextureColorMod(sdl_texture, 0, 255, 0);
            if (filter_pos.count(pos))
                SDL_SetTextureColorMod(sdl_texture, 255,0, 0);
        }
        SDL_Rect src_rect = {64, 256, 192, 192};
        SDL_Rect dst_rect = {grid_offset.x + pos.x * square_size, grid_offset.y + pos.y * square_size, square_size, square_size};
        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
        SDL_SetTextureColorMod(sdl_texture, 255, 255, 255);
    }

    std::map<XYPos, int> total_taken;
    std::list<GridRegion*> display_regions;

    XYPos mouse_filter_pos(-1,-1);
    if ((mouse_mode == MOUSE_MODE_FILTER) && (mouse - grid_offset).inside(XYPos(grid_size,grid_size)))
    {
        XYPos pos = mouse - grid_offset;
        mouse_filter_pos = pos / square_size;
    }
    int region_vis_counts[3] = {0,0,0};
    {
        bool has_hover = false;
        for (GridRegion& region : grid->regions)
        {
            region_vis_counts[int(region.vis_level)]++;
            if (!filter_pos.empty() && !region.contains_all(filter_pos))
                continue;
            if (mouse_filter_pos.x >= 0 && !filter_pos.empty() && !region.elements.contains(mouse_filter_pos))
                continue;
            if (region.vis_level == vis_level)
            {
                display_regions.push_back(&region);
                if (&region == hover)
                    has_hover = true;
            }
        }
        if (hover && !has_hover)
            display_regions.push_back(hover);
    }

    for (GridRegion* region : display_regions)
    {
        FOR_XY_SET(pos, region->elements)
        {
            total_taken[pos]++;
        }
    }

    if ((mouse - grid_offset).inside(XYPos(grid_size,grid_size)))
    {
        XYPos pos = mouse - grid_offset;
        XYPos gpos = pos / square_size;
        XYPos ipos = pos - gpos * square_size;
        unsigned tot = total_taken[gpos];
        unsigned size = taken_to_size(tot);

        for (int i = 0; i < 100; i++)
        {
            XYPos spos = taken_to_pos(i, tot);
            if (XYPosFloat(ipos).distance(spos) < (double(square_size) / (size*2)))
            {
                int c = 0;
                for (GridRegion* region : display_regions)
                {
                    if (region->elements.get(gpos))
                    {
                        if (c == i)
                        {
                            hover = region;
                            break;
                        }
                        c++;
                    }
                }
                break;
            }
        }
    }

    for (GridRegion* region : display_regions)
    {
        if (!hover || (region == hover))
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

    for (pos.y = 0; pos.y < grid->size.y; pos.y++)
    for (pos.x = 0; pos.x < grid->size.x; pos.x++)
    {
        SDL_Rect src_rect = {64, 256, 192, 192};
        SDL_Rect dst_rect = {grid_offset.x + pos.x * square_size, grid_offset.y + pos.y * square_size, square_size, square_size};

        GridPlace place = grid->get(pos);
        if (place.revealed)
        {
            if (place.bomb)
            {
                SDL_Rect src_rect = {320, 192, 192, 192};
                SDL_Rect dst_rect = {grid_offset.x + pos.x * square_size, grid_offset.y + pos.y * square_size, square_size, square_size};
                SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            }
            else
            {
                render_region_type(place.clue, grid_offset + pos * square_size, square_size);
            }
        }
    }
    for (std::pair<const XYPos&, RegionType> p : grid->edges)
    {
        if (p.first.x == 0)
        {
            render_region_type(p.second, grid_offset + XYPos(-1, p.first.y) * square_size, square_size);
        }
        if (p.first.x == 1)
        {
            render_region_type(p.second, grid_offset + XYPos(p.first.y, -1) * square_size, square_size);
        }
    }

    {
        SDL_Rect src_rect = {704, 960, 192, 192};
        SDL_Rect dst_rect = {left_panel_offset.x + 0 * button_size, left_panel_offset.y + button_size * 0, button_size, button_size};
        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
        add_tooltip(dst_rect, "Hint");
    }
    {
        SDL_Rect src_rect = {704 + 192, 960, 192, 192};
        SDL_Rect dst_rect = {left_panel_offset.x + 1 * button_size, left_panel_offset.y + button_size * 0, button_size, button_size};
        if (auto_region)
            src_rect = {1280, 768, 192, 192};
        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
        add_tooltip(dst_rect, "Auto Region");
    }
    {
        SDL_Rect src_rect = {704 + 192 * 2, 960, 192, 192};
        SDL_Rect dst_rect = {left_panel_offset.x + 2 * button_size, left_panel_offset.y + button_size * 0, button_size, button_size};
        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
        add_tooltip(dst_rect, "Refresh Regions");
    }
    {
        SDL_Rect src_rect = {704 + 192 * 3, 960, 192, 192};
        SDL_Rect dst_rect = {left_panel_offset.x + 3 * button_size, left_panel_offset.y + button_size * 0, button_size, button_size};
        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
        add_tooltip(dst_rect, "Skip Level");
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
        int s = filter_pos.size();
        if (s)
        {
            render_number(s, left_panel_offset + XYPos(button_size * 4 + button_size / 4, button_size * 1 + button_size / 6), XYPos(button_size/2, button_size/4));

        }
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

        render_number(level_progress[i].counts[0], pos + XYPos(0, button_size / 12), XYPos(button_size, button_size / 3));
//        render_number(level_progress[i].counts[1], pos + XYPos(button_size /2, button_size / 12), XYPos(button_size /2, button_size / 3));
//        render_number(level_progress[i].counts[2], pos + XYPos(0, button_size / 2 + button_size / 12), XYPos(button_size /2, button_size / 3));
        render_number(level_progress[i].counts[3], pos + XYPos(0, button_size / 2 + button_size / 12), XYPos(button_size, button_size / 3));

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

        RegionType r_type = menu_region_types1[region_menu];
        if (r_type.type != RegionType::NONE)
            FOR_XY(pos, XYPos(), XYPos(5, 2))
            {
                if ((mouse_mode == MOUSE_MODE_DRAWING_REGION) && (region_type == r_type))
                    render_box(right_panel_offset+ pos * button_size + XYPos(0, button_size * 8), XYPos(button_size, button_size), button_size/4);
                render_region_type(r_type, right_panel_offset + pos * button_size + XYPos(0, button_size * 8), button_size);
                r_type.value ++;
            }

        r_type = menu_region_types2[region_menu];
        if (r_type.type != RegionType::NONE)
            FOR_XY(pos, XYPos(), XYPos(5, 2))
            {
                if ((mouse_mode == MOUSE_MODE_DRAWING_REGION) && (region_type == r_type))
                    render_box(right_panel_offset+ pos * button_size + XYPos(0, button_size * 10 + button_size/2), XYPos(button_size, button_size), button_size/4);
                render_region_type(r_type, right_panel_offset + pos * button_size + XYPos(0, button_size * 10 + button_size/2), button_size);
                r_type.value ++;
            }

        {
            if (mouse_mode == MOUSE_MODE_CLEAR)
                render_box(right_panel_offset + XYPos(button_size * 1, button_size * 6), XYPos(button_size, button_size), button_size/4);
            SDL_Rect src_rect = {512, 192, 192, 192};
            SDL_Rect dst_rect = {right_panel_offset.x + button_size * 1, right_panel_offset.y + button_size * 6, button_size, button_size};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            add_tooltip(dst_rect, "Clear");

            if (mouse_mode == MOUSE_MODE_BOMB)
                render_box(right_panel_offset + XYPos(button_size * 2, button_size * 6), XYPos(button_size, button_size), button_size/4);
            src_rect = {320, 192, 192, 192};
            dst_rect = {right_panel_offset.x + button_size * 2, right_panel_offset.y + button_size * 6, button_size, button_size};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            add_tooltip(dst_rect, "Bomb");
        }

        {
            if (mouse_mode == MOUSE_MODE_NONE)
                render_box(right_panel_offset + XYPos(0, button_size * 6), XYPos(button_size, button_size), button_size/4);
            SDL_Rect src_rect = {896, 192, 192, 192};
            SDL_Rect dst_rect = {right_panel_offset.x, right_panel_offset.y + button_size * 6, button_size, button_size};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            add_tooltip(dst_rect, "Don't Care");
        }

        {
            if (mouse_mode == MOUSE_MODE_VIS)
                render_box(right_panel_offset + XYPos(button_size * 3, button_size * 6), XYPos(button_size, button_size), button_size/4);
            SDL_Rect src_rect = {vis_mode == GRID_VIS_LEVEL_SHOW ? 1088 : 896, 384, 192, 192};
            if (vis_mode == GRID_VIS_LEVEL_BIN)
            {
                src_rect.x = 512;
                src_rect.y = 768;
            }

            SDL_Rect dst_rect = {right_panel_offset.x + 3 * button_size, right_panel_offset.y + button_size * 6, button_size, button_size};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            if (vis_mode == GRID_VIS_LEVEL_SHOW)
                add_tooltip(dst_rect, "Move to Visible");
            if (vis_mode == GRID_VIS_LEVEL_HIDE)
                add_tooltip(dst_rect, "Move to Hidden");
            if (vis_mode == GRID_VIS_LEVEL_BIN)
                add_tooltip(dst_rect, "Move to Trash");
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
            unsigned colour = (right_panel_mode == RIGHT_MENU_RULE_GEN) ? rule_gen_region[0]->colour : 0;

            set_region_colour(sdl_texture, rule.region_type[0].value, colour, 255);
            render_box(right_panel_offset + XYPos(0 * button_size, 0 * button_size), XYPos(2 * button_size, 5 * button_size), button_size / 2);
            render_region_bubble(rule.region_type[0], colour, right_panel_offset + XYPos(0 * button_size, 0 * button_size), button_size * 2 / 3);

        }
        if (rule.region_count >= 2)
        {
            unsigned colour = (right_panel_mode == RIGHT_MENU_RULE_GEN) ? rule_gen_region[1]->colour : 0;
            set_region_colour(sdl_texture, rule.region_type[1].value, colour, 255);
            render_box(right_panel_offset + XYPos(1 * button_size, button_size / 12), XYPos(2 * button_size, 5 * button_size), button_size / 2);
            render_region_bubble(rule.region_type[1], colour, right_panel_offset + XYPos(2 * button_size + button_size / 3, button_size / 12), button_size * 2 / 3);

        }

        if (rule.region_count >= 3)
        {
            unsigned colour = (right_panel_mode == RIGHT_MENU_RULE_GEN) ? rule_gen_region[2]->colour : 0;
            set_region_colour(sdl_texture, rule.region_type[2].value, colour, 255);
            render_box(right_panel_offset + XYPos(0 * button_size, 2 * button_size), XYPos(5 * button_size, 2 * button_size), button_size / 2);
            render_region_bubble(rule.region_type[2], colour, right_panel_offset + XYPos(4 * button_size + button_size / 3, 2 * button_size), button_size * 2 / 3);
        }

        if (rule.region_count >= 4)
        {
            unsigned colour = (right_panel_mode == RIGHT_MENU_RULE_GEN) ? rule_gen_region[3]->colour : 0;
            set_region_colour(sdl_texture, rule.region_type[3].value, colour, 255);
            render_box(right_panel_offset + XYPos(button_size / 12, 3 * button_size), XYPos(5 * button_size, 2 * button_size), button_size / 2);
            render_region_bubble(rule.region_type[3], colour, right_panel_offset + XYPos(button_size / 12 + 4 * button_size + button_size / 3, 4 * button_size + button_size / 3), button_size * 2 / 3);
        }

        if (rule.apply_type == GridRule::SHOW || rule.apply_type == GridRule::HIDE || rule.apply_type == GridRule::BIN)
        {
            SDL_SetTextureColorMod(sdl_texture, 255, 255, 255);
            SDL_Rect src_rect = {(rule.apply_type == GridRule::SHOW) ? 1088 : 896, 384, 192, 192};
            if (rule.apply_type == GridRule::BIN)
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
            // if (right_panel_mode == RIGHT_MENU_RULE_GEN)
            // {
            //     if (rule_gen_region_undef_num & (1 << i)) r_type = RegionType(RegionType::NONE, 0);
            // }
            // else
            // {
            //     if (rule.square_counts[i] < 0) r_type = RegionType(RegionType::NONE, 0);
            // }

            // bool hover_rulemaker_lower_right = false;

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
                if (rule.apply_type == GridRule::REGION)
                {
                    render_region_bubble(rule.apply_region_type, 0, right_panel_offset + XYPos(button_size / 2, button_size / 2) + p * button_size, button_size / 2);
                }
                else if (rule.apply_type == GridRule::BOMB || rule.apply_type == GridRule::CLEAR)
                {
                    SDL_Rect src_rect = {(rule.apply_type == GridRule::BOMB) ? 320 : 320 + 192, 192, 192, 192};
                    SDL_Rect dst_rect = {right_panel_offset.x + button_size / 2 + button_size * p.x, right_panel_offset.y + p.y * button_size + button_size / 2, button_size / 2, button_size / 2};
                    SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
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
                if (mouse_mode != MOUSE_MODE_DRAWING_REGION)
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

                if (mouse_mode != MOUSE_MODE_DRAWING_REGION)
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
                if (mouse_mode == MOUSE_MODE_DRAWING_REGION)
                {
                    render_region_bubble(region_type, 0, p, button_size);
                }
                else if (mouse_mode == MOUSE_MODE_BOMB || mouse_mode == MOUSE_MODE_CLEAR)
                {
                    SDL_Rect src_rect = {(mouse_mode == MOUSE_MODE_BOMB) ? 320 : 320 + 192, 192, 192, 192};
                    SDL_Rect dst_rect = {p.x, p.y, button_size, button_size};
                    SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
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
                    add_tooltip(dst_rect, "Rule already present");
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
    render_tooltip();
    SDL_RenderPresent(sdl_renderer);
}

void GameState::grid_click(XYPos pos, bool right)
{
    if (right)
    {
        reset_rule_gen_region();
        return;
    }

    pos /= square_size;
//     if ((mouse_mode == MOUSE_MODE_BOMB) || (mouse_mode == MOUSE_MODE_CLEAR))
//     {
//         current_level_manual = true;
//         GridPlace place = grid->get(pos);
//         if (place.revealed)
//             return;
//         if ((mouse_mode == MOUSE_MODE_BOMB) != place.bomb)
//         {
// //            deaths++;
//             return;
//         }
//         if (grid->is_determinable(pos))
//         {
//             grid->reveal(pos);
//             reset_rule_gen_region();
//         }
// //        else
// //            deaths++;
//
//     }
    if (mouse_mode == MOUSE_MODE_FILTER)
    {
        if (filter_pos.count(pos))
            filter_pos.erase(pos);
        else
            filter_pos.insert(pos);
    }
    else    // if((mouse_mode == MOUSE_MODE_SELECT) || (mouse_mode == MOUSE_MODE_VIS))
    {
        GridRegion* hover = NULL;
        if ((mouse - grid_offset).inside(XYPos(grid_size,grid_size)))
        {
            XYPos pos = mouse - grid_offset;
            XYPos gpos = pos / square_size;
            XYPos ipos = pos - gpos * square_size;

            unsigned tot = 0;
            std::list<GridRegion*> display_regions;

            for (GridRegion& region : grid->regions)
            {
                if (!filter_pos.empty() && !region.contains_all(filter_pos))
                    continue;
                if (region.vis_level == vis_level)
                    display_regions.push_back(&region);
            }


            unsigned size = taken_to_size(tot);

            for (int i = 0; i < 1000; i++)
            {
                XYPos spos = taken_to_pos(i, tot);
                if (XYPosFloat(ipos).distance(spos) < (double(square_size) / (size * 2)))
                {
                    int c = 0;
                    for (GridRegion* region : display_regions)
                    {
                        if (region->elements.get(gpos))
                        {
                            if (c == i)
                            {
                                hover = region;
                                break;
                            }
                            c++;
                        }
                    }
                    break;
                }
            }
        }
        if (hover)
        {
            {
                if (inspected_region == hover && right_panel_mode == RIGHT_MENU_REGION)
                {
                    right_panel_mode = RIGHT_MENU_RULE_GEN;
                    if (constructed_rule.region_count < 4)
                    {
                        bool found = false;
                        for (int i = 0; i < constructed_rule.region_count; i++)
                        {
                            if (rule_gen_region[i] == hover)
                                found = true;
                        }
                        if (!found)
                            rule_gen_region[constructed_rule.region_count] = hover;
                        constructed_rule.import_rule_gen_regions(rule_gen_region[0], rule_gen_region[1], rule_gen_region[2], rule_gen_region[3]);
                        update_constructed_rule();
                    }
                }
                else
                {
                    right_panel_mode = RIGHT_MENU_REGION;
                    inspected_region = hover;
                }

                // if (rule_gen_region_count < 3)
                // {
                //     rule_gen_region[rule_gen_region_count] = hover;
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
            mouse_mode = MOUSE_MODE_BOMB;
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
    {
        current_level_hinted = true;
        clue_solves.clear();
        FOR_XY(pos, XYPos(), grid->size)
        {
            if (grid->is_determinable_using_regions(pos, true))
                clue_solves.insert(pos);
        }
        get_hint = true;

    }
    if ((pos - XYPos(button_size * 1, button_size * 0)).inside(XYPos(button_size,button_size)))
        auto_region = !auto_region;
    if ((pos - XYPos(button_size * 2, button_size * 0)).inside(XYPos(button_size,button_size)))
    {
        clue_solves.clear();
        grid->regions.clear();
        reset_rule_gen_region();
    }
    if ((pos - XYPos(button_size * 3, button_size * 0)).inside(XYPos(button_size,button_size)))
        skip_level = true;

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
            inspected_region->vis_level = GRID_VIS_LEVEL_SHOW;
        if ((pos - XYPos(button_size * 3, button_size * 3)).inside(XYPos(button_size, button_size)))
            inspected_region->vis_level = GRID_VIS_LEVEL_HIDE;
        if ((pos - XYPos(button_size * 3, button_size * 4)).inside(XYPos(button_size, button_size)))
            inspected_region->vis_level = GRID_VIS_LEVEL_BIN;

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
            if ((right_panel_mode == RIGHT_MENU_RULE_GEN) && (mouse_mode == MOUSE_MODE_VIS))
            {
                constructed_rule.apply_region_bitmap ^= 1 << region_index;
                if (vis_mode == GRID_VIS_LEVEL_SHOW)
                    constructed_rule.apply_type = GridRule::SHOW;
                else if (vis_mode == GRID_VIS_LEVEL_HIDE)
                    constructed_rule.apply_type = GridRule::HIDE;
                else
                    constructed_rule.apply_type = GridRule::BIN;
                update_constructed_rule();
            }
            else
            {
                printf("test\n");
                right_panel_mode = RIGHT_MENU_REGION;
                inspected_region = rule_cause.regions[region_index];
            }
        }
    }

    if (right_panel_mode == RIGHT_MENU_RULE_GEN)
    {
        if ((pos - XYPos(button_size * 1, button_size * 6)).inside(XYPos(button_size,button_size)))
                mouse_mode = MOUSE_MODE_CLEAR;
        if ((pos - XYPos(button_size * 2, button_size * 6)).inside(XYPos(button_size,button_size)))
                mouse_mode = MOUSE_MODE_BOMB;
        if ((pos - XYPos(button_size * 0, button_size * 6)).inside(XYPos(button_size,button_size)))
        {
            mouse_mode = MOUSE_MODE_NONE;
        }
        if ((pos - XYPos(button_size * 3, button_size * 6)).inside(XYPos(button_size,button_size)))
        {
            if (mouse_mode == MOUSE_MODE_VIS)
            {
                if (vis_mode == GRID_VIS_LEVEL_SHOW)  vis_mode = GRID_VIS_LEVEL_HIDE;
                else if (vis_mode == GRID_VIS_LEVEL_HIDE)  vis_mode = GRID_VIS_LEVEL_BIN;
                else vis_mode = GRID_VIS_LEVEL_SHOW;

            }
            mouse_mode = MOUSE_MODE_VIS;
        }

        if ((pos - XYPos(button_size * 0, button_size * 7)).inside(XYPos(5 * button_size,1 * button_size)))
        {
            region_menu = pos.x / button_size;
        }

        if ((pos - XYPos(button_size * 0, button_size * 8)).inside(XYPos(5 * button_size,2 * button_size)))
        {
            mouse_mode = MOUSE_MODE_DRAWING_REGION;
            region_item_selected = (pos - XYPos(0, button_size * 8)) / button_size;
        }

        if ((pos - XYPos(button_size * 0, button_size * 10 + button_size / 2)).inside(XYPos(5 * button_size, 2 * button_size)))
        {
            mouse_mode = MOUSE_MODE_DRAWING_REGION;
            region_item_selected = (pos - XYPos(0, button_size * 10 + button_size / 2)) / button_size + XYPos(0, 2);
        }

        if (region_item_selected.y < 2)
        {
            region_type = menu_region_types1[region_menu];
            region_type.value += region_item_selected.x + region_item_selected.y * 5;
        }
        else
        {
            region_type = menu_region_types2[region_menu];
            region_type.value += region_item_selected.x + (region_item_selected.y - 2) * 5;
        }
        if (region_type.type == RegionType::NONE)
            mouse_mode = MOUSE_MODE_NONE;

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
                    if (mouse_mode != MOUSE_MODE_DRAWING_REGION)
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
                else if ((mouse_mode == MOUSE_MODE_BOMB) || (mouse_mode == MOUSE_MODE_CLEAR) || (mouse_mode == MOUSE_MODE_DRAWING_REGION))
                {
                    constructed_rule.apply_region_bitmap ^= 1 << hover_rulemaker_bits;
                    if (mouse_mode == MOUSE_MODE_DRAWING_REGION)
                    {
                        if (constructed_rule.apply_type != GridRule::REGION)
                            constructed_rule.apply_region_bitmap = 1 << hover_rulemaker_bits;
                        if (constructed_rule.apply_region_type != region_type)
                            constructed_rule.apply_region_bitmap = 1 << hover_rulemaker_bits;
                        constructed_rule.apply_type = GridRule::REGION;
                        constructed_rule.apply_region_type = region_type;
                    }
                    else
                    {
                        GridRule::ApplyType t = (mouse_mode == MOUSE_MODE_BOMB) ? GridRule::BOMB : GridRule::CLEAR;
                        if (constructed_rule.apply_type != t)
                        {
                            constructed_rule.apply_type = t;
                            constructed_rule.apply_region_bitmap = 1 << hover_rulemaker_bits;
                        }
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
                        grid = new Grid(s);
                        SDL_free(s);
                        break;
                    }
                    case SDL_SCANCODE_ESCAPE:
		                    quit = true;
                        break;
                    case SDL_SCANCODE_F1:
                    {
                        current_level_hinted = true;
                        clue_solves.clear();
                        //grid->find_easiest_move(clue_solves, clue_needs);
                        //grid->find_easiest_move_using_regions(clue_solves);
                        FOR_XY(pos, XYPos(), grid->size)
                        {
                            if (grid->is_determinable_using_regions(pos, true))
                                clue_solves.insert(pos);
                        }
                        get_hint = true;
                        break;
                    }
                    case SDL_SCANCODE_F2:
                    {
                        auto_region = !auto_region;
                        break;
                    }
                    case SDL_SCANCODE_F3:
                    {
                        clue_solves.clear();
                        grid->regions.clear();
                        reset_rule_gen_region();
                        get_hint = false;
                        break;
                    }
                    case SDL_SCANCODE_F4:
                    {
                        skip_level = true;
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

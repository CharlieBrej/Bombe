#define _USE_MATH_DEFINES
#include <cmath>

#include "GameState.h"
#include "SaveState.h"
#include "Misc.h"

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

static int BoardGeneratorThread(void *ptr)
{
    GameState* state = (GameState*)ptr;

    for (int i = 5; i < 20; i++)
    for (int j = 0; j < 5; j++)
    {
        Grid* grid = new  Grid(XYPos(i,i));
        grid->make_harder();
        SDL_LockMutex(state->levels_mutex);
        state->levels.push_back(grid);
        SDL_UnlockMutex(state->levels_mutex);
    }


    return 0;
}


GameState::GameState(std::ifstream& loadfile)
{
    levels_mutex = SDL_CreateMutex();
    for (int i = 0; i < SDL_GetCPUCount(); i++)
        SDL_CreateThread(BoardGeneratorThread, "BoardGeneratorThread", (void *)this);
    bool load_was_good = false;
    try
    {
        if (!loadfile.fail() && !loadfile.eof())
        {
            SaveObjectMap* omap;
            omap = SaveObject::load(loadfile)->get_map();
            if (omap->get_num("version") < 1)
                throw(std::runtime_error("Bad Version"));


            SaveObjectList* rlist = omap->get_item("rules")->get_list();

            for (int i = 0; i < rlist->get_count(); i++)
            {
                GridRule r(rlist->get_item(i));
//                if (r.is_legal())
                rules.push_back(r);

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

    sdl_window = SDL_CreateWindow( "Bombe", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1920/2, 1080/2, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | (full_screen? SDL_WINDOW_FULLSCREEN_DESKTOP  | SDL_WINDOW_BORDERLESS : 0));
    sdl_renderer = SDL_CreateRenderer(sdl_window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE |SDL_RENDERER_PRESENTVSYNC);
    SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 0);
	sdl_texture = loadTexture("texture.png");

    {
        SDL_Surface* icon_surface = IMG_Load("icon.png");
        SDL_SetWindowIcon(sdl_window, icon_surface);
	    SDL_FreeSurface(icon_surface);
    }

    font = TTF_OpenFont("fixed.ttf", 19);

    Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048);
    Mix_AllocateChannels(32);

//    Mix_VolumeMusic(music_volume);
//    music = Mix_LoadMUS("music.ogg");
//    Mix_PlayMusic(music, -1);
    grid = new  Grid(XYPos(3,3));
    grid->print();

    grid->make_harder();

    grid->print();

//     {
//         Grid t = *grid;
//         t.solve(1000000);
//     }

}

SaveObject* GameState::save(bool lite)
{
    SaveObjectMap* omap = new SaveObjectMap;
    omap->add_num("version", 1);

    SaveObjectList* rlist = new SaveObjectList;

    for (GridRule& rule : rules)
    {
        rlist->add_item(rule.save());
    }
    omap->add_item("rules", rlist);

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

void GameState::advance()
{
    if ((grid->is_solved() || skip_level) && !levels.empty())
    {
        delete grid;
        SDL_LockMutex(levels_mutex);
        grid = levels.front();
        levels.pop_front();
        SDL_UnlockMutex(levels_mutex);
        skip_level = false;
        reset_rule_gen_region();
    }

    if (auto_region)
    {
        if (cooldown)
        {
            cooldown--;
        }
        else
        {
            bool hit = false;
            while (grid->add_regions(-1)) {}

            grid->add_one_new_region();
            for (std::list<GridRule>::iterator it=rules.begin(); it != rules.end(); ++it)
            {
                GridRule& rule = *it;
//                if (hit) break;
                if ((rule.apply_type == GridRule::HIDE) || (rule.apply_type == GridRule::SHOW))
                    continue;

                Grid::ApplyRuleResp resp  = grid->apply_rule(rule);
                if (resp == Grid::APPLY_RULE_RESP_HIT)
                {
                    hit = true;
                    show_clue = false;
//                    break;
                }
                if (resp == Grid::APPLY_RULE_RESP_ERROR)
                {
//                    rules.erase(it);
                    deaths++;
                    break;
                }
                if (resp == Grid::APPLY_RULE_RESP_NONE)
                {
                    rule.stale = true;
                }
            }
            for (std::list<GridRule>::iterator it=rules.begin(); it != rules.end(); ++it)
            {
                GridRule& rule = *it;
                if ((rule.apply_type == GridRule::HIDE) || (rule.apply_type == GridRule::SHOW))
                {
                    grid->apply_rule(rule);
                    rule.stale = true;
                }

            }
            if (hit)
            {
                cooldown = 1;
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
    rule_gen_region_count = 0;
    rule_gen_region_undef_num = 0;
    constructed_rule.apply_region_bitmap = 0;
    update_constructed_rule();
    right_panel_mode = RIGHT_MENU_NONE;
}
void GameState::update_constructed_rule()
{
    constructed_rule_is_logical = false;
    if (!rule_gen_region_count)
        return;

    constructed_rule.import_rule_gen_regions(rule_gen_region[0], rule_gen_region[1], rule_gen_region[2]);
    for (int i = 0; i < 8; i++)
    {
        if ((rule_gen_region_undef_num >> i) & 1)
        {
            constructed_rule.square_counts[i] = -1;
        }
    }
    if (constructed_rule.is_legal())
    {
        constructed_rule_is_logical = true;
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

    for (XYPos pos : region.elements)
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
    for (XYPos pos : region.elements)
    {
        unsigned size = taken_to_size(total_taken[pos]);
        XYPos n = pos * square_size + taken_to_pos(taken[pos], total_taken[pos]);
        taken[pos]++;

        set_region_colour(sdl_texture, region.type.value, region.colour, region.fade);
        SDL_Rect src_rect = {64, 512, 192, 192};
        SDL_Rect dst_rect = {grid_offset.x + n.x - square_size / (size * 2), grid_offset.y + n.y - square_size / (size * 2), square_size / size, square_size / size};
        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);

        SDL_SetTextureColorMod(sdl_texture, 0,0,0);
        render_region_type(region.type, XYPos(grid_offset.x + n.x - square_size / (size * 2), grid_offset.y + n.y - square_size / (size * 2)), square_size / size);
    }

    SDL_SetTextureColorMod(sdl_texture, 255, 255, 255);

}

void GameState::render_box(XYPos pos, XYPos size)
{
        SDL_Rect src_rect = {64, 256, 192, 192};
        SDL_Rect dst_rect = {pos.x, pos.y, size.x, size.y};
        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
}
void GameState::render_region_type(RegionType reg, XYPos pos, unsigned siz)
{
    if (reg.type == RegionType::MORE || reg.type == RegionType::LESS)
    {
        int border = siz / 8;
        SDL_Rect src_rect = {32 + reg.value * 192, 0, 128, 192};
        SDL_Rect dst_rect = {pos.x + border, pos.y + border * 2, (siz - border * 4) * 3 / 4, siz - border * 4};
        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);

        src_rect = {reg.type == RegionType::MORE ? 416 : 288 , 512, 128, 192};
        dst_rect.x += border * 3;
        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
    }
    else
    {
        int border = siz / 8;
        SDL_Rect src_rect = {32 + reg.value * 192, 0, 128, 192};
        SDL_Rect dst_rect = {pos.x + border * 2, pos.y + border, (siz - border * 2) * 3 / 4, siz - border * 2};

        if (reg.type == RegionType::NONE)
        {
            src_rect.y = 192;
            src_rect.x = 928;
        }

        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
    }


}

void GameState::render(bool saving)
{
    XYPos window_size;
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

        square_size = grid_size / std::max(grid->size.x,grid->size.y);
    }

    SDL_RenderClear(sdl_renderer);

    bool hover_rulemaker = false;
    unsigned hover_rulemaker_bits = 0;

    int button_size = (grid_size * 7) / (18 * 4);

    GridRegion* hover = NULL;

    if (right_panel_mode == RIGHT_MENU_RULE_GEN)
    {


        if ((mouse - (right_panel_offset + XYPos(button_size / 2, 0))).inside(XYPos(button_size, button_size)) && rule_gen_region_count >= 1)
            hover = rule_gen_region[0];
        if ((mouse - (right_panel_offset + XYPos(button_size / 2 + button_size * 2, button_size))).inside(XYPos(button_size, button_size)) && rule_gen_region_count >= 2)
            hover = rule_gen_region[1];
        if ((mouse - (right_panel_offset + XYPos(button_size / 2, button_size * 4))).inside(XYPos(button_size, button_size)) && rule_gen_region_count >= 2)
            hover = rule_gen_region[2];

        if ((mouse - (right_panel_offset + XYPos(button_size / 2, button_size))).inside(XYPos(button_size*3, button_size * 5)))
        {
            XYPos pos = mouse - (right_panel_offset + XYPos(button_size / 2, button_size));
            XYPos gpos = pos / button_size;
            if (gpos == XYPos(0,0) && rule_gen_region_count >= 1)
            {
                hover_rulemaker = true;
                hover_rulemaker_bits = 1;
            }
            if (gpos == XYPos(1,0) && rule_gen_region_count >= 2)
            {
                hover_rulemaker = true;
                hover_rulemaker_bits = 3;
            }
            if (gpos == XYPos(2,1) && rule_gen_region_count >= 2)
            {
                hover_rulemaker = true;
                hover_rulemaker_bits = 2;
            }

            if (gpos == XYPos(0,1) && rule_gen_region_count >= 3)
            {
                hover_rulemaker = true;
                hover_rulemaker_bits = 5;
            }
            if (gpos == XYPos(1,1) && rule_gen_region_count >= 3)
            {
                hover_rulemaker = true;
                hover_rulemaker_bits = 7;
            }
            if (gpos == XYPos(0,2) && rule_gen_region_count >= 3)
            {
                hover_rulemaker = true;
                hover_rulemaker_bits = 4;
            }
            if (gpos == XYPos(1,2) && rule_gen_region_count >= 3)
            {
                hover_rulemaker = true;
                hover_rulemaker_bits = 6;
            }

            if (hover_rulemaker)
            {
                SDL_Rect src_rect = {793, 250, 1, 1};
                SDL_Rect dst_rect = {right_panel_offset.x + button_size / 2 + gpos.x * button_size, right_panel_offset.y + button_size + gpos.y * button_size, button_size, button_size};
                SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            }
        }
    }

    XYPos pos;
    for (pos.y = 0; pos.y < grid->size.y; pos.y++)
    for (pos.x = 0; pos.x < grid->size.x; pos.x++)
    {
        if (hover_rulemaker)
        {
            bool show = true;
            {
                if (rule_gen_region_count >= 1)
                {
                    bool b = (hover_rulemaker_bits & 1);
                    if (rule_gen_region[0]->elements.count(pos) != b)
                        show = false;
                }
                if (rule_gen_region_count >= 2)
                {
                    bool b = (hover_rulemaker_bits & 2);
                    if (rule_gen_region[1]->elements.count(pos) != b)
                        show = false;
                }
                if (rule_gen_region_count >= 3)
                {
                    bool b = (hover_rulemaker_bits & 4);
                    if (rule_gen_region[2]->elements.count(pos) != b)
                        show = false;
                }
            }
            if (show)
            {
                SDL_Rect src_rect = {793, 250, 1, 1};
                SDL_Rect dst_rect = {grid_offset.x + pos.x * square_size, grid_offset.y + pos.y * square_size, square_size, square_size};
                SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            }
        }

        if (show_clue)
        {
//            if ((clue_needs.get(pos).revealed) && (clue_needs.get(pos).revealed) && (!clue_needs.get(pos).bomb)&& (clue_needs.get(pos).clue.type != RegionType::NONE))
//                SDL_SetTextureColorMod(sdl_texture, 255, 0, 0);
            if (clue_solves.count(pos))
                SDL_SetTextureColorMod(sdl_texture, 0, 255, 0);
        }
        SDL_Rect src_rect = {64, 256, 192, 192};
        SDL_Rect dst_rect = {grid_offset.x + pos.x * square_size, grid_offset.y + pos.y * square_size, square_size, square_size};
        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
        SDL_SetTextureColorMod(sdl_texture, 255, 255, 255);
    }

    std::map<XYPos, int> total_taken;
    for (GridRegion& region : grid->regions)
    {
        for (XYPos pos : region.elements)
        {
            if (region.hidden == (mouse_mode == MOUSE_MODE_SHOW))
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
                for (GridRegion& region : grid->regions)
                {
                    if (region.hidden != (mouse_mode == MOUSE_MODE_SHOW))
                        continue;
                    if (region.elements.count(gpos))
                    {
                        if (c == i)
                        {
                            hover = &region;
                            break;
                        }
                        c++;
                    }
                }
                break;
            }
        }
    }

    for (GridRegion& region : grid->regions)
    {
        if (!hover || (&region == hover))
        {
            if (region.fade < 255)
            {
                region.fade += std::min(10, 255 - int(region.fade));
            }
        }
        else
        {
            if (region.fade)
            {
                region.fade -= std::min(10, int(region.fade - 100));
            }
        }
    }

    {
        std::map<XYPos, int> taken;

        for (GridRegion& region : grid->regions)
        {
            if (region.hidden == (mouse_mode == MOUSE_MODE_SHOW))
                render_region_bg(region, taken, total_taken);
        }
        edited_region.fade = 255;
        render_region_bg(edited_region, taken, total_taken);
    }
    {
        std::map<XYPos, int> taken;
        for (GridRegion& region : grid->regions)
        {
            if (region.hidden == (mouse_mode == MOUSE_MODE_SHOW))
                render_region_fg(region, taken, total_taken);
        }
        render_region_fg(edited_region, taken, total_taken);
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

    {
        int bombs_hidden = 0;
        int bombs_total = 0;
        FOR_XY(pos, XYPos(), grid->size)
        {
            GridPlace place = grid->get(pos);
            if (place.bomb)
            {
                bombs_total++;
                if (!place.revealed)
                    bombs_hidden++;
            }
        }

        const int trim = 27;
        int digit_height = button_size * 0.75;
        int digit_width = (digit_height * (192 - trim * 2)) / 192;

        // if (show_clue && clue_needs.count_revealed)
        // {
        //     SDL_SetTextureColorMod(sdl_texture, 255, 0, 0);
        //     render_box(left_panel_offset + XYPos(0, 0), XYPos(digit_height + digit_width * 5, digit_height));
        //     SDL_SetTextureColorMod(sdl_texture, 255, 255, 255);
        // }

        {
            SDL_Rect src_rect = {320, 192, 192, 192};
            SDL_Rect dst_rect = {left_panel_offset.x, left_panel_offset.y, digit_height, digit_height};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
        }

        SDL_Rect src_rect = {0, 0, 192 - (trim * 2), 192};
        SDL_Rect dst_rect = {left_panel_offset.x + digit_height, left_panel_offset.y, digit_width, digit_height};
        src_rect.x = (bombs_hidden / 10) * 192 + trim;
        if (bombs_hidden / 10)
        {
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            dst_rect.x += digit_width;
        }
        src_rect.x = (bombs_hidden % 10) * 192 + trim;
        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
        dst_rect.x += digit_width;
        {
            SDL_Rect src_rect = {1400, 30, 100, 150};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
        }
        dst_rect.x += digit_width;
        src_rect.x = (bombs_total / 10) * 192 + trim;
        if (bombs_total / 10)
        {
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            dst_rect.x += digit_width;
        }
        src_rect.x = (bombs_total % 10) * 192 + trim;
        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
    }

    {
//        if (edited_region.type.type == RegionType::EQUAL)
//            render_box(left_panel_offset+ XYPos(0 * button_size, 1 * button_size), XYPos(button_size, button_size));
        SDL_Rect src_rect = {544, 544, 128, 128};
        SDL_Rect dst_rect = {right_panel_offset.x + 0 * button_size, right_panel_offset.y + button_size * 7, button_size, button_size};
        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
    }
    {
//        if (edited_region.type.type == RegionType::MORE)
//            render_box(left_panel_offset+ XYPos(1 * button_size, 1 * button_size), XYPos(button_size, button_size));
        SDL_Rect src_rect = {416, 544, 128, 128};
        SDL_Rect dst_rect = {right_panel_offset.x + 1 * button_size, right_panel_offset.y + button_size * 7, button_size, button_size};
        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
    }
    {
//        if (edited_region.type.type == RegionType::LESS)
//            render_box(left_panel_offset+ XYPos(2 * button_size, 1 * button_size), XYPos(button_size, button_size));
        SDL_Rect src_rect = {288, 544, 128, 128};
        SDL_Rect dst_rect = {right_panel_offset.x + 2 * button_size, right_panel_offset.y + button_size * 7, button_size, button_size};
        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
    }

    for (int i = 0; i < 4; i++)
    {
        if ((mouse_mode == MOUSE_MODE_DRAWING_REGION) && (edited_region.type.value == (i + 1)))
            render_box(right_panel_offset+ XYPos(i * button_size, 8 * button_size), XYPos(button_size, button_size));
        render_region_type(RegionType(edited_region.type.type, i + 1), XYPos(right_panel_offset.x + i * button_size, right_panel_offset.y + button_size * 8), button_size);
    }

    for (int i = 0; i < 4; i++)
    {
        if ((mouse_mode == MOUSE_MODE_DRAWING_REGION) && (edited_region.type.value == (i + 5)))
            render_box(right_panel_offset + XYPos(i * button_size, 9 * button_size), XYPos(button_size, button_size));
        render_region_type(RegionType(edited_region.type.type, i + 5), XYPos(right_panel_offset.x + i * button_size, right_panel_offset.y + button_size * 9), button_size);
    }

    {
        if (mouse_mode == MOUSE_MODE_CLEAR)
            render_box(right_panel_offset + XYPos(button_size * 1, button_size * 6), XYPos(button_size, button_size));
        if (mouse_mode == MOUSE_MODE_BOMB)
            render_box(right_panel_offset + XYPos(button_size * 2, button_size * 6), XYPos(button_size, button_size));
        SDL_Rect src_rect = {512, 192, 192, 192};
        SDL_Rect dst_rect = {right_panel_offset.x + button_size * 1, right_panel_offset.y + button_size * 6, button_size, button_size};
        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
        src_rect = {320, 192, 192, 192};
        dst_rect = {right_panel_offset.x + button_size * 2, right_panel_offset.y + button_size * 6, button_size, button_size};
        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
    }

    {
        if (mouse_mode == MOUSE_MODE_SELECT)
            render_box(right_panel_offset + XYPos(0, button_size * 6), XYPos(button_size, button_size));
        SDL_Rect src_rect = {1088, 192, 192, 192};
        SDL_Rect dst_rect = {right_panel_offset.x, right_panel_offset.y + button_size * 6, button_size, button_size};
        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
    }

    {
        if (mouse_mode == MOUSE_MODE_HIDE || mouse_mode == MOUSE_MODE_SHOW)
            render_box(right_panel_offset + XYPos(button_size * 3, button_size * 6), XYPos(button_size, button_size));
        SDL_Rect src_rect = {show_mode ? 1088 : 896, 384, 192, 192};
        SDL_Rect dst_rect = {right_panel_offset.x + 3 * button_size, right_panel_offset.y + button_size * 6, button_size, button_size};
        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
    }

    for (int i = 0; i < deaths; i++)
    {
        SDL_Rect src_rect = {704, 192, 192, 192};
        SDL_Rect dst_rect = {left_panel_offset.x + (i % 4) * button_size, left_panel_offset.y + button_size * 8 + (i / 4) * button_size, button_size, button_size};
        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
    }

    if (right_panel_mode == RIGHT_MENU_REGION)
    {
        set_region_colour(sdl_texture, inspected_region->type.value, inspected_region->colour, 255);
        {
        SDL_Rect src_rect = {1344, 256, 256, 384};
        SDL_Rect dst_rect = {right_panel_offset.x + button_size / 2, right_panel_offset.y + 1 * button_size, button_size * 1, button_size  * 2};
        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
        }

        {
        SDL_Rect src_rect = {64, 512, 192, 192};
        SDL_Rect dst_rect = {right_panel_offset.x + button_size / 2, right_panel_offset.y + 1 * button_size, button_size * 2 / 3, button_size * 2 / 3};
        SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
        }
        {
        SDL_SetTextureColorMod(sdl_texture, 0,0,0);
        render_region_type(inspected_region->type, XYPos(right_panel_offset.x + button_size / 2, right_panel_offset.y + 1 * button_size), button_size * 2 / 3);
        }
        SDL_SetTextureColorMod(sdl_texture, 255, 255, 255);
        {
            int count = inspected_region->elements.size();
            SDL_Rect src_rect = {192 * count, 0, 192, 192};
            SDL_Rect dst_rect = {right_panel_offset.x + button_size / 2 + button_size/8, right_panel_offset.y + 2 * button_size + button_size/8, button_size*6/8, button_size*6/8};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
        }
        {
            SDL_Rect src_rect = { 1088, 576, 192, 192 };
            SDL_Rect dst_rect = { right_panel_offset.x + button_size * 3, right_panel_offset.y, button_size, button_size};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
        }
        if (inspected_region->rule)
        {
            SDL_Rect src_rect = { 704, 768, 192, 192 };
            SDL_Rect dst_rect = { right_panel_offset.x + button_size * 0, right_panel_offset.y + 4 * button_size, button_size, button_size };
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
        }
    }

    if (right_panel_mode == RIGHT_MENU_RULE_GEN)
    {
        if (rule_gen_region_count >= 1)
        {
            set_region_colour(sdl_texture, rule_gen_region[0]->type.value, rule_gen_region[0]->colour, 255);
            {
            SDL_Rect src_rect = {1344, 256, 256, 384};
            SDL_Rect dst_rect = {right_panel_offset.x + button_size / 2, right_panel_offset.y + 0 * button_size, button_size * 2, button_size  * 3};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            }

            {
            SDL_Rect src_rect = {64, 512, 192, 192};
            SDL_Rect dst_rect = {right_panel_offset.x + button_size / 2, right_panel_offset.y + 0 * button_size, button_size * 2 / 3, button_size * 2 / 3};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            }
            {
            SDL_SetTextureColorMod(sdl_texture, 0,0,0);
            render_region_type(rule_gen_region[0]->type, XYPos(right_panel_offset.x + button_size / 2, right_panel_offset.y), button_size * 2 / 3);
            }
            if ((constructed_rule.apply_type == GridRule::SHOW || constructed_rule.apply_type == GridRule::HIDE) && constructed_rule.apply_region_bitmap & 1)
            {
                SDL_SetTextureColorMod(sdl_texture, 255, 255, 255);
                SDL_Rect src_rect = {(constructed_rule.apply_type == GridRule::SHOW) ? 1088 : 896, 384, 192, 192};
                SDL_Rect dst_rect = {right_panel_offset.x + button_size / 2 + button_size / 3, right_panel_offset.y + 0 * button_size + button_size / 3, button_size * 2 / 3, button_size * 2 / 3};
                SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            }
        }
        if (rule_gen_region_count >= 2)
        {
            set_region_colour(sdl_texture, rule_gen_region[1]->type.value, rule_gen_region[1]->colour, 255);
            {
            SDL_Rect src_rect = {1344, 256, 256, 384};
            SDL_Rect dst_rect = {right_panel_offset.x + int(button_size * 1.5), right_panel_offset.y + 1 * button_size, button_size * 2, button_size  * 3};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            }
            {
            SDL_Rect src_rect = {64, 512, 192, 192};
            SDL_Rect dst_rect = {right_panel_offset.x + int(button_size * 3.5) - button_size * 2 / 3, right_panel_offset.y + 1 * button_size, button_size * 2 / 3, button_size * 2 / 3};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            }
            {
            SDL_SetTextureColorMod(sdl_texture, 0,0,0);
            render_region_type(rule_gen_region[1]->type, XYPos(right_panel_offset.x + int(button_size * 3.5) - button_size * 2 / 3, right_panel_offset.y + button_size), button_size * 2 / 3);
            }
            if ((constructed_rule.apply_type == GridRule::SHOW || constructed_rule.apply_type == GridRule::HIDE) && constructed_rule.apply_region_bitmap & 2)
            {
                SDL_SetTextureColorMod(sdl_texture, 255, 255, 255);
                SDL_Rect src_rect = {(constructed_rule.apply_type == GridRule::SHOW) ? 1088 : 896, 384, 192, 192};
                SDL_Rect dst_rect = {right_panel_offset.x + int(button_size * 3.5) - button_size, right_panel_offset.y + 1 * button_size + button_size / 3, button_size * 2 / 3, button_size * 2 / 3};
                SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            }
        }
        if (rule_gen_region_count >= 3)
        {
            set_region_colour(sdl_texture, rule_gen_region[2]->type.value, rule_gen_region[2]->colour, 255);
            {
            SDL_Rect src_rect = {1344, 256, 256, 384};
            SDL_Rect dst_rect = {right_panel_offset.x + button_size / 2 + button_size / 16, right_panel_offset.y + 2 * button_size, button_size * 2, button_size  * 3};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            }
            {
            SDL_Rect src_rect = {64, 512, 192, 192};
            SDL_Rect dst_rect = {right_panel_offset.x + button_size / 2 + button_size / 16, right_panel_offset.y + 5 * button_size - button_size * 2 / 3, button_size * 2 / 3, button_size * 2 / 3};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            }
            {
            SDL_SetTextureColorMod(sdl_texture, 0,0,0);
            render_region_type(rule_gen_region[2]->type, XYPos(right_panel_offset.x + button_size / 2 + button_size / 16, right_panel_offset.y + 5 * button_size - button_size * 2 / 3), button_size * 2 / 3);
            }
            if ((constructed_rule.apply_type == GridRule::SHOW || constructed_rule.apply_type == GridRule::HIDE) && constructed_rule.apply_region_bitmap & 4)
            {
                SDL_SetTextureColorMod(sdl_texture, 255, 255, 255);
                SDL_Rect src_rect = {(constructed_rule.apply_type == GridRule::SHOW) ? 1088 : 896, 384, 192, 192};
                SDL_Rect dst_rect = {right_panel_offset.x + button_size / 2 + button_size / 16 + button_size / 3, right_panel_offset.y + 5 * button_size - button_size, button_size * 2 / 3, button_size * 2 / 3};
                SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            }
        }
        SDL_SetTextureColorMod(sdl_texture, 255, 255, 255);
        if (rule_gen_region_count >= 1)
        {
            {
                SDL_Rect src_rect = { 192*3+128, 192*3, 192, 192 };
                SDL_Rect dst_rect = { right_panel_offset.x + button_size * 3, right_panel_offset.y, button_size, button_size};
                SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            }
        }
        if (rule_gen_region_count >= 1 && constructed_rule.apply_region_bitmap)
        {
            {
                SDL_Rect src_rect = { 192 * ((constructed_rule_is_logical) ? 3 : 4) + 128, 192 * ((constructed_rule_is_logical) ? 2 : 3), 192, 192 };
                SDL_Rect dst_rect = { right_panel_offset.x + button_size * 3, right_panel_offset.y + 4 * button_size, button_size, button_size };
                SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            }
        }


        int counts[10] = {};

        if (rule_gen_region_count >= 1)
        {
            counts[1] = rule_gen_region[0]->elements.size();
        }
        if (rule_gen_region_count >= 2)
        {
            counts[2] = rule_gen_region[1]->elements.size();
            for (XYPos pos : rule_gen_region[1]->elements)
            {
                if (rule_gen_region[0]->elements.count(pos))
                {
                    counts[1]--;
                    counts[2]--;
                    counts[3]++;
                }
            }
        }
        if (rule_gen_region_count >= 3)
        {
            counts[4] = rule_gen_region[2]->elements.size();

            for (XYPos pos : rule_gen_region[2]->elements)
            {
                bool r1 = rule_gen_region[0]->elements.count(pos);
                bool r2 = rule_gen_region[1]->elements.count(pos);
                if (r1 && r2)
                {
                    counts[3]--;
                    counts[4]--;
                    counts[7]++;
                }
                else if (r1)
                {
                    counts[1]--;
                    counts[4]--;
                    counts[5]++;
                }
                else if (r2)
                {
                    counts[2]--;
                    counts[4]--;
                    counts[6]++;
                }
            }
        }


        XYPos sq_pos[8] = { XYPos(0,0), XYPos(0,0), XYPos(2,1), XYPos(1,0),
                            XYPos(0,2), XYPos(0,1), XYPos(1,2), XYPos(1,1)};

        for (int i = 1; i < (1 << rule_gen_region_count); i++)
        {
            SDL_Rect src_rect = {192 * counts[i], 0, 192, 192};
            if (rule_gen_region_undef_num & (1 << i)) src_rect = {896, 192, 192, 192};
            SDL_Rect dst_rect = {right_panel_offset.x + button_size / 2 + button_size * sq_pos[i].x + button_size/8, right_panel_offset.y + sq_pos[i].y * button_size + button_size + button_size/8, button_size*6/8, button_size*6/8};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            if ((constructed_rule.apply_region_bitmap >> i) & 1)
            {
                if (constructed_rule.apply_type == GridRule::REGION)
                {
                    set_region_colour(sdl_texture, constructed_rule.apply_region_type.value, 0, 255);
                    src_rect = {64, 512, 192, 192};
                    dst_rect = {right_panel_offset.x + button_size + button_size * sq_pos[i].x, right_panel_offset.y + sq_pos[i].y * button_size + button_size + button_size / 2, button_size / 2, button_size / 2};
                    SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                    SDL_SetTextureColorMod(sdl_texture, 0,0,0);
                    render_region_type(constructed_rule.apply_region_type, XYPos(right_panel_offset.x + button_size + button_size * sq_pos[i].x, right_panel_offset.y + sq_pos[i].y * button_size + button_size + button_size / 2), button_size / 2);
                    SDL_SetTextureColorMod(sdl_texture, 255, 255, 255);
                }
                else if (constructed_rule.apply_type == GridRule::BOMB || constructed_rule.apply_type == GridRule::CLEAR)
                {
                    src_rect = {(constructed_rule.apply_type == GridRule::BOMB) ? 320 : 320 + 192, 192, 192, 192};
                    dst_rect = {right_panel_offset.x + button_size + button_size * sq_pos[i].x, right_panel_offset.y + sq_pos[i].y * button_size + button_size + button_size / 2, button_size / 2, button_size / 2};
                    SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                }
            }
        }
    }

    if (right_panel_mode == RIGHT_MENU_RULE_INSPECT)
    {
        if (inspected_rule->region_count >= 1)
        {
            set_region_colour(sdl_texture, inspected_rule->region_type[0].value, 0, 255);
            {
            SDL_Rect src_rect = {1344, 256, 256, 384};
            SDL_Rect dst_rect = {right_panel_offset.x + button_size / 2, right_panel_offset.y + 0 * button_size, button_size * 2, button_size  * 3};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            }

            {
            SDL_Rect src_rect = {64, 512, 192, 192};
            SDL_Rect dst_rect = {right_panel_offset.x + button_size / 2, right_panel_offset.y + 0 * button_size, button_size * 2 / 3, button_size * 2 / 3};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            }
            {
            SDL_SetTextureColorMod(sdl_texture, 0,0,0);
            render_region_type(inspected_rule->region_type[0], XYPos(right_panel_offset.x + button_size / 2, right_panel_offset.y), button_size * 2 / 3);
            }
            if ((inspected_rule->apply_type == GridRule::SHOW || inspected_rule->apply_type == GridRule::HIDE) && inspected_rule->apply_region_bitmap & 1)
            {
                SDL_SetTextureColorMod(sdl_texture, 255, 255, 255);
                SDL_Rect src_rect = {(inspected_rule->apply_type == GridRule::SHOW) ? 1088 : 896, 384, 192, 192};
                SDL_Rect dst_rect = {right_panel_offset.x + button_size / 2 + button_size / 3, right_panel_offset.y + 0 * button_size + button_size / 3, button_size * 2 / 3, button_size * 2 / 3};
                SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            }
        }
        if (inspected_rule->region_count >= 2)
        {
            set_region_colour(sdl_texture, inspected_rule->region_type[1].value, 0, 255);
            {
            SDL_Rect src_rect = {1344, 256, 256, 384};
            SDL_Rect dst_rect = {right_panel_offset.x + int(button_size * 1.5), right_panel_offset.y + 1 * button_size, button_size * 2, button_size  * 3};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            }
            {
            SDL_Rect src_rect = {64, 512, 192, 192};
            SDL_Rect dst_rect = {right_panel_offset.x + int(button_size * 3.5) - button_size * 2 / 3, right_panel_offset.y + 1 * button_size, button_size * 2 / 3, button_size * 2 / 3};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            }
            {
            SDL_SetTextureColorMod(sdl_texture, 0,0,0);
            render_region_type(inspected_rule->region_type[0], XYPos(right_panel_offset.x + int(button_size * 3.5) - button_size * 2 / 3, right_panel_offset.y + button_size), button_size * 2 / 3);
            }
            if ((inspected_rule->apply_type == GridRule::SHOW || inspected_rule->apply_type == GridRule::HIDE) && inspected_rule->apply_region_bitmap & 2)
            {
                SDL_SetTextureColorMod(sdl_texture, 255, 255, 255);
                SDL_Rect src_rect = {(inspected_rule->apply_type == GridRule::SHOW) ? 1088 : 896, 384, 192, 192};
                SDL_Rect dst_rect = {right_panel_offset.x + int(button_size * 3.5) - button_size, right_panel_offset.y + 1 * button_size + button_size / 3, button_size * 2 / 3, button_size * 2 / 3};
                SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            }
        }
        if (inspected_rule->region_count >= 3)
        {
            set_region_colour(sdl_texture, inspected_rule->region_type[2].value, 0, 255);
            {
            SDL_Rect src_rect = {1344, 256, 256, 384};
            SDL_Rect dst_rect = {right_panel_offset.x + button_size / 2 + button_size / 16, right_panel_offset.y + 2 * button_size, button_size * 2, button_size  * 3};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            }
            {
            SDL_Rect src_rect = {64, 512, 192, 192};
            SDL_Rect dst_rect = {right_panel_offset.x + button_size / 2 + button_size / 16, right_panel_offset.y + 5 * button_size - button_size * 2 / 3, button_size * 2 / 3, button_size * 2 / 3};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            }
            {
            SDL_SetTextureColorMod(sdl_texture, 0,0,0);
            render_region_type(inspected_rule->region_type[0], XYPos(right_panel_offset.x + button_size / 2 + button_size / 16, right_panel_offset.y + 5 * button_size - button_size * 2 / 3), button_size * 2 / 3);
            }
            if ((inspected_rule->apply_type == GridRule::SHOW || inspected_rule->apply_type == GridRule::HIDE) && inspected_rule->apply_region_bitmap & 4)
            {
                SDL_SetTextureColorMod(sdl_texture, 255, 255, 255);
                SDL_Rect src_rect = {(inspected_rule->apply_type == GridRule::SHOW) ? 1088 : 896, 384, 192, 192};
                SDL_Rect dst_rect = {right_panel_offset.x + button_size / 2 + button_size / 16 + button_size / 3, right_panel_offset.y + 5 * button_size - button_size, button_size * 2 / 3, button_size * 2 / 3};
                SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            }
        }
        SDL_SetTextureColorMod(sdl_texture, 255, 255, 255);
        // if (rule_gen_region_count >= 1)
        // {
        //     {
        //         SDL_Rect src_rect = { 192*3+128, 192*3, 192, 192 };
        //         SDL_Rect dst_rect = { right_panel_offset.x + button_size * 3, right_panel_offset.y, button_size, button_size};
        //         SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
        //     }
        // }
        // if (rule_gen_region_count >= 1 && constructed_rule.apply_region_bitmap)
        // {
        //     {
        //         SDL_Rect src_rect = { 192 * ((constructed_rule_is_logical) ? 3 : 4) + 128, 192 * ((constructed_rule_is_logical) ? 2 : 3), 192, 192 };
        //         SDL_Rect dst_rect = { right_panel_offset.x + button_size * 3, right_panel_offset.y + 4 * button_size, button_size, button_size };
        //         SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
        //     }
        // }


        XYPos sq_pos[8] = { XYPos(0,0), XYPos(0,0), XYPos(2,1), XYPos(1,0),
                            XYPos(0,2), XYPos(0,1), XYPos(1,2), XYPos(1,1)};

        for (int i = 1; i < (1 << inspected_rule->region_count); i++)
        {
            SDL_Rect src_rect = {192 * inspected_rule->square_counts[i], 0, 192, 192};
            if (inspected_rule->square_counts[i] < 0) src_rect = {896, 192, 192, 192};
            SDL_Rect dst_rect = {right_panel_offset.x + button_size / 2 + button_size * sq_pos[i].x + button_size/8, right_panel_offset.y + sq_pos[i].y * button_size + button_size + button_size/8, button_size*6/8, button_size*6/8};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
            if ((inspected_rule->apply_region_bitmap >> i) & 1)
            {
                if (inspected_rule->apply_type == GridRule::REGION)
                {
                    set_region_colour(sdl_texture, inspected_rule->apply_region_type.value, 0, 255);
                    src_rect = {64, 512, 192, 192};
                    dst_rect = {right_panel_offset.x + button_size + button_size * sq_pos[i].x, right_panel_offset.y + sq_pos[i].y * button_size + button_size + button_size / 2, button_size / 2, button_size / 2};
                    SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                    SDL_SetTextureColorMod(sdl_texture, 0,0,0);
                    render_region_type(inspected_rule->apply_region_type, XYPos(right_panel_offset.x + button_size + button_size * sq_pos[i].x, right_panel_offset.y + sq_pos[i].y * button_size + button_size + button_size / 2), button_size / 2);
                    SDL_SetTextureColorMod(sdl_texture, 255, 255, 255);
                }
                else if (inspected_rule->apply_type == GridRule::BOMB || inspected_rule->apply_type == GridRule::CLEAR)
                {
                    src_rect = {(inspected_rule->apply_type == GridRule::BOMB) ? 320 : 320 + 192, 192, 192, 192};
                    dst_rect = {right_panel_offset.x + button_size + button_size * sq_pos[i].x, right_panel_offset.y + sq_pos[i].y * button_size + button_size + button_size / 2, button_size / 2, button_size / 2};
                    SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
                }
            }
        }
    }

//     if (rule_gen_region_count >= 1)
//     {
//         SDL_Rect src_rect = {192 * counts[1], 0, 192, 192};
//         if (rule_gen_region_undef_num & (1 << 1)) src_rect = {896, 192, 192, 192};
//         SDL_Rect dst_rect = {right_panel_offset.x + button_size / 2 + button_size/8 + button_size/8, right_panel_offset.y + 1 * button_size + button_size/8, button_size*6/8, button_size*6/8};
//         SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
//     }
//     if (rule_gen_region_count >= 2)
//     {
//         {
//         SDL_Rect src_rect = {192 * counts[2], 0, 192, 192};
//         if (rule_gen_region_undef_num & (1 << 2)) src_rect = {896, 192, 192, 192};
//         SDL_Rect dst_rect = {right_panel_offset.x + 2 * button_size + button_size / 2 + button_size/8, right_panel_offset.y + 2 * button_size + button_size/8, button_size*6/8, button_size*6/8};
//         SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
//         }
//         {
//         SDL_Rect src_rect = {192 * counts[3], 0, 192, 192};
//         if (rule_gen_region_undef_num & (1 << 3)) src_rect = {896, 192, 192, 192};
//         SDL_Rect dst_rect = {right_panel_offset.x + 1 * button_size + button_size / 2 + button_size/8, right_panel_offset.y + 1 * button_size + button_size/8, button_size*6/8, button_size*6/8};
//         SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
//         }
//     }
//     if (rule_gen_region_count >= 3)
//     {
//         {
//         SDL_Rect src_rect = {192 * counts[4], 0, 192, 192};
//         if (rule_gen_region_undef_num & (1 << 4)) src_rect = {896, 192, 192, 192};
//         SDL_Rect dst_rect = {right_panel_offset.x + button_size / 2 + button_size / 8, right_panel_offset.y + 3 * button_size + button_size/8, button_size*6/8, button_size*6/8};
//         SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
//         }
//         {
//         SDL_Rect src_rect = {192 * counts[5], 0, 192, 192};
//         if (rule_gen_region_undef_num & (1 << 5)) src_rect = {896, 192, 192, 192};
//         SDL_Rect dst_rect = {right_panel_offset.x + button_size / 2 + button_size / 8, right_panel_offset.y + 2 * button_size + button_size/8, button_size*6/8, button_size*6/8};
//         SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
//         }
//         {
//         SDL_Rect src_rect = {192 * counts[6], 0, 192, 192};
//         if (rule_gen_region_undef_num & (1 << 6)) src_rect = {896, 192, 192, 192};
//         SDL_Rect dst_rect = {right_panel_offset.x + 1 * button_size + button_size / 2 + button_size/8, right_panel_offset.y + 3 * button_size + button_size/8, button_size*6/8, button_size*6/8};
//         SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
//         }
//         {
//         SDL_Rect src_rect = {192 * counts[7], 0, 192, 192};
//         if (rule_gen_region_undef_num & (1 << 7)) src_rect = {896, 192, 192, 192};
//         SDL_Rect dst_rect = {right_panel_offset.x + 1 * button_size + button_size / 2 + button_size/8, right_panel_offset.y + 2 * button_size + button_size/8, button_size*6/8, button_size*6/8};
//         SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
//         }
//     }

    // XYPos rule_pos = XYPos(0,0);
    // for (GridRule& rule : rules)
    // {
    //     SDL_Rect src_rect = {rule.halted ? 704 : 704 + 192, 192, 192, 192};
    //     SDL_Rect dst_rect = {right_panel_offset.x + rule_pos.x * button_size / 2 , right_panel_offset.y + 6 * button_size + rule_pos.y * button_size/2, button_size/2, button_size/2};
    //     SDL_RenderCopy(sdl_renderer, sdl_texture, &src_rect, &dst_rect);
    //
    //     rule_pos.y++;
    //     if (rule_pos.y >= 8)
    //     {
    //         rule_pos.y = 0;
    //         rule_pos.x++;
    //     }
    // }



    SDL_RenderPresent(sdl_renderer);
}

void GameState::grid_click(XYPos pos, bool right)
{
    pos /= square_size;
    if ((mouse_mode == MOUSE_MODE_BOMB) || (mouse_mode == MOUSE_MODE_CLEAR))
    {
        GridPlace place = grid->get(pos);
        if (place.revealed)
            return;
        if ((mouse_mode == MOUSE_MODE_BOMB) != place.bomb)
        {
            deaths++;
            return;
        }
        if (grid->is_determinable(pos))
        {
            grid->reveal(pos);
            show_clue = false;
            reset_rule_gen_region();
        }
        else
            deaths++;

    }
    else if (mouse_mode == MOUSE_MODE_DRAWING_REGION)
    {
        if (!edited_region.elements.count(pos))
        {
            edited_region.elements.insert(pos);
        }
        else
        {
            edited_region.elements.erase(pos);
        }
    }
    else if((mouse_mode == MOUSE_MODE_SELECT) || (mouse_mode == MOUSE_MODE_HIDE) || (mouse_mode == MOUSE_MODE_SHOW))
    {
        GridRegion* hover = NULL;
        if ((mouse - grid_offset).inside(XYPos(grid_size,grid_size)))
        {
            XYPos pos = mouse - grid_offset;
            XYPos gpos = pos / square_size;
            XYPos ipos = pos - gpos * square_size;

            unsigned tot = 0;
            for (GridRegion& region : grid->regions)
            {
                if (region.hidden == (mouse_mode == MOUSE_MODE_SHOW))
                    if (region.elements.count(gpos))
                        tot++;
            }
            unsigned size = taken_to_size(tot);

            for (int i = 0; i < 100; i++)
            {
                XYPos spos = taken_to_pos(i, tot);
                if (XYPosFloat(ipos).distance(spos) < (double(square_size) / (size * 2)))
                {
                    int c = 0;
                    for (GridRegion& region : grid->regions)
                    {
                        if (region.hidden != (mouse_mode == MOUSE_MODE_SHOW))
                            continue;
                        if (region.elements.count(gpos))
                        {
                            if (c == i)
                            {
                                hover = &region;
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
            if (mouse_mode == MOUSE_MODE_SELECT)
            {
                if (inspected_region == hover && right_panel_mode == RIGHT_MENU_REGION)
                {
                    right_panel_mode = RIGHT_MENU_RULE_GEN;
                    if (rule_gen_region_count < 3)
                    {
                        rule_gen_region[rule_gen_region_count] = hover;
                        rule_gen_region_count++;
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
            if (mouse_mode == MOUSE_MODE_HIDE)
            {
                hover->hidden = true;
                hover->visibility_forced = true;
            }
            if (mouse_mode == MOUSE_MODE_SHOW)
            {
                assert(hover->hidden);
                hover->hidden = false;
                hover->visibility_forced = true;
            }
        }
    }
}

void GameState::left_panel_click(XYPos pos, bool right)
{
    int button_size = (grid_size * 7) / (18 * 4);
    const int trim = 27;
    int digit_height = button_size * 0.75;
    int digit_width = (digit_height * (192 - trim * 2)) / 192;

    if (mouse_mode == MOUSE_MODE_SELECT && pos.inside(XYPos(digit_height + digit_width * 5, digit_height)))
    {
        for (GridRegion& region : grid->regions)
        {
            if (region.global)
            {
                if (inspected_region == &region && right_panel_mode == RIGHT_MENU_REGION)
                {
                    right_panel_mode = RIGHT_MENU_RULE_GEN;
                    if (rule_gen_region_count < 3)
                    {
                        rule_gen_region[rule_gen_region_count] = &region;
                        rule_gen_region_count++;
                        update_constructed_rule();
                    }
                }
                else
                {
                    right_panel_mode = RIGHT_MENU_REGION;
                    inspected_region = &region;
                }
            }
        }
    }


}

void GameState::right_panel_click(XYPos pos, bool right)
{
    int button_size = (grid_size * 7) / (18 * 4);


    if ((pos - XYPos(button_size * 1, button_size * 6)).inside(XYPos(button_size,button_size)))
            mouse_mode = MOUSE_MODE_CLEAR;
    if ((pos - XYPos(button_size * 2, button_size * 6)).inside(XYPos(button_size,button_size)))
            mouse_mode = MOUSE_MODE_BOMB;
    if ((pos - XYPos(button_size * 0, button_size * 6)).inside(XYPos(button_size,button_size)))
            mouse_mode = MOUSE_MODE_SELECT;
    if ((pos - XYPos(button_size * 3, button_size * 6)).inside(XYPos(button_size,button_size)))
    {
        if (mouse_mode == MOUSE_MODE_HIDE || mouse_mode == MOUSE_MODE_SHOW)
            show_mode = !show_mode;
        mouse_mode = show_mode ? MOUSE_MODE_SHOW : MOUSE_MODE_HIDE;
    }

    if ((pos - XYPos(0 * button_size, button_size * 7)).inside(XYPos(button_size,button_size)))
            edited_region.type.type = RegionType::EQUAL;
    if ((pos - XYPos(1 * button_size, button_size * 7)).inside(XYPos(button_size,button_size)))
            edited_region.type.type = RegionType::MORE;
    if ((pos - XYPos(2 * button_size, button_size * 7)).inside(XYPos(button_size,button_size)))
            edited_region.type.type = RegionType::LESS;

    for (int i = 0; i < 4; i++)
    {
        if ((pos - XYPos(i * button_size, button_size * 8)).inside(XYPos(button_size,button_size)))
        {
            mouse_mode = MOUSE_MODE_DRAWING_REGION;
            edited_region.reset(RegionType(edited_region.type.type, i + 1));
        }
    }
    for (int i = 0; i < 4; i++)
    {
        if ((pos - XYPos(i * button_size, button_size * 9)).inside(XYPos(button_size,button_size)))
        {
            mouse_mode = MOUSE_MODE_DRAWING_REGION;
            edited_region.reset(RegionType(edited_region.type.type, i + 5));
        }
    }

    if (right_panel_mode == RIGHT_MENU_REGION)
    {
        if ((pos - XYPos(button_size * 3, 0)).inside(XYPos(button_size, button_size)))
        {
            right_panel_mode = RIGHT_MENU_RULE_GEN;
            if (rule_gen_region_count < 3)
            {
                rule_gen_region[rule_gen_region_count] = inspected_region;
                rule_gen_region_count++;
                update_constructed_rule();
            }
            return;
        }
        if ((pos - XYPos(button_size * 0, button_size * 4)).inside(XYPos(button_size, button_size)))
        {
            if(inspected_region->rule)
            {
                right_panel_mode = RIGHT_MENU_RULE_INSPECT;
                inspected_rule = inspected_region->rule;
            }
            return;
        }
    }

    if (right_panel_mode == RIGHT_MENU_RULE_GEN)
    {
        if ((mouse_mode == MOUSE_MODE_HIDE) || (mouse_mode == MOUSE_MODE_SHOW))
        {
            int r = 0;
            if ((pos - XYPos(button_size / 2, 0)).inside(XYPos(button_size, button_size)) && rule_gen_region_count >= 1)
                r = 1;
            else if ((pos - XYPos(button_size / 2 + button_size * 2, button_size)).inside(XYPos(button_size, button_size)) && rule_gen_region_count >= 2)
                r = 2;
            else if ((pos - XYPos(button_size / 2, button_size * 4)).inside(XYPos(button_size, button_size)) && rule_gen_region_count >= 3)
                r = 4;
            if (r)
            {
                    constructed_rule.apply_region_bitmap ^= r;
                    constructed_rule.apply_type = (mouse_mode == MOUSE_MODE_HIDE) ? GridRule::HIDE : GridRule::SHOW;
                    update_constructed_rule();
            }
        }

        if ((pos - XYPos(button_size / 2, button_size)).inside(XYPos(button_size * 3, button_size * 5)))
        {
            XYPos ppos = pos - XYPos(button_size / 2, button_size);
            XYPos gpos = ppos / button_size;
            bool hover_rulemaker = false;
            unsigned hover_rulemaker_bits = 0;

            if (gpos == XYPos(0, 0) && rule_gen_region_count >= 1)
            {
                hover_rulemaker = true;
                hover_rulemaker_bits = 1;
            }
            if (gpos == XYPos(1, 0) && rule_gen_region_count >= 2)
            {
                hover_rulemaker = true;
                hover_rulemaker_bits = 3;
            }
            if (gpos == XYPos(2, 1) && rule_gen_region_count >= 2)
            {
                hover_rulemaker = true;
                hover_rulemaker_bits = 2;
            }

            if (gpos == XYPos(0, 1) && rule_gen_region_count >= 3)
            {
                hover_rulemaker = true;
                hover_rulemaker_bits = 5;
            }
            if (gpos == XYPos(1, 1) && rule_gen_region_count >= 3)
            {
                hover_rulemaker = true;
                hover_rulemaker_bits = 7;
            }
            if (gpos == XYPos(0, 2) && rule_gen_region_count >= 3)
            {
                hover_rulemaker = true;
                hover_rulemaker_bits = 4;
            }
            if (gpos == XYPos(1, 2) && rule_gen_region_count >= 3)
            {
                hover_rulemaker = true;
                hover_rulemaker_bits = 6;
            }

            if (hover_rulemaker)
            {
                if (mouse_mode == MOUSE_MODE_SELECT)
                {
                    rule_gen_region_undef_num ^= 1 << hover_rulemaker_bits;
                    update_constructed_rule();
                }
                else if ((mouse_mode == MOUSE_MODE_BOMB) || (mouse_mode == MOUSE_MODE_CLEAR) || (mouse_mode == MOUSE_MODE_DRAWING_REGION))
                {
                    constructed_rule.apply_region_bitmap ^= 1 << hover_rulemaker_bits;
                    if (mouse_mode == MOUSE_MODE_DRAWING_REGION)
                    {
                        if (constructed_rule.apply_type != GridRule::REGION)
                            constructed_rule.apply_region_bitmap = 1 << hover_rulemaker_bits;
                        if (constructed_rule.apply_region_type != edited_region.type)
                            constructed_rule.apply_region_bitmap = 1 << hover_rulemaker_bits;
                        constructed_rule.apply_type = GridRule::REGION;
                        constructed_rule.apply_region_type = edited_region.type;
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
        if ((pos - XYPos(button_size * 3 , button_size * 4)).inside(XYPos(button_size, button_size)))
            if (rule_gen_region_count)
                {
                    constructed_rule.import_rule_gen_regions(rule_gen_region[0], rule_gen_region[1], rule_gen_region[2]);
                    for (int i = 0; i < 8; i++)
                    {
                        if ((rule_gen_region_undef_num >> i) & 1)
                        {
                            constructed_rule.square_counts[i] = -1;
                        }
                    }
                    if (constructed_rule.is_legal())
                    {
                        rules.push_back(constructed_rule);
                        reset_rule_gen_region();
                    }
                }

        if ((pos - XYPos(button_size * 3, 0)).inside(XYPos(button_size, button_size)))
        {
            if (rule_gen_region_count)
                rule_gen_region_count--;
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
                        std::string s = SDL_GetClipboardText();
                        grid->from_string(s);
                        break;
                    }
                    case SDL_SCANCODE_ESCAPE:
		                    quit = true;
                        break;
                    case SDL_SCANCODE_F1:
                    {
                        clue_solves.clear();
                        //grid->find_easiest_move(clue_solves, clue_needs);
                        grid->find_easiest_move_using_regions(clue_solves);
                        show_clue = true;
                        break;
                    }
                    case SDL_SCANCODE_F2:
                    {
                        auto_region = !auto_region;
                        break;
                    }
                    case SDL_SCANCODE_F3:
                    {
                        grid->regions.clear();
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

                if (right && mouse_mode == MOUSE_MODE_DRAWING_REGION)
                {
                    grid->regions.push_back(edited_region);
                    edited_region.elements.clear();
                    mouse_mode = MOUSE_MODE_CLEAR;
                    break;
                }
                if (right && mouse_mode == MOUSE_MODE_SELECT)
                {
                    reset_rule_gen_region();
                    break;
                }
                if ((mouse - left_panel_offset).inside(panel_size))
                {
                    left_panel_click(mouse - left_panel_offset, right);
                }
                else if ((mouse - grid_offset).inside(XYPos(grid_size,grid_size)))
                {
                    grid_click(mouse - grid_offset, right);
                }
                else if ((mouse - right_panel_offset).inside(panel_size))
                {
                    right_panel_click(mouse - right_panel_offset, right);
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

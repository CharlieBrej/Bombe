#pragma once
#include "Misc.h"
#include "SaveState.h"
#include "Grid.h"

#include <SDL.h>
#include <SDL_image.h>
#include <SDL_mixer.h>
#include <SDL_ttf.h>

#include <map>
#include <list>
#include <set>

extern bool IS_DEMO;

class GameState
{
public:
    SDL_Window* sdl_window;
    SDL_Renderer* sdl_renderer;
    SDL_Texture* sdl_texture;
    TTF_Font *font;

    std::string steam_session_string;
    bool achievement[10] = {};
    std::string steam_name;
    uint64_t steam_id = 0;


    Grid *grid;
    std::list<GridRule> rules;


    XYPos mouse;
    enum {
        MOUSE_MODE_BOMB,
        MOUSE_MODE_CLEAR,
        MOUSE_MODE_NONE,
        MOUSE_MODE_VIS,
        MOUSE_MODE_DRAWING_REGION,
        MOUSE_MODE_FILTER,
        }
        mouse_mode = MOUSE_MODE_BOMB;

    enum {
        RIGHT_MENU_NONE,
        RIGHT_MENU_REGION,
        RIGHT_MENU_RULE_GEN,
        RIGHT_MENU_RULE_INSPECT,
        }
        right_panel_mode = RIGHT_MENU_NONE;


    GridRegion *rule_gen_region[4] = {};
//    unsigned rule_gen_region_count = 0;
//    unsigned rule_gen_region_undef_num = 0;
    GridRule constructed_rule;
    RegionType region_type = RegionType(RegionType::EQUAL, 1);
    bool constructed_rule_is_logical = false;

    GridRegion *inspected_region = NULL;
    GridRule *inspected_rule = NULL;
    bool rule_gen_target_square_count = false;
    std::set<XYPos> filter_pos;

    int grid_size;
    XYPos panel_size;
    int square_size = 1;
    int button_size = 1;
    XYPos left_panel_offset;
    XYPos right_panel_offset;
    XYPos grid_offset;
    bool full_screen = false;

    bool skip_level = false;
    int cooldown = 0;
    bool last_active_was_hit = false;

    GridVisLevel vis_level = GRID_VIS_LEVEL_SHOW;
    GridVisLevel vis_mode = GRID_VIS_LEVEL_HIDE;
    bool xor_is_3 = false;

    bool auto_region = true;
    bool get_hint = false;
    Grid clue_needs;
    std::set<XYPos> clue_solves;

    std::list<Grid*> levels;
    SDL_mutex *levels_mutex;

    class LevelProgress
    {
    public:
        unsigned counts[4] = {};
        std::vector<uint8_t> level_status;
    };

    std::vector<LevelProgress> level_progress;

    unsigned current_level_set_index = 0;
    unsigned current_level_index = 0;
    bool current_level_is_temp = true;
    bool current_level_hinted = false;
    bool current_level_manual = false;


    GameState(std::ifstream& loadfile);
    SaveObject* save(bool lite = false);
    void save(std::ostream& outfile, bool lite = false);
    ~GameState();
    SDL_Texture* loadTexture(const char* filename);

    void advance();
    void audio();

    GridRule rule_from_rule_gen_region();
    void reset_rule_gen_region();
    void update_constructed_rule();
    unsigned taken_to_size(unsigned total);
    XYPos taken_to_pos(unsigned count, unsigned total);
    void render_region_bg(GridRegion& region, std::map<XYPos, int>& taken, std::map<XYPos, int>& total_taken);
    void render_region_fg(GridRegion& region, std::map<XYPos, int>& taken, std::map<XYPos, int>& total_taken);
    void render_box2(XYPos pos, XYPos size);
    void render_box(XYPos pos, XYPos size, int button_size = 32);
    void render_number(unsigned num, XYPos pos, XYPos siz);
    void render_region_bubble(RegionType type, unsigned colour, XYPos pos, unsigned siz);
    void render_region_type(RegionType reg, XYPos pos, unsigned siz);
    void render(bool saving = false);
    void grid_click(XYPos pos, bool right);
    void left_panel_click(XYPos pos, bool right);
    void right_panel_click(XYPos pos, bool right);

    bool events();
};

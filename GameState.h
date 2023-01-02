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
extern bool IS_PLAYTEST;

struct ServerResp
{
    SaveObject* resp = NULL;
    bool done = false;
    bool error = false;
    SDL_SpinLock working = 0;
};

class GameState
{
public:
    SDL_Window* sdl_window;
    SDL_Renderer* sdl_renderer;
    SDL_Texture* sdl_texture;
    TTF_Font *font;
    SaveObjectMap* lang_data;

    static const int tut_texture_count = 4;
    SDL_Texture* tutorial_texture[tut_texture_count] = {};

    std::string steam_session_string;
    bool achievement[10] = {};
    std::string steam_username = "dummy";
    uint64_t steam_id = 0;
    std::string language = "English";

    Grid *grid;
    std::list<GridRule> rules;

    enum {
        DISPLAY_MODE_NORMAL,
        DISPLAY_MODE_HELP,
        DISPLAY_MODE_LANGUAGE

    } display_mode = DISPLAY_MODE_NORMAL;


    XYPos mouse;
    enum {
        MOUSE_MODE_NONE,
        MOUSE_MODE_FILTER,
        }
        mouse_mode = MOUSE_MODE_NONE;

    enum {
        RIGHT_MENU_NONE,
        RIGHT_MENU_REGION,
        RIGHT_MENU_RULE_GEN,
        RIGHT_MENU_RULE_INSPECT,
        }
        right_panel_mode = RIGHT_MENU_NONE;

    int tutorial_index = 0;

    GridRegion *rule_gen_region[4] = {};
    GridRule constructed_rule;
    RegionType region_type = RegionType(RegionType::SET, 0);
    int region_menu = 0;
    const RegionType menu_region_types1[5] = {  RegionType(RegionType::EQUAL, 1),
                                                RegionType(RegionType::MORE, 1),
                                                RegionType(RegionType::XOR2, 0),
                                                RegionType(RegionType::XOR22, 0),
                                                RegionType(RegionType::VISIBILITY, 0)};
    const RegionType menu_region_types2[5] = {  RegionType(RegionType::NOTEQUAL, 1),
                                                RegionType(RegionType::LESS, 1),
                                                RegionType(RegionType::XOR3, 0),
                                                RegionType(RegionType::XOR222, 0),
                                                RegionType(RegionType::NONE, 0)};


    bool constructed_rule_is_logical = false;
    bool constructed_rule_is_already_present = false;

    GridRegion *inspected_region = NULL;
    GridRegionCause inspected_rule;
    bool rule_gen_target_square_count = false;
    XYSet filter_pos;

    int grid_size;
    XYPos panel_size;
    int square_size = 1;
    int button_size = 1;
    XYPos left_panel_offset;
    XYPos right_panel_offset;
    XYPos grid_offset;
    bool full_screen = false;
    std::string tooltip_string = "";

    bool skip_level = false;
    int cooldown = 0;
    int completed_count = 0;

    unsigned server_timeout = 0;

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
        unsigned count_todo = 0;
        std::vector<bool> level_status;
    };

    std::vector<LevelProgress> level_progress;

    unsigned current_level_set_index = 0;
    unsigned current_level_index = 0;
    bool current_level_is_temp = true;


    GameState(std::ifstream& loadfile);
    SaveObject* save(bool lite = false);
    void save(std::ostream& outfile, bool lite = false);
    ~GameState();
    void post_to_server(SaveObject* send, bool sync);
    void fetch_from_server(SaveObject* send, ServerResp* resp);
    void save_to_server(bool sync);
    SDL_Texture* loadTexture(const char* filename);

    void advance(int steps);
    void audio();

    GridRule rule_from_rule_gen_region();
    void reset_rule_gen_region();
    void update_constructed_rule();
    unsigned taken_to_size(unsigned total);
    XYPos taken_to_pos(unsigned count, unsigned total);
    void render_region_bg(GridRegion& region, std::map<XYPos, int>& taken, std::map<XYPos, int>& total_taken);
    void render_region_fg(GridRegion& region, std::map<XYPos, int>& taken, std::map<XYPos, int>& total_taken);
    void render_text_box(XYPos pos, std::string& s, bool left = false);
    void render_tooltip();
    void add_tooltip(SDL_Rect& dst_rect, const char* text);
    void render_box(XYPos pos, XYPos size, int corner_size, int style = 0);
    void render_number(unsigned num, XYPos pos, XYPos siz);
    void render_number_string(std::string str, XYPos pos, XYPos siz);
    void render_region_bubble(RegionType type, unsigned colour, XYPos pos, unsigned siz);
    void render_region_type(RegionType reg, XYPos pos, unsigned siz);
    void render(bool saving = false);
    void grid_click(XYPos pos, bool right);
    void left_panel_click(XYPos pos, bool right);
    void right_panel_click(XYPos pos, bool right);

    bool events();
};

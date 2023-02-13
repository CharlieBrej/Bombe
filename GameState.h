#pragma once
#include "Misc.h"
#include "SaveState.h"
#include "Grid.h"
#include "LevelSet.h"

#include <SDL.h>
#include <SDL_image.h>
#include <SDL_mixer.h>
#include <SDL_ttf.h>

#include <map>
#include <list>
#include <set>
#include <algorithm>
#include <iterator>

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
    static const int game_version = 7;
    SDL_Window* sdl_window;
    SDL_Renderer* sdl_renderer;
    SDL_Texture* sdl_texture;
    std::map<std::string, TTF_Font*> fonts;
    TTF_Font *font = NULL;
    TTF_Font *score_font = NULL;
    SaveObjectMap* lang_data;
    unsigned frame = 0;

    static const int tut_texture_count = 5;
    SDL_Texture* tutorial_texture[tut_texture_count] = {};

    std::string steam_session_string;
    bool achievement[10] = {};
    std::string steam_username = "dummy";
    uint64_t steam_id = 0;
    std::set<uint64_t> steam_friends;
    std::string language = "English";

    Grid *grid;
    std::list<GridRule> rules;

    bool display_menu = false;
    bool display_help = false;
    bool display_language_chooser = false;
    bool display_reset_confirm = false;
    bool display_reset_confirm_levels_only = false;
    bool display_rules = false;
    bool display_scores = false;

    char key_held = 0;

    bool dragging_speed = false;
    double speed_dial = 0.25;

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
    int rules_list_offset = 0;
    bool display_rules_click = false;
    XYPos display_rules_click_pos;
    bool display_rules_click_drag = false;
    int display_rules_sort_col = 0;
    bool display_rules_sort_dir = true;

    struct ConstructedRuleState
    {
        GridRule rule;
        GridRegion* regions[4] = {};
        ConstructedRuleState(GridRule& rule_, GridRegion* regions_[4])
        {
            rule = rule_;
            std::copy(regions_, regions_ + 4, regions);
        }
    };

    GridRegion *rule_gen_region[4] = {};
    GridRule constructed_rule;
    GridRule* replace_rule = NULL;

    std::list<ConstructedRuleState> constructed_rule_undo;
    std::list<ConstructedRuleState> constructed_rule_redo;

    
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
    GridRule* constructed_rule_is_already_present = NULL;

    GridRegion* mouse_hover_region = NULL;
    GridRegion *inspected_region = NULL;
    GridRegionCause inspected_rule;
    bool rule_gen_target_square_count = false;
    XYSet filter_pos;

    int grid_size;
    XYPos panel_size;
    XYPos grid_pitch;
    int button_size = 1;
    XYPos left_panel_offset;
    XYPos right_panel_offset;
    XYPos grid_offset;
    int grid_zoom = 0;
    bool grid_dragging = false;
    XYPos grid_dragging_last_pos;
    XYPos scaled_grid_offset;
    int scaled_grid_size;

    EdgePos best_edge_pos;

    bool full_screen = false;
    std::string tooltip_string = "";

    bool skip_level = false;
    int cooldown = 0;
    double steps_had = 0;

    unsigned server_timeout = 0;

    bool last_active_was_hit = false;

    GridVisLevel vis_level = GRID_VIS_LEVEL_SHOW;
    GridVisLevel vis_mode = GRID_VIS_LEVEL_HIDE;
    bool xor_is_3 = false;

    bool show_row_clues = true;

    bool auto_region = true;
    bool get_hint = false;
    std::set<XYPos> clue_solves;

    std::list<Grid*> levels;
    ServerResp scores_from_server;

    class LevelProgress
    {
    public:
        unsigned count_todo = 0;
        std::vector<bool> level_status;
    };

    std::vector<LevelProgress> level_progress[GLBAL_LEVEL_SETS];
    class PlayerScore
    {
    public:
        unsigned pos = 0;
        std::string name;
        unsigned score;
        unsigned is_friend;
        unsigned hidden;
        PlayerScore(unsigned pos_, std::string name_, unsigned score_, unsigned is_friend_, unsigned hidden_):
            pos(pos_), name(name_), score(score_), is_friend(is_friend_), hidden(hidden_)
        {}
    };

    std::vector<PlayerScore> score_tables[GLBAL_LEVEL_SETS];

    unsigned current_level_group_index = 1;
    unsigned current_level_set_index = 0;
    unsigned current_level_index = 0;
    bool current_level_is_temp = true;


    GameState(std::string& lost_data, bool json);
    SaveObject* save(bool lite = false);
    void save(std::ostream& outfile, bool lite = false);
    ~GameState();
    bool level_is_accessible(unsigned set);
    void post_to_server(SaveObject* send, bool sync);
    void fetch_from_server(SaveObject* send, ServerResp* resp);
    void fetch_scores();
    SDL_Texture* loadTexture(const char* filename);

    void advance(int steps);
    void audio();
    void set_language(std::string lang);

    GridRule rule_from_rule_gen_region();
    void rule_gen_undo();
    void rule_gen_redo();
    void reset_rule_gen_region();
    void update_constructed_rule_pre();
    void update_constructed_rule();
    void render_region_bg(GridRegion& region, std::map<XYPos, int>& taken, std::map<XYPos, int>& total_taken, int disp_type = 0);
    void render_region_fg(GridRegion& region, std::map<XYPos, int>& taken, std::map<XYPos, int>& total_taken, int disp_type = 0);
    void render_text_box(XYPos pos, std::string& s, bool left = false);
    std::string translate(std::string s);
    void render_tooltip();
    void add_tooltip(SDL_Rect& dst_rect, const char* text);
    void render_box(XYPos pos, XYPos size, int corner_size, int style = 0);
    void render_number(unsigned num, XYPos pos, XYPos siz);
    void render_number_string(std::string str, XYPos pos, XYPos siz);
    void render_region_bubble(RegionType type, unsigned colour, XYPos pos, unsigned siz, bool selected = false);
    void render_region_type(RegionType reg, XYPos pos, unsigned siz);
    void render(bool saving = false);
    void grid_click(XYPos pos, int clicks, int btn);
    void left_panel_click(XYPos pos, int clicks, int btn);
    void right_panel_click(XYPos pos, int clicks, int btn);
    bool events();
    void deal_with_scores();
};

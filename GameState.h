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
    int frame_step = 0;

    static const int tut_texture_count = 5;
    SDL_Texture* tutorial_texture[tut_texture_count] = {};

    std::string steam_session_string;
    std::set <std::string> achievements;
    std::string steam_username = "dummy";
    uint64_t steam_id = 0;
    std::set<uint64_t> steam_friends;
    std::string language = "English";
    Mix_Chunk* sounds[8];
    int sound_frame_index = 0;

    static const int GAME_MODES = 4;
    int game_mode = 0;

    Grid *grid;
    std::list<GridRule> rules[GAME_MODES];

    bool display_menu = false;
    bool display_help = false;
    bool display_language_chooser = false;
    bool display_reset_confirm = false;
    bool display_reset_confirm_levels_only = false;
    bool display_rules = false;
    bool display_clipboard_rules = false;
    bool display_scores = false;
    bool display_modes = false;

    char key_held = 0;

    bool dragging_speed = false;
    bool dragging_volume = false;
    double speed_dial = 0.25;
    double volume = 0.50;

    XYPos mouse;
    enum {
        MOUSE_MODE_NONE,
        MOUSE_MODE_FILTER,
        }
        mouse_mode = MOUSE_MODE_NONE;
    SDL_SystemCursor mouse_cursor = SDL_SYSTEM_CURSOR_ARROW;
    SDL_SystemCursor prev_mouse_cursor = SDL_SYSTEM_CURSOR_ARROW;
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
    int display_rules_sort_col_2nd = 1;
    bool display_rules_sort_dir_2nd = true;
    bool display_rules_level = false;

    struct AnimationStarBurst
    {
        XYPos pos;
        XYPos size;
        int progress;
        bool lock;
        AnimationStarBurst (XYPos pos_, XYPos size_, int progress_, bool lock_):
            pos(pos_),
            size(size_),
            progress(progress_),
            lock(lock_)
            {}
    };
    std::list<AnimationStarBurst> star_burst_animations;

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

    std::map<XYPos, int> grid_cells_animation;
    std::map<GridRegion*, int> grid_regions_animation;
    std::map<GridRegion*, int> grid_regions_fade;

    std::list<ConstructedRuleState> constructed_rule_undo;
    std::list<ConstructedRuleState> constructed_rule_redo;

    int clipboard_check = 0;
    enum {
        CLIPBOARD_HAS_NONE,
        CLIPBOARD_HAS_RULE,
        CLIPBOARD_HAS_RULE_SET
        }
        clipboard_has_item = CLIPBOARD_HAS_NONE;

    std::string clipboard_last;
    GridRule clipboard_rule;
    std::list<GridRule> clipboard_rule_set;

    
    RegionType region_type = RegionType(RegionType::SET, 0);
    RegionType select_region_type = RegionType(RegionType::EQUAL, 0);
    const RegionType::Type menu_region_types[2][5] = {{RegionType::EQUAL, RegionType::MORE, RegionType::XOR2, RegionType::XOR22, RegionType::NONE},
                                                        {RegionType::NOTEQUAL, RegionType::LESS, RegionType::XOR3, RegionType::XOR222, RegionType::NONE}};

    GridRule::IsLogicalRep constructed_rule_is_logical = GridRule::OK;
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
    double grid_zoom = 1;
    double target_grid_zoom = 1;
    bool grid_dragging = false;
    XYPos grid_dragging_last_pos;
    XYPos scaled_grid_offset;
    int scaled_grid_size = 1;

    struct WrapPos
    {
        XYPos pos;
        double size;
    };

    EdgePos best_edge_pos;

    bool full_screen = false;
    std::string tooltip_string = "";
    XYRect tooltip_rect;

    int skip_level = 0;
    bool load_level = true;
    bool auto_progress = false;
    bool auto_progress_all = false;
    double steps_had = 0;

    unsigned server_timeout = 0;

    bool last_active_was_hit = false;

    GridVisLevel vis_level = GRID_VIS_LEVEL_SHOW;
    GridVisLevel vis_mode = GRID_VIS_LEVEL_HIDE;

    bool show_row_clues = true;

    bool get_hint = false;
    std::set<XYPos> clue_solves;

    ServerResp scores_from_server;

    class LevelProgress
    {
    public:
        unsigned count_todo = 0;
        std::vector<bool> level_status;
        std::vector<uint64_t> level_stats;
    };
    std::vector<std::vector<std::string>> server_levels;
    int server_levels_version = 0;

    std::vector<LevelProgress> level_progress[GAME_MODES][GLBAL_LEVEL_SETS + 1];
    int max_stars = 0;
    int cur_stars = 0;

    enum {
        PROG_LOCK_HEX,
        PROG_LOCK_SQUARE,
        PROG_LOCK_TRIANGLE,
        PROG_LOCK_GRID,
        PROG_LOCK_SERVER,

        PROG_LOCK_NUMBER_TYPES,
        PROG_LOCK_VISIBILITY,
        PROG_LOCK_VISIBILITY2,
        PROG_LOCK_VISIBILITY3,
        PROG_LOCK_VISIBILITY4,

        PROG_LOCK_GAME_MODE,

        PROG_LOCK_VARS1,
        PROG_LOCK_VARS2,

        PROG_LOCK_FILTER,

        PROG_LOCK_TOTAL
        };
        
    int prog_stars[PROG_LOCK_TOTAL]= {};
    bool prog_seen[PROG_LOCK_TOTAL]= {};

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

    std::vector<PlayerScore> score_tables[GAME_MODES][GLBAL_LEVEL_SETS + 1];

    unsigned current_level_group_index = 0;
    unsigned current_level_set_index = 0;
    int current_level_index = 0;
    bool current_level_is_temp = true;

    SDL_mutex* level_gen_mutex;
    SDL_Thread* level_gen_thread = NULL;
    std::string level_gen_req;
    std::string level_gen_resp;


    GameState(std::string& lost_data, bool json);
    SaveObject* save(bool lite = false);
    void save(std::ostream& outfile, bool lite = false);
    ~GameState();
    void reset_levels();
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
    void render_region_bg(GridRegion& region, std::map<XYPos, int>& taken, std::map<XYPos, int>& total_taken, std::vector<WrapPos>& wraps, int disp_type = 0);
    void render_region_fg(GridRegion& region, std::map<XYPos, int>& taken, std::map<XYPos, int>& total_taken, std::vector<WrapPos>& wraps, int disp_type = 0);
    void render_text_box(XYPos pos, std::string& s, bool left = false);
    std::string translate(std::string s);
    void render_tooltip();
    void add_clickable_highlight(SDL_Rect& dst_rect);
    void add_tooltip(SDL_Rect& dst_rect, const char* text, bool clickable = true);
    void render_box(XYPos pos, XYPos size, int corner_size, int style = 0);
    void render_number(unsigned num, XYPos pos, XYPos siz);
    void render_number_string(std::string str, XYPos pos, XYPos siz, XYPos style = XYPos(0,0));
    void render_region_bubble(RegionType type, unsigned colour, XYPos pos, int siz, bool selected = false);
    void render_region_type(RegionType reg, XYPos pos, unsigned siz);
    bool render_lock(int lock_type, XYPos pos, int size);
    void render(bool saving = false);
    void grid_click(XYPos pos, int clicks, int btn);
    void left_panel_click(XYPos pos, int clicks, int btn);
    void right_panel_click(XYPos pos, int clicks, int btn);
    bool events();
    void deal_with_scores();
    void export_all_rules_to_clipboard();
    void send_to_clipboard(SaveObject* obj);
    void check_clipboard();
    void import_all_rules();

};

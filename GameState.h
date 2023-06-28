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
    static const int game_version = 8;
    SDL_Window* sdl_window;
    SDL_Renderer* sdl_renderer;
    SDL_Texture* sdl_texture;
    std::map<std::string, TTF_Font*> fonts;
    TTF_Font *font = NULL;
    TTF_Font *score_font = NULL;
    SaveObjectMap* lang_data;
    unsigned frame = 0;
    int frame_step = 0;
    bool pirate = false;

    static const int tut_texture_count = 7;
    SDL_Texture* tutorial_texture[tut_texture_count] = {};
    int tut_page_count = 3;
    SDL_Texture* overlay_texture;
    bool overlay_texture_is_clean = true;

    std::string steam_session_string;
    std::set <std::string> achievements;
    std::string steam_username = "dummy";
    uint64_t steam_id = 0;
    std::set<uint64_t> steam_friends;
    std::string language = "English";
    bool has_sound = true;
    Mix_Chunk* sounds[16] = {};
    int sound_frame_index = 0;
    int sound_success_round_robin = 0;
    Mix_Music *music;


    static const int GAME_MODES = 4;
    int game_mode = 0;

    Grid *grid;
    std::list<GridRule> rules[GAME_MODES];
    int rule_del_count[GAME_MODES] = {};

    bool display_menu = false;
    bool display_help = false;
    bool display_language_chooser = false;
    bool display_reset_confirm = false;
    bool display_reset_confirm_levels_only = false;
    bool display_key_select = false;
    bool display_about = false;
    bool display_debug = false;

    bool display_rules = false;
    bool display_clipboard_rules = false;
    bool display_scores = false;
    bool display_levels = false;
    bool display_levels_center_current = false;
    bool display_modes = false;

    bool low_contrast = false;
    uint8_t contrast = 255;

    char key_held = 0;

    uint8_t ctrl_held = 0;
    uint8_t shift_held = 0;


    enum{
        KEY_CODE_HELP,
        KEY_CODE_HINT,
        KEY_CODE_SKIP,
        KEY_CODE_REFRESH,
        KEY_CODE_FULL_SCREEN,
        KEY_CODE_G_VISIBLE,
        KEY_CODE_G_HIDDEN,
        KEY_CODE_G_TRASH,
        KEY_CODE_UNDO,
        KEY_CODE_REDO,

        KEY_CODE_DONT_CARE,
        KEY_CODE_CLEAR,
        KEY_CODE_BOMB,
        KEY_CODE_HIDE,
        KEY_CODE_TRASH,
        KEY_CODE_VAR1,
        KEY_CODE_VAR2,
        KEY_CODE_VAR3,
        KEY_CODE_VAR4,
        KEY_CODE_VAR5,
        KEY_CODE_0,
        KEY_CODE_1,
        KEY_CODE_2,
        KEY_CODE_3,
        KEY_CODE_4,
        KEY_CODE_5,
        KEY_CODE_6,
        KEY_CODE_7,
        KEY_CODE_8,
        KEY_CODE_9,
        KEY_CODE_EQUAL,
        KEY_CODE_NOTEQUAL,
        KEY_CODE_PLUS,
        KEY_CODE_MINUS,
        KEY_CODE_XOR3,
        KEY_CODE_XOR2,
        KEY_CODE_XOR22,
        KEY_CODE_PARITY,
        KEY_CODE_XOR1,
        KEY_CODE_XOR11,
        KEY_CODE_TOTAL,
        };
    int key_codes[KEY_CODE_TOTAL] = {SDLK_F1, SDLK_F2, SDLK_F3, SDLK_F4, SDLK_F11, SDLK_q, SDLK_w, SDLK_e, SDLK_z, SDLK_y,
                                     SDLK_QUESTION, SDLK_SPACE, SDLK_TAB, SDLK_COMMA, SDLK_PERIOD, SDLK_a, SDLK_s, SDLK_d, SDLK_f, SDLK_g,
                                     SDLK_0, SDLK_1, SDLK_2, SDLK_3, SDLK_4, SDLK_5, SDLK_6, SDLK_7, SDLK_8, SDLK_9,
                                     SDLK_EQUALS, SDLK_EXCLAIM, SDLK_PLUS, SDLK_MINUS, SDLK_x, SDLK_c, SDLK_v, SDLK_b, SDLK_n, SDLK_m
                                     };
    int capturing_key = -1;

    bool dragging_speed = false;
    bool dragging_scroller = false;
    enum {
        DRAGGING_SCROLLER_VOLUME,
        DRAGGING_SCROLLER_MUSIC,
        DRAGGING_SCROLLER_COLOUR,
        DRAGGING_SCROLLER_RULES,
        }
        dragging_scroller_type = DRAGGING_SCROLLER_VOLUME;

    double speed_dial = 0.25;
    double volume = 0.50;
    double music_volume = 0.50;
    double colors = 0.00;
    double rule_limit_slider = 1.00;
    int rule_limit_count = -1;

    XYPos mouse;
    enum {
        MOUSE_MODE_NONE,
        MOUSE_MODE_FILTER,
        MOUSE_MODE_PAINT,
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
    int key_remap_page_index = 0;

    int rules_list_offset = 0;
    bool display_rules_click = false;
    XYPos display_rules_click_pos;
    bool display_rules_click_drag = false;
    bool display_rules_click_line = false;

    int display_rules_sort_col = 0;
    bool display_rules_sort_dir = true;
    int display_rules_sort_col_2nd = 1;
    bool display_rules_sort_dir_2nd = true;
    bool display_rules_level = false;
    int display_levels_sort_col = 0;
    bool display_levels_sort_dir = true;
    int display_levels_sort_col_2nd = 1;
    bool display_levels_sort_dir_2nd = true;
    bool display_levels_level = false;

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
        CLIPBOARD_HAS_RULE_SET,
        CLIPBOARD_HAS_LEVEL
        }
        clipboard_has_item = CLIPBOARD_HAS_NONE;

    std::string clipboard_last;
    GridRule clipboard_rule;
    std::list<GridRule> clipboard_rule_set;
    std::string clipboard_level;

    
    RegionType region_type = RegionType(RegionType::SET, 0);
    RegionType select_region_type = RegionType(RegionType::EQUAL, 0);
    const RegionType::Type menu_region_types[2][5] = {{RegionType::EQUAL, RegionType::LESS, RegionType::XOR1, RegionType::XOR2, RegionType::XOR3},
                                                      {RegionType::NOTEQUAL, RegionType::MORE, RegionType::XOR11, RegionType::XOR22, RegionType::PARITY}};

    GridRule::IsLogicalRep constructed_rule_is_logical = GridRule::OK;
    GridRule rule_illogical_reason;
    GridRule* constructed_rule_is_already_present = NULL;

    GridRegion* mouse_hover_region = NULL;
    GridRegion *inspected_region = NULL;
    GridRegionCause inspected_rule;
    std::set<GridRule*> selected_rules;
    bool rule_gen_target_square_count = false;
    XYSet filter_pos;

    XYPos window_size;
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
    int grid_dragging_btn;
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
    bool force_load_level = false;
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
        int star_anim_prog = 0;
        int unlock_anim_prog = 0;
        std::vector<bool> level_status;
        std::vector<uint64_t> level_stats;
    };
    std::vector<std::vector<std::string>> server_levels;
    int server_levels_version = 0;

    std::vector<LevelProgress> level_progress[GAME_MODES][GLBAL_LEVEL_SETS + 1];
    int max_stars = 0;
    int cur_stars = 0;

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

    enum{
        PROG_LOCK_HEX,
        PROG_LOCK_SQUARE,
        PROG_LOCK_TRIANGLE,
        PROG_LOCK_GRID,
        PROG_LOCK_SERVER,

        PROG_LOCK_DONT_CARE,
        PROG_LOCK_NUMBER_TYPES,
        PROG_LOCK_LEVELS_AND_LOCKS,
        PROG_LOCK_VISIBILITY,
        PROG_LOCK_VISIBILITY2,
        PROG_LOCK_VISIBILITY3,
        PROG_LOCK_VISIBILITY4,

        PROG_LOCK_GAME_MODE,

        PROG_LOCK_VARS1,
        PROG_LOCK_VARS2,
        PROG_LOCK_VARS3,
        PROG_LOCK_VARS4,
        PROG_LOCK_VARS5,

        PROG_LOCK_FILTER,
        PROG_LOCK_PRIORITY,
        PROG_LOCK_PRIORITY2,
        PROG_LOCK_PAUSE,
        PROG_LOCK_COLORS,
        PROG_LOCK_TABLES,
        PROG_LOCK_SPEED,

        PROG_LOCK_USE_DONT_CARE,
        PROG_LOCK_REGION_HINT,
        PROG_LOCK_DOUBLE_CLICK_HINT,
        PROG_LOCK_REGION_LIMIT,

        PROG_LOCK_TOTAL
        };

    static const int PROG_ANIM_MAX = 5000;
    int prog_stars[PROG_LOCK_TOTAL]= {};
    int prog_seen[PROG_LOCK_TOTAL]= {};
    int server_level_anim = PROG_ANIM_MAX;
    bool seen_ff = false;
    bool walkthrough = false;
    int walkthrough_step = 0;
    XYRect walkthrough_region;
    bool walkthrough_double_click = false;

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
    bool level_is_accessible(int mode, int group_index, int  set);
    void post_to_server(SaveObject* send, bool sync);
    void fetch_from_server(SaveObject* send, ServerResp* resp);
    void fetch_scores();
    void clear_overlay();
    SDL_Texture* loadTexture(const char* filename);

    bool rule_is_permitted(GridRule& rule, int mode);
    void load_grid(std::string s);
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
    void render_text_box(XYPos pos, std::string& s, bool left = false, int force_width = -1);
    std::string translate(std::string s);
    void render_tooltip();
    void add_clickable_highlight(SDL_Rect& dst_rect);
    bool add_tooltip(SDL_Rect& dst_rect, const char* text, bool clickable = true);
    void render_box(XYPos pos, XYPos size, int corner_size, int style = 0);
    void render_number(unsigned num, XYPos pos, XYPos siz);
    void render_number_string(std::string str, XYPos pos, XYPos siz, XYPos style = XYPos(0,0));
    void render_region_bubble(RegionType type, unsigned colour, XYPos pos, int siz, bool selected = false);
    void render_region_type(RegionType reg, XYPos pos, unsigned siz);
    void render_star_burst(XYPos pos, XYPos size, int progress, bool lock);
    bool render_lock(int lock_type, XYPos pos, XYPos size);
    void render_rule(GridRule& rule, XYPos pos, int size, int hover_rulemaker_region_base_index, bool reason = false);
    void render(bool saving = false);
    void grid_click(XYPos pos, int clicks, int btn);
    void left_panel_click(XYPos pos, int clicks, int btn);
    void right_panel_click(XYPos pos, int clicks, int btn);
    bool events();
    void deal_with_scores();
    void export_all_rules_to_clipboard();
    void send_to_clipboard(std::string title, SaveObject* obj);
    void check_clipboard();
    void send_rule_to_img_clipboard(GridRule& rule);
    void import_all_rules();

};

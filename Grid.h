#pragma once
#include "z3++.h"
#include "SaveState.h"
#include "Misc.h"
#include <map>
#include <set>
#include <list>

void grid_set_rnd(int a = 0);

class RegionType
{
public:
    enum Type
    {
        EQUAL,
        LESS,
        MORE,
        NONE,
    } type = EQUAL;
    int8_t value = 0;

    RegionType() : type (EQUAL), value(0) {}
    RegionType(char dummy,  unsigned t) : type (Type(t >> 8)), value(t & 255) {}
    RegionType(Type t, uint8_t v) : type (Type(t)), value(v) {}

    bool operator==(const RegionType& other) const
    {
        return (type == other.type) && (value == other.value);
    }

    unsigned as_int() const
    {
        return (int(type) << 8 | value);
    }
    z3::expr apply_z3_rule(z3::expr in);
};

class GridPlace
{
public:
    bool bomb;
    bool revealed;
    RegionType clue = RegionType(RegionType::NONE, 0);

    GridPlace(bool bomb_ = false, bool revealed_ = false) :
        bomb(bomb_),
        revealed(revealed_)
    {}
};
class GridRule;

class GridRegion
{
public:
    RegionType type;
    unsigned colour;
    unsigned fade = 0;
    bool hidden = false;
    bool visibility_forced = false;
    bool global = false;
    bool stale = false;
    std::set<XYPos> elements;
    GridRule* rule = NULL;

    bool overlaps(GridRegion& other);
    void reset(RegionType type);
    bool operator==(const GridRegion& other) const
    {
        return (type == other.type) && (elements == other.elements);
    }

};

class GridRule
{
public:
    uint8_t region_count = 0;
    RegionType region_type[3] = {};
    int8_t square_counts[8] = {};
    enum ApplyType
    {
        REGION,
        BOMB,
        CLEAR,
        HIDE,
        SHOW,
    } apply_type = REGION;
    RegionType apply_region_type;
    uint8_t apply_region_bitmap = 0;
    bool stale = false;


    GridRule(){};
    GridRule(SaveObject* sobj);
    SaveObject* save();
    bool matches(GridRule& other);
    void import_rule_gen_regions(GridRegion* r1, GridRegion* r2, GridRegion* r3);
    bool is_legal();
};

class Grid
{
public:
    XYPos size;
    bool count_revealed = true;

    int count_dec = 0;

    std::map<XYPos, GridPlace> vals;
    std::list<GridRegion> regions;
    std::list<GridRegion> regions_to_add;

    Grid(XYPos);
    Grid();
    void print(void);
    GridPlace get(XYPos p);
    void solve_easy();
    bool solve(int hard);
    bool is_solveable(bool use_high_count = false);
    void find_easiest_move(std::set<XYPos>& best_pos, int& hardness);
    XYPos find_easiest_move(int& hardness);
    void find_easiest_move(std::set<XYPos>& solves, Grid& needed);
    void find_easiest_move_using_regions(std::set<XYPos>& solves);
    int solve_complexity(XYPos p, std::set<XYPos> *needed = NULL);
    int solve_complexity(XYPos q, Grid& min_grid);

    bool is_determinable(XYPos q);
    bool is_determinable_using_regions(XYPos q, bool hidden = false);
    bool has_solution(void);
    void make_harder(void);
    void reveal(XYPos p);
    void reveal_switch(XYPos q);
    std::string to_string();
    void from_string(std::string s);
    bool is_solved(void);

    static GridRule rule_from_selected_regions(GridRegion* r1, GridRegion* r2, GridRegion* r3);
    bool add_regions(int level);

    enum ApplyRuleResp
    {
        APPLY_RULE_RESP_NONE,
        APPLY_RULE_RESP_HIT,
        APPLY_RULE_RESP_ERROR
    };

    ApplyRuleResp apply_rule(GridRule& rule, GridRegion* r1, GridRegion* r2, GridRegion* r3);
    ApplyRuleResp apply_rule(GridRule& rule);
    void add_new_regions();
    void add_one_new_region();

};

#pragma once
#include "z3++.h"
#include "SaveState.h"
#include "Misc.h"
#include <map>
#include <set>
#include <list>
#include <bitset>

void grid_set_rnd(int a = 0);

class XYSet
{
    static const unsigned WIDTH = 10;
    static const unsigned SIZE = WIDTH*WIDTH;
    std::bitset <SIZE> d;
public:
    XYSet(){}
    XYSet(const std::bitset<SIZE> d_) : d(d_){}

    unsigned p2i(XYPos p) { return p.x + p.y * WIDTH; }
    XYPos i2p(unsigned i) { return XYPos(i % WIDTH, i / WIDTH); }
    bool get(unsigned i) {return d[i];}
    bool get(XYPos p) {return get(p2i(p));}
    void set(unsigned i) {d[i] = 1;}
    void set(XYPos p) {set(p2i(p));}
    void clear(unsigned i) {d[i] = 0;}
    void clear(XYPos p) {clear(p2i(p));}
    unsigned count() {return d.count();}
    bool any() {return d.any();}
    bool none() {return d.none();}
    XYPos first() {if (get(0)) return XYPos(0,0); else return next(XYPos(0,0));}
    XYPos next(XYPos p) {unsigned i = p2i(p); do {i++; if (i>= SIZE) return XYPos(-1,-1);} while(!get(i)); return i2p(i);}
    void clear() { d.reset(); }
    void insert(XYPos p) { set(p); }

    bool contains(XYPos p) {return get(p);}
    bool empty() {return d.none();}
    bool operator==(const XYSet& other) const { return (d == other.d); }
    XYSet operator~() const {return XYSet(~d); }
    XYSet operator&(const XYSet other) const {return XYSet(d & other.d); }
    XYSet operator|(const XYSet other) const {return XYSet(d | other.d); }
    bool overlaps(const XYSet other) const {return (d & other.d).any(); }


};

#define FOR_XY_SET(NAME, SET) for (XYPos NAME = SET.first(); NAME.x >= 0; NAME = SET.next(NAME))


class RegionType
{
public:
    enum Type
    {
        NONE,
        EQUAL,
        LESS,
        MORE,
        XOR2,
        XOR3,
        XOR22,
        XOR222,
        NOTEQUAL,
        BOMB = 100,
        CLEAR = 101,
        VISIBILITY = 200,
    } type = NONE;
    int8_t value = 0;


    RegionType() : type (NONE), value(0) {}
    RegionType(char dummy, unsigned t) : type (Type(t >> 8)), value(t & 255) {}
    RegionType(Type t, uint8_t v) : type (Type(t)), value(v) {}

    bool operator==(const RegionType& other) const { return (type == other.type) && (value == other.value); }
    bool operator!=(const RegionType& other) const { return !(*this == other); }
    unsigned as_int() const { return (int(type) << 8 | value); }

    template<class RESP, class IN> RESP apply_rule(IN in);
    z3::expr apply_z3_rule(z3::expr in);
    bool apply_int_rule(unsigned in);

    int max();
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

enum GridVisLevel{
    GRID_VIS_LEVEL_SHOW,
    GRID_VIS_LEVEL_HIDE,
    GRID_VIS_LEVEL_BIN,
};

class GridRegion;
class GridRegionCause
{
public:
    GridRule* rule = NULL;
    GridRegion* regions[4] = {};
    GridRegionCause(){};
    GridRegionCause(GridRule* rule_, GridRegion* r1, GridRegion* r2, GridRegion* r3, GridRegion* r4)
    {
        rule = rule_;
        regions[0] = r1;
        regions[1] = r2;
        regions[2] = r3;
        regions[3] = r4;
    }
};


class GridRegion
{
public:
    RegionType type;
    unsigned colour;
    unsigned fade = 0;
    GridVisLevel vis_level = GRID_VIS_LEVEL_SHOW;
    enum
    {
        VIS_FORCE_NONE,
        VIS_FORCE_HINT,
        VIS_FORCE_USER,
    } visibility_force = VIS_FORCE_NONE;
    bool stale = false;
    XYSet elements;

    GridRegionCause gen_cause;
    GridRegionCause vis_cause;

    GridRegion(RegionType type);
    bool overlaps(GridRegion& other);
    bool contains_all(std::set<XYPos>& other);
    bool operator==(const GridRegion& other) const
    {
        return (type == other.type) && (elements == other.elements);
    }

};

class GridRule
{
public:
    uint8_t region_count = 0;
    RegionType region_type[4] = {};
    RegionType square_counts[16] = {};
    enum ApplyType
    {
        REGION,
        BOMB,
        CLEAR,
        HIDE,
        SHOW,
        BIN,
    } apply_type = REGION;
    RegionType apply_region_type;
    uint16_t apply_region_bitmap = 0;
    bool stale = false;
    bool deleted = false;


    GridRule(){};
    GridRule(SaveObject* sob, int version);
    SaveObject* save();
    static void get_square_counts(uint8_t square_counts[16], GridRegion* r1, GridRegion* r2, GridRegion* r3, GridRegion* r4);

    bool matches(GridRule& other);
    bool matches(GridRegion* r1, GridRegion* r2, GridRegion* r3, GridRegion* r4);
    void import_rule_gen_regions(GridRegion* r1, GridRegion* r2, GridRegion* r3, GridRegion* r4);
    bool is_legal();
};

class Grid
{
public:
    XYPos size;

    std::map<XYPos, GridPlace> vals;
    std::map<XYPos, RegionType> edges;      //  X=0 - vertical, X=1 horizontal
    std::list<GridRegion> regions;
    std::list<GridRegion> regions_to_add;
    std::list<GridRegion> deleted_regions;

    Grid(XYPos);
    Grid();
    Grid(std::string s);
    void print(void);
    GridPlace get(XYPos p);
    RegionType& get_clue(XYPos p);

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
    void make_harder(bool plus_minus, bool x_y, bool misc, int row_col);
    void reveal(XYPos p);
    void reveal_switch(XYPos q);
    std::string to_string();
    bool is_solved(void);

    bool add_region(XYSet& elements, RegionType clue);
    bool add_regions(int level);

    enum ApplyRuleResp
    {
        APPLY_RULE_RESP_NONE,
        APPLY_RULE_RESP_HIT,
        APPLY_RULE_RESP_ERROR
    };

    ApplyRuleResp apply_rule(GridRule& rule, GridRegion* r1, GridRegion* r2, GridRegion* r3, GridRegion* r4);
    ApplyRuleResp apply_rule(GridRule& rule, bool force = false);
    void add_new_regions();
    void add_one_new_region();

};

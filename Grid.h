#pragma once
#include "z3++.h"
#include "SaveState.h"
#include "Misc.h"
#include <map>
#include <set>
#include <list>
#include <bitset>
#include <array>

void grid_set_rnd(int a = 0);

class XYSet
{
    static const unsigned WIDTH = 16;
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
    void flip(unsigned i) {d.flip(i);}
    void flip(XYPos p) {flip(p2i(p));}
    unsigned count() {return d.count();}
    bool any() {return d.any();}
    bool none() {return d.none();}
    XYPos first() {if (get(0)) return XYPos(0,0); else return next(XYPos(0,0));}
    XYPos next(XYPos p) {unsigned i = p2i(p); do {i++; if (i>= SIZE) return XYPos(-1,-1);} while(!get(i)); return i2p(i);}
    void clear() { d.reset(); }
    void insert(XYPos p) { set(p); }

    bool contains(XYPos p) {return get(p);}
    bool contains(XYSet p) {return !(p & ~*this).any();}
    bool empty() {return d.none();}
    bool operator==(const XYSet& other) const { return (d == other.d); }
    bool operator<(const XYSet& other) const
    {
        typedef std::array<uint64_t, (SIZE / 64)> AsArray;
        const AsArray a = *reinterpret_cast<const AsArray*>(this);
        const AsArray b = *reinterpret_cast<const AsArray*>(&other);
        bool rep = false;
        for (int i = (SIZE / 64) - 1; i >= 0 ; i--)
        {
            if (a[i] < b[i]) {rep = true; break; }
            if (a[i] > b[i]) {rep = false; break; }
        }
        return rep;
    }
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
        SET = 100,
        VISIBILITY = 101,
    } type = NONE;
    int8_t value = 0;


    RegionType() : type (NONE), value(0) {}
    RegionType(char dummy, unsigned t) : type (Type(t >> 8)), value(t & 255) {}
    RegionType(Type t, uint8_t v) : type (Type(t)), value(v) {}

    bool operator==(const RegionType& other) const { return (type == other.type) && (value == other.value); }
    bool operator!=(const RegionType& other) const { return !(*this == other); }
    bool operator<(const RegionType& other) const { return (type < other.type) || ((type == other.type) && (value < other.value)); }
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
    void reset()
    {
        rule = NULL;
        regions[0] = NULL;
        regions[1] = NULL;
        regions[2] = NULL;
        regions[3] = NULL;
    }
    bool operator==(const GridRegionCause& other) const
    {
        return ((rule == other.rule) && (regions[0] == other.regions[0]) && (regions[1] == other.regions[1]) && (regions[2] == other.regions[2]) && (regions[3] == other.regions[3]));
    }
    
};


class GridRegion
{
public:
    RegionType type;
    unsigned colour;
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
    bool operator<(const GridRegion& other) const
    {
        if (type < other.type) return true;
        if (other.type < type) return false;
        if (elements < other.elements) return true;
        return false;
    }
    void next_colour();

};

class GridRule
{
public:
    uint8_t region_count = 0;
    RegionType region_type[4] = {};
    RegionType square_counts[16] = {};
    RegionType apply_region_type;
    uint16_t apply_region_bitmap = 0;
    bool stale = false;
    bool deleted = false;
    unsigned used_count = 0;
    unsigned clear_count = 0;
    uint8_t sort_perm = 0;


    GridRule(){};
    GridRule(SaveObject* sob, int version);
    SaveObject* save();
    static void get_square_counts(uint8_t square_counts[16], GridRegion* r1, GridRegion* r2, GridRegion* r3, GridRegion* r4);

    GridRule permute(std::vector<int>& p);
    bool covers(GridRule& other);
    bool matches(GridRegion* r1, GridRegion* r2, GridRegion* r3, GridRegion* r4);
    void import_rule_gen_regions(GridRegion* r1, GridRegion* r2, GridRegion* r3, GridRegion* r4);
    bool is_legal();
    void remove_region(int index);
    RegionType get_region_sorted(int index);

};

struct RenderCmd
{
    XYRect src;
    XYRect dst;
    double angle = 0.0;
    XYPos center = XYPos(0,0);
    bool bg = false;
    RenderCmd (XYRect src_, XYRect dst_, bool bg_ = false): src(src_),dst(dst_),bg(bg_){}
    RenderCmd (XYRect src_, XYRect dst_, double angle_, XYPos center_): src(src_),dst(dst_), angle(angle_), center(center_) {}
};

struct EdgePos
{
    RegionType type;
    double angle;
    double pos;
    EdgePos (RegionType type_, double angle_, double pos_): type(type_),angle(angle_),pos(pos_){}
    EdgePos (){}
    bool operator==(const EdgePos& other) const
    {
        return (type == other.type) && (angle == other.angle) && (pos == other.pos);
    }
};


struct GridRegionCompare {
    bool operator()(GridRegion* const& lhs, GridRegion* const& rhs) const {
        return *lhs < *rhs;
    }
};

class Grid
{
public:
    XYPos size;
    bool wrapped = false;
    bool big_regions_to_add = false;
    std::map<XYPos, GridPlace> vals;
    std::map<XYPos, RegionType> edges;      //  X=0 - vertical, X=1 horizontal
    std::map<XYPos, XYPos> merged;
    std::list<GridRegion> regions;
    std::set<GridRegion*, GridRegionCompare> regions_set;
    std::list<GridRegion> regions_to_add;
    std::multiset<GridRegion*, GridRegionCompare> regions_to_add_multiset;
    std::list<GridRegion> deleted_regions;

protected:
    Grid();

public:
    virtual ~Grid(){};
    void randomize(XYPos size_, bool wrapped, int merged_count, int row_percent);
    void from_string(std::string s);

    static Grid* Load(std::string s);
    GridPlace get(XYPos p);
    RegionType& get_clue(XYPos p);

    virtual std::string to_string();
    virtual Grid* dup() = 0;
    virtual XYSet get_squares() = 0;
    virtual XYSet get_row(unsigned type, int index) = 0;
    virtual XYSet get_neighbors(XYPos p) = 0;
    virtual void get_row_types(std::vector<XYPos>& rep) = 0;
    virtual void get_edges(std::vector<EdgePos>& rep, XYPos grid_pitch) = 0;
    virtual XYPos get_square_from_mouse_pos(XYPos pos, XYPos grid_pitch) = 0;

    virtual XYPos get_grid_pitch(XYPos grid_size) = 0;
    virtual XYRect get_square_pos(XYPos pos, XYPos grid_pitch) = 0;
    virtual XYRect get_bubble_pos(XYPos pos, XYPos grid_pitch, unsigned index, unsigned total) = 0;
    virtual void render_square(XYPos pos, XYPos grid_pitch, std::vector<RenderCmd>& cmd) = 0;
    virtual void add_random_merged(int count) {}
    virtual XYPos get_base_square(XYPos p) {return p;}
    virtual XYPos get_wrapped_size(XYPos grid_pitch) = 0;

    void solve_easy();
    bool is_solveable();

    bool is_determinable(XYPos q);
    bool is_determinable_using_regions(XYPos q, bool hidden = false);
//    bool has_solution(void);
    void make_harder(bool plus_minus, bool x_y, bool misc);
    void reveal(XYPos p);
    bool is_solved(void);

    bool add_region(GridRegion& r, bool front = false);
    bool add_region(XYSet& elements, RegionType clue);
    bool add_regions(int level);

    enum ApplyRuleResp
    {
        APPLY_RULE_RESP_NONE,
        APPLY_RULE_RESP_HIT,
        APPLY_RULE_RESP_ERROR
    };

    ApplyRuleResp apply_rule(GridRule& rule, GridRegion* r1, GridRegion* r2, GridRegion* r3, GridRegion* r4);
    ApplyRuleResp apply_rule(GridRule& rule, GridRegion* region);
    ApplyRuleResp apply_rule(GridRule& rule, bool force = false);
    void remove_from_regions_to_add_multiset(GridRegion*);
    void add_new_regions();
    bool add_one_new_region();
    void clear_regions();
};

class LocalGrid
{
private:
    Grid* grid = NULL;
public:
    LocalGrid() {}
    LocalGrid(Grid& other) { grid = other.dup(); }
    ~LocalGrid() {delete grid;}
    void operator=(Grid& other) { delete grid; grid = other.dup(); }
    Grid& operator*() { return *grid; }
    Grid* operator->() { return grid; }
};

class SquareGrid : public Grid
{
public:
    SquareGrid() {}
    SquareGrid(std::string s) {from_string(s);}

    std::string to_string();
    Grid* dup() {return new SquareGrid(*this);}
    XYSet get_squares();
    XYSet get_row(unsigned type, int index);
    XYSet get_neighbors(XYPos p);
    void get_row_types(std::vector<XYPos>& rep);
    void get_edges(std::vector<EdgePos>& rep, XYPos grid_pitch);
    XYPos get_square_from_mouse_pos(XYPos pos, XYPos grid_pitch);

    XYPos get_grid_pitch(XYPos grid_size);
    XYRect get_square_pos(XYPos pos, XYPos grid_pitch);
    XYRect get_bubble_pos(XYPos pos, XYPos grid_pitch, unsigned index, unsigned total);
    void render_square(XYPos pos, XYPos grid_pitch, std::vector<RenderCmd>& cmd);
    void add_random_merged(int count);
    XYPos get_square_size(XYPos p);
    XYPos get_base_square(XYPos p);
    XYPos get_wrapped_size(XYPos grid_pitch);
};

class TriangleGrid : public Grid
{
public:
    TriangleGrid() {}
    TriangleGrid(std::string s) {from_string(s);}

    std::string to_string();
    Grid* dup() {return new TriangleGrid(*this);}
    XYSet get_squares();
    XYSet get_row(unsigned type, int index);
private:
    XYSet base_get_neighbors(XYPos pos);
public:
    XYSet get_neighbors(XYPos p);
    void get_row_types(std::vector<XYPos>& rep);
    void get_edges(std::vector<EdgePos>& rep, XYPos grid_pitch);
    XYPos get_square_from_mouse_pos(XYPos pos, XYPos grid_pitch);

    XYPos get_grid_pitch(XYPos grid_size);
    XYRect get_square_pos(XYPos pos, XYPos grid_pitch);
    XYRect get_bubble_pos(XYPos pos, XYPos grid_pitch, unsigned index, unsigned total);
    void render_square(XYPos pos, XYPos grid_pitch, std::vector<RenderCmd>& cmd);
    void add_random_merged(int count);
    XYPos get_square_size(XYPos p);
    XYPos get_base_square(XYPos p);
    XYPos get_wrapped_size(XYPos grid_pitch);
};

class HexagonGrid : public Grid
{
public:
    HexagonGrid() {}
    HexagonGrid(std::string s) {from_string(s);}

    std::string to_string();
    Grid* dup() {return new HexagonGrid(*this);}
    XYSet get_squares();
    XYSet get_row(unsigned type, int index);
    XYSet get_neighbors(XYPos p);
    void get_row_types(std::vector<XYPos>& rep);
    void get_edges(std::vector<EdgePos>& rep, XYPos grid_pitch);
    XYPos get_square_from_mouse_pos(XYPos pos, XYPos grid_pitch);

    XYPos get_grid_pitch(XYPos grid_size);
    XYRect get_square_pos(XYPos pos, XYPos grid_pitch);
    XYRect get_bubble_pos(XYPos pos, XYPos grid_pitch, unsigned index, unsigned total);
    void render_square(XYPos pos, XYPos grid_pitch, std::vector<RenderCmd>& cmd);
    XYPos get_wrapped_size(XYPos grid_pitch);
};

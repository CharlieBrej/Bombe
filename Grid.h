#pragma once
#include "z3++.h"
#include "SaveState.h"
#include "Misc.h"
#include <map>
#include <set>
#include <list>
#include <bitset>
#include <array>

extern bool SHUTDOWN;

void grid_set_rnd(int a = 0);

void global_mutex_lock();
void global_mutex_unlock();

class XYSet
{
    static const unsigned WIDTH = 32;
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
    inline bool operator==(const XYSet& other) const { return (d == other.d); }
    inline bool operator<(const XYSet& other) const
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
    inline XYSet operator~() const {return XYSet(~d); }
    inline XYSet operator&(const XYSet other) const {return XYSet(d & other.d); }
    inline void operator&=(const XYSet other) {d &= other.d;}
    inline XYSet operator|(const XYSet other) const {return XYSet(d | other.d); }
    inline void operator|=(const XYSet other) {d |= other.d;}
    inline bool overlaps(const XYSet other) const {return (d & other.d).any(); }


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
        PARITY,
        XOR1,
        XOR11,
        SET = 100,
        VISIBILITY = 101,
    } type = NONE;
    int8_t value = 0;
    uint8_t var = 0;


    RegionType() : type (NONE), value(0) {}
    RegionType(char dummy, unsigned t) : type (Type((t >> 8) & 0xFF)), value(t & 255), var((t >> 16) & 0xFF) {}
    RegionType(Type t, uint8_t v) : type (Type(t)), value(v) {}
    static int type_priority(Type t)
    {
        static const int pri[] = {12, 0, 3, 4, 7, 8, 9, 10, 1, 11, 5, 6};
        return (t <= XOR11) ? pri[t] : t;
    }

    bool operator==(const RegionType& other) const { return (type == other.type) && (value == other.value) && (var == other.var); }
    bool operator!=(const RegionType& other) const { return !(*this == other); }
    bool operator<(const RegionType& other) const { return (type_priority(type) < type_priority(other.type)) || ((type == other.type) && ((value < other.value) || ((value == other.value) && (var < other.var)))); }
    unsigned as_int() const { return (int(var) << 16 | int(type) << 8 | value); }
    std::string val_as_str(int offset = 0);

    template<class RESP, class IN, class OTHER> RESP apply_rule_imp(IN in, OTHER other);
    template<class RESP, class IN, class VAR_ARR> RESP apply_rule(IN in, VAR_ARR& vars);

    z3::expr apply_z3_rule(z3::expr in, z3::expr_vector& var_vect);

    bool apply_int_rule(unsigned in, int vars[32]);

    int max();
    int min();
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
    bool deleted = false;
    XYSet elements;

    XYPos gen_cause_pos;
    GridRegionCause gen_cause;
    GridRegionCause vis_cause;
    float priority = 0;

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
    bool has_ancestor(GridRegion* other, std::set<GridRegion*>& has, std::set<GridRegion*>& hasnt);

};

class GridRule
{
public:
    int8_t priority = 0;
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
    unsigned cpu_time = 0;


    GridRule(){};
    GridRule(SaveObject* sob);
    SaveObject* save(bool lite = false);
    static void get_square_counts(uint8_t square_counts[16], GridRegion* r1, GridRegion* r2, GridRegion* r3, GridRegion* r4);

    class FastOp
    {
    public:
        enum OpType
        {
            REG_TYPE,
            CELL_COUNT,
            VAR_ADD,
            VAR_SUB,
            VAR_TRIPLE,
            MIN_CELL_COUNT,
        };
        OpType op;
        bool set = true;
        uint8_t vi;
        uint8_t p1;
        uint8_t p2;
        uint8_t p3;
        FastOp(OpType op_, bool set_, uint8_t vi_, uint8_t p1_, uint8_t p2_ = 0, uint8_t p3_ = 0):
            op(op_), set(set_), vi(vi_), p1(p1_), p2(p2_), p3(p3_)
        {}
        FastOp() {}
    };

    class FastOpGroup
    {
    public:
        std::vector<GridRule::FastOp> ops[4];
    };

    GridRule permute(std::vector<int>& p);
    bool covers(GridRule& other);
    void jit_preprocess_calc(std::vector<GridRule::FastOp>& fast_ops, bool have[32]);
    void jit_preprocess(FastOpGroup& fast_ops);
    bool jit_matches(std::vector<GridRule::FastOp>& fast_ops, bool final, GridRegion* r1, GridRegion* r2, GridRegion* r3, GridRegion* r4, int var_counts[32]);
    bool matches(GridRegion* r1, GridRegion* r2, GridRegion* r3, GridRegion* r4, int var_counts[32]);
    void import_rule_gen_regions(GridRegion* r1, GridRegion* r2, GridRegion* r3, GridRegion* r4);
    typedef enum {OK, ILLOGICAL, LOSES_DATA, IMPOSSIBLE, UNBOUNDED, LIMIT} IsLogicalRep;
    IsLogicalRep is_legal(GridRule& why);
    void remove_region(int index);
    void add_region(RegionType type);
    void resort_region();
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
    XYPos rule_pos;
    RegionType type;
    double angle;
    double pos;
    EdgePos (XYPos _rule_pos, RegionType type_, double angle_, double pos_): rule_pos(_rule_pos),type(type_),angle(angle_),pos(pos_){}
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
    enum WrapType
    {
        WRAPPED_NOT,
        WRAPPED_SIDE,
        WRAPPED_IN,
    } wrapped = WRAPPED_NOT;
    bool big_regions_to_add = false;
    std::map<XYPos, GridPlace> vals;
    std::map<XYPos, RegionType> edges;      //  X=0 - vertical, X=1 horizontal
    std::map<XYPos, XYPos> merged;
    std::map<XYPos, GridRegionCause> cell_causes;
    XYPos innie_pos = XYPos(1,1);
    std::list<GridRegion> regions;
    std::set<GridRegion*, GridRegionCompare> regions_set;
    std::list<GridRegion> regions_to_add;
    std::multiset<GridRegion*, GridRegionCompare> regions_to_add_multiset;
    std::list<GridRegion> deleted_regions;
    XYSet last_cleared_regions;
    std::map<GridRule*, int> level_used_count;
    std::map<GridRule*, int> level_clear_count;

protected:
    Grid();

public:
    virtual ~Grid(){};
    void randomize(XYPos size_, WrapType wrapped, int merged_count, int row_percent);
    void from_string(std::string s);

    static Grid* Load(std::string s);
    GridPlace get(XYPos p);
    RegionType& get_clue(XYPos p);

    virtual std::string text_desciption() = 0;
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
    virtual XYRect get_icon_pos(XYPos pos, XYPos grid_pitch) = 0;
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
    void make_harder(int plus_minus, int x_y, int x_y3, int x_y_z, int exc, int parity, int xor1, int xor11);
    void reveal(XYPos p);
    bool is_solved(void);

    bool add_region(GridRegion& r, bool front = false);
    bool add_region(XYSet& elements, RegionType clue, XYPos cause);
    void add_base_regions(void);

    enum ApplyRuleResp
    {
        APPLY_RULE_RESP_NONE,
        APPLY_RULE_RESP_HIT
    };

    ApplyRuleResp apply_rule(GridRule& rule, GridRegion* regions[4], int var_counts[32]);
    ApplyRuleResp apply_rule(GridRule& rule, GridRegion* region, bool always_ignore_bin = false);
//    ApplyRuleResp apply_rule(GridRule& rule, bool force = false);
    void remove_from_regions_to_add_multiset(GridRegion*);
    void add_new_regions();
    GridRegion* add_one_new_region(GridRegion* r);
    void clear_regions();
    void commit_level_counts();
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

    std::string text_desciption();
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
    XYRect get_icon_pos(XYPos pos, XYPos grid_pitch);
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

    bool is_inside(XYPos p);

    std::string text_desciption();
    std::string to_string();
    Grid* dup() {return new TriangleGrid(*this);}
    XYSet get_squares();
    XYSet get_row(unsigned type, int index);
private:
    XYSet base_get_neighbors_of_point(XYPos pos);
    XYSet get_neighbors_of_point(XYPos pos);
    XYSet base_get_neighbors(XYPos pos);
public:
    XYSet get_neighbors(XYPos p);
    void get_row_types(std::vector<XYPos>& rep);
    void get_edges(std::vector<EdgePos>& rep, XYPos grid_pitch);
    XYPos get_square_from_mouse_pos(XYPos pos, XYPos grid_pitch);

    XYPos get_grid_pitch(XYPos grid_size);
    XYRect get_square_pos(XYPos pos, XYPos grid_pitch);
    XYRect get_icon_pos(XYPos pos, XYPos grid_pitch);
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

    std::string text_desciption();
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
    XYRect get_icon_pos(XYPos pos, XYPos grid_pitch);
    XYRect get_bubble_pos(XYPos pos, XYPos grid_pitch, unsigned index, unsigned total);
    void render_square(XYPos pos, XYPos grid_pitch, std::vector<RenderCmd>& cmd);
    XYPos get_wrapped_size(XYPos grid_pitch);
};

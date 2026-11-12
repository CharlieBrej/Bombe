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

    unsigned p2i(XYPos p) const { return p.x + p.y * WIDTH; }
    XYPos i2p(unsigned i) const { return XYPos(i % WIDTH, i / WIDTH); }
    bool get(unsigned i) const {return d[i];}
    bool get(XYPos p) const {return get(p2i(p));}
    void set(unsigned i) {d[i] = 1;}
    void set(XYPos p) {set(p2i(p));}
    void clear(unsigned i) {d[i] = 0;}
    void clear(XYPos p) {clear(p2i(p));}
    void flip(unsigned i) {d.flip(i);}
    void flip(XYPos p) {flip(p2i(p));}
    unsigned count() const {return d.count();}
    bool any() const {return d.any();}
    bool none() const {return d.none();}
    XYPos first() const {if (get(0)) return XYPos(0,0); else return next(XYPos(0,0));}
    XYPos next(XYPos p) const {unsigned i = p2i(p); do {i++; if (i>= SIZE) return XYPos(-1,-1);} while(!get(i)); return i2p(i);}
    void clear() { d.reset(); }
    void insert(XYPos p) { set(p); }

    bool contains(XYPos p) const {return get(p);}
    bool contains(XYSet p) const {return !(p & ~*this).any();}
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
    inline XYSet operator&(const XYSet &other) const {return XYSet(d & other.d); }
    inline void operator&=(const XYSet &other) {d &= other.d;}
    inline XYSet operator|(const XYSet &other) const {return XYSet(d | other.d); }
    inline void operator|=(const XYSet &other) {d |= other.d;}
    inline bool overlaps(const XYSet &other) const {return (d & other.d).any(); }


};

#define FOR_XY_SET(NAME, SET) for (XYPos NAME = (SET).first(); NAME.x >= 0; NAME = (SET).next(NAME))


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
        PRIME,
        TRIANGLE,
        POW2,
        FIBONACCI,
        BOX,
        SET = 100,
        VISIBILITY = 101,
    };
    Type type = NONE;
    int value = 0;
    uint8_t var = 0;


    RegionType() : type (NONE), value(0) {}
    RegionType(char dummy, unsigned t) : type (Type((t >> 8) & 0xFF)), value(static_cast<int8_t>(t & 0xFF)), var((t >> 16) & 0xFF) {}
    RegionType(Type t, int v) : type (Type(t)), value(v) {}
    static int type_priority(Type t)
    {
        static const int pri[] = {12, 0, 3, 4, 7, 8, 9, 10, 1, 11, 5, 6};
        return (t <= XOR11) ? pri[t] : t;
    }

    bool operator==(const RegionType& other) const { return (type == other.type) && (value == other.value) && (var == other.var); }
    bool operator!=(const RegionType& other) const { return !(*this == other); }
    bool operator<(const RegionType& other) const { return (type_priority(type) < type_priority(other.type)) || ((type == other.type) && ((value < other.value) || ((value == other.value) && (var < other.var)))); }
    std::string val_as_str(int offset = 0) const;
    unsigned as_int() const { return ((unsigned(var) & 0xFF) << 16 | (unsigned(type) & 0xFF) << 8 | (unsigned(value) & 0xFF)); }
    bool mapper_based_equal(const RegionType& other, uint8_t mapper[32]);

    template<class RESP, class IN, class OTHER> RESP apply_rule_imp(IN in, OTHER other);
    template<class RESP, class IN, class VAR_ARR> RESP apply_rule(IN in, VAR_ARR& vars);

    z3::expr apply_z3_rule(z3::expr in, z3::expr_vector& var_vect);

    bool apply_int_rule(unsigned in);
    bool apply_int_rule(unsigned in, int vars[32]);

    int max() const;
};


class RegionIfType
{
public:
    enum IfPartition
    {
        ALTERNATING,
        SINGLE_CELL,
        ADJACENT_PAIR,
    } if_partition = ALTERNATING;
    uint8_t if_partition_index = 0;
    uint8_t if_partition_index2 = 0;
    RegionType if_type;
    RegionType type;

    RegionIfType() {}
    RegionIfType(RegionType _type): type(_type){}
    RegionIfType(RegionType::Type t, uint8_t v) : type(t, v) {}
    bool operator==(const RegionIfType& other) const {
        return type == other.type && if_type == other.if_type &&
               if_partition == other.if_partition &&
               if_partition_index == other.if_partition_index &&
               if_partition_index2 == other.if_partition_index2;
    }
    bool is_if_then() const {
        return if_type.type != RegionType::NONE;
    }
};


class GridPlace
{
public:
    bool bomb;
    bool revealed;
    bool negated = false;

    RegionIfType clue;;

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
    // Physical inputs are packed here. An
    // IF/THEN pair refers to one region.
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
    RegionType if_type;
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
    XYSet elements_neg;

    XYPos gen_cause_pos;
    GridRegionCause gen_cause;
    GridRegionCause vis_cause;
    float priority = 0;

    GridRegion(RegionIfType type);
    bool overlaps(GridRegion& other);
    bool operator==(const GridRegion& other) const
    {
        return (type == other.type) && (if_type == other.if_type) && (elements == other.elements) && (elements_neg == other.elements_neg);
    }
    bool operator<(const GridRegion& other) const
    {
        if (type < other.type) return true;
        if (other.type < type) return false;
        if (if_type < other.if_type) return true;
        if (other.if_type < if_type) return false;
        if (elements < other.elements) return true;
        if (other.elements < elements) return false;
        if (elements_neg < other.elements_neg) return true;
        return false;
    }
    void next_colour();
    bool has_ancestor(GridRegion* other, std::set<GridRegion*>& has, std::set<GridRegion*>& hasnt);

    bool matches_filters(const XYSet& _and, const XYSet& _not) const {
        if (!is_if_then())
            return (_and.none() || elements.contains(_and)) &&
                (_not.none() || !(elements & _not).any());
        const XYSet filter_elements =
            elements | elements_neg;
        return (_and.none() || filter_elements.contains(_and)) &&
            (_not.none() || !(filter_elements & _not).any());
    }
    bool isHintable() const {
        return visibility_force == GridRegion::VIS_FORCE_NONE && vis_level == GRID_VIS_LEVEL_SHOW;
    }
    bool is_if_then() const {
        return if_type.type != RegionType::NONE;
    }
};

class GridRule
{
public:
    int8_t priority = 0;
    bool paused = false;
    uint8_t region_count = 0;
    uint8_t neg_reg_count = 0;
    uint8_t if_reg_count = 0;
    RegionType region_type[4] = {};
    RegionType square_counts[16] = {};
    RegionType apply_if_region_type;
    RegionType apply_region_type;
    uint16_t apply_region_bitmap = 0;
    uint16_t neg_apply_region_bitmap = 0;
    int8_t group = 0;
    bool stale = false;
    bool deleted = false;
    unsigned used_count = 0;
    unsigned clear_count = 0;
    uint8_t sort_perm = 0;
    unsigned cpu_time = 0;
    std::string comment;


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
            REG_TYPE_IF,
            CELL_COUNT,
            VAR_ADD,
            VAR_SUB,
            VAR_TRIPLE,
            VAR_QUAD,
            VAR_BCD,
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
//    bool matches(GridRegion* r1, GridRegion* r2, GridRegion* r3, GridRegion* r4, int var_counts[32]);
    void import_rule_gen_regions(GridRegion* r1, GridRegion* r2, GridRegion* r3, GridRegion* r4);
    typedef enum {OK, ILLOGICAL, LOSES_DATA, IMPOSSIBLE, USELESS, UNBOUNDED, LIMIT} IsLogicalRep;
    IsLogicalRep is_legal(GridRule& why, int vars[5]);
    void remove_region(int index);
    void add_region(RegionType type, bool neg = false);
    void resort_region();
    RegionType get_region_sorted(int index);
    // Rule slots include separate IF and
    // THEN dimensions. Input regions do not.
    int input_index_for_slot(int slot) const;
    GridRegionCause make_cause(
        GridRegion* r1, GridRegion* r2,
        GridRegion* r3, GridRegion* r4);
    bool has_valid_structure() const;
    unsigned valid_area_mask() const;
    std::string debug_description(unsigned indent = 0) const;

};

struct RenderCmd
{
    XYRect src;
    XYRect dst;
    double angle = 0.0;
    XYPos center = XYPos(0,0);
    bool bg = false;
    // Scale the rotation centre's y along with the rest of the command when
    // drawing smaller or larger copies of a recursed board. Lines centred on
    // an edge need it; the original grids' lines pivot on their first pixel.
    bool scale_center = false;
    // A solid background triangle through points, instead of a sprite.
    bool triangle = false;
    XYPosFloat points[3];
    RenderCmd (XYRect src_, XYRect dst_, bool bg_ = false): src(src_),dst(dst_),bg(bg_){}
    RenderCmd (XYRect src_, XYRect dst_, double angle_, XYPos center_): src(src_),dst(dst_), angle(angle_), center(center_) {}
    RenderCmd (XYPosFloat a, XYPosFloat b, XYPosFloat c): src(0, 0, 0, 0), dst(0, 0, 0, 0), bg(true), triangle(true), points{a, b, c} {}
};

struct EdgePos
{
    XYPos rule_pos;
    RegionIfType type;
    double angle;
    double pos;
    EdgePos (XYPos _rule_pos, RegionIfType type_, double angle_, double pos_): rule_pos(_rule_pos),type(type_),angle(angle_),pos(pos_){}
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
        // Square grid only. Both sides wrap, but going off the top or bottom
        // comes back mirrored left to right: a Klein bottle.
        WRAPPED_KLEIN,
        // Square grid only. The left and right sides wrap, coming back
        // mirrored top to bottom; the top and bottom are real edges: a
        // Moebius strip.
        WRAPPED_MOBIUS,
    } wrapped = WRAPPED_NOT;
    // The board repeats beside itself, as a torus or a mirrored surface.
    bool wraps_sides() const {return wrapped == WRAPPED_SIDE || wrapped == WRAPPED_KLEIN || wrapped == WRAPPED_MOBIUS;}
    // Mirrored surfaces: whether the copy of the board r copies away is
    // drawn mirrored left to right (flip.x) or top to bottom (flip.y).
    XYPos get_copy_flip(XYPos r) const
    {
        if (wrapped == WRAPPED_KLEIN)
            return XYPos(r.y & 1, 0);
        if (wrapped == WRAPPED_MOBIUS)
            return XYPos(0, r.x & 1);
        return XYPos(0, 0);
    }
    // How many copies along each axis before the copies repeat exactly.
    XYPos get_copy_period() const
    {
        if (wrapped == WRAPPED_KLEIN)
            return XYPos(1, 2);
        if (wrapped == WRAPPED_MOBIUS)
            return XYPos(2, 1);
        return XYPos(1, 1);
    }
    bool wants_base_regions = true;

    std::map<XYPos, GridPlace> vals;
    std::map<XYPos, RegionIfType> edges;      //  X=0 - vertical, X=1 horizontal
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
    void randomize(XYPos size_, WrapType wrapped, int merged_count, int row_percent, int negated_percent);
    // Keeps at most count row clues, chosen at random.
    void limit_row_clues(int count);
    void from_string(std::string s);

    static Grid* Load(std::string s);
    GridPlace get(XYPos p);
    RegionIfType& get_clue(XYPos p);

    virtual std::string text_desciption() = 0;
    virtual std::string to_string();
    std::string debug_dump() const;
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
    virtual XYPos get_grid_size(XYPos grid_pitch) = 0;
    virtual XYPos get_pos_from_mouse_pos(XYPos pos, XYPos grid_pitch) = 0;
    // Where a row clue's line enters and leaves its cells, in pixels, so its
    // tabs can sit at the ends of the row. Grids that fill the play area
    // don't need this; their tabs go at the edge of the view.
    virtual bool get_row_ends(XYPos rule_pos, XYPos grid_pitch, XYPosFloat& start, XYPosFloat& end) {return false;}
    // Plane boards: the size must be a multiple of this so the board's edges
    // line up with the next copy. randomize() rounds the size up to it.
    virtual XYPos plane_size_step() {return XYPos(1, 1);}
    // Recursed boards: how much bigger the board is drawn than the copy of
    // it inside the innie.
    virtual double get_innie_mag(XYPos grid_pitch, int scaled_grid_size)
    {
        return double(scaled_grid_size) / get_square_pos(innie_pos, grid_pitch).size.x;
    }

    void solve_easy();
    bool is_solveable();

    bool is_determinable(XYPos q);
    bool is_determinable_using_regions(XYPos q, bool hidden = false);
//    bool has_solution(void);
    void make_harder(int plus_minus, int x_y, int x_y3, int x_y_z, int exc, int parity, int xor1, int xor11, int prime, int if_then);
    void reveal(XYPos p);
    bool is_solved(void);

    bool add_region(GridRegion& r, bool front = false);
    bool add_region(XYSet& elements, XYSet& elements_neg, RegionIfType if_clue, XYPos cause);
    void add_base_regions(void);

    enum ApplyRuleResp
    {
        APPLY_RULE_RESP_NONE,
        APPLY_RULE_RESP_HIT
    };

    ApplyRuleResp apply_rule(GridRule& rule, GridRegion* regions[4], int var_counts[32], bool update_stats = true);
    ApplyRuleResp apply_rule(GridRule& rule, GridRegion* region, bool update_stats = true);
//    ApplyRuleResp apply_rule(GridRule& rule, bool force = false);
    void remove_from_regions_to_add_multiset(GridRegion*);
    void add_new_regions();
    bool region_is_correct(GridRegion* r);
    GridRegion* add_one_new_region(GridRegion* ancestor, const XYSet& filter_pos_and, const XYSet& filter_pos_not);
    void clear_regions();
    void commit_level_counts();
    void remove_from_regions_to_add_for_rule(GridRule* rule);
    bool uses_neg_bombs();
    bool uses_if_then_clues();
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
    XYPos get_grid_size(XYPos grid_pitch);
    XYPos get_pos_from_mouse_pos(XYPos pos, XYPos grid_pitch);
private:
    // Moves a cell position off the board onto the cell it reaches through
    // the wrapped sides. Returns false if it's past a real edge.
    bool wrap_pos(XYPos& p);
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
    XYPos get_grid_size(XYPos grid_pitch);
    XYPos get_pos_from_mouse_pos(XYPos pos, XYPos grid_pitch);
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
    XYPos get_grid_size(XYPos grid_pitch);
    XYPos get_pos_from_mouse_pos(XYPos pos, XYPos grid_pitch);
};

// Base for tilings whose cells are convex polygons with corners on an integer
// lattice. A lattice unit is grid_pitch.x wide and grid_pitch.y tall. Cells
// are neighbours when they share a corner, and corners are listed clockwise
// on screen.
class PolygonGrid : public Grid
{
public:
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
    void render_square(XYPos pos, XYPos grid_pitch, std::vector<RenderCmd>& cmds);
    XYPos get_wrapped_size(XYPos grid_pitch);
    XYPos get_grid_size(XYPos grid_pitch);
    XYPos get_pos_from_mouse_pos(XYPos pos, XYPos grid_pitch);
    XYPos get_base_square(XYPos p);
    void add_random_merged(int count);
    bool get_row_ends(XYPos rule_pos, XYPos grid_pitch, XYPosFloat& start, XYPosFloat& end);
    double get_innie_mag(XYPos grid_pitch, int scaled_grid_size);

    // Big cells are stored in Grid::merged as anchor -> (kind, 0), keeping
    // the level string format of the square and triangle grids.
    enum MergeKind
    {
        MERGE_PARENT = 1,   // every piece of the shape the cell was cut from
        MERGE_PAIR = 2,     // the anchor and the next piece of that shape
        MERGE_ACROSS = 3,   // the anchor and the cell across its first edge
    };

protected:
    static const int MAX_CORNERS = 4;
    static const int MAX_PIECES = 6;
    // Fills corners and returns how many there are.
    virtual int get_corners(XYPos pos, XYPos corners[MAX_CORNERS]) = 0;
    // Lattice size of one copy of the board, used for plane wrapping.
    virtual XYPos lattice_period() = 0;
    // Height of a lattice unit relative to its width.
    virtual double unit_aspect() = 0;
    virtual int edge_thickness(XYPos grid_pitch) = 0;
    // How much of the space inside a cell clue icons and region bubbles use.
    virtual double content_scale() {return 1.0;}
    // The pieces of the larger shape a cell was cut from, clockwise,
    // e.g. the three rhombi of a hexagon.
    virtual int get_parent_pieces(XYPos pos, XYPos pieces[MAX_PIECES]) = 0;
    // Only kinds that make convex big cells are allowed.
    virtual bool merge_kind_allowed(int kind) = 0;

    // Whether a piece is part of the board. Recursed boards must be the same
    // shape as their innie, so some tilings trim them.
    virtual bool piece_in_board(XYPos pos) {return true;}
    // Whether a piece can be part of a big cell. Pieces cut down to fit the
    // board would make big cells of the wrong shape.
    virtual bool piece_can_merge(XYPos pos) {return piece_in_board(pos);}
    // Recursed boards: the innie (at innie_pos, stored as a MERGE_PARENT big
    // cell) holds a copy of the board scaled down by `scale`, with the board's
    // lattice point board_ref landing on innie_ref.
    virtual bool get_recursion(XYPos& board_ref, XYPos& innie_ref, int& scale) {return false;}
    virtual bool innie_allowed(XYPos anchor) {return false;}

    // Row clues. A family of rows is parallel lines in one direction,
    // `spacing` apart, with lines halfway between band edges that pass
    // through `origin` or, when centred on cells, through cell centres. A row
    // is exactly the cells its line passes through, so it walks along the
    // line, and the row's tab is drawn on that same line. Directions, spacing
    // and origin are in true proportions: lattice x, and lattice y times
    // unit_aspect().
    struct RowFamily
    {
        double angle;               // degrees, clockwise on screen from pointing right
        double spacing;
        XYPosFloat origin;
        bool centred_on_cells;
    };
    static const int MAX_ROW_FAMILIES = 4;
    virtual int get_row_families(RowFamily families[MAX_ROW_FAMILIES]) = 0;
private:
    double row_band_start(const RowFamily& family);
    // Where the family's line k sits, as a distance along its normal.
    double row_line(const RowFamily& family, int k);
    bool piece_on_row_line(XYPos pos, const RowFamily& family, int k);
    // The stretch of line k inside a piece, as distances along the line.
    // Returns false if the line misses the piece or only grazes a corner.
    bool row_line_chord(XYPos pos, const RowFamily& family, int k, double& lo, double& hi);
    // The lowest and highest line passing through any piece.
    void row_line_range(const RowFamily& family, int& first, int& last);
    // The small cells making up a cell: just itself, or a big cell's pieces.
    int get_members(XYPos base, XYPos members[MAX_PIECES]);
    // Recursed boards: neighbours, including across the innie's edge.
    XYSet get_recursed_neighbors(XYPos pos);
    // Maps a point inside the innie to the same place on the board, in pixels.
    XYPos point_out_of_innie(XYPos pos, XYPos grid_pitch);
    bool point_in_innie(XYPos pos, XYPos grid_pitch);
    int get_merge_members(XYPos anchor, int kind, XYPos members[MAX_PIECES]);
    // For big cells, the centre of the shape and the distance from it to the
    // nearest outer edge.
    void get_merged_circle(XYPos pos, XYPos grid_pitch, XYPosFloat& centre, double& radius);
    XYPos wrap_corner(XYPos corner);
    XYPos lattice_extent();
    // The largest circle inside the cell, in pixels.
    void get_incircle(XYPos pos, XYPos grid_pitch, XYPosFloat& centre, double& radius);
    // The largest ellipse inside the cell, in pixels: its centre, the unit
    // direction of its long axis and its two semi-axes.
    void get_inellipse(XYPos pos, XYPos grid_pitch, XYPosFloat& centre, XYPosFloat& major_dir, double& major, double& minor);
};

// Rhombille tiling: hexagons laid out as in HexagonGrid, each split into
// three rhombi. Cell (x, y) is rhombus x % 3 of hexagon column x / 3, row y,
// so size.x is always a multiple of 3.
//
// Recursed boards are one big hexagon, the shape of the innie's three rhombi.
// Its straight sides can't follow the rhombi's zigzag, so where a side cuts a
// rhombus along its short diagonal, the half on the board is kept as an
// equilateral triangle cell.
class RhombilleGrid : public PolygonGrid
{
public:
    RhombilleGrid() {}
    RhombilleGrid(std::string s) {from_string(s);}
    std::string text_desciption();
    std::string to_string();
    Grid* dup() {return new RhombilleGrid(*this);}
    // Recursed boards with hexagon sides `side` rhombus sides long are
    // recursed_size(side) cells.
    static XYPos recursed_size(int side);
protected:
    bool get_recursion(XYPos& board_ref, XYPos& innie_ref, int& scale);
    bool innie_allowed(XYPos anchor);
    bool piece_in_board(XYPos pos);
    bool piece_can_merge(XYPos pos);
    int get_corners(XYPos pos, XYPos corners[MAX_CORNERS]);
    XYPos lattice_period();
    double unit_aspect();
    int edge_thickness(XYPos grid_pitch);
    int get_parent_pieces(XYPos pos, XYPos pieces[MAX_PIECES]);
    bool merge_kind_allowed(int kind);
    int get_row_families(RowFamily families[MAX_ROW_FAMILIES]);
    // An even number of hexagon columns, as they alternate up and down.
    XYPos plane_size_step() {return XYPos(6, 1);}
    double content_scale() {return 0.7;}
private:
    bool is_recursed_board();
    // Where the board hexagon is: the rhombi are moved by this so its
    // bounding box starts at the origin.
    XYPos board_shift();
    // The rhombus's corners, moved as the board, and which of its two halves
    // are on the board: bit 0 for corners 3, 0, 1 and bit 1 for 1, 2, 3.
    int rhombus_on_board(XYPos pos, XYPos rhombus[4]);
    // The piece's own outline, a rhombus or an edge triangle, before corner
    // pairs are joined. Returns the corner count, or 0 if it is off the board.
    int piece_corners(XYPos pos, XYPos corners[MAX_CORNERS]);
    // Recursed boards: whether pos is one of the two triangles at a corner of
    // the board that share an edge, and so make one rhombus cell. The cell
    // belongs to whichever of the two is lower in XYPos order.
    bool corner_pair(XYPos pos, XYPos& other);
};

// Tetrakis square tiling: squares cut by both diagonals into four right
// isosceles triangles. Cell (x, y) is in square (x / 2, y / 2); the low bits
// pick the top, right, bottom or left triangle.
class TetrakisGrid : public PolygonGrid
{
public:
    TetrakisGrid() {}
    TetrakisGrid(std::string s) {from_string(s);}
    std::string text_desciption();
    std::string to_string();
    Grid* dup() {return new TetrakisGrid(*this);}
protected:
    bool get_recursion(XYPos& board_ref, XYPos& innie_ref, int& scale);
    bool innie_allowed(XYPos anchor);
    int get_corners(XYPos pos, XYPos corners[MAX_CORNERS]);
    XYPos lattice_period();
    double unit_aspect();
    int edge_thickness(XYPos grid_pitch);
    int get_parent_pieces(XYPos pos, XYPos pieces[MAX_PIECES]);
    bool merge_kind_allowed(int kind);
    int get_row_families(RowFamily families[MAX_ROW_FAMILIES]);
    // Smaller so the bomb's fuse spark clears the slanted edges.
    double content_scale() {return 0.85;}
};

// Kisrhombille tiling: each triangle of a TriangleGrid layout cut by its
// medians into six 30-60-90 triangles. Cell (x, y) is in triangle
// (x / 3, y / 2), piece (x % 3) + 3 * (y % 2) clockwise from a corner.
class KisrhombilleGrid : public PolygonGrid
{
public:
    KisrhombilleGrid() {}
    KisrhombilleGrid(std::string s) {from_string(s);}
    std::string text_desciption();
    std::string to_string();
    Grid* dup() {return new KisrhombilleGrid(*this);}
protected:
    bool get_recursion(XYPos& board_ref, XYPos& innie_ref, int& scale);
    bool innie_allowed(XYPos anchor);
    bool piece_in_board(XYPos pos);
    // Recursed boards are one big triangle of this many rows.
    int board_rows();
    // Recursed boards flip which triangles point up, so the board's top
    // triangle points up whatever the number of rows.
    bool flip_triangles();
    int get_corners(XYPos pos, XYPos corners[MAX_CORNERS]);
    XYPos lattice_period();
    double unit_aspect();
    int edge_thickness(XYPos grid_pitch);
    int get_parent_pieces(XYPos pos, XYPos pieces[MAX_PIECES]);
    bool merge_kind_allowed(int kind);
    int get_row_families(RowFamily families[MAX_ROW_FAMILIES]);
    // An even number of triangles across and down, as they alternate up and down.
    XYPos plane_size_step() {return XYPos(6, 4);}
};

// Triakis triangular tiling: each triangle of a TriangleGrid layout cut from
// its centre to its corners into three obtuse triangles. Cell (x, y) is in
// triangle (x / 3, y), piece x % 3.
class TriakisGrid : public PolygonGrid
{
public:
    TriakisGrid() {}
    TriakisGrid(std::string s) {from_string(s);}
    std::string text_desciption();
    std::string to_string();
    Grid* dup() {return new TriakisGrid(*this);}
protected:
    bool get_recursion(XYPos& board_ref, XYPos& innie_ref, int& scale);
    bool innie_allowed(XYPos anchor);
    bool piece_in_board(XYPos pos);
    // Recursed boards are one big triangle of this many rows.
    int board_rows();
    // Recursed boards flip which triangles point up, so the board's top
    // triangle points up whatever the number of rows.
    bool flip_triangles();
    int get_corners(XYPos pos, XYPos corners[MAX_CORNERS]);
    XYPos lattice_period();
    double unit_aspect();
    int edge_thickness(XYPos grid_pitch);
    int get_parent_pieces(XYPos pos, XYPos pieces[MAX_PIECES]);
    bool merge_kind_allowed(int kind);
    int get_row_families(RowFamily families[MAX_ROW_FAMILIES]);
    // An even number of triangles across and down, as they alternate up and down.
    XYPos plane_size_step() {return XYPos(6, 2);}
};

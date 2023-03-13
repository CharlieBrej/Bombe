#include "z3++.h"


#include "Grid.h"
#include <sstream>

bool IS_DEMO = false;
bool IS_PLAYTEST = false;
static std::random_device rd;
static Rand rnd(rd());
//static Rand rnd(1);

static std::map<int,int> colours_used;

void grid_set_rnd(int a)
{
    rnd.gen.seed(a);
}

template<class RESP, class IN>
RESP RegionType::apply_rule(IN in)
{
    if (type == EQUAL)
    {
        return in == value;
    }
    if (type == NOTEQUAL)
    {
        return in != value;
    }
    if (type == LESS)
    {
        return in <= value;
    }
    if (type == MORE)
    {
        return in >= value;
    }
    if (type == XOR2)
    {
        return (in == value) || (in == (value + 2));
    }
    if (type == XOR3)
    {
        return (in == value) || (in == (value + 3));
    }
    if (type == XOR22)
    {
        return (in == value) || (in == (value + 2)) || (in == (value + 4));
    }
    if (type == XOR222)
    {
        return (in == value) || (in == (value + 2)) || (in == (value + 4)) || (in == (value + 6));
    }
    assert(0);
}
z3::expr RegionType::apply_z3_rule(z3::expr in)
{
    return apply_rule<z3::expr,z3::expr>(in);
}

bool RegionType::apply_int_rule(unsigned in)
{
    return apply_rule<bool,unsigned>(in);
}

int RegionType::max()
{
    if (type == NONE)
    {
        return -1;
    }
    if (type == EQUAL)
    {
        return value;
    }
    if (type == NOTEQUAL)
    {
        return -1;
    }
    if (type == LESS)
    {
        return value;
    }
    if (type == MORE)
    {
        return -1;
    }
    if (type == XOR2)
    {
        return value + 2;
    }
    if (type == XOR3)
    {
        return value + 3;
    }
    if (type == XOR22)
    {
        return value + 4;
    }
    if (type == XOR222)
    {
        return value + 6;
    }
    assert(0);
}

GridRegion::GridRegion(RegionType type_)
{
    type = type_;
    colour = colours_used[type.value]++;
}

bool GridRegion::overlaps(GridRegion& other)
{
    return elements.overlaps(other.elements);
}

bool GridRegion::contains_all(std::set<XYPos>& other)
{
    for (const XYPos& p : other)
    {
        if (!elements.contains(p))
            return false;
    }
    return true;
}

void GridRegion::next_colour()
{
    colour = colours_used[type.value]++;
}

bool GridRegion::has_ancestor(GridRegion* other)
{
    for (int i = 0; i < 4; i++)
    {
        if (gen_cause.regions[0])
        {
            if (gen_cause.regions[0] == other)
                return true;
            if (gen_cause.regions[0]->has_ancestor(other))
                return true;
        }
    }
    return false;
}



GridRule::GridRule(SaveObject* sobj, int version)
{
    SaveObjectMap* omap = sobj->get_map();
    region_count = omap->get_num("region_count");
    apply_region_type = RegionType('a',omap->get_num("apply_region_type"));

    if (omap->has_key("apply_type"))
    {
        enum ApplyType
        {
            REGION,
            BOMB,
            CLEAR,
            HIDE,
            SHOW,
            BIN,
        } apply_type = ApplyType(omap->get_num("apply_type"));
        if (apply_type == BOMB) apply_region_type = RegionType(RegionType::SET, 1);
        if (apply_type == CLEAR) apply_region_type = RegionType(RegionType::SET, 0);
        if (apply_type == SHOW) apply_region_type = RegionType(RegionType::VISIBILITY, 0);
        if (apply_type == HIDE) apply_region_type = RegionType(RegionType::VISIBILITY, 1);
        if (apply_type == BIN) apply_region_type = RegionType(RegionType::VISIBILITY, 2);
    }


    apply_region_bitmap = omap->get_num("apply_region_bitmap");

    SaveObjectList* rlist = omap->get_item("region_type")->get_list();
    for (int i = 0; i < rlist->get_count(); i++)
        region_type[i] = RegionType('a',rlist->get_num(i));

    rlist = omap->get_item("square_counts")->get_list();
    for (int i = 0; i < rlist->get_count(); i++)
    {
        int v = rlist->get_num(i);
        if (version >= 3)
            square_counts[i] = RegionType('a',v);
        else
        {
            int8_t v2 = v;
            if (v2 < 0)
                square_counts[i] = RegionType(RegionType::NONE, 0);
            else
                square_counts[i] = RegionType(RegionType::EQUAL, v2);
        }
    }
    if (omap->has_key("used_count"))
        used_count = omap->get_num("used_count");
    if (omap->has_key("clear_count"))
        clear_count = omap->get_num("clear_count");
}

SaveObject* GridRule::save()
{
    SaveObjectMap* omap = new SaveObjectMap;
    omap->add_num("region_count", region_count);
    omap->add_num("apply_region_type", apply_region_type.as_int());
    omap->add_num("apply_region_bitmap", apply_region_bitmap);

    SaveObjectList* region_type_list = new SaveObjectList;
    for (int i = 0; i < region_count; i++)
        region_type_list->add_num(region_type[i].as_int());
    omap->add_item("region_type", region_type_list);

    SaveObjectList* square_counts_list = new SaveObjectList;
    for (int i = 0; i < (1<<region_count); i++)
        square_counts_list->add_num(square_counts[i].as_int());
    omap->add_item("square_counts", square_counts_list);
    omap->add_num("used_count", used_count);
    omap->add_num("clear_count", clear_count);


    return omap;
}

void GridRule::get_square_counts(uint8_t square_counts[16], GridRegion* r1, GridRegion* r2, GridRegion* r3, GridRegion* r4)
{
    for (int i = 0; i < 16; i++)
        square_counts[i] = 0;

    if (!r1)
        return;

    if (!r2)
    {
        XYSet a0 = r1->elements;
        square_counts[1] = (a0).count();
        return;
    }
    if (!r3)
    {
        XYSet a0 = r1->elements;
        XYSet a1 = r2->elements;
        square_counts[1] =  (a0 & ~a1).count();
        square_counts[2] = (~a0 &  a1).count();
        square_counts[3] = ( a0 &  a1).count();
        return;
    }
    if (!r4)
    {
        XYSet a0 = r1->elements;
        XYSet a1 = r2->elements;
        XYSet a2 = r3->elements;
        square_counts[1] =  (a0 & ~a1 & ~a2).count();
        square_counts[2] = (~a0 &  a1 & ~a2).count();
        square_counts[3] = ( a0 &  a1 & ~a2).count();
        square_counts[4] = (~a0 & ~a1 &  a2).count();
        square_counts[5] = ( a0 & ~a1 &  a2).count();
        square_counts[6] = (~a0 &  a1 &  a2).count();
        square_counts[7] = ( a0 &  a1 &  a2).count();
        return;
    }

    {
        XYSet a0 = r1->elements;
        XYSet a1 = r2->elements;
        XYSet a2 = r3->elements;
        XYSet a3 = r4->elements;
        square_counts[1] =  ( a0 & ~a1 & ~a2 & ~a3).count();
        square_counts[2] =  (~a0 &  a1 & ~a2 & ~a3).count();
        square_counts[3] =  ( a0 &  a1 & ~a2 & ~a3).count();
        square_counts[4] =  (~a0 & ~a1 &  a2 & ~a3).count();
        square_counts[5] =  ( a0 & ~a1 &  a2 & ~a3).count();
        square_counts[6] =  (~a0 &  a1 &  a2 & ~a3).count();
        square_counts[7] =  ( a0 &  a1 &  a2 & ~a3).count();
        square_counts[8] =  (~a0 & ~a1 & ~a2 &  a3).count();
        square_counts[9] =  ( a0 & ~a1 & ~a2 &  a3).count();
        square_counts[10] = (~a0 &  a1 & ~a2 &  a3).count();
        square_counts[11] = ( a0 &  a1 & ~a2 &  a3).count();
        square_counts[12] = (~a0 & ~a1 &  a2 &  a3).count();
        square_counts[13] = ( a0 & ~a1 &  a2 &  a3).count();
        square_counts[14] = (~a0 &  a1 &  a2 &  a3).count();
        square_counts[15] = ( a0 &  a1 &  a2 &  a3).count();
        return;
    }
}

GridRule GridRule::permute(std::vector<int>& p)
{
    GridRule r;
    r.region_count = region_count;
    r.apply_region_type = apply_region_type;
    
    for (int i = 0; i < region_count; i++)
        r.region_type[i] = region_type[p[i]];

    for (int i = 0; i < (1 << region_count); i++)
    {
        int p_index = 0;
        for (int a = 0; a < region_count; a++)
        {
            if ((i >> a) & 1)
                p_index |= 1 << p[a];
        }
        r.square_counts[i] = square_counts[p_index];
        if (apply_region_type.type != RegionType::VISIBILITY)
            r.apply_region_bitmap |= ((apply_region_bitmap >> p_index) & 1) << i;

    }
    if (apply_region_type.type == RegionType::VISIBILITY)
        for (int i = 0; i < region_count; i++)
            r.apply_region_bitmap |= ((apply_region_bitmap >> p[i]) & 1) << i;
    
    return r;
}

bool GridRule::covers(GridRule& other)
{
    if (deleted || other.deleted)
        return false;
    if (region_count != other.region_count)
        return false;
    for (int i = 0; i < region_count; i++)
        if (region_type[i] != other.region_type[i])
            return false;
    for (int i = 0; i < (1 << region_count); i++)
    {
        if (square_counts[i] != other.square_counts[i])
            return false;
        if (((apply_region_bitmap >> i) & 1) != ((other.apply_region_bitmap >> i) & 1))
            return false;
    }
    if (apply_region_type != other.apply_region_type)
        return false;
    return true;
}

bool GridRule::matches(GridRegion* r1, GridRegion* r2, GridRegion* r3, GridRegion* r4)
{
    for (int i = 1; i < (1 << region_count); i++)
    {
        if (square_counts[i].type == RegionType::NONE)
            continue;
        XYSet s = (i & 1) ? r1->elements : ~r1->elements;
        if (r2)
            s = s & ((i & 2) ? r2->elements : ~r2->elements);
        if (r3)
            s = s & ((i & 4) ? r3->elements : ~r3->elements);
        if (r4)
            s = s & ((i & 8) ? r4->elements : ~r4->elements);
        if (!square_counts[i].apply_int_rule(s.count()))
            return false;
    }
    return true;
}

void GridRule::import_rule_gen_regions(GridRegion* r1, GridRegion* r2, GridRegion* r3, GridRegion* r4)
{
    region_count = 0;
    apply_region_bitmap = 0;

    if (r1)
    {
        region_count = 1;
        region_type[0] = r1->type;
    }
    if (r2)
    {
        region_count = 2;
        region_type[1] = r2->type;
    }
    if (r3)
    {
        region_count = 3;
        region_type[2] = r3->type;
    }
    if (r4)
    {
        region_count = 4;
        region_type[3] = r4->type;
    }

    uint8_t sqc[16];
    get_square_counts(sqc, r1, r2, r3, r4);

    for (int i = 1; i < 16; i++)
        square_counts[i] = RegionType(RegionType::NONE, 0);

    for (int i = 1; i < (1 << region_count); i++)
    {
        square_counts[i] = RegionType(RegionType::EQUAL, sqc[i]);
    }
}

bool GridRule::is_legal()
{
    if (apply_region_type.type == RegionType::VISIBILITY)
        return true;
    z3::context c;
    z3::solver s(c);

    z3::expr_vector vec(c);

    vec.push_back(c.bool_const("DUMMY"));

    for (int i = 1; i < (1 << region_count); i++)
    {
        std::stringstream x_name;
        x_name << "A" << i;
        vec.push_back(c.int_const(x_name.str().c_str()));
        s.add(vec[i] >= 0);
        int m = square_counts[i].max();
        if (m >= 0)
            s.add(vec[i] <= int(m));
    }

    if (region_count == 1)
    {
        s.add(region_type[0].apply_z3_rule(vec[1]));
    }
    if (region_count == 2)
    {
        s.add(region_type[0].apply_z3_rule(vec[1] + vec[3]));
        s.add(region_type[1].apply_z3_rule(vec[2] + vec[3]));
    }
    if (region_count == 3)
    {
        s.add(region_type[0].apply_z3_rule(vec[1] + vec[3] + vec[5] + vec[7]));
        s.add(region_type[1].apply_z3_rule(vec[2] + vec[3] + vec[6] + vec[7]));
        s.add(region_type[2].apply_z3_rule(vec[4] + vec[5] + vec[6] + vec[7]));
    }
    if (region_count == 4)
    {
        s.add(region_type[0].apply_z3_rule(vec[1] + vec[3] + vec[5] + vec[7] + vec[9] + vec[11] + vec[13] + vec[15]));
        s.add(region_type[1].apply_z3_rule(vec[2] + vec[3] + vec[6] + vec[7] + vec[10] + vec[11] + vec[14] + vec[15]));
        s.add(region_type[2].apply_z3_rule(vec[4] + vec[5] + vec[6] + vec[7] + vec[12] + vec[13] + vec[14] + vec[15]));
        s.add(region_type[3].apply_z3_rule(vec[8] + vec[9] + vec[10] + vec[11] + vec[12] + vec[13] + vec[14] + vec[15]));
    }


    z3::expr e = c.int_val(0);
    int tot = 0;

    if (!apply_region_bitmap)
        return false;

    for (int i = 1; i < (1 << region_count); i++)
    {
        if ((apply_region_bitmap >> i) & 1)
        {
            e = e + vec[i];
            int m = square_counts[i].max();
            if ((m < 0) && (apply_region_type == RegionType(RegionType::SET,1)))
                return false;
            tot += m;
        }
    }

    if (apply_region_type.type == RegionType::VISIBILITY)
        return true;
    else if (apply_region_type.type == RegionType::SET)
    {
        if (apply_region_type.value)
            s.add(e != tot);
        else
            s.add(e != 0);
    }
    else
    {
        s.add(!apply_region_type.apply_z3_rule(e));
    }

    if (s.check() == z3::sat)
    {
        printf("sat\n");
        for (int i = 1; i < (1 << region_count); i++)
        {
            z3::model m = s.get_model();
            std::cout << "B" << i << ":" << m.eval(vec[i]) << "\n";
        }



        return false;
    }
    else
    {
        return true;
    }
}

void GridRule::remove_region(int index)
{
    region_count--;
    for (int i = index; i < region_count; i++)
        region_type[i] = region_type[i + 1];
    region_type[region_count] = RegionType();

    uint16_t new_apply_region_bitmap = 0;
    for (int i = 1; i < (1 << region_count); i++)
    {
            int idx = i & ((1 << (index)) - 1);
            idx |= (i >> (index)) << (index + 1);
            int idx2 = idx | (1 << index);
            int c = square_counts[idx].value + square_counts[idx | (1 << index)].value;
            square_counts[i] = RegionType(RegionType::EQUAL, c);
            if ((apply_region_bitmap >> idx) & (apply_region_bitmap >> idx2) & 1)
                new_apply_region_bitmap |= 1 << i;
    }
    apply_region_bitmap = new_apply_region_bitmap;
}
void GridRule::resort_region()
{
    struct Sorter {
        GridRule& g;
        Sorter(GridRule& g_): g(g_) {};
        bool operator() (int i,int j) { return (g.region_type[i] < g.region_type[j]);}
    };
    Sorter sorter(*this);
    std::vector<int> idx;
    for(int i=0;i<region_count;i++)
        idx.push_back(i);
    std::sort (idx.begin(), idx.end(), sorter);
    sort_perm = idx[0] | (idx[1] << 2) | (idx[2] << 4) | (idx[3] << 6);
}

RegionType GridRule::get_region_sorted(int index)
{
    return region_type[(sort_perm >> (index * 2)) & 0x3];
}

void Grid::randomize(XYPos size_, WrapType wrapped_, int merged_count, int row_percent)
{
    size = size_;
    wrapped = wrapped_;
    add_random_merged(merged_count);

    XYSet grid_squares = get_squares();
    FOR_XY_SET(p, grid_squares)
    {
        vals[p] = GridPlace((unsigned(rnd)%100) < 40, true);
    }

    FOR_XY_SET(p, grid_squares)
    {
        if (!vals[p].bomb)
        {
            int cnt = 0;
            XYSet neigh = get_neighbors(p);
            FOR_XY_SET(p, neigh)
                cnt += get(p).bomb;

            vals[p].clue.type = RegionType::EQUAL;
            vals[p].clue.value = cnt;
        }
    }
    std::vector<XYPos> row_types;
    get_row_types(row_types);
    for (int i = 0; i < row_types.size(); i++)
    {
        XYPos row_type = row_types[i];
        for (int j = row_type.x; j < row_type.y; j++)
        {
            if (rnd % 100 < row_percent)
            {
                int c = 0;
                XYSet grid_squares = get_row(i, j);
                FOR_XY_SET(n, grid_squares)
                    if (get(n).bomb)
                        c++;
                edges[XYPos(i, j)] = RegionType(RegionType::EQUAL, c);
            }
        }
    }
}

Grid::Grid()
{
}

void Grid::from_string(std::string s)
{
    assert (s.length() >= 4);
    int a = s[0] - 'A';
    if (a < 0 || a > 50) return;
    size.x = a;

    a = s[1] - 'A';
    if (a < 0 || a > 50) return;
    size.y = a;

    a = s[2] - 'A';
    wrapped = WrapType(a);
    int i = 3;
    if (wrapped == WRAPPED_IN)
    {
        innie_pos.x = s[i++] - '0';
        innie_pos.y = s[i++] - '0';
    }

    while (s[i] == '#')
    {
        i++;
        XYPos mp;
        XYPos ms;
        mp.x = s[i++] - '0';
        mp.y = s[i++] - '0';
        ms.x = s[i++] - '0';
        ms.y = s[i++] - '0';
        merged[mp] = ms;
    }

    while (s[i] == '|')
    {
        i++;
        XYPos mp;
        RegionType t;
        mp.x = s[i++] - '0';
        mp.y = s[i++] - '0';
        t.type = RegionType::Type(s[i++] - 'A');
        t.value = s[i++] - '0';
        if (t.type != RegionType::NONE)
        {
            edges[mp] = t;
        }
    }


    XYSet grid_squares = get_squares();
    FOR_XY_SET(p, grid_squares)
    {
        if (i >= s.length()) return;
        char c = s[i++];

        vals[p] = GridPlace(true, true);

        if (c == '_')
        {
            vals[p].revealed = false;
            if (i >= s.length()) return;
            c = s[i++];
        }

        if (c != '!')
        {
            vals[p].bomb = false;
            vals[p].clue.type = RegionType::Type(c - 'A');
            if (i >= s.length()) return;
            c = s[i++];
            vals[p].clue.value = c - '0';
        }
    }

    // for (int x = 0; x < size.y; x++)
    // {
    //     if (s.length() < i) return;
    //     char c = s[i++];
    //     RegionType t;
    //     t.type = RegionType::Type(c - 'A');
    //     if (s.length() < i) return;
    //     c = s[i++];
    //     t.value = c - '0';
    //     if (t.type != RegionType::NONE)
    //     {
    //         edges[XYPos(0,x)] = t;
    //     }
    // }
    //
    // for (int x = 0; x < size.x; x++)
    // {
    //     if (s.length() < i) return;
    //     char c = s[i++];
    //     RegionType t;
    //     t.type = RegionType::Type(c - 'A');
    //     if (s.length() < i) return;
    //     c = s[i++];
    //     t.value = c - '0';
    //     if (t.type != RegionType::NONE)
    //         edges[XYPos(1,x)] = t;
    // }
}

Grid* Grid::Load(std::string s)
{
    assert(s.length() >= 3);
    int a = s[0] - 'A';
    if (a == 0)
        return new SquareGrid(s.substr (1, std::string::npos));
    if (a == 1)
        return new TriangleGrid(s.substr (1, std::string::npos));
    if (a == 2)
        return new HexagonGrid(s.substr (1, std::string::npos));
    assert(0);
}

GridPlace Grid::get(XYPos p)
{
    assert(p.inside(size));
    assert(vals.count(p));

    return vals[p];
}
RegionType& Grid::get_clue(XYPos p)
{
    if (p.x < 0)
        return edges[XYPos(-1 - p.x, p.y)];
    assert (p.inside(size));
    return vals[p].clue;
}

static std::list<GridRule> global_rules;

void Grid::solve_easy()
{
    if (global_rules.empty())
    {
        std::string sin = "[]";
        SaveObject* sobj = SaveObject::load(sin);
        SaveObjectList* rlist = sobj->get_list();
        for (int i = 0; i < rlist->get_count(); i++)
        {
            global_rules.push_back(GridRule(rlist->get_item(i), 3));
        }
        for (GridRule& rule : global_rules)
        {
            rule.stale = true;
        }
    }

    bool rep = true;

    while (rep)
    {
        rep = false;
    	while (add_regions(-1)) {}
        add_new_regions();
        if (regions.size() > 1000)
            return;
        for (GridRule& rule : global_rules)
        {
            if (rule.apply_region_type.type == RegionType::VISIBILITY)
            {
                apply_rule(rule);
            }
        }
        for (GridRule& rule : global_rules)
        {
            if (rule.apply_region_type.type == RegionType::VISIBILITY)
                continue;
            while (apply_rule(rule) != APPLY_RULE_RESP_NONE)
                rep = true;
        }
        for (GridRegion& r : regions)
        {
            r.stale = true;
        }
    }
}

bool Grid::is_solveable()
{
    bool rep = true;

    XYPos best_pos;


    while (rep && !is_solved())
    {
        rep = false;
        solve_easy();

        unsigned hidden  = 0;
        XYSet grid_squares = get_squares();
        FOR_XY_SET(p, grid_squares)
            if (!vals[p].revealed)
                hidden++;

//        count_revealed = (hidden <= 8);

        FOR_XY_SET(p, grid_squares)
        {
            if (!vals[p].revealed)
            {
                if (is_determinable(p))
                {
                    reveal(p);
                    rep = true;
                    solve_easy();
                }
            }
        }
    }
    return is_solved();
}

bool Grid::is_determinable(XYPos q)
{
    LocalGrid tst = *this;
    tst->regions.clear();
    tst->regions_set.clear();

    while (tst->add_regions(-1)) {}
    tst->add_new_regions();

    return tst->is_determinable_using_regions(q);
}

bool Grid::is_determinable_using_regions(XYPos q, bool hidden)
{
    std::map <XYPos, unsigned> pos_to_set;
    unsigned set_index = 10000000;

    for (GridRegion& r : regions)
    {
        if ((r.vis_level != GRID_VIS_LEVEL_SHOW) && hidden)
          continue;
        FOR_XY_SET (p, r.elements)
        {
            set_index++;
            unsigned v = pos_to_set[p];
            FOR_XY_SET (p2, r.elements)
            {
                if (pos_to_set[p2] == v)
                    pos_to_set[p2] = set_index;
            }
        }
    }

    set_index = 1;

    for (auto &[key, value] : pos_to_set)
    {
        unsigned v = value;
        if (v >= set_index)
        {
            for (auto &[key1, value1] : pos_to_set)
            {
                if (value1 == v)
                    value1 = set_index;
            }
            set_index++;
        }
    }

    unsigned si = pos_to_set[q];
    if (si == 0)
        return false;

    unsigned bom_count = 0;
    unsigned clr_count = 0;

    for (auto &[key, value] : pos_to_set)
    {
        if (value == si)
        {
            if (get(key).bomb)
                bom_count++;
            else
                clr_count++;
        }
    }

    assert(bom_count || clr_count);
    if (bom_count && clr_count)
        return false;


    z3::context c;
    z3::solver s(c);

    std::string uid;

    std::vector<unsigned> set_size(set_index);
    for (auto &[key, value] : pos_to_set)
    {
        set_size[value]++;
    }

    z3::expr_vector vec(c);
    vec.push_back(c.bool_const("DUMMY"));

    for (int i = 1; i < set_index; i++)
    {
        std::stringstream x_name;
        x_name << "S" << i;
        vec.push_back(c.int_const(x_name.str().c_str()));
        s.add(vec[i] >= 0);
        s.add(vec[i] <= int(set_size[i]));
        assert(set_size[i]);
        uid += "S" + std::to_string(set_size[i]);
    }

    for (GridRegion& r : regions)
    {
        if ((r.vis_level != GRID_VIS_LEVEL_SHOW) && hidden)
          continue;
        std::set<unsigned> seen;
        z3::expr e = c.int_val(0);
        uid += "E";
        FOR_XY_SET (p, r.elements)
        {
            unsigned si = pos_to_set[p];
            if (!seen.count(si))
            {
                seen.insert(si);
                e = e + vec[si];
                uid += std::to_string(si) + ",";
            }
        }
        s.add(r.type.apply_z3_rule(e));
        uid += std::to_string(r.type.as_int());
    }

    if (bom_count)
    {
        assert(bom_count == set_size[si]);
        s.add(vec[si] < int(bom_count));
    }
    else if (clr_count)
    {
        assert(clr_count == set_size[si]);
        s.add(vec[si] > 0);
    }
    uid += "F" + std::to_string(si) + std::to_string(bool(bom_count));

    static std::map<std::string, bool> solution_cache;

    global_mutex_lock();
    bool det = solution_cache.count(uid);
    global_mutex_unlock();

    if (det)
        return solution_cache[uid];

    det = (s.check() != z3::sat);

    global_mutex_lock();
    solution_cache[uid] = det;
    global_mutex_unlock();

    return det;
}

// static std::set<std::string> solution_cache;
// static std::set<std::string> no_solution_cache;

//bool Grid::has_solution(void)
// {
// //    std::string str = to_string();
//
// //   if (solution_cache.count(str))
// //       return true;
// //   if (no_solution_cache.count(str))
// //       return false;
//     z3::context c;
//     z3::expr_vector vec(c);
//     std::map<XYPos, unsigned> vec_index;
//     XYSet grid_squares = get_squares();
//     FOR_XY_SET(p, grid_squares)
//     {
//         if (!vals[p].revealed)
//         {
//             vec_index[p] = vec.size();
//             std::stringstream x_name;
//             x_name << (char)('A' + p.y)  << p.x;
//             vec.push_back(c.bool_const(x_name.str().c_str()));
//         }
//     }
//     z3::solver s(c);
//
//     int hidden = 0;
//     FOR_XY_SET(p, grid_squares)
//     {
//         if (!vals[p].revealed)
//             hidden++;
//     }
//
// //     if (count_revealed) // && (hidden < 12))
// //     {
// //         z3::expr_vector t(c);
// //         int cnt = 0;
// //         int cntn = 0;
// //         FOR_XY(p, XYPos(), size)
// //         {
// //             if (!vals[p].revealed)
// //             {
// //                 t.push_back(vec[vec_index[p]]);
// //                 if (vals[p].bomb)
// //                     cnt++;
// //                 else
// //                     cntn++;
// //
// //             }
// //         }
// //         cnt-=count_dec;
// //         if (cnt < 0)
// //         {
// // //            no_solution_cache.insert(str);
// // //            printf("cnt < 0\n");
// //             return false;
// //         }
// // //        printf("cnt: %d %d\n", cnt, cntn);
// //
// //         if (t.size() == 0)
// //         {
// // //            no_solution_cache.insert(str);
// // //            printf("t.size() == 0\n");
// //             return false;
// //         }
// //         else
// //         {
// //             s.add(atleast(t, cnt));
// //             s.add(atmost(t, cnt));
// //         }
// //     }
//
//     FOR_XY_SET(p, grid_squares)
//     {
//         if (vals[p].revealed && !vals[p].bomb)
//         {
//             RegionType clue = vals[p].clue;
//             if (clue.type  == RegionType::NONE)
//                 continue;
//             int cnt = clue.value;
//             z3::expr_vector t(c);
//             FOR_XY(offset, XYPos(-1,-1), XYPos(2,2))
//             {
//                 XYPos n = p + offset;
//                 if (!get(n).revealed)
//                 {
//                     t.push_back(vec[vec_index[n]]);
//                 }
//                 else if (get(n).bomb)
//                 {
//                     if (cnt == 0)
//                     {
//                         if (clue.type == RegionType::XOR2)
//                             continue;
//                         if (clue.type == RegionType::XOR3)
//                             continue;
//                         if (clue.type == RegionType::MORE)
//                             continue;
// //                        no_solution_cache.insert(str);
// //            	        printf("cnt < 0\n");
//                         return false;
//                     }
//                     cnt--;
//                 }
//             }
//
//
//             if (t.size())
//             {
//                 if (clue.type == RegionType::LESS)
//                 {
//                     s.add(atmost(t, cnt));
//                 }
//                 if (clue.type == RegionType::MORE)
//                 {
//                     s.add(atleast(t, cnt));
//                 }
//                 if (clue.type == RegionType::EQUAL)
//                 {
//                     s.add(atleast(t, cnt));
//                     s.add(atmost(t, cnt));
//                 }
//                 if (clue.type != RegionType::XOR2)
//                     if (cnt >= 0)
//                     {
//                         s.add((atmost(t, cnt) && atmost(t, cnt)) || (atmost(t, cnt + 2) && atmost(t, cnt + 2)));
//                     }
//
//                     if (cnt >= -2)
//                     {
//                         s.add(atleast(t, cnt + 2));
//                         s.add(atmost(t, cnt + 2));
//                     }
//                     if (clue.type != RegionType::XOR3)
//                         if (cnt >= 0)
//                         {
//                             s.add((atmost(t, cnt) && atmost(t, cnt)) || (atmost(t, cnt + 3) && atmost(t, cnt + 3)));
//                         }
//
//                         if (cnt >= -3)
//                         {
//                             s.add(atleast(t, cnt + 3));
//                             s.add(atmost(t, cnt + 3));
//                         }
//
// //                s.add(sum(t) == cnt);
//             }
//             else
//             {
//                 if (cnt && ((clue.type == RegionType::MORE) || (clue.type == RegionType::EQUAL)))
//                 {
// //                    no_solution_cache.insert(str);
// //            	    printf("cnt but all taken\n");
//                     return false;
//                 }
//                 if (cnt != 0 && cnt != -2 && (clue.type == RegionType::XOR2))
//                 {
//                     return false;
//                 }
//                 if (cnt != 0 && cnt != -3 && (clue.type == RegionType::XOR3))
//                 {
//                     return false;
//                 }
//             }
//         }
//     }
// //    printf("pos:%s\n", (s.check() == z3::sat) ? "sat" : "unsat");
//     if (s.check() == z3::sat)
//     {
// //        solution_cache.insert(str);
//         return true;
//     }
//     else
//     {
// //        no_solution_cache.insert(str);
//         return false;
//     }
// }

void Grid::make_harder(bool plus_minus, bool x_y, bool misc)
{

    XYSet grid_squares = get_squares();

    {
        std::vector<XYPos> tgt;
        FOR_XY_SET(p, grid_squares)
        {
            if (vals[p].revealed)
                tgt.push_back(p);
        }

        std::shuffle(tgt.begin(), tgt.end(), rnd.gen);

        for (XYPos p : tgt)
        {
            LocalGrid tst = *this;
            tst->vals[p].revealed = false;
            if (tst->is_solveable())
            {
                vals[p].revealed = false;
            }
        }
    }

    {
        std::vector<XYPos> tgt;
        FOR_XY_SET(p, grid_squares)
        {
            if (!vals[p].bomb)
                tgt.push_back(p);
        }

        for (auto const& [pos, type] : edges)
        {
            tgt.push_back(XYPos(-1 - pos.x, pos.y));
        }


        std::shuffle(tgt.begin(), tgt.end(), rnd.gen);

        for (XYPos p : tgt)
        {
            LocalGrid tst;
            {
                tst = *this;
                tst->get_clue(p).type = RegionType::NONE;
                if (tst->is_solveable())
                {
                    get_clue(p).type = RegionType::NONE;
                    continue;
                }
            }
            if (misc)
            {
                if (rnd % 10 < 4)
                {
                    tst = *this;
                    tst->get_clue(p).type = RegionType::NOTEQUAL;
                    tst->get_clue(p).value += 2;
                    if (tst->is_solveable())
                    {
                        get_clue(p) = tst->get_clue(p);
                        continue;
                    }
                }
                if (rnd % 10 < 4 && (get_clue(p).value >= 3))
                {
                    tst = *this;
                    tst->get_clue(p).type = RegionType::NOTEQUAL;
                    tst->get_clue(p).value -= 2;
                    if (tst->is_solveable())
                    {
                        get_clue(p) = tst->get_clue(p);
                        continue;
                    }
                }
                if (rnd % 10 < 4 && (get_clue(p).value >= 2))
                {
                    tst = *this;
                    tst->get_clue(p).type = RegionType::NOTEQUAL;
                    tst->get_clue(p).value -= 1;
                    if (tst->is_solveable())
                    {
                        get_clue(p) = tst->get_clue(p);
                        continue;
                    }
                }
                if (rnd % 10 < 4)
                {
                    tst = *this;
                    tst->get_clue(p).type = RegionType::NOTEQUAL;
                    tst->get_clue(p).value += 1;
                    if (tst->is_solveable())
                    {
                        get_clue(p) = tst->get_clue(p);
                        continue;
                    }
                }

                if (rnd % 10 < 2 && (get_clue(p).value >= 2))
                {
                    tst = *this;
                    tst->get_clue(p).type = RegionType::XOR22;
                    tst->get_clue(p).value -= 2;
                    if (tst->is_solveable())
                    {
                        get_clue(p) = tst->get_clue(p);
                        continue;
                    }
                }
                if (rnd % 10 < 2)
                {
                    tst = *this;
                    tst->get_clue(p).type = RegionType::XOR22;
                    if (tst->is_solveable())
                    {
                        get_clue(p) = tst->get_clue(p);
                        continue;
                    }
                }
                if (rnd % 10 < 2 && (get_clue(p).value >= 4))
                {
                    tst = *this;
                    tst->get_clue(p).type = RegionType::XOR22;
                    tst->get_clue(p).value -= 4;
                    if (tst->is_solveable())
                    {
                        get_clue(p) = tst->get_clue(p);
                        continue;
                    }
                }
                if (rnd % 10 < 2 && (get_clue(p).value >= 2))
                {
                    tst = *this;
                    tst->get_clue(p).type = RegionType::XOR222;
                    tst->get_clue(p).value -= 2;
                    if (tst->is_solveable())
                    {
                        get_clue(p) = tst->get_clue(p);
                        continue;
                    }
                }
                if (rnd % 10 < 2 && (get_clue(p).value >= 4))
                {
                    tst = *this;
                    tst->get_clue(p).type = RegionType::XOR222;
                    tst->get_clue(p).value -= 4;
                    if (tst->is_solveable())
                    {
                        get_clue(p) = tst->get_clue(p);
                        continue;
                    }
                }
                if (rnd % 10 < 2)
                {
                    tst = *this;
                    tst->get_clue(p).type = RegionType::XOR222;
                    if (tst->is_solveable())
                    {
                        get_clue(p) = tst->get_clue(p);
                        continue;
                    }
                }
                if (rnd % 10 < 2 && (get_clue(p).value >= 6))
                {
                    tst = *this;
                    tst->get_clue(p).type = RegionType::XOR222;
                    tst->get_clue(p).value -= 6;
                    if (tst->is_solveable())
                    {
                        get_clue(p) = tst->get_clue(p);
                        continue;
                    }
                }
            }
            if (x_y)
            {
                if (rnd % 10 < 2)
                {
                    tst = *this;
                    tst->get_clue(p).type = RegionType::XOR3;
                    if (tst->is_solveable())
                    {
                        get_clue(p) = tst->get_clue(p);
                        continue;
                    }
                }

                if ((rnd % 10 < 2) && (get_clue(p).value >= 3))
                {
                    tst = *this;
                    tst->get_clue(p).type = RegionType::XOR3;
                    tst->get_clue(p).value -= 3;
                    if (tst->is_solveable())
                    {
                        get_clue(p) = tst->get_clue(p);
                        continue;
                    }
                }

                if (rnd % 10 < 2)
                {
                    tst = *this;
                    tst->get_clue(p).type = RegionType::XOR2;
                    if (tst->is_solveable())
                    {
                        get_clue(p) = tst->get_clue(p);
                        continue;
                    }
                }

                if ((rnd % 10 < 2) && (get_clue(p).value >= 2))
                {
                    tst = *this;
                    tst->get_clue(p).type = RegionType::XOR2;
                    tst->get_clue(p).value -= 2;
                    if (tst->is_solveable())
                    {
                        get_clue(p) = tst->get_clue(p);
                        continue;
                    }
                }
            }

            if (plus_minus)
            {
                if (rnd % 10 < 5)
                {
                    tst = *this;
                    tst->get_clue(p).type = RegionType::LESS;
                    if (tst->is_solveable())
                    {
                        get_clue(p) = tst->get_clue(p);
                        while (true)
                        {
                            tst = *this;
                            tst->get_clue(p).type = RegionType::LESS;
                            tst->get_clue(p).value++;
                            if (!tst->is_solveable())
                                break;
                            if (tst->vals[p].clue.value > 19)
                            {
                                assert(0);
                            }
                            printf("tst->get_clue(p).value %d++ \n", tst->get_clue(p).value);
                            if (tst->get_clue(p).value)
                            {
                                get_clue(p).type = RegionType::LESS;
                                get_clue(p).value = tst->get_clue(p).value;
                            }
                        }
                        continue;
                    }
                    tst = *this;
                    tst->get_clue(p).type = RegionType::MORE;
                    if (tst->is_solveable())
                    {
                        get_clue(p) = tst->get_clue(p);
                        while (true)
                        {
                            tst = *this;
                            tst->get_clue(p).type = RegionType::MORE;
                            tst->get_clue(p).value--;
                            if (!tst->is_solveable())
                                break;
                            printf("tst->get_clue(p).value %d-- \n", tst->get_clue(p).value);
                            get_clue(p).type = RegionType::MORE;
                            get_clue(p).value = tst->get_clue(p).value;
                        }
                        continue;
                    }
                }
            }
        }
    }

}

void Grid::reveal(XYPos p)
{
    vals[p].revealed = true;

    std::list<GridRegion>::iterator it = regions.begin();
    while (it != regions.end())
    {
        if((*it).elements.get(p))
        {
            (*it).deleted = true;
            regions_set.erase(&*it);
            std::list<GridRegion>::iterator old_it = it;
            old_it++;
            deleted_regions.splice(deleted_regions.end(),regions, it);
            it = old_it;
        }
        else
            it++;
    }

    it = regions_to_add.begin();
    while (it != regions_to_add.end())
    {
        GridRegion* rp = &(*it);
        if(rp->elements.get(p))
        {
            remove_from_regions_to_add_multiset(&(*it));
            it = regions_to_add.erase(it);
        }
        else
            ++it;
    }
}

std::string Grid::to_string()
{
    std::string s;

    s += 'A' + size.x;
    s += 'A' + size.y;
    s += 'A' + int(wrapped);
    if (wrapped == WRAPPED_IN)
    {
        s += '0' + innie_pos.x;
        s += '0' + innie_pos.y;
    }

    for ( const auto &m_reg : merged )
    {
        s += '#';
        s += '0' + m_reg.first.x;
        s += '0' + m_reg.first.y;
        s += '0' + m_reg.second.x;
        s += '0' + m_reg.second.y;
    }

    for ( const auto &m_reg : edges )
    {
        s += '|';
        s += '0' + m_reg.first.x;
        s += '0' + m_reg.first.y;
        s += 'A' + m_reg.second.type;
        s += '0' + m_reg.second.value;
    }

    XYSet grid_squares = get_squares();
    FOR_XY_SET(p, grid_squares)
    {
        GridPlace g = vals[p];
        if (!g.revealed)
        {
            s += '_';
        }

        if (g.bomb)
        {
            s += '!';
        }
        else
        {
            char c = 'A' + (char)(g.clue.type);
            s += c;
            c = '0' + g.clue.value;
            s += c;
        }
    }
    return s;
}

bool Grid::is_solved(void)
{
    XYSet grid_squares = get_squares();
    FOR_XY_SET(p, grid_squares)
    {
        if (!vals[p].revealed)
            return false;
    }
    return true;
}

bool Grid::add_region(GridRegion& reg, bool front)
{
    if (regions_set.count(&reg)) 
        return false;
    int cnt = 0;
    const auto [start, end] = regions_to_add_multiset.equal_range(&reg);
    for (auto i{start}; i != end; i++)
    {
        GridRegion& r = **i;
        cnt++;
        if (cnt > 100)
            return false;
        if (r.gen_cause == reg.gen_cause)
        {
            return false;
        }
    }

    if (front)
    {
        regions_to_add.push_front(reg);
        regions_to_add_multiset.insert(&regions_to_add.front());
    }
    else
    {
        regions_to_add.push_back(reg);
        regions_to_add_multiset.insert(&regions_to_add.back());
    }
    return true;
}

bool Grid::add_region(XYSet& elements, RegionType clue)
{
    if (!elements.count())
        return false;
    if (clue.value < 0 && clue.type == RegionType::XOR222)
    {
        clue.value += 2;
        clue.type = RegionType::XOR22;
    }
    if (clue.value < 0 && clue.type == RegionType::XOR22)
    {
        clue.value += 2;
        clue.type = RegionType::XOR2;
    }
    if (clue.value < 0 && clue.value >= -2 && clue.type == RegionType::XOR2)
    {
        clue.value += 2;
        clue.type = RegionType::EQUAL;
    }
    if (clue.value < 0 && clue.value >= -3 && clue.type == RegionType::XOR3)
    {
        clue.value += 3;
        clue.type = RegionType::EQUAL;
    }
    if (clue.value < 0 && clue.type == RegionType::NOTEQUAL)
    {
        return false;
    }
    if (clue.value < 0 && clue.type == RegionType::MORE)
    {
        return false;
    }
    assert (clue.value >= 0);
    GridRegion reg(clue);
    reg.elements = elements;
    return add_region(reg, true);
}

bool Grid::add_regions(int level)
{
    for (const auto &edg : edges)
    {
        XYPos e_pos = edg.first;
        RegionType clue = edg.second;
        if (clue.type == RegionType::NONE)
            continue;
        XYSet line = get_row(e_pos.x, e_pos.y);
        XYSet elements;
        FOR_XY_SET(n, line)
        {
            if (!get(n).revealed)
            {
                elements.set(n);
            }
            else if (get(n).bomb)
            {
                clue.value--;
            }
        }
        if (add_region(elements, clue))
            return true;
    }

    XYSet grid_squares = get_squares();
    FOR_XY_SET(p, grid_squares)
    {
        XYSet elements;
        GridPlace g = vals[p];
        if (g.revealed && !g.bomb && ((g.clue.type != RegionType::NONE)))
        {
            RegionType clue = g.clue;
            if (!vals[p].bomb)
            {
                int cnt = 0;
                XYSet neigh = get_neighbors(p);
                FOR_XY_SET(n, neigh)
                {
                    if (!get(n).revealed)
                        elements.set(n);
                    else if (get(n).bomb)
                        clue.value--;
                }
            }
            if (add_region(elements, clue))
                return true;
        }
    }
    return false;
}

static void add_clear_count(GridRegion* region, int count)
{
    if (!region)
        return;
    if (region->gen_cause.rule)
    {
        region->gen_cause.rule->clear_count += count;
        for (int i = 0; i < 4; i++)
            add_clear_count(region->gen_cause.regions[i], count);
    }
}

Grid::ApplyRuleResp Grid::apply_rule(GridRule& rule, GridRegion* r1, GridRegion* r2, GridRegion* r3, GridRegion* r4)
{
    if (rule.deleted)
        return APPLY_RULE_RESP_NONE;
    assert(rule.apply_region_bitmap);
    if (rule.apply_region_type.type == RegionType::VISIBILITY)
    {
        GridVisLevel vis_level =  GridVisLevel(rule.apply_region_type.value);
        if ((rule.apply_region_bitmap & 1) && r1->visibility_force != GridRegion::VIS_FORCE_USER)
        {
            r1->vis_level = vis_level;
            r1->vis_cause = GridRegionCause(&rule, r1, r2, r3, r4);
        }
        if ((rule.apply_region_bitmap & 2) && r2->visibility_force != GridRegion::VIS_FORCE_USER)
        {
            r2->vis_level = vis_level;
            r2->vis_cause = GridRegionCause(&rule, r1, r2, r3, r4);
        }
        if ((rule.apply_region_bitmap & 4) && r3->visibility_force != GridRegion::VIS_FORCE_USER)
        {
            r3->vis_level = vis_level;
            r3->vis_cause = GridRegionCause(&rule, r1, r2, r3, r4);
        }
        if ((rule.apply_region_bitmap & 8) && r4->visibility_force != GridRegion::VIS_FORCE_USER)
        {
            r4->vis_level = vis_level;
            r4->vis_cause = GridRegionCause(&rule, r1, r2, r3, r4);
        }
        return APPLY_RULE_RESP_NONE;
    }

    XYSet itter_elements;
    GridRegion* itter = NULL;
    FOR_XY_SET(pos, r1->elements)
        itter_elements.insert(pos);
    if (r2)
        FOR_XY_SET(pos, r2->elements)
            itter_elements.insert(pos);
    if (r3)
        FOR_XY_SET(pos, r3->elements)
            itter_elements.insert(pos);
    if (r4)
        FOR_XY_SET(pos, r4->elements)
            itter_elements.insert(pos);

    XYSet to_reveal;

    FOR_XY_SET(pos, itter_elements)
    {
        unsigned m = 0;
        if (r1->elements.get(pos))
            m |= 1;
        if (r2 && (r2->elements.get(pos)))
            m |= 2;
        if (r3 && (r3->elements.get(pos)))
            m |= 4;
        if (r4 && (r4->elements.get(pos)))
            m |= 8;
        if ((rule.apply_region_bitmap >> m) & 1)
            to_reveal.set(pos);
    }
    if (to_reveal.empty())
        return APPLY_RULE_RESP_NONE;

    if (rule.apply_region_type.type == RegionType::SET)
    {
        FOR_XY_SET(pos, to_reveal)
        {
            if (vals[pos].bomb != bool(rule.apply_region_type.value))
            {
                printf("wrong\n");
                assert(0);
                return APPLY_RULE_RESP_ERROR;
            }
        }
        int c = 0;
        FOR_XY_SET(pos, to_reveal)
        {
            reveal(pos);
            c++;
        }
        rule.used_count++;
        rule.clear_count += c;
        add_clear_count(r1, c);
        add_clear_count(r2, c);
        add_clear_count(r3, c);
        add_clear_count(r4, c);
        return APPLY_RULE_RESP_HIT;
    }
    else
    {
        GridRegion reg(rule.apply_region_type);
        FOR_XY_SET(pos, to_reveal)
        {
            reg.elements.set(pos);
        }
        reg.gen_cause = GridRegionCause(&rule, r1, r2, r3, r4);
        bool added = add_region(reg);
        if (added)
        {
            rule.used_count++;
            return APPLY_RULE_RESP_HIT;
        }
        else
            return APPLY_RULE_RESP_NONE;
    }
    assert(0);
    return APPLY_RULE_RESP_HIT;
}

static void find_connected(GridRegion* start, unsigned& connected, GridRegion* r1, GridRegion* r2, GridRegion* r3, GridRegion* r4)
{
    if (r1 != start && !(connected & 1) && start->overlaps(*r1))
    {
        connected |= 1;
        find_connected(r1, connected, r1, r2, r3, r4);
    }
    if (!r2)
        return;
    if (r2 != start && !(connected & 2) && start->overlaps(*r2))
    {
        connected |= 2;
        find_connected(r2, connected, r1, r2, r3, r4);
    }
    if (!r3)
        return;
    if (r3 != start && !(connected & 4) && start->overlaps(*r3))
    {
        connected |= 4;
        find_connected(r3, connected, r1, r2, r3, r4);
    }
    if (!r4)
        return;
    if (r4 != start && !(connected & 8) && start->overlaps(*r4))
    {
        connected |= 8;
        find_connected(r4, connected, r1, r2, r3, r4);
    }
}

static bool are_connected(GridRegion* r0, GridRegion* r1, GridRegion* r2, GridRegion* r3)
{
    XYSet s = r0->elements;
    unsigned int connected = 1 << 0;
    bool hit;
    do
    {
        hit = false;
        if (r1 && !(connected & (1 << 1)) && s.overlaps(r1->elements))
        {
            connected |= (1 << 1);
            s = s | r1->elements;
            hit = true;
        }
        if (r2 && !(connected & (1 << 2)) && s.overlaps(r2->elements))
        {
            connected |= (1 << 2);
            s = s | r2->elements;
            hit = true;
        }
        if (r3 && !(connected & (1 << 3)) && s.overlaps(r3->elements))
        {
            connected |= (1 << 3);
            s = s | r3->elements;
            hit = true;
        }
    }
    while (hit);
    if (!r1)
        return true;
    if (!r2 && connected == 3)
        return true;
    if (!r3 && connected == 7)
        return true;
    if (connected == 0xf)
        return true;
    return false;
}


Grid::ApplyRuleResp Grid::apply_rule(GridRule& rule, bool force)
{
    if (rule.deleted)
        return APPLY_RULE_RESP_NONE;
    std::set<GridRegion*> unstale_regions;
    for (GridRegion& r : regions)
    {
        if (!r.stale)
            unstale_regions.insert(&r);
    }
    if (unstale_regions.empty() && rule.stale && !force)
        return APPLY_RULE_RESP_NONE;

    bool ignore_bin = (rule.apply_region_type.type == RegionType::VISIBILITY);
    assert(rule.region_count);
    for (GridRegion& r1 : regions)
    {
        if (r1.type != rule.region_type[0])
            continue;
        if (r1.vis_level == GRID_VIS_LEVEL_BIN && !ignore_bin)
            continue;
        if (rule.region_count == 1)
        {
            if (rule.stale && r1.stale && !force)
                continue;
            if (rule.matches(&r1, NULL, NULL, NULL))
            {
                ApplyRuleResp resp = apply_rule(rule, &r1, NULL, NULL, NULL);
                if (resp != APPLY_RULE_RESP_NONE)
                    return resp;
            }
        }
        else
        {
            for (GridRegion& r2 : regions)
            {
                if (r2.type != rule.region_type[1])
                    continue;
                if (r2 == r1)
                    continue;
                if (r2.vis_level == GRID_VIS_LEVEL_BIN && !ignore_bin)
                    continue;
                if (rule.region_count == 2)
                {
                    if (rule.stale && r1.stale && r2.stale && !force)
                        continue;
                    if (!r1.overlaps(r2))
                        continue;
                    if (rule.matches(&r1, &r2, NULL, NULL))
                    {
                        ApplyRuleResp resp = apply_rule(rule, &r1, &r2, NULL, NULL);
                        if (resp != APPLY_RULE_RESP_NONE)
                            return resp;
                    }
                }
                else
                {
                    for (GridRegion& r3 : regions)
                    {
                        if (r3.type != rule.region_type[2])
                            continue;
                        if ((r3 == r1) || (r3 == r2))
                            continue;
                        if (r3.vis_level == GRID_VIS_LEVEL_BIN && !ignore_bin)
                            continue;
                        if(rule.region_count == 3)
                        {
                            if (rule.stale && r1.stale && r2.stale && r3.stale && !force)
                                continue;
                            unsigned connected = 1;
                            find_connected(&r1, connected, &r1, &r2, &r3, NULL);
                            if (connected != 0x7)
                                continue;
                            if (rule.matches(&r1, &r2, &r3, NULL))
                            {
                                ApplyRuleResp resp = apply_rule(rule, &r1, &r2, &r3, NULL);
                                if (resp != APPLY_RULE_RESP_NONE)
                                    return resp;
                            }
                        }
                        else
                        {

                            for (GridRegion& r4 : regions)
                            {
                                if (r4.type != rule.region_type[3])
                                    continue;
                                if ((r4 == r1) || (r4 == r2) || (r4 == r3))
                                    continue;
                                if (r4.vis_level == GRID_VIS_LEVEL_BIN && !ignore_bin)
                                    continue;
                                assert (rule.region_count == 4);
                                {
                                    if (rule.stale && r1.stale && r2.stale && r3.stale && r4.stale && !force)
                                        continue;
                                    unsigned connected = 1;
                                    find_connected(&r1, connected, &r1, &r2, &r3, &r4);
                                    if (connected != 0xF)
                                        continue;
                                    if (rule.matches(&r1, &r2, &r3, &r4))
                                    {
                                        ApplyRuleResp resp = apply_rule(rule, &r1, &r2, &r3, &r4);
                                        if (resp != APPLY_RULE_RESP_NONE)
                                            return resp;
                                    }
                                }
                            }

                        }
                    }
                }
            }
        }
    }
    return APPLY_RULE_RESP_NONE;
}

Grid::ApplyRuleResp Grid::apply_rule(GridRule& rule, GridRegion* unstale_region)
{
    if (rule.deleted)
        return APPLY_RULE_RESP_NONE;
    bool ignore_bin = (rule.apply_region_type.type == RegionType::VISIBILITY);

    assert(rule.region_count);
    unsigned places_for_reg = 0;
    if (unstale_region)
    {
        for (int i = 0; i < rule.region_count; i++)
        {
            if (unstale_region->type == rule.region_type[i])
                places_for_reg |= 1 << i;
        }
        if (!places_for_reg)
            return APPLY_RULE_RESP_NONE;
    }
    else
        places_for_reg = 1;

    std::vector<GridRegion*> pos_regions[4];
    for (int i = 0; i < 4; i++)
    {
        if (i >= rule.region_count)
            pos_regions[i].push_back(NULL);
        else
        {
            for (GridRegion& r : regions)
            {
                if (r.type != rule.region_type[i])
                    continue;
                if (r.vis_level == GRID_VIS_LEVEL_BIN && !ignore_bin)
                    continue;

                pos_regions[i].push_back(&r);
            }
        }
    }

    std::vector<GridRegion*> unstale_regions;
    unstale_regions.push_back(unstale_region);
    ApplyRuleResp rep = APPLY_RULE_RESP_NONE;

    for (int nonstale_rep_index = 0; nonstale_rep_index < rule.region_count; nonstale_rep_index++)
    {
        if ((places_for_reg >> nonstale_rep_index) & 1)
        {
            std::vector<GridRegion*>& set0 = (unstale_region && (nonstale_rep_index == 0)) ? unstale_regions : pos_regions[0];
            for (GridRegion* r0 : set0)
            {
                std::vector<GridRegion*>& set1 = (nonstale_rep_index == 1) ? unstale_regions : pos_regions[1];
                for (GridRegion* r1 : set1)
                {
                    std::vector<GridRegion*>& set2 = (nonstale_rep_index == 2) ? unstale_regions : pos_regions[2];
                    for (GridRegion* r2 : set2)
                    {
                        std::vector<GridRegion*>& set3 = (nonstale_rep_index == 3) ? unstale_regions : pos_regions[3];
                        for (GridRegion* r3 : set3)
                        {
                            if (r1 && ((r0 == r1) || (r2 == r1) || (r3 == r1))) continue;
                            if (r2 && ((r0 == r2) || (r1 == r2) || (r3 == r2))) continue;
                            if (r3 && ((r0 == r3) || (r1 == r3) || (r2 == r3))) continue;
                            if (!are_connected(r0, r1, r2, r3)) continue;
                            if (rule.matches(r0, r1, r2, r3))
                            {
                                ApplyRuleResp resp = apply_rule(rule, r0, r1, r2, r3);
                                if ((resp != APPLY_RULE_RESP_NONE) && (rule.apply_region_type.type == RegionType::SET))
                                    return resp;
                                if (resp == APPLY_RULE_RESP_HIT)
                                    rep = APPLY_RULE_RESP_HIT;

                            }
                        }
                    }
                }
            }
        }
    }
    return rep;
}

void Grid::remove_from_regions_to_add_multiset(GridRegion* r)
{
    const auto [start, end] = regions_to_add_multiset.equal_range(r);
    for (auto i{start}; i != end; i++)
    {
        if (*i == r)
        {
            regions_to_add_multiset.erase(i);
            return;
        }
    }
}

void Grid::add_new_regions()
{
    for (GridRegion& r :regions_to_add)
        regions_set.insert(&r);
    regions.splice(regions.end(), regions_to_add);
    regions_to_add_multiset.clear();
}

bool Grid::add_one_new_region(GridRegion* r)
{
    std::list<GridRegion>::iterator it = regions_to_add.begin();
    for (std::list<GridRegion>::iterator it = regions_to_add.begin(); it != regions_to_add.end(); it++)
    {
        if (r && !(*it).has_ancestor(r))
            continue;
        GridRegionCause c = (*it).gen_cause;
        if (regions_set.count(&*it) ||
            (
            (c.regions[0] && c.regions[0]->vis_level == GRID_VIS_LEVEL_BIN) ||
            (c.regions[1] && c.regions[1]->vis_level == GRID_VIS_LEVEL_BIN) ||
            (c.regions[2] && c.regions[2]->vis_level == GRID_VIS_LEVEL_BIN) ||
            (c.regions[3] && c.regions[3]->vis_level == GRID_VIS_LEVEL_BIN)))
        {
            remove_from_regions_to_add_multiset(&(*it));
            it = regions_to_add.erase(it);
        }
        else
        {
            remove_from_regions_to_add_multiset(&(*it));
            regions_set.insert(&(*it));
            regions.splice(regions.end(), regions_to_add, it);
            return true;
        }
    }
    if (r)
        return add_one_new_region(NULL);
    return false;
}

void Grid::clear_regions()
{
    regions.clear();
    regions_set.clear();
    regions_to_add.clear();
    regions_to_add_multiset.clear();
    deleted_regions.clear();
}

std::string SquareGrid::to_string()
{
    return "A" + Grid::to_string();
}

XYSet SquareGrid::get_squares()
{
    XYSet rep;
    FOR_XY(pos, XYPos(), size)
        rep.set(pos);
    if (wrapped == WRAPPED_IN)
        rep.clear(innie_pos);
    for ( const auto &m_reg : merged )
    {
        FOR_XY(pos, m_reg.first, m_reg.first + m_reg.second)
        {
            if (pos == m_reg.first)
                continue;
            rep.clear(pos);
        }
    }

    return rep;
}

XYSet SquareGrid::get_row(unsigned type, int index)
{
    XYSet rep;
    if (type == 0)
    {
        for (int x = 0; x < size.x; x++)
        {
            XYPos p = get_base_square(XYPos(x, index));
            rep.set(p);
        }
    }
    else
    {
        for (int y = 0; y < size.y; y++)
        {
            XYPos p = get_base_square(XYPos(index, y));
            rep.set(p);
        }
    }
    return rep;
}

void SquareGrid::get_edges(std::vector<EdgePos>& rep, XYPos grid_pitch)
{
    for (auto const& [pos, type] : edges)
    {
        if (pos.x == 0)
            rep.push_back(EdgePos (type, XYPosFloat(1,0).angle(), (pos.y + 0.5) * grid_pitch.y));
        if (pos.x == 1)
            rep.push_back(EdgePos (type, XYPosFloat(0,1).angle(), -(pos.y + 0.5) * grid_pitch.y));
    }
}

XYPos SquareGrid::get_square_from_mouse_pos(XYPos pos, XYPos grid_pitch)
{
    XYPos rep(pos / grid_pitch);
    if (wrapped == WRAPPED_SIDE)
        rep = rep % size;
    if (rep.inside(size))
        return rep;
    return XYPos(-1,-1);
}

XYSet SquareGrid::get_neighbors(XYPos p)
{
    XYSet rep;
    XYPos s = get_square_size(p);

    FOR_XY(o, XYPos(-1, -1), XYPos(2, 2))
    {
        if (o == XYPos(0,0))
            continue;
        XYPos start = o;
        XYPos end;

        if (o.x == 1)
            start.x = o.x + s.x - 1;
        if (o.y == 1)
            start.y = o.y + s.y - 1;
        end = start + XYPos(1, 1);
        if (o.x == 0)
            end.x = o.x + s.x;
        if (o.y == 0)
            end.y = o.y + s.y;

        start += p;
        end += p;
        FOR_XY(t, start, end)
        {
            if (wrapped == WRAPPED_SIDE)
            {
                rep.set(get_base_square(t % size));
            }
            else if (wrapped == WRAPPED_IN)
            {
                if (t.inside(size))
                {
                    XYPos tb = get_base_square(t);
                    if (tb == innie_pos)
                    {
                        XYPos tbs = get_square_size(tb);
                        XYPos chunk_size = size / tbs;
                        if (o.x && o.y)                     // diagonal 
                        {
                            XYPos corner_pos = (t - tb) * chunk_size;
                            if (o.x == -1)
                                corner_pos.x += chunk_size.x - 1;
                            if (o.y == -1)
                                corner_pos.y += chunk_size.y - 1;
                            rep.set(get_base_square(corner_pos));
                        }
                        else
                        {
                            XYPos sq_pos = (t - tb) * chunk_size;
                            if (o.x == 1)
                                for (int i = 0; i < chunk_size.y; i++)
                                    rep.set(get_base_square(sq_pos + XYPos(0, i)));
                            else if (o.x == -1)
                                for (int i = 0; i < chunk_size.y; i++)
                                    rep.set(get_base_square(sq_pos + XYPos(chunk_size.x - 1, i)));
                            else if (o.y == 1)
                                for (int i = 0; i < chunk_size.x; i++)
                                    rep.set(get_base_square(sq_pos + XYPos(i, 0)));
                            else if (o.y == -1)
                                for (int i = 0; i < chunk_size.y; i++)
                                    rep.set(get_base_square(sq_pos + XYPos(i, chunk_size.y - 1)));
                            else
                                assert(0);
                        }
                    }
                    else
                        rep.set(tb);
                }
                else
                {
                    XYPos tbs = get_square_size(innie_pos);
                    XYPos chunk_size = size / tbs;
                    XYPos op = innie_pos + t / chunk_size;
                    rep.set(get_base_square(op));
                }
            }
            else if (t.inside(size))
                rep.set(get_base_square(t));
        }
    }

    // for (int y = -1; y <= s.y; y++)
    // {
    //     XYPos t;
    //     t = p + XYPos(-1, y);
    //     if (wrapped == WRAPPED_SIDE)
    //         t = t % size;
    //     if (t.inside(size))
    //         rep.set(get_base_square(t));
    //     t = p + XYPos(s.x, y);
    //     if (wrapped == WRAPPED_SIDE)
    //         t = t % size;
    //     if (t.inside(size))
    //         rep.set(get_base_square(t));
    // }
    // for (int x = 0; x < s.x; x++)
    // {
    //     XYPos t;
    //     t = p + XYPos(x, -1);
    //     if (wrapped == WRAPPED_SIDE)
    //         t = t % size;
    //     if (t.inside(size))
    //         rep.set(get_base_square(t));
    //     t = p + XYPos(x, s.y);
    //     if (wrapped == WRAPPED_SIDE)
    //         t = t % size;
    //     if (t.inside(size))
    //         rep.set(get_base_square(t));
    // }
    return rep;
}

void SquareGrid::get_row_types(std::vector<XYPos>& rep)
{
    rep.push_back(XYPos(0, size.y));
    rep.push_back(XYPos(0, size.x));
}

XYPos SquareGrid::get_grid_pitch(XYPos grid_size)
{
    int s = std::min(grid_size.x / size.x, grid_size.y / size.y);
    s &= ~1;
    return XYPos(s,s);
}

XYRect SquareGrid::get_square_pos(XYPos pos, XYPos grid_pitch)
{
    return XYRect(pos * grid_pitch, get_square_size(pos) * grid_pitch);
}

XYRect SquareGrid::get_bubble_pos(XYPos pos, XYPos grid_pitch, unsigned index, unsigned total)
{
    XYPos size = get_square_size(pos);
    int min = std::min(size.x, size.y);
    XYPos offset = size - XYPos(min, min);
    unsigned s = 2 + min;
    while (total > (s * s))
        s++;
    XYPos p = (XYPos((index / s + index % s) % s, index % s) * grid_pitch) * min / s;
    return XYRect(pos * grid_pitch + offset * grid_pitch / 2 + p, XYPos(grid_pitch.x * min / s, grid_pitch.y * min / s));

}

void SquareGrid::render_square(XYPos pos, XYPos grid_pitch, std::vector<RenderCmd>& cmds)
{
    XYPos s = get_square_size(pos);
    {
        XYRect src(350, 1280, 1, 1);
        XYRect dst(pos * grid_pitch, grid_pitch * s);
        cmds.push_back({src,dst, true});
    }
    {
        XYRect src(5, 1024, 1, 1);
        XYRect dst;

        dst = XYRect ((pos + XYPos(0,   0)  ) * grid_pitch, XYPos(grid_pitch.x * s.x, grid_pitch.x / 24 + 1));
        cmds.push_back(RenderCmd(src, dst, 0,   XYPos(0,1)));
        dst = XYRect ((pos + XYPos(s.x, 0)  ) * grid_pitch, XYPos(grid_pitch.y * s.y, grid_pitch.x / 24 + 1));
        cmds.push_back(RenderCmd(src, dst, 90,  XYPos(0,1)));
        dst = XYRect ((pos + XYPos(s.x, s.y)) * grid_pitch, XYPos(grid_pitch.x * s.x, grid_pitch.x / 24 + 1));
        cmds.push_back(RenderCmd(src, dst, 180, XYPos(0,1)));
        dst = XYRect ((pos + XYPos(0,   s.y)) * grid_pitch, XYPos(grid_pitch.y * s.y, grid_pitch.x / 24 + 1));
        cmds.push_back(RenderCmd(src, dst, 270, XYPos(0,1)));
    }
}
void SquareGrid::add_random_merged(int merged_count)
{
    bool done_innie = (wrapped != WRAPPED_IN);
    for (int i = 0; i < merged_count;)
    {
        XYPos m_pos(unsigned(rnd) % size.x, unsigned(rnd) % size.y);
        XYPos m_size(1 + unsigned(rnd) % 3, 1 + unsigned(rnd) % 3);
        if (!(m_size + m_pos - XYPos(1,1)).inside(size))
            continue;
        if (m_size == XYPos(1,1))
            continue;
        bool bad = false;
        for ( const auto &m_reg : merged )
        {
            if ( (std::min(m_reg.first.x + m_reg.second.x, m_pos.x + m_size.x) > std::max(m_reg.first.x, m_pos.x)) &&
                 (std::min(m_reg.first.y + m_reg.second.y, m_pos.y + m_size.y) > std::max(m_reg.first.y, m_pos.y)) )
                bad = true;
        }
        if (bad)
            continue;
        if (!done_innie)
        {
            if (size.x % m_size.x) continue;
            if (size.y % m_size.y) continue;
            if ((size.y / m_size.y) != (size.x / m_size.x)) continue;
            if (m_pos.x == 0) continue;
            if (m_pos.y == 0) continue;
            if (!(m_size + m_pos).inside(size)) continue;
            done_innie = true;
            innie_pos = m_pos;
        }
        merged[m_pos] = m_size;
        i++;
    }
}

XYPos SquareGrid::get_square_size(XYPos p)
{
    for (const auto &m_reg : merged)
    {
        if (p == m_reg.first)
            return m_reg.second;
    }
    return XYPos(1,1);

}
XYPos SquareGrid::get_base_square(XYPos p)
{
    for (const auto &m_reg : merged)
    {
        if ((p - m_reg.first).inside(m_reg.second))
            return m_reg.first;
    }
    assert (p.inside(size));
    return p;
}

XYPos SquareGrid::get_wrapped_size(XYPos grid_pitch)
{
    return size * grid_pitch;
}

std::string TriangleGrid::to_string()
{
    return "B" + Grid::to_string();
}

XYSet TriangleGrid::get_squares()
{
    XYSet rep;
    FOR_XY(pos, XYPos(), size)
        rep.set(pos);
    for ( const auto &m_reg : merged )
    {
        FOR_XY(pos, m_reg.first, m_reg.first + m_reg.second)
        {
            if (pos == m_reg.first)
                continue;
            rep.clear(pos);
        }
    }

    return rep;
}

XYSet TriangleGrid::get_row(unsigned type, int index)
{
    XYSet rep;
    if (type == 0)
    {
        for (int x = 0; x < size.x; x++)
        {
            XYPos p = get_base_square(XYPos(x, index));
            rep.set(p);
        }
    }
    else if (type == 1)
    {
        for (int y = 0; y < size.y; y++)
        {
            int x = ((index - ((size.y - 1) / 2)) * 2) + y - 1;
            if (x >= 0 && x < size.x)
                rep.set(get_base_square(XYPos(x, y)));
            x++;
            if (x >= 0 && x < size.x)
                rep.set(get_base_square(XYPos(x, y)));
        }
    }
    else if (type == 2)
    {
        for (int y = 0; y < size.y; y++)
        {
            int x = (index * 2) - y;
            if (x >= 0 && x < size.x)
                rep.set(get_base_square(XYPos(x, y)));
            x++;
            if (x >= 0 && x < size.x)
                rep.set(get_base_square(XYPos(x, y)));
        }
    }
    else
    {
        assert(0);
    }
    return rep;
}

XYSet TriangleGrid::base_get_neighbors(XYPos pos)
{
    bool downwards = (pos.x ^ pos.y) & 1;
    XYSet rep;

    FOR_XY(offset, XYPos(-2,-1), XYPos(3,2))
    {
        if (offset == XYPos(-2, downwards ? 1 : -1)) continue;
        if (offset == XYPos(2, downwards ? 1 : -1)) continue;
        XYPos t = pos + offset;
        if (wrapped == WRAPPED_SIDE)
            t = t % size;
        if (!t.inside(size))
            continue;
        rep.set(get_base_square(t));
    }

    return rep;
}


XYSet TriangleGrid::get_neighbors(XYPos pos)
{
    XYSet rep;
    XYPos s = get_square_size(pos);
    FOR_XY(p, pos, pos + s)
        rep = rep | base_get_neighbors(p);
    FOR_XY(p, pos, pos + s)
        rep.clear(p);
    return rep;
}

void TriangleGrid::get_row_types(std::vector<XYPos>& rep)
{
    rep.push_back(XYPos(0, size.y));
    rep.push_back(XYPos(0, ((size.y - 1) / 2) + (size.x + 1) / 2 + 1));
    rep.push_back(XYPos(0, (size.x + 1) / 2));
}

void TriangleGrid::get_edges(std::vector<EdgePos>& rep, XYPos grid_pitch)
{
    for (auto const& [pos, type] : edges)
    {
        if (pos.x == 0)
            rep.push_back(EdgePos (type, XYPosFloat(1, 0).angle(), (pos.y + 0.5) * grid_pitch.y));
        else if (pos.x == 1)
        {
            rep.push_back(EdgePos (type, XYPosFloat(1, std::sqrt(3)).angle(), (((size.y - 1) / 2) - pos.y)  * grid_pitch.y));
        }
        else if (pos.x == 2)
        {
            rep.push_back(EdgePos (type, XYPosFloat(-1, std::sqrt(3)).angle(), -(pos.y + 1)  * grid_pitch.y));
        }
        else
            assert(0);
    }
}

XYPos TriangleGrid::get_square_from_mouse_pos(XYPos pos, XYPos grid_pitch)
{
    XYPos rep(pos / grid_pitch);
    XYPos rem(pos % grid_pitch);
    if (!((rep.x ^ rep.y) & 1))
        rem.y = grid_pitch.y - rem.y - 1;
    rem.x = rem.x * std::sqrt(3);
    if (rem.y > rem.x)
        rep.x--;
    if (wrapped == WRAPPED_SIDE)
        rep = rep % size;
    if (rep.inside(size))
        return rep;
    return XYPos(-1,-1);
}

XYPos TriangleGrid::get_grid_pitch(XYPos grid_size)
{
    XYPosFloat gsize((size.x + 1.0) / 2, size.y * std::sqrt(3) / 2);
    int s = std::min(grid_size.x / gsize.x, grid_size.y / gsize.y);
    return XYPos(s / 2, std::sqrt(3) * s / 2);
}

XYRect TriangleGrid::get_square_pos(XYPos pos, XYPos grid_pitch)
{
    XYPos sq_size = get_square_size(pos);
    if (sq_size == XYPos(3,2))
    {
        int s = grid_pitch.x * 3;
        XYPos siz = XYPos(s,s);
        XYPos off = ((grid_pitch * XYPos(4,2)) - siz) / 2;
        return XYRect(pos * grid_pitch + off, siz);
    }
    bool downwards = (pos.x ^ pos.y) & 1;
    XYPos border(grid_pitch.x / 2, grid_pitch.y / 6);
    return XYRect(pos * grid_pitch + XYPos(border.x, downwards ? 0 : border.y * 2), XYPos((grid_pitch.x - border.x) * 2, grid_pitch.y - border.y * 2));
}

XYRect TriangleGrid::get_bubble_pos(XYPos pos, XYPos grid_pitch, unsigned index, unsigned total)
{
    XYPos sq_size = get_square_size(pos);
    if (sq_size == XYPos(3,2))
    {
        unsigned s = 3;
        while (total > (1 + 3 * (s * (s - 1))))
            s++;

        double bsize = double(grid_pitch.x * 2) / (s - 1 + 1 / std::sqrt(3));

        XYPos gpos = XYPos(0,0);
        while (true)
        {
            int w = s + gpos.y;
            int ofst = 0;
            if (gpos.y >= s)
                ofst = (gpos.y - s + 1);

            if (index < (w - ofst * 2))
            {
                gpos.x = index + ofst;
                break;
            }
            index -= w - ofst * 2;
            gpos.y++;
        }

        XYPos p(grid_pitch.x  + gpos.x * bsize - gpos.y * bsize / 2 - bsize / 2 + bsize / (std::sqrt(3) * 2), gpos.y * std::sqrt(3) * bsize / 2);
        return XYRect(pos * grid_pitch + p, XYPos(bsize, bsize));
    }



    bool downwards = (pos.x ^ pos.y) & 1;
    unsigned s = 3;
    while (total > ((s * (s + 1)) / 2))
        s++;
    double bsize = double(grid_pitch.x * 2) / (std::sqrt(3) + s - 1);

    XYPos gpos = XYPos(0,0);
    while (index >= (s - gpos.y))
    {
        index -= s - gpos.y;
        gpos.y++;
    }
    gpos.x = index;

    XYPos p(bsize * std::sqrt(3) / 2 - (bsize / 2) + gpos.x * bsize + gpos.y * bsize / 2, gpos.y * std::sqrt(3) * bsize / 2);

    if (downwards)
        return XYRect(pos * grid_pitch + p, XYPos(bsize, bsize));
    else
        return XYRect(pos * grid_pitch + XYPos(p.x, grid_pitch.y - bsize - p.y), XYPos(bsize, bsize));
}

void TriangleGrid::render_square(XYPos pos, XYPos grid_pitch, std::vector<RenderCmd>& cmds)
{
    XYPos sq_size = get_square_size(pos);
    XYPos line_seg(grid_pitch.x * 2, grid_pitch.x / 16 + 1);
    if (sq_size == XYPos(3,2))
    {

        
        {
            XYRect src(64, 1984 , 384, 384);
            XYRect dst(pos * grid_pitch, XYPos(grid_pitch.x * 4, grid_pitch.y * 2));
            cmds.push_back(RenderCmd(src,dst, true));
        }
        XYRect src(5, 1024, 1, 1);
        XYRect dst;
        dst = XYRect ((pos + XYPos(1, 0)) * grid_pitch, line_seg);
        cmds.push_back(RenderCmd(src, dst, 0, XYPos(0,1)));
        dst = XYRect ((pos + XYPos(3, 0)) * grid_pitch, line_seg);
        cmds.push_back(RenderCmd(src, dst, 60, XYPos(0,1)));
        dst = XYRect ((pos + XYPos(4, 1)) * grid_pitch, line_seg);
        cmds.push_back(RenderCmd(src, dst, 120, XYPos(0,1)));
        dst = XYRect ((pos + XYPos(3, 2)) * grid_pitch, line_seg);
        cmds.push_back(RenderCmd(src, dst, 180, XYPos(0,1)));
        dst = XYRect ((pos + XYPos(1, 2)) * grid_pitch, line_seg);
        cmds.push_back(RenderCmd(src, dst, 240, XYPos(0,1)));
        dst = XYRect ((pos + XYPos(0, 1)) * grid_pitch, line_seg);
        cmds.push_back(RenderCmd(src, dst, 300, XYPos(0,1)));
        return;

    }
    bool downwards = (pos.x ^ pos.y) & 1;
    {
        XYRect src(256, downwards ? 1344 : 1152 , 192, 192);
        XYRect dst(pos * grid_pitch, XYPos(grid_pitch.x * 2, grid_pitch.y));
        cmds.push_back(RenderCmd(src,dst, true));
    }

    if (downwards)
    {
        XYRect src(1, 1024, 192, 6);
        XYRect dst;
        dst = XYRect ((pos + XYPos(0, 0)) * grid_pitch, line_seg);
        cmds.push_back(RenderCmd(src,dst));
        dst = XYRect ((pos + XYPos(2, 0)) * grid_pitch, line_seg);
        cmds.push_back(RenderCmd(src, dst, 120, XYPos(0,1)));
        dst = XYRect ((pos + XYPos(1, 1)) * grid_pitch, line_seg);
        cmds.push_back(RenderCmd(src, dst, 240, XYPos(0,1)));
    }
    else
    {
        XYRect src(1, 1024, 192, 6);
        XYRect dst;
        dst = XYRect ((pos + XYPos(0, 1)) * grid_pitch, line_seg);
        cmds.push_back(RenderCmd(src, dst, -60, XYPos(0,1)));
        dst = XYRect ((pos + XYPos(1, 0)) * grid_pitch, line_seg);
        cmds.push_back(RenderCmd(src, dst, 60, XYPos(0,1)));
        dst = XYRect ((pos + XYPos(2, 1)) * grid_pitch, line_seg);
        cmds.push_back(RenderCmd(src, dst, 180, XYPos(0,1)));
    }


}

void TriangleGrid::add_random_merged(int merged_count)
{
    for (int i = 0; i < merged_count;)
    {
        XYPos m_pos(unsigned(rnd) % size.x, unsigned(rnd) % size.y);
        XYPos m_size(3, 2);

        if ((m_pos.x ^ m_pos.y) & 1)
            continue;
        if (!(m_size + m_pos - XYPos(1,1)).inside(size))
            continue;
        bool bad = false;
        for ( const auto &m_reg : merged )
        {
            if ( (std::min(m_reg.first.x + m_reg.second.x, m_pos.x + m_size.x) > std::max(m_reg.first.x, m_pos.x)) &&
                 (std::min(m_reg.first.y + m_reg.second.y, m_pos.y + m_size.y) > std::max(m_reg.first.y, m_pos.y)) )
                bad = true;
        }
        if (bad)
            continue;
        merged[m_pos] = m_size;
        i++;
    }
}
XYPos TriangleGrid::get_square_size(XYPos p)
{
    for (const auto &m_reg : merged)
    {
        if (p == m_reg.first)
            return m_reg.second;
    }
    return XYPos(1,1);

}
XYPos TriangleGrid::get_base_square(XYPos p)
{
    for (const auto &m_reg : merged)
    {
        if ((p - m_reg.first).inside(m_reg.second))
            return m_reg.first;
    }
    return p;
}

XYPos TriangleGrid::get_wrapped_size(XYPos grid_pitch)
{
    return size * grid_pitch;
}

std::string HexagonGrid::to_string()
{
    return "C" + Grid::to_string();
}

XYSet HexagonGrid::get_squares()
{
    XYSet rep;
    FOR_XY(pos, XYPos(), size)
        rep.set(pos);
    return rep;
}

XYSet HexagonGrid::get_row(unsigned type, int index)
{
    XYSet rep;
    if (type == 0)
    {
        for (int y = 0; y < size.y; y++)
        {
            int x = (index + y - size.y) * 2;
            if (x >= 0 && x < size.x)
                rep.set(get_base_square(XYPos(x, y)));
            x++;
            if (x >= 0 && x < size.x)
                rep.set(get_base_square(XYPos(x, y)));
        }
    }
    else if (type == 1)
    {
        for (int y = 0; y < size.y; y++)
        {
            int x = index;
            if (x >= 0 && x < size.x)
                rep.set(get_base_square(XYPos(x, y)));
        }
    }
    else if (type == 2)
    {
        for (int y = 0; y < size.y; y++)
        {
            int x = (index - y) * 2;
            if (x >= 0 && x < size.x)
                rep.set(get_base_square(XYPos(x, y)));
            x--;
            if (x >= 0 && x < size.x)
                rep.set(get_base_square(XYPos(x, y)));
        }
    }
    else
    {
        assert(0);
    }
    return rep;
}

XYSet HexagonGrid::get_neighbors(XYPos pos)
{
    bool downstep = pos.x & 1;
    XYSet rep;
    FOR_XY(offset, XYPos(-1,-1), XYPos(2,2))
    {
        if (downstep && offset.y == -1 && offset.x) continue;
        if (!downstep && offset.y == 1 && offset.x) continue;
        XYPos t = pos + offset;
        if (wrapped == WRAPPED_SIDE)
            t = t % size;
        if (t.inside(size))
            rep.set(t);
    }
    return rep;
}

void HexagonGrid::get_row_types(std::vector<XYPos>& rep)
{
    rep.push_back(XYPos(0, (size.x - 1) / 2 + size.y));
    rep.push_back(XYPos(0, size.x));
    rep.push_back(XYPos(0, size.x / 2 + size.y));
}

void HexagonGrid::get_edges(std::vector<EdgePos>& rep, XYPos grid_pitch)
{
    for (auto const& [pos, type] : edges)
    {
        if (pos.x == 0)
        {
            rep.push_back(EdgePos (type, XYPosFloat(1.5, std::sqrt(3)/2).angle(), -((pos.y - size.y) * 3 - 0.5) * grid_pitch.x));
        }
        else if (pos.x == 1)
        {
            rep.push_back(EdgePos (type, XYPosFloat(0, 1).angle(), -(2 + pos.y * 3) * grid_pitch.x));
        }
        else if (pos.x == 2)
        {
            rep.push_back(EdgePos (type, XYPosFloat(-1.5, std::sqrt(3)/2).angle(), -(std::sqrt(7) + pos.y * 3) * grid_pitch.x));
        }
        else
            assert(0);
    }
}

XYPos HexagonGrid::get_square_from_mouse_pos(XYPos pos, XYPos grid_pitch)
{
    pos += XYPos(grid_pitch.x, 0);
    XYPos rep = pos / (grid_pitch * XYPos(6, 2));
    rep.x *= 2;
    XYPos rem = pos % (grid_pitch * XYPos(6, 2)) - (grid_pitch * XYPos(3, 1));
    XYPos p = XYPos(abs(rem.x), abs(rem.y));
    p.x = 2 * grid_pitch.x - p.x;
    if ((p.y * grid_pitch.x) > (p.x * grid_pitch.y))
    {
        if (rem.x > 0)
            rep.x++;
        else
            rep.x--;
        if (rem.y < 0)
            rep.y--;
    }
    if (wrapped)
        rep = rep % size;
    if (rep.inside(size))
        return rep;
    return XYPos(-1,-1);
}

XYPos HexagonGrid::get_grid_pitch(XYPos grid_size)
{
    XYPosFloat gsize(size.x * 3 + 1, (size.y * 2 + 1) * std::sqrt(3));
    int s = std::min(grid_size.x / gsize.x, grid_size.y / gsize.y);
    return XYPos(s, s * std::sqrt(3));
}

XYRect HexagonGrid::get_square_pos(XYPos pos, XYPos grid_pitch)
{
    int downstep = pos.x & 1;
    int s = grid_pitch.x * 3;
    XYPos siz = XYPos(s,s);
    XYPos off = ((grid_pitch * XYPos(4,2)) - siz) / 2;
    XYRect dst((pos * XYPos(3, 2) + XYPos(0, downstep)) * grid_pitch + off, siz);
    return dst;
}

XYRect HexagonGrid::get_bubble_pos(XYPos pos, XYPos grid_pitch, unsigned index, unsigned total)
{
    int downstep = pos.x & 1;
    unsigned s = 2;
    while (total > (1 + 3 * (s * (s - 1))))
        s++;

    double bsize = double(grid_pitch.x * 2) / (s - 1 + 1 / std::sqrt(3));

    XYPos gpos = XYPos(0,0);
    while (true)
    {
        int w = s + gpos.y;
        int ofst = 0;
        if (gpos.y >= s)
            ofst = (gpos.y - s + 1);

        if (index < (w - ofst * 2))
        {
            gpos.x = index + ofst;
            break;
        }
        index -= w - ofst * 2;
        gpos.y++;
    }

    XYPos p(grid_pitch.x  + gpos.x * bsize - gpos.y * bsize / 2 - bsize / 2 + bsize / (std::sqrt(3) * 2), gpos.y * std::sqrt(3) * bsize / 2);
    XYPos ppos = (pos * XYPos(3, 2) + XYPos(0, downstep)) * grid_pitch;

    return XYRect(ppos + p, XYPos(bsize, bsize));
}

void HexagonGrid::render_square(XYPos pos, XYPos grid_pitch, std::vector<RenderCmd>& cmds)
{
    int downstep = pos.x & 1;
    {
        XYRect src(64, 1984 , 384, 384);
        XYRect dst((pos * XYPos(3, 2) + XYPos(0, downstep)) * grid_pitch, XYPos(grid_pitch.x * 4, grid_pitch.y * 2));
        cmds.push_back(RenderCmd(src,dst, true));
    }

    {
        XYRect src(5, 1024, 1, 1);
        XYRect dst;
        dst = XYRect ((pos * XYPos(3, 2) + XYPos(0, downstep) + XYPos(1, 0)) * grid_pitch, XYPos(grid_pitch.x * 2, grid_pitch.x / 6 + 1));
        cmds.push_back(RenderCmd(src, dst, 0, XYPos(0,1)));
        dst = XYRect ((pos * XYPos(3, 2) + XYPos(0, downstep) + XYPos(3, 0)) * grid_pitch, XYPos(grid_pitch.x * 2, grid_pitch.x / 6 + 1));
        cmds.push_back(RenderCmd(src, dst, 60, XYPos(0,1)));
        dst = XYRect ((pos * XYPos(3, 2) + XYPos(0, downstep) + XYPos(4, 1)) * grid_pitch, XYPos(grid_pitch.x * 2, grid_pitch.x / 6 + 1));
        cmds.push_back(RenderCmd(src, dst, 120, XYPos(0,1)));
        dst = XYRect ((pos * XYPos(3, 2) + XYPos(0, downstep) + XYPos(3, 2)) * grid_pitch, XYPos(grid_pitch.x * 2, grid_pitch.x / 6 + 1));
        cmds.push_back(RenderCmd(src, dst, 180, XYPos(0,1)));
        dst = XYRect ((pos * XYPos(3, 2) + XYPos(0, downstep) + XYPos(1, 2)) * grid_pitch, XYPos(grid_pitch.x * 2, grid_pitch.x / 6 + 1));
        cmds.push_back(RenderCmd(src, dst, 240, XYPos(0,1)));
        dst = XYRect ((pos * XYPos(3, 2) + XYPos(0, downstep) + XYPos(0, 1)) * grid_pitch, XYPos(grid_pitch.x * 2, grid_pitch.x / 6 + 1));
        cmds.push_back(RenderCmd(src, dst, 300, XYPos(0,1)));
    }
}
XYPos HexagonGrid::get_wrapped_size(XYPos grid_pitch)
{
    return XYPos(size.x * 3, size.y * 2) * grid_pitch;
}

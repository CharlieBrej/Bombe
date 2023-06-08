#include "z3++.h"


#include "Grid.h"
#include <sstream>

bool IS_DEMO = false;
bool IS_PLAYTEST = false;
bool SHUTDOWN = false;
static std::random_device rd;
static Rand rnd(rd());
//static Rand rnd(1);

static std::map<int,int> colours_used;

void grid_set_rnd(int a)
{
    rnd.gen.seed(a);
}

std::string RegionType::val_as_str(int offset)
{
    std::string s;
    int dig = 0;
    for (int i = 0; i <5; i++)
    {
        if ((var >> i) & 1)
        {
            if (!dig)
            {
                s += char('a' + i);
            }
            else
            {
                if (dig == 1)
                    s += '^';
                s += '+';
                s += char('a' + i);
            }
            dig++;
        }
    }
    if (dig)
    {
        if (value + offset)
        {
            if (dig == 1)
                s += '^';
            s += '+';
            s += std::to_string(value + offset);
            s += '^';
        }
        else if (dig > 1)
            s += '^';
    }
    else
    {
        s = std::to_string(value + offset);
    }


    return s;
}

template<class RESP, class IN, class OTHER>
RESP RegionType::apply_rule_imp(IN in, OTHER other)
{
    if (type == NONE)
    {
        return (in  != (other - 1000));
    }
    if (type == EQUAL)
    {
        return in == other;
    }
    if (type == NOTEQUAL)
    {
        return in != other;
    }
    if (type == LESS)
    {
        return in <= other;
    }
    if (type == MORE)
    {
        return in >= other;
    }
    if (type == XOR2)
    {
        return (in == other) || (in == (other + 2));
    }
    if (type == XOR3)
    {
        return (in == other) || (in == (other + 3));
    }
    if (type == XOR22)
    {
        return (in == other) || (in == (other + 2)) || (in == (other + 4));
    }
    if (type == XOR222)
    {
        return (in == other) || (in == (other + 2)) || (in == (other + 4)) || (in == (other + 6));
    }
    if (type == PARITY)
    {
        return (in >= other) && (((in - other) % 2) == 0);
    }
    if (type == XOR1)
    {
        return (in == other) || (in == (other + 1));
    }
    if (type == XOR11)
    {
        return (in == other) || (in == (other + 1))  || (in == (other + 2));
    }
    assert(0);
}

template<class RESP, class IN, class VAR_ARR>
RESP RegionType::apply_rule(IN in, VAR_ARR& vars)
{
    if (var)
        return apply_rule_imp<RESP,IN, IN>(in, vars[var - 1] + value);
    return apply_rule_imp<RESP,IN, int8_t>(in, value);
}

z3::expr RegionType::apply_z3_rule(z3::expr in, z3::expr_vector& var_vect)
{
    return apply_rule<z3::expr,z3::expr, z3::expr_vector>(in, var_vect);
}

bool RegionType::apply_int_rule(unsigned in, int vars[32])
{
    int v = 0;
    if (var)
    {
        for (unsigned i = 0; i < 5; i++)
        {
            if ((var >> i) & 1)
            {
                if (vars[(1 << i) - 1] == -1)
                    return false;
                v += vars[(1 << i) - 1];
            }
        }
        assert(vars[var - 1]  == v);
    }
    return apply_rule_imp<bool,unsigned>(in, v + value);
}

int RegionType::max()
{
    if (type == NONE)
        return -1;
    if (type == EQUAL)
        return 0;
    if (type == NOTEQUAL)
        return -1;
    if (type == LESS)
        return 0;
    if (type == MORE)
        return -1;
    if (type == XOR2)
        return 2;
    if (type == XOR3)
        return 3;
    if (type == XOR22)
        return 4;
    if (type == XOR222)
        return 6;
    if (type == PARITY)
        return -1;
    if (type == XOR1)
        return 1;
    if (type == XOR11)
        return 2;
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
        if (gen_cause.regions[i])
        {
            if (gen_cause.regions[i] == other)
                return true;
            if (gen_cause.regions[i]->has_ancestor(other))
                return true;
        }
    }
    return false;
}



GridRule::GridRule(SaveObject* sobj)
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

    if (omap->has_key("priority"))
        priority = omap->get_num("priority");

    apply_region_bitmap = omap->get_num("apply_region_bitmap");

    SaveObjectList* rlist = omap->get_item("region_type")->get_list();
    for (int i = 0; i < rlist->get_count(); i++)
        region_type[i] = RegionType('a',rlist->get_num(i));

    rlist = omap->get_item("square_counts")->get_list();
    for (int i = 0; i < rlist->get_count(); i++)
    {
        int v = rlist->get_num(i);
        square_counts[i] = RegionType('a',v);
    }
    if (omap->has_key("used_count"))
        used_count = omap->get_num("used_count");
    if (omap->has_key("clear_count"))
        clear_count = omap->get_num("clear_count");
//    assert(is_legal() == GridRule::OK);
}

SaveObject* GridRule::save(bool lite)
{
    SaveObjectMap* omap = new SaveObjectMap;
    omap->add_num("region_count", region_count);
    omap->add_num("apply_region_type", apply_region_type.as_int());
    omap->add_num("apply_region_bitmap", apply_region_bitmap);
    omap->add_num("priority", priority);

    SaveObjectList* region_type_list = new SaveObjectList;
    for (int i = 0; i < region_count; i++)
        region_type_list->add_num(region_type[i].as_int());
    omap->add_item("region_type", region_type_list);

    SaveObjectList* square_counts_list = new SaveObjectList;
    for (int i = 0; i < (1<<region_count); i++)
        square_counts_list->add_num(square_counts[i].as_int());
    omap->add_item("square_counts", square_counts_list);
    if (!lite)
    {
        omap->add_num("used_count", used_count);
        omap->add_num("clear_count", clear_count);
    }
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

bool GridRule::matches(GridRegion* r1, GridRegion* r2, GridRegion* r3, GridRegion* r4, int var_counts[32])
{
    GridRegion* grid_regions[4] = {r1, r2, r3, r4};

    for (int i = 0; i < region_count; i++)
    {
        if (region_type[i].type != RegionType::NONE)
            assert(region_type[i].type == grid_regions[i]->type.type);
        if (region_type[i].var)
        {
            int vi = region_type[i].var - 1;
            if (var_counts[vi] < 0)
            {
                int v = grid_regions[i]->type.value - region_type[i].value;
                if (v < 0)
                    return false;
                var_counts[vi] = v;
            }
            else
            {
                if (var_counts[vi] != (grid_regions[i]->type.value - region_type[i].value))
                    return false;
            }
        }
    }

    for (int i = 1; i < (1 << region_count); i++)
    {
        if ((square_counts[i].type == RegionType::EQUAL) && square_counts[i].var)
        {
            XYSet s = (i & 1) ? r1->elements : ~r1->elements;
            if (r2)
                s &= ((i & 2) ? r2->elements : ~r2->elements);
            if (r3)
                s &= ((i & 4) ? r3->elements : ~r3->elements);
            if (r4)
                s &= ((i & 8) ? r4->elements : ~r4->elements);

            int vi = square_counts[i].var - 1;
            int v = s.count() - square_counts[i].value;
            if (v < 0)
                return false;
            if (var_counts[vi] < 0)
                var_counts[vi] = v;
        }
    }
    for (int i = 1; i < 32; i++)
    {
        if (var_counts[i-1] >= 0)
        {
            for (int j = 1; j < 32; j++)
            {
                if ((var_counts[j-1] >= 0) && (i != j))
                {
                    if ((i & j) == 0)
                    {
                        if (var_counts[(i | j) - 1] >= 0)
                        {
                            if (var_counts[(i | j) - 1] != var_counts[i - 1] + var_counts[j - 1])
                                return false;
                        }
                        else
                            var_counts[(i | j) - 1] = var_counts[i - 1] + var_counts[j - 1];
                    }
                    else if ((i & j) == i)
                    {
                        if (var_counts[(j & ~i) - 1] >= 0)
                        {
                            if (var_counts[(j & ~i) - 1] != var_counts[j - 1] - var_counts[i - 1])
                                return false;
                        }
                        else
                        {
                            int v = var_counts[j - 1] - var_counts[i - 1];
                            if (v < 0)
                                return false;
                            var_counts[(j & ~i) - 1] = v;
                            if (i > ((j & ~i) - 1))
                            {
                                i = (j & ~i) - 1;
                                break;
                            }
                        }
                    }
                    else
                    {
                        int x = i ^ j;
                        if (var_counts[x - 1] >= 0)
                        {
                            int v = var_counts[i - 1] + var_counts[j - 1] + var_counts[x - 1];
                            if (v % 2)
                                return false;
                            v /= 2;
                            if (var_counts[(j | i) - 1] >= 0)
                            {
                                if (var_counts[(j | i) - 1] != v)
                                    return false;
                            }
                            else
                                var_counts[(j | i) - 1] = v;
                        }

                    }
                }
            }
        }
    }

    // if (var_counts[2] < 0 && var_counts[0] >= 0 && var_counts[1] >= 0)
    // {
    //     var_counts[2] = var_counts[0] + var_counts[1];
    // }
    // if (var_counts[0] < 0 && var_counts[1] >= 0 && var_counts[2] >= 0)
    // {
    //     var_counts[0] = var_counts[2] - var_counts[1];
    //     if (var_counts[0] < 0)
    //         return false;
    // }
    // if (var_counts[1] < 0 && var_counts[0] >= 0 && var_counts[2] >= 0)
    // {
    //     var_counts[1] = var_counts[2] - var_counts[0];
    //     if (var_counts[1] < 0)
    //         return false;
    // }

    // if (var_counts[0] >= 0 && var_counts[1] >= 0 && var_counts[2] >= 0)
    // {
    //     if (var_counts[2] != var_counts[0] + var_counts[1])
    //         return false;
    // }

    for (int i = 1; i < (1 << region_count); i++)
    {
        if (square_counts[i].type == RegionType::NONE)
            continue;
        XYSet s = (i & 1) ? r1->elements : ~r1->elements;
        if (r2)
            s &= ((i & 2) ? r2->elements : ~r2->elements);
        if (r3)
            s &= ((i & 4) ? r3->elements : ~r3->elements);
        if (r4)
            s &= ((i & 8) ? r4->elements : ~r4->elements);
        if (!square_counts[i].apply_int_rule(s.count(), var_counts))
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

GridRule::IsLogicalRep GridRule::is_legal(GridRule& why)
{
    z3::context c;
    z3::solver s(c);

    z3::expr_vector vec(c);
    z3::expr_vector var_vec(c);
    why = *this;

    for (int v = 1; v < 32; v++)
    {
        std::stringstream x_name;
        x_name << "V" << v;
        var_vec.push_back(c.int_const(x_name.str().c_str()));
        if ((v & (v - 1)) == 0)
        {
             s.add(var_vec[v - 1] >= 0);
        }
        else
        {
            z3::expr e = c.int_val(0);
            for (int i = 0; i < 5; i++)
            {
                if ((v >> i) & 1)
                    e = e + var_vec[(1 << i) - 1];
            }
            s.add(var_vec[v - 1] == e);
        }
    }

    vec.push_back(c.bool_const("DUMMY"));
    if (region_count == 0)
        return IMPOSSIBLE;

    for (int i = 1; i < (1 << region_count); i++)
    {
        std::stringstream x_name;
        x_name << "A" << i;
        vec.push_back(c.int_const(x_name.str().c_str()));
        s.add(vec[i] >= 0);
        int m = square_counts[i].max();
        if (m >= 0)
        {
            if (square_counts[i].var)
                s.add(vec[i] <= var_vec[square_counts[i].var - 1] + square_counts[i].value + m);
            else
                s.add(vec[i] <= int(square_counts[i].value + m));

        }
    }

    if (region_count == 1)
    {
        s.add(region_type[0].apply_z3_rule(vec[1], var_vec));
    }
    if (region_count == 2)
    {
        s.add(region_type[0].apply_z3_rule(vec[1] + vec[3], var_vec));
        s.add(region_type[1].apply_z3_rule(vec[2] + vec[3], var_vec));
    }
    if (region_count == 3)
    {
        s.add(region_type[0].apply_z3_rule(vec[1] + vec[3] + vec[5] + vec[7], var_vec));
        s.add(region_type[1].apply_z3_rule(vec[2] + vec[3] + vec[6] + vec[7], var_vec));
        s.add(region_type[2].apply_z3_rule(vec[4] + vec[5] + vec[6] + vec[7], var_vec));
    }
    if (region_count == 4)
    {
        s.add(region_type[0].apply_z3_rule(vec[1] + vec[3] + vec[5] + vec[7] + vec[9] + vec[11] + vec[13] + vec[15], var_vec));
        s.add(region_type[1].apply_z3_rule(vec[2] + vec[3] + vec[6] + vec[7] + vec[10] + vec[11] + vec[14] + vec[15], var_vec));
        s.add(region_type[2].apply_z3_rule(vec[4] + vec[5] + vec[6] + vec[7] + vec[12] + vec[13] + vec[14] + vec[15], var_vec));
        s.add(region_type[3].apply_z3_rule(vec[8] + vec[9] + vec[10] + vec[11] + vec[12] + vec[13] + vec[14] + vec[15], var_vec));
    }
    if (s.check() != z3::sat)
    {
        return IMPOSSIBLE;
    }
    
    if (apply_region_type.type == RegionType::VISIBILITY)
        return OK;

    z3::expr e = c.int_val(0);
    z3::expr tot = c.int_val(0);

    if (!apply_region_bitmap)
        return OK;

    for (int i = 1; i < (1 << region_count); i++)
    {
        if ((apply_region_bitmap >> i) & 1)
        {
            e = e + vec[i];
            int m = square_counts[i].max();
            if ((m < 0) && (apply_region_type == RegionType(RegionType::SET, 1)))
            {
                for (int i = 1; i < (1 << region_count); i++)
                    why.square_counts[i] = RegionType(RegionType::NONE, 0);

                why.square_counts[i] = RegionType(RegionType::MORE, 0);
                return ILLOGICAL;
            }
            if (square_counts[i].var)
                tot = tot + var_vec[square_counts[i].var-1] + square_counts[i].value + m;
            else
                tot = tot + int(square_counts[i].value + m);
        }
    }

    if (apply_region_type.type == RegionType::SET)
    {
        if (apply_region_type.value)
            s.add(e != tot);
        else
            s.add(e != 0);
    }
    else
    {
        s.add(!apply_region_type.apply_z3_rule(e, var_vec));
    }

    if (s.check() == z3::sat)
    {
        z3::model m = s.get_model();
        for (int i = 1; i < (1 << region_count); i++)
        {
            int v = m.eval(vec[i]).get_numeral_int();
            why.square_counts[i] = RegionType(RegionType::EQUAL ,v);
        }
        int vals[32];
        for (int i = 0; i < 31; i++)
        {
            vals[i] = m.eval(var_vec[i]).get_numeral_int();
        }
        for (int i = 0; i < region_count; i++)
        {
            if (why.region_type[i].var)
            {
                why.region_type[i].value += vals[why.region_type[i].var - 1];
                why.region_type[i].var = 0;
            }
        }
        if (why.apply_region_type.var)
        {
            why.apply_region_type.value += vals[why.apply_region_type.var - 1];
            why.apply_region_type.var = 0;
        }
        return ILLOGICAL;
    }
    else
    {
        // if (!apply_region_type.var)
        //     return OK;
        uint64_t seen = 0;
        uint64_t want = 0;
        for (int i = 0; i < region_count; i++)
            seen |= 1 << region_type[i].var;
        for (int i = 1; i < (1 << region_count); i++)
            if (square_counts[i].type == RegionType::EQUAL)
                seen |= 1 << square_counts[i].var;
            else
                want |= 1 << square_counts[i].var;
        want |= 1 << apply_region_type.var;
        want &= ~1;
        seen &= ~1;
        if (want)
        {
            for (int i = 1; i < 32; i++)
            {
                if ((seen >> i) & 1)
                {
                    for (int j = 1; j < 32; j++)
                    {
                        if (((seen >> j) & 1) && (i != j))
                        {
                            if ((i & j) == 0)
                                seen |= 1 << (i | j);
                            if ((i & j) == i)
                            {
                                if (((seen >> (j & ~i)) & 1) == 0)
                                {
                                    seen |= 1 << (j & ~i);
                                    if (i > (j & ~i) - 1)
                                        i = (j & ~i) - 1;
                                    break;
                                }
                            }
                            else
                            {
                                int x = i ^ j;
                                if ((seen >> x) & 1)
                                    seen |= 1 << (j | i);
                            }
                        }
                    }

                }
            }
        }
        if (want & ~seen)
            return UNBOUNDED;
        return OK;
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
    sort_perm = 0;
    for(int i = 0; i < region_count; i++)
        sort_perm |= idx[i] << (i * 2);
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
            FOR_XY_SET(t, neigh)
                cnt += get(t).bomb;

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
            if (rnd % 10 < row_percent)
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
        std::string sin = "[{\"apply_region_bitmap\":2,\"apply_region_type\":66304,\"clear_count\":492,\"priority\":0,\"region_count\":1,\"region_type\":[65792],\"square_counts\":[0,0],\"used_count\":1089},{\"apply_region_bitmap\":2,\"apply_region_type\":66048,\"clear_count\":1156,\"priority\":0,\"region_count\":1,\"region_type\":[65792],\"square_counts\":[0,0],\"used_count\":961},{\"apply_region_bitmap\":2,\"apply_region_type\":25857,\"clear_count\":0,\"priority\":0,\"region_count\":2,\"region_type\":[65792,66304],\"square_counts\":[0,256,256,0],\"used_count\":0},{\"apply_region_bitmap\":2,\"apply_region_type\":25857,\"clear_count\":0,\"priority\":0,\"region_count\":2,\"region_type\":[65792,66048],\"square_counts\":[0,256,256,0],\"used_count\":0},{\"apply_region_bitmap\":2,\"apply_region_type\":25601,\"clear_count\":902,\"priority\":0,\"region_count\":1,\"region_type\":[66304],\"square_counts\":[0,65792],\"used_count\":483},{\"apply_region_bitmap\":2,\"apply_region_type\":25600,\"clear_count\":1602,\"priority\":0,\"region_count\":1,\"region_type\":[512],\"square_counts\":[0,0],\"used_count\":842},{\"apply_region_bitmap\":4,\"apply_region_type\":131841,\"clear_count\":39,\"priority\":0,\"region_count\":2,\"region_type\":[66048,197377],\"square_counts\":[0,0,0,66305],\"used_count\":49},{\"apply_region_bitmap\":4,\"apply_region_type\":262656,\"clear_count\":33,\"priority\":0,\"region_count\":2,\"region_type\":[197376,328192],\"square_counts\":[0,131328,0,0],\"used_count\":359},{\"apply_region_bitmap\":1,\"apply_region_type\":25858,\"clear_count\":0,\"priority\":0,\"region_count\":2,\"region_type\":[66048,66048],\"square_counts\":[0,256,0,0],\"used_count\":0},{\"apply_region_bitmap\":8,\"apply_region_type\":25600,\"clear_count\":5,\"priority\":0,\"region_count\":3,\"region_type\":[66049,131584,459520],\"square_counts\":[0,0,0,0,262656,0,0,0],\"used_count\":3},{\"apply_region_bitmap\":2,\"apply_region_type\":65792,\"clear_count\":200,\"priority\":0,\"region_count\":1,\"region_type\":[66816],\"square_counts\":[0,66050],\"used_count\":167},{\"apply_region_bitmap\":2,\"apply_region_type\":65792,\"clear_count\":241,\"priority\":0,\"region_count\":1,\"region_type\":[66560],\"square_counts\":[0,66049],\"used_count\":305},{\"apply_region_bitmap\":4,\"apply_region_type\":132096,\"clear_count\":1,\"priority\":0,\"region_count\":2,\"region_type\":[65792,197632],\"square_counts\":[0,256,0,0],\"used_count\":6},{\"apply_region_bitmap\":2,\"apply_region_type\":25858,\"clear_count\":0,\"priority\":0,\"region_count\":2,\"region_type\":[65792,66560],\"square_counts\":[0,256,256,0],\"used_count\":0},{\"apply_region_bitmap\":2,\"apply_region_type\":25858,\"clear_count\":0,\"priority\":0,\"region_count\":2,\"region_type\":[65792,66816],\"square_counts\":[0,256,256,0],\"used_count\":0},{\"apply_region_bitmap\":2,\"apply_region_type\":25858,\"clear_count\":0,\"priority\":0,\"region_count\":2,\"region_type\":[65792,68096],\"square_counts\":[0,256,256,0],\"used_count\":0},{\"apply_region_bitmap\":2,\"apply_region_type\":25858,\"clear_count\":0,\"priority\":0,\"region_count\":2,\"region_type\":[65792,68352],\"square_counts\":[0,256,256,0],\"used_count\":0},{\"apply_region_bitmap\":12,\"apply_region_type\":65794,\"clear_count\":2,\"priority\":0,\"region_count\":2,\"region_type\":[197377,66560],\"square_counts\":[0,131328,0,0],\"used_count\":1},{\"apply_region_bitmap\":2,\"apply_region_type\":66048,\"clear_count\":54,\"priority\":0,\"region_count\":1,\"region_type\":[67585],\"square_counts\":[0,65793],\"used_count\":78},{\"apply_region_bitmap\":2,\"apply_region_type\":66560,\"clear_count\":205,\"priority\":0,\"region_count\":1,\"region_type\":[67072],\"square_counts\":[0,66051],\"used_count\":425},{\"apply_region_bitmap\":12,\"apply_region_type\":65795,\"clear_count\":0,\"priority\":0,\"region_count\":2,\"region_type\":[197377,66816],\"square_counts\":[0,131584,0,0],\"used_count\":0},{\"apply_region_bitmap\":2,\"apply_region_type\":25601,\"clear_count\":177,\"priority\":0,\"region_count\":1,\"region_type\":[68096],\"square_counts\":[0,65792],\"used_count\":80},{\"apply_region_bitmap\":2,\"apply_region_type\":67072,\"clear_count\":305,\"priority\":0,\"region_count\":1,\"region_type\":[67840],\"square_counts\":[0,66053],\"used_count\":432},{\"apply_region_bitmap\":2,\"apply_region_type\":769,\"clear_count\":44,\"priority\":0,\"region_count\":1,\"region_type\":[2048],\"square_counts\":[0,0],\"used_count\":55},{\"apply_region_bitmap\":4,\"apply_region_type\":132608,\"clear_count\":0,\"priority\":0,\"region_count\":2,\"region_type\":[66560,197634],\"square_counts\":[0,256,0,0],\"used_count\":0},{\"apply_region_bitmap\":4,\"apply_region_type\":1025,\"clear_count\":1,\"priority\":0,\"region_count\":2,\"region_type\":[66560,66561],\"square_counts\":[0,256,0,0],\"used_count\":1},{\"apply_region_bitmap\":2,\"apply_region_type\":66304,\"clear_count\":173,\"priority\":0,\"region_count\":1,\"region_type\":[67072],\"square_counts\":[0,0],\"used_count\":270},{\"apply_region_bitmap\":2,\"apply_region_type\":66304,\"clear_count\":50,\"priority\":0,\"region_count\":1,\"region_type\":[68352],\"square_counts\":[0,0],\"used_count\":62},{\"apply_region_bitmap\":2,\"apply_region_type\":66049,\"clear_count\":18,\"priority\":0,\"region_count\":1,\"region_type\":[68096],\"square_counts\":[0,0],\"used_count\":202},{\"apply_region_bitmap\":4,\"apply_region_type\":68096,\"clear_count\":3,\"priority\":0,\"region_count\":2,\"region_type\":[131328,199168],\"square_counts\":[0,256,0,0],\"used_count\":2},{\"apply_region_bitmap\":12,\"apply_region_type\":196864,\"clear_count\":10,\"priority\":0,\"region_count\":2,\"region_type\":[66048,199168],\"square_counts\":[0,0,131584,0],\"used_count\":7},{\"apply_region_bitmap\":4,\"apply_region_type\":132352,\"clear_count\":4,\"priority\":0,\"region_count\":2,\"region_type\":[65792,197888],\"square_counts\":[0,256,0,0],\"used_count\":2},{\"apply_region_bitmap\":2,\"apply_region_type\":1024,\"clear_count\":2,\"priority\":0,\"region_count\":1,\"region_type\":[2049],\"square_counts\":[0,258],\"used_count\":14},{\"apply_region_bitmap\":1,\"apply_region_type\":25858,\"clear_count\":0,\"priority\":0,\"region_count\":2,\"region_type\":[67072,66560],\"square_counts\":[0,256,256,0],\"used_count\":0},{\"apply_region_bitmap\":2,\"apply_region_type\":25858,\"clear_count\":0,\"priority\":0,\"region_count\":2,\"region_type\":[66560,67840],\"square_counts\":[0,256,256,0],\"used_count\":0}]";
        SaveObject* sobj = SaveObject::load(sin);
        SaveObjectList* rlist = sobj->get_list();
        for (int i = 0; i < rlist->get_count(); i++)
        {
            global_rules.push_back(GridRule(rlist->get_item(i)));
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
                apply_rule(rule, NULL);
            }
        }
        for (GridRule& rule : global_rules)
        {
            if (rule.apply_region_type.type == RegionType::VISIBILITY)
                continue;
            while (apply_rule(rule, NULL) != APPLY_RULE_RESP_NONE)
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
    z3::expr_vector dummy_vec(c);

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
        s.add(r.type.apply_z3_rule(e, dummy_vec));
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

void Grid::make_harder(int plus_minus, int x_y, int x_y3, int x_y_z, int exc, int parity, int xor1, int xor11)
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
            if (SHUTDOWN) return;
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
            if (SHUTDOWN) return;
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
            if (exc)
            {
                if (rnd % 10 < exc)
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
                if (rnd % 10 < exc && (get_clue(p).value >= 3))
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
                if (rnd % 10 < exc && (get_clue(p).value >= 2))
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
                if (rnd % 10 < exc)
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
            }
            if (parity)
            {
                if (rnd % 10 < parity && (get_clue(p).value >= 4))
                {
                    tst = *this;
                    tst->get_clue(p).type = RegionType::PARITY;
                    tst->get_clue(p).value -= 4;
                    if (tst->is_solveable())
                    {
                        get_clue(p) = tst->get_clue(p);
                        continue;
                    }
                }
                if (rnd % 10 < parity && (get_clue(p).value >= 2))
                {
                    tst = *this;
                    tst->get_clue(p).type = RegionType::PARITY;
                    tst->get_clue(p).value -= 2;
                    if (tst->is_solveable())
                    {
                        get_clue(p) = tst->get_clue(p);
                        continue;
                    }
                }
                if (rnd % 10 < parity)
                {
                    tst = *this;
                    tst->get_clue(p).type = RegionType::PARITY;
                    if (tst->is_solveable())
                    {
                        get_clue(p) = tst->get_clue(p);
                        continue;
                    }
                }
            }
            if (xor11)
            {
                if (rnd % 10 < xor11 && (get_clue(p).value >= 2))
                {
                    tst = *this;
                    tst->get_clue(p).type = RegionType::XOR11;
                    tst->get_clue(p).value -= 2;
                    if (tst->is_solveable())
                    {
                        get_clue(p) = tst->get_clue(p);
                        continue;
                    }
                }
                if (rnd % 10 < xor11 && (get_clue(p).value >= 1))
                {
                    tst = *this;
                    tst->get_clue(p).type = RegionType::XOR11;
                    tst->get_clue(p).value -= 1;
                    if (tst->is_solveable())
                    {
                        get_clue(p) = tst->get_clue(p);
                        continue;
                    }
                }
                if (rnd % 10 < xor11)
                {
                    tst = *this;
                    tst->get_clue(p).type = RegionType::XOR11;
                    if (tst->is_solveable())
                    {
                        get_clue(p) = tst->get_clue(p);
                        continue;
                    }
                }
            }
            if (xor1)
            {
                if (rnd % 10 < xor1 && (get_clue(p).value >= 1))
                {
                    tst = *this;
                    tst->get_clue(p).type = RegionType::XOR1;
                    tst->get_clue(p).value -= 1;
                    if (tst->is_solveable())
                    {
                        get_clue(p) = tst->get_clue(p);
                        continue;
                    }
                }
                if (rnd % 10 < xor1)
                {
                    tst = *this;
                    tst->get_clue(p).type = RegionType::XOR1;
                    if (tst->is_solveable())
                    {
                        get_clue(p) = tst->get_clue(p);
                        continue;
                    }
                }
            }
            if (x_y_z)
            {
                if (rnd % 10 < x_y_z && (get_clue(p).value >= 2))
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
                if (rnd % 10 < x_y_z)
                {
                    tst = *this;
                    tst->get_clue(p).type = RegionType::XOR22;
                    if (tst->is_solveable())
                    {
                        get_clue(p) = tst->get_clue(p);
                        continue;
                    }
                }
                if (rnd % 10 < x_y_z && (get_clue(p).value >= 4))
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
            }
            if (x_y3)
            {
                if (rnd % 10 < x_y3)
                {
                    tst = *this;
                    tst->get_clue(p).type = RegionType::XOR3;
                    if (tst->is_solveable())
                    {
                        get_clue(p) = tst->get_clue(p);
                        continue;
                    }
                }

                if ((rnd % 10 < x_y3) && (get_clue(p).value >= 3))
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
            }

            if (x_y)
            {
                if (rnd % 10 < x_y)
                {
                    tst = *this;
                    tst->get_clue(p).type = RegionType::XOR2;
                    if (tst->is_solveable())
                    {
                        get_clue(p) = tst->get_clue(p);
                        continue;
                    }
                }

                if ((rnd % 10 < x_y) && (get_clue(p).value >= 2))
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
                if (rnd % 10 < plus_minus)
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
    assert(!vals[p].revealed);
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

    cnt = 0;
    FOR_XY_SET(p, reg.elements)
    {
        if (vals[p].bomb)
            cnt++;
    }
    assert(!reg.type.var);
    assert((reg.type.apply_rule_imp<bool,unsigned>(cnt, reg.type.value)));


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

bool Grid::add_region(XYSet& elements, RegionType clue, XYPos cause)
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
    if (clue.value < 0 && clue.type == RegionType::PARITY)
    {
        clue.value &= 1;
    }
    if (clue.value < 0 && clue.type == RegionType::XOR11)
    {
        clue.value++;
        clue.type = RegionType::XOR1;
    }
    if (clue.value < 0 && clue.type == RegionType::XOR1)
    {
        assert(clue.value == -1);
        clue.value = 0;
        clue.type = RegionType::EQUAL;
    }
    assert (clue.value >= 0);
    GridRegion reg(clue);
    reg.elements = elements;
    reg.gen_cause_pos = cause;
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
        if (add_region(elements, clue, XYPos(e_pos.x + 1000,e_pos.y)))
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
            if (add_region(elements, clue, p))
                return true;
        }
    }
    return false;
}

static void add_clear_count(GridRegion* region, std::set<GridRule*>& rules_to_credit)
{
    if (!region)
        return;
    if (region->gen_cause.rule)
    {
        rules_to_credit.insert(region->gen_cause.rule);
        for (int i = 0; i < 4; i++)
        {
            if (region->gen_cause.regions[i])
            {
                assert(i < region->gen_cause.rule->region_count);
                add_clear_count(region->gen_cause.regions[i], rules_to_credit);
            }
        }
    }
}

Grid::ApplyRuleResp Grid::apply_rule(GridRule& rule, GridRegion* r[4], int var_counts[32])
{
    if (rule.priority < -100)
        return APPLY_RULE_RESP_NONE;
    if (rule.deleted)
        return APPLY_RULE_RESP_NONE;
    assert(rule.apply_region_bitmap);
    if (rule.apply_region_type.type == RegionType::VISIBILITY)
    {
        GridVisLevel vis_level =  GridVisLevel(rule.apply_region_type.value);
        for (int i = 0; i < 4; i++)
        {
            if (((rule.apply_region_bitmap >> i) & 1) && (r[i]->visibility_force != GridRegion::VIS_FORCE_USER))
            {
                if (r[i]->vis_level < vis_level)
                {
                    r[i]->vis_level = vis_level;
                    r[i]->visibility_force = GridRegion::VIS_FORCE_NONE;
                    r[i]->vis_cause = GridRegionCause(&rule, r[0], r[1], r[2], r[3]);
                }
            }
        }
        return APPLY_RULE_RESP_NONE;
    }

    XYSet itter_elements;
    GridRegion* itter = NULL;
    for (int i = 0; i < 4; i++)
    {
        if (r[i])
            itter_elements = itter_elements | r[i]->elements;
    }

    XYSet to_reveal;

    FOR_XY_SET(pos, itter_elements)
    {
        unsigned m = 0;
        if (r[0]->elements.get(pos))
            m |= 1;
        if (r[1] && (r[1]->elements.get(pos)))
            m |= 2;
        if (r[2] && (r[2]->elements.get(pos)))
            m |= 4;
        if (r[3] && (r[3]->elements.get(pos)))
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
            }
        }
        int c = 0;
        FOR_XY_SET(pos, to_reveal)
        {
            reveal(pos);
            c++;
        }
        last_cleared_regions = to_reveal;
        rule.level_used_count++;
        rule.level_clear_count += c;
        std::set<GridRule*> rules_to_credit;
        add_clear_count(r[0], rules_to_credit);
        add_clear_count(r[1], rules_to_credit);
        add_clear_count(r[2], rules_to_credit);
        add_clear_count(r[3], rules_to_credit);
        for (GridRule* rule : rules_to_credit)
            rule->level_clear_count += c;
        return APPLY_RULE_RESP_HIT;
    }
    else
    {
        RegionType typ = rule.apply_region_type;
        if (typ.var)
        {
            assert(var_counts[typ.var - 1] >= 0);
            typ.value += var_counts[typ.var - 1];
            typ.var = false;
            if (typ.value > 32)
                return APPLY_RULE_RESP_NONE;
        }
        GridRegion reg(typ);
        FOR_XY_SET(pos, to_reveal)
        {
            reg.elements.set(pos);
        }
        reg.gen_cause = GridRegionCause(&rule, r[0], r[1], r[2], r[3]);
        reg.priority = rule.priority;
        float f = 0;
        for (int i = 0; i < rule.region_count; i++)
            f += r[i]->priority;
        f /= rule.region_count;
        f /= 2;
        f += rule.priority;
        reg.priority = f;

        bool added = add_region(reg);
        if (added)
        {
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

static bool are_connected_old(GridRegion* r0, GridRegion* r1, GridRegion* r2, GridRegion* r3)
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

static bool are_connected(GridRegion* r0, GridRegion* r1, GridRegion* r2, GridRegion* r3)
{
    if (!r1)
        return true;
    if (r0->elements.overlaps(r1->elements))
    {
        if (!r2)
            return true;
        XYSet s = r0->elements | r1->elements;
        if (s.overlaps(r2->elements))
        {
            s |= r2->elements;
            if (!r3)
                return true;
            if (s.overlaps(r3->elements))
                return true;
            else
                return false;
        }
        else
        {
            if (!r3)
                return false;
            if (s.overlaps(r3->elements))
            {
                if (r2->elements.overlaps(r3->elements))
                    return true;
                else
                    return false;
            }
            else
            {
                return false;
            }
        }
    }
    else
    {
        if (!r2)
            return false;
        if (r0->elements.overlaps(r2->elements))
        {
            if (r1->elements.overlaps(r2->elements))
            {
                if (!r3)
                    return true;
                XYSet s = r0->elements | r1->elements | r2->elements;
                if (s.overlaps(r3->elements))
                    return true;
                else
                    return false;
            }
            else            // 02 1
            {
                if (!r3)
                    return false;
                if (r1->elements.overlaps(r3->elements))
                {
                    XYSet s = r0->elements | r2->elements;
                    if (s.overlaps(r3->elements))
                        return true;
                    else
                        return false;
                }
                else
                    return false;
            }
        }
        else
        {
            if (!r3)
                return false;           // 0x1  0x2
            if (r0->elements.overlaps(r3->elements))
            {
                if (r1->elements.overlaps(r3->elements))
                {
                    if (r2->elements.overlaps(r3->elements))
                        return true;
                    if (r1->elements.overlaps(r2->elements))
                        return true;
                    return false;
                }
                else                    // 0x1 0x2 0-3 1x3
                {
                    if (r2->elements.overlaps(r3->elements))
                    {
                        if (r1->elements.overlaps(r2->elements) &&
                            r2->elements.overlaps(r3->elements))
                            return true;
                        return false;
                    }
                    else
                        return false;
                }
            }
            else
                return false;
        }
    }
    assert(0);
    return false;
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
            if (unstale_region->type == rule.region_type[i] || rule.region_type[i].type == RegionType::NONE || (rule.region_type[i].var && (unstale_region->type.type == rule.region_type[i].type)))
                places_for_reg |= 1 << i;
        }
        if (!places_for_reg)
            return APPLY_RULE_RESP_NONE;
    }
    else
        places_for_reg = 0x1;

    std::vector<GridRegion*> pos_regions[4];
    for (int i = 0; i < 4; i++)
    {
        if (i >= rule.region_count)
            pos_regions[i].push_back(NULL);
        else
        {
            for (GridRegion& r : regions)
            {
                if (r.type != rule.region_type[i] && rule.region_type[i].type != RegionType::NONE && !(rule.region_type[i].var && (r.type.type == rule.region_type[i].type)))
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
                            GridRegion* regions[4] = {r0, r1, r2, r3};
                            if (r0 == r1) continue;
                            if (r2 && ((r0 == r2) || (r1 == r2))) continue;
                            if (r3 && ((r0 == r3) || (r1 == r3) || (r2 == r3))) continue;
                            if (!are_connected(r0, r1, r2, r3)) continue;
                            int var_counts[32];
                            for (int i = 0; i < 32; i++)
                                var_counts[i] = -1;
                            if (rule.matches(r0, r1, r2, r3, var_counts))
                            {
                                ApplyRuleResp resp = apply_rule(rule, regions, var_counts);
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
    {
        if (r.gen_cause.rule)
        {
            r.gen_cause.rule->level_used_count++;
        }
        regions_set.insert(&r);
    }
    regions.splice(regions.end(), regions_to_add);
    regions_to_add_multiset.clear();
}

bool Grid::add_one_new_region(GridRegion* r)
{
    if (regions_to_add.empty())
        return false;

    std::list<GridRegion>::iterator it = regions_to_add.begin();
    while (it != regions_to_add.end())
    {
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
            it++;
    }

    if (regions_to_add.empty())
        return false;

    it = regions_to_add.begin();
    std::list<GridRegion>::iterator best_reg = regions_to_add.begin();
    float best_pri = (*best_reg).priority;
    while (it != regions_to_add.end())
    {
        float pri = (*it).priority;
        if (r && (*it).has_ancestor(r))
            pri += 10;
        if (!(*it).gen_cause.rule)
            pri += 20;
        if (pri > best_pri)
        {
            best_pri = pri;
            best_reg = it;
        }
        it++;
    }
    if ((*best_reg).gen_cause.rule)
        (*best_reg).gen_cause.rule->level_used_count++;

    remove_from_regions_to_add_multiset(&(*best_reg));
    regions_set.insert(&(*best_reg));
    regions.splice(regions.end(), regions_to_add, best_reg);
    return true;
}

void Grid::clear_regions()
{
    regions.clear();
    regions_set.clear();
    regions_to_add.clear();
    regions_to_add_multiset.clear();
    deleted_regions.clear();
}

std::string SquareGrid::text_desciption()
{
    return "Square " + std::to_string(size.x) + "x" + std::to_string(size.y) + ((wrapped == WRAPPED_NOT) ? "" : ((wrapped == WRAPPED_SIDE) ? " Plane" : " Recursed"));
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
            rep.push_back(EdgePos(pos, type, XYPosFloat(1,0).angle(), (pos.y + 0.5) * grid_pitch.y));
        if (pos.x == 1)
            rep.push_back(EdgePos(pos, type, XYPosFloat(0,1).angle(), -(pos.y + 0.5) * grid_pitch.y));
    }
}

XYPos SquareGrid::get_square_from_mouse_pos(XYPos pos, XYPos grid_pitch)
{
    if (grid_pitch == XYPos(0, 0))
        return XYPos(-1,-1);
    XYPos rep((pos / grid_pitch));
    if (wrapped == WRAPPED_SIDE)
        rep = rep % size;
    if (!rep.inside(size))
        return XYPos(-1,-1);

    rep = get_base_square(rep);
    if (wrapped == WRAPPED_IN && rep == innie_pos)
        return get_square_from_mouse_pos(pos - innie_pos * grid_pitch, grid_pitch * get_square_size(innie_pos) / size);
    return rep;
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

XYRect SquareGrid::get_icon_pos(XYPos pos, XYPos grid_pitch)
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
    XYPos border = grid_pitch / 24;
    XYPos p = XYPos((index / s + index % s) % s, index % s) * (grid_pitch * min - border * 2) / s;
    return XYRect(pos * grid_pitch + offset * grid_pitch / 2 + p + border, (grid_pitch * min - border * 2) / s);
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

bool TriangleGrid::is_inside(XYPos pos)
{
    if (wrapped == WRAPPED_IN)
    {
        XYPos p = pos;
        int side = size.y / 2;
        XYPos cnt = XYPos(side * 2 - 1, side);
        XYPos s = XYPos(side * 4 - 1, side * 2);
        if (!(side & 1))
            p.x --;
        if (p.x >= cnt.x)
            p.x = s.x - p.x - 1;
        if (p.y >= cnt.y)
            p.y = s.y - p.y - 1;
        if ((p.x + p.y) < (side - 1))
            return false;
        if (p.x < 0)
            return false;
        if (p.y < 0)
            return false;
        return true;
    }
    return pos.inside(size);
}

std::string TriangleGrid::text_desciption()
{
    return "Triangle " + std::to_string(size.x) + "x" + std::to_string(size.y) + ((wrapped == WRAPPED_NOT) ? "" : ((wrapped == WRAPPED_SIDE) ? " Plane" : " Recursed"));
}

std::string TriangleGrid::to_string()
{
    return "B" + Grid::to_string();
}

XYSet TriangleGrid::get_squares()
{
    XYSet rep;
    FOR_XY(pos, XYPos(), size)
    {
        if (!is_inside(pos))
            continue;
        rep.set(pos);
    }
    for ( const auto &m_reg : merged )
    {
        FOR_XY(pos, m_reg.first, m_reg.first + m_reg.second)
        {
            if (pos == m_reg.first)
                continue;
            rep.clear(pos);
        }
    }
    if (wrapped == WRAPPED_IN)
        rep.clear(innie_pos);

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
XYSet TriangleGrid::base_get_neighbors_of_point(XYPos pos)
{
    XYSet rep;
    FOR_XY(offset, XYPos(-1,-1), XYPos(2,1))
    {
        XYPos t = pos + offset;
        if (wrapped == WRAPPED_SIDE)
            t = t % size;
        if (wrapped == WRAPPED_IN)
        {
            if (!is_inside(t))
                continue;
            if (get_base_square(t) == innie_pos)
                continue;
        }
        else if (!is_inside(t))
            continue;
        rep.set(get_base_square(t));
    }
    return rep;

}

XYSet TriangleGrid::get_neighbors_of_point(XYPos pos)
{

    assert(!((pos.x ^ pos.y) & 1));
    XYSet rep;
    
    if (wrapped == WRAPPED_IN)
    {
        int side = size.y / 2;

        XYPos n = pos - (innie_pos + XYPos(1,1));
        XYPos an = XYPos(abs(n.x), abs(n.y));
        if (an == XYPos(1,1))
        {
            XYPos k = XYPos((side / 2) * 2, 0);
            if (n.x > 0)
                k.x += side * 2;
            if (n.y > 0)
                k.y += side * 2;
            rep = rep | base_get_neighbors_of_point(k);
        }
        if (an == XYPos(2,0))
        {
            XYPos k = XYPos(((n.x > 0) ? side * 4 : 0) - (side & 1), side);
            rep = rep | base_get_neighbors_of_point(k);
        }


        XYPos p = pos;
        XYPos cnt = XYPos(side * 2, side);
        XYPos s = XYPos(side * 4 - 2, side * 2);
        XYPos r = innie_pos;
        XYPos rs = innie_pos - XYPos(1,0);
        if (!(side & 1))
            p.x --;
        if (p.x >= cnt.x)
        {
            r.x += 2;
            rs.x += 4;
            p.x = s.x - p.x;
        }
        if (p.y >= cnt.y)
        {
            r.y += 2;
            rs.y += 1;
            p.y = s.y - p.y;
        }
        bool ts = ((p.x + p.y) <= (side - 1));

        if (ts)
            rep.set(get_base_square(rs));
        if (p.y == 0)
        {
            if (pos.y == 0)
                rep.set(get_base_square(innie_pos + XYPos(1, -1)));
            else
                rep.set(get_base_square(innie_pos + XYPos(1, 2)));
        }
        if (ts && (p.y == 0))
        {
            rep = rep | base_get_neighbors_of_point(r);
        }
    }
    rep = rep | base_get_neighbors_of_point(pos);
    return rep;
}

XYSet TriangleGrid::base_get_neighbors(XYPos pos)
{
    bool downwards = (pos.x ^ pos.y) & 1;
    XYSet rep;

    if (!downwards)
    {
        rep = rep | get_neighbors_of_point(pos + XYPos(0, 0));
        rep = rep | get_neighbors_of_point(pos + XYPos(1, 1));
        rep = rep | get_neighbors_of_point(pos + XYPos(-1, 1));
    }
    else
    {
        rep = rep | get_neighbors_of_point(pos + XYPos(0, 1));
        rep = rep | get_neighbors_of_point(pos + XYPos(1, 0));
        rep = rep | get_neighbors_of_point(pos + XYPos(-1, 0));
    }


    if (wrapped == WRAPPED_IN)
    {
        int side = size.y / 2;
        XYPos k = pos - (innie_pos + XYPos(1,0));
        XYPos ak = XYPos(abs(k.x), k.y);
        if (ak.x == 2 && (ak.y == 0 || ak.y == 1))
        {
            for (int i = 0; i < side; i++)
            {
                XYPos t;
                t.x = (k.x > 0) ? 4 * side - 3 - i : i;
                t.x += (!(side & 1));
                t.y = k.y ? side + i : side - i - 1;
                rep.set(get_base_square(t));
                t.x++;
                rep.set(get_base_square(t));
            }
        }
        if (k.x == 0 && (k.y == -1 || k.y == 2))
        {
            for (int i = side; i < side * 3; i++)
            {
                XYPos t;
                t.x = i;
                t.y = (k.y > 0) ?  side * 2 - 1: 0;
                rep.set(get_base_square(t));
            }
        }
    }


    // FOR_XY(offset, XYPos(-2,-1), XYPos(3,2))
    // {
    //     if (offset == XYPos(-2, downwards ? 1 : -1)) continue;
    //     if (offset == XYPos(2, downwards ? 1 : -1)) continue;
    //     XYPos t = pos + offset;
    //     if (wrapped == WRAPPED_SIDE)
    //         t = t % size;
    //     if (wrapped == WRAPPED_IN)
    //     {
    //         if (get_base_square(t) == innie_pos)
    //             continue;
    //         if (!is_inside(t))
    //             continue;
    //     }
    //     else if (!is_inside(t))
    //         continue;
    //     rep.set(get_base_square(t));
    // }

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
            rep.push_back(EdgePos(pos, type, XYPosFloat(1, 0).angle(), (pos.y + 0.5) * grid_pitch.y));
        else if (pos.x == 1)
        {
            rep.push_back(EdgePos(pos, type, XYPosFloat(1, std::sqrt(3)).angle(), (((size.y - 1) / 2) - pos.y)  * grid_pitch.y));
        }
        else if (pos.x == 2)
        {
            rep.push_back(EdgePos(pos, type, XYPosFloat(-1, std::sqrt(3)).angle(), -(pos.y + 1)  * grid_pitch.y));
        }
        else
            assert(0);
    }
}

XYPos TriangleGrid::get_square_from_mouse_pos(XYPos pos, XYPos grid_pitch)
{
    if (grid_pitch.x <= 0 || grid_pitch.y <= 0)
        return XYPos(-1,-1);
    if (wrapped == WRAPPED_IN && !(size.y / 2 & 1))
        pos.x += grid_pitch.x;
    XYPos rep(pos / grid_pitch);
    XYPos rem(pos % grid_pitch);
    if (!((rep.x ^ rep.y) & 1))
        rem.y = grid_pitch.y - rem.y - 1;
    rem.x = rem.x * std::sqrt(3);
    if (rem.y > rem.x)
        rep.x--;
    if (wrapped == WRAPPED_SIDE)
        rep = rep % size;
    if (!is_inside(rep))
        return XYPos(-1,-1);

    rep = get_base_square(rep);
    if (wrapped == WRAPPED_IN && rep == innie_pos)
        return get_square_from_mouse_pos(pos - innie_pos * grid_pitch, (grid_pitch * (get_square_size(innie_pos) + XYPos(1,0))) / size);

    return rep;
}

XYPos TriangleGrid::get_grid_pitch(XYPos grid_size)
{
    XYPosFloat gsize((size.x + ((wrapped == WRAPPED_IN) ? 0.0 : 1.0)) / 2, size.y * std::sqrt(3) / 2);
    double s = std::min(grid_size.x / gsize.x, grid_size.y / gsize.y);
    return XYPos(ceil(s / 2.0), ceil(std::sqrt(3) * s / 2.0));
}

XYRect TriangleGrid::get_square_pos(XYPos pos, XYPos grid_pitch)
{
    XYPos sq_size = get_square_size(pos);
    if (wrapped == WRAPPED_IN && !(size.y / 2 & 1))
        pos.x--;
    return XYRect(pos * grid_pitch, (sq_size + XYPos(1,0)) * grid_pitch);
}

XYRect TriangleGrid::get_icon_pos(XYPos pos, XYPos grid_pitch)
{
    XYPos sq_size = get_square_size(pos);
    if (sq_size == XYPos(3,2))
    {
        int s = grid_pitch.x * 3;
        XYPos siz = XYPos(s,s);
        XYPos off = ((grid_pitch * XYPos(4,2)) - siz) / 2;
        if (wrapped == WRAPPED_IN && !(size.y / 2 & 1))
            pos.x--;
        return XYRect(pos * grid_pitch + off, siz);
    }
    bool downwards = (pos.x ^ pos.y) & 1;
    XYPos border(grid_pitch.x / 2, grid_pitch.y / 6);
    if (wrapped == WRAPPED_IN && !(size.y / 2 & 1))
        pos.x--;
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
        XYPos border = grid_pitch / 24;

        double bsize = double(grid_pitch.x * 2 - border.x * 2) / (s - 1 + 1 / std::sqrt(3));

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
        if (wrapped == WRAPPED_IN && !(size.y / 2 & 1))
            pos.x--;
        return XYRect(pos * grid_pitch + p + border, XYPos(bsize, bsize));
    }

    XYPos border = grid_pitch / 24;
    bool downwards = (pos.x ^ pos.y) & 1;
    unsigned s = 3;
    while (total > ((s * (s + 1)) / 2))
        s++;
    double bsize = double(grid_pitch.x * 2 - border.x * 6) / (std::sqrt(3) + s - 1);

    XYPos gpos = XYPos(0,0);
    while (index >= (s - gpos.y))
    {
        index -= s - gpos.y;
        gpos.y++;
    }
    gpos.x = index;

    XYPos p(bsize * std::sqrt(3) / 2 - (bsize / 2) + gpos.x * bsize + gpos.y * bsize / 2 + border.x * 3, gpos.y * std::sqrt(3) * bsize / 2 + border.y);
    if (wrapped == WRAPPED_IN && !(size.y / 2 & 1))
        pos.x--;

    if (downwards)
        return XYRect(pos * grid_pitch + p, XYPos(bsize, bsize));
    else
        return XYRect(pos * grid_pitch + XYPos(p.x, grid_pitch.y - bsize - p.y), XYPos(bsize, bsize));
}

void TriangleGrid::render_square(XYPos pos, XYPos grid_pitch, std::vector<RenderCmd>& cmds)
{
    XYPos sq_size = get_square_size(pos);
    XYPos line_seg(grid_pitch.x * 2, grid_pitch.y / 24 + 1);
    if (sq_size == XYPos(3,2))
    {
        if (wrapped == WRAPPED_IN && !(size.y / 2 & 1))
            pos.x--;
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
    if (wrapped == WRAPPED_IN && !(size.y / 2 & 1))
        pos.x--;
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
    bool done_innie = (wrapped != WRAPPED_IN);
    for (int i = 0; i < merged_count;)
    {
        XYPos m_pos(unsigned(rnd) % size.x, unsigned(rnd) % size.y);
        XYPos m_size(3, 2);

        if ((m_pos.x ^ m_pos.y) & 1)
            continue;
        if (!is_inside(m_size + m_pos - XYPos(1,1)))
            continue;
        if (!is_inside(m_pos))
            continue;
        if(!done_innie)
        {
            if (!is_inside(m_pos - XYPos(1,0)))
                continue;
            if (!is_inside(m_pos - XYPos(0,1)))
                continue;
            if (!is_inside(m_pos + XYPos(3,0)))
                continue;
            if (!is_inside(m_pos + XYPos(3,1)))
                continue;
            if (!is_inside(m_pos + XYPos(0,2)))
                continue;
        }
        bool bad = false;
        for ( const auto &m_reg : merged)
        {
            if ( (std::min(m_reg.first.x + m_reg.second.x, m_pos.x + m_size.x) > std::max(m_reg.first.x, m_pos.x)) &&
                 (std::min(m_reg.first.y + m_reg.second.y, m_pos.y + m_size.y) > std::max(m_reg.first.y, m_pos.y)) )
                bad = true;
        }
        if (bad)
            continue;
        if (!done_innie)
        {
            done_innie = true;
            innie_pos = m_pos;
        }
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
    assert(is_inside(p));
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

std::string HexagonGrid::text_desciption()
{
    return "Hexagon " + std::to_string(size.x) + "x" + std::to_string(size.y) + ((wrapped == WRAPPED_NOT) ? "" : ((wrapped == WRAPPED_SIDE) ? " Plane" : " Recursed"));
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
            rep.push_back(EdgePos(pos, type, XYPosFloat(1.5, std::sqrt(3)/2).angle(), -((pos.y - size.y) * 3 - 0.5) * grid_pitch.x));
        }
        else if (pos.x == 1)
        {
            rep.push_back(EdgePos(pos, type, XYPosFloat(0, 1).angle(), -(2 + pos.y * 3) * grid_pitch.x));
        }
        else if (pos.x == 2)
        {
            rep.push_back(EdgePos(pos, type, XYPosFloat(-1.5, std::sqrt(3)/2).angle(), -(std::sqrt(7) + pos.y * 3) * grid_pitch.x));
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

XYRect HexagonGrid::get_icon_pos(XYPos pos, XYPos grid_pitch)
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
    XYPos border = grid_pitch / 10;
    int downstep = pos.x & 1;
    unsigned s = 2;
    while (total > (1 + 3 * (s * (s - 1))))
        s++;

    double bsize = double(grid_pitch.x * 2 - border.x * 2) / (s - 1 + 1 / std::sqrt(3));

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
    XYPos ppos = (pos * XYPos(3, 2) + XYPos(0, downstep)) * grid_pitch + border;

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
        dst = XYRect ((pos * XYPos(3, 2) + XYPos(0, downstep) + XYPos(1, 0)) * grid_pitch, XYPos(grid_pitch.x * 2, grid_pitch.y / 10 + 1));
        cmds.push_back(RenderCmd(src, dst, 0, XYPos(0,1)));
        dst = XYRect ((pos * XYPos(3, 2) + XYPos(0, downstep) + XYPos(3, 0)) * grid_pitch, XYPos(grid_pitch.x * 2, grid_pitch.y / 10 + 1));
        cmds.push_back(RenderCmd(src, dst, 60, XYPos(0,1)));
        dst = XYRect ((pos * XYPos(3, 2) + XYPos(0, downstep) + XYPos(4, 1)) * grid_pitch, XYPos(grid_pitch.x * 2, grid_pitch.y / 10 + 1));
        cmds.push_back(RenderCmd(src, dst, 120, XYPos(0,1)));
        dst = XYRect ((pos * XYPos(3, 2) + XYPos(0, downstep) + XYPos(3, 2)) * grid_pitch, XYPos(grid_pitch.x * 2, grid_pitch.y / 10 + 1));
        cmds.push_back(RenderCmd(src, dst, 180, XYPos(0,1)));
        dst = XYRect ((pos * XYPos(3, 2) + XYPos(0, downstep) + XYPos(1, 2)) * grid_pitch, XYPos(grid_pitch.x * 2, grid_pitch.y / 10 + 1));
        cmds.push_back(RenderCmd(src, dst, 240, XYPos(0,1)));
        dst = XYRect ((pos * XYPos(3, 2) + XYPos(0, downstep) + XYPos(0, 1)) * grid_pitch, XYPos(grid_pitch.x * 2, grid_pitch.y / 10 + 1));
        cmds.push_back(RenderCmd(src, dst, 300, XYPos(0,1)));
    }
}
XYPos HexagonGrid::get_wrapped_size(XYPos grid_pitch)
{
    return XYPos(size.x * 3, size.y * 2) * grid_pitch;
}

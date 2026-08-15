#include "z3++.h"


#include "Grid.h"
#include <bit>
#include <sstream>
#include <algorithm>

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
static unsigned get_valid_cells_mask(int region_count, int neg_reg_count)
{
    unsigned mask = 0;
    for (int i = 1; i < (1 << region_count); i++)
    {
        mask |= 1 << i;
        if (neg_reg_count == 1)
            mask |= 1 << (i | ((i & 1) << 3));
        if (neg_reg_count == 2)
        {
            mask |= 1 << (i | ((i & 1) << 2));
            mask |= 1 << (i | ((i & 2) << 2));
            mask |= 1 << (i | ((i & 3) << 2));
        }
    }
    return mask;
}

static bool is_if_partition_cell(const RegionIfType& clue, unsigned index)
{
    switch (clue.if_partition)
    {
    case RegionIfType::ALTERNATING:
        return !(index & 1);
    case RegionIfType::SINGLE_CELL:
        return index == clue.if_partition_index;
    case RegionIfType::ADJACENT_PAIR:
        return index == clue.if_partition_index || index == clue.if_partition_index2;
    }
    return false;
}

static void validate_region_type(const RegionType& region_type)
{
    const unsigned type = static_cast<unsigned>(region_type.type);
    const bool known_type = type <= RegionType::BOX ||
                            region_type.type == RegionType::SET ||
                            region_type.type == RegionType::VISIBILITY;
    if (!known_type || region_type.var > 0x1f)
        throw(std::runtime_error("Invalid region type"));
}

static z3::expr_vector make_count_vector(
    z3::context& c, z3::solver& s, z3::expr_vector& var_vec,
    const RegionType square_counts[16], const char* prefix)
{
    z3::expr_vector counts(c);
    counts.push_back(c.bool_const((std::string(prefix) + "DUMMY").c_str()));
    for (int i = 1; i < 16; i++)
    {
        std::stringstream name;
        name << prefix << i;
        counts.push_back(c.int_const(name.str().c_str()));
        s.add(counts[i] >= 0);
        const int maximum = square_counts[i].max();
        if (maximum >= 1000)
            continue;
        if (square_counts[i].var)
            s.add(counts[i] <= var_vec[square_counts[i].var - 1] + maximum);
        else
            s.add(counts[i] <= maximum);
    }
    return counts;
}


std::string RegionType::val_as_str(int offset) const
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
            if (value + offset > 0)
            {
                s += '+';
                s += std::to_string(value + offset);
            }
            else
            {
                s += '-';
                s += std::to_string(-(value + offset));
            }
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

bool RegionType::mapper_based_equal(const RegionType& other, uint8_t mapper[32])
{
    if (type != other.type)
        return false;
    if (value != other.value)
        return false;
    if (std::popcount(var) != std::popcount(other.var))
        return false;
    if (!var)
        return true;
    assert(var < 32);
    if (mapper[var])
        return (mapper[var] == other.var);
    mapper[var] = other.var;
    return true;
}

template<class RESP, class IN, class OTHER>
RESP RegionType::apply_rule_imp(IN in, OTHER other)
{
    if (type == NONE)
    {
        return (in == in);
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
    if (type == PRIME)
    {
        return (((in - other) == 2) || ((in - other) == 3) || ((in - other) == 5) || ((in - other) == 7) || ((in - other) == 11) || ((in - other) == 13) || ((in - other) == 17) || ((in - other) == 19) || ((in - other) == 23) || ((in - other) == 29) || ((in - other) == 31));
    }
    if (type == TRIANGLE)
    {
        return (((in - other) == 0) || ((in - other) == 1) || ((in - other) == 3) || ((in - other) == 6) || ((in - other) == 10) || ((in - other) == 15) || ((in - other) == 21) || ((in - other) == 28));
    }
    if (type == POW2)
    {
        return (((in - other) == 1) || ((in - other) == 2) || ((in - other) == 4) || ((in - other) == 8) || ((in - other) == 16) || ((in - other) == 32));
    }
    if (type == FIBONACCI)
    {
        return (((in - other) == 1) || ((in - other) == 2) || ((in - other) == 3) || ((in - other) == 5) || ((in - other) == 8) || ((in - other) == 13) || ((in - other) == 21) || ((in - other) == 34));
    }
    if (type == BOX)
    {
        return ((in - other) % 4) < 2;
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
bool RegionType::apply_int_rule(unsigned in)
{
    assert(!var);
    return apply_rule_imp<bool,int>(in, value);
}

bool RegionType::apply_int_rule(unsigned in, int vars[32])
{
    int v = 0;
    // if (var)
    // {
    //     for (unsigned i = 0; i < 5; i++)
    //     {
    //         if ((var >> i) & 1)
    //         {
    //             if (vars[(1 << i) - 1] == -1)
    //                 return false;
    //             v += vars[(1 << i) - 1];
    //         }
    //     }
    //     assert(vars[var - 1]  == v);
    // }
    if (var)
        v += vars[var - 1];
    return apply_rule_imp<bool,int>(in, v + value);
}

int RegionType::max() const
{
    if (type == NONE)
        return 1000;
    if (type == EQUAL)
        return value;
    if (type == NOTEQUAL)
        return 1000;
    if (type == LESS)
        return value;
    if (type == MORE)
        return 1000;
    if (type == XOR2)
        return value + 2;
    if (type == XOR3)
        return value + 3;
    if (type == XOR22)
        return value + 4;
    if (type == XOR222)
        return value + 6;
    if (type == PARITY)
        return 1000;
    if (type == XOR1)
        return value + 1;
    if (type == XOR11)
        return value + 2;
    if (type == PRIME)
        return 1000;
    if (type == TRIANGLE)
        return 1000;
    if (type == POW2)
        return 1000;
    if (type == FIBONACCI)
        return 1000;
    if (type == BOX)
        return 1000;
    assert(0);
}


GridRegion::GridRegion(RegionIfType type_)
{
    type = type_.type;
    if_type = type_.if_type;
    colour = colours_used[type.value]++;
}

bool GridRegion::overlaps(GridRegion& other)
{
    return elements.overlaps(other.elements);
}

void GridRegion::next_colour()
{
    colour = colours_used[type.value]++;
}

bool GridRegion::has_ancestor(GridRegion* other, std::set<GridRegion*>& has, std::set<GridRegion*>& hasnt)
{
    if (this == other)
        return true;
    if (gen_cause.rule && gen_cause.rule->apply_region_type.type == RegionType::SET)
        return false;

    if (has.count(this))
        return true;
    if (hasnt.count(this))
        return false;

    for (int i = 0; i < 4; i++)
    {
        if (gen_cause.regions[i])
        {
            if (gen_cause.regions[i]->has_ancestor(other, has, hasnt))
            {
                has.insert(this);
                return true;
            }
        }
    }
    hasnt.insert(this);
    return false;
}

bool GridRule::has_valid_structure() const
{
    return region_count <= 4 &&
        neg_reg_count <= 2 &&
        if_reg_count <= 2 &&
        neg_reg_count <= region_count &&
        if_reg_count * 2 <= region_count &&
        region_count + neg_reg_count <= 4 &&
        !(neg_reg_count && if_reg_count);
}

int GridRule::input_index_for_slot(int slot) const
{
    assert(has_valid_structure());
    assert(slot >= 0 && slot < region_count);
    if (slot < if_reg_count * 2)
        return slot / 2;
    return slot - if_reg_count;
}

GridRegionCause GridRule::make_cause(
    GridRegion* r1, GridRegion* r2, GridRegion* r3, GridRegion* r4)
{
    assert(has_valid_structure());
    GridRegion* raw_regions[4] = {r1, r2, r3, r4};
    GridRegion* input_regions[4] = {};
    for (int slot = 0; slot < region_count; slot++)
    {
        if (slot < if_reg_count * 2 && (slot & 1))
            continue;
        input_regions[input_index_for_slot(slot)] = raw_regions[slot];
    }
    return GridRegionCause(
        this, input_regions[0], input_regions[1],
        input_regions[2], input_regions[3]);
}

GridRule::GridRule(SaveObject* sobj)
{
    SaveObjectMap* omap = sobj->get_map();
    const int64_t loaded_region_count =
        omap->get_num("region_count");
    int64_t loaded_neg_reg_count = 0;
    int64_t loaded_if_reg_count = 0;
    if (omap->has_key("neg_reg_count"))
        loaded_neg_reg_count = omap->get_num("neg_reg_count");
    if (omap->has_key("if_reg_count"))
        loaded_if_reg_count = omap->get_num("if_reg_count");
    if (loaded_region_count < 0 || loaded_region_count > 4 ||
        loaded_neg_reg_count < 0 || loaded_neg_reg_count > 2 ||
        loaded_if_reg_count < 0 || loaded_if_reg_count > 2)
        throw(std::runtime_error("Invalid rule region structure"));
    region_count = loaded_region_count;
    neg_reg_count = loaded_neg_reg_count;
    if_reg_count = loaded_if_reg_count;
    if (!has_valid_structure())
        throw(std::runtime_error("Invalid rule region structure"));
    apply_region_type = RegionType('a',omap->get_num("apply_region_type"));
    if (omap->has_key("apply_if_region_type"))
        apply_if_region_type = RegionType('a',omap->get_num("apply_if_region_type"));

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

    if (apply_region_type.type == RegionType::VISIBILITY)
    {
        apply_region_type.var = 0;
        if (if_reg_count)
            apply_region_bitmap &= ~2ull;
        if (if_reg_count >= 2)
            apply_region_bitmap &= ~8ull;
    }

    if (omap->has_key("paused"))
        paused = omap->get_num("paused");
    if (omap->has_key("priority"))
        priority = omap->get_num("priority");
    if (priority < -100)
    {
        priority = 0;
        paused = true;
    }
    if (omap->has_key("group"))
        group = omap->get_num("group");

    if (omap->has_key("neg_apply_region_bitmap"))
        neg_apply_region_bitmap = omap->get_num("neg_apply_region_bitmap");

    validate_region_type(apply_region_type);
    validate_region_type(apply_if_region_type);

    if (apply_region_type.type >= RegionType::SET)
    {
        apply_if_region_type = RegionType();
        neg_apply_region_bitmap = 0;
    }
    else if (apply_if_region_type.type >= RegionType::SET ||
             (apply_if_region_type.type != RegionType::NONE &&
              apply_region_type.type == RegionType::NONE))
    {
        apply_if_region_type = RegionType();
        neg_apply_region_bitmap = 0;
    }

    // An implication with no antecedent
    // cells is just its consequent. Older
    // saves can contain this intermediate
    // editor state, so load it as the
    // equivalent ordinary output.
    if (apply_if_region_type.type != RegionType::NONE &&
        !neg_apply_region_bitmap)
        apply_if_region_type = RegionType();
    if (apply_region_type.type < 100)
        apply_region_bitmap &= ~1ull;
    if (apply_if_region_type.type == RegionType::NONE)
        neg_apply_region_bitmap &= apply_region_bitmap;
    SaveObjectList* rlist = omap->get_item("region_type")->get_list();
    if (rlist->get_count() != region_count)
        throw(std::runtime_error(
            "Rule region type count does not match region_count"));
    for (unsigned i = 0; i < rlist->get_count(); i++)
    {
        region_type[i] = RegionType('a',rlist->get_num(i));
        validate_region_type(region_type[i]);
    }

    rlist = omap->get_item("square_counts")->get_list();
    if (rlist->get_count() > 16)
        throw(std::runtime_error("Too many rule square counts"));
    for (unsigned i = 0; i < rlist->get_count(); i++)
    {
        int v = rlist->get_num(i);
        square_counts[i] = RegionType('a',v);
        validate_region_type(square_counts[i]);
    }
    if (omap->has_key("used_count"))
        used_count = omap->get_num("used_count");
    if (omap->has_key("clear_count"))
        clear_count = omap->get_num("clear_count");
    if (omap->has_key("cpu_time"))
        cpu_time = omap->get_num("cpu_time");
    if (omap->has_key("comment"))
        comment = omap->get_string("comment");

    // GridRule why;
    // int vars[5];
    // IsLogicalRep rep = is_legal(why, vars);
    // assert(rep == GridRule::OK || rep == GridRule::LOSES_DATA);
}

SaveObject* GridRule::save(bool lite)
{
    SaveObjectMap* omap = new SaveObjectMap;
    omap->add_num("region_count", region_count);
    if (neg_reg_count)
        omap->add_num("neg_reg_count", neg_reg_count);
    if (if_reg_count)
        omap->add_num("if_reg_count", if_reg_count);
    omap->add_num("apply_region_type", apply_region_type.as_int());
    if (apply_if_region_type.type != RegionType::NONE)
        omap->add_num("apply_if_region_type", apply_if_region_type.as_int());
    omap->add_num("apply_region_bitmap", apply_region_bitmap);
    if (neg_apply_region_bitmap)
        omap->add_num("neg_apply_region_bitmap", neg_apply_region_bitmap);
    omap->add_num("priority", priority);
    omap->add_num("paused", paused);
    omap->add_num("group", group);

    SaveObjectList* region_type_list = new SaveObjectList;
    for (int i = 0; i < region_count; i++)
        region_type_list->add_num(region_type[i].as_int());
    omap->add_item("region_type", region_type_list);

    SaveObjectList* square_counts_list = new SaveObjectList;
    for (int i = 0; i < 16; i++)
        square_counts_list->add_num(square_counts[i].as_int());
    omap->add_item("square_counts", square_counts_list);
    if (!lite)
    {
        omap->add_num("used_count", used_count);
        omap->add_num("clear_count", clear_count);
        omap->add_num("cpu_time", cpu_time);
    }
    if (comment != "")
        omap->add_string("comment", comment);
    return omap;
}

void GridRule::get_square_counts(uint8_t square_counts[16], GridRegion* r1, GridRegion* r2, GridRegion* r3, GridRegion* r4)
{
    for (int i = 0; i < 16; i++)
        square_counts[i] = 0;

    if (!r1)
        return;

    if (r1->is_if_then())
    {
        XYSet a0 = r1->elements_neg;
        XYSet a0_neg = r1->elements;
        if (!r2)
        {
            square_counts[1] = (a0 & ~a0_neg).count();
            square_counts[2] = (~a0 &  a0_neg).count();
            square_counts[3] = (a0 &  a0_neg).count();
            return;
        }
        XYSet a1 = r2->elements;
        if (r2->is_if_then())
        {
            XYSet a1_neg = r2->elements;
            a1 = r2->elements_neg;
            square_counts[1] = ( a0 & ~a0_neg & ~a1 & ~a1_neg).count();
            square_counts[2] = (~a0 &  a0_neg & ~a1 & ~a1_neg).count();
            square_counts[3] = ( a0 &  a0_neg & ~a1 & ~a1_neg).count();
            square_counts[4] = (~a0 & ~a0_neg &  a1 & ~a1_neg).count();
            square_counts[5] = ( a0 & ~a0_neg &  a1 & ~a1_neg).count();
            square_counts[6] = (~a0 &  a0_neg &  a1 & ~a1_neg).count();
            square_counts[7] = ( a0 &  a0_neg &  a1 & ~a1_neg).count(); 
            square_counts[8] = (~a0 & ~a0_neg & ~a1 &  a1_neg).count();
            square_counts[9] = ( a0 & ~a0_neg & ~a1 &  a1_neg).count();
            square_counts[10] = (~a0 &  a0_neg & ~a1 &  a1_neg).count();
            square_counts[11] = ( a0 &  a0_neg & ~a1 &  a1_neg).count();
            square_counts[12] = (~a0 & ~a0_neg &  a1 &  a1_neg).count();
            square_counts[13] = ( a0 & ~a0_neg &  a1 &  a1_neg).count();
            square_counts[14] = (~a0 &  a0_neg &  a1 &  a1_neg).count();
            square_counts[15] = ( a0 &  a0_neg &  a1 &  a1_neg).count();
            return;
        }
        if (!r3)
        {
            square_counts[1]  = ( a0 & ~a0_neg & ~a1).count();
            square_counts[2]  = (~a0 &  a0_neg & ~a1).count();
            square_counts[3]  = ( a0 &  a0_neg & ~a1).count();
            square_counts[4]  = (~a0 & ~a0_neg &  a1).count();
            square_counts[5]  = ( a0 & ~a0_neg &  a1).count();
            square_counts[6]  = (~a0 &  a0_neg &  a1).count();
            square_counts[7]  = ( a0 &  a0_neg &  a1).count();
            return;
        }
        XYSet a2 = r3->elements;
        square_counts[1] = ( a0 & ~a0_neg & ~a1 & ~a2).count();
        square_counts[2] = (~a0 &  a0_neg & ~a1 & ~a2).count();
        square_counts[3] = ( a0 &  a0_neg & ~a1 & ~a2).count();
        square_counts[4] = (~a0 & ~a0_neg &  a1 & ~a2).count();
        square_counts[5] = ( a0 & ~a0_neg &  a1 & ~a2).count();
        square_counts[6] = (~a0 &  a0_neg &  a1 & ~a2).count();
        square_counts[7] = ( a0 &  a0_neg &  a1 & ~a2).count(); 
        square_counts[8] = (~a0 & ~a0_neg & ~a1 &  a2).count();
        square_counts[9] = ( a0 & ~a0_neg & ~a1 &  a2).count();
        square_counts[10] = (~a0 &  a0_neg & ~a1 &  a2).count();
        square_counts[11] = ( a0 &  a0_neg & ~a1 &  a2).count();
        square_counts[12] = (~a0 & ~a0_neg &  a1 &  a2).count();
        square_counts[13] = ( a0 & ~a0_neg &  a1 &  a2).count();
        square_counts[14] = (~a0 &  a0_neg &  a1 &  a2).count();
        square_counts[15] = ( a0 &  a0_neg &  a1 &  a2).count();
        return;
    }

    XYSet a0 = r1->elements;
    XYSet a0_neg = r1->elements_neg;
    if (!r2)
    {
        square_counts[1] = (a0 & ~a0_neg).count();
        square_counts[9] = (a0 &  a0_neg).count();
        return;
    }

    XYSet a1 = r2->elements;
    if (!r3)
    {
        XYSet a1_neg = r2->elements_neg;
        if (a1_neg.any())
        {
            square_counts[1]  = ( a0 & ~a1 & ~a0_neg          ).count();
            square_counts[2]  = (~a0 &  a1           & ~a1_neg).count();
            square_counts[3]  = ( a0 &  a1 & ~a0_neg & ~a1_neg).count();

            square_counts[5]  = ( a0 & ~a1 &  a0_neg          ).count();
            square_counts[7]  = ( a0 &  a1 &  a0_neg & ~a1_neg).count();

            square_counts[10] = (~a0 &  a1 &            a1_neg).count();
            square_counts[11] = ( a0 &  a1 & ~a0_neg &  a1_neg).count();

            square_counts[15] = ( a0 &  a1 &  a0_neg &  a1_neg).count();
        }
        else
        {
            square_counts[1]  = ( a0 & ~a1 & ~a0_neg).count();
            square_counts[2]  = (~a0 &  a1          ).count();
            square_counts[3]  = ( a0 &  a1 & ~a0_neg).count();

            square_counts[9]  = ( a0 & ~a1 &  a0_neg).count();
            square_counts[11] = ( a0 &  a1 &  a0_neg).count();
        }
        return;
    }

    XYSet a2 = r3->elements;
    if (!r4)
    {
        square_counts[1] =  ( a0 & ~a1 & ~a2 & ~a0_neg).count();
        square_counts[2] =  (~a0 &  a1 & ~a2          ).count();
        square_counts[3] =  ( a0 &  a1 & ~a2 & ~a0_neg).count();
        square_counts[4] =  (~a0 & ~a1 &  a2          ).count();
        square_counts[5] =  ( a0 & ~a1 &  a2 & ~a0_neg).count();
        square_counts[6] =  (~a0 &  a1 &  a2          ).count();
        square_counts[7] =  ( a0 &  a1 &  a2 & ~a0_neg).count();

        square_counts[9] =   (a0 & ~a1 & ~a2 &  a0_neg).count();
        square_counts[11] =  (a0 &  a1 & ~a2 &  a0_neg).count();
        square_counts[13] =  (a0 & ~a1 &  a2 &  a0_neg).count();
        square_counts[15] =  (a0 &  a1 &  a2 &  a0_neg).count();
        return;
    }

    XYSet a3 = r4->elements;
    {
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
    r.if_reg_count = if_reg_count;
    r.apply_region_type = apply_region_type;
    r.apply_if_region_type = apply_if_region_type;
    r.neg_reg_count = neg_reg_count;
    
    for (int i = 0; i < region_count; i++)
        r.region_type[i] = region_type[p[i]];

    unsigned mask = get_valid_cells_mask(region_count, neg_reg_count);
    for (int i = 0; i < 16; i++)
    {
        if (!((mask >> i) & 1))
            continue;
        int p_index = 0;
        for (int a = 0; a < region_count; a++)
        {
            if ((i >> a) & 1)
                p_index |= 1 << p[a];
        }
        if (neg_reg_count == 1)
            p_index |= i & 8;
        if (neg_reg_count == 2)
        {
            if (p[0] == 1)
            {
                p_index |= (i & 8) >> 1;
                p_index |= (i & 4) << 1;
            }
            else
                p_index |= i & 0xC;
        }

        r.square_counts[i] = square_counts[p_index];
        if (apply_region_type.type != RegionType::VISIBILITY)
        {
            r.apply_region_bitmap |= ((apply_region_bitmap >> p_index) & 1) << i;
            r.neg_apply_region_bitmap |= ((neg_apply_region_bitmap >> p_index) & 1) << i;
        }

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
    if (neg_reg_count != other.neg_reg_count)
        return false;
    if (if_reg_count != other.if_reg_count)
        return false;
    uint8_t mapper[32] = {};

    for (int i = 0; i < region_count; i++)
        if (!region_type[i].mapper_based_equal(other.region_type[i], mapper))
            return false;
    unsigned mask = get_valid_cells_mask(region_count, neg_reg_count);
    for (int i = 0; i < 16; i++)
    {
        if (!((mask >> i) & 1))
            continue;
        if (!square_counts[i].mapper_based_equal(other.square_counts[i], mapper))
            return false;
        if (apply_region_type.type != RegionType::VISIBILITY)
        {
            if (((apply_region_bitmap >> i) & 1) != ((other.apply_region_bitmap >> i) & 1))
                return false;
            if (((neg_apply_region_bitmap >> i) & 1) != ((other.neg_apply_region_bitmap >> i) & 1))
                return false;
        }
    }
    if (apply_region_type.type == RegionType::VISIBILITY &&
        (apply_region_bitmap != other.apply_region_bitmap ||
         neg_apply_region_bitmap != other.neg_apply_region_bitmap))
        return false;
    if (!apply_region_type.mapper_based_equal(other.apply_region_type, mapper))
        return false;
    if ((apply_if_region_type.type != RegionType::NONE ||
         other.apply_if_region_type.type != RegionType::NONE) &&
        !apply_if_region_type.mapper_based_equal(other.apply_if_region_type, mapper))
        return false;
    for (int i = 1; i < 32; i++)
    {
        if (!mapper[i])
            continue;
        for (int j = i + 1; j < 32; j++)
        {
            if (!mapper[j])
                continue;
            if (!(i & j) )   //try adding
            {
                if (mapper[i] & mapper[j])
                    return false;
                if (mapper[i | j])
                {
                    if (mapper[i | j] != (mapper[i] | mapper[j]))
                        return false;
                }
                else
                {
                    mapper[i | j] = mapper[i] | mapper[j];
                    i = 0;
                    break;
                }
            }
            else if ((i & j) == i)   //try subtracting
            {
                if ((mapper[i] & mapper[j]) != mapper[i])
                    return false;
                if (mapper[j & ~i])
                {
                    if (mapper[j & ~i] != (mapper[j] & ~mapper[i]))
                        return false;
                }
                else
                {
                    mapper[j & ~i] = (mapper[j] & ~mapper[i]);
                    i = 0;
                    break;
                }
            }
            else
            {
                uint8_t x = i ^ j;
                uint8_t y = mapper[i] ^ mapper[j];
                if (std::popcount(x) != std::popcount(y))
                    return false;
                if (mapper[x])
                {
                    if (mapper[x] != y)
                        return false;
                }
                else
                {
                    mapper[x] = y;
                    i = 0;
                    break;
                }
            }
        }
    }



    return true;
}

static void add_to_fast_ops(std::vector<GridRule::FastOp>& fast_ops, GridRule::FastOp new_op)
{
    for (GridRule::FastOp& op : fast_ops)
    {
        if (op.op != new_op.op)
            continue;
        if (op.vi != new_op.vi)
            continue;
        if (op.p1 != new_op.p1)
            continue;
        if (op.p2 != new_op.p2)
            continue;
        if (op.p3 != new_op.p3)
            continue;
        return;
    }
    fast_ops.push_back(new_op);
}

void GridRule::jit_preprocess_calc(std::vector<GridRule::FastOp>& fast_ops, bool have[32])
{
    for (int i = 1; i < 32; i++)
    {
        if (have[i-1])
        {
            for (int j = 1; j < 32; j++)
            {
                if (i == j)
                    continue;
                if ((have[j-1]) && (i != j))
                {
                    if ((i & j) == 0)
                    {
                        int vi = (i | j) - 1;
                        if (!have[vi])
                        {
                            have[vi] = true;
                            add_to_fast_ops(fast_ops, FastOp(FastOp::OpType::VAR_ADD, true, vi, i, j));
                            i = 0;
                            break;
                        }
                    }
                    else if ((i & j) == i)
                    {
                        int vi = (j & ~i) - 1;
                        if (!have[vi])
                        {
                            have[vi] = true;
                            add_to_fast_ops(fast_ops, FastOp(FastOp::OpType::VAR_SUB, true, vi, i, j));
                            i = 0;
                            break;
                        }
                    }
                    else if (have[(i ^ j) - 1]) // AB+BC+CA -> 2ABC
                    {
                        int vi = (i | j) - 1;
                        if (!have[vi])
                        {
                            have[vi] = true;
                            add_to_fast_ops(fast_ops, FastOp(FastOp::OpType::VAR_TRIPLE, true, vi, i, j, i ^ j));
                            i = 0;
                            break;
                        }
                    }
                    else // ABX+CDX-ACX -> BDX
                    {
                        for (int k = 1; k < 32; k++)
                        {
                            if (!have[k-1])
                                continue;
                            if (k == i)
                                continue;
                            if (k == j)
                                continue;
                            if ((j & i) == j)
                                continue;
                            if ((k & (i | j)) != k)
                                continue;
                            if (k == (i | j))
                                continue;
                            int both = i & j;
                            if (!both)
                                continue;
                            if ((k & both) != both)
                                continue;
                            int cov = i ^ j;
                            int vi = (k ^ cov) - 1;
                            if (!(k ^ cov))
                                continue;
                            if (!have[vi])
                            {
                                have[vi] = true;
                                add_to_fast_ops(fast_ops, FastOp(FastOp::OpType::VAR_QUAD, true, vi, i, j, k));
                                i = 0;
                                j = 32;
                                break;
                            }
                        }
                        int c = (i & j);                        // AB + AC + AD - BCD = 3A
                        if (c)
                            for (int k = 1; k < 32; k++)
                            {
                                if (!have[k-1])
                                    continue;
                                if ((k & c) != c)
                                    continue;
                                if ((k & i) != c)
                                    continue;
                                if ((k & j) != c)
                                    continue;

                                int bcd = (i | j | k) & ~c;
                                if (!have[bcd-1])
                                    continue;

                                int vi = c - 1;
                                if (!have[vi])
                                {
                                    have[vi] = true;
                                    add_to_fast_ops(fast_ops, FastOp(FastOp::OpType::VAR_BCD, true, vi, i, j, k));
                                    i = 0;
                                    j = 32;
                                    break;
                                }
                            }
                    }
                }
            }
        }
    }

}

void GridRule::jit_preprocess(FastOpGroup& fast_ops)
{
    bool have[32] = {};
    unsigned mask = get_valid_cells_mask(region_count, neg_reg_count);

    for (int r = 0; r < region_count; r++)
    {
            if (region_type[r].var)
            {
                FastOp::OpType op_type = FastOp::OpType::REG_TYPE;
                int tgt = r;
                if (if_reg_count * 2 > r)
                {
                    if (r & 1)
                        tgt--;
                    else
                        op_type = FastOp::OpType::REG_TYPE_IF;
                }
                int vi = region_type[r].var - 1;
                bool set = !have[vi];
                have[vi] = true;
                add_to_fast_ops(fast_ops.ops[tgt], FastOp(op_type, set, vi, tgt, r));
                jit_preprocess_calc(fast_ops.ops[tgt], have);
            }

    }
    for (int i = 0; i < 16; i++)
    {
        if (!((mask >> i) & 1))
            continue;
        if (square_counts[i].var)
        {
            if (square_counts[i].type == RegionType::EQUAL)
            {
                int vi = square_counts[i].var - 1;
                bool set = !have[vi];
                have[vi] = true;
                add_to_fast_ops(fast_ops.ops[region_count - 1], FastOp(FastOp::OpType::CELL_COUNT, set, vi, i));
                jit_preprocess_calc(fast_ops.ops[region_count - 1], have);
            }
        }
    }

    for (int i = 1; i < 16; i++)
    {
        if (!((mask >> i) & 1))
            continue;
        if (square_counts[i].var)
        {
            if ((square_counts[i].type != RegionType::EQUAL))
            {
                int vi = square_counts[i].var - 1;
                assert(have[vi]);
            }
        }
    }
    if (apply_region_type.var)
        assert(have[apply_region_type.var - 1]);

    // bool atoms[32] = {};
    // for (int i = 1; i < 32; i++)
    //     if (distance[i - 1])
    //         atoms[i - 1] = true;
    // for (int i = 1; i < 32; i++)
    // {
    //     for (int j = 1; j < 32; j++)
    //     {
    //         if (((i & j) == 0) && atoms[i - 1] && atoms[j - 1])
    //         {
    //             atoms[(i | j) - 1] = false;
    //         }
    //     }
    // }
    // int c = 0;
    // for (int i = 1; i < 32; i++)
    // {
    //     if (atoms[i - 1])
    //     {
    //         while (!have[i - 1]);
    //         {
    //             int at = i - 1;
    //             while(true)
    //             {
    //                 if (fops[at].p1 && !have[fops[at].p1 - 1])
    //                 {
    //                     at = fops[at].p1;
    //                     continue;
    //                 }
    //                 if (fops[at].p2 && !have[fops[at].p2 - 1])
    //                 {
    //                     at = fops[at].p2;
    //                     continue;
    //                 }
    //                 if (fops[at].p3 && !have[fops[at].p3 - 1])
    //                 {
    //                     at = fops[at].p3;
    //                     continue;
    //                 }
    //                 break;
    //             }
    //             have[at] = true;
    //             fops[at].set = true;
    //             add_to_fast_ops(fast_ops, fops[at]);
    //         }
    //     }
    // }

    // for (int i = 1; i < 32; i++)
    // {
    //     distance[i - 1] = 0;
    //     if (atoms[i - 1])
    //         distance[i - 1] = 1;
    // }
    // for (int i = 1; i < 32; i++)
    // {
    //     if (distance[i-1])
    //     {
    //         for (int j = 1; j < 32; j++)
    //         {
    //             if ((distance[j-1]) && (i != j))
    //             {
    //                 if ((i & j) == 0)
    //                 {
    //                     int vi = (i | j) - 1;
    //                     int d = std::max(distance[i - 1], distance[j - 1]) + 1;
    //                     if (d >= distance[vi])
    //                         continue;
    //                     distance[vi] = d;
    //                     fops[vi] = FastOp(FastOp::OpType::VAR_ADD, false, vi, i, j);
    //                 }
    //                 else if ((i & j) == i)
    //                 {
    //                     int vi = (j & ~i) - 1;
    //                     int d = std::max(distance[i - 1], distance[j - 1]) + 1;
    //                     if (d >= distance[vi])
    //                         continue;
    //                     distance[vi] = d;
    //                     fops[vi] = FastOp(FastOp::OpType::VAR_SUB, false, vi, i, j);
    //                     if (i > vi)
    //                     {
    //                         i = vi;
    //                         break;
    //                     }
    //                 }
    //                 else if (distance[(i ^ j) - 1])
    //                 {
    //                     int k = i ^ j;
    //                     int vi = (i | j) - 1;
    //                     int d = std::max(distance[i - 1], distance[j - 1]) + 1;
    //                     if (d >= distance[vi])
    //                         continue;
    //                     distance[vi] = d;
    //                     fops[vi] = FastOp(FastOp::OpType::VAR_TRIPLE, false, vi, i, j, k);
    //                     if (i > vi)
    //                     {
    //                         i = vi;
    //                         break;
    //                     }
    //                 }
    //             }
    //         }
    //     }
    // }
    // for (int i = 1; i < 32; i++)
    //     have[i - 1] = atoms[i - 1];

    // for (int i = 1; i < 32; i++)
    // {
    //     if (want[i - 1])
    //     {
    //         while (!have[i - 1]);
    //         {
    //             int at = i - 1;
    //             while(true)
    //             {
    //                 if (fops[at].p1 && !have[fops[at].p1 - 1])
    //                 {
    //                     at = fops[at].p1;
    //                     continue;
    //                 }
    //                 if (fops[at].p2 && !have[fops[at].p2 - 1])
    //                 {
    //                     at = fops[at].p2;
    //                     continue;
    //                 }
    //                 if (fops[at].p3 && !have[fops[at].p3 - 1])
    //                 {
    //                     at = fops[at].p3;
    //                     continue;
    //                 }
    //                 break;
    //             }
    //             fops[at].set = want[at];
    //             add_to_fast_ops(fast_ops, fops[at]);
    //             have[at] = true;
    //         }
    //     }
    // }






    // printf("%d\n", fast_ops.size());
    // for (int i = 1; i < 32; i++)
    // {
    //     if (want[i])
    //         assert(have[i]);
    // }

}

template<int i>
static int count_subregion_size_r4(const uint64_t* a, const uint64_t* b, const uint64_t* c, const uint64_t* d)
{
    int count = 0;
    for (int j = 0; j < 16; j += 1) {
        uint64_t p = (i&1) ? a[j] : ~a[j];
        p &= (i&2) ? b[j] : ~b[j];
        p &= (i&4) ? c[j] : ~c[j];
        p &= (i&8) ? d[j] : ~d[j];
        count += std::popcount(p);
    }
    return count;
}
using S4_FUNC = int(const uint64_t* a, const uint64_t* b, const uint64_t* c, const uint64_t* d);
S4_FUNC *s4_funcs[17] = {
    nullptr,
    &count_subregion_size_r4<1>, &count_subregion_size_r4<2>, &count_subregion_size_r4<3>, &count_subregion_size_r4<4>,
    &count_subregion_size_r4<5>, &count_subregion_size_r4<6>, &count_subregion_size_r4<7>, &count_subregion_size_r4<8>,
    &count_subregion_size_r4<9>, &count_subregion_size_r4<10>, &count_subregion_size_r4<11>, &count_subregion_size_r4<12>,
    &count_subregion_size_r4<13>, &count_subregion_size_r4<14>, &count_subregion_size_r4<15>, nullptr,
};

template<int i>
static int count_subregion_size_r3(const uint64_t* a, const uint64_t* b, const uint64_t* c)
{
    int count = 0;
    for (int j = 0; j < 16; j += 1) {
        uint64_t p = (i&1) ? a[j] : ~a[j];
        p &= (i&2) ? b[j] : ~b[j];
        p &= (i&4) ? c[j] : ~c[j];
        count += std::popcount(p);
    }
    return count;
}
using S3_FUNC = int(const uint64_t* a, const uint64_t* b, const uint64_t* c);
S3_FUNC *s3_funcs[17] = {
    nullptr,
    &count_subregion_size_r3<1>, &count_subregion_size_r3<2>, &count_subregion_size_r3<3>, &count_subregion_size_r3<4>,
    &count_subregion_size_r3<5>, &count_subregion_size_r3<6>, &count_subregion_size_r3<7>, &count_subregion_size_r3<8>,
    &count_subregion_size_r3<9>, &count_subregion_size_r3<10>, &count_subregion_size_r3<11>, &count_subregion_size_r3<12>,
    &count_subregion_size_r3<13>, &count_subregion_size_r3<14>, &count_subregion_size_r3<15>, nullptr,
};

template<int i>
static int count_subregion_size_r2(const uint64_t* a, const uint64_t* b)
{
    int count = 0;
    for (int j = 0; j < 16; j += 1) {
        uint64_t p = (i&1) ? a[j] : ~a[j];
        p &= (i&2) ? b[j] : ~b[j];
        count += std::popcount(p);
    }
    return count;
}
using S2_FUNC = int(const uint64_t* a, const uint64_t* b);
S2_FUNC *s2_funcs[17] = {
    nullptr,
    &count_subregion_size_r2<1>, &count_subregion_size_r2<2>, &count_subregion_size_r2<3>, &count_subregion_size_r2<4>,
    &count_subregion_size_r2<5>, &count_subregion_size_r2<6>, &count_subregion_size_r2<7>, &count_subregion_size_r2<8>,
    &count_subregion_size_r2<9>, &count_subregion_size_r2<10>, &count_subregion_size_r2<11>, &count_subregion_size_r2<12>,
    &count_subregion_size_r2<13>, &count_subregion_size_r2<14>, &count_subregion_size_r2<15>, nullptr,
};
template<int i>
static int count_subregion_size_r1(const uint64_t* a)
{
    int count = 0;
    for (int j = 0; j < 16; j += 1) {
        uint64_t p = (i&1) ? a[j] : ~a[j];
        count += std::popcount(p);
    }
    return count;
}
using S1_FUNC = int(const uint64_t* a);
S1_FUNC *s1_funcs[17] = {
    nullptr,
    &count_subregion_size_r1<1>, &count_subregion_size_r1<2>, &count_subregion_size_r1<3>, &count_subregion_size_r1<4>,
    &count_subregion_size_r1<5>, &count_subregion_size_r1<6>, &count_subregion_size_r1<7>, &count_subregion_size_r1<8>,
    &count_subregion_size_r1<9>, &count_subregion_size_r1<10>, &count_subregion_size_r1<11>, &count_subregion_size_r1<12>,
    &count_subregion_size_r1<13>, &count_subregion_size_r1<14>, &count_subregion_size_r1<15>, nullptr,
};

static int count_subregion_size(int i, GridRegion* r1, GridRegion* r2, GridRegion* r3, GridRegion* r4)
{
    bool neg = r1->elements_neg.any() && (i & 1);
    bool neg2 = r2 && r2->elements_neg.any();

    const uint64_t *a = reinterpret_cast<const uint64_t*>(&r1->elements);
    const uint64_t *a_neg = reinterpret_cast<const uint64_t*>(&r1->elements_neg);
    const uint64_t *b = reinterpret_cast<const uint64_t*>(&r2->elements);
    const uint64_t *b_neg = reinterpret_cast<const uint64_t*>(&r2->elements_neg);
    const uint64_t *c = reinterpret_cast<const uint64_t*>(&r3->elements);
    const uint64_t *d = reinterpret_cast<const uint64_t*>(&r4->elements);
    if (r1->is_if_then())
    {
        a = reinterpret_cast<const uint64_t*>(&r1->elements_neg);
        b = reinterpret_cast<const uint64_t*>(&r1->elements);
        r2 = r1;
        neg = false;
        neg2 = false;
        if (r3 && r3->is_if_then())
        {
            c = reinterpret_cast<const uint64_t*>(&r3->elements_neg);
            d = reinterpret_cast<const uint64_t*>(&r3->elements);
            r4 = r3;
        }
    }

    if (r4) {
        return s4_funcs[i](a, b, c, d);
    } else if (r3) {
        if (neg)
            return s4_funcs[i](a, b, c, a_neg);
        return s3_funcs[i](a, b, c);
    } else if (r2) {
        if (neg2)
        {
            if ((i & 3) == 3)
                return s4_funcs[i](a, b, a_neg, b_neg);
            if ((i & 3) == 2)
                return s3_funcs[2 | ((i&8) >> 1)](a, b, b_neg);
            if ((i & 3) == 1)
                return s3_funcs[i](a, b, a_neg);
        }
        if (neg)
            return s3_funcs[(i&3) | ((i&8) >> 1)](a, b, a_neg);
        return s2_funcs[i](a, b);
    } else { // r1 is supposed to be always non-null
        if (neg)
            return s2_funcs[(i&1) | ((i&8) >> 2)](a, a_neg);
        return s1_funcs[i](a);
    }
}

bool GridRule::jit_matches(std::vector<GridRule::FastOp>& fast_ops, bool final, GridRegion* r1, GridRegion* r2, GridRegion* r3, GridRegion* r4, int var_counts[32])
{
    GridRegion* grid_regions[4] = {r1, r2, r3, r4};

    for (FastOp& op : fast_ops)
    {
        int v = -1;

        if (op.op == FastOp::VAR_ADD) {
            v = var_counts[op.p1 - 1] + var_counts[op.p2 - 1];
        } else if (op.op == FastOp::VAR_SUB) {
            v = var_counts[op.p2 - 1] - var_counts[op.p1 - 1];
        } else if (op.op == FastOp::REG_TYPE) {
            v = grid_regions[op.p1]->type.value - region_type[op.p2].value;
        } else if (op.op == FastOp::REG_TYPE_IF) {
            v = grid_regions[op.p1]->if_type.value - region_type[op.p2].value;
        } else if (op.op == FastOp::CELL_COUNT) {
            int i = op.p1;
            int count = count_subregion_size(i, r1, r2, r3, r4);
            v = count - square_counts[i].value;
        } else if (op.op == FastOp::VAR_TRIPLE) {
            int x = op.p1 ^ op.p2;
            v = var_counts[op.p1 - 1] + var_counts[op.p2 - 1] + var_counts[x - 1];
//            assert(!(v & 1));
            if (v % 2)
                return false;
            v /= 2;
        } else if (op.op == FastOp::VAR_QUAD) {
            v = var_counts[op.p1 - 1] + var_counts[op.p2 - 1] - var_counts[op.p3 - 1];
//            assert(!(v & 1));
        } else if (op.op == FastOp::VAR_BCD) {
            v = var_counts[op.p1 - 1] + var_counts[op.p2 - 1] + var_counts[op.p3 - 1];
            v -= var_counts[((op.p1 | op.p2 | op.p3) & ~(op.vi + 1)) - 1];
            if (v % 3)
                return false;
            v /= 3;
//            assert(!(v & 1));
        } else {
            assert(0);
        }
        if (v < 0)
            return false;
        if (op.set)
        {
//            assert (var_counts[op.vi] == v);
            var_counts[op.vi] = v;
        }
        else
        {
            if (var_counts[op.vi] != v)
                return false;
        }
    }
    if (final)
    {
        unsigned mask = get_valid_cells_mask(region_count, neg_reg_count);
        for (int i = 1; i < 16; i++)
        {
            if (!((mask >> i) & 1))
                continue;
            if (square_counts[i].type == RegionType::NONE)
                continue;
            int count = count_subregion_size(i, r1, r2, r3, r4);
            if (!square_counts[i].apply_int_rule(count, var_counts))
                return false;
        }
    }
    return true;
}


// bool GridRule::matches(GridRegion* r1, GridRegion* r2, GridRegion* r3, GridRegion* r4, int var_counts[32])
// {
//     GridRegion* grid_regions[4] = {r1, r2, r3, r4};

//     for (int i = 0; i < region_count; i++)
//     {
//         if (region_type[i].type != RegionType::NONE)
//             assert(region_type[i].type == grid_regions[i]->type.type);
//         if (region_type[i].var)
//         {
//             int vi = region_type[i].var - 1;
//             if (var_counts[vi] < 0)
//             {
//                 int v = grid_regions[i]->type.value - region_type[i].value;
//                 if (v < 0)
//                     return false;
//                 var_counts[vi] = v;
//             }
//             else
//             {
//                 if (var_counts[vi] != (grid_regions[i]->type.value - region_type[i].value))
//                     return false;
//             }
//         }
//     }

//     for (int i = 1; i < (1 << region_count); i++)
//     {
//         if ((square_counts[i].type == RegionType::EQUAL) && square_counts[i].var)
//         {
//             int count = count_subregion_size(i, r1, r2, r3, r4);

//             int vi = square_counts[i].var - 1;
//             int v = count - square_counts[i].value;
//             if (v < 0)
//                 return false;
//             if (var_counts[vi] < 0)
//                 var_counts[vi] = v;
//         }
//     }
//     for (int i = 1; i < 32; i++)
//     {
//         if (var_counts[i-1] >= 0)
//         {
//             for (int j = 1; j < 32; j++)
//             {
//                 if ((var_counts[j-1] >= 0) && (i != j))
//                 {
//                     if ((i & j) == 0)
//                     {
//                         if (var_counts[(i | j) - 1] >= 0)
//                         {
//                             if (var_counts[(i | j) - 1] != var_counts[i - 1] + var_counts[j - 1])
//                                 return false;
//                         }
//                         else
//                             var_counts[(i | j) - 1] = var_counts[i - 1] + var_counts[j - 1];
//                     }
//                     else if ((i & j) == i)
//                     {
//                         if (var_counts[(j & ~i) - 1] >= 0)
//                         {
//                             if (var_counts[(j & ~i) - 1] != var_counts[j - 1] - var_counts[i - 1])
//                                 return false;
//                         }
//                         else
//                         {
//                             int v = var_counts[j - 1] - var_counts[i - 1];
//                             if (v < 0)
//                                 return false;
//                             var_counts[(j & ~i) - 1] = v;
//                             if (i > ((j & ~i) - 1))
//                             {
//                                 i = (j & ~i) - 1;
//                                 break;
//                             }
//                         }
//                     }
//                     else
//                     {
//                         int x = i ^ j;
//                         if (var_counts[x - 1] >= 0)
//                         {
//                             int v = var_counts[i - 1] + var_counts[j - 1] + var_counts[x - 1];
//                             if (v % 2)
//                                 return false;
//                             v /= 2;
//                             if (var_counts[(j | i) - 1] >= 0)
//                             {
//                                 if (var_counts[(j | i) - 1] != v)
//                                     return false;
//                             }
//                             else
//                                 var_counts[(j | i) - 1] = v;
//                         }

//                     }
//                 }
//             }
//         }
//     }

//     // if (var_counts[2] < 0 && var_counts[0] >= 0 && var_counts[1] >= 0)
//     // {
//     //     var_counts[2] = var_counts[0] + var_counts[1];
//     // }
//     // if (var_counts[0] < 0 && var_counts[1] >= 0 && var_counts[2] >= 0)
//     // {
//     //     var_counts[0] = var_counts[2] - var_counts[1];
//     //     if (var_counts[0] < 0)
//     //         return false;
//     // }
//     // if (var_counts[1] < 0 && var_counts[0] >= 0 && var_counts[2] >= 0)
//     // {
//     //     var_counts[1] = var_counts[2] - var_counts[0];
//     //     if (var_counts[1] < 0)
//     //         return false;
//     // }

//     // if (var_counts[0] >= 0 && var_counts[1] >= 0 && var_counts[2] >= 0)
//     // {
//     //     if (var_counts[2] != var_counts[0] + var_counts[1])
//     //         return false;
//     // }

//     for (int i = 1; i < (1 << region_count); i++)
//     {
//         if (square_counts[i].type == RegionType::NONE)
//             continue;
//         int count = count_subregion_size(i, r1, r2, r3, r4);
//         if (!square_counts[i].apply_int_rule(count, var_counts))
//             return false;
//     }
//     return true;
// }

void GridRule::import_rule_gen_regions(GridRegion* r1, GridRegion* r2, GridRegion* r3, GridRegion* r4)
{
    region_count = 0;
    neg_reg_count = 0;
    if_reg_count = 0;
    apply_region_bitmap = 0;
    neg_apply_region_bitmap = 0;
    if (r1)
    {
        if (r1->is_if_then())
        {
            if_reg_count++;
            region_type[region_count++] = r1->if_type;
        }
        else if (r1->elements_neg.any())
            neg_reg_count++;
        region_type[region_count++] = r1->type;
    }
    if (r2)
    {
        if (r2->is_if_then())
        {
            if_reg_count++;
            region_type[region_count++] = r2->if_type;
        }
        else if (r2->elements_neg.any())
            neg_reg_count++;
        region_type[region_count++] = r2->type;
    }
    if (r3)
    {
        assert (!r3->elements_neg.any());
        assert(neg_reg_count < 2);
        region_type[region_count++] = r3->type;
    }
    if (r4)
    {
        assert (!r4->elements_neg.any());
        assert(neg_reg_count < 1);
        region_type[region_count++] = r4->type;
    }

    uint8_t sqc[16];
    get_square_counts(sqc, r1, r2, r3, r4);

    unsigned mask = get_valid_cells_mask(region_count, neg_reg_count);
    for (int i = 0; i < 16; i++)
    {
        if ((mask >> i) & 1)
            square_counts[i] = RegionType(RegionType::EQUAL, sqc[i]);
    }
}

GridRule::IsLogicalRep GridRule::is_legal(GridRule& why, int vars[5])
{
    if (!has_valid_structure())
        return IMPOSSIBLE;
    if (apply_if_region_type.type != RegionType::NONE &&
        (apply_if_region_type.type >= RegionType::SET ||
         apply_region_type.type == RegionType::NONE ||
         apply_region_type.type >= RegionType::SET ||
         !neg_apply_region_bitmap))
        return IMPOSSIBLE;
    if (apply_region_type.type >= RegionType::SET &&
        neg_apply_region_bitmap)
        return IMPOSSIBLE;

    z3::context c;
    z3::solver s(c);

    z3::expr_vector vec(c);
    z3::expr_vector neg_vec(c);
    z3::expr_vector var_vec(c);
    why = *this;

    bool loses_data = false;

    if (region_count == 0)
        return IMPOSSIBLE;
    if (!apply_region_bitmap)
        return USELESS;

    if(region_count > 1)
        for (int r = 0; r < region_count; r++)
        {
            bool lap = false;
            unsigned mask = get_valid_cells_mask(region_count, neg_reg_count);
            unsigned r_mask = 1 << r;
            if (r == 0 && if_reg_count)
            {
                r_mask |= 2;
                r++;
                if (region_count == 2)
                    lap = true;
            }
            if (r == 2 && if_reg_count >= 2)
            {
                r_mask |= 8;
                r++;
            }
            for (int i = 0; i < 16; i++)
            {
                if (!((mask >> i) & 1))
                    continue;
                if (!(i & r_mask))
                    continue;
                if (i == (i & r_mask))
                    continue;
                if (square_counts[i].max())
                    lap = true;
                if (square_counts[i].var)
                    lap = true;
            }
            if (!lap)
                return IMPOSSIBLE;
        }

    for (int r = 0; r < region_count; r++)
    {
        bool ots = false;
        if (region_type[r] != apply_region_type)
            continue;
        if (if_reg_count * 2 > r)
            continue;

        unsigned mask = get_valid_cells_mask(region_count, neg_reg_count);
        for (int i = 0; i < 16; i++)
        {
            if ((mask >> i) & 1)
            {
                if ((i >> r) & 1)
                {
                    if (!((apply_region_bitmap >> i) & 1))
                    {
                        if (square_counts[i].max() || square_counts[i].var)
                            ots = true;
                    }
                    else
                    {
                        bool neg = false;
                        if (neg_reg_count == 1 && r == 0)
                            neg = i & 8;
                        if (neg_reg_count == 2 && r == 0)
                            neg = i & 4;
                        if (neg_reg_count == 2 && r == 1)
                            neg = i & 8;
                        bool rneg = (neg_apply_region_bitmap >> i) & 1;
                        if (neg != rneg)
                            ots = true;
                    }
                }
                else
                {
                    if (((apply_region_bitmap >> i) & 1) &&
                        (square_counts[i].max() || square_counts[i].var))
                        ots = true;
                }
            }
        }
        if (!ots)
            return USELESS;
    }

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

    z3::func_decl_vector wildcard_region(c);
    for (int i = 0; i < 4; i++)
    {
        const std::string suffix =
            std::to_string(i);
        wildcard_region.push_back(c.function(
            ("W_REGION" + suffix).c_str(),
            c.int_sort(), c.bool_sort()));
    }

    // A wildcard endpoint matches any
    // concrete predicate. Reusing the same
    // function keeps equal counts equal.
    auto apply_z3_region_type = [&] (
        int region,
        const z3::expr& count)
    {
        if (region_type[region].type ==
            RegionType::NONE)
            return wildcard_region[region](count);
        return region_type[region].apply_z3_rule(
            count, var_vec);
    };

    auto apply_z3_if_then = [&] (
        int if_region,
        const z3::expr& if_count,
        int then_region,
        const z3::expr& then_count)
    {
        return z3::implies(
            apply_z3_region_type(
                if_region, if_count),
            apply_z3_region_type(
                then_region, then_count));
    };

    vec = make_count_vector(c, s, var_vec, square_counts, "A");

    if (neg_reg_count == 0)
    {
        if (region_count == 1)
        {
            s.add(region_type[0].apply_z3_rule(vec[1], var_vec));
        }
        if (region_count == 2)
        {
            if (if_reg_count)
            {
                s.add(apply_z3_if_then(
                    0, vec[1] + vec[3],
                    1, vec[2] + vec[3]));
            }
            else
            {
                s.add(region_type[0].apply_z3_rule(vec[1] + vec[3], var_vec));
                s.add(region_type[1].apply_z3_rule(vec[2] + vec[3], var_vec));
            }
        }
        if (region_count == 3)
        {
            if (if_reg_count)
            {
                s.add(apply_z3_if_then(
                    0,
                    vec[1] + vec[3] + vec[5] + vec[7],
                    1,
                    vec[2] + vec[3] + vec[6] + vec[7]));
            }
            else
            {
                s.add(region_type[0].apply_z3_rule(vec[1] + vec[3] + vec[5] + vec[7], var_vec));
                s.add(region_type[1].apply_z3_rule(vec[2] + vec[3] + vec[6] + vec[7], var_vec));
            }
            s.add(region_type[2].apply_z3_rule(vec[4] + vec[5] + vec[6] + vec[7], var_vec));
        }
        if (region_count == 4)
        {
            if (if_reg_count)
            {
                s.add(apply_z3_if_then(
                    0,
                    vec[1] + vec[3] + vec[5] + vec[7] + vec[9] + vec[11] + vec[13] + vec[15],
                    1,
                    vec[2] + vec[3] + vec[6] + vec[7] + vec[10] + vec[11] + vec[14] + vec[15]));
            }
            else
            {
                s.add(region_type[0].apply_z3_rule(vec[1] + vec[3] + vec[5] + vec[7] + vec[9] + vec[11] + vec[13] + vec[15], var_vec));
                s.add(region_type[1].apply_z3_rule(vec[2] + vec[3] + vec[6] + vec[7] + vec[10] + vec[11] + vec[14] + vec[15], var_vec));
            }
            if (if_reg_count >= 2)
            {
                s.add(apply_z3_if_then(
                    2,
                    vec[4] + vec[5] + vec[6] + vec[7] + vec[12] + vec[13] + vec[14] + vec[15],
                    3,
                    vec[8] + vec[9] + vec[10] + vec[11] + vec[12] + vec[13] + vec[14] + vec[15]));
            }
            else
            {
                s.add(region_type[2].apply_z3_rule(vec[4] + vec[5] + vec[6] + vec[7] + vec[12] + vec[13] + vec[14] + vec[15], var_vec));
                s.add(region_type[3].apply_z3_rule(vec[8] + vec[9] + vec[10] + vec[11] + vec[12] + vec[13] + vec[14] + vec[15], var_vec));
            }
        }
    }
    else if (neg_reg_count == 1)
    {
        if (region_count == 1)
        {
            s.add(region_type[0].apply_z3_rule(vec[1] - vec[9], var_vec));
        }
        if (region_count == 2)
        {
            s.add(region_type[0].apply_z3_rule(vec[1] + vec[3] - vec[9] - vec[11], var_vec));
            s.add(region_type[1].apply_z3_rule(vec[2] + vec[3] + vec[11], var_vec));
        }
        if (region_count == 3)
        {
            s.add(region_type[0].apply_z3_rule(vec[1] + vec[3] + vec[5] + vec[7] - vec[9] - vec[11] - vec[13] - vec[15], var_vec));
            s.add(region_type[1].apply_z3_rule(vec[2] + vec[3] + vec[6] + vec[7] + vec[11] + vec[15], var_vec));
            s.add(region_type[2].apply_z3_rule(vec[4] + vec[5] + vec[6] + vec[7] + vec[13] + vec[15], var_vec));
        }
    }
    else if (neg_reg_count == 2)
    {
        assert (region_count == 2);
        {
            s.add(region_type[0].apply_z3_rule(vec[1] + vec[3] + vec[11] - vec[5] - vec[7] - vec[15], var_vec));
            s.add(region_type[1].apply_z3_rule(vec[2] + vec[3] + vec[7] - vec[10] - vec[11] - vec[15], var_vec));
        }
    }

    if (apply_region_type.type != RegionType::VISIBILITY)
    {
        bool has = false;
        uint32_t vars = 0;
        for (int i = 1; i < 16; i++)
            if ((apply_region_bitmap >> i) & 1)
            {
                if(square_counts[i].max())
                    has = true;
                vars |= uint32_t(1) << square_counts[i].var;
            }
        if (!has)
        {
            z3::expr cnt = c.int_val(0);
            for (int i = 1; i < 32; i++)
                if ((vars >> i) & 1)
                    cnt = cnt + var_vec[i - 1];
            s.add(cnt > 0);
        }
    }

    if (s.check() != z3::sat)
    {
        return IMPOSSIBLE;
    }

    if (apply_region_type.type == RegionType::VISIBILITY)
    {
        uint32_t vars_want = 0;
        z3::expr_vector vec2(c);

        vec2 = make_count_vector(c, s, var_vec, square_counts, "B");

        unsigned vis_apply_inv = apply_region_bitmap;
        z3::expr t = c.bool_val(false);
        if (region_count == 1)
        {
            assert(!if_reg_count);
            if (region_type[0].type != RegionType::NONE)
            {
                if (neg_reg_count == 0)
                {
                    if (vis_apply_inv & 1)
                        t = t | !region_type[0].apply_z3_rule(vec2[1], var_vec);
                    else
                        s.add(region_type[0].apply_z3_rule(vec2[1], var_vec));
                }
                else
                {
                    if (vis_apply_inv & 1)
                        t = t | !region_type[0].apply_z3_rule(vec2[1] - vec2[9], var_vec);
                    else
                        s.add(region_type[0].apply_z3_rule(vec2[1] - vec2[9], var_vec));
                }
            }
        }
        if (region_count == 2)
        {
            if (if_reg_count)
            {
                z3::expr implication = apply_z3_if_then(
                    0,
                    vec2[1] + vec2[3],
                    1,
                    vec2[2] + vec2[3]);
                if (vis_apply_inv & 1)
                    t = t | !implication;
                else
                    s.add(implication);
            }
            else if (region_type[0].type != RegionType::NONE)
            {
                if (neg_reg_count == 0)
                {
                    if (vis_apply_inv & 1)
                        t = t | !region_type[0].apply_z3_rule(vec2[1] + vec2[3], var_vec);
                    else
                        s.add(region_type[0].apply_z3_rule(vec2[1] + vec2[3], var_vec));
                }
                else if(neg_reg_count == 1)
                {
                    if (vis_apply_inv & 1)
                        t = t | !region_type[0].apply_z3_rule(vec2[1] - vec2[9] + vec2[3] - vec2[11], var_vec);
                    else
                        s.add(region_type[0].apply_z3_rule(vec2[1] - vec2[9] + vec2[3] - vec2[11], var_vec));
                }
                else if(neg_reg_count == 2)
                {
                    if (vis_apply_inv & 1)
                        t = t | !region_type[0].apply_z3_rule(vec2[1] - vec2[5] + vec2[3] - vec2[7] + vec2[11] - vec2[15], var_vec);
                    else
                        s.add(region_type[0].apply_z3_rule(vec2[1] - vec2[5] + vec2[3] - vec2[7] + vec2[11] - vec2[15], var_vec));
                }
            }
            if (region_type[1].type != RegionType::NONE)
            {
                if (if_reg_count)
                {}
                else if (neg_reg_count == 0)
                {
                    if (vis_apply_inv & 2)
                        t = t | !region_type[1].apply_z3_rule(vec2[2] + vec2[3], var_vec);
                    else
                        s.add(region_type[1].apply_z3_rule(vec2[2] + vec2[3], var_vec));
                }
                else if(neg_reg_count == 1)
                {
                    if (vis_apply_inv & 2)
                        t = t | !region_type[1].apply_z3_rule(vec2[2] + vec2[3] + vec2[11], var_vec);
                    else
                        s.add(region_type[1].apply_z3_rule(vec2[2] + vec2[3] + vec2[11], var_vec));
                }
                else if(neg_reg_count == 2)
                {
                    if (vis_apply_inv & 2)
                        t = t | !region_type[1].apply_z3_rule(vec2[2] + vec2[3] + vec2[7] - vec2[10] - vec2[11] - vec2[15], var_vec);
                    else
                        s.add(region_type[1].apply_z3_rule(vec2[2] + vec2[3] + vec2[7] - vec2[10] - vec2[11] - vec2[15], var_vec));
                }
            }
        }
        if (region_count == 3)
        {
            if (if_reg_count)
            {
                z3::expr implication = apply_z3_if_then(
                    0,
                    vec2[1] + vec2[3] + vec2[5] + vec2[7],
                    1,
                    vec2[2] + vec2[3] + vec2[6] + vec2[7]);
                if (vis_apply_inv & 1)
                    t = t | !implication;
                else
                    s.add(implication);
            }
            else if (region_type[0].type != RegionType::NONE)
            {
                if (neg_reg_count == 0)
                {
                    if (vis_apply_inv & 1)
                        t = t | !region_type[0].apply_z3_rule(vec2[1] + vec2[3] + vec2[5] + vec2[7], var_vec);
                    else
                        s.add(region_type[0].apply_z3_rule(vec2[1] + vec2[3] + vec2[5] + vec2[7], var_vec));
                }
                else if(neg_reg_count == 1)
                {
                    if (vis_apply_inv & 1)
                        t = t | !region_type[0].apply_z3_rule(vec2[1] + vec2[3] + vec2[5] + vec2[7] - vec2[9] - vec2[11] - vec2[13] - vec2[15], var_vec);
                    else
                        s.add(region_type[0].apply_z3_rule(vec2[1] + vec2[3] + vec2[5] + vec2[7] - vec2[9] - vec2[11] - vec2[13] - vec2[15], var_vec));
                }
            }
            if (region_type[1].type != RegionType::NONE)
            {
                if (if_reg_count)
                {}
                else if (neg_reg_count == 0)
                {
                    if (vis_apply_inv & 2)
                        t = t | !region_type[1].apply_z3_rule(vec2[2] + vec2[3] + vec2[6] + vec2[7], var_vec);
                    else
                        s.add(region_type[1].apply_z3_rule(vec2[2] + vec2[3] + vec2[6] + vec2[7], var_vec));
                }
                else if(neg_reg_count == 1)
                {
                    if (vis_apply_inv & 2)
                        t = t | !region_type[1].apply_z3_rule(vec2[2] + vec2[3] + vec2[6] + vec2[7] + vec2[11] + vec2[15], var_vec);
                    else
                        s.add(region_type[1].apply_z3_rule(vec2[2] + vec2[3] + vec2[6] + vec2[7] + vec2[11] + vec2[15], var_vec));
                }
            }
            
            if (region_type[2].type != RegionType::NONE)
            {
                if (neg_reg_count == 0)
                {
                    if (vis_apply_inv & 4)
                        t = t | !region_type[2].apply_z3_rule(vec2[4] + vec2[5] + vec2[6] + vec2[7], var_vec);
                    else
                        s.add(region_type[2].apply_z3_rule(vec2[4] + vec2[5] + vec2[6] + vec2[7], var_vec));
                }
                else if(neg_reg_count == 1)
                {
                    if (vis_apply_inv & 4)
                        t = t | !region_type[2].apply_z3_rule(vec2[4] + vec2[5] + vec2[6] + vec2[7] + vec2[13] + vec2[15], var_vec);
                    else
                        s.add(region_type[2].apply_z3_rule(vec2[4] + vec2[5] + vec2[6] + vec2[7] + vec2[13] + vec2[15], var_vec));
                }
            }
        }
        if (region_count == 4)
        {
            if (if_reg_count)
            {
                z3::expr implication = apply_z3_if_then(
                    0,
                    vec2[1] + vec2[3] + vec2[5] + vec2[7] + vec2[9] + vec2[11] + vec2[13] + vec2[15],
                    1,
                    vec2[2] + vec2[3] + vec2[6] + vec2[7] + vec2[10] + vec2[11] + vec2[14] + vec2[15]);
                if (vis_apply_inv & 1)
                    t = t | !implication;
                else
                    s.add(implication);
            }
            else
            {
                if (region_type[0].type != RegionType::NONE)
                {
                    if (vis_apply_inv & 1)
                        t = t | !region_type[0].apply_z3_rule(vec2[1] + vec2[3] + vec2[5] + vec2[7] + vec2[9] + vec2[11] + vec2[13] + vec2[15], var_vec);
                    else
                        s.add(region_type[0].apply_z3_rule(vec2[1] + vec2[3] + vec2[5] + vec2[7] + vec2[9] + vec2[11] + vec2[13] + vec2[15], var_vec));
                }
                if (region_type[1].type != RegionType::NONE)
                {
                    if (vis_apply_inv & 2)
                        t = t | !region_type[1].apply_z3_rule(vec2[2] + vec2[3] + vec2[6] + vec2[7] + vec2[10] + vec2[11] + vec2[14] + vec2[15], var_vec);
                    else
                        s.add(region_type[1].apply_z3_rule(vec2[2] + vec2[3] + vec2[6] + vec2[7] + vec2[10] + vec2[11] + vec2[14] + vec2[15], var_vec));
                }
            }
            if (if_reg_count >= 2)
            {
                z3::expr implication = apply_z3_if_then(
                    2,
                    vec2[4] + vec2[5] + vec2[6] + vec2[7] + vec2[12] + vec2[13] + vec2[14] + vec2[15],
                    3,
                    vec2[8] + vec2[9] + vec2[10] + vec2[11] + vec2[12] + vec2[13] + vec2[14] + vec2[15]);
                if (vis_apply_inv & 4)
                    t = t | !implication;
                else
                    s.add(implication);
            }
            else
            {
                if (region_type[2].type != RegionType::NONE)
                {
                    if (vis_apply_inv & 4)
                        t = t | !region_type[2].apply_z3_rule(vec2[4] + vec2[5] + vec2[6] + vec2[7] + vec2[12] + vec2[13] + vec2[14] + vec2[15], var_vec);
                    else
                        s.add(region_type[2].apply_z3_rule(vec2[4] + vec2[5] + vec2[6] + vec2[7] + vec2[12] + vec2[13] + vec2[14] + vec2[15], var_vec));
                }
                if (region_type[3].type != RegionType::NONE)
                {
                    if (vis_apply_inv & 8)
                        t = t | !region_type[3].apply_z3_rule(vec2[8] + vec2[9] + vec2[10] + vec2[11] + vec2[12] + vec2[13] + vec2[14] + vec2[15], var_vec);
                    else
                        s.add(region_type[3].apply_z3_rule(vec2[8] + vec2[9] + vec2[10] + vec2[11] + vec2[12] + vec2[13] + vec2[14] + vec2[15], var_vec));
                }
            }
        }

        uint8_t hiding_dontcare = 0;
        // Conditional wildcards are already
        // represented by their implication.
        // This fallback is only for ordinary
        // wildcard regions.
        for (int i = if_reg_count * 2;
             i < region_count; i++)
            if ((region_type[i].type ==
                 RegionType::NONE) &&
                ((vis_apply_inv >> i) & 1))
                hiding_dontcare |= 1 << i;
        if (hiding_dontcare)
        {
            unsigned mask = get_valid_cells_mask(region_count, neg_reg_count);
            for (int i = 0; i < 16; i++)
            {
                if (!((mask >> i) & 1))
                    continue;
                if (i & hiding_dontcare)
                {
                    t = t | (vec[i] != vec2[i]);
                }
            }
        }

        s.add(t);
        if (s.check() == z3::sat)
        {
            z3::model m = s.get_model();
            unsigned mask = get_valid_cells_mask(region_count, neg_reg_count);
            for (int i = 0; i < 16; i++)
            {
                if (!((mask >> i) & 1))
                    continue;
                vars_want |= why.square_counts[i].var;
                int v = m.eval(vec2[i]).get_numeral_int();
                why.square_counts[i] = RegionType(RegionType::EQUAL, v);
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
                    vars_want |= why.region_type[i].var;
                    why.region_type[i].value += vals[why.region_type[i].var - 1];
                    why.region_type[i].var = 0;
                }
            }
            if (why.apply_region_type.var)
            {
                why.apply_region_type.value += vals[why.apply_region_type.var - 1];
                why.apply_region_type.var = 0;
            }
            for (int i = 0; i < 5; i++)
                vars[i] = ((vars_want >> i) & 1) ? vals[(1 << i) - 1] : -1;
            loses_data = true;
        }
    }
    else
    {
        z3::expr e = c.int_val(0);
        z3::expr e_if = c.int_val(0);
        z3::expr tot = c.int_val(0);
        bool if_then = (apply_if_region_type.type != RegionType::NONE);

        assert (apply_region_bitmap);

        for (int i = 1; i < 16; i++)
        {
            if (if_then && ((neg_apply_region_bitmap >> i) & 1))
                e_if = e_if + vec[i];

            if ((apply_region_bitmap >> i) & 1)
            {
                if (!if_then && (neg_apply_region_bitmap >> i) & 1)
                    e = e - vec[i];
                else
                    e = e + vec[i];

                int m = square_counts[i].max();
                if ((m < 0) && (apply_region_type == RegionType(RegionType::SET, 1)))
                {
                    for (int i = 1; i < 16; i++)
                        why.square_counts[i] = RegionType(RegionType::NONE, 0);

                    why.square_counts[i] = RegionType(RegionType::MORE, 0);
                    for (int i = 0; i < 5; i++)
                        vars[i] = -1;
                    return ILLOGICAL;
                }
                if (square_counts[i].var)
                    tot = tot + var_vec[square_counts[i].var - 1] + m;
                else
                    tot = tot + m;
            }
        }

        if (if_then)
        {
            s.add(!z3::implies(apply_if_region_type.apply_z3_rule(e_if, var_vec), apply_region_type.apply_z3_rule(e, var_vec)));
        }
        else if (apply_region_type.type == RegionType::SET)
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
            uint32_t vars_want = 0;
            z3::model m = s.get_model();
            for (int i = 1; i < 16; i++)
            {
                vars_want |= why.square_counts[i].var;
                int v = m.eval(vec[i]).get_numeral_int();
                why.square_counts[i] = RegionType(RegionType::EQUAL, v);
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
                    vars_want |= why.region_type[i].var;
                    why.region_type[i].value += vals[why.region_type[i].var - 1];
                    why.region_type[i].var = 0;
                }
            }
            if (why.apply_region_type.var)
            {
                vars_want |= why.apply_region_type.var;
                why.apply_region_type.value += vals[why.apply_region_type.var - 1];
                why.apply_region_type.var = 0;
            }
            for (int i = 0; i < 5; i++)
                vars[i] = ((vars_want >> i) & 1) ? vals[(1 << i) - 1] : -1;
            return ILLOGICAL;
        }
    }

    {
        // if (!apply_region_type.var)
        //     return OK;
        uint64_t seen = 0;
        uint64_t want = 0;
        for (int i = 0; i < region_count; i++)
            seen |= uint64_t(1) << region_type[i].var;
        unsigned mask = get_valid_cells_mask(region_count, neg_reg_count);
        for (int i = 1; i < 16; i++)
        {
            if (!((mask >> i) & 1))
                continue;
            if (square_counts[i].type == RegionType::EQUAL)
                seen |= uint64_t(1) << square_counts[i].var;
            else
                want |= uint64_t(1) << square_counts[i].var;
        }
        if (apply_region_type.type != RegionType::VISIBILITY)
            want |= uint64_t(1) << apply_region_type.var;
        if (apply_if_region_type.type != RegionType::NONE)
            want |= uint64_t(1) << apply_if_region_type.var;
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
                            {
                                int x = i | j;
                                if (!((seen >> x) & 1))
                                {
                                    seen |= uint64_t(1) << x;
                                    i = 0;
                                    break;
                                }
                            }
                            else if ((i & j) == i)
                            {
                                if (((seen >> (j & ~i)) & 1) == 0)
                                {
                                    seen |= uint64_t(1) << (j & ~i);
                                    if (i > (j & ~i) - 1)
                                        i = 0;
                                    break;
                                }
                            }
                            else if ((seen >> (i ^ j) & 1))
                            {
                                int x = i | j;
                                if (!((seen >> x) & 1))
                                {
                                    seen |= uint64_t(1) << x;
                                    i = 0;
                                    break;
                                }
                            }
                            else
                            {
                                for (int k = 1; k < 32; k++)    // ABX+CDX-ACX -> BDX
                                {
                                    if (!((seen >> k) & 1))
                                        continue;
                                    if (k == i)
                                        continue;
                                    if (k == j)
                                        continue;
                                    if ((j & i) == j)
                                        continue;
                                    if ((k & (i | j)) != k)
                                        continue;
                                    if (k == (i | j))
                                        continue;
                                    int both = i & j;
                                    if (!both)
                                        continue;
                                    if ((k & both) != both)
                                        continue;
                                    int cov = i ^ j;
                                    int x = (k ^ cov);
                                    if (!((seen >> x) & 1))
                                    {
                                        seen |= uint64_t(1) << x;
                                        i = 0;
                                        break;
                                    }
                                }
                                int c = (i & j);                        // AB + AC + AD - BCD = 3A
                                if (c)
                                    for (int k = 1; k < 32; k++)
                                    {
                                        if (!((seen >> k) & 1))
                                            continue;
                                        if ((k & c) != c)
                                            continue;
                                        if ((k & i) != c)
                                            continue;
                                        if ((k & j) != c)
                                            continue;

                                        int bcd = (i | j | k) & ~c;
                                        if (!((seen >> bcd) & 1))
                                            continue;

                                        if (!((seen >> c) & 1))
                                        {
                                            seen |= uint64_t(1) << c;
                                            i = 0;
                                            j = 32;
                                            break;
                                        }
                                    }
                            }
                        }
                    }
                }
            }
        }
        if (want & ~seen)
            return UNBOUNDED;
        if (loses_data)
            return LOSES_DATA;
        return OK;
    }
}

void GridRule::remove_region(int index)
{
    assert(has_valid_structure());
    assert(index >= 0 && index < region_count);
    if (index < if_reg_count * 2)
    {
        int new_if_reg_count = if_reg_count - 1;
        if_reg_count = 0;
        index &= ~1;
        remove_region(index);
        remove_region(index);
        if_reg_count = new_if_reg_count;
        assert(has_valid_structure());
        return;
    }

    const int old_region_count = region_count;
    const bool visibility_action =
        apply_region_type.type == RegionType::VISIBILITY;
    unsigned mask = get_valid_cells_mask(region_count, neg_reg_count);
    uint8_t cnt[16] = {};
    uint8_t var[16] = {};
    uint8_t action_state[16];
    std::fill(action_state, action_state + 16, uint8_t(0xFF));

    for (int i = 0; i < 16; i++)
    {
        if (!((mask >> i) & 1))
            continue;

        int to = (i & ((1 << index) - 1)) | ((i & ((1 << region_count) - 1)) >> (index + 1)) << (index);
        if (neg_reg_count)
        {
            to |= i & (0x30 >> neg_reg_count);
            if (index == 0 && neg_reg_count == 1)
                to &= ~8;
            if (index == 0 && neg_reg_count == 2)
                to &= ~4;
            if (index == 1 && neg_reg_count == 2)
            {
                to &= ~0xC;
                if (i & 4)
                    to |= 8;
            }
        }
        if (square_counts[i].type == RegionType::NONE)
            cnt[to] = 100;
        if (cnt[to] < 100)
            cnt[to] += square_counts[i].value;
        var[to] |= square_counts[i].var;

        if (!visibility_action)
        {
            const uint8_t state =
                ((apply_region_bitmap >> i) & 1) |
                (((neg_apply_region_bitmap >> i) & 1) << 1);
            if (action_state[to] == 0xFF)
                action_state[to] = state;
            else if (action_state[to] != state)
                action_state[to] = 4;
        }
    }

    uint16_t new_apply_region_bitmap = 0;
    uint16_t new_neg_apply_region_bitmap = 0;

    region_count--;
    if (index < neg_reg_count)
        neg_reg_count--;
    for (int i = index; i < region_count; i++)
        region_type[i] = region_type[i + 1];
    region_type[region_count] = RegionType();


    mask = get_valid_cells_mask(region_count, neg_reg_count);
    for (int i = 0; i < 16; i++)
    {
        if (!((mask >> i) & 1))
        {
            square_counts[i] = RegionType(RegionType::NONE, 0);
            continue;
        }
        if (cnt[i] < 100)
        {
            square_counts[i] = RegionType(RegionType::EQUAL, cnt[i]);
            square_counts[i].var = var[i];
        }
        else
            square_counts[i] = RegionType(RegionType::NONE, 0);

        if (!visibility_action && action_state[i] < 4)
        {
            new_apply_region_bitmap |= (action_state[i] & 1) << i;
            new_neg_apply_region_bitmap |=
                ((action_state[i] & 2) >> 1) << i;
        }
    }
    if (visibility_action)
    {
        const auto remove_slot = [index, old_region_count](uint16_t bitmap)
        {
            bitmap &= (1u << old_region_count) - 1;
            const uint16_t lower = bitmap & ((1u << index) - 1);
            const uint16_t upper = bitmap >> (index + 1);
            return uint16_t(lower | (upper << index));
        };
        new_apply_region_bitmap = remove_slot(apply_region_bitmap);
        new_neg_apply_region_bitmap = remove_slot(neg_apply_region_bitmap);
    }
    apply_region_bitmap = new_apply_region_bitmap;
    neg_apply_region_bitmap = new_neg_apply_region_bitmap;
    if (apply_if_region_type.type == RegionType::NONE)
        neg_apply_region_bitmap &= apply_region_bitmap;
    assert(has_valid_structure());
}

void GridRule::add_region(RegionType type, bool neg)
{
    int index;
    if (neg)
    {
        index = neg_reg_count;
        neg_reg_count++;
    }
    else
        index = region_count;

    if (!index)
    {
        for (int i = region_count; i > 0; i--)
        {
            region_type[i] = region_type[i - 1];
        }
    }

    region_count++;
    unsigned mask = get_valid_cells_mask(region_count, neg_reg_count);
    if (neg_reg_count == 2)
        square_counts[5] = square_counts[9];
    for (int i = 15; i >= 0; i--)
    {
        if (!((mask >> i) & 1))
            continue;
        if ((i >> index) & 1)
            square_counts[i] = RegionType(RegionType::EQUAL, 0);
        else if (!index)
            square_counts[i] = square_counts[i >> 1];
    }


    region_type[index] = type;

}

void GridRule::resort_region()
{
    struct Sorter {
        GridRule& g;
        Sorter(GridRule& g_): g(g_) {};
        bool operator() (int i,int j) { return (g.region_type[i] < g.region_type[j]);}
    };
    Sorter sorter(*this);
    // Conditional inputs are ordered [if, then] pairs. Keep that structural
    // prefix intact and sort only the ordinary inputs that follow it.
    const int fixed_prefix_count = std::min<int>(
        region_count, std::max<int>(neg_reg_count, if_reg_count * 2));
    std::vector<int> idx;
    for(int i = fixed_prefix_count; i < region_count; i++)
        idx.push_back(i);
    std::sort (idx.begin(), idx.end(), sorter);
    sort_perm = 0;
    for(int i = 0; i < fixed_prefix_count; i++)
        sort_perm |= i << (i * 2);
    for(int i = fixed_prefix_count; i < region_count; i++)
        sort_perm |= idx[i-fixed_prefix_count] << (i * 2);
    if (!if_reg_count && neg_reg_count == 2)
    {
        sort_perm = (region_type[0] < region_type[1]) ? 4 : 1;

    }
}

RegionType GridRule::get_region_sorted(int index)
{
    return region_type[(sort_perm >> (index * 2)) & 0x3];
}

void Grid::randomize(XYPos size_, WrapType wrapped_, int merged_count, int row_percent, int negated_percent)
{
    size = size_;
    wrapped = wrapped_;
    add_random_merged(merged_count);

    XYSet grid_squares = get_squares();
    FOR_XY_SET(p, grid_squares)
    {
        vals[p] = GridPlace((unsigned(rnd)%100) < 40, true);
        if ((unsigned(rnd) % 100) < negated_percent)
            vals[p].negated = true;
    }

    FOR_XY_SET(p, grid_squares)
    {
        if (!vals[p].bomb)
        {
            int cnt = 0;
            XYSet neigh = get_neighbors(p);
            FOR_XY_SET(t, neigh)
            {
                if (get(t).bomb)
                {
                    if (get(t).negated)
                        cnt--;
                    else
                        cnt++;
                }
            }

            vals[p].clue.type.type = RegionType::EQUAL;
            vals[p].clue.type.value = cnt;
        }
    }
    std::vector<XYPos> row_types;
    get_row_types(row_types);
    for (unsigned i = 0; i < row_types.size(); i++)
    {
        XYPos row_type = row_types[i];
        for (int j = row_type.x; j < row_type.y; j++)
        {
            if (int(rnd % 10) < row_percent)
            {
                int c = 0;
                XYSet grid_squares = get_row(i, j);
                FOR_XY_SET(n, grid_squares)
                    if (get(n).bomb)
                    {
                        if (get(n).negated)
                            c--;
                        else
                            c++;
                    }
                edges[XYPos(i, j)] = RegionIfType(RegionType::EQUAL, c);
            }
        }
    }
}

Grid::Grid()
{
}

static const char* debug_region_type_name(RegionType::Type type)
{
    switch (type)
    {
    case RegionType::NONE:       return "any";
    case RegionType::EQUAL:      return "equal";
    case RegionType::LESS:       return "less";
    case RegionType::MORE:       return "more";
    case RegionType::XOR2:       return "xor2";
    case RegionType::XOR3:       return "xor3";
    case RegionType::XOR22:      return "xor22";
    case RegionType::XOR222:     return "xor222";
    case RegionType::NOTEQUAL:   return "not-equal";
    case RegionType::PARITY:     return "parity";
    case RegionType::XOR1:       return "xor1";
    case RegionType::XOR11:      return "xor11";
    case RegionType::PRIME:      return "prime";
    case RegionType::TRIANGLE:   return "triangle";
    case RegionType::POW2:       return "power-of-two";
    case RegionType::FIBONACCI:  return "fibonacci";
    case RegionType::BOX:        return "box";
    case RegionType::SET:        return "set";
    case RegionType::VISIBILITY: return "visibility";
    }
    return "unknown";
}

static std::string debug_region_type(const RegionType& type)
{
    std::ostringstream out;
    out << debug_region_type_name(type.type);
    if (type.type != RegionType::NONE || type.value || type.var)
        out << '(' << type.val_as_str() << ')';
    return out.str();
}

static std::string debug_positions(const XYSet& positions)
{
    std::ostringstream out;
    bool first = true;
    FOR_XY_SET(pos, positions)
    {
        if (!first)
            out << ' ';
        out << '(' << pos.x << ',' << pos.y << ')';
        first = false;
    }
    return first ? "-" : out.str();
}

static std::array<std::string, 4> debug_rule_dimensions(const GridRule& rule)
{
    std::array<std::string, 4> names;
    if (rule.if_reg_count)
    {
        unsigned dimension = 0;
        unsigned region = 1;
        for (unsigned i = 0; i < rule.if_reg_count; i++, region++)
        {
            names[dimension++] = "R" + std::to_string(region) + ".if";
            names[dimension++] = "R" + std::to_string(region) + ".then";
        }
        while (dimension < rule.region_count)
            names[dimension++] = "R" + std::to_string(region++);
    }
    else
    {
        for (unsigned i = 0; i < rule.region_count; i++)
            names[i] = "R" + std::to_string(i + 1);
        if (rule.neg_reg_count == 1)
            names[3] = "R1.negated";
        else if (rule.neg_reg_count == 2)
        {
            names[2] = "R1.negated";
            names[3] = "R2.negated";
        }
    }
    return names;
}

static unsigned debug_rule_dimension_mask(const GridRule& rule)
{
    unsigned mask = (1u << rule.region_count) - 1;
    if (!rule.if_reg_count && rule.neg_reg_count == 1)
        mask |= 1u << 3;
    if (!rule.if_reg_count && rule.neg_reg_count == 2)
        mask |= (1u << 2) | (1u << 3);
    return mask;
}

static std::string debug_rule_area(const GridRule& rule, unsigned area)
{
    const std::array<std::string, 4> names = debug_rule_dimensions(rule);
    const unsigned dimension_mask = debug_rule_dimension_mask(rule);
    std::ostringstream out;
    bool first = true;
    for (unsigned bit = 0; bit < names.size(); bit++)
    {
        if (!(dimension_mask & (1u << bit)) || names[bit].empty())
            continue;
        if (!first)
            out << " & ";
        if (!(area & (1u << bit)))
            out << "not ";
        out << names[bit];
        first = false;
    }
    return first ? "outside all inputs" : out.str();
}

static std::string debug_rule_areas(const GridRule& rule, uint16_t bitmap)
{
    std::ostringstream out;
    bool first = true;
    const unsigned valid = get_valid_cells_mask(rule.region_count, rule.neg_reg_count);
    for (unsigned area = 1; area < 16; area++)
    {
        if (!(valid & (1u << area)) || !(bitmap & (1u << area)))
            continue;
        if (!first)
            out << "; ";
        out << "area " << area << " [" << debug_rule_area(rule, area) << ']';
        first = false;
    }
    return first ? "-" : out.str();
}

unsigned GridRule::valid_area_mask() const
{
    return get_valid_cells_mask(region_count, neg_reg_count);
}

static unsigned debug_rule_region_for_slot(const GridRule& rule, unsigned slot)
{
    if (slot < rule.if_reg_count * 2)
        return slot / 2 + 1;
    return rule.if_reg_count + slot - rule.if_reg_count * 2 + 1;
}

std::string GridRule::debug_description(unsigned indent) const
{
    const std::string pad(indent, ' ');
    const std::string detail_pad(indent + 2, ' ');
    std::ostringstream out;

    out << pad << "Inputs:\n";
    unsigned type_index = 0;
    unsigned region_index = 1;
    for (unsigned i = 0; i < if_reg_count; i++, region_index++)
    {
        out << detail_pad << 'R' << region_index << ": if "
            << debug_region_type(region_type[type_index]) << " then "
            << debug_region_type(region_type[type_index + 1]) << '\n';
        type_index += 2;
    }
    while (type_index < region_count)
    {
        out << detail_pad << 'R' << region_index << ": "
            << debug_region_type(region_type[type_index]);
        const unsigned regular_index = type_index - if_reg_count * 2;
        if (regular_index < neg_reg_count)
            out << " (has a negated subset)";
        out << '\n';
        type_index++;
        region_index++;
    }

    bool have_constraints = false;
    const unsigned valid = get_valid_cells_mask(region_count, neg_reg_count);
    for (unsigned area = 1; area < 16; area++)
        have_constraints |= (valid & (1u << area)) && square_counts[area].type != RegionType::NONE;
    out << pad << "Area constraints:";
    if (!have_constraints)
        out << " none\n";
    else
    {
        out << '\n';
        for (unsigned area = 1; area < 16; area++)
        {
            if (!(valid & (1u << area)) || square_counts[area].type == RegionType::NONE)
                continue;
            out << detail_pad << "area " << area << " [" << debug_rule_area(*this, area) << "]: cell count "
                << debug_region_type(square_counts[area]) << '\n';
        }
    }

    out << pad << "Action: ";
    if (apply_region_type.type == RegionType::VISIBILITY)
    {
        static const char* visibility_names[] = {"show", "hide", "trash"};
        if (apply_region_type.value >= 0 && apply_region_type.value < 3)
            out << visibility_names[apply_region_type.value];
        else
            out << debug_region_type(apply_region_type);
        out << " input region";
        std::set<unsigned> targets;
        for (unsigned slot = 0; slot < region_count; slot++)
            if (apply_region_bitmap & (1u << slot))
                targets.insert(debug_rule_region_for_slot(*this, slot));
        if (targets.size() != 1)
            out << 's';
        out << ' ';
        bool first = true;
        for (unsigned target : targets)
        {
            if (!first)
                out << ", ";
            out << 'R' << target;
            first = false;
        }
        if (targets.empty())
            out << '-';
        out << '\n';
    }
    else if (apply_region_type.type == RegionType::SET)
    {
        out << (apply_region_type.value ? "reveal as bomb in " : "reveal as clear in ")
            << debug_rule_areas(*this, apply_region_bitmap) << '\n';
    }
    else if (apply_if_region_type.type != RegionType::NONE)
    {
        out << "create implication region: if " << debug_region_type(apply_if_region_type)
            << " over " << debug_rule_areas(*this, neg_apply_region_bitmap)
            << ", then " << debug_region_type(apply_region_type)
            << " over " << debug_rule_areas(*this, apply_region_bitmap) << '\n';
    }
    else
    {
        out << "create " << debug_region_type(apply_region_type) << " region over "
            << debug_rule_areas(*this, apply_region_bitmap);
        if (neg_apply_region_bitmap)
            out << "; negated cells " << debug_rule_areas(*this, neg_apply_region_bitmap);
        out << '\n';
    }
    return out.str();
}

std::string Grid::debug_dump() const
{
    std::ostringstream out;
    out << "Board: " << const_cast<Grid*>(this)->text_desciption() << '\n';
    out << "Cells (x,y):\n";
    for (const auto& entry : vals)
    {
        const XYPos& pos = entry.first;
        const GridPlace& cell = entry.second;
        out << "  (" << pos.x << ',' << pos.y << ") "
            << (cell.bomb ? "bomb" : "clear") << ", "
            << (cell.revealed ? "revealed" : "hidden");
        if (cell.negated)
            out << ", negated";
        if (cell.clue.if_type.type != RegionType::NONE)
            out << ", if " << debug_region_type(cell.clue.if_type)
                << " then " << debug_region_type(cell.clue.type);
        else if (cell.clue.type.type != RegionType::NONE)
            out << ", clue " << debug_region_type(cell.clue.type);
        out << '\n';
    }

    if (!merged.empty())
    {
        out << "Merged cells:\n";
        for (const auto& entry : merged)
            out << "  (" << entry.first.x << ',' << entry.first.y << ") size "
                << entry.second.x << 'x' << entry.second.y << '\n';
    }
    if (!edges.empty())
    {
        out << "Edge clues:\n";
        for (const auto& entry : edges)
            out << "  (" << entry.first.x << ',' << entry.first.y << ") "
                << debug_region_type(entry.second.type) << '\n';
    }

    std::map<const GridRegion*, unsigned> region_ids;
    unsigned next_id = 0;
    for (const GridRegion& region : regions)
        region_ids[&region] = next_id++;

    unsigned unprocessed_regions = 0;
    for (const GridRegion& region : regions)
        if (!region.stale)
            unprocessed_regions++;
    const bool regions_pending = wants_base_regions || unprocessed_regions || !regions_to_add.empty();
    out << "Regions still to be revealed: " << (regions_pending ? "yes" : "no");
    if (regions_pending)
    {
        out << " (";
        bool have_detail = false;
        if (wants_base_regions)
        {
            out << "base regions not generated";
            have_detail = true;
        }
        if (unprocessed_regions)
        {
            if (have_detail)
                out << ", ";
            out << unprocessed_regions << " awaiting processing";
            have_detail = true;
        }
        if (!regions_to_add.empty())
        {
            if (have_detail)
                out << ", ";
            out << regions_to_add.size() << " queued";
        }
        out << ')';
    }
    out << '\n';

    out << "Regions (" << regions.size() << "):\n";
    for (const GridRegion& region : regions)
    {
        out << "  R" << region_ids[&region] << ": ";
        if (region.if_type.type != RegionType::NONE)
            out << "if " << debug_region_type(region.if_type) << " then ";
        out << debug_region_type(region.type)
            << ", visibility="
            << (region.vis_level == GRID_VIS_LEVEL_SHOW ? "shown" :
                region.vis_level == GRID_VIS_LEVEL_HIDE ? "hidden" : "trash");
        if (region.stale)
            out << ", stale";
        if (region.deleted)
            out << ", deleted";
        out << "\n    cells: " << debug_positions(region.elements)
            << "\n    negative cells: " << debug_positions(region.elements_neg);
        bool has_parents = false;
        for (const GridRegion* parent : region.gen_cause.regions)
            has_parents |= parent != NULL;
        if (has_parents)
        {
            out << "\n    derived from:";
            for (const GridRegion* parent : region.gen_cause.regions)
            {
                if (!parent)
                    continue;
                const auto found = region_ids.find(parent);
                if (found == region_ids.end())
                    out << " [old region]";
                else
                    out << " R" << found->second;
            }
        }
        out << '\n';
    }
    return out.str();
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
    unsigned i = 3;
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
        RegionIfType t;
        mp.x = s[i++] - '0';
        mp.y = s[i++] - '0';
        if (s[i] == '?')
        {
            i++;
            if (s[i] == '@')
            {
                i++;
                t.if_partition = RegionIfType::IfPartition(s[i++] - '0');
                t.if_partition_index = s[i++] - 'A';
                if (t.if_partition == RegionIfType::ADJACENT_PAIR)
                    t.if_partition_index2 = s[i++] - 'A';
            }
            t.if_type.type = RegionType::Type(s[i++] - 'A');
            t.if_type.value = s[i++] - '0';

        }
        t.type.type = RegionType::Type(s[i++] - 'A');
        t.type.value = s[i++] - '0';

        if (t.type.type != RegionType::NONE)
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

        if (c == '~')
        {
            vals[p].negated = true;
            if (i >= s.length()) return;
            c = s[i++];
        }

        if (c == '!')
        {
            vals[p].bomb = true;
        }
        else if (c == '?')
        {
            vals[p].bomb = false;
            if (s[i] == '@')
            {
                i++;
                vals[p].clue.if_partition = RegionIfType::IfPartition(s[i++] - '0');
                vals[p].clue.if_partition_index = s[i++] - 'A';
                if (vals[p].clue.if_partition == RegionIfType::ADJACENT_PAIR)
                    vals[p].clue.if_partition_index2 = s[i++] - 'A';
            }
            vals[p].clue.if_type.type = RegionType::Type(s[i++] - 'A');
            vals[p].clue.if_type.value = s[i++] - '0';
            vals[p].clue.type.type = RegionType::Type(s[i++] - 'A');
            vals[p].clue.type.value = s[i++] - '0';
        }
        else
        {
            vals[p].bomb = false;
            vals[p].clue.type.type = RegionType::Type(c - 'A');
            if (i >= s.length()) return;
            c = s[i++];
            if (vals[p].clue.type.type == RegionType::Type(RegionType::NONE))
                vals[p].clue.type.value = 0;
            else
                vals[p].clue.type.value = c - '0';

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
RegionIfType& Grid::get_clue(XYPos p)
{
    if (p.x < 0)
        return edges[XYPos(-1 - p.x, p.y)];
    assert (p.inside(size));
    return vals[p].clue;
}


static std::list<GridRule> global_rules;

void Grid::solve_easy()
{
    return;
    if (global_rules.empty())
    {
        std::string sin = "[{\"apply_region_bitmap\":2,\"apply_region_type\":66304,\"clear_count\":492,\"priority\":0,\"region_count\":1,\"region_type\":[65792],\"square_counts\":[0,0],\"used_count\":1089},{\"apply_region_bitmap\":2,\"apply_region_type\":66048,\"clear_count\":1156,\"priority\":0,\"region_count\":1,\"region_type\":[65792],\"square_counts\":[0,0],\"used_count\":961},{\"apply_region_bitmap\":2,\"apply_region_type\":25857,\"clear_count\":0,\"priority\":0,\"region_count\":2,\"region_type\":[65792,66304],\"square_counts\":[0,256,256,0],\"used_count\":0},{\"apply_region_bitmap\":2,\"apply_region_type\":25857,\"clear_count\":0,\"priority\":0,\"region_count\":2,\"region_type\":[65792,66048],\"square_counts\":[0,256,256,0],\"used_count\":0},{\"apply_region_bitmap\":2,\"apply_region_type\":25601,\"clear_count\":902,\"priority\":0,\"region_count\":1,\"region_type\":[66304],\"square_counts\":[0,65792],\"used_count\":483},{\"apply_region_bitmap\":2,\"apply_region_type\":25600,\"clear_count\":1602,\"priority\":0,\"region_count\":1,\"region_type\":[512],\"square_counts\":[0,0],\"used_count\":842},{\"apply_region_bitmap\":4,\"apply_region_type\":131841,\"clear_count\":39,\"priority\":0,\"region_count\":2,\"region_type\":[66048,197377],\"square_counts\":[0,0,0,66305],\"used_count\":49},{\"apply_region_bitmap\":4,\"apply_region_type\":262656,\"clear_count\":33,\"priority\":0,\"region_count\":2,\"region_type\":[197376,328192],\"square_counts\":[0,131328,0,0],\"used_count\":359},{\"apply_region_bitmap\":1,\"apply_region_type\":25858,\"clear_count\":0,\"priority\":0,\"region_count\":2,\"region_type\":[66048,66048],\"square_counts\":[0,256,0,0],\"used_count\":0},{\"apply_region_bitmap\":8,\"apply_region_type\":25600,\"clear_count\":5,\"priority\":0,\"region_count\":3,\"region_type\":[66049,131584,459520],\"square_counts\":[0,0,0,0,262656,0,0,0],\"used_count\":3},{\"apply_region_bitmap\":2,\"apply_region_type\":65792,\"clear_count\":200,\"priority\":0,\"region_count\":1,\"region_type\":[66816],\"square_counts\":[0,66050],\"used_count\":167},{\"apply_region_bitmap\":2,\"apply_region_type\":65792,\"clear_count\":241,\"priority\":0,\"region_count\":1,\"region_type\":[66560],\"square_counts\":[0,66049],\"used_count\":305},{\"apply_region_bitmap\":4,\"apply_region_type\":132096,\"clear_count\":1,\"priority\":0,\"region_count\":2,\"region_type\":[65792,197632],\"square_counts\":[0,256,0,0],\"used_count\":6},{\"apply_region_bitmap\":2,\"apply_region_type\":25858,\"clear_count\":0,\"priority\":0,\"region_count\":2,\"region_type\":[65792,66560],\"square_counts\":[0,256,256,0],\"used_count\":0},{\"apply_region_bitmap\":2,\"apply_region_type\":25858,\"clear_count\":0,\"priority\":0,\"region_count\":2,\"region_type\":[65792,66816],\"square_counts\":[0,256,256,0],\"used_count\":0},{\"apply_region_bitmap\":2,\"apply_region_type\":25858,\"clear_count\":0,\"priority\":0,\"region_count\":2,\"region_type\":[65792,68096],\"square_counts\":[0,256,256,0],\"used_count\":0},{\"apply_region_bitmap\":2,\"apply_region_type\":25858,\"clear_count\":0,\"priority\":0,\"region_count\":2,\"region_type\":[65792,68352],\"square_counts\":[0,256,256,0],\"used_count\":0},{\"apply_region_bitmap\":12,\"apply_region_type\":65794,\"clear_count\":2,\"priority\":0,\"region_count\":2,\"region_type\":[197377,66560],\"square_counts\":[0,131328,0,0],\"used_count\":1},{\"apply_region_bitmap\":2,\"apply_region_type\":66048,\"clear_count\":54,\"priority\":0,\"region_count\":1,\"region_type\":[67585],\"square_counts\":[0,65793],\"used_count\":78},{\"apply_region_bitmap\":2,\"apply_region_type\":66560,\"clear_count\":205,\"priority\":0,\"region_count\":1,\"region_type\":[67072],\"square_counts\":[0,66051],\"used_count\":425},{\"apply_region_bitmap\":12,\"apply_region_type\":65795,\"clear_count\":0,\"priority\":0,\"region_count\":2,\"region_type\":[197377,66816],\"square_counts\":[0,131584,0,0],\"used_count\":0},{\"apply_region_bitmap\":2,\"apply_region_type\":25601,\"clear_count\":177,\"priority\":0,\"region_count\":1,\"region_type\":[68096],\"square_counts\":[0,65792],\"used_count\":80},{\"apply_region_bitmap\":2,\"apply_region_type\":67072,\"clear_count\":305,\"priority\":0,\"region_count\":1,\"region_type\":[67840],\"square_counts\":[0,66053],\"used_count\":432},{\"apply_region_bitmap\":2,\"apply_region_type\":769,\"clear_count\":44,\"priority\":0,\"region_count\":1,\"region_type\":[2048],\"square_counts\":[0,0],\"used_count\":55},{\"apply_region_bitmap\":4,\"apply_region_type\":132608,\"clear_count\":0,\"priority\":0,\"region_count\":2,\"region_type\":[66560,197634],\"square_counts\":[0,256,0,0],\"used_count\":0},{\"apply_region_bitmap\":4,\"apply_region_type\":1025,\"clear_count\":1,\"priority\":0,\"region_count\":2,\"region_type\":[66560,66561],\"square_counts\":[0,256,0,0],\"used_count\":1},{\"apply_region_bitmap\":2,\"apply_region_type\":66304,\"clear_count\":173,\"priority\":0,\"region_count\":1,\"region_type\":[67072],\"square_counts\":[0,0],\"used_count\":270},{\"apply_region_bitmap\":2,\"apply_region_type\":66304,\"clear_count\":50,\"priority\":0,\"region_count\":1,\"region_type\":[68352],\"square_counts\":[0,0],\"used_count\":62},{\"apply_region_bitmap\":2,\"apply_region_type\":66049,\"clear_count\":18,\"priority\":0,\"region_count\":1,\"region_type\":[68096],\"square_counts\":[0,0],\"used_count\":202},{\"apply_region_bitmap\":4,\"apply_region_type\":68096,\"clear_count\":3,\"priority\":0,\"region_count\":2,\"region_type\":[131328,199168],\"square_counts\":[0,256,0,0],\"used_count\":2},{\"apply_region_bitmap\":12,\"apply_region_type\":196864,\"clear_count\":10,\"priority\":0,\"region_count\":2,\"region_type\":[66048,199168],\"square_counts\":[0,0,131584,0],\"used_count\":7},{\"apply_region_bitmap\":4,\"apply_region_type\":132352,\"clear_count\":4,\"priority\":0,\"region_count\":2,\"region_type\":[65792,197888],\"square_counts\":[0,256,0,0],\"used_count\":2},{\"apply_region_bitmap\":2,\"apply_region_type\":1024,\"clear_count\":2,\"priority\":0,\"region_count\":1,\"region_type\":[2049],\"square_counts\":[0,258],\"used_count\":14},{\"apply_region_bitmap\":1,\"apply_region_type\":25858,\"clear_count\":0,\"priority\":0,\"region_count\":2,\"region_type\":[67072,66560],\"square_counts\":[0,256,256,0],\"used_count\":0},{\"apply_region_bitmap\":2,\"apply_region_type\":25858,\"clear_count\":0,\"priority\":0,\"region_count\":2,\"region_type\":[66560,67840],\"square_counts\":[0,256,256,0],\"used_count\":0}]";
        SaveObject* sobj = SaveObject::load(sin);
        SaveObjectList* rlist = sobj->get_list();
        for (unsigned i = 0; i < rlist->get_count(); i++)
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
        add_base_regions();
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
        // for (GridRegion& r : regions)
        // {
        //     r.stale = true;
        // }
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

    wants_base_regions = true;
    tst->add_base_regions();
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
        FOR_XY_SET (p, (r.elements & ~r.elements_neg))
        {
            set_index++;
            unsigned v = pos_to_set[p];
            FOR_XY_SET (p2, (r.elements & ~r.elements_neg))
            {
                if (pos_to_set[p2] == v)
                    pos_to_set[p2] = set_index;
            }
        }
        FOR_XY_SET (p, (r.elements & r.elements_neg))
        {
            set_index++;
            unsigned v = pos_to_set[p];
            FOR_XY_SET (p2, (r.elements & r.elements_neg))
            {
                if (pos_to_set[p2] == v)
                    pos_to_set[p2] = set_index;
            }
        }
        FOR_XY_SET (p, (~r.elements & r.elements_neg))
        {
            set_index++;
            unsigned v = pos_to_set[p];
            FOR_XY_SET (p2, (~r.elements & r.elements_neg))
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

    for (unsigned i = 1; i < set_index; i++)
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
        z3::expr e_if = c.int_val(0);
        uid += "E";
        FOR_XY_SET (p, (r.elements | r.elements_neg))
        {
            unsigned si = pos_to_set[p];
            if (!seen.count(si))
            {
                seen.insert(si);
                if (r.elements_neg.get(p))
                {
                    if (r.is_if_then())
                    {
                        e_if = e_if + vec[si];
                        uid += "i" + std::to_string(si) + ",";
                        if (r.elements.get(p))
                        {
                            e = e + vec[si];
                            uid += std::to_string(si) + ",";
                        }

                    }
                    else
                    {
                        e = e - vec[si];
                        uid += "-" + std::to_string(si) + ",";
                    }
                }
                else
                {
                    e = e + vec[si];
                    uid += std::to_string(si) + ",";
                }
            }
        }
        if (r.is_if_then())
            s.add(z3::implies(r.if_type.apply_z3_rule(e_if, dummy_vec), r.type.apply_z3_rule(e, dummy_vec)));
        else
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
//    solution_cache[uid] = det;
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

static RegionType& implication_side(Grid& grid, XYPos pos, bool antecedent)
{
    RegionIfType& clue = grid.get_clue(pos);
    return antecedent ? clue.if_type : clue.type;
}

static void relax_implication_side(
    Grid& grid, XYPos pos, bool antecedent,
    int plus_minus, int x_y, int x_y3, int x_y_z, int exc,
    int parity, int xor1, int xor11, int prime)
{
    auto try_type = [&](int chance, RegionType::Type type, int offset, int minimum = 0)
    {
        RegionType current = implication_side(grid, pos, antecedent);
        if (!chance || current.value < minimum || int(rnd % 10) >= chance)
            return false;

        LocalGrid test = grid;
        RegionType& candidate = implication_side(*test, pos, antecedent);
        candidate.type = type;
        candidate.value += offset;
        if (!test->is_solveable())
            return false;

        implication_side(grid, pos, antecedent) = candidate;
        return true;
    };

    if (try_type(exc, RegionType::NOTEQUAL, 2) ||
        try_type(exc, RegionType::NOTEQUAL, -2, 3) ||
        try_type(exc, RegionType::NOTEQUAL, -1, 2) ||
        try_type(exc, RegionType::NOTEQUAL, 1))
        return;

    if (try_type(parity, RegionType::PARITY, -4, 4) ||
        try_type(parity, RegionType::PARITY, -2, 2) ||
        try_type(parity, RegionType::PARITY, 0))
        return;

    if (prime)
    {
        static const struct
        {
            RegionType::Type type;
            int value;
        } value_types[] = {
            {RegionType::BOX, 16}, {RegionType::POW2, 16},
            {RegionType::TRIANGLE, 15}, {RegionType::FIBONACCI, 13},
            {RegionType::BOX, 13}, {RegionType::PRIME, 13},
            {RegionType::BOX, 12}, {RegionType::PRIME, 11},
            {RegionType::TRIANGLE, 10}, {RegionType::BOX, 9},
            {RegionType::BOX, 8}, {RegionType::FIBONACCI, 8},
            {RegionType::POW2, 8}, {RegionType::PRIME, 7},
            {RegionType::TRIANGLE, 6}, {RegionType::FIBONACCI, 5},
            {RegionType::BOX, 5}, {RegionType::PRIME, 5},
            {RegionType::BOX, 4}, {RegionType::POW2, 4},
            {RegionType::FIBONACCI, 3}, {RegionType::TRIANGLE, 3},
            {RegionType::PRIME, 3}, {RegionType::FIBONACCI, 2},
            {RegionType::PRIME, 2}, {RegionType::POW2, 2},
            {RegionType::BOX, 1}, {RegionType::FIBONACCI, 1},
            {RegionType::POW2, 1}, {RegionType::TRIANGLE, 1},
            {RegionType::BOX, 0}, {RegionType::TRIANGLE, 0},
        };
        for (const auto& value_type : value_types)
            if (try_type(prime, value_type.type, -value_type.value, value_type.value))
                return;
    }

    if (try_type(xor11, RegionType::XOR11, -2, 2) ||
        try_type(xor11, RegionType::XOR11, -1, 1) ||
        try_type(xor11, RegionType::XOR11, 0) ||
        try_type(xor1, RegionType::XOR1, -1, 1) ||
        try_type(xor1, RegionType::XOR1, 0) ||
        try_type(x_y_z, RegionType::XOR22, -2, 2) ||
        try_type(x_y_z, RegionType::XOR22, 0) ||
        try_type(x_y_z, RegionType::XOR22, -4, 4) ||
        try_type(x_y3, RegionType::XOR3, 0) ||
        try_type(x_y3, RegionType::XOR3, -3, 3) ||
        try_type(x_y, RegionType::XOR2, 0) ||
        try_type(x_y, RegionType::XOR2, -2, 2))
        return;

    if (!plus_minus || int(rnd % 10) >= plus_minus)
        return;

    LocalGrid test = grid;
    implication_side(*test, pos, antecedent).type = RegionType::LESS;
    if (test->is_solveable())
    {
        implication_side(grid, pos, antecedent) = implication_side(*test, pos, antecedent);
        while (implication_side(*test, pos, antecedent).value < 19)
        {
            LocalGrid next = grid;
            implication_side(*next, pos, antecedent).value++;
            if (!next->is_solveable())
                break;
            implication_side(grid, pos, antecedent) = implication_side(*next, pos, antecedent);
            test = grid;
        }
        return;
    }

    test = grid;
    implication_side(*test, pos, antecedent).type = RegionType::MORE;
    if (!test->is_solveable())
        return;
    implication_side(grid, pos, antecedent) = implication_side(*test, pos, antecedent);
    while (implication_side(*test, pos, antecedent).value > -19)
    {
        LocalGrid next = grid;
        implication_side(*next, pos, antecedent).value--;
        if (!next->is_solveable())
            break;
        implication_side(grid, pos, antecedent) = implication_side(*next, pos, antecedent);
        test = grid;
    }
}

static bool try_relaxed_implication_antecedent(
    Grid& grid, XYPos pos, RegionIfType clue, unsigned cell_count,
    unsigned hidden_cell_count, unsigned revealed_bomb_count,
    int plus_minus, int x_y, int x_y3, int x_y_z, int exc,
    int parity, int xor1, int xor11, int prime)
{
    const RegionType exact = clue.if_type;
    std::vector<RegionType> candidates;

    auto add_candidate = [&](int chance, RegionType::Type type, int offset, int minimum = 0)
    {
        if (!chance || exact.value < minimum || int(rnd % 10) >= chance)
            return;
        RegionType candidate = exact;
        candidate.type = type;
        candidate.value += offset;
        if (!candidate.apply_int_rule(exact.value))
            return;
        if (std::find(candidates.begin(), candidates.end(), candidate) == candidates.end())
            candidates.push_back(candidate);
    };

    add_candidate(exc, RegionType::NOTEQUAL, 2);
    add_candidate(exc, RegionType::NOTEQUAL, -2, 3);
    add_candidate(exc, RegionType::NOTEQUAL, -1, 2);
    add_candidate(exc, RegionType::NOTEQUAL, 1);

    add_candidate(parity, RegionType::PARITY, -4, 4);
    add_candidate(parity, RegionType::PARITY, -2, 2);
    add_candidate(parity, RegionType::PARITY, 0);

    if (prime)
    {
        static const struct
        {
            RegionType::Type type;
            int value;
        } value_types[] = {
            {RegionType::BOX, 16}, {RegionType::POW2, 16},
            {RegionType::TRIANGLE, 15}, {RegionType::FIBONACCI, 13},
            {RegionType::BOX, 13}, {RegionType::PRIME, 13},
            {RegionType::BOX, 12}, {RegionType::PRIME, 11},
            {RegionType::TRIANGLE, 10}, {RegionType::BOX, 9},
            {RegionType::BOX, 8}, {RegionType::FIBONACCI, 8},
            {RegionType::POW2, 8}, {RegionType::PRIME, 7},
            {RegionType::TRIANGLE, 6}, {RegionType::FIBONACCI, 5},
            {RegionType::BOX, 5}, {RegionType::PRIME, 5},
            {RegionType::BOX, 4}, {RegionType::POW2, 4},
            {RegionType::FIBONACCI, 3}, {RegionType::TRIANGLE, 3},
            {RegionType::PRIME, 3}, {RegionType::FIBONACCI, 2},
            {RegionType::PRIME, 2}, {RegionType::POW2, 2},
            {RegionType::BOX, 1}, {RegionType::FIBONACCI, 1},
            {RegionType::POW2, 1}, {RegionType::TRIANGLE, 1},
            {RegionType::BOX, 0}, {RegionType::TRIANGLE, 0},
        };
        for (const auto& value_type : value_types)
            add_candidate(prime, value_type.type, -value_type.value, value_type.value);
    }

    add_candidate(xor11, RegionType::XOR11, -2, 2);
    add_candidate(xor11, RegionType::XOR11, -1, 1);
    add_candidate(xor11, RegionType::XOR11, 0);
    add_candidate(xor1, RegionType::XOR1, -1, 1);
    add_candidate(xor1, RegionType::XOR1, 0);
    add_candidate(x_y_z, RegionType::XOR22, -2, 2);
    add_candidate(x_y_z, RegionType::XOR22, 0);
    add_candidate(x_y_z, RegionType::XOR22, -4, 4);
    add_candidate(x_y3, RegionType::XOR3, 0);
    add_candidate(x_y3, RegionType::XOR3, -3, 3);
    add_candidate(x_y, RegionType::XOR2, 0);
    add_candidate(x_y, RegionType::XOR2, -2, 2);

    if (plus_minus && int(rnd % 10) < plus_minus)
    {
        for (int limit = 0; limit <= exact.value; limit++)
            candidates.push_back(RegionType(RegionType::MORE, limit));
        for (int limit = exact.value; limit <= int(cell_count); limit++)
            candidates.push_back(RegionType(RegionType::LESS, limit));
    }

    auto breadth = [hidden_cell_count, revealed_bomb_count](const RegionType& type)
    {
        RegionType test_type = type;
        test_type.value -= revealed_bomb_count;
        unsigned count = 0;
        for (unsigned value = 0; value <= hidden_cell_count; value++)
            if (test_type.apply_int_rule(value))
                count++;
        return count;
    };
    candidates.erase(std::remove_if(candidates.begin(), candidates.end(), [&](const RegionType& candidate)
    {
        return breadth(candidate) == hidden_cell_count + 1;
    }), candidates.end());
    std::stable_sort(candidates.begin(), candidates.end(), [&](const RegionType& a, const RegionType& b)
    {
        return breadth(a) > breadth(b);
    });

    bool found = false;
    RegionType strictest;
    for (const RegionType& candidate : candidates)
    {
        LocalGrid test = grid;
        RegionIfType test_clue = clue;
        test_clue.if_type = candidate;
        test->get_clue(pos) = test_clue;
        if (test->is_solveable())
        {
            strictest = candidate;
            found = true;
        }
    }
    if (!found)
        return false;

    clue.if_type = strictest;
    grid.get_clue(pos) = clue;
    return true;
}

void Grid::make_harder(int plus_minus, int x_y, int x_y3, int x_y_z, int exc, int parity, int xor1, int xor11, int prime, int if_then)
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
                tst->get_clue(p) = RegionIfType();
                if (tst->is_solveable())
                {
                    get_clue(p) = RegionIfType();
                    continue;
                }
            }
            if (if_then)
            {
                if (int(rnd % 10) < if_then)
                {
                    tst = *this;

                    XYSet neigh;
                    if (p.x < 0)
                        neigh = get_row(-1 - p.x, p.y);
                    else
                        neigh = get_neighbors(p);

                    std::vector<XYPos> cells;
                    FOR_XY_SET(n, neigh)
                        cells.push_back(n);

                    std::vector<RegionIfType> partitions(1);
                    for (unsigned i = 0; i < cells.size(); i++)
                    {
                        RegionIfType single;
                        single.if_partition = RegionIfType::SINGLE_CELL;
                        single.if_partition_index = i;
                        partitions.push_back(single);

                        XYSet adjacent = get_neighbors(cells[i]);
                        for (unsigned j = i + 1; j < cells.size(); j++)
                        {
                            if (!adjacent.get(cells[j]))
                                continue;
                            RegionIfType pair;
                            pair.if_partition = RegionIfType::ADJACENT_PAIR;
                            pair.if_partition_index = i;
                            pair.if_partition_index2 = j;
                            partitions.push_back(pair);
                        }
                    }

                    bool got = false;
                    for (RegionIfType& candidate : partitions)
                    {
                        int if_v = 0;
                        int v = 0;
                        bool if_has_hidden_cell = false;
                        bool then_has_hidden_cell = false;
                        unsigned if_cell_count = 0;
                        unsigned hidden_if_cell_count = 0;
                        unsigned revealed_if_bomb_count = 0;
                        for (unsigned i = 0; i < cells.size(); i++)
                        {
                            bool in_if = is_if_partition_cell(candidate, i);
                            if (in_if)
                                if_cell_count++;
                            if (!get(cells[i]).revealed)
                            {
                                if (in_if)
                                {
                                    if_has_hidden_cell = true;
                                    hidden_if_cell_count++;
                                }
                                else
                                    then_has_hidden_cell = true;
                            }
                            if (get(cells[i]).bomb)
                            {
                                if (in_if)
                                {
                                    if_v++;
                                    if (get(cells[i]).revealed)
                                        revealed_if_bomb_count++;
                                }
                                else
                                    v++;
                            }
                        }

                        if (!if_has_hidden_cell || !then_has_hidden_cell)
                            continue;

                        candidate.if_type = RegionType(RegionType::EQUAL, if_v);
                        candidate.type = RegionType(RegionType::EQUAL, v);
                        tst = *this;
                        tst->get_clue(p) = candidate;
                        if (tst->is_solveable())
                        {
                            get_clue(p) = candidate;
                            got = true;
                            break;
                        }
                        if (try_relaxed_implication_antecedent(
                                *this, p, candidate, if_cell_count,
                                hidden_if_cell_count, revealed_if_bomb_count,
                                plus_minus, x_y, x_y3, x_y_z, exc,
                                parity, xor1, xor11, prime))
                        {
                            got = true;
                            break;
                        }
                    }
                    if (got)
                    {
                        relax_implication_side(*this, p, false, plus_minus, x_y, x_y3, x_y_z, exc, parity, xor1, xor11, prime);
                        continue;
                    }
                }
            }
            if (exc)
            {
                if (int(rnd % 10) < exc)
                {
                    tst = *this;
                    tst->get_clue(p).type.type = RegionType::NOTEQUAL;
                    tst->get_clue(p).type.value += 2;
                    if (tst->is_solveable())
                    {
                        get_clue(p) = tst->get_clue(p);
                        continue;
                    }
                }
                if (int(rnd % 10) < exc && (get_clue(p).type.value >= 3))
                {
                    tst = *this;
                    tst->get_clue(p).type.type = RegionType::NOTEQUAL;
                    tst->get_clue(p).type.value -= 2;
                    if (tst->is_solveable())
                    {
                        get_clue(p) = tst->get_clue(p);
                        continue;
                    }
                }
                if (int(rnd % 10) < exc && (get_clue(p).type.value >= 2))
                {
                    tst = *this;
                    tst->get_clue(p).type.type = RegionType::NOTEQUAL;
                    tst->get_clue(p).type.value -= 1;
                    if (tst->is_solveable())
                    {
                        get_clue(p) = tst->get_clue(p);
                        continue;
                    }
                }
                if (int(rnd % 10) < exc)
                {
                    tst = *this;
                    tst->get_clue(p).type.type = RegionType::NOTEQUAL;
                    tst->get_clue(p).type.value += 1;
                    if (tst->is_solveable())
                    {
                        get_clue(p) = tst->get_clue(p);
                        continue;
                    }
                }
            }
            if (parity)
            {
                if (int(rnd % 10) < parity && (get_clue(p).type.value >= 4))
                {
                    tst = *this;
                    tst->get_clue(p).type.type = RegionType::PARITY;
                    tst->get_clue(p).type.value -= 4;
                    if (tst->is_solveable())
                    {
                        get_clue(p) = tst->get_clue(p);
                        continue;
                    }
                }
                if (int(rnd % 10) < parity && (get_clue(p).type.value >= 2))
                {
                    tst = *this;
                    tst->get_clue(p).type.type = RegionType::PARITY;
                    tst->get_clue(p).type.value -= 2;
                    if (tst->is_solveable())
                    {
                        get_clue(p) = tst->get_clue(p);
                        continue;
                    }
                }
                if (int(rnd % 10) < parity)
                {
                    tst = *this;
                    tst->get_clue(p).type.type = RegionType::PARITY;
                    if (tst->is_solveable())
                    {
                        get_clue(p) = tst->get_clue(p);
                        continue;
                    }
                }
            }
            if (prime)
            {
                struct {RegionType::Type type; int value;} val_types[] = {
                    {RegionType::BOX, 16},
                    {RegionType::POW2, 16},
                    {RegionType::TRIANGLE, 15},
                    {RegionType::FIBONACCI, 13},
                    {RegionType::BOX, 13},
                    {RegionType::PRIME, 13},
                    {RegionType::BOX, 12},
                    {RegionType::PRIME, 11},
                    {RegionType::TRIANGLE, 10},
                    {RegionType::BOX, 9},
                    {RegionType::BOX, 8},
                    {RegionType::FIBONACCI, 8},
                    {RegionType::POW2, 8},
                    {RegionType::PRIME, 7},
                    {RegionType::TRIANGLE, 6},
                    {RegionType::FIBONACCI, 5},
                    {RegionType::BOX, 5},
                    {RegionType::PRIME, 5},
                    {RegionType::BOX, 4},
                    {RegionType::POW2, 4},
                    {RegionType::FIBONACCI, 3},
                    {RegionType::TRIANGLE, 3},
                    {RegionType::PRIME, 3},
                    {RegionType::FIBONACCI, 2},
                    {RegionType::PRIME, 2},
                    {RegionType::POW2, 2},
                    {RegionType::BOX, 1},
                    {RegionType::FIBONACCI, 1},
                    {RegionType::POW2, 1},
                    {RegionType::TRIANGLE, 1},
                    {RegionType::BOX, 0},
                    {RegionType::TRIANGLE, 0},
                    {RegionType::NONE, 0},
                    };
                bool got = false;
                for (int i = 0; ;i++)
                {
                    if (val_types[i].type == RegionType::NONE)
                        break;
                    if (int(rnd % 10) < prime && (get_clue(p).type.value >= val_types[i].value))
                    {
                        tst = *this;
                        tst->get_clue(p).type.type = val_types[i].type;
                        tst->get_clue(p).type.value -= val_types[i].value;
                        if (tst->is_solveable())
                        {
                            get_clue(p) = tst->get_clue(p);
                            got = true;
                            break;
                        }
                    }
                }
                if (got)
                    continue;

                // if (int(rnd % 10) < prime && (get_clue(p).type.value >= 7))
                // {
                //     tst = *this;
                //     tst->get_clue(p).type.type = RegionType::PRIME;
                //     tst->get_clue(p).type.value -= 7;
                //     if (tst->is_solveable())
                //     {
                //         get_clue(p) = tst->get_clue(p);
                //         continue;
                //     }
                // }
                // if (int(rnd % 10) < prime && (get_clue(p).type.value >= 6))
                // {
                //     tst = *this;
                //     tst->get_clue(p).type.type = RegionType::TRIANGLE;
                //     tst->get_clue(p).type.value -= 6;
                //     if (tst->is_solveable())
                //     {
                //         get_clue(p) = tst->get_clue(p);
                //         continue;
                //     }
                // }
                // if (int(rnd % 10) < prime && (get_clue(p).type.value >= 5))
                // {
                //     tst = *this;
                //     tst->get_clue(p).type.type = RegionType::PRIME;
                //     tst->get_clue(p).type.value -= 5;
                //     if (tst->is_solveable())
                //     {
                //         get_clue(p) = tst->get_clue(p);
                //         continue;
                //     }
                // }
                // if (int(rnd % 10) < prime && (get_clue(p).type.value >= 4))
                // {
                //     tst = *this;
                //     tst->get_clue(p).type.type = RegionType::POW2;
                //     tst->get_clue(p).type.value -= 4;
                //     if (tst->is_solveable())
                //     {
                //         get_clue(p) = tst->get_clue(p);
                //         continue;
                //     }
                // }
                // if (int(rnd % 10) < prime && (get_clue(p).type.value >= 3))
                // {
                //     tst = *this;
                //     tst->get_clue(p).type.type = RegionType::TRIANGLE;
                //     tst->get_clue(p).type.value -= 3;
                //     if (tst->is_solveable())
                //     {
                //         get_clue(p) = tst->get_clue(p);
                //         continue;
                //     }
                // }
                // if (int(rnd % 10) < prime && (get_clue(p).type.value >= 3))
                // {
                //     tst = *this;
                //     tst->get_clue(p).type.type = RegionType::PRIME;
                //     tst->get_clue(p).type.value -= 3;
                //     if (tst->is_solveable())
                //     {
                //         get_clue(p) = tst->get_clue(p);
                //         continue;
                //     }
                // }
                // if (int(rnd % 10) < prime && (get_clue(p).type.value >= 2))
                // {
                //     tst = *this;
                //     tst->get_clue(p).type.type = RegionType::POW2;
                //     tst->get_clue(p).type.value -= 2;
                //     if (tst->is_solveable())
                //     {
                //         get_clue(p) = tst->get_clue(p);
                //         continue;
                //     }
                // }
                // if (int(rnd % 10) < prime && (get_clue(p).type.value >= 2))
                // {
                //     tst = *this;
                //     tst->get_clue(p).type.type = RegionType::PRIME;
                //     tst->get_clue(p).type.value -= 2;
                //     if (tst->is_solveable())
                //     {
                //         get_clue(p) = tst->get_clue(p);
                //         continue;
                //     }
                // }
                // if (int(rnd % 10) < prime && (get_clue(p).type.value >= 1))
                // {
                //     tst = *this;
                //     tst->get_clue(p).type.type = RegionType::TRIANGLE;
                //     tst->get_clue(p).type.value -= 1;
                //     if (tst->is_solveable())
                //     {
                //         get_clue(p) = tst->get_clue(p);
                //         continue;
                //     }
                // }
                // if (int(rnd % 10) < prime && (get_clue(p).type.value >= 1))
                // {
                //     tst = *this;
                //     tst->get_clue(p).type.type = RegionType::POW2;
                //     tst->get_clue(p).type.value -= 1;
                //     if (tst->is_solveable())
                //     {
                //         get_clue(p) = tst->get_clue(p);
                //         continue;
                //     }
                // }
                // if (int(rnd % 10) < prime && (get_clue(p).type.value >= 0))
                // {
                //     tst = *this;
                //     tst->get_clue(p).type.type = RegionType::TRIANGLE;
                //     tst->get_clue(p).type.value -= 0;
                //     if (tst->is_solveable())
                //     {
                //         get_clue(p) = tst->get_clue(p);
                //         continue;
                //     }
                // }
            }
            if (xor11)
            {
                if (int(rnd % 10) < xor11 && (get_clue(p).type.value >= 2))
                {
                    tst = *this;
                    tst->get_clue(p).type.type = RegionType::XOR11;
                    tst->get_clue(p).type.value -= 2;
                    if (tst->is_solveable())
                    {
                        get_clue(p) = tst->get_clue(p);
                        continue;
                    }
                }
                if (int(rnd % 10) < xor11 && (get_clue(p).type.value >= 1))
                {
                    tst = *this;
                    tst->get_clue(p).type.type = RegionType::XOR11;
                    tst->get_clue(p).type.value -= 1;
                    if (tst->is_solveable())
                    {
                        get_clue(p) = tst->get_clue(p);
                        continue;
                    }
                }
                if (int(rnd % 10) < xor11)
                {
                    tst = *this;
                    tst->get_clue(p).type.type = RegionType::XOR11;
                    if (tst->is_solveable())
                    {
                        get_clue(p) = tst->get_clue(p);
                        continue;
                    }
                }
            }
            if (xor1)
            {
                if (int(rnd % 10) < xor1 && (get_clue(p).type.value >= 1))
                {
                    tst = *this;
                    tst->get_clue(p).type.type = RegionType::XOR1;
                    tst->get_clue(p).type.value -= 1;
                    if (tst->is_solveable())
                    {
                        get_clue(p) = tst->get_clue(p);
                        continue;
                    }
                }
                if (int(rnd % 10) < xor1)
                {
                    tst = *this;
                    tst->get_clue(p).type.type = RegionType::XOR1;
                    if (tst->is_solveable())
                    {
                        get_clue(p) = tst->get_clue(p);
                        continue;
                    }
                }
            }
            if (x_y_z)
            {
                if (int(rnd % 10) < x_y_z && (get_clue(p).type.value >= 2))
                {
                    tst = *this;
                    tst->get_clue(p).type.type = RegionType::XOR22;
                    tst->get_clue(p).type.value -= 2;
                    if (tst->is_solveable())
                    {
                        get_clue(p) = tst->get_clue(p);
                        continue;
                    }
                }
                if (int(rnd % 10) < x_y_z)
                {
                    tst = *this;
                    tst->get_clue(p).type.type = RegionType::XOR22;
                    if (tst->is_solveable())
                    {
                        get_clue(p) = tst->get_clue(p);
                        continue;
                    }
                }
                if (int(rnd % 10) < x_y_z && (get_clue(p).type.value >= 4))
                {
                    tst = *this;
                    tst->get_clue(p).type.type = RegionType::XOR22;
                    tst->get_clue(p).type.value -= 4;
                    if (tst->is_solveable())
                    {
                        get_clue(p) = tst->get_clue(p);
                        continue;
                    }
                }
            }
            if (x_y3)
            {
                if (int(rnd % 10) < x_y3)
                {
                    tst = *this;
                    tst->get_clue(p).type.type = RegionType::XOR3;
                    if (tst->is_solveable())
                    {
                        get_clue(p) = tst->get_clue(p);
                        continue;
                    }
                }

                if ((int(rnd % 10) < x_y3) && (get_clue(p).type.value >= 3))
                {
                    tst = *this;
                    tst->get_clue(p).type.type = RegionType::XOR3;
                    tst->get_clue(p).type.value -= 3;
                    if (tst->is_solveable())
                    {
                        get_clue(p) = tst->get_clue(p);
                        continue;
                    }
                }
            }

            if (x_y)
            {
                if (int(rnd % 10) < x_y)
                {
                    tst = *this;
                    tst->get_clue(p).type.type = RegionType::XOR2;
                    if (tst->is_solveable())
                    {
                        get_clue(p) = tst->get_clue(p);
                        continue;
                    }
                }

                if ((int(rnd % 10) < x_y) && (get_clue(p).type.value >= 2))
                {
                    tst = *this;
                    tst->get_clue(p).type.type = RegionType::XOR2;
                    tst->get_clue(p).type.value -= 2;
                    if (tst->is_solveable())
                    {
                        get_clue(p) = tst->get_clue(p);
                        continue;
                    }
                }
            }

            if (plus_minus)
            {
                if (int(rnd % 10) < plus_minus)
                {
                    tst = *this;
                    tst->get_clue(p).type.type = RegionType::LESS;
                    if (tst->is_solveable())
                    {
                        get_clue(p) = tst->get_clue(p);
                        while (true)
                        {
                            tst = *this;
                            tst->get_clue(p).type.type = RegionType::LESS;
                            tst->get_clue(p).type.value++;
                            if (!tst->is_solveable())
                                break;
                            if (tst->vals[p].clue.type.value > 19)
                            {
                                assert(0);
                            }
                            //printf("tst->get_clue(p).type.value %d++ \n", tst->get_clue(p).type.value);
                            if (tst->get_clue(p).type.value)
                            {
                                get_clue(p).type.type = RegionType::LESS;
                                get_clue(p).type.value = tst->get_clue(p).type.value;
                            }
                        }
                        continue;
                    }
                    tst = *this;
                    tst->get_clue(p).type.type = RegionType::MORE;
                    if (tst->is_solveable())
                    {
                        get_clue(p) = tst->get_clue(p);
                        while (true)
                        {
                            tst = *this;
                            tst->get_clue(p).type.type = RegionType::MORE;
                            tst->get_clue(p).type.value--;
                            if (!tst->is_solveable())
                                break;
                            printf("tst->get_clue(p).type.value %d-- \n", tst->get_clue(p).type.value);
                            get_clue(p).type.type = RegionType::MORE;
                            get_clue(p).type.value = tst->get_clue(p).type.value;
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
        if((*it).elements.get(p) || (*it).elements_neg.get(p))
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
        if(rp->elements.get(p) || rp->elements_neg.get(p))
        {
            remove_from_regions_to_add_multiset(&(*it));
            it = regions_to_add.erase(it);
        }
        else
            ++it;
    }
    wants_base_regions = true;
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
        if (m_reg.second.is_if_then())
        {
            s += '?';
            if (m_reg.second.if_partition != RegionIfType::ALTERNATING)
            {
                s += '@';
                s += '0' + m_reg.second.if_partition;
                s += 'A' + m_reg.second.if_partition_index;
                if (m_reg.second.if_partition == RegionIfType::ADJACENT_PAIR)
                    s += 'A' + m_reg.second.if_partition_index2;
            }
            s += 'A' + (char)(m_reg.second.if_type.type);
            s += '0' + m_reg.second.if_type.value;
        }
        s += 'A' + m_reg.second.type.type;
        s += '0' + m_reg.second.type.value;
    }

    XYSet grid_squares = get_squares();
    FOR_XY_SET(p, grid_squares)
    {
        GridPlace g = vals[p];
        if (!g.revealed)
        {
            s += '_';
        }
        if (g.negated)
        {
            s += '~';
        }

        if (g.bomb)
        {
            s += '!';
        }
        else
        {
            if (g.clue.is_if_then())
            {
                s += '?';
                if (g.clue.if_partition != RegionIfType::ALTERNATING)
                {
                    s += '@';
                    s += '0' + g.clue.if_partition;
                    s += 'A' + g.clue.if_partition_index;
                    if (g.clue.if_partition == RegionIfType::ADJACENT_PAIR)
                        s += 'A' + g.clue.if_partition_index2;
                }
                s += 'A' + (char)(g.clue.if_type.type);
                s += '0' + g.clue.if_type.value;
            }
            s += 'A' + (char)(g.clue.type.type);
            s += '0' + g.clue.type.value;
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
    assert(region_is_correct(&reg));

    if (regions_set.count(&reg)) 
        return false;
    int cnt = 0;
    const auto [start, end] = regions_to_add_multiset.equal_range(&reg);
    for (auto i{start}; i != end; i++)
    {
        GridRegion& r = **i;
        cnt++;
        // if (cnt > 100)
        //     return false;
        if (r.gen_cause == reg.gen_cause)
        {
            return false;
        }
    }

    // cnt = 0;
    // FOR_XY_SET(p, reg.elements)
    // {
    //     if (vals[p].bomb)
    //     {
    //         if (reg.elements_neg.get(p))
    //             cnt--;
    //         else
    //             cnt++;
    //     }
    // }
    assert(!reg.type.var);
    // assert((reg.type.apply_rule_imp<bool,int>(cnt, reg.type.value)));


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

bool Grid::add_region(XYSet& elements, XYSet& elements_neg, RegionIfType if_clue, XYPos cause)
{
    RegionType& clue = if_clue.type;
    if (!elements.count())
        return false;
    if (elements_neg == elements && clue.type < RegionType::PRIME)
    {
        elements_neg.clear();
        if (clue.type == RegionType::MORE)
            clue.type = RegionType::LESS;
        else if (clue.type == RegionType::LESS)
            clue.type = RegionType::MORE;
        
        if (clue.type == RegionType::XOR2)
            clue.value = -clue.value - 2;
        else if (clue.type == RegionType::XOR22)
            clue.value = -clue.value - 4;
        else if (clue.type == RegionType::XOR3)
            clue.value = -clue.value - 3;
        else if (clue.type == RegionType::XOR1)
            clue.value = -clue.value - 1;
        else if (clue.type == RegionType::XOR11)
            clue.value = -clue.value - 2;
        else if (clue.type == RegionType::PARITY)
        {
            if      (clue.value == -0) { clue.value = 0; clue.type = RegionType::EQUAL; }
            else if (clue.value == -1) { clue.value = 1; clue.type = RegionType::EQUAL; }
            else if (clue.value == -2) { clue.value = 0; clue.type = RegionType::XOR2; }
            else if (clue.value == -3) { clue.value = 1; clue.type = RegionType::XOR2; }
            else if (clue.value == -4) { clue.value = 0; clue.type = RegionType::XOR22; }
            else if (clue.value == -5) { clue.value = 1; clue.type = RegionType::XOR22; }
            else elements_neg = elements;
        }
        else
            clue.value = -clue.value;
    }
    if (elements_neg.any() && !if_clue.is_if_then())
    {
        if (clue.value < 0 && clue.type == RegionType::EQUAL)
        {
            clue.value = -clue.value;
            elements_neg = elements & ~elements_neg;
        }
        if (clue.value < 0 && clue.type == RegionType::NOTEQUAL)
        {
            clue.value = -clue.value;
            elements_neg = elements & ~elements_neg;
        }
        if (clue.value < 0 && clue.type == RegionType::MORE)
        {
            clue.value = -clue.value;
            elements_neg = elements & ~elements_neg;
            clue.type = RegionType::LESS;
        }
        if (clue.value < 0 && clue.type == RegionType::LESS)
        {
            clue.value = -clue.value;
            elements_neg = elements & ~elements_neg;
            clue.type = RegionType::MORE;
        }
        if (clue.value <= -2 && clue.type == RegionType::XOR2)
        {
            clue.value = -clue.value - 2;
            elements_neg = elements & ~elements_neg;
        }
        if (clue.value <= -4 && clue.type == RegionType::XOR22)
        {
            clue.value = -clue.value - 4;
            elements_neg = elements & ~elements_neg;
        }
        if (clue.value <= -3 && clue.type == RegionType::XOR3)
        {
            clue.value = -clue.value - 3;
            elements_neg = elements & ~elements_neg;
        }
        if (clue.value <= -1 && clue.type == RegionType::XOR1)
        {
            clue.value = -clue.value - 1;
            elements_neg = elements & ~elements_neg;
        }
        if (clue.value <= -2 && clue.type == RegionType::XOR11)
        {
            clue.value = -clue.value - 2;
            elements_neg = elements & ~elements_neg;
        }
    }
    else
    {
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
        if (!if_clue.is_if_then() && clue.type != RegionType::PRIME && clue.type != RegionType::TRIANGLE && clue.type != RegionType::POW2 && clue.type != RegionType::FIBONACCI && clue.type != RegionType::BOX)
            assert (clue.value >= 0);
    }
    GridRegion reg(if_clue);
    reg.elements = elements;
    reg.elements_neg = elements_neg;
    if (cell_causes.count(cause))
    {
        reg.gen_cause = cell_causes[cause];
    }
    reg.gen_cause_pos = cause;
    reg.priority = 3;
    return add_region(reg, true);
}

void Grid::add_base_regions(void)
{
    if (!wants_base_regions)
        return;
    wants_base_regions = false;
    for (const auto &edg : edges)
    {
        XYPos e_pos = edg.first;
        RegionIfType clue = edg.second;
        if (clue.type.type == RegionType::NONE)
            continue;
        XYSet line = get_row(e_pos.x, e_pos.y);
        XYSet elements;
        XYSet elements_neg;
        unsigned partition_index = 0;
        FOR_XY_SET(n, line)
        {
            bool in_if = is_if_partition_cell(clue, partition_index++);
            if (!get(n).revealed)
            {
                if (clue.is_if_then() && in_if)
                    elements_neg.set(n);
                else
                    elements.set(n);
                if (get(n).negated)
                    elements_neg.set(n);
            }
            else if (get(n).bomb)
            {
                if (get(n).negated)
                    clue.type.value++;
                else
                {
                    if (clue.is_if_then() && in_if)
                        clue.if_type.value--;
                    else
                        clue.type.value--;
                }
            }
        }
        if (clue.is_if_then() && elements_neg.empty())
        {
            if (clue.if_type.apply_int_rule(0))
            {
                clue.if_type = RegionType();
            }
            else
                continue;
        }
        add_region(elements, elements_neg, clue, XYPos(e_pos.x + 1000,e_pos.y));
    }

    XYSet grid_squares = get_squares();
    FOR_XY_SET(p, grid_squares)
    {
        XYSet elements;
        XYSet elements_neg;
        GridPlace g = vals[p];
        if (g.revealed && !g.bomb && ((g.clue.type.type != RegionType::NONE)))
        {
            RegionIfType clue = g.clue;
            if (!vals[p].bomb)
            {
                XYSet neigh = get_neighbors(p);
                unsigned partition_index = 0;
                FOR_XY_SET(n, neigh)
                {
                    bool in_if = is_if_partition_cell(clue, partition_index++);
                    if (!get(n).revealed)
                    {
                        if (clue.is_if_then() && in_if)
                            elements_neg.set(n);
                        else
                            elements.set(n);
                        if (get(n).negated)
                            elements_neg.set(n);
                    }
                    else if (get(n).bomb)
                    {
                        if (clue.is_if_then() && in_if)
                        {
                            clue.if_type.value--;
                        }
                        else
                        {
                            if (get(n).negated)
                                clue.type.value++;
                            else
                                clue.type.value--;
                        }
                    }
                }
            }
            if (clue.is_if_then() && elements_neg.empty())
            {
                if (clue.if_type.apply_int_rule(0))
                {
                    clue.if_type = RegionType();
                }
                else
                    continue;
            }
            add_region(elements, elements_neg, clue, p);
        }
    }
    return;
}

static void add_clear_count(GridRegion* region, std::set<GridRule*>& rules_to_credit, std::set<GridRegion*>& regions_to_credit)
{
    if (!region)
        return;
    if (regions_to_credit.count(region))
        return;
    regions_to_credit.insert(region);
    if (region->gen_cause.rule && region->gen_cause.rule->apply_region_type.type != RegionType::SET)
    {
        rules_to_credit.insert(region->gen_cause.rule);
        for (int i = 0; i < region->gen_cause.rule->region_count; i++)
        {
            if (region->gen_cause.regions[i])
            {
                add_clear_count(region->gen_cause.regions[i], rules_to_credit, regions_to_credit);
            }
        }
    }
}

Grid::ApplyRuleResp Grid::apply_rule(GridRule& rule, GridRegion* r[4], int var_counts[32], bool update_stats)
{
    if (rule.paused)
        return APPLY_RULE_RESP_NONE;
    if (rule.deleted)
        return APPLY_RULE_RESP_NONE;
    assert(rule.apply_region_bitmap);
    const GridRegionCause cause =
        rule.make_cause(r[0], r[1], r[2], r[3]);
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
                    r[i]->vis_cause = cause;
                    if (update_stats)
                        level_used_count[&rule]++;
                }
            }
        }
        return APPLY_RULE_RESP_NONE;
    }

    XYSet to_reveal;
    XYSet neg_to_reveal;
    if (rule.if_reg_count)
    {
        for (int i = 0; i < 16; i++)
        {
            XYSet s = (i & 1) ? r[0]->elements_neg : ~r[0]->elements_neg;
            s &= ((i & 2) ? r[0]->elements : ~r[0]->elements);
            if (rule.if_reg_count == 2)
            {
                assert(r[2]);
                s &= (i & 4) ? r[2]->elements_neg : ~r[2]->elements_neg;
                s &= (i & 8) ? r[2]->elements : ~r[2]->elements;
            }
            else
            {
                if (r[2])
                    s &= ((i & 4) ? r[2]->elements : ~r[2]->elements);
                if (r[3])
                    s &= ((i & 8) ? r[3]->elements : ~r[3]->elements);
            }
            if ((rule.apply_region_bitmap >> i) & 1)
                to_reveal |= s;
            if ((rule.neg_apply_region_bitmap >> i) & 1)
                neg_to_reveal |= s;
        }
    }
    else
    {
        for (int i = 0; i < 16; i++)
        {
            XYSet s = (i & 1) ? r[0]->elements : ~r[0]->elements;
            if (rule.neg_reg_count == 1)
                s &= ((i & 8) ? r[0]->elements_neg : ~r[0]->elements_neg);
            if (rule.neg_reg_count == 2)
                s &= ((i & 4) ? r[0]->elements_neg : ~r[0]->elements_neg);
            if (r[1])
                s &= ((i & 2) ? r[1]->elements : ~r[1]->elements);
            if (rule.neg_reg_count == 2)
                s &= ((i & 8) ? r[1]->elements_neg : ~r[1]->elements_neg);
            if (r[2])
                s &= ((i & 4) ? r[2]->elements : ~r[2]->elements);
            if (r[3])
                s &= ((i & 8) ? r[3]->elements : ~r[3]->elements);
            if ((rule.apply_region_bitmap >> i) & 1)
                to_reveal |= s;
            if ((rule.neg_apply_region_bitmap >> i) & 1)
                neg_to_reveal |= s;
        }
    }
    if (to_reveal.empty())
        return APPLY_RULE_RESP_NONE;
    if (rule.apply_if_region_type.type != RegionType::NONE && neg_to_reveal.empty())
        return APPLY_RULE_RESP_NONE;


    if (rule.apply_region_type.type == RegionType::SET)
    {
        assert(neg_to_reveal.none());
        FOR_XY_SET(pos, to_reveal)
        {
            if (vals[pos].bomb != bool(rule.apply_region_type.value))
            {
                printf("wrong\n");
                assert(0);
                exit(1);
            }
        }
        int c = 0;
        FOR_XY_SET(pos, to_reveal)
        {
            reveal(pos);
            cell_causes[pos] = cause;
            c++;
        }
        assert(c);
        last_cleared_regions = to_reveal;
        level_used_count[&rule]++;
        std::set<GridRule*> rules_to_credit;
        std::set<GridRegion*> regions_to_credit;
        rules_to_credit.insert(&rule);
        for (int i = 0; i < rule.region_count; i++)
            add_clear_count(r[i], rules_to_credit, regions_to_credit);
        for (GridRule* rule : rules_to_credit)
            level_clear_count[rule] += c;
        return APPLY_RULE_RESP_HIT;
    }
    else
    {
        RegionType typ = rule.apply_region_type;
        RegionType if_typ = rule.apply_if_region_type;
        if (typ.var)
        {
            assert(var_counts[typ.var - 1] >= 0);
            typ.value += var_counts[typ.var - 1];
            typ.var = false;
            if (typ.value > 32)
                return APPLY_RULE_RESP_NONE;
            if (typ.value < -32)
                return APPLY_RULE_RESP_NONE;
        }
        GridRegion reg(typ);
        if (if_typ.var)
        {
            assert(var_counts[if_typ.var - 1] >= 0);
            if_typ.value += var_counts[if_typ.var - 1];
            if_typ.var = false;
            if (if_typ.value > 32)
                return APPLY_RULE_RESP_NONE;
            if (if_typ.value < -32)
                return APPLY_RULE_RESP_NONE;
        }
        reg.if_type = if_typ;

        reg.elements = to_reveal;
        reg.elements_neg =  neg_to_reveal;
        reg.gen_cause = cause;
        float f = 0;
        for (int i = 0; i < rule.region_count; i++)
        {
            if (r[i])
                f += r[i]->priority;
        }
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

// static bool are_connected_old(GridRegion* r0, GridRegion* r1, GridRegion* r2, GridRegion* r3)
// {
//     XYSet s = r0->elements;
//     unsigned int connected = 1 << 0;
//     bool hit;
//     do
//     {
//         hit = false;
//         if (r1 && !(connected & (1 << 1)) && s.overlaps(r1->elements))
//         {
//             connected |= (1 << 1);
//             s = s | r1->elements;
//             hit = true;
//         }
//         if (r2 && !(connected & (1 << 2)) && s.overlaps(r2->elements))
//         {
//             connected |= (1 << 2);
//             s = s | r2->elements;
//             hit = true;
//         }
//         if (r3 && !(connected & (1 << 3)) && s.overlaps(r3->elements))
//         {
//             connected |= (1 << 3);
//             s = s | r3->elements;
//             hit = true;
//         }
//     }
//     while (hit);
//     if (!r1)
//         return true;
//     if (!r2 && connected == 3)
//         return true;
//     if (!r3 && connected == 7)
//         return true;
//     if (connected == 0xf)
//         return true;
//     return false;
// }

static bool are_connected(GridRegion* r0, GridRegion* r1, GridRegion* r2, GridRegion* r3)
{
    if (!r1 && !r2)
        return true;

    if (r2)
        if (r2->is_if_then())
            return ((r0->elements | r0->elements_neg).overlaps(r2->elements | r2->elements_neg));

    if (r0->is_if_then())
    {
        assert(!r1);
        if (!r2)
            return true;
        XYSet s = r0->elements | r0->elements_neg;
        bool r2_overlaps = s.overlaps(r2->elements);
        if (!r3)
            return r2_overlaps;
        if (r2_overlaps)
            return (s | r2->elements).overlaps(r3->elements);
        return s.overlaps(r3->elements) && r2->elements.overlaps(r3->elements);
    }

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

Grid::ApplyRuleResp Grid::apply_rule(GridRule& rule, GridRegion* unstale_region, bool update_stats)
{
    if (rule.deleted)
        return APPLY_RULE_RESP_NONE;

    assert(rule.region_count);
    unsigned places_for_reg = 0;
    if (unstale_region)
    {
        for (int i = 0; i < rule.region_count; i++)
        {
            if ((rule.if_reg_count >= 1 && i == 1) || (rule.if_reg_count >= 2 && i == 3))
                continue;
            bool has_if_then = unstale_region->is_if_then();
            bool want_if_then = (rule.if_reg_count >= 1 && i == 0) || (rule.if_reg_count >= 2 && i == 2);
            bool has_neg = unstale_region->elements_neg.any();
            bool want_neg = (i < rule.neg_reg_count) || want_if_then;

            if (has_neg != want_neg)
                continue;

            if (has_if_then != want_if_then)
                continue;

            if (has_if_then)
            {
                if (unstale_region->if_type == rule.region_type[i] || rule.region_type[i].type == RegionType::NONE || (rule.region_type[i].var && (unstale_region->if_type.type == rule.region_type[i].type)))
                    if (unstale_region->type == rule.region_type[i+1] || rule.region_type[i+1].type == RegionType::NONE || (rule.region_type[i+1].var && (unstale_region->type.type == rule.region_type[i+1].type)))
                        places_for_reg |= 1 << i;
            }
            else
            {
                if (unstale_region->type == rule.region_type[i] || rule.region_type[i].type == RegionType::NONE || (rule.region_type[i].var && (unstale_region->type.type == rule.region_type[i].type)))
                    places_for_reg |= 1 << i;
            }
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
        else if ((rule.if_reg_count >= 1 && i == 1) || (rule.if_reg_count >= 2 && i == 3))
            pos_regions[i].push_back(NULL);
        else
        {
            for (GridRegion& r : regions)
            {

                if ((r.vis_level == GRID_VIS_LEVEL_BIN) && (rule.apply_region_type.type != RegionType::VISIBILITY))
                    continue;
                if ((r.vis_level == GRID_VIS_LEVEL_BIN) && (r.visibility_force == GridRegion::VIS_FORCE_USER))
                    continue;
                bool has_if_then = r.is_if_then();
                bool want_if_then = (rule.if_reg_count >= 1 && i == 0) || (rule.if_reg_count >= 2 && i == 2);
                if (has_if_then != want_if_then)
                    continue;
                if (want_if_then)
                {
                    if (r.if_type != rule.region_type[i] && rule.region_type[i].type != RegionType::NONE && !(rule.region_type[i].var && (r.if_type.type == rule.region_type[i].type)))
                        continue;
                    if (r.type != rule.region_type[i+1] && rule.region_type[i+1].type != RegionType::NONE && !(rule.region_type[i+1].var && (r.type.type == rule.region_type[i+1].type)))
                        continue;
                }
                else
                {
                    bool has_neg = r.elements_neg.any();
                    bool want_neg = (i < rule.neg_reg_count);
                    if (has_neg != want_neg)
                        continue;
                    if (r.type != rule.region_type[i] && rule.region_type[i].type != RegionType::NONE && !(rule.region_type[i].var && (r.type.type == rule.region_type[i].type)))
                        continue;
                }
                pos_regions[i].push_back(&r);
            }
        }
    }

    std::vector<GridRegion*> unstale_regions;
    unstale_regions.push_back(unstale_region);
    ApplyRuleResp rep = APPLY_RULE_RESP_NONE;

    GridRule::FastOpGroup fast_ops;

    rule.jit_preprocess(fast_ops);
    int var_counts[32];
    for (int i = 0; i < 32; i++)
        var_counts[i] = -1;

    for (int nonstale_rep_index = 0; nonstale_rep_index < rule.region_count; nonstale_rep_index++)
    {
        if ((places_for_reg >> nonstale_rep_index) & 1)
        {
            std::vector<GridRegion*>& set0 = (unstale_region && (nonstale_rep_index == 0)) ? unstale_regions : pos_regions[0];
            for (GridRegion* r0 : set0)
            {
                if (!rule.jit_matches(fast_ops.ops[0], (rule.region_count == 1), r0, NULL, NULL, NULL, var_counts))
                    continue;
                std::vector<GridRegion*>& set1 = (nonstale_rep_index == 1) ? unstale_regions : pos_regions[1];
                for (GridRegion* r1 : set1)
                {
                    if (r0 == r1) continue;
                    if (!rule.jit_matches(fast_ops.ops[1], (rule.region_count == 2), r0, r1, NULL, NULL, var_counts))
                        continue;
                    std::vector<GridRegion*>& set2 = (nonstale_rep_index == 2) ? unstale_regions : pos_regions[2];
                    for (GridRegion* r2 : set2)
                    {
                        if (r2 && ((r0 == r2) || (r1 == r2))) continue;
                        if (!rule.jit_matches(fast_ops.ops[2], (rule.region_count == 3), r0, r1, r2, NULL, var_counts))
                            continue;
                        std::vector<GridRegion*>& set3 = (nonstale_rep_index == 3) ? unstale_regions : pos_regions[3];
                        for (GridRegion* r3 : set3)
                        {
                            if (r3 && ((r0 == r3) || (r1 == r3) || (r2 == r3))) continue;
                            bool m = rule.jit_matches(fast_ops.ops[3], (rule.region_count == 4), r0, r1, r2, r3, var_counts);
                            if (!m)
                                continue;
                            // int var_counts2[32];
                            // for (int i = 0; i < 32; i++)
                            //     var_counts2[i] = -1;
                            // bool m2 = rule.matches(r0, r1, r2, r3, var_counts2);
                            // if (!m2)
                            //     continue;
                            // for (int i = 0; i < 32; i++)
                            // {
                            //     if (var_counts2[i] >= 0)
                            //         assert(var_counts2[i] == var_counts[i]);
                            // }
                            // if (m != m2)
                            // {
                            //     for (int i = 0; i < 32; i++)
                            //         var_counts2[i] = -1;
                            //     m2 = rule.matches(r0, r1, r2, r3, var_counts2);
                            //     m = rule.jit_matches(fast_ops.ops[3], (rule.region_count == 4), r0, r1, r2, r3, var_counts);
                            //     assert(0);
                            // }
                            {
                                // for (int i = 0; i < 32; i++)
                                //     var_counts[i] = -1;
                                // assert(rule.matches(r0, r1, r2, r3, var_counts));
                                if (!are_connected(r0, r1, r2, r3)) continue;
                                GridRegion* regions[4] = {r0, r1, r2, r3};
                                ApplyRuleResp resp = apply_rule(rule, regions, var_counts, update_stats);
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
        regions_set.insert(&r);
    }
    regions.splice(regions.end(), regions_to_add);
    regions_to_add_multiset.clear();
}

bool Grid::region_is_correct(GridRegion* r)
{
    unsigned bombs = 0;
    unsigned if_bombs = 0;
    bool if_type = (r->if_type.type != RegionType::NONE);
    FOR_XY_SET(pos, r->elements_neg)
    {
        if (vals[pos].bomb)
            if_bombs++;
    }
    FOR_XY_SET(pos, r->elements)
    {
        // if (!r->gen_cause.rule)
        //     assert(get(pos).negated == r->elements_neg.get(pos));
        if (vals[pos].bomb)
        {
            if (r->elements_neg.get(pos))
            {
                if (if_type)
                    bombs++;
                else
                    bombs--;
            }
            else
                bombs++;
        }
    }
   
    bool valid = r->type.apply_int_rule(bombs);
    if (if_type)
    {
        if (!r->if_type.apply_int_rule(if_bombs))
            valid = true;
    }
    assert(valid);
    return valid;
}

GridRegion* Grid::add_one_new_region(GridRegion* ancestor, const XYSet& filter_pos_and, const XYSet& filter_pos_not)
{
    for (GridRegion& r : regions)
    {
        if (!r.stale)
        {
            r.stale = true;
            return &r;
        }
    }
    if (regions_to_add.empty())
        return NULL;

    std::list<GridRegion>::iterator it = regions_to_add.begin();
    while (it != regions_to_add.end())
    {
        GridRegionCause c = (*it).gen_cause;
        bool del = false;

        if (regions_set.count(&*it))
            del = true;
        if (c.rule && c.rule->paused)
            del = true;
        if ((c.rule && c.rule->apply_region_type.type != RegionType::SET) &&
            ((c.regions[0] && (c.regions[0]->vis_level == GRID_VIS_LEVEL_BIN || c.regions[0]->deleted)) ||
             (c.regions[1] && (c.regions[1]->vis_level == GRID_VIS_LEVEL_BIN || c.regions[1]->deleted)) ||
             (c.regions[2] && (c.regions[2]->vis_level == GRID_VIS_LEVEL_BIN || c.regions[2]->deleted)) ||
             (c.regions[3] && (c.regions[3]->vis_level == GRID_VIS_LEVEL_BIN || c.regions[3]->deleted))))
            del = true;
        if (del)
        {
            remove_from_regions_to_add_multiset(&(*it));
            it = regions_to_add.erase(it);
        }
        else
            it++;
    }

    if (regions_to_add.empty())
        return NULL;

    it = regions_to_add.begin();
    std::list<GridRegion>::iterator best_reg = regions_to_add.begin();
    float best_pri = (*best_reg).priority;
    std::set<GridRegion*> has, hasnt;

    while (it != regions_to_add.end())
    {
        float pri = (*it).priority;
        if (ancestor && (*it).has_ancestor(ancestor, has, hasnt))
            pri += 10;
        if (it->matches_filters(filter_pos_and, filter_pos_not))
            pri += 10;
        if (!(*it).gen_cause.rule || (*it).gen_cause.rule->apply_region_type.type == RegionType::SET)
            pri += 30;
        if (pri > best_pri)
        {
            best_pri = pri;
            best_reg = it;
        }
        it++;
    }
    assert(region_is_correct(&*best_reg));
    if ((*best_reg).gen_cause.rule && (*best_reg).gen_cause.rule->apply_region_type.type != RegionType::SET)
        level_used_count[(*best_reg).gen_cause.rule]++;

    remove_from_regions_to_add_multiset(&(*best_reg));
    regions_set.insert(&(*best_reg));
    regions.splice(regions.end(), regions_to_add, best_reg);
    return &*best_reg;
}

void Grid::clear_regions()
{
    regions.clear();
    regions_set.clear();
    regions_to_add.clear();
    regions_to_add_multiset.clear();
    deleted_regions.clear();
    cell_causes.clear();
    last_cleared_regions.clear();
    wants_base_regions = true;
}

void Grid::commit_level_counts()
{
    for (auto [rule, count] : level_used_count)
        rule->used_count += count;
    for (auto [rule, count] : level_clear_count)
        rule->clear_count += count;
}

void Grid::remove_from_regions_to_add_for_rule(GridRule* rule)
{
    std::list<GridRegion>::iterator it = regions_to_add.begin();
    while (it != regions_to_add.end())
    {
        GridRegion* rp = &(*it);
        if(rp->gen_cause.rule == rule)
        {
            remove_from_regions_to_add_multiset(&(*it));
            it = regions_to_add.erase(it);
        }
        else
            ++it;
    }
}

bool Grid::uses_neg_bombs()
{
    XYSet grid_squares = get_squares();
    FOR_XY_SET(p, grid_squares)
    {
        if (vals[p].negated && !vals[p].revealed)
            return true;
    }
    return false;
}

bool Grid::uses_if_then_clues()
{
    for (const auto& [pos, clue] : edges)
        if (clue.is_if_then())
            return true;

    for (const auto& [pos, place] : vals)
        if (place.revealed && !place.bomb && place.clue.is_if_then())
            return true;

    return false;
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

XYPos SquareGrid::get_grid_size(XYPos grid_pitch)
{
    return size * grid_pitch;
}

XYPos SquareGrid::get_pos_from_mouse_pos(XYPos pos, XYPos grid_pitch)
{
    if (grid_pitch.x < 16)
        return XYPos(-1, -1);
    if (grid_pitch.y < 16)
        return XYPos(-1, -1);
    if (wrapped == Grid::WRAPPED_SIDE)
    {
        pos += grid_pitch * size * 100;
        pos = pos % (grid_pitch * size);
    }
    XYPos r(pos / grid_pitch);
    if (!r.inside(size))
        return pos;
    r = get_base_square(r);
    if (wrapped == WRAPPED_IN && r == innie_pos)
    {
        pos = get_pos_from_mouse_pos(pos - innie_pos * grid_pitch, (grid_pitch * get_square_size(innie_pos)) / size);
        if (pos.x >= 0 && pos.y >= 0)
            return (pos * size) / get_square_size(innie_pos);
        return XYPos(-1,-1);
    }

    return pos;
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
    return XYPos(floor(s / 2.0), floor(std::sqrt(3) * s / 2.0));
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
            if (gpos.y >= (int)s)
                ofst = (gpos.y - s + 1);

            if ((int)index < (w - ofst * 2))
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

XYPos TriangleGrid::get_grid_size(XYPos grid_pitch)
{
    return (size + XYPos(1, 0)) * grid_pitch;
}

XYPos TriangleGrid::get_pos_from_mouse_pos(XYPos p, XYPos grid_pitch)
{
    if (wrapped == Grid::WRAPPED_SIDE)
    {
        p += get_wrapped_size(grid_pitch) * 100;
        p = p % get_wrapped_size(grid_pitch);
    }
    XYPos pos = p;
    if (grid_pitch.x < 16)
        return XYPos(-1,-1);
    if (grid_pitch.y < 16)
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
    if (!is_inside(rep))
        return p;

    rep = get_base_square(rep);
    if (wrapped == WRAPPED_IN && rep == innie_pos)
    {
        p = get_pos_from_mouse_pos(pos - innie_pos * grid_pitch, (grid_pitch * (get_square_size(innie_pos) + XYPos(1,0))) / size);
        if (p.x >= 0 && p.y >= 0)
            return p  * size / (get_square_size(innie_pos) + XYPos(1,0));
        return XYPos(-1,-1);
    }

    return p;
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
        if (gpos.y >= (int)s)
            ofst = (gpos.y - s + 1);

        if ((int)index < (w - ofst * 2))
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

XYPos HexagonGrid::get_grid_size(XYPos grid_pitch)
{
    return XYPos(size.x * 3 + 1, size.y * 2 + 1) * grid_pitch;
}

XYPos HexagonGrid::get_pos_from_mouse_pos(XYPos pos, XYPos grid_pitch)
{
    if (wrapped == Grid::WRAPPED_SIDE)
    {
        pos += get_wrapped_size(grid_pitch) * 100;
        pos = pos % get_wrapped_size(grid_pitch);
    }
    return pos;
}

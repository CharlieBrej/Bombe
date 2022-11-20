#include "z3++.h"


#include "Grid.h"
#include <sstream>


static std::random_device rd;
static Rand rnd(rd());
//static Rand rnd(1);

static std::map<int,int> colours_used;

void grid_set_rnd(int a)
{
    rnd.gen.seed(a);
}

z3::expr RegionType::apply_z3_rule(z3::expr in)
{
    if (type == EQUAL)
    {
        return in == value;
    }
    if (type == LESS)
    {
        return in <= value;
    }
    if (type == MORE)
    {
        return in >= value;
    }
    assert(0);
}

void GridRegion::reset(RegionType type_)
{
    type = type_;
    fade = 0;
    colour = colours_used[type.value]++;
    hidden = false;
    visibility_forced = false;
    global = false;
    elements.clear();
}

bool GridRegion::overlaps(GridRegion& other)
{
    for (const XYPos& e : elements)
    {
        if (other.elements.contains(e))
            return true;
    }
    return false;
}

GridRule::GridRule(SaveObject* sobj)
{
    SaveObjectMap* omap = sobj->get_map();
    region_count = omap->get_num("region_count");
    apply_type = ApplyType(omap->get_num("apply_type"));
    apply_region_type = RegionType('a', omap->get_num("apply_region_type"));
    apply_region_bitmap = omap->get_num("apply_region_bitmap");

    SaveObjectList* rlist = omap->get_item("region_type")->get_list();
    for (int i = 0; i < rlist->get_count(); i++)
        region_type[i] = RegionType('a', rlist->get_num(i));

    rlist = omap->get_item("square_counts")->get_list();
    for (int i = 0; i < rlist->get_count(); i++)
        square_counts[i] = rlist->get_num(i);
}

SaveObject* GridRule::save()
{
    SaveObjectMap* omap = new SaveObjectMap;
    omap->add_num("region_count", region_count);
    omap->add_num("apply_type", apply_type);
    omap->add_num("apply_region_type", apply_region_type.as_int());
    omap->add_num("apply_region_bitmap", apply_region_bitmap);

    SaveObjectList* region_type_list = new SaveObjectList;
    for (int i = 0; i < 3; i++)
        region_type_list->add_num(region_type[i].as_int());
    omap->add_item("region_type", region_type_list);

    SaveObjectList* square_counts_list = new SaveObjectList;
    for (int i = 0; i < 8; i++)
        square_counts_list->add_num(square_counts[i]);
    omap->add_item("square_counts", square_counts_list);

    return omap;
}

bool GridRule::matches(GridRule& other)
{
    if (region_count != other.region_count)
        return false;
    if ((square_counts[1] >= 0) && (square_counts[1] != other.square_counts[1]))
        return false;
    if (region_type[0] != other.region_type[0])
        return false;
    if (region_count >= 2)
    {
        if ((square_counts[2] >= 0) && (square_counts[2] != other.square_counts[2]))
            return false;
        if ((square_counts[3] >= 0) && (square_counts[3] != other.square_counts[3]))
            return false;
        if (region_type[1] != other.region_type[1])
            return false;
    }
    if (region_count == 3)
    {
        if (region_type[2] != other.region_type[2])
            return false;
        for (int i = 4; i < 8; i++)
        {
            if ((square_counts[i] >= 0) && (square_counts[i] != other.square_counts[i]))
                return false;
        }
    }
    return true;
}

void GridRule::import_rule_gen_regions(GridRegion* r1, GridRegion* r2, GridRegion* r3)
{
    for (int i = 0; i < 8; i++)
        square_counts[i] = 0;
    if (r1)
    {
        region_count = 1;
        region_type[0] = r1->type;
        square_counts[1] = r1->elements.size();
    }
    if (r2)
    {
        region_count = 2;
        region_type[1] = r2->type;
        square_counts[2] = r2->elements.size();
        for (XYPos pos : r2->elements)
        {
            if (r1->elements.count(pos))
            {
                square_counts[1]--;
                square_counts[2]--;
                square_counts[3]++;
            }
        }
    }
    if (r3)
    {
        region_count = 3;
        region_type[2] = r3->type;
        square_counts[4] = r3->elements.size();

        for (XYPos pos : r3->elements)
        {
            if (r1->elements.count(pos) && r2->elements.count(pos))
            {
                square_counts[3]--;
                square_counts[4]--;
                square_counts[7]++;
            }
            else if (r1->elements.count(pos))
            {
                square_counts[1]--;
                square_counts[4]--;
                square_counts[5]++;
            }
            else if (r2->elements.count(pos))
            {
                square_counts[2]--;
                square_counts[4]--;
                square_counts[6]++;
            }
        }
    }
}

bool GridRule::is_legal()
{
    if (apply_type == HIDE || apply_type == SHOW)
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
        if (int(square_counts[i]) >= 0)
            s.add(vec[i] <= int(square_counts[i]));
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


    z3::expr e = c.int_val(0);
    int tot = 0;

    if (!apply_region_bitmap)
        return false;

    for (int i = 1; i < (1 << region_count); i++)
    {
        if ((apply_region_bitmap >> i) & 1)
        {
            e = e + vec[i];
            if ((square_counts[i] < 0) && (apply_type == BOMB))
                return false;
            tot += square_counts[i];
        }
    }

    switch(apply_type)
    {
        case REGION:
            s.add(!apply_region_type.apply_z3_rule(e));
            break;
        case BOMB:
            s.add(e != tot);
            break;
        case CLEAR:
            s.add(e != 0);
            break;
        case HIDE:
            return true;
        case SHOW:
            return true;
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

Grid::Grid(XYPos size_)
{
  size = size_;
  FOR_XY(p, XYPos(), size)
  {
    vals[p] = GridPlace((unsigned(rnd)%100) < 40, true);
  }

  FOR_XY(p, XYPos(), size)
  {
    if (!vals[p].bomb)
    {
        int cnt = 0;
        cnt += get(p + XYPos(-1,-1)).bomb;
        cnt += get(p + XYPos( 0,-1)).bomb;
        cnt += get(p + XYPos( 1,-1)).bomb;
        cnt += get(p + XYPos(-1, 0)).bomb;
        cnt += get(p + XYPos( 1, 0)).bomb;
        cnt += get(p + XYPos(-1, 1)).bomb;
        cnt += get(p + XYPos( 0, 1)).bomb;
        cnt += get(p + XYPos( 1, 1)).bomb;
        vals[p].clue.type = RegionType::EQUAL;
        vals[p].clue.value = cnt;
    }
  }
}

Grid::Grid()
{
}

void Grid::print(void)
{
  XYPos p;
  for (p.y = 0; p.y < size.y; p.y++)
  {
    for (p.x = 0; p.x < size.x; p.x++)
    {
        GridPlace gp = vals[p];
        if (gp.revealed)
        {
            if (gp.bomb)
                std::cout << "B";
            else
            {
                if (gp.clue.type == RegionType::NONE)
                    std::cout << '.';
                else
                    std::cout << (int)gp.clue.value;
            }
        }
        else
        {
            std::cout << " ";
        }
    }
    std::cout << "\n";
  }
  std::cout << "\n";
}

GridPlace Grid::get(XYPos p)
{
    if (p.inside(size))
        return vals[p];
    return GridPlace(false, true);
}

static std::list<GridRule> global_rules;

void Grid::solve_easy()
{
    if (global_rules.empty())
    {
        std::string sin = "[{\"apply_region_bitmap\":2,\"apply_region_type\":0,\"apply_type\":1,\"square_counts\":[0,1,0,0,0,0,0,0],\"region_count\":1,\"region_type\":[1,0,0]},{\"apply_region_bitmap\":2,\"apply_region_type\":0,\"apply_type\":2,\"square_counts\":[0,-1,0,0,0,0,0,0],\"region_count\":1,\"region_type\":[0,0,0]},{\"apply_region_bitmap\":2,\"apply_region_type\":0,\"apply_type\":1,\"square_counts\":[0,3,0,0,0,0,0,0],\"region_count\":1,\"region_type\":[3,0,0]},{\"apply_region_bitmap\":2,\"apply_region_type\":0,\"apply_type\":1,\"square_counts\":[0,2,0,0,0,0,0,0],\"region_count\":1,\"region_type\":[2,0,0]},{\"apply_region_bitmap\":2,\"apply_region_type\":0,\"apply_type\":2,\"square_counts\":[0,-1,0,0,0,0,0,0],\"region_count\":1,\"region_type\":[256,0,0]},{\"apply_region_bitmap\":2,\"apply_region_type\":0,\"apply_type\":2,\"square_counts\":[0,-1,0,-1,0,0,0,0],\"region_count\":2,\"region_type\":[2,2,0]},{\"apply_region_bitmap\":2,\"apply_region_type\":0,\"apply_type\":2,\"square_counts\":[0,-1,0,-1,0,0,0,0],\"region_count\":2,\"region_type\":[1,1,0]},{\"apply_region_bitmap\":2,\"apply_region_type\":513,\"apply_type\":0,\"square_counts\":[0,-1,-1,-1,0,0,0,0],\"region_count\":2,\"region_type\":[2,257,0]},{\"apply_region_bitmap\":2,\"apply_region_type\":513,\"apply_type\":1,\"square_counts\":[0,1,-1,-1,0,0,0,0],\"region_count\":1,\"region_type\":[513,257,0]},{\"apply_region_bitmap\":2,\"apply_region_type\":513,\"apply_type\":2,\"square_counts\":[0,-1,0,-1,0,0,0,0],\"region_count\":2,\"region_type\":[257,1,0]},{\"apply_region_bitmap\":2,\"apply_region_type\":513,\"apply_type\":1,\"square_counts\":[0,4,0,-1,0,0,0,0],\"region_count\":1,\"region_type\":[4,1,0]},{\"apply_region_bitmap\":2,\"apply_region_type\":1,\"apply_type\":0,\"square_counts\":[0,-1,0,-1,0,0,0,0],\"region_count\":2,\"region_type\":[2,1,0]},{\"apply_region_bitmap\":2,\"apply_region_type\":513,\"apply_type\":0,\"square_counts\":[0,-1,0,-1,0,0,0,0],\"region_count\":2,\"region_type\":[3,258,0]},{\"apply_region_bitmap\":4,\"apply_region_type\":1,\"apply_type\":0,\"square_counts\":[0,0,-1,-1,0,0,0,0],\"region_count\":2,\"region_type\":[2,3,0]},{\"apply_region_bitmap\":8,\"apply_region_type\":513,\"apply_type\":0,\"square_counts\":[0,1,-1,-1,0,0,0,0],\"region_count\":2,\"region_type\":[2,1,0]},{\"apply_region_bitmap\":2,\"apply_region_type\":513,\"apply_type\":2,\"square_counts\":[0,-1,0,-1,0,0,0,0],\"region_count\":2,\"region_type\":[1,513,0]},{\"apply_region_bitmap\":2,\"apply_region_type\":0,\"apply_type\":2,\"square_counts\":[0,-1,0,-1,0,0,0,0],\"region_count\":2,\"region_type\":[3,3,0]},{\"apply_region_bitmap\":2,\"apply_region_type\":514,\"apply_type\":0,\"square_counts\":[0,-1,-1,-1,0,0,0,0],\"region_count\":2,\"region_type\":[3,257,0]},{\"apply_region_bitmap\":2,\"apply_region_type\":514,\"apply_type\":1,\"square_counts\":[0,2,-1,-1,0,0,0,0],\"region_count\":1,\"region_type\":[514,257,0]},{\"apply_region_bitmap\":1,\"apply_region_type\":0,\"apply_type\":3,\"square_counts\":[0,0,0,2,0,0,0,0],\"region_count\":2,\"region_type\":[257,1,0]},{\"apply_region_bitmap\":2,\"apply_region_type\":515,\"apply_type\":0,\"square_counts\":[0,-1,0,-1,0,0,0,0],\"region_count\":2,\"region_type\":[5,258,0]},{\"apply_region_bitmap\":2,\"apply_region_type\":515,\"apply_type\":1,\"square_counts\":[0,3,0,-1,0,0,0,0],\"region_count\":1,\"region_type\":[515,258,0]},{\"apply_region_bitmap\":2,\"apply_region_type\":515,\"apply_type\":0,\"square_counts\":[0,-1,-1,-1,0,0,0,0],\"region_count\":2,\"region_type\":[4,257,0]},{\"apply_region_bitmap\":4,\"apply_region_type\":257,\"apply_type\":0,\"square_counts\":[0,0,-1,-1,0,0,0,0],\"region_count\":2,\"region_type\":[1,258,0]},{\"apply_region_bitmap\":2,\"apply_region_type\":257,\"apply_type\":1,\"square_counts\":[0,6,-1,-1,0,0,0,0],\"region_count\":1,\"region_type\":[6,258,0]},{\"apply_region_bitmap\":2,\"apply_region_type\":1,\"apply_type\":0,\"square_counts\":[0,-1,0,-1,0,0,0,0],\"region_count\":2,\"region_type\":[4,3,0]},{\"apply_region_bitmap\":2,\"apply_region_type\":1,\"apply_type\":2,\"square_counts\":[0,-1,0,-1,0,0,0,0],\"region_count\":2,\"region_type\":[258,2,0]},{\"apply_region_bitmap\":2,\"apply_region_type\":0,\"apply_type\":3,\"square_counts\":[0,0,0,3,0,0,0,0],\"region_count\":2,\"region_type\":[1,257,0]},{\"apply_region_bitmap\":2,\"apply_region_type\":3,\"apply_type\":0,\"square_counts\":[0,-1,0,-1,0,0,0,0],\"region_count\":2,\"region_type\":[4,1,0]},{\"apply_region_bitmap\":2,\"apply_region_type\":516,\"apply_type\":0,\"square_counts\":[0,-1,0,-1,0,0,0,0],\"region_count\":2,\"region_type\":[5,257,0]},{\"apply_region_bitmap\":2,\"apply_region_type\":515,\"apply_type\":0,\"square_counts\":[0,-1,-1,-1,0,0,0,0],\"region_count\":2,\"region_type\":[516,257,0]},{\"apply_region_bitmap\":2,\"apply_region_type\":515,\"apply_type\":2,\"square_counts\":[0,-1,0,-1,0,0,0,0],\"region_count\":2,\"region_type\":[257,513,0]},{\"apply_region_bitmap\":2,\"apply_region_type\":515,\"apply_type\":1,\"square_counts\":[0,5,0,-1,0,0,0,0],\"region_count\":1,\"region_type\":[5,513,0]},{\"apply_region_bitmap\":4,\"apply_region_type\":514,\"apply_type\":0,\"square_counts\":[0,-1,-1,-1,0,0,0,0],\"region_count\":2,\"region_type\":[258,4,0]},{\"apply_region_bitmap\":1,\"apply_region_type\":0,\"apply_type\":3,\"square_counts\":[0,0,1,5,0,0,0,0],\"region_count\":2,\"region_type\":[515,4,0]},{\"apply_region_bitmap\":2,\"apply_region_type\":257,\"apply_type\":0,\"square_counts\":[0,-1,0,-1,0,0,0,0],\"region_count\":2,\"region_type\":[258,513,0]},{\"apply_region_bitmap\":1,\"apply_region_type\":0,\"apply_type\":3,\"square_counts\":[0,1,0,0,0,0,0,0],\"region_count\":1,\"region_type\":[257,0,0]},{\"apply_region_bitmap\":8,\"apply_region_type\":513,\"apply_type\":0,\"square_counts\":[0,-1,1,-1,0,0,0,0],\"region_count\":2,\"region_type\":[257,514,0]},{\"apply_region_bitmap\":2,\"apply_region_type\":0,\"apply_type\":3,\"square_counts\":[0,2,0,3,0,0,0,0],\"region_count\":2,\"region_type\":[4,514,0]},{\"apply_region_bitmap\":8,\"apply_region_type\":513,\"apply_type\":0,\"square_counts\":[0,3,-1,-1,0,0,0,0],\"region_count\":2,\"region_type\":[4,258,0]},{\"apply_region_bitmap\":2,\"apply_region_type\":517,\"apply_type\":0,\"square_counts\":[0,-1,0,-1,0,0,0,0],\"region_count\":2,\"region_type\":[6,257,0]},{\"apply_region_bitmap\":2,\"apply_region_type\":517,\"apply_type\":1,\"square_counts\":[0,5,0,-1,0,0,0,0],\"region_count\":1,\"region_type\":[517,257,0]},{\"apply_region_bitmap\":1,\"apply_region_type\":0,\"apply_type\":3,\"square_counts\":[0,0,0,13,0,0,0,0],\"region_count\":2,\"region_type\":[515,516,0]},{\"apply_region_bitmap\":1,\"apply_region_type\":0,\"apply_type\":3,\"square_counts\":[0,0,1,3,0,0,0,0],\"region_count\":2,\"region_type\":[514,3,0]},{\"apply_region_bitmap\":1,\"apply_region_type\":0,\"apply_type\":3,\"square_counts\":[0,0,1,2,0,0,0,0],\"region_count\":2,\"region_type\":[513,2,0]},{\"apply_region_bitmap\":2,\"apply_region_type\":3,\"apply_type\":0,\"square_counts\":[0,-1,0,-1,0,0,0,0],\"region_count\":2,\"region_type\":[5,2,0]},{\"apply_region_bitmap\":8,\"apply_region_type\":514,\"apply_type\":0,\"square_counts\":[0,1,-1,-1,0,0,0,0],\"region_count\":2,\"region_type\":[3,258,0]},{\"apply_region_bitmap\":2,\"apply_region_type\":6,\"apply_type\":0,\"square_counts\":[0,-1,0,-1,0,0,0,0],\"region_count\":2,\"region_type\":[7,1,0]},{\"apply_region_bitmap\":2,\"apply_region_type\":518,\"apply_type\":0,\"square_counts\":[0,-1,0,-1,0,0,0,0],\"region_count\":2,\"region_type\":[6,257,0]},{\"apply_region_bitmap\":1,\"apply_region_type\":0,\"apply_type\":3,\"square_counts\":[0,0,0,7,0,0,0,0],\"region_count\":2,\"region_type\":[517,518,0]},{\"apply_region_bitmap\":4,\"apply_region_type\":1,\"apply_type\":0,\"square_counts\":[0,0,-1,-1,0,0,0,0],\"region_count\":2,\"region_type\":[5,6,0]}]";
            //"[{\"apply_region_bitmap\":1,\"apply_region_type\":768,\"apply_type\":1,\"square_counts\":[0,4,0,0,0,0,0,0],\"region_count\":1,\"region_type\":[4,768,768]},{\"apply_region_bitmap\":1,\"apply_region_type\":768,\"apply_type\":2,\"square_counts\":[0,-1,0,0,0,0,0,0],\"region_count\":1,\"region_type\":[0,768,768]},{\"apply_region_bitmap\":1,\"apply_region_type\":768,\"apply_type\":1,\"square_counts\":[0,2,0,0,0,0,0,0],\"region_count\":1,\"region_type\":[2,768,768]},{\"apply_region_bitmap\":1,\"apply_region_type\":768,\"apply_type\":1,\"square_counts\":[0,3,0,0,0,0,0,0],\"region_count\":1,\"region_type\":[515,768,768]},{\"apply_region_bitmap\":1,\"apply_region_type\":768,\"apply_type\":2,\"square_counts\":[0,1,0,0,0,0,0,0],\"region_count\":1,\"region_type\":[256,768,768]},{\"apply_region_bitmap\":1,\"apply_region_type\":768,\"apply_type\":1,\"square_counts\":[0,2,0,0,0,0,0,0],\"region_count\":1,\"region_type\":[514,768,768]},{\"apply_region_bitmap\":2,\"apply_region_type\":768,\"apply_type\":2,\"square_counts\":[0,0,-1,-1,0,0,0,0],\"region_count\":2,\"region_type\":[1,1,768]},{\"apply_region_bitmap\":1,\"apply_region_type\":768,\"apply_type\":1,\"square_counts\":[0,1,0,0,0,0,0,0],\"region_count\":1,\"region_type\":[1,768,768]},{\"apply_region_bitmap\":2,\"apply_region_type\":768,\"apply_type\":2,\"square_counts\":[0,2,-1,-1,0,0,0,0],\"region_count\":2,\"region_type\":[3,1,768]},{\"apply_region_bitmap\":1,\"apply_region_type\":768,\"apply_type\":1,\"square_counts\":[0,2,-1,-1,0,0,0,0],\"region_count\":2,\"region_type\":[3,1,768]},{\"apply_region_bitmap\":1,\"apply_region_type\":768,\"apply_type\":1,\"square_counts\":[0,3,0,0,0,0,0,0],\"region_count\":1,\"region_type\":[3,768,768]},{\"apply_region_bitmap\":1,\"apply_region_type\":768,\"apply_type\":2,\"square_counts\":[0,-1,0,-1,0,0,0,0],\"region_count\":2,\"region_type\":[3,515,768]},{\"apply_region_bitmap\":1,\"apply_region_type\":768,\"apply_type\":1,\"square_counts\":[0,1,0,0,0,0,0,0],\"region_count\":1,\"region_type\":[513,768,768]},{\"apply_region_bitmap\":1,\"apply_region_type\":768,\"apply_type\":1,\"square_counts\":[0,1,-1,-1,0,0,0,0],\"region_count\":2,\"region_type\":[2,1,768]},{\"apply_region_bitmap\":1,\"apply_region_type\":768,\"apply_type\":1,\"square_counts\":[0,6,0,0,0,0,0,0],\"region_count\":1,\"region_type\":[6,768,768]},{\"apply_region_bitmap\":1,\"apply_region_type\":768,\"apply_type\":1,\"square_counts\":[0,7,0,0,0,0,0,0],\"region_count\":1,\"region_type\":[519,768,768]},{\"apply_region_bitmap\":1,\"apply_region_type\":768,\"apply_type\":1,\"square_counts\":[0,4,0,0,0,0,0,0],\"region_count\":1,\"region_type\":[516,768,768]},{\"apply_region_bitmap\":2,\"apply_region_type\":768,\"apply_type\":2,\"square_counts\":[0,0,-1,-1,0,0,0,0],\"region_count\":2,\"region_type\":[3,3,768]},{\"apply_region_bitmap\":1,\"apply_region_type\":768,\"apply_type\":2,\"square_counts\":[0,-1,0,0,0,0,0,0],\"region_count\":1,\"region_type\":[256,768,768]},{\"apply_region_bitmap\":1,\"apply_region_type\":768,\"apply_type\":1,\"square_counts\":[0,7,0,0,0,0,0,0],\"region_count\":1,\"region_type\":[7,768,768]},{\"apply_region_bitmap\":1,\"apply_region_type\":768,\"apply_type\":2,\"square_counts\":[0,-1,0,-1,0,0,0,0],\"region_count\":2,\"region_type\":[1,513,768]},{\"apply_region_bitmap\":1,\"apply_region_type\":768,\"apply_type\":1,\"square_counts\":[0,3,-1,-1,0,0,0,0],\"region_count\":2,\"region_type\":[516,257,768]},{\"apply_region_bitmap\":1,\"apply_region_type\":768,\"apply_type\":2,\"square_counts\":[0,-1,0,-1,0,0,0,0],\"region_count\":2,\"region_type\":[257,513,768]},{\"apply_region_bitmap\":1,\"apply_region_type\":768,\"apply_type\":1,\"square_counts\":[0,5,0,0,0,0,0,0],\"region_count\":1,\"region_type\":[517,768,768]},{\"apply_region_bitmap\":1,\"apply_region_type\":768,\"apply_type\":1,\"square_counts\":[0,5,0,0,0,0,0,0],\"region_count\":1,\"region_type\":[5,768,768]},{\"apply_region_bitmap\":1,\"apply_region_type\":768,\"apply_type\":2,\"square_counts\":[0,-1,0,-1,0,-1,0,0],\"region_count\":3,\"region_type\":[2,1,1]},{\"apply_region_bitmap\":1,\"apply_region_type\":768,\"apply_type\":1,\"square_counts\":[0,4,0,-1,0,0,0,0],\"region_count\":2,\"region_type\":[7,3,768]},{\"apply_region_bitmap\":1,\"apply_region_type\":768,\"apply_type\":2,\"square_counts\":[0,-1,0,-1,0,-1,0,0],\"region_count\":3,\"region_type\":[3,2,1]},{\"apply_region_bitmap\":1,\"apply_region_type\":1,\"apply_type\":0,\"square_counts\":[0,-1,0,-1,0,0,0,0],\"region_count\":2,\"region_type\":[3,2,768]},{\"apply_region_bitmap\":1,\"apply_region_type\":768,\"apply_type\":2,\"square_counts\":[0,-1,0,-1,0,-1,0,0],\"region_count\":3,\"region_type\":[4,2,2]},{\"apply_region_bitmap\":1,\"apply_region_type\":768,\"apply_type\":1,\"square_counts\":[0,4,-1,-1,0,0,0,0],\"region_count\":2,\"region_type\":[5,257,768]},{\"apply_region_bitmap\":1,\"apply_region_type\":768,\"apply_type\":2,\"square_counts\":[0,-1,0,-1,0,0,0,0],\"region_count\":2,\"region_type\":[257,1,768]},{\"apply_region_bitmap\":1,\"apply_region_type\":768,\"apply_type\":1,\"square_counts\":[0,7,0,-1,0,0,0,0],\"region_count\":2,\"region_type\":[8,1,768]},{\"apply_region_bitmap\":1,\"apply_region_type\":768,\"apply_type\":1,\"square_counts\":[0,6,0,-1,0,0,0,0],\"region_count\":2,\"region_type\":[7,257,768]},{\"apply_region_bitmap\":1,\"apply_region_type\":768,\"apply_type\":1,\"square_counts\":[0,2,-1,-1,0,0,0,0],\"region_count\":2,\"region_type\":[5,3,768]},{\"apply_region_bitmap\":1,\"apply_region_type\":2,\"apply_type\":0,\"square_counts\":[0,-1,0,-1,0,0,0,0],\"region_count\":2,\"region_type\":[4,2,768]},{\"apply_region_bitmap\":1,\"apply_region_type\":768,\"apply_type\":1,\"square_counts\":[0,2,-1,-1,0,0,0,0],\"region_count\":2,\"region_type\":[4,2,768]},{\"apply_region_bitmap\":1,\"apply_region_type\":768,\"apply_type\":1,\"square_counts\":[0,1,0,-1,0,0,0,0],\"region_count\":2,\"region_type\":[3,258,768]},{\"apply_region_bitmap\":1,\"apply_region_type\":768,\"apply_type\":1,\"square_counts\":[0,1,0,-1,0,0,0,0],\"region_count\":2,\"region_type\":[514,1,768]},{\"apply_region_bitmap\":1,\"apply_region_type\":768,\"apply_type\":1,\"square_counts\":[0,3,0,-1,0,-1,0,0],\"region_count\":3,\"region_type\":[5,1,1]},{\"apply_region_bitmap\":1,\"apply_region_type\":768,\"apply_type\":1,\"square_counts\":[0,2,-1,-1,0,0,0,0],\"region_count\":2,\"region_type\":[3,257,768]},{\"apply_region_bitmap\":2,\"apply_region_type\":768,\"apply_type\":1,\"square_counts\":[0,-1,1,-1,0,0,0,0],\"region_count\":2,\"region_type\":[257,2,768]},{\"apply_region_bitmap\":1,\"apply_region_type\":768,\"apply_type\":1,\"square_counts\":[0,1,-1,-1,0,0,0,0],\"region_count\":2,\"region_type\":[3,258,768]},{\"apply_region_bitmap\":2,\"apply_region_type\":768,\"apply_type\":2,\"square_counts\":[0,0,-1,-1,0,0,0,0],\"region_count\":2,\"region_type\":[2,258,768]},{\"apply_region_bitmap\":1,\"apply_region_type\":768,\"apply_type\":1,\"square_counts\":[0,3,-1,-1,0,0,0,0],\"region_count\":2,\"region_type\":[4,1,768]},{\"apply_region_bitmap\":2,\"apply_region_type\":768,\"apply_type\":2,\"square_counts\":[0,1,-1,-1,1,0,-1,0],\"region_count\":3,\"region_type\":[2,2,2]},{\"apply_region_bitmap\":1,\"apply_region_type\":768,\"apply_type\":1,\"square_counts\":[0,8,0,0,0,0,0,0],\"region_count\":1,\"region_type\":[8,768,768]},{\"apply_region_bitmap\":1,\"apply_region_type\":768,\"apply_type\":1,\"square_counts\":[0,4,-1,-1,0,0,0,0],\"region_count\":2,\"region_type\":[5,1,768]},{\"apply_region_bitmap\":1,\"apply_region_type\":768,\"apply_type\":2,\"square_counts\":[0,-1,0,-1,0,0,0,0],\"region_count\":2,\"region_type\":[2,2,768]},{\"apply_region_bitmap\":1,\"apply_region_type\":768,\"apply_type\":1,\"square_counts\":[0,5,0,-1,0,0,0,0],\"region_count\":2,\"region_type\":[6,1,768]},{\"apply_region_bitmap\":1,\"apply_region_type\":768,\"apply_type\":1,\"square_counts\":[0,9,0,0,0,0,0,0],\"region_count\":1,\"region_type\":[9,768,768]},{\"apply_region_bitmap\":1,\"apply_region_type\":768,\"apply_type\":1,\"square_counts\":[0,1,-1,-1,0,0,0,0],\"region_count\":2,\"region_type\":[3,2,768]},{\"apply_region_bitmap\":1,\"apply_region_type\":768,\"apply_type\":1,\"square_counts\":[0,4,-1,-1,0,0,0,0],\"region_count\":2,\"region_type\":[6,2,768]},{\"apply_region_bitmap\":1,\"apply_region_type\":768,\"apply_type\":2,\"square_counts\":[0,-1,0,-1,-1,-1,-1,0],\"region_count\":3,\"region_type\":[1,2,1]},{\"apply_region_bitmap\":2,\"apply_region_type\":1,\"apply_type\":0,\"square_counts\":[0,0,-1,-1,0,0,0,0],\"region_count\":2,\"region_type\":[1,2,768]},{\"apply_region_bitmap\":1,\"apply_region_type\":768,\"apply_type\":1,\"square_counts\":[0,2,-1,-1,-1,-1,-1,0],\"region_count\":3,\"region_type\":[4,1,1]},{\"apply_region_bitmap\":1,\"apply_region_type\":768,\"apply_type\":2,\"square_counts\":[0,-1,0,-1,-1,-1,-1,-1],\"region_count\":3,\"region_type\":[2,3,1]},{\"apply_region_bitmap\":5,\"apply_region_type\":768,\"apply_type\":2,\"square_counts\":[0,-1,0,-1,-1,1,-1,-1],\"region_count\":3,\"region_type\":[257,1,1]},{\"apply_region_bitmap\":1,\"apply_region_type\":768,\"apply_type\":1,\"square_counts\":[0,1,0,-1,0,0,0,0],\"region_count\":2,\"region_type\":[514,257,768]},{\"apply_region_bitmap\":2,\"apply_region_type\":768,\"apply_type\":1,\"square_counts\":[0,-1,1,-1,0,0,0,0],\"region_count\":2,\"region_type\":[2,515,768]},{\"apply_region_bitmap\":1,\"apply_region_type\":768,\"apply_type\":2,\"square_counts\":[0,-1,0,-1,0,0,0,0],\"region_count\":2,\"region_type\":[2,514,768]},{\"apply_region_bitmap\":5,\"apply_region_type\":768,\"apply_type\":2,\"square_counts\":[0,-1,0,-1,-1,-1,-1,-1],\"region_count\":3,\"region_type\":[1,1,1]},{\"apply_region_bitmap\":1,\"apply_region_type\":768,\"apply_type\":1,\"square_counts\":[0,3,-1,-1,0,0,0,0],\"region_count\":2,\"region_type\":[5,2,768]},{\"apply_region_bitmap\":1,\"apply_region_type\":768,\"apply_type\":2,\"square_counts\":[0,-1,1,-1,-1,0,-1,0],\"region_count\":3,\"region_type\":[1,3,1]},{\"apply_region_bitmap\":1,\"apply_region_type\":768,\"apply_type\":1,\"square_counts\":[0,2,-1,-1,0,0,0,0],\"region_count\":2,\"region_type\":[516,2,768]},{\"apply_region_bitmap\":1,\"apply_region_type\":3,\"apply_type\":0,\"square_counts\":[0,-1,0,-1,0,0,0,0],\"region_count\":2,\"region_type\":[5,2,768]},{\"apply_region_bitmap\":1,\"apply_region_type\":768,\"apply_type\":1,\"square_counts\":[0,5,-1,-1,0,0,0,0],\"region_count\":2,\"region_type\":[6,257,768]},{\"apply_region_bitmap\":4,\"apply_region_type\":768,\"apply_type\":2,\"square_counts\":[0,-1,0,-1,-1,-1,-1,-1],\"region_count\":3,\"region_type\":[1,515,2]},{\"apply_region_bitmap\":1,\"apply_region_type\":2,\"apply_type\":0,\"square_counts\":[0,-1,0,-1,0,0,0,0],\"region_count\":2,\"region_type\":[4,2,768]},{\"apply_region_bitmap\":1,\"apply_region_type\":1,\"apply_type\":0,\"square_counts\":[0,-1,0,-1,0,0,0,0],\"region_count\":2,\"region_type\":[4,3,768]},{\"apply_region_bitmap\":2,\"apply_region_type\":768,\"apply_type\":2,\"square_counts\":[0,2,-1,-1,0,0,0,0],\"region_count\":2,\"region_type\":[4,258,768]},{\"apply_region_bitmap\":1,\"apply_region_type\":768,\"apply_type\":1,\"square_counts\":[0,3,0,-1,0,-1,0,-1],\"region_count\":3,\"region_type\":[6,258,1]},{\"apply_region_bitmap\":1,\"apply_region_type\":768,\"apply_type\":1,\"square_counts\":[0,3,-1,-1,0,0,0,0],\"region_count\":2,\"region_type\":[4,257,768]},{\"apply_region_bitmap\":1,\"apply_region_type\":768,\"apply_type\":1,\"square_counts\":[0,6,0,-1,0,0,0,0],\"region_count\":2,\"region_type\":[8,258,768]},{\"apply_region_bitmap\":1,\"apply_region_type\":768,\"apply_type\":1,\"square_counts\":[0,1,-1,-1,-1,-1,0,-1],\"region_count\":3,\"region_type\":[3,257,257]},{\"apply_region_bitmap\":4,\"apply_region_type\":768,\"apply_type\":2,\"square_counts\":[0,0,-1,-1,-1,-1,-1,-1],\"region_count\":3,\"region_type\":[2,257,257]},{\"apply_region_bitmap\":1,\"apply_region_type\":768,\"apply_type\":1,\"square_counts\":[0,2,0,-1,0,-1,0,-1],\"region_count\":3,\"region_type\":[5,258,257]},{\"apply_region_bitmap\":2,\"apply_region_type\":768,\"apply_type\":2,\"square_counts\":[0,0,-1,-1,0,0,0,0],\"region_count\":2,\"region_type\":[4,4,768]},{\"apply_region_bitmap\":1,\"apply_region_type\":768,\"apply_type\":1,\"square_counts\":[0,3,0,-1,0,-1,0,-1],\"region_count\":3,\"region_type\":[6,258,257]},{\"apply_region_bitmap\":7,\"apply_region_type\":768,\"apply_type\":2,\"square_counts\":[0,0,0,-1,0,0,-1,-1],\"region_count\":3,\"region_type\":[258,3,257]},{\"apply_region_bitmap\":2,\"apply_region_type\":768,\"apply_type\":1,\"square_counts\":[0,-1,3,-1,0,0,0,0],\"region_count\":2,\"region_type\":[259,6,768]},{\"apply_region_bitmap\":1,\"apply_region_type\":768,\"apply_type\":1,\"square_counts\":[0,3,-1,-1,0,0,0,0],\"region_count\":2,\"region_type\":[5,258,768]},{\"apply_region_bitmap\":5,\"apply_region_type\":768,\"apply_type\":2,\"square_counts\":[0,-1,1,-1,-1,1,-1,-1],\"region_count\":3,\"region_type\":[257,2,257]},{\"apply_region_bitmap\":1,\"apply_region_type\":257,\"apply_type\":0,\"square_counts\":[0,-1,0,-1,0,0,0,0],\"region_count\":2,\"region_type\":[258,1,0]},{\"apply_region_bitmap\":1,\"apply_region_type\":514,\"apply_type\":0,\"square_counts\":[0,-1,0,-1,0,0,0,0],\"region_count\":2,\"region_type\":[3,257,0]},{\"apply_region_bitmap\":1,\"apply_region_type\":0,\"apply_type\":2,\"square_counts\":[0,-1,0,-1,0,0,0,0],\"region_count\":2,\"region_type\":[258,514,0]},{\"apply_region_bitmap\":5,\"apply_region_type\":0,\"apply_type\":2,\"square_counts\":[0,-1,0,-1,-1,-1,-1,-1],\"region_count\":3,\"region_type\":[257,1,257]},{\"apply_region_bitmap\":1,\"apply_region_type\":0,\"apply_type\":2,\"square_counts\":[0,-1,0,-1,0,0,0,0],\"region_count\":2,\"region_type\":[259,3,0]},{\"apply_region_bitmap\":2,\"apply_region_type\":257,\"apply_type\":0,\"square_counts\":[0,2,-1,-1,0,0,0,0],\"region_count\":2,\"region_type\":[3,258,0]},{\"apply_region_bitmap\":2,\"apply_region_type\":2,\"apply_type\":0,\"square_counts\":[0,0,-1,-1,0,0,0,0],\"region_count\":2,\"region_type\":[1,3,0]},{\"apply_region_bitmap\":1,\"apply_region_type\":516,\"apply_type\":0,\"square_counts\":[0,5,-1,-1,0,0,0,0],\"region_count\":2,\"region_type\":[5,257,0]},{\"apply_region_bitmap\":3,\"apply_region_type\":514,\"apply_type\":0,\"square_counts\":[0,1,-1,-1,0,0,0,0],\"region_count\":2,\"region_type\":[3,259,0]},{\"apply_region_bitmap\":1,\"apply_region_type\":257,\"apply_type\":0,\"square_counts\":[0,-1,0,-1,0,0,0,0],\"region_count\":2,\"region_type\":[259,514,0]},{\"apply_region_bitmap\":1,\"apply_region_type\":258,\"apply_type\":0,\"square_counts\":[0,-1,0,-1,0,0,0,0],\"region_count\":2,\"region_type\":[259,1,0]},{\"apply_region_bitmap\":1,\"apply_region_type\":257,\"apply_type\":0,\"square_counts\":[0,-1,1,-1,0,0,0,0],\"region_count\":2,\"region_type\":[258,2,0]},{\"apply_region_bitmap\":1,\"apply_region_type\":0,\"apply_type\":1,\"square_counts\":[0,4,0,-1,0,-1,0,-1],\"region_count\":3,\"region_type\":[7,257,258]},{\"apply_region_bitmap\":1,\"apply_region_type\":513,\"apply_type\":0,\"square_counts\":[0,-1,0,-1,0,0,0,0],\"region_count\":2,\"region_type\":[4,259,0]},{\"apply_region_bitmap\":1,\"apply_region_type\":3,\"apply_type\":0,\"square_counts\":[0,-1,0,-1,0,0,0,0],\"region_count\":2,\"region_type\":[4,1,0]},{\"apply_region_bitmap\":1,\"apply_region_type\":0,\"apply_type\":1,\"square_counts\":[0,4,-1,-1,-1,-1,-1,-1],\"region_count\":3,\"region_type\":[6,257,257]},{\"apply_region_bitmap\":1,\"apply_region_type\":1,\"apply_type\":0,\"square_counts\":[0,-1,0,-1,0,0,0,0],\"region_count\":2,\"region_type\":[4,3,0]},{\"apply_region_bitmap\":1,\"apply_region_type\":0,\"apply_type\":1,\"square_counts\":[0,1,-1,-1,-1,-1,-1,-1],\"region_count\":3,\"region_type\":[4,258,257]},{\"apply_region_bitmap\":1,\"apply_region_type\":514,\"apply_type\":0,\"square_counts\":[0,2,-1,-1,-1,-1,0,0],\"region_count\":3,\"region_type\":[4,257,257]},{\"apply_region_bitmap\":1,\"apply_region_type\":514,\"apply_type\":0,\"square_counts\":[0,-1,-1,-1,0,0,0,0],\"region_count\":2,\"region_type\":[3,257,0]}]";
        //"[{\"apply_region\":0,\"apply_region_bitmap\":1,\"apply_region_type\":0,\"square_counts\":[0,-1,0,0,0,0,0,0],\"region_count\":1,\"region_type\":[0,0,0]},{\"apply_region\":0,\"apply_region_bitmap\":1,\"apply_region_type\":0,\"square_counts\":[0,1,0,0,0,0,0,0],\"region_count\":1,\"region_type\":[1,0,0]},{\"apply_region\":0,\"apply_region_bitmap\":1,\"apply_region_type\":0,\"square_counts\":[0,3,0,0,0,0,0,0],\"region_count\":1,\"region_type\":[3,0,0]},{\"apply_region\":0,\"apply_region_bitmap\":1,\"apply_region_type\":0,\"square_counts\":[0,-1,0,-1,0,0,0,0],\"region_count\":2,\"region_type\":[1,1,0]},{\"apply_region\":0,\"apply_region_bitmap\":1,\"apply_region_type\":0,\"square_counts\":[0,2,-1,-1,0,0,0,0],\"region_count\":2,\"region_type\":[3,1,0]},{\"apply_region\":0,\"apply_region_bitmap\":1,\"apply_region_type\":0,\"square_counts\":[0,2,0,0,0,0,0,0],\"region_count\":1,\"region_type\":[2,0,0]},{\"apply_region\":0,\"apply_region_bitmap\":1,\"apply_region_type\":0,\"square_counts\":[0,5,0,0,0,0,0,0],\"region_count\":1,\"region_type\":[5,0,0]},{\"apply_region\":0,\"apply_region_bitmap\":1,\"apply_region_type\":0,\"square_counts\":[0,4,0,0,0,0,0,0],\"region_count\":1,\"region_type\":[4,0,0]},{\"apply_region\":0,\"apply_region_bitmap\":2,\"apply_region_type\":0,\"square_counts\":[0,0,1,-1,0,0,0,0],\"region_count\":2,\"region_type\":[1,2,0]},{\"apply_region\":0,\"apply_region_bitmap\":1,\"apply_region_type\":0,\"square_counts\":[0,7,0,0,0,0,0,0],\"region_count\":1,\"region_type\":[7,0,0]},{\"apply_region\":0,\"apply_region_bitmap\":1,\"apply_region_type\":0,\"square_counts\":[0,8,0,0,0,0,0,0],\"region_count\":1,\"region_type\":[8,0,0]},{\"apply_region\":0,\"apply_region_bitmap\":2,\"apply_region_type\":0,\"square_counts\":[0,-1,1,-1,0,0,0,0],\"region_count\":2,\"region_type\":[2,3,0]},{\"apply_region\":0,\"apply_region_bitmap\":2,\"apply_region_type\":0,\"square_counts\":[0,-1,2,-1,0,0,0,0],\"region_count\":2,\"region_type\":[2,4,0]},{\"apply_region\":0,\"apply_region_bitmap\":2,\"apply_region_type\":0,\"square_counts\":[0,0,-1,-1,0,0,0,0],\"region_count\":2,\"region_type\":[2,2,0]},{\"apply_region\":0,\"apply_region_bitmap\":2,\"apply_region_type\":0,\"square_counts\":[0,-1,3,-1,0,0,0,0],\"region_count\":2,\"region_type\":[1,4,0]},{\"apply_region\":0,\"apply_region_bitmap\":1,\"apply_region_type\":0,\"square_counts\":[0,1,-1,-1,0,0,0,0],\"region_count\":2,\"region_type\":[2,1,0]},{\"apply_region\":0,\"apply_region_bitmap\":5,\"apply_region_type\":0,\"square_counts\":[0,-1,0,-1,-1,1,-1,-1],\"region_count\":3,\"region_type\":[1,1,1]},{\"apply_region\":0,\"apply_region_bitmap\":2,\"apply_region_type\":0,\"square_counts\":[0,-1,3,-1,0,0,0,0],\"region_count\":2,\"region_type\":[2,5,0]},{\"apply_region\":0,\"apply_region_bitmap\":1,\"apply_region_type\":0,\"square_counts\":[0,4,-1,-1,0,0,0,0],\"region_count\":2,\"region_type\":[5,1,0]},{\"apply_region\":0,\"apply_region_bitmap\":1,\"apply_region_type\":0,\"square_counts\":[0,1,-1,-1,0,0,0,0],\"region_count\":2,\"region_type\":[4,3,0]},{\"apply_region\":0,\"apply_region_bitmap\":1,\"apply_region_type\":0,\"square_counts\":[0,-1,0,-1,0,0,0,0],\"region_count\":2,\"region_type\":[3,3,0]},{\"apply_region\":0,\"apply_region_bitmap\":1,\"apply_region_type\":0,\"square_counts\":[0,5,-1,-1,0,0,0,0],\"region_count\":2,\"region_type\":[6,1,0]},{\"apply_region\":0,\"apply_region_bitmap\":1,\"apply_region_type\":0,\"square_counts\":[0,6,-1,2,0,0,0,0],\"region_count\":2,\"region_type\":[7,1,0]},{\"apply_region\":0,\"apply_region_bitmap\":4,\"apply_region_type\":0,\"square_counts\":[0,0,0,-1,-1,0,-1,0],\"region_count\":3,\"region_type\":[1,2,1]},{\"apply_region\":0,\"apply_region_bitmap\":2,\"apply_region_type\":0,\"square_counts\":[0,0,-1,-1,0,0,-1,0],\"region_count\":3,\"region_type\":[1,2,1]},{\"apply_region\":0,\"apply_region_bitmap\":4,\"apply_region_type\":0,\"square_counts\":[0,0,0,-1,-1,0,-1,0],\"region_count\":3,\"region_type\":[1,2,2]},{\"apply_region\":0,\"apply_region_bitmap\":1,\"apply_region_type\":0,\"square_counts\":[0,5,-1,-1,0,0,0,0],\"region_count\":2,\"region_type\":[7,2,0]},{\"apply_region\":0,\"apply_region_bitmap\":1,\"apply_region_type\":0,\"square_counts\":[0,2,0,2,0,2,0,0],\"region_count\":3,\"region_type\":[4,1,1]},{\"apply_region\":0,\"apply_region_bitmap\":5,\"apply_region_type\":0,\"square_counts\":[0,-1,1,-1,-1,1,-1,-1],\"region_count\":3,\"region_type\":[1,2,1]},{\"apply_region\":0,\"apply_region_bitmap\":2,\"apply_region_type\":0,\"square_counts\":[0,-1,-1,-1,-1,0,-1,0],\"region_count\":3,\"region_type\":[1,3,1]},{\"apply_region\":0,\"apply_region_bitmap\":1,\"apply_region_type\":0,\"square_counts\":[0,6,0,0,0,0,0,0],\"region_count\":1,\"region_type\":[6,0,0]},{\"apply_region\":0,\"apply_region_bitmap\":1,\"apply_region_type\":1,\"square_counts\":[0,2,0,-1,-1,-1,0,0],\"region_count\":3,\"region_type\":[5,2,1]},{\"apply_region\":0,\"apply_region_bitmap\":2,\"apply_region_type\":0,\"square_counts\":[0,0,-1,-1,0,0,0,0],\"region_count\":2,\"region_type\":[4,4,0]},{\"apply_region\":0,\"apply_region_bitmap\":1,\"apply_region_type\":0,\"square_counts\":[0,-1,0,-1,-1,0,-1,0],\"region_count\":3,\"region_type\":[2,4,2]},{\"apply_region\":0,\"apply_region_bitmap\":1,\"apply_region_type\":0,\"square_counts\":[0,3,0,-1,0,-1,0,0],\"region_count\":3,\"region_type\":[5,1,1]},{\"apply_region\":0,\"apply_region_bitmap\":1,\"apply_region_type\":0,\"square_counts\":[0,-1,0,-1,-1,0,-1,0],\"region_count\":3,\"region_type\":[1,2,1]}]";
        SaveObject* sobj = SaveObject::load(sin);
        SaveObjectList* rlist = sobj->get_list();
        for (int i = 0; i < rlist->get_count(); i++)
        {
            global_rules.push_back(rlist->get_item(i));
        }
    }

    bool rep = true;

    while (rep)
    {
        rep = false;
    	while (add_regions(-1)) {}
        add_new_regions();
        for (GridRule& rule : global_rules)
        {
            while (apply_rule(rule))
                rep = true;
        }
        for (GridRegion& r : regions)
        {
            r.stale = true;
        }

        add_new_regions();

        //
        //
        // std::set<XYPos> to_revl;;
        // for (GridRegion& r : regions)
        // {
        //     bool revl = false;
        //     if ((r.type.value == r.elements.size()) && (r.type.type == RegionType::EQUAL))
        //     {
        //         for (XYPos p : r.elements)
        //         {
        //             assert(vals[p].bomb);
        //             to_revl.insert(p);
        //         }
        //     }
        //
        //     if ((r.type.value == 0) && (r.type.type == RegionType::EQUAL))
        //     {
        //         for (XYPos p : r.elements)
        //         {
        //             assert(!vals[p].bomb);
        //             to_revl.insert(p);
        //         }
        //     }
        // }
        // for (XYPos p : to_revl)
        // {
        //     reveal(p);
        //     rep = true;
        // }
    }
}

bool Grid::is_solveable(bool use_high_count)
{
    bool rep = true;

    XYPos best_pos;


    while (rep && !is_solved())
    {
        rep = false;
        solve_easy();

        unsigned hidden  = 0;
        FOR_XY(p, XYPos(), size)
            if (!vals[p].revealed)
                hidden++;

        count_revealed = (hidden <= 8);



        FOR_XY(p, XYPos(), size)
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

bool Grid::solve(int hard)
{
    bool rep = true;
    bool solved  = false;

    XYPos best_pos;

    while (rep)
    {
        solved  = true;
        rep = false;

        int hardness;
        std::set<XYPos> best_pos;

        find_easiest_move(best_pos, hardness);
        std::cout << ":" << hardness << "\n";
        if (hardness < 10000)
        {
            if (hardness == 1)
            {
                for (XYPos p : best_pos)
                    reveal(p);
            }
            else
            {
                reveal(*best_pos.begin());
            }
            rep = true;
        }
        FOR_XY(p, XYPos(), size)
        {
            if (!vals[p].revealed)
            {
                solved  = false;
            }
        }
    }
    return solved;
}

void Grid::find_easiest_move(std::set<XYPos>& best_pos, int& hardness)
{
    hardness = 10000000;

    FOR_XY(p, XYPos(), size)
    {
        if (!vals[p].revealed)
        {
            if (is_determinable(p))
            {
                int complexity = solve_complexity(p);
                if (complexity == hardness)
                {
                    best_pos.insert(p);
                }
                else if (complexity < hardness)
                {
                    hardness = complexity;
                    best_pos.clear();
                    best_pos.insert(p);
                }
            }
        }
    }
}

XYPos Grid::find_easiest_move(int& hardness)
{
    int best_harndess = 10000000;
    XYPos best_pos;

    FOR_XY(p, XYPos(), size)
    {
        if (!vals[p].revealed)
        {
            if (is_determinable(p))
            {
                int complexity = solve_complexity(p);
                if (complexity < best_harndess)
                {
                    best_harndess = complexity;
                    best_pos = p;
                }
            }
        }
    }
    hardness = best_harndess;
    return best_pos;
}

void Grid::find_easiest_move(std::set<XYPos>& solves, Grid& needed)
{
    int best_harndess = 10000000;
    XYPos best_pos;
    Grid best_grid;

    FOR_XY(p, XYPos(), size)
    {
        if (!vals[p].revealed)
        {
            if (is_determinable(p))
            {
                Grid min_grid;
                int complexity = solve_complexity(p, min_grid);
                if (complexity < best_harndess)
                {
                    best_harndess = complexity;
                    best_grid = min_grid;
                }
            }
        }
    }
    solves.clear();
    best_grid.print();

    FOR_XY(p, XYPos(), size)
    {

        if (!best_grid.get(p).revealed && best_grid.is_determinable(p))
            solves.insert(p);
    }
    needed = best_grid;
}

void Grid::find_easiest_move_using_regions(std::set<XYPos>& solves)
{
    int hardness;
    XYPos pos = find_easiest_move(hardness);
    if (!is_determinable_using_regions(pos, true))
    {
        for (GridRegion& r : regions)
        {
            r.hidden = false;
            r.visibility_forced = true;
            printf("reset regions\n");
        }
    }

    assert(is_determinable_using_regions(pos, true));

    for (GridRegion& r : regions)
    {
        if (r.hidden)
            continue;
        r.hidden = true;

        if (!is_determinable_using_regions(pos, true))
            r.hidden = false;
        else
            r.visibility_forced = true;
    }

    solves.clear();
    FOR_XY(p, XYPos(), size)
    {
        if (is_determinable_using_regions(p, true))
            solves.insert(p);
    }
}

int Grid::solve_complexity(XYPos q, std::set<XYPos>* needed)
{
    Grid tst = *this;
    tst.reveal_switch(q);

    int cnt = 0;

    assert(!tst.has_solution());

    tst.count_revealed = false;
    if (tst.has_solution())
    {
        tst.count_revealed = true;
        cnt += 3;
    }


    FOR_XY(p, XYPos(), size)
    {
        if (q == p)
            continue;
        if (tst.vals[p].revealed && !tst.vals[p].bomb)
        {
            RegionType clue = tst.vals[p].clue;
            tst.vals[p].clue.type = RegionType::NONE;
            if (tst.has_solution())
            {
                tst.vals[p].clue = clue;
                cnt++;
                if (needed)
                    needed->insert(p);
            }
        }
    }

    return cnt;
}

int Grid::solve_complexity(XYPos q, Grid& min_grid)
{
    Grid tst = *this;

    int cnt = 0;

    tst.count_revealed = false;
    if (!tst.is_determinable(q))
    {
        tst.count_revealed = true;
        cnt += 3;
    }


    FOR_XY(p, XYPos(), size)
    {
        if (q == p)
            continue;
        if (tst.vals[p].revealed && !tst.vals[p].bomb)
        {
            RegionType clue = tst.vals[p].clue;
            tst.vals[p].clue.type = RegionType::NONE;
            if (!tst.is_determinable(q))
            {
                tst.vals[p].clue = clue;
                cnt++;
            }
        }
    }

    min_grid = tst;
    return cnt;
}

bool Grid::is_determinable(XYPos q)
{
    Grid tst = *this;
    tst.regions.clear();
    while (tst.add_regions(-1)) {}
    add_new_regions();

    return tst.is_determinable_using_regions(q);
//    bool b = is_determinable_sat(q);
//     if (a != b)
//     {
//         printf("%d %d : ", q.x, q.y);
//         std::cout << to_string() << "\n";
//     }
//    assert(a == b);
//    return a;
}

bool Grid::is_determinable_using_regions(XYPos q, bool hidden)
{

    std::map <XYPos, unsigned> pos_to_set;
    unsigned set_index = 10000000;

    for (GridRegion& r : regions)
    {
        if (r.hidden && hidden)
          continue;
        for (const XYPos& p : r.elements)
        {
            set_index++;
            unsigned v = pos_to_set[p];
            for (const XYPos& p2 : r.elements)
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
    }

    for (GridRegion& r : regions)
    {
        if (r.hidden && hidden)
          continue;
        std::set<unsigned> seen;
        z3::expr e = c.int_val(0);

        for (const XYPos& p : r.elements)
        {
            unsigned si = pos_to_set[p];
            if (!seen.count(si))
            {
                seen.insert(si);
                e = e + vec[si];
            }
        }
        s.add(r.type.apply_z3_rule(e));
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


//    s.add(vec[si] == int(get(q).bomb ? 0 : set_size[si]));


//     XYPos pos;
//     for(pos.y = 0; pos.y < size.y; pos.y++)
//     {
//         for(pos.x = 0; pos.x < size.x; pos.x++)
//         {
//             printf("%02d ", pos_to_set[pos]);
//         }
//         printf("\n");
//     }
//     printf("\n");

    if (s.check() == z3::sat)
    {
//        printf("sat\n");
        return false;
    }
    else
    {
//        printf("unsat\n");
        return true;
    }
}

// static std::set<std::string> solution_cache;
// static std::set<std::string> no_solution_cache;

bool Grid::has_solution(void)
{
//    std::string str = to_string();

//   if (solution_cache.count(str))
//       return true;
//   if (no_solution_cache.count(str))
//       return false;

    z3::context c;
    z3::expr_vector vec(c);
    std::map<XYPos, unsigned> vec_index;
    FOR_XY(p, XYPos(), size)
    {
        if (!vals[p].revealed)
        {
            vec_index[p] = vec.size();
            std::stringstream x_name;
            x_name << (char)('A' + p.y)  << p.x;
            vec.push_back(c.bool_const(x_name.str().c_str()));
        }
    }
    z3::solver s(c);

    int hidden = 0;
    FOR_XY(p, XYPos(), size)
    {
        if (!vals[p].revealed)
            hidden++;
    }

    if (count_revealed) // && (hidden < 12))
    {
        z3::expr_vector t(c);
        int cnt = 0;
        int cntn = 0;
        FOR_XY(p, XYPos(), size)
        {
            if (!vals[p].revealed)
            {
                t.push_back(vec[vec_index[p]]);
                if (vals[p].bomb)
                    cnt++;
                else
                    cntn++;

            }
        }
        cnt-=count_dec;
        if (cnt < 0)
        {
//            no_solution_cache.insert(str);
//            printf("cnt < 0\n");
            return false;
        }
//        printf("cnt: %d %d\n", cnt, cntn);

        if (t.size() == 0)
        {
//            no_solution_cache.insert(str);
//            printf("t.size() == 0\n");
            return false;
        }
        else
        {
            s.add(atleast(t, cnt));
            s.add(atmost(t, cnt));
        }
    }

    FOR_XY(p, XYPos(), size)
    {
        if (vals[p].revealed && !vals[p].bomb)
        {
            RegionType clue = vals[p].clue;
            if (clue.type  == RegionType::NONE)
                continue;
            int cnt = clue.value;
            z3::expr_vector t(c);
            FOR_XY(offset, XYPos(-1,-1), XYPos(2,2))
            {
                XYPos n = p + offset;
                if (!get(n).revealed)
                {
                    t.push_back(vec[vec_index[n]]);
                }
                else if (get(n).bomb)
                {
                    if (cnt == 0)
                    {
                        if (clue.type == RegionType::MORE)
                            continue;
//                        no_solution_cache.insert(str);
//            	        printf("cnt < 0\n");
                        return false;
                    }
                    cnt--;
                }
            }


            if (t.size())
            {
                if (clue.type  != RegionType::LESS)
                    s.add(atleast(t, cnt));
                if (clue.type  != RegionType::MORE)
                    s.add(atmost(t, cnt));
//                s.add(sum(t) == cnt);
            }
            else
            {
                if (cnt && (clue.type != RegionType::LESS))
                {
//                    no_solution_cache.insert(str);
//            	    printf("cnt but all taken\n");
                    return false;
                }
            }
        }
    }
//    printf("pos:%s\n", (s.check() == z3::sat) ? "sat" : "unsat");
    if (s.check() == z3::sat)
    {
//        solution_cache.insert(str);
        return true;
    }
    else
    {
//        no_solution_cache.insert(str);
        return false;
    }
}

void Grid::make_harder(void)
{
    {
        std::vector<XYPos> tgt;
        FOR_XY(p, XYPos(), size)
        {
            if (!vals[p].bomb)
                tgt.push_back(p);
        }
        std::shuffle(tgt.begin(), tgt.end(), rnd.gen);

        int do_count = 0; //tgt.size();
        for (XYPos p : tgt)
        {
            Grid tst = *this;
            if (!do_count--)
                break;

            tst = *this;
            if (rnd & 1)
            {
                tst.vals[p].clue.type = RegionType::LESS;
//                tst.vals[p].clue.value += rnd % 2;
            }
            else
            {
                tst.vals[p].clue.type = RegionType::MORE;
//                tst.vals[p].clue.value -= rnd % 2;
                if (tst.vals[p].clue.value < 0)
                    tst.vals[p].clue.value = 0;
            }
            if (tst.is_solveable())
            {
                vals[p].clue = tst.vals[p].clue;
            }
        }
    }



    {
        std::vector<XYPos> tgt;
        FOR_XY(p, XYPos(), size)
        {
            if (vals[p].revealed)
                tgt.push_back(p);
        }

        std::shuffle(tgt.begin(), tgt.end(), rnd.gen);

        for (XYPos p : tgt)
        {
            Grid tst = *this;
            tst.vals[p].revealed = false;
            if (tst.is_solveable())
            {
                vals[p].revealed = false;
            }
        }
    }

    {
        std::vector<XYPos> tgt;
        FOR_XY(p, XYPos(), size)
        {
            if (!vals[p].bomb)
                tgt.push_back(p);
        }
        std::shuffle(tgt.begin(), tgt.end(), rnd.gen);

        for (XYPos p : tgt)
        {
            Grid tst = *this;
            assert(!tst.vals[p].bomb);
            tst.vals[p].clue.type = RegionType::NONE;
            if (tst.is_solveable())
            {
                vals[p].clue.type = RegionType::NONE;
            }
            else
            {
                tst = *this;
                tst.vals[p].clue.type = RegionType::LESS;
                if (tst.is_solveable())
                {
                    vals[p].clue.type = RegionType::LESS;
                    while (true)
                    {
                        tst = *this;
                        tst.vals[p].clue.type = RegionType::LESS;
                        tst.vals[p].clue.value++;
                        if (!tst.is_solveable())
                            break;
                        if (tst.vals[p].clue.value > 9)
                        {
                            assert(0);
                        }
                        printf("tst.vals[p].clue.value %d++ \n", tst.vals[p].clue.value);
                        vals[p].clue.type = RegionType::LESS;
                        vals[p].clue.value = tst.vals[p].clue.value;
                    }
                }
                else
                {
                    tst = *this;
                    tst.vals[p].clue.type = RegionType::MORE;
                    if (tst.is_solveable())
                    {
                        tst.vals[p].clue.type = RegionType::MORE;
                        while (true)
                        {
                            tst = *this;
                            tst.vals[p].clue.type = RegionType::MORE;
                            tst.vals[p].clue.value--;
                            if (!tst.is_solveable())
                                break;
                            printf("tst.vals[p].clue.value %d-- \n", tst.vals[p].clue.value);
                            vals[p].clue.type = RegionType::MORE;
                            vals[p].clue.value = tst.vals[p].clue.value;
                        }


                    }
                }
                if (vals[p].clue.value == 0)
                {
                    if (vals[p].clue.type == RegionType::MORE)
                    {
                        vals[p].clue.type = RegionType::NONE;
                        assert(0);
                    }
                    else if (vals[p].clue.type == RegionType::LESS)
                    {
                        vals[p].clue.type = RegionType::EQUAL;
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
        if((*it).elements.count(p))
            it = regions.erase(it);
        else
            ++it;
    }

    it = regions_to_add.begin();
    while (it != regions_to_add.end())
    {
        if((*it).elements.count(p))
            it = regions_to_add.erase(it);
        else
            ++it;
    }
}

void Grid::reveal_switch(XYPos q)
{
    reveal(q);
    vals[q].bomb = !vals[q].bomb;
    vals[q].clue.type = RegionType::NONE;
    if (vals[q].bomb)
        count_dec++;
    else
        count_dec--;
//    printf("%d\n", count_dec);
}

std::string Grid::to_string()
{
    std::string s;

    s += 'A' + size.x;
    s += 'A' + size.y;
    s += 'A' + count_revealed;
    FOR_XY(p, XYPos(), size)
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

void Grid::from_string(std::string s)
{
    if (s.length() < 5)
        return;
    Grid g;
    int a = s[0] - 'A';
    if (a < 0 || a > 50) return;
    g.size.x = a;

    a = s[1] - 'A';
    if (a < 0 || a > 50) return;
    g.size.y = a;

    a = s[2] - 'A';
    if (a < 0 || a > 1) return;
    g.count_revealed = a;

    int i = 3;
    FOR_XY(p, XYPos(), size)
    {
        if (s.length() < i) return;
        char c = s[i++];

        g.vals[p] = GridPlace(true, true);

        if (c == '_')
        {
            g.vals[p].revealed = false;
            if (s.length() < i) return;
            c = s[i++];
        }

        if (c != '!')
        {
            g.vals[p].bomb = false;
            g.vals[p].clue.type = RegionType::Type(c - 'A');
            if (s.length() < i) return;
            c = s[i++];
            g.vals[p].clue.value = c - '0';
        }
    }
    *this = g;
}

bool Grid::is_solved(void)
{
    FOR_XY(p, XYPos(), size)
    {
        if (!vals[p].revealed)
            return false;
    }
    return true;
}

bool Grid::add_regions(int level)
{
    {
        std::set<XYPos> elements;
        int hidden = 0;
        int hidden_bombs = 0;
        FOR_XY(p, XYPos(), size)
        {
            if (!vals[p].revealed)
            {
                hidden++;
                if (vals[p].bomb)
                    hidden_bombs++;
                elements.insert(p);
            }
        }
        if (hidden_bombs <= 8)
        {
            GridRegion reg;
            reg.reset(RegionType(RegionType::EQUAL, hidden_bombs));
            reg.elements = elements;
            reg.hidden = true;
            reg.global = true;
            reg.visibility_forced = true;
            if (!contains(regions, reg) && !contains(regions_to_add, reg))
            {
                regions.push_back(reg);
            }
        }
    }


    FOR_XY(p, XYPos(), size)
    {
        std::set<XYPos> elements;
        GridPlace g = vals[p];
        if (g.revealed && !g.bomb && ((g.clue.type != RegionType::NONE)))
        {
            RegionType clue = g.clue;

            FOR_XY(offset, XYPos(-1,-1), XYPos(2,2))
            {
                XYPos n = p + offset;
                if (!get(n).revealed)
                {
                    elements.insert(n);
                }
                else if (get(n).bomb)
                {
                    clue.value--;
                }
            }
            if (clue.value < 0)
                continue;
            if (elements.size() == 0)
                continue;
//            if (clue.value == 0 && clue.type == RegionType::MORE)
//                continue;
//            if (clue.value > elements.size())
//                continue;
//            if (clue.value == elements.size() && clue.type == RegionType::LESS)
//                continue;
            {
                GridRegion reg;
                reg.reset(clue);
                reg.elements = elements;
                if (!contains(regions, reg) && !contains(regions_to_add, reg))
                {
                    regions.push_back(reg);
                    return true;
                }
            }
        }
    }
    return false;
}

GridRule Grid::rule_from_selected_regions(GridRegion* r1, GridRegion* r2, GridRegion* r3)
{
    GridRule rule;

    if (r1)
    {
        rule.region_count = 1;
        rule.region_type[0] = r1->type;
        rule.square_counts[1] = r1->elements.size();
    }
    if (r2)
    {
        rule.region_count = 2;
        rule.region_type[1] = r2->type;
        rule.square_counts[2] = r2->elements.size();
        for (XYPos pos : r2->elements)
        {
            if (r1->elements.count(pos))
            {
                rule.square_counts[1]--;
                rule.square_counts[2]--;
                rule.square_counts[3]++;
            }
        }
    }
    if (r3)
    {
        rule.region_count = 3;
        rule.region_type[2] = r3->type;
        rule.square_counts[4] = r3->elements.size();

        for (XYPos pos : r3->elements)
        {
            if (r1->elements.count(pos) && r2->elements.count(pos))
            {
                rule.square_counts[3]--;
                rule.square_counts[4]--;
                rule.square_counts[7]++;
            }
            else if (r1->elements.count(pos))
            {
                rule.square_counts[1]--;
                rule.square_counts[4]--;
                rule.square_counts[5]++;
            }
            else if (r2->elements.count(pos))
            {
                rule.square_counts[2]--;
                rule.square_counts[4]--;
                rule.square_counts[6]++;
            }
        }
    }
    return rule;
}

Grid::ApplyRuleResp Grid::apply_rule(GridRule& rule, GridRegion* r1, GridRegion* r2, GridRegion* r3)
{
    if (rule.stale && r1->stale && (!r2 || r2->stale) && (!r3 || r3->stale))
        return APPLY_RULE_RESP_NONE;
    assert(rule.apply_region_bitmap);
    if ((rule.apply_type == GridRule::HIDE) || (rule.apply_type == GridRule::SHOW))
    {
        bool h = (rule.apply_type == GridRule::HIDE);
        if ((rule.apply_region_bitmap & 1) && !r1->visibility_forced)
            r1->hidden = h;
        if ((rule.apply_region_bitmap & 2) && !r2->visibility_forced)
            r2->hidden = h;
        if ((rule.apply_region_bitmap & 4) && !r3->visibility_forced)
            r3->hidden = h;
        return APPLY_RULE_RESP_NONE;
    }

    std::set<XYPos> itter_elements;
    GridRegion* itter = NULL;
    for (XYPos pos : r1->elements)
        itter_elements.insert(pos);
    if (r2)
        for (XYPos pos : r2->elements)
            itter_elements.insert(pos);
    if (r3)
        for (XYPos pos : r3->elements)
            itter_elements.insert(pos);

    std::set<XYPos> to_reveal;

    for (XYPos pos : itter_elements)
    {
        unsigned m = 0;
        if (r1->elements.count(pos))
            m |= 1;
        if (r2 && (r2->elements.count(pos)))
            m |= 2;
        if (r3 && (r3->elements.count(pos)))
            m |= 4;
        if ((rule.apply_region_bitmap >> m) & 1)
            to_reveal.insert(pos);
    }
    if (to_reveal.empty())
        return APPLY_RULE_RESP_NONE;

    if ((rule.apply_type == GridRule::BOMB) || (rule.apply_type == GridRule::CLEAR))
    {
        for (XYPos pos : to_reveal)
        {
            if (vals[pos].bomb != (rule.apply_type == GridRule::BOMB))
            {
                printf("wrong\n");
                return APPLY_RULE_RESP_ERROR;
            }
        }
        for (XYPos pos : to_reveal)
            reveal(pos);
        return APPLY_RULE_RESP_HIT;
    }
    else if (rule.apply_type == GridRule::REGION)
    {
        GridRegion reg;
        reg.reset(rule.apply_region_type);
        for (XYPos pos : to_reveal)
        {
            reg.elements.insert(pos);
        }
        reg.rule = &rule;
        bool found = (std::find(regions.begin(), regions.end(), reg) != regions.end())  ||
                     (std::find(regions_to_add.begin(), regions_to_add.end(),reg) != regions_to_add.end());

        if (!found)
        {
            regions_to_add.push_back(reg);
            return APPLY_RULE_RESP_NONE;
        }
        else
            return APPLY_RULE_RESP_NONE;
    }
    assert(0);
    return APPLY_RULE_RESP_HIT;
}

Grid::ApplyRuleResp Grid::apply_rule(GridRule& rule)
{
    assert(rule.region_count);
    for (GridRegion& r1 : regions)
    {
        if (r1.type != rule.region_type[0])
            continue;
        if (rule.region_count == 1)
        {
            if (rule.stale && r1.stale)
                continue;
            GridRule try_rule = rule_from_selected_regions(&r1, NULL, NULL);
            if (rule.matches(try_rule))
            {
                ApplyRuleResp resp = apply_rule(rule, &r1, NULL, NULL);
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
                if (rule.region_count == 2)
                {
                    if (rule.stale && r1.stale && r2.stale)
                        continue;
                    if (!r1.overlaps(r2))
                        continue;
                    GridRule try_rule = rule_from_selected_regions(&r1, &r2, NULL);
                    if (!try_rule.square_counts[3])
                        continue;
                    if (rule.matches(try_rule))
                    {
                        ApplyRuleResp resp = apply_rule(rule, &r1, &r2, NULL);
                        if (resp != APPLY_RULE_RESP_NONE)
                            return resp;
                    }
                }
                else
                {
                    for (GridRegion& r3 : regions)
                    {
                        if (rule.stale && r1.stale && r2.stale && r3.stale)
                            continue;
                        if (r3.type != rule.region_type[2])
                            continue;
                        if ((r3 == r1) || (r3 == r2))
                            continue;
                        if (!r1.overlaps(r3) && !r2.overlaps(r3))
                            continue;
                        assert(rule.region_count == 3);
                        {
                            GridRule try_rule = rule_from_selected_regions(&r1, &r2, &r3);
                            int c = int(try_rule.square_counts[3] != 0) + int(try_rule.square_counts[5] != 0) + int(try_rule.square_counts[6] != 0);
                            if (c < 2 && !try_rule.square_counts[7])
                                continue;
                            if (rule.matches(try_rule))
                            {
                                ApplyRuleResp resp = apply_rule(rule, &r1, &r2, &r3);
                                if (resp != APPLY_RULE_RESP_NONE)
                                    return resp;
                            }
                        }
                    }
                }
            }
        }
    }
    return APPLY_RULE_RESP_NONE;
}
void Grid::add_new_regions()
{
    regions.splice(regions.end(), regions_to_add);

}

void Grid::add_one_new_region()
{
    if (!regions_to_add.empty())
    regions.splice(regions.end(), regions_to_add, regions_to_add.begin());

}

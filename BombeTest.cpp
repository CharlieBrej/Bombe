#include "Grid.h"

#include <iostream>

void global_mutex_lock()
{}

void global_mutex_unlock()
{}

namespace
{

const char* legality_name(
    GridRule::IsLogicalRep result)
{
    static const char* names[] = {
        "OK", "ILLOGICAL", "LOSES_DATA",
        "IMPOSSIBLE", "USELESS", "UNBOUNDED",
        "LIMIT"
    };
    return names[result];
}

int failures = 0;

void expect_condition(
    const char* name,
    bool actual)
{
    if (actual)
    {
        std::cout << "PASS: " << name << '\n';
        return;
    }

    failures++;
    std::cerr << "FAIL: " << name << '\n';
}

void expect_integer(
    const char* name,
    int actual,
    int expected)
{
    if (actual == expected)
    {
        std::cout << "PASS: " << name << '\n';
        return;
    }

    failures++;
    std::cerr << "FAIL: " << name
              << ": expected " << expected
              << ", got " << actual << '\n';
}

void expect_legality(
    const char* name,
    GridRule& rule,
    GridRule::IsLogicalRep expected)
{
    GridRule why;
    int variables[5] = {-1, -1, -1, -1, -1};
    const GridRule::IsLogicalRep actual =
        rule.is_legal(why, variables);
    if (actual == expected)
    {
        std::cout << "PASS: " << name << '\n';
        return;
    }

    failures++;
    std::cerr << "FAIL: " << name
              << ": expected "
              << legality_name(expected)
              << ", got "
              << legality_name(actual) << '\n';
}

void expect_filter(
    const char* name,
    const GridRegion& region,
    const XYSet& required,
    const XYSet& forbidden,
    bool expected)
{
    const bool actual = region.matches_filters(
        required, forbidden);
    if (actual == expected)
    {
        std::cout << "PASS: " << name << '\n';
        return;
    }

    failures++;
    std::cerr << "FAIL: " << name
              << ": expected " << expected
              << ", got " << actual << '\n';
}

void test_implication_region_filters()
{
    const XYPos if_cell(1, 1);
    const XYPos then_cell(2, 1);
    const XYPos outside_cell(3, 1);

    RegionIfType implication_type;
    implication_type.if_type = RegionType(
        RegionType::EQUAL, 1);
    implication_type.type = RegionType(
        RegionType::EQUAL, 1);
    GridRegion implication(implication_type);
    implication.elements_neg.set(if_cell);
    implication.elements.set(then_cell);

    XYSet empty;
    XYSet required_if;
    required_if.set(if_cell);
    expect_filter(
        "required IF cell matches",
        implication, required_if, empty, true);

    XYSet required_both = required_if;
    required_both.set(then_cell);
    expect_filter(
        "required IF and THEN cells match",
        implication, required_both, empty, true);

    XYSet forbidden_if;
    forbidden_if.set(if_cell);
    expect_filter(
        "forbidden IF cell rejects",
        implication, empty, forbidden_if, false);

    XYSet required_outside;
    required_outside.set(outside_cell);
    expect_filter(
        "outside required cell rejects",
        implication, required_outside, empty, false);

    GridRegion ordinary{RegionIfType(
        RegionType::EQUAL, 1)};
    ordinary.elements.set(then_cell);
    ordinary.elements_neg.set(if_cell);
    expect_filter(
        "ordinary negative cells stay excluded",
        ordinary, required_if, empty, false);
}

void test_implication_input_mapping()
{
    GridRule rule;
    rule.region_count = 3;
    rule.if_reg_count = 1;

    expect_integer(
        "IF slot maps to first input",
        rule.input_index_for_slot(0), 0);
    expect_integer(
        "THEN slot maps to first input",
        rule.input_index_for_slot(1), 0);
    expect_integer(
        "ordinary slot maps to second input",
        rule.input_index_for_slot(2), 1);

    RegionIfType implication_type;
    implication_type.if_type = RegionType(
        RegionType::EQUAL, 0);
    implication_type.type = RegionType(
        RegionType::MORE, 0);
    GridRegion implication(implication_type);
    GridRegion ordinary{RegionIfType(
        RegionType::LESS, 0)};

    // Matcher inputs use raw slots. The
    // THEN slot has no separate region.
    GridRegionCause cause = rule.make_cause(
        &implication, NULL, &ordinary, NULL);
    expect_condition(
        "cause keeps its rule",
        cause.rule == &rule);
    expect_condition(
        "cause packs implication input",
        cause.regions[0] == &implication);
    expect_condition(
        "cause packs ordinary input",
        cause.regions[1] == &ordinary);
    expect_condition(
        "cause clears unused inputs",
        !cause.regions[2] && !cause.regions[3]);
}

void test_remove_implication_input()
{
    GridRule rule;
    rule.region_count = 3;
    rule.if_reg_count = 1;
    rule.region_type[0] = RegionType(
        RegionType::EQUAL, 0);
    rule.region_type[1] = RegionType(
        RegionType::MORE, 0);
    rule.region_type[2] = RegionType(
        RegionType::LESS, 0);

    // The action covers the ordinary
    // input for every IF/THEN state.
    rule.apply_region_bitmap =
        (1 << 4) | (1 << 5) |
        (1 << 6) | (1 << 7);

    rule.remove_region(0);

    expect_integer(
        "removing implication leaves one slot",
        rule.region_count, 1);
    expect_integer(
        "removing implication clears pair count",
        rule.if_reg_count, 0);
    expect_condition(
        "ordinary input remains after removal",
        rule.region_type[0] == RegionType(
            RegionType::LESS, 0));
    expect_integer(
        "ordinary action survives pair removal",
        rule.apply_region_bitmap, 1 << 1);
}

void test_remove_ordinary_from_implication()
{
    GridRule rule;
    rule.region_count = 3;
    rule.if_reg_count = 1;
    rule.region_type[0] = RegionType(
        RegionType::EQUAL, 0);
    rule.region_type[1] = RegionType(
        RegionType::MORE, 0);
    rule.region_type[2] = RegionType(
        RegionType::LESS, 0);

    rule.apply_if_region_type = RegionType(
        RegionType::EQUAL, 1);
    rule.apply_region_type = RegionType(
        RegionType::EQUAL, 2);

    // IF and THEN output areas are
    // independent implication halves.
    rule.neg_apply_region_bitmap =
        (1 << 2) | (1 << 6);
    rule.apply_region_bitmap =
        (1 << 1) | (1 << 5);

    rule.remove_region(2);

    expect_integer(
        "ordinary removal keeps implication",
        rule.if_reg_count, 1);
    expect_integer(
        "ordinary removal keeps two slots",
        rule.region_count, 2);
    expect_integer(
        "THEN action survives ordinary removal",
        rule.apply_region_bitmap, 1 << 1);
    expect_integer(
        "IF action survives ordinary removal",
        rule.neg_apply_region_bitmap, 1 << 2);
}

void test_remove_region_action_conflicts()
{
    GridRule action_first;
    action_first.region_count = 2;
    action_first.apply_region_bitmap = 1 << 2;
    action_first.remove_region(0);
    expect_integer(
        "action then empty clears conflict",
        action_first.apply_region_bitmap, 0);

    GridRule empty_first;
    empty_first.region_count = 2;
    empty_first.apply_region_bitmap = 1 << 3;
    empty_first.remove_region(0);
    expect_integer(
        "empty then action clears conflict",
        empty_first.apply_region_bitmap, 0);
}

void test_remove_region_if_only_action()
{
    GridRule rule;
    rule.region_count = 2;
    rule.apply_if_region_type = RegionType(
        RegionType::EQUAL, 1);
    rule.apply_region_type = RegionType(
        RegionType::EQUAL, 2);

    // Both source areas carry only the
    // IF half of the output implication.
    rule.neg_apply_region_bitmap =
        (1 << 2) | (1 << 3);

    rule.remove_region(0);

    expect_integer(
        "IF-only action keeps no THEN half",
        rule.apply_region_bitmap, 0);
    expect_integer(
        "IF-only action survives removal",
        rule.neg_apply_region_bitmap, 1 << 1);
}

void test_then_only_loads_as_ordinary()
{
    SaveObjectMap saved;
    saved.add_num("region_count", 1);
    saved.add_num(
        "apply_region_type",
        RegionType(RegionType::EQUAL, 2).as_int());
    saved.add_num(
        "apply_if_region_type",
        RegionType(RegionType::EQUAL, 1).as_int());
    saved.add_num("apply_region_bitmap", 1 << 1);

    SaveObjectList* region_types =
        new SaveObjectList;
    region_types->add_num(
        RegionType(RegionType::EQUAL, 0).as_int());
    saved.add_item("region_type", region_types);
    saved.add_item(
        "square_counts", new SaveObjectList);

    GridRule rule(&saved);
    expect_integer(
        "THEN-only load keeps consequent",
        rule.apply_region_bitmap, 1 << 1);
    expect_condition(
        "THEN-only load becomes ordinary",
        rule.apply_if_region_type.type ==
            RegionType::NONE);
}

void test_rule_structure_validation()
{
    GridRule valid;
    valid.region_count = 3;
    valid.if_reg_count = 1;
    expect_condition(
        "complete implication is valid",
        valid.has_valid_structure());

    GridRule truncated;
    truncated.region_count = 1;
    truncated.if_reg_count = 1;
    expect_condition(
        "truncated implication is rejected",
        !truncated.has_valid_structure());

    GridRule too_many_pairs;
    too_many_pairs.region_count = 4;
    too_many_pairs.if_reg_count = 3;
    expect_condition(
        "too many implication pairs rejected",
        !too_many_pairs.has_valid_structure());

    GridRule too_many_dimensions;
    too_many_dimensions.region_count = 4;
    too_many_dimensions.neg_reg_count = 1;
    expect_condition(
        "too many dimensions are rejected",
        !too_many_dimensions.has_valid_structure());

    GridRule mixed_layouts;
    mixed_layouts.region_count = 3;
    mixed_layouts.if_reg_count = 1;
    mixed_layouts.neg_reg_count = 1;
    expect_condition(
        "mixed implication layout rejected",
        !mixed_layouts.has_valid_structure());
}

void test_clear_action_cannot_be_implication()
{
    GridRule rule;
    rule.region_count = 1;
    rule.region_type[0] = RegionType(
        RegionType::EQUAL, 0);
    rule.apply_if_region_type = RegionType(
        RegionType::EQUAL, 0);
    rule.apply_region_type = RegionType(
        RegionType::SET, 0);
    rule.apply_region_bitmap = 1 << 1;
    rule.neg_apply_region_bitmap = 1 << 1;

    expect_condition(
        "SET implication keeps valid inputs",
        rule.has_valid_structure());
    expect_legality(
        "SET action cannot be implication",
        rule, GridRule::IMPOSSIBLE);
}

void test_pictured_wildcard_rule()
{
    constexpr int variable_x = 1;
    constexpr int first_zero_area = 2;
    constexpr int last_zero_area = 5;
    constexpr int first_implication = 1 << 0;

    GridRule rule;
    rule.region_count = 3;
    rule.if_reg_count = 1;

    rule.region_type[0] = RegionType(
        RegionType::NONE, 0);
    rule.region_type[1] = RegionType(
        RegionType::EQUAL, 0);
    rule.region_type[1].var = variable_x;
    rule.region_type[2] = rule.region_type[1];

    // These are the four zero areas shown
    // in the reported rule.
    for (int area = first_zero_area;
         area <= last_zero_area; area++)
        rule.square_counts[area] = RegionType(
            RegionType::EQUAL, 0);

    rule.apply_region_type = RegionType(
        RegionType::VISIBILITY,
        GRID_VIS_LEVEL_BIN);
    rule.apply_region_bitmap = first_implication;

    expect_legality(
        "pictured wildcard antecedent rule",
        rule, GridRule::OK);
}

void test_trash_wildcard_consequent()
{
    GridRule rule;
    rule.region_count = 2;
    rule.if_reg_count = 1;

    rule.region_type[0] = RegionType(
        RegionType::EQUAL, 0);
    rule.region_type[1] = RegionType(
        RegionType::NONE, 0);

    rule.apply_region_type = RegionType(
        RegionType::VISIBILITY,
        GRID_VIS_LEVEL_BIN);
    rule.apply_region_bitmap = 1;

    expect_legality(
        "trash wildcard consequent",
        rule, GridRule::LOSES_DATA);
}

void test_wildcard_if_is_not_true()
{
    GridRule rule;
    rule.region_count = 2;
    rule.if_reg_count = 1;

    rule.region_type[0] = RegionType(
        RegionType::NONE, 0);
    rule.region_type[1] = RegionType(
        RegionType::EQUAL, 0);

    rule.apply_region_type = RegionType(
        RegionType::EQUAL, 0);
    rule.apply_region_bitmap =
        (1 << 2) | (1 << 3);

    expect_legality(
        "wildcard IF is not unconditional",
        rule, GridRule::ILLOGICAL);
}

void test_second_wildcard_implication()
{
    GridRule rule;
    rule.region_count = 4;
    rule.if_reg_count = 2;

    rule.region_type[0] = RegionType(
        RegionType::EQUAL, 0);
    rule.region_type[1] = RegionType(
        RegionType::EQUAL, 0);
    rule.region_type[2] = RegionType(
        RegionType::NONE, 0);
    rule.region_type[3] = RegionType(
        RegionType::EQUAL, 0);

    // Give both implication pairs an
    // overlapping area.
    rule.square_counts[5] = RegionType(
        RegionType::EQUAL, 1);

    rule.apply_region_type = RegionType(
        RegionType::VISIBILITY,
        GRID_VIS_LEVEL_BIN);
    rule.apply_region_bitmap = 1 << 2;

    expect_legality(
        "second wildcard implication",
        rule, GridRule::LOSES_DATA);
}

void test_tautological_wildcard_consequent()
{
    GridRule rule;
    rule.region_count = 2;
    rule.if_reg_count = 1;

    rule.region_type[0] = RegionType(
        RegionType::EQUAL, 2);
    rule.region_type[1] = RegionType(
        RegionType::NONE, 0);
    rule.square_counts[1] = RegionType(
        RegionType::EQUAL, 0);
    rule.square_counts[2] = RegionType(
        RegionType::EQUAL, 1);
    rule.square_counts[3] = RegionType(
        RegionType::EQUAL, 1);

    rule.apply_region_type = RegionType(
        RegionType::VISIBILITY,
        GRID_VIS_LEVEL_BIN);
    rule.apply_region_bitmap = 1 << 0;

    expect_legality(
        "tautological wildcard consequent",
        rule, GridRule::OK);
}

void test_ordinary_wildcard_still_loses_data()
{
    GridRule rule;
    rule.region_count = 3;
    rule.if_reg_count = 1;

    rule.region_type[0] = RegionType(
        RegionType::EQUAL, 0);
    rule.region_type[1] = RegionType(
        RegionType::EQUAL, 0);
    rule.region_type[2] = RegionType(
        RegionType::NONE, 0);
    rule.square_counts[5] = RegionType(
        RegionType::EQUAL, 1);

    rule.apply_region_type = RegionType(
        RegionType::VISIBILITY,
        GRID_VIS_LEVEL_BIN);
    rule.apply_region_bitmap = 1 << 2;

    expect_legality(
        "ordinary wildcard loses data",
        rule, GridRule::LOSES_DATA);
}

// Exposes the rhombille geometry the recursed board tests check.
class TestRhombilleGrid : public RhombilleGrid
{
public:
    TestRhombilleGrid(XYPos size_)
    {
        size = size_;
        wrapped = WRAPPED_IN;
    }
    using RhombilleGrid::get_corners;
    using RhombilleGrid::piece_in_board;
    using RhombilleGrid::innie_allowed;
    using RhombilleGrid::get_recursion;
};

void test_rhombille_recursed_geometry()
{
    // Equilateral triangles cut from rhombi along the board's edges, from
    // working the board out by hand for each size. The two triangles at a
    // corner of the board are joined into one rhombus, so only those along
    // the sides are left.
    const int expected_triangles[] = {0, 0, 0, 6, 0, 6, 12, 6};
    for (int side = 1; side <= 7; side++)
    {
        std::string name = "rhombille recursed side " + std::to_string(side);
        XYPos size = RhombilleGrid::recursed_size(side);
        expect_condition((name + " fits in XYSet").c_str(),
                         size.x <= 32 && size.y <= 32 && size.x % 3 == 0);

        TestRhombilleGrid g(size);
        // In triangles: the board hexagon is 6 * side^2 of them.
        int area = 0;
        int triangles = 0;
        XYPos extent;
        FOR_XY(pos, XYPos(), size)
        {
            if (!g.piece_in_board(pos))
                continue;
            XYPos corners[4];
            int n = g.get_corners(pos, corners);
            area += n - 2;
            if (n == 3)
                triangles++;
            for (int i = 0; i < n; i++)
                extent = XYPos(std::max(extent.x, corners[i].x), std::max(extent.y, corners[i].y));
        }
        expect_integer((name + " area").c_str(), area, 6 * side * side);
        expect_integer((name + " helper triangles").c_str(), triangles, expected_triangles[side]);
        expect_condition((name + " bounding box").c_str(), extent == XYPos(4 * side, 2 * side));

        int innies = 0;
        FOR_XY(pos, XYPos(), size)
            if (g.piece_in_board(pos) && g.innie_allowed(pos))
                innies++;
        // Smaller boards have no hexagon clear of the edges.
        expect_condition((name + " innie spots").c_str(), (innies > 0) == (side >= 3));
    }

    // A size that doesn't fit the hexagon isn't a recursed board.
    TestRhombilleGrid odd(XYPos(12, 3));
    XYPos board_ref, innie_ref;
    int scale;
    expect_condition("rhombille recursed wrong size",
                     !odd.get_recursion(board_ref, innie_ref, scale));
}

void test_rhombille_recursed_board()
{
    for (int side = 3; side <= 7; side++)
    {
        std::string name = "rhombille recursed board side " + std::to_string(side);
        RhombilleGrid g;
        g.randomize(RhombilleGrid::recursed_size(side), Grid::WRAPPED_IN, 3, 0, 0);
        expect_condition((name + " has innie").c_str(),
                         g.merged.count(g.innie_pos) && !g.get_squares().get(g.innie_pos));

        XYSet squares = g.get_squares();
        bool symmetric = true;
        bool connected = true;
        FOR_XY_SET(a, squares)
        {
            XYSet neighbours = g.get_neighbors(a);
            if (neighbours.none() || neighbours.get(a))
                connected = false;
            FOR_XY_SET(b, neighbours)
                if (!squares.get(b) || !g.get_neighbors(b).get(a))
                    symmetric = false;
        }
        expect_condition((name + " neighbours symmetric").c_str(), symmetric);
        expect_condition((name + " neighbours sane").c_str(), connected);

        // Cells on the innie's edge also touch the copy of the board's edge
        // inside it, so they have more neighbours than on a flat board.
        RhombilleGrid flat(g);
        flat.wrapped = Grid::WRAPPED_NOT;
        bool through_innie = false;
        FOR_XY_SET(a, squares)
            if (g.get_neighbors(a).count() > (flat.get_neighbors(a) & squares).count())
                through_innie = true;
        expect_condition((name + " neighbours through innie").c_str(), through_innie);

        std::string s = g.to_string();
        Grid* loaded = Grid::Load(s);
        expect_condition((name + " round trip").c_str(),
                         loaded->to_string() == s && loaded->get_squares() == squares);
        delete loaded;
    }
}

}

int main()
{
    test_implication_region_filters();
    test_implication_input_mapping();
    test_remove_implication_input();
    test_remove_ordinary_from_implication();
    test_remove_region_action_conflicts();
    test_remove_region_if_only_action();
    test_then_only_loads_as_ordinary();
    test_rule_structure_validation();
    test_clear_action_cannot_be_implication();
    test_pictured_wildcard_rule();
    test_trash_wildcard_consequent();
    test_wildcard_if_is_not_true();
    test_second_wildcard_implication();
    test_tautological_wildcard_consequent();
    test_ordinary_wildcard_still_loses_data();
    test_rhombille_recursed_geometry();
    test_rhombille_recursed_board();

    if (failures)
        std::cerr << failures
                  << " test(s) failed\n";
    return failures ? 1 : 0;
}

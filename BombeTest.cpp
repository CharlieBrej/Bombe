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

    if (failures)
        std::cerr << failures
                  << " test(s) failed\n";
    return failures ? 1 : 0;
}

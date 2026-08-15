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

#include "Grid.h"

#include <iostream>

void global_mutex_lock() {}
void global_mutex_unlock() {}

static unsigned partition_count(
    int partition, GridRegion* r0, GridRegion* r1,
    GridRegion* r2, GridRegion* r3)
{
    GridRegion* regions[4] = {r0, r1, r2, r3};
    unsigned count = 0;
    for (unsigned index = 0; index < 32 * 32; index++)
    {
        bool matches = true;
        for (int region = 0; region < 4; region++)
        {
            const bool in_region = regions[region]->elements.get(index);
            if (in_region != bool(partition & (1 << region)))
                matches = false;
        }
        if (matches)
            count++;
    }
    return count;
}

// This is the old, non-JIT matcher that was left commented out in Grid.cpp,
// specialized only by removing support for negative/IF regions. The captured
// failing rule uses neither.
static bool old_matches(
    GridRule& rule, GridRegion* r0, GridRegion* r1,
    GridRegion* r2, GridRegion* r3, int var_counts[32])
{
    GridRegion* regions[4] = {r0, r1, r2, r3};

    for (int i = 0; i < rule.region_count; i++)
    {
        if (rule.region_type[i].type != RegionType::NONE &&
            rule.region_type[i].type != regions[i]->type.type)
            return false;
        if (rule.region_type[i].var)
        {
            const int vi = rule.region_type[i].var - 1;
            const int value = regions[i]->type.value - rule.region_type[i].value;
            if (value < 0)
                return false;
            if (var_counts[vi] < 0)
                var_counts[vi] = value;
            else if (var_counts[vi] != value)
                return false;
        }
    }

    for (int i = 1; i < (1 << rule.region_count); i++)
    {
        if (rule.square_counts[i].type != RegionType::EQUAL ||
            !rule.square_counts[i].var)
            continue;
        const int vi = rule.square_counts[i].var - 1;
        const int value = int(partition_count(i, r0, r1, r2, r3)) -
                          rule.square_counts[i].value;
        if (value < 0)
            return false;
        if (var_counts[vi] < 0)
            var_counts[vi] = value;
    }

    for (int i = 1; i < 32; i++)
    {
        if (var_counts[i - 1] < 0)
            continue;
        for (int j = 1; j < 32; j++)
        {
            if (i == j || var_counts[j - 1] < 0)
                continue;
            if ((i & j) == 0)
            {
                const int index = (i | j) - 1;
                const int value = var_counts[i - 1] + var_counts[j - 1];
                if (var_counts[index] < 0)
                    var_counts[index] = value;
                else if (var_counts[index] != value)
                    return false;
            }
            else if ((i & j) == i)
            {
                const int index = (j & ~i) - 1;
                const int value = var_counts[j - 1] - var_counts[i - 1];
                if (value < 0)
                    return false;
                if (var_counts[index] < 0)
                    var_counts[index] = value;
                else if (var_counts[index] != value)
                    return false;
            }
            else
            {
                const int x = i ^ j;
                if (var_counts[x - 1] < 0)
                    continue;
                const int sum = var_counts[i - 1] + var_counts[j - 1] +
                                var_counts[x - 1];
                if (sum % 2)
                    return false;
                const int index = (i | j) - 1;
                const int value = sum / 2;
                if (var_counts[index] < 0)
                    var_counts[index] = value;
                else if (var_counts[index] != value)
                    return false;
            }
        }
    }

    for (int i = 1; i < (1 << rule.region_count); i++)
    {
        if (rule.square_counts[i].type == RegionType::NONE)
            continue;
        if (!rule.square_counts[i].apply_int_rule(
                partition_count(i, r0, r1, r2, r3), var_counts))
            return false;
    }
    return true;
}

static void set_cells(XYSet& set, std::initializer_list<unsigned> cells)
{
    for (unsigned cell : cells)
        set.set(cell);
}

int main()
{
    GridRule rule;
    rule.region_count = 4;
    rule.region_type[0] = RegionType(RegionType::MORE, 0);
    rule.region_type[0].var = 7;
    rule.region_type[1] = RegionType(RegionType::MORE, 0);
    rule.region_type[1].var = 11;
    rule.region_type[2] = RegionType(RegionType::NOTEQUAL, 0);
    rule.region_type[2].var = 13;
    rule.region_type[3] = RegionType(RegionType::LESS, 0);
    rule.region_type[3].var = 14;

    for (int i : {1, 2, 3, 4, 5, 6, 9, 10, 15})
        rule.square_counts[i] = RegionType(RegionType::EQUAL, 0);
    rule.square_counts[7] = RegionType(RegionType::EQUAL, 1);
    rule.square_counts[7].var = 1;
    rule.square_counts[11] = RegionType(RegionType::EQUAL, 0);
    rule.square_counts[11].var = 2;
    rule.apply_region_type = RegionType(RegionType::SET, 1);
    rule.apply_region_bitmap = 1 << 7;

    GridRegion r0(RegionType(RegionType::MORE, 3));
    GridRegion r1(RegionType(RegionType::MORE, 7));
    GridRegion r2(RegionType(RegionType::NOTEQUAL, 4));
    GridRegion r3(RegionType(RegionType::LESS, 4));
    set_cells(r0.elements, {5, 6, 37, 70});
    set_cells(r1.elements, {3, 5, 6, 35, 37, 70, 193, 194});
    set_cells(r2.elements, {3, 5, 6, 35, 37, 70, 193, 194});
    set_cells(r3.elements, {1, 3, 34, 35, 193, 194});

    GridRule why;
    int legal_vars[5] = {};
    const GridRule::IsLogicalRep legality = rule.is_legal(why, legal_vars);

    int old_vars[32];
    int jit_vars[32];
    for (int& value : old_vars)
        value = -1;
    for (int& value : jit_vars)
        value = -1;

    const bool old_match = old_matches(
        rule, &r0, &r1, &r2, &r3, old_vars);

    GridRule::FastOpGroup ops;
    rule.jit_preprocess(ops);
    const bool jit_match =
        rule.jit_matches(ops.ops[0], false, &r0, nullptr, nullptr, nullptr, jit_vars) &&
        rule.jit_matches(ops.ops[1], false, &r0, &r1, nullptr, nullptr, jit_vars) &&
        rule.jit_matches(ops.ops[2], false, &r0, &r1, &r2, nullptr, jit_vars) &&
        rule.jit_matches(ops.ops[3], true, &r0, &r1, &r2, &r3, jit_vars);

    std::cout << "is_legal: " << int(legality) << " (0=OK, 1=ILLOGICAL)\n";
    std::cout << "old_matches: " << old_match << '\n';
    std::cout << "jit_matches: " << jit_match << '\n';
    std::cout << "jit V1=" << jit_vars[0]
              << " V4=" << jit_vars[3]
              << " V8=" << jit_vars[7]
              << " V12=" << jit_vars[11]
              << " V13=" << jit_vars[12] << '\n';

    // The rule is abstractly legal, but this concrete region tuple is
    // rejected by both the old matcher and the corrected JIT matcher.
    return legality == GridRule::OK && !old_match && !jit_match ? 0 : 1;
}

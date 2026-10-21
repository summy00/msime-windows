#include "ipc/candidate_ui_action_policy.h"
#include "tests/includes/test_framework.h"

TEST_CASE(candidate_ui_action_accepts_exact_one_based_page_range)
{
    REQUIRE(FanyImeIpc::IsValidCandidateUiOneBasedIndex(1));
    REQUIRE(FanyImeIpc::IsValidCandidateUiOneBasedIndex(9));
    REQUIRE(FanyImeIpc::IsValidCandidateUiOneBasedIndex(10));

    REQUIRE(!FanyImeIpc::IsValidCandidateUiOneBasedIndex(0));
    REQUIRE(!FanyImeIpc::IsValidCandidateUiOneBasedIndex(-1));
    REQUIRE(!FanyImeIpc::IsValidCandidateUiOneBasedIndex(11));
}

TEST_CASE(candidate_down_expands_once_even_before_the_ui_handles_the_request)
{
    using FanyImeIpc::CandidateExpandState;
    std::atomic<CandidateExpandState> state{CandidateExpandState::Unavailable};
    REQUIRE(!FanyImeIpc::RequestCandidateExpandOnDown(0x28, 0, false, state));

    FanyImeIpc::PublishCandidateExpandAvailability(state, true, false);
    REQUIRE(FanyImeIpc::RequestCandidateExpandOnDown(0x28, 0, false, state));
    REQUIRE_EQ(state.load(), CandidateExpandState::Requested);
    // A queued repaint and a second Down must not cancel or duplicate the first request.
    FanyImeIpc::PublishCandidateExpandAvailability(state, true, false);
    REQUIRE_EQ(state.load(), CandidateExpandState::Requested);
    REQUIRE(!FanyImeIpc::RequestCandidateExpandOnDown(0x28, 0, false, state));

    FanyImeIpc::PublishCandidateExpandAvailability(state, true, true);
    REQUIRE_EQ(state.load(), CandidateExpandState::Expanded);
    REQUIRE(!FanyImeIpc::RequestCandidateExpandOnDown(0x28, 0, false, state));
}

TEST_CASE(candidate_down_expansion_leaves_other_keys_and_native_candidates_unchanged)
{
    using FanyImeIpc::CandidateExpandState;
    std::atomic<CandidateExpandState> state{CandidateExpandState::Collapsed};
    for (const uint32_t key : {0x26u, 0x25u, 0x27u, 0x20u})
        REQUIRE(!FanyImeIpc::RequestCandidateExpandOnDown(key, 0, false, state));
    for (const uint32_t modifiers : {1u, 2u, 4u, 3u, 7u})
        REQUIRE(!FanyImeIpc::RequestCandidateExpandOnDown(0x28, modifiers, false, state));
    REQUIRE(!FanyImeIpc::RequestCandidateExpandOnDown(0x28, 0, true, state));
    REQUIRE_EQ(state.load(), CandidateExpandState::Collapsed);
}

TEST_CASE(candidate_down_expansion_resets_after_hide_or_switching_to_a_non_expandable_skin)
{
    using FanyImeIpc::CandidateExpandState;
    std::atomic<CandidateExpandState> state{CandidateExpandState::Collapsed};
    REQUIRE(FanyImeIpc::RequestCandidateExpandOnDown(0x28, 0, false, state));
    FanyImeIpc::PublishCandidateExpandAvailability(state, false, false);
    REQUIRE_EQ(state.load(), CandidateExpandState::Unavailable);
    REQUIRE(!FanyImeIpc::RequestCandidateExpandOnDown(0x28, 0, false, state));

    FanyImeIpc::PublishCandidateExpandAvailability(state, true, false);
    REQUIRE(FanyImeIpc::RequestCandidateExpandOnDown(0x28, 0, false, state));
}

TEST_CASE(candidate_row_navigation_uses_the_nearest_column_in_the_adjacent_visual_row)
{
    const std::vector<FanyImeIpc::CandidateRowPosition> positions{{15, 0},  {75, 0},   {130, 0}, {20, 30},
                                                                  {60, 30}, {100, 30}, {20, 60}};
    REQUIRE_EQ(FanyImeIpc::CandidateInAdjacentRow(positions, 1, 1), 4);
    REQUIRE_EQ(FanyImeIpc::CandidateInAdjacentRow(positions, 4, -1), 1);
    REQUIRE_EQ(FanyImeIpc::CandidateInAdjacentRow(positions, 5, 1), 6);
    REQUIRE_EQ(FanyImeIpc::CandidateInAdjacentRow(positions, 0, -1), -1);
    REQUIRE_EQ(FanyImeIpc::CandidateInAdjacentRow(positions, 6, 1), -1);
    REQUIRE_EQ(FanyImeIpc::CandidateInAdjacentRow(positions, -1, 1), -1);
    REQUIRE_EQ(FanyImeIpc::CandidateInAdjacentRow(positions, 7, 1), -1);
    REQUIRE_EQ(FanyImeIpc::CandidateInAdjacentRow({}, 0, 1), -1);
}

TEST_CASE(candidate_row_navigation_is_unchanged_by_dpi_scaling_and_scrolling)
{
    for (const float scale : {1.0f, 1.25f, 1.5f, 2.0f})
    {
        const std::vector<FanyImeIpc::CandidateRowPosition> positions{
            {20 * scale, -60 * scale}, {80 * scale, -60 * scale}, {20 * scale, -30 * scale}, {80 * scale, -30 * scale}};
        REQUIRE_EQ(FanyImeIpc::CandidateInAdjacentRow(positions, 1, 1), 3);
        REQUIRE_EQ(FanyImeIpc::CandidateInAdjacentRow(positions, 3, -1), 1);
    }
}

TEST_CASE(candidate_repeated_down_keeps_the_original_column_through_a_wide_candidate)
{
    const std::vector<FanyImeIpc::CandidateRowPosition> positions{{20, 0, 20},  {80, 30, 80},  {20, 60, 20},
                                                                  {60, 60, 20}, {100, 60, 20}, {140, 60, 20}};
    FanyImeIpc::CandidateArrowNavigation navigation;
    const int secondRow = navigation.Target(positions, 0, 0x28);
    REQUIRE_EQ(secondRow, 1);
    REQUIRE_EQ(navigation.Target(positions, secondRow, 0x28), 2);
    REQUIRE_EQ(navigation.Target(positions, 2, 0x26), 1);
    REQUIRE_EQ(navigation.Target(positions, 1, 0x26), 0);
}

TEST_CASE(candidate_vertical_navigation_prefers_the_item_covering_the_column_over_a_nearer_center)
{
    const std::vector<FanyImeIpc::CandidateRowPosition> positions{{220, 0, 20}, {120, 30, 120}, {260, 30, 20}};
    FanyImeIpc::CandidateArrowNavigation navigation;
    REQUIRE_EQ(navigation.Target(positions, 0, 0x28), 1);
}

TEST_CASE(candidate_four_arrow_sequence_stays_in_visual_rows_and_preserves_vertical_column)
{
    const std::vector<FanyImeIpc::CandidateRowPosition> positions{
        {20, 0, 18},   {60, 0, 18},  {100, 0, 18}, {140, 0, 18},  {40, 30, 38}, {100, 30, 18},
        {140, 30, 18}, {20, 60, 18}, {60, 60, 18}, {100, 60, 18}, {140, 60, 18}};
    FanyImeIpc::CandidateArrowNavigation navigation;
    int current = 0;
    const std::vector<std::pair<uint32_t, int>> sequence{
        {0x25, -1}, {0x27, 1}, {0x28, 4},  {0x28, 8}, {0x28, -1}, {0x26, 4}, {0x27, 5},  {0x28, 9},
        {0x26, 5},  {0x26, 2}, {0x26, -1}, {0x27, 3}, {0x27, -1}, {0x28, 6}, {0x27, -1}, {0x25, 5}};
    for (const auto &[key, expected] : sequence)
    {
        const int target = navigation.Target(positions, current, key);
        REQUIRE_EQ(target, expected);
        if (target >= 0)
            current = target;
    }
}

TEST_CASE(candidate_vertical_column_survives_a_short_last_row_and_is_reset_by_horizontal_movement)
{
    const std::vector<FanyImeIpc::CandidateRowPosition> positions{
        {20, 0, 18}, {60, 0, 18}, {100, 0, 18}, {20, 30, 18}, {60, 30, 18}, {100, 30, 18}, {20, 60, 18}};
    FanyImeIpc::CandidateArrowNavigation navigation;
    REQUIRE_EQ(navigation.Target(positions, 2, 0x28), 5);
    REQUIRE_EQ(navigation.Target(positions, 5, 0x28), 6);
    REQUIRE_EQ(navigation.Target(positions, 6, 0x26), 5);
    REQUIRE_EQ(navigation.Target(positions, 5, 0x25), 4);
    REQUIRE_EQ(navigation.Target(positions, 4, 0x26), 1);
    REQUIRE_EQ(navigation.Target(positions, 1, 0x28), 4);
    navigation.Reset();
    REQUIRE_EQ(navigation.Target(positions, 6, 0x26), 3);
    REQUIRE_EQ(navigation.Target(positions, -1, 0x28), -1);
    REQUIRE_EQ(navigation.Target(positions, 7, 0x28), -1);
    REQUIRE_EQ(navigation.Target({}, 0, 0x28), -1);
    REQUIRE_EQ(navigation.Target(positions, 0, 0x20), -1);
}

TEST_CASE(candidate_column_is_reanchored_when_selection_or_panel_width_changes)
{
    std::vector<FanyImeIpc::CandidateRowPosition> positions{{20, 0, 20},  {80, 30, 80},  {20, 60, 20},
                                                            {60, 60, 20}, {100, 60, 20}, {140, 60, 20}};
    FanyImeIpc::CandidateArrowNavigation navigation;
    REQUIRE_EQ(navigation.Target(positions, 0, 0x28), 1);
    // A narrower panel reflows the wide candidate; the old preferred column no longer applies.
    positions[1] = {60, 30, 60};
    positions.pop_back();
    REQUIRE_EQ(navigation.Target(positions, 1, 0x28), 3);
    // A selection made by another action starts a new column.
    REQUIRE_EQ(navigation.Target(positions, 4, 0x26), 1);
    REQUIRE_EQ(navigation.Target(positions, 1, 0x28), 4);
}

TEST_CASE(candidate_four_arrow_sequence_is_identical_at_100_125_150_and_200_percent_scale)
{
    for (const float scale : {1.0f, 1.25f, 1.5f, 2.0f})
    {
        std::vector<FanyImeIpc::CandidateRowPosition> positions{
            {20, 0, 18}, {60, 0, 18}, {40, 30, 38}, {20, 60, 18}, {60, 60, 18}};
        for (auto &position : positions)
        {
            position.center_x *= scale;
            position.top *= scale;
            position.half_width *= scale;
        }
        FanyImeIpc::CandidateArrowNavigation navigation;
        REQUIRE_EQ(navigation.Target(positions, 1, 0x28), 2);
        REQUIRE_EQ(navigation.Target(positions, 2, 0x28), 4);
        REQUIRE_EQ(navigation.Target(positions, 4, 0x25), 3);
        REQUIRE_EQ(navigation.Target(positions, 3, 0x26), 2);
        REQUIRE_EQ(navigation.Target(positions, 2, 0x26), 0);
    }
}

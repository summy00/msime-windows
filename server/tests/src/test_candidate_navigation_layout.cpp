#include "window/candidate_skin_scene.h"
#include "global/globals.h"
#include "tests/includes/test_framework.h"

#include "msimeui/Scene.h"

#include <algorithm>
#include <cmath>

namespace
{
struct ExpandedPanel
{
    std::shared_ptr<msimeui::CandidateList> list = std::make_shared<msimeui::CandidateList>(28.0f);
    std::shared_ptr<msimeui::CandidateList> grid = std::make_shared<msimeui::CandidateList>(28.0f);
    std::shared_ptr<CandidateExpandContent> content = std::make_shared<CandidateExpandContent>(list, grid);
    msimeui::ScrollViewer scroller{content};

    ExpandedPanel()
    {
        msimeui::CandidateList::Appearance appearance;
        appearance.itemHeight = 28.0f;
        appearance.itemGap = 4.0f;
        appearance.minItemWidth = 60.0f;
        appearance.gridColumns = 4;
        for (auto *control : {list.get(), grid.get()})
        {
            control->SetAppearance(appearance);
            control->SetOrientation(msimeui::CandidateList::Orientation::Horizontal);
        }
        list->SetItems({{L"", L"a", L"", L""}, {L"", L"b", L"", L""}, {L"", L"c", L"", L""}, {L"", L"d", L"", L""}});
        std::vector<msimeui::CandidateList::Item> items;
        for (const auto *word :
             {L"longer word", L"a", L"b", L"c", L"d", L"e", L"f", L"longer word", L"g", L"h", L"i", L"j", L"k", L"l"})
            items.push_back({L"", word, L"", L""});
        grid->SetItems(std::move(items));
        content->SetGap(4.0f);
        content->SetShowGrid(true);
        Layout(316.0f);
    }

    void Layout(float width)
    {
        scroller.MeasureInLayout({width, 60.0f});
        scroller.ArrangeInLayout({17.0f, 23.0f, width, 60.0f});
    }

    msimeui::RectF Bounds(int index) const
    {
        return index < 4 ? list->GetItemBounds(static_cast<size_t>(index))
                         : grid->GetItemBounds(static_cast<size_t>(index - 4));
    }
};
} // namespace

TEST_CASE(candidate_panel_repeated_down_up_follows_actual_arranged_rows_and_scrolls_selection_into_view)
{
    ExpandedPanel panel;
    auto positions = panel.content->NavigationPositions();
    REQUIRE_EQ(positions.size(), size_t{18});
    REQUIRE(std::abs(panel.Bounds(4).width - 156.0f) < 0.01f);
    REQUIRE(std::abs(panel.Bounds(8).x - panel.Bounds(1).x) < 0.01f);

    Global::CandidateUiState state;
    state.page_size = 4;
    state.item_total_count = 18;
    state.expanded_count = 18;
    state.selected_index_in_page = 1;
    const std::vector<std::pair<uint32_t, int>> sequence{{VK_DOWN, 4},  {VK_DOWN, 8}, {VK_DOWN, 11}, {VK_DOWN, 15},
                                                         {VK_DOWN, -1}, {VK_UP, 11},  {VK_UP, 8},    {VK_UP, 4},
                                                         {VK_UP, 1},    {VK_UP, -1}};
    for (const auto &[key, expected] : sequence)
    {
        positions = panel.content->NavigationPositions();
        const int target = state.arrow_navigation.Target(positions, state.selected_index_in_page, key);
        REQUIRE_EQ(target, expected);
        if (target < 0)
            continue;
        REQUIRE(state.move_selection(target - state.selected_index_in_page));
        REQUIRE_EQ(state.selected_index_in_page, expected);
        REQUIRE_EQ(state.page_index, 0);
        panel.scroller.ScrollIntoView(panel.Bounds(target));
        const auto rect = panel.Bounds(target);
        const auto viewport = panel.scroller.GetBounds();
        REQUIRE(rect.y >= viewport.y - 0.01f);
        REQUIRE(rect.y + rect.height <= viewport.y + viewport.height + 0.01f);
    }
    REQUIRE(std::abs(panel.scroller.GetScrollOffset()) < 0.01f);
}

TEST_CASE(candidate_panel_left_right_visit_only_neighbors_in_the_same_actual_row)
{
    ExpandedPanel panel;
    const auto positions = panel.content->NavigationPositions();
    for (int current = 0; current < static_cast<int>(positions.size()); ++current)
    {
        FanyImeIpc::CandidateArrowNavigation navigation;
        const bool hasLeft = current > 0 && std::abs(positions[current - 1].top - positions[current].top) < 0.01f;
        const bool hasRight = current + 1 < static_cast<int>(positions.size()) &&
                              std::abs(positions[current + 1].top - positions[current].top) < 0.01f;
        REQUIRE_EQ(navigation.Target(positions, current, VK_LEFT), hasLeft ? current - 1 : -1);
        REQUIRE_EQ(navigation.Target(positions, current, VK_RIGHT), hasRight ? current + 1 : -1);
    }
}

TEST_CASE(candidate_panel_navigation_coordinates_survive_scroll_and_relayout)
{
    ExpandedPanel panel;
    for (const float width : {316.0f, 200.0f, 480.0f, 316.0f})
    {
        panel.Layout(width);
        const auto before = panel.content->NavigationPositions();
        panel.scroller.SetScrollOffset(panel.scroller.GetMaxScrollOffset());
        const auto after = panel.content->NavigationPositions();
        REQUIRE_EQ(before.size(), after.size());
        for (size_t i = 0; i < after.size(); ++i)
        {
            REQUIRE(std::abs(before[i].center_x - after[i].center_x) < 0.01f);
            REQUIRE(std::abs(before[i].top - after[i].top) < 0.01f);
            const auto rect = panel.Bounds(static_cast<int>(i));
            const auto content = panel.content->GetBounds();
            // Published geometry must match the rectangles used for painting and mouse hit testing.
            REQUIRE(std::abs(after[i].center_x - (rect.x + rect.width * 0.5f - content.x)) < 0.01f);
            REQUIRE(std::abs(after[i].top - (rect.y - content.y)) < 0.01f);
        }
        panel.scroller.SetScrollOffset(0.0f);
    }
}

TEST_CASE(wechat_scroll_skin_keyboard_targets_match_the_final_scene_layout)
{
    for (const bool light : {false, true})
        for (const float fontSize : {16.0f, 20.0f, 24.0f, 32.0f})
        {
            CandidateSkinScene scene;
            CandidateSkinScene::Settings settings;
            settings.skinId = "wechat_scroll";
            settings.horizontal = true;
            settings.light = light;
            settings.fontSize = fontSize;
            settings.pageSize = 4;
            scene.Apply(settings);
            REQUIRE(scene.ExpandContent() != nullptr);
            scene.List()->SetItems({{L"1", L"测试", L"", L""},
                                    {L"2", L"输入", L"", L""},
                                    {L"3", L"候选", L"", L""},
                                    {L"4", L"位置", L"", L""}});
            std::vector<msimeui::CandidateList::Item> grid;
            for (int i = 0; i < 50; ++i)
                grid.push_back({L"", i % 7 == 0 ? L"这是一个较长的候选词" : L"测试", L"", L""});
            scene.ExpandGrid()->SetItems(std::move(grid));
            scene.ExpandContent()->SetShowGrid(true);
            msimeui::Scene host;
            host.SetRoot(scene.Root());
            for (const float width : {320.0f, 480.0f, 640.0f})
            {
                host.EnsureLayout({width, 320.0f});
                const auto positions = scene.ExpandContent()->NavigationPositions();
                REQUIRE_EQ(positions.size(), size_t{54});
                for (size_t i = 0; i < positions.size(); ++i)
                {
                    const auto rect = i < 4 ? scene.List()->GetItemBounds(i) : scene.ExpandGrid()->GetItemBounds(i - 4);
                    const auto content = scene.ExpandContent()->GetBounds();
                    REQUIRE(std::abs(rect.x + rect.width * 0.5f - content.x - positions[i].center_x) < 0.01f);
                    FanyImeIpc::CandidateArrowNavigation navigation;
                    for (const uint32_t key : {VK_LEFT, VK_UP, VK_RIGHT, VK_DOWN})
                    {
                        navigation.Reset();
                        const int target = navigation.Target(positions, static_cast<int>(i), key);
                        if (target < 0)
                        {
                            if (key == VK_UP)
                                REQUIRE(std::none_of(positions.begin(), positions.end(),
                                                     [&](const auto &p) { return p.top < positions[i].top - 0.5f; }));
                            if (key == VK_DOWN)
                                REQUIRE(std::none_of(positions.begin(), positions.end(),
                                                     [&](const auto &p) { return p.top > positions[i].top + 0.5f; }));
                            continue;
                        }
                        const auto &destination = positions[static_cast<size_t>(target)];
                        if (key == VK_LEFT || key == VK_RIGHT)
                        {
                            REQUIRE_EQ(target, static_cast<int>(i) + (key == VK_LEFT ? -1 : 1));
                            REQUIRE(std::abs(destination.top - positions[i].top) < 0.01f);
                        }
                        else
                        {
                            const float direction = key == VK_UP ? -1.0f : 1.0f;
                            REQUIRE((destination.top - positions[i].top) * direction > 0.5f);
                            for (const auto &p : positions)
                                REQUIRE(!((p.top - positions[i].top) * direction > 0.5f &&
                                          (destination.top - p.top) * direction > 0.5f));
                            const float distance = (std::max)(
                                0.0f, std::abs(destination.center_x - positions[i].center_x) - destination.half_width);
                            for (const auto &p : positions)
                                if (std::abs(p.top - destination.top) < 0.01f)
                                    REQUIRE((std::max)(0.0f, std::abs(p.center_x - positions[i].center_x) -
                                                                 p.half_width) >= distance - 0.01f);
                        }
                    }
                }
            }
        }
}

#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

namespace FanyImeIpc
{
struct CandidateRowPosition
{
    float center_x;
    float top;
    float half_width = 0.0f;
};

inline int CandidateInAdjacentRow(const std::vector<CandidateRowPosition> &positions, int current, int direction,
                                  float column = std::numeric_limits<float>::quiet_NaN())
{
    if (current < 0 || static_cast<size_t>(current) >= positions.size() || (direction != -1 && direction != 1))
        return -1;
    const auto &origin = positions[static_cast<size_t>(current)];
    if (!std::isfinite(column))
        column = origin.center_x;
    float closest_row = std::numeric_limits<float>::infinity();
    float closest_column = std::numeric_limits<float>::infinity();
    int target = -1;
    for (size_t i = 0; i < positions.size(); ++i)
    {
        const float row_distance = (positions[i].top - origin.top) * static_cast<float>(direction);
        if (row_distance <= 0.5f)
            continue;
        const float column_distance =
            (std::max)(0.0f, std::abs(positions[i].center_x - column) - positions[i].half_width);
        if (row_distance < closest_row - 0.5f ||
            (std::abs(row_distance - closest_row) <= 0.5f && column_distance < closest_column))
        {
            closest_row = row_distance;
            closest_column = column_distance;
            target = static_cast<int>(i);
        }
    }
    return target;
}

class CandidateArrowNavigation
{
  public:
    void Reset()
    {
        *this = {};
    }

    int Target(const std::vector<CandidateRowPosition> &positions, int current, uint32_t key)
    {
        if (current < 0 || static_cast<size_t>(current) >= positions.size() || key < 0x25u || key > 0x28u)
            return -1;
        const auto &origin = positions[static_cast<size_t>(current)];
        float width = 0.0f;
        for (const auto &position : positions)
            width = (std::max)(width, position.center_x + position.half_width);
        if (current != last_target_ || std::abs(width - layout_width_) > 0.5f)
            column_.reset();
        layout_width_ = width;
        int target = -1;
        if (key == 0x26u || key == 0x28u) // Up / Down
        {
            if (!column_)
                column_ = origin.center_x;
            target = CandidateInAdjacentRow(positions, current, key == 0x26u ? -1 : 1, *column_);
        }
        else // Left / Right stay in the same visual row.
        {
            column_.reset();
            float closest = std::numeric_limits<float>::infinity();
            const float direction = key == 0x25u ? -1.0f : 1.0f;
            for (size_t i = 0; i < positions.size(); ++i)
            {
                const float distance = (positions[i].center_x - origin.center_x) * direction;
                if (std::abs(positions[i].top - origin.top) <= 0.5f && distance > 0.5f && distance < closest)
                {
                    closest = distance;
                    target = static_cast<int>(i);
                }
            }
        }
        last_target_ = target >= 0 ? target : current;
        return target;
    }

  private:
    int last_target_ = -1;
    float layout_width_ = 0.0f;
    std::optional<float> column_;
};

enum class CandidateExpandState
{
    Unavailable,
    Collapsed,
    Requested,
    Expanded,
};

inline void PublishCandidateExpandAvailability(std::atomic<CandidateExpandState> &state, bool available, bool expanded)
{
    if (!available)
        state.store(CandidateExpandState::Unavailable, std::memory_order_release);
    else if (expanded)
        state.store(CandidateExpandState::Expanded, std::memory_order_release);
    else
    {
        // A repaint must preserve a keyboard request queued for the UI thread.
        auto expected = CandidateExpandState::Unavailable;
        state.compare_exchange_strong(expected, CandidateExpandState::Collapsed, std::memory_order_release,
                                      std::memory_order_relaxed);
    }
}

inline bool RequestCandidateExpandOnDown(uint32_t key, uint32_t modifiers, bool ui_less,
                                         std::atomic<CandidateExpandState> &state)
{
    if (key != 0x28u || (modifiers & 0b111u) != 0 || ui_less) // VK_DOWN; Shift / Ctrl / Alt
        return false;
    auto expected = CandidateExpandState::Collapsed;
    return state.compare_exchange_strong(expected, CandidateExpandState::Requested, std::memory_order_acq_rel,
                                         std::memory_order_acquire);
}

inline constexpr int kMinCandidateUiOneBasedIndex = 1;
inline constexpr int kMaxCandidateUiOneBasedIndex = 10;

constexpr bool IsValidCandidateUiOneBasedIndex(int index)
{
    return index >= kMinCandidateUiOneBasedIndex && index <= kMaxCandidateUiOneBasedIndex;
}
} // namespace FanyImeIpc

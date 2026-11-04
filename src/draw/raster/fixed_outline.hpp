#pragma once

// The shape the rasterizer and the stroker work on: contours of points in 26.6 fixed point (1/64
// pixel), each point either "on" the curve or a cubic control point. A cubic segment is two control
// points followed by an on point. There are no quadratic curves here (the path converter turns them
// into cubics first).

#include <vector>

#include "draw/raster/fixed.hpp"

namespace stb::detail {

enum class point_tag : char { on = 1, cubic = 2 };

struct fixed_outline {
    std::vector<fvec> points;
    std::vector<point_tag> tags;
    std::vector<int> contours;  // index of the last point of each contour
    std::vector<char> open;     // per contour: 1 = open (gets caps when stroked), 0 = closed

    bool empty() const noexcept { return points.empty(); }

    // Building, with the same meaning as the path commands. Coordinates are 26.6.
    void move_to(fvec p) {
        points.push_back(p);
        tags.push_back(point_tag::on);
        if (points.size() > 1) contours.push_back(int(points.size()) - 2);  // ends the previous one
        open.push_back(1);
    }
    void line_to(fvec p) {
        points.push_back(p);
        tags.push_back(point_tag::on);
    }
    void cubic_to(fvec c1, fvec c2, fvec p) {
        points.push_back(c1);
        tags.push_back(point_tag::cubic);
        points.push_back(c2);
        tags.push_back(point_tag::cubic);
        points.push_back(p);
        tags.push_back(point_tag::on);
    }
    // Marks the current contour closed and repeats its first point at the end.
    void close() {
        if (open.empty()) return;
        open.back() = 0;
        const std::size_t first = contours.empty() ? 0 : std::size_t(contours.back()) + 1;
        if (first == points.size()) return;
        points.push_back(points[first]);
        tags.push_back(point_tag::on);
    }
    // Ends the last contour; call once, after the last command.
    void end() {
        if (!points.empty()) contours.push_back(int(points.size()) - 1);
    }
};

}  // namespace stb::detail

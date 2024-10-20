// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * @file Test the geom-pathstroke functionality.
 */
/*
 * Authors:
 *   Hendrik Roehm <git@roehm.ws>
 *
 * Copyright (C) 2023 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */
#include "helper/geom-pathstroke.h"

#include <iostream>
#include <gtest/gtest.h>

#include "object/sp-path.h"
#include <2geom/pathvector.h>
#include "svg/svg.h"
#include "test-with-svg-object-pairs.h"

class GeomPathstrokeTest : public Inkscape::TestWithSvgObjectPairs
{
protected:
    GeomPathstrokeTest()
        : Inkscape::TestWithSvgObjectPairs("data/geom-pathstroke.svg", 8) {}
};

double approximate_directed_hausdorff_distance(const Geom::Path *path1, const Geom::Path *path2)
{
    auto const time_range = path1->timeRange();
    double dist_max = 0.;
    for (size_t i = 0; i <= 25; i += 1) {
        auto const time = time_range.valueAt(static_cast<double>(i) / 25);
        auto const search_point = path1->pointAt(time);
        Geom::Coord dist;
        path2->nearestTime(search_point, &dist);
        if (dist > dist_max) {
            dist_max = dist;
        }
    }

    return dist_max;
}

TEST_F(GeomPathstrokeTest, BoundedHausdorffDistance)
{
    double const tolerance = 0.1;
    // same as 0.1 inch in the document (only works without viewBox and transformations)
    auto const offset_width = -9.6;

    unsigned case_index = 0;
    for (auto test_case : getTestCases()) {
        auto const *test_item = cast<SPPath>(test_case.test_object);
        auto const *comp_item = cast<SPPath>(test_case.reference_object);
        ASSERT_TRUE(test_item && comp_item);

        // Note that transforms etc are not considered. Therefore the objects shoud have equal transforms.
        auto const test_curve = test_item->curve();
        auto const comp_curve = comp_item->curve();
        ASSERT_TRUE(test_curve && comp_curve);

        auto const &test_pathvector = test_curve->get_pathvector();
        auto const &comp_pathvector = comp_curve->get_pathvector();
        ASSERT_EQ(test_pathvector.size(), 1);
        ASSERT_EQ(comp_pathvector.size(), 1);

        auto const &test_path = test_pathvector.at(0);
        auto const &comp_path = comp_pathvector.at(0);

        auto const offset_path = Inkscape::half_outline(test_path, offset_width, 0, Inkscape::JOIN_EXTRAPOLATE, 0.);
        double const error1 = approximate_directed_hausdorff_distance(&offset_path, &comp_path);
        double const error2 = approximate_directed_hausdorff_distance(&comp_path, &offset_path);
        double const error = std::max(error1, error2);

        EXPECT_LE(error, tolerance) << "Hausdorff distance above tolerance in test case #" << case_index
                                    << "\nactual d " << sp_svg_write_path(Geom::PathVector(offset_path), true)
                                    << "\nexpected d " << sp_svg_write_path(Geom::PathVector(comp_path), true)
                                    << std::endl;
        case_index++;
    }
}

/*
  Local Variables:
  mode:c++
  c-file-style:"stroustrup"
  c-file-offsets:((innamespace . 0)(inline-open . 0)(case-label . +))
  indent-tabs-mode:nil
  fill-column:99
  End:
*/
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:fileencoding=utf-8:textwidth=99 :
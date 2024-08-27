// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Shared test header for testing colors
 *
 * Copyright (C) 2024 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include <cmath>
#include <iomanip>
#include <gtest/gtest.h>

namespace {

/**
 * Allow the correct tracing of the file and line where data came from when using P_TESTs.
 */
struct traced_data
{
    const char *_file;
    const int _line;

    ::testing::ScopedTrace enable_scope() const { return ::testing::ScopedTrace(_file, _line, ""); }
};
// Macro for the above tracing in P Tests
#define _P(type, ...)                   \
    type                                \
    {                                   \
        __FILE__, __LINE__, __VA_ARGS__ \
    }


/**
 * Print a vector of doubles for debugging
 */
std::string print_values(const std::vector<double> &v) 
{
    std::ostringstream oo; 
    oo << "{";
    bool first = true;
    for (double const &item : v) {
        if (!first) {
            oo << ", ";
        }   
        first = false;
        oo << std::setprecision(3) << item;
    }   
    oo << "}";
    return oo.str();
}

/**
 * Test each value in a values list is within a certain distance from each other.
 */
::testing::AssertionResult VectorIsNear(std::vector<double> const &A, std::vector<double> const &B, double epsilon)
{
    bool is_same = A.size() == B.size();
    for (size_t i = 0; is_same && i < A.size(); i++) {
        is_same = is_same and (std::fabs((A[i]) - (B[i])) < epsilon);
    }
    if (!is_same) {
        return ::testing::AssertionFailure() << "\n" << print_values(A) << "\n != \n" << print_values(B);
    }
    return ::testing::AssertionSuccess();
}


/**
 * Generate a count of random doubles between 0 and 1.
 *
 * Randomly appends an extra value for optional opacity.
 */
inline static std::vector<double> random_values(unsigned ccount)
{
    std::vector<double> values;
    for (unsigned j = 0; j < ccount; j++) {
        values.emplace_back(static_cast<double>(std::rand()) / RAND_MAX);
    }
    // randomly add opacity
    if (std::rand() > (RAND_MAX / 2)) {
        values.emplace_back(static_cast<double>(std::rand()) / RAND_MAX);
    }
    return values;
}

} // namespace

/*
  Local Variables:
  mode:c++
  c-file-style:"stroustrup"
  c-file-offsets:((innamespace . 0)(inline-open . 0))
  indent-tabs-mode:nil
  fill-column:99
  End:
*/
// vim: expandtab:shiftwidth=4:tabstop=8:softtabstop=4 :

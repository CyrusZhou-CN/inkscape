// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef SEEN_NR_FILTER_COLOR_MATRIX_H
#define SEEN_NR_FILTER_COLOR_MATRIX_H

/*
 * feColorMatrix filter primitive renderer
 *
 * Authors:
 *   Felipe Corrêa da Silva Sanches <juca@members.fsf.org>
 *
 * Copyright (C) 2007 authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include <vector>
#include <2geom/forward.h>
#include "display/nr-filter-primitive.h"

typedef unsigned int guint32;
typedef signed int gint32;

namespace Inkscape {
namespace Filters {

class FilterSlot;

enum FilterColorMatrixType
{
    COLORMATRIX_MATRIX,
    COLORMATRIX_SATURATE,
    COLORMATRIX_HUEROTATE,
    COLORMATRIX_LUMINANCETOALPHA,
    COLORMATRIX_ENDTYPE
};

class FilterColorMatrix : public FilterPrimitive
{
public:
    FilterColorMatrix();
    ~FilterColorMatrix() override;

    void render_cairo(FilterSlot &slot) const override;
    bool can_handle_affine(Geom::Affine const &) const override;
    double complexity(Geom::Affine const &ctm) const override;

    virtual void set_type(FilterColorMatrixType type);
    virtual void set_value(double value);
    virtual void set_values(std::vector<double> const &values);

    Glib::ustring name() const override { return Glib::ustring("Color Matrix"); }

public:
    struct ColorMatrixMatrix
    {
        ColorMatrixMatrix(std::vector<double> const &values);
        guint32 operator()(guint32 in) const;
    private:
        gint32 _v[20];
    };

private:
    std::vector<double> values;
    double value;
    FilterColorMatrixType type;
};

} // namespace Filters
} // namespace Inkscape

#endif // SEEN_NR_FILTER_COLOR_MATRIX_H
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

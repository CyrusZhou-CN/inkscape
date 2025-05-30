// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * TODO: insert short description here
 *//*
 * Authors: see git history
 *
 * Copyright (C) 2018 Authors
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */
#ifndef SP_LINEAR_GRADIENT_H
#define SP_LINEAR_GRADIENT_H

/** \file
 * SPLinearGradient: SVG <lineargradient> implementation
 */

#include "sp-gradient.h"
#include "svg/svg-length.h"
#include <2geom/line.h>

/** Linear gradient. */
class SPLinearGradient final
    : public SPGradient
{
public:
    SPLinearGradient();
    ~SPLinearGradient() override;
    int tag() const override { return tag_of<decltype(*this)>; }

    SVGLength x1;
    SVGLength y1;
    SVGLength x2;
    SVGLength y2;

    std::unique_ptr<Inkscape::DrawingPaintServer> create_drawing_paintserver() override;

    Geom::Line getLine() const {
        return Geom::Line({x1.computed, y1.computed}, {x2.computed, y2.computed});
    }

protected:
    void build(SPDocument *document, Inkscape::XML::Node *repr) override;
    void set(SPAttr key, char const *value) override;
    void update(SPCtx *ctx, guint flags) override;
    Inkscape::XML::Node* write(Inkscape::XML::Document *xml_doc, Inkscape::XML::Node *repr, unsigned int flags) override;
};

#endif /* !SP_LINEAR_GRADIENT_H */

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

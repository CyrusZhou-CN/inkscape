// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef SEEN_SP_DRAW_CONTEXT_H
#define SEEN_SP_DRAW_CONTEXT_H

/*
 * Generic drawing context
 *
 * Author:
 *   Lauris Kaplinski <lauris@kaplinski.com>
 *
 * Copyright (C) 2000 Lauris Kaplinski
 * Copyright (C) 2000-2001 Ximian, Inc.
 * Copyright (C) 2002 Lauris Kaplinski
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include <memory>
#include <optional>

#include <sigc++/connection.h>

#include "ui/tools/tool-base.h"
#include "live_effects/effect-enum.h"
#include "display/curve.h"
#include "display/control/canvas-item-ptr.h"

class SPCurve;
class SPCanvasItem;

struct SPDrawAnchor;

namespace Inkscape {
    class CanvasItemBpath;
    class Selection;
}

namespace Inkscape {
namespace UI {
namespace Tools {

enum shapeType { NONE, TRIANGLE_IN, TRIANGLE_OUT, ELLIPSE, CLIPBOARD, BEND_CLIPBOARD, LAST_APPLIED };

class FreehandBase : public ToolBase
{
public:
    FreehandBase(SPDesktop *desktop, std::string &&prefs_path, std::string &&cursor_filename);
    ~FreehandBase() override;

    Selection *selection;

protected:
    uint32_t red_color;
    uint32_t blue_color;
    uint32_t green_color;
    uint32_t highlight_color;

public:
    // Red - Last segment as it's drawn.
    CanvasItemPtr<CanvasItemBpath> red_bpath;
    SPCurve red_curve;
    std::optional<Geom::Point> red_curve_get_last_point();

    // Blue - New path after LPE as it's drawn.
    CanvasItemPtr<CanvasItemBpath> blue_bpath;
    SPCurve blue_curve;

    // Green - New path as it's drawn.
    std::vector<CanvasItemPtr<CanvasItemBpath>> green_bpaths;
    std::shared_ptr<SPCurve> green_curve;
    std::unique_ptr<SPDrawAnchor> green_anchor;
    bool green_closed; // a flag meaning we hit the green anchor, so close the path on itself

    // White
    SPItem *white_item;
    std::vector<std::shared_ptr<SPCurve>> white_curves;
    std::vector<std::unique_ptr<SPDrawAnchor>> white_anchors;

    // Temporary modified curve when start anchor
    std::shared_ptr<SPCurve> sa_overwrited;

    // Start anchor
    SPDrawAnchor *sa;

    // End anchor
    SPDrawAnchor *ea;

    /* Type of the LPE that is to be applied automatically to a finished path (if any) */
    LivePathEffect::EffectType waiting_LPE_type;

    sigc::connection sel_changed_connection;
    sigc::connection sel_modified_connection;

    bool red_curve_is_valid;

    bool anchor_statusbar;
    
    bool tablet_enabled;

    bool is_tablet;

    double pressure;
    void set(Preferences::Entry const &val) override;

    void onSelectionModified();

protected:
    bool root_handler(CanvasEvent const &event) override;
    void _attachSelection();
};

/**
 * Returns FIRST active anchor (the activated one).
 */
SPDrawAnchor *spdc_test_inside(FreehandBase *dc, Geom::Point p);

/**
 * Concats red, blue and green.
 * If any anchors are defined, process these, optionally removing curves from white list
 * Invoke _flush_white to write result back to object.
 */
void spdc_concat_colors_and_flush(FreehandBase *dc, gboolean forceclosed);

/**
 *  Snaps node or handle to PI/rotationsnapsperpi degree increments.
 *
 *  @param dc draw context.
 *  @param p cursor point (to be changed by snapping).
 *  @param o origin point.
 *  @param state  keyboard state to check if ctrl or shift was pressed.
 */
void spdc_endpoint_snap_rotation(ToolBase* const ec, Geom::Point &p, Geom::Point const &o, guint state);

void spdc_endpoint_snap_free(ToolBase* const ec, Geom::Point &p, std::optional<Geom::Point> &start_of_line, guint state);

/**
 * If we have an item and a waiting LPE, apply the effect to the item
 * (spiro spline mode is treated separately).
 */
void spdc_check_for_and_apply_waiting_LPE(FreehandBase *dc, SPItem *item);

/**
 * Create a single dot represented by a circle.
 */
void spdc_create_single_dot(ToolBase *ec, Geom::Point const &pt, char const *tool, guint event_state);

}
}
}

#endif // SEEN_SP_DRAW_CONTEXT_H

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

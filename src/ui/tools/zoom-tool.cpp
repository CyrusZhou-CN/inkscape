// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Handy zooming tool
 *
 * Authors:
 *   Lauris Kaplinski <lauris@kaplinski.com>
 *   Frank Felfe <innerspace@iname.com>
 *   bulia byak <buliabyak@users.sf.net>
 *
 * Copyright (C) 1999-2002 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include <gdk/gdkkeysyms.h>

#include "zoom-tool.h"

#include "desktop.h"
#include "rubberband.h"
#include "selection-chemistry.h"
#include "ui/widget/events/canvas-event.h"

namespace Inkscape::UI::Tools {

ZoomTool::ZoomTool(SPDesktop *desktop)
    : ToolBase(desktop, "/tools/zoom", "zoom-in.svg")
{
    auto prefs = Preferences::get();

    if (prefs->getBool("/tools/zoom/selcue")) {
        enableSelectionCue();
    }

    if (prefs->getBool("/tools/zoom/gradientdrag")) {
        enableGrDrag();
    }
}

ZoomTool::~ZoomTool()
{
    enableGrDrag(false);
    ungrabCanvasEvents();
}

bool ZoomTool::root_handler(CanvasEvent const &event)
{
    auto prefs = Preferences::get();
    tolerance = prefs->getIntLimited("/options/dragtolerance/value", 0, 0, 100);
    double const zoom_inc = prefs->getDoubleLimited("/options/zoomincrement/value", M_SQRT2, 1.01, 10);

    bool ret = false;

    inspect_event(event,
        [&] (ButtonPressEvent const &event) {
            if (event.numPress() != 1) {
                return;
            }

            auto const button_w = event.eventPos();
            auto const button_dt = _desktop->w2d(button_w);

            if (event.button() == 1) {
                saveDragOrigin(event.eventPos());
                Rubberband::get(_desktop)->start(_desktop, button_dt);
                escaped = false;
                ret = true;
            } else if (event.button() == 3) {
                double const zoom_rel = (event.modifiers() & GDK_SHIFT_MASK)
                                       ? zoom_inc
                                       : 1 / zoom_inc;
                _desktop->zoom_relative(button_dt, zoom_rel);
                ret = true;
            }

            grabCanvasEvents(EventType::KEY_PRESS      |
                             EventType::KEY_RELEASE    |
                             EventType::BUTTON_PRESS   |
                             EventType::BUTTON_RELEASE |
                             EventType::MOTION);
        },

        [&] (MotionEvent const &event) {
            if (!(event.modifiers() & GDK_BUTTON1_MASK)) {
                return;
            }

            if (!checkDragMoved(event.eventPos())) {
                return;
            }

            auto const motion_dt = _desktop->w2d(event.eventPos());
            Rubberband::get(_desktop)->move(motion_dt);
            gobble_motion_events(GDK_BUTTON1_MASK);

            ret = true;
        },

        [&] (ButtonReleaseEvent const &event) {
            auto const button_dt = _desktop->w2d(event.eventPos());

            if (event.button() == 1) {
                auto const b = Rubberband::get(_desktop)->getRectangle();

                if (b && !within_tolerance && !(event.modifiers() & GDK_SHIFT_MASK)) {
                    _desktop->set_display_area(*b, 10);
                } else if (!escaped) {
                    double const zoom_rel = (event.modifiers() & GDK_SHIFT_MASK)
                                          ? 1 / zoom_inc
                                          : zoom_inc;
                    _desktop->zoom_relative(button_dt, zoom_rel);
                }

                ret = true;
            }

            Rubberband::get(_desktop)->stop();

            ungrabCanvasEvents();

            xyp = {};
            escaped = false;
        },

        [&] (KeyPressEvent const &event) {
            switch (get_latin_keyval(event.original())) {
                case GDK_KEY_Escape:
                    if (!Rubberband::get(_desktop)->is_started()) {
                        SelectionHelper::selectNone(_desktop);
                    }

                    Rubberband::get(_desktop)->stop();
                    xyp = {};
                    escaped = true;
                    ret = true;
                    break;

                case GDK_KEY_Up:
                case GDK_KEY_Down:
                case GDK_KEY_KP_Up:
                case GDK_KEY_KP_Down:
                    // prevent the zoom field from activation
                    if (!MOD__CTRL_ONLY(event)) {
                        ret = true;
                    }
                    break;

                case GDK_KEY_Shift_L:
                case GDK_KEY_Shift_R:
                    set_cursor("zoom-out.svg");
                    break;

                case GDK_KEY_Delete:
                case GDK_KEY_KP_Delete:
                case GDK_KEY_BackSpace:
                    ret = deleteSelectedDrag(MOD__CTRL_ONLY(event));
                    break;

                default:
                    break;
            }
        },

        [&] (KeyReleaseEvent const &event) {
            switch (get_latin_keyval(event.original())) {
            	case GDK_KEY_Shift_L:
            	case GDK_KEY_Shift_R:
                    set_cursor("zoom-in.svg");
                    break;
            	default:
                    break;
            }
        },

        [&] (CanvasEvent const &event) {}
    );

    return ret || ToolBase::root_handler(event);
}

} // namespace Inkscape::UI::Tools

/*
  Local Variables:
  mode:c++
  c-file-style:"stroustrup"
  c-file-offsets:((innamespace . 0)(inline-open . 0)(case-label . +))
  indent-tabs-mode:nil
  fill-column:99
  End:
*/
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4 :

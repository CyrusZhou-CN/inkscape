// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Rubberbanding selector.
 *
 * Author:
 *   Lauris Kaplinski <lauris@kaplinski.com>
 *   Jon A. Cruz <jon@joncruz.org>
 *
 * Copyright (C) 1999-2002 Lauris Kaplinski
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "desktop.h"

#include "rubberband.h"

#include "2geom/path.h"

#include "display/curve.h"
#include "display/control/canvas-item-bpath.h"
#include "display/control/canvas-item-rect.h"

#include "ui/widget/canvas.h" // autoscroll

Inkscape::Rubberband *Inkscape::Rubberband::_instance = nullptr;

Inkscape::Rubberband::Rubberband(SPDesktop *dt)
    : _desktop(dt)
{
    _touchpath_curve = new SPCurve();
}

void Inkscape::Rubberband::delete_canvas_items()
{
    _rect.reset();
    _touchpath.reset();
}

Geom::Path Inkscape::Rubberband::getPath() const
{
    g_assert(_started);
    if (_mode == RUBBERBAND_MODE_TOUCHPATH) {
        return _path * _desktop->w2d();
    }
    return Geom::Path(*getRectangle());
}

std::vector<Geom::Point> Inkscape::Rubberband::getPoints() const
{
    return _path.nodes();
}

void Inkscape::Rubberband::start(SPDesktop *d, Geom::Point const &p, bool tolerance)
{
    _desktop = d;

    _start = p;
    _started = true;
    _moved = false;

    auto prefs = Inkscape::Preferences::get();
    _tolerance = tolerance ? prefs->getIntLimited("/options/dragtolerance/value", 0, 0, 100) : 0.0;

    _touchpath_curve->reset();
    _touchpath_curve->moveto(p);

    _path = Geom::Path(_desktop->d2w(p));

    delete_canvas_items();
}

void Inkscape::Rubberband::stop()
{
    _started = false;
    _moved = false;
    defaultMode(); // restore the default

    _touchpath_curve->reset();
    _path.clear();

    delete_canvas_items();

    resetColor();
}

void Inkscape::Rubberband::move(Geom::Point const &p)
{
    if (!_started) 
        return;

    if (!_moved) {
        if (Geom::are_near(_start, p, _tolerance/_desktop->current_zoom()))
            return;
    }

    _end = p;
    _moved = true;
    _desktop->getCanvas()->enable_autoscroll();
    _touchpath_curve->lineto(p);

    Geom::Point next = _desktop->d2w(p);
    // we want the points to be at most 0.5 screen pixels apart,
    // so that we don't lose anything small;
    // if they are farther apart, we interpolate more points
    auto prev = _path.finalPoint();
    if (Geom::L2(next-prev) > 0.5) {
        int subdiv = 2 * (int) round(Geom::L2(next-prev) + 0.5);
        for (int i = 1; i <= subdiv; i ++) {
            _path.appendNew<Geom::LineSegment>(prev + ((double)i/subdiv) * (next - prev));
        }
    } else {
        _path.appendNew<Geom::LineSegment>(next);
    }

    if (_touchpath) _touchpath->set_visible(false);
    if (_rect) _rect->set_visible(false);

    switch (_mode) {
        case RUBBERBAND_MODE_RECT:
            if (!_rect) {
                _rect = make_canvasitem<CanvasItemRect>(_desktop->getCanvasControls());
                _rect->set_stroke(_color.value_or(0x808080ff));
                _rect->set_shadow(0xffffffff, 0); // Not a shadow
                _rect->set_dashed(false);
                _rect->set_inverted(true);
            }
            _rect->set_rect(Geom::Rect(_start, _end));
            _rect->set_visible(true);
            break;
        case RUBBERBAND_MODE_TOUCHRECT:
            if (!_rect) {
                _rect = make_canvasitem<CanvasItemRect>(_desktop->getCanvasControls());
                _rect->set_stroke(_color.value_or(0xff0000ff));
                _rect->set_shadow(0xffffffff, 0); // Not a shadow
                _rect->set_dashed(false);
                _rect->set_inverted(false);
            }
            _rect->set_rect(Geom::Rect(_start, _end));
            _rect->set_visible(true);
            break;
        case RUBBERBAND_MODE_TOUCHPATH:
            if (!_touchpath) {
                _touchpath = make_canvasitem<CanvasItemBpath>(_desktop->getCanvasControls()); // Should be sketch?
                _touchpath->set_stroke(_color.value_or(0xff0000ff));
                _touchpath->set_fill(0x0, SP_WIND_RULE_NONZERO);
            }
            _touchpath->set_bpath(_touchpath_curve);
            _touchpath->set_visible(true);
            break;
        default:
            break;
    }
}

void Inkscape::Rubberband::setColor(uint32_t color)
{
    _color = color;

    if (_mode == RUBBERBAND_MODE_TOUCHPATH) {
        if (_touchpath) {
            _touchpath->set_stroke(color);
        }
    } else {
        if (_rect) {
            _rect->set_stroke(color);
        }
    }
}

void Inkscape::Rubberband::setMode(int mode) 
{
    _mode = mode;
}

/**
 * Set the default mode (usually rect or touchrect)
 */
void Inkscape::Rubberband::defaultMode()
{
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    if (prefs->getBool("/tools/select/touch_box", false)) {
        _mode = RUBBERBAND_MODE_TOUCHRECT;
    } else {
        _mode = RUBBERBAND_MODE_RECT;
    }
}

/**
 * @return Rectangle in desktop coordinates
 */
Geom::OptRect Inkscape::Rubberband::getRectangle() const
{
    if (!_started) {
        return Geom::OptRect();
    }

    return Geom::Rect(_start, _end);
}

Inkscape::Rubberband *Inkscape::Rubberband::get(SPDesktop *desktop)
{
    if (!_instance) {
        _instance = new Inkscape::Rubberband(desktop);
    }

    return _instance;
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

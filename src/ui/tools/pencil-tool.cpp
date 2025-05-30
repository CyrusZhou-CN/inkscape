// SPDX-License-Identifier: GPL-2.0-or-later
/** \file
 * Pencil event context implementation.
 */

/*
 * Authors:
 *   Lauris Kaplinski <lauris@kaplinski.com>
 *   bulia byak <buliabyak@users.sf.net>
 *   Jon A. Cruz <jon@joncruz.org>
 *
 * Copyright (C) 2000 Lauris Kaplinski
 * Copyright (C) 2000-2001 Ximian, Inc.
 * Copyright (C) 2002 Lauris Kaplinski
 * Copyright (C) 2004 Monash University
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "pencil-tool.h"

#include <cmath>   // std::lerp
#include <numeric> // std::accumulate

#include <gdk/gdkkeysyms.h>
#include <glibmm/i18n.h>

#include <2geom/bezier-utils.h>
#include <2geom/circle.h>
#include <2geom/sbasis-to-bezier.h>
#include <2geom/svg-path-parser.h>


#include "context-fns.h"
#include "desktop.h"
#include "desktop-style.h"
#include "layer-manager.h"
#include "message-context.h"
#include "message-stack.h"
#include "selection-chemistry.h"
#include "selection.h"
#include "snap.h"

#include "display/curve.h"
#include "display/control/canvas-item-bpath.h"
#include "display/control/snap-indicator.h"

#include "livarot/Path.h"  // Simplify paths

#include "live_effects/lpe-powerstroke-interpolators.h"
#include "live_effects/lpe-powerstroke.h"
#include "live_effects/lpe-simplify.h"
#include "live_effects/lpeobject.h"

#include "object/sp-lpe-item.h"
#include "object/sp-path.h"
#include "path/path-boolop.h"
#include "style.h"

#include "svg/svg.h"

#include "ui/draw-anchor.h"
#include "ui/widget/events/canvas-event.h"

#include "xml/node.h"
#include "xml/sp-css-attr.h"

#define DDC_MIN_PRESSURE      0.0
#define DDC_MAX_PRESSURE      1.0
#define DDC_DEFAULT_PRESSURE  1.0

namespace Inkscape::UI::Tools {

static Geom::Point pencil_drag_origin_w(0, 0);
static bool pencil_within_tolerance = false;

static bool in_svg_plane(Geom::Point const &p) { return Geom::LInfty(p) < 1e18; }

PencilTool::PencilTool(SPDesktop *desktop)
    : FreehandBase(desktop, "/tools/freehand/pencil", "pencil.svg")
{
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    if (prefs->getBool("/tools/freehand/pencil/selcue")) {
        this->enableSelectionCue();
    }
    this->_is_drawing = false;
    this->anchor_statusbar = false;
}

PencilTool::~PencilTool() = default;

void PencilTool::_extinput(ExtendedInput const &ext)
{
    if (ext.pressure) {
        pressure = std::clamp(*ext.pressure, DDC_MIN_PRESSURE, DDC_MAX_PRESSURE);
        is_tablet = true;
    } else {
        pressure = DDC_DEFAULT_PRESSURE;
        is_tablet = false;
    }
}

/** Snaps new node relative to the previous node. */
void PencilTool::_endpointSnap(Geom::Point &p, guint const state) {
    if ((state & GDK_CONTROL_MASK)) { //CTRL enables constrained snapping
        if (this->_npoints > 0) {
            spdc_endpoint_snap_rotation(this, p, p_array[0], state);
        }
    } else {
        if (!(state & GDK_SHIFT_MASK)) { //SHIFT disables all snapping, except the angular snapping above
                                         //After all, the user explicitly asked for angular snapping by
                                         //pressing CTRL
            std::optional<Geom::Point> origin = this->_npoints > 0 ? p_array[0] : std::optional<Geom::Point>();
            spdc_endpoint_snap_free(this, p, origin);
        } else {
            _desktop->getSnapIndicator()->remove_snaptarget();
        }
    }
}

/**
 * Callback for handling all pencil context events.
 */
bool PencilTool::root_handler(CanvasEvent const &event)
{
    bool ret = false;

    inspect_event(event,
        [&] (ButtonPressEvent const &event) {
            _extinput(event.extinput);
            ret = _handleButtonPress(event);
        },
        [&] (MotionEvent const &event) {
            _extinput(event.extinput);
            ret = _handleMotionNotify(event);
        },
        [&] (ButtonReleaseEvent const &event) {
            ret = _handleButtonRelease(event);
        },
        [&] (KeyPressEvent const &event) {
            ret = _handleKeyPress(event);
        },
        [&] (KeyReleaseEvent const &event) {
            ret = _handleKeyRelease(event);
        },
        [&] (CanvasEvent const &event) {}
    );

    return ret || FreehandBase::root_handler(event);
}

bool PencilTool::_handleButtonPress(ButtonPressEvent const &event)
{
    bool ret = false;
    if (event.num_press == 1 && event.button == 1) {
        Inkscape::Selection *selection = _desktop->getSelection();

        if (Inkscape::have_viable_layer(_desktop, defaultMessageContext()) == false) {
            return true;
        }

        /* Grab mouse, so release will not pass unnoticed */
        grabCanvasEvents();

        /* Find desktop coordinates */
        Geom::Point p = _desktop->w2d(event.pos);

        /* Test whether we hit any anchor. */
        SPDrawAnchor *anchor = spdc_test_inside(this, event.pos);
        if (tablet_enabled) {
            anchor = nullptr;
        }
        pencil_drag_origin_w = event.pos;
        pencil_within_tolerance = true;
        Inkscape::Preferences *prefs = Inkscape::Preferences::get();
        tablet_enabled = prefs->getBool("/tools/freehand/pencil/pressure", false);

        switch (this->_state) {
            case SP_PENCIL_CONTEXT_ADDLINE:
                /* Current segment will be finished with release */
                ret = true;
                break;
            default:
                /* Set first point of sequence */
                auto &m = _desktop->getNamedView()->snap_manager;
                if (event.modifiers & GDK_CONTROL_MASK) {
                    m.setup(_desktop, true);
                    if (!(event.modifiers & GDK_SHIFT_MASK)) {
                        m.freeSnapReturnByRef(p, Inkscape::SNAPSOURCE_NODE_HANDLE);
                      }
                    spdc_create_single_dot(this, p, "/tools/freehand/pencil", event.modifiers);
                    m.unSetup();
                    ret = true;
                    break;
                }
                if (anchor) {
                    p = anchor->dp;
                    //Put the start overwrite curve always on the same direction
                    if (anchor->start) {
                        sa_overwrited = std::make_shared<SPCurve>(anchor->curve->reversed());
                    } else {
                        sa_overwrited = std::make_shared<SPCurve>(*anchor->curve);
                    }
                    _desktop->messageStack()->flash(Inkscape::NORMAL_MESSAGE, _("Continuing selected path"));
                } else {
                    m.setup(_desktop, true);
                    if (tablet_enabled) {
                        // This is the first click of a new curve; deselect item so that
                        // this curve is not combined with it (unless it is drawn from its
                        // anchor, which is handled by the sibling branch above)
                        selection->clear();
                        _desktop->messageStack()->flash(Inkscape::NORMAL_MESSAGE, _("Creating new path"));
                    } else if (!(event.modifiers & GDK_SHIFT_MASK)) {
                        // This is the first click of a new curve; deselect item so that
                        // this curve is not combined with it (unless it is drawn from its
                        // anchor, which is handled by the sibling branch above)
                        selection->clear();
                        _desktop->messageStack()->flash(Inkscape::NORMAL_MESSAGE, _("Creating new path"));
                        m.freeSnapReturnByRef(p, Inkscape::SNAPSOURCE_NODE_HANDLE);
                    } else if (selection->singleItem() && is<SPPath>(selection->singleItem())) {
                        _desktop->messageStack()->flash(Inkscape::NORMAL_MESSAGE, _("Appending to selected path"));
                        m.freeSnapReturnByRef(p, Inkscape::SNAPSOURCE_NODE_HANDLE);
                    }
                    m.unSetup();
                }
                if (!tablet_enabled) {
                    sa = anchor;
                }
                _setStartpoint(p);
                ret = true;
                break;
        }

        set_high_motion_precision();
        _is_drawing = true;
    }
    return ret;
}

bool PencilTool::_handleMotionNotify(MotionEvent const &event) {
    if ((event.modifiers & GDK_CONTROL_MASK) && (event.modifiers & GDK_BUTTON1_MASK)) {
        // mouse was accidentally moved during Ctrl+click;
        // ignore the motion and create a single point
        _is_drawing = false;
        return true;
    }

    if ((event.modifiers & GDK_BUTTON2_MASK)) {
        // allow scrolling
        return false;
    }

    /* Test whether we hit any anchor. */
    SPDrawAnchor *anchor = spdc_test_inside(this, pencil_drag_origin_w);
    if (this->pressure == 0.0 && tablet_enabled && !anchor) {
        // tablet event was accidentally fired without press;
        return false;
    }
    
    if ( ( event.modifiers & GDK_BUTTON1_MASK ) && this->_is_drawing) {
        /* Grab mouse, so release will not pass unnoticed */
        grabCanvasEvents();
    }

    /* Find desktop coordinates */
    Geom::Point p = _desktop->w2d(event.pos);

    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    if (pencil_within_tolerance) {
        gint const tolerance = prefs->getIntLimited("/options/dragtolerance/value", 0, 0, 100);
        if ( Geom::LInfty(event.pos - pencil_drag_origin_w ) < tolerance ) {
            return false;   // Do not drag if we're within tolerance from origin.
        }
    }

    // Once the user has moved farther than tolerance from the original location
    // (indicating they intend to move the object, not click), then always process the
    // motion notify coordinates as given (no snapping back to origin)
    pencil_within_tolerance = false;
    
    anchor = spdc_test_inside(this, event.pos);

    bool ret = false;

    switch (this->_state) {
        case SP_PENCIL_CONTEXT_ADDLINE:
            if (is_tablet) {
                _state = SP_PENCIL_CONTEXT_FREEHAND;
                return false;
            }
            /* Set red endpoint */
            if (anchor) {
                p = anchor->dp;
            } else {
                Geom::Point ptnr(p);
                _endpointSnap(ptnr, event.modifiers);
                p = ptnr;
            }
            _setEndpoint(p);
            ret = true;
            break;
        default:
            /* We may be idle or already freehand */
            if ( (event.modifiers & GDK_BUTTON1_MASK) && _is_drawing ) {
                if (_state == SP_PENCIL_CONTEXT_IDLE) {
                    discard_delayed_snap_event();
                }
                _state = SP_PENCIL_CONTEXT_FREEHAND;

                if ( !sa && !green_anchor ) {
                    /* Create green anchor */
                    green_anchor = std::make_unique<SPDrawAnchor>(this, green_curve, true, p_array[0]);
                }
                if (anchor) {
                    p = anchor->dp;
                }
                if ( _npoints != 0) { // buttonpress may have happened before we entered draw context!
                    if (ps.empty()) {
                        // Only in freehand mode we have to add the first point also to ps (apparently)
                        // - We cannot add this point in spdc_set_startpoint, because we only need it for freehand
                        // - We cannot do this in the button press handler because at that point we don't know yet
                        //   whether we're going into freehand mode or not
                        ps.push_back(p_array[0]);
                        if (tablet_enabled) {
                            _wps.emplace_back(0, 0);
                        }
                    }
                    _addFreehandPoint(p, event.modifiers, false);
                    ret = true;
                }
                if (anchor && !anchor_statusbar) {
                    message_context->set(Inkscape::NORMAL_MESSAGE, _("<b>Release</b> here to close and finish the path."));
                    anchor_statusbar = true;
                    ea = anchor;
                } else if (!anchor && anchor_statusbar) {
                    message_context->clear();
                    anchor_statusbar = false;
                    ea = nullptr;
                } else if (!anchor) {
                    message_context->set(Inkscape::NORMAL_MESSAGE, _("Drawing a freehand path"));
                    ea = nullptr;
                }

            } else {
                if (anchor && !anchor_statusbar) {
                    message_context->set(Inkscape::NORMAL_MESSAGE, _("<b>Drag</b> to continue the path from this point."));
                    anchor_statusbar = true;
                } else if (!anchor && anchor_statusbar) {
                    message_context->clear();
                    anchor_statusbar = false;
                }
            }

            // Show the pre-snap indicator to communicate to the user where we would snap to if he/she were to
            // a) press the mousebutton to start a freehand drawing, or
            // b) release the mousebutton to finish a freehand drawing
            if (!tablet_enabled && !sp_event_context_knot_mouseover()) {
                SnapManager &m = _desktop->getNamedView()->snap_manager;
                m.setup(_desktop, true);
                m.preSnap(Inkscape::SnapCandidatePoint(p, Inkscape::SNAPSOURCE_NODE_HANDLE));
                m.unSetup();
            }
            break;
    }
    return ret;
}

bool PencilTool::_handleButtonRelease(ButtonReleaseEvent const &event) {

    bool ret = false;

    set_high_motion_precision(false);

    if (event.button == 1 && _is_drawing) {
        _is_drawing = false;

        /* Find desktop coordinates */
        Geom::Point p = _desktop->w2d(event.pos);

        /* Test whether we hit any anchor. */
        SPDrawAnchor *anchor = spdc_test_inside(this, event.pos);

        switch (_state) {
            case SP_PENCIL_CONTEXT_IDLE:
                /* Releasing button in idle mode means single click */
                /* We have already set up start point/anchor in button_press */
                if (!(event.modifiers & GDK_CONTROL_MASK) && !is_tablet) {
                    // Ctrl+click creates a single point so only set context in ADDLINE mode when Ctrl isn't pressed
                    _state = SP_PENCIL_CONTEXT_ADDLINE;
                }
                /*Or select the down item if we are in tablet mode*/
                if (is_tablet) {
                    using namespace Inkscape::LivePathEffect;
                    SPItem *item = sp_event_context_find_item(_desktop, event.pos, false, false);
                    if (item && (!white_item || item != white_item)) {
                        if (is<SPLPEItem>(item)) {
                            Effect* lpe = cast<SPLPEItem>(item)->getCurrentLPE();
                            if (lpe) {
                                LPEPowerStroke* ps = static_cast<LPEPowerStroke*>(lpe);
                                if (ps) {
                                    _desktop->getSelection()->clear();
                                    _desktop->getSelection()->add(item);
                                }
                            }
                        }
                    }
                }
                break;
            case SP_PENCIL_CONTEXT_ADDLINE:
                /* Finish segment now */
                if (anchor) {
                    p = anchor->dp;
                } else {
                    _endpointSnap(p, event.modifiers);
                }
                ea = anchor;
                _setEndpoint(p);
                _finishEndpoint();
                _state = SP_PENCIL_CONTEXT_IDLE;
                discard_delayed_snap_event();
                break;
            case SP_PENCIL_CONTEXT_FREEHAND:
                if (event.modifiers & GDK_ALT_MASK && !tablet_enabled) {
                    /* sketch mode: interpolate the sketched path and improve the current output path with the new interpolation. don't finish sketch */
                    _sketchInterpolate();

                    green_anchor.reset();

                    _state = SP_PENCIL_CONTEXT_SKETCH;
                } else {
                    /* Finish segment now */
                    /// \todo fixme: Clean up what follows (Lauris)
                    if (anchor) {
                        p = anchor->dp;
                    } else {
                        Geom::Point p_end = p;
                        if (tablet_enabled) {
                            _addFreehandPoint(p_end, event.modifiers, true);
                            _pressure_curve.reset();
                        } else {
                            _endpointSnap(p_end, event.modifiers);
                            if (p_end != p) {
                                // then we must have snapped!
                                _addFreehandPoint(p_end, event.modifiers, true);
                            }
                        }
                    }

                    ea = anchor;
                    /* Write curves to object */
                    _desktop->messageStack()->flash(Inkscape::NORMAL_MESSAGE, _("Finishing freehand"));
                    _interpolate();
                    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
                    if (tablet_enabled) {
                        gint shapetype = prefs->getInt("/tools/freehand/pencil/shape", 0);
                        gint simplify = prefs->getInt("/tools/freehand/pencil/simplify", 0);
                        gint mode = prefs->getInt("/tools/freehand/pencil/freehand-mode", 0);
                        prefs->setInt("/tools/freehand/pencil/shape", 0);
                        prefs->setInt("/tools/freehand/pencil/simplify", 0);
                        prefs->setInt("/tools/freehand/pencil/freehand-mode", 0);
                        spdc_concat_colors_and_flush(this, false);
                        prefs->setInt("/tools/freehand/pencil/freehand-mode", mode);
                        prefs->setInt("/tools/freehand/pencil/simplify", simplify);
                        prefs->setInt("/tools/freehand/pencil/shape", shapetype);
                    } else {
                        spdc_concat_colors_and_flush(this, false);
                    }
                    points.clear();
                    sa = nullptr;
                    ea = nullptr;
                    ps.clear();
                    _wps.clear();
                    green_anchor.reset();
                    _state = SP_PENCIL_CONTEXT_IDLE;
                    // reset sketch mode too
                    sketch_n = 0;
                }
                break;
            case SP_PENCIL_CONTEXT_SKETCH:
            default:
                break;
        }

        ungrabCanvasEvents();

        ret = true;
    }
    return ret;
}

void PencilTool::_cancel() {
    ungrabCanvasEvents();

    _is_drawing = false;
    _state = SP_PENCIL_CONTEXT_IDLE;
    discard_delayed_snap_event();

    red_curve.reset();
    red_bpath->set_bpath(&red_curve);

    green_bpaths.clear();
    green_curve->reset();
    green_anchor.reset();

    message_context->clear();
    message_context->flash(Inkscape::NORMAL_MESSAGE, _("Drawing cancelled"));
}

bool PencilTool::_handleKeyPress(KeyPressEvent const &event) {
    bool ret = false;

    switch (get_latin_keyval(event)) {
        case GDK_KEY_Up:
        case GDK_KEY_Down:
        case GDK_KEY_KP_Up:
        case GDK_KEY_KP_Down:
            // Prevent the zoom field from activation.
            if (!mod_ctrl_only(event.modifiers)) {
                ret = true;
            }
            break;
        case GDK_KEY_Escape:
            if (_npoints != 0) {
                // if drawing, cancel, otherwise pass it up for deselecting
                if (_state != SP_PENCIL_CONTEXT_IDLE) {
                    _cancel();
                    ret = true;
                }
            }
            break;
        case GDK_KEY_z:
        case GDK_KEY_Z:
            if (mod_ctrl_only(event.modifiers) && _npoints != 0) {
                // if drawing, cancel, otherwise pass it up for undo
                if (_state != SP_PENCIL_CONTEXT_IDLE) {
                    _cancel();
                    ret = true;
                }
            }
            break;
        case GDK_KEY_g:
        case GDK_KEY_G:
            if (mod_shift_only(event.modifiers)) {
                _desktop->getSelection()->toGuides();
                ret = true;
            }
            break;
        case GDK_KEY_Alt_L:
        case GDK_KEY_Alt_R:
        case GDK_KEY_Meta_L:
        case GDK_KEY_Meta_R:
            if (_state == SP_PENCIL_CONTEXT_IDLE) {
                _desktop->messageStack()->flash(Inkscape::NORMAL_MESSAGE, _("<b>Sketch mode</b>: holding <b>Alt</b> interpolates between sketched paths. Release <b>Alt</b> to finalize."));
            }
            break;
        default:
            break;
    }
    return ret;
}

bool PencilTool::_handleKeyRelease(KeyReleaseEvent const &event) {
    bool ret = false;

    switch (get_latin_keyval(event)) {
        case GDK_KEY_Alt_L:
        case GDK_KEY_Alt_R:
        case GDK_KEY_Meta_L:
        case GDK_KEY_Meta_R:
            if (_state == SP_PENCIL_CONTEXT_SKETCH) {
                spdc_concat_colors_and_flush(this, false);
                sketch_n = 0;
                sa = nullptr;
                ea = nullptr;
                green_anchor.reset();
                _state = SP_PENCIL_CONTEXT_IDLE;
                discard_delayed_snap_event();
                _desktop->messageStack()->flash(Inkscape::NORMAL_MESSAGE, _("Finishing freehand sketch"));
                ret = true;
            }
            break;
        default:
            break;
    }
    return ret;
}

/**
 * Reset points and set new starting point.
 */
void PencilTool::_setStartpoint(Geom::Point const &p) {
    _npoints = 0;
    red_curve_is_valid = false;
    if (in_svg_plane(p)) {
        p_array[_npoints++] = p;
    }
}

/**
 * Change moving endpoint position.
 * <ul>
 * <li>Ctrl constrains to moving to H/V direction, snapping in given direction.
 * <li>Otherwise we snap freely to whatever attractors are available.
 * </ul>
 *
 * Number of points is (re)set to 2 always, 2nd point is modified.
 * We change RED curve.
 */
void PencilTool::_setEndpoint(Geom::Point const &p) {
    if (_npoints == 0) {
        return;
        /* May occur if first point wasn't in SVG plane (e.g. weird w2d transform, perhaps from bad
         * zoom setting).
         */
    }
    g_return_if_fail( this->_npoints > 0 );

    red_curve.reset();
    if ( ( p == p_array[0] )
         || !in_svg_plane(p) )
    {
        _npoints = 1;
    } else {
        p_array[1] = p;
        _npoints = 2;

        red_curve.moveto(p_array[0]);
        red_curve.lineto(p_array[1]);
        red_curve_is_valid = true;
        if (!tablet_enabled) {
            red_bpath->set_bpath(&red_curve);
        }
    }
}

/**
 * Finalize addline.
 *
 * \todo
 * fixme: I'd like remove red reset from concat colors (lauris).
 * Still not sure, how it will make most sense.
 */
void PencilTool::_finishEndpoint() {
    if (this->red_curve.is_unset() ||
        this->red_curve.first_point() == this->red_curve.second_point())
    {
        this->red_curve.reset();
        if (!tablet_enabled) {
            red_bpath->set_bpath(nullptr);
        }
    } else {
        /* Write curves to object. */
        spdc_concat_colors_and_flush(this, false);
        this->sa = nullptr;
        this->ea = nullptr;
    }
}

static inline double square(double const x) { return x * x; }



void PencilTool::addPowerStrokePencil()
{
    {
        SPDocument *document = _desktop->doc();
        if (!document) {
            return;
        }
        using namespace Inkscape::LivePathEffect;
        Inkscape::Preferences *prefs = Inkscape::Preferences::get();
        double tol = prefs->getDoubleLimited("/tools/freehand/pencil/base-simplify", 25.0, 0.0, 100.0) * 0.4;
        double tolerance_sq = 0.02 * square(_desktop->w2d().descrim() * tol) * exp(0.2 * tol - 2);
        int n_points = this->ps.size();
        // worst case gives us a segment per point
        int max_segs = 4 * n_points;
        std::vector<Geom::Point> b(max_segs);
        SPCurve curvepressure;
        int const n_segs = Geom::bezier_fit_cubic_r(b.data(), this->ps.data(), n_points, tolerance_sq, max_segs);
        if (n_segs > 0) {
            /* Fit and draw and reset state */
            curvepressure.moveto(b[0]);
            for (int c = 0; c < n_segs; c++) {
                curvepressure.curveto(b[4 * c + 1], b[4 * c + 2], b[4 * c + 3]);
            }
        }
        curvepressure.transform(currentLayer()->i2dt_affine().inverse());
        Geom::Path path = curvepressure.get_pathvector()[0];

        if (!path.empty()) {
            Inkscape::XML::Document *xml_doc = document->getReprDoc();
            Inkscape::XML::Node *pp = nullptr;
            pp = xml_doc->createElement("svg:path");
            pp->setAttribute("d", sp_svg_write_path(path));
            pp->setAttribute("id", "power_stroke_preview");
            Inkscape::GC::release(pp);

            auto powerpreview = cast<SPShape>(currentLayer()->appendChildRepr(pp));
            auto lpeitem = powerpreview;
            if (!lpeitem) {
                return;
            }
            DocumentUndo::ScopedInsensitive tmp(document);
            tol = prefs->getDoubleLimited("/tools/freehand/pencil/tolerance", 10.0, 0.0, 100.0) + 30;
            if (tol > 30) {
                tol = tol / (130.0 * (132.0 - tol));
                Inkscape::SVGOStringStream threshold;
                threshold << tol;
                Effect::createAndApply(SIMPLIFY, document, lpeitem);
                Effect *lpe = lpeitem->getCurrentLPE();
                Inkscape::LivePathEffect::LPESimplify *simplify =
                    static_cast<Inkscape::LivePathEffect::LPESimplify *>(lpe);
                if (simplify) {
                    sp_lpe_item_enable_path_effects(lpeitem, false);
                    Glib::ustring pref_path = "/live_effects/simplify/smooth_angles";
                    bool valid = prefs->getEntry(pref_path).isValidDouble();
                    if (!valid) {
                        lpe->getRepr()->setAttribute("smooth_angles", "0");
                    }
                    pref_path = "/live_effects/simplify/helper_size";
                    valid = prefs->getEntry(pref_path).isValidDouble();
                    if (!valid) {
                        lpe->getRepr()->setAttribute("helper_size", "0");
                    }
                    pref_path = "/live_effects/simplify/step";
                    valid = prefs->getEntry(pref_path).isValidDouble();
                    if (!valid) {
                        lpe->getRepr()->setAttribute("step", "1");
                    }
                    lpe->getRepr()->setAttribute("threshold", threshold.str());
                    lpe->getRepr()->setAttribute("simplify_individual_paths", "false");
                    lpe->getRepr()->setAttribute("simplify_just_coalesce", "false");
                    sp_lpe_item_enable_path_effects(lpeitem, true);
                }
                sp_lpe_item_update_patheffect(lpeitem, false, true);
                SPCurve const *curvepressure = powerpreview->curve();
                if (curvepressure->is_empty()) {
                    return;
                }
                path = curvepressure->get_pathvector()[0];
            }
            powerStrokeInterpolate(path);
            Inkscape::Preferences *prefs = Inkscape::Preferences::get();
            Glib::ustring pref_path_pp = "/live_effects/powerstroke/powerpencil";
            prefs->setBool(pref_path_pp, true);
            Effect::createAndApply(POWERSTROKE, document, lpeitem);
            Effect *lpe = lpeitem->getCurrentLPE();
            Inkscape::LivePathEffect::LPEPowerStroke *pspreview = static_cast<LPEPowerStroke *>(lpe);
            if (pspreview) {
                sp_lpe_item_enable_path_effects(lpeitem, false);
                Glib::ustring pref_path = "/live_effects/powerstroke/interpolator_type";
                bool valid = prefs->getEntry(pref_path).isValidString();
                if (!valid) {
                    pspreview->getRepr()->setAttribute("interpolator_type", "CentripetalCatmullRom");
                }
                pref_path = "/live_effects/powerstroke/linejoin_type";
                valid = prefs->getEntry(pref_path).isValidString();
                if (!valid) {
                    pspreview->getRepr()->setAttribute("linejoin_type", "spiro");
                }
                pref_path = "/live_effects/powerstroke/interpolator_beta";
                valid = prefs->getEntry(pref_path).isValidDouble();
                if (!valid) {
                    pspreview->getRepr()->setAttribute("interpolator_beta", "0.75");
                }
                gint cap = prefs->getInt("/live_effects/powerstroke/powerpencilcap", 2);
                pspreview->getRepr()->setAttribute("start_linecap_type", LineCapTypeConverter.get_key(cap));
                pspreview->getRepr()->setAttribute("end_linecap_type", LineCapTypeConverter.get_key(cap));
                pspreview->getRepr()->setAttribute("sort_points", "true");
                pspreview->getRepr()->setAttribute("not_jump", "true");
                pspreview->offset_points.param_set_and_write_new_value(this->points);
                sp_lpe_item_enable_path_effects(lpeitem, true);
                sp_lpe_item_update_patheffect(lpeitem, false, true);
                pp->setAttribute("style", "fill:#888888;opacity:1;fill-rule:nonzero;stroke:none;");
            }
            prefs->setBool(pref_path_pp, false);
        }
    }
}

/**
 * Add a virtual point to the future pencil path.
 *
 * @param p the point to add.
 * @param state event state
 * @param last the point is the last of the user stroke.
 */
void PencilTool::_addFreehandPoint(Geom::Point const &p, guint /*state*/, bool last)
{
    g_assert(_npoints > 0 );
    g_return_if_fail(unsigned(_npoints) < G_N_ELEMENTS(p_array));

    double distance = 0;
    if ( ( p != p_array[_npoints - 1 ] )
         && in_svg_plane(p) )
    {
        p_array[_npoints++] = p;
        this->_fitAndSplit();
        if (tablet_enabled) {
            distance = Geom::distance(p, this->ps.back()) + this->_wps.back()[Geom::X];
        }
        this->ps.push_back(p);
    }
    if (tablet_enabled && in_svg_plane(p)) {
        Inkscape::Preferences *prefs = Inkscape::Preferences::get();
        double min = prefs->getIntLimited("/tools/freehand/pencil/minpressure",  0, 0, 100) / 100.0;
        double max = prefs->getIntLimited("/tools/freehand/pencil/maxpressure", 30, 0, 100) / 100.0;
        if (min > max) {
            min = max;
        }
        double dezoomify_factor = 0.05 * 1000 / _desktop->current_zoom();
        double const pressure_shrunk = std::lerp(min, max, pressure);
        double pressure_computed = std::abs(pressure_shrunk * dezoomify_factor);
        double pressure_computed_scaled = std::abs(pressure_computed * _desktop->getDocument()->getDocumentScale().inverse()[Geom::X]);
        if (p != p_array[_npoints - 1]) {
            _wps.emplace_back(distance, pressure_computed_scaled);
        }
        if (pressure_computed) {
            Geom::Circle pressure_dot(p, pressure_computed);
            Geom::Piecewise<Geom::D2<Geom::SBasis>> pressure_piecewise;
            pressure_piecewise.push_cut(0);
            pressure_piecewise.push(pressure_dot.toSBasis(), 1);
            Geom::PathVector pressure_path = Geom::path_from_piecewise(pressure_piecewise, 0.1);
            Geom::PathVector previous_presure = _pressure_curve.get_pathvector();
            if (!pressure_path.empty() && !previous_presure.empty()) {
                pressure_path = sp_pathvector_boolop(pressure_path, previous_presure, bool_op_union, fill_nonZero, fill_nonZero);
            }
            _pressure_curve = SPCurve(std::move(pressure_path));
            red_bpath->set_bpath(&_pressure_curve);
        }
        if (last) {
            this->addPowerStrokePencil();
        }
    }
}

void PencilTool::powerStrokeInterpolate(Geom::Path const path)
{
    size_t ps_size = this->ps.size();
    if ( ps_size <= 1 ) {
        return;
    }

    using Geom::X;
    using Geom::Y;
    gint path_size = path.size();
    std::vector<Geom::Point> tmp_points;
    Geom::Point previous = Geom::Point(Geom::infinity(), 0);
    bool increase = false;
    size_t i = 0;
    double dezoomify_factor = 0.05 * 1000 / _desktop->current_zoom();
    double limit = 6 * dezoomify_factor;
    double max =
        std::max(this->_wps.back()[Geom::X] - (this->_wps.back()[Geom::X] / 10), this->_wps.back()[Geom::X] - limit);
    double min = std::min(this->_wps.back()[Geom::X] / 10, limit);
    double original_lenght = this->_wps.back()[Geom::X];
    double max10 = 0;
    double min10 = 0;
    for (auto wps : this->_wps) {
        i++;
        Geom::Coord pressure = wps[Geom::Y];
        max10 = max10  > pressure ? max10 : pressure;
        min10 = min10 <= pressure ? min10 : pressure;
        if (!original_lenght || wps[Geom::X] > max) {
            break;
        }
        if (wps[Geom::Y] == 0 || wps[Geom::X] < min) {
            continue;
        }
        if (previous[Geom::Y] < (max10 + min10) / 2.0) {
            if (increase && tmp_points.size() > 1) {
                tmp_points.pop_back();
            }
            wps[Geom::Y] = max10;
            tmp_points.push_back(wps);
            increase = true;
        } else {
            if (!increase && tmp_points.size() > 1) {
                tmp_points.pop_back();
            }
            wps[Geom::Y] = min10;
            tmp_points.push_back(wps);
            increase = false;
        }
        previous = wps;
        max10 = 0;
        min10 = 999999999;
    }
    this->points.clear();
    double prev_pressure = 0;
    for (auto point : tmp_points) {
        point[Geom::X] /= (double)original_lenght;
        point[Geom::X] *= path_size;
        if (std::abs(point[Geom::Y] - prev_pressure) > point[Geom::Y] / 10.0) {
            this->points.push_back(point);
            prev_pressure = point[Geom::Y];
        }
    }
    if (points.empty() && !_wps.empty()) {
        // Synthesize a pressure data point based on the average pressure
        double average_pressure = std::accumulate(_wps.begin(), _wps.end(), 0.0,
            [](double const &sum_so_far, Geom::Point const &point) -> double {
                return sum_so_far + point[Geom::Y];
        }) / (double)_wps.size();
        points.emplace_back(0.5 * path.size(), /* place halfway along the path */
                            2.0 * average_pressure /* 2.0 - for correct average thickness of a kite */);
    }
}

void PencilTool::_interpolate() {
    size_t ps_size = this->ps.size();
    if ( ps_size <= 1 ) {
        return;
    }
    using Geom::X;
    using Geom::Y;
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    double tol = prefs->getDoubleLimited("/tools/freehand/pencil/tolerance", 10.0, 0.0, 100.0) * 0.4;
    bool simplify = prefs->getInt("/tools/freehand/pencil/simplify", 0);
    if(simplify){
        double tol2 = prefs->getDoubleLimited("/tools/freehand/pencil/base-simplify", 25.0, 0.0, 100.0) * 0.4;
        tol = std::min(tol,tol2);
    }
    this->green_curve->reset();
    this->red_curve.reset();
    this->red_curve_is_valid = false;

    double tolerance_sq = 0.02 * square(_desktop->w2d().descrim() * tol) * exp(0.2 * tol - 2);

    g_assert(is_zero(this->_req_tangent) || is_unit_vector(this->_req_tangent));

    int n_points = this->ps.size();

    // worst case gives us a segment per point
    int max_segs = 4 * n_points;

    std::vector<Geom::Point> b(max_segs);
    int const n_segs = Geom::bezier_fit_cubic_r(b.data(), this->ps.data(), n_points, tolerance_sq, max_segs);
    if (n_segs > 0) {
        /* Fit and draw and reset state */
        this->green_curve->moveto(b[0]);
        Inkscape::Preferences *prefs = Inkscape::Preferences::get();
        guint mode = prefs->getInt("/tools/freehand/pencil/freehand-mode", 0);
        for (int c = 0; c < n_segs; c++) {
            // if we are in BSpline we modify the trace to create adhoc nodes 
            if (mode == 2) {
                Geom::Point point_at1 = b[4 * c + 0] + (1./3) * (b[4 * c + 3] - b[4 * c + 0]);
                Geom::Point point_at2 = b[4 * c + 3] + (1./3) * (b[4 * c + 0] - b[4 * c + 3]);
                this->green_curve->curveto(point_at1,point_at2,b[4*c+3]);
            } else {
                if (!tablet_enabled || c != n_segs - 1) {
                    this->green_curve->curveto(b[4 * c + 1], b[4 * c + 2], b[4 * c + 3]);
                } else {
                    std::optional<Geom::Point> finalp = this->green_curve->last_point();
                    if (this->green_curve->nodes_in_path() > 4 && Geom::are_near(*finalp, b[4 * c + 3], 10.0)) {
                        this->green_curve->backspace();
                        this->green_curve->curveto(*finalp, b[4 * c + 3], b[4 * c + 3]);
                    } else {
                        this->green_curve->curveto(b[4 * c + 1], b[4 * c + 3], b[4 * c + 3]);
                    }
                }
            }
        }
        if (!tablet_enabled) {
            red_bpath->set_bpath(green_curve.get());
        }

        /* Fit and draw and copy last point */
        g_assert(!this->green_curve->is_empty());

        /* Set up direction of next curve. */
        {
            Geom::Curve const * last_seg = this->green_curve->last_segment();
            g_assert( last_seg );      // Relevance: validity of (*last_seg)
            p_array[0] = last_seg->finalPoint();
            _npoints = 1;
            Geom::Curve *last_seg_reverse = last_seg->reverse();
            Geom::Point const req_vec( -last_seg_reverse->unitTangentAt(0) );
            delete last_seg_reverse;
            _req_tangent = ( ( Geom::is_zero(req_vec) || !in_svg_plane(req_vec) )
                             ? Geom::Point(0, 0)
                             : Geom::unit_vector(req_vec) );
        }
    }
}


/* interpolates the sketched curve and tweaks the current sketch interpolation*/
void PencilTool::_sketchInterpolate() {
    if ( this->ps.size() <= 1 ) {
        return;
    }

    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    double tol = prefs->getDoubleLimited("/tools/freehand/pencil/tolerance", 10.0, 1.0, 100.0) * 0.4;
    bool simplify = prefs->getInt("/tools/freehand/pencil/simplify", 0);
    if(simplify){
        double tol2 = prefs->getDoubleLimited("/tools/freehand/pencil/base-simplify", 25.0, 1.0, 100.0) * 0.4;
        tol = std::min(tol,tol2);
    }
    double tolerance_sq = 0.02 * square(_desktop->w2d().descrim() * tol) * exp(0.2 * tol - 2);

    bool average_all_sketches = prefs->getBool("/tools/freehand/pencil/average_all_sketches", true);

    g_assert(is_zero(this->_req_tangent) || is_unit_vector(this->_req_tangent));

    this->red_curve.reset();
    this->red_curve_is_valid = false;

    int n_points = this->ps.size();

    // worst case gives us a segment per point
    int max_segs = 4 * n_points;

    std::vector<Geom::Point> b(max_segs);

    int const n_segs = Geom::bezier_fit_cubic_r(b.data(), this->ps.data(), n_points, tolerance_sq, max_segs);

    if (n_segs > 0) {
        Geom::Path fit(b[0]);

        for (int c = 0; c < n_segs; c++) {
            fit.appendNew<Geom::CubicBezier>(b[4 * c + 1], b[4 * c + 2], b[4 * c + 3]);
        }

        Geom::Piecewise<Geom::D2<Geom::SBasis> > fit_pwd2 = fit.toPwSb();

        if (this->sketch_n > 0) {
            double t;

            if (average_all_sketches) {
                // Average = (sum of all) / n
                //         = (sum of all + new one) / n+1
                //         = ((old average)*n + new one) / n+1
                t = this->sketch_n / (this->sketch_n + 1.);
            } else {
                t = 0.5;
            }

            this->sketch_interpolation = Geom::lerp(t, fit_pwd2, this->sketch_interpolation);

            // simplify path, to eliminate small segments
            Path path;
            path.LoadPathVector(Geom::path_from_piecewise(this->sketch_interpolation, 0.01));
            path.Simplify(0.5);

            Geom::PathVector pathv = path.MakePathVector();
            this->sketch_interpolation = pathv[0].toPwSb();
        } else {
            this->sketch_interpolation = fit_pwd2;
        }

        this->sketch_n++;

        this->green_curve->reset();
        this->green_curve->set_pathvector(Geom::path_from_piecewise(this->sketch_interpolation, 0.01));
        if (!tablet_enabled) {
            red_bpath->set_bpath(green_curve.get());
        }
        /* Fit and draw and copy last point */
        g_assert(!this->green_curve->is_empty());

        /* Set up direction of next curve. */
        {
            Geom::Curve const * last_seg = this->green_curve->last_segment();
            g_assert( last_seg );      // Relevance: validity of (*last_seg)
            p_array[0] = last_seg->finalPoint();
            _npoints = 1;
            Geom::Curve *last_seg_reverse = last_seg->reverse();
            Geom::Point const req_vec( -last_seg_reverse->unitTangentAt(0) );
            delete last_seg_reverse;
            _req_tangent = ( ( Geom::is_zero(req_vec) || !in_svg_plane(req_vec) )
                             ? Geom::Point(0, 0)
                             : Geom::unit_vector(req_vec) );
        }
    }

    this->ps.clear();
    this->points.clear();
    this->_wps.clear();
}

void PencilTool::_fitAndSplit() {
    g_assert(_npoints > 1 );

    double const tolerance_sq = 0;

    Geom::Point b[4];
    g_assert(is_zero(this->_req_tangent)
             || is_unit_vector(this->_req_tangent));
    Geom::Point const tHatEnd(0, 0);
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    int const n_segs = Geom::bezier_fit_cubic_full(b, nullptr, p_array, _npoints,
                                                _req_tangent, tHatEnd,
                                                tolerance_sq, 1);
    if ( n_segs > 0
         && unsigned(_npoints) < G_N_ELEMENTS(p_array) )
    {
        /* Fit and draw and reset state */

        this->red_curve.reset();
        this->red_curve.moveto(b[0]);
        using Geom::X;
        using Geom::Y;
            // if we are in BSpline we modify the trace to create adhoc nodes
        guint mode = prefs->getInt("/tools/freehand/pencil/freehand-mode", 0);
        if(mode == 2){
            Geom::Point point_at1 = b[0] + (1./3)*(b[3] - b[0]);
            Geom::Point point_at2 = b[3] + (1./3)*(b[0] - b[3]);
            this->red_curve.curveto(point_at1,point_at2,b[3]);
        }else{
            this->red_curve.curveto(b[1], b[2], b[3]);
        }
        if (!tablet_enabled) {
            red_bpath->set_bpath(&red_curve);
        }
        this->red_curve_is_valid = true;
    } else {
        /* Fit and draw and copy last point */

        g_assert(!this->red_curve.is_empty());

        /* Set up direction of next curve. */
        {
            Geom::Curve const * last_seg = this->red_curve.last_segment();
            g_assert( last_seg );      // Relevance: validity of (*last_seg)
            p_array[0] = last_seg->finalPoint();
            _npoints = 1;
            Geom::Curve *last_seg_reverse = last_seg->reverse();
            Geom::Point const req_vec( -last_seg_reverse->unitTangentAt(0) );
            delete last_seg_reverse;
            _req_tangent = ( ( Geom::is_zero(req_vec) || !in_svg_plane(req_vec) )
                             ? Geom::Point(0, 0)
                             : Geom::unit_vector(req_vec) );
        }

        green_curve->append_continuous(red_curve);

        /// \todo fixme:

        auto layer = _desktop->layerManager().currentLayer();
        auto highlight = layer->highlight_color();
        auto other = prefs->getColor("/tools/nodes/highlight_color", "#ff0000ff");

        if(other == highlight) {
            green_color = 0x00ff007f;
        } else {
            green_color = highlight.toRGBA();
        }
        highlight_color = highlight.toRGBA();

        auto cshape = new Inkscape::CanvasItemBpath(_desktop->getCanvasSketch(), red_curve.get_pathvector(), true);
        cshape->set_stroke(green_color);
        cshape->set_fill(0x0, SP_WIND_RULE_NONZERO);

        this->green_bpaths.emplace_back(cshape);

        this->red_curve_is_valid = false;
    }
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
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:fileencoding=utf-8:textwidth=99 :

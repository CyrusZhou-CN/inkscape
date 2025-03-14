// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * Editable view implementation
 */
/*
 * Authors:
 *   Lauris Kaplinski <lauris@kaplinski.com>
 *   Frank Felfe <innerspace@iname.com>
 *   MenTaLguY <mental@rydia.net>
 *   bulia byak <buliabyak@users.sf.net>
 *   Ralf Stephan <ralf@ark.in-berlin.de>
 *   John Bintz <jcoswell@coswellproductions.org>
 *   Johan Engelen <j.b.c.engelen@ewi.utwente.nl>
 *   Jon A. Cruz <jon@joncruz.org>
 *   Abhishek Sharma
 *
 * Copyright (C) 2007 Jon A. Cruz
 * Copyright (C) 2006-2008 Johan Engelen
 * Copyright (C) 2006 John Bintz
 * Copyright (C) 2004 MenTaLguY
 * Copyright (C) 1999-2002 Lauris Kaplinski
 * Copyright (C) 2000-2001 Ximian, Inc.
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include <glibmm/i18n.h>
#include <gtkmm/gesturezoom.h>
#include <sigc++/adaptors/bind.h>
#include <2geom/transforms.h>
#include <2geom/rect.h>

#include "desktop.h"
#include "desktop-events.h"
#include "desktop-style.h"
#include "document-undo.h"
#include "inkscape-window.h"
#include "layer-manager.h"
#include "message-context.h"
#include "message-stack.h"
#include "actions/actions-canvas-mode.h"
#include "actions/actions-canvas-transform.h"
#include "actions/actions-view-mode.h" // To update View menu
#include "actions/actions-tools.h" // To change tools
#include "display/drawing.h"
#include "display/control/canvas-temporary-item-list.h"
#include "display/control/snap-indicator.h"
#include "display/control/canvas-item-catchall.h"
#include "display/control/canvas-item-drawing.h"
#include "display/control/canvas-item-group.h"
#include "display/translucency-group.h"
#include "io/fix-broken-links.h"
#include "object/sp-namedview.h"
#include "object/sp-root.h"
#include "ui/dialog/dialog-container.h"
#include "ui/interface.h" // Only for getLayoutPrefPath
#include "ui/tool-factory.h"
#include "ui/tools/tool-base.h"
#include "ui/tools/box3d-tool.h"
#include "ui/widget/canvas-grid.h"
#include "ui/widget/canvas.h"
#include "ui/widget/desktop-widget.h"
#include "ui/widget/events/canvas-event.h"
// TODO those includes are only for node tool quick zoom. Remove them after fixing it.
#include "ui/tools/node-tool.h"
#include "ui/tool/control-point-selection.h"
#include "util/enums.h"

namespace Inkscape::XML { class Node; }

template <typename T>
static void delete_then_null(std::unique_ptr<T> &uptr)
{
    // reset() would null the ptr before calling delete. But a Tool dtor may call getTool(), so
    // do it this way so that until dtor is done, our getTool() will still return the valid ptr
    delete uptr.get();
    uptr.release();
}

SPDesktop::SPDesktop(SPNamedView *namedview_)
    : namedview(namedview_)
{
    // Moving this into the list initializer breaks the application because this->_document_replaced_signal
    // is accessed before it is initialized
    _layer_manager = std::make_unique<Inkscape::LayerManager>(this);
    _selection = std::make_unique<Inkscape::Selection>(this);

    _message_stack = std::make_unique<Inkscape::MessageStack>();
    _tips_message_context = std::make_unique<Inkscape::MessageContext>(*_message_stack);
    _guides_message_context = std::make_unique<Inkscape::MessageContext>(*_message_stack);

    _message_changed_connection = _message_stack->connectChanged([this] (auto type, auto message) {
        _message_idle_connection = Glib::signal_idle().connect([=, this] {
            onStatusMessage(type, message);
            return false;
        }, Glib::PRIORITY_HIGH);
    });

    auto const prefs = Inkscape::Preferences::get();

    current = prefs->getStyle("/desktop/style");

    auto const document = namedview->document;
    dkey = SPItem::display_key_new(1);

    canvas = std::make_unique<Inkscape::UI::Widget::Canvas>();
    canvas->set_desktop(this);

    _setupCanvasItems();

    _temporary_item_list = std::make_unique<Inkscape::Display::TemporaryItemList>();
    _translucency_group = std::make_unique<Inkscape::Display::TranslucencyGroup>(dkey);
    _snapindicator = std::make_unique<Inkscape::Display::SnapIndicator>(this);

    // display rect and zoom are now handled in sp_desktop_widget_realize()

    // pinch zoom
    auto const zoom = Gtk::GestureZoom::create();
    zoom->set_propagation_phase(Gtk::PropagationPhase::CAPTURE);
    zoom->signal_begin().connect(sigc::mem_fun(*this, &SPDesktop::on_zoom_begin));
    zoom->signal_scale_changed().connect(sigc::mem_fun(*this, &SPDesktop::on_zoom_scale));
    zoom->signal_end().connect(sigc::mem_fun(*this, &SPDesktop::on_zoom_end));
    canvas->add_controller(zoom);

    /* Connect document */
    setDocument(document);

    // Set the select tool as the active tool.
    setTool("/tools/select");

    schedule_zoom_from_document();

    apply_preferences_canvas_mode(this);
    apply_preferences_canvas_transform(this);
}

void SPDesktop::_setupCanvasItems()
{
    /* CanvasItems: Controls/Grids/etc. Canvas items are owned by the canvas through
     * canvas_item_root. Canvas items are automatically added and removed from the tree when
     * created and deleted (as long as a canvas item group is passed in the constructor).
     */

    auto const canvas_item_root = canvas->get_canvas_item_root();

    // The order in which these canvas items are added determines the z-order. It's therefore
    // important to add the tempgroup (which will contain the snapindicator) before adding the
    // controls. Only this way one will be able to quickly (before the snap indicator has
    // disappeared) reselect a node after snapping it. If the z-order is wrong however, this
    // will not work (the snap indicator is on top of the node handler; is the snapindicator
    // being selected? or does it intercept some of the events that should have gone to the
    // node handler? see bug https://bugs.launchpad.net/inkscape/+bug/414142)

    _canvas_catchall       = new Inkscape::CanvasItemCatchall{canvas_item_root}; // Lowest item!
    _canvas_group_pages_bg = new Inkscape::CanvasItemGroup   {canvas_item_root};
    _canvas_group_drawing  = new Inkscape::CanvasItemGroup   {canvas_item_root};
    _canvas_group_pages_fg = new Inkscape::CanvasItemGroup   {canvas_item_root};
    _canvas_group_grids    = new Inkscape::CanvasItemGroup   {canvas_item_root};
    _canvas_group_guides   = new Inkscape::CanvasItemGroup   {canvas_item_root};
    _canvas_group_sketch   = new Inkscape::CanvasItemGroup   {canvas_item_root};
    _canvas_group_temp     = new Inkscape::CanvasItemGroup   {canvas_item_root};
    _canvas_group_controls = new Inkscape::CanvasItemGroup   {canvas_item_root};
    _canvas_drawing        = new Inkscape::CanvasItemDrawing {_canvas_group_drawing};

    _canvas_group_pages_bg->set_name("CanvasItemGroup:PagesBg" ); // Page backgrounds
    _canvas_group_drawing ->set_name("CanvasItemGroup:Drawing" ); // The actual SVG drawing.
    _canvas_group_pages_fg->set_name("CanvasItemGroup:PagesFg" ); // Page borders, when on top.
    _canvas_group_grids   ->set_name("CanvasItemGroup:Grids"   ); // Grids.
    _canvas_group_guides  ->set_name("CanvasItemGroup:Guides"  ); // Guides.
    _canvas_group_sketch  ->set_name("CanvasItemGroup:Sketch"  ); // Temporary items before becoming permanent.
    _canvas_group_temp    ->set_name("CanvasItemGroup:Temp"    ); // Temporary items that disappear by themselves.
    _canvas_group_controls->set_name("CanvasItemGroup:Controls"); // Controls (handles, knots, rectangles, etc.).

    _canvas_group_sketch->set_pickable(false); // Temporary items are not pickable!
    _canvas_group_temp  ->set_pickable(false); // Temporary items are not pickable!

    // The root should never emit events. The "catchall" should get it!
    // But somehow there are still exceptions, e.g. Ctrl+scroll to zoom.
    canvas_item_root->connect_event(sigc::bind(&sp_desktop_root_handler, this));
    _canvas_catchall->connect_event(sigc::bind(&sp_desktop_root_handler, this));

    _canvas_drawing->connect_drawing_event(sigc::mem_fun(*this, &SPDesktop::drawing_handler));

    canvas->set_drawing(_canvas_drawing->get_drawing());
}

SPDesktop::~SPDesktop()
{
    _destroy_signal.emit(this);

    delete_then_null(_tool);

    // Canvas
    canvas->set_drawing(nullptr); // Ensures deactivation
    canvas->set_desktop(nullptr); // Todo: Remove desktop dependency.

    if (document) {
        _detachDocument();
    }

    _snapindicator.reset();
    _temporary_item_list.reset();
    _selection.reset();

    _guides_message_context = nullptr;
}

void SPDesktop::setDesktopWidget(SPDesktopWidget *dtw)
{
    _widget = dtw;
}

//--------------------------------------------------------------------
/* Public methods */

/* These methods help for temporarily showing things on-canvas.
 * The *only* valid use of the TemporaryItem* that you get from add_temporary_canvasitem
 * is when you want to prematurely remove the item from the canvas, by calling
 * desktop->remove_temporary_canvasitem(tempitem).
 */
/**
 * One should *not* keep a reference to the SPCanvasItem, the temporary item code will
 * delete the object for you and the reference will become invalid without you knowing it.
 * It is perfectly safe to ignore the returned pointer: the object is deleted by itself, so don't delete it elsewhere!
 * The *only* valid use of the returned TemporaryItem* is as argument for SPDesktop::remove_temporary_canvasitem,
 * because the object might be deleted already without you knowing it.
 * move_to_bottom = true by default so the item does not interfere with handling of other items on the canvas like nodes.
 */
Inkscape::Display::TemporaryItem *SPDesktop::add_temporary_canvasitem(Inkscape::CanvasItem *item, int lifetime_msecs, bool move_to_bottom)
{
    if (move_to_bottom) {
        item->lower_to_bottom();
    }

    return _temporary_item_list->add_item(item, lifetime_msecs);
}

/** It is perfectly safe to call this function while the object has already been deleted due to a timeout.
*/
// Note: This function may free the wrong temporary item if it is called on a freed pointer that
// has had another TemporaryItem reallocated in its place.
void SPDesktop::remove_temporary_canvasitem(Inkscape::Display::TemporaryItem *tempitem)
{
    // check for non-null temporary_item_list, because during destruction of desktop, some destructor might try to access this list!
    if (tempitem && _temporary_item_list) {
        _temporary_item_list->delete_item(tempitem);
    }
}

/**
 * True if desktop viewport intersects \a item's bbox.
 */
bool SPDesktop::isWithinViewport(SPItem const *item) const
{
    auto const bbox = item->desktopVisualBounds();
    if (!bbox) {
        return false;
    }
    auto const viewport = get_display_area();
    return viewport.intersects(*bbox);
}

///
bool SPDesktop::itemIsHidden(SPItem const *item) const {
    return item->isHidden(dkey);
}

/**
 * Set activate status of current desktop's named view.
 */
void
SPDesktop::activate_guides(bool activate)
{
    guides_active = activate;
    namedview->activateGuides(this, activate);
}

/**
 * Make desktop switch documents.
 */
void SPDesktop::change_document(SPDocument *theDocument)
{
    g_return_if_fail(theDocument);

    /* unselect everything before switching documents */
    _selection->clear();

    // Reset any tool actions currently in progress.
    setTool(std::string{_tool->getPrefsPath()}); // Copy so not passing ref to member of reset tool

    setDocument(theDocument);

    /* update the rulers, connect the desktop widget's signal to the new namedview etc.
       (this can probably be done in a better way) */
    getInkscapeWindow()->change_document(theDocument);
    _widget->desktopChangedDocument(this);

    sp_namedview_zoom_and_view_from_document(this);
}

/**
 * Replaces the currently active tool with a new one. Pass the empty string to
 * unset and free the current tool.
 * @param toolName The preferences path for the new tool. Note that if you are calling this to
 * reset the currently active tool, you must copy the string from _tool->getPrefsPath(), so we
 * do not keep a ref to string in reset tool. e.g. setTool(std::string{_tool->getPrefsPath()})
 */
void SPDesktop::setTool(std::string const &toolName)
{
    // Tool should be able to be replaced with itself. See commit 29df5ca05d
    if (_tool) {
        _tool->switching_away(toolName);
        delete_then_null(_tool);
    }

    if (!toolName.empty()) {
        _tool.reset(ToolFactory::createObject(this, toolName));
        // Switch back, though we don't know what the tool was
        if (!_tool->is_ready()) {
            set_active_tool(this, "Select");
            return;
        }
    }

    _event_context_changed_signal.emit(this, _tool.get());
}

/**
 * Sets the coordinate status to a given point
 */
void SPDesktop::set_coordinate_status(Geom::Point const &p) {
    _widget->setCoordinateStatus(p);
}

Inkscape::UI::Dialog::DialogContainer *SPDesktop::getContainer()
{
    return _widget->getDialogContainer();
}

/**
 * \see SPDocument::getItemFromListAtPointBottom()
 */
SPItem *SPDesktop::getItemFromListAtPointBottom(const std::vector<SPItem*> &list, Geom::Point const &p) const
{
    g_return_val_if_fail (doc() != nullptr, NULL);
    return SPDocument::getItemFromListAtPointBottom(dkey, doc()->getRoot(), list, p);
}

/**
 * \see SPDocument::getItemAtPoint()
 */
SPItem *SPDesktop::getItemAtPoint(Geom::Point const &p, bool into_groups, SPItem *upto) const
{
    g_return_val_if_fail (doc() != nullptr, NULL);
    return doc()->getItemAtPoint( dkey, p, into_groups, upto);
}

std::vector<SPItem*> SPDesktop::getItemsAtPoints(std::vector<Geom::Point> points, bool all_layers, bool topmost_only, size_t limit, bool active_only) const
{
    if (!doc())
        return {};
    return doc()->getItemsAtPoints(dkey, points, all_layers, topmost_only, limit, active_only);
}

/**
 * \see SPDocument::getGroupAtPoint()
 */
SPItem *SPDesktop::getGroupAtPoint(Geom::Point const &p) const
{
    g_return_val_if_fail (doc() != nullptr, NULL);
    return doc()->getGroupAtPoint(dkey, p);
}

/**
 * Returns the mouse point in desktop coordinates; if mouse is
 * outside the canvas, returns the center of canvas viewpoint.
 */
Geom::Point SPDesktop::point() const
{
    auto ret = canvas->get_last_mouse();
    auto pt = ret ? *ret : Geom::Point(canvas->get_dimensions()) / 2.0;
    return w2d(canvas->canvas_to_world(pt));
}

/**
 * Revert back to previous transform if possible. Note: current transform is
 * always at front of stack.
 */
void
SPDesktop::prev_transform()
{
    if (transforms_past.empty()) {
        std::cerr << "SPDesktop::prev_transform: current transform missing!" << std::endl;
        return;
    }

    if (transforms_past.size() == 1) {
        messageStack()->flash(Inkscape::WARNING_MESSAGE, _("No previous transform."));
        return;
    }

    // Push current transform into future transforms list.
    transforms_future.push_front( _current_affine );

    // Remove the current transform from the past transforms list.
    transforms_past.pop_front();

    // restore previous transform
    _current_affine = transforms_past.front();
    set_display_area (false);
}

/**
 * Set transform to next in list.
 */
void SPDesktop::next_transform()
{
    if (transforms_future.empty()) {
        this->messageStack()->flash(Inkscape::WARNING_MESSAGE, _("No next transform."));
        return;
    }

    // restore next transform
    _current_affine = transforms_future.front();
    set_display_area (false);

    // remove the just-used transform from the future transforms list
    transforms_future.pop_front();

    // push current transform into past transforms list
    transforms_past.push_front( _current_affine );
}

/**
 * Clear transform lists.
 */
void SPDesktop::clear_transform_history()
{
    transforms_past.clear();
    transforms_future.clear();
}

/**
 * Does all the dirty work in setting the display area.
 * _current_affine must already be full updated (including offset).
 * log: if true, save transform in transform stack for reuse.
 */
void SPDesktop::set_display_area(bool log)
{
    // Save the transform
    if (log) {
        transforms_past.push_front(_current_affine);
        // if we do a logged transform, our transform-forward list is invalidated, so delete it
        transforms_future.clear();
    }

    // Scroll
    canvas->set_pos(_current_affine.getOffset());
    canvas->set_affine(_current_affine.d2w()); // For CanvasItems.

    // Update perspective lines if we are in the 3D box tool (so that infinite ones are shown correctly).
    if (auto const boxtool = dynamic_cast<Inkscape::UI::Tools::Box3dTool*>(_tool.get())) {
        boxtool->_vpdrag->updateLines();
    }

    // Update GUI (TODO: should be handled by CanvasGrid).
    _widget->get_canvas_grid()->updateRulers();
    _widget->get_canvas_grid()->updateScrollbars(_current_affine.getZoom());
    _widget->update_zoom();
    _widget->update_rotation();

    signal_zoom_changed.emit(_current_affine.getZoom());  // Observed by path-manipulator to update arrows.
}

/**
 * Map the drawing to the window so that 'c' lies at 'w' where where 'c'
 * is a point on the canvas and 'w' is position in window in screen pixels.
 */
void SPDesktop::set_display_area(Geom::Point const &c, Geom::Point const &w, bool log)
{
    // The relative offset needed to keep c at w.
    Geom::Point offset = d2w(c) - w;
    _current_affine.addOffset(offset);
    set_display_area(log);
}

/**
 * Map the center of rectangle 'r' (which specifies a non-rotated region of the
 * drawing) to lie at the center of the window. The zoom factor is calculated such that
 * the edges of 'r' closest to 'w' are 'border' length inside of the window (if
 * there is no rotation). 'r' is in document pixel units, 'border' is in screen pixels.
 */
void SPDesktop::set_display_area(Geom::Rect const &r, double border, bool log)
{
    // Create a rectangle the size of the window aligned with origin.
    Geom::Rect w( Geom::Point(), canvas->get_dimensions() );

    // Shrink window to account for border padding.
    w.expandBy(-border);

    double zoom = 1.0;
    // Determine which direction limits scale:
    //   if (r.width/w.width > r.height/w.height) then zoom using width.
    //   Avoiding division in test:
    if ( r.width()*w.height() > r.height()*w.width() ) {
        zoom = w.width() / r.width();
    } else {
        zoom = w.height() / r.height();
    }
    zoom = std::clamp(zoom, SP_DESKTOP_ZOOM_MIN, SP_DESKTOP_ZOOM_MAX);
    _current_affine.setScale( Geom::Scale(zoom, yaxisdir() * zoom) );
    // Zero offset, actual offset calculated later.
    _current_affine.setOffset( Geom::Point( 0, 0 ) );

    set_display_area( r.midpoint(), w.midpoint(), log );
}

/**
 * Return canvas viewbox in desktop coordinates
 */
Geom::Parallelogram SPDesktop::get_display_area() const
{
    // viewbox in world coordinates
    Geom::Rect const viewbox = canvas->get_area_world();

    // display area in desktop coordinates
    return Geom::Parallelogram(viewbox) * w2d();
}

/**
 * Zoom to the given absolute zoom level
 *
 * @param center - Point we want to zoom in on
 * @param zoom - Absolute amount of zoom (1.0 is 100%)
 * @param keep_point - Keep center fixed in the desktop window.
 */
void
SPDesktop::zoom_absolute(Geom::Point const &center, double zoom, bool keep_point)
{
    Geom::Point w = d2w(center); // Must be before zoom changed.
    if(!keep_point) {
        w = Geom::Rect(canvas->get_area_world()).midpoint();
    }
    zoom = CLAMP (zoom, SP_DESKTOP_ZOOM_MIN, SP_DESKTOP_ZOOM_MAX);
    _current_affine.setScale( Geom::Scale(zoom, yaxisdir() * zoom) );
    set_display_area( center, w );
}

/**
 * Zoom in or out relatively to the current zoom
 *
 * @param center - Point we want to zoom in on
 * @param zoom - Relative amount of zoom. at 50% + 50% -> 25% zoom
 * @param keep_point - Keep center fixed in the desktop window.
 */
void
SPDesktop::zoom_relative(Geom::Point const &center, double zoom, bool keep_point)
{
    double new_zoom = _current_affine.getZoom() * zoom;
    this->zoom_absolute(center, new_zoom, keep_point);
}

/**
 * Zoom in to an absolute realworld ratio, e.g. 1:1 physical screen units
 *
 * @param center - Point we want to zoom in on.
 * @param ratio - Absolute physical zoom ratio.
 */
void
SPDesktop::zoom_realworld(Geom::Point const &center, double ratio)
{
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    double correction = prefs->getDouble("/options/zoomcorrection/value", 1.0);
    this->zoom_absolute(center, ratio * correction, false);
}

/**
 * Set display area in only the width dimension.
 */
void SPDesktop::set_display_width(Geom::Rect const &rect, Geom::Coord border)
{
    if (rect.width() < 1.0)
        return;
    auto const center_y = current_center().y();
    set_display_area(Geom::Rect(
        Geom::Point(rect.left(), center_y),
        Geom::Point(rect.width(), center_y)), border);
}

/**
 * Centre Rect, without zooming
 */
void SPDesktop::set_display_center(Geom::Rect const &rect)
{
    zoom_absolute(rect.midpoint(), this->current_zoom(), false);
}

/**
 * Zoom to whole drawing.
 */
void
SPDesktop::zoom_drawing()
{
    g_return_if_fail (doc() != nullptr);
    SPItem *docitem = doc()->getRoot();
    g_return_if_fail (docitem != nullptr);

    docitem->bbox_valid = FALSE;
    Geom::OptRect d = docitem->desktopVisualBounds();

    /* Note that the second condition here indicates that
    ** there are no items in the drawing.
    */
    if ( !d || d->minExtent() < 0.1 ) {
        return;
    }

    set_display_area(*d, 10);
}

/**
 * Zoom to selection.
 */
void
SPDesktop::zoom_selection()
{
    Geom::OptRect const d = _selection->visualBounds();

    if ( !d || d->minExtent() < 0.1 ) {
        return;
    }

    set_display_area(*d, 10);
}

/**
 * Schedule the zoom/view settings from the document to be applied to the desktop
 * just after the canvas is first allocated a size, but before any drawing has started.
 *
 * We do things at this point because we need to know the canvas size in order to center the
 * page correctly, and the page needs to be centered correctly before we start drawing!
 *
 * Note that during startup, GTK is supposed to allocate each widget once. In the past,
 * it was observed doing multiple wrong allocations first, but this no longer happens.
 * This would be a symptom of a widget (like ToolbarWidget) that tries to change its size
 * upon being allocated a size.
 */
void SPDesktop::schedule_zoom_from_document()
{
    if (_schedule_zoom_from_document_connection) {
        return;
    }

    _schedule_zoom_from_document_connection = canvas->connectResize([this] {
        sp_namedview_zoom_and_view_from_document(this);
        _schedule_zoom_from_document_connection.disconnect(); // one-shot
    });
}

Geom::Point SPDesktop::current_center() const {
    return Geom::Rect(canvas->get_area_world()).midpoint() * _current_affine.w2d();
}

/**
 * Performs a quick zoom into what the user is working on.
 *
 * @param  enable  Whether we're going in or out of quick zoom.
 */
void SPDesktop::zoom_quick(bool enable)
{
    if (enable == _quick_zoom_enabled) {
        return;
    }

    if (enable) {
        _quick_zoom_affine = _current_affine;
        bool zoomed = false;

        // TODO This needs to migrate into the node tool, but currently the design
        // of this method is sufficiently wrong to prevent this.
        if (!zoomed) {
            if (auto const nt = dynamic_cast<Inkscape::UI::Tools::NodeTool*>(_tool.get())) {
                if (!nt->_selected_nodes->empty()) {
                    Geom::Rect nodes = *nt->_selected_nodes->bounds();
                    double area = nodes.area();
                    // do not zoom if a single cusp node is selected aand the bounds
                    // have zero area.
                    if (!Geom::are_near(area, 0)) {
                        set_display_area(nodes, true);
                        zoomed = true;
                    }
                }
            }
        }

        if (!zoomed) {
            Geom::OptRect const d = _selection->visualBounds();
            if (d) {
                set_display_area(*d, true);
                zoomed = true;
            }
        }

        if (!zoomed) {
            Geom::Rect const d_canvas = canvas->get_area_world();
            Geom::Point midpoint = w2d(d_canvas.midpoint()); // Midpoint of drawing on canvas.
            zoom_relative(midpoint, 2.0, false);
        }
    } else {
        _current_affine = _quick_zoom_affine;
        set_display_area( false );
    }

    _quick_zoom_enabled = enable;
    return;
}

/**
 * Tell widget to let zoom widget grab keyboard focus.
 */
void
SPDesktop::zoom_grab_focus()
{
    _widget->letZoomGrabFocus();
}

/**
 * Tell widget to let rotate widget grab keyboard focus.
 */
void
SPDesktop::rotate_grab_focus()
{
    _widget->letRotateGrabFocus();
}

/**
 * Set new rotation, keeping the point 'c' fixed in the desktop window.
 *
 * @param c Point in desktop coordinates
 * @param rotate Angle in clockwise direction
 */
void SPDesktop::rotate_absolute_keep_point(Geom::Point const &c, double rotate)
{
    auto const w = d2w(c); // Must be before rotate changed.
    _current_affine.setRotate(rotate);
    set_display_area(c, w);
}

/**
 * Rotate keeping the point 'c' fixed in the desktop window.
 *
 * @param c Point in desktop coordinates
 * @param rotate Angle in clockwise direction
 */
void SPDesktop::rotate_relative_keep_point(Geom::Point const &c, double rotate)
{
    auto const w = d2w(c); // Must be before rotate changed.
    _current_affine.addRotate(rotate);
    set_display_area(c, w);
}

/**
 * Set new rotation, aligning the point 'c' to the center of desktop window.
 *
 * @param c Point in desktop coordinates
 * @param rotate Angle in clockwise direction
 */
void SPDesktop::rotate_absolute_center_point(Geom::Point const &c, double rotate)
{
    _current_affine.setRotate(rotate);
    auto const viewbox = canvas->get_area_world();
    set_display_area(c, viewbox.midpoint());
}

/**
 * Rotate aligning the point 'c' to the center of desktop window.
 *
 * @param c Point in desktop coordinates
 * @param rotate Angle in clockwise direction
 */
void SPDesktop::rotate_relative_center_point(Geom::Point const &c, double rotate)
{
    _current_affine.addRotate(rotate);
    auto const viewbox = canvas->get_area_world();
    set_display_area(c, viewbox.midpoint());
}

/**
 * Set new flip direction, keeping the point 'c' fixed in the desktop window.
 *
 * @param c Point in desktop coordinates
 * @param flip Direction the canvas will be set as.
 */
void
SPDesktop::flip_absolute_keep_point (Geom::Point const &c, CanvasFlip flip)
{
    Geom::Point w = d2w(c); // Must be before flip.
    _current_affine.setFlip(flip);
    set_display_area(c, w);
}

/**
 * Flip direction, keeping the point 'c' fixed in the desktop window.
 *
 * @param c Point in desktop coordinates
 * @param flip Direction to flip canvas
 */
void
SPDesktop::flip_relative_keep_point (Geom::Point const &c, CanvasFlip flip)
{
    Geom::Point w = d2w(c); // Must be before flip.
    _current_affine.addFlip(flip);
    set_display_area(c, w);
}

/**
 * Set new flip direction, aligning the point 'c' to the center of desktop window.
 *
 * @param c Point in desktop coordinates
 * @param flip Direction the canvas will be set as.
 */
void
SPDesktop::flip_absolute_center_point (Geom::Point const &c, CanvasFlip flip)
{
    _current_affine.setFlip(flip);
    Geom::Rect viewbox = canvas->get_area_world();
    set_display_area(c, viewbox.midpoint());
}

/**
 * Flip direction, aligning the point 'c' to the center of desktop window.
 *
 * @param c Point in desktop coordinates
 * @param flip Direction to flip canvas
 */
void
SPDesktop::flip_relative_center_point (Geom::Point const &c, CanvasFlip flip)
{
    _current_affine.addFlip(flip);
    Geom::Rect viewbox = canvas->get_area_world();
    set_display_area(c, viewbox.midpoint());
}

bool
SPDesktop::is_flipped (CanvasFlip flip)
{
    return _current_affine.isFlipped(flip);
}

/**
 * Scroll canvas by to a particular point (window coordinates).
 */
void
SPDesktop::scroll_absolute (Geom::Point const &point)
{
    canvas->set_pos(point);
    _current_affine.setOffset( point );

    /*  update perspective lines if we are in the 3D box tool (so that infinite ones are shown correctly) */
    if (auto const boxtool = dynamic_cast<Inkscape::UI::Tools::Box3dTool*>(_tool.get())) {
        boxtool->_vpdrag->updateLines();
    }

    _widget->get_canvas_grid()->updateRulers();
    _widget->get_canvas_grid()->updateScrollbars(_current_affine.getZoom());
}

/**
 * Scroll canvas by specific coordinate amount (window coordinates).
 */
void
SPDesktop::scroll_relative (Geom::Point const &delta)
{
    Geom::Rect const viewbox = canvas->get_area_world();
    scroll_absolute( viewbox.min() - delta );
}

/**
 * Scroll canvas by specific coordinate amount in svg coordinates.
 */
void
SPDesktop::scroll_relative_in_svg_coords (double dx, double dy)
{
    double scale = _current_affine.getZoom();
    scroll_relative(Geom::Point(dx*scale, dy*scale));
}

/**
 * Scroll screen so as to keep point 'p' visible in window.
 * (Used, for example, during spellcheck.)
 * 'p': The point in desktop coordinates.
 */
// Todo: Eliminate second argument and return value.
bool SPDesktop::scroll_to_point(Geom::Point const &p, double)
{
    auto prefs = Inkscape::Preferences::get();

    // autoscrolldistance is in screen pixels.
    double const autoscrolldistance = prefs->getIntLimited("/options/autoscrolldistance/value", 0, -1000, 10000);

    auto w = Geom::Rect(canvas->get_area_world()); // Window in screen coordinates.
    w.expandBy(-autoscrolldistance);  // Shrink window

    auto const c = d2w(p);  // Point 'p' in screen coordinates.
    if (!w.contains(c)) {
        auto const c2 = w.clamp(c); // Constrain c to window.
        scroll_relative(c2 - c);
        return true;
    }

    return false;
}

bool SPDesktop::isMinimised() const
{
    return getInkscapeWindow()->isMinimised();
}

bool SPDesktop::is_darktheme() const
{
    return getInkscapeWindow()->has_css_class("dark");
}

bool SPDesktop::is_maximized() const
{
    return getInkscapeWindow()->isMaximised();
}

bool SPDesktop::is_fullscreen() const
{
    return getInkscapeWindow()->isFullscreen();
}

/**
 * Checks to see if the user is working in focused mode.
 *
 * @return  the value of \c _focusMode.
 */
bool SPDesktop::is_focusMode() const
{
    return _focusMode;
}

/**
 * Changes whether the user is in focus mode or not.
 *
 * @param  mode  Which mode the view should be in.
 */
void SPDesktop::focusMode(bool mode)
{
    if (mode == _focusMode) {
        return;
    }

    _focusMode = mode;

    layoutWidget();
}

Geom::IntPoint SPDesktop::getWindowSize() const
{
    return _widget->getWindowSize();
}

void SPDesktop::setWindowSize(Geom::IntPoint const &size)
{
    _widget->setWindowSize(size);
}

void SPDesktop::setWindowTransient(Gtk::Window &window, int transient_policy)
{
    _widget->setWindowTransient(window, transient_policy);
}

InkscapeWindow const *SPDesktop::getInkscapeWindow() const
{
    return _widget->get_window();
}

InkscapeWindow *SPDesktop::getInkscapeWindow()
{
    return _widget->get_window();
}

void
SPDesktop::presentWindow()
{
    _widget->presentWindow();
}

void SPDesktop::showInfoDialog(Glib::ustring const &message)
{
    _widget->showInfoDialog(message);
}

bool
SPDesktop::warnDialog (Glib::ustring const &text)
{
    return _widget->warnDialog (text);
}

void SPDesktop::setRenderMode(Inkscape::RenderMode mode)
{
    canvas->set_render_mode(mode);
    if (_widget) {
        _widget->desktopChangedTitle(this);
    }
}

void SPDesktop::setColorMode(Inkscape::ColorMode mode)
{
    canvas->set_color_mode(mode);
    if (_widget) {
        _widget->desktopChangedTitle(this);
    }
}

void
SPDesktop::toggleCommandPalette() {
    _widget->toggle_command_palette();
}
void
SPDesktop::toggleRulers()
{
    _widget->toggle_rulers();
}

void
SPDesktop::toggleScrollbars()
{
    _widget->toggle_scrollbars();
}

/**
 * Shows or hides the on-canvas overlays and controls, such as grids, guides, manipulation handles,
 * knots, selection cues, etc.
 * @param hide - whether the aforementioned UI elements should be hidden
 */
void SPDesktop::setTempHideOverlays(bool hide)
{
    if (_overlays_visible != hide) {
        return; // Nothing to do
    }

    if (hide) {
        _canvas_group_controls->set_visible(false);
        _canvas_group_grids->set_visible(false);
        _saved_guides_visible = namedview->getShowGuides();
        if (_saved_guides_visible) {
            namedview->temporarily_show_guides(false);
        }
        if (canvas && !canvas->has_focus()) {
            canvas->grab_focus(); // Ensure we receive the key up event
        }
        _overlays_visible = false;
    } else {
        _canvas_group_controls->set_visible(true);
        if (_saved_guides_visible) {
            namedview->temporarily_show_guides(true);
        }
        _canvas_group_grids->set_visible(true);
        _overlays_visible = true;
    }
}

// (De)Activate preview mode: hide overlays (grid, guides, etc) and crop content to page areas
void SPDesktop::quick_preview(bool activate) {
    setTempHideOverlays(activate);
    if (canvas) {
        canvas->set_clip_to_page_mode(activate ? true : static_cast<bool>(namedview->clip_to_page));
    }
}

void SPDesktop::toggleToolbar(char const * const toolbar_name)
{
    Glib::ustring pref_path = getLayoutPrefPath(this) + toolbar_name + "/state";

    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    gboolean visible = prefs->getBool(pref_path, true);
    prefs->setBool(pref_path, !visible);

    layoutWidget();
}

void
SPDesktop::layoutWidget()
{
    _widget->layoutWidgets();
}

/**
 *  onWindowStateChanged
 *
 *  Called when the window changes its maximize/fullscreen/iconify/pinned state.
 */
void
SPDesktop::onWindowStateChanged(Gdk::Toplevel::State const changed,
                                Gdk::Toplevel::State const new_state)
{
    // Layout may differ depending on full-screen mode or not
    if (Inkscape::Util::has_flag(changed, Gdk::Toplevel::State::FULLSCREEN | Gdk::Toplevel::State::MAXIMIZED)) {
        layoutWidget();
        view_set_gui(getInkscapeWindow()); // Updates View menu
    }
}

/**
  * Apply the desktop's current style or the tool style to the object.
  */
void SPDesktop::applyCurrentOrToolStyle(SPObject *obj, Glib::ustring const &tool_path, bool with_text)
{
    SPCSSAttr *css_current = sp_desktop_get_style(this, with_text);
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();

    if (prefs->getBool(tool_path + "/usecurrent") && css_current) {
        obj->setCSS(css_current,"style");
    } else {
        SPCSSAttr *css = prefs->getInheritedStyle(tool_path + "/style");
        obj->setCSS(css,"style");
        sp_repr_css_attr_unref(css);
    }
    if (css_current) {
        sp_repr_css_attr_unref(css_current);
    }
}

void
SPDesktop::setToolboxFocusTo(char const * const label)
{
    _widget->setToolboxFocusTo (label);
}

void
SPDesktop::setToolboxAdjustmentValue(char const * const id, double const val)
{
    _widget->setToolboxAdjustmentValue (id, val);
}

Gtk::Widget *SPDesktop::get_toolbar_by_name(const Glib::ustring &name)
{
    return _widget->get_toolbar_by_name(name);
}

bool
SPDesktop::isToolboxButtonActive(char const * const id) const
{
    return _widget->isToolboxButtonActive(id);
}

void SPDesktop::updateDialogs()
{
    getContainer()->set_inkscape_window(getInkscapeWindow());
}

void SPDesktop::setWaitingCursor()
{
    canvas->set_cursor("wait");

    // GDK needs the flush for the cursor change to take effect
    // TODO: GTK4: Is that still the case?
    // display->flush();

    waiting_cursor = true;
}

void SPDesktop::clearWaitingCursor() {
  if (waiting_cursor && _tool) {
      _tool->use_tool_cursor();
  }
}

void SPDesktop::toggleLockGuides()
{
    namedview->toggleLockGuides();
}

//----------------------------------------------------------------------
// Callback implementations. The virtual ones are connected by the view.

/**
 * Associate document with desktop.
 */
void SPDesktop::setDocument(SPDocument *doc)
{
    if (document) {
        _detachDocument();
    }

    _selection->setDocument(doc);
    document = doc;

    if (document) {
        _attachDocument();
    }
}

void SPDesktop::_attachDocument()
{
    /* XXX:
     * ensureUpToDate() sends a 'modified' signal to the root element.
     * This is required to prevent flickering after the document
     * loads. However, many SPObjects write to their repr in response
     * to this signal. This is apparently done to support live path effects,
     * which rewrite their result paths after each modification of the base object.
     * This causes the generation of an incomplete undo transaction,
     * which causes problems down the line, including crashes in the
     * Undo History dialog.
     *
     * For now, this is handled by disabling undo tracking during this call.
     * A proper fix would involve modifying the way ensureUpToDate() works,
     * so that the LPE results are not rewritten.
     */
    {
        Inkscape::DocumentUndo::ScopedInsensitive _no_undo(document);
        document->ensureUpToDate();
    }

    /* Set up notification of rebuilding the document, this allows
       for saving object related settings in the document. */
    _reconstruction_start_connection = document->connectReconstructionStart(sigc::mem_fun(*this, &SPDesktop::reconstruction_start));
    _reconstruction_finish_connection = document->connectReconstructionFinish(sigc::mem_fun(*this, &SPDesktop::reconstruction_finish));
    _reconstruction_old_layer_id.clear();

    _y_axis_flipped = document->get_y_axis_flipped().connect([this](double yshift){ handle_y_axis_flip(yshift); });

    auto const drawing = _canvas_drawing->get_drawing();

    if (auto const drawing_item = document->getRoot()->invoke_show(*drawing, dkey, SP_ITEM_SHOW_DISPLAY)) {
        drawing->root()->prependChild(drawing_item);
    }

    namedview = document->getNamedView();
    namedview->viewcount++;
    namedview->show(this);
    namedview->setShowGrids(namedview->getShowGrids());
    namedview->set_desk_color(this); // Background page sits on.

    _view_number = namedview->viewcount;

    /* Ugly hack */
    activate_guides(true);

    _document_uri_set_connection = document->connectFilenameSet([this] (auto) {
        _widget->desktopChangedTitle(this);
    });
    _saved_or_modified_conn = document->connectSavedOrModified([this] {
        _widget->desktopChangedTitle(this);
    });

    // set new document before firing signal, so handlers can see new value if they query desktop
    _document_replaced_signal.emit(this, document);

    sp_namedview_update_layers_from_document(this);
}

void SPDesktop::_detachDocument()
{
    namedview->hide(this);
    document->getRoot()->invoke_hide(dkey);

    _document_uri_set_connection.disconnect();
    _saved_or_modified_conn.disconnect();
    _reconstruction_start_connection.disconnect();
    _reconstruction_finish_connection.disconnect();
    _schedule_zoom_from_document_connection.disconnect();
}

void SPDesktop::showNotice(Glib::ustring const &msg, int timeout)
{
    _widget->showNotice(msg, timeout);
}

void SPDesktop::onStatusMessage(Inkscape::MessageType type, char const *message)
{
    if (_widget && _widget->get_desktop() == this) {
        _widget->setMessage(type, message);
    }
}

/**
 * Calls event handler of current event context.
 */
bool SPDesktop::drawing_handler(Inkscape::CanvasEvent const &event, Inkscape::DrawingItem *drawing_item)
{
    auto const tool = getTool();
    if (!tool) return false;

    if (event.type() == Inkscape::EventType::KEY_PRESS &&
        Inkscape::UI::Tools::get_latin_keyval(static_cast<Inkscape::KeyPressEvent const &>(event)) == GDK_KEY_space &&
        tool->is_space_panning())
    {
        return true;
    }

    if (drawing_item) {
        return tool->start_item_handler(drawing_item->getItem(), event);
    }

    return tool->start_root_handler(event);
}

/// Called when document is starting to be rebuilt.
void SPDesktop::reconstruction_start()
{
    auto layer = layerManager().currentLayer();
    _reconstruction_old_layer_id = layer->getId() ? layer->getId() : "";
    layerManager().reset();

    getSelection()->clear();
}

/// Called when document rebuild is finished.
void SPDesktop::reconstruction_finish()
{
    g_debug("Desktop, finishing reconstruction\n");
    if (!_reconstruction_old_layer_id.empty()) {
        if (auto const newLayer = getNamedView()->document->getObjectById(_reconstruction_old_layer_id)) {
            layerManager().setCurrentLayer(newLayer);
        }

        _reconstruction_old_layer_id.clear();
    }
    g_debug("Desktop, finishing reconstruction end\n");
}

void SPDesktop::handle_y_axis_flip(double yshift) {
    // selection is repainted in a wrong location, so clearing it for now
    _selection->clear();

    auto offset = _current_affine.getOffset();
    auto zoom = _current_affine.getZoom();
    _current_affine.setScale(Geom::Scale(zoom, yaxisdir() * zoom));
    _current_affine.setOffset(Geom::Point(offset.x(), offset.y() + zoom * yshift));
    set_display_area(false);
}

Geom::Affine const &SPDesktop::doc2dt() const
{
    assert(document);
    return document->doc2dt();
}

Geom::Affine const &SPDesktop::dt2doc() const
{
    assert(document);
    return document->dt2doc();
}

sigc::connection SPDesktop::connect_gradient_stop_selected(sigc::slot<void (SPStop *)> const &slot) {
    return _gradient_stop_selected.connect(slot);
}

sigc::connection SPDesktop::connect_control_point_selected(sigc::slot<void (Inkscape::UI::ControlPointSelection *)> const &slot) {
    return _control_point_selected.connect(slot);
}

sigc::connection SPDesktop::connect_text_cursor_moved(sigc::slot<void (Inkscape::UI::Tools::TextTool*)> const &slot) {
    return _text_cursor_moved.connect(slot);
}

void SPDesktop::emit_gradient_stop_selected(SPStop *stop) {
    _gradient_stop_selected.emit(stop);
}

void SPDesktop::emit_control_point_selected(Inkscape::UI::ControlPointSelection *selection) {
    _control_point_selected.emit(selection);
}

void SPDesktop::emit_text_cursor_moved(Inkscape::UI::Tools::TextTool *tool) {
    _text_cursor_moved.emit(tool);
}

/*
 * pinch zoom
 */

void SPDesktop::on_zoom_begin(Gdk::EventSequence * /*sequence*/)
{
    _begin_zoom = current_zoom();
}

void SPDesktop::on_zoom_scale(double const scale)
{
    if (!_begin_zoom) {
        std::cerr << "on_zoom_scale: Missed on_zoom_begin event" << std::endl;
        return;
    }
    auto const widget_point = canvas->get_last_mouse().value_or(canvas->get_dimensions() / 2);
    auto const world_point = canvas->canvas_to_world(widget_point);
    zoom_absolute(w2d(world_point), *_begin_zoom * scale);
}

void SPDesktop::on_zoom_end(Gdk::EventSequence * /*sequence*/)
{
    _begin_zoom.reset();
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

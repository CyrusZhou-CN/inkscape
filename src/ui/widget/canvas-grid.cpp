// SPDX-License-Identifier: GPL-2.0-or-later

/*
 * Author:
 *   Tavmjong Bah
 *
 * Rewrite of code originally in desktop-widget.cpp.
 *
 * Copyright (C) 2020 Tavmjong Bah
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

// The scrollbars, and canvas are tightly coupled so it makes sense to have a dedicated
// widget to handle their interactions. The buttons are along for the ride. I don't see
// how to add the buttons easily via a .ui file (which would allow the user to put any
// buttons they want in their place).

#include <glibmm/i18n.h>
#include <gtkmm/enums.h>
#include <gtkmm/label.h>

#include "canvas-grid.h"

#include "desktop.h"        // Hopefully temp.
#include "desktop-events.h" // Hopefully temp.

#include "selection.h"

#include "display/control/canvas-item-drawing.h" // sticky

#include "page-manager.h"

#include "ui/dialog/command-palette.h"
#include "ui/icon-loader.h"
#include "ui/widget/canvas.h"
#include "ui/widget/events/canvas-event.h"
#include "ui/widget/canvas-notice.h"
#include "ui/widget/ink-ruler.h"
#include "io/resource.h"

#include "widgets/desktop-widget.h"  // Hopefully temp.

namespace Inkscape {
namespace UI {
namespace Widget {

CanvasGrid::CanvasGrid(SPDesktopWidget *dtw)
{
    _dtw = dtw;
    set_name("CanvasGrid");

    // Canvas
    _canvas = std::make_unique<Inkscape::UI::Widget::Canvas>();
    _canvas->set_hexpand(true);
    _canvas->set_vexpand(true);
    _canvas->set_can_focus(true);

    // Command palette
    _command_palette = std::make_unique<Inkscape::UI::Dialog::CommandPalette>();

    // Notice overlay, note using unique_ptr will cause destruction race conditions
    _notice = CanvasNotice::create();

    // Canvas overlay
    _canvas_overlay.add(*_canvas);
    _canvas_overlay.add_overlay(*_command_palette->get_base_widget());
    _canvas_overlay.add_overlay(*_notice);

    // Horizontal Ruler
    _hruler = std::make_unique<Inkscape::UI::Widget::Ruler>(Gtk::ORIENTATION_HORIZONTAL);
    _hruler->add_track_widget(*_canvas);
    _hruler->set_hexpand(true);
    _hruler->set_visible(true);
    // Tooltip/Unit set elsewhere

    // Vertical Ruler
    _vruler = std::make_unique<Inkscape::UI::Widget::Ruler>(Gtk::ORIENTATION_VERTICAL);
    _vruler->add_track_widget(*_canvas);
    _vruler->set_vexpand(true);
    _vruler->set_visible(true);
    // Tooltip/Unit set elsewhere.

    // Guide Lock
    _guide_lock.set_name("LockGuides");
    _guide_lock.add(*Gtk::make_managed<Gtk::Image>("object-locked", Gtk::ICON_SIZE_MENU));
    // To be replaced by Gio::Action:
    _guide_lock.signal_toggled().connect(sigc::mem_fun(*_dtw, &SPDesktopWidget::update_guides_lock));
    _guide_lock.set_tooltip_text(_("Toggle lock of all guides in the document"));
    // Subgrid
    _subgrid.attach(_guide_lock,     0, 0, 1, 1);
    _subgrid.attach(*_vruler,        0, 1, 1, 1);
    _subgrid.attach(*_hruler,        1, 0, 1, 1);
    _subgrid.attach(_canvas_overlay, 1, 1, 1, 1);

    // Horizontal Scrollbar
    _hadj = Gtk::Adjustment::create(0.0, -4000.0, 4000.0, 10.0, 100.0, 4.0);
    _hadj->signal_value_changed().connect(sigc::mem_fun(*_dtw, &SPDesktopWidget::on_adjustment_value_changed));
    _hscrollbar = Gtk::Scrollbar(_hadj, Gtk::ORIENTATION_HORIZONTAL);
    _hscrollbar.set_name("CanvasScrollbar");
    _hscrollbar.set_hexpand(true);

    // Vertical Scrollbar
    _vadj = Gtk::Adjustment::create(0.0, -4000.0, 4000.0, 10.0, 100.0, 4.0);
    _vadj->signal_value_changed().connect(sigc::mem_fun(*_dtw, &SPDesktopWidget::on_adjustment_value_changed));
    _vscrollbar = Gtk::Scrollbar(_vadj, Gtk::ORIENTATION_VERTICAL);
    _vscrollbar.set_name("CanvasScrollbar");
    _vscrollbar.set_vexpand(true);

    // CMS Adjust (To be replaced by Gio::Action)
    _cms_adjust.set_name("CMS_Adjust");
    _cms_adjust.add(*Gtk::make_managed<Gtk::Image>("color-management", Gtk::ICON_SIZE_MENU));
    // Can't access via C++ API, fixed in Gtk4.
    gtk_actionable_set_action_name( GTK_ACTIONABLE(_cms_adjust.gobj()), "win.canvas-color-manage");
    _cms_adjust.set_tooltip_text(_("Toggle color-managed display for this document window"));

    // popover with some common display mode related options
    auto builder = Gtk::Builder::create_from_file(Inkscape::IO::Resource::get_filename(Inkscape::IO::Resource::UIS, "display-popup.glade"));
    _display_popup = builder;
    Gtk::Popover* popover;
    _display_popup->get_widget("popover", popover);
    Gtk::CheckButton* sticky_zoom;
    _display_popup->get_widget("zoom-resize", sticky_zoom);
    // To be replaced by Gio::Action:
    sticky_zoom->signal_toggled().connect([=](){ _dtw->sticky_zoom_toggled(); });
    _quick_actions.set_name("QuickActions");
    _quick_actions.set_popover(*popover);
    _quick_actions.set_image_from_icon_name("display-symbolic");
    _quick_actions.set_direction(Gtk::ARROW_LEFT);
    _quick_actions.set_tooltip_text(_("Display options"));

    // Main grid
    attach(_subgrid,       0, 0, 1, 2);
    attach(_hscrollbar,    0, 2, 1, 1);
    attach(_cms_adjust,    1, 2, 1, 1);
    attach(_quick_actions, 1, 0, 1, 1);
    attach(_vscrollbar,    1, 1, 1, 1);

    // For creating guides, etc.
    _hruler->signal_button_press_event().connect(
        sigc::bind(sigc::mem_fun(*_dtw, &SPDesktopWidget::on_ruler_box_button_press_event),   _hruler.get(), true));
    _hruler->signal_button_release_event().connect(
        sigc::bind(sigc::mem_fun(*_dtw, &SPDesktopWidget::on_ruler_box_button_release_event), _hruler.get(), true));
    _hruler->signal_motion_notify_event().connect(
        sigc::bind(sigc::mem_fun(*_dtw, &SPDesktopWidget::on_ruler_box_motion_notify_event),  _hruler.get(), true));

    // For creating guides, etc.
    _vruler->signal_button_press_event().connect(
        sigc::bind(sigc::mem_fun(*_dtw, &SPDesktopWidget::on_ruler_box_button_press_event),   _vruler.get(), false));
    _vruler->signal_button_release_event().connect(
        sigc::bind(sigc::mem_fun(*_dtw, &SPDesktopWidget::on_ruler_box_button_release_event), _vruler.get(), false));
    _vruler->signal_motion_notify_event().connect(
        sigc::bind(sigc::mem_fun(*_dtw, &SPDesktopWidget::on_ruler_box_motion_notify_event),  _vruler.get(), false));

    show_all();
}

CanvasGrid::~CanvasGrid()
{
    _page_modified_connection.disconnect();
    _page_selected_connection.disconnect();
    _sel_modified_connection.disconnect();
    _sel_changed_connection.disconnect();
    _document = nullptr;
    _notice = nullptr;
}

void CanvasGrid::on_realize() {
    // actions should be available now

    if (auto map = _dtw->get_action_map()) {
        auto set_display_icon = [=]() {
            Glib::ustring id;
            auto mode = _canvas->get_render_mode();
            switch (mode) {
                case RenderMode::NORMAL: id = "display";
                    break;
                case RenderMode::OUTLINE: id = "display-outline";
                    break;
                case RenderMode::OUTLINE_OVERLAY: id = "display-outline-overlay";
                    break;
                case RenderMode::VISIBLE_HAIRLINES: id = "display-enhance-stroke";
                    break;
                case RenderMode::NO_FILTERS: id = "display-no-filter";
                    break;
                default:
                    g_warning("Unknown display mode in canvas-grid");
                    break;
            }

            if (!id.empty()) {
                // if CMS is ON show alternative icons
                if (_canvas->get_cms_active()) {
                    id += "-alt";
                }
                _quick_actions.set_image_from_icon_name(id + "-symbolic");
            }
        };

        set_display_icon();

        // when display mode state changes, update icon
        auto cms_action = Glib::RefPtr<Gio::SimpleAction>::cast_dynamic(map->lookup_action("canvas-color-manage"));
        auto disp_action = Glib::RefPtr<Gio::SimpleAction>::cast_dynamic(map->lookup_action("canvas-display-mode"));

        if (cms_action && disp_action) {
            disp_action->signal_activate().connect([=](const Glib::VariantBase& state){ set_display_icon(); });
            cms_action-> signal_activate().connect([=](const Glib::VariantBase& state){ set_display_icon(); });
        }
        else {
            g_warning("No canvas-display-mode and/or canvas-color-manage action available to canvas-grid");
        }
    }
    else {
        g_warning("No action map available to canvas-grid");
    }

    parent_type::on_realize();
}

// TODO: remove when sticky zoom gets replaced by Gio::Action:
Gtk::ToggleButton* CanvasGrid::GetStickyZoom() {
    Gtk::CheckButton* sticky_zoom;
    _display_popup->get_widget("zoom-resize", sticky_zoom);
    return sticky_zoom;
}

// _dt2r should be a member of _canvas.
// get_display_area should be a member of _canvas.
void
CanvasGrid::UpdateRulers()
{
    auto prefs = Inkscape::Preferences::get();
    auto desktop = _dtw->desktop;
    auto document = desktop->getDocument();
    auto &pm = document->getPageManager();
    auto sel = desktop->getSelection();

    // Our connections to the document are handled with a lazy pattern to avoid
    // having to refactor the SPDesktopWidget class. We know UpdateRulers is
    // called in all situations when documents are loaded and replaced.
    if (document != _document) {
        _document = document;
        _page_selected_connection = pm.connectPageSelected([=](SPPage *) { UpdateRulers(); });
        _page_modified_connection = pm.connectPageModified([=](SPPage *) { UpdateRulers(); });
        _sel_modified_connection = sel->connectModified([=](Inkscape::Selection *, int) { UpdateRulers(); });
        _sel_changed_connection = sel->connectChanged([=](Inkscape::Selection *) { UpdateRulers(); });
    }

    Geom::Rect viewbox = desktop->get_display_area().bounds();
    Geom::Rect startbox = viewbox;
    if (prefs->getBool("/options/origincorrection/page", true)) {
        // Move viewbox according to the selected page's position (if any)
        startbox *= pm.getSelectedPageAffine().inverse();
    }

    // Scale and offset the ruler coordinates
    // Use an integer box to align the ruler to the grid and page.
    auto rulerbox = (startbox * Geom::Scale(_dtw->_dt2r));
    _hruler->set_range(rulerbox.left(), rulerbox.right());
    if (_dtw->desktop->is_yaxisdown()) {
        _vruler->set_range(rulerbox.top(), rulerbox.bottom());
    } else {
        _vruler->set_range(rulerbox.bottom(), rulerbox.top());
    }

    Geom::Point pos(_canvas->get_pos());
    auto scale = _canvas->get_affine();
    auto d2c = Geom::Translate(pos * scale.inverse()).inverse() * scale;
    auto pagebox = (pm.getSelectedPageRect() * d2c).roundOutwards();
    _hruler->set_page(pagebox.left(), pagebox.right());
    _vruler->set_page(pagebox.top(), pagebox.bottom());

    Geom::Rect selbox = Geom::IntRect(0, 0, 0, 0);
    if (auto bbox = sel->preferredBounds())
        selbox = (*bbox * d2c).roundOutwards();
    _hruler->set_selection(selbox.left(), selbox.right());
    _vruler->set_selection(selbox.top(), selbox.bottom());
}

void
CanvasGrid::ShowScrollbars(bool state)
{
    if (_show_scrollbars == state) return;
    _show_scrollbars = state;

    if (_show_scrollbars) {
        // Show scrollbars
        _hscrollbar.set_visible(true);
        _vscrollbar.set_visible(true);
        _cms_adjust.set_visible(true);
        _cms_adjust.show_all_children();
        _quick_actions.set_visible(true);
    } else {
        // Hide scrollbars
        _hscrollbar.set_visible(false);
        _vscrollbar.set_visible(false);
        _cms_adjust.set_visible(false);
        _quick_actions.set_visible(false);
    }
}

void
CanvasGrid::ToggleScrollbars()
{
    _show_scrollbars = !_show_scrollbars;
    ShowScrollbars(_show_scrollbars);

    // Will be replaced by actions
    auto prefs = Inkscape::Preferences::get();
    prefs->setBool("/fullscreen/scrollbars/state", _show_scrollbars);
    prefs->setBool("/window/scrollbars/state", _show_scrollbars);
}

void
CanvasGrid::ShowRulers(bool state)
{
    if (_show_rulers == state) return;
    _show_rulers = state;

    if (_show_rulers) {
        // Show rulers
        _hruler->set_visible(true);
        _vruler->set_visible(true);
        _guide_lock.set_visible(true);
        _guide_lock.show_all_children();
    } else {
        // Hide rulers
        _hruler->set_visible(false);
        _vruler->set_visible(false);
        _guide_lock.set_visible(false);
    }
}

void
CanvasGrid::ToggleRulers()
{
    _show_rulers = !_show_rulers;
    ShowRulers(_show_rulers);

    // Will be replaced by actions
    auto prefs = Inkscape::Preferences::get();
    prefs->setBool("/fullscreen/rulers/state", _show_rulers);
    prefs->setBool("/window/rulers/state", _show_rulers);
}

void
CanvasGrid::ToggleCommandPalette()
{
    _command_palette->toggle();
}

void
CanvasGrid::showNotice(Glib::ustring const &msg, unsigned timeout)
{
    _notice->show(msg, timeout);
}

void
CanvasGrid::ShowCommandPalette(bool state)
{
    if (state) {
        _command_palette->open();
    }
    _command_palette->close();
}

// Update rulers on change of widget size, but only if allocation really changed.
void
CanvasGrid::on_size_allocate(Gtk::Allocation& allocation)
{
    Gtk::Grid::on_size_allocate(allocation);
    if (!(_allocation == allocation)) { // No != function defined!
        _allocation = allocation;
        UpdateRulers();
    }
}

// TODO Add actions so we can set shortcuts.
// * Sticky Zoom
// * CMS Adjust
// * Guide Lock


} // namespace Widget
} // namespace UI
} // namespace Inkscape

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

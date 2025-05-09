// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * @file Node toolbar
 */
/* Authors:
 *   MenTaLguY <mental@rydia.net>
 *   Lauris Kaplinski <lauris@kaplinski.com>
 *   bulia byak <buliabyak@users.sf.net>
 *   Frank Felfe <innerspace@iname.com>
 *   John Cliff <simarilius@yahoo.com>
 *   David Turner <novalis@gnu.org>
 *   Josh Andler <scislac@scislac.com>
 *   Jon A. Cruz <jon@joncruz.org>
 *   Maximilian Albert <maximilian.albert@gmail.com>
 *   Tavmjong Bah <tavmjong@free.fr>
 *   Abhishek Sharma
 *   Kris De Gussem <Kris.DeGussem@gmail.com>
 *   Vaibhav Malik <vaibhavmalik2018@gmail.com>
 *
 * Copyright (C) 2004 David Turner
 * Copyright (C) 2003 MenTaLguY
 * Copyright (C) 1999-2011 authors
 * Copyright (C) 2001-2002 Ximian, Inc.
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "node-toolbar.h"

#include <giomm/simpleactiongroup.h>
#include <glibmm/i18n.h>
#include <gtkmm/adjustment.h>
#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/image.h>
#include <gtkmm/menubutton.h>
#include <gtkmm/togglebutton.h>
#include <sigc++/functors/mem_fun.h>

#include "desktop.h"
#include "document-undo.h"
#include "inkscape.h"
#include "object/sp-namedview.h"
#include "page-manager.h"
#include "selection.h"
#include "ui/builder-utils.h"
#include "ui/simple-pref-pusher.h"
#include "ui/tool/control-point-selection.h"
#include "ui/tool/multi-path-manipulator.h"
#include "ui/tools/node-tool.h"
#include "ui/util.h"
#include "ui/widget/combo-tool-item.h"
#include "ui/widget/spinbutton.h"
#include "ui/widget/unit-tracker.h"
#include "widgets/widget-sizes.h"

using Inkscape::UI::Widget::UnitTracker;
using Inkscape::Util::Unit;
using Inkscape::Util::Quantity;
using Inkscape::DocumentUndo;
using Inkscape::UI::Tools::NodeTool;

namespace Inkscape::UI::Toolbar {

NodeToolbar::NodeToolbar()
    : NodeToolbar{create_builder("toolbar-node.ui")}
{}

NodeToolbar::NodeToolbar(Glib::RefPtr<Gtk::Builder> const &builder)
    : Toolbar{get_widget<Gtk::Box>(builder, "node-toolbar")}
    , _tracker{std::make_unique<UnitTracker>(Util::UNIT_TYPE_LINEAR)}
    , _nodes_lpeedit_btn{get_widget<Gtk::Button>(builder, "_nodes_lpeedit_btn")}
    , _show_helper_path_btn{&get_widget<Gtk::ToggleButton>(builder, "_show_helper_path_btn")}
    , _show_handles_btn{&get_widget<Gtk::ToggleButton>(builder, "_show_handles_btn")}
    , _show_transform_handles_btn{&get_widget<Gtk::ToggleButton>(builder, "_show_transform_handles_btn")}
    , _object_edit_mask_path_btn{&get_widget<Gtk::ToggleButton>(builder, "_object_edit_mask_path_btn")}
    , _object_edit_clip_path_btn{&get_widget<Gtk::ToggleButton>(builder, "_object_edit_clip_path_btn")}
    , _nodes_x_item{get_derived_widget<UI::Widget::SpinButton>(builder, "_nodes_x_item")}
    , _nodes_y_item{get_derived_widget<UI::Widget::SpinButton>(builder, "_nodes_y_item")}
    , _nodes_d_item{get_derived_widget<UI::Widget::SpinButton>(builder, "_nodes_d_item")}
    , _nodes_d_box{get_widget<Gtk::Box>(builder, "_nodes_d_box")}
{
    // Setup the derived spin buttons.
    setup_derived_spin_button(_nodes_x_item, "x");
    setup_derived_spin_button(_nodes_y_item, "y");
    setup_derived_spin_button(_nodes_d_item, "d");

    auto unit_menu = _tracker->create_tool_item(_("Units"), (""));
    get_widget<Gtk::Box>(builder, "unit_menu_box").append(*unit_menu);

    // Attach the signals.
    struct ButtonMapping
    {
        char const *button_id;
        void (NodeToolbar::*callback)();
    };

    static constexpr ButtonMapping button_mapping[] = {
        {"insert_node_btn", &NodeToolbar::edit_add},
        {"delete_btn", &NodeToolbar::edit_delete},
        {"join_btn", &NodeToolbar::edit_join},
        {"break_btn", &NodeToolbar::edit_break},
        {"join_segment_btn", &NodeToolbar::edit_join_segment},
        {"delete_segment_btn", &NodeToolbar::edit_delete_segment},
        {"cusp_btn", &NodeToolbar::edit_cusp},
        {"smooth_btn", &NodeToolbar::edit_smooth},
        {"symmetric_btn", &NodeToolbar::edit_symmetrical},
        {"auto_btn", &NodeToolbar::edit_auto},
        {"line_btn", &NodeToolbar::edit_toline},
        {"curve_btn", &NodeToolbar::edit_tocurve},
    };

    for (auto const &button_info : button_mapping) {
        get_widget<Gtk::Button>(builder, button_info.button_id)
            .signal_clicked()
            .connect(sigc::mem_fun(*this, button_info.callback));
    }

    setup_insert_node_menu();

    _pusher_show_outline = std::make_unique<SimplePrefPusher>(_show_helper_path_btn, "/tools/nodes/show_outline");
    _show_helper_path_btn->signal_toggled().connect(sigc::bind(sigc::mem_fun(*this, &NodeToolbar::on_pref_toggled),
                                                               _show_helper_path_btn, "/tools/nodes/show_outline"));

    _pusher_show_handles = std::make_unique<SimplePrefPusher>(_show_handles_btn, "/tools/nodes/show_handles");
    _show_handles_btn->signal_toggled().connect(sigc::bind(sigc::mem_fun(*this, &NodeToolbar::on_pref_toggled),
                                                           _show_handles_btn, "/tools/nodes/show_handles"));

    _pusher_show_transform_handles =
        std::make_unique<SimplePrefPusher>(_show_transform_handles_btn, "/tools/nodes/show_transform_handles");
    _show_transform_handles_btn->signal_toggled().connect(
        sigc::bind(sigc::mem_fun(*this, &NodeToolbar::on_pref_toggled), _show_transform_handles_btn,
                   "/tools/nodes/show_transform_handles"));

    _pusher_edit_masks = std::make_unique<SimplePrefPusher>(_object_edit_mask_path_btn, "/tools/nodes/edit_masks");
    _object_edit_mask_path_btn->signal_toggled().connect(sigc::bind(
        sigc::mem_fun(*this, &NodeToolbar::on_pref_toggled), _object_edit_mask_path_btn, "/tools/nodes/edit_masks"));

    _pusher_edit_clipping_paths =
        std::make_unique<SimplePrefPusher>(_object_edit_clip_path_btn, "/tools/nodes/edit_clipping_paths");
    _object_edit_clip_path_btn->signal_toggled().connect(sigc::bind(sigc::mem_fun(*this, &NodeToolbar::on_pref_toggled),
                                                                    _object_edit_clip_path_btn,
                                                                    "/tools/nodes/edit_clipping_paths"));

    _initMenuBtns();
}

NodeToolbar::~NodeToolbar() = default;

void NodeToolbar::setDesktop(SPDesktop *desktop)
{
    if (_desktop) {
        c_selection_changed.disconnect();
        c_selection_modified.disconnect();
        c_subselection_changed.disconnect();
    }

    Toolbar::setDesktop(desktop);

    if (_desktop) {
        // watch selection
        c_selection_changed = desktop->getSelection()->connectChanged(sigc::mem_fun(*this, &NodeToolbar::sel_changed));
        c_selection_modified = desktop->getSelection()->connectModified(sigc::mem_fun(*this, &NodeToolbar::sel_modified));
        c_subselection_changed = desktop->connect_control_point_selected([this] (ControlPointSelection *selection) {
            coord_changed(selection);
        });

        sel_changed(desktop->getSelection());
        coord_changed(get_node_tool()->_selected_nodes);
    }
}

void NodeToolbar::setActiveUnit(Util::Unit const *unit)
{
    _tracker->setActiveUnit(unit);
}

void NodeToolbar::setup_derived_spin_button(UI::Widget::SpinButton &btn, Glib::ustring const &name)
{
    auto adj = btn.get_adjustment();
    adj->set_value(0);
    adj->signal_value_changed().connect(sigc::bind(sigc::mem_fun(*this, &NodeToolbar::value_changed), name, adj));

    _tracker->addAdjustment(adj->gobj());
    btn.addUnitTracker(_tracker.get());

    btn.setDefocusTarget(this);
}

void NodeToolbar::setup_insert_node_menu()
{
    // insert_node_menu
    auto const actions = Gio::SimpleActionGroup::create();
    actions->add_action("insert-leftmost", sigc::mem_fun(*this, &NodeToolbar::edit_add_leftmost));
    actions->add_action("insert-rightmost", sigc::mem_fun(*this, &NodeToolbar::edit_add_rightmost));
    actions->add_action("insert-topmost", sigc::mem_fun(*this, &NodeToolbar::edit_add_topmost));
    actions->add_action("insert-bottommost", sigc::mem_fun(*this, &NodeToolbar::edit_add_bottommost));
    insert_action_group("node-toolbar", actions);
}

void NodeToolbar::value_changed(Glib::ustring const &name, Glib::RefPtr<Gtk::Adjustment> const &adj)
{
    // quit if run by the XML listener or a unit change
    if (_blocker.pending() || _tracker->isUpdating()) {
        return;
    }

    // in turn, prevent XML listener from responding
    auto guard = _blocker.block();

    auto const unit = _tracker->getActiveUnit();

    auto nt = get_node_tool();
    double val = Quantity::convert(adj->get_value(), unit, "px");
    auto pwb = nt->_selected_nodes->pointwiseBounds();
    auto fsp = nt->_selected_nodes->firstSelectedPoint();

    if (name == "d") {
        // Length has changed, not a coordinate...
        double delta = val / pwb->diameter();

        if (delta > 0) {
            auto center = fsp ? *fsp : pwb->midpoint();
            nt->_multipath->scale(center, {delta, delta});
        }

    } else if (nt && !nt->_selected_nodes->empty()) {
        // Coordinate
        auto d = name == "x" ? Geom::X : Geom::Y;
        double oldval = pwb->midpoint()[d];

        // Adjust the coordinate to the current page, if needed
        auto &pm = _desktop->getDocument()->getPageManager();
        if (_desktop->getDocument()->get_origin_follows_page()) {
            auto page = pm.getSelectedPageRect();
            oldval -= page.corner(0)[d];
        }

        Geom::Point delta;
        delta[d] = val - oldval;
        nt->_multipath->move(delta);
    }
}

void NodeToolbar::sel_changed(Selection *selection)
{
    if (auto lpeitem = cast<SPLPEItem>(selection->singleItem())) {
        _nodes_lpeedit_btn.set_sensitive(lpeitem->hasPathEffect());
    } else {
        _nodes_lpeedit_btn.set_sensitive(false);
    }
}

void NodeToolbar::sel_modified(Selection *selection, guint /*flags*/)
{
    sel_changed(selection);
}

// is called when the node selection is modified
void NodeToolbar::coord_changed(ControlPointSelection *selected_nodes)
{
    // quit if run by the attr_changed listener
    if (_blocker.pending()) {
        return;
    }

    // in turn, prevent listener from responding
    auto guard = _blocker.block();

    if (!_tracker) {
        return;
    }
    auto const unit = _tracker->getActiveUnit();

    if (!selected_nodes || selected_nodes->empty()) {
        // no path selected
        _nodes_x_item.set_sensitive(false);
        _nodes_y_item.set_sensitive(false);
    } else {
        _nodes_x_item.set_sensitive(true);
        _nodes_y_item.set_sensitive(true);
        auto adj_x = _nodes_x_item.get_adjustment();
        auto adj_y = _nodes_y_item.get_adjustment();
        Geom::Coord oldx = Quantity::convert(adj_x->get_value(), unit, "px");
        Geom::Coord oldy = Quantity::convert(adj_y->get_value(), unit, "px");
        Geom::Point mid = selected_nodes->pointwiseBounds()->midpoint();

        // Adjust shown coordinate according to the selected page
        if (_desktop->getDocument()->get_origin_follows_page()) {
            auto &pm = _desktop->getDocument()->getPageManager();
            mid *= pm.getSelectedPageAffine().inverse();
        }

        if (oldx != mid.x()) {
            adj_x->set_value(Quantity::convert(mid.x(), "px", unit));
        }
        if (oldy != mid.y()) {
            adj_y->set_value(Quantity::convert(mid.y(), "px", unit));
        }
    }

    if (selected_nodes->size() == 2) {
        // Length is only visible when exactly two nodes are selected
        _nodes_d_box.set_visible(true);
        auto adj_l = _nodes_d_item.get_adjustment();
        Geom::Coord oldl = Quantity::convert(adj_l->get_value(), unit, "px");

        Geom::Coord length = selected_nodes->pointwiseBounds()->diameter();
        if (oldl != length) {
            adj_l->set_value(Quantity::convert(length, "px", unit));
        }
    } else {
        _nodes_d_box.set_visible(false);
    }
}

void NodeToolbar::edit_add()
{
    if (auto nt = get_node_tool()) {
        nt->_multipath->insertNodes();
    }
}

/* add a node at the left-most point on selected path(s)*/
void NodeToolbar::edit_add_leftmost()
{
    if (auto nt = get_node_tool()) {
        nt->_multipath->insertNodesAtExtrema(PointManipulator::EXTR_MIN_X);
    }
}

/* add a node at the right-most point on selected path(s)*/
void NodeToolbar::edit_add_rightmost()
{
    if (auto nt = get_node_tool()) {
        nt->_multipath->insertNodesAtExtrema(PointManipulator::EXTR_MAX_X);
    }
}

/* add a node at the top-most point on selected path(s)*/
void NodeToolbar::edit_add_topmost()
{
    const auto extrema = _desktop->is_yaxisdown()
                         ? PointManipulator::EXTR_MIN_Y
                         : PointManipulator::EXTR_MAX_Y;
    if (auto nt = get_node_tool()) {
        nt->_multipath->insertNodesAtExtrema(extrema);
    }
}

/* add a node at the bottom-most point on selected path(s)*/
void NodeToolbar::edit_add_bottommost()
{
    const auto extrema = _desktop->is_yaxisdown()
                         ? PointManipulator::EXTR_MAX_Y
                         : PointManipulator::EXTR_MIN_Y;
    if (auto nt = get_node_tool()) {
        nt->_multipath->insertNodesAtExtrema(extrema);
    }
}

void NodeToolbar::edit_delete()
{
    if (auto nt = get_node_tool()) {
        auto prefs = Preferences::get();
        nt->_multipath->deleteNodes((NodeDeleteMode)prefs->getInt("/tools/node/delete-mode-default", (int)NodeDeleteMode::automatic));
    }
}

void NodeToolbar::edit_join()
{
    if (auto nt = get_node_tool()) {
        nt->_multipath->joinNodes();
    }
}

void NodeToolbar::edit_break()
{
    if (auto nt = get_node_tool()) {
        nt->_multipath->breakNodes();
    }
}

void NodeToolbar::edit_delete_segment()
{
    if (auto nt = get_node_tool()) {
        nt->_multipath->deleteSegments();
    }
}

void NodeToolbar::edit_join_segment()
{

    if (auto nt = get_node_tool()) {
        nt->_multipath->joinSegments();
    }
}

void NodeToolbar::edit_cusp()
{
    if (auto nt = get_node_tool()) {
        nt->_multipath->setNodeType(NODE_CUSP);
    }
}

void NodeToolbar::edit_smooth()
{
    if (auto nt = get_node_tool()) {
        nt->_multipath->setNodeType(NODE_SMOOTH);
    }
}

void NodeToolbar::edit_symmetrical()
{   
    if (auto nt = get_node_tool()) {
        nt->_multipath->setNodeType(NODE_SYMMETRIC);
    }
}

void NodeToolbar::edit_auto()
{
    if (auto nt = get_node_tool()) {
        nt->_multipath->setNodeType(NODE_AUTO);
    }
}

void NodeToolbar::edit_toline()
{
    if (auto nt = get_node_tool()) {
        nt->_multipath->setSegmentType(SEGMENT_STRAIGHT);
    }
}

void NodeToolbar::edit_tocurve()
{
    if (auto nt = get_node_tool()) {
        nt->_multipath->setSegmentType(SEGMENT_CUBIC_BEZIER);
    }
}

void NodeToolbar::on_pref_toggled(Gtk::ToggleButton *item, Glib::ustring const &path)
{
    Preferences::get()->setBool(path, item->get_active());
}

NodeTool *NodeToolbar::get_node_tool() const
{
    return dynamic_cast<NodeTool *>(_desktop->getTool());
}

} // namespace Inkscape::UI::Toolbar

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

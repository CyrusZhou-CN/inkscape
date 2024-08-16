// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * @file
 * Node aux toolbar
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

#ifndef SEEN_NODE_TOOLBAR_H
#define SEEN_NODE_TOOLBAR_H

#include <memory>
#include <2geom/coord.h>

#include "toolbar.h"

namespace Gtk {
class Builder;
class Button;
class ToggleButton;
} // namespace Gtk

class SPDesktop;

namespace Inkscape {

class Selection;

namespace UI {

class SimplePrefPusher;
class ControlPointSelection;

namespace Tools {
class ToolBase;
} // namespace Tools

namespace Widget {
class SpinButton;
class UnitTracker;
} // namespace Widget

namespace Toolbar {

class NodeToolbar final : public Toolbar
{
public:
    NodeToolbar(SPDesktop *desktop);
    ~NodeToolbar() override;

private:
    Glib::RefPtr<Gtk::Builder> _builder;
    std::unique_ptr<UI::Widget::UnitTracker> _tracker;

    std::unique_ptr<UI::SimplePrefPusher> _pusher_show_transform_handles;
    std::unique_ptr<UI::SimplePrefPusher> _pusher_show_handles;
    std::unique_ptr<UI::SimplePrefPusher> _pusher_show_outline;
    std::unique_ptr<UI::SimplePrefPusher> _pusher_edit_clipping_paths;
    std::unique_ptr<UI::SimplePrefPusher> _pusher_edit_masks;

    Gtk::Button &_nodes_lpeedit_btn;

    Gtk::ToggleButton *_show_helper_path_btn;
    Gtk::ToggleButton *_show_handles_btn;
    Gtk::ToggleButton *_show_transform_handles_btn;
    Gtk::ToggleButton *_object_edit_mask_path_btn;
    Gtk::ToggleButton *_object_edit_clip_path_btn;

    UI::Widget::SpinButton &_nodes_x_item;
    UI::Widget::SpinButton &_nodes_y_item;
    UI::Widget::SpinButton &_nodes_d_item;
    Gtk::Box &_nodes_d_box;

    bool _freeze;

    sigc::connection c_selection_changed;
    sigc::connection c_selection_modified;
    sigc::connection c_subselection_changed;

    void value_changed(Glib::ustring const &name, Glib::RefPtr<Gtk::Adjustment> &adj);
    void sel_changed(Inkscape::Selection *selection);
    void sel_modified(Inkscape::Selection *selection, guint /*flags*/);
    void coord_changed(Inkscape::UI::ControlPointSelection* selected_nodes);
    void watch_ec(SPDesktop* desktop, Inkscape::UI::Tools::ToolBase* tool);
    void edit_add();
    void edit_add_min_x();
    void edit_add_max_x();
    void edit_add_min_y();
    void edit_add_max_y();
    void edit_delete();
    void edit_join();
    void edit_break();
    void edit_join_segment();
    void edit_delete_segment();
    void edit_cusp();
    void edit_smooth();
    void edit_symmetrical();
    void edit_auto();
    void edit_toline();
    void edit_tocurve();
    void on_pref_toggled(Gtk::ToggleButton *item, const Glib::ustring &path);

    void setup_derived_spin_button(Inkscape::UI::Widget::SpinButton &btn, Glib::ustring const &name);
    void setup_insert_node_menu();
};

} // namespace Toolbar
} // namespace UI
} // namespace Inkscape

#endif /* !SEEN_SELECT_TOOLBAR_H */

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

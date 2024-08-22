// SPDX-License-Identifier: GPL-2.0-or-later

/** @file
 * @brief A widget with multiple panes. Agnostic to type what kind of widgets panes contain.
 *
 * Authors: see git history
 *   Tavmjong Bah
 *
 * Copyright (c) 2020 Tavmjong Bah, Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef INKSCAPE_UI_DIALOG_MULTIPANED_H
#define INKSCAPE_UI_DIALOG_MULTIPANED_H

#include <glibmm/refptr.h>
#include <gtk/gtk.h> // GtkEventControllerMotion
#include <gtkmm/enums.h>
#include <gtkmm/eventbox.h>
#include <gtkmm/gesture.h> // Gtk::EventSequenceState
#include <gtkmm/orientable.h>
#include <gtkmm/widget.h>

namespace Cairo {
class Context;
}

namespace Gdk {
class DragContext;
}

namespace Gtk {
class GestureDrag;
class GestureMultiPress;
}

namespace Inkscape {
namespace UI {
namespace Dialog {

/** A widget with multiple panes */

class MyDropZone;
class MyHandle;
class DialogMultipaned;

/* ============ DROPZONE  ============ */

/**
 * Dropzones are eventboxes at the ends of a DialogMultipaned where you can drop dialogs.
 */
class MyDropZone
    : public Gtk::Orientable
    , public Gtk::EventBox
{
public:
    MyDropZone(Gtk::Orientation orientation);
    ~MyDropZone() override;

    static void add_highlight_instances();
    static void remove_highlight_instances();

private:
    void set_size(int size);
    bool _active = false;
    void add_highlight();
    void remove_highlight();

    static std::list<MyDropZone *> _instances_list;
};

/* ============  HANDLE   ============ */

/**
 * Handles are event boxes that help with resizing DialogMultipaned' children.
 */
class MyHandle
    : public Gtk::Orientable
    , public Gtk::EventBox
{
public:
    MyHandle(Gtk::Orientation orientation, int size);
    ~MyHandle() override = default;

    void set_dragging    (bool dragging);
    void set_drag_updated(bool updated );

private:
    Gtk::EventSequenceState on_motion_enter (GtkEventControllerMotion const *motion,
                                             double x, double y);
    Gtk::EventSequenceState on_motion_motion(GtkEventControllerMotion const *motion,
                                             double x, double y);
    Gtk::EventSequenceState on_motion_leave (GtkEventControllerMotion const *motion);

    Gtk::EventSequenceState on_click_pressed (Gtk::GestureMultiPress const &gesture,
                                              int n_press, double x, double y);
    Gtk::EventSequenceState on_click_released(Gtk::GestureMultiPress &gesture,
                                              int n_press, double x, double y);

    void toggle_multipaned();
    void update_click_indicator(double x, double y);
    void show_click_indicator(bool show);
    bool on_draw(const Cairo::RefPtr<Cairo::Context>& cr) override;
    Cairo::Rectangle get_active_click_zone();

    int _cross_size;
    Gtk::Widget *_child;
    void resize_handler(Gtk::Allocation &allocation);
    bool is_click_resize_active() const;
    bool _click = false;
    bool _click_indicator = false;

    bool _dragging = false;
    bool _drag_updated = false;
};

/* ============ MULTIPANE ============ */

/*
 * A widget with multiple panes. Agnostic to type what kind of widgets panes contain.
 * Handles allow a user to resize children widgets. Drop zones allow adding widgets
 * at either end.
 */
class DialogMultipaned
    : public Gtk::Orientable
    , public Gtk::Container
{
public:
    DialogMultipaned(Gtk::Orientation orientation = Gtk::ORIENTATION_HORIZONTAL);
    ~DialogMultipaned() override;

    void prepend(Gtk::Widget *new_widget);
    void append(Gtk::Widget *new_widget);

    // Getters and setters
    Gtk::Widget *get_first_widget();
    Gtk::Widget *get_last_widget();
    std::vector<Gtk::Widget *> get_children() { return children; }
    void set_target_entries(const std::vector<Gtk::TargetEntry> &target_entries);
    bool has_empty_widget() { return (bool)_empty_widget; }

    // Signals
    sigc::signal<void (const Glib::RefPtr<Gdk::DragContext>)> signal_prepend_drag_data();
    sigc::signal<void (const Glib::RefPtr<Gdk::DragContext>)> signal_append_drag_data();
    sigc::signal<void ()> signal_now_empty();

    // UI functions
    void set_dropzone_sizes(int start, int end);
    void toggle_multipaned_children(bool show);
    void children_toggled();
    void ensure_multipaned_children();
    void set_restored_width(int width);

protected:
    // Overrides
    Gtk::SizeRequestMode get_request_mode_vfunc() const override;
    void get_preferred_width_vfunc(int &minimum_width, int &natural_width) const override;
    void get_preferred_height_vfunc(int &minimum_height, int &natural_height) const override;
    void get_preferred_width_for_height_vfunc(int height, int &minimum_width, int &natural_width) const override;
    void get_preferred_height_for_width_vfunc(int width, int &minimum_height, int &natural_height) const override;
    void on_size_allocate(Gtk::Allocation &allocation) override;

    // Allow us to keep track of our widgets ourselves.
    void forall_vfunc(gboolean include_internals, GtkCallback callback, gpointer callback_data) override;

    void on_add(Gtk::Widget *child) override;
    void on_remove(Gtk::Widget *child) override;

    // Signals
    sigc::signal<void (const Glib::RefPtr<Gdk::DragContext>)> _signal_prepend_drag_data;
    sigc::signal<void (const Glib::RefPtr<Gdk::DragContext>)> _signal_append_drag_data;
    sigc::signal<void ()> _signal_now_empty;

private:
    // We must manage children ourselves.
    std::vector<Gtk::Widget *> children;

    // Values used when dragging handle.
    int _handle = -1; // Child number of active handle
    int _drag_handle = -1;
    Gtk::Widget* _resizing_widget1 = nullptr;
    Gtk::Widget* _resizing_widget2 = nullptr;
    Gtk::Widget* _hide_widget1 = nullptr;
    Gtk::Widget* _hide_widget2 = nullptr;
    Gtk::Allocation start_allocation1;
    Gtk::Allocation start_allocationh;
    Gtk::Allocation start_allocation2;
    Gtk::Allocation allocation1;
    Gtk::Allocation allocationh;
    Gtk::Allocation allocation2;

    // drag on handle/separator
    Gtk::EventSequenceState on_drag_begin (Gtk::GestureDrag const &gesture, double  start_x, double  start_y);
    Gtk::EventSequenceState on_drag_end   (Gtk::GestureDrag const &gesture, double offset_x, double offset_y);
    Gtk::EventSequenceState on_drag_update(Gtk::GestureDrag const &gesture, double offset_x, double offset_y);
    // drag+drop data
    void on_drag_data(const Glib::RefPtr<Gdk::DragContext> context, int x, int y,
                      const Gtk::SelectionData &selection_data, guint info, guint time);
    void on_prepend_drag_data(const Glib::RefPtr<Gdk::DragContext> context, int x, int y,
                              const Gtk::SelectionData &selection_data, guint info, guint time);
    void on_append_drag_data(const Glib::RefPtr<Gdk::DragContext> context, int x, int y,
                             const Gtk::SelectionData &selection_data, guint info, guint time);

    // Others
    Gtk::Widget *_empty_widget; // placeholder in an empty container
    void add_empty_widget();
    void remove_empty_widget();
    std::vector<sigc::connection> _connections;
    int _natural_width = 0;
};

} // namespace Dialog
} // namespace UI
} // namespace Inkscape

#endif // INKSCAPE_UI_DIALOG_MULTIPANED_H

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

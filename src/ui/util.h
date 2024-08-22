// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Utility functions for UI
 *
 * Authors:
 *   Tavmjong Bah
 *   John Smith
 *
 * Copyright (C) 2013, 2018 Authors
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef UI_UTIL_SEEN
#define UI_UTIL_SEEN

#include <cstddef> // size_t
#include <exception>
#include <type_traits>

#include <2geom/affine.h>
#include <2geom/point.h>
#include <2geom/rect.h>
#include <gdkmm/rgba.h>
#include <gtkmm/cellrenderer.h>
#include <gtkmm/container.h>
#include <gtkmm/enums.h>
#include <gtkmm/stylecontext.h>
#include <gtkmm/widget.h>

/*
 * Use these errors when building from glade files for graceful
 * fallbacks and prevent crashes from corrupt ui files.
 */
class UIBuilderError : public std::exception {};
class UIFileUnavailable : public UIBuilderError {};
class WidgetUnavailable : public UIBuilderError {};

namespace Cairo {
class Matrix;
class ImageSurface;
}

namespace Glib {
class ustring;
}

namespace Gtk {
class Revealer;
class Container;
class Widget;
}

Glib::ustring ink_ellipsize_text (Glib::ustring const &src, size_t maxlen);
void reveal_widget(Gtk::Widget *widget, bool show);

// check if widget in a container is actually visible
bool is_widget_effectively_visible(Gtk::Widget const *widget);

namespace Inkscape::UI {

void set_icon_sizes(Gtk::Widget* parent, int pixel_size);
void set_icon_sizes(GtkWidget* parent, int pixel_size);

/// Utility function to ensure correct sizing after adding child widgets.
void resize_widget_children(Gtk::Widget *widget);

void gui_warning(const std::string &msg, Gtk::Window * parent_window = nullptr);

/// Translate cell renderer state to style flags.
Gtk::StateFlags cell_flags_to_state_flags(Gtk::CellRendererState state);

/// Whether for_each_*() will continue or stop after calling Func per child.
enum class ForEachResult {_continue, _break};

/// Call Func with a reference to each child of parent, until it returns _break.
/// Accessing children changes between GTK3 & GTK4, so best consolidate it here.
/// See also src/widgets/spw-utilities: sp_traverse_widget_tree().
template <typename Func>
void for_each_child(Gtk::Container &parent, Func &&func)
{
    static_assert(std::is_invocable_r_v<ForEachResult, Func, Gtk::Widget &>);
    for (auto const child: parent.get_children()) {
        auto const result = func(*child);
        if (result == ForEachResult::_break) {
            return;
        }
    }
}

/// Call Func with a reference to successive parents, until Func returns _break.
template <typename Func>
void for_each_parent(Gtk::Widget &widget, Func &&func)
{
    static_assert(std::is_invocable_r_v<ForEachResult, Func, Gtk::Widget &>);
    auto parent = widget.get_parent();
    for (; parent != nullptr; parent = parent->get_parent()) {
        auto const result = func(*parent);
        if (result == ForEachResult::_break) {
            return;
        }
    }
}

} // namespace Inkscape::UI

// Mix two RGBA colors using simple linear interpolation:
//  0 -> only a, 1 -> only b, x in 0..1 -> (1 - x)*a + x*b
Gdk::RGBA mix_colors(const Gdk::RGBA& a, const Gdk::RGBA& b, float ratio);

// Create the same color, but with a different opacity (alpha)
Gdk::RGBA change_alpha(const Gdk::RGBA& color, double new_alpha);

// Calculate luminance of an RGBA color from its RGB, in range 0 to 1 inclusive.
double get_luminance(const Gdk::RGBA &color);

// Get the foreground / CSS color for a StyleContext based on its current state.
Gdk::RGBA get_foreground_color(Glib::RefPtr<Gtk::StyleContext const> const &context);

// Get CSS color for a StyleContext, based on its current state & a given class.
// N.B.!! Big GTK devs donʼt think changing classes should work ‘within a frame’
// …but it does… & GTK3 GtkCalendar does that – so keep doing it, till we canʼt!
Gdk::RGBA get_color_with_class(Glib::RefPtr<Gtk::StyleContext> const &context,
                               Glib::ustring const &css_class);

guint32 to_guint32(Gdk::RGBA const &rgba);
Gdk::RGBA to_rgba(guint32 const u32);

Geom::IntRect cairo_to_geom(const Cairo::RectangleInt &rect);
Cairo::RectangleInt geom_to_cairo(const Geom::IntRect &rect);
Cairo::Matrix geom_to_cairo(const Geom::Affine &affine);
Geom::IntPoint dimensions(const Cairo::RefPtr<Cairo::ImageSurface> &surface);
Geom::IntPoint dimensions(const Gdk::Rectangle &allocation);

// create a gradient with multiple steps to approximate profile described by given cubic spline
Cairo::RefPtr<Cairo::LinearGradient> create_cubic_gradient(
    Geom::Rect rect,
    const Gdk::RGBA& from,
    const Gdk::RGBA& to,
    Geom::Point ctrl1,
    Geom::Point ctrl2,
    Geom::Point p0 = Geom::Point(0, 0),
    Geom::Point p1 = Geom::Point(1, 1),
    int steps = 8
);

void set_dark_tittlebar(Glib::RefPtr<Gdk::Window> win, bool is_dark);
// convert Gdk::RGBA into 32-bit rrggbbaa color, optionally replacing alpha, if specified
uint32_t conv_gdk_color_to_rgba(const Gdk::RGBA& color, double replace_alpha = -1);

#endif

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

// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Ruler widget. Indicates horizontal or vertical position of a cursor in a specified widget.
 *
 * Copyright (C) 2019, 2023 Tavmjong Bah
 *               2022 Martin Owens
 *
 * Rewrite of the 'C' ruler code which came originally from Gimp.
 *
 * The contents of this file may be used under the GNU General Public License Version 2 or later.
 *
 */

#include <cmath>
#include <utility>
#include <sigc++/functors/mem_fun.h>
#include <cairomm/context.h>
#include <glibmm/ustring.h>
#include <gtkmm/popover.h>
#include <gtkmm/stylecontext.h>

#include "ink-ruler.h"
#include "inkscape.h"
#include "ui/controller.h"
#include "ui/themes.h"
#include "ui/util.h"
#include "util/units.h"

using Inkscape::Util::unit_table;

struct SPRulerMetric
{
  gdouble ruler_scale[16];
  gint    subdivide[5];
};

// Ruler metric for general use.
static SPRulerMetric const ruler_metric_general = {
  { 1, 2, 5, 10, 25, 50, 100, 250, 500, 1000, 2500, 5000, 10000, 25000, 50000, 100000 },
  { 1, 5, 10, 50, 100 }
};

// Ruler metric for inch scales.
static SPRulerMetric const ruler_metric_inches = {
  { 1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768 },
  { 1, 2, 4, 8, 16 }
};

// Half width of pointer triangle.
static double half_width = 5.0;

namespace Inkscape::UI::Widget {

Ruler::Ruler(Gtk::Orientation orientation)
    : _orientation(orientation)
    , _backing_store(nullptr)
    , _lower(0)
    , _upper(1000)
    , _max_size(1000)
    , _unit(nullptr)
    , _backing_store_valid(false)
    , _rect()
    , _position(0)
{
    set_name("InkRuler");
    get_style_context()->add_class(_orientation == Gtk::ORIENTATION_HORIZONTAL ? "horz" : "vert");

    Controller::add_motion<nullptr, &Ruler::on_motion, nullptr>(*this, *this);
    Controller::add_click(*this, sigc::mem_fun(*this, &Ruler::on_click_pressed), {}, Controller::Button::right);

    set_no_show_all();

    auto prefs = Inkscape::Preferences::get();
    _watch_prefs = prefs->createObserver("/options/ruler/show_bbox", sigc::mem_fun(*this, &Ruler::on_prefs_changed));
    on_prefs_changed();

    set_context_menu();

    INKSCAPE.themecontext->getChangeThemeSignal().connect(sigc::mem_fun(*this, &Ruler::on_style_updated));
}

void Ruler::on_prefs_changed()
{
    auto prefs = Inkscape::Preferences::get();
    _sel_visible = prefs->getBool("/options/ruler/show_bbox", true);

    _backing_store_valid = false;
    queue_draw();
}

// Set display unit for ruler.
void
Ruler::set_unit(Inkscape::Util::Unit const *unit)
{
    if (_unit != unit) {
        _unit = unit;

        _backing_store_valid = false;
        queue_draw();
    }
}

// Set range for ruler, update ticks.
void
Ruler::set_range(double lower, double upper)
{
    if (_lower != lower || _upper != upper) {

        _lower = lower;
        _upper = upper;
        _max_size = _upper - _lower;
        if (_max_size == 0) {
            _max_size = 1;
        }

        _backing_store_valid = false;
        queue_draw();
    }
}

/**
 * Set the location of the currently selected page.
 */
void Ruler::set_page(double lower, double upper)
{
    if (_page_lower != lower || _page_upper != upper) {
        _page_lower = lower;
        _page_upper = upper;

        _backing_store_valid = false;
        queue_draw();
    }
}

/**
 * Set the location of the currently selected page.
 */
void Ruler::set_selection(double lower, double upper)
{
    if (_sel_lower != lower || _sel_upper != upper) {
        _sel_lower = lower;
        _sel_upper = upper;

        _backing_store_valid = false;
        queue_draw();
    }
}

// Add a widget (i.e. canvas) to monitor. Note, we don't worry about removing this signal as
// our ruler is tied tightly to the canvas, if one is destroyed, so is the other.
void
Ruler::add_track_widget(Gtk::Widget& widget)
{
    Controller::add_motion<nullptr, &Ruler::on_motion, nullptr>(widget, *this,
        Gtk::PHASE_TARGET, Controller::When::before); // We connected 1st to event, so continue
}

// Draws marker in response to motion events from canvas.  Position is defined in ruler pixel
// coordinates. The routine assumes that the ruler is the same width (height) as the canvas. If
// not, one could use Gtk::Widget::translate_coordinates() to convert the coordinates.
bool
Ruler::on_motion(GtkEventControllerMotion const * const motion, double const x, double const y)
{
    double position = 0;
    if (_orientation == Gtk::ORIENTATION_HORIZONTAL) {
        position = x;
    } else {
        position = y;
    }

    if (position != _position) {
        _position = position;

        // Find region to repaint (old and new marker positions).
        Cairo::RectangleInt new_rect = marker_rect();
        Cairo::RefPtr<Cairo::Region> region = Cairo::Region::create(new_rect);
        region->do_union(_rect);

        // Queue repaint
        queue_draw_region(region);

        _rect = new_rect;
    }

    return false;
}

Gtk::EventSequenceState
Ruler::on_click_pressed(Gtk::GestureMultiPress const &click,
                        int const n_press, double const x, double const y)
{
    _popover->set_pointing_to(Gdk::Rectangle(x, y, 1, 1));
    _popover->popup();
    return Gtk::EVENT_SEQUENCE_CLAIMED;
}

// Find smallest dimension of ruler based on font size.
void
Ruler::size_request (Gtk::Requisition& requisition) const
{
    // Get border size
    Glib::RefPtr<Gtk::StyleContext> style_context = get_style_context();
    Gtk::Border border = style_context->get_border(get_state_flags());

    // get ruler's size from CSS style
    GValue minimum_height = G_VALUE_INIT;
    gtk_style_context_get_property(style_context->gobj(), "min-height", GTK_STATE_FLAG_NORMAL, &minimum_height);
    auto size = g_value_get_int(&minimum_height);
    g_value_unset(&minimum_height);

    int width = border.get_left() + border.get_right();
    int height = border.get_top() + border.get_bottom();

    if (_orientation == Gtk::ORIENTATION_HORIZONTAL) {
        width += 1;
        height += size;
    } else {
        width += size;
        height += 1;
    }

    // Only valid for orientation in question (smallest dimension)!
    requisition.width = width;
    requisition.height = height;
}

void
Ruler::get_preferred_width_vfunc (int& minimum_width,  int& natural_width) const
{
    Gtk::Requisition requisition;
    size_request(requisition);
    minimum_width = natural_width = requisition.width;
} 	

void
Ruler::get_preferred_height_vfunc (int& minimum_height, int& natural_height) const
{
    Gtk::Requisition requisition;
    size_request(requisition);
    minimum_height = natural_height = requisition.height;
}

// Update backing store when scale changes.
// Note: in principle, there should not be a border (ruler ends should match canvas ends). If there
// is a border, we calculate tick position ignoring border width at ends of ruler but move the
// ticks and position marker inside the border.
bool
Ruler::draw_scale(const::Cairo::RefPtr<::Cairo::Context>& cr_in)
{
    Gtk::Allocation allocation = get_allocation();
    int awidth  = allocation.get_width();
    int aheight = allocation.get_height();

    // Create backing store (need surface_in to get scale factor correct).
    Cairo::RefPtr<Cairo::Surface> surface_in = cr_in->get_target();
    _backing_store = Cairo::Surface::create(surface_in, Cairo::CONTENT_COLOR_ALPHA, awidth, aheight);

    // Get context
    Cairo::RefPtr<::Cairo::Context> cr = ::Cairo::Context::create(_backing_store);

    // background
    auto context = get_style_context();
    context->render_background(cr, 0, 0, awidth, aheight);

    // Color in page indication box
    if (double psize = std::abs(_page_upper - _page_lower)) {
        Gdk::Cairo::set_source_rgba(cr, _page_fill);
        cr->begin_new_path();
        if (_orientation == Gtk::ORIENTATION_HORIZONTAL) {
            cr->rectangle(_page_lower, 0, psize, aheight);
        } else {
            cr->rectangle(0, _page_lower, awidth, psize);
        }
        cr->fill();
    } else {
        g_warning("No size?");
    }
    cr->set_line_width(1.0);

    // Ruler size (only smallest dimension used later).
    int rwidth  = awidth  - (_border.get_left() + _border.get_right());
    int rheight = aheight - (_border.get_top()  + _border.get_bottom());

    auto paint_line = [=](Gdk::RGBA color, int offset) {
        if (_orientation == Gtk::ORIENTATION_HORIZONTAL) {
            cr->move_to(0, offset - 0.5);
            cr->line_to(allocation.get_width(), offset - 0.5);
        } else {
            cr->move_to(offset - 0.5, 0);
            cr->line_to(offset - 0.5, allocation.get_height());
        }
        Gdk::Cairo::set_source_rgba(cr, color);
        cr->stroke();
    };

    if (_orientation != Gtk::ORIENTATION_HORIZONTAL) {
        // From here on, awidth is the longest dimension of the ruler, rheight is the shortest.
        std::swap(awidth, aheight);
        std::swap(rwidth, rheight);
    }
    // Draw bottom/right line of ruler
    paint_line(_foreground, aheight);

    // Draw a shadow which overlaps any previously painted object.
    auto paint_shadow = [=](double size_x, double size_y, double width, double height) {
        auto trans = change_alpha(_shadow, 0.0);
        auto gr = create_cubic_gradient(Geom::Rect(0, 0, size_x, size_y), _shadow, trans, Geom::Point(0, 0.5), Geom::Point(0.5, 1));
        cr->rectangle(0, 0, width, height);
        cr->set_source(gr);
        cr->fill();
    };
    int gradient_size = 4;
    if (_orientation == Gtk::ORIENTATION_HORIZONTAL) {
        paint_shadow(0, gradient_size, allocation.get_width(), gradient_size);
    } else {
        paint_shadow(gradient_size, 0, gradient_size, allocation.get_height());
    }

    // Figure out scale. Largest ticks must be far enough apart to fit largest text in vertical ruler.
    // We actually require twice the distance.
    unsigned int scale = std::ceil (_max_size); // Largest number
    Glib::ustring scale_text = std::to_string(scale);
    unsigned int digits = scale_text.length() + 1; // Add one for negative sign.
    unsigned int minimum = digits * _font_size * 2;

    double pixels_per_unit = awidth/_max_size; // pixel per distance

    SPRulerMetric ruler_metric = ruler_metric_general;
    if (_unit == Inkscape::Util::unit_table.getUnit("in")) {
        ruler_metric = ruler_metric_inches;
    }

    unsigned scale_index;
    for (scale_index = 0; scale_index < G_N_ELEMENTS (ruler_metric.ruler_scale)-1; ++scale_index) {
        if (ruler_metric.ruler_scale[scale_index] * std::abs (pixels_per_unit) > minimum) break;
    }

    // Now we find out what is the subdivide index for the closest ticks we can draw
    unsigned divide_index;
    for (divide_index = 0; divide_index < G_N_ELEMENTS (ruler_metric.subdivide)-1; ++divide_index) {
        if (ruler_metric.ruler_scale[scale_index] * std::abs (pixels_per_unit) < 5 * ruler_metric.subdivide[divide_index+1]) break;
    }

    // We'll loop over all ticks.
    double pixels_per_tick = pixels_per_unit *
        ruler_metric.ruler_scale[scale_index] / ruler_metric.subdivide[divide_index];

    double units_per_tick = pixels_per_tick/pixels_per_unit;
    double ticks_per_unit = 1.0/units_per_tick;

    // Find first and last ticks
    int start = 0;
    int end = 0;
    if (_lower < _upper) {
        start = std::floor (_lower * ticks_per_unit);
        end   = std::ceil  (_upper * ticks_per_unit);
    } else {
        start = std::floor (_upper * ticks_per_unit);
        end   = std::ceil  (_lower * ticks_per_unit);
    }

    // Loop over all ticks
    Gdk::Cairo::set_source_rgba(cr, _foreground);
    for (int i = start; i < end+1; ++i) {

        // Position of tick (add 0.5 to center tick on pixel).
        double position = std::floor(i*pixels_per_tick - _lower*pixels_per_unit) + 0.5;

        // Height of tick
        int height = rheight - 7;
        for (int j = divide_index; j > 0; --j) {
            if (i%ruler_metric.subdivide[j] == 0) break;
            height = height/2 + 1;
        }

        // Draw text for major ticks.
        if (i%ruler_metric.subdivide[divide_index] == 0) {
            cr->save();

            int label_value = std::round(i * units_per_tick);

            auto &label = _label_cache[label_value];
            if (!label) {
                label = draw_label(surface_in, label_value);
            }

            // Align text to pixel
            int x = _border.get_left() + 3;
            int y = position + 2.5;
            if (_orientation == Gtk::ORIENTATION_HORIZONTAL) {
                x = position + 2.5;
                y = _border.get_top() + 3;
            }

            // We don't know the surface height/width, damn you cairo.
            cr->rectangle(x, y, 100, 100);
            cr->clip();
            cr->set_source(label, x, y);
            cr->paint();
            cr->restore();
        }

        // Draw ticks
        Gdk::Cairo::set_source_rgba(cr, _foreground);
        if (_orientation == Gtk::ORIENTATION_HORIZONTAL) {
            cr->move_to(position, rheight + _border.get_top() - height);
            cr->line_to(position, rheight + _border.get_top());
        } else {
            cr->move_to(rheight + _border.get_left() - height, position);
            cr->line_to(rheight + _border.get_left(),          position);
        }
        cr->stroke();
    }

    // Draw a selection bar
    if (_sel_lower != _sel_upper && _sel_visible) {

        const auto radius = 3.0;
        const auto delta = _sel_upper - _sel_lower;
        const auto dxy = delta > 0 ? radius : -radius;
        double sy0 = _sel_lower;
        double sy1 = _sel_upper;
        double sx0 = floor(aheight * 0.7);
        double sx1 = sx0;

        if (_orientation == Gtk::ORIENTATION_HORIZONTAL) {
            std::swap(sy0, sx0);
            std::swap(sy1, sx1);
        }

        cr->set_line_width(2.0);

        if (fabs(delta) > 2 * radius) {
            Gdk::Cairo::set_source_rgba(cr, _select_stroke);
            if (_orientation == Gtk::ORIENTATION_HORIZONTAL) {
                cr->move_to(sx0 + dxy, sy0);
                cr->line_to(sx1 - dxy, sy1);
            }
            else {
                cr->move_to(sx0, sy0 + dxy);
                cr->line_to(sx1, sy1 - dxy);
            }
            cr->stroke();
        }

        // Markers
        Gdk::Cairo::set_source_rgba(cr, _select_fill);
        cr->begin_new_path();
        cr->arc(sx0, sy0, radius, 0, 2 * M_PI);
        cr->arc(sx1, sy1, radius, 0, 2 * M_PI);
        cr->fill();

        Gdk::Cairo::set_source_rgba(cr, _select_stroke);
        cr->begin_new_path();
        cr->arc(sx0, sy0, radius, 0, 2 * M_PI);
        cr->stroke();
        cr->begin_new_path();
        cr->arc(sx1, sy1, radius, 0, 2 * M_PI);
        cr->stroke();
    }

    _backing_store_valid = true;
    return true;
}

/**
 * Generate the label as it's only small surface for caching.
 */
Cairo::RefPtr<Cairo::Surface> Ruler::draw_label(Cairo::RefPtr<Cairo::Surface> const &surface_in, int label_value)
{
    bool rotate = _orientation != Gtk::ORIENTATION_HORIZONTAL;

    Glib::RefPtr<Pango::Layout> layout = create_pango_layout(std::to_string(label_value));
    layout->set_font_description(_font);

    int text_width;
    int text_height;
    layout->get_pixel_size(text_width, text_height);
    if (rotate) {
        std::swap(text_width, text_height);
    }

    auto surface = Cairo::Surface::create(surface_in, Cairo::CONTENT_COLOR_ALPHA, text_width, text_height);
    Cairo::RefPtr<::Cairo::Context> cr = ::Cairo::Context::create(surface);

    cr->save();
    Gdk::Cairo::set_source_rgba(cr, _foreground);
    if (rotate) {
        cr->translate(text_width / 2, text_height / 2);
        cr->rotate(-M_PI_2);
        cr->translate(-text_height / 2, -text_width / 2);
    }
    layout->show_in_cairo_context(cr);
    cr->restore();

    return surface;
}

// Draw position marker, we use doubles here.
void
Ruler::draw_marker(const Cairo::RefPtr<::Cairo::Context>& cr)
{
    Gtk::Allocation allocation = get_allocation();
    const int awidth  = allocation.get_width();
    const int aheight = allocation.get_height();

    Gdk::Cairo::set_source_rgba(cr, _foreground);
    if (_orientation == Gtk::ORIENTATION_HORIZONTAL) {
        double offset = aheight - _border.get_bottom();
        cr->move_to(_position,              offset);
        cr->line_to(_position - half_width, offset - half_width);
        cr->line_to(_position + half_width, offset - half_width);
        cr->close_path();
     } else {
        double offset = awidth - _border.get_right();
        cr->move_to(offset,              _position);
        cr->line_to(offset - half_width, _position - half_width);
        cr->line_to(offset - half_width, _position + half_width);
        cr->close_path();
    }
    cr->fill();
}

// This is a pixel aligned integer rectangle that encloses the position marker. Used to define the
// redraw area.
Cairo::RectangleInt
Ruler::marker_rect()
{
    Gtk::Allocation allocation = get_allocation();
    const int awidth  = allocation.get_width();
    const int aheight = allocation.get_height();

    int rwidth  = awidth  - _border.get_left() - _border.get_right();
    int rheight = aheight - _border.get_top()  - _border.get_bottom();

    Cairo::RectangleInt rect;
    rect.x = 0;
    rect.y = 0;
    rect.width = 0;
    rect.height = 0;

    // Find size of rectangle to enclose triangle.
    if (_orientation == Gtk::ORIENTATION_HORIZONTAL) {
        rect.x = std::floor(_position - half_width);
        rect.y = std::floor(_border.get_top() + rheight - half_width);
        rect.width  = std::ceil(half_width * 2.0 + 1);
        rect.height = std::ceil(half_width);
    } else {
        rect.x = std::floor(_border.get_left() + rwidth - half_width);
        rect.y = std::floor(_position - half_width);
        rect.width  = std::ceil(half_width);
        rect.height = std::ceil(half_width * 2.0 + 1);
    }

    return rect;
}

// Draw the ruler using the tick backing store.
bool
Ruler::on_draw(const::Cairo::RefPtr<::Cairo::Context>& cr) {
    if (!_backing_store_valid) {
        draw_scale (cr);
    }

    cr->set_source (_backing_store, 0, 0);
    cr->paint();

    draw_marker (cr);

    return true;
}

// Update ruler on style change (font-size, etc.)
void
Ruler::on_style_updated() {
    Gtk::DrawingArea::on_style_updated();

    Glib::RefPtr<Gtk::StyleContext> style_context = get_style_context();

    // Cache all our colors to speed up rendering.
    _border = style_context->get_border();
    _foreground = get_foreground_color(style_context);
    _font = style_context->get_font();
    _font_size = _font.get_size();
    if (!_font.get_size_is_absolute())
        _font_size /= Pango::SCALE;

    _shadow = get_color_with_class(style_context, "shadow");
    _page_fill = get_color_with_class(style_context, "page");

    style_context->add_class("selection");
    _select_fill = get_color_with_class(style_context, "background");
    _select_stroke = get_color_with_class(style_context, "border");
    style_context->remove_class("selection");

    _label_cache.clear();
    _backing_store_valid = false;

    queue_resize();
    queue_draw();
}

/**
 * Return a contextmenu for the ruler
 */
void Ruler::set_context_menu()
{
    auto unit_menu = Gio::Menu::create();

    for (auto &pair : unit_table.units(Inkscape::Util::UNIT_TYPE_LINEAR)) {
        auto unit = pair.second.abbr;
        Glib::ustring action_name = "doc.set-display-unit('" + unit + "')";
        auto item = Gio::MenuItem::create(unit, action_name);
        unit_menu->append_item(item);
    }

    _popover = Gtk::make_managed<Gtk::Popover>(*this, unit_menu);
    _popover->set_modal(true); // set_autohide in Gtk4
}

} // namespace Inkscape::UI::Widget

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

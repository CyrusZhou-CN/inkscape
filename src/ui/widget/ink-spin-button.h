// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef INK_SPIN_BUTTON_H
#define INK_SPIN_BUTTON_H

#include <gtkmm.h>
#include <sigc++/scoped_connection.h>

namespace Inkscape::UI::Widget {

class InkSpinButton : public Gtk::Widget {
public:
    InkSpinButton();
    InkSpinButton(BaseObjectType* cobject, const Glib::RefPtr<Gtk::Builder>& builder);
    explicit InkSpinButton(BaseObjectType* cobject);

    ~InkSpinButton() override;

    // Set "adjustment" to establish limits and step
    void set_adjustment(const Glib::RefPtr<Gtk::Adjustment>& adjustment);
    Glib::RefPtr<Gtk::Adjustment>& get_adjustment();
    // Number of decimal digits to use for formatting values
    void set_digits(int digits);
    int get_digits() const;
    // Set range of allowed input values (as an alternative to specifying 'adjustment')
    void set_range(double min, double max);
    // Set the step increment of the spin button
    void set_step(double step_increment);
    // Set new value; it will be rescaled if scaling is set
    void set_value(double new_value);
    // Get current value; it will be rescaled if scaling is set
    double get_value() const;
    // Specify optional suffix to show after the value
    void set_suffix(const std::string& suffix, bool add_half_space = true);
    // Specify optional prefix to show in front of the value
    void set_prefix(const std::string& prefix, bool add_space = true);
    // Set to true to draw a border, false to hide it
    void set_has_frame(bool frame = true);
    // Set to true to hide insignificant zeros after decimal point
    void set_trim_zeros(bool trim);
    // Set scaling factor to multiply all values before presenting them; by default it is 1.0
    // Example: with a factor of 100 user can edit and see percentages, while read and set values are 0..1 fractions
    void set_scaling_factor(double factor);
    // Which widget to focus if defocusing this spin button;
    // if not set explicitly, next available focusable widget will be used
    void set_defocus_widget(Gtk::Widget* widget) { _defocus_widget = widget; }
    // Suppress expression evaluator
    void set_dont_evaluate(bool flag) { _dont_evaluate = flag; }
    // Set distance in pixels of drag travel to adjust full button range; the lower the value the more sensitive the dragging gets
    void set_drag_sensitivity(double distance);
    // Specify label to show inside spin button
    void set_label(const std::string& label);
    // Signal fired when numerical value changes
    sigc::signal<void (double)> signal_value_changed() const;
    // Base spin button's min size on the pattern provided; ex: "99.99"
    void set_min_size(const std::string& pattern);
    // Set callback function that parses text and returns "double" value; it may throw std::exception on failure
    void set_evaluator_function(std::function<double (const Glib::ustring&)> cb);
    // Pass true to enable decrement/increment arrow buttons (on by default)
    void set_has_arrows(bool enable = true);
    // Pass true to make Enter key exit editing mode
    void set_enter_exit_edit(bool enable = true);
    // ----------- PROPERTIES ------------
    // Glib::PropertyProxy<int> property_digits() { return prop_digits.get_proxy(); }

private:
    void construct();
    void update(bool fire_change_notification = true);
    void set_new_value(double new_value);
    Gtk::SizeRequestMode get_request_mode_vfunc() const override;
    void measure_vfunc(Gtk::Orientation orientation, int for_size, int& minimum, int& natural, int& minimum_baseline, int& natural_baseline) const override;
    void size_allocate_vfunc(int width, int height, int baseline) override;

    Glib::RefPtr<Gtk::Adjustment> _adjustment = Gtk::Adjustment::create(0, 0, 100, 1);
    Gtk::Button _minus;
    Gtk::Label _value;
    Gtk::Button _plus;
    Gtk::Entry _entry;
    Gtk::Label _label;

    // ------------- CONTROLLERS -------------

    Glib::RefPtr<Gtk::EventControllerMotion> _motion;
    void on_motion_enter(double x, double y);
    void on_motion_leave();

    Glib::RefPtr<Gtk::EventControllerMotion> _motion_value;
    void on_motion_enter_value(double x, double y);
    void on_motion_leave_value();

    Glib::RefPtr<Gtk::GestureDrag> _drag_value;
    void on_drag_begin_value(Gdk::EventSequence* sequence);
    void on_drag_update_value(Gdk::EventSequence* sequence);
    void on_drag_end_value(Gdk::EventSequence* sequence);

    Glib::RefPtr<Gtk::EventControllerScroll> _scroll;
    void on_scroll_begin(); // Not used with mouse wheel.
    bool on_scroll(double dx, double dy);
    void on_scroll_end(); // Not used with mouse wheel.

    // Use Gesture to get access to modifier keys (rather than "clicked" signal).
    Glib::RefPtr<Gtk::GestureClick> _click_plus;
    void on_pressed_plus(int n_press, double x, double y);

    Glib::RefPtr<Gtk::GestureClick> _click_minus;
    void on_pressed_minus(int n_press, double x, double y);

    Glib::RefPtr<Gtk::EventControllerFocus> _focus;

    Glib::RefPtr<Gtk::EventControllerKey> _key_entry;
    bool on_key_pressed(guint keyval, guint keycode, Gdk::ModifierType state); // "pressed" vs GTK3 "press"
    // void on_key_released(guint keyval, guint keycode, Gdk::ModifierType state);

    void on_activate(); // "activate" fires on pressing Enter in an entry widget

    void on_changed();
    void on_editing_done();
    void enter_edit();
    void exit_edit();
    void cancel_editing();
    bool defocus();
    void show_arrows(bool on = true);
    void show_label(bool on = true);
    bool commit_entry();
    void change_value(double inc, Gdk::ModifierType state);
    std::string format(double value, bool with_prefix_suffix, bool with_markup, bool trim_zeros, bool limit_size) const;
    void start_spinning(double steps, Gdk::ModifierType state, Glib::RefPtr<Gtk::GestureClick>& gesture);
    void stop_spinning();

    // ---------------- DATA ------------------
    double _initial_value = 0.0; // Value of adjustment at start of drag.
    double _drag_full_travel = 300.0; // dragging sensitivity in pixels
    bool _dragged = false;      // Hack to avoid enabling entry after drag. TODO Probably not needed now.
    double _scroll_counter = 0; // Counter to control incrementing/decrementing rate
    bool _trim_zeros = true;    // hide insignificant zeros in decimal fraction
    double _fmt_scaling_factor = 1.0; // multiplier for value formatting
    sigc::connection _connection;
    int _button_width = 0;     // width of increment/decrement button
    int _entry_height = 0;      // natural height of Gtk::Entry
    int _baseline = 0;
    int _label_width = 0;
    bool _enable_arrows = true;
    sigc::scoped_connection _spinning;
    Gtk::Widget* _defocus_widget = nullptr;
    bool _dont_evaluate = false; // turn off expression evaluator?
    bool _enter_exit_edit = false;
    Glib::RefPtr<Gdk::Cursor> _old_cursor;
    Glib::RefPtr<Gdk::Cursor> _current_cursor;
    struct Point { double x = 0; double y = 0; } _drag_start;
    sigc::signal<void (double)> _signal_value_changed;
    std::string _min_size_pattern;
    std::function<double (const Glib::ustring&)> _evaluator; // evaluator callback

    // ----------- PROPERTIES ------------
    Glib::Property<Glib::RefPtr<Gtk::Adjustment>> _adjust;
    Glib::Property<int> _digits;
    Glib::Property<double> _num_value;
    Glib::Property<double> _min_value;
    Glib::Property<double> _max_value;
    Glib::Property<double> _step_value;
    Glib::Property<double> _scaling_factor;
    Glib::Property<bool> _has_frame;
    Glib::Property<bool> _show_arrows;
    Glib::Property<bool> _enter_exit;
    Glib::Property<Glib::ustring> _label_text;
    Glib::Property<Glib::ustring> _prefix;
    Glib::Property<Glib::ustring> _suffix;

public:
    Glib::PropertyProxy<int> property_digits() { return _digits.get_proxy(); }
    Glib::PropertyProxy<double> property_scaling_factor() { return _scaling_factor.get_proxy(); }
    Glib::PropertyProxy<double> property_min_value() { return _min_value.get_proxy(); }
    Glib::PropertyProxy<double> property_max_value() { return _max_value.get_proxy(); }
    Glib::PropertyProxy<double> property_step_value() { return _step_value.get_proxy(); }
    Glib::PropertyProxy<double> property_value() { return _num_value.get_proxy(); }
    Glib::PropertyProxy<Glib::ustring> property_label() { return _label_text.get_proxy(); }
    Glib::PropertyProxy<Glib::ustring> property_prefix() { return _prefix.get_proxy(); }
    Glib::PropertyProxy<Glib::ustring> property_suffix() { return _suffix.get_proxy(); }
    Glib::PropertyProxy<bool> property_has_frame() { return _has_frame.get_proxy(); }
    Glib::PropertyProxy<bool> property_show_arrows() { return _show_arrows.get_proxy(); }
    Glib::PropertyProxy<bool> property_enter_exit() { return _enter_exit.get_proxy(); }

    // Construct a C++ object from a parent (=base) C class object

    static Glib::ObjectBase* wrap_new(GObject* o) {
        auto obj = new InkSpinButton(GTK_WIDGET(o));

        //TODO: check if this is needed
        // // if (gtk_widget_is_toplevel(GTK_WIDGET(o)))
        //     return obj;
        // else
            return Gtk::manage(obj);
    }

    // Register a "new" type in Glib and bind it to the C++ wrapper function
    static void register_type() {
        if (gtype) return;

        InkSpinButton dummy;
        gtype = G_OBJECT_TYPE(dummy.gobj());

        Glib::wrap_register(gtype, InkSpinButton::wrap_new);
    }
private:
    static GType gtype;

};

} // namespace Inkscape::UI::Widget

#endif // INK_SPIN_BUTTON_H

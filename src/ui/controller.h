// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * Utilities to more easily use Gtk::EventController & subclasses like Gesture.
 */
/*
 * Authors:
 *   Daniel Boles <dboles.src+inkscape@gmail.com>
 *
 * Copyright (C) 2023 Daniel Boles
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef SEEN_UI_CONTROLLER_H
#define SEEN_UI_CONTROLLER_H

#include <cstddef>
#include <functional>
#include <type_traits>
#include <utility>
#include <sigc++/slot.h>
#include <glibmm/refptr.h>
#include <gdkmm/types.h>
#include <gtk/gtk.h>
#include <gtkmm/eventcontroller.h>
#include <gtkmm/gesture.h> // Gtk::EventSequenceState
#include <gtkmm/widget.h>

#include "ui/manage.h"
#include "util/callback-converter.h"

namespace Gtk {
class GestureDrag;
class GestureMultiPress;
class GestureSingle;
} // namespace Gtk

/// Utilities to more easily use Gtk::EventController & subclasses like Gesture.
namespace Inkscape::UI::Controller {

/*
* * helpers to query common state of controllers
 */

/// Get default seat for the surface of the widget, & return its modifier state.
Gdk::ModifierType get_device_state(GtkEventController const *controller);

/// Get the current sequence's last event.
GdkEvent const *get_last_event(Gtk::GestureSingle const &gesture);

/// Get the current sequence's last event & return modifier state of that event.
Gdk::ModifierType get_current_event_state(Gtk::GestureSingle const &gesture);

/// Helper to get key group from a const controller (C API is not good wrt const)
unsigned get_group(GtkEventControllerKey const *controller);

/// Helper to query if ModifierType state contains one or more of given flag(s).
// This will be needed in GTK4 as enums are scoped there, so bitwise is tougher.
[[nodiscard]] inline bool has_flag(Gdk::ModifierType const state,
                                   Gdk::ModifierType const flags)
    { return (state & flags) != Gdk::ModifierType{}; }

// migration aid for above, to later replace GdkModifierType w Gdk::ModifierType
[[nodiscard]] inline bool has_flag(GdkModifierType const state,
                                   GdkModifierType const flags)
    { return (state & flags) != GdkModifierType{}; }

/*
* * helpers to more easily add Controllers to Widgets, & let Widgets manage them
 */

/// Whether to connect a slot to a signal before or after the default handler.
enum class When {before, after};

// name is Click b/c A: shorter!, B: GTK4 renames GestureMultiPress→GestureClick

/// Type of slot connected to GestureMultiPress::pressed & ::released signals.
/// The args are the gesture, n_press count, x coord & y coord (in widget space)
using ClickSlot = sigc::slot<Gtk::EventSequenceState(Gtk::GestureMultiPress &, int, double, double)>;

/// helper to stop accidents on int vs gtkmm3's weak=typed enums, & looks nicer!
enum class Button {any = 0, left = 1, middle = 2, right = 3};

/// Create a click gesture for & manage()d by widget; by default claim sequence.
Gtk::GestureMultiPress &add_click(Gtk::Widget &widget,
                                  ClickSlot on_pressed,
                                  ClickSlot on_released = {},
                                  Button button = Button::any,
                                  Gtk::PropagationPhase phase = Gtk::PHASE_BUBBLE,
                                  When when = When::after);

/// Type of slot connected to GestureDrag::drag-(begin|update|end) signals.
/// The arguments are the gesture, x coordinate & y coordinate (in widget space)
using DragSlot = sigc::slot<Gtk::EventSequenceState(Gtk::GestureDrag &, double, double)>;

/// Create a drag gesture for & manage()d by widget.
Gtk::GestureDrag &add_drag(Gtk::Widget &widget,
                           DragSlot on_begin  ,
                           DragSlot on_update ,
                           DragSlot on_end    ,
                           Gtk::PropagationPhase phase = Gtk::PHASE_BUBBLE,
                           When when = When::after);

/// internal stuff
namespace Detail {

/// Move controller to manage()ed by widget & returns a reference to it.
template <typename Controller, typename Widget>
Controller &managed(Glib::RefPtr<Controller> controller, Widget &widget)
{
    auto& ref = *controller.operator->();
    manage(std::move(controller), widget);
    return ref;
}

/// Helper to connect member func of a C++ Listener object to a C GObject signal.
/// If method is not a nullptr, calls make_g_callback<method>, & connects result
/// with either g_signal_connect (when=before) or g_signal_connect_after (after)
template <auto method,
          typename Emitter, typename Listener>
void connect(Emitter * const emitter, char const * const detailed_signal,
             Listener &listener, When const when)
{
    // Special-case nullptr so we neednʼt make a no-op function to connect.
    if constexpr (std::is_same_v<decltype(method), std::nullptr_t>) return;
    else {
        auto const object = G_OBJECT(emitter);
        auto const c_handler = Util::make_g_callback<method>;

        switch (when) {
            case When::before:
                g_signal_connect(object, detailed_signal, c_handler, &listener);
                break;

            case When::after:
                g_signal_connect_after(object, detailed_signal, c_handler, &listener);
                break;

            default:
                g_assert_not_reached();
        }
    }
}

/// Whether Function can be invoked with Args... to return Result; OR it's a nullptr.
// TODO: C++20: Use concepts instead.
template <typename Function, typename Result, typename ...Args>
auto constexpr callable_or_null = std::is_same_v       <Function, std::nullptr_t > ||
                                  std::is_invocable_r_v<Result, Function, Args...>;

} // namespace Detail

/// Whether Function is suitable to handle EventControllerKey::pressed|released.
/// The arguments are the controller, keyval, hardware keycode & modifier state.
template <typename Function, typename Listener>
auto constexpr is_key_handler = Detail::callable_or_null<Function, bool,
    Listener *, GtkEventControllerKey *, unsigned, unsigned, GdkModifierType>;

/// Whether Function is suitable to handle EventControllerKey::modifiers.
/// The arguments are the controller & modifier state.
template <typename Function, typename Listener>
auto constexpr is_mod_handler = Detail::callable_or_null<Function, bool,
    Listener *, GtkEventControllerKey *, GdkModifierType>;

/// Whether Function is suitable to handle EventControllerMotion::enter|motion.
/// The arguments are the controller, x coordinate & y coord (in widget space).
template <typename Function, typename Listener>
auto constexpr is_motion_handler = Detail::callable_or_null<Function, void,
    Listener *, GtkEventControllerMotion *, double, double>;

/// Whether Function is suitable to handle EventControllerMotion::leave.
/// The argument is the controller. Coordinates arenʼt given on leaving.
template <typename Function, typename Listener>
auto constexpr is_leave_handler = Detail::callable_or_null<Function, void,
    Listener *, GtkEventControllerMotion *>;

/// Whether Function is suitable for EventControllerScroll::scroll-(begin|end).
/// The argument is the controller.
template <typename Function, typename Listener>
auto constexpr is_scroll_handler = Detail::callable_or_null<Function, void,
    Listener *, GtkEventControllerScroll *>;

/// Whether Function is suitable for EventControllerScroll::scroll|decelerate.
/// The arguments are controller & for scroll dx,dy; or decelerate: vel_x, vel_y
template <typename Function, typename Listener>
auto constexpr is_scroll_xy_handler = Detail::callable_or_null<Function, void,
    Listener *, GtkEventControllerScroll *, double, double>;

/// Create a key event controller for & manage()d by widget.
// As gtkmm 3 lacks EventControllerKey, this must go via C API, so to make it
// easier I reuse Util::make_g_callback(), which needs methods as template args.
// Once on gtkmm4, we can do as Click etc, & accept anything convertible to slot
template <auto on_pressed, auto on_released = nullptr, auto on_modifiers = nullptr,
          typename Listener>
Gtk::EventController &add_key(Gtk::Widget &widget  ,
                              Listener    &listener,
                              Gtk::PropagationPhase const phase = Gtk::PHASE_BUBBLE,
                              When const when = When::after)
{
    // NB make_g_callback<> must type-erase methods, so we must check arg compat
    // TODO: C++20: Use concepts instead.
    static_assert(is_key_handler<decltype(on_pressed  ), Listener>);
    static_assert(is_key_handler<decltype(on_released ), Listener>);
    static_assert(is_mod_handler<decltype(on_modifiers), Listener>);

    auto const gcontroller = gtk_event_controller_key_new(widget.gobj());
    gtk_event_controller_set_propagation_phase(gcontroller, static_cast<GtkPropagationPhase>(phase));
    Detail::connect<on_pressed  >(gcontroller, "key-pressed" , listener, when);
    Detail::connect<on_released >(gcontroller, "key-released", listener, when);
    Detail::connect<on_modifiers>(gcontroller, "modifiers"   , listener, when);
    return Detail::managed(Glib::wrap(gcontroller), widget);
}

/// Create a motion event controller for & manage()d by widget.
// See comments for add_key().
template <auto on_enter, auto on_motion, auto on_leave,
          typename Listener>
Gtk::EventController &add_motion(Gtk::Widget &widget  ,
                                 Listener    &listener,
                                 Gtk::PropagationPhase const phase = Gtk::PHASE_BUBBLE,
                                 When const when = When::after)
{
    // NB make_g_callback<> must type-erase methods, so we must check arg compat
    static_assert(is_motion_handler<decltype(on_enter ), Listener>);
    static_assert(is_motion_handler<decltype(on_motion), Listener>);
    static_assert( is_leave_handler<decltype(on_leave ), Listener>);

    auto const gcontroller = gtk_event_controller_motion_new(widget.gobj());
    gtk_event_controller_set_propagation_phase(gcontroller, static_cast<GtkPropagationPhase>(phase));
    Detail::connect<on_enter >(gcontroller, "enter" , listener, when);
    Detail::connect<on_motion>(gcontroller, "motion", listener, when);
    Detail::connect<on_leave >(gcontroller, "leave" , listener, when);
    return Detail::managed(Glib::wrap(gcontroller), widget);
}

/// Create a scroll event controller for & manage()d by widget.
// See comments for add_key().
template <auto on_begin, auto on_scroll, auto on_end, auto on_decelerate = nullptr,
          typename Listener>
Gtk::EventController &add_scroll(Gtk::Widget &widget  ,
                                 Listener    &listener,
                                 GtkEventControllerScrollFlags const flags = GTK_EVENT_CONTROLLER_SCROLL_BOTH_AXES,
                                 Gtk::PropagationPhase const phase = Gtk::PHASE_BUBBLE,
                                 When const when = When::after)
{
    // NB make_g_callback<> must type-erase methods, so we must check arg compat
    static_assert(is_scroll_handler   <decltype(on_begin     ), Listener>);
    static_assert(is_scroll_xy_handler<decltype(on_scroll    ), Listener>);
    static_assert(is_scroll_handler   <decltype(on_end       ), Listener>);
    static_assert(is_scroll_xy_handler<decltype(on_decelerate), Listener>);

    auto const gcontroller = gtk_event_controller_scroll_new(widget.gobj(), flags);
    gtk_event_controller_set_propagation_phase(gcontroller, static_cast<GtkPropagationPhase>(phase));
    Detail::connect<on_begin     >(gcontroller, "scroll-begin", listener, when);
    Detail::connect<on_scroll    >(gcontroller, "scroll"      , listener, when);
    Detail::connect<on_end       >(gcontroller, "scroll-end"  , listener, when);
    Detail::connect<on_decelerate>(gcontroller, "decelerate"  , listener, when);
    return Detail::managed(Glib::wrap(gcontroller), widget);
}

} // namespace Inkscape::UI::Controller

#endif // SEEN_UI_CONTROLLER_H

/*
  Local Variables:
  mode:c++
  c-file-style:"stroustrup"
  c-file-offsets:((innamespace .0)(inline-open . 0)(case-label . +))
  indent-tabs-mode:nil
  fill-column:99
  End:
*/
// vim:filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:fileencoding=utf-8:textwidth=99:

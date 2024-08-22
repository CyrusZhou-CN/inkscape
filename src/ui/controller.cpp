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

#include <cassert>
#include <sigc++/adaptors/bind.h>
#include <gdk/gdk.h>
#include <gtkmm/gesturedrag.h>
#include <gtkmm/gesturemultipress.h>

#include "controller.h"

namespace Inkscape::UI::Controller {

template <typename T> static auto asserted(T * const t) { assert(t); return t; }

// TODO: GTK4: We will have gtkmm API for all controllers, so migrate from C API

// TODO: GTK4: We will have EventController.get_current_event_state(). And phew!
Gdk::ModifierType get_device_state(GtkEventController const * const controller)
{
    auto const widget = asserted(
        gtk_event_controller_get_widget(const_cast<GtkEventController *>(controller)));
    auto const surface = asserted(gtk_widget_get_window       (widget ));
    auto const display = asserted(gdk_window_get_display      (surface));
    auto const seat    = asserted(gdk_display_get_default_seat(display));
    auto const device  = asserted(gdk_seat_get_pointer        (seat   ));
    GdkModifierType state{};
    gdk_window_get_device_position(surface, device, NULL, NULL, &state);
    return static_cast<Gdk::ModifierType>(state);
}

GdkEvent const *get_last_event(Gtk::GestureSingle const &gesture)
{
    auto const sequence = gesture.get_current_sequence();
    return gesture.get_last_event(const_cast<GdkEventSequence*>(sequence));
}

// TODO: GTK4: We can replace w/ just EventController.get_current_event_state().
Gdk::ModifierType get_current_event_state(Gtk::GestureSingle const &gesture)
{
    auto state = GdkModifierType{};
    gdk_event_get_state(get_last_event(gesture), &state);
    return static_cast<Gdk::ModifierType>(state);
}

unsigned get_group(GtkEventControllerKey const * const controller)
{
    return gtk_event_controller_key_get_group(const_cast<GtkEventControllerKey *>(controller));
}

namespace {

/// Helper to create EventController or subclass, for & manage()d by the widget.
template <typename Controller>
Controller &create(Gtk::Widget &widget, Gtk::PropagationPhase const phase)
{
    static_assert(std::is_base_of_v<Gtk::EventController, Controller>);
    auto &controller = Detail::managed(Controller::create(widget), widget);
    controller.set_propagation_phase(phase);
    return controller;
}

/// Helper to invoke getter on object, & connect a slot to the resulting signal.
template <typename Object, typename Getter, typename Slot>
void connect(Object &object, Getter const getter, Slot slot, When const when)
{
    auto signal = std::invoke(getter, object);
    signal.connect(sigc::bind<0>(std::move(slot), std::ref(object)),
                   when == When::after);
}

/// Create Controller for & manage()d by widget, & connect to one signal on it.
template <typename Controller,
          typename Getter, typename Slot>
Controller &add(Gtk::Widget &widget,
                Getter const getter, Slot slot,
                Gtk::PropagationPhase const phase,
                When const when)
{
    auto &controller = create<Controller>(widget, phase);
    connect(controller, getter, std::move(slot), when);
    return controller;
}

/// Create Controller for & manage()d by widget, & connect to two signals on it.
template <typename Controller,
          typename Getter1, typename Slot1,
          typename Getter2, typename Slot2>
Controller &add(Gtk::Widget &widget,
                Getter1 const getter1, Slot1 slot1,
                Getter2 const getter2, Slot2 slot2,
                Gtk::PropagationPhase const phase,
                When const when)
{
    auto &controller = create<Controller>(widget, phase);
    connect(controller, getter1, std::move(slot1), when);
    connect(controller, getter2, std::move(slot2), when);
    return controller;
}

/// Create Controller for & manage()d by widget, & connect to 3 signals on it.
template <typename Controller,
          typename Getter1, typename Slot1,
          typename Getter2, typename Slot2,
          typename Getter3, typename Slot3>
Controller &add(Gtk::Widget &widget,
                Getter1 const getter1, Slot1 slot1,
                Getter2 const getter2, Slot2 slot2,
                Getter3 const getter3, Slot3 slot3,
                Gtk::PropagationPhase const phase,
                When const when)
{
    auto &controller = create<Controller>(widget, phase);
    connect(controller, getter1, std::move(slot1), when);
    connect(controller, getter2, std::move(slot2), when);
    connect(controller, getter3, std::move(slot3), when);
    return controller;
}

// We add the requirement that slots return an EventSequenceState, which if itʼs
// not NONE we set on the controller. This makes it easier & less error-prone to
// migrate code that returned a bool whether GdkEvent is handled, to Controllers
// & their way of claiming the sequence if handled – as then we only require end
// users to change their returned type/value – rather than need them to manually
// call controller.set_state(), which is easy to forget & unlike a return cannot
// be enforced by the compiler. So… this wraps a callerʼs slot that returns that
// state & uses it, with a void-returning wrapper as thatʼs what GTK/mm expects.
template <typename Slot>
auto use_state(Slot &&slot)
{
    return [slot = std::move(slot)](auto &controller, auto &&...args)
    {
        if (!slot) return;
        Gtk::EventSequenceState const state = slot(
            controller, std::forward<decltype(args)>(args)...);
        if (state != Gtk::EVENT_SEQUENCE_NONE) {
            controller.set_state(state);
        }
    };
}

} // unnamed namespace

Gtk::GestureMultiPress &add_click(Gtk::Widget &widget,
                                  ClickSlot on_pressed,
                                  ClickSlot on_released,
                                  Button const button,
                                  Gtk::PropagationPhase const phase,
                                  When const when)
{
    auto &click = add<Gtk::GestureMultiPress>(widget,
        &Gtk::GestureMultiPress::signal_pressed , use_state(std::move(on_pressed )),
        &Gtk::GestureMultiPress::signal_released, use_state(std::move(on_released)),
        phase, when);
    click.set_button(static_cast<int>(button));
    return click;
}

Gtk::GestureDrag &add_drag(Gtk::Widget &widget,
                           DragSlot on_begin  ,
                           DragSlot on_update ,
                           DragSlot on_end    ,
                           Gtk::PropagationPhase const phase,
                           When const when)
{
    return add<Gtk::GestureDrag>(widget,
        &Gtk::GestureDrag::signal_drag_begin , use_state(std::move(on_begin )),
        &Gtk::GestureDrag::signal_drag_update, use_state(std::move(on_update)),
        &Gtk::GestureDrag::signal_drag_end   , use_state(std::move(on_end   )),
        phase, when);
}

} // namespace Inkscape::UI::Controller

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

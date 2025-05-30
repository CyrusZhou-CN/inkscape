// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Authors:
 *   Ted Gould <ted@gould.cx>
 *
 * Copyright (C) 2007 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef INKSCAPE_EXTENSION_EXECUTION_ENV_H__
#define INKSCAPE_EXTENSION_EXECUTION_ENV_H__

#include <glibmm/main.h>
#include <glibmm/ustring.h>
#include <gtkmm/dialog.h>
#include <memory>

#include "selection.h"

class SPDesktop;
class SPDocument;

namespace Inkscape {
namespace Extension {

class Effect;

namespace  Implementation
{
class ImplementationDocumentCache;
}

class ExecutionEnv {
private:
    enum state_t {
        INIT,     //< The context has been initialized
        COMPLETE, //< We've completed atleast once
        RUNNING   //< The effect is currently running
    };
    /** \brief  What state the execution engine is in. */
    state_t _state;

    /** \brief If there is a working dialog it'll be referenced
               right here. */
    Gtk::Dialog * _visibleDialog = nullptr;
    /** \brief Signal that the run is complete. */
    sigc::signal<void ()> _runComplete;
    /** \brief  In some cases we need a mainLoop, when we do, this is
                a pointer to it. */
    Glib::RefPtr<Glib::MainLoop> _mainloop;
    /** \brief  The desktop containing the document that we're working on. */
    SPDesktop * _desktop = nullptr;
    /** \brief  A document cache if we were passed one. */
    Implementation::ImplementationDocumentCache * _docCache;

    /** \brief  Saved selection state before running the effect. */
    std::unique_ptr<Inkscape::SelectionState> _selectionState;

    /** \brief  The effect that we're executing in this context. */
    Effect * _effect;

    /** \brief  Show the working dialog when the effect is executing. */
    bool _show_working;
public:

    /** \brief  Create a new context for execution of an effect
        \param effect  The effect to execute
        \param desktop   The desktop containing the document to execute the effect on
        \param docCache  The implementation cache of the document.  May be
                         NULL in which case it'll be created by the execution
                         environment.
        \prarm show_working  Show a small dialog signaling the effect
                             is working.  Allows for user canceling.
        \param show_errors   If the effect has an error, show it or not.
    */
    ExecutionEnv (Effect * effect,
                  SPDesktop * desktop,
                  Implementation::ImplementationDocumentCache * docCache = nullptr,
                  bool show_working = true,
                  bool show_errors = true);
    virtual ~ExecutionEnv ();

    /** \brief Starts the execution of the effect
        \return Returns whether the effect was executed to completion */
    void run ();
    /** \brief Cancel the execution of the effect */
    void cancel ();
    /** \brief Commit the changes to the document */
    void commit ();
    /** \brief Undoes what the effect completed. */
    void undo ();
    /** \brief Wait for the effect to complete if it hasn't. */
    bool wait ();
    void reselect ();

    /** \brief Return reference to working dialog (if any) */
    Gtk::Dialog *get_working_dialog () { return _visibleDialog; };

    // Public according to Core Guideline C.131
    SPDocument *document = nullptr;
private:
    void runComplete ();
    void createWorkingDialog ();
    void workingCanceled (const int resp);
    void genDocCache ();
    void killDocCache ();
};

} }  /* namespace Inkscape, Extension */
#endif /* INKSCAPE_EXTENSION_EXECUTION_ENV_H__ */

/*
  Local Variables:
  mode:c++
  c-file-style:"stroustrup"
  c-file-offsets:((innamespace . 0)(inline-open . 0)(case-label . +))
  indent-tabs-mode:nil
  fill-column:99
  End:
*/
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4 :

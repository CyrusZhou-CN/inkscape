// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Authors:
 *   Ted Gould <ted@gould.cx>
 *
 * Copyright (C) 2004-2005 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "extension/implementation/implementation.h"

namespace Inkscape {
namespace Extension {

class Effect;
class Extension;

namespace Internal {

/** \brief  Create a grid.
*/
class Grid : public Inkscape::Extension::Implementation::Implementation {

public:
    bool load(Inkscape::Extension::Extension *module) override;
    void effect(Inkscape::Extension::Effect *module, ExecutionEnv *executionEnv, SPDesktop *desktop,
                Inkscape::Extension::Implementation::ImplementationDocumentCache *docCache) override;
    Gtk::Widget *prefs_effect(Inkscape::Extension::Effect *module, SPDesktop *desktop,
                              sigc::signal<void()> *changeSignal,
                              Inkscape::Extension::Implementation::ImplementationDocumentCache *docCache) override;

    static void init ();
};

}; /* namespace Internal */
}; /* namespace Extension */
}; /* namespace Inkscape */

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

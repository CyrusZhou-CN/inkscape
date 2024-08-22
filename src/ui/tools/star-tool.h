// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef __SP_STAR_CONTEXT_H__
#define __SP_STAR_CONTEXT_H__

/*
 * Star drawing context
 *
 * Authors:
 *   Mitsuru Oka <oka326@parkcity.ne.jp>
 *   Lauris Kaplinski <lauris@kaplinski.com>
 *
 * Copyright (C) 1999-2002 Lauris Kaplinski
 * Copyright (C) 2001-2002 Mitsuru Oka
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include <cstddef>
#include <sigc++/sigc++.h>
#include <2geom/point.h>
#include "ui/tools/tool-base.h"
#include "object/weakptr.h"

class SPStar;

namespace Inkscape {

class Selection;

namespace UI {
namespace Tools {

class StarTool : public ToolBase
{
public:
    StarTool(SPDesktop *desktop);
    ~StarTool() override;

    void set(Preferences::Entry const &val) override;
    bool root_handler(CanvasEvent const &event) override;

private:
    SPWeakPtr<SPStar> star;

    Geom::Point center;

    /* Number of corners */
    int magnitude;

    /* Outer/inner radius ratio */
    double proportion;

    /* flat sides or not? */
    bool isflatsided;

    /* rounded corners ratio */
    double rounded;

    // randomization
    double randomized;

    sigc::connection sel_changed_connection;

    void drag(Geom::Point p, unsigned state);
	void finishItem();
	void cancel();
    void selection_changed(Selection *selection);
};

}
}
}

#endif

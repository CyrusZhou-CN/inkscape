// SPDX-License-Identifier: GPL-2.0-or-later
/**
    \file grid.cpp

    A plug-in to add a grid creation effect into Inkscape.
*/
/*
 * Copyright (C) 2004-2005  Ted Gould <ted@gould.cx>
 * Copyright (C) 2007  MenTaLguY <mental@rydia.net>
 *   Abhishek Sharma
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include <gtkmm/box.h>
#include <gtkmm/adjustment.h>
#include <gtkmm/spinbutton.h>

#include "desktop.h"
#include "layer-manager.h"

#include "document.h"
#include "selection.h"
#include <2geom/geom.h>

#include "object/sp-object.h"

#include "svg/path-string.h"

#include "extension/effect.h"
#include "extension/system.h"

#include "util/units.h"

#include "grid.h"

namespace Inkscape {
namespace Extension {
namespace Internal {

/**
    \brief  A function to allocated anything -- just an example here
    \param  module  Unused
    \return Whether the load was successful
*/
bool
Grid::load (Inkscape::Extension::Extension */*module*/)
{
    // std::cerr << "Hey, I'm Grid, I'm loading!" << std::endl;
    return TRUE;
}

namespace {

Glib::ustring build_lines(Geom::Rect bounding_area,
                          Geom::Point const &offset, Geom::Point const &spacing)
{

    std::cerr << "Building lines" << std::endl;

    Geom::Point point_offset(0.0, 0.0);

    SVG::PathString path_data;

    for ( int axis = Geom::X ; axis <= Geom::Y ; ++axis ) {
        point_offset[axis] = offset[axis];

        for (Geom::Point start_point = bounding_area.min();
             start_point[axis] + offset[axis] <= (bounding_area.max())[axis];
             start_point[axis] += spacing[axis]) {
            Geom::Point end_point = start_point;
            end_point[1-axis] = (bounding_area.max())[1-axis];

            path_data.moveTo(start_point + point_offset)
                .lineTo(end_point + point_offset);
        }
    }
    std::cerr << "Path data:" << path_data.c_str() << std::endl;
    return path_data;
}

} // namespace

/**
    \brief  This actually draws the grid.
    \param  module   The effect that was called (unused)
    \param  document What should be edited.
*/
void
Grid::effect (Inkscape::Extension::Effect *module, SPDesktop *desktop, Inkscape::Extension::Implementation::ImplementationDocumentCache * /*docCache*/)
{

    std::cerr << "Executing effect" << std::endl;

    Inkscape::Selection *selection = desktop->getSelection();

    Geom::Rect bounding_area = Geom::Rect(Geom::Point(0,0), Geom::Point(100,100));
    if (selection->isEmpty()) {
        /* get page size */
        SPDocument * doc = desktop->doc();
        bounding_area = *(doc->preferredBounds());
    } else {
        Geom::OptRect bounds = selection->visualBounds();
        if (bounds) {
            bounding_area = *bounds;
        }

        gdouble doc_height  =  (desktop->doc())->getHeight().value("px");
        Geom::Rect temprec = Geom::Rect(Geom::Point(bounding_area.min()[Geom::X], doc_height - bounding_area.min()[Geom::Y]),
                                    Geom::Point(bounding_area.max()[Geom::X], doc_height - bounding_area.max()[Geom::Y]));

        bounding_area = temprec;
    }

    double scale = desktop->doc()->getDocumentScale().inverse()[Geom::X];

    bounding_area *= Geom::Scale(scale);
    Geom::Point spacings( scale * module->get_param_float("xspacing"),
                          scale * module->get_param_float("yspacing") );
    gdouble line_width = scale * module->get_param_float("lineWidth");
    Geom::Point offsets( scale * module->get_param_float("xoffset"),
                         scale * module->get_param_float("yoffset") );

    Glib::ustring path_data("");

    path_data = build_lines(bounding_area, offsets, spacings);
    Inkscape::XML::Document * xml_doc = desktop->doc()->getReprDoc();

    //XML Tree being used directly here while it shouldn't be.
    Inkscape::XML::Node * current_layer = desktop->layerManager().currentLayer()->getRepr();
    Inkscape::XML::Node * path = xml_doc->createElement("svg:path");

    path->setAttribute("d", path_data);

    std::ostringstream stringstream;
    stringstream << "fill:none;stroke:#000000;stroke-width:" << line_width << "px";
    path->setAttribute("style", stringstream.str());

    current_layer->appendChild(path);
    Inkscape::GC::release(path);
}

/** \brief  A class to make an adjustment that uses Extension params */
class PrefAdjustment : public Gtk::Adjustment {
    /** Extension that this relates to */
    Inkscape::Extension::Extension * _ext;
    /** The string which represents the parameter */
    char * _pref;
public:
    /** \brief  Make the adjustment using an extension and the string
                describing the parameter. */
    PrefAdjustment(Inkscape::Extension::Extension * ext, char * pref) :
            Gtk::Adjustment(0.0, 0.0, 10.0, 0.1), _ext(ext), _pref(pref) {
        this->set_value(_ext->get_param_float(_pref));
        this->signal_value_changed().connect(sigc::mem_fun(*this, &PrefAdjustment::val_changed));
        return;
    };

    void val_changed ();
}; /* class PrefAdjustment */

/** \brief  A function to respond to the value_changed signal from the
            adjustment.

    This function just grabs the value from the adjustment and writes
    it to the parameter.  Very simple, but yet beautiful.
*/
void
PrefAdjustment::val_changed ()
{
    // std::cerr << "Value Changed to: " << this->get_value() << std::endl;
    _ext->set_param_float(_pref, this->get_value());
    return;
}

/** \brief  A function to get the preferences for the grid
    \param  module  Module which holds the params
    \param  desktop

    Uses AutoGUI for creating the GUI.
*/
Gtk::Widget *
Grid::prefs_effect(Inkscape::Extension::Effect *module, SPDesktop *desktop, sigc::signal<void ()> * changeSignal, Inkscape::Extension::Implementation::ImplementationDocumentCache * /*docCache*/)
{
    auto current_document = desktop->doc();
    auto selected = desktop->getSelection()->items();

    Inkscape::XML::Node * first_select = nullptr;
    if (!selected.empty()) {
        first_select = selected.front()->getRepr();
    }

    return module->autogui(current_document, first_select, changeSignal);
}




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

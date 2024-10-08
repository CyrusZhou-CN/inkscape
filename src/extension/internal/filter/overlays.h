// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef SEEN_INKSCAPE_EXTENSION_INTERNAL_FILTER_OVERLAYS_H__
#define SEEN_INKSCAPE_EXTENSION_INTERNAL_FILTER_OVERLAYS_H__
/* Change the 'OVERLAYS' above to be your file name */

/*
 * Copyright (C) 2011 Authors:
 *   Ivan Louette (filters)
 *   Nicolas Dufour (UI) <nicoduf@yahoo.fr>
 *
 * Overlays filters
 *   Noise fill
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */
/* ^^^ Change the copyright to be you and your e-mail address ^^^ */

#include "filter.h"

#include "extension/internal/clear-n_.h"
#include "extension/system.h"
#include "extension/extension.h"

namespace Inkscape {
namespace Extension {
namespace Internal {
namespace Filter {

/**
    \brief    Custom predefined Noise fill filter.
    
    Basic noise fill and transparency texture

    Filter's parameters:
    * Turbulence type (enum, default fractalNoise else turbulence) -> turbulence (type)
    * Horizontal frequency (*1000) (0.01->10000., default 20) -> turbulence (baseFrequency [/1000])
    * Vertical frequency (*1000) (0.01->10000., default 40) -> turbulence (baseFrequency [/1000])
    * Complexity (1->5, default 5) -> turbulence (numOctaves)
    * Variation (1->360, default 1) -> turbulence (seed)
    * Dilatation (1.->50., default 3) -> color (n-1th value)
    * Erosion (0.->50., default 1) -> color (nth value 0->-50)
    * Color (guint, default 148,115,39,255) -> flood (flood-color, flood-opacity)
    * Inverted (boolean, default false)  -> composite1 (operator, true="in", false="out")
*/

class NoiseFill : public Inkscape::Extension::Internal::Filter::Filter {
protected:
    gchar const * get_filter_text (Inkscape::Extension::Extension * ext) override;

public:
    NoiseFill ( ) : Filter() { };
    ~NoiseFill ( ) override { if (_filter != nullptr) g_free((void *)_filter); return; }

    static void init () {
        // clang-format off
        Inkscape::Extension::build_from_mem(
            "<inkscape-extension xmlns=\"" INKSCAPE_EXTENSION_URI "\">\n"
              "<name>" N_("Noise Fill") "</name>\n"
              "<id>org.inkscape.effect.filter.NoiseFill</id>\n"
              "<param name=\"tab\" type=\"notebook\">\n"
                "<page name=\"optionstab\" gui-text=\"" N_("Options") "\">\n"
                  "<param name=\"type\" gui-text=\"" N_("Turbulence type:") "\" type=\"optiongroup\" appearance=\"combo\">\n"
                    "<option value=\"fractalNoise\">" N_("Fractal noise") "</option>\n"
                    "<option value=\"turbulence\">" N_("Turbulence") "</option>\n"
                  "</param>\n"
                  "<param name=\"hfreq\" gui-text=\"" N_("Horizontal frequency:") "\" type=\"float\" appearance=\"full\" precision=\"2\" min=\"0\" max=\"100.00\">20</param>\n"
                  "<param name=\"vfreq\" gui-text=\"" N_("Vertical frequency:") "\" type=\"float\" appearance=\"full\" precision=\"2\" min=\"0\" max=\"100.00\">40</param>\n"
                  "<param name=\"complexity\" gui-text=\"" N_("Complexity:") "\" type=\"int\" appearance=\"full\" min=\"1\" max=\"5\">5</param>\n"
                  "<param name=\"variation\" gui-text=\"" N_("Variation:") "\" type=\"int\" appearance=\"full\" min=\"1\" max=\"360\">0</param>\n"
                  "<param name=\"dilat\" gui-text=\"" N_("Dilatation:") "\" type=\"float\" appearance=\"full\" precision=\"2\" min=\"1\" max=\"50\">3</param>\n"
                  "<param name=\"erosion\" gui-text=\"" N_("Erosion:") "\" type=\"float\" appearance=\"full\" precision=\"2\" min=\"0\" max=\"50\">1</param>\n"
                  "<param name=\"inverted\" gui-text=\"" N_("Inverted") "\" type=\"bool\" >false</param>\n"
                "</page>\n"
                "<page name=\"co11tab\" gui-text=\"" N_("Noise color") "\">\n"
                  "<param name=\"color\" gui-text=\"" N_("Color") "\" type=\"color\">354957823</param>\n"
                "</page>\n"
              "</param>\n"
              "<effect>\n"
                "<object-type>all</object-type>\n"
                "<effects-menu>\n"
                  "<submenu name=\"" N_("Filters") "\">\n"
                    "<submenu name=\"" N_("Overlays") "\"/>\n"
                  "</submenu>\n"
                "</effects-menu>\n"
                "<menu-tip>" N_("Basic noise fill and transparency texture") "</menu-tip>\n"
              "</effect>\n"
            "</inkscape-extension>\n", std::make_unique<NoiseFill>());
        // clang-format on
    };

};

gchar const *
NoiseFill::get_filter_text (Inkscape::Extension::Extension * ext)
{
    if (_filter != nullptr) g_free((void *)_filter);

    std::ostringstream type;
    std::ostringstream hfreq;
    std::ostringstream vfreq;
    std::ostringstream complexity;
    std::ostringstream variation;
    std::ostringstream dilat;
    std::ostringstream erosion;
    std::ostringstream inverted;

    type << ext->get_param_optiongroup("type");
    hfreq << (ext->get_param_float("hfreq"));
    vfreq << (ext->get_param_float("vfreq"));
    complexity << ext->get_param_int("complexity");
    variation << ext->get_param_int("variation");
    dilat << ext->get_param_float("dilat");
    erosion << (- ext->get_param_float("erosion"));
    auto color = ext->get_param_color("color");
    if (ext->get_param_bool("inverted"))
        inverted << "out";
    else
        inverted << "in";

    _filter = g_strdup_printf(
        "<filter xmlns:inkscape=\"http://www.inkscape.org/namespaces/inkscape\" style=\"color-interpolation-filters:sRGB;\" inkscape:label=\"Noise Fill\">\n"
          "<feTurbulence type=\"%s\" baseFrequency=\"%s %s\" numOctaves=\"%s\" seed=\"%s\" result=\"turbulence\"/>\n"
          "<feComposite in=\"SourceGraphic\" in2=\"turbulence\" operator=\"%s\" result=\"composite1\" />\n"
          "<feColorMatrix values=\"1 0 0 0 0 0 1 0 0 0 0 0 1 0 0 0 0 0 %s %s \" result=\"color\" />\n"
          "<feFlood flood-opacity=\"%f\" flood-color=\"%s\" result=\"flood\" />\n"
          "<feMerge result=\"merge\">\n"
            "<feMergeNode in=\"flood\" />\n"
            "<feMergeNode in=\"color\" />\n"
          "</feMerge>\n"
          "<feComposite in2=\"SourceGraphic\" operator=\"in\" result=\"composite2\" />\n"
        "</filter>\n", type.str().c_str(), hfreq.str().c_str(), vfreq.str().c_str(), complexity.str().c_str(), variation.str().c_str(), inverted.str().c_str(), dilat.str().c_str(), erosion.str().c_str(),
        color.getOpacity(), color.toString(false).c_str());

    return _filter;
}; /* NoiseFill filter */

}; /* namespace Filter */
}; /* namespace Internal */
}; /* namespace Extension */
}; /* namespace Inkscape */

/* Change the 'OVERLAYS' below to be your file name */
#endif /* SEEN_INKSCAPE_EXTENSION_INTERNAL_FILTER_OVERLAYS_H__ */

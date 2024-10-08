// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef SEEN_INKSCAPE_EXTENSION_INTERNAL_FILTER_PAINT_H__
#define SEEN_INKSCAPE_EXTENSION_INTERNAL_FILTER_PAINT_H__
/* Change the 'PAINT' above to be your file name */

/*
 * Copyright (C) 2012 Authors:
 *   Ivan Louette (filters)
 *   Nicolas Dufour (UI) <nicoduf@yahoo.fr>
 *
 * Image paint and draw filters
 *   Chromolitho
 *   Cross engraving
 *   Drawing
 *   Electrize
 *   Neon draw
 *   Point engraving
 *   Posterize
 *   Posterize basic
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
    \brief    Custom predefined Chromolitho filter.
    
    Chromo effect with customizable edge drawing and graininess

    Filter's parameters:
    * Drawing (boolean, default checked) -> Checked = blend1 (in="convolve1"), unchecked = blend1 (in="composite1")
    * Transparent (boolean, default unchecked) -> Checked = colormatrix5 (in="colormatrix4"), Unchecked = colormatrix5 (in="component1")
    * Invert (boolean, default false) -> component1 (tableValues) [adds a trailing 0]
    * Dented (boolean, default false) -> component1 (tableValues) [adds intermediate 0s]
    * Lightness (0.->10., default 0.) -> composite1 (k1)
    * Saturation (0.->1., default 1.) -> colormatrix3 (values)
    * Noise reduction (1->1000, default 20) -> convolve (kernelMatrix, central value -1001->-2000, default -1020)
    * Drawing blend (enum, default Normal) -> blend1 (mode)
    * Smoothness (0.01->10, default 1) -> blur1 (stdDeviation)
    * Grain (boolean, default unchecked) -> Checked = blend2 (in="colormatrix2"), Unchecked = blend2 (in="blur1")
        * Grain x frequency (0.->1000, default 1000) -> turbulence1 (baseFrequency, first value)
        * Grain y frequency (0.->1000, default 1000) -> turbulence1 (baseFrequency, second value)
        * Grain complexity (1->5, default 1) -> turbulence1 (numOctaves)
        * Grain variation (0->1000, default 0) -> turbulence1 (seed)
        * Grain expansion (1.->50., default 1.) -> colormatrix1 (n-1 value)
        * Grain erosion (0.->40., default 0.) -> colormatrix1 (nth value) [inverted]
        * Grain color (boolean, default true) -> colormatrix2 (values)
        * Grain blend (enum, default Normal) -> blend2 (mode)
*/
class Chromolitho : public Inkscape::Extension::Internal::Filter::Filter {
protected:
    gchar const * get_filter_text (Inkscape::Extension::Extension * ext) override;

public:
    Chromolitho ( ) : Filter() { };
    ~Chromolitho ( ) override { if (_filter != nullptr) g_free((void *)_filter); return; }

    static void init () {
        // clang-format off
        Inkscape::Extension::build_from_mem(
            "<inkscape-extension xmlns=\"" INKSCAPE_EXTENSION_URI "\">\n"
              "<name>" N_("Chromolitho") "</name>\n"
              "<id>org.inkscape.effect.filter.Chromolitho</id>\n"
              "<param name=\"tab\" type=\"notebook\">\n"
                "<page name=\"optionstab\" gui-text=\"Options\">\n"
                  "<param name=\"drawing\" gui-text=\"" N_("Drawing mode") "\" type=\"bool\" >true</param>\n"
                  "<param name=\"dblend\" gui-text=\"" N_("Drawing blend:") "\" type=\"optiongroup\" appearance=\"combo\">\n"
                    "<option value=\"darken\">Darken</option>\n"
                    "<option value=\"normal\">Normal</option>\n"
                    "<option value=\"multiply\">Multiply</option>\n"
                    "<option value=\"screen\">Screen</option>\n"
                    "<option value=\"lighten\">Lighten</option>\n"
                  "</param>\n"
                  "<param name=\"transparent\" gui-text=\"" N_("Transparent") "\" type=\"bool\" >false</param>\n"
                  "<param name=\"dented\" gui-text=\"" N_("Dented") "\" type=\"bool\" >false</param>\n"
                  "<param name=\"inverted\" gui-text=\"" N_("Inverted") "\" type=\"bool\" >false</param>\n"
                  "<param name=\"light\" gui-text=\"" N_("Lightness") "\" type=\"float\" appearance=\"full\" precision=\"2\" min=\"0\" max=\"10\">0</param>\n"
                  "<param name=\"saturation\" gui-text=\"" N_("Saturation") "\" type=\"float\" precision=\"2\" appearance=\"full\" min=\"0\" max=\"1\">1</param>\n"
                  "<param name=\"noise\" gui-text=\"" N_("Noise reduction") "\" type=\"int\" appearance=\"full\" min=\"1\" max=\"1000\">10</param>\n"
                  "<param name=\"smooth\" gui-text=\"" N_("Smoothness") "\" type=\"float\" appearance=\"full\" precision=\"2\" min=\"0.01\" max=\"10.00\">1</param>\n"
                "</page>\n"
                "<page name=\"graintab\" gui-text=\"" N_("Grain") "\">\n"
                  "<param name=\"grain\" gui-text=\"" N_("Grain mode") "\" type=\"bool\" >true</param>\n"
                  "<param name=\"grainxf\" gui-text=\"" N_("Horizontal frequency") "\" type=\"float\" appearance=\"full\" precision=\"2\" min=\"0\" max=\"1000\">1000</param>\n"
                  "<param name=\"grainyf\" gui-text=\"" N_("Vertical frequency") "\" type=\"float\" appearance=\"full\" precision=\"2\" min=\"0\" max=\"1000\">1000</param>\n"
                  "<param name=\"grainc\" gui-text=\"" N_("Complexity") "\" type=\"int\" appearance=\"full\" min=\"1\" max=\"5\">1</param>\n"
                  "<param name=\"grainv\" gui-text=\"" N_("Variation") "\" type=\"int\" appearance=\"full\" min=\"0\" max=\"1000\">0</param>\n"
                  "<param name=\"grainexp\" gui-text=\"" N_("Expansion") "\" type=\"float\" appearance=\"full\" precision=\"2\" min=\"1\" max=\"50\">1</param>\n"
                  "<param name=\"grainero\" gui-text=\"" N_("Erosion") "\" type=\"float\" appearance=\"full\" precision=\"2\" min=\"0\" max=\"40\">0</param>\n"
                  "<param name=\"graincol\" gui-text=\"" N_("Color") "\" type=\"bool\" >true</param>\n"
                  "<param name=\"gblend\" gui-text=\"" N_("Grain blend:") "\" type=\"optiongroup\" appearance=\"combo\">\n"
                    "<option value=\"normal\">Normal</option>\n"
                    "<option value=\"multiply\">Multiply</option>\n"
                    "<option value=\"screen\">Screen</option>\n"
                    "<option value=\"lighten\">Lighten</option>\n"
                    "<option value=\"darken\">Darken</option>\n"
                  "</param>\n"
                "</page>\n"
              "</param>\n"
              "<effect>\n"
                "<object-type>all</object-type>\n"
                "<effects-menu>\n"
                  "<submenu name=\"" N_("Filters") "\">\n"
                    "<submenu name=\"" N_("Image Paint and Draw") "\"/>\n"
                  "</submenu>\n"
                "</effects-menu>\n"
                "<menu-tip>" N_("Chromo effect with customizable edge drawing and graininess") "</menu-tip>\n"
              "</effect>\n"
            "</inkscape-extension>\n", std::make_unique<Chromolitho>());
        // clang-format on
    };
};

gchar const *
Chromolitho::get_filter_text (Inkscape::Extension::Extension * ext)
{
    if (_filter != nullptr) g_free((void *)_filter);
    
    std::ostringstream b1in;
    std::ostringstream b2in;
    std::ostringstream col3in;
    std::ostringstream transf;
    std::ostringstream light;
    std::ostringstream saturation;
    std::ostringstream noise;
    std::ostringstream dblend;
    std::ostringstream smooth;
    std::ostringstream grainxf;
    std::ostringstream grainyf;
    std::ostringstream grainc;
    std::ostringstream grainv;
    std::ostringstream gblend;
    std::ostringstream grainexp;
    std::ostringstream grainero;
    std::ostringstream graincol;

    if (ext->get_param_bool("drawing"))
        b1in << "convolve1";
    else
        b1in << "composite1";

    if (ext->get_param_bool("transparent"))
        col3in << "colormatrix4";
    else
        col3in << "component1";
    light << ext->get_param_float("light");
    saturation << ext->get_param_float("saturation");
    noise << (-1000 - ext->get_param_int("noise"));
    dblend << ext->get_param_optiongroup("dblend");
    smooth << ext->get_param_float("smooth");

    if (ext->get_param_bool("dented")) {
        transf << "0 1 0 1";
    } else {
        transf << "0 1 1";
    }
    if (ext->get_param_bool("inverted"))
        transf << " 0";

    if (ext->get_param_bool("grain"))
        b2in << "colormatrix2";
    else
        b2in << "blur1";
    grainxf << (ext->get_param_float("grainxf") / 1000);
    grainyf << (ext->get_param_float("grainyf") / 1000);
    grainc << ext->get_param_int("grainc");
    grainv << ext->get_param_int("grainv");
    gblend << ext->get_param_optiongroup("gblend");
    grainexp << ext->get_param_float("grainexp");
    grainero << (-ext->get_param_float("grainero"));
    if (ext->get_param_bool("graincol"))
        graincol << "1";
    else
        graincol << "0";

    // clang-format off
    _filter = g_strdup_printf(
        "<filter xmlns:inkscape=\"http://www.inkscape.org/namespaces/inkscape\" style=\"color-interpolation-filters:sRGB;\" inkscape:label=\"Chromolitho\">\n"
          "<feComposite in=\"SourceGraphic\" in2=\"SourceGraphic\" operator=\"arithmetic\" k1=\"%s\" k2=\"1\" result=\"composite1\" />\n"
          "<feConvolveMatrix in=\"composite1\" kernelMatrix=\"0 250 0 250 %s 250 0 250 0 \" order=\"3 3\" result=\"convolve1\" />\n"
          "<feBlend in=\"%s\" in2=\"composite1\" mode=\"%s\" result=\"blend1\" />\n"
          "<feGaussianBlur in=\"blend1\" stdDeviation=\"%s\" result=\"blur1\" />\n"
          "<feTurbulence baseFrequency=\"%s %s\" numOctaves=\"%s\" seed=\"%s\" type=\"fractalNoise\" result=\"turbulence1\" />\n"
          "<feColorMatrix values=\"1 0 0 0 0 0 1 0 0 0 0 0 1 0 0 0 0 0 %s %s \" result=\"colormatrix1\" />\n"
          "<feColorMatrix type=\"saturate\" values=\"%s\" result=\"colormatrix2\" />\n"
          "<feBlend in=\"%s\" in2=\"blur1\" mode=\"%s\" result=\"blend2\" />\n"
          "<feColorMatrix in=\"blend2\" type=\"saturate\" values=\"%s\" result=\"colormatrix3\" />\n"
          "<feComponentTransfer in=\"colormatrix3\" result=\"component1\">\n"
            "<feFuncR type=\"discrete\" tableValues=\"%s\" />\n"
            "<feFuncG type=\"discrete\" tableValues=\"%s\" />\n"
            "<feFuncB type=\"discrete\" tableValues=\"%s\" />\n"
          "</feComponentTransfer>\n"
          "<feColorMatrix values=\"1 0 0 0 0 0 1 0 0 0 0 0 1 0 0 -0.2125 -0.7154 -0.0721 1 0 \" result=\"colormatrix4\" />\n"
          "<feColorMatrix in=\"%s\" values=\"1 0 0 0 0 0 1 0 0 0 0 0 1 0 0 0 0 0 15 0 \" result=\"colormatrix5\" />\n"
          "<feComposite in2=\"SourceGraphic\" operator=\"in\" result=\"composite2\" />\n"
        "</filter>\n", light.str().c_str(), noise.str().c_str(), b1in.str().c_str(), dblend.str().c_str(), smooth.str().c_str(), grainxf.str().c_str(), grainyf.str().c_str(), grainc.str().c_str(), grainv.str().c_str(), grainexp.str().c_str(), grainero.str().c_str(), graincol.str().c_str(), b2in.str().c_str(), gblend.str().c_str(), saturation.str().c_str(), transf.str().c_str(), transf.str().c_str(), transf.str().c_str(), col3in.str().c_str());
    // clang-format on

    return _filter;
}; /* Chromolitho filter */

/**
    \brief    Custom predefined Cross engraving filter.
    
    Convert image to an engraving made of vertical and horizontal lines

    Filter's parameters:
    * Clean-up (1->500, default 30) -> convolve1 (kernelMatrix, central value -1001->-1500, default -1030)
    * Dilatation (1.->50., default 1) -> color2 (n-1th value)
    * Erosion (0.->50., default 0) -> color2 (nth value 0->-50)
    * Strength (0.->10., default 0.5) -> composite2 (k2)
    * Length (0.5->20, default 4) -> blur1 (stdDeviation x), blur2 (stdDeviation y)
    * Transparent (boolean, default false) -> composite 4 (in, true->composite3, false->blend)
*/
class CrossEngraving : public Inkscape::Extension::Internal::Filter::Filter {
protected:
    gchar const * get_filter_text (Inkscape::Extension::Extension * ext) override;

public:
    CrossEngraving ( ) : Filter() { };
    ~CrossEngraving ( ) override { if (_filter != nullptr) g_free((void *)_filter); return; }

    static void init () {
        // clang-format off
        Inkscape::Extension::build_from_mem(
            "<inkscape-extension xmlns=\"" INKSCAPE_EXTENSION_URI "\">\n"
              "<name>" N_("Cross Engraving") "</name>\n"
              "<id>org.inkscape.effect.filter.CrossEngraving</id>\n"
              "<param name=\"clean\" gui-text=\"" N_("Clean-up") "\" type=\"int\" appearance=\"full\" min=\"1\" max=\"500\">30</param>\n"
              "<param name=\"dilat\" gui-text=\"" N_("Dilatation") "\" type=\"float\" appearance=\"full\" min=\"1\" max=\"50\">1</param>\n"
              "<param name=\"erosion\" gui-text=\"" N_("Erosion") "\" type=\"float\" appearance=\"full\" min=\"0\" max=\"50\">0</param>\n"
              "<param name=\"strength\" gui-text=\"" N_("Strength") "\" type=\"float\" appearance=\"full\" min=\"0.1\" max=\"10\">0.5</param>\n"
              "<param name=\"length\" gui-text=\"" N_("Length") "\" type=\"float\" appearance=\"full\" min=\"0.5\" max=\"20\">4</param>\n"
              "<param name=\"trans\" gui-text=\"" N_("Transparent") "\" type=\"bool\" >false</param>\n"
              "<effect>\n"
                "<object-type>all</object-type>\n"
                "<effects-menu>\n"
                  "<submenu name=\"" N_("Filters") "\">\n"
                    "<submenu name=\"" N_("Image Paint and Draw") "\"/>\n"
                  "</submenu>\n"
                "</effects-menu>\n"
                "<menu-tip>" N_("Convert image to an engraving made of vertical and horizontal lines") "</menu-tip>\n"
              "</effect>\n"
            "</inkscape-extension>\n", std::make_unique<CrossEngraving>());
        // clang-format on
    };
};

gchar const *
CrossEngraving::get_filter_text (Inkscape::Extension::Extension * ext)
{
    if (_filter != nullptr) g_free((void *)_filter);

    std::ostringstream clean;
    std::ostringstream dilat;
    std::ostringstream erosion;
    std::ostringstream strength;
    std::ostringstream length;
    std::ostringstream trans;

    clean << (-1000 - ext->get_param_int("clean"));
    dilat << ext->get_param_float("dilat");
    erosion << (- ext->get_param_float("erosion"));
    strength << ext->get_param_float("strength");
    length << ext->get_param_float("length");
    if (ext->get_param_bool("trans"))
        trans << "composite3";
    else
        trans << "blend";

    // clang-format off
    _filter = g_strdup_printf(
        "<filter xmlns:inkscape=\"http://www.inkscape.org/namespaces/inkscape\" style=\"color-interpolation-filters:sRGB;\" inkscape:label=\"Cross Engraving\">\n"
          "<feConvolveMatrix in=\"SourceGraphic\" targetY=\"1\" targetX=\"1\" kernelMatrix=\"0 250 0 250 %s 250 0 250 0 \" order=\"3 3\" result=\"convolve\" />\n"
          "<feComposite in=\"convolve\" in2=\"convolve\" k1=\"1\" k2=\"1\" operator=\"arithmetic\" result=\"composite1\" />\n"
          "<feColorMatrix in=\"composite1\" values=\"0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 -0.2125 -0.7154 -0.0721 1 0 \" result=\"color1\" />\n"
          "<feColorMatrix in=\"color1\" values=\"1 0 0 0 0 0 1 0 0 0 0 0 1 0 0 0 0 0 %s %s \" result=\"color2\" />\n"
          "<feComposite in=\"color2\" in2=\"color2\" operator=\"arithmetic\" k2=\"%s\" result=\"composite2\" />\n"
          "<feGaussianBlur in=\"composite2\" stdDeviation=\"%s 0.01\" result=\"blur1\" />\n"
          "<feGaussianBlur in=\"composite2\" stdDeviation=\"0.01 %s\" result=\"blur2\" />\n"
          "<feComposite in=\"blur2\" in2=\"blur1\" k3=\"1\" k2=\"1\" operator=\"arithmetic\" result=\"composite3\" />\n"
          "<feFlood flood-color=\"rgb(255,255,255)\" flood-opacity=\"1\" result=\"flood\" />\n"
          "<feBlend in=\"flood\" in2=\"composite3\" mode=\"multiply\" result=\"blend\" />\n"
          "<feComposite in=\"%s\" in2=\"SourceGraphic\" operator=\"in\" result=\"composite4\" />\n"
        "</filter>\n", clean.str().c_str(), dilat.str().c_str(), erosion.str().c_str(), strength.str().c_str(), length.str().c_str(), length.str().c_str(), trans.str().c_str());
    // clang-format on

    return _filter;
}; /* CrossEngraving filter */

/**
    \brief    Custom predefined Drawing filter.
    
    Convert images to duochrome drawings.

    Filter's parameters:
    * Simplification strength (0.01->20, default 0.6) -> blur1 (stdDeviation)
    * Clean-up (1->500, default 10) -> convolve1 (kernelMatrix, central value -1001->-1500, default -1010)
    * Erase (0.->6., default 0) -> composite1 (k4)
    * Smoothness strength (0.01->20, default 0.6) -> blur2 (stdDeviation)
    * Dilatation (1.->50., default 6) -> color2 (n-1th value)
    * Erosion (0.->50., default 2) -> color2 (nth value 0->-50)
    * translucent (boolean, default false) -> composite 8 (in, true->merge1, false->color5)

    * Blur strength (0.01->20., default 1.) -> blur3 (stdDeviation)
    * Blur dilatation (1.->50., default 6) -> color4 (n-1th value)
    * Blur erosion (0.->50., default 2) -> color4 (nth value 0->-50)

    * Stroke color (guint, default 64,64,64,255) -> flood2 (flood-color), composite3 (k2)
    * Image on stroke (boolean, default false) -> composite2 (in="flood2" true-> in="SourceGraphic")
    * Offset (-100->100, default 0) -> offset (val)

    * Fill color (guint, default 200,200,200,255) -> flood3 (flood-opacity), composite5 (k2)
    * Image on fill (boolean, default false) -> composite4 (in="flood3" true-> in="SourceGraphic")

*/

class Drawing : public Inkscape::Extension::Internal::Filter::Filter {
protected:
    gchar const * get_filter_text (Inkscape::Extension::Extension * ext) override;

public:
    Drawing ( ) : Filter() { };
    ~Drawing ( ) override { if (_filter != nullptr) g_free((void *)_filter); return; }

    static void init () {
        // clang-format off
        Inkscape::Extension::build_from_mem(
            "<inkscape-extension xmlns=\"" INKSCAPE_EXTENSION_URI "\">\n"
              "<name>" N_("Drawing") "</name>\n"
              "<id>org.inkscape.effect.filter.Drawing</id>\n"
              "<param name=\"tab\" type=\"notebook\">\n"
                "<page name=\"optionstab\" gui-text=\"Options\">\n"
                  "<label appearance=\"header\">" N_("Simplify") "</label>\n"
                  "<param name=\"simply\" gui-text=\"" N_("Strength") "\" type=\"float\" indent=\"1\" appearance=\"full\" precision=\"2\" min=\"0.01\" max=\"20.00\">0.6</param>\n"
                  "<param name=\"clean\" gui-text=\"" N_("Clean-up") "\" type=\"int\" indent=\"1\" appearance=\"full\" min=\"1\" max=\"500\">10</param>\n"
                  "<param name=\"erase\" gui-text=\"" N_("Erase") "\" type=\"float\" indent=\"1\" appearance=\"full\" min=\"0\" max=\"60\">0</param>\n"
                  "<param name=\"translucent\" gui-text=\"" N_("Translucent") "\" indent=\"1\" type=\"bool\" >false</param>\n"
                  "<label appearance=\"header\">" N_("Smoothness") "</label>\n"
                    "<param name=\"smooth\" gui-text=\"" N_("Strength") "\" type=\"float\" indent=\"1\" appearance=\"full\" precision=\"2\" min=\"0.01\" max=\"20.00\">0.6</param>\n"
                    "<param name=\"dilat\" gui-text=\"" N_("Dilatation") "\" type=\"float\" indent=\"1\" appearance=\"full\" min=\"1\" max=\"50\">6</param>\n"
                    "<param name=\"erosion\" gui-text=\"" N_("Erosion") "\" type=\"float\" indent=\"1\" appearance=\"full\" min=\"0\" max=\"50\">2</param>\n"
                  "<label appearance=\"header\">" N_("Melt") "</label>\n"
                    "<param name=\"blur\" gui-text=\"" N_("Level") "\" type=\"float\" indent=\"1\" appearance=\"full\" precision=\"2\" min=\"0.01\" max=\"20.00\">1</param>\n"
                    "<param name=\"bdilat\" gui-text=\"" N_("Dilatation") "\" type=\"float\" indent=\"1\" appearance=\"full\" min=\"1\" max=\"50\">6</param>\n"
                    "<param name=\"berosion\" gui-text=\"" N_("Erosion") "\" type=\"float\" indent=\"1\" appearance=\"full\" min=\"0\" max=\"50\">2</param>\n"
                "</page>\n"
                "<page name=\"co11tab\" gui-text=\"Fill color\">\n"
                  "<param name=\"fcolor\" gui-text=\"" N_("Fill color") "\" type=\"color\">-1515870721</param>\n"
                  "<param name=\"iof\" gui-text=\"" N_("Image on fill") "\" type=\"bool\" >false</param>\n"
                "</page>\n"
                "<page name=\"co12tab\" gui-text=\"Stroke color\">\n"
                  "<param name=\"scolor\" gui-text=\"" N_("Stroke color") "\" type=\"color\">589505535</param>\n"
                  "<param name=\"ios\" gui-text=\"" N_("Image on stroke") "\" type=\"bool\" >false</param>\n"
                  "<param name=\"offset\" gui-text=\"" N_("Offset") "\" type=\"int\" appearance=\"full\" min=\"-100\" max=\"100\">0</param>\n"
                "</page>\n"
              "</param>\n"
              "<effect>\n"
                "<object-type>all</object-type>\n"
                "<effects-menu>\n"
                  "<submenu name=\"" N_("Filters") "\">\n"
                    "<submenu name=\"" N_("Image Paint and Draw") "\"/>\n"
                  "</submenu>\n"
                "</effects-menu>\n"
                "<menu-tip>" N_("Convert images to duochrome drawings") "</menu-tip>\n"
              "</effect>\n"
            "</inkscape-extension>\n", std::make_unique<Drawing>());
        // clang-format on
    };
};

gchar const *
Drawing::get_filter_text (Inkscape::Extension::Extension * ext)
{
    if (_filter != nullptr) g_free((void *)_filter);

    std::ostringstream simply;
    std::ostringstream clean;
    std::ostringstream erase;
    std::ostringstream smooth;
    std::ostringstream dilat;
    std::ostringstream erosion;
    std::ostringstream translucent;
    std::ostringstream offset;
    std::ostringstream blur;
    std::ostringstream bdilat;
    std::ostringstream berosion;
    std::ostringstream ios;
    std::ostringstream iof;

    simply << ext->get_param_float("simply");
    clean << (-1000 - ext->get_param_int("clean"));
    erase << (ext->get_param_float("erase") / 10);
    smooth << ext->get_param_float("smooth");
    dilat << ext->get_param_float("dilat");
    erosion << (- ext->get_param_float("erosion"));
    if (ext->get_param_bool("translucent"))
        translucent << "merge1";
    else
        translucent << "color5";
    offset << ext->get_param_int("offset");
    
    blur << ext->get_param_float("blur");
    bdilat << ext->get_param_float("bdilat");
    berosion << (- ext->get_param_float("berosion"));

    auto fcolor = ext->get_param_color("fcolor");
    if (ext->get_param_bool("iof"))
        iof << "SourceGraphic";
    else
        iof << "flood3";

    auto scolor = ext->get_param_color("scolor");
    if (ext->get_param_bool("ios"))
        ios << "SourceGraphic";
    else
        ios << "flood2";
    
    // clang-format off
    _filter = g_strdup_printf(
        "<filter xmlns:inkscape=\"http://www.inkscape.org/namespaces/inkscape\" style=\"color-interpolation-filters:sRGB;\" inkscape:label=\"Drawing\">\n"
        "<feGaussianBlur in=\"SourceGraphic\" stdDeviation=\"%s\" result=\"blur1\" />\n"
        "<feConvolveMatrix in=\"blur1\" targetX=\"1\" targetY=\"1\" order=\"3 3\" kernelMatrix=\"0 250 0 250 %s 250 0 250 0 \" result=\"convolve1\" />\n"
        "<feComposite in=\"convolve1\" in2=\"convolve1\" k1=\"1\" k2=\"1\" k4=\"%s\" operator=\"arithmetic\" result=\"composite1\" />\n"
        "<feColorMatrix in=\"composite1\" values=\"0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 -0.2125 -0.7154 -0.0721 1 0 \" result=\"color1\" />\n"
        "<feGaussianBlur stdDeviation=\"%s\" result=\"blur2\" />\n"
        "<feColorMatrix values=\"1 0 0 0 0 0 1 0 0 0 0 0 1 0 0 0 0 0 %s %s \" result=\"color2\" />\n"
        "<feFlood flood-color=\"rgb(255,255,255)\" result=\"flood1\" />\n"
        "<feBlend in2=\"color2\" mode=\"multiply\" result=\"blend1\" />\n"
        "<feComponentTransfer in=\"blend1\" result=\"component1\">\n"
          "<feFuncR type=\"discrete\" tableValues=\"0 1 1 1\" />\n"
          "<feFuncG type=\"discrete\" tableValues=\"0 1 1 1\" />\n"
          "<feFuncB type=\"discrete\" tableValues=\"0 1 1 1\" />\n"
        "</feComponentTransfer>\n"
        "<feGaussianBlur stdDeviation=\"%s\" result=\"blur3\" />\n"
        "<feColorMatrix in=\"blur3\" values=\"0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 -0.2125 -0.7154 -0.0721 1 0 \" result=\"color3\" />\n"
        "<feColorMatrix values=\"1 0 0 0 0 0 1 0 0 0 0 0 1 0 0 0 0 0 %s %s \" result=\"color4\" />\n"
        "<feFlood flood-color=\"%s\" result=\"flood2\" />\n"
        "<feComposite in=\"%s\" in2=\"color4\" operator=\"in\" result=\"composite2\" />\n"
        "<feComposite in=\"composite2\" in2=\"composite2\" operator=\"arithmetic\" k2=\"%f\" result=\"composite3\" />\n"
        "<feOffset dx=\"%s\" dy=\"%s\" result=\"offset1\" />\n"
        "<feFlood in=\"color4\" flood-color=\"%s\" result=\"flood3\" />\n"
        "<feComposite in=\"%s\" in2=\"color4\" operator=\"out\" result=\"composite4\" />\n"
        "<feComposite in=\"composite4\" in2=\"composite4\" operator=\"arithmetic\" k2=\"%f\" result=\"composite5\" />\n"
        "<feMerge result=\"merge1\">\n"
          "<feMergeNode in=\"composite5\" />\n"
          "<feMergeNode in=\"offset1\" />\n"
        "</feMerge>\n"
        "<feColorMatrix values=\"1 0 0 0 0 0 1 0 0 0 0 0 1 0 0 0 0 0 1.3 0 \" result=\"color5\" flood-opacity=\"0.56\" />\n"
        "<feComposite in=\"%s\" in2=\"SourceGraphic\" operator=\"in\" result=\"composite8\" />\n"
        "</filter>\n", simply.str().c_str(), clean.str().c_str(), erase.str().c_str(), smooth.str().c_str(), dilat.str().c_str(), erosion.str().c_str(),  blur.str().c_str(), bdilat.str().c_str(), berosion.str().c_str(), scolor.toString(false).c_str(), ios.str().c_str(), scolor.getOpacity(), offset.str().c_str(), offset.str().c_str(), fcolor.toString(false).c_str(), iof.str().c_str(), fcolor.getOpacity(), translucent.str().c_str());
    // clang-format on

    return _filter;
}; /* Drawing filter */


/**
    \brief    Custom predefined Electrize filter.
    
    Electro solarization effects.

    Filter's parameters:
    * Simplify (0.01->10., default 2.) -> blur (stdDeviation)
    * Effect type (enum: table or discrete, default "table") -> component (type)
    * Level (0->10, default 3) -> component (tableValues)
    * Inverted (boolean, default false) -> component (tableValues)
*/
class Electrize : public Inkscape::Extension::Internal::Filter::Filter {
protected:
    gchar const * get_filter_text (Inkscape::Extension::Extension * ext) override;

public:
    Electrize ( ) : Filter() { };
    ~Electrize ( ) override { if (_filter != nullptr) g_free((void *)_filter); return; }

    static void init () {
        // clang-format off
        Inkscape::Extension::build_from_mem(
            "<inkscape-extension xmlns=\"" INKSCAPE_EXTENSION_URI "\">\n"
              "<name>" N_("Electrize") "</name>\n"
              "<id>org.inkscape.effect.filter.Electrize</id>\n"
              "<param name=\"blur\" gui-text=\"" N_("Simplify") "\" type=\"float\" appearance=\"full\" min=\"0.01\" max=\"10.0\">2.0</param>\n"
              "<param name=\"type\" gui-text=\"" N_("Effect type:") "\" type=\"optiongroup\" appearance=\"combo\">\n"
                "<option value=\"table\">" N_("Table") "</option>\n"
                "<option value=\"discrete\">" N_("Discrete") "</option>\n"
              "</param>\n"
              "<param name=\"levels\" gui-text=\"" N_("Levels") "\" type=\"int\" appearance=\"full\" min=\"0\" max=\"10\">3</param>\n"
              "<param name=\"invert\" gui-text=\"" N_("Inverted") "\" type=\"bool\">false</param>\n"
              "<effect>\n"
                "<object-type>all</object-type>\n"
                "<effects-menu>\n"
                  "<submenu name=\"" N_("Filters") "\">\n"
                    "<submenu name=\"" N_("Image Paint and Draw") "\"/>\n"
                  "</submenu>\n"
                "</effects-menu>\n"
                "<menu-tip>" N_("Electro solarization effects") "</menu-tip>\n"
              "</effect>\n"
            "</inkscape-extension>\n", std::make_unique<Electrize>());
        // clang-format on
    };
};

gchar const *
Electrize::get_filter_text (Inkscape::Extension::Extension * ext)
{
    if (_filter != nullptr) g_free((void *)_filter);

    std::ostringstream blur;
    std::ostringstream type;
    std::ostringstream values;

    blur << ext->get_param_float("blur");
    type << ext->get_param_optiongroup("type");

    // TransfertComponent table values are calculated based on the effect level and inverted parameters.
    int val = 0;
    int levels = ext->get_param_int("levels") + 1;
    if (ext->get_param_bool("invert")) {
        val = 1;
    }
    values << val;
    for ( int step = 1 ; step <= levels ; step++ ) {
        if (val == 1) {
            val = 0;
        }
        else {
            val = 1;
        }
        values << " " << val;
    }
  
    // clang-format off
    _filter = g_strdup_printf(
        "<filter xmlns:inkscape=\"http://www.inkscape.org/namespaces/inkscape\" style=\"color-interpolation-filters:sRGB;\" inkscape:label=\"Electrize\">\n"
          "<feGaussianBlur stdDeviation=\"%s\" result=\"blur\" />\n"
          "<feComponentTransfer in=\"blur\" result=\"component\" >\n"
            "<feFuncR type=\"%s\" tableValues=\"%s\" />\n"
            "<feFuncG type=\"%s\" tableValues=\"%s\" />\n"
            "<feFuncB type=\"%s\" tableValues=\"%s\" />\n"
          "</feComponentTransfer>\n"
        "</filter>\n", blur.str().c_str(), type.str().c_str(), values.str().c_str(), type.str().c_str(), values.str().c_str(), type.str().c_str(), values.str().c_str());
    // clang-format on

    return _filter;
}; /* Electrize filter */

/**
    \brief    Custom predefined Neon draw filter.
    
    Posterize and draw smooth lines around color shapes

    Filter's parameters:
    * Lines type (enum, default smooth) ->
        smooth = component2 (type="table"), composite1 (in2="blur2")
        hard = component2 (type="discrete"), composite1 (in2="component1")
    * Simplify (0.01->20., default 3) -> blur1 (stdDeviation)
    * Line width (0.01->20., default 3) -> blur2 (stdDeviation)
    * Lightness (0.->10., default 1) -> composite1 (k2)
    * Blend (enum [normal, multiply, screen], default normal) -> blend (mode)
    * Dark mode (boolean, default false) -> composite2 (true: in2="component2")
*/
class NeonDraw : public Inkscape::Extension::Internal::Filter::Filter {
protected:
    gchar const * get_filter_text (Inkscape::Extension::Extension * ext) override;

public:
    NeonDraw ( ) : Filter() { };
    ~NeonDraw ( ) override { if (_filter != nullptr) g_free((void *)_filter); return; }

    static void init () {
        // clang-format off
        Inkscape::Extension::build_from_mem(
            "<inkscape-extension xmlns=\"" INKSCAPE_EXTENSION_URI "\">\n"
              "<name>" N_("Neon Draw") "</name>\n"
              "<id>org.inkscape.effect.filter.NeonDraw</id>\n"
              "<param name=\"type\" gui-text=\"" N_("Line type:") "\" type=\"optiongroup\" appearance=\"combo\">\n"
                "<option value=\"table\">" N_("Smoothed") "</option>\n"
                "<option value=\"discrete\">" N_("Contrasted") "</option>\n"
              "</param>\n"
              "<param name=\"simply\" gui-text=\"" N_("Simplify") "\" type=\"float\" appearance=\"full\" precision=\"2\" min=\"0.01\" max=\"20.00\">3</param>\n"
              "<param name=\"width\" gui-text=\"" N_("Line width") "\" type=\"float\" appearance=\"full\" precision=\"2\" min=\"0.01\" max=\"20.00\">3</param>\n"
              "<param name=\"lightness\" gui-text=\"" N_("Lightness") "\" type=\"float\" appearance=\"full\" precision=\"2\" min=\"0.00\" max=\"10.00\">1</param>\n"
              "<param name=\"blend\" gui-text=\"" N_("Blend mode:") "\" type=\"optiongroup\" appearance=\"combo\">\n"
                "<option value=\"normal\">Normal</option>\n"
                "<option value=\"multiply\">Multiply</option>\n"
                "<option value=\"screen\">Screen</option>\n"
              "</param>\n"
              "<effect>\n"
                "<object-type>all</object-type>\n"
                "<effects-menu>\n"
                  "<submenu name=\"" N_("Filters") "\">\n"
                    "<submenu name=\"" N_("Image Paint and Draw") "\"/>\n"
                  "</submenu>\n"
                "</effects-menu>\n"
                "<menu-tip>" N_("Posterize and draw smooth lines around color shapes") "</menu-tip>\n"
              "</effect>\n"
            "</inkscape-extension>\n", std::make_unique<NeonDraw>());
        // clang-format on
    };
};

gchar const *
NeonDraw::get_filter_text (Inkscape::Extension::Extension * ext)
{
    if (_filter != nullptr) g_free((void *)_filter);

    std::ostringstream blend;
    std::ostringstream simply;
    std::ostringstream width;
    std::ostringstream lightness;
    std::ostringstream type;

    type << ext->get_param_optiongroup("type");
    blend << ext->get_param_optiongroup("blend");
    simply << ext->get_param_float("simply");
    width << ext->get_param_float("width");
    lightness << ext->get_param_float("lightness");

    // clang-format off
    _filter = g_strdup_printf(
        "<filter xmlns:inkscape=\"http://www.inkscape.org/namespaces/inkscape\" style=\"color-interpolation-filters:sRGB;\" inkscape:label=\"Neon Draw\">\n"
          "<feBlend mode=\"%s\" result=\"blend\" />\n"
          "<feGaussianBlur in=\"blend\" stdDeviation=\"%s\" result=\"blur1\" />\n"
          "<feColorMatrix values=\"1 0 0 0 0 0 1 0 0 0 0 0 1 0 0 0 0 0 50 0\" result=\"color1\" />\n"
          "<feComponentTransfer result=\"component1\">\n"
            "<feFuncR type=\"discrete\" tableValues=\"0 0.3 0.3 0.3 0.3 0.6 0.6 0.6 0.6 1 1\" />\n"
            "<feFuncG type=\"discrete\" tableValues=\"0 0.3 0.3 0.3 0.3 0.6 0.6 0.6 0.6 1 1\" />\n"
            "<feFuncB type=\"discrete\" tableValues=\"0 0.3 0.3 0.3 0.3 0.6 0.6 0.6 0.6 1 1\" />\n"
          "</feComponentTransfer>\n"
          "<feGaussianBlur in=\"component1\" stdDeviation=\"%s\" result=\"blur2\" />\n"
          "<feColorMatrix values=\"1 0 0 0 0 0 1 0 0 0 0 0 1 0 0 0 0 0 50 0\" result=\"color2\" />\n"
          "<feComponentTransfer in=\"color2\" result=\"component2\">\n"
            "<feFuncR type=\"%s\" tableValues=\"0 1 1 1 0 0 0 1 1 1 0 0 0 1 1 1 0 0 0 1\" />\n"
            "<feFuncG type=\"%s\" tableValues=\"0 1 1 1 0 0 0 1 1 1 0 0 0 1 1 1 0 0 0 1\" />\n"
            "<feFuncB type=\"%s\" tableValues=\"0 1 1 1 0 0 0 1 1 1 0 0 0 1 1 1 0 0 0 1\" />\n"
          "</feComponentTransfer>\n"
          "<feComposite in=\"component2\" in2=\"blur2\" k3=\"%s\" operator=\"arithmetic\" k2=\"1\" result=\"composite1\" />\n"
          "<feComposite in=\"composite1\" in2=\"SourceGraphic\" operator=\"in\" result=\"composite2\" />\n"
        "</filter>\n", blend.str().c_str(), simply.str().c_str(), width.str().c_str(), type.str().c_str(), type.str().c_str(), type.str().c_str(), lightness.str().c_str());
    // clang-format on

    return _filter;
}; /* NeonDraw filter */

/**
    \brief    Custom predefined Point engraving filter.
    
    Convert image to a transparent point engraving

    Filter's parameters:

    * Turbulence type (enum, default fractalNoise else turbulence) -> turbulence (type)
    * Horizontal frequency (0.001->1., default 1) -> turbulence (baseFrequency [/100])
    * Vertical frequency (0.001->1., default 1) -> turbulence (baseFrequency [/100])
    * Complexity (1->5, default 3) -> turbulence (numOctaves)
    * Variation (0->1000, default 0) -> turbulence (seed)
    * Noise reduction (-1000->-1500, default -1045) -> convolve (kernelMatrix, central value)
    * Noise blend (enum, all blend options, default normal) -> blend (mode)
    * Lightness (0.->10., default 2.5) -> composite1 (k1)
    * Grain lightness (0.->10., default 1.3) -> composite1 (k2)
    * Erase (0.00->1., default 0) -> composite1 (k4)
    * Blur (0.01->2., default 0.5) -> blur (stdDeviation)
    
    * Drawing color (guint32, default rgb(255,255,255)) -> flood1 (flood-color, flood-opacity)
    
    * Background color (guint32, default rgb(99,89,46)) -> flood2 (flood-color, flood-opacity)
*/

class PointEngraving : public Inkscape::Extension::Internal::Filter::Filter {
protected:
    gchar const * get_filter_text (Inkscape::Extension::Extension * ext) override;

public:
    PointEngraving ( ) : Filter() { };
    ~PointEngraving ( ) override { if (_filter != nullptr) g_free((void *)_filter); return; }

    static void init () {
        // clang-format off
        Inkscape::Extension::build_from_mem(
            "<inkscape-extension xmlns=\"" INKSCAPE_EXTENSION_URI "\">\n"
              "<name>" N_("Point Engraving") "</name>\n"
              "<id>org.inkscape.effect.filter.PointEngraving</id>\n"
              "<param name=\"tab\" type=\"notebook\">\n"
                "<page name=\"optionstab\" gui-text=\"" N_("Options") "\">\n"
                  "<param name=\"type\" gui-text=\"" N_("Turbulence type:") "\" type=\"optiongroup\" appearance=\"combo\">\n"
                    "<option value=\"fractalNoise\">" N_("Fractal noise") "</option>\n"
                  "<option value=\"turbulence\">" N_("Turbulence") "</option>\n"
                  "</param>\n"
                  "<param name=\"hfreq\" gui-text=\"" N_("Horizontal frequency") "\" type=\"float\" appearance=\"full\" precision=\"2\" min=\"0.1\" max=\"100.00\">100</param>\n"
                  "<param name=\"vfreq\" gui-text=\"" N_("Vertical frequency") "\" type=\"float\" appearance=\"full\" precision=\"2\" min=\"0.1\" max=\"100.00\">100</param>\n"
                  "<param name=\"complexity\" gui-text=\"" N_("Complexity") "\" type=\"int\" appearance=\"full\" min=\"1\" max=\"5\">1</param>\n"
                  "<param name=\"variation\" gui-text=\"" N_("Variation") "\" type=\"int\" appearance=\"full\" min=\"1\" max=\"100\">0</param>\n"
                  "<param name=\"reduction\" gui-text=\"" N_("Noise reduction") "\" type=\"int\" appearance=\"full\" min=\"0\" max=\"500\">45</param>\n"
                  "<param name=\"blend\" gui-text=\"" N_("Noise blend:") "\" type=\"optiongroup\" appearance=\"combo\">\n"
                    "<option value=\"multiply\">" N_("Multiply") "</option>\n"
                    "<option value=\"normal\">" N_("Normal") "</option>\n"
                    "<option value=\"screen\">" N_("Screen") "</option>\n"
                    "<option value=\"lighten\">" N_("Lighten") "</option>\n"
                    "<option value=\"darken\">" N_("Darken") "</option>\n"
                  "</param>\n"
                  "<param name=\"lightness\" gui-text=\"" N_("Lightness") "\" type=\"float\" appearance=\"full\" precision=\"2\" min=\"0\" max=\"10\">2.5</param>\n"
                  "<param name=\"grain\" gui-text=\"" N_("Grain lightness") "\" type=\"float\" appearance=\"full\" precision=\"2\" min=\"0\" max=\"10\">1.3</param>\n"
                  "<param name=\"erase\" gui-text=\"" N_("Erase") "\" type=\"float\" appearance=\"full\" precision=\"2\" min=\"0\" max=\"1\">0</param>\n"
                  "<param name=\"blur\" gui-text=\"" N_("Blur") "\" type=\"float\" appearance=\"full\" precision=\"2\" min=\"0.01\" max=\"2\">0.5</param>\n"
                "</page>\n"
                "<page name=\"fcolortab\" gui-text=\"" N_("Fill color") "\">\n"
                  "<param name=\"fcolor\" gui-text=\"" N_("Color") "\" type=\"color\">-1</param>\n"
                  "<param name=\"iof\" gui-text=\"" N_("Image on fill") "\" type=\"bool\" >false</param>\n"
                "</page>\n"
                "<page name=\"pcolortab\" gui-text=\"" N_("Points color") "\">\n"
                  "<param name=\"pcolor\" gui-text=\"" N_("Color") "\" type=\"color\">1666789119</param>\n"
                  "<param name=\"iop\" gui-text=\"" N_("Image on points") "\" type=\"bool\" >false</param>\n"
                "</page>\n"
              "</param>\n"
              "<effect>\n"
                "<object-type>all</object-type>\n"
                "<effects-menu>\n"
                  "<submenu name=\"" N_("Filters") "\">\n"
                    "<submenu name=\"" N_("Image Paint and Draw") "\"/>\n"
                  "</submenu>\n"
                "</effects-menu>\n"
                "<menu-tip>" N_("Convert image to a transparent point engraving") "</menu-tip>\n"
              "</effect>\n"
            "</inkscape-extension>\n", std::make_unique<PointEngraving>());
        // clang-format on
    };

};

gchar const *
PointEngraving::get_filter_text (Inkscape::Extension::Extension * ext)
{
    if (_filter != nullptr) g_free((void *)_filter);
  
    std::ostringstream type;
    std::ostringstream hfreq;
    std::ostringstream vfreq;
    std::ostringstream complexity;
    std::ostringstream variation;
    std::ostringstream reduction;
    std::ostringstream blend;
    std::ostringstream lightness;
    std::ostringstream grain;
    std::ostringstream erase;
    std::ostringstream blur;
    std::ostringstream iof;
    std::ostringstream iop;

    type << ext->get_param_optiongroup("type");
    hfreq << ext->get_param_float("hfreq") / 100;
    vfreq << ext->get_param_float("vfreq") / 100;
    complexity << ext->get_param_int("complexity");
    variation << ext->get_param_int("variation");
    reduction << (-1000 - ext->get_param_int("reduction"));
    blend << ext->get_param_optiongroup("blend");
    lightness << ext->get_param_float("lightness");
    grain << ext->get_param_float("grain");
    erase << ext->get_param_float("erase");
    blur << ext->get_param_float("blur");
    
    auto fcolor = ext->get_param_color("fcolor");
    auto pcolor = ext->get_param_color("pcolor");

    if (ext->get_param_bool("iof"))
        iof << "SourceGraphic";
    else
        iof << "flood2";

    if (ext->get_param_bool("iop"))
        iop << "SourceGraphic";
    else
        iop << "flood1";

    // clang-format off
    _filter = g_strdup_printf(
        "<filter xmlns:inkscape=\"http://www.inkscape.org/namespaces/inkscape\" inkscape:label=\"Point Engraving\" style=\"color-interpolation-filters:sRGB;\">\n"
          "<feConvolveMatrix in=\"SourceGraphic\" kernelMatrix=\"0 250 0 250 %s 250 0 250 0\" order=\"3 3\" result=\"convolve\" />\n"
          "<feBlend in=\"convolve\" in2=\"SourceGraphic\" mode=\"%s\" result=\"blend\" />\n"
          "<feTurbulence type=\"%s\" baseFrequency=\"%s %s\" numOctaves=\"%s\" seed=\"%s\" result=\"turbulence\" />\n"
          "<feColorMatrix in=\"blend\" type=\"luminanceToAlpha\" result=\"colormatrix1\" />\n"
          "<feComposite in=\"turbulence\" in2=\"colormatrix1\" k1=\"%s\" k2=\"%s\" k4=\"%s\" operator=\"arithmetic\" result=\"composite1\" />\n"
          "<feColorMatrix in=\"composite1\" values=\"1 0 0 0 0 0 1 0 0 0 0 0 1 0 0 0 0 0 10 -9 \" result=\"colormatrix2\" />\n"
          "<feGaussianBlur stdDeviation=\"%s\" result=\"blur\" />\n"
          "<feFlood flood-color=\"%s\" flood-opacity=\"%f\" result=\"flood1\" />\n"
          "<feComposite in=\"%s\" in2=\"blur\" operator=\"out\" result=\"composite2\" />\n"
          "<feFlood flood-color=\"%s\" flood-opacity=\"%f\" result=\"flood2\" />\n"
          "<feComposite in=\"%s\" in2=\"blur\" operator=\"in\" result=\"composite3\" />\n"
          "<feComposite in=\"composite3\" in2=\"composite2\" k2=\"%f\" k3=\"%f\"  operator=\"arithmetic\" result=\"composite4\" />\n"
          "<feComposite in2=\"SourceGraphic\" operator=\"in\" result=\"composite5\" />\n"
        "</filter>\n", reduction.str().c_str(), blend.str().c_str(),
                       type.str().c_str(), hfreq.str().c_str(), vfreq.str().c_str(), complexity.str().c_str(), variation.str().c_str(),
                       lightness.str().c_str(), grain.str().c_str(), erase.str().c_str(), blur.str().c_str(),
                       pcolor.toString(false).c_str(), pcolor.getOpacity(), iop.str().c_str(),
                       fcolor.toString(false).c_str(), fcolor.getOpacity(), iof.str().c_str(),
                       fcolor.getOpacity(), pcolor.getOpacity());
    // clang-format on

    return _filter;
}; /* Point engraving filter */

/**
    \brief    Custom predefined Poster paint filter.
    
    Poster and painting effects.

    Filter's parameters:
    * Effect type (enum, default "Normal") ->
        Normal = feComponentTransfer
        Dented = Normal + intermediate values
    * Transfer type (enum, default "discrete") -> component (type)
    * Levels (0->15, default 5) -> component (tableValues)
    * Blend mode (enum, default "Lighten") -> blend (mode)
    * Primary simplify (0.01->100., default 4.) -> blur1 (stdDeviation)
    * Secondary simplify (0.01->100., default 0.5) -> blur2 (stdDeviation)
    * Pre-saturation (0.->1., default 1.) -> color1 (values)
    * Post-saturation (0.->1., default 1.) -> color2 (values)
    * Simulate antialiasing (boolean, default false) -> blur3 (true->stdDeviation=0.5, false->stdDeviation=0.01)
*/
class Posterize : public Inkscape::Extension::Internal::Filter::Filter {
protected:
    gchar const * get_filter_text (Inkscape::Extension::Extension * ext) override;

public:
    Posterize ( ) : Filter() { };
    ~Posterize ( ) override { if (_filter != nullptr) g_free((void *)_filter); return; }

    static void init () {
        // clang-format off
        Inkscape::Extension::build_from_mem(
            "<inkscape-extension xmlns=\"" INKSCAPE_EXTENSION_URI "\">\n"
              "<name>" N_("Poster Paint") "</name>\n"
              "<id>org.inkscape.effect.filter.Posterize</id>\n"
              "<param name=\"type\" gui-text=\"" N_("Effect type:") "\" type=\"optiongroup\" appearance=\"combo\">\n"
                "<option value=\"normal\">Normal</option>\n"
                "<option value=\"dented\">Dented</option>\n"
              "</param>\n"
              "<param name=\"table\" gui-text=\"" N_("Transfer type:") "\" type=\"optiongroup\" appearance=\"combo\">\n"
                "<option value=\"discrete\">" N_("Poster") "</option>\n"
                "<option value=\"table\">" N_("Painting") "</option>\n"
              "</param>\n"
              "<param name=\"levels\" gui-text=\"" N_("Levels") "\" type=\"int\" appearance=\"full\" min=\"0\" max=\"15\">5</param>\n"
              "<param name=\"blend\" gui-text=\"" N_("Blend mode:") "\" type=\"optiongroup\" appearance=\"combo\">\n"
                "<option value=\"lighten\">Lighten</option>\n"
                "<option value=\"normal\">Normal</option>\n"
                "<option value=\"darken\">Darken</option>\n"
                "<option value=\"multiply\">Multiply</option>\n"
                "<option value=\"screen\">Screen</option>\n"
              "</param>\n"
              "<param name=\"blur1\" gui-text=\"" N_("Simplify (primary)") "\" type=\"float\" appearance=\"full\" precision=\"2\" min=\"0.01\" max=\"100.00\">4.0</param>\n"
              "<param name=\"blur2\" gui-text=\"" N_("Simplify (secondary)") "\" type=\"float\" appearance=\"full\" precision=\"2\" min=\"0.01\" max=\"100.00\">0.5</param>\n"
              "<param name=\"presaturation\" gui-text=\"" N_("Pre-saturation") "\" type=\"float\" appearance=\"full\" precision=\"2\" min=\"0.00\" max=\"1.00\">1.00</param>\n"
              "<param name=\"postsaturation\" gui-text=\"" N_("Post-saturation") "\" type=\"float\" appearance=\"full\" precision=\"2\" min=\"0.00\" max=\"1.00\">1.00</param>\n"
              "<param name=\"antialiasing\" gui-text=\"" N_("Simulate antialiasing") "\" type=\"bool\">false</param>\n"
              "<effect>\n"
                "<object-type>all</object-type>\n"
                "<effects-menu>\n"
                  "<submenu name=\"" N_("Filters") "\">\n"
                    "<submenu name=\"" N_("Image Paint and Draw") "\"/>\n"
                  "</submenu>\n"
                "</effects-menu>\n"
                "<menu-tip>" N_("Poster and painting effects") "</menu-tip>\n"
              "</effect>\n"
            "</inkscape-extension>\n", std::make_unique<Posterize>());
        // clang-format on
    };
};

gchar const *
Posterize::get_filter_text (Inkscape::Extension::Extension * ext)
{
    if (_filter != nullptr) g_free((void *)_filter);

    std::ostringstream table;
    std::ostringstream blendmode;
    std::ostringstream blur1;
    std::ostringstream blur2;
    std::ostringstream presat;
    std::ostringstream postsat;
    std::ostringstream transf;
    std::ostringstream antialias;
    
    table << ext->get_param_optiongroup("table");
    blendmode << ext->get_param_optiongroup("blend");
    blur1 << ext->get_param_float("blur1");
    blur2 << ext->get_param_float("blur2");
    presat << ext->get_param_float("presaturation");
    postsat << ext->get_param_float("postsaturation");

    // TransfertComponent table values are calculated based on the poster type.
    transf << "0";
    int levels = ext->get_param_int("levels") + 1;
    const gchar *effecttype =  ext->get_param_optiongroup("type");
    if (levels == 1) {
        if ((g_ascii_strcasecmp("dented", effecttype) == 0)) {
            transf << " 1 0 1";
        } else {
            transf << " 1";
        }
    } else {
        for ( int step = 1 ; step <= levels ; step++ ) {
            float val = (float) step / levels;
            transf << " " << val;
            if ((g_ascii_strcasecmp("dented", effecttype) == 0)) {
                transf << " " << (val - ((float) 1 / (3 * levels))) << " " << (val + ((float) 1 / (2 * levels)));
            }
        }
    }
    transf << " 1";
    
    if (ext->get_param_bool("antialiasing"))
        antialias << "0.5";
    else
        antialias << "0.01";
    
    // clang-format off
    _filter = g_strdup_printf(
        "<filter xmlns:inkscape=\"http://www.inkscape.org/namespaces/inkscape\" style=\"color-interpolation-filters:sRGB;\" inkscape:label=\"Poster Paint\">\n"
          "<feComposite operator=\"arithmetic\" k2=\"1\" result=\"composite1\" />\n"
          "<feGaussianBlur stdDeviation=\"%s\" result=\"blur1\" />\n"
          "<feGaussianBlur in=\"composite1\" stdDeviation=\"%s\" result=\"blur2\" />\n"
          "<feBlend in2=\"blur1\" mode=\"%s\" result=\"blend\"/>\n"
          "<feColorMatrix type=\"saturate\" values=\"%s\" result=\"color1\" />\n"
          "<feComponentTransfer result=\"component\">\n"
            "<feFuncR type=\"%s\" tableValues=\"%s\" />\n"
            "<feFuncG type=\"%s\" tableValues=\"%s\" />\n"
            "<feFuncB type=\"%s\" tableValues=\"%s\" />\n"
          "</feComponentTransfer>\n"
          "<feColorMatrix type=\"saturate\" values=\"%s\" result=\"color2\" />\n"
          "<feGaussianBlur stdDeviation=\"%s\" result=\"blur3\" />\n"
          "<feComposite in2=\"SourceGraphic\" operator=\"in\" result=\"composite3\" />\n"
        "</filter>\n", blur1.str().c_str(), blur2.str().c_str(), blendmode.str().c_str(), presat.str().c_str(), table.str().c_str(), transf.str().c_str(), table.str().c_str(), transf.str().c_str(), table.str().c_str(), transf.str().c_str(), postsat.str().c_str(), antialias.str().c_str());
    // clang-format on

    return _filter;
}; /* Posterize filter */

/**
    \brief    Custom predefined Posterize basic filter.
    
    Simple posterizing effect

    Filter's parameters:
    * Levels (0->20, default 5) -> component1 (tableValues)
    * Blur (0.01->20., default 4.) -> blur1 (stdDeviation)
*/
class PosterizeBasic : public Inkscape::Extension::Internal::Filter::Filter {
protected:
    gchar const * get_filter_text (Inkscape::Extension::Extension * ext) override;

public:
    PosterizeBasic ( ) : Filter() { };
    ~PosterizeBasic ( ) override { if (_filter != nullptr) g_free((void *)_filter); return; }

    static void init () {
        // clang-format off
        Inkscape::Extension::build_from_mem(
            "<inkscape-extension xmlns=\"" INKSCAPE_EXTENSION_URI "\">\n"
              "<name>" N_("Posterize Basic") "</name>\n"
              "<id>org.inkscape.effect.filter.PosterizeBasic</id>\n"
              "<param name=\"levels\" gui-text=\"" N_("Levels") "\" type=\"int\" appearance=\"full\" min=\"0\" max=\"20\">5</param>\n"
              "<param name=\"blur\" gui-text=\"" N_("Simplify") "\" type=\"float\" appearance=\"full\" precision=\"2\" min=\"0.01\" max=\"20.00\">4.0</param>\n"
              "<effect>\n"
                "<object-type>all</object-type>\n"
                "<effects-menu>\n"
                  "<submenu name=\"" N_("Filters") "\">\n"
                    "<submenu name=\"" N_("Image Paint and Draw") "\"/>\n"
                  "</submenu>\n"
                "</effects-menu>\n"
                "<menu-tip>" N_("Simple posterizing effect") "</menu-tip>\n"
              "</effect>\n"
            "</inkscape-extension>\n", std::make_unique<PosterizeBasic>());
        // clang-format on
    };
};

gchar const *
PosterizeBasic::get_filter_text (Inkscape::Extension::Extension * ext)
{
    if (_filter != nullptr) g_free((void *)_filter);

    std::ostringstream blur;
    std::ostringstream transf;
    
    blur << ext->get_param_float("blur");

    transf << "0";
    int levels = ext->get_param_int("levels") + 1;
    for ( int step = 1 ; step <= levels ; step++ ) {
        const float val = (float) step / levels;
        transf << " " << val;
    }
    transf << " 1";

    // clang-format off
    _filter = g_strdup_printf(
        "<filter xmlns:inkscape=\"http://www.inkscape.org/namespaces/inkscape\" style=\"color-interpolation-filters:sRGB;\" inkscape:label=\"Posterize Basic\">\n"
          "<feGaussianBlur stdDeviation=\"%s\" result=\"blur1\" />\n"
          "<feComponentTransfer in=\"blur1\" result=\"component1\">\n"
            "<feFuncR type=\"discrete\" tableValues=\"%s\" />\n"
            "<feFuncG type=\"discrete\" tableValues=\"%s\" />\n"
            "<feFuncB type=\"discrete\" tableValues=\"%s\" />\n"
          "</feComponentTransfer>\n"
          "<feComposite in=\"component1\" in2=\"SourceGraphic\" operator=\"in\" />\n"
        "</filter>\n", blur.str().c_str(), transf.str().c_str(), transf.str().c_str(), transf.str().c_str());
    // clang-format on

    return _filter;
}; /* PosterizeBasic filter */

}; /* namespace Filter */
}; /* namespace Internal */
}; /* namespace Extension */
}; /* namespace Inkscape */

/* Change the 'PAINT' below to be your file name */
#endif /* SEEN_INKSCAPE_EXTENSION_INTERNAL_FILTER_PAINT_H__ */

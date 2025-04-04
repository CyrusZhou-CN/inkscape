// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * @brief Metafile printing - common routines
 *//*
 * Authors:
 *   Krzysztof Kosiński <tweenk.pl@gmail.com>
 *
 * Copyright (C) 2013 Authors
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include <cstring>
#include <fstream>
#include <glib.h>
#include <glibmm/miscutils.h>
#include <2geom/rect.h>
#include <2geom/curves.h>
#include <2geom/svg-path-parser.h>

#include "extension/internal/metafile-print.h"
#include "extension/print.h"
#include "path-prefix.h"
#include "object/sp-gradient.h"
#include "object/sp-image.h"
#include "object/sp-linear-gradient.h"
#include "object/sp-pattern.h"
#include "object/sp-radial-gradient.h"
#include "style.h"

namespace Inkscape {
namespace Extension {
namespace Internal {

U_COLORREF toColorRef(std::optional<Colors::Color> color)
{
    // Make sure it's RGB first, if wmf can support other color formats, this can be changed.
    auto c = color ? *color->converted(Colors::Space::Type::RGB) : Colors::Color(0x000000ff);
    return U_RGBA(255 * c[0], 255 * c[1], 255 * c[2], 255 * c[3]);
}


PrintMetafile::~PrintMetafile() = default;

static std::map<Glib::ustring, FontfixParams> const &get_ppt_fixable_fonts()
{
    static std::map<Glib::ustring, FontfixParams> _ppt_fixable_fonts;

    if (_ppt_fixable_fonts.empty()) {
        _ppt_fixable_fonts = {
    // clang-format off
    {{"Arial"},                    { 0.05,  -0.055, -0.065}},
    {{"Times New Roman"},          { 0.05,  -0.055, -0.065}},
    {{"Lucida Sans"},              {-0.025, -0.055, -0.065}},
    {{"Sans"},                     { 0.05,  -0.055, -0.065}},
    {{"Microsoft Sans Serif"},     {-0.05,  -0.055, -0.065}},
    {{"Serif"},                    { 0.05,  -0.055, -0.065}},
    {{"Garamond"},                 { 0.05,  -0.055, -0.065}},
    {{"Century Schoolbook"},       { 0.25,   0.025,  0.025}},
    {{"Verdana"},                  { 0.025,  0.0,    0.0}},
    {{"Tahoma"},                   { 0.045,  0.025,  0.025}},
    {{"Symbol"},                   { 0.025,  0.0,    0.0}},
    {{"Wingdings"},                { 0.05,   0.0,    0.0}},
    {{"Zapf Dingbats"},            { 0.025,  0.0,    0.0}},
    {{"Convert To Symbol"},        { 0.025,  0.0,    0.0}},
    {{"Convert To Wingdings"},     { 0.05,   0.0,    0.0}},
    {{"Convert To Zapf Dingbats"}, { 0.025,  0.0,    0.0}},
    {{"Sylfaen"},                  { 0.1,    0.0,    0.0}},
    {{"Palatino Linotype"},        { 0.175,  0.125,  0.125}},
    {{"Segoe UI"},                 { 0.1,    0.0,    0.0}},
    // clang-format on
        };
    }
    return _ppt_fixable_fonts;
}


bool PrintMetafile::textToPath(Inkscape::Extension::Print *ext)
{
    return ext->get_param_bool("textToPath");
}

unsigned int PrintMetafile::bind(Inkscape::Extension::Print * /*mod*/, Geom::Affine const &transform, float /*opacity*/)
{
    if (!m_tr_stack.empty()) {
        Geom::Affine tr_top = m_tr_stack.top();
        m_tr_stack.push(transform * tr_top);
    } else {
        m_tr_stack.push(transform);
    }

    return 1;
}

unsigned int PrintMetafile::release(Inkscape::Extension::Print * /*mod*/)
{
    m_tr_stack.pop();
    return 1;
}

// Finds font fix parameters for the given fontname.
void PrintMetafile::_lookup_ppt_fontfix(Glib::ustring const &fontname, FontfixParams &params)
{
    auto const &fixable_fonts = get_ppt_fixable_fonts();
    auto it = fixable_fonts.find(fontname);
    if (it != fixable_fonts.end()) {
        params = it->second;
    }
}

U_COLORREF PrintMetafile::_gethexcolor(uint32_t color)
{
    U_COLORREF out;
    out =   U_RGB(
                (color >> 16) & 0xFF,
                (color >>  8) & 0xFF,
                (color >>  0) & 0xFF
            );
    return out;
}

// Translate Inkscape weights to EMF weights.
uint32_t PrintMetafile::_translate_weight(unsigned inkweight)
{
    switch (inkweight) {
        // 400 is tested first, as it is the most common case
        case SP_CSS_FONT_WEIGHT_400: return U_FW_NORMAL;
        case SP_CSS_FONT_WEIGHT_100: return U_FW_THIN;
        case SP_CSS_FONT_WEIGHT_200: return U_FW_EXTRALIGHT;
        case SP_CSS_FONT_WEIGHT_300: return U_FW_LIGHT;
        case SP_CSS_FONT_WEIGHT_500: return U_FW_MEDIUM;
        case SP_CSS_FONT_WEIGHT_600: return U_FW_SEMIBOLD;
        case SP_CSS_FONT_WEIGHT_700: return U_FW_BOLD;
        case SP_CSS_FONT_WEIGHT_800: return U_FW_EXTRABOLD;
        case SP_CSS_FONT_WEIGHT_900: return U_FW_HEAVY;
        default: return U_FW_NORMAL;
    }
}

/* opacity weighting of two colors as float.  v1 is the color, op is its opacity, v2 is the background color */
inline float opweight(float v1, float v2, float op)
{
    return v1 * op + v2 * (1.0 - op);
}

U_COLORREF PrintMetafile::avg_stop_color(SPGradient *gr)
{
    U_COLORREF cr;
    int last = gr->vector.stops.size() - 1;
    if (last >= 1) {
        auto rgbs = *gr->vector.stops[0   ].color->converted(Colors::Space::Type::RGB);
        auto rgbe = *gr->vector.stops[last].color->converted(Colors::Space::Type::RGB);

        /* Replace opacity at start & stop with that fraction background color, then average those two for final color. */
        cr =    U_RGB(
                    255 * ((opweight(rgbs[0], gv.rgb[0], rgbs[3])   +   opweight(rgbe[0], gv.rgb[0], rgbe[3])) / 2.0),
                    255 * ((opweight(rgbs[1], gv.rgb[1], rgbs[3])   +   opweight(rgbe[1], gv.rgb[1], rgbe[3])) / 2.0),
                    255 * ((opweight(rgbs[2], gv.rgb[2], rgbs[3])   +   opweight(rgbe[2], gv.rgb[2], rgbe[3])) / 2.0)
                );
    } else {
        cr = U_RGB(0, 0, 0);  // The default fill
    }
    return cr;
}

U_COLORREF PrintMetafile::weight_opacity(U_COLORREF c1)
{
    float opa = c1.Reserved / 255.0;
    U_COLORREF result = U_RGB(
                            255 * opweight((float)c1.Red  / 255.0, gv.rgb[0], opa),
                            255 * opweight((float)c1.Green / 255.0, gv.rgb[1], opa),
                            255 * opweight((float)c1.Blue / 255.0, gv.rgb[2], opa)
                        );
    return result;
}

/* t between 0 and 1, values outside that range use the nearest limit */
U_COLORREF PrintMetafile::weight_colors(U_COLORREF c1, U_COLORREF c2, double t)
{
#define clrweight(a,b,t) ((1-t)*((double) a) + (t)*((double) b))
    U_COLORREF result;
    t = ( t > 1.0 ? 1.0 : ( t < 0.0 ? 0.0 : t));
    // clang-format off
    result.Red      = clrweight(c1.Red,      c2.Red,      t);
    result.Green    = clrweight(c1.Green,    c2.Green,    t);
    result.Blue     = clrweight(c1.Blue,     c2.Blue,     t);
    result.Reserved = clrweight(c1.Reserved, c2.Reserved, t);
    // clang-format on

    // now handle the opacity, mix the RGB with background at the weighted opacity

    if (result.Reserved != 255) {
        result = weight_opacity(result);
    }

    return result;
}

// Extract hatchType, hatchColor from a name like
// EMFhatch<hatchType>_<hatchColor>
// Where the first one is a number and the second a color in hex.
// hatchType and hatchColor have been set with defaults before this is called.
//
void PrintMetafile::hatch_classify(char *name, int *hatchType, U_COLORREF *hatchColor, U_COLORREF *bkColor)
{
    int      val;
    uint32_t hcolor = 0;
    uint32_t bcolor = 0;

    // name should be EMFhatch or WMFhatch but *MFhatch will be accepted
    if (0 != strncmp(&name[1], "MFhatch", 7)) {
        return;    // not anything we can parse
    }
    name += 8; // EMFhatch already detected
    val   = 0;
    while (*name && isdigit(*name)) {
        val = 10 * val + *name - '0';
        name++;
    }
    *hatchType = val;
    if (*name != '_' || val > U_HS_DITHEREDBKCLR) { // wrong syntax, cannot classify
        *hatchType = -1;
    } else {
        name++;
        if (2 != sscanf(name, "%X_%X", &hcolor, &bcolor)) { // not a pattern with background
            if (1 != sscanf(name, "%X", &hcolor)) {
                *hatchType = -1;    // not a pattern, cannot classify
            }
            *hatchColor = _gethexcolor(hcolor);
        } else {
            *hatchColor = _gethexcolor(hcolor);
            *bkColor    = _gethexcolor(bcolor);
            usebk       = true;
        }
    }
    /* Everything > U_HS_SOLIDCLR is solid, just specify the color in the brush rather than messing around with background or textcolor */
    if (*hatchType > U_HS_SOLIDCLR) {
        *hatchType = U_HS_SOLIDCLR;
    }
}

//
//  Recurse down from a brush pattern, try to figure out what it is.
//  If an image is found set a pointer to the epixbuf, else set that to NULL
//  If a pattern is found with a name like [EW]MFhatch3_3F7FFF return hatchType=3, hatchColor=3F7FFF (as a uint32_t),
//    otherwise hatchType is set to -1 and hatchColor is not defined.
//

void PrintMetafile::brush_classify(SPObject *parent, int depth, Inkscape::Pixbuf const **epixbuf, int *hatchType, U_COLORREF *hatchColor, U_COLORREF *bkColor)
{
    if (depth == 0) {
        *epixbuf    = nullptr;
        *hatchType  = -1;
        *hatchColor = U_RGB(0, 0, 0);
        *bkColor    = U_RGB(255, 255, 255);
    }
    depth++;
    // first look along the pattern chain, if there is one
    if (is<SPPattern>(parent)) {
        for (auto pat_i = cast_unsafe<SPPattern>(parent); pat_i; pat_i = pat_i->ref.getObject()) {
            char temp[32];  // large enough
            strncpy(temp, pat_i->getAttribute("id"), sizeof(temp)-1); // Some names may be longer than [EW]MFhatch#_######
            temp[sizeof(temp)-1] = '\0';
            hatch_classify(temp, hatchType, hatchColor, bkColor);
            if (*hatchType != -1) {
                return;
            }

            // still looking?  Look at this pattern's children, if there are any
            for (auto& child: pat_i->children) {
                if (*epixbuf || *hatchType != -1) {
                    break;
                }
                brush_classify(&child, depth, epixbuf, hatchType, hatchColor, bkColor);
            }
        }
    } else if (auto img = cast<SPImage>(parent)) {
        *epixbuf = img->pixbuf.get();
        return;
    } else { // some inkscape rearrangements pass through nodes between pattern and image which are not classified as either.
        for (auto& child: parent->children) {
            if (*epixbuf || *hatchType != -1) {
                break;
            }
            brush_classify(&child, depth, epixbuf, hatchType, hatchColor, bkColor);
        }
    }
}

//swap R/B in 4 byte pixel
void PrintMetafile::swapRBinRGBA(char *px, int pixels)
{
    char tmp;
    for (int i = 0; i < pixels * 4; px += 4, i += 4) {
        tmp = px[2];
        px[2] = px[0];
        px[0] = tmp;
    }
}

int PrintMetafile::hold_gradient(void *gr, int mode)
{
    gv.mode = mode;
    gv.grad = gr;
    if (mode == DRAW_RADIAL_GRADIENT) {
        SPRadialGradient *rg = (SPRadialGradient *) gr;
        gv.r  = rg->r.computed;                                 // radius, but of what???
        gv.p1 = Geom::Point(rg->cx.computed, rg->cy.computed);  // center
        gv.p2 = Geom::Point(gv.r, 0) + gv.p1;                   // xhandle
        gv.p3 = Geom::Point(0, -gv.r) + gv.p1;                  // yhandle
        if (rg->gradientTransform_set) {
            gv.p1 = gv.p1 * rg->gradientTransform;
            gv.p2 = gv.p2 * rg->gradientTransform;
            gv.p3 = gv.p3 * rg->gradientTransform;
        }
    } else if (mode == DRAW_LINEAR_GRADIENT) {
        SPLinearGradient *lg = (SPLinearGradient *) gr;
        gv.r = 0;                                               // unused
        gv.p1 = Geom::Point(lg->x1.computed, lg->y1.computed);  // start
        gv.p2 = Geom::Point(lg->x2.computed, lg->y2.computed);  // end
        gv.p3 = Geom::Point(0, 0);                              // unused
        if (lg->gradientTransform_set) {
            gv.p1 = gv.p1 * lg->gradientTransform;
            gv.p2 = gv.p2 * lg->gradientTransform;
        }
    } else {
        g_error("Fatal programming error, hold_gradient() in metafile-print.cpp called with invalid draw mode");
    }
    return 1;
}

/*  convert from center ellipse to SVGEllipticalArc ellipse

    From:
    http://www.w3.org/TR/SVG/implnote.html#ArcConversionEndpointToCenter
    A point (x,y) on the arc can be found by:

    {x,y} = {cx,cy} + {cosF,-sinF,sinF,cosF} x {rxcosT,rysinT}

    where
      {cx,cy} is the center of the ellipse
      F       is the rotation angle of the X axis of the ellipse from the true X axis
      T       is the rotation angle around the ellipse
      {,,,}   is the rotation matrix
      rx,ry   are the radii of the ellipse's axes

    For SVG parameterization need two points.
    Arbitrarily we can use T=0 and T=pi
    Since the sweep is 180 the flags are always 0:

    F is in RADIANS, but the SVGEllipticalArc needs degrees!

*/
Geom::PathVector PrintMetafile::center_ellipse_as_SVG_PathV(Geom::Point ctr, double rx, double ry, double F)
{
    using Geom::X;
    using Geom::Y;
    double x1, y1, x2, y2;
    Geom::Path SVGep;

    x1 = ctr[X]  +  cos(F) * rx * cos(0)      +   sin(-F) * ry * sin(0);
    y1 = ctr[Y]  +  sin(F) * rx * cos(0)      +   cos(F)  * ry * sin(0);
    x2 = ctr[X]  +  cos(F) * rx * cos(M_PI)   +   sin(-F) * ry * sin(M_PI);
    y2 = ctr[Y]  +  sin(F) * rx * cos(M_PI)   +   cos(F)  * ry * sin(M_PI);

    char text[256];
    snprintf(text, 256, " M %f,%f A %f %f %f 0 0 %f %f A %f %f %f 0 0 %f %f z",
                  x1, y1,  rx, ry, F * 360. / (2.*M_PI), x2, y2,   rx, ry, F * 360. / (2.*M_PI), x1, y1);
    Geom::PathVector outres =  Geom::parse_svg_path(text);
    return outres;
}


/*  rx2,ry2 must be larger than rx1,ry1!
    angle is in RADIANS
*/
Geom::PathVector PrintMetafile::center_elliptical_ring_as_SVG_PathV(Geom::Point ctr, double rx1, double ry1, double rx2, double ry2, double F)
{
    using Geom::X;
    using Geom::Y;
    double x11, y11, x12, y12;
    double x21, y21, x22, y22;
    double degrot = F * 360. / (2.*M_PI);

    x11 = ctr[X]  +  cos(F) * rx1 * cos(0)      +   sin(-F) * ry1 * sin(0);
    y11 = ctr[Y]  +  sin(F) * rx1 * cos(0)      +   cos(F)  * ry1 * sin(0);
    x12 = ctr[X]  +  cos(F) * rx1 * cos(M_PI)   +   sin(-F) * ry1 * sin(M_PI);
    y12 = ctr[Y]  +  sin(F) * rx1 * cos(M_PI)   +   cos(F)  * ry1 * sin(M_PI);

    x21 = ctr[X]  +  cos(F) * rx2 * cos(0)      +   sin(-F) * ry2 * sin(0);
    y21 = ctr[Y]  +  sin(F) * rx2 * cos(0)      +   cos(F)  * ry2 * sin(0);
    x22 = ctr[X]  +  cos(F) * rx2 * cos(M_PI)   +   sin(-F) * ry2 * sin(M_PI);
    y22 = ctr[Y]  +  sin(F) * rx2 * cos(M_PI)   +   cos(F)  * ry2 * sin(M_PI);

    char text[512];
    snprintf(text, 512, " M %f,%f A %f %f %f 0 1 %f %f A %f %f %f 0 1 %f %f z M %f,%f  A %f %f %f 0 0 %f %f A %f %f %f 0 0 %f %f z",
                  x11, y11,  rx1, ry1, degrot, x12, y12,   rx1, ry1, degrot, x11, y11,
                  x21, y21,  rx2, ry2, degrot, x22, y22,   rx2, ry2, degrot, x21, y21);
    Geom::PathVector outres = Geom::parse_svg_path(text);

    return outres;
}

/* Elliptical hole in a large square extending from -50k to +50k */
Geom::PathVector PrintMetafile::center_elliptical_hole_as_SVG_PathV(Geom::Point ctr, double rx, double ry, double F)
{
    using Geom::X;
    using Geom::Y;
    double x1, y1, x2, y2;
    Geom::Path SVGep;

    x1 = ctr[X]  +  cos(F) * rx * cos(0)      +   sin(-F) * ry * sin(0);
    y1 = ctr[Y]  +  sin(F) * rx * cos(0)      +   cos(F)  * ry * sin(0);
    x2 = ctr[X]  +  cos(F) * rx * cos(M_PI)   +   sin(-F) * ry * sin(M_PI);
    y2 = ctr[Y]  +  sin(F) * rx * cos(M_PI)   +   cos(F)  * ry * sin(M_PI);

    char text[256];
    snprintf(text, 256, " M %f,%f A %f %f %f 0 0 %f %f A %f %f %f 0 0 %f %f z M 50000,50000 50000,-50000 -50000,-50000 -50000,50000 z",
                  x1, y1,  rx, ry, F * 360. / (2.*M_PI), x2, y2,   rx, ry, F * 360. / (2.*M_PI), x1, y1);
    Geom::PathVector outres =  Geom::parse_svg_path(text);
    return outres;
}

/* rectangular cutter.
ctr    "center" of rectangle (might not actually be in the center with respect to leading/trailing edges
pos    vector from center to leading edge
neg    vector from center to trailing edge
width  vector to side edge
*/
Geom::PathVector PrintMetafile::rect_cutter(Geom::Point ctr, Geom::Point pos, Geom::Point neg, Geom::Point width)
{
    Geom::PathVector outres;
    Geom::Path cutter;
    cutter.start(ctr + pos - width);
    cutter.appendNew<Geom::LineSegment>(ctr + pos + width);
    cutter.appendNew<Geom::LineSegment>(ctr + neg + width);
    cutter.appendNew<Geom::LineSegment>(ctr + neg - width);
    cutter.close();
    outres.push_back(cutter);
    return outres;
}

/*  Convert from SPWindRule to livarot's FillRule
    This is similar to what sp_selected_path_boolop() does
*/
FillRule PrintMetafile::SPWR_to_LVFR(SPWindRule wr)
{
    FillRule fr;
    if (wr ==  SP_WIND_RULE_EVENODD) {
        fr = fill_oddEven;
    } else {
        fr = fill_nonZero;
    }
    return fr;
}

} // namespace Internal
} // namespace Extension
} // namespace Inkscape

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

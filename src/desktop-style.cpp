// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Desktop style management
 *
 * Authors:
 *   bulia byak
 *   verbalshadow
 *   Jon A. Cruz <jon@joncruz.org>
 *   Abhishek Sharma
 *
 * Copyright (C) 2004, 2006 authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include <string>
#include <cstring>

#include <glibmm.h>

#include "desktop-style.h"

#include "colors/color.h"
#include "colors/color-set.h"
#include "desktop.h"
#include "filter-chemistry.h"
#include "inkscape.h"
#include "selection.h"
#include "message-context.h"
#include "message-stack.h"


#include "object/box3d-side.h"
#include "object/filters/blend.h"
#include "object/filters/gaussian-blur.h"
#include "object/sp-flowdiv.h"
#include "object/sp-flowregion.h"
#include "object/sp-flowtext.h"
#include "object/sp-hatch.h"
#include "object/sp-linear-gradient.h"
#include "object/sp-path.h"
#include "object/sp-pattern.h"
#include "object/sp-radial-gradient.h"
#include "object/sp-textpath.h"
#include "object/sp-tref.h"
#include "object/sp-tspan.h"
#include "object/sp-use.h"
#include "style.h"

#include "svg/css-ostringstream.h"
#include "svg/svg.h"

#include "ui/tools/tool-base.h"

#include <glibmm/i18n.h>
#include "xml/sp-css-attr.h"
#include "xml/attribute-record.h"

static bool isTextualItem(SPObject const *obj)
{
    return is<SPText>(obj)
        || is<SPFlowtext>(obj)
        || is<SPTSpan>(obj)
        || is<SPTRef>(obj)
        || is<SPTextPath>(obj)
        || is<SPFlowdiv>(obj)
        || is<SPFlowpara>(obj)
        || is<SPFlowtspan>(obj);
}

/**
 * Set color on selection on desktop.
 */
void
sp_desktop_set_color(SPDesktop *desktop, Color const &color, bool is_relative, bool fill)
{
    /// \todo relative color setting
    if (is_relative) {
        g_warning("FIXME: relative color setting not yet implemented");
        return;
    }

    SPCSSAttr *css = sp_repr_css_attr_new();
    sp_repr_css_set_property_string(css, fill ? "fill" : "stroke", color.toString(false));
    sp_repr_css_set_property_double(css, fill ? "fill-opacity" : "stroke-opacity", color.getOpacity());
    sp_desktop_set_style(desktop, css);

    sp_repr_css_attr_unref(css);
}

/**
 * Apply style on object and children, recursively.
 */
void
sp_desktop_apply_css_recursive(SPObject *o, SPCSSAttr *css, bool skip_lines)
{
    // non-items should not have style
    auto item = cast<SPItem>(o);
    if (!item) {
        return;
    }

    // 1. tspans with role=line are not regular objects in that they are not supposed to have style of their own,
    // but must always inherit from the parent text. Same for textPath.
    // However, if the line tspan or textPath contains some style (old file?), we reluctantly set our style to it too.

    // 2. Generally we allow setting style on clones, but when it's inside flowRegion, do not touch
    // it, be it clone or not; it's just styleless shape (because that's how Inkscape does
    // flowtext).

    auto tspan = cast<SPTSpan>(o);

    if (!(skip_lines
          && ((tspan && tspan->role == SP_TSPAN_ROLE_LINE)
              || is<SPFlowdiv>(o)
              || is<SPFlowpara>(o)
              || is<SPTextPath>(o))
          &&  !o->getAttribute("style"))
        &&
        !(is<SPFlowregionbreak>(o) ||
          is<SPFlowregionExclude>(o) ||
          (is<SPUse>(o) &&
           o->parent &&
           (is<SPFlowregion>(o->parent) ||
            is<SPFlowregionExclude>(o->parent)
               )
              )
            )
        ) {

        SPCSSAttr *css_set = sp_repr_css_attr_new();
        sp_repr_css_merge(css_set, css);

        // Scale the style by the inverse of the accumulated parent transform in the paste context.
        {
            Geom::Affine const local(item->i2doc_affine());
            double const ex(local.descrim());
            if ( ( ex != 0. )
                 && ( ex != 1. ) ) {
                sp_css_attr_scale(css_set, 1/ex);
            }
        }

        o->changeCSS(css_set,"style");

        sp_repr_css_attr_unref(css_set);
    }

    // setting style on child of clone spills into the clone original (via shared repr), don't do it!
    if (is<SPUse>(o)) {
        return;
    }

    for (auto& child: o->children) {
        if (sp_repr_css_property(css, "opacity", nullptr) != nullptr) {
            // Unset properties which are accumulating and thus should not be set recursively.
            // For example, setting opacity 0.5 on a group recursively would result in the visible opacity of 0.25 for an item in the group.
            SPCSSAttr *css_recurse = sp_repr_css_attr_new();
            sp_repr_css_merge(css_recurse, css);
            sp_repr_css_set_property(css_recurse, "opacity", nullptr);
            sp_desktop_apply_css_recursive(&child, css_recurse, skip_lines);
            sp_repr_css_attr_unref(css_recurse);
        } else {
            sp_desktop_apply_css_recursive(&child, css, skip_lines);
        }
    }
}

/**
 * Apply style on selection on desktop.
 */

void sp_desktop_set_style(SPDesktop *desktop, SPCSSAttr *css, bool change, bool write_current, bool switch_style)
{
    return sp_desktop_set_style(desktop->getSelection(), desktop, css, change, write_current, switch_style);
}

void
sp_desktop_set_style(Inkscape::ObjectSet *set, SPDesktop *desktop, SPCSSAttr *css, bool change, bool write_current, bool switch_style)
{
    if (write_current) {
        Inkscape::Preferences *prefs = Inkscape::Preferences::get();
        // 1. Set internal value
        sp_repr_css_merge(desktop->current, css);

        // 1a. Write to prefs; make a copy and unset any URIs first
        SPCSSAttr *css_write = sp_repr_css_attr_new();
        sp_repr_css_merge(css_write, css);
        sp_css_attr_unset_uris(css_write);
        prefs->mergeStyle("/desktop/style", css_write);
        auto itemlist = set->items();
        for (auto i = itemlist.begin(); i!= itemlist.end(); ++i) {
            /* last used styles for 3D box faces are stored separately */
            SPObject *obj = *i;
            auto side = cast<Box3DSide>(obj);
            if (side) {
                prefs->mergeStyle(
                        Glib::ustring("/desktop/") + side->axes_string() + "/style", css_write);
            }
        }
        sp_repr_css_attr_unref(css_write);
    }

    if (!change)
        return;

// 2. Emit signal... See desktop->connectSetStyle in text-tool, tweak-tool, and gradient-drag.
    bool intercepted = desktop->_set_style_signal.emit(css, switch_style);

/** \todo
 * FIXME: in set_style, compensate pattern and gradient fills, stroke width,
 * rect corners, font size for the object's own transform so that pasting
 * fills does not depend on preserve/optimize.
 */

// 3. If nobody has intercepted the signal, apply the style to the selection
    if (!intercepted) {
        // If we have an event context, update its cursor (TODO: it could be neater to do this with the signal sent above, but what if the signal gets intercepted?)
        if (auto const tool = desktop->getTool()) {
            tool->use_tool_cursor();
        }

        // Remove text attributes if not text...
        // Do this once in case a zillion objects are selected.
        SPCSSAttr *css_no_text = sp_repr_css_attr_new();
        sp_repr_css_merge(css_no_text, css);
        css_no_text = sp_css_attr_unset_text(css_no_text);

        auto itemlist = set->items();
        for (auto i = itemlist.begin(); i!= itemlist.end(); ++i) {
            SPItem *item = *i;

            // If not text, don't apply text attributes (can a group have text attributes? Yes! FIXME)
            if (isTextualItem(item)) {
                // If any font property has changed, then we have written out the font
                // properties in longhand and we need to remove the 'font' shorthand.
                if (!sp_repr_css_property_is_unset(css, "font-family")) {
                    sp_repr_css_unset_property(css, "font");
                }
                sp_desktop_apply_css_recursive(item, css, true);
            } else {
                sp_desktop_apply_css_recursive(item, css_no_text, true);
            }
        }
        sp_repr_css_attr_unref(css_no_text);
    }
}

/**
 * Return the desktop's current style.
 */
SPCSSAttr *
sp_desktop_get_style(SPDesktop *desktop, bool with_text)
{
    SPCSSAttr *css = sp_repr_css_attr_new();
    sp_repr_css_merge(css, desktop->current);
    const auto & l = css->attributeList();
    if (l.empty()) {
        sp_repr_css_attr_unref(css);
        return nullptr;
    } else {
        if (!with_text) {
            css = sp_css_attr_unset_text(css);
        }
        return css;
    }
}

/**
 * Return the desktop's current color.
 */
std::optional<Color>
sp_desktop_get_color(SPDesktop *desktop, bool is_fill)
{
    gchar const *property = sp_repr_css_property(desktop->current,
                                                 is_fill ? "fill" : "stroke",
                                                 "#000");

    // if there is style and the property in it,
    return Color::parse(desktop->current ? property : nullptr);
}

double
sp_desktop_get_master_opacity_tool(SPDesktop *desktop, Glib::ustring const &tool, bool *has_opacity)
{
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    SPCSSAttr *css = nullptr;
    gfloat value = 1.0; // default if nothing else found
    if (has_opacity)
        *has_opacity = false;
    if (prefs->getBool(tool + "/usecurrent")) {
        css = sp_desktop_get_style(desktop, true);
    } else {
        css = prefs->getStyle(tool + "/style");
    }

    if (css) {
        gchar const *property = css ? sp_repr_css_property(css, "opacity", "1.000") : nullptr;

        if (desktop->current && property) { // if there is style and the property in it,
            if ( !sp_svg_number_read_f(property, &value) ) {
                value = 1.0; // things failed. set back to the default
            } else {
                if (has_opacity)
                   *has_opacity = true;
            }
        }

        sp_repr_css_attr_unref(css);
    }

    return value;
}
double
sp_desktop_get_opacity_tool(SPDesktop *desktop, Glib::ustring const &tool, bool is_fill)
{
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    SPCSSAttr *css = nullptr;
    gfloat value = 1.0; // default if nothing else found
    if (prefs->getBool(tool + "/usecurrent")) {
        css = sp_desktop_get_style(desktop, true);
    } else {
        css = prefs->getStyle(tool + "/style");
    }

    if (css) {
        gchar const *property = css ? sp_repr_css_property(css, is_fill ? "fill-opacity": "stroke-opacity", "1.000") : nullptr;

        if (desktop->current && property) { // if there is style and the property in it,
            if ( !sp_svg_number_read_f(property, &value) ) {
                value = 1.0; // things failed. set back to the default
            }
        }

        sp_repr_css_attr_unref(css);
    }

    return value;
}

std::optional<Color>
sp_desktop_get_color_tool(SPDesktop *desktop, Glib::ustring const &tool, bool is_fill)
{
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    SPCSSAttr *css = nullptr;
    bool styleFromCurrent = prefs->getBool(tool + "/usecurrent");
    if (styleFromCurrent) {
        css = sp_desktop_get_style(desktop, true);
    } else {
        css = prefs->getStyle(tool + "/style");
        Inkscape::GC::anchor(css);
    }

    std::optional<Color> ret;
    if (css) {
        gchar const *property = sp_repr_css_property(css, is_fill ? "fill" : "stroke", "#000");
        // if there is style and the property in it,
        ret = Color::parse(desktop->current ? property : nullptr);
        sp_repr_css_attr_unref(css);
    }
    return ret;
}

/**
 * Apply the desktop's current style or the tool style to repr.
 */
void
sp_desktop_apply_style_tool(SPDesktop *desktop, Inkscape::XML::Node *repr, Glib::ustring const &tool_path, bool with_text)
{
    SPCSSAttr *css_current = sp_desktop_get_style(desktop, with_text);
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();

    if (prefs->getBool(tool_path + "/usecurrent") && css_current) {
        sp_repr_css_unset_property(css_current, "shape-inside");
        sp_repr_css_unset_property(css_current, "shape-subtract");
        sp_repr_css_unset_property(css_current, "mix-blend-mode");
        sp_repr_css_unset_property(css_current, "filter");
        sp_repr_css_unset_property(css_current, "stop-color");
        sp_repr_css_unset_property(css_current, "stop-opacity");
        sp_repr_css_set(repr, css_current, "style");
    } else {
        SPCSSAttr *css = prefs->getInheritedStyle(tool_path + "/style");
        sp_repr_css_unset_property(css, "shape-inside");
        sp_repr_css_unset_property(css, "shape-subtract");
        sp_repr_css_set(repr, css, "style");
        sp_repr_css_attr_unref(css);
    }
    if (css_current) {
        sp_repr_css_attr_unref(css_current);
    }
}

/**
 * Returns the font size (in SVG pixels) of the text tool style (if text
 * tool uses its own style) or desktop style (otherwise).
*/
double
sp_desktop_get_font_size_tool(SPDesktop *desktop)
{
    (void)desktop; // TODO cleanup
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    Glib::ustring desktop_style = prefs->getString("/desktop/style");
    Glib::ustring style_str;
    if ((prefs->getBool("/tools/text/usecurrent")) && !desktop_style.empty()) {
        style_str = desktop_style;
    } else {
        style_str = prefs->getString("/tools/text/style");
    }

    double ret = 12;
    if (!style_str.empty()) {
        SPStyle style(SP_ACTIVE_DOCUMENT);
        style.mergeString(style_str.data());
        ret = style.font_size.computed;
    }
    return ret;
}

/** Determine average stroke width, simple method */
gdouble
stroke_average_width (const std::vector<SPItem*> &objects)
{
    if (objects.empty())
        return Geom::infinity();

    gdouble avgwidth = 0.0;
    bool notstroked = true;
    int n_notstroked = 0;
    for (auto item : objects) {
        if (!item) {
            continue;
        }

        Geom::Affine i2dt = item->i2dt_affine();

        double width = item->style->stroke_width.computed * i2dt.descrim();

        // Width becomes NaN when scaling a diagonal line to a horizontal line (lp:825840)
        if (item->style->stroke.isNone() || std::isnan(width)) {
            ++n_notstroked;   // do not count nonstroked objects
            continue;
        } else {
            notstroked = false;
        }

        avgwidth += width;
    }

    if (notstroked)
        return Geom::infinity();

    return avgwidth / (objects.size() - n_notstroked);
}

/**
 * Write to style_res the average fill or stroke of list of objects, if applicable.
 */
int
objects_query_fillstroke (const std::vector<SPItem*> &objects, SPStyle *style_res, bool const isfill)
{
    if (objects.empty()) {
        /* No objects, set empty */
        return QUERY_STYLE_NOTHING;
    }

    SPIPaint *paint_res = style_res->getFillOrStroke(isfill);
    paint_res->set = true;

    bool paintImpossible = true;
    Colors::ColorSet colors;

    for (auto obj : objects) {
        if (!obj) {
            continue;
        }
        SPStyle *style = obj->style;
        if (!style) {
            continue;
        }

        SPIPaint *paint = style->getFillOrStroke(isfill);
        if (!paint) {
            continue;
        }

        // We consider paint "effectively set" for anything within text hierarchy
        SPObject *parent = obj->parent;
        bool paint_effectively_set =
            paint->set || is<SPText>(parent) || is<SPTextPath>(parent) || is<SPTSpan>(parent)
            || is<SPFlowtext>(parent) || is<SPFlowdiv>(parent) || is<SPFlowpara>(parent)
            || is<SPFlowtspan>(parent) || is<SPFlowline>(parent);

        // 1. Bail out with QUERY_STYLE_MULTIPLE_DIFFERENT if necessary

        // cppcheck-suppress comparisonOfBoolWithInt
        if ((!paintImpossible) && (!paint->isSameType(*paint_res) || (paint_res->set != paint_effectively_set))) {
            return QUERY_STYLE_MULTIPLE_DIFFERENT;  // different types of paint
        }

        if (paint_res->set && paint->set && paint_res->isPaintserver()) {
            // both previous paint and this paint were a server, see if the servers are compatible

            SPPaintServer *server_res = isfill ? style_res->getFillPaintServer() : style_res->getStrokePaintServer();
            SPPaintServer *server = isfill ? style->getFillPaintServer() : style->getStrokePaintServer();

            auto linear_res = cast<SPLinearGradient>(server_res);
            SPRadialGradient *radial_res = linear_res ? nullptr : cast<SPRadialGradient>(server_res);
            SPPattern *pattern_res = (linear_res || radial_res) ? nullptr : cast<SPPattern>(server_res);
            SPHatch *hatch_res =
                (linear_res || radial_res || pattern_res) ? nullptr : cast<SPHatch>(server_res);

            if (linear_res) {
                auto linear = cast<SPLinearGradient>(server);
                if (!linear) {
                   return QUERY_STYLE_MULTIPLE_DIFFERENT;  // different kind of server
                }

                SPGradient *vector = linear->getVector();
                SPGradient *vector_res = linear_res->getVector();
                if (vector_res != vector) {
                   return QUERY_STYLE_MULTIPLE_DIFFERENT;  // different gradient vectors
                }
            } else if (radial_res) {
                auto radial = cast<SPRadialGradient>(server);
                if (!radial) {
                   return QUERY_STYLE_MULTIPLE_DIFFERENT;  // different kind of server
                }

                SPGradient *vector = radial->getVector();
                SPGradient *vector_res = radial_res->getVector();
                if (vector_res != vector) {
                   return QUERY_STYLE_MULTIPLE_DIFFERENT;  // different gradient vectors
                }
            } else if (pattern_res) {
                auto pattern = cast<SPPattern>(server);
                if (!pattern) {
                   return QUERY_STYLE_MULTIPLE_DIFFERENT;  // different kind of server
                }

                auto pat = cast<SPPattern>(server)->rootPattern();
                auto pat_res = cast<SPPattern>(server_res)->rootPattern();
                if (pat_res != pat) {
                   return QUERY_STYLE_MULTIPLE_DIFFERENT;  // different pattern roots
                }
            } else if (hatch_res) {
                auto hatch = cast<SPHatch>(server);
                if (!hatch) {
                    return QUERY_STYLE_MULTIPLE_DIFFERENT; // different kind of server
                }

                auto hat = cast<SPHatch>(server)->rootHatch();
                auto hat_res = cast<SPHatch>(server_res)->rootHatch();
                if (hat_res != hat) {
                    return QUERY_STYLE_MULTIPLE_DIFFERENT; // different hatch roots
                }
            }
        }

        // 2. Sum color, copy server from paint to paint_res
        if (paint_res->set && paint->isColor()) {
            auto copy = paint->getColor();
            copy.addOpacity(isfill ? style->fill_opacity : style->stroke_opacity);

            if (colors.isEmpty()) {
                paint_res->setColor(copy);
            }
            // Remove colors from this list somehow?
            if (auto id = obj->getId()) {
                colors.set(id, copy);
            }
        }

       paintImpossible = false;
       paint_res->paintOrigin = paint->paintOrigin;
       if (paint_res->set && paint_effectively_set && paint->isPaintserver()) { // copy the server
           if (isfill) {
               sp_style_set_to_uri(style_res, true, style->getFillURI());
           } else {
               sp_style_set_to_uri(style_res, false, style->getStrokeURI());
           }
       }
       paint_res->set = paint_effectively_set;
       style_res->fill_rule.computed = style->fill_rule.computed; // no averaging on this, just use the last one
    }

    if (paint_res->set && paint_res->isColor() && !colors.isEmpty()) {
        auto color = colors.getAverage();
        if (isfill) {
            style_res->fill_opacity.set_double(color.stealOpacity());
        } else {
            style_res->stroke_opacity.set_double(color.stealOpacity());
        }
        paint_res->setColor(color);

        if (colors.size() > 1) {
            if (colors.isSame())
                return QUERY_STYLE_MULTIPLE_SAME;
            else
                return QUERY_STYLE_MULTIPLE_AVERAGED;
        } else {
            return QUERY_STYLE_SINGLE;
        }
    }

    // Not color
    if (objects.size() > 1) {
        return QUERY_STYLE_MULTIPLE_SAME;
    } else {
        return QUERY_STYLE_SINGLE;
    }
}

/**
 * Write to style_res the average opacity of a list of objects.
 */
int
objects_query_opacity (const std::vector<SPItem*> &objects, SPStyle *style_res)
{
    if (objects.empty()) {
        /* No objects, set empty */
        return QUERY_STYLE_NOTHING;
    }

    gdouble opacity_sum = 0;
    gdouble opacity_prev = -1;
    bool same_opacity = true;
    guint opacity_items = 0;


    for (auto obj : objects) {
        if (!obj) {
            continue;
        }
        SPStyle *style = obj->style;
        if (!style) {
            continue;
        }

        double opacity = SP_SCALE24_TO_FLOAT(style->opacity.value);
        opacity_sum += opacity;
        if (opacity_prev != -1 && opacity != opacity_prev) {
            same_opacity = false;
        }
        opacity_prev = opacity;
        opacity_items ++;
    }
    if (opacity_items > 1) {
        opacity_sum /= opacity_items;
    }

    style_res->opacity.value = SP_SCALE24_FROM_FLOAT(opacity_sum);

    if (opacity_items == 0) {
        return QUERY_STYLE_NOTHING;
    } else if (opacity_items == 1) {
        return QUERY_STYLE_SINGLE;
    } else {
        if (same_opacity) {
            return QUERY_STYLE_MULTIPLE_SAME;
        } else {
            return QUERY_STYLE_MULTIPLE_AVERAGED;
        }
    }
}

/**
 * Write to style_res the average stroke width of a list of objects.
 */
int
objects_query_strokewidth (const std::vector<SPItem*> &objects, SPStyle *style_res)
{
    if (objects.empty()) {
        /* No objects, set empty */
        return QUERY_STYLE_NOTHING;
    }

    gdouble avgwidth = 0.0;

    gdouble prev_sw = -1;
    bool same_sw = true;
    bool noneSet = true; // is stroke set to none?
    bool prev_hairline;

    int n_stroked = 0;

    for (auto obj : objects) {
        if (!obj) {
            continue;
        }
        auto item = obj;
        if (!item) {
            continue;
        }
        SPStyle *style = obj->style;
        if (!style) {
            continue;
        }

        noneSet &= style->stroke.isNone();

        if (style->stroke_extensions.hairline) {
            // Can't average a bool. It's true if there's any hairlines in the selection.
            style_res->stroke_extensions.hairline = true;
        }

        if (n_stroked > 0 && prev_hairline != style->stroke_extensions.hairline) {
            same_sw = false;
        }
        prev_hairline = style->stroke_extensions.hairline;

        Geom::Affine i2d = item->i2dt_affine();
        double sw = style->stroke_width.computed * i2d.descrim();

        if (!std::isnan(sw)) {
            if (prev_sw != -1 && fabs(sw - prev_sw) > 1e-3)
                same_sw = false;
            prev_sw = sw;

            avgwidth += sw;
            n_stroked ++;
        } else if (style->stroke_extensions.hairline) {
            n_stroked ++;
        }
    }

    if (n_stroked > 1)
        avgwidth /= (n_stroked);

    style_res->stroke_width.computed = avgwidth;
    style_res->stroke_width.set = true;
    style_res->stroke.noneSet = noneSet; // Will only be true if none of the selected objects has it's stroke set.

    if (n_stroked == 0) {
        return QUERY_STYLE_NOTHING;
    } else if (n_stroked == 1) {
        return QUERY_STYLE_SINGLE;
    } else {
        if (same_sw)
            return QUERY_STYLE_MULTIPLE_SAME;
        else
            return QUERY_STYLE_MULTIPLE_AVERAGED;
    }
}

/**
 * Write to style_res the average miter limit of a list of objects.
 */
int
objects_query_miterlimit (const std::vector<SPItem*> &objects, SPStyle *style_res)
{
    if (objects.empty()) {
        /* No objects, set empty */
        return QUERY_STYLE_NOTHING;
    }

    gdouble avgml = 0.0;
    int n_stroked = 0;

    gdouble prev_ml = -1;
    bool same_ml = true;

    for (auto obj : objects) {
        if (!obj) {
            continue;
        }
        SPStyle *style = obj->style;
        if (!style) {
            continue;
        }

        if ( style->stroke.isNone() ) {
            continue;
        }

        n_stroked ++;

        if (prev_ml != -1 && fabs(style->stroke_miterlimit.value - prev_ml) > 1e-3) {
            same_ml = false;
        }
        prev_ml = style->stroke_miterlimit.value;

        avgml += style->stroke_miterlimit.value;
    }

    if (n_stroked > 1) {
        avgml /= (n_stroked);
    }

    style_res->stroke_miterlimit.value = avgml;
    style_res->stroke_miterlimit.set = true;

    if (n_stroked == 0) {
        return QUERY_STYLE_NOTHING;
    } else if (n_stroked == 1) {
        return QUERY_STYLE_SINGLE;
    } else {
        if (same_ml)
            return QUERY_STYLE_MULTIPLE_SAME;
        else
            return QUERY_STYLE_MULTIPLE_AVERAGED;
    }
}

/**
 * Write to style_res the stroke cap of a list of objects.
 */
int
objects_query_strokecap (const std::vector<SPItem*> &objects, SPStyle *style_res)
{
    if (objects.empty()) {
        /* No objects, set empty */
        return QUERY_STYLE_NOTHING;
    }

    auto prev_cap = SP_STROKE_LINECAP_BUTT;
    bool same_cap = true;
    int n_stroked = 0;

    for (auto obj : objects) {
        if (!obj) {
            continue;
        }
        SPStyle *style = obj->style;
        if (!style) {
            continue;
        }

        if ( style->stroke.isNone() ) {
            continue;
        }

        n_stroked ++;

        if (n_stroked > 1 && style->stroke_linecap.value != prev_cap)
            same_cap = false;
        prev_cap = style->stroke_linecap.value;
    }

    style_res->stroke_linecap.value = prev_cap;
    style_res->stroke_linecap.set = true;

    if (n_stroked == 0) {
        return QUERY_STYLE_NOTHING;
    } else if (n_stroked == 1) {
        return QUERY_STYLE_SINGLE;
    } else {
        if (same_cap)
            return QUERY_STYLE_MULTIPLE_SAME;
        else
            return QUERY_STYLE_MULTIPLE_DIFFERENT;
    }
}

/**
 * Write to style_res the stroke join of a list of objects.
 */
int
objects_query_strokejoin (const std::vector<SPItem*> &objects, SPStyle *style_res)
{
    if (objects.empty()) {
        /* No objects, set empty */
        return QUERY_STYLE_NOTHING;
    }

    auto prev_join = SP_STROKE_LINEJOIN_MITER;
    bool same_join = true;
    int n_stroked = 0;

    for (auto obj : objects) {
        if (!obj) {
            continue;
        }
        SPStyle *style = obj->style;
        if (!style) {
            continue;
        }

        if ( style->stroke.isNone() ) {
            continue;
        }

        n_stroked ++;

        if (n_stroked > 1 && style->stroke_linejoin.value != prev_join) {
            same_join = false;
        }
        prev_join = style->stroke_linejoin.value;
    }

    style_res->stroke_linejoin.value = prev_join;
    style_res->stroke_linejoin.set = true;

    if (n_stroked == 0) {
        return QUERY_STYLE_NOTHING;
    } else if (n_stroked == 1) {
        return QUERY_STYLE_SINGLE;
    } else {
        if (same_join)
            return QUERY_STYLE_MULTIPLE_SAME;
        else
            return QUERY_STYLE_MULTIPLE_DIFFERENT;
    }
}

/**
 * Write to style_res the paint order of a list of objects.
 */
int
objects_query_paintorder (const std::vector<SPItem*> &objects, SPStyle *style_res)
{
    if (objects.empty()) {
        /* No objects, set empty */
        return QUERY_STYLE_NOTHING;
    }

    std::string prev_order;
    bool same_order = true;
    int n_order = 0;

    for (auto obj : objects) {
        if (!obj) {
            continue;
        }
        SPStyle *style = obj->style;
        if (!style) {
            continue;
        }

        if ( style->stroke.isNone() ) {
            continue;
        }

        n_order ++;

        if (style->paint_order.set) {
            if (!prev_order.empty() && prev_order.compare( style->paint_order.value ) != 0) {
                same_order = false;
            }
            prev_order = style->paint_order.value;
        }
    }


    g_free( style_res->paint_order.value );
    style_res->paint_order.value= g_strdup( prev_order.c_str() );
    style_res->paint_order.set = true;

    if (n_order == 0) {
        return QUERY_STYLE_NOTHING;
    } else if (n_order == 1) {
        return QUERY_STYLE_SINGLE;
    } else {
        if (same_order)
            return QUERY_STYLE_MULTIPLE_SAME;
        else
            return QUERY_STYLE_MULTIPLE_DIFFERENT;
    }
}

/**
 * Write to style_res the average font size and spacing of objects.
 */
int
objects_query_fontnumbers (const std::vector<SPItem*> &objects, SPStyle *style_res)
{
    bool different = false;
    bool different_lineheight = false;
    bool different_lineheight_unit = false;

    double size = 0;
    double letterspacing = 0;
    double wordspacing = 0;
    double lineheight = 0;
    bool letterspacing_normal = false;
    bool wordspacing_normal = false;
    bool lineheight_normal = false;
    bool lineheight_unit_proportional = false;
    bool lineheight_unit_absolute = false;
    bool lineheight_set = false; // Set true if any object has lineheight set.

    double size_prev = 0;
    double letterspacing_prev = 0;
    double wordspacing_prev = 0;
    double lineheight_prev = 0;
    int  lineheight_unit_prev = -1;

    int texts = 0;
    int no_size = 0;

    for (auto obj : objects) {
        if (!isTextualItem(obj)) {
            continue;
        }

        SPStyle *style = obj->style;
        if (!style) {
            continue;
        }

        texts ++;
        auto item = obj;
        g_assert(item != nullptr);

        // Quick way of getting document scale. Should be same as:
        // item->document->getDocumentScale().Affine().descrim()
        double doc_scale = Geom::Affine(item->i2dt_affine()).descrim();

        double dummy = style->font_size.computed * doc_scale;
        if (!std::isnan(dummy)) {
            size += dummy; /// \todo FIXME: we assume non-% units here
        } else {
            no_size++;
        }

        if (style->letter_spacing.normal) {
            if (!different && (letterspacing_prev == 0 || letterspacing_prev == letterspacing)) {
                letterspacing_normal = true;
            }
        } else {
            letterspacing += style->letter_spacing.computed * doc_scale;; /// \todo FIXME: we assume non-% units here
            letterspacing_normal = false;
        }

        if (style->word_spacing.normal) {
            if (!different && (wordspacing_prev == 0 || wordspacing_prev == wordspacing)) {
                wordspacing_normal = true;
            }
        } else {
            wordspacing += style->word_spacing.computed * doc_scale; /// \todo FIXME: we assume non-% units here
            wordspacing_normal = false;
        }

        // If all line spacing units the same, use that (average line spacing).
        // Else if all line spacings absolute, use 'px' (average line spacing).
        // Else if all line spacings proportional, use % (average line spacing).
        // Else use default.
        double lineheight_current;
        int    lineheight_unit_current;
        if (style->line_height.normal) {
            lineheight_current = Inkscape::Text::Layout::LINE_HEIGHT_NORMAL;
            lineheight_unit_current = SP_CSS_UNIT_NONE;
            if (!different_lineheight &&
                (lineheight_prev == 0 || lineheight_prev == lineheight_current))
                lineheight_normal = true;
        } else if (style->line_height.unit == SP_CSS_UNIT_NONE ||
                   style->line_height.unit == SP_CSS_UNIT_PERCENT ||
                   style->line_height.unit == SP_CSS_UNIT_EM ||
                   style->line_height.unit == SP_CSS_UNIT_EX ||
                   style->font_size.computed == 0) {
            lineheight_current = style->line_height.value;
            lineheight_unit_current = style->line_height.unit;
            lineheight_unit_proportional = true;
            lineheight_normal = false;
            lineheight += lineheight_current;
        } else {
            // Always 'px' internally
            lineheight_current = style->line_height.computed;
            lineheight_unit_current = style->line_height.unit;
            lineheight_unit_absolute = true;
            lineheight_normal = false;
            lineheight += lineheight_current * doc_scale;
        }
        if (style->line_height.set) {
            lineheight_set = true;
        }

        if ((size_prev != 0 && style->font_size.computed != size_prev) ||
            (letterspacing_prev != 0 && style->letter_spacing.computed != letterspacing_prev) ||
            (wordspacing_prev != 0 && style->word_spacing.computed != wordspacing_prev)) {
            different = true;
        }

        if (lineheight_prev != 0 && lineheight_current != lineheight_prev) {
            different_lineheight = true;
        }

        if (lineheight_unit_prev != -1 && lineheight_unit_current != lineheight_unit_prev) {
            different_lineheight_unit = true;
        }

        size_prev = style->font_size.computed;
        letterspacing_prev = style->letter_spacing.computed;
        wordspacing_prev = style->word_spacing.computed;
        lineheight_prev = lineheight_current;
        lineheight_unit_prev = lineheight_unit_current;

        // FIXME: we must detect MULTIPLE_DIFFERENT for these too
        style_res->text_anchor.computed = style->text_anchor.computed;
    }

    if (texts == 0)
        return QUERY_STYLE_NOTHING;

    if (texts > 1) {
        if (texts - no_size > 0) {
            size /= (texts - no_size);
        }
        letterspacing /= texts;
        wordspacing /= texts;
        lineheight /= texts;
    }

    style_res->font_size.computed = size;
    style_res->font_size.type = SP_FONT_SIZE_LENGTH;

    style_res->letter_spacing.normal = letterspacing_normal;
    style_res->letter_spacing.computed = letterspacing;

    style_res->word_spacing.normal = wordspacing_normal;
    style_res->word_spacing.computed = wordspacing;

    style_res->line_height.normal = lineheight_normal;
    style_res->line_height.computed = lineheight;
    style_res->line_height.value = lineheight;
    if (different_lineheight_unit) {
        if (lineheight_unit_absolute && !lineheight_unit_proportional) {
            // Mixture of absolute units
            style_res->line_height.unit = SP_CSS_UNIT_PX;
        } else {
            // Mixture of relative units
            style_res->line_height.unit = SP_CSS_UNIT_PERCENT;
        }
        if (lineheight_unit_absolute && lineheight_unit_proportional) {
            // Mixed types of units, fallback to default
            style_res->line_height.computed = Inkscape::Text::Layout::LINE_HEIGHT_NORMAL * 100.0;
            style_res->line_height.value    = Inkscape::Text::Layout::LINE_HEIGHT_NORMAL * 100.0;
        }
    } else {
        // Same units.
        if (lineheight_unit_prev != -1) {
            style_res->line_height.unit = lineheight_unit_prev;
        } else {
            // No text object... use default.
            style_res->line_height.unit = SP_CSS_UNIT_NONE;
            style_res->line_height.computed = Inkscape::Text::Layout::LINE_HEIGHT_NORMAL;
            style_res->line_height.value    = Inkscape::Text::Layout::LINE_HEIGHT_NORMAL;
        }
    }

    // Used by text toolbar unset 'line-height'
    style_res->line_height.set = lineheight_set;

    if (texts > 1) {
        if (different || different_lineheight) {
            return QUERY_STYLE_MULTIPLE_AVERAGED;
        } else {
            return QUERY_STYLE_MULTIPLE_SAME;
        }
    } else {
        return QUERY_STYLE_SINGLE;
    }
}

/**
 * Write to style_res the average font style of objects.
 */
int
objects_query_fontstyle (const std::vector<SPItem*> &objects, SPStyle *style_res)
{
    bool different = false;
    bool set = false;

    int texts = 0;

    for (auto obj : objects) {
        if (!isTextualItem(obj)) {
            continue;
        }

        SPStyle *style = obj->style;
        if (!style) {
            continue;
        }

        texts ++;

        if (set &&
            ( ( style_res->font_weight.computed    != style->font_weight.computed  ) ||
              ( style_res->font_style.computed     != style->font_style.computed   ) ||
              ( style_res->font_stretch.computed   != style->font_stretch.computed ) ||
              ( style_res->font_variant.computed   != style->font_variant.computed ) ||
              ( style_res->font_variation_settings != style->font_variation_settings ) ) ) {
            different = true;  // different styles
        }

        set = true;
        style_res->font_weight.value = style_res->font_weight.computed = style->font_weight.computed;
        style_res->font_style.value = style_res->font_style.computed = style->font_style.computed;
        style_res->font_stretch.value = style_res->font_stretch.computed = style->font_stretch.computed;
        style_res->font_variant.value = style_res->font_variant.computed = style->font_variant.computed;
        style_res->font_variation_settings = style->font_variation_settings;
        style_res->text_align.value = style_res->text_align.computed = style->text_align.computed;
        style_res->font_size.value = style->font_size.value;
        style_res->font_size.unit = style->font_size.unit;
    }

    if (texts == 0 || !set)
        return QUERY_STYLE_NOTHING;

    if (texts > 1) {
        if (different) {
            return QUERY_STYLE_MULTIPLE_DIFFERENT;
        } else {
            return QUERY_STYLE_MULTIPLE_SAME;
        }
    } else {
        return QUERY_STYLE_SINGLE;
    }
}

int
objects_query_fontvariants (const std::vector<SPItem*> &objects, SPStyle *style_res)
{
    bool set = false;

    int texts = 0;

    SPILigatures* ligatures_res = &(style_res->font_variant_ligatures);
    SPINumeric* numeric_res     = &(style_res->font_variant_numeric);
    SPIEastAsian* asian_res     = &(style_res->font_variant_east_asian);

    // Stores 'and' of all values
    ligatures_res->computed = SP_CSS_FONT_VARIANT_LIGATURES_NORMAL;
    int position_computed   = SP_CSS_FONT_VARIANT_POSITION_NORMAL;
    int caps_computed       = SP_CSS_FONT_VARIANT_CAPS_NORMAL;
    numeric_res->computed   = SP_CSS_FONT_VARIANT_NUMERIC_NORMAL;
    asian_res->computed     = SP_CSS_FONT_VARIANT_EAST_ASIAN_NORMAL;

    // Stores only differences
    ligatures_res->value = 0;
    int position_value   = 0;
    int caps_value       = 0;
    numeric_res->value   = 0;
    asian_res->value     = 0;

    for (auto obj : objects) {
        if (!isTextualItem(obj)) {
            continue;
        }

        SPStyle *style = obj->style;
        if (!style) {
            continue;
        }

        texts ++;

        SPILigatures* ligatures_in = &(style->font_variant_ligatures);
        auto*         position_in  = &(style->font_variant_position);
        auto*         caps_in      = &(style->font_variant_caps);
        SPINumeric*   numeric_in   = &(style->font_variant_numeric);
        SPIEastAsian* asian_in     = &(style->font_variant_east_asian);

        // computed stores which bits are on/off, only valid if same between all selected objects.
        // value stores which bits are different between objects. This is a bit of an abuse of
        // the values but then we don't need to add new variables to class.
        if (set) {
            ligatures_res->value  |= (ligatures_res->computed ^ ligatures_in->computed );
            ligatures_res->computed &= ligatures_in->computed;

            position_value |= (position_computed ^ position_in->computed);
            position_computed &= position_in->computed;

            caps_value |= (caps_computed ^ caps_in->computed);
            caps_computed &= caps_in->computed;

            numeric_res->value  |= (numeric_res->computed ^ numeric_in->computed );
            numeric_res->computed &= numeric_in->computed;

            asian_res->value  |= (asian_res->computed ^ asian_in->computed );
            asian_res->computed &= asian_in->computed;

        } else {
            ligatures_res->computed  = ligatures_in->computed;
            position_computed        = position_in->computed;
            caps_computed            = caps_in->computed;
            numeric_res->computed    = numeric_in->computed;
            asian_res->computed      = asian_in->computed;
        }

        set = true;
    }

    // violates enum type safety!
    style_res->font_variant_position.value = static_cast<SPCSSFontVariantPosition>(position_value);
    style_res->font_variant_position.computed = static_cast<SPCSSFontVariantPosition>(position_computed);
    style_res->font_variant_caps.value = static_cast<SPCSSFontVariantCaps>(caps_value);
    style_res->font_variant_caps.computed = static_cast<SPCSSFontVariantCaps>(caps_computed);

    bool different = (style_res->font_variant_ligatures.value  != 0 ||
                      position_value                           != 0 ||
                      caps_value                               != 0 ||
                      style_res->font_variant_numeric.value    != 0 ||
                      style_res->font_variant_east_asian.value != 0);

    if (texts == 0 || !set)
        return QUERY_STYLE_NOTHING;

    if (texts > 1) {
        if (different) {
            return QUERY_STYLE_MULTIPLE_DIFFERENT;
        } else {
            return QUERY_STYLE_MULTIPLE_SAME;
        }
    } else {
        return QUERY_STYLE_SINGLE;
    }
}


/**
 * Write to style_res the average writing modes style of objects.
 */
int
objects_query_writing_modes (const std::vector<SPItem*> &objects, SPStyle *style_res)
{
    bool different = false;
    bool set = false;

    int texts = 0;

    for (auto obj : objects) {
        if (!isTextualItem(obj)) {
            continue;
        }

        SPStyle *style = obj->style;
        if (!style) {
            continue;
        }

        texts ++;

        if (set &&
            ( ( style_res->writing_mode.computed     != style->writing_mode.computed     ) ||
              ( style_res->direction.computed        != style->direction.computed        ) ||
              ( style_res->text_orientation.computed != style->text_orientation.computed ) ) ) {
            different = true;  // different styles
        }

        set = true;
        style_res->writing_mode.computed = style->writing_mode.computed;
        style_res->direction.computed = style->direction.computed;
        style_res->text_orientation.computed = style->text_orientation.computed;
    }

    if (texts == 0 || !set)
        return QUERY_STYLE_NOTHING;

    if (texts > 1) {
        if (different) {
            return QUERY_STYLE_MULTIPLE_DIFFERENT;
        } else {
            return QUERY_STYLE_MULTIPLE_SAME;
        }
    } else {
        return QUERY_STYLE_SINGLE;
    }
}

int
objects_query_fontfeaturesettings (const std::vector<SPItem*> &objects, SPStyle *style_res)
{
    bool different = false;
    int texts = 0;

    style_res->font_feature_settings.clear();

    for (auto obj : objects) {
        // std::cout << "  " << reinterpret_cast<SPObject*>(i->data)->getId() << std::endl;
        if (!isTextualItem(obj)) {
            continue;
        }

        SPStyle *style = obj->style;
        if (!style) {
            continue;
        }

        texts ++;

        if (style_res->font_feature_settings.set && //
            strcmp(style_res->font_feature_settings.value(),
                   style->font_feature_settings.value())) {
            different = true;  // different fonts
        }

        style_res->font_feature_settings = style->font_feature_settings;
        style_res->font_feature_settings.set = true;
    }

    if (texts == 0 || !style_res->font_feature_settings.set) {
        return QUERY_STYLE_NOTHING;
    }

    if (texts > 1) {
        if (different) {
            return QUERY_STYLE_MULTIPLE_DIFFERENT;
        } else {
            return QUERY_STYLE_MULTIPLE_SAME;
        }
    } else {
        return QUERY_STYLE_SINGLE;
    }
}


/**
 * Write to style_res the baseline numbers.
 */
static int
objects_query_baselines (const std::vector<SPItem*> &objects, SPStyle *style_res)
{
    bool different = false;

    // Only baseline-shift at the moment
    // We will return:
    //   If baseline-shift is same for all objects:
    //     The full baseline-shift data (used for subscripts and superscripts)
    //   If baseline-shift is different:
    //     The average baseline-shift (not implemented at the moment as this is complicated June 2010)
    SPIBaselineShift old;
    old.value = 0.0;
    old.computed = 0.0;

    // double baselineshift = 0.0;
    bool set = false;

    int texts = 0;

    for (auto obj : objects) {
        if (!isTextualItem(obj)) {
            continue;
        }

        SPStyle *style = obj->style;
        if (!style) {
            continue;
        }

        texts ++;

        SPIBaselineShift current;
        if(style->baseline_shift.set) {

            current.set      = style->baseline_shift.set;
            current.inherit  = style->baseline_shift.inherit;
            current.type     = style->baseline_shift.type;
            current.literal  = style->baseline_shift.literal;
            current.value    = style->baseline_shift.value;
            current.computed = style->baseline_shift.computed;

            if( set ) {
                if( current.set      != old.set ||
                    current.inherit  != old.inherit ||
                    current.type     != old.type ||
                    current.literal  != old.literal ||
                    current.value    != old.value ||
                    current.computed != old.computed ) {
                    // Maybe this needs to be better thought out.
                    different = true;
                }
            }

            set = true;

            old.set      = current.set;
            old.inherit  = current.inherit;
            old.type     = current.type;
            old.literal  = current.literal;
            old.value    = current.value;
            old.computed = current.computed;
        }
    }

    if (different || !set ) {
        style_res->baseline_shift.set = false;
        style_res->baseline_shift.computed = 0.0;
    } else {
        style_res->baseline_shift.set      = old.set;
        style_res->baseline_shift.inherit  = old.inherit;
        style_res->baseline_shift.type     = old.type;
        style_res->baseline_shift.literal  = old.literal;
        style_res->baseline_shift.value    = old.value;
        style_res->baseline_shift.computed = old.computed;
    }

    if (texts == 0 || !set)
        return QUERY_STYLE_NOTHING;

    if (texts > 1) {
        if (different) {
            return QUERY_STYLE_MULTIPLE_DIFFERENT;
        } else {
            return QUERY_STYLE_MULTIPLE_SAME;
        }
    } else {
        return QUERY_STYLE_SINGLE;
    }
}

/**
 * Write to style_res the average font family of objects.
 */
int
objects_query_fontfamily (const std::vector<SPItem*> &objects, SPStyle *style_res)
{
    bool different = false;
    int texts = 0;

    style_res->font_family.clear();

    for (auto obj : objects) {
        // std::cout << "  " << reinterpret_cast<SPObject*>(i->data)->getId() << std::endl;
        if (!isTextualItem(obj)) {
            continue;
        }

        SPStyle *style = obj->style;
        if (!style) {
            continue;
        }

        texts ++;

        if (style_res->font_family.set && //
            strcmp(style_res->font_family.value(), style->font_family.value())) {
            different = true;  // different fonts
        }

        style_res->font_family = style->font_family;
        style_res->font_family.set = true;
    }

    if (texts == 0 || !style_res->font_family.set) {
        return QUERY_STYLE_NOTHING;
    }

    if (texts > 1) {
        if (different) {
            return QUERY_STYLE_MULTIPLE_DIFFERENT;
        } else {
            return QUERY_STYLE_MULTIPLE_SAME;
        }
    } else {
        return QUERY_STYLE_SINGLE;
    }
}

static int
objects_query_fontspecification (const std::vector<SPItem*> &objects, SPStyle *style_res)
{
    bool different = false;
    int texts = 0;

    style_res->font_specification.clear();

    for (auto obj : objects) {
        // std::cout << "  " << reinterpret_cast<SPObject*>(i->data)->getId() << std::endl;
        if (!isTextualItem(obj)) {
            continue;
        }

        SPStyle *style = obj->style;
        if (!style) {
            continue;
        }

        texts ++;

        if (style_res->font_specification.set &&
            g_strcmp0(style_res->font_specification.value(), style->font_specification.value())) {
            different = true;  // different fonts
        }

        if (style->font_specification.set) {
            style_res->font_specification = style->font_specification;
            style_res->font_specification.set = true;
        }
    }

    if (texts == 0) {
        return QUERY_STYLE_NOTHING;
    }

    if (texts > 1) {
        if (different) {
            return QUERY_STYLE_MULTIPLE_DIFFERENT;
        } else {
            return QUERY_STYLE_MULTIPLE_SAME;
        }
    } else {
        return QUERY_STYLE_SINGLE;
    }
}

int
objects_query_blend (const std::vector<SPItem*> &objects, SPStyle *style_res)
{
    auto blend = SP_CSS_BLEND_NORMAL;
    auto blend_prev = blend;
    bool same_blend = true;
    guint items = 0;

    for (auto obj : objects) {
        if (!obj) {
            continue;
        }
        SPStyle *style = obj->style;
        if (!style || !obj) {
            continue;
        }

        items++;

        if (style->mix_blend_mode.set) {
            blend = style->mix_blend_mode.value;
        }
        // defaults to blend mode = "normal"
        else {
            if (style->filter.set && style->getFilter()) {
                blend = filter_get_legacy_blend(obj);
            } else {
                blend = SP_CSS_BLEND_NORMAL;
            }
        }

        if (items > 1 && blend_prev != blend)
            same_blend = false;
        blend_prev = blend;
    }

    if (items > 0) {
        style_res->mix_blend_mode.value = blend;
    }

    if (items == 0) {
        return QUERY_STYLE_NOTHING;
    } else if (items == 1) {
        return QUERY_STYLE_SINGLE;
    } else {
        if(same_blend)
            return QUERY_STYLE_MULTIPLE_SAME;
        else
            return QUERY_STYLE_MULTIPLE_DIFFERENT;
    }
}

int objects_query_isolation(const std::vector<SPItem *> &objects, SPStyle *style_res)
{
    auto isolation = SP_CSS_ISOLATION_AUTO;
    auto isolation_prev = isolation;
    bool same_isolation = true;
    guint items = 0;

    for (auto obj : objects) {
        if (!obj) {
            continue;
        }
        SPStyle *style = obj->style;
        if (!style || !obj) {
            continue;
        }

        items++;

        if (style->isolation.set) {
            isolation = style->isolation.value;
        }
        else {
            isolation = SP_CSS_ISOLATION_AUTO;
        }

        if (items > 1 && isolation_prev != isolation)
            same_isolation = false;
        isolation_prev = isolation;
    }

    if (items > 0) {
        style_res->isolation.value = isolation;
    }

    if (items == 0) {
        return QUERY_STYLE_NOTHING;
    } else if (items == 1) {
        return QUERY_STYLE_SINGLE;
    } else {
        if (same_isolation)
            return QUERY_STYLE_MULTIPLE_SAME;
        else
            return QUERY_STYLE_MULTIPLE_DIFFERENT;
    }
}

/**
 * Write to style_res the average blurring of a list of objects.
 */
int
objects_query_blur (const std::vector<SPItem*> &objects, SPStyle *style_res)
{
   if (objects.empty()) {
        /* No objects, set empty */
        return QUERY_STYLE_NOTHING;
    }

    float blur_sum = 0;
    float blur_prev = -1;
    bool same_blur = true;
    guint blur_items = 0;
    guint items = 0;

    for (auto obj : objects) {
        if (!obj) {
            continue;
        }
        SPStyle *style = obj->style;
        if (!style) {
            continue;
        }
        auto item = obj;
        if (!item) {
            continue;
        }

        Geom::Affine i2d = item->i2dt_affine();

        items ++;

        //if object has a filter
        if (style->filter.set && style->getFilter()) {
            //cycle through filter primitives
            for(auto& primitive_obj: style->getFilter()->children) {
                auto primitive = cast<SPFilterPrimitive>(&primitive_obj);
                if (primitive) {

                    //if primitive is gaussianblur
                    auto spblur = cast<SPGaussianBlur>(primitive);
                    if (spblur) {
                        float num = spblur->get_std_deviation().getNumber();
                        float dummy = num * i2d.descrim();
                        if (!std::isnan(dummy)) {
                            blur_sum += dummy;
                            if (blur_prev != -1 && fabs (num - blur_prev) > 1e-2) // rather low tolerance because difference in blur radii is much harder to notice than e.g. difference in sizes
                                same_blur = false;
                            blur_prev = num;
                            //TODO: deal with opt number, for the moment it's not necessary to the ui.
                            blur_items ++;
                        }
                    }
                }
            }
        }
    }

    if (items > 0) {
        if (blur_items > 0)
            blur_sum /= blur_items;
        style_res->filter_gaussianBlur_deviation.value = blur_sum;
    }

    if (items == 0) {
        return QUERY_STYLE_NOTHING;
    } else if (items == 1) {
        return QUERY_STYLE_SINGLE;
    } else {
        if (same_blur)
            return QUERY_STYLE_MULTIPLE_SAME;
        else
            return QUERY_STYLE_MULTIPLE_AVERAGED;
    }
}

/**
 * Query the given list of objects for the given property, write
 * the result to style, return appropriate flag.
 */
int
sp_desktop_query_style_from_list (const std::vector<SPItem*> &list, SPStyle *style, int property)
{
    if (property == QUERY_STYLE_PROPERTY_FILL) {
        return objects_query_fillstroke (list, style, true);
    } else if (property == QUERY_STYLE_PROPERTY_STROKE) {
        return objects_query_fillstroke (list, style, false);

    } else if (property == QUERY_STYLE_PROPERTY_STROKEWIDTH) {
        return objects_query_strokewidth (list, style);
    } else if (property == QUERY_STYLE_PROPERTY_STROKEMITERLIMIT) {
        return objects_query_miterlimit (list, style);
    } else if (property == QUERY_STYLE_PROPERTY_STROKECAP) {
        return objects_query_strokecap (list, style);
    } else if (property == QUERY_STYLE_PROPERTY_STROKEJOIN) {
        return objects_query_strokejoin (list, style);

    } else if (property == QUERY_STYLE_PROPERTY_PAINTORDER) {
        return objects_query_paintorder (list, style);
    } else if (property == QUERY_STYLE_PROPERTY_MASTEROPACITY) {
        return objects_query_opacity (list, style);

    } else if (property == QUERY_STYLE_PROPERTY_FONT_SPECIFICATION) {
        return objects_query_fontspecification (list, style);
    } else if (property == QUERY_STYLE_PROPERTY_FONTFAMILY) {
        return objects_query_fontfamily (list, style);
    } else if (property == QUERY_STYLE_PROPERTY_FONTSTYLE) {
        return objects_query_fontstyle (list, style);
    } else if (property == QUERY_STYLE_PROPERTY_FONTVARIANTS) {
        return objects_query_fontvariants (list, style);
    } else if (property == QUERY_STYLE_PROPERTY_FONTFEATURESETTINGS) {
        return objects_query_fontfeaturesettings (list, style);
    } else if (property == QUERY_STYLE_PROPERTY_FONTNUMBERS) {
        return objects_query_fontnumbers (list, style);
    } else if (property == QUERY_STYLE_PROPERTY_WRITINGMODES) {
        return objects_query_writing_modes (list, style);
    } else if (property == QUERY_STYLE_PROPERTY_BASELINES) {
        return objects_query_baselines (list, style);

    } else if (property == QUERY_STYLE_PROPERTY_BLEND) {
        return objects_query_blend (list, style);
    } else if (property == QUERY_STYLE_PROPERTY_ISOLATION) {
        return objects_query_isolation(list, style);
    } else if (property == QUERY_STYLE_PROPERTY_BLUR) {
        return objects_query_blur (list, style);
    }
    return QUERY_STYLE_NOTHING;
}


/**
 * Query the subselection (if any) or selection on the given desktop for the given property, write
 * the result to style, return appropriate flag.
 */
int
sp_desktop_query_style(SPDesktop *desktop, SPStyle *style, int property)
{
    // Used by text tool and in gradient dragging. See connectQueryStyle.
    int ret = desktop->_query_style_signal.emit(style, property);

    if (ret != QUERY_STYLE_NOTHING)
        return ret; // subselection returned a style, pass it on

    // otherwise, do querying and averaging over selection
    if (auto selection = desktop->getSelection()) {
        std::vector<SPItem *> vec(selection->items().begin(), selection->items().end());
        return sp_desktop_query_style_from_list (vec, style, property);
    }

    return QUERY_STYLE_NOTHING;
}

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

// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * TODO: insert short description here
 *//*
 * Authors: see git history
 *
 * Copyright (C) 2018 Authors
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */
/*
 * attribute-rel-util.h
 *
 *  Created on: Sep 8, 2011
 *      Author: tavmjong
 */

/**
 * Utility functions for cleaning SVG tree of unneeded attributes and style properties.
 */

#include "attribute-rel-util.h"

#include <fstream>
#include <iostream>
#include <sstream>

#include <2geom/path-sink.h>
#include <2geom/svg-path-parser.h>

#include "attribute-rel-css.h"
#include "attribute-rel-svg.h"
#include "preferences.h"
#include "xml/attribute-record.h"

using Inkscape::XML::Node;

/**
 * Get preferences
 */
unsigned int sp_attribute_clean_get_prefs()
{
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    unsigned int flags = 0;
    if (prefs->getBool("/options/svgoutput/incorrect_attributes_warn"))
        flags += SP_ATTRCLEAN_ATTR_WARN;
    if (prefs->getBool("/options/svgoutput/incorrect_attributes_remove") &&
        !prefs->getBool("/options/svgoutput/disable_optimizations"))
        flags += SP_ATTRCLEAN_ATTR_REMOVE;
    if (prefs->getBool("/options/svgoutput/incorrect_style_properties_warn"))
        flags += SP_ATTRCLEAN_STYLE_WARN;
    if (prefs->getBool("/options/svgoutput/incorrect_style_properties_remove") &&
        !prefs->getBool("/options/svgoutput/disable_optimizations"))
        flags += SP_ATTRCLEAN_STYLE_REMOVE;
    if (prefs->getBool("/options/svgoutput/style_defaults_warn"))
        flags += SP_ATTRCLEAN_DEFAULT_WARN;
    if (prefs->getBool("/options/svgoutput/style_defaults_remove") &&
        !prefs->getBool("/options/svgoutput/disable_optimizations"))
        flags += SP_ATTRCLEAN_DEFAULT_REMOVE;

    return flags;
}

/**
 * Remove or warn about inappropriate attributes and useless stype properties.
 * repr: the root node in a document or any other node.
 */
void sp_attribute_clean_tree(Node *repr)
{
    g_return_if_fail(repr != nullptr);

    unsigned int flags = sp_attribute_clean_get_prefs();

    if (flags) {
        sp_attribute_clean_recursive(repr, flags);
    }
}

/**
 * Clean recursively over all elements.
 */
void sp_attribute_clean_recursive(Node *repr, unsigned int flags)
{
    g_return_if_fail(repr != nullptr);

    if (repr->type() == Inkscape::XML::NodeType::ELEMENT_NODE) {
        Glib::ustring element = repr->name();

        // Only clean elements in svg namespace
        if (element.substr(0, 4) == "svg:") {
            sp_attribute_clean_element(repr, flags);
        }
    }

    for (Node *child = repr->firstChild(); child; child = child->next()) {
        // Don't remove default css values if element is in <defs> or is a <symbol>
        Glib::ustring element = child->name();
        unsigned int flags_temp = flags;
        if (element.compare("svg:defs") == 0 || element.compare("svg:symbol") == 0) {
            flags_temp &= ~(SP_ATTRCLEAN_DEFAULT_WARN | SP_ATTRCLEAN_DEFAULT_REMOVE);
        }
        sp_attribute_clean_recursive(child, flags_temp);
    }
}

/**
 * Clean attributes on an element
 */
void sp_attribute_clean_element(Node *repr, unsigned int flags)
{
    g_return_if_fail(repr != nullptr);
    g_return_if_fail(repr->type() == Inkscape::XML::NodeType::ELEMENT_NODE);

    Glib::ustring element = repr->name();
    Glib::ustring id = (repr->attribute("id") == nullptr ? "" : repr->attribute("id"));

    // Clean style: this attribute is unique in that normally we want to change it and not simply
    // delete it.
    sp_attribute_clean_style(repr, flags);

    // Clean attributes
    std::set<Glib::ustring> attributesToDelete;
    for (const auto &iter : repr->attributeList()) {
        Glib::ustring attribute = g_quark_to_string(iter.key);
        // Glib::ustring value = (const char*)iter->value;

        bool is_useful = sp_attribute_check_attribute(element, id, attribute, flags & SP_ATTRCLEAN_ATTR_WARN);
        if (!is_useful && (flags & SP_ATTRCLEAN_ATTR_REMOVE)) {
            attributesToDelete.insert(attribute);
        }
    }

    // Do actual deleting (done after so as not to perturb List iterator).
    for (const auto &iter_d : attributesToDelete) {
        repr->removeAttribute(iter_d);
    }
}

/**
 * Clean CSS style on an element.
 */
void sp_attribute_clean_style(Node *repr, unsigned int flags)
{
    g_return_if_fail(repr != nullptr);
    g_return_if_fail(repr->type() == Inkscape::XML::NodeType::ELEMENT_NODE);

    // Find element's style
    SPCSSAttr *css = sp_repr_css_attr(repr, "style");
    sp_attribute_clean_style(repr, css, flags);

    // Convert css node's properties data to string and set repr node's attribute "style" to that string.
    // sp_repr_css_set( repr, css, "style"); // Don't use as it will cause loop.
    Glib::ustring value;
    sp_repr_css_write_string(css, value);
    repr->setAttributeOrRemoveIfEmpty("style", value);

    sp_repr_css_attr_unref(css);
}

/**
 * Clean CSS style on an element.
 */
Glib::ustring sp_attribute_clean_style(Node *repr, gchar const *string, unsigned int flags)
{
    g_return_val_if_fail(repr != nullptr, "");
    g_return_val_if_fail(repr->type() == Inkscape::XML::NodeType::ELEMENT_NODE, "");

    SPCSSAttr *css = sp_repr_css_attr_new();
    sp_repr_css_attr_add_from_string(css, string);
    sp_attribute_clean_style(repr, css, flags);
    Glib::ustring string_cleaned;
    sp_repr_css_write_string(css, string_cleaned);

    sp_repr_css_attr_unref(css);

    return string_cleaned;
}

/**
 * Clean CSS style on an element.
 *
 * 1. Is a style property appropriate on the given element?
 *    e.g, font-size is useless on <svg:rect>
 * 2. Is the value of the style property useful?
 *    Is it the same as the parent and it inherits?
 *    Is it the default value (and the property on the parent is not set or does not inherit)?
 */
void sp_attribute_clean_style(Node *repr, SPCSSAttr *css, unsigned int flags)
{
    g_return_if_fail(repr != nullptr);
    g_return_if_fail(css != nullptr);

    Glib::ustring element = repr->name();
    Glib::ustring id = (repr->attribute("id") == nullptr ? "" : repr->attribute("id"));

    // Find parent's style, including properties that are inherited.
    // Note, a node may not have a parent if it has not yet been added to tree.
    SPCSSAttr *css_parent = nullptr;
    if (repr->parent())
        css_parent = sp_repr_css_attr_inherited(repr->parent(), "style");

    // Loop over all properties in "style" node, keeping track of which to delete.
    std::set<Glib::ustring> toDelete;
    for (const auto &iter : css->attributeList()) {
        Glib::ustring property = g_quark_to_string(iter.key);
        gchar const *value = iter.value;

        // Check if a property is applicable to an element (i.e. is font-family useful for a <rect>?).
        if (!SPAttributeRelCSS::findIfValid(property, element)) {
            if (flags & SP_ATTRCLEAN_STYLE_WARN) {
                g_warning("<%s id=\"%s\">: CSS Style property: \"%s\" is inappropriate.", element.c_str(), id.c_str(),
                          property.c_str());
            }
            if (flags & SP_ATTRCLEAN_STYLE_REMOVE) {
                toDelete.insert(property);
            }
            continue;
        }

        // Find parent value for same property (property)
        gchar const *value_p = nullptr;
        if (css_parent != nullptr) {
            for (const auto &iter_p : css_parent->attributeList()) {
                gchar const *property_p = g_quark_to_string(iter_p.key);

                if (!g_strcmp0(property.c_str(), property_p)) {
                    value_p = iter_p.value;
                    break;
                }
            }
        }

        // If parent has same property value and property is inherited, mark for deletion.
        if (!g_strcmp0(value, value_p) && SPAttributeRelCSS::findIfInherit(property)) {
            if (flags & SP_ATTRCLEAN_DEFAULT_WARN) {
                g_warning("<%s id=\"%s\">: CSS Style property: \"%s\" has same value as parent (%s).", element.c_str(),
                          id.c_str(), property.c_str(), value);
            }
            if (flags & SP_ATTRCLEAN_DEFAULT_REMOVE) {
                toDelete.insert(property);
            }
            continue;
        }

        // If property value is same as default and the parent value not set or property is not inherited,
        // mark for deletion.
        if (SPAttributeRelCSS::findIfDefault(property, value) &&
            ((value_p == nullptr) || !SPAttributeRelCSS::findIfInherit(property))) {
            if (flags & SP_ATTRCLEAN_DEFAULT_WARN) {
                g_warning("<%s id=\"%s\">: CSS Style property: \"%s\" with default value (%s) not needed.",
                          element.c_str(), id.c_str(), property.c_str(), value);
            }
            if (flags & SP_ATTRCLEAN_DEFAULT_REMOVE) {
                toDelete.insert(property);
            }
            continue;
        }

    } // End loop over style properties

    // Delete unneeded style properties. Do this at the end so as to not perturb List iterator.
    for (const auto &iter_d : toDelete) {
        sp_repr_css_set_property(css, iter_d.c_str(), nullptr);
    }
}

/**
 * Remove CSS style properties with default values.
 */
void sp_attribute_purge_default_style(SPCSSAttr *css, unsigned int flags)
{
    g_return_if_fail(css != nullptr);

    // Loop over all properties in "style" node, keeping track of which to delete.
    std::set<Glib::ustring> toDelete;
    for (const auto &iter : css->attributeList()) {
        Glib::ustring property = g_quark_to_string(iter.key);
        gchar const *value = iter.value;

        // If property value is same as default mark for deletion.
        if (SPAttributeRelCSS::findIfDefault(property, value)) {
            if (flags & SP_ATTRCLEAN_DEFAULT_WARN) {
                g_warning("Preferences CSS Style property: \"%s\" with default value (%s) not needed.",
                          property.c_str(), value);
            }
            if (flags & SP_ATTRCLEAN_DEFAULT_REMOVE) {
                toDelete.insert(property);
            }
            continue;
        }

    } // End loop over style properties

    // Delete unneeded style properties. Do this at the end so as to not perturb List iterator.
    for (const auto &iter_d : toDelete) {
        sp_repr_css_set_property(css, iter_d.c_str(), nullptr);
    }
}

/**
 * Check one attribute on an element
 */
bool sp_attribute_check_attribute(Glib::ustring const &element, Glib::ustring const &id, Glib::ustring const &attribute,
                                  bool warn)
{
    bool is_useful = true;

    if (SPAttributeRelCSS::findIfProperty(attribute)) {
        // First check if it is a presentation attribute. Presentation attributes can be applied to
        // any element.  At the moment, we are only going to check if it is a possibly useful
        // attribute. Note, we don't explicitly check against the list of elements where presentation
        // attributes are allowed (See SVG1.1 spec, Appendix M.2).
        if (!SPAttributeRelCSS::findIfValid(attribute, element)) {
            // Non-useful presentation attribute on SVG <element>
            if (warn) {
                g_warning("<%s id=\"%s\">: Non-useful presentation attribute: \"%s\" found.", element.c_str(),
                          id.c_str(), attribute.c_str());
            }
            is_useful = false;
        }

    } else {
        // Second check if it is a valid attribute
        if (!SPAttributeRelSVG::findIfValid(attribute, element)) {
            // Invalid attribute on SVG <element>
            if (warn) {
                g_warning("<%s id=\"%s\">: Invalid attribute: \"%s\" found.", element.c_str(), id.c_str(),
                          attribute.c_str());
            }
            is_useful = false;
        }
    }

    return is_useful;
}

bool sp_is_valid_svg_path_d(Glib::ustring const &d)
{
    /** A PathSink going straight to /dev/null */
    class PathBlackHole final : public Geom::PathSink
    {
        using Geom::PathSink::feed;
        void moveTo(Geom::Point const &) final {}
        void lineTo(Geom::Point const &) final {}
        void curveTo(Geom::Point const &, Geom::Point const &, Geom::Point const &) final {}
        void quadTo(Geom::Point const &, Geom::Point const &) final {}
        void arcTo(Geom::Coord, Geom::Coord, Geom::Coord, bool, bool, Geom::Point const &) final {}
        void closePath() final {}
        void flush() final {}
        void feed(Geom::Curve const &, bool) final {}
        void feed(Geom::Path const &) final {}
        void feed(Geom::PathVector const &) final {}
        void feed(Geom::Rect const &) final {}
        void feed(Geom::Circle const &) final {}
        void feed(Geom::Ellipse const &) final {}
    } dev_null;

    auto validator = Geom::SVGPathParser(dev_null);
    try {
        validator.parse(static_cast<std::string>(d));
    } catch (Geom::SVGPathParseError &) {
        return false;
    }
    return true;
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

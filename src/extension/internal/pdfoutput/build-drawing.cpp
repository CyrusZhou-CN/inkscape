// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Provide a capypdf interface that understands 2geom, styles, etc.
 *
 * Authors:
 *   Martin Owens <doctormo@geek-2.com>
 *
 * Copyright (C) 2024 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "build-drawing.h"

#include "build-document.h"
#include "build-page.h"
#include "build-patterns.h"
#include "build-text.h"
#include "object/sp-anchor.h"
#include "object/sp-flowtext.h"
#include "object/sp-image.h"
#include "object/sp-item.h"
#include "object/sp-marker.h"
#include "object/sp-mask.h"
#include "object/sp-page.h"
#include "object/sp-pattern.h"
#include "object/sp-root.h"
#include "object/sp-symbol.h"
#include "object/sp-text.h"
#include "object/sp-use.h"
#include "object/uri.h"
#include "style.h"

namespace Inkscape::Extension::Internal::PdfBuilder {

/**
 * Set the transformation matrix for this context Group.
 */
void DrawContext::set_matrix(Geom::Affine const &tr)
{
    _ctx.set_group_matrix(tr[0], tr[1], tr[2], tr[3], tr[4], tr[5]);
}

/**
 * Add a transform to the current context stream.
 */
void DrawContext::transform(Geom::Affine const &tr)
{
    if (tr != Geom::identity()) {
        _ctx.cmd_cm(tr[0], tr[1], tr[2], tr[3], tr[4], tr[5]);
    }
}

GroupContext::GroupContext(Document &doc, Geom::OptRect const &clip, bool soft_mask)
    : DrawContext(doc,
                  doc.generator().new_transparency_group_context(
                      // CapyPDF is very strict about clipping regions being valid. We want to
                      // be more flexible to allow our painting flow to be less repetative
                      // So we check the clip before constructing the new capypdf context.
                      clip ? clip->left() : 0, clip ? clip->top() : 0,
                      clip ? std::max(clip->right(), clip->left() + 0.0001) : 1,
                      clip ? std::max(clip->bottom(), clip->top() + 0.0001) : 1),
                  soft_mask)
{
    capypdf::TransparencyGroupProperties props;
    if (soft_mask) {
        props.set_CS(CAPY_DEVICE_CS_GRAY);
    } else {
        // Do groups have a color space?
    }
    props.set_I(true);  // Isolate from the document
    props.set_K(false); // Do not knock out
    _ctx.set_transparency_group_properties(props);
}

/**
 * Paint the given object into the given context, making groups if needed.
 */
void DrawContext::paint_item(SPItem const *item, Geom::Affine const &tr, SPStyle const *context_style)
{
    // Special exception, return without drawing anything when using LaTeX
    if (!_doc.get_text_enabled() && (is<SPText>(item) || is<SPFlowtext>(item))) {
        return;
    }

    auto style_map = _doc.paint_memory().get_ifset(item->style);
    auto style_scope = _doc.paint_memory().remember(style_map);
    auto resolution = item->isFiltered() ? _doc.get_filter_resolution() : 0;

    bool isolate = tr != Geom::identity() || !style_map.empty() || true; // has_pattern || has_opacity etc etc
    if (isolate) {
        // Isolate everything in the item
        _ctx.cmd_q();

        if (!resolution) {
            transform(tr);
            // Set styles for cascading
            set_paint_style(style_map, item->style, context_style);
        }

        // This text is not affected by the get_text_enabled option.
        if (auto text_clip = item->getClipTextObject()) {
            clip_text_layout(text_clip->layout);
        } else {
            set_clip_path(item->getClipPathVector(), item->style);
        }
    }

    // These styles are never cascaded because of the complexity in PDF transparency groups.
    if (!resolution && !is<SPGroup>(item) && !_soft_mask) {
        if (auto gsid = _doc.get_shape_graphics_state(item->style)) {
            _ctx.cmd_gs(*gsid);
        }
    }

    if (resolution) {
        // Turn the item into a raster for the PDF
        paint_item_to_raster(item, tr, resolution, true);
    } else if (auto shape = cast<SPShape>(item)) {
        if (shape->curve() && !shape->curve()->get_pathvector().empty()) {
            paint_shape(shape, context_style);
        }
    } else if (auto use = cast<SPUse>(item)) {
        paint_item_clone(use, context_style);
    } else if (auto text = cast<SPText>(item)) {
        paint_text_layout(text->layout, context_style);
    } else if (auto flowtext = cast<SPFlowtext>(item)) {
        // sp_flowtext_render(flowtext);
    } else if (auto image = cast<SPImage>(item)) {
        paint_raster(image);
    } else if (auto group = cast<SPGroup>(item)) { // SPSymbol, SPRoot, SPMarker

        // Optional Content Group tracks layers
        bool has_ocg = false;
        if (group->isLayer()) {
            if (auto label = group->label()) {
                auto ocg = capypdf::OptionalContentGroup(label);
                start_ocg(_doc._gen.add_optional_content_group(ocg));
                has_ocg = true;
            }
        }

        paint_item_group(group, context_style);

        if (has_ocg) {
            end_ocg();
        }
    } else {
        g_warning("Unknown object: %s", get_id(item).c_str());
    }
    if (isolate) {
        _ctx.cmd_Q();
    }
}

void DrawContext::paint_item_group(SPGroup const *group, SPStyle const *context_style)
{
    // Render children in the group
    for (auto &obj : group->children) {
        if (auto child_item = cast<SPItem>(&obj)) {
            // Calculate a soft mask
            // const cast because mask references are not created and tracked properly.
            std::optional<CapyPDF_TransparencyGroupId> mask_id;
            if (auto ref = const_cast<SPItem *>(child_item)->getMaskRef().getObject()) {
                mask_id = _doc.mask_to_transparency_group(ref, child_item->transform);
            }

            // Find out if this object is a source for a clone
            std::vector<SPObject *> links;
            child_item->getLinkedRecursive(links, SPObject::LinkedObjectNature::DEPENDENT);

            // Try not creating groups for *every* shape if they don't need them.
            if (!is<SPGroup>(child_item) && !mask_id && links.empty() && !style_needs_group(child_item->style)) {
                // Paint the child-shape directly
                paint_item(child_item, child_item->transform, context_style);

            } else if (auto item_id = _doc.item_to_transparency_group(child_item, context_style)) {
                // Each reused transparency group has to re-specify it's transform and opacity settings
                // since PDF applies properties from the outside of the group being drawn.
                paint_group(*item_id, child_item->style, Geom::identity(), mask_id);
            }
        }
    }
}

/**
 * Paint the given clone object, finding or generating a transparency group from it.
 */
void DrawContext::paint_item_clone(SPUse const *use, SPStyle const *context_style)
{
    // Children contains a copy of the clone with the right context style
    // if (auto child_item = use->get_original()) {
    for (auto &child_obj : use->children) {
        if (auto child_item = cast<SPItem>(&child_obj)) {
            // Consume the SPUse object as the context style
            if (auto item_id = _doc.item_to_transparency_group(child_item, use->style)) {
                paint_group(*item_id, child_item->style, Geom::Translate(use->x.computed, use->y.computed));
            } else {
                g_warning("Couldn't paint clone: '%s'", get_id(use).c_str());
            }
        }
    }
}

/**
 * Paint a child group at the requested location
 */
void DrawContext::paint_group(CapyPDF_TransparencyGroupId child_id, SPStyle const *style, Geom::Affine const &tr,
                              std::optional<CapyPDF_TransparencyGroupId> soft_mask)
{
    auto gsid = _doc.get_group_graphics_state(style, soft_mask);

    if (gsid || tr != Geom::identity()) {
        _ctx.cmd_q();
    }

    transform(tr);
    if (gsid) {
        _ctx.cmd_gs(*gsid);
    }

    _ctx.cmd_Do(child_id);

    if (gsid || tr != Geom::identity()) {
        _ctx.cmd_Q();
    }
}

/**
 * Paint a single shape path
 *
 * @param shape - The shape we want to paint
 */
void DrawContext::paint_shape(SPShape const *shape, SPStyle const *context_style)
{
    auto const style = shape->style;

    bool evenodd = style->fill_rule.computed == SP_WIND_RULE_EVENODD;
    for (auto layer : get_paint_layers(style, context_style)) {
        switch (layer) {
            case PAINT_FILLSTROKE:
                if (set_shape(shape)) {
                    if (evenodd) {
                        _ctx.cmd_bstar();
                    } else {
                        _ctx.cmd_b();
                    }
                } else { // Not closed path
                    if (evenodd) {
                        _ctx.cmd_Bstar();
                    } else {
                        _ctx.cmd_B();
                    }
                }
                break;
            case PAINT_FILL:
                // Fill only without stroke, either because it's only fill, or not in order
                set_shape(shape);

                if (evenodd) {
                    _ctx.cmd_fstar();
                } else {
                    _ctx.cmd_f();
                }
                break;
            case PAINT_STROKE:
                // Stroke only without fill, either because it's only stroke, or not in order
                if (set_shape(shape)) {
                    _ctx.cmd_s();
                } else { // Not closed path
                    _ctx.cmd_S();
                }
                break;
            case PAINT_MARKERS:
                // Markers can still be visible is no_stroke is true
                for (auto [loc, marker, tr] : shape->get_markers()) {
                    // Isolate each marker render
                    if (auto item_id = _doc.item_to_transparency_group(marker, style, _soft_mask)) {
                        // We don't pass on the style at this stage
                        paint_group(*item_id, nullptr, tr);
                    }
                }
                break;
        }
    }
}

/**
 * Apply the clip path to the existing context.
 */
void DrawContext::set_clip_path(std::optional<Geom::PathVector> clip, SPStyle *style)
{
    if (clip) {
        set_shape_pathvector(*clip);
        // Default to NONZERO when style is nullptr.
        if (style && style->clip_rule.computed == SP_WIND_RULE_EVENODD) {
            _ctx.cmd_W();
        } else {
            _ctx.cmd_Wstar();
        }
        _ctx.cmd_n();
    }
}

/**
 * Apply the clipping rectangle with a NONZERO fill rule.
 */
void DrawContext::set_clip_rectangle(Geom::OptRect const &rect)
{
    if (rect) {
        set_clip_path(Geom::PathVector(Geom::Path(*rect)));
    }
}

void DrawContext::start_ocg(CapyPDF_OptionalContentGroupId ocgid)
{
    _ctx.cmd_BDC(ocgid);
}

void DrawContext::end_ocg()
{
    _ctx.cmd_EMC();
}

ItemContext::ItemContext(Document &doc, SPItem const *item)
    : GroupContext(doc, item ? item->visualBounds(Geom::identity(), true, false, true) : Geom::OptRect(), false)
    , _item{item}
{}

bool ItemContext::is_valid() const
{
    return !_item->isHidden();
}

ItemCacheKey ItemContext::cache_key() const
{
    return {get_id(_item), "", ""};
}

void ItemContext::paint()
{
    paint_item(_item);
}

} // namespace Inkscape::Extension::Internal::PdfBuilder

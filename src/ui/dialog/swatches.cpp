// SPDX-License-Identifier: GPL-2.0-or-later
/* Authors:
 *   Jon A. Cruz
 *   John Bintz
 *   Abhishek Sharma
 *   PBS <pbs3141@gmail.com>
 *
 * Copyright (C) 2005 Jon A. Cruz
 * Copyright (C) 2008 John Bintz
 * Copyright (C) 2022 PBS
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "swatches.h"

#include <algorithm>
#include <glibmm/i18n.h>

#include "document.h"
#include "object/sp-defs.h"
#include "style.h"
#include "desktop-style.h"
#include "object/sp-gradient-reference.h"

#include "inkscape-preferences.h"
#include "widgets/paintdef.h"
#include "ui/widget/color-palette.h"
#include "ui/dialog/global-palettes.h"
#include "ui/dialog/color-item.h"

namespace Inkscape {
namespace UI {
namespace Dialog {

/*
 * Lifecycle
 */

SwatchesPanel::SwatchesPanel(char const *prefsPath)
    : DialogBase(prefsPath, "Swatches")
{
    _palette = Gtk::make_managed<Inkscape::UI::Widget::ColorPalette>();
    pack_start(*_palette);
    update_palettes();

    bool embedded = _prefs_path != "/dialogs/swatches";
    _palette->set_compact(embedded);

    Inkscape::Preferences* prefs = Inkscape::Preferences::get();

    index = name_to_index(prefs->getString(_prefs_path + "/palette"));

    // restore palette settings
    _palette->set_tile_size(prefs->getInt(_prefs_path + "/tile_size", 16));
    _palette->set_aspect(prefs->getDoubleLimited(_prefs_path + "/tile_aspect", 0.0, -2, 2));
    _palette->set_tile_border(prefs->getInt(_prefs_path + "/tile_border", 1));
    _palette->set_rows(prefs->getInt(_prefs_path + "/rows", 1));
    _palette->enable_stretch(prefs->getBool(_prefs_path + "/tile_stretch", false));
    _palette->set_large_pinned_panel(embedded && prefs->getBool(_prefs_path + "/enlarge_pinned", true));
    _palette->enable_labels(!embedded && prefs->getBool(_prefs_path + "/show_labels", true));

    // save settings when they change
    _palette->get_settings_changed_signal().connect([=] {
        prefs->setInt(_prefs_path + "/tile_size", _palette->get_tile_size());
        prefs->setDouble(_prefs_path + "/tile_aspect", _palette->get_aspect());
        prefs->setInt(_prefs_path + "/tile_border", _palette->get_tile_border());
        prefs->setInt(_prefs_path + "/rows", _palette->get_rows());
        prefs->setBool(_prefs_path + "/tile_stretch", _palette->is_stretch_enabled());
        prefs->setBool(_prefs_path + "/enlarge_pinned", _palette->is_pinned_panel_large());
        prefs->setBool(_prefs_path + "/show_labels", !embedded && _palette->are_labels_enabled());
    });

    // Respond to requests from the palette widget to change palettes.
    _palette->get_palette_selected_signal().connect([this] (Glib::ustring name) {
        Preferences::get()->setString(_prefs_path + "/palette", name);
        set_index(name_to_index(name));
    });

    // Watch for pinned palette options.
    _pinned_observer = prefs->createObserver(_prefs_path + "/pinned/", [this]() {
        rebuild();
    });

    rebuild();
}

SwatchesPanel::~SwatchesPanel()
{
    untrack_gradients();
}

/*
 * Activation
 */

// Note: The "Auto" palette shows the list of gradients that are swatches. When this palette is
// shown (and we have a document), we therefore need to track both addition/removal of gradients
// and changes to the isSwatch() status to keep the palette up-to-date.

void SwatchesPanel::documentReplaced()
{
    if (getDocument()) {
        if (index == PALETTE_AUTO) {
            track_gradients();
        }
    } else {
        untrack_gradients();
    }

    if (index == PALETTE_AUTO) {
        rebuild();
    }
}

void SwatchesPanel::desktopReplaced()
{
    documentReplaced();
}

void SwatchesPanel::set_index(PaletteIndex new_index)
{
    if (index == new_index) return;
    index = new_index;

    if (index == PALETTE_AUTO) {
        if (getDocument()) {
            track_gradients();
        }
    } else {
        untrack_gradients();
    }

    rebuild();
}

void SwatchesPanel::track_gradients()
{
    auto doc = getDocument();

    // Subscribe to the addition and removal of gradients.
    conn_gradients.disconnect();
    conn_gradients = doc->connectResourcesChanged("gradient", [this] {
        gradients_changed = true;
        queue_resize();
    });

    // Subscribe to child modifications of the defs section. We will use this to monitor
    // each gradient for whether its isSwatch() status changes.
    conn_defs.disconnect();
    conn_defs = doc->getDefs()->connectModified([this] (SPObject*, unsigned flags) {
        if (flags & SP_OBJECT_CHILD_MODIFIED_FLAG) {
            defs_changed = true;
            queue_resize();
        }
    });

    gradients_changed = false;
    defs_changed = false;
    rebuild_isswatch();
}

void SwatchesPanel::untrack_gradients()
{
    conn_gradients.disconnect();
    conn_defs.disconnect();
    gradients_changed = false;
    defs_changed = false;
}

/*
 * Updating
 */

void SwatchesPanel::selectionChanged(Selection*)
{
    selection_changed = true;
    queue_resize();
}

void SwatchesPanel::selectionModified(Selection*, guint flags)
{
    if (flags & SP_OBJECT_STYLE_MODIFIED_FLAG) {
        selection_changed = true;
        queue_resize();
    }
}

// Document updates are handled asynchronously by setting a flag and queuing a resize. This results in
// the following function being run at the last possible moment before the widget will be repainted.
// This ensures that multiple document updates only result in a single UI update.
void SwatchesPanel::on_size_allocate(Gtk::Allocation &alloc)
{
    if (gradients_changed) {
        assert(index = PALETTE_AUTO);
        // We are in the "Auto" palette, and a gradient was added or removed.
        // The list of widgets has therefore changed, and must be completely rebuilt.
        // We must also rebuild the tracking information for each gradient's isSwatch() status.
        rebuild_isswatch();
        rebuild();
    } else if (defs_changed) {
        assert(index = PALETTE_AUTO);
        // We are in the "Auto" palette, and a gradient's isSwatch() status was possibly modified.
        // Check if it has; if so, then the list of widgets has changed, and must be rebuilt.
        if (update_isswatch()) {
            rebuild();
        }
    }

    if (selection_changed) {
        update_fillstroke_indicators();
    }

    selection_changed = false;
    gradients_changed = false;
    defs_changed = false;

    // Necessary to perform *after* the above widget modifications, so GTK can process the new layout.
    DialogBase::on_size_allocate(alloc);
}

// TODO: The following two functions can made much nicer using C++20 ranges.

void SwatchesPanel::rebuild_isswatch()
{
    auto grads = getDocument()->getResourceList("gradient");

    isswatch.resize(grads.size());

    for (int i = 0; i < grads.size(); i++) {
        isswatch[i] = static_cast<SPGradient*>(grads[i])->isSwatch();
    }
}

bool SwatchesPanel::update_isswatch()
{
    auto grads = getDocument()->getResourceList("gradient");

    // Should be guaranteed because we catch all size changes and call rebuild_isswatch() instead.
    assert(isswatch.size() == grads.size());

    bool modified = false;

    for (int i = 0; i < grads.size(); i++) {
        if (isswatch[i] != static_cast<SPGradient*>(grads[i])->isSwatch()) {
            isswatch[i].flip();
            modified = true;
        }
    }

    return modified;
}

static auto spcolor_to_rgb(SPColor const &color)
{
    float rgbf[3];
    color.get_rgb_floatv(rgbf);

    std::array<unsigned, 3> rgb;
    for (int i = 0; i < 3; i++) {
        rgb[i] = SP_COLOR_F_TO_U(rgbf[i]);
    };

    return rgb;
}

void SwatchesPanel::update_fillstroke_indicators()
{
    auto doc = getDocument();
    auto style = SPStyle(doc);

    // Get the current fill or stroke as a ColorKey.
    auto current_color = [&, this] (bool fill) -> std::optional<ColorKey> {
        switch (sp_desktop_query_style(getDesktop(), &style, fill ? QUERY_STYLE_PROPERTY_FILL : QUERY_STYLE_PROPERTY_STROKE))
        {
            case QUERY_STYLE_SINGLE:
            case QUERY_STYLE_MULTIPLE_AVERAGED:
            case QUERY_STYLE_MULTIPLE_SAME:
                break;
            default:
                return {};
        }

        auto attr = style.getFillOrStroke(fill);
        if (!attr->set) {
            return {};
        }

        if (attr->isNone()) {
            return std::monostate{};
        } else if (attr->isColor()) {
            return spcolor_to_rgb(attr->value.color);
        } else if (attr->isPaintserver()) {
            if (auto grad = cast<SPGradient>(fill ? style.getFillPaintServer() : style.getStrokePaintServer())) {
                if (grad->isSwatch()) {
                    return grad;
                } else if (grad->ref) {
                    if (auto ref = grad->ref->getObject(); ref && ref->isSwatch()) {
                        return ref;
                    }
                }
            }
        }

        return {};
    };

    for (auto w : current_fill) w->set_fill(false);
    for (auto w : current_stroke) w->set_stroke(false);

    current_fill.clear();
    current_stroke.clear();

    if (auto fill = current_color(true)) {
        auto range = widgetmap.equal_range(*fill);
        for (auto it = range.first; it != range.second; ++it) {
            current_fill.emplace_back(it->second);
        }
    }

    if (auto stroke = current_color(false)) {
        auto range = widgetmap.equal_range(*stroke);
        for (auto it = range.first; it != range.second; ++it) {
            current_stroke.emplace_back(it->second);
        }
    }

    for (auto w : current_fill) w->set_fill(true);
    for (auto w : current_stroke) w->set_stroke(true);
}

SwatchesPanel::PaletteIndex SwatchesPanel::name_to_index(Glib::ustring const &name)
{
    auto &palettes = GlobalPalettes::get().palettes;
    if (name == "Auto") {
        return PALETTE_AUTO;
    } else if (auto it = std::find_if(palettes.begin(), palettes.end(), [&] (PaletteFileData const &p) {return p.name == name;}); it != palettes.end()) {
        return (PaletteIndex)(PALETTE_GLOBAL + std::distance(palettes.begin(), it));
    } else {
        return PALETTE_NONE;
    }
}

Glib::ustring SwatchesPanel::index_to_name(PaletteIndex index)
{
    auto &palettes = GlobalPalettes::get().palettes;
    if (index == PALETTE_AUTO) {
        return "Auto";
    } else if (auto n = index - PALETTE_GLOBAL; n >= 0 && n < palettes.size()) {
        return palettes[n].name;
    } else {
        return "";
    }
}

/**
 * Process the list of available palettes and update the list in the _palette widget.
 */
void SwatchesPanel::update_palettes()
{
    std::vector<Inkscape::UI::Widget::ColorPalette::palette_t> palettes;
    palettes.reserve(1 + GlobalPalettes::get().palettes.size());

    // The first palette in the list is always the "Auto" palette. Although this
    // will contain colors when selected, the preview we show for it is empty.
    palettes.push_back({"Auto", {}});

    // The remaining palettes in the list are the global palettes.
    for (auto &p : GlobalPalettes::get().palettes) {
        Inkscape::UI::Widget::ColorPalette::palette_t palette;
        palette.name = p.name;
        for (auto const &c : p.colors) {
            auto [r, g, b] = c.rgb;
            palette.colors.push_back({r / 255.0, g / 255.0, b / 255.0});
        }
        palettes.emplace_back(std::move(palette));
    }

    _palette->set_palettes(palettes);
}

/**
 * Rebuild the list of color items shown by the palette.
 */
void SwatchesPanel::rebuild()
{
    std::vector<ColorItem*> palette;

    // The pointers in widgetmap are to widgets owned by the ColorPalette. It is assumed it will not
    // delete them unless we ask, via the call to set_colors() later in this function.
    widgetmap.clear();
    current_fill.clear();
    current_stroke.clear();

    // Add the "remove-color" color.
    auto const w = Gtk::make_managed<ColorItem>(PaintDef(), this);
    w->set_pinned_pref(_prefs_path);
    palette.emplace_back(w);
    widgetmap.emplace(std::monostate{}, w);

    if (index >= PALETTE_GLOBAL) {
        auto &pal = GlobalPalettes::get().palettes[index - PALETTE_GLOBAL];
        palette.reserve(palette.size() + pal.colors.size());
        for (auto &c : pal.colors) {
            auto const w = Gtk::make_managed<ColorItem>(PaintDef(c.rgb, c.name), this);
            w->set_pinned_pref(_prefs_path);
            palette.emplace_back(w);
            widgetmap.emplace(c.rgb, w);
        }
    } else if (index == PALETTE_AUTO && getDocument()) {
        auto grads = getDocument()->getResourceList("gradient");
        for (auto obj : grads) {
            auto grad = static_cast<SPGradient*>(obj);
            if (grad->isSwatch()) {
                auto const w = Gtk::make_managed<ColorItem>(grad, this);
                palette.emplace_back(w);
                widgetmap.emplace(grad, w);
                // Rebuild if the gradient gets pinned or unpinned
                w->signal_pinned().connect([=]() {
                    rebuild();
                });
            }
        }
    }

    if (getDocument()) {
        update_fillstroke_indicators();
    }

    _palette->set_colors(palette);
    _palette->set_selected(index_to_name(index));
}

} //namespace Dialog
} //namespace UI
} //namespace Inkscape

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

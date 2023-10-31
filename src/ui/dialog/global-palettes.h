// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * @brief Global color palette information.
 */
/* Authors: PBS <pbs3141@gmail.com>
 * Copyright (C) 2022 PBS
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef INKSCAPE_UI_DIALOG_GLOBAL_PALETTES_H
#define INKSCAPE_UI_DIALOG_GLOBAL_PALETTES_H

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include <glibmm/ustring.h>

namespace Gtk {
class Window;
} // namespace Gtk

namespace Inkscape::UI::Dialog {

/**
 * The data loaded from a palette file.
 */
struct PaletteFileData
{
    /// Name of the palette, either specified in the file or taken from the filename.
    Glib::ustring name;

    /// Unique ID of this palette.
    Glib::ustring id;

    /// The preferred number of columns.
    /// Certain color palettes are organized into blocks, typically 7 or 8 colors long.
    /// This value tells us how big the block are, if any.
    /// We can use this info to organize colors in columns in multiples of this value.
    int columns;

    /// Color space of all colors in this palette. Original definitions are kept in "Color.channels"
    /// for use with ICC profiles. Preview sRGB colors are inside "Color.rgb"
    enum ColorSpace {
        Undefined, // not a valid color definition
        Rgb255, // RGB 0..255
        Lab100, // Cie*Lab, L 0..100, a, b -128..127
        Cmyk100 // CMYK 0%..100%
    };

    enum ColorMode: uint8_t {
        Normal,
        Global,
        Spot,
    };

    struct Color
    {
        /// Original color definition (Lab, Cmyk, Rgb); unused channels 0.0
        std::array<float, 4> channels;

        /// Color space of this color.
        ColorSpace space = Undefined;

        /// RGB color.
        std::array<unsigned, 3> rgb;

        /// Name of the color, either specified in the file or generated from the rgb.
        Glib::ustring name;

        /// Color as defined in a palette, for informational purposes.
        Glib::ustring definition;

        /// Mode (not used currently, for informational purposes only)
        ColorMode mode = Normal;

        /// if true, this color definition is blank, and it acts as a spacer to align other colors
        bool filler = false;

        /// if true, this color definition is blank, and it is a start of a group of colors
        bool group = false;

        static Color add_group(Glib::ustring name) {
            Color c{{0,0,0,0}, Undefined, {0,0,0}, name, "", Normal, false, true};
            return c;
        }

#ifdef false // not currently used
        bool operator < (const Color& c) const {
            for (int i = 0; i < rgb.size(); ++i) {
                if (rgb[i] < c.rgb[i]) return true;
                if (rgb[i] > c.rgb[i]) return false;
            }
            for (int i = 0; i < channels.size(); ++i) {
                if (channels[i] < c.channels[i]) return true;
                if (channels[i] > c.channels[i]) return false;
            }
            return name < c.name;
        }
#endif
    };

    /// The list of colors in the palette.
    std::vector<Color> colors;

    /// Index to a representative color of the color block; starts from 0 for each block.
    unsigned int page_offset = 0;

    /// Empty palette
    PaletteFileData() {}
};

/**
 * Singleton class that manages the static list of global palettes.
 */
class GlobalPalettes
{
    GlobalPalettes();
public:
    static GlobalPalettes const &get();

    const std::vector<PaletteFileData>& palettes() const { return _palettes; }
    const PaletteFileData* find_palette(const Glib::ustring& id) const;

private:
    std::vector<PaletteFileData> _palettes;
    std::unordered_map<std::string, PaletteFileData*> _access;
};

// Try to load color/swatch palette from the file
struct PaletteResult { // todo: replace with std::expected when it becomes available
    std::optional<PaletteFileData> palette;
    Glib::ustring error_message;
};
PaletteResult load_palette(std::string const &path);

// Show file chooser and select color palette file
std::string choose_palette_file(Gtk::Window* window);

} // namespace Inkscape::UI::Dialog

#endif // INKSCAPE_UI_DIALOG_GLOBAL_PALETTES_H

/*
  Local Variables:
  mode:c++
  c-file-style:"stroustrup"
  c-file-offsets:((innamespace . 0)(inline-open . 0)(case-label . +))
  indent-tabs-mode:nil
  fill-column:99
  End:
*/
// vim:filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:fileencoding=utf-8:textwidth=99:

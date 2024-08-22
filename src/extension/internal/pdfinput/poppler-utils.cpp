// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * PDF parsing utilities for libpoppler.
 *//*
 * Authors:
 *    Martin Owens
 * 
 * Copyright (C) 2022 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "poppler-utils.h"

#include "2geom/affine.h"
#include "GfxFont.h"
#include "GfxState.h"
#include "PDFDoc.h"
#include "libnrtype/font-factory.h"

/**
 * Get the default transformation state from the GfxState
 */
Geom::Affine stateToAffine(GfxState *state)
{
    return ctmToAffine(state->getCTM());
}

/**
 * Convert a transformation matrix to a lib2geom affine object.
 */
Geom::Affine ctmToAffine(const double *ctm)
{
    if (!ctm)
        return Geom::identity();
    return Geom::Affine(ctm[0], ctm[1], ctm[2], ctm[3], ctm[4], ctm[5]);
}

void ctmout(const char *label, const double *ctm)
{
    std::cout << "C:" << label << ":" << ctm[0] << "," << ctm[1] << "," << ctm[2] << "," << ctm[3] << "," << ctm[4]
              << "," << ctm[5] << "\n";
}

void affout(const char *label, Geom::Affine ctm)
{
    std::cout << "A:" << label << ":" << ctm[0] << "," << ctm[1] << "," << ctm[2] << "," << ctm[3] << "," << ctm[4]
              << "," << ctm[5] << "\n";
}

//------------------------------------------------------------------------
// GfxFontDict from GfxFont.cc in poppler 22.09
//
// Modified under the Poppler project - http://poppler.freedesktop.org
//
// All changes made under the Poppler project to this file are licensed
// under GPL version 2 or later
//
// See poppler source code for full list of copyright holders.
//------------------------------------------------------------------------

InkFontDict::InkFontDict(XRef *xref, Ref *fontDictRef, Dict *fontDict)
{
    Ref r;

    fonts.resize(fontDict->getLength());
    for (std::size_t i = 0; i < fonts.size(); ++i) {
        const Object &obj1 = fontDict->getValNF(i);
        Object obj2 = obj1.fetch(xref);
        if (obj2.isDict()) {
            if (obj1.isRef()) {
                r = obj1.getRef();
            } else if (fontDictRef) {
                // legal generation numbers are five digits, so we use a
                // 6-digit number here
                r.gen = 100000 + fontDictRef->num;
                r.num = i;
            } else {
                // no indirect reference for this font, or for the containing
                // font dict, so hash the font and use that
                r.gen = 100000;
                r.num = hashFontObject(&obj2);
            }
            // Newer poppler will require some reworking as it gives a shared ptr.
            fonts[i] = GfxFont::makeFont(xref, fontDict->getKey(i), r, obj2.getDict());
            if (fonts[i] && !fonts[i]->isOk()) {
                fonts[i] = nullptr;
            }
        } else {
            error(errSyntaxError, -1, "font resource is not a dictionary");
            fonts[i] = nullptr;
        }
    }
}

FontPtr InkFontDict::lookup(const char *tag) const
{
    for (const auto &font : fonts) {
        if (font && font->matches(tag)) {
            return font;
        }
    }
    return nullptr;
}

// FNV-1a hash
class FNVHash
{
public:
    FNVHash() { h = 2166136261U; }

    void hash(char c)
    {
        h ^= c & 0xff;
        h *= 16777619;
    }

    void hash(const char *p, int n)
    {
        int i;
        for (i = 0; i < n; ++i) {
            hash(p[i]);
        }
    }

    int get31() { return (h ^ (h >> 31)) & 0x7fffffff; }

private:
    unsigned int h;
};

int InkFontDict::hashFontObject(Object *obj)
{
    FNVHash h;

    hashFontObject1(obj, &h);
    return h.get31();
}

void InkFontDict::hashFontObject1(const Object *obj, FNVHash *h)
{
    const GooString *s;
    const char *p;
    double r;
    int n, i;

    switch (obj->getType()) {
        case objBool:
            h->hash('b');
            h->hash(obj->getBool() ? 1 : 0);
            break;
        case objInt:
            h->hash('i');
            n = obj->getInt();
            h->hash((char *)&n, sizeof(int));
            break;
        case objReal:
            h->hash('r');
            r = obj->getReal();
            h->hash((char *)&r, sizeof(double));
            break;
        case objString:
            h->hash('s');
            s = obj->getString();
            h->hash(s->c_str(), s->getLength());
            break;
        case objName:
            h->hash('n');
            p = obj->getName();
            h->hash(p, (int)strlen(p));
            break;
        case objNull:
            h->hash('z');
            break;
        case objArray:
            h->hash('a');
            n = obj->arrayGetLength();
            h->hash((char *)&n, sizeof(int));
            for (i = 0; i < n; ++i) {
                const Object &obj2 = obj->arrayGetNF(i);
                hashFontObject1(&obj2, h);
            }
            break;
        case objDict:
            h->hash('d');
            n = obj->dictGetLength();
            h->hash((char *)&n, sizeof(int));
            for (i = 0; i < n; ++i) {
                p = obj->dictGetKey(i);
                h->hash(p, (int)strlen(p));
                const Object &obj2 = obj->dictGetValNF(i);
                hashFontObject1(&obj2, h);
            }
            break;
        case objStream:
            // this should never happen - streams must be indirect refs
            break;
        case objRef:
            h->hash('f');
            n = obj->getRefNum();
            h->hash((char *)&n, sizeof(int));
            n = obj->getRefGen();
            h->hash((char *)&n, sizeof(int));
            break;
        default:
            h->hash('u');
            break;
    }
}

std::string getNameWithoutSubsetTag(FontPtr font)
{
    if (!font->getName())
        return {};

    std::string tagname = font->getName()->c_str();
    unsigned int i;
    for (i = 0; i < tagname.size(); ++i) {
        if (tagname[i] < 'A' || tagname[i] > 'Z') {
            break;
        }
    }
    if (i != 6 || tagname.size() <= 7 || tagname[6] != '+')
        return tagname;
    return tagname.substr(7);
}

/**
 * Extract all the useful information from the GfxFont object
 */
FontData::FontData(FontPtr font)
{
    // Level one parsing is taking the data from the PDF font, although this
    // information is almost always missing. Perhaps sometimes it's not.
    found = false;

    // Style: italic, oblique, normal
    style = font->isItalic() ? "italic" : "";

    // Weight: normal, bold, etc
    weight = "normal";
    switch (font->getWeight()) {
        case GfxFont::WeightNotDefined:
            break;
        case GfxFont::W400:
            weight = "normal";
            break;
        case GfxFont::W700:
            weight = "bold";
            break;
        default:
            weight = std::to_string(font->getWeight() * 100);
            break;
    }

    // Stretch: condensed or expanded
    stretch = "";
    switch (font->getStretch()) {
        case GfxFont::UltraCondensed:
            stretch = "ultra-condensed";
            break;
        case GfxFont::ExtraCondensed:
            stretch = "extra-condensed";
            break;
        case GfxFont::Condensed:
            stretch = "condensed";
            break;
        case GfxFont::SemiCondensed:
            stretch = "semi-condensed";
            break;
        case GfxFont::Normal:
            stretch = "normal";
            break;
        case GfxFont::SemiExpanded:
            stretch = "semi-expanded";
            break;
        case GfxFont::Expanded:
            stretch = "expanded";
            break;
        case GfxFont::ExtraExpanded:
            stretch = "extra-expanded";
            break;
        case GfxFont::UltraExpanded:
            stretch = "ultra-expanded";
            break;
    }

    name = getNameWithoutSubsetTag(font);
    // Use this when min-poppler version is newer:
    // name = font->getNameWithoutSubsetTag();

    // Embeded CID Fonts don't have family names
    if (!font->getFamily())
        return;

    family = font->getFamily()->c_str();

    // Level two parsing, we break off the font description part of the name
    // which often contains font data and use it as a pango font description.
    auto desc_str = family;
    auto pos = name.find("-");
    if (pos != std::string::npos) {
        // Insert spaces where we see capital letters.
        std::stringstream ret;
        auto str = name.substr(pos + 1, name.size());
        for (char l : str) {
            if (l >= 'A' && l <= 'Z')
                ret << " ";
            ret << l;
        }
        desc_str = desc_str + ret.str();
    }

    // Now we pull data out of the description.
    if (auto desc = pango_font_description_from_string(desc_str.c_str())) {
        auto new_family = pango_font_description_get_family(desc);
        if (FontFactory::get().hasFontFamily(new_family)) {
            family = new_family;

            // Style from pango description
            switch (pango_font_description_get_style(desc)) {
                case PANGO_STYLE_ITALIC:
                    style = "italic";
                    break;
                case PANGO_STYLE_OBLIQUE:
                    style = "oblique";
                    break;
            }

            // Weight from pango description
            auto pw = pango_font_description_get_weight(desc);
            if (pw != PANGO_WEIGHT_NORMAL) {
                weight = std::to_string(pw); // Number 100-1000
            }

            // Stretch from pango description
            switch (pango_font_description_get_stretch(desc)) {
                case PANGO_STRETCH_ULTRA_CONDENSED:
                    stretch = "ultra-condensed";
                    break;
                case PANGO_STRETCH_EXTRA_CONDENSED:
                    stretch = "extra-condensed";
                    break;
                case PANGO_STRETCH_CONDENSED:
                    stretch = "condensed";
                    break;
                case PANGO_STRETCH_SEMI_CONDENSED:
                    stretch = "semi-condensed";
                    break;
                case PANGO_STRETCH_SEMI_EXPANDED:
                    stretch = "semi-expanded";
                    break;
                case PANGO_STRETCH_EXPANDED:
                    stretch = "expanded";
                    break;
                case PANGO_STRETCH_EXTRA_EXPANDED:
                    stretch = "extra-expanded";
                    break;
                case PANGO_STRETCH_ULTRA_EXPANDED:
                    stretch = "ultra-expanded";
                    break;
            }

            // variant = TODO Convert to variant pango_font_description_get_variant(desc)

            found = true;
            // All information has been processed, don't over-write with level three.
            return;
        }
        // Sometimes it's possible to match the description string directly.
        if (auto desc = pango_font_description_from_string(family.c_str())) {
            auto new_family = pango_font_description_get_family(desc);
            if (FontFactory::get().hasFontFamily(new_family)) {
                family = new_family;
            }
        }
    }

    found = FontFactory::get().hasFontFamily(family);
    // TODO: If !found we could suggest a substitute

    // Level three parsing, we take our name and attempt to match known style names
    // Copy id-name stored in PDF and make it lower case and strip whitespaces
    std::string source = name;
    transform(source.begin(), source.end(), source.begin(), ::tolower);
    source.erase(std::remove_if(source.begin(), source.end(), ::isspace), source.end());
    auto contains = [=](const std::string &other) { return source.find(other) != std::string::npos; };

    if (contains("italic") || contains("slanted")) {
        style = "italic";
    } else if (contains("oblique")) {
        style = "oblique";
    }

    // Ordered by string matching pass through.
    static std::map<std::string, std::string> weights{
        // clang-format off
        {"bold",        "bold"},
        {"ultrabold",   "800"},
        {"extrabold",   "800"},
        {"demibold",    "600"},
        {"semibold",    "600"},
        {"thin",        "100"},
        {"ultralight",  "200"},
        {"extralight",  "200"},
        {"light",       "300"},
        {"black",       "900"},
        {"heavy",       "900"},
        {"medium",      "500"},
        {"book",        "normal"},
        {"regular",     "normal"},
        {"roman",       "normal"},
        {"normal",      "normal"},
        // clang-format on
    };
    // Apply the font weight translations
    for (auto w : weights) {
        if (contains(w.first))
            weight = w.second;
    }

    static std::map<std::string, std::string> stretches{
        // clang-format off
        {"ultracondensed", "ultra-condensed"},
        {"extracondensed", "extra-condensed"},
        {"semicondensed", "semi-condensed"},
        {"condensed", "condensed"},
        {"ultraexpanded", "ultra-expanded"},
        {"extraexpanded", "extra-expanded"},
        {"semiexpanded", "semi-expanded"},
        {"expanded", "expanded"},
        // clang-format on
    };
    // Apply the font weight translations
    for (auto s : stretches) {
        if (contains(s.first))
            stretch = s.second;
    }
}

/*
    MatchingChars
    Count for how many characters s1 matches sp taking into account
    that a space in sp may be removed or replaced by some other tokens
    specified in the code. (Bug LP #179589)
*/
static size_t MatchingChars(std::string s1, std::string sp)
{
    size_t is = 0;
    size_t ip = 0;

    while (is < s1.length() && ip < sp.length()) {
        if (s1[is] == sp[ip]) {
            is++;
            ip++;
        } else if (sp[ip] == ' ') {
            ip++;
            if (s1[is] == '_') { // Valid matches to spaces in sp.
                is++;
            }
        } else {
            break;
        }
    }
    return ip;
}

/*
 * Scan the available fonts to find the font name that best match.
 *
 * If nothing can be matched, returns an empty string.
 */
std::string FontData::getSubstitute() const
{
    if (found)
        return "";

    double bestMatch = 0;
    std::string bestFontname = "";

    for (auto fontname : FontFactory::get().GetAllFontNames()) {
        // At least the first word of the font name should match.
        size_t minMatch = fontname.find(" ");
        if (minMatch == std::string::npos) {
            minMatch = fontname.length();
        }

        size_t Match = MatchingChars(family, fontname);
        if (Match >= minMatch) {
            double relMatch = (float)Match / (fontname.length() + family.length());
            if (relMatch > bestMatch) {
                bestMatch = relMatch;
                bestFontname = fontname;
            }
        }
    }
    return bestFontname.empty() ? "Arial" : bestFontname;
}

std::string FontData::getSpecification() const
{
    return family + (style.empty() ? "" : "-" + style);
}

//------------------------------------------------------------------------
// scanFonts from FontInfo.cc
//------------------------------------------------------------------------

void _getFontsRecursive(std::shared_ptr<PDFDoc> pdf_doc, Dict *resources, const FontList &fontsList,
                        std::set<int> &visitedObjects, int page)
{
    auto xref = pdf_doc->getXRef();

    InkFontDict *fontDict = nullptr;
    const Object &obj1 = resources->lookupNF("Font");
    if (obj1.isRef()) {
        Object obj2 = obj1.fetch(xref);
        if (obj2.isDict()) {
            auto r = obj1.getRef();
            fontDict = new InkFontDict(xref, &r, obj2.getDict());
        }
    } else if (obj1.isDict()) {
        fontDict = new InkFontDict(xref, nullptr, obj1.getDict());
    }

    if (fontDict) {
        for (int i = 0; i < fontDict->getNumFonts(); ++i) {
            auto font = fontDict->getFont(i);
            if (fontsList->find(font) == fontsList->end()) {
                // Create new font data
                fontsList->emplace(font, FontData(font));
            }
            fontsList->at(font).pages.insert(page);
        }
    }

    // recursively scan any resource dictionaries in objects in this resource dictionary
    const char *resTypes[] = {"XObject", "Pattern"};
    for (const char *resType : resTypes) {
        Object objDict = resources->lookup(resType);
        if (!objDict.isDict())
            continue;

        for (int i = 0; i < objDict.dictGetLength(); ++i) {
            Ref obj2Ref;
            const Object obj2 = objDict.getDict()->getVal(i, &obj2Ref);
            if (obj2Ref != Ref::INVALID() && !visitedObjects.insert(obj2Ref.num).second)
                continue;

            if (!obj2.isStream())
                continue;

            Ref resourcesRef;
            const Object resObj = obj2.streamGetDict()->lookup("Resources", &resourcesRef);
            if (resourcesRef != Ref::INVALID() && !visitedObjects.insert(resourcesRef.num).second)
                continue;

            if (resObj.isDict() && resObj.getDict() != resources) {
                _getFontsRecursive(pdf_doc, resObj.getDict(), fontsList, visitedObjects, page);
            }
        }
    }
}

FontList getPdfFonts(std::shared_ptr<PDFDoc> pdf_doc)
{
    auto fontsList = std::make_shared<std::map<FontPtr, FontData>>();
    auto count = pdf_doc->getCatalog()->getNumPages();
    std::set<int> visitedObjects;

    for (auto page_num = 1; page_num <= count; page_num++) {
        auto page = pdf_doc->getCatalog()->getPage(page_num);
        auto resources = page->getResourceDict();
        _getFontsRecursive(pdf_doc, resources, fontsList, visitedObjects, page_num);
    }
    return fontsList;
}


std::string getDictString(Dict *dict, const char *key)
{
    Object obj = dict->lookup(key);

    if (!obj.isString()) {
        return "";
    }

    const GooString *value = obj.getString();
    if (value->hasUnicodeMarker()) {
        return g_convert(value->getCString () + 2, value->getLength () - 2,
                         "UTF-8", "UTF-16BE", NULL, NULL, NULL);
    } else if (value->hasUnicodeMarkerLE()) {
        return g_convert(value->getCString () + 2, value->getLength () - 2,
                         "UTF-8", "UTF-16LE", NULL, NULL, NULL);
    }
    return value->toStr();
}



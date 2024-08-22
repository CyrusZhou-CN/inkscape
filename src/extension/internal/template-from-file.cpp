// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Authors:
 *   Martin Owens <doctormo@geek-2.com>
 *
 * Copyright (C) 2022 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include <glibmm/miscutils.h>

#include "document.h"
#include "extension/prefdialog/parameter.h"
#include "io/file.h"
#include "io/resource.h"
#include "io/sys.h"
#include "page-manager.h"
#include "template-from-file.h"

#include "clear-n_.h"

using namespace Inkscape::IO::Resource;

namespace Inkscape {
namespace Extension {
namespace Internal {

/**
 * A file based template preset.
 */
TemplatePresetFile::TemplatePresetFile(Template *mod, const std::string &filename)
    : TemplatePreset(mod, nullptr)
{
    _visibility = TEMPLATE_NEW_ICON; // No searching

    // TODO: Add cache here.
    _prefs["filename"] = filename;
    _name = Glib::path_get_basename(filename);
    std::replace(_name.begin(), _name.end(), '_', '-');
    _name.replace(_name.rfind(".svg"), 4, 1, ' ');
    
    Inkscape::XML::Document *rdoc = sp_repr_read_file(filename.c_str(), SP_SVG_NS_URI);
    if (rdoc){
        Inkscape::XML::Node *root = rdoc->root();
        if (!strcmp(root->name(), "svg:svg")) {
            Inkscape::XML::Node *templateinfo = sp_repr_lookup_name(root, "inkscape:templateinfo");
            if (!templateinfo) {
                templateinfo = sp_repr_lookup_name(root, "inkscape:_templateinfo"); // backwards-compatibility
            }
            if (templateinfo) {
                _load_data(templateinfo);
            }
        }
    }

    // Key is just the whole filename, it's unique enough.
    _key = filename;
    std::replace(_key.begin(), _key.end(), '/', '.');
    std::replace(_key.begin(), _key.end(), '\\', '.');
}

void TemplatePresetFile::_load_data(const Inkscape::XML::Node *root)
{
    _name = sp_repr_lookup_content(root, "inkscape:name", _name);
    _name = sp_repr_lookup_content(root, "inkscape:_name", _name); // backwards-compatibility
    _label = sp_repr_lookup_content(root, "inkscape:shortdesc", N_("Custom Template"));
    _label = sp_repr_lookup_content(root, "inkscape:shortdesc", _label); // backwards-compatibility

    _icon = sp_repr_lookup_content(root, "inkscape:icon", _icon);
    // Original functionality not yet used...
    // _author = sp_repr_lookup_content(root, "inkscape:author");
    // _preview = sp_repr_lookup_content(root, "inkscape:preview");
    // _date = sp_repr_lookup_name(root, "inkscape:date");
    // _keywords = sp_repr_lookup_name(root, "inkscape:_keywords");
}


SPDocument *TemplateFromFile::new_from_template(Inkscape::Extension::Template *tmod)
{
    auto filename = tmod->get_param_string("filename", "");
    if (Inkscape::IO::file_test(filename, (GFileTest)(G_FILE_TEST_EXISTS))) {
        return ink_file_new(filename);
    }
    // Default template
    g_error("Couldn't load filename I expected to exist.");
    return tmod->get_template_document();
}

void TemplateFromFile::init()
{
    // clang-format off
    Inkscape::Extension::build_from_mem(
        "<inkscape-extension xmlns=\"" INKSCAPE_EXTENSION_URI "\">"
            "<id>org.inkscape.template.from-file</id>"
            "<name>" N_("Load from User File") "</name>"
            "<description>" N_("Custom list of templates for a folder") "</description>"
            "<category>" NC_("TemplateCategory", "Custom") "</category>"

            "<param name='filename' gui-text='" N_("Filename") "' type='string'></param>"
            "<template icon='custom' priority='-1' visibility='both'>"
              // Auto & lazy generated content (see function)
            "</template>"
        "</inkscape-extension>",
        new TemplateFromFile());
}

/**
 * Generate a list of available files as selectable presets.
 */
void TemplateFromFile::get_template_presets(const Template *tmod, TemplatePresets &presets) const
{
    for(auto &filename: get_filenames(TEMPLATES, {".svg"}, {"default"})) {
        if (filename.find("icons") != Glib::ustring::npos) continue;
        presets.emplace_back(new TemplatePresetFile(const_cast<Template *>(tmod), filename));
    }
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

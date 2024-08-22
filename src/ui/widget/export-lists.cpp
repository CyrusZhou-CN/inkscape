// SPDX-License-Identifier: GPL-2.0-or-later
/* Authors:
 *   Anshudhar Kumar Singh <anshudhar2001@gmail.com>
 *
 * Copyright (C) 2021 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "export-lists.h"

#include <glibmm/convert.h>
#include <glibmm/i18n.h>
#include <glibmm/miscutils.h>
#include <gtkmm.h>
#include <png.h>

#include "desktop.h"
#include "document-undo.h"
#include "document.h"
#include "extension/db.h"
#include "extension/output.h"
#include "file.h"
#include "helper/png-write.h"
#include "inkscape-window.h"
#include "inkscape.h"
#include "io/resource.h"
#include "io/sys.h"
#include "message-stack.h"
#include "object/object-set.h"
#include "object/sp-namedview.h"
#include "object/sp-page.h"
#include "object/sp-root.h"
#include "page-manager.h"
#include "preferences.h"
#include "selection-chemistry.h"
#include "ui/dialog-events.h"
#include "ui/dialog/dialog-notebook.h"
#include "ui/dialog/filedialog.h"
#include "ui/icon-loader.h"
#include "ui/interface.h"
#include "ui/widget/scrollprotected.h"
#include "ui/widget/unit-menu.h"
#include "ui/builder-utils.h"

using Inkscape::Util::unit_table;

namespace Inkscape {
namespace UI {
namespace Dialog {

ExtensionList::ExtensionList()
{
    init();
}

ExtensionList::ExtensionList(BaseObjectType *cobject, const Glib::RefPtr<Gtk::Builder> &refGlade)
    : Inkscape::UI::Widget::ScrollProtected<Gtk::ComboBoxText>(cobject, refGlade)
{
    init();
}

ExtensionList::~ExtensionList()
{
    _popover_signal.disconnect();
}

void ExtensionList::init()
{
    _builder = create_builder("dialog-export-prefs.glade");
    _builder->get_widget("pref_button", _pref_button);
    _builder->get_widget("pref_popover", _pref_popover);
    _builder->get_widget("pref_holder", _pref_holder);

    _popover_signal = _pref_popover->signal_show().connect([=]() {
        _pref_holder->remove();
        if (auto ext = getExtension()) {
            if (auto gui = ext->autogui(nullptr, nullptr)) {
                _pref_holder->add(*gui);
                _pref_popover->grab_focus();
            }
        }
    });

    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    _watch_pref = prefs->createObserver("/dialogs/export/show_all_extensions", [=]() { setup(); });
}

void ExtensionList::on_changed()
{
    bool has_prefs = false;
    if (auto ext = getExtension()) {
        has_prefs = (ext->widget_visible_count() > 0);
    }
    _pref_button->set_sensitive(has_prefs);
}

void ExtensionList::setup()
{
    this->remove_all();

    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    bool export_all = prefs->getBool("/dialogs/export/show_all_extensions", false);

    Inkscape::Extension::DB::OutputList extensions;
    Inkscape::Extension::db.get_output_list(extensions);
    for (auto omod : extensions) {
        auto oid = Glib::ustring(omod->get_id());
        if (!export_all && !omod->is_raster() && !omod->is_exported())
            continue;
        // Comboboxes don't have a disabled row property
        if (omod->deactivated())
            continue;
        this->append(oid, omod->get_filetypename());
        // Record extensions map for filename-to-combo selections
        auto ext = omod->get_extension();
        if (!ext_to_mod[ext]) {
            // Some extensions have multiple of the same extension (for example PNG)
            // we're going to pick the first in the found list to back-link to.
            ext_to_mod[ext] = omod;
        }
    }
    this->set_active_id(SP_MODULE_KEY_RASTER_PNG);
}

/**
 * Returns the Output extension currently selected in this dropdown.
 */
Inkscape::Extension::Output *ExtensionList::getExtension()
{
    return dynamic_cast<Inkscape::Extension::Output *>(Inkscape::Extension::db.get(this->get_active_id().c_str()));
}

/**
 * Returns the file extension (file ending) of the currently selected extension.
 */
Glib::ustring ExtensionList::getFileExtension()
{
    if (auto ext = getExtension()) {
        return ext->get_extension();
    }
    return "";
}

/**
 * Removes the file extension, *if* it's one of the extensions in the list.
 */
void ExtensionList::removeExtension(Glib::ustring &filename)
{
    auto ext = Inkscape::IO::get_file_extension(filename);
    if (ext_to_mod[ext]) {
        filename.erase(filename.size()-ext.size());
    }
}

void ExtensionList::setExtensionFromFilename(Glib::ustring const &filename)
{
    auto ext = Inkscape::IO::get_file_extension(filename);
    if (auto omod = ext_to_mod[ext]) {
        this->set_active_id(omod->get_id());
    }
}

void ExportList::setup()
{
    if (_initialised) {
        return;
    }
    _initialised = true;
    prefs = Inkscape::Preferences::get();
    default_dpi = prefs->getDouble("/dialogs/export/defaultxdpi/value", DPI_BASE);

    auto const add_button = Gtk::make_managed<Gtk::Button>();
    Glib::ustring label = _("Add Export");
    add_button->set_label(label);
    this->attach(*add_button, 0, 0, 4, 1);

    this->insert_row(0);

    auto const suffix_label = Gtk::make_managed<Gtk::Label>(_("Suffix"));
    this->attach(*suffix_label, _suffix_col, 0, 1, 1);
    suffix_label->set_visible(true);

    auto const extension_label = Gtk::make_managed<Gtk::Label>(_("Format"));
    this->attach(*extension_label, _extension_col, 0, 2, 1);
    extension_label->set_visible(true);

    auto const dpi_label = Gtk::make_managed<Gtk::Label>(_("DPI"));
    this->attach(*dpi_label, _dpi_col, 0, 1, 1);
    dpi_label->set_visible(true);

    append_row();

    add_button->signal_clicked().connect(sigc::mem_fun(*this, &ExportList::append_row));
    add_button->set_hexpand(true);
    add_button->set_visible(true);

    this->set_row_spacing(5);
    this->set_column_spacing(2);
}

void ExportList::removeExtension(Glib::ustring &filename)
{
    ExtensionList *extension_cb = dynamic_cast<ExtensionList *>(this->get_child_at(_extension_col, 1));
    if (extension_cb) {
        extension_cb->removeExtension(filename);
        return;
    }
}

void ExportList::append_row()
{
    int current_row = _num_rows + 1; // because we have label row at top
    this->insert_row(current_row);

    auto const suffix = Gtk::make_managed<Gtk::Entry>();
    this->attach(*suffix, _suffix_col, current_row, 1, 1);
    suffix->set_width_chars(2);
    suffix->set_hexpand(true);
    suffix->set_placeholder_text(_("Suffix"));
    suffix->set_visible(true);

    auto const extension = Gtk::make_managed<ExtensionList>();
    auto const dpi_sb = Gtk::make_managed<SpinButton>();

    extension->setup();
    extension->set_visible(true);
    this->attach(*extension, _extension_col, current_row, 1, 1);
    this->attach(*extension->getPrefButton(), _prefs_col, current_row, 1, 1);

    // Disable DPI when not using a raster image output
    extension->signal_changed().connect([=]() {
        if (auto ext = extension->getExtension()) {
            dpi_sb->set_sensitive(ext->is_raster());
        }
    });

    dpi_sb->set_digits(2);
    dpi_sb->set_increments(0.1, 1.0);
    dpi_sb->set_range(1.0, 100000.0);
    dpi_sb->set_value(default_dpi);
    dpi_sb->set_sensitive(true);
    dpi_sb->set_width_chars(6);
    dpi_sb->set_max_width_chars(6);
    dpi_sb->set_visible(true);
    this->attach(*dpi_sb, _dpi_col, current_row, 1, 1);

    auto const pIcon = Gtk::manage(sp_get_icon_image("window-close", Gtk::ICON_SIZE_SMALL_TOOLBAR));
    auto const delete_btn = Gtk::make_managed<Gtk::Button>();
    delete_btn->set_relief(Gtk::RELIEF_NONE);
    delete_btn->add(*pIcon);
    delete_btn->show_all();
    delete_btn->set_no_show_all(true);
    this->attach(*delete_btn, _delete_col, current_row, 1, 1);
    delete_btn->signal_clicked().connect(sigc::bind(sigc::mem_fun(*this, &ExportList::delete_row), delete_btn));

    _num_rows++;
}

void ExportList::delete_row(Gtk::Widget *widget)
{
    if (widget == nullptr) {
        return;
    }
    if (_num_rows <= 1) {
        return;
    }
    int row = this->child_property_top_attach(*widget);
    this->remove_row(row);
    _num_rows--;
    if (_num_rows <= 1) {
        auto const d_button_0 = this->get_child_at(_delete_col, 1);
        if (d_button_0) {
            d_button_0->set_visible(false);
        }
    }
}

Glib::ustring ExportList::get_suffix(int row)
{
    Glib::ustring suffix = "";
    Gtk::Entry *entry = dynamic_cast<Gtk::Entry *>(this->get_child_at(_suffix_col, row + 1));
    if (entry == nullptr) {
        return suffix;
    }
    suffix = entry->get_text();
    return suffix;
}
Inkscape::Extension::Output *ExportList::getExtension(int row)
{
    ExtensionList *extension_cb = dynamic_cast<ExtensionList *>(this->get_child_at(_extension_col, row + 1));
    return extension_cb->getExtension();
}

double ExportList::get_dpi(int row)
{
    double dpi = default_dpi;
    SpinButton *spin_sb = dynamic_cast<SpinButton *>(this->get_child_at(_dpi_col, row + 1));
    if (spin_sb == nullptr) {
        return dpi;
    }
    dpi = spin_sb->get_value();
    return dpi;
}



} // namespace Dialog
} // namespace UI
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

// SPDX-License-Identifier: GPL-2.0-or-later

#include "choose-file.h"
#include "ui/dialog-run.h"
#include <glib/gi18n.h>
#include <gtkmm/filechooser.h>
#include <gtkmm/filechooserdialog.h>
#include <glibmm/miscutils.h>
#include <string>

namespace Inkscape {

std::string choose_file_save(Glib::ustring title, Gtk::Window* parent, Glib::ustring mime_type, Glib::ustring file_name, std::string& current_folder) {
    if (!parent) return {};

    if (current_folder.empty()) {
        current_folder = Glib::get_home_dir();
    }

    Gtk::FileChooserDialog dlg(*parent, title, Gtk::FILE_CHOOSER_ACTION_SAVE);
    constexpr int save_id = Gtk::RESPONSE_OK;
    dlg.add_button(_("Cancel"), Gtk::RESPONSE_CANCEL);
    dlg.add_button(_("Save"), save_id);
    dlg.set_default_response(save_id);
    auto filter = Gtk::FileFilter::create();
    filter->add_mime_type(mime_type);
    dlg.set_filter(filter);
    dlg.set_current_folder(current_folder);
    dlg.set_current_name(file_name);
    dlg.set_do_overwrite_confirmation();

    auto id = UI::dialog_run(dlg);
    if (id != save_id) return {};

    auto fname = dlg.get_filename();
    if (fname.empty()) return {};

    current_folder = dlg.get_current_folder();

    return fname;
}

std::string choose_file_open(Glib::ustring title, Gtk::Window* parent, std::vector<Glib::ustring> mime_types, std::string& current_folder) {
    if (!parent) return {};

    if (current_folder.empty()) {
        current_folder = Glib::get_home_dir();
    }

    Gtk::FileChooserDialog dlg(*parent, title, Gtk::FILE_CHOOSER_ACTION_OPEN);
    constexpr int open_id = Gtk::RESPONSE_OK;
    dlg.add_button(_("Cancel"), Gtk::RESPONSE_CANCEL);
    dlg.add_button(_("Open"), open_id);
    dlg.set_default_response(open_id);
    auto filter = Gtk::FileFilter::create();
    for (auto&& t : mime_types) {
        filter->add_mime_type(t);
    }
    dlg.set_filter(filter);
    dlg.set_current_folder(current_folder);

    auto id = UI::dialog_run(dlg);
    if (id != open_id) return {};

    auto fname = dlg.get_filename();
    if (fname.empty()) return {};

    current_folder = dlg.get_current_folder();

    return fname;
}

} // namespace Inkscape

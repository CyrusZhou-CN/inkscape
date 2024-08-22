// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * @file
 * Implementation of the file dialog interfaces defined in filedialogimpl.h.
 */
/* Authors:
 *   Bob Jamison
 *   Joel Holdsworth
 *   Bruno Dilly
 *   Other dudes from The Inkscape Organization
 *   Abhishek Sharma
 *
 * Copyright (C) 2004-2007 Bob Jamison
 * Copyright (C) 2006 Johan Engelen <johan@shouraizou.nl>
 * Copyright (C) 2007-2008 Joel Holdsworth
 * Copyright (C) 2004-2007 The Inkscape Organization
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "filedialogimpl-gtkmm.h"

#include <glibmm/convert.h>
#include <glibmm/fileutils.h>
#include <glibmm/i18n.h>
#include <glibmm/miscutils.h>
#include <glibmm/regex.h>
#include <gtkmm/expander.h>
#include <iostream>

#include "document.h"
#include "extension/db.h"
#include "extension/input.h"
#include "extension/output.h"
#include "inkscape.h"
#include "io/resource.h"
#include "io/sys.h"
#include "path-prefix.h"
#include "preferences.h"
#include "ui/dialog-events.h"
#include "ui/dialog-run.h"
#include "ui/view/svg-view-widget.h"
#include "widgets/spw-utilities.h"

// Routines from file.cpp
#undef INK_DUMP_FILENAME_CONV

#ifdef INK_DUMP_FILENAME_CONV
void dump_str(const gchar *str, const gchar *prefix);
void dump_ustr(const Glib::ustring &ustr);
#endif

/**
 * Information stored about all save and open filters applied to the dialog.
 */
struct FilterListClass : public Gtk::TreeModelColumnRecord
{
    Gtk::TreeModelColumn<Glib::ustring> label;
    Gtk::TreeModelColumn<Inkscape::Extension::Extension *> extension;
    Gtk::TreeModelColumn<bool> enabled;

    FilterListClass()
    {
        add(label);
        add(extension);
        add(enabled);
    }
};
FilterListClass FilterList;

namespace Inkscape {
namespace UI {
namespace Dialog {

/*#########################################################################
### F I L E     D I A L O G    B A S E    C L A S S
#########################################################################*/

void FileDialogBaseGtk::internalSetup()
{
    filterComboBox = dynamic_cast<Gtk::ComboBoxText *>(sp_search_by_name_recursive(this, "GtkComboBoxText"));
    g_assert(filterComboBox);

    filterStore = Gtk::ListStore::create(FilterList);
    filterComboBox->set_model(filterStore);
    filterComboBox->signal_changed().connect(sigc::mem_fun(*this, &FileDialogBaseGtk::filterChangedCallback));

    auto cell_renderer = filterComboBox->get_first_cell();
    if (cell_renderer) {
        // Add enabled column to cell_renderer property
        filterComboBox->add_attribute(cell_renderer->property_sensitive(), FilterList.enabled);
    }

    // Open executable file dialogs don't need the preview panel
    if (_dialogType != EXE_TYPES) {
        Inkscape::Preferences *prefs = Inkscape::Preferences::get();
        bool enablePreview   = prefs->getBool(preferenceBase + "/enable_preview", true);
        bool enableSVGExport = prefs->getBool(preferenceBase + "/enable_svgexport", false);

        previewCheckbox.set_label(Glib::ustring(_("Enable preview")));
        previewCheckbox.set_active(enablePreview);

        previewCheckbox.signal_toggled().connect(sigc::mem_fun(*this, &FileDialogBaseGtk::_updatePreviewCallback));

        svgexportCheckbox.set_label(Glib::ustring(_("Export as SVG 1.1 per settings in Preferences dialog")));
        svgexportCheckbox.set_active(enableSVGExport);

        svgexportCheckbox.signal_toggled().connect(sigc::mem_fun(*this, &FileDialogBaseGtk::_svgexportEnabledCB));

        // Catch selection-changed events, so we can adjust the text widget
        signal_update_preview().connect(sigc::mem_fun(*this, &FileDialogBaseGtk::_updatePreviewCallback));

        //###### Add a preview widget
        set_preview_widget(svgPreview);
        set_preview_widget_active(enablePreview);
        set_use_preview_label(false);
    }
}


void FileDialogBaseGtk::cleanup(bool showConfirmed)
{
    if (_dialogType != EXE_TYPES) {
        Inkscape::Preferences *prefs = Inkscape::Preferences::get();
        if (showConfirmed) {
            prefs->setBool(preferenceBase + "/enable_preview", previewCheckbox.get_active());
        }
    }
}

void FileDialogBaseGtk::_svgexportEnabledCB()
{
    bool enabled = svgexportCheckbox.get_active();
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    prefs->setBool(preferenceBase + "/enable_svgexport", enabled);
}

/**
 * Callback for checking if the preview needs to be redrawn
 */
void FileDialogBaseGtk::_updatePreviewCallback()
{
    bool enabled = previewCheckbox.get_active();

    set_preview_widget_active(enabled);

    if (!enabled)
        return;

    Glib::ustring fileName = get_preview_filename();
    if (fileName.empty()) {
        fileName = get_preview_uri();
    }

    if (!fileName.empty()) {
        svgPreview.set(fileName, _dialogType);
    } else {
        svgPreview.showNoPreview();
    }
}

Glib::RefPtr<Gtk::FileFilter> FileDialogBaseGtk::addFilter(const Glib::ustring &name, Glib::ustring ext,
                                                           Inkscape::Extension::Extension *extension)
{
    auto filter = Gtk::FileFilter::create();
    filter->set_name(name);
    add_filter(filter);

    if (!ext.empty()) {
        filter->add_pattern(extToPattern(ext));
    }

    // ListStore is populated by add_filter, so get the last row to add the rest
    Gtk::TreeRow row;
    for (auto child : filterStore->children()) {
        row = child;
    }
    if (row) {
        row[FilterList.extension] = extension;
        row[FilterList.enabled] = !extension || !extension->deactivated();
    }
    return filter;
}

// Replace this with add_suffix in Gtk4
Glib::ustring FileDialogBaseGtk::extToPattern(const Glib::ustring &extension) const
{
    Glib::ustring pattern = "*";
    for (unsigned int ch : extension) {
        if (Glib::Unicode::isalpha(ch)) {
            pattern += '[';
            pattern += Glib::Unicode::toupper(ch);
            pattern += Glib::Unicode::tolower(ch);
            pattern += ']';
        } else {
            pattern += ch;
        }
    }
    return pattern;
}

/*#########################################################################
### F I L E    O P E N
#########################################################################*/

/**
 * Constructor.  Not called directly.  Use the factory.
 */
FileOpenDialogImplGtk::FileOpenDialogImplGtk(Gtk::Window &parentWindow, const Glib::ustring &dir,
                                             FileDialogType fileTypes, const Glib::ustring &title)
    : FileDialogBaseGtk(parentWindow, title, Gtk::FILE_CHOOSER_ACTION_OPEN, fileTypes, "/dialogs/open")
{


    if (_dialogType == EXE_TYPES) {
        /* One file at a time */
        set_select_multiple(false);
    } else {
        /* And also Multiple Files */
        set_select_multiple(true);
    }

    set_local_only(false);

    /* Set our dialog type (open, import, etc...)*/
    _dialogType = fileTypes;

    /* Set the pwd and/or the filename */
    if (dir.size() > 0) {
        Glib::ustring udir(dir);
        Glib::ustring::size_type len = udir.length();
        // leaving a trailing backslash on the directory name leads to the infamous
        // double-directory bug on win32
        if (len != 0 && udir[len - 1] == '\\')
            udir.erase(len - 1);
        if (_dialogType == EXE_TYPES) {
            set_filename(udir.c_str());
        } else {
            set_current_folder(udir.c_str());
        }
    }

    if (_dialogType != EXE_TYPES) {
        set_extra_widget(previewCheckbox);
    }

    //###### Add the file types menu
    createFilterMenu();

    add_button(_("_Cancel"), Gtk::RESPONSE_CANCEL);
    set_default(*add_button(_("_Open"), Gtk::RESPONSE_OK));

    //###### Allow easy access to our examples folder

    using namespace Inkscape::IO::Resource;
    auto examplesdir = get_path_string(SYSTEM, EXAMPLES);
    if (Glib::file_test(examplesdir, Glib::FILE_TEST_IS_DIR) && //
        Glib::path_is_absolute(examplesdir)) {
        add_shortcut_folder(examplesdir);
    }
}

void FileOpenDialogImplGtk::createFilterMenu()
{
    if (_dialogType == CUSTOM_TYPE) {
        return;
    }

    addFilter(_("All Files"), "*");

    if (_dialogType != EXE_TYPES) {
        auto allInkscapeFilter = addFilter(_("All Inkscape Files"));
        auto allImageFilter = addFilter(_("All Images"));
        auto allVectorFilter = addFilter(_("All Vectors"));
        auto allBitmapFilter = addFilter(_("All Bitmaps"));

        // patterns added dynamically below
        Inkscape::Extension::DB::InputList extension_list;
        Inkscape::Extension::db.get_input_list(extension_list);

        for (auto imod : extension_list)
        {
            addFilter(imod->get_filetypename(true), imod->get_extension(), imod);

            auto upattern = extToPattern(imod->get_extension());
            allInkscapeFilter->add_pattern(upattern);
            if (strncmp("image", imod->get_mimetype(), 5) == 0)
                allImageFilter->add_pattern(upattern);

            // I don't know of any other way to define "bitmap" formats other than by listing them
            if (strncmp("image/png", imod->get_mimetype(), 9) == 0 ||
                strncmp("image/jpeg", imod->get_mimetype(), 10) == 0 ||
                strncmp("image/gif", imod->get_mimetype(), 9) == 0 ||
                strncmp("image/x-icon", imod->get_mimetype(), 12) == 0 ||
                strncmp("image/x-navi-animation", imod->get_mimetype(), 22) == 0 ||
                strncmp("image/x-cmu-raster", imod->get_mimetype(), 18) == 0 ||
                strncmp("image/x-xpixmap", imod->get_mimetype(), 15) == 0 ||
                strncmp("image/bmp", imod->get_mimetype(), 9) == 0 ||
                strncmp("image/vnd.wap.wbmp", imod->get_mimetype(), 18) == 0 ||
                strncmp("image/tiff", imod->get_mimetype(), 10) == 0 ||
                strncmp("image/x-xbitmap", imod->get_mimetype(), 15) == 0 ||
                strncmp("image/x-tga", imod->get_mimetype(), 11) == 0 ||
                strncmp("image/x-pcx", imod->get_mimetype(), 11) == 0)
            {
                allBitmapFilter->add_pattern(upattern);
             } else {
                allVectorFilter->add_pattern(upattern);
            }
        }
    }
    return;
}

/**
 * Show this dialog modally.  Return true if user hits [OK]
 */
bool FileOpenDialogImplGtk::show()
{
    set_modal(TRUE); // Window
    sp_transientize(GTK_WIDGET(gobj())); // Make transient
    gint b = dialog_run(*this); // Dialog
    svgPreview.showNoPreview();

    if (b == Gtk::RESPONSE_OK) {
        if (auto iter = filterComboBox->get_active()) {
            setExtension((*iter)[FilterList.extension]);
        }

        auto fn = get_filename();
        setFilename(fn.empty() ? get_uri() : Glib::ustring(fn));

        cleanup(true);
        return true;
    } else {
        cleanup(false);
        return false;
    }
}


/**
 * To Get Multiple filenames selected at-once.
 */
std::vector<Glib::ustring> FileOpenDialogImplGtk::getFilenames()
{
    auto result_tmp = get_filenames();

    // Copy filenames to a vector of type Glib::ustring
    std::vector<Glib::ustring> result;

    for (auto it : result_tmp)
        result.emplace_back(it);

    if (result.empty()) {
        result = get_uris();
    }

    return result;
}

Glib::ustring FileOpenDialogImplGtk::getCurrentDirectory()
{
    return get_current_folder();
}



//########################################################################
//# F I L E    S A V E
//########################################################################

/**
 * Constructor
 */
FileSaveDialogImplGtk::FileSaveDialogImplGtk(Gtk::Window &parentWindow, const Glib::ustring &dir,
                                             FileDialogType fileTypes, const Glib::ustring &title,
                                             const Glib::ustring & /*default_key*/, const gchar *docTitle,
                                             const Inkscape::Extension::FileSaveMethod save_method)
    : FileDialogBaseGtk(parentWindow, title, Gtk::FILE_CHOOSER_ACTION_SAVE, fileTypes,
                        (save_method == Inkscape::Extension::FILE_SAVE_METHOD_SAVE_COPY) ? "/dialogs/save_copy"
                                                                                         : "/dialogs/save_as")
    , save_method(save_method)
    , fromCB(false)
    , checksBox(Gtk::ORIENTATION_VERTICAL)
    , childBox(Gtk::ORIENTATION_HORIZONTAL)
{
    FileSaveDialog::myDocTitle = docTitle;

    /* One file at a time */
    set_select_multiple(false);

    set_local_only(false);

    /* Set our dialog type (save, export, etc...)*/
    _dialogType = fileTypes;

    /* Set the pwd and/or the filename */
    if (dir.size() > 0) {
        Glib::ustring udir(dir);
        Glib::ustring::size_type len = udir.length();
        // leaving a trailing backslash on the directory name leads to the infamous
        // double-directory bug on win32
        if ((len != 0) && (udir[len - 1] == '\\')) {
            udir.erase(len - 1);
        }
        setFilename(udir);
    }

    //###### Do we want the .xxx extension automatically added?
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    fileTypeCheckbox.set_label(Glib::ustring(_("Append filename extension automatically")));
    if (save_method == Inkscape::Extension::FILE_SAVE_METHOD_SAVE_COPY) {
        fileTypeCheckbox.set_active(prefs->getBool("/dialogs/save_copy/append_extension", true));
    } else {
        fileTypeCheckbox.set_active(prefs->getBool("/dialogs/save_as/append_extension", true));
    }

    if (_dialogType != CUSTOM_TYPE)
        createFilterMenu();

    childBox.pack_start(checksBox);
    checksBox.pack_start(fileTypeCheckbox);
    checksBox.pack_start(previewCheckbox);
    checksBox.pack_start(svgexportCheckbox);

    set_extra_widget(childBox);

    // Let's do some customization
    fileNameEntry = dynamic_cast<Gtk::Entry *>(sp_search_by_name_recursive(this, "GtkEntry"));
    if (fileNameEntry) {
        // Catch when user hits [return] on the text field
        fileNameEntry->signal_activate().connect(
            sigc::mem_fun(*this, &FileSaveDialogImplGtk::fileNameEntryChangedCallback));
    }

    if (auto const expander = dynamic_cast<Gtk::Expander *>(sp_search_by_name_recursive(this, "GtkExpander"))) {
        // Always show the file list
        expander->set_expanded(true);
    }

    signal_selection_changed().connect(sigc::mem_fun(*this, &FileSaveDialogImplGtk::fileNameChanged));

    // allow easy access to the user's own templates folder
    using namespace Inkscape::IO::Resource;
    char const *templates = Inkscape::IO::Resource::get_path(USER, TEMPLATES);
    if (Inkscape::IO::file_test(templates, G_FILE_TEST_EXISTS) &&
        Inkscape::IO::file_test(templates, G_FILE_TEST_IS_DIR) && g_path_is_absolute(templates)) {
        add_shortcut_folder(templates);
    }

    add_button(_("_Cancel"), Gtk::RESPONSE_CANCEL);
    set_default(*add_button(_("_Save"), Gtk::RESPONSE_OK));

    show_all_children();
}

/**
 * Callback for fileNameEntry widget
 */
void FileSaveDialogImplGtk::fileNameEntryChangedCallback()
{
    if (!fileNameEntry)
        return;

    Glib::ustring fileName = fileNameEntry->get_text();
    if (!Glib::get_charset()) // If we are not utf8
        fileName = Glib::filename_to_utf8(fileName);

    // g_message("User hit return.  Text is '%s'\n", fileName.c_str());

    if (!Glib::path_is_absolute(fileName)) {
        // try appending to the current path
        // not this way: fileName = get_current_folder() + "/" + fileName;
        std::vector<Glib::ustring> pathSegments;
        pathSegments.emplace_back(get_current_folder());
        pathSegments.push_back(fileName);
        fileName = Glib::build_filename(pathSegments);
    }

    // g_message("path:'%s'\n", fileName.c_str());

    if (Glib::file_test(fileName, Glib::FILE_TEST_IS_DIR)) {
        set_current_folder(fileName);
    } else if (/*Glib::file_test(fileName, Glib::FILE_TEST_IS_REGULAR)*/ true) {
        // dialog with either (1) select a regular file or (2) cd to dir
        // simulate an 'OK'
        set_filename(fileName);
        response(Gtk::RESPONSE_OK);
    }
}



/**
 * Callback for fileNameEntry widget
 */
void FileSaveDialogImplGtk::filterChangedCallback()
{
    if (auto iter = filterComboBox->get_active())
        setExtension((*iter)[FilterList.extension]);
    if (!fromCB)
        updateNameAndExtension();
}

void FileSaveDialogImplGtk::fileNameChanged() {
    Glib::ustring name = get_filename();
    Glib::ustring::size_type pos = name.rfind('.');
    if ( pos == Glib::ustring::npos ) return;
    Glib::ustring ext = name.substr( pos ).casefold();
    if (auto output = dynamic_cast<Inkscape::Extension::Output *>(_extension))
        if (Glib::ustring(output->get_extension()).casefold() == ext)
            return;
    if (knownExtensions.find(ext) == knownExtensions.end()) return;
    fromCB = true;
    filterComboBox->set_active_text(knownExtensions[ext]->get_filetypename(true));
}

void FileSaveDialogImplGtk::createFilterMenu()
{
    Inkscape::Extension::DB::OutputList extension_list;
    Inkscape::Extension::db.get_output_list(extension_list);
    knownExtensions.clear();

    addFilter(_("Guess from extension"), "*");

    for (auto omod : extension_list) {
        // Export types are either exported vector types, or any raster type.
        if (!omod->is_exported() && omod->is_raster() != (_dialogType == EXPORT_TYPES))
            continue;

        // This extension is limited to save copy only.
        if (omod->savecopy_only() && save_method != Inkscape::Extension::FILE_SAVE_METHOD_SAVE_COPY)
            continue;

        Glib::ustring extension = omod->get_extension();
        addFilter(omod->get_filetypename(true), extension, omod);
        knownExtensions.insert(std::pair<Glib::ustring, Inkscape::Extension::Output*>(extension.casefold(), omod));
    }

    filterComboBox->set_active(0);
    filterChangedCallback(); // call at least once to set the filter
}

/**
 * Show this dialog modally.  Return true if user hits [OK]
 */
bool FileSaveDialogImplGtk::show()
{
    change_path(getFilename());
    set_modal(TRUE); // Window
    sp_transientize(GTK_WIDGET(gobj())); // Make transient
    gint b = dialog_run(*this); // Dialog
    svgPreview.showNoPreview();
    set_preview_widget_active(false);

    if (b == Gtk::RESPONSE_OK) {
        updateNameAndExtension();
        Inkscape::Preferences *prefs = Inkscape::Preferences::get();

        // Store changes of the "Append filename automatically" checkbox back to preferences.
        if (save_method == Inkscape::Extension::FILE_SAVE_METHOD_SAVE_COPY) {
            prefs->setBool("/dialogs/save_copy/append_extension", fileTypeCheckbox.get_active());
        } else {
            prefs->setBool("/dialogs/save_as/append_extension", fileTypeCheckbox.get_active());
        }

        auto extension = getExtension();
        Inkscape::Extension::store_file_extension_in_prefs((extension != nullptr ? extension->get_id() : ""), save_method);

        cleanup(true);

        return true;
    } else {
        cleanup(false);
        return false;
    }
}

void FileSaveDialogImplGtk::setExtension(Inkscape::Extension::Extension *key)
{
    // If no pointer to extension is passed in, look up based on filename extension.
    if (!key) {
        auto fn = getFilename().casefold();

        for (auto const &iter : knownExtensions) {
            auto ext = Glib::ustring(iter.second->get_extension()).casefold();
            if (Glib::str_has_suffix(fn, ext))
                key = iter.second;
        }
    }

    FileDialog::setExtension(key);

    // Ensure the proper entry in the combo box is selected.
    if (auto omod = dynamic_cast<Inkscape::Extension::Output *>(key)) {
        filterComboBox->set_active_text(omod->get_filetypename(true));
    }
}

Glib::ustring FileSaveDialogImplGtk::getCurrentDirectory()
{
    return get_current_folder();
}


/**
  * Change the default save path location.
  */
void FileSaveDialogImplGtk::change_path(const Glib::ustring &path)
{
    setFilename(path);

    if (Glib::file_test(_filename, Glib::FILE_TEST_IS_DIR)) {
        // fprintf(stderr,"set_current_folder(%s)\n",_filename.c_str());
        set_current_folder(_filename);
    } else {
        // fprintf(stderr,"set_filename(%s)\n",_filename.c_str());
        if (Glib::file_test(_filename, Glib::FILE_TEST_EXISTS)) {
            set_filename(_filename);
        } else {
            std::string dirName = Glib::path_get_dirname(_filename);
            if (dirName != get_current_folder()) {
                set_current_folder(dirName);
            }
        }
        Glib::ustring basename = Glib::path_get_basename(_filename);
        // fprintf(stderr,"set_current_name(%s)\n",basename.c_str());
        try
        {
            set_current_name(Glib::filename_to_utf8(basename));
        }
        catch (Glib::ConvertError &e)
        {
            g_warning("Error converting save filename to UTF-8.");
            // try a fallback.
            set_current_name(basename);
        }
    }
}

void FileSaveDialogImplGtk::updateNameAndExtension()
{
    // Pick up any changes the user has typed in.
    Glib::ustring tmp = get_filename();

    if (tmp.empty()) {
        tmp = get_uri();
    }

    if (!tmp.empty()) {
        setFilename(tmp);
    }

    auto output = dynamic_cast<Inkscape::Extension::Output *>(getExtension());
    if (fileTypeCheckbox.get_active() && output) {
        // Append the file extension if it's not already present and display it in the file name entry field
        appendExtension(_filename, output);
        change_path(_filename);
    }
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

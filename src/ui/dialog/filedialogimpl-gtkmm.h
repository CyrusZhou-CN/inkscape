// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * @brief Implementation of the file dialog interfaces defined in filedialogimpl.h
 */
/* Authors:
 *   Bob Jamison
 *   Johan Engelen <johan@shouraizou.nl>
 *   Joel Holdsworth
 *   Bruno Dilly
 *   Other dudes from The Inkscape Organization
 *
 * Copyright (C) 2004-2008 Authors
 * Copyright (C) 2004-2007 The Inkscape Organization
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef SEEN_FILE_DIALOG_IMPL_GTKMM_H
#define SEEN_FILE_DIALOG_IMPL_GTKMM_H

#include <vector>
#include <glib/gstdio.h>
#include <glibmm/refptr.h>
#include <glibmm/ustring.h>
#include <gtkmm/box.h>
#include <gtkmm/checkbutton.h>
#include <gtkmm/comboboxtext.h>
#include <gtkmm/enums.h>
#include <gtkmm/filechooserdialog.h>

#include "filedialog.h"
#include "svg-preview.h"

namespace Gtk {
class Entry;
class Expander;
class FileFilter;
class ListStore;
class Window;
} // namespace Gtk

namespace Inkscape {

class URI;

namespace UI {

namespace View {
class SVGViewWidget;
} // namespace View

namespace Dialog {

/*#########################################################################
### F I L E     D I A L O G    B A S E    C L A S S
#########################################################################*/

/**
 * This class is the base implementation for the others.  This
 * reduces redundancies and bugs.
 */
class FileDialogBaseGtk : public Gtk::FileChooserDialog
{
public:
    FileDialogBaseGtk(Gtk::Window &parentWindow, Glib::ustring const &title,
    		      Gtk::FileChooser::Action dialogType, FileDialogType type,
                      char const *preferenceBase);
    ~FileDialogBaseGtk() override;

    /**
     * Add a Gtk filter to our specially controlled filter dropdown.
     */
    Glib::RefPtr<Gtk::FileFilter> addFilter(const Glib::ustring &name, Glib::ustring pattern = "",
                                            Inkscape::Extension::Extension *mod = nullptr);

    Glib::ustring extToPattern(const Glib::ustring &extension) const;

    virtual void filterChangedCallback() {}

protected:

    Glib::ustring const _preferenceBase;

    /**
     * What type of 'open' are we? (open, import, place, etc)
     */
    FileDialogType _dialogType;

    /**
     * Our svg preview widget
     */
    SVGPreview svgPreview;

    /**
     * Aquired Widgets
     */
    Gtk::ComboBoxText *filterComboBox;

private:
    /**
     * Callback for seeing if the preview needs to be drawn
     */
    void _updatePreviewCallback();

    /**
     * Overriden filter store.
     */
    Glib::RefPtr<Gtk::ListStore> filterStore;
};

/*#########################################################################
### F I L E    O P E N
#########################################################################*/

/**
 * Our implementation class for the FileOpenDialog interface..
 */
class FileOpenDialogImplGtk final
    : public FileOpenDialog
      , public FileDialogBaseGtk
{
public:
    FileOpenDialogImplGtk(Gtk::Window& parentWindow,
    		       const Glib::ustring &dir,
                       FileDialogType fileTypes,
                       const Glib::ustring &title);

    bool show() final;

    Glib::ustring getFilename();

    std::vector<Glib::ustring> getFilenames() final;

    Glib::ustring getCurrentDirectory() final;

    void addFilterMenu(const Glib::ustring &name, Glib::ustring pattern = "",
                       Inkscape::Extension::Extension *mod = nullptr) final
    {
        addFilter(name, pattern, mod);
    }

private:
    /**
     *  Create a filter menu for this type of dialog
     */
    void createFilterMenu();
};

//########################################################################
//# F I L E    S A V E
//########################################################################

/**
 * Our implementation of the FileSaveDialog interface.
 */
class FileSaveDialogImplGtk final
    : public FileSaveDialog
    , public FileDialogBaseGtk
{
public:
    FileSaveDialogImplGtk(Gtk::Window &parentWindow,
                          const Glib::ustring &dir,
                          FileDialogType fileTypes,
                          const Glib::ustring &title,
                          const Glib::ustring &default_key,
                          const gchar* docTitle,
                          const Inkscape::Extension::FileSaveMethod save_method);

    bool show() final;
    Glib::ustring getCurrentDirectory() final;
    void setExtension(Inkscape::Extension::Extension *key) final;

    void addFilterMenu(const Glib::ustring &name, Glib::ustring pattern = {},
                       Inkscape::Extension::Extension *mod = nullptr) final
    {
        addFilter(name, pattern, mod);
    }

private:
    void change_path(const Glib::ustring& path);
    void updateNameAndExtension();

    /**
     * The file save method (essentially whether the dialog was invoked by "Save as ..." or "Save a
     * copy ..."), which is used to determine file extensions and save paths.
     */
    Inkscape::Extension::FileSaveMethod save_method;

    /**
     * Fix to allow the user to type the file name
     */
    Gtk::Entry *fileNameEntry;

    /**
     * Callback for user input into fileNameEntry
     */
    void filterChangedCallback() final;

    /**
     *  Create a filter menu for this type of dialog
     */
    void createFilterMenu();

    /**
     * Callback for user input into fileNameEntry
     */
    void fileNameChanged();
    bool fromCB;
};

} // namespace Dialog
} // namespace UI
} // namespace Inkscape

#endif // SEEN_FILE_DIALOG_IMPL_GTKMM_H

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

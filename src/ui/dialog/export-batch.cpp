// SPDX-License-Identifier: GPL-2.0-or-later
/* Authors:
 *   Lauris Kaplinski <lauris@kaplinski.com>
 *   bulia byak <buliabyak@users.sf.net>
 *   Johan Engelen <j.b.c.engelen@ewi.utwente.nl>
 *   Anshudhar Kumar Singh <anshudhar2001@gmail.com>
 *
 * Copyright (C) 1999-2007, 2021 Authors
 * Copyright (C) 2001-2002 Ximian, Inc.
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include <glibmm/convert.h>
#include <glibmm/i18n.h>
#include <glibmm/miscutils.h>
#include <gtkmm.h>
#include <png.h>
#include <regex>

#include "desktop.h"
#include "document-undo.h"
#include "document.h"
#include "extension/db.h"
#include "extension/output.h"
#include "file.h"
#include "helper/auto-connection.h"
#include "helper/png-write.h"
#include "inkscape-window.h"
#include "inkscape.h"
#include "io/resource.h"
#include "io/sys.h"
#include "layer-manager.h"
#include "message-stack.h"
#include "object/object-set.h"
#include "object/sp-namedview.h"
#include "object/sp-page.h"
#include "object/sp-root.h"
#include "page-manager.h"
#include "preferences.h"
#include "selection-chemistry.h"
#include "ui/dialog-events.h"
#include "ui/dialog/export.h"
#include "ui/dialog/export-batch.h"
#include "ui/dialog/dialog-notebook.h"
#include "ui/dialog/filedialog.h"
#include "ui/interface.h"
#include "ui/widget/color-picker.h"
#include "ui/widget/export-lists.h"
#include "ui/widget/export-preview.h"
#include "ui/widget/scrollprotected.h"
#include "ui/widget/unit-menu.h"

namespace Inkscape {
namespace UI {
namespace Dialog {

BatchItem::BatchItem(SPItem *item, std::shared_ptr<PreviewDrawing> drawing)
{
    _item = item;
    init(drawing);
    _object_modified_conn = _item->connectModified([=](SPObject *obj, unsigned int flags) {
        update_label();
    });
    update_label();
}

BatchItem::BatchItem(SPPage *page, std::shared_ptr<PreviewDrawing> drawing)
{
    _page = page;
    init(drawing);
    _object_modified_conn = _page->connectModified([=](SPObject *obj, unsigned int flags) {
        update_label();
    });
    update_label();
}

void BatchItem::update_label()
{
    Glib::ustring label = "no-name";
    if (_page) {
        label = _page->getDefaultLabel();
        if (auto id = _page->label()) {
            label = id;
        }
    } else if (_item) {
        label = _item->defaultLabel();
        if (label.empty()) {
            if (auto _id = _item->getId()) {
                label = _id;
            } else {
                label = "no-id";
            }
        }
    }
    _label_str = label;
    _label.set_text(label);
    set_tooltip_text(label);
}

void BatchItem::init(std::shared_ptr<PreviewDrawing> drawing) {


    _grid.set_row_spacing(5);
    _grid.set_column_spacing(5);
    _grid.set_valign(Gtk::Align::ALIGN_CENTER);

    _selector.set_active(true);
    _selector.set_can_focus(false);
    _selector.set_margin_start(2);
    _selector.set_margin_bottom(2);
    _selector.set_valign(Gtk::ALIGN_END);

    _option.set_active(false);
    _option.set_can_focus(false);
    _option.set_margin_start(2);
    _option.set_margin_bottom(2);
    _option.set_valign(Gtk::ALIGN_END);

    _preview.set_name("export_preview_batch");
    _preview.setItem(_item);
    _preview.setDrawing(drawing);
    _preview.setSize(64);
    _preview.set_halign(Gtk::ALIGN_CENTER);
    _preview.set_valign(Gtk::ALIGN_CENTER);

    _label.set_width_chars(10);
    _label.set_ellipsize(Pango::ELLIPSIZE_END);
    _label.set_halign(Gtk::Align::ALIGN_CENTER);

    set_valign(Gtk::Align::ALIGN_START);
    set_halign(Gtk::Align::ALIGN_START);
    add(_grid);
    set_visible(true);
    this->set_can_focus(false);

    _selector.signal_toggled().connect([=]() {
        set_selected(_selector.get_active());
    });
    _option.signal_toggled().connect([=]() {
        set_selected(_option.get_active());
    });

    // This initially packs the widgets with a hidden preview.
    refresh(!is_hide, 0);
}

/**
 * Syncronise the FlowBox selection to the active widget activity.
 */
void BatchItem::set_selected(bool selected)
{
    auto box = dynamic_cast<Gtk::FlowBox *>(get_parent());
    if (box && selected != is_selected()) {
        if (selected) {
            box->select_child(*this);
        } else {
            box->unselect_child(*this);
        }
    }
}

/**
 * Syncronise the FlowBox selection to the existing active widget state.
 */
void BatchItem::update_selected()
{
    if (auto parent = dynamic_cast<Gtk::FlowBox *>(get_parent()))
        on_mode_changed(parent->get_selection_mode());
    if (_selector.get_visible()) {
        set_selected(_selector.get_active());
    } else if (_option.get_visible()) {
        set_selected(_option.get_active());
    }
}

/**
 * A change in the selection mode for the flow box.
 */
void BatchItem::on_mode_changed(Gtk::SelectionMode mode)
{
    _selector.set_visible(mode == Gtk::SELECTION_MULTIPLE);
    _option.set_visible(mode == Gtk::SELECTION_SINGLE);
}

/**
 * Update the connection to the parent FlowBox
 */
void BatchItem::on_parent_changed(Gtk::Widget *previous) {
    auto parent = dynamic_cast<Gtk::FlowBox *>(get_parent());
    if (!parent)
        return;

    _selection_widget_changed_conn = parent->signal_selected_children_changed().connect([=]() {
        // Syncronise the active widget state to the Flowbox selection.
        if (_selector.get_visible()) {
            _selector.set_active(is_selected());
        } else if (_option.get_visible()) {
            _option.set_active(is_selected());
        }
    });
    update_selected();

    if (auto first = dynamic_cast<BatchItem *>(parent->get_child_at_index(0))) {
        auto group = first->get_radio_group();
        _option.set_group(group);
    }
}


void BatchItem::refresh(bool hide, guint32 bg_color)
{
    if (_page) {
        _preview.setBox(_page->getDocumentRect());
    }

    _preview.setBackgroundColor(bg_color);

    // When hiding the preview, we show the items as a checklist
    // So all items must be packed differently on refresh.
    if (hide != is_hide) {
        is_hide = hide;
        _grid.remove(_selector);
        _grid.remove(_option);
        _grid.remove(_label);
        _grid.remove(_preview);

        if (hide) {
            _selector.set_valign(Gtk::Align::ALIGN_BASELINE);
            _label.set_xalign(0.0);
            _grid.attach(_selector, 0, 1, 1, 1);
            _grid.attach(_option, 0, 1, 1, 1);
            _grid.attach(_label, 1, 1, 1, 1);
        } else {
            _selector.set_valign(Gtk::Align::ALIGN_END);
            _label.set_xalign(0.5);
            _grid.attach(_selector, 0, 1, 1, 1);
            _grid.attach(_option, 0, 1, 1, 1);
            _grid.attach(_label, 0, 2, 2, 1);
            _grid.attach(_preview, 0, 0, 2, 2);
        }
        show_all_children();
        update_selected();
    }

    if (!hide) {
        _preview.queueRefresh();
    }
}


BatchExport::BatchExport(BaseObjectType *cobject, const Glib::RefPtr<Gtk::Builder>& builder)
    : Gtk::Box(cobject) {
    prefs = Inkscape::Preferences::get();

    builder->get_widget("b_s_selection", selection_buttons[SELECTION_SELECTION]);
    selection_names[SELECTION_SELECTION] = "selection";
    builder->get_widget("b_s_layers", selection_buttons[SELECTION_LAYER]);
    selection_names[SELECTION_LAYER] = "layer";
    builder->get_widget("b_s_pages", selection_buttons[SELECTION_PAGE]);
    selection_names[SELECTION_PAGE] = "page";

    builder->get_widget("b_preview_box", preview_container);
    builder->get_widget("b_show_preview", show_preview);
    builder->get_widget("b_num_elements", num_elements);
    builder->get_widget("b_hide_all", hide_all);
    builder->get_widget("b_filename", filename_entry);
    builder->get_widget("b_export", export_btn);
    builder->get_widget("b_cancel", cancel_btn);
    builder->get_widget("b_inprogress", progress_box);

    builder->get_widget("b_progress", _prog);
    builder->get_widget("b_progress_batch", _prog_batch);
    builder->get_widget_derived("b_export_list", export_list);

    Gtk::Button* button = nullptr;
    builder->get_widget("b_backgnd", button); 
    assert(button);
    _bgnd_color_picker = std::make_unique<Inkscape::UI::Widget::ColorPicker>(
        _("Background color"), _("Color used to fill the image background"), 0xffffff00, true, button);
    setup();
}

void BatchExport::selectionModified(Inkscape::Selection *selection, guint flags)
{
    if (!_desktop || _desktop->getSelection() != selection) {
        return;
    }
    if (!(flags & (SP_OBJECT_MODIFIED_FLAG | SP_OBJECT_PARENT_MODIFIED_FLAG | SP_OBJECT_CHILD_MODIFIED_FLAG))) {
        return;
    }
    queueRefreshItems();
}

void BatchExport::selectionChanged(Inkscape::Selection *selection)
{
    if (!_desktop || _desktop->getSelection() != selection) {
        return;
    }
    selection_buttons[SELECTION_SELECTION]->set_sensitive(!selection->isEmpty());
    if (selection->isEmpty()) {
        if (current_key == SELECTION_SELECTION) {
            selection_buttons[SELECTION_LAYER]->set_active(true); // This causes refresh area
            // return otherwise refreshArea will be called again
            // even though we are at default key, selection is the one which was original key.
            prefs->setString("/dialogs/export/batchexportarea/value", selection_names[SELECTION_SELECTION]);
            return;
        }
    } else {
        Glib::ustring pref_key_name = prefs->getString("/dialogs/export/batchexportarea/value");
        if (selection_names[SELECTION_SELECTION] == pref_key_name && current_key != SELECTION_SELECTION) {
            selection_buttons[SELECTION_SELECTION]->set_active();
            return;
        }
    }
    queueRefresh();
}

void BatchExport::pagesChanged()
{
    if (!_desktop || !_document) return;

    bool has_pages = _document->getPageManager().hasPages();
    selection_buttons[SELECTION_PAGE]->set_sensitive(has_pages);

    if (current_key == SELECTION_PAGE && !has_pages) {
        current_key = SELECTION_LAYER;
        selection_buttons[SELECTION_LAYER]->set_active();
    }

    queueRefresh();
}

// Setup Single Export.Called by export on realize
void BatchExport::setup()
{
    if (setupDone) {
        return;
    }
    setupDone = true;

    export_list->setup();

    // set them before connecting to signals
    setDefaultSelectionMode();
    setExporting(false);
    queueRefresh();

    // Connect Signals
    for (auto [key, button] : selection_buttons) {
        button->signal_toggled().connect(sigc::bind(sigc::mem_fun(*this, &BatchExport::onAreaTypeToggle), key));
    }
    show_preview->signal_toggled().connect(sigc::mem_fun(*this, &BatchExport::refreshPreview));
    filename_conn = filename_entry->signal_changed().connect(sigc::mem_fun(*this, &BatchExport::onFilenameModified));
    export_conn = export_btn->signal_clicked().connect(sigc::mem_fun(*this, &BatchExport::onExport));
    cancel_conn = cancel_btn->signal_clicked().connect(sigc::mem_fun(*this, &BatchExport::onCancel));
    browse_conn = filename_entry->signal_icon_release().connect(sigc::mem_fun(*this, &BatchExport::onBrowse));
    hide_all->signal_toggled().connect(sigc::mem_fun(*this, &BatchExport::refreshPreview));
    _bgnd_color_picker->connectChanged([=](guint32 color){
        if (_desktop) {
            Inkscape::UI::Dialog::set_export_bg_color(_desktop->getNamedView(), color);
        }
        refreshPreview();
    });
}

void BatchExport::refreshItems()
{
    if (!_desktop || !_document) return;

    // Create New List of Items
    std::set<SPItem *> itemsList;
    std::set<SPPage *, SPPage::PageIndexOrder> pageList;
    std::set<SPPage *> pageUnsorted;

    char *num_str = nullptr;
    switch (current_key) {
        case SELECTION_SELECTION: {
            auto items = _desktop->getSelection()->items();
            for (auto i = items.begin(); i != items.end(); ++i) {
                if (SPItem *item = *i) {
                    // Ignore empty items (empty groups, other bad items)
                    if (item->visualBounds()) {
                        itemsList.insert(item);
                    }
                }
            }
            num_str = g_strdup_printf(ngettext("%d Item", "%d Items", itemsList.size()), (int)itemsList.size());
            break;
        }
        case SELECTION_LAYER: {
            for (auto layer : _desktop->layerManager().getAllLayers()) {
                // Ignore empty layers, they have no size.
                if (layer->geometricBounds()) {
                    itemsList.insert(layer);
                }
            }
            num_str = g_strdup_printf(ngettext("%d Layer", "%d Layers", itemsList.size()), (int)itemsList.size());
            break;
        }
        case SELECTION_PAGE: {
            for (auto page : _desktop->getDocument()->getPageManager().getPages()) {
                pageList.insert(page);
                pageUnsorted.insert(page);
            }
            num_str = g_strdup_printf(ngettext("%d Page", "%d Pages", pageList.size()), (int)pageList.size());
            break;
        }
        default:
            break;
    }
    if (num_str) {
        num_elements->set_text(num_str);
        g_free(num_str);
    }

    // Create a list of items which are already present but will be removed as they are not present anymore
    std::vector<std::string> toRemove;
    for (auto &[key, val] : current_items) {
        if (SPItem *item = val->getItem()) {
            // if item is not present in itemList add it to remove list so that we can remove it
            auto itemItr = itemsList.find(item);
            if (itemItr == itemsList.end() || !(*itemItr)->getId() || (*itemItr)->getId() != key) {
                toRemove.push_back(key);
            }
        }
        if (SPPage *page = val->getPage()) {
            auto pageItr = pageUnsorted.find(page);
            if (pageItr == pageUnsorted.end() || !(*pageItr)->getId() || (*pageItr)->getId() != key) {
                toRemove.push_back(key);
            }
        }
    }

    // now remove all the items
    for (auto const &key : toRemove) {
        if (current_items[key]) {
            preview_container->remove(*current_items[key]);
            current_items.erase(key);
        }
    }

    // now add which were are new
    for (auto &item : itemsList) {
        if (auto id = item->getId()) {
            // If an Item with same Id is already present, Skip
            if (current_items[id] && current_items[id]->getItem() == item) {
                continue;
            }
            // Add new item to the end of list
            current_items[id] = std::make_unique<BatchItem>(item, _preview_drawing);
            preview_container->insert(*current_items[id], -1);
            current_items[id]->set_selected(true);
        }
    }
    for (auto &page : pageList) {
        if (auto id = page->getId()) {
            if (current_items[id] && current_items[id]->getPage() == page) {
                continue;
            }
            current_items[id] = std::make_unique<BatchItem>(page, _preview_drawing);
            preview_container->insert(*current_items[id], -1);
            current_items[id]->set_selected(true);
        }
    }

    refreshPreview();
}

void BatchExport::refreshPreview()
{
    if (!_desktop) return;

    // For Batch Export we are now hiding all object except current object
    bool hide = hide_all->get_active();
    bool preview = show_preview->get_active();
    preview_container->set_orientation(preview ? Gtk::ORIENTATION_HORIZONTAL : Gtk::ORIENTATION_VERTICAL);

    if (preview) {
        std::vector<SPItem *> selected;
        for (auto &[key, val] : current_items) {
            if (hide) {
                // Assumption: This will never alternate between these branches in the same
                // list of current_items. Either it's a selection, layers xor pages.
                if (auto item = val->getItem()) {
                    selected.push_back(item);
                } else if (val->getPage()) {
                    auto sels = _desktop->getSelection()->items();
                    selected = std::vector<SPItem *>(sels.begin(), sels.end());
                    break;
                }
            }
        }
        _preview_drawing->set_shown_items(std::move(selected));

        for (auto &[key, val] : current_items) {
            val->refresh(!preview, _bgnd_color_picker->get_current_color());
        }
    }
}

void BatchExport::loadExportHints()
{
    if (!_desktop) return;

    SPDocument *doc = _desktop->getDocument();
    auto old_filename = filename_entry->get_text();
    if (old_filename.empty()) {
        Glib::ustring filename = doc->getRoot()->getExportFilename();
        if (filename.empty()) {
            Glib::ustring filename_entry_text = filename_entry->get_text();
            Glib::ustring extension = ".png";
            filename = Export::defaultFilename(doc, original_name, extension);
        }
        filename_entry->set_text(filename);
        filename_entry->set_position(filename.length());
        doc_export_name = filename;
    }
}

// Signals CallBack

void BatchExport::onAreaTypeToggle(selection_mode key)
{
    // Prevent executing function twice
    if (!selection_buttons[key]->get_active()) {
        return;
    }
    // If you have reached here means the current key is active one ( not sure if multiple transitions happen but
    // last call will change values)
    current_key = key;
    prefs->setString("/dialogs/export/batchexportarea/value", selection_names[current_key]);

    queueRefresh();
}

void BatchExport::onFilenameModified()
{
}

void BatchExport::onCancel()
{
    interrupted = true;
    setExporting(false);
}

void BatchExport::onExport()
{
    interrupted = false;
    if (!_desktop)
        return;

    // If there are no selected button, simply flash message in status bar
    int num = current_items.size();
    if (current_items.size() == 0) {
        _desktop->messageStack()->flash(Inkscape::ERROR_MESSAGE, _("No items selected."));
        return;
    }

    setExporting(true);

    // Find and remove any extension from filename so that we can add suffix to it.
    Glib::ustring filename = filename_entry->get_text();
    export_list->removeExtension(filename);

    bool hide = hide_all->get_active();
    auto sels = _desktop->getSelection()->items();
    std::vector<SPItem *> selected_items(sels.begin(), sels.end());

    // Start Exporting Each Item
    int num_rows = export_list->get_rows();
    for (int j = 0; j < num_rows && !interrupted; j++) {

        auto suffix = export_list->get_suffix(j);
        auto ext = export_list->getExtension(j);
        float dpi = export_list->get_dpi(j);

        if (!ext || ext->deactivated()) {
            continue;
        }

        int count = 0;
        for (auto i = current_items.begin(); i != current_items.end() && !interrupted; ++i) {
            count++;

            auto &batchItem = i->second;
            if (!batchItem->is_selected()) {
                continue;
            }

            SPItem *item = batchItem->getItem();
            SPPage *page = batchItem->getPage();

            std::vector<SPItem *> show_only;
            Geom::Rect area;
            if (item) {
                if (auto bounds = item->documentVisualBounds()) {
                    area = *bounds;
                } else {
                    continue;
                }
                show_only.emplace_back(item);
            } else if (page) {
                area = page->getDesktopRect();
                show_only = selected_items; // Maybe stuff here
            } else {
                continue;
            }

            Glib::ustring id = batchItem->getLabel();
            if (id.empty()) {
                continue;
            }

            Glib::ustring item_filename = filename;
            if (!filename.empty()) {
                Glib::ustring::value_type last_char = filename.at(filename.length() - 1);
                if (last_char != '/' && last_char != '\\') {
                    item_filename += "_";
                }
            }
            if (id.at(0) == '#' && batchItem->getItem() && !batchItem->getItem()->label()) {
                item_filename += id.substr(1);
            } else {
                item_filename += id;
            }

            if (!suffix.empty()) {
                if (ext->is_raster()) {
                    // Put the dpi in at the user's requested location.
                    suffix = std::regex_replace(suffix.c_str(), std::regex("\\{dpi\\}"), std::to_string((int)dpi));
                }
                item_filename = item_filename + "_" + suffix;
            }

            bool found = Export::unConflictFilename(_document, item_filename, ext->get_extension());
            if (!found) {
                continue;
            }

            // Set the progress bar with our updated information
            double progress = (((double)count / num) + j) / num_rows;
            _prog_batch->set_fraction(progress);

            setExporting(true,
                         Glib::ustring::compose(_("Exporting %1"), item_filename),
                         Glib::ustring::compose(_("Format %1, Selection %2"), j + 1, count));


            if (ext->is_raster()) {
                unsigned long int width = (int)(area.width() * dpi / DPI_BASE + 0.5);
                unsigned long int height = (int)(area.height() * dpi / DPI_BASE + 0.5);

                Export::exportRaster(
                    area, width, height, dpi, _bgnd_color_picker->get_current_color(),
                    item_filename, true, onProgressCallback, this, ext, hide ? &show_only : nullptr);
            } else {
                auto copy_doc = _document->copy();
                Export::exportVector(ext, copy_doc.get(), item_filename, true, show_only, page);
            }
        }
    }
    // Do this right at the end to finish up
    setExporting(false);
}

void BatchExport::onBrowse(Gtk::EntryIconPosition pos, const GdkEventButton *ev)
{
    if (!_app || !_app->get_active_window()) {
        return;
    }
    Gtk::Window *window = _app->get_active_window();
    browse_conn.block();
    Glib::ustring filename = Glib::filename_from_utf8(filename_entry->get_text());

    if (filename.empty()) {
        filename = Export::defaultFilename(_document, filename, ".png");
    }

    Inkscape::UI::Dialog::FileSaveDialog *dialog = Inkscape::UI::Dialog::FileSaveDialog::create(
        *window, filename, Inkscape::UI::Dialog::EXPORT_TYPES, _("Select a filename for exporting"), "", "",
        Inkscape::Extension::FILE_SAVE_METHOD_EXPORT);

    if (dialog->show()) {
        filename = dialog->getFilename();
        // Remove extension and don't add a new one, for obvious reasons.
        export_list->removeExtension(filename);

        filename_entry->set_text(filename);
        filename_entry->set_position(filename.length());

        // deleting dialog before exporting is important
        // proper delete function should be made for dialog IMO
        delete dialog;
    } else {
        delete dialog;
    }
    browse_conn.unblock();
}

void BatchExport::setDefaultSelectionMode()
{
    current_key = (selection_mode)0; // default key
    bool found = false;
    Glib::ustring pref_key_name = prefs->getString("/dialogs/export/batchexportarea/value");
    for (auto [key, name] : selection_names) {
        if (pref_key_name == name) {
            current_key = key;
            found = true;
            break;
        }
    }
    if (!found) {
        pref_key_name = selection_names[current_key];
    }
    if (_desktop) {
        if (auto _sel = _desktop->getSelection()) {
            selection_buttons[SELECTION_SELECTION]->set_sensitive(!_sel->isEmpty());
        }
        selection_buttons[SELECTION_PAGE]->set_sensitive(_document->getPageManager().hasPages());
    }
    if (!selection_buttons[current_key]->get_sensitive()) {
        current_key = SELECTION_LAYER;
    }
    selection_buttons[current_key]->set_active(true);

    // we need to set pref key because signals above will set set pref == current key but we sometimes change
    // current key like selection key
    prefs->setString("/dialogs/export/batchexportarea/value", pref_key_name);
}

void BatchExport::setExporting(bool exporting, Glib::ustring const &text, Glib::ustring const &text_batch)
{
    if (exporting) {
        set_sensitive(false);
        set_opacity(0.2);
        progress_box->set_visible(true);
        _prog->set_text(text);
        _prog->set_fraction(0.0);
        _prog_batch->set_text(text_batch);
    } else {
        set_sensitive(true);
        set_opacity(1.0);
        progress_box->set_visible(false);
        _prog->set_text("");
        _prog->set_fraction(0.0);
        _prog_batch->set_text("");
    }
}

unsigned int BatchExport::onProgressCallback(float value, void *data)
{
    if (auto bi = static_cast<BatchExport *>(data)) {
        bi->_prog->set_fraction(value);
        auto main_context = Glib::MainContext::get_default();
        main_context->iteration(false);
        return !bi->interrupted;
    }
    return false;
}

void BatchExport::setDesktop(SPDesktop *desktop)
{
    if (desktop != _desktop) {
        _pages_changed_connection.disconnect();
        _desktop = desktop;
    }
}

void BatchExport::setDocument(SPDocument *document)
{
    if (!_desktop) {
        document = nullptr;
    }
    if (_document == document)
        return;

    _document = document;
    _pages_changed_connection.disconnect();
    if (document) {
        // when the page selected is changed, update the export area
        _pages_changed_connection = document->getPageManager().connectPagesChanged([=]() { pagesChanged(); });

        auto bg_color = get_export_bg_color(document->getNamedView(), 0xffffff00);
        _bgnd_color_picker->setRgba32(bg_color);
        _preview_drawing = std::make_shared<PreviewDrawing>(document);
    } else {
        _preview_drawing.reset();
    }

    refreshItems();
}

void BatchExport::queueRefreshItems()
{
    if (refresh_items_conn) {
        return;
    }
    // Asynchronously refresh the preview
    refresh_items_conn = Glib::signal_idle().connect([this] {
        refreshItems();
        return false;
    }, Glib::PRIORITY_HIGH);
}

void BatchExport::queueRefresh()
{
    if (refresh_conn) {
        return;
    }
    refresh_conn = Glib::signal_idle().connect([this] {
        refreshItems();
        loadExportHints();
        return false;
    }, Glib::PRIORITY_HIGH);
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

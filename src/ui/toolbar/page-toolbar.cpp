// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * @file
 * Page aux toolbar: Temp until we convert all toolbars to ui files with Gio::Actions.
 */
/* Authors:
 *   Martin Owens <doctormo@geek-2.com>

 * Copyright (C) 2021 Tavmjong Bah
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "page-toolbar.h"

#include <glibmm/i18n.h>
#include <gtkmm.h>
#include <regex>

#include "desktop.h"
#include "document-undo.h"
#include "document.h"
#include "extension/db.h"
#include "extension/template.h"
#include "io/resource.h"
#include "object/sp-namedview.h"
#include "object/sp-page.h"
#include "ui/builder-utils.h"
#include "ui/icon-names.h"
#include "ui/themes.h"
#include "ui/tools/pages-tool.h"
#include "util/paper.h"
#include "util/units.h"

using Inkscape::IO::Resource::UIS;

namespace Inkscape {
namespace UI {
namespace Toolbar {

class SearchCols : public Gtk::TreeModel::ColumnRecord
{
public:
    // These types must match those for the model in the ui file
    SearchCols()
    {
        add(name);
        add(label);
        add(key);
    }
    Gtk::TreeModelColumn<Glib::ustring> name;  // translated name
    Gtk::TreeModelColumn<Glib::ustring> label; // translated label
    Gtk::TreeModelColumn<Glib::ustring> key;
};

PageToolbar::PageToolbar(BaseObjectType *cobject, const Glib::RefPtr<Gtk::Builder> &builder, SPDesktop *desktop)
    : Gtk::Toolbar(cobject)
    , _desktop(desktop)
    , combo_page_sizes(nullptr)
    , text_page_label(nullptr)
{
    builder->get_widget("page_sizes", combo_page_sizes);
    builder->get_widget("page_margins", text_page_margins);
    builder->get_widget("page_bleeds", text_page_bleeds);
    builder->get_widget("page_label", text_page_label);
    builder->get_widget("page_pos", label_page_pos);
    builder->get_widget("page_backward", btn_page_backward);
    builder->get_widget("page_foreward", btn_page_foreward);
    builder->get_widget("page_delete", btn_page_delete);
    builder->get_widget("page_move_objects", btn_move_toggle);
    builder->get_widget("sep1", sep1);

    sizes_list = Glib::RefPtr<Gtk::ListStore>::cast_dynamic(
        builder->get_object("page_sizes_list")
    );
    sizes_search = Glib::RefPtr<Gtk::ListStore>::cast_dynamic(
        builder->get_object("page_sizes_search")
    );
    sizes_searcher = Glib::RefPtr<Gtk::EntryCompletion>::cast_dynamic(
        builder->get_object("sizes_searcher")
    );

    builder->get_widget("margin_popover", margin_popover);
    builder->get_widget_derived("margin_top", margin_top);
    builder->get_widget_derived("margin_right", margin_right);
    builder->get_widget_derived("margin_bottom", margin_bottom);
    builder->get_widget_derived("margin_left", margin_left);

    if (text_page_label) {
        text_page_label->signal_changed().connect(sigc::mem_fun(*this, &PageToolbar::labelEdited));
    }
    if (sizes_searcher) {
        sizes_searcher->signal_match_selected().connect([=](const Gtk::TreeModel::iterator &iter) {
            SearchCols cols;
            Gtk::TreeModel::Row row = *(iter);
            Glib::ustring preset_key = row[cols.key];
            sizeChoose(preset_key);
            return false;
        }, false);
    }
    text_page_bleeds->signal_activate().connect(sigc::mem_fun(*this, &PageToolbar::bleedsEdited));
    text_page_margins->signal_activate().connect(sigc::mem_fun(*this, &PageToolbar::marginsEdited));
    text_page_margins->signal_icon_press().connect([=](Gtk::EntryIconPosition, const GdkEventButton*){
        if (auto page = _document->getPageManager().getSelected()) {
            auto margin = page->getMargin();
            auto unit = _document->getDisplayUnit()->abbr;
            margin_top->set_value(margin.top().toValue(unit));
            margin_right->set_value(margin.right().toValue(unit));
            margin_bottom->set_value(margin.bottom().toValue(unit));
            margin_left->set_value(margin.left().toValue(unit));
            text_page_bleeds->set_text(page->getBleedLabel());
        }
        margin_popover->set_visible(true);
    });
    margin_top->signal_value_changed().connect(sigc::mem_fun(*this, &PageToolbar::marginTopEdited));
    margin_right->signal_value_changed().connect(sigc::mem_fun(*this, &PageToolbar::marginRightEdited));
    margin_bottom->signal_value_changed().connect(sigc::mem_fun(*this, &PageToolbar::marginBottomEdited));
    margin_left->signal_value_changed().connect(sigc::mem_fun(*this, &PageToolbar::marginLeftEdited));

    if (combo_page_sizes) {
        combo_page_sizes->set_id_column(2);
        combo_page_sizes->signal_changed().connect([=] {
            std::string preset_key = combo_page_sizes->get_active_id();
            sizeChoose(preset_key);
        });
        entry_page_sizes = dynamic_cast<Gtk::Entry *>(combo_page_sizes->get_child());
        if (entry_page_sizes) {
            entry_page_sizes->set_placeholder_text(_("ex.: 100x100cm"));
            entry_page_sizes->set_tooltip_text(_("Type in width & height of a page. (ex.: 15x10cm, 10in x 100mm)\n"
                                                 "or choose preset from dropdown."));
            entry_page_sizes->get_style_context()->add_class("symbolic");
            entry_page_sizes->signal_activate().connect(sigc::mem_fun(*this, &PageToolbar::sizeChanged));
            entry_page_sizes->signal_icon_press().connect([=](Gtk::EntryIconPosition, const GdkEventButton*){
                _document->getPageManager().changeOrientation();
                DocumentUndo::maybeDone(_document, "page-resize", _("Resize Page"), INKSCAPE_ICON("tool-pages"));
                setSizeText();
            });
            entry_page_sizes->signal_focus_in_event().connect([=](GdkEventFocus *) {
                setSizeText(nullptr, false); // Show just raw dimensions when user starts editing
                return false;
            });
            entry_page_sizes->signal_focus_out_event().connect([=](GdkEventFocus *) {
               if (_document)
                   setSizeText(nullptr, true);
               return false;
            });
            populate_sizes();
        }
    }

    // Watch for when the tool changes
    _ec_connection = _desktop->connectEventContextChanged(sigc::mem_fun(*this, &PageToolbar::toolChanged));
    _doc_connection = _desktop->connectDocumentReplaced([=](SPDesktop *desktop, SPDocument *doc) {
        if (doc) {
            toolChanged(desktop, desktop->getEventContext());
        }
    });

    // Constructed by a builder, so we're going to protect the widget from destruction.
    this->reference();
    was_referenced = true;
}

/**
 * Take all selectable page sizes and add to search and dropdowns
 */
void PageToolbar::populate_sizes()
{
    SearchCols cols;

    Inkscape::Extension::DB::TemplateList extensions;
    Inkscape::Extension::db.get_template_list(extensions);

    for (auto tmod : extensions) {
        if (!tmod->can_resize())
            continue;
        for (auto preset : tmod->get_presets()) {
            auto label = preset->get_label();
            if (!label.empty()) label = _(label.c_str());

            if (preset->is_visible(Inkscape::Extension::TEMPLATE_SIZE_LIST)) {
                // Goes into drop down
                Gtk::TreeModel::Row row = *(sizes_list->append());
                row[cols.name] = _(preset->get_name().c_str());
                row[cols.label] = " <small><span fgalpha=\"50%\">" + label + "</span></small>";
                row[cols.key] = preset->get_key();
            }
            if (preset->is_visible(Inkscape::Extension::TEMPLATE_SIZE_SEARCH)) {
                // Goes into text search
                Gtk::TreeModel::Row row = *(sizes_search->append());
                row[cols.name] = _(preset->get_name().c_str());
                row[cols.label] = label;
                row[cols.key] = preset->get_key();
            }
        }
    }
}

void PageToolbar::on_parent_changed(Gtk::Widget *)
{
    if (was_referenced) {
        // Undo the gtkbuilder protection now that we have a parent
        this->unreference();
        was_referenced = false;
    }
}

PageToolbar::~PageToolbar()
{
    _ec_connection.disconnect();
    _doc_connection.disconnect();
    toolChanged(nullptr, nullptr);
}

void PageToolbar::toolChanged(SPDesktop *desktop, Inkscape::UI::Tools::ToolBase *ec)
{
    // Disconnect previous page changed signal
    _page_selected.disconnect();
    _pages_changed.disconnect();
    _page_modified.disconnect();
    _document = nullptr;

    if (dynamic_cast<Inkscape::UI::Tools::PagesTool *>(ec)) {
        // Save the document and page_manager for future use.
        if ((_document = desktop->getDocument())) {
            auto &page_manager = _document->getPageManager();
            // Connect the page changed signal and indicate changed
            _pages_changed = page_manager.connectPagesChanged(sigc::mem_fun(*this, &PageToolbar::pagesChanged));
            _page_selected = page_manager.connectPageSelected(sigc::mem_fun(*this, &PageToolbar::selectionChanged));
            // Update everything now.
            pagesChanged();
        }
    }
}

void PageToolbar::labelEdited()
{
    auto text = text_page_label->get_text();
    if (auto page = _document->getPageManager().getSelected()) {
        page->setLabel(text.empty() ? nullptr : text.c_str());
        DocumentUndo::maybeDone(_document, "page-relabel", _("Relabel Page"), INKSCAPE_ICON("tool-pages"));
    }
}

void PageToolbar::bleedsEdited()
{
    auto text = text_page_bleeds->get_text();

    // And modifiction to the bleed causes pages to be enabled
    auto &pm = _document->getPageManager();
    pm.enablePages();

    if (auto page = pm.getSelected()) {
        page->setBleed(text);
        DocumentUndo::maybeDone(_document, "page-bleed", _("Edit page bleed"), INKSCAPE_ICON("tool-pages"));

        auto bleed = page->getBleed();
        text_page_bleeds->set_text(page->getBleedLabel());
    }
}

void PageToolbar::marginsEdited()
{
    auto text = text_page_margins->get_text();

    // And modifiction to the margin causes pages to be enabled
    auto &pm = _document->getPageManager();
    pm.enablePages();

    if (auto page = pm.getSelected()) {
        page->setMargin(text);
        DocumentUndo::maybeDone(_document, "page-margin", _("Edit page margin"), INKSCAPE_ICON("tool-pages"));
        setMarginText(page);
    }
}

void PageToolbar::marginTopEdited()
{
    marginSideEdited(0, margin_top->get_text());
}
void PageToolbar::marginRightEdited()
{
    marginSideEdited(1, margin_right->get_text());
}
void PageToolbar::marginBottomEdited()
{
    marginSideEdited(2, margin_bottom->get_text());
}
void PageToolbar::marginLeftEdited()
{
    marginSideEdited(3, margin_left->get_text());
}
void PageToolbar::marginSideEdited(int side, const Glib::ustring &value)
{
    // And modifiction to the margin causes pages to be enabled
    auto &pm = _document->getPageManager();
    pm.enablePages();

    if (auto page = pm.getSelected()) {
        page->setMarginSide(side, value, false);
        DocumentUndo::maybeDone(_document, "page-margin", _("Edit page margin"), INKSCAPE_ICON("tool-pages"));
        setMarginText(page);
    }
}

void PageToolbar::sizeChoose(const std::string &preset_key)
{
    if (auto preset = Extension::Template::get_any_preset(preset_key)) {
        auto &pm = _document->getPageManager();
        // The page orientation is a part of the toolbar widget, so we pass this
        // as a specially named pref, the extension can then decide to use it or not.
        auto p_rect = pm.getSelectedPageRect();
        std::string orient = p_rect.width() > p_rect.height() ? "land" : "port";

        auto page = pm.getSelected();
        preset->resize_to_template(_document, page, {
            {"orientation", orient},
        });
        if (page) {
            page->setSizeLabel(preset->get_name());
        }

        setSizeText();
        DocumentUndo::maybeDone(_document, "page-resize", _("Resize Page"), INKSCAPE_ICON("tool-pages"));
    } else {
        // Page not found, i.e., "Custom" was selected or user is typing in.
        entry_page_sizes->grab_focus();
    }
}

/**
 * Convert the parsed sections of a text input into a desktop pixel value.
 */
double PageToolbar::_unit_to_size(std::string number, std::string unit_str, std::string backup)
{
    // We always support comma, even if not in that particular locale.
    std::replace(number.begin(), number.end(), ',', '.');
    double value = std::stod(number);

    // Get the best unit, for example 50x40cm means cm for both
    if (unit_str.empty() && !backup.empty())
        unit_str = backup;
    if (unit_str == "\"")
        unit_str = "in";

    // Output is always in px as it's the most useful.
    auto px = Inkscape::Util::unit_table.getUnit("px");

    // Convert from user entered unit to display unit
    if (!unit_str.empty())
        return Inkscape::Util::Quantity::convert(value, unit_str, px);

    // Default unit is the document's display unit
    auto unit = _document->getDisplayUnit();
    return Inkscape::Util::Quantity::convert(value, unit, px);
}

/**
 * A manually typed input size, parse out what we can understand from
 * the text or ignore it if the text can't be parsed.
 *
 * Format: 50cm x 40mm
 *         20',40"
 *         30,4-40.2
 */
void PageToolbar::sizeChanged()
{
    // Parse the size out of the typed text if possible.
    auto text = std::string(combo_page_sizes->get_active_text());
    // This does not support negative values, because pages can not be negatively sized.
    static std::string arg = "([0-9]+[\\.,]?[0-9]*|\\.[0-9]+) ?(px|mm|cm|in|\\\")?";
    // We can't support × here since it's UTF8 and this doesn't match
    static std::regex re_size("^ *" + arg + " *([ *Xx,\\-]) *" + arg + " *$");

    std::smatch matches;
    if (std::regex_match(text, matches, re_size)) {
        // Convert the desktop px back into document units for 'resizePage'
        double width = _unit_to_size(matches[1], matches[2], matches[5]);
        double height = _unit_to_size(matches[4], matches[5], matches[2]);
        if (width > 0 && height > 0) {
            _document->getPageManager().resizePage(width, height);
        }
    }
    setSizeText();
}

/**
 * Sets the size of the current page into the entry page size.
 */
void PageToolbar::setSizeText(SPPage *page, bool display_only)
{
    SearchCols cols;

    if (!page)
        page = _document->getPageManager().getSelected();

    auto label = _document->getPageManager().getSizeLabel(page);

    // If this is a known size in our list, add the size paren to it.
    for (auto iter : sizes_search->children()) {
        auto row = *iter;
        if (label == row[cols.name]) {
            label = label + " (" + row[cols.label] + ")";
            break;
        }
    }
    entry_page_sizes->set_text(label);


    // Orientation button
    auto box = page ? page->getDesktopRect() : *_document->preferredBounds();
    std::string icon = box.width() > box.height() ? "page-landscape" : "page-portrait";
    if (box.width() == box.height()) {
        entry_page_sizes->unset_icon(Gtk::ENTRY_ICON_SECONDARY);
    } else {
        entry_page_sizes->set_icon_from_icon_name(INKSCAPE_ICON(icon), Gtk::ENTRY_ICON_SECONDARY);
    }

    if (!display_only) {
        // The user has started editing the combo box; we set up a convenient initial state.
        // Select text if box is currently in focus.
        if (entry_page_sizes->has_focus()) {
            entry_page_sizes->select_region(0, -1);
        }
    }
}

void PageToolbar::setMarginText(SPPage *page)
{
    text_page_margins->set_text(page ? page->getMarginLabel() : "");
    text_page_margins->set_sensitive(true);
}

void PageToolbar::pagesChanged()
{
    selectionChanged(_document->getPageManager().getSelected());
}

void PageToolbar::selectionChanged(SPPage *page)
{
    _page_modified.disconnect();
    auto &page_manager = _document->getPageManager();
    text_page_label->set_tooltip_text(_("Page label"));

    setMarginText(page);

    // Set label widget content with page label.
    if (page) {
        text_page_label->set_sensitive(true);
        text_page_label->set_placeholder_text(page->getDefaultLabel());

        if (auto label = page->label()) {
            text_page_label->set_text(label);
        } else {
            text_page_label->set_text("");
        }


        // TRANSLATORS: "%1" is replaced with the page we are on, and "%2" is the total number of pages.
        auto label = Glib::ustring::compose(_("%1/%2"), page->getPagePosition(), page_manager.getPageCount());
        label_page_pos->set_label(label);

        _page_modified = page->connectModified([=](SPObject *obj, unsigned int flags) {
            if (auto page = cast<SPPage>(obj)) {
                // Make sure we don't 'select' on removal of the page
                if (flags & SP_OBJECT_MODIFIED_FLAG) {
                    selectionChanged(page);
                }
            }
        });
    } else {
        text_page_label->set_text("");
        text_page_label->set_sensitive(false);
        text_page_label->set_placeholder_text(_("Single Page Document"));
        label_page_pos->set_label(_("1/-"));

        _page_modified = _document->connectModified([=](guint) {
            selectionChanged(nullptr);
        });
    }
    if (!page_manager.hasPrevPage() && !page_manager.hasNextPage() && !page) {
        sep1->set_visible(false);
        label_page_pos->get_parent()->set_visible(false);
        btn_page_backward->set_visible(false);
        btn_page_foreward->set_visible(false);
        btn_page_delete->set_visible(false);
        btn_move_toggle->set_sensitive(false);
    } else {
        // Set the forward and backward button sensitivities
        sep1->set_visible(true);
        label_page_pos->get_parent()->set_visible(true);
        btn_page_backward->set_visible(true);
        btn_page_foreward->set_visible(true);
        btn_page_backward->set_sensitive(page_manager.hasPrevPage());
        btn_page_foreward->set_sensitive(page_manager.hasNextPage());
        btn_page_delete->set_visible(true);
        btn_move_toggle->set_sensitive(true);
    }
    setSizeText(page);
}

GtkWidget *PageToolbar::create(SPDesktop *desktop)
{
    PageToolbar *toolbar = nullptr;
    auto builder = Inkscape::UI::create_builder("toolbar-page.ui");
    builder->get_widget_derived("page-toolbar", toolbar, desktop);

    if (!toolbar) {
        std::cerr << "InkscapeWindow: Failed to load page toolbar!" << std::endl;
        return nullptr;
    }
    // This widget will be auto-freed by the builder unless you have called reference();
    return GTK_WIDGET(toolbar->gobj());
}


} // namespace Toolbar
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

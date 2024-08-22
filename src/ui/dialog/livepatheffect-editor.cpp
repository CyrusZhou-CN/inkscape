// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * @brief A dialog for Live Path Effects (LPE)
 */
/* Authors:
 *   Jabiertxof
 *   Adam Belis (UX/Design)
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include <cstddef>
#include <algorithm>
#include <tuple>
#include <glibmm/i18n.h>

#include "livepatheffect-editor.h"
#include "livepatheffect-add.h"
#include "live_effects/effect.h"
#include "live_effects/effect-enum.h"
#include "live_effects/lpeobject.h"
#include "live_effects/lpeobject-reference.h"
#include "io/resource.h"
#include "object/sp-flowtext.h"
#include "object/sp-item-group.h"
#include "object/sp-lpe-item.h"
#include "object/sp-path.h"
#include "object/sp-shape.h"
#include "object/sp-text.h"
#include "object/sp-tspan.h"
#include "object/sp-use.h"
#include "svg/svg.h"
#include "ui/builder-utils.h"
#include "ui/controller.h"
#include "ui/icon-loader.h"
#include "ui/icon-names.h"
#include "ui/manage.h"
#include "ui/tools/node-tool.h"
#include "ui/widget/custom-tooltip.h"
#include "util/optstr.h"

namespace Inkscape {
namespace UI {

bool sp_can_apply_lpeffect(SPLPEItem* item, LivePathEffect::EffectType etype) {
    if (!item) return false;

    auto shape = cast<SPShape>(item);
    auto path = cast<SPPath>(item);
    auto group = cast<SPGroup>(item);
    Glib::ustring item_type;
    if (group) {
        item_type = "group";
    } else if (path) {
        item_type = "path";
    } else if (shape) {
        item_type = "shape";
    }
    bool has_clip = item->getClipObject() != nullptr;
    bool has_mask = item->getMaskObject() != nullptr;
    bool applicable = true;
    if (!has_clip && etype == LivePathEffect::POWERCLIP) {
        applicable = false;
    }
    if (!has_mask && etype == LivePathEffect::POWERMASK) {
        applicable = false;
    }
    if (item_type == "group" && !Inkscape::LivePathEffect::LPETypeConverter.get_on_group(etype)) {
        applicable = false;
    } else if (item_type == "shape" && !Inkscape::LivePathEffect::LPETypeConverter.get_on_shape(etype)) {
        applicable = false;
    } else if (item_type == "path" && !Inkscape::LivePathEffect::LPETypeConverter.get_on_path(etype)) {
        applicable = false;
    }
    return applicable;
}

void sp_apply_lpeffect(SPDesktop* desktop, SPLPEItem* item, LivePathEffect::EffectType etype) {
    if (!sp_can_apply_lpeffect(item, etype)) return;

    Glib::ustring key = Inkscape::LivePathEffect::LPETypeConverter.get_key(etype);
    LivePathEffect::Effect::createAndApply(key.c_str(), item->document, item);
    item->getCurrentLPE()->refresh_widgets = true;
    DocumentUndo::done(item->document, _("Create and apply path effect"), INKSCAPE_ICON("dialog-path-effects"));

    if (desktop) {
        // this is rotten - UI LPE knots refresh
        // force selection change
        desktop->getSelection()->clear();
        desktop->getSelection()->add(item);
        Inkscape::UI::Tools::sp_update_helperpath(desktop);
    }
}

namespace Dialog {

/*####################
 * Callback functions
 */

bool sp_has_fav(Glib::ustring effect)
{
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    Glib::ustring favlist = prefs->getString("/dialogs/livepatheffect/favs");
    size_t pos = favlist.find(effect);
    if (pos != Glib::ustring::npos) {
        return true;
    }
    return false;
}

void sp_add_fav(Glib::ustring effect)
{
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    Glib::ustring favlist = prefs->getString("/dialogs/livepatheffect/favs");
    if (!sp_has_fav(effect)) {
        prefs->setString("/dialogs/livepatheffect/favs", favlist + effect + ";");
    }
}

void sp_remove_fav(Glib::ustring effect)
{
    if (sp_has_fav(effect)) {
        Inkscape::Preferences *prefs = Inkscape::Preferences::get();
        Glib::ustring favlist = prefs->getString("/dialogs/livepatheffect/favs");
        effect += ";";
        size_t pos = favlist.find(effect);
        if (pos != Glib::ustring::npos) {
            favlist.erase(pos, effect.length());
            prefs->setString("/dialogs/livepatheffect/favs", favlist);
        }
    }
}

void sp_toggle_fav(Glib::ustring effect, Gtk::MenuItem *LPEtoggleFavorite)
{
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    Glib::ustring favlist = prefs->getString("/dialogs/livepatheffect/favs");
    if (sp_has_fav(effect)) {
        sp_remove_fav(effect);
        LPEtoggleFavorite->set_label(_("Set Favorite"));
    } else {
        sp_add_fav(effect);
        LPEtoggleFavorite->set_label(_("Unset Favorite"));
    }
}

void LivePathEffectEditor::selectionChanged(Inkscape::Selection * selection)
{
    if (selection_changed_lock) {
        return;
    }
    onSelectionChanged(selection);
    clearMenu();
}
void LivePathEffectEditor::selectionModified(Inkscape::Selection * selection, guint flags)
{
    current_lpeitem = cast<SPLPEItem>(selection->singleItem());
    if (!selection_changed_lock && current_lpeitem && effectlist != current_lpeitem->getEffectList()) {
        onSelectionChanged(selection);
    } else if (current_lpeitem && current_lperef.first) {
        showParams(current_lperef, false);
    }
    clearMenu();
}

/**
 * Constructor
 */
LivePathEffectEditor::LivePathEffectEditor()
    : DialogBase("/dialogs/livepatheffect", "LivePathEffect"),
    _builder(create_builder("dialog-livepatheffect.glade")),
    LPEListBox(get_widget<Gtk::ListBox>(_builder, "LPEListBox")),
    _LPEContainer(get_widget<Gtk::Box>(_builder, "LPEContainer")),
    _LPEAddContainer(get_widget<Gtk::Box>(_builder, "LPEAddContainer")),
    _LPEParentBox(get_widget<Gtk::ListBox>(_builder, "LPEParentBox")),
    _LPECurrentItem(get_widget<Gtk::Box>(_builder, "LPECurrentItem")),
    _LPESelectionInfo(get_widget<Gtk::Label>(_builder, "LPESelectionInfo")),
    _LPEGallery(get_widget<Gtk::Button>(_builder, "LPEGallery")),
    _showgallery_observer(Preferences::PreferencesObserver::create(
        "/dialogs/livepatheffect/showgallery", sigc::mem_fun(*this, &LivePathEffectEditor::on_showgallery_notify))),
    converter(Inkscape::LivePathEffect::LPETypeConverter)
{
    _LPEGallery.signal_clicked().connect(sigc::mem_fun(*this, &LivePathEffectEditor::onAddGallery));
    _showgallery_observer->call(); // Set initial visibility per Preference (widget is :no-show-all)

    _LPEContainer.signal_map().connect(sigc::mem_fun(*this, &LivePathEffectEditor::map_handler) );

    Controller::add_click(_LPEContainer, [this](Gtk::GestureMultiPress const &, int, double, double)
    {
        dnd = false; // hack to fix DnD freezing expander
        return Gtk::EVENT_SEQUENCE_NONE;
    });

    setMenu();
    add(_LPEContainer);
    selection_info();

    _lpes_popup.get_entry().set_placeholder_text(_("Add Live Path Effect"));
    _lpes_popup.on_match_selected().connect([=](int id){ onAdd((LivePathEffect::EffectType)id); });
    _lpes_popup.on_button_press().connect([=](){ setMenu(); });
    _lpes_popup.on_focus().connect([=](){ setMenu(); return true; });
    _LPEAddContainer.pack_start(_lpes_popup);

    show_all();
}

LivePathEffectEditor::~LivePathEffectEditor()
{
    sp_clear_custom_tooltip();
}

bool separator_func(const Glib::RefPtr<Gtk::TreeModel>& model,
                    const Gtk::TreeModel::iterator& iter) {
    Gtk::TreeModel::Row row = *iter;
    bool *separator;
    row->get_value(3, separator);
    return separator;
}

bool
LivePathEffectEditor::is_appliable(LivePathEffect::EffectType etype, Glib::ustring item_type, bool has_clip, bool has_mask) {
    bool appliable = true;

    if (!has_clip && etype == LivePathEffect::POWERCLIP) {
        appliable = false;
    }
    if (!has_mask && etype == LivePathEffect::POWERMASK) {
        appliable = false;
    }
    if (item_type == "group" && !converter.get_on_group(etype)) {
        appliable = false;
    } else if (item_type == "shape" && !converter.get_on_shape(etype)) {
        appliable = false;
    } else if (item_type == "path" && !converter.get_on_path(etype)) {
        appliable = false;
    }
    return appliable;
}

void align(Gtk::Widget* top, gint spinbutton_width_chars) {
    auto box = dynamic_cast<Gtk::Box*>(top);
    if (!box) return;
    box->set_spacing(2);

    // traverse container, locate n-th child in each row
    auto for_child_n = [=](int child_index, const std::function<void (Gtk::Widget*)>& action) {
        for (auto child : box->get_children()) {
            auto container = dynamic_cast<Gtk::Box*>(child);
            if (!container) continue;
            container->set_spacing(2);
            const auto& children = container->get_children();
            if (children.size() > child_index) {
                action(children[child_index]);
            }
        }
    };

    // column 0 - labels
    int max_width = 0;
    for_child_n(0, [&](Gtk::Widget* child){
        if (auto label = dynamic_cast<Gtk::Label*>(child)) {
            label->set_xalign(0); // left-align
            int label_width = 0, dummy = 0;
            label->get_preferred_width(dummy, label_width);
            if (label_width > max_width) {
                max_width = label_width;
            }
        }
    });
    // align
    for_child_n(0, [=](Gtk::Widget* child) {
        if (auto label = dynamic_cast<Gtk::Label*>(child)) {
            label->set_size_request(max_width);
        }
    });

    // column 1 - align spin buttons, if any
    int button_width = 0;
    for_child_n(1, [&](Gtk::Widget* child) {
        if (auto spin = dynamic_cast<Gtk::SpinButton*>(child)) {
            // selected spinbutton size by each LPE default 7
            spin->set_width_chars(spinbutton_width_chars);
            int dummy = 0;
            spin->get_preferred_width(dummy, button_width);
        } 
    });
    // set min size for comboboxes, if any
    int combo_size = button_width > 0 ? button_width : 50; // match with spinbuttons, or just min of 50px
    for_child_n(1, [=](Gtk::Widget* child) {
        if (auto combo = dynamic_cast<Gtk::ComboBox*>(child)) {
            combo->set_size_request(combo_size);
        }
    });
}

void
LivePathEffectEditor::clearMenu()
{
    sp_clear_custom_tooltip();
    _reload_menu = true;
}

void
LivePathEffectEditor::toggleVisible(Inkscape::LivePathEffect::Effect *lpe , Gtk::Button *visbutton) {
    auto *visimage = dynamic_cast<Gtk::Image *>(visbutton->get_image());
    bool hide = false;
    if (!g_strcmp0(lpe->getRepr()->attribute("is_visible"),"true")) {
        visimage->set_from_icon_name("object-hidden-symbolic", Gtk::IconSize(Gtk::ICON_SIZE_SMALL_TOOLBAR));
        lpe->getRepr()->setAttribute("is_visible", "false");
        hide = true;
    } else {
        visimage->set_from_icon_name("object-visible-symbolic", Gtk::IconSize(Gtk::ICON_SIZE_SMALL_TOOLBAR));
        lpe->getRepr()->setAttribute("is_visible", "true");
    }
    lpe->doOnVisibilityToggled(current_lpeitem);
    DocumentUndo::done(getDocument(), hide ? _("Deactivate path effect") :  _("Activate path effect"), INKSCAPE_ICON("dialog-path-effects"));
}

const Glib::ustring& get_category_name(Inkscape::LivePathEffect::LPECategory category) {
    static const std::map<Inkscape::LivePathEffect::LPECategory, Glib::ustring> category_names = {
        { Inkscape::LivePathEffect::LPECategory::Favorites,     _("Favorites")    },
        { Inkscape::LivePathEffect::LPECategory::EditTools,     _("Edit/Tools")   },
        { Inkscape::LivePathEffect::LPECategory::Distort,       _("Distort")      },
        { Inkscape::LivePathEffect::LPECategory::Generate,      _("Generate")     },
        { Inkscape::LivePathEffect::LPECategory::Convert,       _("Convert")      },
        { Inkscape::LivePathEffect::LPECategory::Experimental,  _("Experimental") },
    };
    return category_names.at(category);
}

struct LPEMetadata {
    Inkscape::LivePathEffect::LPECategory category;
    Glib::ustring icon_name;
    Glib::ustring tooltip;
    bool sensitive;
};

static std::map<Inkscape::LivePathEffect::EffectType, LPEMetadata> g_lpes;
// populate popup with lpes and completion list for a search box
void LivePathEffectEditor::add_lpes(Inkscape::UI::Widget::CompletionPopup& popup, bool symbolic) {
    auto& menu = popup.get_menu();
    struct LPE {
        Inkscape::LivePathEffect::EffectType type;
        Glib::ustring label;
        Inkscape::LivePathEffect::LPECategory category;
        Glib::ustring icon_name;
        Glib::ustring tooltip;
        bool sensitive;
    };
    std::vector<LPE> lpes;
    lpes.reserve(g_lpes.size());
    for (auto&& lpe : g_lpes) {
        lpes.push_back({
            lpe.first,
            g_dpgettext2(0, "path effect", converter.get_label(lpe.first).c_str()),
            lpe.second.category,
            lpe.second.icon_name,
            lpe.second.tooltip,
            lpe.second.sensitive
        });
    }
    auto const tie = [](LPE const &lpe){ return std::tie(lpe.category, lpe.label); };
    std::sort(lpes.begin(), lpes.end(), [=](auto &l, auto &r){ return tie(l) < tie(r); });

    popup.clear_completion_list();

    // 2-column menu
    for (auto w:menu.get_children()) {
        menu.remove(*w);
    }
    Inkscape::UI::ColumnMenuBuilder<Inkscape::LivePathEffect::LPECategory> builder(menu, 3, Gtk::ICON_SIZE_LARGE_TOOLBAR);

    for (auto& lpe : lpes) {
        // build popup menu
        auto type = lpe.type;
        auto *menuitem = builder.add_item(lpe.label, lpe.category, lpe.tooltip, lpe.icon_name, lpe.sensitive, true, [=](){ onAdd((LivePathEffect::EffectType)type); });
        gint id = (gint)type;
        menuitem->property_has_tooltip() = true;
        menuitem->signal_query_tooltip().connect([=](int x, int y, bool kbd, const Glib::RefPtr<Gtk::Tooltip>& tooltipw){
            return sp_query_custom_tooltip(x, y, kbd, tooltipw, id, lpe.tooltip, lpe.icon_name);
        });
        if (builder.new_section()) {
            builder.set_section(get_category_name(lpe.category));
        }
    
        // build completion list
        if (lpe.sensitive) {
            popup.add_to_completion_list(static_cast<int>(lpe.type), lpe.label, lpe.icon_name + (symbolic ? "-symbolic" : ""));
        }
    }

    if (symbolic) {
        menu.get_style_context()->add_class("symbolic");
    }
}


void
LivePathEffectEditor::setMenu()
{
    if (!_reload_menu) {
        return;
    }

    _reload_menu = false;

    auto shape = cast<SPShape>(current_lpeitem);
    auto path = cast<SPPath>(current_lpeitem);
    auto group = cast<SPGroup>(current_lpeitem);
    bool has_clip = current_lpeitem && (current_lpeitem->getClipObject() != nullptr);
    bool has_mask = current_lpeitem && (current_lpeitem->getMaskObject() != nullptr);
    Glib::ustring item_type = "";
    if (group) {
        item_type = "group";
    } else if (path) {
        item_type = "path";
    } else if (shape) {
        item_type = "shape";
    }
    if (_item_type != item_type || has_clip != _has_clip || has_mask != _has_mask) {
        _item_type = item_type;
        _has_clip = has_clip;
        _has_mask = has_mask;
        g_lpes.clear();
        Inkscape::Preferences *prefs = Inkscape::Preferences::get();
        bool experimental = prefs->getBool("/dialogs/livepatheffect/showexperimental", false);
        std::map<Inkscape::LivePathEffect::LPECategory, std::map< Glib::ustring, const LivePathEffect::EnumEffectData<LivePathEffect::EffectType> *> > lpesorted;
        for (int i = 0; i < static_cast<int>(converter._length); ++i) {
            const LivePathEffect::EnumEffectData<LivePathEffect::EffectType> *data = &converter.data(i);
            const Glib::ustring label = _(converter.get_label(data->id).c_str());
            const Glib::ustring untranslated_label = converter.get_label(data->id);
            Glib::ustring name = label;
            if (untranslated_label != label) {
                name =+ "\n<span size='x-small'>" + untranslated_label + "</span>";
            }
            Inkscape::LivePathEffect::LPECategory category = converter.get_category(data->id);
            if (sp_has_fav(untranslated_label)) {
                //category = 0;
                category = Inkscape::LivePathEffect::LPECategory::Favorites;
            }
            if (!experimental && category == Inkscape::LivePathEffect::LPECategory::Experimental) {
                continue;
            }
            lpesorted[category][name] = data;
        }
        for (auto e : lpesorted) {
            for (auto e2 : e.second) {
                const Glib::ustring label = _(converter.get_label(e2.second->id).c_str());
                const Glib::ustring untranslated_label = converter.get_label(e2.second->id);
                Glib::ustring tooltip = _(converter.get_description(e2.second->id).c_str());
                if (untranslated_label != label) {
                    tooltip = "[" + untranslated_label + "] " + _(converter.get_description(e2.second->id).c_str());
                }            
                Glib::ustring name = label;
                Glib::ustring icon = converter.get_icon(e2.second->id);
                LPEMetadata mdata;
                mdata.category = e.first;
                mdata.icon_name = icon;
                mdata.tooltip = tooltip;
                mdata.sensitive = is_appliable(e2.second->id, item_type, has_clip, has_mask);
                g_lpes[e2.second->id] = mdata;
            }
        }
        auto symbolic = Inkscape::Preferences::get()->getBool("/theme/symbolicIcons", true);
        add_lpes(_lpes_popup, symbolic);
    }
}

void LivePathEffectEditor::onAdd(LivePathEffect::EffectType etype)
{
    selection_changed_lock = true;
    Glib::ustring key = converter.get_key(etype);
    SPLPEItem *fromclone = clonetolpeitem();
    if (fromclone) {
        current_lpeitem = fromclone;
        if (key == "clone_original") {
            current_lpeitem->getCurrentLPE()->refresh_widgets = true;
            selection_changed_lock = false;
            DocumentUndo::done(getDocument(), _("Create and apply path effect"), INKSCAPE_ICON("dialog-path-effects"));
            return;
        }
    }
    selection_changed_lock = false;
    if (current_lpeitem) {
        LivePathEffect::Effect::createAndApply(key.c_str(), getDocument(), current_lpeitem);
        current_lpeitem->getCurrentLPE()->refresh_widgets = true;
        DocumentUndo::done(getDocument(), _("Create and apply path effect"), INKSCAPE_ICON("dialog-path-effects"));
    }
}

void
LivePathEffectEditor::map_handler()
{
    ensure_size();
}

void
LivePathEffectEditor::selection_info() 
{
    auto selection = getSelection();
    SPItem * selected = nullptr;
    _LPESelectionInfo.set_visible(false);
    if (selection && (selected = selection->singleItem()) ) {
        if (is<SPText>(selected) || is<SPFlowtext>(selected)) {
            _LPESelectionInfo.set_text(_("Text objects do not support Live Path Effects"));
            _LPESelectionInfo.set_visible(true);
            Glib::ustring labeltext = _("Convert text to paths");
            auto const selectbutton = Gtk::make_managed<Gtk::Button>();
            auto const boxc = Gtk::make_managed<Gtk::Box>();
            auto const lbl = Gtk::make_managed<Gtk::Label>(labeltext);
            std::string shape_type = "group";
            std::string highlight = SPColor(selected->highlight_color()).toString();
            auto const type = Gtk::make_managed<Gtk::Image>(sp_get_shape_icon(shape_type, Gdk::RGBA(highlight), 20, 1));
            boxc->pack_start(*type, false, false);
            boxc->pack_start(*lbl, false, false);
            type->set_margin_start(4);
            type->set_margin_end(4);
            selectbutton->add(*boxc);
            selectbutton->signal_clicked().connect([=](){
                selection->toCurves();
            });
            _LPEParentBox.add(*selectbutton);
            Glib::ustring labeltext2 = _("Clone");
            auto const selectbutton2 = Gtk::make_managed<Gtk::Button>();
            auto const boxc2 = Gtk::make_managed<Gtk::Box>();
            auto const lbl2 = Gtk::make_managed<Gtk::Label>(labeltext2);
            std::string shape_type2 = "clone";
            std::string highlight2 = SPColor(selected->highlight_color()).toString();
            auto const type2 = Gtk::make_managed<Gtk::Image>(sp_get_shape_icon(shape_type2, Gdk::RGBA(highlight2), 20, 1));
            boxc2->pack_start(*type2, false, false);
            boxc2->pack_start(*lbl2, false, false);
            type2->set_margin_start(4);
            type2->set_margin_end(4);
            selectbutton2->add(*boxc2);
            selectbutton2->signal_clicked().connect([=](){
                selection->clone();;
            });
            _LPEParentBox.add(*selectbutton2);
            _LPEParentBox.show_all();
        } else if (!is<SPLPEItem>(selected) && !is<SPUse>(selected)) {
            _LPESelectionInfo.set_text(_("Select a path, shape, clone or group"));
            _LPESelectionInfo.set_visible(true);
        } else {
            if (selected->getId()) {
                Glib::ustring labeltext = selected->label() ? selected->label() : selected->getId();
                auto const boxc = Gtk::make_managed<Gtk::Box>();
                auto const lbl = Gtk::make_managed<Gtk::Label>(labeltext);
                lbl->set_ellipsize(Pango::ELLIPSIZE_END);
                std::string shape_type = selected->typeName();
                std::string highlight = SPColor(selected->highlight_color()).toString();
                auto const type = Gtk::make_managed<Gtk::Image>(sp_get_shape_icon(shape_type, Gdk::RGBA(highlight), 20, 1));
                boxc->pack_start(*type, false, false);
                boxc->pack_start(*lbl, false, false);
                _LPECurrentItem.add(*boxc);
                _LPECurrentItem.get_children()[0]->set_halign(Gtk::ALIGN_CENTER);
                _LPESelectionInfo.set_visible(false);
            }
            std::vector<std::pair <Glib::ustring, Glib::ustring> > newrootsatellites;
            for (auto root : selected->rootsatellites) {
                auto lpeobj = cast<LivePathEffectObject>(selected->document->getObjectById(root.second));
                Inkscape::LivePathEffect::Effect *lpe = nullptr;
                if (lpeobj) {
                    lpe = lpeobj->get_lpe();
                }
                if (lpe) {
                    const Glib::ustring label = _(converter.get_label(lpe->effectType()).c_str());
                    Glib::ustring labeltext = Glib::ustring::compose(_("Select %1 with %2 LPE"), root.first, label);
                    auto lpeitem = cast<SPLPEItem>(selected->document->getObjectById(root.first));
                    if (lpeitem && lpeitem->getLPEIndex(lpe) != Glib::ustring::npos) {
                        newrootsatellites.emplace_back(root.first, root.second);
                        auto const selectbutton = Gtk::make_managed<Gtk::Button>();
                        auto const boxc = Gtk::make_managed<Gtk::Box>();
                        auto const lbl = Gtk::make_managed<Gtk::Label>(labeltext);
                        std::string shape_type = selected->typeName();
                        std::string highlight = SPColor(selected->highlight_color()).toString();
                        auto const type = Gtk::make_managed<Gtk::Image>(sp_get_shape_icon(shape_type, Gdk::RGBA(highlight), 20, 1));
                        boxc->pack_start(*type, false, false);
                        boxc->pack_start(*lbl, false, false);
                        type->set_margin_start(4);
                        type->set_margin_end(4);
                        selectbutton->add(*boxc);
                        selectbutton->signal_clicked().connect([=](){
                            selection->set(lpeitem);
                        });
                        _LPEParentBox.add(*selectbutton);
                    }
                }
            }
            selected->rootsatellites = newrootsatellites;
            _LPEParentBox.show_all();
            _LPEParentBox.drag_dest_unset();
            _LPECurrentItem.show_all();
        }
    } else if (!selection || selection->isEmpty()) {
        _LPESelectionInfo.set_text(_("Select a path, shape, clone or group"));
        _LPESelectionInfo.set_visible(true);
    } else if (selection->size() > 1) {
        _LPESelectionInfo.set_text(_("Select only one path, shape, clone or group"));
        _LPESelectionInfo.set_visible(true);
    }
}

void
LivePathEffectEditor::onSelectionChanged(Inkscape::Selection *sel)
{
    SPUse *use = nullptr;
    _reload_menu = true;
    if ( sel && !sel->isEmpty() ) {
        SPItem *item = sel->singleItem();
        if ( item ) {
            auto lpeitem = cast<SPLPEItem>(item);
            use = cast<SPUse>(item);
            if (lpeitem) {
                lpeitem->update_satellites();
                current_lpeitem = lpeitem;
                _LPEAddContainer.set_sensitive(true);
                effect_list_reload(lpeitem);
                return;
            }
        }
    }
    current_lpeitem = nullptr;
    _LPEAddContainer.set_sensitive(use != nullptr);
    clear_lpe_list();
    selection_info();
}

void
LivePathEffectEditor::move_list(gint origin, gint dest)
{
    Inkscape::Selection *sel = getDesktop()->getSelection();

    if ( sel && !sel->isEmpty() ) {
        SPItem *item = sel->singleItem();
        if ( item ) {
            auto lpeitem = cast<SPLPEItem>(item);
            if ( lpeitem ) {
                lpeitem->movePathEffect(origin, dest);
            }
        }
    }
}

static const std::vector<Gtk::TargetEntry> entries = {Gtk::TargetEntry("GTK_LIST_BOX_ROW", Gtk::TARGET_SAME_APP, 0 )};

void
LivePathEffectEditor::showParams(std::pair<Gtk::Expander *, std::shared_ptr<Inkscape::LivePathEffect::LPEObjectReference> > expanderdata, bool changed)
{
    LivePathEffectObject *lpeobj = expanderdata.second->lpeobject;
   
    if (lpeobj) {
        Inkscape::LivePathEffect::Effect *lpe = lpeobj->get_lpe();
        if (lpe) {
            if (effectwidget && !lpe->refresh_widgets && expanderdata == current_lperef && !changed) {
                return;
            }
            if (effectwidget) {
                effectwidget->get_parent()->remove(*effectwidget);
                delete effectwidget;
                effectwidget = nullptr;
            }
            effectwidget = lpe->newWidget();
            if (!dynamic_cast<Gtk::Container *>(effectwidget)->get_children().size()) {
                auto * label = new Gtk::Label("", Gtk::ALIGN_START, Gtk::ALIGN_CENTER);
                label->set_markup(_("<small>Without parameters</small>"));
                label->set_margin_top(5);
                label->set_margin_bottom(5);
                label->set_margin_start(5);
                effectwidget = label;
            }
            expanderdata.first->add(*effectwidget);
            expanderdata.first->show_all_children();
            align(effectwidget, lpe->spinbutton_width_chars);
            // fixme: add resizing of dialog
            lpe->refresh_widgets = false;
            ensure_size();
        } else {
            current_lperef = std::make_pair(nullptr, nullptr);
        }
    } else {
        current_lperef = std::make_pair(nullptr, nullptr);
    }
}

static void
set_cursor(Gtk::Widget &widget, Glib::ustring const &name)
{
    auto const window = widget.get_window();
    auto const display = window->get_display();
    auto const cursor = Gdk::Cursor::create(display, name);
    window->set_cursor(cursor);
}

/*
 * First clears the effectlist_store, then appends all effects from the effectlist.
 */
void
LivePathEffectEditor::effect_list_reload(SPLPEItem *lpeitem)
{
    clear_lpe_list();
    _LPEExpanders.clear();
    auto gladefile = get_filename_string(Inkscape::IO::Resource::UIS, "dialog-livepatheffect-item.glade");
    gint counter = -1;
    Gtk::Expander *LPEExpanderCurrent = nullptr;
    effectlist = lpeitem->getEffectList();
    gint total = effectlist.size();
    if (total > 1) {
        _LPECurrentItem.drag_dest_unset();
        _lpes_popup.drag_dest_unset();
        _lpes_popup.get_entry().drag_dest_unset();
        _LPEAddContainer.drag_dest_unset();
        _LPEContainer.drag_dest_set(entries, Gtk::DEST_DEFAULT_ALL, Gdk::ACTION_MOVE);
        _LPEContainer.signal_drag_data_received().connect([=](const Glib::RefPtr<Gdk::DragContext>& context, int x, int y, const Gtk::SelectionData& selection_data, guint info, guint time)
        {
            if (dnd) {
                unsigned int pos_target, pos_source;
                Gtk::Widget *target = &_LPEContainer;
                pos_source = atoi(reinterpret_cast<char const*>(selection_data.get_data()));
                pos_target = LPEListBox.get_children().size()-1;
                if (y < 90) {
                    pos_target = 0;
                }
                if (pos_target == pos_source) {
                    gtk_drag_finish(context->gobj(), FALSE, FALSE, time);
                    dnd = false;
                    return;
                }
                Glib::RefPtr<Gtk::StyleContext> stylec = target->get_style_context();
                if (pos_source > pos_target) {
                    if (stylec->has_class("after")) {
                        pos_target ++;
                    }
                } else if (pos_source < pos_target) {
                    if (stylec->has_class("before")) {
                        pos_target --;
                    }
                }
                Gtk::Widget *source = LPEListBox.get_row_at_index(pos_source);
                g_object_ref(source->gobj());
                LPEListBox.remove(*source);
                LPEListBox.insert(*source, pos_target);
                g_object_unref(source->gobj());
                move_list(pos_source,pos_target);
                gtk_drag_finish(context->gobj(), TRUE, TRUE, time);
                dnd = false;
            }
        });
        _LPEContainer.signal_drag_motion().connect([=](const Glib::RefPtr<Gdk::DragContext>& context, int x, int y, guint time)
        {
            Glib::RefPtr<Gtk::StyleContext> stylec = _LPEContainer.get_style_context();
            if (y < 90) {
                stylec->add_class("before");
                stylec->remove_class("after");
            } else {
                stylec->remove_class("before");
                stylec->add_class("after");
            }
            return true;
        }, true);
    }
    PathEffectList::iterator it;
    Gtk::MenuItem *LPEMoveUpExtrem = nullptr;
    Gtk::MenuItem *LPEMoveDownExtrem = nullptr;
    Gtk::Button *LPEDrag = nullptr;
    for( it = effectlist.begin() ; it!=effectlist.end(); ++it)
    {
        if ( !(*it)->lpeobject ) {
            continue;
        }
        auto lpe = (*it)->lpeobject->get_lpe();
        bool current = lpeitem->getCurrentLPE() == lpe;
        counter++;
        Glib::RefPtr<Gtk::Builder> builder;
        if (lpe) {
            try {
                builder = Gtk::Builder::create_from_file(gladefile);
            } catch (const Glib::Error &ex) {
                g_warning("Glade file loading failed for path effect dialog");
                return;
            }
            Gtk::Box *LPEEffect;
            Gtk::Box *LPEExpanderBox;
            Gtk::Box *LPEActionButtons;
            Gtk::EventBox *LPEOpenExpander;
            Gtk::Expander *LPEExpander;
            Gtk::Image *LPEIconImage;
            Gtk::Button *LPEErase;
            Gtk::Button *LPEHide;
            Gtk::MenuItem *LPEtoggleFavorite;
            Gtk::Label *LPENameLabel;
            Gtk::Menu *LPEEffectMenu;
            Gtk::MenuItem *LPEMoveUp;
            Gtk::MenuItem *LPEMoveDown;
            Gtk::MenuItem *LPEResetDefault;
            Gtk::MenuItem *LPESetDefault;            
            builder->get_widget("LPEMoveUp", LPEMoveUp);
            builder->get_widget("LPEMoveDown", LPEMoveDown);
            builder->get_widget("LPEResetDefault", LPEResetDefault);
            builder->get_widget("LPESetDefault", LPESetDefault);
            builder->get_widget("LPENameLabel", LPENameLabel);
            builder->get_widget("LPEEffectMenu", LPEEffectMenu);
            builder->get_widget("LPEHide", LPEHide);
            builder->get_widget("LPEIconImage", LPEIconImage);
            builder->get_widget("LPEExpanderBox", LPEExpanderBox);
            builder->get_widget("LPEEffect", LPEEffect);
            builder->get_widget("LPEExpander", LPEExpander);
            builder->get_widget("LPEOpenExpander", LPEOpenExpander);
            builder->get_widget("LPEErase", LPEErase);
            builder->get_widget("LPEDrag", LPEDrag);
            builder->get_widget("LPEActionButtons", LPEActionButtons);
            builder->get_widget("LPEtoggleFavorite", LPEtoggleFavorite);
            LPEExpander->drag_dest_unset();
            LPEActionButtons->drag_dest_unset();
            LPEDrag->set_tooltip_text(_("Drag to change position in path effects stack"));
            if (current) {
                LPEExpanderCurrent = LPEExpander;
            }
            if (counter == 0) {
                LPEMoveUpExtrem = LPEMoveUp;
            }
            LPEMoveDownExtrem = LPEMoveDown;
            auto effectype = lpe->effectType();
            const Glib::ustring label = _(converter.get_label(effectype).c_str());
            const Glib::ustring untranslated_label = converter.get_label(effectype);
            const Glib::ustring icon = converter.get_icon(effectype);
            LPEIconImage->set_from_icon_name(icon, Gtk::IconSize(Gtk::ICON_SIZE_SMALL_TOOLBAR));
            Glib::ustring lpename = "";
            if (untranslated_label == label) {
                lpename = label;
            } else {
                lpename = (label + "\n<span size='x-small'>" + untranslated_label + "</span>");
            }
            auto *visimage = dynamic_cast<Gtk::Image *>(LPEHide->get_image());
            if (!g_strcmp0(lpe->getRepr()->attribute("is_visible"),"true")) {
                visimage->set_from_icon_name("object-visible-symbolic", Gtk::IconSize(Gtk::ICON_SIZE_SMALL_TOOLBAR));
            } else {
                visimage->set_from_icon_name("object-hidden-symbolic", Gtk::IconSize(Gtk::ICON_SIZE_SMALL_TOOLBAR));
            }
            _LPEExpanders.emplace_back(LPEExpander, (*it));
            LPEListBox.add(*LPEEffect);
            
            Glib::ustring name = "drag_";
            name += Glib::ustring::format(counter);
            LPEDrag->set_name(name);
            if (total > 1) {
                //DnD
                LPEDrag->drag_source_set(entries, Gdk::BUTTON1_MASK, Gdk::ACTION_MOVE);
            }
            Glib::ustring tooltip = _(converter.get_description(effectype).c_str());
            if (untranslated_label != label) {
                tooltip = "[" + untranslated_label + "] " + _(converter.get_description(effectype).c_str());
            } 
            gint id = (gint)effectype;
            LPEExpanderBox->property_has_tooltip() = true;
            LPEExpanderBox->signal_query_tooltip().connect([=](int x, int y, bool kbd, const Glib::RefPtr<Gtk::Tooltip>& tooltipw){
                return sp_query_custom_tooltip(x, y, kbd, tooltipw, id, tooltip, icon);
            });
            size_t pos = 0;
            std::shared_ptr<Inkscape::LivePathEffect::LPEObjectReference> lperef = (*it);
            for (auto w : LPEEffectMenu->get_children()) {
                auto * mitem = dynamic_cast<Gtk::MenuItem *>(w);
                if (mitem) {
                    mitem->signal_activate().connect([=](){
                        if (pos == 0) {
                            current_lpeitem->setCurrentPathEffect(lperef);
                            current_lpeitem->duplicateCurrentPathEffect();
                            effect_list_reload(current_lpeitem);
                            DocumentUndo::done(getDocument(), _("Duplicate path effect"), INKSCAPE_ICON("dialog-path-effects"));
                        } else if (pos == 1) {
                            current_lpeitem->setCurrentPathEffect(lperef);
                            current_lpeitem->upCurrentPathEffect();
                            effect_list_reload(current_lpeitem);
                            DocumentUndo::done(getDocument(), _("Move path effect up"), INKSCAPE_ICON("dialog-path-effects"));
                        } else if (pos == 2) {
                            current_lpeitem->setCurrentPathEffect(lperef);
                            current_lpeitem->downCurrentPathEffect();
                            effect_list_reload(current_lpeitem);
                            DocumentUndo::done(getDocument(), _("Move path effect down"), INKSCAPE_ICON("dialog-path-effects"));
                        } else if (pos == 3) {
                            lpeFlatten(lperef);
                        } else if (pos == 4) {
                            lpe->setDefaultParameters();
                            effect_list_reload(current_lpeitem);
                        } else if (pos == 5) {
                            lpe->resetDefaultParameters();
                            effect_list_reload(current_lpeitem);
                        } else if (pos == 6) {
                            sp_toggle_fav(untranslated_label, LPEtoggleFavorite);
                            _reload_menu = true;
                            _item_type = ""; // here we force reload even with the same tipe item selected
                        }
                    });
                    if (pos == 6) {
                        if (sp_has_fav(untranslated_label)) {
                            LPEtoggleFavorite->set_label(_("Unset Favorite"));
                        } else {
                            LPEtoggleFavorite->set_label(_("Set Favorite"));
                        }
                    }
                }
                pos ++;
            }
            if (total > 1) {
                LPEDrag->signal_drag_begin().connect([=](const Glib::RefPtr<Gdk::DragContext> context){
                    cairo_surface_t *surface;
                    cairo_t *cr;
                    int x, y;
                    double sx = 1;
                    double sy = 1;
                    dnd = true;
                    Gtk::Allocation alloc = LPEEffect->get_allocation ();
                    auto device_scale = get_scale_factor();
                    surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, alloc.get_width() * device_scale, alloc.get_height() * device_scale);
                    cairo_surface_set_device_scale(surface, device_scale, device_scale);
                    cr = cairo_create (surface);
                    LPEEffect->get_style_context()->add_class("drag-icon");
                    gtk_widget_draw (GTK_WIDGET(LPEEffect->gobj()), cr);
                    LPEEffect->get_style_context()->remove_class("drag-icon");
                    LPEDrag->translate_coordinates(*LPEEffect, dndx, dndy, x, y);
                    #ifndef __APPLE__
                    cairo_surface_get_device_scale (surface, &sx, &sy);
                    #endif
                    cairo_surface_set_device_offset (surface, -x * sx, -y * sy);
                    gtk_drag_set_icon_surface (context->gobj(), surface);
                    cairo_destroy (cr);
                    cairo_surface_destroy (surface);
                });
                auto row = dynamic_cast<Gtk::ListBoxRow *>(LPEEffect->get_parent());
                LPEDrag->signal_drag_data_get().connect([=](const Glib::RefPtr<Gdk::DragContext>& context, Gtk::SelectionData& selection_data, guint info, guint time)
                {
                    selection_data.set("GTK_LIST_BOX_ROW", Glib::ustring::format(row->get_index()));
                });
                LPEDrag->signal_drag_end().connect([=](const Glib::RefPtr<Gdk::DragContext>& context)
                {
                    dnd = false;
                });
                row->signal_drag_data_received().connect([=](const Glib::RefPtr<Gdk::DragContext>& context, int x, int y, const Gtk::SelectionData& selection_data, guint info, guint time)
                {
                    if (dnd) {
                        unsigned int pos_target, pos_source;
                        Gtk::Widget *target = row;
                        pos_target = row->get_index();
                        pos_source = atoi(reinterpret_cast<char const*>(selection_data.get_data()));
                        Glib::RefPtr<Gtk::StyleContext> stylec = target->get_style_context();
                        if (pos_source > pos_target) {
                            if (stylec->has_class("after")) {
                                pos_target ++;
                            }
                        } else if (pos_source < pos_target) {
                            if (stylec->has_class("before")) {
                                pos_target --;
                            }
                        }
                        Gtk::Widget *source = LPEListBox.get_row_at_index(pos_source);
                        if (source == target) {
                            gtk_drag_finish(context->gobj(), FALSE, FALSE, time);
                            dnd = false;
                            return;
                        }
                        g_object_ref(source->gobj());
                        LPEListBox.remove(*source);
                        LPEListBox.insert(*source, pos_target);
                        g_object_unref(source->gobj());
                        move_list(pos_source,pos_target);
                        gtk_drag_finish(context->gobj(), TRUE, TRUE, time);
                        dnd = false;
                    }
                });
                row->drag_dest_set(entries, Gtk::DEST_DEFAULT_ALL, Gdk::ACTION_MOVE);
                row->signal_drag_motion().connect([=](const Glib::RefPtr<Gdk::DragContext>& context, int x, int y, guint time)
                {
                    gint half = row->get_allocated_height()/2;
                    Glib::RefPtr<Gtk::StyleContext> stylec = row->get_style_context();
                    if (y < half) {
                        stylec->add_class("before");
                        stylec->remove_class("after");
                    } else {
                        stylec->remove_class("before");
                        stylec->add_class("after");
                    }
                    return true;
                }, true);
            }

            // other

            LPEEffect->set_name("LPEEffectItem");
            LPENameLabel->set_label(g_dpgettext2(nullptr, "path effect", (*it)->lpeobject->get_lpe()->getName().c_str()));

            LPEExpander->property_expanded().signal_changed().connect(sigc::bind(sigc::mem_fun(*this, &LivePathEffectEditor::expanded_notify),LPEExpander)); 

            Controller::add_click(*LPEOpenExpander, [=](Gtk::GestureMultiPress &, int, double, double)
            {
               LPEExpander->set_expanded(!LPEExpander->get_expanded());
               return Gtk::EVENT_SEQUENCE_CLAIMED;
            }, {}, Controller::Button::left);

            LPEHide->signal_clicked().connect(sigc::bind(sigc::mem_fun(*this, &LivePathEffectEditor::toggleVisible), lpe, LPEHide));
            LPEErase->signal_clicked().connect([=](){ removeEffect(LPEExpander);});

            Controller::add_click(*LPEDrag, [this](Gtk::GestureMultiPress const &, int, double const x, double const y)
            {
                dndx = x;
                dndy = y;
                return Gtk::EVENT_SEQUENCE_NONE;
            });

            if (total > 1) {
                // TODO: gtkmm4: Gtk::Widget.set_cursor() should suffice.
                auto const motion = gtk_event_controller_motion_new(LPEDrag->Gtk::Widget::gobj());
                gtk_event_controller_set_propagation_phase(motion, GTK_PHASE_TARGET);
                g_signal_connect_swapped(motion, "enter", G_CALLBACK(+[](Gtk::Widget * const widget){ set_cursor(*widget, "grab"   ); }), LPEDrag);
                g_signal_connect_swapped(motion, "leave", G_CALLBACK(+[](Gtk::Widget * const widget){ set_cursor(*widget, "default"); }), LPEDrag);
                manage(Glib::wrap(motion), *LPEDrag);
            }

            if (lpe->hasDefaultParameters()) {
                LPEResetDefault->set_visible(true);
                LPESetDefault->set_visible(false);
            } else {
                LPEResetDefault->set_visible(false);
                LPESetDefault->set_visible(true);
            }
        }
    }

    if (counter == 0 && LPEDrag) {
        LPEDrag->set_visible(false);
        LPEDrag->set_tooltip_text("");
    }

    if (LPEMoveUpExtrem) {
        LPEMoveUpExtrem->set_visible(false);
        LPEMoveDownExtrem->set_visible(false);
    }

    if (LPEExpanderCurrent) {
        _LPESelectionInfo.set_visible(false);
        LPEExpanderCurrent->set_expanded(true);
        if (auto const current_window = dynamic_cast<Gtk::Window *>(LPEExpanderCurrent->get_toplevel())) {
            current_window->set_focus(*LPEExpanderCurrent);
        }
    }

    selection_info();
    LPEListBox.show_all_children();
    ensure_size();
}

void LivePathEffectEditor::expanded_notify(Gtk::Expander *expander) {
    if (updating) {
        return;
    }
    if (!dnd) {
        _freezeexpander = false;
    }
    if (_freezeexpander) {
        _freezeexpander = false;
        return;
    }
    if (dnd) {
        _freezeexpander = true;
        expander->set_expanded(!expander->get_expanded());
        return;
    };
    updating = true;
    if (expander->get_expanded()) {
        for (auto &w : _LPEExpanders){
            if (w.first == expander) {
                w.first->set_expanded(true);
                w.first->get_parent()->get_parent()->get_parent()->set_name("currentlpe");
                current_lperef = w;
                current_lpeitem->setCurrentPathEffect(w.second);
                showParams(w, true);
            } else {
                w.first->set_expanded(false);
                w.first->get_parent()->get_parent()->get_parent()->set_name("unactive_lpe");
            }
        }
    }
    auto selection = SP_ACTIVE_DESKTOP->getSelection();
    if (selection && current_lpeitem && !selection->isEmpty()) {
        selection_changed_lock = true;
        selection->clear();
        selection->add(current_lpeitem);
        Inkscape::UI::Tools::sp_update_helperpath(getDesktop());
        selection_changed_lock = false;
    }
    updating = false; 
}

bool 
LivePathEffectEditor::lpeFlatten(std::shared_ptr<Inkscape::LivePathEffect::LPEObjectReference> lperef)
{
    current_lpeitem->setCurrentPathEffect(lperef);
    current_lpeitem = current_lpeitem->flattenCurrentPathEffect();
    auto selection = getSelection();
    if (selection && selection->isEmpty() ) {
        selection->add(current_lpeitem);
    }
    DocumentUndo::done(getDocument(), _("Flatten path effect(s)"), INKSCAPE_ICON("dialog-path-effects"));
    return false;
}

void
LivePathEffectEditor::removeEffect(Gtk::Expander * expander) {
    bool reload = current_lperef.first != expander;
    auto current_lperef_tmp = current_lperef;
    for (auto &w : _LPEExpanders){
        if (w.first == expander) {
            current_lpeitem->setCurrentPathEffect(w.second);
            current_lpeitem = current_lpeitem->removeCurrentPathEffect(false);
        } 
    }
    if (reload) {
        current_lpeitem->setCurrentPathEffect(current_lperef_tmp.second);
    }
    effect_list_reload(current_lpeitem);
    DocumentUndo::done(getDocument(), _("Remove path effect"), INKSCAPE_ICON("dialog-path-effects"));
}

/*
 * Clears the effectlist
 */
void
LivePathEffectEditor::clear_lpe_list()
{
    for (auto &w : LPEListBox.get_children()) {
        LPEListBox.remove(*w);
    }
    for (auto &w : _LPEParentBox.get_children()) {
        _LPEParentBox.remove(*w);
    }
    for (auto &w : _LPECurrentItem.get_children()) {
        _LPECurrentItem.remove(*w);
    }
}

SPLPEItem * LivePathEffectEditor::clonetolpeitem()
{
    auto selection = getSelection();
    if (selection && !selection->isEmpty() ) {
        auto use = cast<SPUse>(selection->singleItem());
        if ( use ) {
            DocumentUndo::ScopedInsensitive tmp(getDocument());
            // item is a clone. do not show effectlist dialog.
            // convert to path, apply CLONE_ORIGINAL LPE, link it to the cloned path

            // test whether linked object is supported by the CLONE_ORIGINAL LPE
            SPItem *orig = use->get_original();
            if ( is<SPShape>(orig) || is<SPGroup>(orig) || is<SPText>(orig) ) {
                // select original
                selection->set(orig);

                // delete clone but remember its id and transform
                auto id_copy = Util::to_opt(use->getAttribute("id"));
                auto transform_copy = Util::to_opt(use->getAttribute("transform"));
                use->deleteObject(false);
                use = nullptr;

                // run sp_selection_clone_original_path_lpe
                selection->cloneOriginalPathLPE(true, true, true);

                SPItem *new_item = selection->singleItem();
                // Check that the cloning was successful. We don't want to change the ID of the original referenced path!
                if (new_item && (new_item != orig)) {
                    new_item->setAttribute("id", Util::to_cstr(id_copy));
                    if (Util::to_cstr(transform_copy)) {
                        Geom::Affine item_t(Geom::identity());
                        sp_svg_transform_read(Util::to_cstr(transform_copy), &item_t);
                        new_item->transform *= item_t;
                        new_item->doWriteTransform(new_item->transform);
                        new_item->requestDisplayUpdate(SP_OBJECT_MODIFIED_FLAG);
                    }
                    new_item->setAttribute("class", "fromclone");
                }
                
                auto *lpeitem = cast<SPLPEItem>(new_item);
                if (lpeitem) {
                    sp_lpe_item_update_patheffect(lpeitem, true, true);
                    return lpeitem;
                }
            }
        }
    }
    return nullptr;
}

void LivePathEffectEditor::onAddGallery()
{
    // show effectlist dialog
    using Inkscape::UI::Dialog::LivePathEffectAdd;
    LivePathEffectAdd::show(getDesktop());
    clearMenu();
    if ( !LivePathEffectAdd::isApplied()) {
        return;
    }

    const LivePathEffect::EnumEffectData<LivePathEffect::EffectType> *data = LivePathEffectAdd::getActiveData();;
    if (!data) {
        return;
    }
    selection_changed_lock = true;
    SPLPEItem *fromclone = clonetolpeitem();
    if (fromclone) {
        current_lpeitem = fromclone;
        if (data->key == "clone_original") {
            current_lpeitem->getCurrentLPE()->refresh_widgets = true;
            selection_changed_lock = false;
            DocumentUndo::done(getDocument(), _("Create and apply path effect"), INKSCAPE_ICON("dialog-path-effects"));
            return;
        }

    }
    selection_changed_lock = false;
    if (current_lpeitem) {
        LivePathEffect::Effect::createAndApply(data->key.c_str(), getDocument(), current_lpeitem);
        current_lpeitem->getCurrentLPE()->refresh_widgets = true;
        DocumentUndo::done(getDocument(), _("Create and apply path effect"), INKSCAPE_ICON("dialog-path-effects"));
    }
}

void LivePathEffectEditor::on_showgallery_notify(Preferences::Entry const &new_val)
{
    _LPEGallery.set_visible(new_val.getBool());
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

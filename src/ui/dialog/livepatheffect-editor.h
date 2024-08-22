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

#ifndef LIVEPATHEFFECTEDITOR_H
#define LIVEPATHEFFECTEDITOR_H

#include <memory>
#include <gtkmm/builder.h>

#include "live_effects/effect-enum.h"
#include "preferences.h"
#include "ui/dialog/dialog-base.h"
#include "ui/widget/completion-popup.h"
#include "ui/column-menu-builder.h"

namespace Inkscape {

namespace UI {
namespace Dialog {

/*
 * @brief The LivePathEffectEditor class
 */
class LivePathEffectEditor : public DialogBase
{
public:
    // No default constructor, noncopyable, nonassignable
    LivePathEffectEditor();
    ~LivePathEffectEditor() override;

    LivePathEffectEditor(LivePathEffectEditor const &d) = delete;
    LivePathEffectEditor operator=(LivePathEffectEditor const &d) = delete;

    static LivePathEffectEditor &getInstance() { return *new LivePathEffectEditor(); }
    void move_list(gint origin, gint dest);

    std::vector<std::pair<Gtk::Expander *, std::shared_ptr<Inkscape::LivePathEffect::LPEObjectReference> > > _LPEExpanders;
    void showParams(std::pair<Gtk::Expander *, std::shared_ptr<Inkscape::LivePathEffect::LPEObjectReference> > expanderdata, bool changed);
    bool updating = false;
    SPLPEItem *current_lpeitem = nullptr;
    std::pair<Gtk::Expander *, std::shared_ptr<Inkscape::LivePathEffect::LPEObjectReference> > current_lperef = std::make_pair(nullptr, nullptr);
    static const LivePathEffect::EnumEffectData<LivePathEffect::EffectType> *getActiveData();
    bool selection_changed_lock = false;
    bool dnd = false;

private:
    Glib::RefPtr<Gtk::Builder> _builder;

public:
    Gtk::ListBox& LPEListBox;
    gint dndx = 0;
    gint dndy = 0;

private:
    void add_lpes(Inkscape::UI::Widget::CompletionPopup& popup, bool symbolic);
    void clear_lpe_list();
    void selectionChanged(Inkscape::Selection *selection) override;
    void selectionModified(Inkscape::Selection *selection, guint flags) override;
    void onSelectionChanged(Inkscape::Selection *selection);
    void onAddGallery();
    void expanded_notify(Gtk::Expander *expander);
    void onAdd(Inkscape::LivePathEffect::EffectType etype);
    void toggleVisible(Inkscape::LivePathEffect::Effect *lpe, Gtk::Button *visbutton);
    bool is_appliable(LivePathEffect::EffectType etypen, Glib::ustring item_type, bool has_clip, bool has_mask);
    void removeEffect(Gtk::Expander * expander);
    void effect_list_reload(SPLPEItem *lpeitem);
    void selection_info();
    void map_handler();
    void clearMenu();
    void setMenu();
    bool lpeFlatten(std::shared_ptr<Inkscape::LivePathEffect::LPEObjectReference> lperef);
    void on_showgallery_notify(Preferences::Entry const &new_val);

    SPLPEItem * clonetolpeitem();
    Inkscape::UI::Widget::CompletionPopup _lpes_popup;
    Gtk::Box& _LPEContainer;
    Gtk::Box& _LPEAddContainer;
    Gtk::Label&_LPESelectionInfo;
    Gtk::ListBox&_LPEParentBox;
    Gtk::Box&_LPECurrentItem;
    PathEffectList effectlist;
    Glib::RefPtr<Gtk::ListStore> _LPEList;
    Glib::RefPtr<Gtk::ListStore> _LPEListFilter;
    const LivePathEffect::EnumEffectDataConverter<LivePathEffect::EffectType> &converter;
    Gtk::Widget *effectwidget = nullptr;
    Gtk::Widget *popupwidg = nullptr;
    GtkWidget *currentdrag = nullptr;
    bool _reload_menu = false;
    gint _buttons_width = 0;
    bool _freezeexpander = false;
    Glib::ustring _item_type;
    bool _has_clip;
    bool _has_mask;
    bool _frezee = false;
    Gtk::Button &_LPEGallery;
    std::unique_ptr<Preferences::PreferencesObserver> const _showgallery_observer;
};

} // namespace Dialog
} // namespace UI
} // namespace Inkscape

#endif // LIVEPATHEFFECTEDITOR_H

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

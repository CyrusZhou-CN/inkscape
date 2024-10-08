// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Authors:
 *   bulia byak <buliabyak@users.sf.net>
 *   Bryce W. Harrington <bryce@bryceharrington.org>
 *   Lauris Kaplinski <lauris@kaplinski.com>
 *   Jon Phillips <jon@rejon.org>
 *   Ralf Stephan <ralf@ark.in-berlin.de> (Gtkmm)
 *   Jon A. Cruz <jon@joncruz.org>
 *   Abhishek Sharma
 *
 * Copyright (C) 2000 - 2005 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "entity-entry.h"

#include <gtkmm/scrolledwindow.h>
#include <gtkmm/entry.h>

#include "desktop.h"
#include "document.h"
#include "document-undo.h"
#include "preferences.h"
#include "rdf.h"

#include "object/sp-root.h"

#include "ui/widget/registry.h"

namespace Inkscape {
namespace UI {
namespace Widget {

//===================================================

//---------------------------------------------------

EntityEntry*
EntityEntry::create (rdf_work_entity_t* ent, Registry& wr)
{
    g_assert (ent);
    EntityEntry* obj = nullptr;
    switch (ent->format)
    {
        case RDF_FORMAT_LINE: 
            obj = new EntityLineEntry (ent, wr);
            break;
        case RDF_FORMAT_MULTILINE: 
            obj = new EntityMultiLineEntry (ent, wr);
            break;
        default:
            g_warning ("An unknown RDF format was requested.");
    }

    g_assert (obj);
    obj->_label.set_visible(true);
    return obj;
}

EntityEntry::EntityEntry (rdf_work_entity_t* ent, Registry& wr)
    : _label(Glib::ustring(_(ent->title)), Gtk::Align::END),
      _packable(nullptr),
      _entity(ent), _wr(&wr)
{
}

EntityEntry::~EntityEntry()
{
    _changed_connection.disconnect();
}

void EntityEntry::save_to_preferences(SPDocument *doc)
{
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    const gchar *text = rdf_get_work_entity (doc, _entity);
    prefs->setString(PREFS_METADATA + Glib::ustring(_entity->name), Glib::ustring(text ? text : ""));
}

EntityLineEntry::EntityLineEntry (rdf_work_entity_t* ent, Registry& wr)
: EntityEntry (ent, wr)
{
    Gtk::Entry *e = new Gtk::Entry;
    e->set_tooltip_text (_(ent->tip));
    _packable = e;
    _changed_connection = e->signal_changed().connect (sigc::mem_fun (*this, &EntityLineEntry::on_changed));
}

EntityLineEntry::~EntityLineEntry()
{
    delete static_cast<Gtk::Entry*>(_packable);
}

void EntityLineEntry::update(SPDocument* doc, bool read_only)
{
    const char *text = rdf_get_work_entity(doc, _entity);
    // If RDF title is not set, get the document's <title> and set the RDF:
    if (!text && !strcmp(_entity->name, "title") && doc->getRoot()) {
        text = doc->getRoot()->title();
        if (!read_only) {
            rdf_set_work_entity(doc, _entity, text);
        }
    }
    static_cast<Gtk::Entry*>(_packable)->set_text(text ? text : "");
}


void EntityLineEntry::load_from_preferences()
{
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    Glib::ustring text = prefs->getString(PREFS_METADATA + Glib::ustring(_entity->name));
    if (text.length() > 0) {
        static_cast<Gtk::Entry*>(_packable)->set_text (text.c_str());
    }
}

void
EntityLineEntry::on_changed()
{
    if (_wr->isUpdating() || !_wr->desktop())
        return;

    _wr->setUpdating (true);
    SPDocument *doc = _wr->desktop()->getDocument();
    Glib::ustring text = static_cast<Gtk::Entry*>(_packable)->get_text();
    if (rdf_set_work_entity (doc, _entity, text.c_str())) {
        if (doc->isSensitive()) {
            DocumentUndo::done(doc, "Document metadata updated", "");
        }
    }
    _wr->setUpdating (false);
}

Glib::ustring EntityLineEntry::content() const {
    return static_cast<Gtk::Entry*>(_packable)->get_text();
}

EntityMultiLineEntry::EntityMultiLineEntry (rdf_work_entity_t* ent, Registry& wr)
: EntityEntry (ent, wr)
{
    Gtk::ScrolledWindow *s = new Gtk::ScrolledWindow;
    s->set_policy (Gtk::PolicyType::AUTOMATIC, Gtk::PolicyType::AUTOMATIC);
    s->set_has_frame(true);
    _packable = s;
    _v.set_size_request (-1, 35);
    _v.set_wrap_mode (Gtk::WrapMode::WORD);
    _v.set_accepts_tab (false);
    s->set_child(_v);
    _v.set_tooltip_text (_(ent->tip));
    _changed_connection = _v.get_buffer()->signal_changed().connect (sigc::mem_fun (*this, &EntityMultiLineEntry::on_changed));
}

Glib::ustring EntityMultiLineEntry::content() const {
    return _v.get_buffer()->get_text();
}

EntityMultiLineEntry::~EntityMultiLineEntry()
{
    delete static_cast<Gtk::ScrolledWindow*>(_packable);
}

void EntityMultiLineEntry::update(SPDocument* doc, bool read_only)
{
    const char *text = rdf_get_work_entity(doc, _entity);
    // If RDF title is not set, get the document's <title> and set the RDF:
    if (!text && !strcmp(_entity->name, "title") && doc->getRoot()) {
        text = doc->getRoot()->title();
        if (!read_only) {
            rdf_set_work_entity(doc, _entity, text);
        }
    }
    Gtk::ScrolledWindow *s = static_cast<Gtk::ScrolledWindow*>(_packable);
    Gtk::TextView *tv = static_cast<Gtk::TextView*>(s->get_child());
    tv->get_buffer()->set_text(text ? text : "");
}


void EntityMultiLineEntry::load_from_preferences()
{
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    Glib::ustring text = prefs->getString(PREFS_METADATA + Glib::ustring(_entity->name));
    if (text.length() > 0) {
        Gtk::ScrolledWindow *s = static_cast<Gtk::ScrolledWindow*>(_packable);
        Gtk::TextView *tv = static_cast<Gtk::TextView*>(s->get_child());
        tv->get_buffer()->set_text (text.c_str());
    }
}


void
EntityMultiLineEntry::on_changed()
{
    if (_wr->isUpdating() || !_wr->desktop())
        return;

    _wr->setUpdating (true);
    SPDocument *doc = _wr->desktop()->getDocument();
    Gtk::ScrolledWindow *s = static_cast<Gtk::ScrolledWindow*>(_packable);
    Gtk::TextView *tv = static_cast<Gtk::TextView*>(s->get_child());
    Glib::ustring text = tv->get_buffer()->get_text();
    if (rdf_set_work_entity (doc, _entity, text.c_str())) {
        DocumentUndo::done(doc, "Document metadata updated", "");
    }
    _wr->setUpdating (false);
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

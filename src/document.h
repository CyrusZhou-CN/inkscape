#ifndef __SP_DOCUMENT_H__
#define __SP_DOCUMENT_H__

/*
 * Typed SVG document implementation
 *
 * Author:
 *   Lauris Kaplinski <lauris@kaplinski.com>
 *
 * Copyright (C) 1999-2002 Lauris Kaplinski
 * Copyright (C) 2000-2001 Ximian, Inc.
 *
 * Released under GNU GPL, read the file 'COPYING' for more information
 */

#include <glib-object.h>
#include <glib.h>
#include <gtk/gtksignal.h>
#include <sigc++/sigc++.h>
#include <sigc++/class_slot.h>

#include "forward.h"
#include "libnr/nr-forward.h"
#include "xml/xml-forward.h"

class SPDocumentPrivate;

struct SPDocument {
	GObject object;

	unsigned int advertize : 1;
	unsigned int keepalive : 1;
	unsigned int virgin    : 1; /* Has the document never been touched? */

	SPReprDoc *rdoc; /* Our SPReprDoc */
	SPRepr *rroot; /* Root element of SPReprDoc */
	SPObject *root; /* Our SPRoot */

	gchar *uri; /* URI string or NULL */
	gchar *base;
	gchar *name;

	/* fixme: remove this */
	SPDocumentPrivate *priv;

	/* Last action key */
	const gchar *actionkey;
	/* Handler ID */
	guint modified_id;

	SPObject *getObjectById(gchar const *id);
	SPObject *getObjectByRepr(SPRepr *repr);

	void queueForOrphanCollection(SPObject *object);
	void collectOrphans();

	GSList *_collection_queue;
};

struct SPDocumentClass {
	GObjectClass parent_class;

	void (* modified) (SPDocument *document, guint flags);
	void (* uri_set) (SPDocument *document, const gchar *uri);
	void (* resized) (SPDocument *document, gdouble width, gdouble height);
};

/* Fetches document from URI, or creates new, if NULL
 * Public document appear in document list
 */

SPDocument *sp_document_new (const gchar *uri, unsigned int advertize, unsigned int keepalive, bool make_new = false);
SPDocument *sp_document_new_from_mem (const gchar *buffer, gint length, unsigned int advertize, unsigned int keepalive);

SPDocument *sp_document_ref (SPDocument *doc);
SPDocument *sp_document_unref (SPDocument *doc);

/*
 * Access methods
 */

#define sp_document_repr_doc(d) (SP_DOCUMENT (d)->rdoc)
#define sp_document_repr_root(d) (SP_DOCUMENT (d)->rroot)
#define sp_document_root(d) (SP_DOCUMENT (d)->root)
#define SP_DOCUMENT_ROOT(d)  (SP_DOCUMENT (d)->root)

gdouble sp_document_width (SPDocument * document);
gdouble sp_document_height (SPDocument * document);
#define SP_DOCUMENT_URI(d) (SP_DOCUMENT (d)->uri)
#define SP_DOCUMENT_NAME(d) (SP_DOCUMENT (d)->name)
#define SP_DOCUMENT_BASE(d) (SP_DOCUMENT (d)->base)

/*
 * Dictionary
 */

void sp_document_def_id(SPDocument *document, const gchar *id, SPObject *object);
sigc::connection sp_document_id_changed_connect(SPDocument *document, const gchar *id, sigc::slot<void, SPObject *>);




/*
 * Undo & redo
 */

void sp_document_set_undo_sensitive (SPDocument * document, gboolean sensitive);
gboolean sp_document_get_undo_sensitive (SPDocument const * document);

void sp_document_clear_undo (SPDocument * document);
void sp_document_clear_redo (SPDocument * document);

void sp_document_child_added (SPDocument *doc, SPObject *object, SPRepr *child, SPRepr *ref);
void sp_document_child_removed (SPDocument *doc, SPObject *object, SPRepr *child, SPRepr *ref);
void sp_document_attr_changed (SPDocument *doc, SPObject *object, const gchar *key, const gchar *oldval, const gchar *newval);
void sp_document_content_changed (SPDocument *doc, SPObject *object, const gchar *oldcontent, const gchar *newcontent);
void sp_document_order_changed (SPDocument *doc, SPObject *object, SPRepr *child, SPRepr *oldref, SPRepr *newref);

/* Object modification root handler */
void sp_document_request_modified (SPDocument *doc);
gint sp_document_ensure_up_to_date (SPDocument *doc);

/* Save all previous actions to stack, as one undo step */
void sp_document_done (SPDocument *document);
void sp_document_maybe_done (SPDocument *document, const gchar *key);
void sp_document_reset_key (Inkscape::Application *inkscape, SPDesktop *desktop, GtkObject *base);

/* Cancel (and revert) current unsaved actions */
void sp_document_cancel (SPDocument *document);

/* Undo and redo */
gboolean sp_document_undo (SPDocument * document);
gboolean sp_document_redo (SPDocument * document);

/* Resource management */
gboolean sp_document_add_resource (SPDocument *document, const gchar *key, SPObject *object);
gboolean sp_document_remove_resource (SPDocument *document, const gchar *key, SPObject *object);
const GSList *sp_document_get_resource_list (SPDocument *document, const gchar *key);

/*
 * Ideas: How to overcome style invalidation nightmare
 *
 * 1. There is reference request dictionary, that contains
 * objects (styles) needing certain id. Object::build checks
 * final id against it, and invokes necesary methods
 *
 * 2. Removing referenced object is simply prohibited -
 * needs analyse, how we can deal with situations, where
 * we simply want to ungroup etc. - probably we need
 * Repr::reparent method :( [Or was it ;)]
 *
 */

/*
 * Misc
 */

GSList * sp_document_items_in_box (SPDocument *document, unsigned int dkey, NRRect const *box);
GSList * sp_document_partial_items_in_box (SPDocument *document, unsigned int dkey, NRRect const *box);
SPItem * sp_document_item_at_point (SPDocument *document, unsigned int key, NR::Point const p, gboolean into_groups);
SPItem * sp_document_group_at_point (SPDocument *document, unsigned int key,  NR::Point const p);

void sp_document_set_uri (SPDocument *document, const gchar *uri);
void sp_document_set_size_px (SPDocument *doc, gdouble width, gdouble height);



#endif

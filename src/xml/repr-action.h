#ifndef __SP_XML_REPR_ACTION_H__
#define __SP_XML_REPR_ACTION_H__

#include <glib/gtypes.h>

#include <xml/xml-forward.h>

struct SPReprAction;
struct SPReprActionAdd;
struct SPReprActionDel;
struct SPReprActionChgAttr;
struct SPReprActionChgContent;
struct SPReprActionChgOrder;

typedef enum {
	SP_REPR_ACTION_INVALID,
	SP_REPR_ACTION_ADD,
	SP_REPR_ACTION_DEL,
	SP_REPR_ACTION_CHGATTR,
	SP_REPR_ACTION_CHGCONTENT,
	SP_REPR_ACTION_CHGORDER
} SPReprActionType;

struct SPReprActionAdd {
	SPRepr *child;
	SPRepr *ref;
};

struct SPReprActionDel {
	SPRepr *child;
	SPRepr *ref;
};

struct SPReprActionChgAttr {
	int key;
	gchar *oldval, *newval;
};

struct SPReprActionChgContent {
	gchar *oldval, *newval;
};

struct SPReprActionChgOrder {
	SPRepr *child;
	SPRepr *oldref, *newref;
};

struct SPReprAction {
	SPReprAction *next;
	SPReprActionType type;
	SPRepr *repr;
	int serial;
	union {
		SPReprActionAdd add;
		SPReprActionDel del;
		SPReprActionChgAttr chgattr;
		SPReprActionChgContent chgcontent;
		SPReprActionChgOrder chgorder;
	};
};

void sp_repr_begin_transaction (SPReprDoc *doc);
void sp_repr_rollback (SPReprDoc *doc);
void sp_repr_commit (SPReprDoc *doc);
SPReprAction *sp_repr_commit_undoable (SPReprDoc *doc);

void sp_repr_undo_log (SPReprAction *log);
void sp_repr_replay_log (SPReprAction *log);
SPReprAction *sp_repr_coalesce_log (SPReprAction *a, SPReprAction *b);
void sp_repr_free_log (SPReprAction *log);

SPReprAction *sp_repr_log_add (SPReprAction *log, SPRepr *repr,
                               SPRepr *child, SPRepr *ref);
SPReprAction *sp_repr_log_remove (SPReprAction *log, SPRepr *repr,
                                  SPRepr *child, SPRepr *ref);

SPReprAction *sp_repr_log_chgattr (SPReprAction *log, SPRepr *repr, int key,
                                   const gchar *oldval, const gchar *newval);
SPReprAction *sp_repr_log_chgcontent (SPReprAction *log, SPRepr *repr,
                                      const gchar *oldval, const gchar *newval);

SPReprAction *sp_repr_log_chgorder (SPReprAction *log, SPRepr *repr,
                                    SPRepr *child,
                                    SPRepr *oldref, SPRepr *newref);

#endif

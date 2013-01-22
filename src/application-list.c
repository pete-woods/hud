/*
 * Copyright © 2012 Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 3, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranties of
 * MERCHANTABILITY, SATISFACTORY QUALITY, or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Author: Ted Gould <ted@canonical.com>
 */

#define G_LOG_DOMAIN "hudapplicationlist"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <libbamf/libbamf.h>

#include "application-list.h"
#include "application-source.h"
#include "hudsource.h"

typedef struct _HudApplicationListPrivate HudApplicationListPrivate;

struct _HudApplicationListPrivate {
	BamfMatcher * matcher;
	gulong matcher_app_sig;
	gulong matcher_view_open_sig;
	gulong matcher_view_close_sig;

	GHashTable * applications;
	HudSource * used_source; /* Not a reference */
};

#define HUD_APPLICATION_LIST_GET_PRIVATE(o) \
	(G_TYPE_INSTANCE_GET_PRIVATE ((o), HUD_TYPE_APPLICATION_LIST, HudApplicationListPrivate))

static void hud_application_list_class_init (HudApplicationListClass * klass);
static void hud_application_list_init       (HudApplicationList *      self);
static void hud_application_list_dispose    (GObject *                 object);
static void hud_application_list_finalize   (GObject *                 object);
static void source_iface_init               (HudSourceInterface *      iface);
static void window_changed                  (BamfMatcher *             matcher,
                                             BamfWindow *              old_window,
                                             BamfWindow *              new_window,
                                             gpointer                  user_data);
static void view_opened                     (BamfMatcher *             matcher,
                                             BamfView *                view,
                                             gpointer                  user_data);
static void source_use                      (HudSource *               hud_source);
static void source_unuse                    (HudSource *               hud_source);
static void source_search                   (HudSource *               hud_source,
                                             HudTokenList *            search_string,
                                             void                    (*append_func) (HudResult * result, gpointer user_data),
                                             gpointer                  user_data);

G_DEFINE_TYPE_WITH_CODE (HudApplicationList, hud_application_list, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (HUD_TYPE_SOURCE, source_iface_init))

/* Class Init */
static void
hud_application_list_class_init (HudApplicationListClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	g_type_class_add_private (klass, sizeof (HudApplicationListPrivate));

	object_class->dispose = hud_application_list_dispose;
	object_class->finalize = hud_application_list_finalize;

	return;
}

/* Intialized the source interface */
static void
source_iface_init (HudSourceInterface *iface)
{
	iface->use = source_use;
	iface->unuse = source_unuse;
	iface->search = source_search;

	return;
}

/* Instance Init */
static void
hud_application_list_init (HudApplicationList *self)
{
	self->priv = HUD_APPLICATION_LIST_GET_PRIVATE(self);

	self->priv->matcher = bamf_matcher_get_default();
	self->priv->matcher_app_sig = g_signal_connect(self->priv->matcher,
		"active-window-changed",
		G_CALLBACK(window_changed), self);
	self->priv->matcher_view_open_sig = g_signal_connect(self->priv->matcher,
		"view-opened",
		G_CALLBACK(view_opened), self);

	self->priv->applications = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_object_unref);

	GList * apps = bamf_matcher_get_applications(self->priv->matcher);
	GList * app = NULL;
	for (app = apps; app != NULL; app = g_list_next(app)) {
		if (!BAMF_IS_APPLICATION(app->data)) {
			continue;
		}

		BamfApplication * bapp = BAMF_APPLICATION(app->data);
		gchar * app_id = hud_application_source_bamf_app_id(bapp);

		if (app_id == NULL) {
			continue;
		}

		HudApplicationSource * appsource = hud_application_source_new_for_app(bapp);

		if (!hud_application_source_is_empty(appsource)) {
			g_hash_table_insert(self->priv->applications, app_id, appsource);
		} else {
			g_object_unref(appsource);
		}
	}

	GList * windows = bamf_matcher_get_windows(self->priv->matcher);
	GList * window = NULL;
	for (window = windows; window != NULL; window = g_list_next(window)) {
		if (!BAMF_IS_WINDOW(window->data)) {
			continue;
		}

		view_opened(self->priv->matcher, BAMF_VIEW(window->data), self);
	}

	return;
}

/* Clean up references */
static void
hud_application_list_dispose (GObject *object)
{
	HudApplicationList * self = HUD_APPLICATION_LIST(object);

	if (self->priv->used_source != NULL) {
		hud_source_unuse(self->priv->used_source);
		self->priv->used_source = NULL;
	}

	if (self->priv->matcher_app_sig != 0 && self->priv->matcher != NULL) {
		g_signal_handler_disconnect(self->priv->matcher, self->priv->matcher_app_sig);
	}
	self->priv->matcher_app_sig = 0;

	if (self->priv->matcher_view_open_sig != 0 && self->priv->matcher != NULL) {
		g_signal_handler_disconnect(self->priv->matcher, self->priv->matcher_view_open_sig);
	}
	self->priv->matcher_view_open_sig = 0;

	if (self->priv->matcher_view_close_sig != 0 && self->priv->matcher != NULL) {
		g_signal_handler_disconnect(self->priv->matcher, self->priv->matcher_view_close_sig);
	}
	self->priv->matcher_view_close_sig = 0;

	g_clear_object(&self->priv->matcher);

	g_clear_pointer(&self->priv->applications, g_hash_table_unref);

	G_OBJECT_CLASS (hud_application_list_parent_class)->dispose (object);
	return;
}

/* Free memory */
static void
hud_application_list_finalize (GObject *object)
{

	G_OBJECT_CLASS (hud_application_list_parent_class)->finalize (object);
	return;
}

/* Get a source from a BamfApp */
static HudApplicationSource *
bamf_app_to_source (HudApplicationList * list, BamfApplication * bapp)
{
	gchar * id = hud_application_source_bamf_app_id(bapp);
	if (id == NULL) {
		return NULL;
	}

	HudApplicationSource * source = g_hash_table_lookup(list->priv->applications, id);
	if (source == NULL) {
		source = hud_application_source_new_for_app(bapp);
		g_hash_table_insert(list->priv->applications, id, source);
		id = NULL; /* We used the malloc in the table */
	}

	g_free(id);

	return source;
}

static gboolean
hud_application_list_name_in_ignore_list (BamfWindow *window)
{
  static const gchar * const ignored_names[] = {
    "Hud Prototype Test",
    "Hud",
    "DNDCollectionWindow",
    "launcher",
    "dash",
    "Dash",
    "panel",
    "hud",
    "unity-2d-shell"
  };
  gboolean ignored = FALSE;
  gchar *window_name;
  gint i;

  window_name = bamf_view_get_name (BAMF_VIEW (window));
  g_debug ("checking window name '%s'", window_name);

  /* sometimes bamf returns NULL here... protect ourselves */
  if (window_name == NULL)
    return TRUE;

  for (i = 0; i < G_N_ELEMENTS (ignored_names); i++)
    if (g_str_equal (ignored_names[i], window_name))
      {
        g_debug ("window name '%s' blocked", window_name);
        ignored = TRUE;
        break;
      }

  g_free (window_name);

  return ignored;
}

/* Called each time the focused application changes */
static void
window_changed (BamfMatcher * matcher, BamfWindow * old_win, BamfWindow * new_win, gpointer user_data)
{
	HudApplicationList * list = HUD_APPLICATION_LIST(user_data);

	/* We care where we're going, not where we've been */
	if (new_win == NULL) {
		if (list->priv->used_source != NULL) {
			hud_source_unuse(list->priv->used_source);
			list->priv->used_source = NULL;
		}
		return;
	}

	if (hud_application_list_name_in_ignore_list (new_win))
	    return;

	/* Try to use BAMF */
	BamfApplication * new_app = bamf_matcher_get_application_for_window(list->priv->matcher, new_win);
	HudApplicationSource * source = NULL;

	/* If we've got an app, we can find it easily */
	if (new_app != NULL) {
		source = bamf_app_to_source(list, new_app);
	}

	/* If we weren't able to use BAMF, let's try to find a source
	   for the window. */
	if (source == NULL) {
		guint32 xid = bamf_window_get_xid(new_win);
		GList * sources = g_hash_table_get_values(list->priv->applications);
		GList * lsource = NULL;

		for (lsource = sources; lsource != NULL; lsource = g_list_next(lsource)) {
			HudApplicationSource * appsource = HUD_APPLICATION_SOURCE(lsource->data);
			if (appsource == NULL) continue;

			if (hud_application_source_has_xid(appsource, xid)) {
				source = appsource;
				break;
			}
		}
	}

	if (source == NULL) {
		g_warning("Unable to find source for window");
		return;
	}

	hud_application_source_focus(source, new_app, new_win);

	if (list->priv->used_source) {
		hud_source_unuse(list->priv->used_source);
	}

  list->priv->used_source = HUD_SOURCE(source);
  hud_source_use(list->priv->used_source);

	return;
}

/* A new view has been opened by BAMF */
static void
view_opened (BamfMatcher * matcher, BamfView * view, gpointer user_data)
{
	if (!BAMF_IS_WINDOW(view)) {
		/* We only want windows.  Sorry. */
		return;
	}

	HudApplicationList * list = HUD_APPLICATION_LIST(user_data);
	BamfApplication * app = bamf_matcher_get_application_for_window(list->priv->matcher, BAMF_WINDOW(view));
	if (app == NULL) {
		return;
	}

	HudApplicationSource * source = bamf_app_to_source(list, app);
	if (source == NULL) {
		return;
	}

	hud_application_source_add_window(source, BAMF_WINDOW(view));

	return;
}

/* Source interface using this source */
static void
source_use (HudSource *hud_source)
{
	g_return_if_fail(HUD_IS_APPLICATION_LIST(hud_source));
	HudApplicationList * list = HUD_APPLICATION_LIST(hud_source);

	BamfApplication * app = bamf_matcher_get_active_application(list->priv->matcher);
	HudApplicationSource * source = NULL;

	if (app != NULL) {
		source = bamf_app_to_source(list, app);
	}

	/* If we weren't able to use BAMF, let's try to find a source
	   for the window. */
	if (source == NULL) {
		guint32 xid = bamf_window_get_xid(bamf_matcher_get_active_window(list->priv->matcher));
		GList * sources = g_hash_table_get_values(list->priv->applications);
		GList * lsource = NULL;

		for (lsource = sources; lsource != NULL; lsource = g_list_next(lsource)) {
			HudApplicationSource * appsource = HUD_APPLICATION_SOURCE(lsource->data);
			if (appsource == NULL) continue;

			if (hud_application_source_has_xid(appsource, xid)) {
				source = appsource;
				break;
			}
		}
	}

	if (source == NULL) {
		g_warning("Unable to find source for window");
		return;
	}

	list->priv->used_source = HUD_SOURCE(source);

	hud_source_use(HUD_SOURCE(source));

	return;
}

/* Source interface unusing this source */
static void
source_unuse (HudSource *hud_source)
{
	g_return_if_fail(HUD_IS_APPLICATION_LIST(hud_source));
	HudApplicationList * list = HUD_APPLICATION_LIST(hud_source);

	g_return_if_fail(list->priv->used_source != NULL);

	hud_source_unuse(list->priv->used_source);
	list->priv->used_source = NULL;

	return;
}

/* Search this source */
static void
source_search (HudSource *     hud_source,
               HudTokenList *  search_string,
               void          (*append_func) (HudResult * result, gpointer user_data),
               gpointer        user_data)
{
	g_return_if_fail(HUD_IS_APPLICATION_LIST(hud_source));
	HudApplicationList * list = HUD_APPLICATION_LIST(hud_source);

	g_return_if_fail(list->priv->used_source != NULL);

	hud_source_search(list->priv->used_source, search_string, append_func, user_data);

	return;
}

/**
 * hud_application_list_new:
 *
 * Create a new application list.
 *
 * Return Value: (transfer full): New #HudApplicationList
 */
HudApplicationList *
hud_application_list_new (void)
{
	return g_object_new(HUD_TYPE_APPLICATION_LIST,
	                    NULL);
}

/**
 * hud_application_list_get_source:
 * @list: A #HudApplicationList object
 * @id: Application ID to find
 *
 * Looks for a source in the application list database or if it
 * doesn't exist, it creates it.
 *
 * Return value: (transfer none): An #HudApplicationSource matching @id
 */
HudApplicationSource *
hud_application_list_get_source (HudApplicationList * list, const gchar * id)
{
	g_return_val_if_fail(HUD_IS_APPLICATION_LIST(list), NULL);
	g_return_val_if_fail(id != NULL, NULL);

	HudApplicationSource * source = g_hash_table_lookup(list->priv->applications, id);
	if (source == NULL) {
		source = hud_application_source_new_for_id(id);
		g_hash_table_insert(list->priv->applications, g_strdup(id), source);
	}

	return source;
}

/**
 * hud_application_list_get_apps:
 * @list: A #HudApplicationList object
 * 
 * Gets a list of applications
 *
 * Return value: A list of #HudApplicationSource objects
 */
GList *
hud_application_list_get_apps (HudApplicationList * list)
{
	g_return_val_if_fail(HUD_IS_APPLICATION_LIST(list), NULL);

	return g_hash_table_get_values(list->priv->applications);
}

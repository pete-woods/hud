#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gio/gio.h>
#include <glib.h>

#include <libdbusmenu-glib/client.h>

#include "dbusmenu-collector.h"
#include "distance.h"

#define GENERIC_ICON   "dbusmenu-lens-panel"

typedef struct _DbusmenuCollectorPrivate DbusmenuCollectorPrivate;

struct _DbusmenuCollectorPrivate {
	GDBusConnection * bus;
	guint signal;
	GHashTable * hash;
};

struct _DbusmenuCollectorFound {
	gchar * display_string;
	guint distance;
	DbusmenuMenuitem * item;
};

typedef struct _menu_key_t menu_key_t;
struct _menu_key_t {
	gchar * sender;
	gchar * path;
};

typedef struct _search_item_t search_item_t;
struct _search_item_t {
	gchar * string;
	guint distance;
};

static void dbusmenu_collector_class_init (DbusmenuCollectorClass *klass);
static void dbusmenu_collector_init       (DbusmenuCollector *self);
static void dbusmenu_collector_dispose    (GObject *object);
static void dbusmenu_collector_finalize   (GObject *object);
static void update_layout_cb (GDBusConnection * connection, const gchar * sender, const gchar * path, const gchar * interface, const gchar * signal, GVariant * params, gpointer user_data);
static guint menu_hash_func (gconstpointer key);
static gboolean menu_equal_func (gconstpointer a, gconstpointer b);
static void menu_key_destroy (gpointer key);
static DbusmenuCollectorFound * dbusmenu_collector_found_new(DbusmenuMenuitem * item, const gchar * string, guint distance, const gchar * indicator_name);

G_DEFINE_TYPE (DbusmenuCollector, dbusmenu_collector, G_TYPE_OBJECT);

static void
dbusmenu_collector_class_init (DbusmenuCollectorClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	g_type_class_add_private (klass, sizeof (DbusmenuCollectorPrivate));

	object_class->dispose = dbusmenu_collector_dispose;
	object_class->finalize = dbusmenu_collector_finalize;

	return;
}

static void
dbusmenu_collector_init (DbusmenuCollector *self)
{
	self->priv = G_TYPE_INSTANCE_GET_PRIVATE ((self), DBUSMENU_COLLECTOR_TYPE, DbusmenuCollectorPrivate);

	self->priv->bus = NULL;
	self->priv->signal = 0;
	self->priv->hash = NULL;

	self->priv->hash = g_hash_table_new_full(menu_hash_func, menu_equal_func,
	                                         menu_key_destroy, g_object_unref /* DbusmenuClient */);

	self->priv->bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, NULL);
	self->priv->signal = g_dbus_connection_signal_subscribe(self->priv->bus,
	                                                        NULL, /* sender */
	                                                        "com.canonical.dbusmenu", /* interface */
	                                                        "LayoutUpdated", /* member */
	                                                        NULL, /* object path */
	                                                        NULL, /* arg0 */
	                                                        G_DBUS_SIGNAL_FLAGS_NONE, /* flags */
	                                                        update_layout_cb, /* cb */
	                                                        self, /* data */
	                                                        NULL); /* free func */


	return;
}

static void
dbusmenu_collector_dispose (GObject *object)
{
	DbusmenuCollector * collector = DBUSMENU_COLLECTOR(object);

	if (collector->priv->signal != 0) {
		g_dbus_connection_signal_unsubscribe(collector->priv->bus, collector->priv->signal);
		collector->priv->signal = 0;
	}

	if (collector->priv->hash != NULL) {
		g_hash_table_destroy(collector->priv->hash);
		collector->priv->hash = NULL;
	}

	G_OBJECT_CLASS (dbusmenu_collector_parent_class)->dispose (object);
	return;
}

static void
dbusmenu_collector_finalize (GObject *object)
{

	G_OBJECT_CLASS (dbusmenu_collector_parent_class)->finalize (object);
	return;
}

DbusmenuCollector *
dbusmenu_collector_new (void)
{
	return DBUSMENU_COLLECTOR(g_object_new(DBUSMENU_COLLECTOR_TYPE, NULL));
}

static void
update_layout_cb (GDBusConnection * connection, const gchar * sender, const gchar * path, const gchar * interface, const gchar * signal, GVariant * params, gpointer user_data)
{
	g_debug("Client updating %s:%s", sender, path);
	DbusmenuCollector * collector = DBUSMENU_COLLECTOR(user_data);
	g_return_if_fail(collector != NULL);

	menu_key_t search_key = {
		sender: (gchar *)sender,
		path:   (gchar *)path
	};

	gpointer found = g_hash_table_lookup(collector->priv->hash, &search_key);
	if (found == NULL) {
		/* Build one becasue we don't have it */
		menu_key_t * built_key = g_new0(menu_key_t, 1);
		built_key->sender = g_strdup(sender);
		built_key->path = g_strdup(path);

		DbusmenuClient * client = dbusmenu_client_new(sender, path);

		g_hash_table_insert(collector->priv->hash, built_key, client);
	}

	/* Assume that dbusmenu client is doing this for us */
	return;
}

static guint
menu_hash_func (gconstpointer key)
{
	const menu_key_t * mk = (const menu_key_t*)key;

	return g_str_hash(mk->sender) + g_str_hash(mk->path) - 5381;
}

static gboolean
menu_equal_func (gconstpointer a, gconstpointer b)
{
	const menu_key_t * ak = (const menu_key_t *)a;
	const menu_key_t * bk = (const menu_key_t *)b;

	if (g_strcmp0(ak->sender, bk->sender) == 0) {
		return TRUE;
	}

	if (g_strcmp0(ak->path, bk->path) == 0) {
		return TRUE;
	}

	return FALSE;
}

static void
menu_key_destroy (gpointer key)
{
	menu_key_t * ikey = (menu_key_t *)key;

	g_free(ikey->sender);
	g_free(ikey->path);

	g_free(ikey);
	return;
}

gchar *
remove_underline (const gchar * input)
{
	const gchar * in = input;
	gchar * output = g_new0(gchar, g_utf8_strlen(input, -1) + 1);
	gchar * out = output;

	while (in[0] != '\0') {
		if (in[0] == '_') {
			in++;
		} else {
			out[0] = in[0];
			in++;
			out++;
		}
	}

	return output;
}

static GList *
tokens_to_children (DbusmenuMenuitem * rootitem, const gchar * search, GList * results, const gchar * label_prefix, DbusmenuClient * client, const gchar * indicator_name)
{
	if (search == NULL) {
		return results;
	}

	if (rootitem == NULL) {
		return results;
	}

	gchar * newstr = NULL;
	if (dbusmenu_menuitem_property_exist(rootitem, DBUSMENU_MENUITEM_PROP_LABEL)) {
		gchar * nounderline = remove_underline(dbusmenu_menuitem_property_get(rootitem, DBUSMENU_MENUITEM_PROP_LABEL));
		if (label_prefix != NULL && label_prefix[0] != '\0') {
			newstr = g_strdup_printf("%s > %s", label_prefix, nounderline);
			g_free(nounderline);
		} else {
			newstr = nounderline;
		}
	} else {
		newstr = g_strdup(label_prefix);
	}

	if (!dbusmenu_menuitem_get_root(rootitem) && dbusmenu_menuitem_property_exist(rootitem, DBUSMENU_MENUITEM_PROP_LABEL)) {
		guint distance = calculate_distance(search, newstr);
		results = g_list_prepend(results, dbusmenu_collector_found_new(rootitem, newstr, distance, indicator_name));
	}

	GList * children = dbusmenu_menuitem_get_children(rootitem);
	GList * child;

	for (child = children; child != NULL; child = g_list_next(child)) {
		DbusmenuMenuitem * item = DBUSMENU_MENUITEM(child->data);

		results = tokens_to_children(item, search, results, newstr, client, indicator_name);
	}

	g_free(newstr);
	return results;
}

static GList *
process_client (DbusmenuCollector * collector, DbusmenuClient * client, const gchar * search, GList * results, const gchar * indicator_name)
{
	/* Handle the case where there are no search terms */
	if (search == NULL || search[0] == '\0') {
		GList * children = dbusmenu_menuitem_get_children(dbusmenu_client_get_root(client));
		GList * child;

		for (child = children; child != NULL; child = g_list_next(child)) {
			DbusmenuMenuitem * item = DBUSMENU_MENUITEM(child->data);

			if (!dbusmenu_menuitem_property_exist(item, DBUSMENU_MENUITEM_PROP_LABEL)) {
				continue;
			}

			const gchar * label = dbusmenu_menuitem_property_get(item, DBUSMENU_MENUITEM_PROP_LABEL);
			gchar * nounderline = remove_underline(label);

			results = g_list_prepend(results, dbusmenu_collector_found_new(item, nounderline, calculate_distance(nounderline, NULL), indicator_name));
			g_free(nounderline);
		}

		return results;
	}

	results = tokens_to_children(dbusmenu_client_get_root(client), search, results, "", client, indicator_name);
	return results;
}

static GList *
just_do_it (DbusmenuCollector * collector, const gchar * dbus_addr, const gchar * dbus_path, const gchar * search, GList * results)
{
	g_return_val_if_fail(IS_DBUSMENU_COLLECTOR(collector), results);

	menu_key_t search_key = {
		sender: (gchar *)dbus_addr,
		path:   (gchar *)dbus_path
	};

	gpointer found = g_hash_table_lookup(collector->priv->hash, &search_key);
	if (found != NULL) {
		results = process_client(collector, DBUSMENU_CLIENT(found), search, results, NULL);
	}

	return results;
}

GList *
dbusmenu_collector_search (DbusmenuCollector * collector, const gchar * dbus_addr, const gchar * dbus_path, const gchar * search)
{
	GList * items = just_do_it(collector, dbus_addr, dbus_path, search, NULL);

	/* This is where we'll do the indicators */

	return items;
}

guint
dbusmenu_collector_found_get_distance (DbusmenuCollectorFound * found)
{
	g_return_val_if_fail(found != NULL, G_MAXUINT);
	return found->distance;
}

const gchar *
dbusmenu_collector_found_get_display (DbusmenuCollectorFound * found)
{
	g_return_val_if_fail(found != NULL, NULL);
	return found->display_string;
}

void
dbusmenu_collector_found_exec (DbusmenuCollectorFound * found)
{
	g_return_if_fail(found != NULL);
	g_return_if_fail(found->item != NULL);

	g_debug("Executing menuitem %d: %s", dbusmenu_menuitem_get_id(found->item), dbusmenu_menuitem_property_get(found->item, DBUSMENU_MENUITEM_PROP_LABEL));
	dbusmenu_menuitem_handle_event(found->item, DBUSMENU_MENUITEM_EVENT_ACTIVATED, NULL, 0);
	return;
}

void
dbusmenu_collector_found_list_free (GList * found_list)
{
	g_list_free_full(found_list, (GDestroyNotify)dbusmenu_collector_found_free);
	return;
}

static DbusmenuCollectorFound *
dbusmenu_collector_found_new(DbusmenuMenuitem * item, const gchar * string, guint distance, const gchar * indicator_name)
{
	DbusmenuCollectorFound * found = g_new0(DbusmenuCollectorFound, 1);

	found->display_string = g_strdup(string);
	found->distance = distance;
	found->item = item;

	g_object_ref(G_OBJECT(item));

	return found;
}

void
dbusmenu_collector_found_free (DbusmenuCollectorFound * found)
{
	g_return_if_fail(found != NULL);
	g_free(found->display_string);
	g_object_unref(found->item);
	g_free(found);
	return;
}

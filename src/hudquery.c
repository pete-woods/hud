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
 * Author: Ryan Lortie <desrt@desrt.ca>
 */

#define G_LOG_DOMAIN "hudquery"

#include <dee.h>

#include "hudquery.h"
#include "hud-query-iface.h"

#include "hudresult.h"

/**
 * SECTION:hudquery
 * @title: HudQuery
 * @short_description: a stateful query against a #HudSource
 *
 * #HudQuery is a stateful query for a particular search string against
 * a given #HudSource.
 *
 * The query monitors its source for the "change" signal and re-submits
 * the query when changes are reported.  The query has its own change
 * signal that is fired when this happens.
 *
 * The query maintains a list of results from the search which are
 * sorted by relevance and accessible by index.  Contrast this with the
 * stateless nature of #HudSource.
 **/

/**
 * HudQuery:
 *
 * This is an opaque structure type.
 **/

struct _HudQuery
{
  GObject parent_instance;

  HudSource *source;
  gchar *search_string;
  HudTokenList *token_list;
  gint num_results;
  guint refresh_id;

  guint querynumber; /* Incrementing count, which one were we? */
  HudQueryIfaceComCanonicalHudQuery * skel;
  gchar * object_path;
  DeeModel * results_model;
  gchar * results_name;
  DeeModelTag * results_tag;
};

typedef GObjectClass HudQueryClass;

G_DEFINE_TYPE (HudQuery, hud_query, G_TYPE_OBJECT)

static guint hud_query_changed_signal;

static HudQuery *last_created_query;

static guint query_count = 0;

static const gchar * results_model_schema[] = {
	"s", /* Command ID */
	"s", /* Command Name */
	"a(ii)", /* Highlights in command name */
	"s", /* Description */
	"a(ii)", /* Highlights in description */
	"s", /* Shortcut */
	"u", /* Distance */
};

static gint
compare_func (GVariant   ** a,
              GVariant   ** b,
              gpointer      user_data)
{
  guint distance_a;
  guint distance_b;

  distance_a = g_variant_get_uint32(a[6]);
  distance_b = g_variant_get_uint32(b[6]);

  return distance_a - distance_b;
}

/* Add a HudResult to the list of results */
static void
results_list_populate (HudResult * result, gpointer user_data)
{
	HudQuery * query = (HudQuery *)user_data;
	HudItem * item = hud_result_get_item(result);

	GVariant * columns[G_N_ELEMENTS(results_model_schema) + 1];
	columns[0] = g_variant_new_string("id");
	columns[1] = g_variant_new_string(hud_item_get_command(item));
	columns[2] = g_variant_new_array(G_VARIANT_TYPE("(ii)"), NULL, 0);
	columns[3] = g_variant_new_string(hud_item_get_context(item));
	columns[4] = g_variant_new_array(G_VARIANT_TYPE("(ii)"), NULL, 0);
	columns[5] = g_variant_new_string(hud_item_get_shortcut(item));
	columns[6] = g_variant_new_uint32(hud_result_get_distance(result, 0)); /* TODO: Figure out max usage */
	columns[7] = NULL;

	DeeModelIter * iter = dee_model_insert_row_sorted(query->results_model,
	                                                  columns /* variants */,
	                                                  compare_func, NULL);

	dee_model_set_tag(query->results_model, iter, query->results_tag, result);

	return;
}

static void
hud_query_refresh (HudQuery *query)
{
  guint64 start_time;

  start_time = g_get_monotonic_time ();

  dee_model_clear(query->results_model);

  if (hud_token_list_get_length (query->token_list) != 0)
    hud_source_search (query->source, query->token_list, results_list_populate, query);

  g_debug ("query took %dus\n", (int) (g_get_monotonic_time () - start_time));
}

static gboolean
hud_query_dispatch_refresh (gpointer user_data)
{
  HudQuery *query = user_data;

  hud_query_refresh (query);

  g_signal_emit (query, hud_query_changed_signal, 0);

  query->refresh_id = 0;

  return G_SOURCE_REMOVE;
}
static void
hud_query_source_changed (HudSource *source,
                          gpointer   user_data)
{
  HudQuery *query = user_data;

  if (!query->refresh_id)
    query->refresh_id = g_idle_add (hud_query_dispatch_refresh, query);
}

static void
hud_query_finalize (GObject *object)
{
  HudQuery *query = HUD_QUERY (object);

  g_debug ("Destroyed query '%s'", query->search_string);

  /* TODO: move to destroy */
  g_clear_object(&query->skel);
  g_clear_object(&query->results_model);

  if (query->refresh_id)
    g_source_remove (query->refresh_id);

  hud_source_unuse (query->source);

  g_object_unref (query->source);
  hud_token_list_free (query->token_list);
  g_free (query->search_string);

  g_clear_pointer(&query->object_path, g_free);
  g_clear_pointer(&query->results_name, g_free);

  G_OBJECT_CLASS (hud_query_parent_class)
    ->finalize (object);
}

static void
hud_query_init (HudQuery *query)
{
  query->querynumber = query_count++;

  query->skel = hud_query_iface_com_canonical_hud_query_skeleton_new();
  query->object_path = g_strdup_printf("/com/canonical/hud/query%d", query->querynumber);
  g_dbus_interface_skeleton_export(G_DBUS_INTERFACE_SKELETON(query->skel),
                                   g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, NULL),
                                   query->object_path,
                                   NULL);

  query->results_name = g_strdup_printf("com.canonical.hud.query%d.results", query->querynumber);
  query->results_model = dee_shared_model_new(query->results_name);
  dee_model_set_schema_full(query->results_model, results_model_schema, G_N_ELEMENTS(results_model_schema));
  query->results_tag = dee_model_register_tag(query->results_model, g_object_unref);

  return;
}

static void
hud_query_class_init (HudQueryClass *class)
{
  /**
   * HudQuery::changed:
   * @query: a #HudQuery
   *
   * Indicates that the results of @query have changed.
   **/
  hud_query_changed_signal = g_signal_new ("changed", HUD_TYPE_QUERY, G_SIGNAL_RUN_LAST, 0, NULL,
                                           NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);

  class->finalize = hud_query_finalize;
}

/**
 * hud_query_new:
 * @source: the #HudSource against which to search
 * @search_string: the string to search for
 * @num_results: the maximum number of results to report
 *
 * Creates a #HudQuery.
 *
 * A #HudQuery is a stateful search for @search_string against a @source.
 *
 * Each #HudQuery is assigned a "query key" when it is created.  This
 * can be used to lookup the hud query later using hud_query_lookup().
 * Because of this, an internal reference is held on the query and the
 * query won't be completely freed until you call hud_query_close() on
 * it in addition to releasing your ref.
 *
 * Returns: the new #HudQuery
 **/
HudQuery *
hud_query_new (HudSource   *source,
               const gchar *search_string,
               gint         num_results)
{
  HudQuery *query;

  g_debug ("Created query '%s'", search_string);

  query = g_object_new (HUD_TYPE_QUERY, NULL);
  query->source = g_object_ref (source);
  query->search_string = g_strdup (search_string);
  query->token_list = hud_token_list_new_from_string (query->search_string);
  query->num_results = num_results;

  hud_source_use (query->source);

  hud_query_refresh (query);

  g_signal_connect_object (source, "changed", G_CALLBACK (hud_query_source_changed), query, 0);

  g_clear_object (&last_created_query);
  last_created_query = g_object_ref (query);

  return query;
}

/**
 * hud_query_get_query_key:
 * @query: a #HudQuery
 *
 * Returns the query key for @HudQuery.
 *
 * Each #HudQuery has a unique identifying key that is assigned when the
 * query is created.
 *
 * FIXME: This is a lie.
 *
 * Returns: (transfer none): the query key for @query
 **/
GVariant *
hud_query_get_query_key (HudQuery *query)
{
  static GVariant *query_key;

  if (query_key == NULL)
    query_key = g_variant_ref_sink (g_variant_new_string ("query key"));

  return query_key;
}

/**
 * hud_query_lookup:
 * @query_key: a query key
 *
 * Finds the query that has the given @query_key.
 *
 * Returns: (transfer none): the query, or %NULL if no such query exists
 **/
HudQuery *
hud_query_lookup (GVariant *query_key)
{
  return last_created_query;
}

/**
 * hud_query_close:
 * @query: a #HudQuery
 *
 * Closes a #HudQuery.
 *
 * This drops the query from the internal list of queries.  Future use
 * of hud_query_lookup() to find this query will fail.
 *
 * You must still release your own reference on @query, if you have one.
 * This only drops the internal reference.
 **/
void
hud_query_close (HudQuery *query)
{
  if (query == last_created_query)
    g_clear_object (&last_created_query);
}

/**
 * hud_query_get_path:
 * @query: a #HudQuery
 *
 * Gets the path that the query object is exported to DBus on.
 *
 * Return value: A dbus object path
 */
const gchar *
hud_query_get_path (HudQuery    *query)
{
	g_return_val_if_fail(HUD_IS_QUERY(query), NULL);

	return query->object_path;
}

/**
 * hud_query_get_results_name:
 * @query: a #HudQuery
 *
 * Gets the DBus name that the shared results model is using
 *
 * Return value: A dbus name
 */
const gchar *
hud_query_get_results_name (HudQuery    *query)
{
	g_return_val_if_fail(HUD_IS_QUERY(query), NULL);

	return query->results_name;
}

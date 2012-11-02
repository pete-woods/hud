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

#define G_LOG_DOMAIN "hud-service"

#include "config.h"

#include <glib.h>
#include <gio/gio.h>
#include <gio/gunixoutputstream.h>
#include <glib/gstdio.h>
#include <stdlib.h>
#include <locale.h>
#include <libintl.h>

/* Pocket Sphinx */
#include "pocketsphinx.h"

#include "hudappindicatorsource.h"
#include "hudindicatorsource.h"
#include "hudwebappsource.h"
#include "hudwindowsource.h"
#include "huddebugsource.h"
#include "hudsourcelist.h"
#include "hudsettings.h"

#include "hud.interface.h"
#include "shared-values.h"
#include "hudquery.h"

static arg_t sphinx_cmd_ln[] = {
	POCKETSPHINX_OPTIONS,
	{NULL, 0, NULL, NULL}
};

/* Actually recognizing the Audio */
static void
recognize_audio(const gchar * lm_filename, const gchar * audio_filename, const gchar * pron_filename)
{
	cmd_ln_t * spx_cmd = cmd_ln_init(NULL, sphinx_cmd_ln, TRUE,
	                                 "-hmm", "/usr/share/pocketsphinx/model/hmm/wsj1",
	                                 "-mdef", "/usr/share/pocketsphinx/model/hmm/wsj1/mdef",
	                                 "-lm", lm_filename,
	                                 "-dict", pron_filename,
	                                 NULL);
	ps_decoder_t * decoder = ps_init(spx_cmd);

	FILE * audiof = fopen(audio_filename, "rb");

	ps_decode_raw(decoder, audiof, "filename", -1);

	char const *hyp, *uttid;
	int32 score;

	hyp = ps_get_hyp(decoder, &score, &uttid);
	if (hyp == NULL) {
		return;
	}
	g_debug("Recognized: %s\n", hyp);

	ps_free(decoder);

	return;
}

/* Function to try and get a query from voice */
static gchar *
do_voice (HudSource * source_kinda)
{
	HudCollector * collector = hud_source_list_active_collector(HUD_SOURCE_LIST(source_kinda));
	if (collector == NULL) {
		/* No active window, that's fine, but we'll just move on */
		return NULL;
	}

	GList * items = hud_collector_get_items(collector);
	if (items == NULL) {
		/* The active window doesn't have items, that's cool.  We'll move on. */
		return NULL;
	}

	/* Get the pronounciations for the items */
	GHashTable * pronounciations = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, (GDestroyNotify)g_strfreev);
	g_list_foreach(items, (GFunc)hud_item_insert_pronounciation, pronounciations);

	/* Get our cache together */
	gchar * string_filename = NULL;
	gchar * pron_filename = NULL;
	gchar * audio_filename = NULL;
	gchar * lm_filename = NULL;

	gint string_file = 0;
	gint pron_file = 0;
	gint audio_file = 0;
	gint lm_file = 0;

	GError * error = NULL;

	if ((string_file = g_file_open_tmp("hud-strings-XXXXXX.txt", &string_filename, &error)) == 0 ||
			(pron_file = g_file_open_tmp("hud-pronounciations-XXXXXX.txt", &pron_filename, &error)) == 0 ||
			(lm_file = g_file_open_tmp("hud-lang-XXXXXX.lm", &lm_filename, &error)) == 0 ||
			(audio_file = g_file_open_tmp("hud-voice-XXXXXX.raw", &audio_filename, &error)) == 0) {
		g_warning("Unable to open temporary filee: %s", error->message);

		if (string_file != 0) {
			close(string_file);
			g_unlink(string_filename);
			g_free(string_filename);
		}

		if (pron_file != 0) {
			close(pron_file);
			g_unlink(pron_filename);
			g_free(pron_filename);
		}

		if (audio_file != 0) {
			close(audio_file);
			g_unlink(audio_filename);
			g_free(audio_filename);
		}

		if (lm_file != 0) {
			close(lm_file);
			g_unlink(lm_filename);
			g_free(lm_filename);
		}

		g_error_free(error);
		g_hash_table_unref(pronounciations);
		return NULL;
	}

	/* Now we have files -- now streams */
	GOutputStream * string_output = g_unix_output_stream_new(string_file, FALSE);
	GOutputStream * pron_output = g_unix_output_stream_new(pron_file, FALSE);
	GOutputStream * audio_output = g_unix_output_stream_new(audio_file, FALSE);

	/* Go through all of the pronounciations */
	GHashTableIter iter;
	g_hash_table_iter_init(&iter, pronounciations);
	gpointer key, value;
	while (g_hash_table_iter_next(&iter, &key, &value)) {
		g_output_stream_write(string_output, "<s> ", g_utf8_strlen("<s> ", -1), NULL, NULL);
		g_output_stream_write(string_output, key, g_utf8_strlen(key, -1), NULL, NULL);
		g_output_stream_write(string_output, " </s>\n", g_utf8_strlen(" </s>\n", -1), NULL, NULL);

		gchar ** prons = (gchar **)value;
		gint i;

		for (i = 0; prons[i] != NULL; i++) {
			g_output_stream_write(pron_output, key, g_utf8_strlen(key, -1), NULL, NULL);
			g_output_stream_write(pron_output, "\t", g_utf8_strlen("\t", -1), NULL, NULL);
			g_output_stream_write(pron_output, prons[i], g_utf8_strlen(prons[i], -1), NULL, NULL);
			g_output_stream_write(pron_output, "\n", g_utf8_strlen("\n", -1), NULL, NULL);
		}
	}

	g_hash_table_unref(pronounciations);
	g_clear_object(&string_output);
	g_clear_object(&pron_output);
	g_clear_object(&audio_output);

	if (string_file != 0) {
		close(string_file);
	}

	if (pron_file != 0) {
		close(pron_file);
	}

	if (audio_file != 0) {
		close(audio_file);
	}

	if (lm_file != 0) {
		close(lm_file);
	}

	g_debug("String: %s", string_filename);
	g_debug("Pronounciations: %s", pron_filename);
	g_debug("Audio: %s", audio_filename);
	g_debug("Lang Model: %s", lm_filename);

	/* Okay, now some shell stuff */
	g_setenv("IRSTLM", "/usr/bin/", TRUE);

	gchar * buildlm = g_strdup_printf("/usr/bin/build-lm.sh -i %s -o %s.gz", string_filename, lm_filename);
	g_spawn_command_line_sync(buildlm, NULL, NULL, NULL, NULL);
	g_free(buildlm);

	gchar * unzipit = g_strdup_printf("gzip -d %s.gz", lm_filename);
	g_spawn_command_line_sync(unzipit, NULL, NULL, NULL, NULL);
	g_free(unzipit);

	gchar * record = g_strdup_printf("timeout -s KILL 5 gst-launch-0.10 pulsesrc ! 'audio/x-raw-int,channels=1;audio/x-raw-int' ! audioconvert ! audio/x-raw-int,channels=1,depth=16 ! wavenc ! filesink location=%s", audio_filename);
	g_spawn_command_line_sync(record, NULL, NULL, NULL, NULL);
	g_free(record);

	recognize_audio(lm_filename, audio_filename, pron_filename);

	if (string_file != 0) {
		g_unlink(string_filename);
	}

	if (pron_file != 0) {
		g_unlink(pron_filename);
	}

	if (audio_file != 0) {
		g_unlink(audio_filename);
	}

	if (lm_file != 0) {
		g_unlink(lm_filename);
	}

	g_free(string_filename);
	g_free(pron_filename);
	g_free(audio_filename);
	g_free(lm_filename);

	return NULL;
}

/* The return value of 'StartQuery' and the signal parameters for
 * 'UpdatedQuery' are the same, so use a utility function for both.
 */
GVariant *
describe_query (HudQuery *query)
{
  GVariantBuilder builder;
  gint n, i;

  n = hud_query_get_n_results (query);

  g_variant_builder_init (&builder, G_VARIANT_TYPE ("(sa(sssssv)v)"));

  /* Target */
  g_variant_builder_add (&builder, "s", "");

  /* List of results */
  g_variant_builder_open (&builder, G_VARIANT_TYPE ("a(sssssv)"));
  for (i = 0; i < n; i++)
    {
      HudResult *result = hud_query_get_result_by_index (query, i);
      HudItem *item;

      item = hud_result_get_item (result);

      g_variant_builder_add (&builder, "(sssssv)",
                             hud_result_get_html_description (result),
                             hud_item_get_app_icon (item),
                             hud_item_get_item_icon (item),
                             "" /* complete text */ , "" /* accel */,
                             g_variant_new_uint64 (hud_item_get_id (item)));
    }
  g_variant_builder_close (&builder);

  /* Query key */
  g_variant_builder_add (&builder, "v", hud_query_get_query_key (query));

  return g_variant_builder_end (&builder);
}

static void
query_changed (HudQuery *query,
               gpointer  user_data)
{
  GDBusConnection *connection = user_data;

  g_debug ("emit UpdatedQuery signal");

  g_dbus_connection_emit_signal (connection, NULL, DBUS_PATH,
                                 DBUS_IFACE, "UpdatedQuery",
                                 describe_query (query), NULL);
}

static GVariant *
unpack_platform_data (GVariant *parameters)
{
  GVariant *platform_data;
  gchar *startup_id;
  guint32 timestamp;

  g_variant_get_child (parameters, 1, "u", &timestamp);
  startup_id = g_strdup_printf ("_TIME%u", timestamp);
  platform_data = g_variant_new_parsed ("{'desktop-startup-id': < %s >}", startup_id);
  g_free (startup_id);

  return g_variant_ref_sink (platform_data);
}

static gboolean
drop_query_timeout (gpointer user_data)
{
  g_object_unref (user_data);

  return G_SOURCE_REMOVE;
}

static void
bus_method (GDBusConnection       *connection,
            const gchar           *sender,
            const gchar           *object_path,
            const gchar           *interface_name,
            const gchar           *method_name,
            GVariant              *parameters,
            GDBusMethodInvocation *invocation,
            gpointer               user_data)
{
  HudSource *source = user_data;

  if (g_str_equal (method_name, "StartQuery"))
    {
      const gchar *search_string;
      gint num_results;
      HudQuery *query;

      g_variant_get (parameters, "(&si)", &search_string, &num_results);
      g_debug ("'StartQuery' from %s: '%s', %d", sender, search_string, num_results);
      query = hud_query_new (source, search_string, num_results);
      g_signal_connect_object (query, "changed", G_CALLBACK (query_changed), connection, 0);
      g_dbus_method_invocation_return_value (invocation, describe_query (query));
      g_object_unref (query);
    }

  else if (g_str_equal (method_name, "ExecuteQuery"))
    {
      GVariant *platform_data;
      GVariant *item_key;
      guint64 key_value;
      HudItem *item;

      g_variant_get_child (parameters, 0, "v", &item_key);

      if (!g_variant_is_of_type (item_key, G_VARIANT_TYPE_UINT64))
        {
          g_debug ("'ExecuteQuery' from %s: incorrect item key (not uint64)", sender);
          g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
                                                 "item key has invalid format");
          g_variant_unref (item_key);
          return;
        }

      key_value = g_variant_get_uint64 (item_key);
      g_variant_unref (item_key);

      item = hud_item_lookup (key_value);
      g_debug ("'ExecuteQuery' from %s, item #%"G_GUINT64_FORMAT": %p", sender, key_value, item);

      if (item == NULL)
        {
          g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
                                                 "item specified by item key does not exist");
          return;
        }

      platform_data = unpack_platform_data (parameters);
      hud_item_activate (item, platform_data);
      g_variant_unref (platform_data);

      g_dbus_method_invocation_return_value (invocation, NULL);
    }

  else if (g_str_equal (method_name, "CloseQuery"))
    {
      GVariant *query_key;
      HudQuery *query;

      g_debug ("Got 'CloseQuery' from %s", sender);

      g_variant_get (parameters, "(v)", &query_key);
      query = hud_query_lookup (query_key);
      g_variant_unref (query_key);

      if (query != NULL)
        {
          g_signal_handlers_disconnect_by_func (query, query_changed, connection);
          /* Unity does 'CloseQuery' immediately followed by
           * 'StartQuery' on every keystroke.  Delay the destruction of
           * the query for a moment just in case a 'StartQuery' is on the
           * way.
           *
           * That way we can avoid allowing the use count to drop to
           * zero only to be increased again back to 1.  This prevents a
           * bunch of dbusmenu "closed"/"opened" calls being sent.
           */
          g_timeout_add (1000, drop_query_timeout, g_object_ref (query));
          hud_query_close (query);
        }

      /* always success -- they may have just been closing a timed out query */
      g_dbus_method_invocation_return_value (invocation, NULL);
    }

  else if (g_str_equal (method_name, "VoiceSearch"))
    {
      gchar * querystr = do_voice(source);

      GVariant * str = NULL;
      if (querystr == NULL) {
        str = g_variant_new_string("");
      } else {
        str = g_variant_new_string(querystr);
      }
      g_dbus_method_invocation_return_value (invocation, g_variant_new_tuple(&str, 1));
    }
}

static GMainLoop *mainloop = NULL;

static GDBusInterfaceInfo *
get_iface_info (void)
{
  GDBusInterfaceInfo *iface_info;
  GDBusNodeInfo *node_info;
  GError *error = NULL;

  node_info = g_dbus_node_info_new_for_xml (hud_interface, &error);
  g_assert_no_error (error);

  iface_info = g_dbus_node_info_lookup_interface (node_info, DBUS_IFACE);
  g_assert (iface_info != NULL);

  g_dbus_interface_info_ref (iface_info);
  g_dbus_node_info_unref (node_info);

  return iface_info;
}

static void
bus_acquired_cb (GDBusConnection *connection,
                 const gchar     *name,
                 gpointer         user_data)
{
  HudSource *source = user_data;
  GDBusInterfaceVTable vtable = {
    bus_method
  };
  GError *error = NULL;

  g_debug ("Bus acquired (guid %s)", g_dbus_connection_get_guid (connection));

  if (!g_dbus_connection_register_object (connection, DBUS_PATH, get_iface_info (), &vtable, source, NULL, &error))
    {
      g_warning ("Unable to register path '"DBUS_PATH"': %s", error->message);
      g_main_loop_quit (mainloop);
      g_error_free (error);
    }
}

static void
name_acquired_cb (GDBusConnection *connection,
                  const gchar     *name,
                  gpointer         user_data)
{
  g_debug ("Acquired bus name '%s'", name);
}

static void
name_lost_cb (GDBusConnection *connection,
              const gchar     *name,
              gpointer         user_data)
{
  g_warning ("Unable to get name '%s'", name);
  g_main_loop_quit (mainloop);
}

int
main (int argc, char **argv)
{
  HudWindowSource *window_source;
  HudSourceList *source_list;

  g_type_init ();

  setlocale (LC_ALL, "");
  bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
  textdomain (GETTEXT_PACKAGE);

  hud_settings_init ();

  source_list = hud_source_list_new ();

  /* we will eventually pull GtkMenu out of this, so keep it around */
  window_source = hud_window_source_new ();
  hud_source_list_add (source_list, HUD_SOURCE (window_source));

  {
    HudIndicatorSource *source;

    source = hud_indicator_source_new ();
    hud_source_list_add (source_list, HUD_SOURCE (source));
    g_object_unref (source);
  }

  {
    HudAppIndicatorSource *source;

    source = hud_app_indicator_source_new ();
    hud_source_list_add (source_list, HUD_SOURCE (source));
    g_object_unref (source);
  }
  
  {
    HudWebappSource *source;
    
    source = hud_webapp_source_new (window_source);
    hud_source_list_add (source_list, HUD_SOURCE (source));
    
    g_object_unref (G_OBJECT (source));
  }

  if (getenv ("HUD_DEBUG_SOURCE"))
    {
      HudDebugSource *source;

      source = hud_debug_source_new ();
      hud_source_list_add (source_list, HUD_SOURCE (source));
      g_object_unref (source);
    }

  g_bus_own_name (G_BUS_TYPE_SESSION, DBUS_NAME, G_BUS_NAME_OWNER_FLAGS_NONE,
                  bus_acquired_cb, name_acquired_cb, name_lost_cb, source_list, NULL);

  mainloop = g_main_loop_new (NULL, FALSE);
  g_main_loop_run (mainloop);
  g_main_loop_unref (mainloop);

  g_object_unref (window_source);
  g_object_unref (source_list);

  return 0;
}

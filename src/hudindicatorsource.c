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
 * Authors: Ryan Lortie <desrt@desrt.ca>
 *          Ted Gould <ted@canonical.com>
 */

#include "hudindicatorsource.h"

#include <glib/gi18n.h>
#include <gio/gio.h>

#include "hudsettings.h"
#include "huddbusmenucollector.h"
#include "hudmenumodelcollector.h"
#include "hudsource.h"

/**
 * SECTION:hudindicatorsource
 * @title:HudIndicatorSource
 * @short_description: a #HudSource to search through the menus of
 *   indicators
 *
 * #HudIndicatorSource searches through the menu items of the
 * indicators.
 **/

/**
 * HudIndicatorSource:
 *
 * This is an opaque structure type.
 **/

typedef struct
{
  const gchar *dbus_name;
  const gchar *dbus_menu_path;
  const gchar *action_path; /* Only if gmenumodel */
  const gchar *indicator_name;
  const gchar *user_visible_name;
  const gchar *icon;
  gboolean     uses_gmenumodel;
} IndicatorInfo;

static const IndicatorInfo indicator_info[] = {
  {
    .dbus_name         = "com.canonical.indicator.datetime",
    .dbus_menu_path    = "/com/canonical/indicator/datetime/menu",
    .indicator_name    = "indicator-datetime",
    .user_visible_name = N_("Date"),
    .icon              = "office-calendar"
  },
  {
    .dbus_name         = "com.canonical.indicator.session",
    .dbus_menu_path    = "/com/canonical/indicator/session/menu",
    .indicator_name    = "indicator-session-device",
    .user_visible_name = N_("Device"),
    .icon              = "system-devices-panel"
  },
  {
    .dbus_name         = "com.canonical.indicator.sound",
    .dbus_menu_path    = "/com/canonical/indicator/sound/menu",
    .indicator_name    = "indicator-sound",
    .user_visible_name = N_("Sound"),
    .icon              = "audio-volume-high-panel"
  },
  {
    .dbus_name         = "com.canonical.indicator.messages",
    .dbus_menu_path    = "/com/canonical/indicator/messages/menu",
    .action_path       = "/com/canonical/indicator/messages/menu",
    .indicator_name    = "indicator-messages",
    .user_visible_name = N_("Messages"),
    .icon              = "indicator-messages",
    .uses_gmenumodel   = TRUE
  },
  {
    .dbus_name         = "com.canonical.indicator.messages",
    .dbus_menu_path    = "/com/canonical/indicator/messages/phone",
    .action_path       = "/com/canonical/indicator/messages",
    .indicator_name    = "indicator-messages-phone",
    .user_visible_name = N_("Messages"),
    .icon              = "indicator-messages",
    .uses_gmenumodel   = TRUE
  },
  {
    .dbus_name         = "com.canonical.indicator.battery",
    .dbus_menu_path    = "/com/canonical/indicator/battery/phone",
    .action_path       = "/com/canonical/indicator/battery",
    .indicator_name    = "indicator-battery-phone",
    .user_visible_name = N_("Battery"),
    .icon              = "indicator-battery",
    .uses_gmenumodel   = TRUE
  },
  {
    .dbus_name         = "com.canonical.indicator.time",
    .dbus_menu_path    = "/com/canonical/indicator/time/phone",
    .action_path       = "/com/canonical/indicator/time",
    .indicator_name    = "indicator-time-phone",
    .user_visible_name = N_("Date and Time"),
    .icon              = "indicator-time",
    .uses_gmenumodel   = TRUE
  },
  {
    .dbus_name         = "com.canonical.settings.network",
    .dbus_menu_path    = "/com/canonical/settings/network/phone",
    .action_path       = "/com/canonical/settings/network",
    .indicator_name    = "indicator-network-phone",
    .user_visible_name = N_("Network"),
    .icon              = "indicator-network",
    .uses_gmenumodel   = TRUE
  },
  {
    .dbus_name         = "com.canonical.settings.sound",
    .dbus_menu_path    = "/com/canonical/settings/sound/phone",
    .action_path       = "/com/canonical/settings/sound",
    .indicator_name    = "indicator-sound-phone",
    .user_visible_name = N_("Sound"),
    .icon              = "indicator-sound",
    .uses_gmenumodel   = TRUE
  }
};

typedef struct
{
  const IndicatorInfo *info;
  HudIndicatorSource *source;
  HudSource *collector;
  guint watch_id;
} HudIndicatorSourceIndicator;

struct _HudIndicatorSource
{
  GObject parent_instance;

  HudIndicatorSourceIndicator *indicators;
  gint n_indicators;
  gint use_count;
};

typedef GObjectClass HudIndicatorSourceClass;

static void hud_indicator_source_iface_init (HudSourceInterface *iface);
G_DEFINE_TYPE_WITH_CODE (HudIndicatorSource, hud_indicator_source, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (HUD_TYPE_SOURCE, hud_indicator_source_iface_init))

static void
hud_indicator_source_use (HudSource *hud_source)
{
  HudIndicatorSource *source = HUD_INDICATOR_SOURCE (hud_source);
  gint i;

  if (source->use_count == 0)
    {
      for (i = 0; i < source->n_indicators; i++)
        if (source->indicators[i].collector)
          hud_source_use (source->indicators[i].collector);
    }

  source->use_count++;
}

static void
hud_indicator_source_unuse (HudSource *hud_source)
{
  HudIndicatorSource *source = HUD_INDICATOR_SOURCE (hud_source);
  gint i;

  g_return_if_fail (source->use_count > 0);

  source->use_count--;

  if (source->use_count == 0)
    {
      for (i = 0; i < source->n_indicators; i++)
        if (source->indicators[i].collector)
          hud_source_unuse (source->indicators[i].collector);
    }
}

static void
hud_indicator_source_search (HudSource    *hud_source,
                             HudTokenList *search_string,
                             void        (*append_func) (HudResult * result, gpointer user_data),
                             gpointer      user_data)
{
  HudIndicatorSource *source = HUD_INDICATOR_SOURCE (hud_source);
  gint i;

  for (i = 0; i < source->n_indicators; i++)
    if (source->indicators[i].collector)
      hud_source_search (source->indicators[i].collector, search_string, append_func, user_data);
}

static void
hud_indicator_source_list_applications (HudSource    *hud_source,
                                        HudTokenList *search_string,
                                        void        (*append_func) (const gchar *application_id, const gchar *application_icon, gpointer user_data),
                                        gpointer      user_data)
{
  HudIndicatorSource *source = HUD_INDICATOR_SOURCE (hud_source);
  gint i;

  for (i = 0; i < source->n_indicators; i++)
    if (source->indicators[i].collector)
      hud_source_list_applications (source->indicators[i].collector, search_string, append_func, user_data);
}

static HudSource *
hud_indicator_source_get (HudSource     *hud_source,
                          const gchar   *application_id)
{
  HudIndicatorSource *source = HUD_INDICATOR_SOURCE (hud_source);
  gint i;

  for (i = 0; i < source->n_indicators; i++)
    if (source->indicators[i].collector) {
      HudSource *result = hud_source_get (source->indicators[i].collector, application_id);
      if (result != NULL)
        return result;
    }

  return NULL;
}

static void
hud_indicator_source_finalize (GObject *object)
{
  gint i;
  HudIndicatorSource *source = HUD_INDICATOR_SOURCE(object);

  for (i = 0; i < source->n_indicators; i++)
  {
    HudIndicatorSourceIndicator *indicator = &source->indicators[i];
    if (indicator->collector)
    {
      g_object_unref(indicator->collector);
    }
    g_bus_unwatch_name(indicator->watch_id);
  }

  g_free (source->indicators);

  G_OBJECT_CLASS(hud_indicator_source_parent_class)->finalize(object);
}

static void
hud_indicator_source_collector_changed (HudSource *source,
                                        gpointer   user_data)
{
  HudIndicatorSourceIndicator *indicator = user_data;

  hud_source_changed (HUD_SOURCE (indicator->source));
}

static void
hud_indicator_source_name_appeared (GDBusConnection *connection,
                                    const gchar     *name,
                                    const gchar     *name_owner,
                                    gpointer         user_data)
{
  HudIndicatorSourceIndicator *indicator = user_data;

  if (indicator->info->uses_gmenumodel)
    {
      HudMenuModelCollector *collector = hud_menu_model_collector_new(indicator->info->indicator_name, indicator->info->icon, hud_settings.indicator_penalty);

      hud_menu_model_collector_add_endpoint (collector,
                                             _(indicator->info->user_visible_name),
                                             name_owner,
                                             indicator->info->dbus_menu_path,
                                             indicator->info->action_path);

      indicator->collector = HUD_SOURCE (collector);
    }
  else
    {
      HudDbusmenuCollector *collector;

      GError *error = NULL;
      if (!g_dbus_connection_call_sync (connection, name_owner,
          indicator->info->dbus_menu_path, "com.canonical.dbusmenu", "GetLayout",
          g_variant_new ("(iias)", 0, 1, NULL ), NULL, G_DBUS_CALL_FLAGS_NONE, -1,
          NULL, &error))
        {
          g_error_free (error);
        }
      else
        {
          collector = hud_dbusmenu_collector_new_for_endpoint (indicator->info->indicator_name,
                                                               _(indicator->info->user_visible_name),
                                                               indicator->info->icon,
                                                               hud_settings.indicator_penalty,
                                                               name_owner, indicator->info->dbus_menu_path);
          indicator->collector = HUD_SOURCE (collector);
        }
    }

  if (indicator->collector)
    {
      g_signal_connect (indicator->collector, "changed",
                        G_CALLBACK (hud_indicator_source_collector_changed), indicator);

      /* Set initial use count on new indicator if query is active. */
      if (indicator->source->use_count)
        hud_source_use (indicator->collector);

      hud_source_changed (HUD_SOURCE (indicator->source));
    }
}

static void
hud_indicator_source_name_vanished (GDBusConnection *connection,
                                    const gchar     *name,
                                    gpointer         user_data)
{
  HudIndicatorSourceIndicator *indicator = user_data;

  if (indicator->collector)
    {
      g_signal_handlers_disconnect_by_func (indicator->collector, hud_indicator_source_collector_changed, indicator);
      /* Drop our use count on dying indicator (if any) */
      if (indicator->source->use_count)
        hud_source_unuse (indicator->collector);
      g_clear_object (&indicator->collector);
    }

  hud_source_changed (HUD_SOURCE (indicator->source));
}

static void
hud_indicator_source_init (HudIndicatorSource *source)
{
  source->n_indicators = G_N_ELEMENTS (indicator_info);
  source->indicators = g_new0 (HudIndicatorSourceIndicator, source->n_indicators);
}

static void
hud_indicator_source_iface_init (HudSourceInterface *iface)
{
  iface->use = hud_indicator_source_use;
  iface->unuse = hud_indicator_source_unuse;
  iface->search = hud_indicator_source_search;
  iface->list_applications = hud_indicator_source_list_applications;
  iface->get = hud_indicator_source_get;
}

static void
hud_indicator_source_class_init (HudIndicatorSourceClass *class)
{
  class->finalize = hud_indicator_source_finalize;
}

/**
 * hud_indicator_source_new:
 *
 * Creates a #HudIndicatorSource.
 *
 * Returns: a new #HudIndicatorSource
 **/
HudIndicatorSource *
hud_indicator_source_new (GDBusConnection *connection)
{
  gint i;
  HudIndicatorSource *source = g_object_new (HUD_TYPE_INDICATOR_SOURCE, NULL );

  for (i = 0; i < source->n_indicators; i++)
    {
      HudIndicatorSourceIndicator *indicator = &source->indicators[i];

      indicator->info = &indicator_info[i];
      indicator->source = source;

      indicator->watch_id = g_bus_watch_name_on_connection (connection,
          indicator->info->dbus_name, G_BUS_NAME_WATCHER_FLAGS_NONE,
          hud_indicator_source_name_appeared, hud_indicator_source_name_vanished,
          indicator, NULL );
    }

  return source;
}

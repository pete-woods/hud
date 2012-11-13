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

#if !defined (_HUD_CLIENT_H_INSIDE) && !defined (HUD_CLIENT_COMPILATION)
#error "Only <hud-client.h> can be included directly."
#endif

#ifndef __HUD_CLIENT_QUERY_H__
#define __HUD_CLIENT_QUERY_H__

#include <glib-object.h>
#include <dee.h>
#include <libhud-client/connection.h>

G_BEGIN_DECLS

#define HUD_CLIENT_TYPE_QUERY            (hud_client_query_get_type ())
#define HUD_CLIENT_QUERY(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), HUD_CLIENT_TYPE_QUERY, HudClientQuery))
#define HUD_CLIENT_QUERY_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), HUD_CLIENT_TYPE_QUERY, HudClientQueryClass))
#define HUD_CLIENT_IS_QUERY(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), HUD_CLIENT_TYPE_QUERY))
#define HUD_CLIENT_IS_QUERY_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), HUD_CLIENT_TYPE_QUERY))
#define HUD_CLIENT_QUERY_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), HUD_CLIENT_TYPE_QUERY, HudClientQueryClass))

typedef struct _HudClientQuery         HudClientQuery;
typedef struct _HudClientQueryClass    HudClientQueryClass;
typedef struct _HudClientQueryPrivate  HudClientQueryPrivate;

struct _HudClientQueryClass {
	GObjectClass parent_class;
};

struct _HudClientQuery {
	GObject parent;
	HudClientQueryPrivate * priv;
};

GType hud_client_query_get_type (void);

HudClientQuery *   hud_client_query_new                   (const gchar *           query);
HudClientQuery *   hud_client_query_new_for_connection    (const gchar *           query,
                                                           HudClientConnection *   connection);

void               hud_client_query_set_query             (HudClientQuery *        cquery,
                                                           const gchar *           query);
const gchar *      hud_client_query_get_query             (HudClientQuery *        cquery);

DeeModel *         hud_client_query_get_results_model     (HudClientQuery *        cquery);

G_END_DECLS

#endif

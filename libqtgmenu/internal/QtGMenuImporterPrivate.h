/*
 * Copyright (C) 2013 Canonical, Ltd.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 3, as published
 * by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranties of
 * MERCHANTABILITY, SATISFACTORY QUALITY, or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Author: Marcus Tomlinson <marcus.tomlinson@canonical.com>
 */

#ifndef QTGMENUEXPORTERPRIVATE_H
#define QTGMENUEXPORTERPRIVATE_H

#include <QtGMenuImporter.h>

#include <QMenu>
#include <QTimer>
#include <memory>

#undef signals
#include <gio/gio.h>

namespace qtgmenu
{

class QtGMenuImporterPrivate : public QObject
{
Q_OBJECT

public:
  QtGMenuImporterPrivate( const QString& service, const QString& path, QtGMenuImporter& parent );
  ~QtGMenuImporterPrivate();

  GMenuModel* GetGMenuModel();
  GActionGroup* GetGActionGroup();

  std::vector< std::shared_ptr< QMenu > > GetQMenus();

  void StartPolling( int interval );

private:
  static void MenuItemsChangedCallback( GMenuModel* model, gint position, gint removed, gint added,
      gpointer user_data );

  static void ActionAddedCallback( GActionGroup* action_group, gchar* action_name,
      gpointer user_data );

  static void ActionRemovedCallback( GActionGroup* action_group, gchar* action_name,
      gpointer user_data );

  static void ActionEnabledCallback( GActionGroup* action_group, gchar* action_name,
      gboolean enabled, gpointer user_data );

  static void ActionStateChangedCallback( GActionGroup* action_group, gchar* action_name,
      GVariant* value, gpointer user_data );

  void ClearGMenuModel();
  void ClearGActionGroup();

private Q_SLOTS:
  bool RefreshGMenuModel();
  bool RefreshGActionGroup();

private:
  QtGMenuImporter& m_parent;

  GDBusConnection* m_connection;
  std::string m_service;
  std::string m_path;

  std::vector< std::shared_ptr< QMenu > > m_qmenus;

  GMenuModel* m_gmenu_model;
  gulong m_menu_items_changed_handler = 0;
  QTimer m_menu_poll_timer;

  GActionGroup* m_gaction_group;
  gulong m_action_added_handler = 0;
  gulong m_action_removed_handler = 0;
  gulong m_action_enabled_handler = 0;
  gulong m_action_state_changed_handler = 0;
  QTimer m_actions_poll_timer;
};

} // namespace qtgmenu

#endif /* QTGMENUEXPORTERPRIVATE_H */

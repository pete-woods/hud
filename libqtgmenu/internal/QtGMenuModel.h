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

#ifndef QTGMENUMODEL_H
#define QTGMENUMODEL_H

#include <QObject>
#include <QMap>
#include <QMenu>

#include <memory>
#include <deque>

#undef signals
#include <gio/gio.h>

namespace qtgmenu
{

class QtGMenuModel : public QObject
{
Q_OBJECT

public:
  enum class LinkType
  {
    Root, Section, SubMenu
  };

  explicit QtGMenuModel( GMenuModel* model );
  QtGMenuModel( GMenuModel* model, const QString& bus_name, const QString& menu_path,
      const QString& actions_path );
  virtual ~QtGMenuModel();

  GMenuModel* Model() const;
  LinkType Type() const;

  int Size() const;

  QtGMenuModel* Parent() const;
  QtGMenuModel* Child( int index ) const;

  std::shared_ptr< QMenu > GetQMenu();

  constexpr static const char* c_property_actionName = "actionName";
  constexpr static const char* c_property_isParameterized = "isParameterized";
  constexpr static const char* c_property_busName = "busName";
  constexpr static const char* c_property_menuPath = "menuPath";
  constexpr static const char* c_property_actionsPath = "actionsPath";
  constexpr static const char* c_property_keywords = "keywords";

Q_SIGNALS:
  void MenuItemsChanged( QtGMenuModel* model, int index, int removed, int added );
  void ActionTriggered( QString action_name, bool checked );

public Q_SLOTS:
  void ActionEnabled( QString action_name, bool enabled );
  void ActionParameterized( QString action_name, bool parameterized );

private Q_SLOTS:
  void ActionTriggered( bool );

private:
  QtGMenuModel( GMenuModel* model, LinkType link_type, QtGMenuModel* parent, int index );

  static QtGMenuModel* CreateChild( QtGMenuModel* parent, GMenuModel* model, int index );

  static void MenuItemsChangedCallback( GMenuModel* model, gint index, gint removed, gint added,
      gpointer user_data );

  void ChangeMenuItems( int index, int added, int removed );

  void ConnectCallback();
  void DisconnectCallback();

  void InsertChild( QtGMenuModel* child, int index );
  int ChildIndex( QtGMenuModel* child );

  QAction* CreateAction( int index );
  QAction* FindAction( QString name );

  void AppendQMenu( std::shared_ptr< QMenu > top_menu );
  void UpdateExtQMenu();

private:
  QtGMenuModel* m_parent = nullptr;
  QMap< int, QtGMenuModel* > m_children;

  GMenuModel* m_model = nullptr;
  gulong m_items_changed_handler = 0;

  LinkType m_link_type;
  int m_size = 0;

  QMenu* m_menu = new QMenu();
  QMenu* m_ext_menu = new QMenu();

  QString m_bus_name;
  QString m_menu_path;
  QString m_actions_path;
};

} // namespace qtgmenu

#endif // QTGMENUMODEL_H
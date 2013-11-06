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
 * Author: Pete Woods <pete.woods@canonical.com>
 */

#include <common/DBusTypes.h>
#include <service/DBusMenuCollector.h>
#include <service/AppmenuRegistrarInterface.h>

#include <dbusmenuimporter.h>
#include <QMenu>

using namespace hud::common;
using namespace hud::service;

DBusMenuCollector::DBusMenuCollector(unsigned int windowId,
		QSharedPointer<ComCanonicalAppMenuRegistrarInterface> registrar) :
		m_windowId(windowId), m_registrar(registrar), m_viewerCount(0), m_menu(
				nullptr) {

	connect(registrar.data(),
	SIGNAL(WindowRegistered(uint, const QString &, const QDBusObjectPath &)),
			this,
			SLOT( WindowRegistered(uint, const QString &, const QDBusObjectPath &)));

	connect(registrar.data(), SIGNAL(
			WindowUnregistered(uint)), this, SLOT(WindowUnregistered(uint)));

	QDBusPendingReply<QString, QDBusObjectPath> windowReply =
			registrar->GetMenuForWindow(m_windowId);

	windowReply.waitForFinished();
	if (windowReply.isError()) {
		return;
	}

	windowRegistered(windowReply.argumentAt<0>(), windowReply.argumentAt<1>());
}

DBusMenuCollector::~DBusMenuCollector() {
}

void DBusMenuCollector::windowRegistered(const QString &service,
		const QDBusObjectPath &menuObjectPath) {
	m_service = service;
	m_path = menuObjectPath;

	if (m_service.isEmpty()) {
		m_menuImporter.reset();
		m_menu = nullptr;
		return;
	}

	qDebug() << "DBusMenu available for" << m_windowId << "at" << m_service
			<< m_path.path();

	m_menuImporter.reset(new DBusMenuImporter(m_service, m_path.path()));
	m_menu = m_menuImporter->menu();
}

bool DBusMenuCollector::isValid() const {
	return !m_menuImporter.isNull();
}

inline uint qHash(const QStringList &key, uint seed) {
	uint hash(0);
	for (const QString &s : key) {
		hash ^= qHash(s, seed);
	}
	return hash;
}

static bool openMenu(QMenu *menu, const QStringList &position,
		QSet<QStringList> &known) {
	menu->aboutToShow();

	bool added = false;

	for (int i(0); i < menu->actions().size(); ++i) {
		QAction *action = menu->actions().at(i);
		QStringList childPosition(position);
		childPosition << action->text();

		if (!known.contains(childPosition)) {
			known.insert(childPosition);
			added = true;
		}

		QMenu *child(action->menu());
		if (child) {
			added |= openMenu(child, childPosition, known);
		}
	}

	return added;
}

static void hideMenu(QMenu *menu) {
	for (int i(0); i < menu->actions().size(); ++i) {
		QAction *action = menu->actions().at(i);
		QMenu *child(action->menu());
		if (child) {
			hideMenu(child);
		}
	}

	menu->aboutToHide();
}

static void printMenu(QMenu *menu) {
//	qDebug() << "title:" << menu->title();

	for (const QAction *action : menu->actions()) {
		if (action) {
			qDebug() << "action:" << action->text();
			QList<QKeySequence> shortcuts(action->shortcuts());
			if (!shortcuts.isEmpty()) {
				qDebug() << "shortcuts:" << shortcuts;
			}
			QMenu *child(action->menu());
			if (child) {
				printMenu(child);
			}
		} else {
			qDebug() << "null action";
		}
	}
}

CollectorToken::Ptr DBusMenuCollector::activate() {
	if (m_viewerCount == 0) {
		++m_viewerCount;
		qDebug() << "Opening menus";
		QSet<QStringList> known;
		while (openMenu(m_menu, QStringList(), known)) {
		}
	}
//	printMenu(m_menu);
	return CollectorToken::Ptr(new CollectorToken(*this));
}

void DBusMenuCollector::deactivate() {
	--m_viewerCount;
	if (m_viewerCount == 0) {
		qDebug() << "Hiding menus";
		hideMenu(m_menu);
	}
}

void DBusMenuCollector::WindowRegistered(uint windowId, const QString &service,
		const QDBusObjectPath &menuObjectPath) {
	// Simply ignore updates for other windows
	if (windowId != m_windowId) {
		return;
	}

	windowRegistered(service, menuObjectPath);
}

void DBusMenuCollector::WindowUnregistered(uint windowId) {
}


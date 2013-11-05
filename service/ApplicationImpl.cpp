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
#include <common/Localisation.h>
#include <service/ApplicationImpl.h>
#include <service/ApplicationAdaptor.h>
#include <service/Factory.h>

using namespace hud::common;
using namespace hud::service;

ApplicationImpl::ApplicationImpl(unsigned int id, const QString &applicationId,
		Factory &factory, const QDBusConnection &connection, QObject *parent) :
		Application(parent), m_adaptor(new ApplicationAdaptor(this)), m_connection(
				connection), m_path(DBusTypes::applicationPath(id)), m_applicationId(
				applicationId), m_factory(factory) {
	if (!m_connection.registerObject(m_path.path(), this)) {
		throw std::logic_error(_("Unable to register HUD object on DBus"));
	}
}

ApplicationImpl::~ApplicationImpl() {
	m_connection.unregisterObject(m_path.path());
}

void ApplicationImpl::addWindow(unsigned int windowId) {
	if (m_windows.contains(windowId)) {
		qWarning() << "Adding already known window" << windowId
				<< "to application" << m_applicationId;
		return;
	}
	m_windows[windowId] = m_factory.newWindow(windowId, m_applicationId);
}

void ApplicationImpl::removeWindow(unsigned int windowId) {
	if (!m_windows.contains(windowId)) {
		qWarning() << "Removing unknown window" << windowId
				<< "from application" << m_applicationId;
		return;
	}
	m_windows.remove(windowId);
}

bool ApplicationImpl::isEmpty() const {
	return m_windows.isEmpty();
}

const QDBusObjectPath & ApplicationImpl::path() const {
	return m_path;
}

QList<ActionGroup> ApplicationImpl::actionGroups() const {
	qDebug() << "actionGroups";
	return QList<ActionGroup>();
}

QString ApplicationImpl::desktopPath() {
	if (m_desktopPath.isEmpty()) {
		QString desktopFile(QString("%1.desktop").arg(m_applicationId));

		QStringList xdgDataDirs(
				QString::fromUtf8(qgetenv("XDG_DATA_DIRS")).split(':'));
		for (const QString &dir : xdgDataDirs) {
			QString desktopPath(
					QDir(QDir(dir).filePath("applications")).filePath(
							desktopFile));
			if (QFile::exists(desktopPath)) {
				m_desktopPath = desktopPath;
				break;
			}
		}
	}

	return m_desktopPath;
}

QString ApplicationImpl::icon() {
	if (m_icon.isEmpty()) {
		QString path(desktopPath());
		if (!path.isEmpty()) {
			QSettings settings(path, QSettings::IniFormat);
			settings.beginGroup("Desktop Entry");
			m_icon = settings.value("Icon").toString();
			settings.endGroup();
		}
	}

	return m_icon;
}

QList<MenuModel> ApplicationImpl::menuModels() const {
	qDebug() << "menuModels";
	return QList<MenuModel>();
}

void ApplicationImpl::AddSources(const QList<Action> &actions,
		const QList<Description> &descriptions) {
	qDebug() << "AddSources";
}

void ApplicationImpl::SetWindowContext(uint window, const QString &context) {
	qDebug() << "SetWindowContext";
}

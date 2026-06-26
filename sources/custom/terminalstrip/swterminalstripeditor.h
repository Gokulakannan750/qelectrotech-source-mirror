/*
	Copyright 2026 Trovo Tech Solutions
	This file is part of a custom feature set built on QElectroTech.

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#ifndef SWTERMINALSTRIPEDITOR_H
#define SWTERMINALSTRIPEDITOR_H

#include <QDialog>
#include <QMap>
#include <QPointer>
#include <QUuid>
#include <QVector>

class QETProject;
class Element;
class QTableWidget;
class QComboBox;
class QPushButton;

/**
	@brief SolidWorks-Electrical-style terminal strip editor.

	Symmetric table: Bridge | left (dest/cable/colour) | Mark | right (colour/cable/dest)
	The Bridge column shows coloured spans connecting terminals that share a
	bridge/jumper group. Bridges are stored as a UUID string in each terminal
	element's DiagramContext under the key "bridge_group".

	Phase 1: symmetric view + editable marks.
	Phase 2: multiple wires per terminal end (sub-rows + Mark span).
	Phase 3: bridges and jumpers (Bridge column, Add/Remove bridge actions).
*/
class SwTerminalStripEditor : public QDialog
{
	Q_OBJECT

	public:
		explicit SwTerminalStripEditor(QETProject *project, QWidget *parent = nullptr);

	private slots:
		void reload();
		void applyMarks();
		void addBridge();
		void removeBridge();

	private:
		void buildUi();
		void paintBridges(const QMap<QUuid, QVector<QPointer<Element>>> &groups);

		struct Side { QString destination, cable, colour; };
		/// All conductors landing on connection point @p index of @p terminal.
		QVector<Side> sidesInfo(Element *terminal, int index) const;

		QString stripNameOf(Element *terminal) const;
		QUuid   bridgeGroupOf(Element *e) const;

		/// Returns the set of unique terminal elements for the current table selection.
		QVector<QPointer<Element>> selectedTerminals() const;

	private:
		QPointer<QETProject>      m_project;
		QComboBox                *m_strip_filter      = nullptr;
		QTableWidget             *m_table             = nullptr;
		QPushButton              *m_add_bridge_btn    = nullptr;
		QPushButton              *m_remove_bridge_btn = nullptr;

		/// Every sub-row maps to its parent terminal element.
		QVector<QPointer<Element>> m_row_terminal;
		/// For each visible terminal: (first_row, nsub).
		QMap<QPointer<Element>, QPair<int,int>> m_terminal_rows;
};

#endif // SWTERMINALSTRIPEDITOR_H

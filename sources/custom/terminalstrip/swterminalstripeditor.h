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

	Column layout:
	  BridgeL | Dest(1) | Cable(1) | Colour(1) | Mark | Colour(2) | Cable(2) | Dest(2) | BridgeR

	Terminal end 1 = the connection point whose orientation is North or East
	(i.e. the top / supply screw in standard European mounting).
	Terminal end 2 = the opposite point (South or West = bottom / load screw).
	This is derived from Terminal::orientation() — NOT from element-array index —
	so it stays correct regardless of XML order.

	Bridges are stored as a UUID string in DiagramContext["bridge_group"] on
	each terminal element.  Row order within a strip is stored as an integer in
	DiagramContext["strip_pos"] and can be changed with the Up/Down buttons.

	Phase 1: symmetric view + editable marks.
	Phase 2: multiple wires per terminal end (sub-rows + Mark span).
	Phase 3: bridges and jumpers (BridgeL/BridgeR columns, Add/Remove bridge).
	Phase 4: correct terminal-end numbering, BridgeR column, Up/Down reorder.
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
		void moveUp();
		void moveDown();
		void updateMoveButtons();

	private:
		void buildUi();
		void paintBridges(const QMap<QUuid, QVector<QPointer<Element>>> &groups);

		struct Side { QString destination, cable, colour; };

		/// Wires on terminal-end @p endIndex (0=end 1, 1=end 2) of @p terminal,
		/// where ends are sorted by orientation (North/East first = end 1).
		QVector<Side> sidesInfo(Element *terminal, int endIndex) const;

		QString stripNameOf(Element *terminal) const;
		QUuid   bridgeGroupOf(Element *e) const;
		int     stripPosOf(Element *e) const;

		/// Unique terminal elements for the current table selection.
		QVector<QPointer<Element>> selectedTerminals() const;

	private:
		QPointer<QETProject>      m_project;
		QComboBox                *m_strip_filter      = nullptr;
		QTableWidget             *m_table             = nullptr;
		QPushButton              *m_add_bridge_btn    = nullptr;
		QPushButton              *m_remove_bridge_btn = nullptr;
		QPushButton              *m_up_btn            = nullptr;
		QPushButton              *m_down_btn          = nullptr;

		/// Every sub-row maps to its parent terminal element.
		QVector<QPointer<Element>> m_row_terminal;
		/// For each visible terminal: (first_row, nsub).
		QMap<QPointer<Element>, QPair<int,int>> m_terminal_rows;
		/// Visible terminals in current display order (used by moveUp/moveDown).
		QVector<QPointer<Element>> m_display_order;
};

#endif // SWTERMINALSTRIPEDITOR_H

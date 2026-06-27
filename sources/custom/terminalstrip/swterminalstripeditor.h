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

	Full column layout (symmetric about Mark):
	  BridgeL | Wire(1) | Folio(1) | Dest.(1) | Cable(1) | Colour(1)
	          | Mark |
	          Colour(2) | Cable(2) | Dest.(2) | Folio(2) | Wire(2) | BridgeR

	Wire number  = conductor text (ConductorProperties::text) — same field that
	               QET displays on the wire; this IS the potential/net label in
	               QET's auto-numbering convention.
	Folio        = finalfolio() of the far-end destination element's diagram.
	Bridges      = UUID stored in DiagramContext["bridge_group"].
	Row order    = integer stored in DiagramContext["strip_pos"], undoable.

	Output:
	  "Export CSV"       → writes a flat CSV file of the strip data.
	  "Generate drawing" → renders a PDF of the terminal strip table.
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
		void exportCsv();
		void generateDrawing();

	private:
		void buildUi();
		void paintBridges(const QMap<QUuid, QVector<QPointer<Element>>> &groups);

		struct Side {
			QString wire_number;  ///< conductor text = wire number / potential label
			QString folio;        ///< finalfolio of the far-end element's diagram
			QString destination;  ///< label of the far-end element
			QString cable;        ///< m_cable from ConductorProperties
			QString colour;       ///< m_wire_color or fallback to text colour
		};

		/// Wires on terminal end @p endIndex (0=end1/supply, 1=end2/load).
		/// Ends are sorted by orientation: North/East = end 1, South/West = end 2.
		QVector<Side> sidesInfo(Element *terminal, int endIndex) const;

		QString stripNameOf(Element *terminal) const;
		QUuid   bridgeGroupOf(Element *e) const;
		int     stripPosOf(Element *e) const;

		QVector<QPointer<Element>> selectedTerminals() const;

	private:
		QPointer<QETProject>      m_project;
		QComboBox                *m_strip_filter      = nullptr;
		QTableWidget             *m_table             = nullptr;
		QPushButton              *m_add_bridge_btn    = nullptr;
		QPushButton              *m_remove_bridge_btn = nullptr;
		QPushButton              *m_up_btn            = nullptr;
		QPushButton              *m_down_btn          = nullptr;
		QPushButton              *m_export_csv_btn    = nullptr;
		QPushButton              *m_generate_draw_btn = nullptr;

		QVector<QPointer<Element>> m_row_terminal;   ///< sub-row → terminal element
		QMap<QPointer<Element>, QPair<int,int>> m_terminal_rows; ///< element → (first_row, nsub)
		QVector<QPointer<Element>> m_display_order;  ///< visible terminals in sorted order
};

#endif // SWTERMINALSTRIPEDITOR_H

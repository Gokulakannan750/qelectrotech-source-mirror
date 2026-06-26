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
#include <QPointer>
#include <QVector>

class QETProject;
class Element;
class QTableWidget;
class QComboBox;

/**
	@brief SolidWorks-Electrical-style terminal strip editor (phase 1).

	Scans the project for terminal elements and presents them in the symmetric
	SWE layout — left (destination / cable / colour) | Mark | right (colour /
	cable / destination) — grouped by terminal block. The left/right cable and
	colour come from the conductors attached to each side (populated by the
	wire-catalogue "Assign wires" feature). The Mark column is editable and
	written back to the terminal elements as one undoable step.

	Later phases: bridges, multi-level terminals, insert/delete/reorder, and
	generating the terminal strip drawing.
*/
class SwTerminalStripEditor : public QDialog
{
	Q_OBJECT

	public:
		explicit SwTerminalStripEditor(QETProject *project, QWidget *parent = nullptr);

	private slots:
		void reload();
		void applyMarks();

	private:
		void buildUi();
		struct Side { QString destination, cable, colour; };
		Side sideInfo(Element *terminal, int index) const;
		QString stripNameOf(Element *terminal) const;

	private:
		QPointer<QETProject>      m_project;
		QComboBox                *m_strip_filter = nullptr;
		QTableWidget             *m_table        = nullptr;
		QVector<QPointer<Element>> m_row_terminal; // row -> terminal element
};

#endif // SWTERMINALSTRIPEDITOR_H

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
#ifndef DESIGNATIONMANAGER_H
#define DESIGNATIONMANAGER_H

#include <QString>
#include <QSet>
#include <QMap>
#include <QList>
#include <QPair>
#include <QPointer>

#include "../../diagramcontext.h"

class Element;
class QETProject;

/**
	@brief IEC 81346 reference-designation engine (Feature 3, minimal core).

	When an element is placed, QElectroTech already resolves the element's IEC
	81346-2 prefix letter from qet_labels.xml (Element::getPrefix()), but it
	only turns that into a label when project element auto-numbering is
	configured — which it is not by default, so nothing appears.

	This engine fills that gap: on placement it assigns "<prefix><number>"
	(e.g. X1, K1, H1), choosing the lowest free number for that prefix across
	the whole project. Because it is stateless (numbers are recomputed by
	scanning current labels), gaps left by deleted elements are naturally
	reused by the next placement, and it stays correct across undo/redo.
*/
namespace DesignationManager
{
	/// Assign the next free designation to a freshly placed element.
	/// No-op for slaves/reports, elements without a prefix, or already-labelled
	/// elements (unless @p force is true, e.g. for paste = new number).
	void assignToElement(Element *element, bool force = false);

	/// Numbers already used by a given prefix across the project (excludes
	/// @p exclude, typically the element being assigned).
	QSet<int> usedNumbers(QETProject *project,
						  const QString &prefix,
						  const Element *exclude = nullptr);

	/// Build the element -> (old, new) info map to compact every prefix's
	/// numbering to 1..N (sorted by current number). Manual/non-matching
	/// labels are left untouched. Feed the result to a single
	/// ChangeElementInformationCommand for one-step undo. Empty if nothing
	/// needs changing.
	QMap<QPointer<Element>, QPair<DiagramContext, DiagramContext>>
	renumberMap(QETProject *project);

	/// Find designations shared by more than one element, project-wide.
	/// Returns label -> elements using it (only entries with size > 1).
	QMap<QString, QList<Element *>> findDuplicates(QETProject *project);
}

#endif // DESIGNATIONMANAGER_H

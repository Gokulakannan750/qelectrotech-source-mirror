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
#include "designationmanager.h"

#include "../../qetgraphicsitem/element.h"
#include "../../diagram.h"
#include "../../qetproject.h"
#include "../../diagramcontext.h"

#include <QRegularExpression>
#include <QGraphicsScene>
#include <QTimer>
#include <QPointer>

namespace DesignationManager {

QSet<int> usedNumbers(QETProject *project,
					  const QString &prefix,
					  const Element *exclude)
{
	QSet<int> used;
	if (!project || prefix.isEmpty())
		return used;

	// Match labels of the exact form "<prefix><digits>", e.g. "X12".
	const QRegularExpression rx(
		QStringLiteral("^%1(\\d+)$").arg(QRegularExpression::escape(prefix)));

	const QList<Diagram *> diagrams = project->diagrams();
	for (Diagram *d : diagrams) {
		const QList<Element *> elements = d->elements();
		for (Element *e : elements) {
			if (e == exclude)
				continue;
			const QString label = e->elementInformations()
					.value(QStringLiteral("label")).toString();
			const QRegularExpressionMatch m = rx.match(label);
			if (m.hasMatch())
				used.insert(m.captured(1).toInt());
		}
	}
	return used;
}

// Performs the actual designation assignment. Runs deferred (see below).
static void doAssign(Element *element, bool force)
{
	if (!element)
		return;

	// Slaves and cross-reference reports mirror a master: never number them.
	if (element->linkType() == Element::Slave
		|| (element->linkType() & Element::AllReport))
		return;

	const QString prefix = element->getPrefix();
	if (prefix.isEmpty())
		return; // element kind has no IEC category in qet_labels.xml

	Diagram *diagram = element->diagram();
	if (!diagram || !diagram->project())
		return;

	// Respect a label the user/library already set, unless forced (paste).
	const QString current = element->elementInformations()
			.value(QStringLiteral("label")).toString();
	if (!force && !current.isEmpty())
		return;

	const QSet<int> used = usedNumbers(diagram->project(), prefix, element);
	int number = 1;
	while (used.contains(number))
		++number;

	DiagramContext info = element->elementInformations();
	info.addValue(QStringLiteral("label"), prefix + QString::number(number));
	element->setElementInformations(info);

	element->update();
	if (element->scene())
		element->scene()->update();
}

void assignToElement(Element *element, bool force)
{
	if (!element)
		return;

	// Defer by one event-loop cycle: at placement time the element's dynamic
	// label text item has not yet connected to elementInfoChange, so a
	// synchronous setElementInformations would not repaint until the next user
	// interaction. Running on the next tick guarantees the label shows at once.
	// QPointer guards against the element being deleted before the timer fires.
	QPointer<Element> guard(element);
	QTimer::singleShot(0, [guard, force]() {
		if (guard)
			doAssign(guard.data(), force);
	});
}

} // namespace DesignationManager

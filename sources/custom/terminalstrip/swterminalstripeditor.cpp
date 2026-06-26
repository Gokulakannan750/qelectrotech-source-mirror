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
#include "swterminalstripeditor.h"

#include "../../qetproject.h"
#include "../../elementprovider.h"
#include "../../qetgraphicsitem/element.h"
#include "../../qetgraphicsitem/terminal.h"
#include "../../qetgraphicsitem/conductor.h"
#include "../../conductorproperties.h"
#include "../../diagramcontext.h"
#include "../../undocommand/changeelementinformationcommand.h"
#include "../wirecatalogue/iec60757.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QComboBox>
#include <QTableWidget>
#include <QHeaderView>
#include <QDialogButtonBox>
#include <QPushButton>
#include <algorithm>

namespace {
	enum Col { LeftDest = 0, LeftCable, LeftColour, Mark,
			   RightColour, RightCable, RightDest, ColCount };

	QString labelOf(Element *e)
	{
		return e ? e->elementInformations().value(QStringLiteral("label")).toString()
				 : QString();
	}
}

SwTerminalStripEditor::SwTerminalStripEditor(QETProject *project, QWidget *parent) :
	QDialog(parent),
	m_project(project)
{
	setWindowTitle(tr("Terminal strip editor (SolidWorks view)"));
	resize(900, 520);
	buildUi();
	reload();
}

void SwTerminalStripEditor::buildUi()
{
	auto *header = new QLabel(tr("Terminal strip — symmetric view"), this);
	header->setStyleSheet(QStringLiteral(
		"QLabel { background: qlineargradient(x1:0, y1:0, x2:1, y2:0,"
		" stop:0 #0066cc, stop:1 #00a651); color: white; font-weight: bold;"
		" padding: 6px 10px; border-radius: 4px; }"));

	auto *filter_row = new QHBoxLayout;
	filter_row->addWidget(new QLabel(tr("Terminal strip:"), this));
	m_strip_filter = new QComboBox(this);
	connect(m_strip_filter, QOverload<int>::of(&QComboBox::currentIndexChanged),
			this, &SwTerminalStripEditor::reload);
	filter_row->addWidget(m_strip_filter);
	filter_row->addStretch(1);

	m_table = new QTableWidget(this);
	m_table->setColumnCount(ColCount);
	m_table->setHorizontalHeaderLabels({
		tr("Destination"), tr("Cable"), tr("Colour"), tr("Mark"),
		tr("Colour"), tr("Cable"), tr("Destination") });
	m_table->verticalHeader()->setVisible(false);
	m_table->horizontalHeader()->setStretchLastSection(true);

	auto *buttons = new QDialogButtonBox(
		QDialogButtonBox::Save | QDialogButtonBox::Close, this);
	buttons->button(QDialogButtonBox::Save)->setText(tr("Apply marks"));
	connect(buttons->button(QDialogButtonBox::Save), &QPushButton::clicked,
			this, &SwTerminalStripEditor::applyMarks);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

	auto *layout = new QVBoxLayout(this);
	layout->addWidget(header);
	layout->addLayout(filter_row);
	layout->addWidget(m_table, 1);
	layout->addWidget(buttons);

	// Populate the strip filter once.
	if (m_project) {
		QStringList strips;
		const auto terms = ElementProvider(m_project).find(ElementData::Terminal);
		for (const QPointer<Element> &e : terms) {
			if (!e) continue;
			const QString s = stripNameOf(e);
			if (!strips.contains(s)) strips << s;
		}
		strips.sort();
		m_strip_filter->blockSignals(true);
		m_strip_filter->addItem(tr("All"), QString());
		for (const QString &s : strips)
			m_strip_filter->addItem(s, s);
		m_strip_filter->blockSignals(false);
	}
}

QString SwTerminalStripEditor::stripNameOf(Element *terminal) const
{
	const QString label = labelOf(terminal);
	const int colon = label.indexOf(QLatin1Char(':'));
	if (colon > 0)
		return label.left(colon);          // "X12:1" -> "X12"
	return label.isEmpty() ? tr("(unassigned)") : label;
}

SwTerminalStripEditor::Side
SwTerminalStripEditor::sideInfo(Element *terminal, int index) const
{
	Side s;
	if (!terminal)
		return s;
	const QList<Terminal *> terms = terminal->terminals();
	if (index < 0 || index >= terms.size())
		return s;

	Terminal *t = terms.at(index);
	if (!t)
		return s;
	const QList<Conductor *> conds = t->conductors();
	if (conds.isEmpty())
		return s;

	Conductor *c = conds.first();
	Terminal *other = (c->terminal1 == t) ? c->terminal2 : c->terminal1;
	Element *dest = other ? other->parentElement() : nullptr;
	s.destination = labelOf(dest);

	const ConductorProperties p = c->properties();
	s.cable  = p.m_cable;
	s.colour = p.m_wire_color.isEmpty() ? p.text : p.m_wire_color;
	return s;
}

void SwTerminalStripEditor::reload()
{
	m_table->setRowCount(0);
	m_row_terminal.clear();
	if (!m_project)
		return;

	const QString filter = m_strip_filter ? m_strip_filter->currentData().toString()
										  : QString();

	auto terms = ElementProvider(m_project).find(ElementData::Terminal);
	// Sort by label so a strip reads in terminal order.
	std::sort(terms.begin(), terms.end(),
			  [](const QPointer<Element> &a, const QPointer<Element> &b) {
				  return labelOf(a) < labelOf(b);
			  });

	for (const QPointer<Element> &e : terms) {
		if (!e)
			continue;
		if (!filter.isEmpty() && stripNameOf(e) != filter)
			continue;

		const Side left  = sideInfo(e, 0);
		const Side right = sideInfo(e, 1);

		const int r = m_table->rowCount();
		m_table->insertRow(r);
		m_row_terminal.append(e);

		auto setCell = [&](int col, const QString &text, bool editable,
						   const QString &colourSwatch = QString()) {
			auto *item = new QTableWidgetItem(text);
			if (!editable)
				item->setFlags(item->flags() & ~Qt::ItemIsEditable);
			if (!colourSwatch.isEmpty()) {
				const QColor c = Iec60757::colorForName(colourSwatch);
				if (c.isValid())
					item->setIcon(QIcon(Iec60757::swatch(colourSwatch, 14)));
			}
			if (col == Mark)
				item->setTextAlignment(Qt::AlignCenter);
			m_table->setItem(r, col, item);
		};

		setCell(LeftDest,    left.destination,  false);
		setCell(LeftCable,   left.cable,        false);
		setCell(LeftColour,  left.colour,       false, left.colour);
		setCell(Mark,        labelOf(e),        true);   // editable
		setCell(RightColour, right.colour,      false, right.colour);
		setCell(RightCable,  right.cable,       false);
		setCell(RightDest,   right.destination, false);
	}
	m_table->resizeColumnsToContents();
	m_table->horizontalHeader()->setStretchLastSection(true);
}

void SwTerminalStripEditor::applyMarks()
{
	if (!m_project)
		return;

	QMap<QPointer<Element>, QPair<DiagramContext, DiagramContext>> changes;
	for (int r = 0; r < m_table->rowCount(); ++r) {
		Element *e = (r < m_row_terminal.size()) ? m_row_terminal.at(r).data() : nullptr;
		QTableWidgetItem *item = m_table->item(r, Mark);
		if (!e || !item)
			continue;
		const QString new_mark = item->text();
		if (new_mark == labelOf(e))
			continue;
		const DiagramContext old_info = e->elementInformations();
		DiagramContext new_info = old_info;
		new_info.addValue(QStringLiteral("label"), new_mark);
		changes.insert(QPointer<Element>(e), qMakePair(old_info, new_info));
	}

	if (changes.isEmpty())
		return;

	m_project->undoStack()->push(new ChangeElementInformationCommand(changes));
	reload();
}

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
#include <QMessageBox>
#include <QPushButton>
#include <climits>
#include <algorithm>

namespace {
	// Column order — Bridge is the leftmost narrow column that shows the jumper bar.
	enum Col {
		Bridge = 0,
		LeftDest, LeftCable, LeftColour,
		Mark,
		RightColour, RightCable, RightDest,
		ColCount
	};

	// Palette for distinct bridge-group colours (soft but vivid).
	const QColor kBridgePalette[] = {
		{255, 193,   7},   // amber
		{ 33, 150, 243},   // sky blue
		{ 76, 175,  80},   // green
		{233,  30,  99},   // rose
		{156,  39, 176},   // purple
		{255, 152,   0},   // orange
		{  0, 188, 212},   // cyan
		{233, 255,  50},   // lime
	};
	const int kPaletteSize = static_cast<int>(sizeof(kBridgePalette)
	                                          / sizeof(kBridgePalette[0]));

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
	setWindowTitle(tr("Terminal strip editor"));
	resize(960, 540);
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

	// ---- filter row -------------------------------------------------------
	auto *filter_row = new QHBoxLayout;
	filter_row->addWidget(new QLabel(tr("Terminal strip:"), this));
	m_strip_filter = new QComboBox(this);
	connect(m_strip_filter, QOverload<int>::of(&QComboBox::currentIndexChanged),
			this, &SwTerminalStripEditor::reload);
	filter_row->addWidget(m_strip_filter);
	filter_row->addStretch(1);

	// ---- bridge action buttons --------------------------------------------
	m_add_bridge_btn = new QPushButton(tr("Add bridge"), this);
	m_add_bridge_btn->setToolTip(
		tr("Select two or more adjacent terminal rows, then click to link them with a bridge/jumper."));
	m_add_bridge_btn->setStyleSheet(
		QStringLiteral("QPushButton { background:#e8f5e9; border:1px solid #81c784;"
		               " border-radius:3px; padding:3px 8px; }"
		               "QPushButton:hover { background:#c8e6c9; }"));

	m_remove_bridge_btn = new QPushButton(tr("Remove bridge"), this);
	m_remove_bridge_btn->setToolTip(
		tr("Select bridged terminal rows, then click to remove their bridge/jumper link."));
	m_remove_bridge_btn->setStyleSheet(
		QStringLiteral("QPushButton { background:#fce4ec; border:1px solid #e57373;"
		               " border-radius:3px; padding:3px 8px; }"
		               "QPushButton:hover { background:#f8bbd0; }"));

	connect(m_add_bridge_btn,    &QPushButton::clicked,
			this, &SwTerminalStripEditor::addBridge);
	connect(m_remove_bridge_btn, &QPushButton::clicked,
			this, &SwTerminalStripEditor::removeBridge);

	filter_row->addWidget(m_add_bridge_btn);
	filter_row->addWidget(m_remove_bridge_btn);

	// ---- table ------------------------------------------------------------
	m_table = new QTableWidget(this);
	m_table->setColumnCount(ColCount);
	m_table->setHorizontalHeaderLabels({
		tr("Br"),
		tr("Destination"), tr("Cable"), tr("Colour"),
		tr("Mark"),
		tr("Colour"), tr("Cable"), tr("Destination") });
	m_table->verticalHeader()->setVisible(false);
	m_table->horizontalHeader()->setStretchLastSection(true);
	// Allow multi-row selection for bridge operations.
	m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
	m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
	// Keep the Bridge column narrow.
	m_table->horizontalHeader()->setSectionResizeMode(Bridge, QHeaderView::Fixed);
	m_table->setColumnWidth(Bridge, 18);

	// ---- buttons ----------------------------------------------------------
	auto *buttons = new QDialogButtonBox(
		QDialogButtonBox::Save | QDialogButtonBox::Close, this);
	buttons->button(QDialogButtonBox::Save)->setText(tr("Apply marks"));
	connect(buttons->button(QDialogButtonBox::Save), &QPushButton::clicked,
			this, &SwTerminalStripEditor::applyMarks);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

	// ---- top-level layout -------------------------------------------------
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

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

QString SwTerminalStripEditor::stripNameOf(Element *terminal) const
{
	const QString label = labelOf(terminal);
	const int colon = label.indexOf(QLatin1Char(':'));
	if (colon > 0)
		return label.left(colon);          // "X12:1" -> "X12"
	return label.isEmpty() ? tr("(unassigned)") : label;
}

QUuid SwTerminalStripEditor::bridgeGroupOf(Element *e) const
{
	if (!e) return QUuid();
	const QString s = e->elementInformations()
	                    .value(QStringLiteral("bridge_group")).toString();
	return s.isEmpty() ? QUuid() : QUuid(s);
}

QVector<QPointer<Element>> SwTerminalStripEditor::selectedTerminals() const
{
	// Collect unique terminal elements covering every selected row.
	QVector<QPointer<Element>> result;
	for (const QTableWidgetSelectionRange &range : m_table->selectedRanges()) {
		for (int r = range.topRow(); r <= range.bottomRow(); ++r) {
			if (r < m_row_terminal.size()) {
				const QPointer<Element> &e = m_row_terminal.at(r);
				if (!result.contains(e))
					result.append(e);
			}
		}
	}
	return result;
}

QVector<SwTerminalStripEditor::Side>
SwTerminalStripEditor::sidesInfo(Element *terminal, int index) const
{
	QVector<Side> sides;
	if (!terminal) return sides;
	const QList<Terminal *> terms = terminal->terminals();
	if (index < 0 || index >= terms.size()) return sides;
	Terminal *t = terms.at(index);
	if (!t) return sides;

	const QList<Conductor *> conds = t->conductors();
	for (Conductor *c : conds) {
		if (!c) continue;
		Side s;
		Terminal *other = (c->terminal1 == t) ? c->terminal2 : c->terminal1;
		Element *dest = other ? other->parentElement() : nullptr;
		s.destination = labelOf(dest);
		const ConductorProperties p = c->properties();
		s.cable  = p.m_cable;
		s.colour = p.m_wire_color.isEmpty() ? p.text : p.m_wire_color;
		sides << s;
	}
	return sides;
}

// ---------------------------------------------------------------------------
// reload — rebuild the whole table
// ---------------------------------------------------------------------------

void SwTerminalStripEditor::reload()
{
	m_table->clearSpans();
	m_table->setRowCount(0);
	m_row_terminal.clear();
	m_terminal_rows.clear();

	if (!m_project) return;

	const QString filter = m_strip_filter
	                       ? m_strip_filter->currentData().toString()
	                       : QString();

	auto terms = ElementProvider(m_project).find(ElementData::Terminal);
	std::sort(terms.begin(), terms.end(),
			  [](const QPointer<Element> &a, const QPointer<Element> &b) {
				  return labelOf(a) < labelOf(b);
			  });

	auto setCell = [&](int r, int col, const QString &text, bool editable,
					   const QString &colourSwatch = QString()) {
		auto *item = new QTableWidgetItem(text);
		if (!editable)
			item->setFlags(item->flags() & ~Qt::ItemIsEditable);
		if (!colourSwatch.isEmpty() && Iec60757::colorForName(colourSwatch).isValid())
			item->setIcon(QIcon(Iec60757::swatch(colourSwatch, 14)));
		if (col == Mark)
			item->setTextAlignment(Qt::AlignCenter);
		m_table->setItem(r, col, item);
	};

	// Bridge groups: UUID -> list of visible terminal elements in this reload.
	QMap<QUuid, QVector<QPointer<Element>>> bridge_groups;

	for (const QPointer<Element> &e : terms) {
		if (!e) continue;
		if (!filter.isEmpty() && stripNameOf(e) != filter) continue;

		const QVector<Side> left  = sidesInfo(e, 0);
		const QVector<Side> right = sidesInfo(e, 1);
		const int nsub = qMax(1, qMax(left.size(), right.size()));

		const int first = m_table->rowCount();
		m_terminal_rows[e] = qMakePair(first, nsub);

		// Collect bridge group membership.
		const QUuid bg = bridgeGroupOf(e.data());
		if (!bg.isNull())
			bridge_groups[bg].append(e);

		for (int i = 0; i < nsub; ++i) {
			const int r = first + i;
			m_table->insertRow(r);
			m_row_terminal.append(e);

			// Bridge column — placeholder item; coloured later by paintBridges().
			auto *br_item = new QTableWidgetItem();
			br_item->setFlags(br_item->flags() & ~Qt::ItemIsEditable);
			m_table->setItem(r, Bridge, br_item);

			const Side l  = left.value(i);
			const Side rt = right.value(i);
			setCell(r, LeftDest,    l.destination,  false);
			setCell(r, LeftCable,   l.cable,        false);
			setCell(r, LeftColour,  l.colour,       false, l.colour);
			setCell(r, RightColour, rt.colour,      false, rt.colour);
			setCell(r, RightCable,  rt.cable,       false);
			setCell(r, RightDest,   rt.destination, false);
			if (i == 0)
				setCell(r, Mark, labelOf(e), true);
		}
		if (nsub > 1)
			m_table->setSpan(first, Mark, nsub, 1);
	}

	// Paint bridge indicators after all rows exist.
	paintBridges(bridge_groups);

	m_table->resizeColumnsToContents();
	m_table->setColumnWidth(Bridge, 18);
	m_table->horizontalHeader()->setStretchLastSection(true);
}

// ---------------------------------------------------------------------------
// paintBridges — colour the Bridge column and span bridged rows
// ---------------------------------------------------------------------------

void SwTerminalStripEditor::paintBridges(
		const QMap<QUuid, QVector<QPointer<Element>>> &groups)
{
	int ci = 0;
	for (auto it = groups.cbegin(); it != groups.cend(); ++it) {
		const QColor color = kBridgePalette[ci++ % kPaletteSize];
		int span_start = INT_MAX, span_end = -1;

		for (const QPointer<Element> &e : it.value()) {
			if (!m_terminal_rows.contains(e)) continue;
			const auto &rows = m_terminal_rows.value(e);
			const int fr   = rows.first;
			const int nsub = rows.second;
			span_start = qMin(span_start, fr);
			span_end   = qMax(span_end, fr + nsub - 1);
		}

		if (span_start == INT_MAX) continue;

		// Colour each Bridge cell in the span — they form a solid coloured bar.
		for (int r = span_start; r <= span_end; ++r) {
			if (auto *item = m_table->item(r, Bridge))
				item->setBackground(color);
		}

		// Merge the entire span into one tall cell for a clean bar appearance.
		const int span_len = span_end - span_start + 1;
		if (span_len > 1)
			m_table->setSpan(span_start, Bridge, span_len, 1);
	}
}

// ---------------------------------------------------------------------------
// applyMarks — write Mark column back to terminal elements (undoable)
// ---------------------------------------------------------------------------

void SwTerminalStripEditor::applyMarks()
{
	if (!m_project) return;

	QMap<QPointer<Element>, QPair<DiagramContext, DiagramContext>> changes;
	for (int r = 0; r < m_table->rowCount(); ++r) {
		Element *e = (r < m_row_terminal.size())
		             ? m_row_terminal.at(r).data() : nullptr;
		QTableWidgetItem *item = m_table->item(r, Mark);
		if (!e || !item) continue;
		const QString new_mark = item->text();
		if (new_mark == labelOf(e)) continue;
		const DiagramContext old_info = e->elementInformations();
		DiagramContext new_info = old_info;
		new_info.addValue(QStringLiteral("label"), new_mark);
		changes.insert(QPointer<Element>(e), qMakePair(old_info, new_info));
	}

	if (changes.isEmpty()) return;
	m_project->undoStack()->push(new ChangeElementInformationCommand(changes));
	reload();
}

// ---------------------------------------------------------------------------
// addBridge — link selected terminals into one bridge/jumper group (undoable)
// ---------------------------------------------------------------------------

void SwTerminalStripEditor::addBridge()
{
	if (!m_project) return;

	const QVector<QPointer<Element>> sel = selectedTerminals();
	if (sel.size() < 2) {
		QMessageBox::information(this, tr("Add bridge"),
			tr("Select at least two terminal rows to create a bridge."));
		return;
	}

	// If any selected terminal already belongs to a bridge group, extend that
	// group rather than creating a new UUID.
	QUuid group_uuid;
	for (const QPointer<Element> &e : sel) {
		const QUuid existing = bridgeGroupOf(e.data());
		if (!existing.isNull()) { group_uuid = existing; break; }
	}
	if (group_uuid.isNull())
		group_uuid = QUuid::createUuid();

	QMap<QPointer<Element>, QPair<DiagramContext, DiagramContext>> changes;
	for (const QPointer<Element> &e : sel) {
		if (!e || bridgeGroupOf(e.data()) == group_uuid) continue;
		const DiagramContext old_info = e->elementInformations();
		DiagramContext new_info = old_info;
		new_info.addValue(QStringLiteral("bridge_group"), group_uuid.toString());
		changes.insert(e, qMakePair(old_info, new_info));
	}

	if (!changes.isEmpty()) {
		m_project->undoStack()->push(new ChangeElementInformationCommand(changes));
		reload();
	}
}

// ---------------------------------------------------------------------------
// removeBridge — detach selected terminals from their bridge groups (undoable)
// ---------------------------------------------------------------------------

void SwTerminalStripEditor::removeBridge()
{
	if (!m_project) return;

	const QVector<QPointer<Element>> sel = selectedTerminals();

	QMap<QPointer<Element>, QPair<DiagramContext, DiagramContext>> changes;
	for (const QPointer<Element> &e : sel) {
		if (!e || bridgeGroupOf(e.data()).isNull()) continue;
		const DiagramContext old_info = e->elementInformations();
		DiagramContext new_info = old_info;
		new_info.addValue(QStringLiteral("bridge_group"), QString());
		changes.insert(e, qMakePair(old_info, new_info));
	}

	if (changes.isEmpty()) {
		QMessageBox::information(this, tr("Remove bridge"),
			tr("None of the selected terminals are part of a bridge."));
		return;
	}
	m_project->undoStack()->push(new ChangeElementInformationCommand(changes));
	reload();
}

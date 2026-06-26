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
#include "../../qet.h"
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
	/*
	 * Column layout (symmetric about the Mark column):
	 *
	 *  BridgeL | Dest(1) | Cable(1) | Colour(1) | Mark | Colour(2) | Cable(2) | Dest(2) | BridgeR
	 *
	 * End 1 = the terminal connection point whose orientation is North or East
	 *         (top/left screw in standard mounting = supply/input side).
	 * End 2 = the opposite point (South or West = load/output side).
	 */
	enum Col {
		BridgeL = 0,
		Dest1, Cable1, Colour1,
		Mark,
		Colour2, Cable2, Dest2,
		BridgeR,
		ColCount
	};

	// Palette for bridge-group colour bars.
	const QColor kBridgePalette[] = {
		{255, 193,   7},  // amber
		{ 33, 150, 243},  // sky blue
		{ 76, 175,  80},  // green
		{233,  30,  99},  // rose
		{156,  39, 176},  // purple
		{255, 152,   0},  // orange
		{  0, 188, 212},  // cyan
		{139, 195,  74},  // lime green
	};
	const int kPaletteSize = static_cast<int>(sizeof(kBridgePalette)
	                                          / sizeof(kBridgePalette[0]));

	// Orientation sort key: North=0, East=1, South=2, West=3.
	// End 1 gets the lower key (North/East = top/right in standard mounting).
	int orientationKey(Qet::Orientation o)
	{
		switch (o) {
			case Qet::North: return 0;
			case Qet::East:  return 1;
			case Qet::South: return 2;
			case Qet::West:  return 3;
		}
		return 4;
	}

	QString labelOf(Element *e)
	{
		return e ? e->elementInformations().value(QStringLiteral("label")).toString()
				 : QString();
	}

	// Narrow bridge-column stylesheet helpers.
	constexpr int kBridgeColWidth = 18;

	QPushButton *makeActionBtn(const QString &text, const QString &bg,
	                           const QString &border, QWidget *parent)
	{
		auto *btn = new QPushButton(text, parent);
		btn->setStyleSheet(QStringLiteral(
			"QPushButton { background:%1; border:1px solid %2;"
			" border-radius:3px; padding:3px 8px; }"
			"QPushButton:hover { background:%2; }"
			"QPushButton:disabled { background:#eee; color:#aaa; border-color:#ccc; }")
			.arg(bg, border));
		return btn;
	}
}

SwTerminalStripEditor::SwTerminalStripEditor(QETProject *project, QWidget *parent) :
	QDialog(parent),
	m_project(project)
{
	setWindowTitle(tr("Terminal strip editor"));
	resize(1020, 560);
	buildUi();
	reload();
}

void SwTerminalStripEditor::buildUi()
{
	// ---- gradient header label --------------------------------------------
	auto *header = new QLabel(
		tr("End 1 (top / supply)   ←   Terminal strip — symmetric view   →   End 2 (bottom / load)"),
		this);
	header->setStyleSheet(QStringLiteral(
		"QLabel { background: qlineargradient(x1:0, y1:0, x2:1, y2:0,"
		" stop:0 #0066cc, stop:1 #00a651); color: white; font-weight: bold;"
		" padding: 6px 10px; border-radius: 4px; }"));

	// ---- filter + reorder row ---------------------------------------------
	auto *filter_row = new QHBoxLayout;
	filter_row->addWidget(new QLabel(tr("Terminal strip:"), this));
	m_strip_filter = new QComboBox(this);
	connect(m_strip_filter, QOverload<int>::of(&QComboBox::currentIndexChanged),
			this, &SwTerminalStripEditor::reload);
	filter_row->addWidget(m_strip_filter);

	m_up_btn   = makeActionBtn(tr("▲ Move up"),   "#e3f2fd", "#90caf9", this);
	m_down_btn = makeActionBtn(tr("▼ Move down"), "#e3f2fd", "#90caf9", this);
	m_up_btn->setToolTip(tr("Move the selected terminal one position up within the strip."));
	m_down_btn->setToolTip(tr("Move the selected terminal one position down within the strip."));
	m_up_btn->setEnabled(false);
	m_down_btn->setEnabled(false);
	connect(m_up_btn,   &QPushButton::clicked, this, &SwTerminalStripEditor::moveUp);
	connect(m_down_btn, &QPushButton::clicked, this, &SwTerminalStripEditor::moveDown);

	filter_row->addStretch(1);
	filter_row->addWidget(m_up_btn);
	filter_row->addWidget(m_down_btn);

	// ---- bridge buttons ---------------------------------------------------
	m_add_bridge_btn = makeActionBtn(tr("Add bridge"), "#e8f5e9", "#81c784", this);
	m_add_bridge_btn->setToolTip(
		tr("Select two or more terminal rows, then click to link them with a bridge/jumper."));

	m_remove_bridge_btn = makeActionBtn(tr("Remove bridge"), "#fce4ec", "#e57373", this);
	m_remove_bridge_btn->setToolTip(
		tr("Select bridged terminal rows, then click to remove the bridge/jumper link."));

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
		tr("Dest. (1)"), tr("Cable (1)"), tr("Colour (1)"),
		tr("Mark"),
		tr("Colour (2)"), tr("Cable (2)"), tr("Dest. (2)"),
		tr("Br") });
	m_table->verticalHeader()->setVisible(false);
	m_table->horizontalHeader()->setStretchLastSection(false);
	// Middle columns stretch; outer ones resize to content.
	m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
	m_table->horizontalHeader()->setSectionResizeMode(Dest1,  QHeaderView::Stretch);
	m_table->horizontalHeader()->setSectionResizeMode(Dest2,  QHeaderView::Stretch);
	m_table->horizontalHeader()->setSectionResizeMode(BridgeL, QHeaderView::Fixed);
	m_table->horizontalHeader()->setSectionResizeMode(BridgeR, QHeaderView::Fixed);
	m_table->setColumnWidth(BridgeL, kBridgeColWidth);
	m_table->setColumnWidth(BridgeR, kBridgeColWidth);

	m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
	m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
	connect(m_table, &QTableWidget::itemSelectionChanged,
			this, &SwTerminalStripEditor::updateMoveButtons);

	// ---- bottom buttons ---------------------------------------------------
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

	// Populate the strip-name filter once on open.
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
		return label.left(colon);    // "X12:3" -> "X12"
	return label.isEmpty() ? tr("(unassigned)") : label;
}

QUuid SwTerminalStripEditor::bridgeGroupOf(Element *e) const
{
	if (!e) return QUuid();
	const QString s = e->elementInformations()
	                    .value(QStringLiteral("bridge_group")).toString();
	return s.isEmpty() ? QUuid() : QUuid(s);
}

int SwTerminalStripEditor::stripPosOf(Element *e) const
{
	if (!e) return -1;
	const QVariant v = e->elementInformations().value(QStringLiteral("strip_pos"));
	if (!v.isValid() || v.isNull()) return -1;
	bool ok = false;
	const int pos = v.toInt(&ok);
	return ok ? pos : -1;
}

QVector<QPointer<Element>> SwTerminalStripEditor::selectedTerminals() const
{
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

/**
 * Returns wires on terminal end @p endIndex of @p terminal.
 *
 * The two physical connection points of a terminal element are sorted by their
 * orientation value (North=0, East=1, South=2, West=3) so that:
 *   endIndex 0  →  end 1  (North/East = top or right screw, supply/input side)
 *   endIndex 1  →  end 2  (South/West = bottom or left screw, load/output side)
 *
 * This is independent of the order the <terminal> tags appear in the .elmt XML.
 */
QVector<SwTerminalStripEditor::Side>
SwTerminalStripEditor::sidesInfo(Element *terminal, int endIndex) const
{
	QVector<Side> sides;
	if (!terminal) return sides;

	// Sort the element's connection points by orientation so end 1 is always
	// the North/East point and end 2 is always the South/West point.
	QList<Terminal *> pts = terminal->terminals();
	std::stable_sort(pts.begin(), pts.end(), [](Terminal *a, Terminal *b) {
		return orientationKey(a->orientation()) < orientationKey(b->orientation());
	});

	if (endIndex < 0 || endIndex >= pts.size()) return sides;
	Terminal *t = pts.at(endIndex);
	if (!t) return sides;

	for (Conductor *c : t->conductors()) {
		if (!c) continue;
		Side s;
		Terminal *other = (c->terminal1 == t) ? c->terminal2 : c->terminal1;
		s.destination = labelOf(other ? other->parentElement() : nullptr);
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
	m_display_order.clear();

	if (!m_project) return;

	const QString filter = m_strip_filter
	                       ? m_strip_filter->currentData().toString()
	                       : QString();

	auto terms = ElementProvider(m_project).find(ElementData::Terminal);

	// Sort: manual strip_pos first, then label alphabetically.
	std::sort(terms.begin(), terms.end(),
		[this](const QPointer<Element> &a, const QPointer<Element> &b) {
			const int pa = stripPosOf(a.data());
			const int pb = stripPosOf(b.data());
			if (pa >= 0 && pb >= 0) return pa < pb;
			if (pa >= 0 && pb <  0) return true;   // positioned before unpositioned
			if (pa <  0 && pb >= 0) return false;
			return labelOf(a) < labelOf(b);
		});

	auto makeEmptyBridgeCell = [&](int r, int col) {
		auto *item = new QTableWidgetItem();
		item->setFlags(item->flags() & ~Qt::ItemIsEditable);
		m_table->setItem(r, col, item);
	};

	auto setCell = [&](int r, int col, const QString &text, bool editable,
	                   const QString &swatchName = QString()) {
		auto *item = new QTableWidgetItem(text);
		if (!editable)
			item->setFlags(item->flags() & ~Qt::ItemIsEditable);
		if (!swatchName.isEmpty() && Iec60757::colorForName(swatchName).isValid())
			item->setIcon(QIcon(Iec60757::swatch(swatchName, 14)));
		if (col == Mark)
			item->setTextAlignment(Qt::AlignCenter);
		m_table->setItem(r, col, item);
	};

	QMap<QUuid, QVector<QPointer<Element>>> bridge_groups;

	for (const QPointer<Element> &e : terms) {
		if (!e) continue;
		if (!filter.isEmpty() && stripNameOf(e) != filter) continue;

		m_display_order.append(e);

		const QVector<Side> end1 = sidesInfo(e, 0);
		const QVector<Side> end2 = sidesInfo(e, 1);
		const int nsub = qMax(1, qMax(end1.size(), end2.size()));

		const int first = m_table->rowCount();
		m_terminal_rows[e] = qMakePair(first, nsub);

		const QUuid bg = bridgeGroupOf(e.data());
		if (!bg.isNull())
			bridge_groups[bg].append(e);

		for (int i = 0; i < nsub; ++i) {
			const int r = first + i;
			m_table->insertRow(r);
			m_row_terminal.append(e);

			makeEmptyBridgeCell(r, BridgeL);
			makeEmptyBridgeCell(r, BridgeR);

			const Side s1 = end1.value(i);
			const Side s2 = end2.value(i);
			setCell(r, Dest1,   s1.destination, false);
			setCell(r, Cable1,  s1.cable,        false);
			setCell(r, Colour1, s1.colour,       false, s1.colour);
			setCell(r, Colour2, s2.colour,       false, s2.colour);
			setCell(r, Cable2,  s2.cable,        false);
			setCell(r, Dest2,   s2.destination,  false);

			if (i == 0)
				setCell(r, Mark, labelOf(e), true);
		}
		if (nsub > 1)
			m_table->setSpan(first, Mark, nsub, 1);
	}

	paintBridges(bridge_groups);

	m_table->setColumnWidth(BridgeL, kBridgeColWidth);
	m_table->setColumnWidth(BridgeR, kBridgeColWidth);

	updateMoveButtons();
}

// ---------------------------------------------------------------------------
// paintBridges — colour BridgeL and BridgeR columns and merge bridged spans
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
			span_start = qMin(span_start, rows.first);
			span_end   = qMax(span_end,   rows.first + rows.second - 1);
		}

		if (span_start == INT_MAX) continue;

		for (int r = span_start; r <= span_end; ++r) {
			if (auto *il = m_table->item(r, BridgeL)) il->setBackground(color);
			if (auto *ir = m_table->item(r, BridgeR)) ir->setBackground(color);
		}

		const int span_len = span_end - span_start + 1;
		if (span_len > 1) {
			m_table->setSpan(span_start, BridgeL, span_len, 1);
			m_table->setSpan(span_start, BridgeR, span_len, 1);
		}
	}
}

// ---------------------------------------------------------------------------
// applyMarks — write the Mark column back to terminal elements (undoable)
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
// addBridge / removeBridge
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

	// Reuse an existing group UUID if one is already in the selection.
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

// ---------------------------------------------------------------------------
// moveUp / moveDown — reorder terminals within the strip (persisted, undoable)
// ---------------------------------------------------------------------------

void SwTerminalStripEditor::moveUp()
{
	const QVector<QPointer<Element>> sel = selectedTerminals();
	if (sel.size() != 1) return;

	const QPointer<Element> e = sel.first();
	const int idx = m_display_order.indexOf(e);
	if (idx <= 0) return;                          // already at top
	const QPointer<Element> above = m_display_order.at(idx - 1);
	if (!above) return;

	// Gather current positions of all displayed terminals (initialize if unset).
	QVector<int> positions;
	positions.reserve(m_display_order.size());
	for (const QPointer<Element> &el : m_display_order)
		positions.append(stripPosOf(el.data()));

	// If any position is unset, initialise all to multiples of 10.
	const bool any_unset = std::any_of(positions.cbegin(), positions.cend(),
	                                   [](int v){ return v < 0; });
	if (any_unset)
		for (int i = 0; i < positions.size(); ++i)
			positions[i] = i * 10;

	std::swap(positions[idx], positions[idx - 1]);

	// Build one batched undo command covering all changed positions.
	QMap<QPointer<Element>, QPair<DiagramContext, DiagramContext>> changes;
	for (int i = 0; i < m_display_order.size(); ++i) {
		const QPointer<Element> &el = m_display_order.at(i);
		if (!el) continue;
		const int new_pos = positions[i];
		if (new_pos == stripPosOf(el.data())) continue;
		const DiagramContext old_info = el->elementInformations();
		DiagramContext new_info = old_info;
		new_info.addValue(QStringLiteral("strip_pos"), new_pos);
		changes.insert(el, qMakePair(old_info, new_info));
	}

	if (!changes.isEmpty()) {
		m_project->undoStack()->push(new ChangeElementInformationCommand(changes));
		reload();
		// Re-select the moved terminal so the user can continue moving.
		if (m_terminal_rows.contains(e)) {
			const int new_first = m_terminal_rows.value(e).first;
			m_table->selectRow(new_first);
		}
	}
}

void SwTerminalStripEditor::moveDown()
{
	const QVector<QPointer<Element>> sel = selectedTerminals();
	if (sel.size() != 1) return;

	const QPointer<Element> e = sel.first();
	const int idx = m_display_order.indexOf(e);
	if (idx < 0 || idx >= m_display_order.size() - 1) return;  // already at bottom
	const QPointer<Element> below = m_display_order.at(idx + 1);
	if (!below) return;

	QVector<int> positions;
	positions.reserve(m_display_order.size());
	for (const QPointer<Element> &el : m_display_order)
		positions.append(stripPosOf(el.data()));

	const bool any_unset = std::any_of(positions.cbegin(), positions.cend(),
	                                   [](int v){ return v < 0; });
	if (any_unset)
		for (int i = 0; i < positions.size(); ++i)
			positions[i] = i * 10;

	std::swap(positions[idx], positions[idx + 1]);

	QMap<QPointer<Element>, QPair<DiagramContext, DiagramContext>> changes;
	for (int i = 0; i < m_display_order.size(); ++i) {
		const QPointer<Element> &el = m_display_order.at(i);
		if (!el) continue;
		const int new_pos = positions[i];
		if (new_pos == stripPosOf(el.data())) continue;
		const DiagramContext old_info = el->elementInformations();
		DiagramContext new_info = old_info;
		new_info.addValue(QStringLiteral("strip_pos"), new_pos);
		changes.insert(el, qMakePair(old_info, new_info));
	}

	if (!changes.isEmpty()) {
		m_project->undoStack()->push(new ChangeElementInformationCommand(changes));
		reload();
		if (m_terminal_rows.contains(e)) {
			const int new_first = m_terminal_rows.value(e).first;
			m_table->selectRow(new_first);
		}
	}
}

void SwTerminalStripEditor::updateMoveButtons()
{
	const QVector<QPointer<Element>> sel = selectedTerminals();
	const bool single = (sel.size() == 1);
	if (!single) {
		m_up_btn->setEnabled(false);
		m_down_btn->setEnabled(false);
		return;
	}
	const int idx = m_display_order.indexOf(sel.first());
	m_up_btn->setEnabled(idx > 0);
	m_down_btn->setEnabled(idx >= 0 && idx < m_display_order.size() - 1);
}

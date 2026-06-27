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
#include "../../diagram.h"
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
#include <QFileDialog>
#include <QMessageBox>
#include <QPainter>
#include <QPrinter>
#include <QPushButton>
#include <QTextStream>
#include <climits>
#include <numeric>
#include <algorithm>

namespace {

/*
 * Full symmetric column layout:
 *
 *  BridgeL | Wire(1) | Folio(1) | Dest.(1) | Cable(1) | Colour(1)
 *          | Mark |
 *          Colour(2) | Cable(2) | Dest.(2) | Folio(2) | Wire(2) | BridgeR
 */
enum Col {
	BridgeL = 0,
	Wire1, Folio1, Dest1, Cable1, Colour1,
	Mark,
	Colour2, Cable2, Dest2, Folio2, Wire2,
	BridgeR,
	ColCount  // 13
};

const QColor kBridgePalette[] = {
	{255, 193,   7},  // amber
	{ 33, 150, 243},  // sky blue
	{ 76, 175,  80},  // green
	{233,  30,  99},  // rose
	{156,  39, 176},  // purple
	{255, 152,   0},  // orange
	{  0, 188, 212},  // cyan
	{139, 195,  74},  // lime
};
const int kPaletteSize = static_cast<int>(sizeof(kBridgePalette)
                                          / sizeof(kBridgePalette[0]));

constexpr int kBridgeColWidth = 18;

// Orientation sort key so North/East comes first (= end 1 = top/supply screw).
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

QPushButton *makeBtn(const QString &text, const QString &bg,
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

// CSV-escape: quote fields containing commas or quotes.
QString csvField(const QString &s)
{
	if (s.contains(QLatin1Char(',')) || s.contains(QLatin1Char('"'))
	        || s.contains(QLatin1Char('\n')))
		return QLatin1Char('"') + QString(s).replace(QStringLiteral("\""),
		                                              QStringLiteral("\"\""))
		       + QLatin1Char('"');
	return s;
}

} // namespace

// ---------------------------------------------------------------------------
SwTerminalStripEditor::SwTerminalStripEditor(QETProject *project, QWidget *parent)
	: QDialog(parent), m_project(project)
{
	setWindowTitle(tr("Terminal strip editor"));
	resize(1100, 580);
	buildUi();
	reload();
}

void SwTerminalStripEditor::buildUi()
{
	// ---- gradient header --------------------------------------------------
	auto *header = new QLabel(
		tr("End 1 (top · supply)   ←   Terminal strip — symmetric view   →   End 2 (bottom · load)"),
		this);
	header->setStyleSheet(QStringLiteral(
		"QLabel { background: qlineargradient(x1:0,y1:0,x2:1,y2:0,"
		"stop:0 #0066cc, stop:1 #00a651); color:white; font-weight:bold;"
		" padding:6px 10px; border-radius:4px; }"));

	// ---- toolbar row 1: strip filter + reorder ----------------------------
	auto *row1 = new QHBoxLayout;
	row1->addWidget(new QLabel(tr("Terminal strip:"), this));
	m_strip_filter = new QComboBox(this);
	connect(m_strip_filter, QOverload<int>::of(&QComboBox::currentIndexChanged),
	        this, &SwTerminalStripEditor::reload);
	row1->addWidget(m_strip_filter);

	m_up_btn   = makeBtn(tr("▲ Up"),   "#e3f2fd", "#90caf9", this);
	m_down_btn = makeBtn(tr("▼ Down"), "#e3f2fd", "#90caf9", this);
	m_up_btn->setToolTip(tr("Move the selected terminal one position up within the strip."));
	m_down_btn->setToolTip(tr("Move the selected terminal one position down within the strip."));
	m_up_btn->setEnabled(false);
	m_down_btn->setEnabled(false);
	connect(m_up_btn,   &QPushButton::clicked, this, &SwTerminalStripEditor::moveUp);
	connect(m_down_btn, &QPushButton::clicked, this, &SwTerminalStripEditor::moveDown);
	row1->addStretch(1);
	row1->addWidget(m_up_btn);
	row1->addWidget(m_down_btn);

	// ---- toolbar row 2: bridge + output -----------------------------------
	auto *row2 = new QHBoxLayout;

	m_add_bridge_btn    = makeBtn(tr("Add bridge"),    "#e8f5e9", "#81c784", this);
	m_remove_bridge_btn = makeBtn(tr("Remove bridge"), "#fce4ec", "#e57373", this);
	m_add_bridge_btn->setToolTip(
		tr("Select ≥2 terminal rows and click to link them with a bridge/jumper."));
	m_remove_bridge_btn->setToolTip(
		tr("Select bridged terminal rows and click to remove the bridge."));
	connect(m_add_bridge_btn,    &QPushButton::clicked, this, &SwTerminalStripEditor::addBridge);
	connect(m_remove_bridge_btn, &QPushButton::clicked, this, &SwTerminalStripEditor::removeBridge);

	m_export_csv_btn    = makeBtn(tr("Export CSV…"),      "#fff8e1", "#ffd54f", this);
	m_generate_draw_btn = makeBtn(tr("Generate drawing…"), "#ede7f6", "#9575cd", this);
	m_export_csv_btn->setToolTip(tr("Save the terminal strip data as a CSV file."));
	m_generate_draw_btn->setToolTip(tr("Render the terminal strip table to a PDF drawing."));
	connect(m_export_csv_btn,    &QPushButton::clicked, this, &SwTerminalStripEditor::exportCsv);
	connect(m_generate_draw_btn, &QPushButton::clicked, this, &SwTerminalStripEditor::generateDrawing);

	row2->addWidget(m_add_bridge_btn);
	row2->addWidget(m_remove_bridge_btn);
	row2->addStretch(1);
	row2->addWidget(m_export_csv_btn);
	row2->addWidget(m_generate_draw_btn);

	// ---- table ------------------------------------------------------------
	m_table = new QTableWidget(this);
	m_table->setColumnCount(ColCount);
	m_table->setHorizontalHeaderLabels({
		tr("Br"),
		tr("Wire (1)"), tr("Folio (1)"), tr("Dest. (1)"), tr("Cable (1)"), tr("Colour (1)"),
		tr("Mark"),
		tr("Colour (2)"), tr("Cable (2)"), tr("Dest. (2)"), tr("Folio (2)"), tr("Wire (2)"),
		tr("Br") });
	m_table->verticalHeader()->setVisible(false);
	m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
	// Destination columns stretch; all others fit content.
	m_table->horizontalHeader()->setSectionResizeMode(Dest1, QHeaderView::Stretch);
	m_table->horizontalHeader()->setSectionResizeMode(Dest2, QHeaderView::Stretch);
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

	// ---- layout -----------------------------------------------------------
	auto *layout = new QVBoxLayout(this);
	layout->addWidget(header);
	layout->addLayout(row1);
	layout->addLayout(row2);
	layout->addWidget(m_table, 1);
	layout->addWidget(buttons);

	// Populate strip filter.
	if (m_project) {
		QStringList strips;
		for (const QPointer<Element> &e :
		         ElementProvider(m_project).find(ElementData::Terminal)) {
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
	const QString lbl = labelOf(terminal);
	const int c = lbl.indexOf(QLatin1Char(':'));
	if (c > 0) return lbl.left(c);
	return lbl.isEmpty() ? tr("(unassigned)") : lbl;
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
				if (!result.contains(e)) result.append(e);
			}
		}
	}
	return result;
}

/**
 * Returns the wires on terminal end @p endIndex of @p terminal.
 *
 * The two physical connection points are sorted by Qet::Orientation:
 *   North (0) / East (1)  →  endIndex 0  =  end 1 (top / supply screw)
 *   South (2) / West (3)  →  endIndex 1  =  end 2 (bottom / load screw)
 *
 * Each Side carries:
 *   wire_number  ConductorProperties::text  (= wire tag / potential label)
 *   folio        finalfolio() of the far-end element's diagram
 *   destination  label of the far-end element
 *   cable        m_cable from ConductorProperties
 *   colour       m_wire_color (fallback: ConductorProperties::text colour string)
 */
QVector<SwTerminalStripEditor::Side>
SwTerminalStripEditor::sidesInfo(Element *terminal, int endIndex) const
{
	QVector<Side> sides;
	if (!terminal) return sides;

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
		const ConductorProperties p = c->properties();

		// Wire number / potential label = the text displayed on the conductor.
		s.wire_number = p.text;

		// Cable and colour from our wire-catalogue assignment.
		s.cable  = p.m_cable;
		s.colour = p.m_wire_color.isEmpty() ? p.text : p.m_wire_color;

		// Far-end destination.
		Terminal *other = (c->terminal1 == t) ? c->terminal2 : c->terminal1;
		Element  *dest  = other ? other->parentElement() : nullptr;
		s.destination = labelOf(dest);

		// Cross-reference: folio of the far-end diagram.
		if (dest && dest->diagram()) {
			Diagram *d = dest->diagram();
			QString f  = d->border_and_titleblock.finalfolio();
			if (f.isEmpty())
				f = QString::number(d->folioIndex() + 1);
			s.folio = f;
		}

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

	// Primary sort: manual strip_pos; secondary: label alphabetical.
	std::sort(terms.begin(), terms.end(),
	    [this](const QPointer<Element> &a, const QPointer<Element> &b) {
	        const int pa = stripPosOf(a.data()), pb = stripPosOf(b.data());
	        if (pa >= 0 && pb >= 0) return pa < pb;
	        if (pa >= 0) return true;
	        if (pb >= 0) return false;
	        return labelOf(a) < labelOf(b);
	    });

	auto mkBridgeCell = [&](int r, int col) {
		auto *it = new QTableWidgetItem();
		it->setFlags(it->flags() & ~Qt::ItemIsEditable);
		m_table->setItem(r, col, it);
	};

	auto setCell = [&](int r, int col, const QString &text, bool editable,
	                   const QString &swatch = QString()) {
		auto *it = new QTableWidgetItem(text);
		if (!editable) it->setFlags(it->flags() & ~Qt::ItemIsEditable);
		if (!swatch.isEmpty() && Iec60757::colorForName(swatch).isValid())
			it->setIcon(QIcon(Iec60757::swatch(swatch, 14)));
		if (col == Mark) it->setTextAlignment(Qt::AlignCenter);
		m_table->setItem(r, col, it);
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
		if (!bg.isNull()) bridge_groups[bg].append(e);

		for (int i = 0; i < nsub; ++i) {
			const int r = first + i;
			m_table->insertRow(r);
			m_row_terminal.append(e);

			mkBridgeCell(r, BridgeL);
			mkBridgeCell(r, BridgeR);

			const Side s1 = end1.value(i);
			const Side s2 = end2.value(i);

			setCell(r, Wire1,   s1.wire_number, false);
			setCell(r, Folio1,  s1.folio,       false);
			setCell(r, Dest1,   s1.destination, false);
			setCell(r, Cable1,  s1.cable,        false);
			setCell(r, Colour1, s1.colour,       false, s1.colour);

			setCell(r, Colour2, s2.colour,       false, s2.colour);
			setCell(r, Cable2,  s2.cable,        false);
			setCell(r, Dest2,   s2.destination,  false);
			setCell(r, Folio2,  s2.folio,        false);
			setCell(r, Wire2,   s2.wire_number,  false);

			if (i == 0) setCell(r, Mark, labelOf(e), true);
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
// paintBridges
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
		const int len = span_end - span_start + 1;
		if (len > 1) {
			m_table->setSpan(span_start, BridgeL, len, 1);
			m_table->setSpan(span_start, BridgeR, len, 1);
		}
	}
}

// ---------------------------------------------------------------------------
// applyMarks
// ---------------------------------------------------------------------------

void SwTerminalStripEditor::applyMarks()
{
	if (!m_project) return;

	QMap<QPointer<Element>, QPair<DiagramContext, DiagramContext>> changes;
	for (int r = 0; r < m_table->rowCount(); ++r) {
		Element *e = (r < m_row_terminal.size()) ? m_row_terminal.at(r).data() : nullptr;
		auto *item = m_table->item(r, Mark);
		if (!e || !item) continue;
		const QString new_mark = item->text();
		if (new_mark == labelOf(e)) continue;
		const DiagramContext old_info = e->elementInformations();
		DiagramContext new_info = old_info;
		new_info.addValue(QStringLiteral("label"), new_mark);
		changes.insert(QPointer<Element>(e), qMakePair(old_info, new_info));
	}
	if (!changes.isEmpty()) {
		m_project->undoStack()->push(new ChangeElementInformationCommand(changes));
		reload();
	}
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

	QUuid group_uuid;
	for (const QPointer<Element> &e : sel) {
		const QUuid ex = bridgeGroupOf(e.data());
		if (!ex.isNull()) { group_uuid = ex; break; }
	}
	if (group_uuid.isNull()) group_uuid = QUuid::createUuid();

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
// moveUp / moveDown
// ---------------------------------------------------------------------------

void SwTerminalStripEditor::moveUp()
{
	const QVector<QPointer<Element>> sel = selectedTerminals();
	if (sel.size() != 1) return;
	const QPointer<Element> e = sel.first();
	const int idx = m_display_order.indexOf(e);
	if (idx <= 0) return;

	QVector<int> positions;
	positions.reserve(m_display_order.size());
	for (const QPointer<Element> &el : m_display_order)
		positions.append(stripPosOf(el.data()));

	if (std::any_of(positions.cbegin(), positions.cend(), [](int v){ return v < 0; }))
		for (int i = 0; i < positions.size(); ++i) positions[i] = i * 10;

	std::swap(positions[idx], positions[idx - 1]);

	QMap<QPointer<Element>, QPair<DiagramContext, DiagramContext>> changes;
	for (int i = 0; i < m_display_order.size(); ++i) {
		const QPointer<Element> &el = m_display_order.at(i);
		if (!el || positions[i] == stripPosOf(el.data())) continue;
		const DiagramContext old_info = el->elementInformations();
		DiagramContext new_info = old_info;
		new_info.addValue(QStringLiteral("strip_pos"), positions[i]);
		changes.insert(el, qMakePair(old_info, new_info));
	}
	if (!changes.isEmpty()) {
		m_project->undoStack()->push(new ChangeElementInformationCommand(changes));
		reload();
		if (m_terminal_rows.contains(e))
			m_table->selectRow(m_terminal_rows.value(e).first);
	}
}

void SwTerminalStripEditor::moveDown()
{
	const QVector<QPointer<Element>> sel = selectedTerminals();
	if (sel.size() != 1) return;
	const QPointer<Element> e = sel.first();
	const int idx = m_display_order.indexOf(e);
	if (idx < 0 || idx >= m_display_order.size() - 1) return;

	QVector<int> positions;
	positions.reserve(m_display_order.size());
	for (const QPointer<Element> &el : m_display_order)
		positions.append(stripPosOf(el.data()));

	if (std::any_of(positions.cbegin(), positions.cend(), [](int v){ return v < 0; }))
		for (int i = 0; i < positions.size(); ++i) positions[i] = i * 10;

	std::swap(positions[idx], positions[idx + 1]);

	QMap<QPointer<Element>, QPair<DiagramContext, DiagramContext>> changes;
	for (int i = 0; i < m_display_order.size(); ++i) {
		const QPointer<Element> &el = m_display_order.at(i);
		if (!el || positions[i] == stripPosOf(el.data())) continue;
		const DiagramContext old_info = el->elementInformations();
		DiagramContext new_info = old_info;
		new_info.addValue(QStringLiteral("strip_pos"), positions[i]);
		changes.insert(el, qMakePair(old_info, new_info));
	}
	if (!changes.isEmpty()) {
		m_project->undoStack()->push(new ChangeElementInformationCommand(changes));
		reload();
		if (m_terminal_rows.contains(e))
			m_table->selectRow(m_terminal_rows.value(e).first);
	}
}

void SwTerminalStripEditor::updateMoveButtons()
{
	const QVector<QPointer<Element>> sel = selectedTerminals();
	const bool single = (sel.size() == 1);
	if (!single) { m_up_btn->setEnabled(false); m_down_btn->setEnabled(false); return; }
	const int idx = m_display_order.indexOf(sel.first());
	m_up_btn->setEnabled(idx > 0);
	m_down_btn->setEnabled(idx >= 0 && idx < m_display_order.size() - 1);
}

// ---------------------------------------------------------------------------
// exportCsv — save terminal strip data as a flat CSV file
// ---------------------------------------------------------------------------

void SwTerminalStripEditor::exportCsv()
{
	const QString filename = QFileDialog::getSaveFileName(
		this, tr("Export terminal strip CSV"),
		QString(), tr("CSV files (*.csv);;All files (*)"));
	if (filename.isEmpty()) return;

	QFile f(filename);
	if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
		QMessageBox::warning(this, tr("Export CSV"),
			tr("Cannot write to:\n%1").arg(filename));
		return;
	}

	QTextStream out(&f);
	out.setCodec("UTF-8");

	// Header row.
	out << "Mark,"
	    << "Wire (1),Folio (1),Dest. (1),Cable (1),Colour (1),"
	    << "Wire (2),Folio (2),Dest. (2),Cable (2),Colour (2),"
	    << "Bridge group\n";

	auto cell = [&](int r, int col) -> QString {
		const auto *it = m_table->item(r, col);
		return it ? csvField(it->text()) : QString();
	};

	// One row per sub-row (multi-wire terminals get one row per wire).
	for (int r = 0; r < m_table->rowCount(); ++r) {
		Element *e = (r < m_row_terminal.size()) ? m_row_terminal.at(r).data() : nullptr;
		if (!e) continue;

		// Mark: the spanned item is always on the first sub-row of each terminal.
		const int first_row = m_terminal_rows.contains(QPointer<Element>(e))
		                      ? m_terminal_rows.value(QPointer<Element>(e)).first : r;
		const auto *mark_it = m_table->item(first_row, Mark);
		const QString mark  = mark_it ? csvField(mark_it->text()) : csvField(labelOf(e));

		const QUuid bg = bridgeGroupOf(e);
		const QString bridge = bg.isNull() ? QString() : csvField(bg.toString());

		out << mark << ","
		    << cell(r, Wire1)   << "," << cell(r, Folio1) << ","
		    << cell(r, Dest1)   << "," << cell(r, Cable1) << ","
		    << cell(r, Colour1) << ","
		    << cell(r, Wire2)   << "," << cell(r, Folio2) << ","
		    << cell(r, Dest2)   << "," << cell(r, Cable2) << ","
		    << cell(r, Colour2) << ","
		    << bridge << "\n";
	}

	QMessageBox::information(this, tr("Export CSV"),
		tr("Terminal strip data saved to:\n%1").arg(filename));
}

// ---------------------------------------------------------------------------
// generateDrawing — render terminal strip table to a PDF
// ---------------------------------------------------------------------------

void SwTerminalStripEditor::generateDrawing()
{
	const QString filename = QFileDialog::getSaveFileName(
		this, tr("Generate terminal strip drawing"),
		QString(), tr("PDF files (*.pdf);;All files (*)"));
	if (filename.isEmpty()) return;

	QPrinter printer(QPrinter::HighResolution);
	printer.setOutputFormat(QPrinter::PdfFormat);
	printer.setOutputFileName(filename);
	printer.setOrientation(QPrinter::Landscape);
	printer.setPaperSize(QPrinter::A3);

	QPainter painter;
	if (!painter.begin(&printer)) {
		QMessageBox::warning(this, tr("Generate drawing"),
			tr("Could not create PDF:\n%1").arg(filename));
		return;
	}

	// Work in device pixels.
	const QRectF page(0, 0, painter.device()->width(), painter.device()->height());
	const qreal dpi    = printer.resolution();
	const qreal mm     = dpi / 25.4;
	const qreal margin = 10 * mm;
	const qreal title_h = 8  * mm;
	const qreal hdr_h   = 8  * mm;
	const qreal row_h   = 6  * mm;
	const qreal tbl_x   = page.left() + margin;
	const qreal tbl_w   = page.width() - 2 * margin;
	const qreal tbl_y   = page.top() + margin + title_h + 4 * mm;

	// Columns to render and their proportional widths.
	const struct DrawCol { int col; QString header; int weight; } draw_cols[] = {
		{ Mark,    tr("Mark"),       2 },
		{ Wire1,   tr("Wire (1)"),   2 },
		{ Folio1,  tr("Folio (1)"),  1 },
		{ Dest1,   tr("Dest. (1)"),  4 },
		{ Cable1,  tr("Cable (1)"),  3 },
		{ Colour1, tr("Colour (1)"), 2 },
		{ Colour2, tr("Colour (2)"), 2 },
		{ Cable2,  tr("Cable (2)"),  3 },
		{ Dest2,   tr("Dest. (2)"),  4 },
		{ Folio2,  tr("Folio (2)"),  1 },
		{ Wire2,   tr("Wire (2)"),   2 },
	};
	const int ncols = static_cast<int>(sizeof(draw_cols) / sizeof(draw_cols[0]));
	const int total_w = std::accumulate(std::begin(draw_cols), std::end(draw_cols),
	                                    0, [](int s, const DrawCol &c){ return s + c.weight; });

	// Precompute column X positions.
	QVector<qreal> col_x(ncols + 1);
	col_x[0] = tbl_x;
	for (int c = 0; c < ncols; ++c)
		col_x[c + 1] = col_x[c] + tbl_w * draw_cols[c].weight / total_w;

	// ---- Title -------------------------------------------------------
	const QString strip_name = m_strip_filter
	                           ? m_strip_filter->currentText() : QString();
	painter.setFont(QFont(QStringLiteral("Arial"), static_cast<int>(4 * mm), QFont::Bold));
	painter.setPen(Qt::black);
	painter.drawText(QRectF(tbl_x, page.top() + margin, tbl_w, title_h),
	                 Qt::AlignCenter,
	                 tr("Terminal strip — %1").arg(strip_name));

	// ---- Header row --------------------------------------------------
	painter.setBrush(QColor(0x0066cc));
	painter.setPen(Qt::NoPen);
	painter.drawRect(QRectF(tbl_x, tbl_y, tbl_w, hdr_h));

	painter.setFont(QFont(QStringLiteral("Arial"), static_cast<int>(2.5 * mm), QFont::Bold));
	painter.setPen(Qt::white);
	for (int c = 0; c < ncols; ++c) {
		painter.drawText(
			QRectF(col_x[c] + mm * 0.5, tbl_y, col_x[c+1] - col_x[c] - mm, hdr_h),
			Qt::AlignLeft | Qt::AlignVCenter,
			draw_cols[c].header);
	}

	// Divider between end-1 and end-2 column groups in header.
	painter.setPen(QPen(QColor(Qt::white), mm * 0.3));
	const qreal mid_x = col_x[6];  // between Colour(1) and Colour(2)
	painter.drawLine(QLineF(mid_x, tbl_y + mm * 0.5, mid_x, tbl_y + hdr_h - mm * 0.5));

	// ---- Data rows ---------------------------------------------------
	painter.setFont(QFont(QStringLiteral("Arial"), static_cast<int>(2.2 * mm)));
	const int nrows = m_table->rowCount();
	qreal cur_y = tbl_y + hdr_h;

	// Track bridge colours by UUID for the side bar.
	QMap<QUuid, QColor> bg_colors;
	int bg_ci = 0;

	for (int r = 0; r < nrows; ++r) {
		// Check for page overflow (leave room for bottom margin).
		if (cur_y + row_h > page.bottom() - margin) {
			printer.newPage();
			cur_y = page.top() + margin;
			// Redraw header on new page.
			painter.setBrush(QColor(0x0066cc));
			painter.setPen(Qt::NoPen);
			painter.drawRect(QRectF(tbl_x, cur_y, tbl_w, hdr_h));
			painter.setFont(QFont(QStringLiteral("Arial"),
			                      static_cast<int>(2.5 * mm), QFont::Bold));
			painter.setPen(Qt::white);
			for (int c = 0; c < ncols; ++c)
				painter.drawText(
					QRectF(col_x[c] + mm * 0.5, cur_y,
					       col_x[c+1] - col_x[c] - mm, hdr_h),
					Qt::AlignLeft | Qt::AlignVCenter, draw_cols[c].header);
			painter.setFont(QFont(QStringLiteral("Arial"),
			                      static_cast<int>(2.2 * mm)));
			cur_y += hdr_h;
		}

		// Alternating row background.
		painter.setPen(Qt::NoPen);
		painter.setBrush(r % 2 == 0 ? QColor(245, 247, 250) : Qt::white);
		painter.drawRect(QRectF(tbl_x, cur_y, tbl_w, row_h));

		// Bridge colour bar on left and right edges.
		Element *e = (r < m_row_terminal.size()) ? m_row_terminal.at(r).data() : nullptr;
		if (e) {
			const QUuid bg = bridgeGroupOf(e);
			if (!bg.isNull()) {
				if (!bg_colors.contains(bg))
					bg_colors[bg] = kBridgePalette[bg_ci++ % kPaletteSize];
				const QColor bc = bg_colors.value(bg);
				painter.setBrush(bc);
				painter.drawRect(QRectF(tbl_x, cur_y, mm * 1.5, row_h));
				painter.drawRect(QRectF(tbl_x + tbl_w - mm * 1.5, cur_y, mm * 1.5, row_h));
			}
		}

		// Cell text.
		painter.setPen(Qt::black);
		for (int c = 0; c < ncols; ++c) {
			const int tcol = draw_cols[c].col;
			// For Mark column, only the first sub-row has the item (due to span).
			// Read from first_row of this terminal.
			int src_row = r;
			if (tcol == Mark && e) {
				const QPointer<Element> ep(e);
				if (m_terminal_rows.contains(ep))
					src_row = m_terminal_rows.value(ep).first;
			}
			const auto *it = m_table->item(src_row, tcol);
			if (!it || it->text().isEmpty()) continue;

			const QRectF cell(col_x[c] + mm * 0.5, cur_y + mm * 0.3,
			                  col_x[c+1] - col_x[c] - mm, row_h - mm * 0.6);
			painter.drawText(cell, Qt::AlignLeft | Qt::AlignVCenter, it->text());
		}

		// Horizontal grid line.
		painter.setPen(QPen(QColor(200, 200, 200), mm * 0.1));
		painter.drawLine(QLineF(tbl_x, cur_y + row_h, tbl_x + tbl_w, cur_y + row_h));

		cur_y += row_h;
	}

	// ---- Outer border + vertical column lines ------------------------
	painter.setPen(QPen(QColor(80, 80, 80), mm * 0.2));
	painter.setBrush(Qt::NoBrush);
	painter.drawRect(QRectF(tbl_x, tbl_y, tbl_w, hdr_h + nrows * row_h));
	for (int c = 1; c < ncols; ++c) {
		painter.drawLine(QLineF(col_x[c], tbl_y,
		                        col_x[c], tbl_y + hdr_h + nrows * row_h));
	}

	painter.end();

	QMessageBox::information(this, tr("Generate drawing"),
		tr("Terminal strip drawing saved to:\n%1").arg(filename));
}

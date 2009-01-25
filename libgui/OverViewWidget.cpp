/***************************************************************************
     OverViewWidget.cpp  -  horizontal slider with overview over a signal
                             -------------------
    begin                : Tue Oct 21 2000
    copyright            : (C) 2000 by Thomas Eschenbacher
    email                : Thomas.Eschenbacher@gmx.de
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "config.h"

#include <QPainter>
#include <QResizeEvent>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QVBoxLayout>

#include "libkwave/Label.h"
#include "libkwave/SignalManager.h"

#include "OverViewWidget.h"

/**
 * interval for limiting the number of repaints per second [ms]
 * (in normal mode, no playback running)
 */
#define REPAINT_INTERVAL 250

/**
 * interval for limiting the number of repaints per second [ms]
 * (when playback is running)
 */
#define REPAINT_INTERVAL_FAST 50

#define BAR_BACKGROUND    palette().mid().color()
#define BAR_FOREGROUND    palette().light().color()

//***************************************************************************
//***************************************************************************
OverViewWidget::WorkerThread::WorkerThread(OverViewWidget *overview)
    :QThread(), m_overview(overview)
{
}

//***************************************************************************
OverViewWidget::WorkerThread::~WorkerThread()
{
    Q_ASSERT(!isRunning());
}

//***************************************************************************
void OverViewWidget::WorkerThread::run()
{
    if (m_overview) m_overview->calculateBitmap();
}

//***************************************************************************
//***************************************************************************
OverViewWidget::OverViewWidget(SignalManager &signal, QWidget *parent)
    :ImageView(parent), m_view_offset(0), m_view_width(0), m_signal_length(0),
     m_sample_rate(0), m_selection_start(0), m_selection_length(0),
     m_playback_position(0), m_last_offset(0), m_cache(signal),
     m_repaint_timer(), m_labels(), m_worker_thread(this)
{
    // update the bitmap if the cache has changed
    connect(&m_cache, SIGNAL(changed()),
            this, SLOT(overviewChanged()));

    // connect repaint timer
    connect(&m_repaint_timer, SIGNAL(timeout()),
            this, SLOT(refreshBitmap()));

    // get informed about label changes
    connect(&signal, SIGNAL(labelsChanged(const LabelList &)),
            this, SLOT(labelsChanged(const LabelList &)));

    // transport the image calculated in a background thread
    // through the signal/slot mechanism
    connect(this, SIGNAL(newImage(QImage)),
            this, SLOT(setImage(QImage)),
            Qt::QueuedConnection);

    setMouseTracking(true);
}

//***************************************************************************
OverViewWidget::~OverViewWidget()
{
    m_repaint_timer.stop();
    m_worker_thread.wait(/* 100 * REPAINT_INTERVAL */);
}

//***************************************************************************
void OverViewWidget::mouseMoveEvent(QMouseEvent *e)
{
    mousePressEvent(e);
}

//***************************************************************************
void OverViewWidget::mousePressEvent(QMouseEvent *e)
{
    Q_ASSERT(e);
    if (!e) return;
    if (e->buttons() != Qt::LeftButton) {
	e->ignore();
	return;
    }

    // move the clicked position to the center of the viewport
    unsigned int offset = pixels2offset(e->x());
    if (offset != m_last_offset) {
	m_last_offset = offset;
	emit valueChanged(offset);
    }
    e->accept();
}

//***************************************************************************
void OverViewWidget::mouseDoubleClickEvent(QMouseEvent *e)
{
    Q_ASSERT(e);
    if (!e) return;
    if (e->button() != Qt::LeftButton) {
	e->ignore();
	return;
    }

    // move the clicked position to the center of the viewport
    unsigned int offset = pixels2offset(e->x());
    if (offset != m_last_offset) {
	m_last_offset = offset;
	emit valueChanged(offset);
    }

    if (e->modifiers() == Qt::NoModifier) {
	// double click without shift => zoom in
	emit sigCommand("zoomin()");
    } else if (e->modifiers() == Qt::ShiftModifier) {
	// double click with shift => zoom out
	emit sigCommand("zoomout()");
    }

    e->accept();
}

//***************************************************************************
int OverViewWidget::pixels2offset(int pixels)
{
    int width = this->width();
    if (!width) return 0;

    int offset = static_cast<int>(m_signal_length *
	(static_cast<qreal>(pixels) / static_cast<qreal>(width)));
    int center = m_view_width >> 1;
    offset = (offset > center) ? (offset - center) : 0;
    return offset;
}

//***************************************************************************
void OverViewWidget::setRange(unsigned int offset, unsigned int viewport,
                              unsigned int total)
{
    m_view_offset   = offset;
    m_view_width    = viewport;
    m_signal_length = total;

    overviewChanged();
}

//***************************************************************************
void OverViewWidget::setSelection(unsigned int offset, unsigned int length,
                                  double rate)
{
    m_selection_start  = offset;
    m_selection_length = length;
    m_sample_rate      = rate;
    overviewChanged();
}

//***************************************************************************
void OverViewWidget::resizeEvent(QResizeEvent *)
{
    refreshBitmap();
}

//***************************************************************************
QSize OverViewWidget::minimumSize() const
{
    return QSize(30, 30);
}

//***************************************************************************
QSize OverViewWidget::sizeHint() const
{
    return minimumSize();
}

//***************************************************************************
void OverViewWidget::overviewChanged()
{
    // repainting is inhibited -> wait until the
    // repaint timer is elapsed
    if (m_repaint_timer.isActive()) {
	return;
    } else {
	// repaint once and once later...
	refreshBitmap();

	// start the repaint timer
	m_repaint_timer.setSingleShot(true);
	m_repaint_timer.start(REPAINT_INTERVAL);
    }
}

//***************************************************************************
void OverViewWidget::labelsChanged(const LabelList &labels)
{
    m_labels = labels;

    // only re-start the repaint timer, this hides some GUI update artifacts
    m_repaint_timer.stop();
    m_repaint_timer.setSingleShot(true);
    m_repaint_timer.start(REPAINT_INTERVAL);
}

//***************************************************************************
void OverViewWidget::playbackPositionChanged(unsigned int pos)
{
    const unsigned int old_pos = m_playback_position;
    const unsigned int new_pos = pos;

    if (new_pos == old_pos) return; // no change
    m_playback_position = new_pos;

    // check for change in pixel units
    unsigned int length = m_signal_length;
    if (m_view_offset + m_view_width > m_signal_length) {
	// showing deleted space after signal
	length = m_view_offset + m_view_width;
    }
    if (!length) return;
    const double scale = static_cast<double>(width()) /
                         static_cast<double>(length);
    const unsigned int old_pixel_pos = static_cast<unsigned int>(
	static_cast<double>(old_pos) * scale);
    const unsigned int new_pixel_pos = static_cast<unsigned int>(
	static_cast<double>(new_pos) * scale);
    if (old_pixel_pos == new_pixel_pos) return;

    // some update is required, start the repaint timer in quick mode
    if (!m_repaint_timer.isActive() || (m_repaint_timer.isActive() &&
	(m_repaint_timer.interval() != REPAINT_INTERVAL_FAST)))
    {
	m_repaint_timer.stop();
	m_repaint_timer.setSingleShot(true);
	m_repaint_timer.start(REPAINT_INTERVAL_FAST);
    }
}

//***************************************************************************
void OverViewWidget::playbackStopped()
{
    playbackPositionChanged(0);
}

//***************************************************************************
void OverViewWidget::drawMark(QPainter &p, int x, int height, QColor color)
{
    QPolygon mark;
    const int w = 5;
    const int y = (height - 1);

    p.setCompositionMode(QPainter::CompositionMode_SourceOver);
    color.setAlpha(100);
    p.setBrush(QBrush(color));
    p.setPen(QPen(Qt::black));

    mark.setPoints(3, x - w, 0, x + w, 0, x, w);     // upper
    p.drawPolygon(mark);
    mark.setPoints(3, x - w, y, x + w, y, x, y - w); // lower
    p.drawPolygon(mark);
}

//***************************************************************************
void OverViewWidget::refreshBitmap()
{
    if (m_worker_thread.isRunning()) {
	// (re)start the repaint timer if the worker thread is still
	// running, try again later...
	m_repaint_timer.stop();
	m_repaint_timer.setSingleShot(true);
	m_repaint_timer.start(REPAINT_INTERVAL);
    } else {
	// start the calculation in a background thread
	m_worker_thread.start(QThread::LowPriority);
    }
}

//***************************************************************************
void OverViewWidget::calculateBitmap()
{
    unsigned int length = m_signal_length;
    if (m_view_offset + m_view_width > m_signal_length) {
	// showing deleted space after signal
	length = m_view_offset + m_view_width;
    }

    int width  = this->width();
    int height = this->height();
    if (!width || !height || !m_view_width || !length)
	return;

    const double scale = static_cast<double>(width) /
	                 static_cast<double>(length);
    const int bitmap_width = static_cast<int>(m_signal_length * scale);

    // let the bitmap be updated from the cache
    QImage bitmap = m_cache.getOverView(bitmap_width, height,
	BAR_FOREGROUND ,BAR_BACKGROUND);

    // draw the bitmap (converted to QImage)
    QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
    QPainter p;
    p.begin(&image);
    p.fillRect(rect(), BAR_BACKGROUND);
    p.drawImage(0, 0, bitmap);

    // hilight the selection
    if ((m_selection_length > 1) && m_signal_length)
    {
	unsigned int first = static_cast<unsigned int>(
	    static_cast<double>(m_selection_start) * scale);
	unsigned int len   = static_cast<unsigned int>(
	    static_cast<double>(m_selection_length) * scale);
	if (len < 1) len = 1;

	// draw the selection as rectangle
	QBrush hilight(Qt::yellow);
	hilight.setStyle(Qt::SolidPattern);
	p.setBrush(hilight);
	p.setPen(QPen(Qt::yellow));
	p.setCompositionMode(QPainter::CompositionMode_Exclusion);
	p.drawRect(first, 0, len, height);

	// marks at start and end of selection
	drawMark(p, first, height, Qt::blue);
	drawMark(p, first + len, height, Qt::blue);
    }

    // draw labels
    unsigned int last_label_pos = width + 1;;
    foreach (const Label &label, m_labels) {
	unsigned int pos = label.pos();
	unsigned int x = static_cast<unsigned int>(
	    static_cast<double>(pos) * scale);

	// position must differ from the last one, otherwise we
	// would wipe out the last one with XOR mode
	if (x == last_label_pos) continue;

	// draw a line for each label
	p.setPen(QPen(Qt::cyan));
	p.setCompositionMode(QPainter::CompositionMode_Exclusion);
	p.drawLine(x, 0, x, height);
	drawMark(p, x, height, Qt::cyan);

	last_label_pos = x;
    }

    // draw playback position
    if (m_playback_position) {
	const unsigned int pos = m_playback_position;
	unsigned int x = static_cast<unsigned int>(
	    static_cast<double>(pos) * scale);

	// draw a line for the playback position
	QPen pen(Qt::yellow);
	pen.setWidth(5);
	p.setPen(pen);
	p.setCompositionMode(QPainter::CompositionMode_Exclusion);
	p.drawLine(x, 0, x, height);
	drawMark(p, x, height, Qt::cyan);
    }

    // dim the currently invisible parts
    if ((m_view_offset > 0) || (m_view_offset + m_view_width < m_signal_length))
    {
	QColor color = BAR_BACKGROUND;
	color.setAlpha(128);
	QBrush out_of_view(color);
	out_of_view.setStyle(Qt::SolidPattern);
	p.setBrush(out_of_view);
	p.setPen(QPen(color));
	p.setCompositionMode(QPainter::CompositionMode_SourceOver);

	if (m_view_offset > 0) {
	    unsigned int x = static_cast<unsigned int>(
		static_cast<double>(m_view_offset) * scale);
	    p.drawRect(0, 0, x, height);
	}

	if (m_view_offset + m_view_width < m_signal_length) {
	    unsigned int x = static_cast<unsigned int>(
		static_cast<double>(m_view_offset + m_view_width) * scale);
	    p.drawRect(x, 0, width - x, height);
	}
    }

    p.end();

    // update the widget with the overview
    emit newImage(image);
}

//***************************************************************************
#include "OverViewWidget.moc"
//***************************************************************************
//***************************************************************************

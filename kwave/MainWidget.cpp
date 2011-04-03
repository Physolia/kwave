/***************************************************************************
         MainWidget.cpp  -  main widget of the Kwave TopWidget
			     -------------------
    begin                : 1999
    copyright            : (C) 1999 by Martin Wilz
    email                : Martin Wilz <mwilz@ernie.mi.uni-koeln.de>
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

#include <errno.h>
#include <math.h>
#include <stdlib.h>

#include <QApplication>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QResizeEvent>
#include <QScrollBar>
#include <QtGlobal>
#include <QWheelEvent>

#include <klocale.h>

#include "libkwave/ApplicationContext.h"
#include "libkwave/CodecManager.h"
#include "libkwave/KwaveDrag.h"
#include "libkwave/KwaveFileDrag.h"
#include "libkwave/MessageBox.h"
#include "libkwave/Parser.h"
#include "libkwave/SignalManager.h"
#include "libkwave/undo/UndoTransactionGuard.h"

#include "libgui/LabelPropertiesWidget.h"
#include "libgui/OverViewWidget.h"
#include "libgui/SignalWidget.h"

#include "MainWidget.h"

/**
 * useful macro for command parsing
 */
#define CASE_COMMAND(x) } else if (parser.command() == x) {

/**
 * Limits the zoom to a minimum number of samples visible in one
 * screen.
 */
#define MINIMUM_SAMPLES_PER_SCREEN 5

/**
 * Default widht of the display in seconds when in streaming mode,
 * where no initial length information is available
 */
#define DEFAULT_DISPLAY_TIME 60.0

//***************************************************************************
MainWidget::MainWidget(QWidget *parent, Kwave::ApplicationContext &context)
    :QWidget(parent), m_context(context), m_upper_dock(), m_lower_dock(),
     m_view_port(this),
     m_signal_widget(&m_view_port, context, &m_upper_dock, &m_lower_dock),
     m_overview(0), m_vertical_scrollbar(0),
     m_horizontal_scrollbar(0), m_offset(0), m_width(0), m_zoom(1.0)
{
//     QPalette palette;
//    qDebug("MainWidget::MainWidget()");

    setAcceptDrops(true); // enable drag&drop

    SignalManager *signal_manager = m_context.signalManager();
    Q_ASSERT(signal_manager);
    if (!signal_manager) return;

    Kwave::PluginManager *plugin_manager = context.pluginManager();
    Q_ASSERT(plugin_manager);
    if (!plugin_manager) return;
    plugin_manager->registerViewManager(&m_signal_widget);

    // topLayout:
    // - upper dock
    // - hbox
    // - lower dock
    // - overview
    // - horizontal scroll bar
    QVBoxLayout *topLayout = new QVBoxLayout(this);
    Q_ASSERT(topLayout);
    if (!topLayout) return;

    // -- upper dock --
    topLayout->addLayout(&m_upper_dock);

    // -- signal widget --

    // hbox: left = viewport, right = vertical scroll bar
    QHBoxLayout *hbox = new QHBoxLayout();
    Q_ASSERT(hbox);
    if (!hbox) return;

    // viewport for the SignalWidget, does the clipping
    hbox->addWidget(&m_view_port);

    connect(&m_signal_widget, SIGNAL(contentSizeChanged()),
	    this, SLOT(resizeViewPort()));

    // -- vertical scrollbar for the view port --

    m_vertical_scrollbar = new QScrollBar();
    Q_ASSERT(m_vertical_scrollbar);
    if (!m_vertical_scrollbar) return;
    m_vertical_scrollbar->setOrientation(Qt::Vertical);
    m_vertical_scrollbar->setFixedWidth(
	m_vertical_scrollbar->sizeHint().width());
    hbox->addWidget(m_vertical_scrollbar);
    topLayout->addLayout(hbox, 100);
    connect(m_vertical_scrollbar, SIGNAL(valueChanged(int)),
            this,                 SLOT(verticalScrollBarMoved(int)));
    m_vertical_scrollbar->hide();

    // -- lower dock --
    topLayout->addLayout(&m_lower_dock);

    // -- overview widget --

    m_overview = new OverViewWidget(*signal_manager, this);
    Q_ASSERT(m_overview);
    if (!m_overview) return;
    m_overview->setMinimumHeight(m_overview->sizeHint().height());
    m_overview->setSizePolicy(
	QSizePolicy::MinimumExpanding, QSizePolicy::Fixed
    );
    topLayout->addWidget(m_overview);
    connect(m_overview, SIGNAL(valueChanged(sample_index_t)),
	    this,       SLOT(setOffset(sample_index_t)));
    connect(m_overview, SIGNAL(sigCommand(const QString &)),
            this,       SIGNAL(sigCommand(const QString &)));
    m_overview->metaDataChanged(signal_manager->metaData());
    m_overview->hide();

    // -- horizontal scrollbar --

    m_horizontal_scrollbar = new QScrollBar(this);
    Q_ASSERT(m_horizontal_scrollbar);
    if (!m_horizontal_scrollbar) return;
    m_horizontal_scrollbar->setOrientation(Qt::Horizontal);
    topLayout->addWidget(m_horizontal_scrollbar);
    connect(m_horizontal_scrollbar, SIGNAL(valueChanged(int)),
	    this,                   SLOT(horizontalScrollBarMoved(int)));
    m_horizontal_scrollbar->hide();

    // -- playback position update --

    PlaybackController &playback = signal_manager->playbackController();
    connect(&playback, SIGNAL(sigPlaybackPos(sample_index_t)),
	m_overview, SLOT(playbackPositionChanged(sample_index_t)));
    connect(&playback, SIGNAL(sigPlaybackStopped()),
	m_overview, SLOT(playbackStopped()));

    // -- connect all signals from/to the signal widget --

    connect(&m_signal_widget, SIGNAL(sigCommand(const QString &)),
	    this,             SIGNAL(sigCommand(const QString &)));

    // -- connect all signals from/to the signal manager --

    connect(signal_manager, SIGNAL(sigTrackInserted(unsigned int, Track *)),
	    this,           SLOT(slotTrackInserted(unsigned int, Track *)));
    connect(signal_manager, SIGNAL(sigTrackDeleted(unsigned int)),
	    this,           SLOT(slotTrackDeleted(unsigned int)));
    connect(signal_manager, SIGNAL(sigStatusInfo(sample_index_t,
	                           unsigned int, double, unsigned int)),
	    this, SLOT(updateViewRange()));

    this->setLayout(topLayout);

    resizeViewPort();

//    qDebug("MainWidget::MainWidget(): done.");
}

//***************************************************************************
bool MainWidget::isOK()
{
    return (m_vertical_scrollbar && m_horizontal_scrollbar && m_overview);
}

//***************************************************************************
MainWidget::~MainWidget()
{
    Kwave::PluginManager *plugin_manager = m_context.pluginManager();
    Q_ASSERT(plugin_manager);
    if (plugin_manager) plugin_manager->registerViewManager(0);
}

//***************************************************************************
void MainWidget::resizeEvent(QResizeEvent *event)
{
    Q_UNUSED(event);
    resizeViewPort();
}

//***************************************************************************
void MainWidget::dragEnterEvent(QDragEnterEvent *event)
{
    if (!event) return;
    if ((event->proposedAction() != Qt::MoveAction) &&
        (event->proposedAction() != Qt::CopyAction))
        return; /* unsupported action */

    if (KwaveFileDrag::canDecode(event->mimeData()))
	event->acceptProposedAction();
}


//***************************************************************************
void MainWidget::dropEvent(QDropEvent *event)
{
    if (!event) return;
    if (!event->mimeData()) return;

    SignalManager *signal_manager = m_context.signalManager();
    Q_ASSERT(signal_manager);
    if (!signal_manager) return;

    if (signal_manager->isEmpty() && KwaveDrag::canDecode(event->mimeData())) {
	sample_index_t pos = m_offset + pixels2samples(event->pos().x());
	sample_index_t len = 0;

	if ((len = KwaveDrag::decode(this, event->mimeData(),
	    *signal_manager, pos)))
	{
	    // set selection to the new area where the drop was done
	    signal_manager->selectRange(pos, len);
	    event->acceptProposedAction();
	} else {
	    qWarning("MainWidget::dropEvent(%s): failed !", event->format(0));
	    event->ignore();
	}
    } else if (event->mimeData()->hasUrls()) {
	bool first = true;
	foreach (QUrl url, event->mimeData()->urls()) {
	    QString filename = url.toLocalFile();
	    QString mimetype = CodecManager::whatContains(filename);
	    if (CodecManager::canDecode(mimetype)) {
		if (first) {
		    // first dropped URL -> open in this window
		    emit sigCommand("open(" + filename + ")");
		    first = false;
		} else {
		    // all others -> open a new window
		    emit sigCommand("newwindow(" + filename + ")");
		}
	    }
	}
    }

    qDebug("MainWidget::dropEvent(): done");
}

//***************************************************************************
void MainWidget::wheelEvent(QWheelEvent *event)
{
    if (!event || !m_overview) return;

    // process only wheel events on the signal and overview frame,
    // not on the channel controls or scrollbars
    if (!m_view_port.geometry().contains(event->pos()) &&
	!m_overview->geometry().contains(event->pos()) )
    {
	event->ignore();
	return;
    }

    switch (event->modifiers()) {
	case Qt::NoModifier: {
	    // no modifier + <WheelUp/Down> => scroll left/right
	    if (event->delta() > 0)
		executeCommand("scrollleft()");
	    else if (event->delta() < 0)
		executeCommand("scrollright()");
	    event->accept();
	    break;
	}
	case Qt::ShiftModifier:
	    // <Shift> + <WheelUp/Down> => page up/down
	    if (event->delta() > 0)
		executeCommand("viewprev()");
	    else if (event->delta() < 0)
		executeCommand("viewnext()");
	    event->accept();
	    break;
	case Qt::ControlModifier:
	    // <Ctrl> + <WheelUp/Down> => zoom in/out
	    if (event->delta() > 0)
		executeCommand("zoomin()");
	    else if (event->delta() < 0)
		executeCommand("zoomout()");
	    event->accept();
	    break;
	default:
	    event->ignore();
    }
}

//***************************************************************************
void MainWidget::verticalScrollBarMoved(int newval)
{
    m_signal_widget.move(0, -newval); // move the signal views
}

//***************************************************************************
void MainWidget::slotTrackInserted(unsigned int index, Track *track)
{
//     qDebug("MainWidget::slotTrackInserted(%u, %p)",
// 	   index, static_cast<void *>(track));
    Q_UNUSED(index);
    Q_UNUSED(track);

    // when the first track has been inserted, set some reasonable zoom
    SignalManager *signal_manager = m_context.signalManager();
    bool first_track = (signal_manager && (signal_manager->tracks() == 1));

    resizeViewPort();
    updateViewRange();
    if (first_track) zoomAll();
}

//***************************************************************************
void MainWidget::slotTrackDeleted(unsigned int index)
{
//     qDebug("MainWidget::slotTrackDeleted(%u)", index);
    Q_UNUSED(index);

    resizeViewPort();
    updateViewRange();
}

//***************************************************************************
double MainWidget::zoom() const
{
    return m_zoom;
}

//***************************************************************************
int MainWidget::executeCommand(const QString &command)
{
    SignalManager *signal_manager = m_context.signalManager();
    Q_ASSERT(signal_manager);
    if (!signal_manager) return -EINVAL;
    if (!command.length()) return -EINVAL;

    Parser parser(command);
    const sample_index_t visible_samples = displaySamples();

    if (false) {

    // -- zoom --
    CASE_COMMAND("zoomin")
	zoomIn();
    CASE_COMMAND("zoomout")
	zoomOut();
    CASE_COMMAND("zoomselection")
	zoomSelection();
    CASE_COMMAND("zoomall")
	zoomAll();
    CASE_COMMAND("zoomnormal")
	zoomNormal();

    // -- navigation --
    CASE_COMMAND("goto")
	sample_index_t offset = parser.toUInt();
	setOffset((offset > (visible_samples / 2)) ?
	          (offset - (visible_samples / 2)) : 0);
	signal_manager->selectRange(offset, 0);
    CASE_COMMAND("scrollright")
	const unsigned int step = visible_samples / 10;
	setOffset(m_offset + step);
    CASE_COMMAND("scrollleft")
	const unsigned int step = visible_samples / 10;
	setOffset((step < m_offset) ? (m_offset - step) : 0);
    CASE_COMMAND("viewstart")
	setOffset(0);
	signal_manager->selectRange(0, 0);
    CASE_COMMAND("viewend")
	sample_index_t len = signal_manager->length();
	if (len >= visible_samples) setOffset(len - visible_samples);
    CASE_COMMAND("viewnext")
	setOffset(m_offset + visible_samples);
    CASE_COMMAND("viewprev")
	setOffset((visible_samples < m_offset) ?
	          (m_offset - visible_samples) : 0);

    // -- selection --
    CASE_COMMAND("selectall")
	signal_manager->selectRange(0, signal_manager->length());
    CASE_COMMAND("selectnext")
	if (signal_manager->selection().length())
	    signal_manager->selectRange(signal_manager->selection().last()+1,
	                signal_manager->selection().length());
	else
	    signal_manager->selectRange(signal_manager->length() - 1, 0);
    CASE_COMMAND("selectprev")
	sample_index_t ofs = signal_manager->selection().first();
	sample_index_t len = signal_manager->selection().length();
	if (!len) len = 1;
	if (len > ofs) len = ofs;
	signal_manager->selectRange(ofs - len, len);
    CASE_COMMAND("selecttoleft")
	signal_manager->selectRange(0, signal_manager->selection().last() + 1);
    CASE_COMMAND("selecttoright")
	signal_manager->selectRange(signal_manager->selection().first(),
	    signal_manager->length() - signal_manager->selection().first()
	);
    CASE_COMMAND("selectvisible")
	signal_manager->selectRange(m_offset, displaySamples());
    CASE_COMMAND("selectnone")
	signal_manager->selectRange(m_offset, 0);
	
    // label handling
    CASE_COMMAND("label")
	unsigned int pos = parser.toUInt();
	addLabel(pos);
//    CASE_COMMAND("chooselabel")
//	Parser parser(command);
//	markertype = globals.markertypes.at(parser.toInt());
//    CASE_COMMAND("amptolabel")
//	markSignal(command);
//    CASE_COMMAND("pitch")
//	markPeriods(command);
//    CASE_COMMAND("labeltopitch")
//      convertMarkstoPitch(command);
//    CASE_COMMAND("loadlabel")  -> plugin
//	loadLabel();
//    CASE_COMMAND("savelabel")  -> plugin
//	saveLabel(command);
//    CASE_COMMAND("markperiod")
//	markPeriods(command);
//    CASE_COMMAND("saveperiods")
//	savePeriods();
	
    } else {
	return (signal_manager) ? 
	    signal_manager->executeCommand(command) : -ENOSYS;
    }

    return 0;
}

//***************************************************************************
void MainWidget::resizeViewPort()
{
    const int old_h = m_signal_widget.height();
    const int old_w = m_signal_widget.width();

    // workaround for layout update issues: give the layouts a chance to resize
    layout()->invalidate();
    layout()->update();
    layout()->activate();
    qApp->sendPostedEvents();

    bool vertical_scrollbar_visible = m_vertical_scrollbar->isVisible();
    const int min_height = m_signal_widget.sizeHint().height();
    int h = m_view_port.height();
    int w = m_view_port.width();
    const int b = m_vertical_scrollbar->sizeHint().width();

    // if the signal widget's minimum height is smaller than the viewport
    if (min_height <= h) {
	// change the signal widget's vertical mode to "MinimumExpanding"
	m_signal_widget.setSizePolicy(
	    QSizePolicy::Expanding,
	    QSizePolicy::MinimumExpanding
	);

	// hide the vertical scrollbar
	if (vertical_scrollbar_visible) {
	    m_vertical_scrollbar->setShown(false);
	    vertical_scrollbar_visible = false;
	    w += b;
	    m_signal_widget.move(0, 0);
	    // qDebug("MainWidget::resizeViewPort(): hiding scrollbar");
	}
    } else {
	// -> otherwise set the widget height to "Fixed"
	m_signal_widget.setSizePolicy(
	    QSizePolicy::Expanding,
	    QSizePolicy::Preferred
	);

	// -- show the scrollbar --
	if (!vertical_scrollbar_visible) {
	    m_vertical_scrollbar->setFixedWidth(b);
	    m_vertical_scrollbar->setValue(0);
	    m_vertical_scrollbar->setShown(true);
	    vertical_scrollbar_visible = true;
	    w -= b;
	    // qDebug("MainWidget::resizeViewPort(): showing scrollbar");
	}

	// adjust the limits of the vertical scrollbar
	int min = m_vertical_scrollbar->minimum();
	int max = m_vertical_scrollbar->maximum();
	double val = (m_vertical_scrollbar->value() -
	    static_cast<double>(min)) / static_cast<double>(max - min);

	h = min_height;
	min = 0;
	max = h - m_view_port.height();
	m_vertical_scrollbar->setRange(min, max);
	m_vertical_scrollbar->setValue(static_cast<int>(floor(val *
	    static_cast<double>(max))));
	m_vertical_scrollbar->setSingleStep(1);
	m_vertical_scrollbar->setPageStep(m_view_port.height());
    }

//     qDebug("w: %4d -> %4d, h: %4d -> %4d", old_w, w, old_h, h);

    // resize the signal widget and the frame with the channel controls
    if ((old_w != w) || (old_h != h))
    {
	m_width = m_width + w - old_w;
	m_signal_widget.resize(w, h);
	fixZoomAndOffset();
    }

    // remember the last width of the signal widget, for zoom calculation
    m_width = m_signal_widget.viewPortWidth();

    repaint();
}

//***************************************************************************
void MainWidget::updateViewInfo(sample_index_t, sample_index_t, sample_index_t)
{
    refreshHorizontalScrollBar();
}

//***************************************************************************
void MainWidget::refreshHorizontalScrollBar()
{
    if (!m_horizontal_scrollbar || !m_context.signalManager()) return;

    m_horizontal_scrollbar->blockSignals(true);

    // show/hide the overview widget
    if (!m_context.signalManager()->isEmpty() && !m_overview->isVisible())
	m_overview->show();
    if (m_context.signalManager()->isEmpty() && m_overview->isVisible())
	m_overview->hide();

    // adjust the limits of the horizontal scrollbar
    if (m_context.signalManager()->length() > 1) {
	// get the view information in samples
	sample_index_t length  = m_context.signalManager()->length();
	sample_index_t visible = displaySamples();
	if (visible > length) visible = length;

	// calculate the scrollbar ranges in scrollbar's units
	//
	// NOTE: we must take care of possible numeric overflows
	//       as the scrollbar works internally with "int" and
	//       the offsets we use for the samples might be bigger!
	//
	// [-------------------------------------------##############]
	// ^                                          ^     ^
	// min                                      max    page
	//
	// max + page = x | x < INT_MAX (!)
	//                                  x := length / f
	// page = x * (visible / length)  = visible  / f
	// max                            = length   / f - page
	// pos  = (m_offset / length) * x = m_offset / f

	const int f = qMax(1U, SAMPLE_INDEX_MAX / INT_MAX);
	int page    = (visible  / f);
	int min     = 0;
	int max     = (length   / f) - page;
	int pos     = (m_offset / f);
	int single  = qMax(1, (page / (10 * qApp->wheelScrollLines())));
	if (page < single) page = single;
// 	qDebug("width=%d, max=%d, page=%d, single=%d, pos=%d, visible=%d",
// 	       m_width, max, page, single, pos, visible);
// 	qDebug("       last=%d", pos + visible - 1);
	m_horizontal_scrollbar->setRange(min, max);
	m_horizontal_scrollbar->setValue(pos);
	m_horizontal_scrollbar->setSingleStep(single);
	m_horizontal_scrollbar->setPageStep(page);
    } else {
	m_horizontal_scrollbar->setRange(0, 0);
    }

    m_horizontal_scrollbar->blockSignals(false);
}

//***************************************************************************
void MainWidget::horizontalScrollBarMoved(int newval)
{
    // new offset = pos * f
    const int f = qMax(1U, SAMPLE_INDEX_MAX / INT_MAX);
    const sample_index_t pos = newval * f;
    setOffset(pos);
}

//***************************************************************************
void MainWidget::updateViewRange()
{
    SignalManager *signal_manager = m_context.signalManager();
    sample_index_t total = (signal_manager) ? signal_manager->length() : 0;

    // forward the zoom and offset to the signal widget and overview
    m_signal_widget.setZoomAndOffset(m_zoom, m_offset);
    if (m_overview) m_overview->setRange(m_offset, displaySamples(), total);
    refreshHorizontalScrollBar();
}

//***************************************************************************
sample_index_t MainWidget::ms2samples(double ms)
{
    SignalManager *signal_manager = m_context.signalManager();
    Q_ASSERT(signal_manager);
    if (!signal_manager) return 0;

    return static_cast<sample_index_t>(
	rint(ms * signal_manager->rate() / 1E3));
}

//***************************************************************************
sample_index_t MainWidget::pixels2samples(unsigned int pixels) const
{
    if ((pixels <= 0) || (m_zoom <= 0.0)) return 0;
    return static_cast<sample_index_t>(rint(
	static_cast<double>(pixels) * m_zoom));
}

//***************************************************************************
int MainWidget::samples2pixels(sample_index_t samples) const
{
    if (m_zoom == 0.0) return 0;
    return static_cast<int>(rint(static_cast<double>(samples) / m_zoom));
}

//***************************************************************************
int MainWidget::displayWidth() const
{
    return m_width;
}

//***************************************************************************
sample_index_t MainWidget::displaySamples() const
{
    return pixels2samples(m_width - 1) + 1;
}

//***************************************************************************
double MainWidget::fullZoom()
{
    SignalManager *signal_manager = m_context.signalManager();
    Q_ASSERT(signal_manager);
    if (!signal_manager) return 0.0;
    if (signal_manager->isEmpty()) return 0.0;    // no zoom if no signal

    sample_index_t length = signal_manager->length();
    if (!length) {
        // no length: streaming mode -> start with a default
        // zoom, use one minute (just guessed)
        length = static_cast<sample_index_t>(ceil(DEFAULT_DISPLAY_TIME *
	    signal_manager->rate()));
    }

    // example: width = 100 [pixels] and length = 3 [samples]
    //          -> samples should be at positions 0, 49.5 and 99
    //          -> 49.5 [pixels / sample]
    //          -> zoom = 1 / 49.5 [samples / pixel]
    // => full zoom [samples/pixel] = (length - 1) / (width - 1)
    return static_cast<double>(length - 1) /
	   static_cast<double>(m_width - 1);
}

//***************************************************************************
bool MainWidget::fixZoomAndOffset()
{
    double max_zoom;
    double min_zoom;
    sample_index_t length;

    const int   old_width = m_width;
    const double old_zoom = m_zoom;

    SignalManager *signal_manager = m_context.signalManager();
    Q_ASSERT(signal_manager);
    if (!signal_manager) return false;
    if (!m_width) return false;

    length = signal_manager->length();
    if (!length) {
	// in streaming mode we have to use a guessed length
	length = static_cast<sample_index_t>(ceil(m_width * fullZoom()));
    }

    // ensure that m_offset is [0...length-1]
    if (m_offset > length - 1) m_offset = length - 1;

    // ensure that the zoom is in a proper range
    max_zoom = fullZoom();
    min_zoom = static_cast<double>(MINIMUM_SAMPLES_PER_SCREEN) /
	       static_cast<double>(m_width);
    if (m_zoom < min_zoom) m_zoom = min_zoom;
    if (m_zoom > max_zoom) m_zoom = max_zoom;

    // try to correct the offset if there is not enough data to fill
    // the current window
    // example: width=100 [pixels], length=3 [samples],
    //          offset=1 [sample], zoom=1/49.5 [samples/pixel] (full)
    //          -> current last displayed sample = length-offset
    //             = 3 - 1 = 2
    //          -> available space = pixels2samples(width-1) + 1
    //             = (99/49.5) + 1 = 3
    //          -> decrease offset by 3 - 2 = 1
    Q_ASSERT(length >= m_offset);
    if (pixels2samples(m_width - 1) + 1 > length - m_offset) {
	// there is space after the signal -> move offset right
	sample_index_t shift = pixels2samples(m_width - 1) + 1 -
	                       (length - m_offset);
	if (shift >= m_offset) {
	    m_offset = 0;
	} else {
	    m_offset -= shift;
	}
    }

    // emit change in the zoom factor
    if (m_zoom != old_zoom) emit sigZoomChanged(m_zoom);

    return ((m_width != old_width) || (m_zoom != old_zoom));
}

//***************************************************************************
void MainWidget::setZoom(double new_zoom)
{
    double         old_zoom   = m_zoom;
    sample_index_t old_offset = m_offset;

    m_zoom = new_zoom;
    fixZoomAndOffset();
    if ((m_offset != old_offset) || (m_zoom != old_zoom))
	updateViewRange();
}

//***************************************************************************
void MainWidget::setOffset(sample_index_t new_offset)
{
    double         old_zoom   = m_zoom;
    sample_index_t old_offset = m_offset;

    m_offset = new_offset;
    fixZoomAndOffset();
    if ((m_offset != old_offset) || (m_zoom != old_zoom))
	updateViewRange();
}

//***************************************************************************
void MainWidget::zoomSelection()
{
    SignalManager *signal_manager = m_context.signalManager();
    Q_ASSERT(signal_manager);
    if (!signal_manager) return;

    sample_index_t ofs = signal_manager->selection().offset();
    sample_index_t len = signal_manager->selection().length();

    if (len) {
	m_offset = ofs;
	setZoom((static_cast<double>(len)) / static_cast<double>(m_width - 1));
    }
}

//***************************************************************************
void MainWidget::zoomAll()
{
    setZoom(fullZoom());
}

//***************************************************************************
void MainWidget::zoomNormal()
{
    const sample_index_t shift1 = m_width / 2;
    const sample_index_t shift2 = displaySamples() / 2;
    m_offset = (m_offset + shift2 >= m_offset) ? (m_offset + shift2) : -shift2;
    m_offset = (m_offset > shift1) ? (m_offset - shift1) : 0;
    setZoom(1.0);
}

//***************************************************************************
void MainWidget::zoomIn()
{
    const sample_index_t shift = displaySamples() / 3;
    m_offset = (m_offset + shift >= m_offset) ? (m_offset + shift) : -shift;
    setZoom(m_zoom / 3);
}

//***************************************************************************
void MainWidget::zoomOut()
{
    const sample_index_t shift = displaySamples();
    m_offset = (m_offset > shift) ? (m_offset - shift) : 0;
    setZoom(m_zoom * 3);
}

//***************************************************************************
void MainWidget::addLabel(sample_index_t pos)
{
    SignalManager *signal_manager = m_context.signalManager();
    Q_ASSERT(signal_manager);
    if (!signal_manager) return;

    UndoTransactionGuard undo(*signal_manager, i18n("Add Label"));

    // add a new label, with undo
    Label label = signal_manager->addLabel(pos, QString());
    if (label.isNull()) {
	signal_manager->abortUndoTransaction();
	return;
    }

    // edit the properties of the new label
    if (!labelProperties(label)) {
	// aborted or failed -> delete (without undo)
	int index = signal_manager->labelIndex(label);
	signal_manager->deleteLabel(index, false);
	signal_manager->abortUndoTransaction();
    }
}

// ////****************************************************************************
// //void MainWidget::loadLabel()
// //{
// //    labels().clear();    //remove old Labels...
// //    appendLabel ();
// //}

// ////****************************************************************************
// //void MainWidget::saveLabel (const char *typestring)
// //{
// //    QString name = KFileDialog::getSaveFileName (0, "*.label", this);
// //    if (!name.isNull()) {
// //	FILE *out;
// //	out = fopen (name.local8Bit(), "w");
// //
// //	Parser parser (typestring);
// //	Label *tmp;
// //	LabelType *act;
// //
// //	const char *actstring = parser.getFirstParam();
// //
// //	while (actstring) {
// //	    printf ("selecting %s\n", actstring);
// //	    for (act = globals.markertypes.first(); act; act = globals.markertypes.next())
// //		if (strcmp(act->name, actstring) == 0) {
// //		    printf ("selected\n");
// //		    act->selected = true;
// //		    break;
// //		}
// //	    actstring = parser.getNextParam();
// //	}
// //
// //	for (act = globals.markertypes.first(); act; act = globals.markertypes.next())
// //	    //write out all selected label types
// //	    if (act->selected)
// //		fprintf (out, "%s\n", act->getCommand());
// //
// //	//ended writing of types, so go on with the labels...
// //
// //	for (tmp = labels().first(); tmp; tmp = labels().next())  //write out labels
// //	{
// //	    fprintf (out, tmp->getCommand());
// //	    fprintf (out, "\n");
// //	}
// //
// //	fclose (out);
// //    }
// //}

//***************************************************************************
bool MainWidget::labelProperties(Label &label)
{
    SignalManager *signal_manager = m_context.signalManager();
    Q_ASSERT(signal_manager);
    if (!signal_manager) return false;

    if (label.isNull()) return false;
    int index = signal_manager->labelIndex(label);
    Q_ASSERT(index >= 0);
    if (index < 0) return false;

    // try to modify the label. just in case that the user moves it
    // to a position where we already have one, catch this situation
    // and ask if he wants to abort, re-enter the label properties
    // dialog or just replace (remove) the label at the target position
    bool accepted;
    sample_index_t new_pos  = label.pos();
    QString        new_name = label.name();
    int old_index = -1;
    while (true) {
	// create and prepare the dialog
	LabelPropertiesWidget *dlg = new LabelPropertiesWidget(this);
	Q_ASSERT(dlg);
	if (!dlg) return false;
	dlg->setLabelIndex(index);
	dlg->setLabelPosition(new_pos, signal_manager->length(),
	    signal_manager->rate());
	dlg->setLabelName(new_name);

	// execute the dialog
	accepted = (dlg->exec() == QDialog::Accepted);
	if (!accepted) {
	    // user pressed "cancel"
	    delete dlg;
	    break;
	}

	// if we get here the user pressed "OK"
	new_pos  = dlg->labelPosition();
	new_name = dlg->labelName();
	dlg->saveSettings();
	delete dlg;

	// check: if there already is a label at the new position
	// -> ask the user if he wants to overwrite that one
	if ((new_pos != label.pos()) &&
	    !signal_manager->findLabel(new_pos).isNull())
	{
	    int res = Kwave::MessageBox::warningYesNoCancel(this, i18n(
		"There already is a label at the position you have chosen.\n"\
		"Do you want to replace it?"));
	    if (res == KMessageBox::Yes) {
		// delete the label at the target position (with undo)
		Label old = signal_manager->findLabel(new_pos);
		old_index = signal_manager->labelIndex(old);
		break;
	    }
	    if (res == KMessageBox::No) {
		// make another try -> re-enter the dialog
		continue;
	    }

	    // cancel -> abort the whole action
	    accepted = false;
	    break;
	} else {
	    // ok, we can put it there
	    break;
	}
    }

    if (accepted) {
	// shortcut: abort if nothing has changed
	if ((new_name == label.name()) && (new_pos == label.pos()))
	    return false;

	UndoTransactionGuard undo(*signal_manager, i18n("Modify Label"));

	// if there is a label at the target position, remove it first
	if (old_index >= 0) {
	    signal_manager->deleteLabel(old_index, true);
	    // this might have changed the current index!
	    index = signal_manager->labelIndex(label);
	}

	// modify the label through the signal manager
	if (!signal_manager->modifyLabel(index, new_pos, new_name)) {
	    // position is already occupied
	    signal_manager->abortUndoTransaction();
	    return false;
	}

	// reflect the change in the passed label
	label.moveTo(new_pos);
	label.rename(new_name);

	// NOTE: moving might also change the index, so the complete
	//       markers layer has to be refreshed
    }
    else
	signal_manager->abortUndoTransaction();

    return accepted;
}

// ////****************************************************************************
// //void MainWidget::jumptoLabel ()
// // another fine function contributed by Gerhard Zintel
// // if lmarker == rmarker (no range selected) cursor jumps to the nearest label
// // if lmarker <  rmarker (range is selected) lmarker jumps to next lower label or zero
// // rmarker jumps to next higher label or end
// //{
// //    if (signalmanage) {
// //	int lmarker = signalmanage->getLMarker();
// //	int rmarker = signalmanage->getRMarker();
// //	bool RangeSelected = (rmarker - lmarker) > 0;
// //	if (!labels().isEmpty()) {
// //	    Label *tmp;
// //	    int position = 0;
// //	    for (tmp = labels->first(); tmp; tmp = labels->next())
// //		if (RangeSelected) {
// //		    if (tmp->pos < lmarker)
// //			if (qAbs(lmarker - position) >
// //			    qAbs(lmarker - ms2samples(tmp->pos)))
// //			    position = ms2samples(tmp->pos);
// //		} else if (qAbs(lmarker - position) >
// //			   qAbs(lmarker - ms2samples(tmp->pos)))
// //		    position = ms2samples(tmp->pos);
// //
// //	    lmarker = position;
// //	    position = signalmanage->getLength();
// //	    for (tmp = labels->first(); tmp; tmp = labels->next())
// //		if (tmp->pos > rmarker)
// //		    if (qAbs(rmarker - position) >
// //			qAbs(rmarker - ms2samples(tmp->pos)))
// //			position = ms2samples(tmp->pos);
// //	    rmarker = position;
// //	    if (RangeSelected) setRange(lmarker, rmarker);
// //	    else setRange (lmarker, lmarker);
// //	    refresh ();
// //	}
// //    }
// //}

// ////****************************************************************************
// //void MainWidget::savePeriods ()
// //{
// //    if (signalmanage) {
// //	Dialog *dialog =
// //	    DynamicLoader::getDialog ("marksave", new DialogOperation(&globals, signalmanage->getRate(), 0, 0));
// //
// //	if ((dialog) && (dialog->exec())) {
// //	    selectMarkers (dialog->getCommand());
// //
// //	    LabelType *act;
// //	    Label *tmp;
// //	    int last = 0;
// //	    int rate = signalmanage->getRate ();
// //
// //	    QString name = KFileDialog::getSaveFileName (0, "*.dat", this);
// //	    if (!name.isNull()) {
// //		QFile out(name.local8Bit());
// //		char buf[160];
// //		float freq = 0, time, lastfreq = 0;
// //		out.open (QIODevice::WriteOnly);
// //		int first = true;
// //
// //		for (act = globals.markertypes.first(); act; act = globals.markertypes.next())
// //		    //write only selected label type
// //		    if (act->selected)
// //			//traverse list of all labels
// //			for (tmp = labels->first(); tmp; tmp = labels->next()) {
// //			    if (tmp->getType() == act) {
// //				freq = tmp->pos - last;
// //				time = last * 1000 / rate;
// //
// //				if ((!first) && (freq != lastfreq)) {
// //				    lastfreq = freq;
// //				    freq = 1 / (freq / rate);
// //				    snprintf (buf, sizeof(buf), "%f %f\n",
// //					time, freq);
// //				    out.writeBlock (&buf[0], strlen(buf));
// //				} else lastfreq = freq;
// //				first = false;
// //				last = ms2samples(tmp->pos);
// //			    }
// //			}
// //
// //		if (!first) //make sure last tone gets its length
// //		{
// //		    time = last * 1000 / rate;
// //		    snprintf (buf, sizeof(buf), "%f %f\n", time, freq);
// //		    out.writeBlock (&buf[0], strlen(buf));
// //		}
// //
// //		out.close ();
// //	    }
// //	}
// //    }
// //}

// ////****************************************************************************
// //void MainWidget::markSignal (const char *str)
// //{
// //    if (signalmanage) {
// //	Label *newmark;
// //
// //	Parser parser (str);
// //
// //	int level = (int) (parser.toDouble() / 100 * (1 << 23));
// //
// //	int len = signalmanage->getLength();
// //	int *sam = signalmanage->getSignal(0)->getSample();    // ### @@@ ###
// //	LabelType *start = findMarkerType(parser.getNextParam());
// //	LabelType *stop = findMarkerType (parser.getNextParam());
// //	int time = (int) (parser.toDouble () * signalmanage->getRate() / 1000);
// //
// //	printf ("%d %d\n", level, time);
// //	printf ("%s %s\n", start->name, stop->name);
// //
// //	ProgressDialog *dialog =
// //	    new ProgressDialog (len, "Searching for Signal portions...");
// //
// //	if (dialog && start && stop) {
// //	    dialog->show();
// //
// //	    newmark = new Label(0, start);     //generate initial Label
// //
// //	    labels->inSort (newmark);
// //
// //	    for (int i = 0; i < len; i++) {
// //		if (qAbs(sam[i]) < level) {
// //		    int j = i;
// //		    while ((i < len) && (qAbs(sam[i]) < level)) i++;
// //
// //		    if (i - j > time) {
// //			//insert labels...
// //			newmark = new Label(i, start);
// //			labels->inSort (newmark);
// //
// //			if (start != stop) {
// //			    newmark = new Label(j, stop);
// //			    labels->inSort (newmark);
// //			}
// //		    }
// //		}
// //		dialog->setProgress (i);
// //	    }
// //
// //	    newmark = new Label(len - 1, stop);
// //	    labels->inSort (newmark);
// //
// //	    refresh ();
// //	    delete dialog;
// //	}
// //    }
// //}

// ////****************************************************************************
// //void MainWidget::markPeriods (const char *str)
// //{
// //    if (signalmanage) {
// //	Parser parser (str);
// //
// //	int high = signalmanage->getRate() / parser.toInt();
// //	int low = signalmanage->getRate() / parser.toInt();
// //	int octave = parser.toBool ("true");
// //	double adjust = parser.toDouble ();
// //
// //	for (int i = 0; i < AUTOKORRWIN; i++)
// //	    autotable[i] = 1 - (((double)i * i * i) / (AUTOKORRWIN * AUTOKORRWIN * AUTOKORRWIN));    //generate static weighting function
// //
// //	if (octave) for (int i = 0; i < AUTOKORRWIN; i++) weighttable[i] = 1;    //initialise moving weight table
// //
// //	Label *newmark;
// //	int next;
// //	int len = signalmanage->getLength();
// //	int *sam = signalmanage->getSignal(0)->getSample();    // ### @@@ ###
// //	LabelType *start = markertype;
// //	int cnt = findFirstMark (sam, len);
// //
// //	ProgressDialog *dialog = new ProgressDialog (len - AUTOKORRWIN, "Correlating Signal to find Periods:");
// //	if (dialog) {
// //	    dialog->show();
// //
// //	    newmark = new Label(cnt, start);
// //	    labels->inSort (newmark);
// //
// //	    while (cnt < len - 2*AUTOKORRWIN) {
// //		if (octave)
// //		    next = findNextRepeatOctave (&sam[cnt], high, adjust);
// //		else
// //		    next = findNextRepeat (&sam[cnt], high);
// //
// //		if ((next < low) && (next > high)) {
// //		    newmark = new Label(cnt, start);
// //
// //		    labels->inSort (newmark);
// //		}
// //		if (next < AUTOKORRWIN) cnt += next;
// //		else
// //		    if (cnt < len - AUTOKORRWIN) {
// //			int a = findFirstMark (&sam[cnt], len - cnt);
// //			if (a > 0) cnt += a;
// //			else cnt += high;
// //		    } else cnt = len;
// //
// //		dialog->setProgress (cnt);
// //	    }
// //
// //	    delete dialog;
// //
// //	    refresh ();
// //	}
// //    }
// //}

// ////*****************************************************************************
// //int findNextRepeat (int *sample, int high)
// ////autocorellation of a windowed part of the sample
// ////returns length of period, if found
// //{
// //    int i, j;
// //    double gmax = 0, max, c;
// //    int maxpos = AUTOKORRWIN;
// //    int down, up;         //flags
// //
// //    max = 0;
// //    for (j = 0; j < AUTOKORRWIN; j++)
// //	gmax += ((double)sample[j]) * sample [j];
// //
// //    //correlate signal with itself for finding maximum integral
// //
// //    down = 0;
// //    up = 0;
// //    i = high;
// //    max = 0;
// //    while (i < AUTOKORRWIN) {
// //	c = 0;
// //	for (j = 0; j < AUTOKORRWIN; j++) c += ((double)sample[j]) * sample [i + j];
// //	c = c * autotable[i];    //multiply window with weight for preference of high frequencies
// //	if (c > max) max = c, maxpos = i;
// //	i++;
// //    }
// //    return maxpos;
// //}

// ////*****************************************************************************
// //int findNextRepeatOctave (int *sample, int high, double adjust = 1.005)
// ////autocorellation of a windowed part of the sample
// ////same as above only with an adaptive weighting to decrease fast period changes
// //{
// //    int i, j;
// //    double gmax = 0, max, c;
// //    int maxpos = AUTOKORRWIN;
// //    int down, up;         //flags
// //
// //    max = 0;
// //    for (j = 0; j < AUTOKORRWIN; j++)
// //	gmax += ((double)sample[j]) * sample [j];
// //
// //    //correlate signal with itself for finding maximum integral
// //
// //    down = 0;
// //    up = 0;
// //    i = high;
// //    max = 0;
// //    while (i < AUTOKORRWIN) {
// //	c = 0;
// //	for (j = 0; j < AUTOKORRWIN; j++) c += ((double)sample[j]) * sample [i + j];
// //	c = c * autotable[i] * weighttable[i];
// //	//multiply window with weight for preference of high frequencies
// //	if (c > max) max = c, maxpos = i;
// //	i++;
// //    }
// //
// //    for (int i = 0; i < AUTOKORRWIN; i++) weighttable[i] /= adjust;
// //
// //    weighttable[maxpos] = 1;
// //    weighttable[maxpos + 1] = .9;
// //    weighttable[maxpos - 1] = .9;
// //    weighttable[maxpos + 2] = .8;
// //    weighttable[maxpos - 2] = .8;
// //
// //    float buf[7];
// //
// //    for (int i = 0; i < 7; buf[i++] = .1)
// //
// //	//low pass filter
// //	for (int i = high; i < AUTOKORRWIN - 3; i++) {
// //	    buf[i % 7] = weighttable[i + 3];
// //	    weighttable[i] = (buf[0] + buf[1] + buf[2] + buf[3] + buf[4] + buf[5] + buf[6]) / 7;
// //	}
// //
// //    return maxpos;
// //}

// //*****************************************************************************
// //int findFirstMark (int *sample, int len)
// ////finds first sample that is non-zero, or one that preceeds a zero crossing
// //{
// //    int i = 1;
// //    int last = sample[0];
// //    int act = last;
// //    if ((last < 100) && (last > -100)) i = 0;
// //    else
// //	while (i < len) {
// //	    act = sample[i];
// //	    if ((act < 0) && (last >= 0)) break;
// //	    if ((act > 0) && (last <= 0)) break;
// //	    last = act;
// //	    i++;
// //	}
// //    return i;
// //}

//***************************************************************************
#include "MainWidget.moc"
//***************************************************************************
//***************************************************************************

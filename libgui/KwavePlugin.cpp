/***************************************************************************
                    KwavePlugin.cpp  -  New Interface for Kwave plugins
                             -------------------
    begin                : Thu Jul 27 2000
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
#include <errno.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <dlfcn.h>
#include <sched.h>

#include <qwidget.h>

#include <kapp.h>

#include "mt/Thread.h"
#include "mt/MutexGuard.h"

#include "libkwave/MultiTrackReader.h"
#include "libkwave/MultiTrackWriter.h"
#include "libkwave/Sample.h"
#include "libkwave/SampleReader.h"
#include "libkwave/Signal.h"
#include "libkwave/Track.h"
#include "libgui/KwavePlugin.h"
#include "libgui/PluginContext.h"

#include "kwave/PluginManager.h"
#include "kwave/TopWidget.h"
#include "kwave/SignalManager.h"

#ifdef DEBUG
#include <execinfo.h> // for backtrace()
#endif

//***************************************************************************
KwavePlugin::KwavePlugin(PluginContext &c)
    :m_context(c),
     m_thread(0),
     m_thread_lock()
{
    m_thread_lock.setName("KwavePlugin");
}

//***************************************************************************
KwavePlugin::~KwavePlugin()
{
    // inform our owner that we close. This allows the plugin to
    // delete itself
    close();
}

//***************************************************************************
const QString &KwavePlugin::name()
{
    return m_context.name;
}

//***************************************************************************
const QString &KwavePlugin::version()
{
    return m_context.version;
}

//***************************************************************************
const QString &KwavePlugin::author()
{
    return m_context.author;
}

//***************************************************************************
void KwavePlugin::load(QStringList &)
{
}

//***************************************************************************
QStringList *KwavePlugin::setup(QStringList &)
{
    QStringList *result = new QStringList();
    ASSERT(result);
    return result;
}

//***************************************************************************
int KwavePlugin::start(QStringList &)
{
    MutexGuard lock(m_thread_lock);
    return 0;
}

//***************************************************************************
int KwavePlugin::stop()
{
    if (m_thread && m_thread->running() &&
	(pthread_self() == m_thread->threadID())) {
	warning("KwavePlugin::stop(): plugin '%s' called stop() from "\
	        "within it's own worker thread (from run() ?). "\
	        "This would produce a deadlock, dear %s, PLEASE FIX THIS !",
	        name().data(), author().data());

#ifdef DEBUG
	debug("pthread_self()=%08X, tid=%08X", (unsigned int)pthread_self(),
	      (unsigned int)m_thread->threadID());
	void *buf[256];
	size_t n = backtrace(buf, 256);
	backtrace_symbols_fd(buf, n, 2);
#endif
	return -EBUSY;
    }

    {
	MutexGuard lock(m_thread_lock);
	if (m_thread) {
	    if (m_thread->running()) m_thread->wait(5000);
	    if (m_thread->running()) m_thread->stop();
	    if (m_thread->running()) m_thread->wait(1000);
	    if (m_thread->running()) {
		// unable to stop the thread
		warning("KwavePlugin::stop(): stale thread !");
	    }
	    delete m_thread;
	    m_thread = 0;
	}
    }
    return 0;
}

//***************************************************************************
int KwavePlugin::execute(QStringList &params)
{
    MutexGuard lock(m_thread_lock);

    m_thread = new Asynchronous_Object_with_1_arg<KwavePlugin, QStringList>(
	this, &KwavePlugin::run,params);
    ASSERT(m_thread);
    if (!m_thread) return -ENOMEM;

    // start the thread, this executes run()
    m_thread->start();

    // sometimes the signal proxies remain blocked until an initial
    // X11 event occurs and thus might block the thread :-(
    QApplication::syncX();
    ASSERT(qApp);
    if (qApp) qApp->wakeUpGuiThread();

    return 0;
}

//***************************************************************************
void KwavePlugin::run(QStringList)
{
    debug("KwavePlugin::run");
}

//***************************************************************************
void KwavePlugin::close()
{
    // only call stop() if we are NOT in the worker thread / run function !
    if (m_thread && m_thread->running() &&
        (pthread_self() != m_thread->threadID()) )
    {
	stop();
    }
    emit sigClosed(this, true);
}

//***************************************************************************
PluginManager &KwavePlugin::manager()
{
    return m_context.manager;
}

//***************************************************************************
QWidget *KwavePlugin::parentWidget()
{
    return &(m_context.top_widget);
}

//***************************************************************************
QString KwavePlugin::signalName()
{
    return (m_context.top_widget.signalName());
}

//***************************************************************************
unsigned int KwavePlugin::signalLength()
{
    return manager().signalLength();
}

//***************************************************************************
unsigned int KwavePlugin::signalRate()
{
    return manager().signalRate();
}

//***************************************************************************
const QArray<unsigned int> KwavePlugin::selectedTracks()
{
    return manager().selectedTracks();
}

//***************************************************************************
unsigned int KwavePlugin::selection(unsigned int *left, unsigned int *right,
                                    bool expand_if_empty)
{
    int l = manager().selectionStart();
    int r = manager().selectionEnd();

    // expand to the whole signal if left==right and expand_if_empty is set
    if ((l == r) && (expand_if_empty)) {
	l = 0;
	r = manager().signalLength()-1;
    }

    if (left)  *left  = l;
    if (right) *right = r;
    return r-l+1;
}

//***************************************************************************
QBitmap *KwavePlugin::overview(unsigned int width, unsigned int height,
                               unsigned int offset, unsigned int length)
{
    return manager().overview(width, height, offset, length);
}

//***************************************************************************
void KwavePlugin::yield()
{
    pthread_testcancel();
    sched_yield();
}

//***************************************************************************
void *KwavePlugin::handle()
{
    return m_context.handle;
}

//***************************************************************************
QString KwavePlugin::zoom2string(double percent)
{
    QString result = "";

    if (percent < 1.0) {
	int digits = (int)ceil(1.0 - log10(percent));
	QString format;
	format = "%0."+format.setNum(digits)+"f %%";
	result = format.sprintf(format, percent);
    } else if (percent < 10.0) {
	result = result.sprintf("%0.1f %%", percent);
    } else if (percent < 1000.0) {
	result = result.sprintf("%0.0f %%", percent);
    } else {
	result = result.sprintf("x %d", (int)rint(percent / 100.0));
    }
    return result;
}

//***************************************************************************
QString KwavePlugin::ms2string(double ms)
{
    char buf[128];
    int bufsize = 128;

    if (ms < 1.0) {
	char format[128];
	// limit to 6 digits, use 0.0 for exact zero
	int digits = (ms != 0.0) ? (int)ceil(1.0 - log10(ms)) : 1;
	if ( (digits < 0) || (digits > 6)) digits = 6;

	snprintf(format, sizeof(format), "%%0.%df ms", digits);
	snprintf(buf, bufsize, format, ms);
    } else if (ms < 1000.0) {
	snprintf(buf, bufsize, "%0.1f ms", ms);
    } else {
	int s = (int)round(ms / 1000.0);
	int m = (int)floor(s / 60.0);
	
	if (m < 1) {
	    char format[128];
	    int digits = (int)ceil(7.0 - log10(ms));
	    snprintf(format, sizeof(format), "%%0.%df s", digits);
	    snprintf(buf, bufsize, format, ms / 1000.0);
	} else {
	    snprintf(buf, bufsize, "%02d:%02d min", m, s % 60);
	}
    }

    QString result(buf);
    return result;
}

//***************************************************************************
void KwavePlugin::emitCommand(const QString &command)
{
    manager().enqueueCommand(command);
}

//***************************************************************************
//***************************************************************************
/* end of libgui/KwavePlugin.cpp */

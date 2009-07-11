/*************************************************************************
      ReversePlugin.cpp  -  reverses the current selection
                             -------------------
    begin                : Tue Jun 09 2009
    copyright            : (C) 2009 by Thomas Eschenbacher
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
#include <math.h>
#include <sched.h>

#include <klocale.h> // for the i18n macro
#include <threadweaver/Job.h>
#include <threadweaver/ThreadWeaver.h>
#include <threadweaver/DebuggingAids.h>

#include <QList>
#include <QStringList>

#include "libkwave/MultiTrackReader.h"
#include "libkwave/MultiTrackWriter.h"
#include "libkwave/PluginManager.h"
#include "libkwave/SampleWriter.h"
#include "libkwave/SignalManager.h"
#include "libkwave/undo/UndoTransactionGuard.h"

#include "libgui/SelectTimeWidget.h" // for selection mode

#include "ReversePlugin.h"
#include "UndoReverseAction.h"

KWAVE_PLUGIN(ReversePlugin,"reverse","2.1","Thomas Eschenbacher");

//***************************************************************************
class ReverseJob: public ThreadWeaver::Job
{
public:

    /**
     * Constructor
     */
    ReverseJob(
	SignalManager &manager, unsigned int track,
	unsigned int first, unsigned int last, unsigned int block_size,
	SampleReader *src_a, SampleReader *src_b
    );

    /** Destructor */
    virtual ~ReverseJob();

    /**
    * overloaded 'run' function that runns goOn() in the context
    * of the worker thread.
    */
    virtual void run();

private:

    /** reverses the content of an array of samples */
    void reverse(Kwave::SampleArray &buffer);

private:

    /** signal manager, for opening a sample writer */
    SignalManager &m_manager;

    /** index of the track */
    unsigned int m_track;

    /** first sample (from start) */
    unsigned int m_first;

    /** last sample (from end) */
    unsigned int m_last;

    /** block size in samples */
    unsigned int m_block_size;

    /** reader for start of signal */
    SampleReader *m_src_a;

    /** reader for end of signal */
    SampleReader *m_src_b;

};

//***************************************************************************
ReverseJob::ReverseJob(
    SignalManager &manager, unsigned int track,
    unsigned int first, unsigned int last, unsigned int block_size,
    SampleReader *src_a, SampleReader *src_b)
    :ThreadWeaver::Job(),
     m_manager(manager), m_track(track),
     m_first(first), m_last(last), m_block_size(block_size),
     m_src_a(src_a), m_src_b(src_b)
{
}

//***************************************************************************
ReverseJob::~ReverseJob()
{
    int i = 0;
    while (!isFinished()) {
	qDebug("job %p waiting... #%u", static_cast<void *>(this), i++);
	sched_yield();
    }
    Q_ASSERT(isFinished());
}

//***************************************************************************
void ReverseJob::reverse(Kwave::SampleArray &buffer)
{
    unsigned int count = buffer.size() >> 1;
    if (count <= 1) return;

    sample_t *a = buffer.data();
    sample_t *b = buffer.data() + (buffer.size() - 1);
    while (count--) {
	register sample_t h = *a;
	*a++ = *b;
	*b-- = h;
    }
}

//***************************************************************************
void ReverseJob::run()
{
    unsigned int start_a = m_first;
    unsigned int start_b = m_last - m_block_size;

    if (start_a + m_block_size < start_b) {
	// read from start
	Kwave::SampleArray buffer_a;
	buffer_a.resize(m_block_size);
	*m_src_a >> buffer_a;

	// read from end
	Kwave::SampleArray buffer_b;
	buffer_b.resize(m_block_size);
	m_src_b->seek(start_b);
	*m_src_b >> buffer_b;

	// swap the contents
	reverse(buffer_a);
	reverse(buffer_b);

	// write back buffer from the end at the start
	SampleWriter *dst_a = m_manager.openSampleWriter(
	    m_track, Overwrite,
	    start_a, start_a + m_block_size - 1,
	    false);
	Q_ASSERT(dst_a);
	*dst_a << buffer_b;
	dst_a->flush();
	delete dst_a;

	// write back buffer from the start at the end
	SampleWriter *dst_b = m_manager.openSampleWriter(
	    m_track, Overwrite,
	    start_b, start_b + m_block_size - 1,
	    false);
	Q_ASSERT(dst_b);
	*dst_b << buffer_a << flush;
	delete dst_b;
    } else {
	// single buffer with last block
	Kwave::SampleArray buffer;
	buffer.resize(m_last - m_first + 1);

	// read from start
	*m_src_a >> buffer;

	// swap content
	reverse(buffer);

	// write back
	SampleWriter *dst = m_manager.openSampleWriter(
	    m_track, Overwrite, m_first, m_last,
	    false);
	(*dst) << buffer << flush;
	delete dst;
    }

}

//***************************************************************************
//***************************************************************************
ReversePlugin::ReversePlugin(const PluginContext &context)
    :Kwave::Plugin(context)
{
     i18n("reverse");
}

//***************************************************************************
ReversePlugin::~ReversePlugin()
{
}

//***************************************************************************
void ReversePlugin::run(QStringList params)
{
    UndoTransactionGuard *undo_guard = 0;

    if ((params.count() != 1) || (params.first() != "noundo")) {
	// undo is enabled, create a undo guard
	undo_guard = new UndoTransactionGuard(*this, i18n("reverse"));
	if (!undo_guard) return;

	// try to save undo information
	UndoAction *undo = new UndoReverseAction(manager());
	if (!undo_guard->registerUndoAction(undo)) {
	    delete undo_guard;
	    return;
	}
	undo->store(signalManager());
    }

    // get the current selection
    QList<unsigned int> tracks;
    unsigned int first = 0;
    unsigned int last  = 0;
    unsigned int length = selection(&tracks, &first, &last, true);
    if (!length || tracks.isEmpty()) {
	if (undo_guard) delete undo_guard;
	return;
    }

    // get the list of affected tracks
    if (tracks.isEmpty()) {
	if (undo_guard) delete undo_guard;
	return;
    }

    MultiTrackReader source_a(Kwave::SinglePassForward,
	signalManager(), tracks, first, last);
    MultiTrackReader source_b(Kwave::SinglePassReverse,
	signalManager(), tracks, first, last);

    // break if aborted
    if (!source_a.tracks() || !source_b.tracks()) {
	if (undo_guard) delete undo_guard;
	return;
    }

    // connect the progress dialog
    connect(&source_a, SIGNAL(progress(unsigned int)),
	    this,      SLOT(updateProgress(unsigned int)),
	    Qt::BlockingQueuedConnection);

    // get the buffers for exchanging the data
    const unsigned int block_size = 5 * source_a.blockSize();
    Kwave::SampleArray buffer_a(block_size);
    Kwave::SampleArray buffer_b(block_size);
    Q_ASSERT(buffer_a.size() == block_size);
    Q_ASSERT(buffer_b.size() == block_size);

    ThreadWeaver::Weaver weaver;
    QList<ThreadWeaver::Job *> joblist;

    // loop over the sample range
    while ((first < last) && !shouldStop()) {

	weaver.suspend();

	// loop over all tracks
	for (int track = 0; track < tracks.count(); track++) {

	    ReverseJob *job = new ReverseJob(
		signalManager(), track, first, last, block_size,
		source_a[track], source_b[track]
	    );

	    if (!job) continue;
	    joblist.append(job);

	    // put the job into the thread weaver
	    weaver.enqueue(job);
	}
	weaver.resume();

	if (!joblist.isEmpty()) {
	    weaver.finish();
	    Q_ASSERT(weaver.isEmpty());
	    qDeleteAll(joblist);
	    joblist.clear();
	}

	// next positions
	first += block_size;
	last  = (last > block_size) ? (last - block_size) : 0;
    }

    close();
    if (undo_guard) delete undo_guard;
}

//***************************************************************************
void ReversePlugin::updateProgress(unsigned int progress)
{
    Kwave::Plugin::updateProgress(progress + progress);
}

//***************************************************************************
#include "ReversePlugin.moc"
//***************************************************************************
//***************************************************************************

/***************************************************************************
              Track.cpp  -  collects one or more stripes in one track
			     -------------------
    begin                : Feb 10 2001
    copyright            : (C) 2001 by Thomas Eschenbacher
    email                : Thomas Eschenbacher <thomas.eschenbacher@gmx.de>
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "mt/SharedLockGuard.h"
#include "mt/MutexSet.h"

#include "libkwave/SampleReader.h"
#include "libkwave/SampleWriter.h"
#include "libkwave/Stripe.h"
#include "libkwave/Track.h"

//***************************************************************************
Track::Track()
    :m_lock(), m_stripes(), m_selected(true)
{
    m_stripes.setAutoDelete(true);
}

//***************************************************************************
Track::Track(unsigned int length)
    :m_lock(), m_stripes(), m_selected(true)
{
    m_stripes.setAutoDelete(true);
    appendStripe(length);
}

//***************************************************************************
Track::~Track()
{
    SharedLockGuard lock(m_lock, true);

    while (m_stripes.count()) {
	m_stripes.remove(m_stripes.last());
    }
}

//***************************************************************************
Stripe *Track::appendStripe(unsigned int length)
{
    SharedLockGuard lock(m_lock, true);

    unsigned int last = unlockedLength();
    Stripe *s = new Stripe(last, 0);
    ASSERT(s);
    if (s) {
	m_stripes.append(s);
	connect(s, SIGNAL(sigSamplesDeleted(Stripe&, unsigned int,
	    unsigned int)),
	    this, SLOT(slotSamplesDeleted(Stripe&, unsigned int,
	    unsigned int)));
	connect(s, SIGNAL(sigSamplesInserted(Stripe&, unsigned int,
	    unsigned int)),
	    this, SLOT( slotSamplesInserted(Stripe&, unsigned int,
	    unsigned int)));
	connect(s, SIGNAL(sigSamplesModified(Stripe&, unsigned int,
	    unsigned int)),
	    this, SLOT(slotSamplesModified(Stripe&, unsigned int,
	    unsigned int)));

	s->resize(length);
    }
//    debug("Track::appendStripe(%d): new stripe at %p", length, s);

    return s;
}

//***************************************************************************
unsigned int Track::length()
{
    SharedLockGuard lock(m_lock, false);
    return unlockedLength();
}

//***************************************************************************
unsigned int Track::unlockedLength()
{
    unsigned int len = 0;
    Stripe *s = m_stripes.last();
    if (s) len = s->start() + s->length();
    return len;
}

//***************************************************************************
SampleWriter *Track::openSampleWriter(InsertMode mode,
	unsigned int left, unsigned int right)
{
    SharedLockGuard lock(m_lock, false);
    QList<Stripe> stripes;
    SampleLock * range_lock = 0;

    switch (mode) {
	case Insert: {
//	    debug("Track::openSampleWriter(insert, %u)", left);
	
	    // find the stripe into which we insert
	    Stripe *target_stripe = 0;
	    Stripe *stripe_before = 0;
	
	    // add all stripes within the specified range to the list
	    QListIterator<Stripe> it(m_stripes);
	    for (; it.current(); ++it) {
		Stripe *s = it.current();
		unsigned int st = s->start();
		unsigned int len = s->length();
		if (!len) continue; // skip zero-length tracks
		
		if (left >= st+len) stripe_before = s;
		
		if ((left >= st) && (left < st+len)) {
		    // match found
//		    debug("Track::openSampleWriter(): found matching range");
		    target_stripe = s;
		    break;
		}
	    }
	
	    // if insert is requested immediately after the last
	    // stripe before
	    if (stripe_before && (left == stripe_before->start()+
	        stripe_before->length()))
	    {
		// append to the existing stripe
//		debug("Track::openSampleWriter(): appending to existing stripe");
		mode = Append;
		target_stripe = stripe_before;
	    }
	
	    // if no stripe was found, create a new one and
	    // insert it between the existing ones
	    if (!target_stripe) {
		debug("Track::openSampleWriter(): creating a new stripe");
		Stripe *target_Stripe = new Stripe(left);
		ASSERT(target_Stripe);
		if (!target_stripe) return 0;
		
		// insert into our stripes, if the stripe before
		// is null, the new one will be prepended
		int index = m_stripes.find(stripe_before) + 1;
		debug("Track::openSampleWriter(): inserting at index %d", index);
		m_stripes.insert(index, target_stripe);
	    }
	
	    // append it to our stripes list and lock it
	    range_lock = new SampleLock(*this, left, 0,
		SampleLock::WriteShared);
		
	    // insert the stripe into the list of affected stripes,
	    // it will be the only one
	    stripes.append(target_stripe);
	
	    break;
	}
	case Append: {
	    // create a new stripe
	    unsigned int next_start = unlockedLength();
	    Stripe *s = new Stripe(next_start);
	    ASSERT(s);
	    if (!s) return 0;
	
	    // append it to our stripes list and lock it
	    range_lock = new SampleLock(*this, next_start, 0,
		SampleLock::WriteShared);
	
	    // use new created stripe as start
	    stripes.append(s);
	    break;
	}
	case Overwrite:
	    if ((right == 0) || (right == left)) right = unlockedLength()-1;
	
	    // lock the needed range for shared writing
	    range_lock = new SampleLock(*this, left, right-left+1,
		SampleLock::WriteShared);
	
	    // add all stripes within the specified range to the list
	    QListIterator<Stripe> it(m_stripes);
	    for (; it.current(); ++it) {
		Stripe *s = it.current();
		unsigned int st = s->start();
		unsigned int len = s->length();
		if (!len) continue; // skip zero-length tracks

		if (st > right) break; // ok, end reached
		if (st+len-1 >= left) {
		    // overlaps -> include to our list
		    stripes.append(s);
		}
	    }
	
	    break;
    }

    // no lock yet, that's not good...
    ASSERT(range_lock);
    if (!range_lock) return 0;

    // create the input stream
    SampleWriter *stream = new SampleWriter(*this, stripes,
	range_lock, mode, left, right);
    ASSERT(stream);
    if (stream) return stream;

    // stream creation failed, clean up
    if (range_lock) delete range_lock;
    return 0;
}

//***************************************************************************
SampleReader *Track::openSampleReader(unsigned int left,
	unsigned int right)
{
    SharedLockGuard lock(m_lock, false);
    QList<Stripe> stripes;

    // lock the needed range for shared writing
    SampleLock *range_lock = new SampleLock(*this, left, right-left+1,
	SampleLock::ReadShared);

    // add all stripes within the specified range to the list
    QListIterator<Stripe> it(m_stripes);
    for (; it.current(); ++it) {
	Stripe *s = it.current();
	unsigned int st = s->start();
	unsigned int len = s->length();
	if (!len) continue; // skip zero-length tracks
	
	if (st > right) break; // ok, end reached
	if (st+len-1 >= left) {
	    // overlaps -> include to our list
	    stripes.append(s);
	}
    }

    // no lock yet, that's not good...
    ASSERT(range_lock);
    if (!range_lock) return 0;

    // create the input stream
    SampleReader *stream = new SampleReader(*this, stripes,
	range_lock, left, right);
    ASSERT(stream);
    if (stream) return stream;

    // stream creation failed, clean up
    if (range_lock) delete range_lock;
    return 0;
}

//***************************************************************************
void Track::deleteRange(unsigned int offset, unsigned int length)
{
    if (!length) return;

    {
	SharedLockGuard lock(m_lock, false);
	
	// lock the needed range for exclusive writing
	SampleLock range_lock(*this, offset, length,
	    SampleLock::WriteExclusive);
	
	// add all stripes within the specified range to the list
	QListIterator<Stripe> it(m_stripes);
	for (it.toLast(); it.current(); --it) {
	    Stripe *s = it.current();
	    unsigned int st = s->start();
	    unsigned int len = s->length();
	
	    if ((st+len >= offset) && (st < offset+length)) {
		// stripe overlaps
		s->deleteRange(offset, length);
		if (!s->length()) {
		    // stripe now is empty -> remove it
		    m_stripes.remove(s);
		}
	    }
	}
	
    }

    emit sigSamplesDeleted(*this, offset, length);
}

//***************************************************************************
void Track::slotSamplesInserted(Stripe &src, unsigned int offset,
                                unsigned int length)
{
    emit sigSamplesInserted(*this, src.start()+offset, length);
}

//***************************************************************************
void Track::slotSamplesDeleted(Stripe &src, unsigned int offset,
                               unsigned int length)
{
    emit sigSamplesDeleted(*this, src.start()+offset, length);
}

//***************************************************************************
void Track::slotSamplesModified(Stripe &src, unsigned int offset,
                                unsigned int length)
{
    emit sigSamplesModified(*this, src.start()+offset, length);
}

//***************************************************************************
//***************************************************************************

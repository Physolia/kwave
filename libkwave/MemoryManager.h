/***************************************************************************
        MemoryManager.h  -  Manager for virtual and physical memory
			     -------------------
    begin                : Wed Aug 08 2001
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

#ifndef MEMORY_MANAGER_H
#define MEMORY_MANAGER_H

#include "config.h"
#include <stddef.h>  // for size_t

#include <QtGlobal>
#include <QDir>
#include <QHash>
#include <QMutex>
#include <QString>

#include "libkwave/LRU_Cache.h"

namespace Kwave
{
    class SwapFile;

    /** handle for memory manager */
    typedef int Handle;

    class MemoryManager
    {
    public:
	/** Constructor */
	MemoryManager();

	/** Destructor */
	virtual ~MemoryManager();

	/** Closes the memory manager and does cleanups at program shutdown */
	void close();

	/**
	 * Gets a block of memory, either in physical memory or in a swap file
	 * if there is not enough physical memory. If there is not enough
	 * memory at all, the return value will be a null pointer.
	 *
	 * @param size number of bytes to allocate
	 * @return handle of a storage object, to be used to be mapped
	 *         into physical memory through map() or zero if out of memory
	 */
	Kwave::Handle allocate(size_t size) Q_DECL_EXPORT;

	/**
	 * Resizes a block of memory to a new size. If the block will no longer
	 * fit in physical memory, the block will be swapped out to a page file.
	 *
	 * @param handle handle of the existing block
	 * @param size new size of the block in bytes
	 * @return true if successful, false if out of memory or if the
	 *         block is currently in use
	 */
	bool resize(Kwave::Handle handle, size_t size) Q_DECL_EXPORT;

	/**
	 * Returns the allocated size of the block
	 * @note this may be more than allocated, can be rounded up
	 */
	size_t sizeOf(Kwave::Handle handle) Q_DECL_EXPORT;

	/**
	 * Frees a block of memory that has been previously allocated with the
	 * allocate() function.
	 *
	 * @param handle reference to the handle to the block to be freed. The
	 *               handle will be set to zero afterwards.
	 */
	void free(Kwave::Handle &handle) Q_DECL_EXPORT;

	/**
	 * Sets the limit of memory that can be used for undo/redo.
	 * @param mb number of whole megabytes of the limit
	 */
	void setUndoLimit(quint64 mb) Q_DECL_EXPORT;

	/**
	 * Returns the limit of memory that can be used for undo/redo
	 * in units of whole megabytes
	 */
	quint64 undoLimit() const Q_DECL_EXPORT;

	/**
	 * Returns the global instance of the memory manager from the
	 * KwaveApp.
	 */
	static MemoryManager &instance() Q_DECL_EXPORT;

	/**
	 * Map a portion of memory and return the physical address.
	 *
	 * @param handle handle of the object that identifies the
	 *        memory block
	 * @return pointer to the mapped area or null if failed
	 */
	void *map(Kwave::Handle handle) Q_DECL_EXPORT;

	/**
	 * Unmap a memory area, previously mapped with map()
	 *
	 * @param handle handle of a mapped block mapped with map()
	 */
	void unmap(Kwave::Handle handle) Q_DECL_EXPORT;

	/**
	 * Read from a memory block into a buffer
	 *
	 * @param handle handle of the object that identifies the
	 *        memory block
	 * @param offset offset within the object [bytes]
	 * @param buffer pointer to a buffer that is to be filled
	 * @param length number of bytes to read
	 * @return number of read bytes or < 0 if failed
	 */
	int readFrom(Kwave::Handle handle, unsigned int offset,
	             void *buffer, unsigned int length) Q_DECL_EXPORT;

	/**
	 * Write a buffer into a memory block
	 *
	 * @param handle handle of the object that identifies the
	 *        memory block
	 * @param offset offset within the object [bytes]
	 * @param buffer pointer to a buffer that is to be written
	 * @param length number of bytes to write
	 * @return number of written bytes or < 0 if failed
	 */
	int writeTo(Kwave::Handle handle, unsigned int offset,
	            const void *buffer, unsigned int length) Q_DECL_EXPORT;

#ifdef DEBUG_MEMORY
	/** structure used for statistics */
	typedef struct {
	    /** physical memory */
	    struct {
		quint64 handles; /**< used handles     */
		quint64 bytes;   /**< allocated bytes  */
		quint64 limit;   /**< maximum allowed  */
		quint64 allocs;  /**< number of allocs */
		quint64 frees;   /**< number of frees  */
	    } physical;
	} statistic_t;
#endif

    protected:

	/** returns the currently allocated physical memory */
	quint64 physicalUsed();

    private:

	/**
	 * get a new handle.
	 * @note the handle does not need to be freed later
	 * @return a non-zero handle or zero if all handles are in use
	 */
	Kwave::Handle newHandle();

	/** dump current state (for debugging) */
	void dump(const char *function);

    private:

	typedef struct physical_memory_t {
	    void  *m_data;     /**< pointer to the physical memory */
	    size_t m_size;     /**< size of the block */
	    int    m_mapcount; /**< counter for mmap */
	} physical_memory_t;

    private:

	/** limit of memory available for undo/redo */
	quint64 m_undo_limit;

	/** map of objects in physical memory */
	Kwave::LRU_Cache<Kwave::Handle, physical_memory_t> m_physical;

	/** mutex for ensuring exclusive access */
	QMutex m_lock;

	/** last used handle */
	static Kwave::Handle m_last_handle;

#ifdef DEBUG_MEMORY
	/** statistics, for debugging */
	statistic_t m_stats;
#endif /* DEBUG_MEMORY */
    };

}

#endif /* MEMORY_MANAGER_H */

//***************************************************************************
//***************************************************************************

/*************************************************************************
         RecordDevice.h  -  device for audio recording, currently only OSS
                             -------------------
    begin                : Wed Sep 17 2003
    copyright            : (C) 2003 by Thomas Eschenbacher
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

#ifndef _RECORD_DEVICE_H_
#define _RECORD_DEVICE_H_

#include "config.h"
#include <qvaluelist.h>
class QString;

class RecordDevice
{
public:

    /** Constructor */
    RecordDevice();

    /** Destructor */
    virtual ~RecordDevice();

    /**
     * Open the record device.
     * @param dev path of the record device
     * @return file descriptor >= 0 or negative error code if failed
     */
    virtual int open(const QString &dev);

    /**
     * Read the raw audio data from the record device.
     * @param buffer pointer to the array of bytes to receive the audio data
     * @param length size of the buffer
     * @return number of bytes read, zero or negative if failed
     */
    virtual int read(char *buffer, unsigned int length);


    /** Close the device */
    virtual int close();

    /**
     * Detect the minimum and maximum number of tracks.
     * If the detection fails, minimum and maximum are set to zero.
     * @param min receives the lowest supported number of tracks
     * @param max receives the highest supported number of tracks
     * @return zero or positive number if ok, negative error number if failed
     */
    virtual int detectTracks(unsigned int &min, unsigned int &max);

    /**
     * Try to set a new number of tracks.
     * @note the device must be open
     * @param tracks the number of tracks to be set, can be modified and
     *        decreased to the next supported number of tracks if the
     *        underlying driver supports that.
     * @return zero on success, negative error code if failed
     */
    virtual int setTracks(unsigned int &tracks);

    /** Returns the current number of tracks */
    virtual int tracks();

    /** get a list of supported sample rates */
    virtual QValueList<double> detectSampleRates();

    /**
     * Try to set a new sample rate.
     * @param new_rate the sample rate to be set [samples/second], can
     *        be modified and rounded up/down to the nearest supported
     *        sample rate if the underlying driver supports that.
     * @return zero on success, negative error code if failed
     */
    virtual int setSampleRate(double &new_rate);

    /** Returns the current sample rate of the device */
    virtual double sampleRate();

    /**
     * Gets a list of supported compression types. If no compression is
     * supported, the list might be empty.
     */
    virtual QValueList<int> detectCompressions();

    /**
     * Try to set a new compression type.
     * @param new_compression the identifier of the new compression
     * @return zero on success, negative error code if failed
     * @see class CompressionType
     */
    virtual int setCompression(int new_compression);

    /** Returns the current compression type (0==none) */
    virtual int compression();

    /**
     * Detect a list of supported bits per sample.
     * @note this depends on the compression type
     * @return a list of bits per sample, empty if failed
     */
    virtual QValueList <unsigned int> detectBitsPerSample();

    /**
     * Set the resolution in bits per sample
     * @param new_bits resolution [bits/sample]
     */
    virtual int setBitsPerSample(unsigned int new_bits);

    /**
     * Returns the current resolution in bits per sample or a negative
     * error code if failed
     */
    virtual int bitsPerSample();

    /**
     * Gets a list of supported sample formats.
     * @note this depends on the current setting of the compression!
     */
    virtual QValueList<int> detectSampleFormats();

    /**
     * Try to set a new sample format (signed/unsigned)
     * @param new_format the identifier for the new format
     * @return zero on success, negative error code if failed
     * @see class SampleFormat
     */
    virtual int setSampleFormat(int new_format);

    /** Returns the current sample format (signed/unsigned) */
    virtual int sampleFormat();

private:

    /**
     * split a device format bitmask into it's parameters.
     * @param format the device specific format
     * @param compression receives a compression type
     * @see CompressionType
     * @param bits receives the number of bits per sample, related
     *        to the decoded stream
     * @param sample_format receives the sample format, as defined in
     *        libaudiofile (signed or unsigned)
     */
    void format2mode(int format, int &compression,
                     int &bits, int &sample_format);

    /**
     * create a device format bitmask from it's parameters.
     * @param compression the compression type
     * @see CompressionType
     * @param bits the number of bits per sample, related
     *        to the decoded stream
     * @param sample_format the sample format, as defined in
     *        libaudiofile (signed or unsigned)
     * @return the device specific format
     */
    int mode2format(int compression, int bits, int sample_format);

private:

    /** file descriptor of the device or -1 if not open */
    int m_fd;

};

#endif /* _RECORD_DEVICE_H_ */

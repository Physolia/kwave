// SPDX-FileCopyrightText: 2007 Thomas Eschenbacher <Thomas.Eschenbacher@gmx.de>
// SPDX-FileCopyrightText: 2024 Mark Penner <mrp@markpenner.space>
// SPDX-License-Identifier: GPL-2.0-or-later
/***************************************************************************
   SaveBlocksPlugin.cpp  -  Plugin for saving blocks between labels
                             -------------------
    begin                : Thu Mar 01 2007
    copyright            : (C) 2007 by Thomas Eschenbacher
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

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QRegularExpression>
#include <QStringList>

#include <KLocalizedString>

#include "libkwave/CodecManager.h"
#include "libkwave/Encoder.h"
#include "libkwave/FileInfo.h"
#include "libkwave/Label.h"
#include "libkwave/LabelList.h"
#include "libkwave/MessageBox.h"
#include "libkwave/MetaDataList.h"
#include "libkwave/Parser.h"
#include "libkwave/SignalManager.h"
#include "libkwave/String.h"
#include "libkwave/Utils.h"

#include "SaveBlocksDialog.h"
#include "SaveBlocksPlugin.h"

using namespace Qt::StringLiterals;

KWAVE_PLUGIN(saveblocks, SaveBlocksPlugin)

//***************************************************************************
Kwave::SaveBlocksPlugin::SaveBlocksPlugin(QObject *parent,
                                          const QVariantList &args)
    :Kwave::Plugin(parent, args),
     m_url(), m_pattern(), m_numbering_mode(CONTINUE),
     m_selection_only(true), m_block_info()
{
}

//***************************************************************************
Kwave::SaveBlocksPlugin::~SaveBlocksPlugin()
{
}

//***************************************************************************
QStringList *Kwave::SaveBlocksPlugin::setup(QStringList &previous_params)
{
    // try to interpret the previous parameters
    interpreteParameters(previous_params);

    // create the setup dialog
    sample_index_t selection_left  = 0;
    sample_index_t selection_right = 0;
    selection(nullptr, &selection_left, &selection_right, false);

    // enable the "selection only" checkbox only if there is something
    // selected but not everything
    bool selected_something = (selection_left != selection_right);
    bool selected_all = ((selection_left == 0) &&
                         (selection_right + 1 >= signalLength()));
    bool enable_selection_only = selected_something && !selected_all;

    QString base = findBase(m_url.path(), m_pattern);
    scanBlocksToSave(base, m_selection_only && enable_selection_only);

    QUrl signalname = Kwave::URLfromUserInput(signalName());

    Kwave::SaveBlocksDialog *dialog = new Kwave::SaveBlocksDialog(
            parentWidget(),
            signalname,
            m_pattern,
            m_numbering_mode,
            m_selection_only,
            enable_selection_only
        );
        if (!dialog) return nullptr;

    // connect the signals/slots from the plugin and the dialog
    connect(dialog, &SaveBlocksDialog::sigSelectionChanged,
        this, &SaveBlocksPlugin::updateExample);
    connect(this, &SaveBlocksPlugin::sigNewExample,
        dialog, &SaveBlocksDialog::setNewExample);

    dialog->setWindowTitle(description());
    dialog->emitUpdate();
    if ((dialog->exec() != QDialog::Accepted) || !dialog) {
        return nullptr;
    }

    QUrl url = dialog->selectedUrl();
    if (url.isEmpty()) {
        return nullptr;
    }
    QString name = url.path();

    QString filename = signalname.fileName();
    QFileInfo path(filename);

    // replace the current extension with the selected extension
    QString old_ext = path.suffix();
    if (old_ext.length()) filename.remove(old_ext);
    QString ext = Kwave::Parser::escape(dialog->extension());
    filename += ext.mid(1);
    name += u"/"_s + filename;

    name = Kwave::Parser::escape(name);
    QString pattern = Kwave::Parser::escape(dialog->pattern());
    int mode = static_cast<int>(dialog->numberingMode());
    bool selection_only = (enable_selection_only) ?
        dialog->selectionOnly() : m_selection_only;

    QStringList *list = new(std::nothrow) QStringList();
    Q_ASSERT(list);
    if (!list) {
        return nullptr;
    }
    *list << name;
    *list << pattern;
    *list << QString::number(mode);
    *list << QString::number(selection_only);

    emitCommand(u"plugin:execute(saveblocks,"_s +
        name + u","_s + pattern + u","_s +
        QString::number(mode) + u","_s +
        QString::number(selection_only) + u")"_s
    );

    return list;
}

//***************************************************************************
QString Kwave::SaveBlocksPlugin::createDisplayList(
    const QStringList &list,
    unsigned int max_entries) const
{
    if (!max_entries || list.isEmpty()) return QString();

    QString retval;
    unsigned int count = 0;

    foreach (const QString &entry, list) {
        if (count == 0) // first entry
            retval = _("<br><br>");
        if (count < max_entries)
            retval += entry + _("<br>");
        else if (count == max_entries)
            retval += i18n("...") + _("<br>");

        if (++count > max_entries)
            break;
    }

    return retval;
}

//***************************************************************************
int Kwave::SaveBlocksPlugin::start(QStringList &params)
{
    qDebug("SaveBlocksPlugin::start()");

    // interpret the parameters
    int result = interpreteParameters(params);
    if (result) return result;

    QString filename = m_url.path();
    QFileInfo file(filename);
    QString path = file.absolutePath();
    QString ext  = file.suffix();
    QString base = findBase(filename, m_pattern);
    QByteArray sep("/");

    // determine the selection settings
    sample_index_t selection_left  = 0;
    sample_index_t selection_right = 0;
    selection(nullptr, &selection_left, &selection_right, false);

    bool selected_something = (selection_left != selection_right);
    bool selected_all = ( (selection_left == 0) &&
                          ((selection_right + 1) >= signalLength()) );
    bool enable_selection_only = selected_something && !selected_all;
    bool selection_only = enable_selection_only && m_selection_only;
    if (!selection_only) {
        selection_left  = 0;
        selection_right = signalLength() - 1;
    }

    // get the index range
    scanBlocksToSave(base, selection_only);
    unsigned int count = static_cast<unsigned int>(m_block_info.count());
    unsigned int first = firstIndex(path, base, ext, m_pattern,
                                    m_numbering_mode, count);

//     qDebug("m_url            = '%s'", m_url.toDisplayString().toLocal8Bit().data());
//     qDebug("m_pattern        = '%s'", m_pattern.toLocal8Bit().data());
//     qDebug("m_numbering_mode = %d", (int)m_numbering_mode);
//     qDebug("selection_only   = %d", selection_only);
//     qDebug("indices          = %u...%u (count=%u)", first,
//             first + count - 1, count);

    // remember the original file info and determine the list of unsupported
    // properties, we need that later to avoid that the signal manager
    // complains on saving each and every block, again and again...
    const Kwave::FileInfo orig_file_info(signalManager().metaData());
    Kwave::FileInfo file_info(orig_file_info);
    QList<Kwave::FileProperty> unsupported_properties;
    {
        QString mimetype = Kwave::CodecManager::mimeTypeOf(m_url);
        Kwave::Encoder *encoder = Kwave::CodecManager::encoder(mimetype);
        if (encoder) {
            unsupported_properties = encoder->unsupportedProperties(
                file_info.properties().keys());
            delete encoder;
        }
    }

    // iterate over all blocks to check for overwritten files and missing dirs
    QStringList  overwritten_files;
    QStringList  missing_dirs;
    for (unsigned int i = first; i < (first + count); i++) {
        QString name = createFileName(base, ext, m_pattern, i, count,
                                      first + count - 1);
        QString display_name = Kwave::Parser::unescape(name);

        // split the name into directory and file name
        name = QString::fromLatin1(QUrl::toPercentEncoding(display_name, sep));
        QUrl url = m_url.adjusted(QUrl::RemoveFilename);
        url.setPath(url.path(QUrl::FullyEncoded) + name, QUrl::StrictMode);

        QString p = url.path();
        QFileInfo fi(p);

        // check for potentially overwritten file
        if (fi.exists())
            overwritten_files += Kwave::Parser::unescape(display_name);

        // check for missing subdirectory
        if (!fi.dir().exists()) {
            QString missing_dir = fi.absolutePath();
            if (!missing_dirs.contains(missing_dir))
                missing_dirs += missing_dir;
        }
    }

    // inform about overwritten files
    if (!overwritten_files.isEmpty()) {
        // ask the user for confirmation if he really wants to overwrite...
        if (Kwave::MessageBox::warningYesNo(parentWidget(),
            _("<html>") +
            i18n("This would overwrite the following file(s): %1" \
            "Do you really want to continue?",
            createDisplayList(overwritten_files, 5)) +
            _("</html>") ) != KMessageBox::PrimaryAction)
        {
            return -1;
        }
    }

    // handle missing directories
    if (!missing_dirs.isEmpty()) {
        // ask the user if he wants to continue and create the directory
        if (Kwave::MessageBox::warningContinueCancel(parentWidget(),
            i18n("The following directories do not exist: %1"
                 "Do you want to create them and continue?",
                 createDisplayList(missing_dirs, 5)),
            QString(),
            QString(),
            QString(),
            _("saveblocks_create_missing_dirs")
            ) != KMessageBox::Continue)
        {
            return -1;
        }

        // create all missing directories
        QUrl base_url = m_url.adjusted(QUrl::RemoveFilename);
        foreach (const QString &missing, missing_dirs) {
            QUrl url(base_url);
            url.setPath(
                QString::fromLatin1(QUrl::toPercentEncoding(missing)),
                QUrl::StrictMode
            );
            QString p = url.path();
            QDir dir;
            if (!dir.mkpath(p))
                qWarning("creating path '%s' failed", DBG(p));
        }
    }

    // save the current selection, we have to restore it afterwards!
    sample_index_t saved_selection_left  = 0;
    sample_index_t saved_selection_right = 0;
    selection(nullptr, &saved_selection_left, &saved_selection_right, false);

    // now we can loop over all blocks and save them
    sample_index_t block_start;
    sample_index_t block_end = 0;
    Kwave::LabelList labels(signalManager().metaData());
    Kwave::LabelListIterator it(labels);
    Kwave::Label label = it.hasNext() ? it.next() : Kwave::Label();

    for (unsigned int index = first;;) {
        block_start = block_end;
        block_end   = (label.isNull()) ? signalLength() : label.pos();

        if ((selection_left < block_end) && (selection_right > block_start)) {
            // found a block to save...
            Q_ASSERT(index < first + count);

            sample_index_t left  = block_start;
            sample_index_t right = block_end - 1;
            if (left  < selection_left)  left  = selection_left;
            if (right > selection_right) right = selection_right;
            Q_ASSERT(right > left);
            if (right <= left) break; // zero-length ?

            // select the range of samples
            selectRange(left, right - left + 1);

            // determine the filename
            QString name = createFileName(base, ext, m_pattern, index, count,
                                          first + count - 1);
            name = Kwave::Parser::unescape(name);
            // use URL encoding for the filename
            name = QString::fromLatin1(QUrl::toPercentEncoding(name, sep));
            QUrl url = m_url.adjusted(QUrl::RemoveFilename);
            url.setPath(url.path(QUrl::FullyEncoded) + name, QUrl::StrictMode);

            // enter the title of the block into the meta data if supported
            if (!unsupported_properties.contains(INF_NAME)) {
                QString title = orig_file_info.get(INF_NAME).toString();
                int idx = index - first;
                if ((idx >= 0) && (idx < m_block_info.count())) {
                    QString block_title = m_block_info[idx].m_title;
                    if (block_title.length())
                        title = title + _(", ") + block_title;
                }
                file_info.set(INF_NAME, QVariant(title));
                signalManager().metaData().replace(
                    Kwave::MetaDataList(file_info));
            }

            qDebug("saving %9lu...%9lu -> '%s'",
                   static_cast<unsigned long int>(left),
                   static_cast<unsigned long int>(right),
                   DBG(url.toDisplayString()));
            if (signalManager().save(url, true) < 0)
                break;

            // if there were unsupported properties, the user might have been
            // asked whether it is ok to continue or not. If he answered with
            // "Cancel", we do not reach this point, otherwise we can continue
            // and prevent any further annoying questions by removing all
            // unsupported file info before the next run...
            if ((index == first) && !unsupported_properties.isEmpty()) {
                foreach (const Kwave::FileProperty &p, unsupported_properties) {
                    file_info.set(p, QVariant());
                }
                signalManager().metaData().replace(
                    Kwave::MetaDataList(file_info));
            }

            // increment the index for the next filename
            index++;
        }
        if (label.isNull()) break;
        label = (it.hasNext()) ? it.next() : Kwave::Label();
    }

    // restore the original file info
    signalManager().metaData().replace(
        Kwave::MetaDataList(orig_file_info));

    // restore the previous selection
    selectRange(saved_selection_left,
        (saved_selection_left != saved_selection_right) ?
        (saved_selection_right - saved_selection_left + 1) : 0);

    return result;
}

//***************************************************************************
int Kwave::SaveBlocksPlugin::interpreteParameters(QStringList &params)
{
    bool ok;
    QString param;

    // evaluate the parameter list
    if (params.count() != 4) {
        return -EINVAL;
    }

    // the selected URL
    m_url = Kwave::URLfromUserInput(Kwave::Parser::unescape(params[0]));
    if (!m_url.isValid()) return -EINVAL;

    // filename pattern
    m_pattern = Kwave::Parser::unescape(params[1]);
    if (!m_pattern.length()) return -EINVAL;

    // numbering mode
    param = params[2];
    int mode = param.toInt(&ok);
    Q_ASSERT(ok);
    if (!ok) return -EINVAL;
    if ((mode != CONTINUE) &&
        (mode != START_AT_ONE)) return -EINVAL;
    m_numbering_mode = static_cast<numbering_mode_t>(mode);

    // flag: save only the selection
    param = params[3];
    m_selection_only = (param.toUInt(&ok) != 0);
    Q_ASSERT(ok);
    if (!ok) return -EINVAL;

    return 0;
}

//***************************************************************************
void Kwave::SaveBlocksPlugin::scanBlocksToSave(const QString &base,
                                               bool selection_only)
{
    sample_index_t selection_left, selection_right;

    sample_index_t block_start;
    sample_index_t block_end = 0;
    QString        block_title;
    Kwave::LabelList labels(signalManager().metaData());
    Kwave::LabelListIterator it(labels);
    Kwave::Label label = (it.hasNext()) ? it.next() : Kwave::Label();

    if (selection_only) {
        selection(nullptr, &selection_left, &selection_right, true);
    } else {
        selection_left  = 0;
        selection_right = signalLength() - 1;
    }

    // get the title of the whole file, in case that a block does not have
    // an own title
    FileInfo info(signalManager().metaData());
    QString file_title = info.get(INF_NAME).toString();

    // fallback: if there is no INF_NAME either, fall back to the file
    //           name as last resort
    if (!file_title.length()) file_title = base;

    m_block_info.clear();
    QString prev_title;
    for (;;) {
        block_start = block_end;
        block_end   = (label.isNull()) ? signalLength() : label.pos();
        block_title = prev_title;
        prev_title  = (label.isNull()) ? file_title     : label.name();

        if ((block_end > selection_left) && (block_start <= selection_right)) {
            BlockInfo block;
            block.m_start  = block_start;
            block.m_length = block_end - block_start;
            block.m_title  = block_title;
            if (!block.m_title.length()) block.m_title = file_title;
            m_block_info.append(block);
        }

        if (label.isNull()) break;
        label = (it.hasNext()) ? it.next() : Kwave::Label();
    }
}

//***************************************************************************
QString Kwave::SaveBlocksPlugin::createFileName(const QString &base,
    const QString &ext, const QString &pattern,
    unsigned int index, int count, int total)
{
    QString p = QRegularExpression::escape(pattern);
    QString nr;

    // format the "index" parameter
    QRegularExpression rx_nr(_("(\\\\\\[\\\\%(\\d*)nr\\\\\\])"),
                             QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatchIterator i = rx_nr.globalMatch(p);
    while (i.hasNext()) {
        QRegularExpressionMatch match = i.next();
        QString format = match.captured(1);
        format = format.mid(3, format.length() - 7);
        QString ex = _("(\\\\\\[\\\\") + format + _("nr\\\\\\])");
        QRegularExpression rx(ex, QRegularExpression::CaseInsensitiveOption);
        format += _("u");
        p.replace(rx, nr.asprintf(format.toLatin1().constData(), index));
    }

    // format the "count" parameter
    QRegularExpression rx_count(_("(\\\\\\[\\\\%\\d*count\\\\\\])"),
                                QRegularExpression::CaseInsensitiveOption);
    i = rx_count.globalMatch(p);
    while (i.hasNext()) {
        QRegularExpressionMatch match = i.next();
        if (count >= 0) {
            QString format = match.captured(1);
            format = format.mid(3, format.length() - 10);
            QString ex = _("(\\\\\\[\\\\") + format + _("count\\\\\\])");
            QRegularExpression rx(ex,
                QRegularExpression::CaseInsensitiveOption);
            format += _("u");
            p.replace(rx, nr.asprintf(format.toLatin1().constData(), count));
        } else {
            p.replace(rx_count, _("(\\d+)"));
        }
    }

    // format the "total" parameter
    QRegularExpression rx_total(_("(\\\\\\[\\\\%\\d*total\\\\\\])"),
                                QRegularExpression::CaseInsensitiveOption);
    i = rx_total.globalMatch(p);
    while (i.hasNext()) {
        QRegularExpressionMatch match = i.next();
        if (total >= 0) {
            QString format = match.captured(1);
            format = format.mid(3, format.length() - 10);
            QString ex = _("(\\\\\\[\\\\") + format + _("total\\\\\\])");
            QRegularExpression rx(ex,
                                  QRegularExpression::CaseInsensitiveOption);
            format += _("u");
            p.replace(rx, nr.asprintf(format.toLatin1().constData(), total));
        } else {
            p.replace(rx_total, _("(\\d+)"));
        }
    }

    // format the "filename" parameter
    QRegularExpression rx_filename(_("\\\\\\[\\\\%filename\\\\\\]"),
                                   QRegularExpression::CaseInsensitiveOption);
    if (p.indexOf(rx_filename) >= 0) {
        p.replace(rx_filename, QRegularExpression::escape(base));
    }

    // support for file info
    QRegularExpression rx_fileinfo(
        _("\\\\\\[\\\\%(\\d*)fileinfo\\\\\\{([\\w\\s]+)\\\\\\}\\\\\\]"),
        QRegularExpression::CaseInsensitiveOption
    );
    Kwave::FileInfo info(signalManager().metaData());
    i = rx_fileinfo.globalMatch(p);
    while (i.hasNext()) {
        QRegularExpressionMatch match = i.next();
        const QString format = match.captured(1);
        const QString id     = match.captured(2);
        QString value;
        FileProperty property = info.fromName(id);
        if (property != Kwave::INF_UNKNOWN) {
            QVariant val = info.get(property);
            if (!val.isNull()) {
                // we have a property value
                value = val.toString();

                // check for format (desired minimum string length)
                bool ok  = false;
                int  len = format.toUInt(&ok);
                if ((len > 0) && ok) {
                    Kwave::FileInfo::Flags flags = info.flags(property);
                    if (flags & Kwave::FileInfo::FP_FORMAT_NUMERIC) {
                        // numeric format, pad with leading zeros or spaces
                        QString pad = (format.startsWith(QLatin1Char('0'))) ?
                            _("0") : _(" ");
                        while (value.length() < len)
                            value = pad + value;
                    } else {
                        // string format, pad with trailing spaces
                        while (value.length() < len)
                            value = value + _(" ");
                    }
                }
                value = Kwave::Parser::escapeForFileName(value);
            }
        }

        QString ex(_("(\\\\\\[\\\\%") + format + _("fileinfo\\\\\\{") + id +
                   _("\\\\\\}\\\\\\])"));
        QRegularExpression rx(ex, QRegularExpression::CaseInsensitiveOption);
        p.replace(rx, value);
    }

    // format the "title" parameter
    QRegularExpression rx_title(_("\\\\\\[\\\\%title\\\\\\]"),
                                QRegularExpression::CaseInsensitiveOption);
    if (p.indexOf(rx_title) >= 0) {
        QString title;
        int idx = (index - 1) - (total - count);
        if ((idx >= 0) && (idx < m_block_info.count()))
            title = m_block_info[idx].m_title;
        if (title.length()) {
            title = Kwave::Parser::escapeForFileName(title);
            p.replace(rx_title, title);
        }
    }

    if (ext.length()) p += _(".") + ext;

    // sanitize the filename/path, make sure that there are no spaces
    // before and after all path separators
    QString sep = _("/");
    QRegularExpression rx_sep(_("\\s*") + sep + _("\\s*"));
    p.replace(rx_sep, sep);

    return p;
}

//***************************************************************************
unsigned int Kwave::SaveBlocksPlugin::firstIndex(const QString &path,
    const QString &base, const QString &ext, const QString &pattern,
    Kwave::SaveBlocksPlugin::numbering_mode_t mode, unsigned int count)
{
    unsigned int first = 1;
    switch (mode) {
        case START_AT_ONE:
            first = 1;
            break;
        case CONTINUE: {
            QDir dir(path, _("*"));
            QStringList files;
            files = dir.entryList();
            for (unsigned int i = first; i < (first + count); i++) {
                QString name = createFileName(base, ext, pattern, i, -1, -1);
                QRegularExpression rx(_("^(") + name + _(")$"),
                           QRegularExpression::CaseInsensitiveOption);
                QStringList matches = files.filter(rx);
                if (matches.count() > 0) first = i + 1;
            }
            break;
        }
        DEFAULT_IMPOSSIBLE;
    }

    return first;
}

//***************************************************************************
QString Kwave::SaveBlocksPlugin::findBase(const QString &filename,
                                          const QString &pattern)
{
    QFileInfo file(filename);
    QString name = file.fileName();
    QString base = file.completeBaseName();
    QString ext  = file.suffix();

    // convert the pattern into a regular expression in order to check if
    // the current name already is produced by the current pattern
    // \[%[0-9]?nr\]      -> \d+
    // \[%[0-9]?count\]   -> \d+
    // \[%[0-9]?total\]   -> \d+
    // \[%filename\]      -> base
    // \[%fileinfo\]      -> .
    // \[%title\]         -> .
    QRegularExpression rx_nr(_("\\\\\\[\\\\%\\d*nr\\\\\\]"),
                             QRegularExpression::CaseInsensitiveOption);
    QRegularExpression rx_count(_("\\\\\\[\\\\%\\d*count\\\\\\]"),
                                QRegularExpression::CaseInsensitiveOption);
    QRegularExpression rx_total(_("\\\\\\[\\\\%\\d*total\\\\\\]"),
                                QRegularExpression::CaseInsensitiveOption);
    QRegularExpression rx_filename(_("\\\\\\[\\\\%filename\\\\\\]"),
                                   QRegularExpression::CaseInsensitiveOption);
    QRegularExpression rx_fileinfo(_("\\\\\\[\\\\%fileinfo\\\\\\]"),
                                   QRegularExpression::CaseInsensitiveOption);
    QRegularExpression rx_title(_("\\\\\\[\\\\%title\\\\\\]"),
                                QRegularExpression::CaseInsensitiveOption);

    QString p = QRegularExpression::escape(pattern);
    qsizetype idx_nr       = p.indexOf(rx_nr);
    qsizetype idx_count    = p.indexOf(rx_count);
    qsizetype idx_total    = p.indexOf(rx_total);
    int       idx_filename = static_cast<int>(p.indexOf(rx_filename));
    qsizetype idx_fileinfo = p.indexOf(rx_fileinfo);
    qsizetype idx_title    = p.indexOf(rx_fileinfo);
    p.replace(rx_nr,       _("(\\d+)"));
    p.replace(rx_count,    _("(\\d+)"));
    p.replace(rx_total,    _("(\\d+)"));
    p.replace(rx_filename, _("(.+)"));
    p.replace(rx_fileinfo, _("(.+)"));
    p.replace(rx_title,    _("(.+)"));

    qsizetype max = 0;
    for (qsizetype i = 0; i < pattern.length(); i++) {
        if (idx_nr       == max) max++;
        if (idx_count    == max) max++;
        if (idx_total    == max) max++;
        if (idx_filename == max) max++;
        if (idx_fileinfo == max) max++;
        if (idx_title    == max) max++;

        if (idx_nr       > max) idx_nr--;
        if (idx_count    > max) idx_count--;
        if (idx_total    > max) idx_total--;
        if (idx_filename > max) idx_filename--;
        if (idx_fileinfo > max) idx_fileinfo--;
        if (idx_title    > max) idx_title--;
    }

    if (ext.length()) p += _(".") + ext;
    QRegularExpression rx_current(p, QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch rxm_current;
    if (name.indexOf(rx_current, 0, &rxm_current) >= 0) {
        // filename already produced by this pattern
        base = rxm_current.captured(idx_filename + 1);
    }

    return base;
}

//***************************************************************************
QString Kwave::SaveBlocksPlugin::firstFileName(const QString &filename,
                                               const QString &pattern,
                                               Kwave::SaveBlocksPlugin::numbering_mode_t mode,
                                               const QString &extension,
                                               bool selection_only)
{
    QFileInfo file(filename);
    QString path = file.absolutePath();
    QString ext  = extension.mid(1);
    QString base = findBase(filename, pattern);

    // now we have a new name, base and extension
    // -> find out the numbering, min/max etc...
    scanBlocksToSave(base, selection_only);
    unsigned int count = static_cast<unsigned int>(m_block_info.count());
    unsigned int first = firstIndex(path, base, ext, pattern, mode, count);
    unsigned int total = first + count - 1;

    // create the complete filename, including extension but without path
    return createFileName(base, ext, pattern, first, count, total);
}

//***************************************************************************
void Kwave::SaveBlocksPlugin::updateExample(const QString &filename,
                                            const QString &pattern,
                                            Kwave::SaveBlocksPlugin::numbering_mode_t mode,
                                            const QString &extension,
                                            bool selection_only)
{
    QString example = firstFileName(filename, pattern, mode, extension, selection_only);
    emit sigNewExample(Kwave::Parser::unescape(example));
}

//***************************************************************************
#include "SaveBlocksPlugin.moc"
//***************************************************************************
//***************************************************************************

#include "moc_SaveBlocksPlugin.cpp"

/***************************************************************************
  SelectRangeDialog.cpp  -  dialog for selecting a range of samples
                             -------------------
    begin                : Sat Jun 15 2002
    copyright            : (C) 2002 by Thomas Eschenbacher
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

#include <math.h>

#include <qradiobutton.h>
#include <qslider.h>

#include <klocale.h>
#include <knuminput.h>

#include "libgui/IntValidatorProxy.h"
#include "SelectRangeDialog.h"

//***************************************************************************
SelectRangeDialog::SelectRangeDialog(QWidget *widget,
    Mode start_mode, Mode range_mode, double range, double sample_rate,
    unsigned int offset, unsigned int signal_length)
    :SelectRangeDlg(widget, 0, true)
{
    if (select_start) {
        select_start->init(SelectTimeWidget::bySamples, offset,
                           sample_rate, 0, signal_length);
        select_start->setTitle(i18n("Start"));
        select_start->setMode(start_mode);
    }

    if (select_range) select_range->init(
        range_mode, range, sample_rate, offset, signal_length);

    connect(select_start, SIGNAL(valueChanged(unsigned int)),
            select_range, SLOT(setOffset(unsigned int)));
        
    setMinimumSize(sizeHint());
    setFixedSize(sizeHint());
}

//***************************************************************************
SelectRangeDialog::~SelectRangeDialog()
{
}

//***************************************************************************
void SelectRangeDialog::setMode(SelectTimeWidget::Mode new_mode)
{
    if (select_range) select_range->setMode(new_mode);
}

//***************************************************************************
//***************************************************************************

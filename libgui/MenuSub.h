/***************************************************************************
                          MenuSub.h  -  description
                             -------------------
    begin                : Mon Jan 10 2000
    copyright            : (C) 2000 by Martin Wilz
    email                : mwilz@ernie.MI.Uni-Koeln.DE
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#ifndef MENUSUB_H
#define MENUSUB_H

#include "MenuItem.h"

/**
 * This is the class for submenu entries in a Menu. It is normally owned by a
 * root menu node, a toplevel menu or an other submenu.
 * @author Thomas Eschenbacher
 */
class MenuSub : public MenuItem
{
  Q_OBJECT

public: // Public methods

};

#endif

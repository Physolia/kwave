#ifndef _DIALOGS_SAVEBLOCK_H_
#define _DIALOGS_SAVEBLOCK_H 1

#include <qdir.h>
#include <qlineedit.h>
#include <qlabel.h>
#include "../../../libgui/Dialog.h"
#include <libkwave/DialogOperation.h>
#include <libkwave/Global.h>

class SaveBlockDialog : public Dialog
{
 Q_OBJECT

 public:

 	SaveBlockDialog 	(Global*,bool modal);
 	~SaveBlockDialog ();
 const char  *getCommand ();

 public slots:

 void check ();

 protected:

 void resizeEvent (QResizeEvent *);

 private:

 QLineEdit      *name,*dirname;
 QLabel         *mark1,*mark2;
 QLabel         *dirlabel,*namelabel;
 QDir           *dir;
 QComboBox	*marktype1,*marktype2;
 QPushButton	*ok,*cancel;
 char           *comstr;
};
//*****************************************************************************
#endif

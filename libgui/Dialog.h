#ifndef _DIALOG_H
#define _DIALOG_H 1

#include <qdialog.h>

#define OK     i18n("&Ok")
#define CANCEL i18n("&Cancel")

class Dialog : public QDialog
{
 Q_OBJECT
 public:

  Dialog (bool=false);
  Dialog (const char *,bool=false);
  ~Dialog ();
  public slots:

   void accept ();
   void reject ();

  virtual const char *getCommand ()=0;
 signals:
  void command (const char *);
 private:
  bool modal;
};

#endif // _DIALOG_H_

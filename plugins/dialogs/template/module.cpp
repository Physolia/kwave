#include <stdio.h>
#include <stdlib.h>
#include <qpushbutton.h>
#include <qkeycode.h>
#include "module.h"
#include <kapp.h>

const char *version="";
const char *author="";
const char *name="";
//**********************************************************
Dialog *getDialog (DialogOperation *operation)
{
  return new Dialog(operation->isModal());
}
//**********************************************************














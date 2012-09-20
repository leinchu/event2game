dnl $Id$
dnl config.m4 for extension event2game

dnl Comments in this file start with the string 'dnl'.
dnl Remove where necessary. This file will not work
dnl without editing.

dnl If your extension references something external, use with:

PHP_ARG_WITH(event2game, for event2game support,
Make sure that the comment is aligned:
[  --with-event2game             Include event2game support])

dnl Otherwise use enable:

dnl PHP_ARG_ENABLE(event2game, whether to enable event2game support,
dnl Make sure that the comment is aligned:
dnl [  --enable-event2game           Enable event2game support])

if test "$PHP_EVENT2GAME" != "no"; then
  dnl Write more examples of tests here...

  dnl # --with-event2game -> check with-path
  dnl SEARCH_PATH="/usr/local /usr"     # you might want to change this
  dnl SEARCH_FOR="/include/event2game.h"  # you most likely want to change this
  dnl if test -r $PHP_EVENT2GAME/$SEARCH_FOR; then # path given as parameter
  dnl   EVENT2GAME_DIR=$PHP_EVENT2GAME
  dnl else # search default path list
  dnl   AC_MSG_CHECKING([for event2game files in default path])
  dnl   for i in $SEARCH_PATH ; do
  dnl     if test -r $i/$SEARCH_FOR; then
  dnl       EVENT2GAME_DIR=$i
  dnl       AC_MSG_RESULT(found in $i)
  dnl     fi
  dnl   done
  dnl fi
  dnl
  dnl if test -z "$EVENT2GAME_DIR"; then
  dnl   AC_MSG_RESULT([not found])
  dnl   AC_MSG_ERROR([Please reinstall the event2game distribution])
  dnl fi

  dnl # --with-event2game -> add include path
  dnl PHP_ADD_INCLUDE($EVENT2GAME_DIR/include)

  dnl # --with-event2game -> check for lib and symbol presence
  dnl LIBNAME=event2game # you may want to change this
  dnl LIBSYMBOL=event2game # you most likely want to change this 

  dnl PHP_CHECK_LIBRARY($LIBNAME,$LIBSYMBOL,
  dnl [
  dnl   PHP_ADD_LIBRARY_WITH_PATH($LIBNAME, $EVENT2GAME_DIR/lib, EVENT2GAME_SHARED_LIBADD)
  dnl   AC_DEFINE(HAVE_EVENT2GAMELIB,1,[ ])
  dnl ],[
  dnl   AC_MSG_ERROR([wrong event2game lib version or lib not found])
  dnl ],[
  dnl   -L$EVENT2GAME_DIR/lib -lm
  dnl ])
  dnl
  dnl PHP_SUBST(EVENT2GAME_SHARED_LIBADD)

  PHP_NEW_EXTENSION(event2game, event2game.c, $ext_shared)
fi

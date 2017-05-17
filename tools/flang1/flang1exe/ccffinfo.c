/*
 * Copyright (c) 2017, NVIDIA CORPORATION.  All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

/** \file
 * \brief  common compiler feedback format module
 */

#include "gbldefs.h"
#include "global.h"
#include "error.h"
#include <stdio.h>

#include <string.h>
#include <time.h>
#if !defined(HOST_WIN)
#include <unistd.h>
#endif
#include "symtab.h"
#include "fih.h"
#include "version.h"
#include "ccffinfo.h"

extern int auto_reinlinedepth; /* For bottom-up auto-inlining */

int bu_auto_inline(void);

static int anyunits = 0;

#define BUILD_VENDOR "flang-compiler"

FIHB fihb = {(FIH *)0, 0, 0, 0, 0, 0, 0};
FIHB ifihb = {(FIH *)0, 0, 0, 0, 0, 0, 0}; /* bottom-up auto-inliner */


#define CCFFAREA 24
#define ICCFFAREA 27
#define COPYSTRING(string) \
  strcpy(GETITEMS(CCFFAREA, char, strlen(string) + 1), string)
#define ICOPYSTRING(string) \
  strcpy(GETITEMS(ICCFFAREA, char, strlen(string) + 1), string)
#define COPYNSTRING(string, len) \
  strncpy(GETITEMS(CCFFAREA, char, (len) + 1), string, len)
#define ICOPYNSTRING(string, len) \
  strncpy(GETITEMS(ICCFFAREA, char, (len) + 1), string, len)

static char *formatbuffer = NULL;
static int formatbuffersize = 0;

static int unitstatus = -1; /* not opened */

static MESSAGE *prevmessage = NULL;
static int globalorder = 0;

/*
 * Initialize for F90/HPF front end
 */
void
ccff_init_f90()
{
  unitstatus = 0; /* we're not dealing with files here
                   * but we've initialized */
} /* ccff_init_f90 */

/*
 * Close up for F90/HPF front end
 */
void
ccff_finish_f90()
{
  unitstatus = -1; /* we've finalized */
} /* ccff_finish_f90 */

/*
 * set up for next program unit
 */
void
ccff_open_unit_f90()
{
  if (unitstatus >= 0) {
    unitstatus = gbl.func_count;
  }
} /* ccff_open_unit_f90 */

/*
 * clean up from this program unit
 */
static void ccff_cleanup_children();
void
ccff_close_unit_f90()
{
  if (unitstatus > 0) {
    unitstatus = 0;
  }
  ccff_cleanup_children();
} /* ccff_close_unit_f90 */

static int *childlist;
static int childlistsize;
static MESSAGE **messagelist;
static int messagelistsize;

static int strngsize = 0;
static char *strng = NULL;

/*
 * dump a message
 */
static void
dumpmessage(MESSAGE *m)
{
  FILE *dfile = gbl.dbgfil ? gbl.dbgfil : stderr;
  ARGUMENT *a;
  fprintf(dfile, "ccff:%p type:%d lineno:%d order:%d id:%s", m, m->msgtype,
          m->lineno, m->order, m->msgid);
  if (m->varname)
    fprintf(dfile, " varname:%s", m->varname);
  if (m->funcname)
    fprintf(dfile, " funcname:%s", m->funcname);
  fprintf(dfile, "\n ");
  fprintf(dfile, " message:%s", m->message);
  for (a = m->args; a; a = a->next) {
    fprintf(dfile, " %s=%s", a->argstring, a->argvalue);
  }
  fprintf(dfile, "\n");
} /* dumpmessage */

/*
 * dump list of messages
 */
void
dumpmsglist(MESSAGE *m)
{
  for (; m; m = m->next)
    dumpmessage(m);
} /* dumpmsglist */

void
dumpmessagelist(int nmessages)
{
  int n;
  for (n = 0; n < nmessages; ++n)
    dumpmessage(messagelist[n]);
} /* dumpmessagelist */

/*
 * heap sort by line number
 */
static void
_childsort(int l, int h)
{
  int m1, m2;
  int c, c1, c2;
  c = childlist[l];
  while (1) {
    m1 = l * 2 + 1;
    if (m1 > h)
      break; /* done */
    c1 = childlist[m1];
    m2 = l * 2 + 2;
    if (m2 <= h) {
      c2 = childlist[m2];
      if (FIH_LINENO(c2) > FIH_LINENO(c1) ||
          (FIH_LINENO(c2) == FIH_LINENO(c1) && c2 > c1)) {
        m1 = m2;
        c1 = c2;
      }
    }
    if (FIH_LINENO(c1) > FIH_LINENO(c) ||
        (FIH_LINENO(c1) == FIH_LINENO(c) && c1 > c)) {
      childlist[l] = c1;
      childlist[m1] = c;
      l = m1;
    } else {
      break;
    }
  }
} /* _childsort */

/*
 *  * heap sort by line number
 *   */
static void
_ichildsort(int l, int h)
{
  int m1, m2;
  int c, c1, c2;
  c = childlist[l];
  while (1) {
    m1 = l * 2 + 1;
    if (m1 > h)
      break; /* done */
    c1 = childlist[m1];
    m2 = l * 2 + 2;
    if (m2 <= h) {
      c2 = childlist[m2];
      if (IFIH_LINENO(c2) > IFIH_LINENO(c1) ||
          (IFIH_LINENO(c2) == IFIH_LINENO(c1) && c2 > c1)) {
        m1 = m2;
        c1 = c2;
      }
    }
    if (IFIH_LINENO(c1) > IFIH_LINENO(c) ||
        (IFIH_LINENO(c1) == IFIH_LINENO(c) && c1 > c)) {
      childlist[l] = c1;
      childlist[m1] = c;
      l = m1;
    } else {
      break;
    }
  }
} /* _ichildsort */

/*
 * all children of fihx appear in FIH_CHILD/FIH_NEXT linked list.
 * sort them by line number
 */
static void
fih_sort_children(int fihx)
{
  int child, nchildren, n;
  if (FIH_CHILD(fihx) == 0)
    return;
  nchildren = 0;
  for (child = FIH_CHILD(fihx); child; child = FIH_NEXT(child)) {
    ++nchildren;
  }
  if (nchildren > childlistsize) {
    childlistsize = nchildren + 100;
    childlist = GETITEMS(CCFFAREA, int, childlistsize);
  }
  nchildren = 0;
  for (child = FIH_CHILD(fihx); child; child = FIH_NEXT(child)) {
    childlist[nchildren] = child;
    ++nchildren;
  }
  /* heap sort */
  for (n = nchildren / 2; n > 0; --n) {
    _childsort(n - 1, nchildren - 1);
  }
  for (n = nchildren - 1; n > 0; --n) {
    int c;
    c = childlist[n];
    childlist[n] = childlist[0];
    childlist[0] = c;
    _childsort(0, n - 1);
  }
  FIH_CHILD(fihx) = childlist[0];
  for (n = 0; n < nchildren - 1; ++n) {
    FIH_NEXT(childlist[n]) = childlist[n + 1];
  }
  FIH_NEXT(childlist[nchildren - 1]) = 0;
} /* fih_sort_children */

/* all children of ifihx appear in IFIH_CHILD/IFIH_NEXT linked list.
 * sort them by line number */
static void
ifih_sort_children(int ifihx)
{
  int child, nchildren, n;
  if (IFIH_CHILD(ifihx) == 0)
    return;
  nchildren = 0;
  for (child = IFIH_CHILD(ifihx); child; child = IFIH_NEXT(child)) {
    ++nchildren;
  }
  if (nchildren > childlistsize) {
    childlistsize = nchildren + 100;
    childlist = GETITEMS(ICCFFAREA, int, childlistsize);
  }
  nchildren = 0;
  for (child = IFIH_CHILD(ifihx); child; child = IFIH_NEXT(child)) {
    childlist[nchildren] = child;
    ++nchildren;
  }
  /* heap sort */
  for (n = nchildren / 2; n > 0; --n) {
    _ichildsort(n - 1, nchildren - 1);
  }
  for (n = nchildren - 1; n > 0; --n) {
    int c;
    c = childlist[n];
    childlist[n] = childlist[0];
    childlist[0] = c;
    _ichildsort(0, n - 1);
  }
  IFIH_CHILD(ifihx) = childlist[0];
  for (n = 0; n < nchildren - 1; ++n) {
    IFIH_NEXT(childlist[n]) = childlist[n + 1];
  }
  IFIH_NEXT(childlist[nchildren - 1]) = 0;
} /* ifih_sort_children */

/*
 * return TRUE if the string is numeric
 */
static LOGICAL
_numeric(char *s, int *v)
{
  int r = 0;
  while (*s) {
    if (*s >= '0' && *s <= '9') {
      r = r * 10 + (*s - '0');
      ++s;
    } else {
      return FALSE;
    }
  }
  *v = r;
  return TRUE;
} /* _numeric */

/*
 * compare two MESSAGEs.
 * keys:
 *  line number
 *  if sortorder==0 or sortorder==1
 *   if the same message type:
 *    message id
 *    varname, if any
 *    funcname, if any
 *    arguments
 *  if sortorder==1 || sortorder == 2
 *    insertion order
 *  if sortorder == 2
 *    suborder
 */
static int
_messagecmp(MESSAGE *m1, MESSAGE *m2, int sortorder)
{
  int r;
  ARGUMENT *a1, *a2;
  if (m1->lineno > m2->lineno)
    return +1;
  if (m1->lineno < m2->lineno)
    return -1;
  if (m1->msgtype == m2->msgtype && sortorder != 2) {
    r = strcmp(m1->msgid, m2->msgid);
    if (r)
      return r;
    if (m1->varname && m2->varname) {
      r = strcmp(m1->varname, m2->varname);
      if (r)
        return r;
    } else if (m1->varname) {
      return +1;
    } else if (m2->varname) {
      return -1;
    }
    if (m1->funcname && m2->funcname) {
      r = strcmp(m1->funcname, m2->funcname);
      if (r)
        return r;
    } else if (m1->funcname) {
      return +1;
    } else if (m2->funcname) {
      return -1;
    }
    for (a1 = m1->args, a2 = m2->args; a1 && a2; a1 = a1->next, a2 = a2->next) {
      if (a1->argstring && a2->argstring) {
        r = strcmp(a1->argstring, a2->argstring);
        if (r)
          return r;
      } else if (a1->argstring) {
        return +1;
      } else if (a2->argstring) {
        return -1;
      }
      if (a1->argvalue && a2->argvalue) {
        int v1, v2;
        if (_numeric(a1->argvalue, &v1) && _numeric(a2->argvalue, &v2)) {
          if (v1 > v2)
            return +1;
          if (v1 < v2)
            return -1;
        } else {
          r = strcmp(a1->argvalue, a2->argvalue);
          if (r)
            return r;
        }
      } else if (a1->argvalue) {
        return +1;
      } else if (a2->argvalue) {
        return -1;
      }
    }
  } else if (!sortorder) {
    /* when sortorder != 0, we are only comparing for equality,
     * so just return a nonzero value to mean not equal */
    return 1;
  }
  if (sortorder > 0) {
    if (m1->order > m2->order)
      return +1;
    if (m1->order < m2->order)
      return -1;
  }
  if (sortorder == 2) {
    if (m1->suborder > m2->suborder)
      return +1;
    if (m1->suborder < m2->suborder)
      return -1;
  }
  return 0;
} /* _messagecmp */

/*
 * heap sort by line number
 */
static void
_messagesort(int l, int h, int sortorder)
{
  int m1, m2, r;
  MESSAGE *c, *c1, *c2;
  c = messagelist[l];
  while (1) {
    m1 = l * 2 + 1;
    if (m1 > h)
      break; /* done */
    c1 = messagelist[m1];
    m2 = l * 2 + 2;
    if (m2 <= h) {
      c2 = messagelist[m2];
      r = _messagecmp(c1, c2, sortorder);
      if (r < 0) {
        /* compare to c2 */
        m1 = m2;
        c1 = c2;
      } else {
        /* compare to c1 */
      }
    }
    r = _messagecmp(c, c1, sortorder);
    if (r < 0) {
      messagelist[l] = c1;
      messagelist[m1] = c;
      l = m1;
    } else {
      break;
    }
  }
} /* _messagesort */

/*
 * sort messages by line number
 */
static MESSAGE *
sort_message_list(MESSAGE *msglist)
{
  int nmessages, n, prevn;
  MESSAGE *mptr;
  MESSAGE *newmsglist;
  nmessages = 0;
  for (mptr = msglist; mptr; mptr = mptr->next)
    ++nmessages;
  if (nmessages == 0)
    return msglist;
  if (nmessages > messagelistsize) {
    messagelistsize = nmessages + 100;
      messagelist = GETITEMS(CCFFAREA, MESSAGE *, messagelistsize);
  }
  nmessages = 0;
  for (mptr = msglist; mptr; mptr = mptr->next)
    messagelist[nmessages++] = mptr;
  /* heap sort */
  for (n = nmessages / 2; n > 0; --n)
    _messagesort(n - 1, nmessages - 1, 1);
  for (n = nmessages - 1; n > 0; --n) {
    mptr = messagelist[n];
    messagelist[n] = messagelist[0];
    messagelist[0] = mptr;
    _messagesort(0, n - 1, 1);
  }
  newmsglist = messagelist[0];
  prevn = 0;
  for (n = 1; n < nmessages; ++n) {
    /* look for duplicate messages, with the same arguments, on the same line */
    MESSAGE *mmptr;
    int nn;

    mptr = messagelist[n];
    mmptr = messagelist[n - 1];
    if (_messagecmp(mptr, mmptr, 0) != 0) {
      /* not a duplicate, include this message */
      messagelist[prevn]->next = messagelist[n];
      prevn = n;
      /* if this message has the same message id, reset order to match */
      if (strcmp(mptr->msgid, mmptr->msgid) == 0) {
        mmptr->order = mptr->order;
        mmptr->suborder = mptr->suborder + 1;
      } else {
        mmptr->suborder = 1;
      }
    }
  }
  if (prevn >= 0)
    messagelist[prevn]->next = NULL;

  /* resort by line number, sort order, suborder,
   * after the duplicate messages are all removed */
  nmessages = 0;
  for (mptr = newmsglist; mptr; mptr = mptr->next) {
    messagelist[nmessages] = mptr;
    ++nmessages;
  }
  /* heap sort */
  for (n = nmessages / 2; n > 0; --n) {
    _messagesort(n - 1, nmessages - 1, 2);
  }
  for (n = nmessages - 1; n > 0; --n) {
    mptr = messagelist[n];
    messagelist[n] = messagelist[0];
    messagelist[0] = mptr;
    _messagesort(0, n - 1, 2);
  }
  mptr = messagelist[0];
  for (n = 1; n < nmessages; ++n) {
    mptr->next = messagelist[n];
    mptr = messagelist[n];
  }
  mptr->next = NULL;
  newmsglist = messagelist[0];
  return newmsglist;
} /* sort_message_list */

static void
fih_sort_messages(int fihx)
{
  MESSAGE *mptr;
  FIH_CCFFINFO(fihx) = (char *)sort_message_list((MESSAGE *)FIH_CCFFINFO(fihx));
  /* sort any child messages */
  for (mptr = (MESSAGE *)FIH_CCFFINFO(fihx); mptr; mptr = mptr->next) {
    if (mptr->msgchild) {
      mptr->msgchild = sort_message_list(mptr->msgchild);
    }
  }
} /* fih_sort_messages */

static void
ifih_sort_messages(int ifihx)
{
  MESSAGE *mptr;
  IFIH_CCFFINFO(ifihx) =
      (char *)sort_message_list((MESSAGE *)IFIH_CCFFINFO(ifihx));
  /* sort any child messages */
  for (mptr = (MESSAGE *)IFIH_CCFFINFO(ifihx); mptr; mptr = mptr->next) {
    if (mptr->msgchild) {
      mptr->msgchild = sort_message_list(mptr->msgchild);
    }
  }
} /* ifih_sort_messages */

/*
 * Does the next message have the same message ID and the same
 * arguments as this one, except for arguments named '*list='
 */
static LOGICAL
combine_message(MESSAGE *mptr1, MESSAGE *mptr2)
{
  ARGUMENT *arg1, *arg2;
  if (XBIT(198, 4))
    return FALSE;
  if (mptr1->lineno != mptr2->lineno)
    return FALSE;
  if (mptr1->fihx != mptr2->fihx)
    return FALSE;
  if (mptr1->msgtype != mptr2->msgtype)
    return FALSE;
  if (strcmp(mptr1->msgid, mptr2->msgid))
    return FALSE;
  for (arg1 = mptr1->args, arg2 = mptr2->args; arg1 && arg2;
       arg1 = arg1->next, arg2 = arg2->next) {
    char *s1, *s2;
    int listarg = 0;
    s1 = arg1->argstring;
    s2 = arg2->argstring;
    if (strcmp(s1, s2))
      return FALSE;
    /* look for %...list */
    for (; *s1 && *s1 != '='; ++s1) {
      if (*s1 == 'l') {
        if (strcmp(s1, "list") == 0) {
          listarg = 1; /* list arguments may differ */
          break;
        }
      }
    }
    if (!listarg) {
      /* not a list argument, must match exactly */
      if (strcmp(arg1->argvalue, arg2->argvalue))
        return FALSE;
    }
  }
  if (arg1 || arg2) /* one message had more arguments */
    return FALSE;
  mptr2->combine = 1;
  return TRUE;
} /* combine_message */

/*
 * print one message to the output file
 * with symbolic substitution
 */
static void
__fih_message(FILE *ofile, MESSAGE *mptr, LOGICAL dolist)
{
  char *message;
  char *chp;
  int strnglen, n;
  ARGUMENT *aptr, *aptr3;
  MESSAGE *mptr2, *mptr3;
  message = mptr->message;
  for (chp = message; *chp; ++chp) {
    if (*chp != '%') {
      fprintf(ofile, "%c", *chp);
    } else {
      ++chp;
      if (*chp == '%') {
        fprintf(ofile, "%c", *chp);
      } else {
        strnglen = 0;
        while ((*chp >= 'a' && *chp <= 'z') || (*chp >= 'A' && *chp <= 'Z') ||
               (*chp >= '0' && *chp <= '9') || *chp == '_') {
          if (strnglen >= strngsize - 1) {
            char *nstrng;
            strng[strnglen] = '\0';
            strngsize *= 2;
              nstrng = (char *)getitem(CCFFAREA, strngsize);
            strcpy(nstrng, strng);
            strng = nstrng;
          }
          strng[strnglen++] = *chp++;
        }
        --chp;
        if (strnglen) {
          int first = 1;
          LOGICAL islist = FALSE;
          strng[strnglen] = '\0';
          if (!XBIT(198, 4) && strnglen > 4 &&
              strcmp(strng + strnglen - 4, "list") == 0)
            islist = TRUE;
          for (mptr2 = mptr; mptr2; mptr2 = mptr2->next) {
            LOGICAL duplicate = FALSE;
            if (mptr2 != mptr && !mptr2->combine)
              break;
            for (aptr = mptr2->args; aptr; aptr = aptr->next) {
              if (strcmp(aptr->argstring, strng) == 0)
                break;
            }
            if (aptr) {
              /* see if argument aptr has already been printed for this list */
              for (mptr3 = mptr; mptr3 != mptr2; mptr3 = mptr3->next) {
                for (aptr3 = mptr3->args; aptr3; aptr3 = aptr3->next) {
                  if (strcmp(aptr3->argstring, strng) == 0)
                    break;
                }
                if (aptr3 && strcmp(aptr3->argvalue, aptr->argvalue) == 0) {
                  duplicate = TRUE;
                  break;
                }
              }
              if (!duplicate) {
                if (first) {
                  fprintf(ofile, "%s", aptr->argvalue);
                } else {
                  fprintf(ofile, ",%s", aptr->argvalue);
                }
                first = 0;
              }
            }
            if (!dolist || !islist)
              break;
          }
        }
      }
    }
  }
} /* __fih_message */

static void
_fih_message(FILE *ofile, MESSAGE *mptr, LOGICAL do_cdata)
{
  __fih_message(ofile, mptr, TRUE);
} /* _fih_message */

#define INDENT 5
#define CINDENT 2
static int prevnest = -1, prevchildnest = -1;
static int prevlineno = 0;
static LOGICAL anymessages = FALSE;

static void
print_func(FILE *ofile)
{
  char *funcname;
  if (!anymessages) {
    anymessages = TRUE;
    funcname = FIH_FUNCNAME(1);
    fprintf(ofile, "%s:\n", funcname);
  }
} /* print_func */

/*
 * Format and print message to log file
 */
static void
fih_message_ofile(FILE *ofile, int nest, int lineno, int childnest,
                  MESSAGE *mptr)
{
  MESSAGE *child, *nextchild;
  if (flg.x[161] != 0 || flg.x[162] != 0) {
    switch (mptr->msgtype) {
    case MSGINLINER:
      if (!XBIT(161, 1))
        return;
      break;
    case MSGNEGINLINER:
      if (!XBIT(162, 1))
        return;
      break;
    case MSGLOOP:
      if (!XBIT(161, 2))
        return;
      break;
    case MSGNEGLOOP:
      if (!XBIT(162, 2))
        return;
      break;
    case MSGLRE:
      if (!XBIT(161, 4))
        return;
      break;
    case MSGNEGLRE:
      if (!XBIT(162, 4))
        return;
      break;
    case MSGINTENSITY:
      if (!XBIT(161, 8))
        return;
      break;
    case MSGIPA:
      if (!XBIT(161, 0x10))
        return;
      break;
    case MSGNEGIPA:
      if (!XBIT(162, 0x10))
        return;
      break;
    case MSGFUSE:
      if (!XBIT(161, 0x20))
        return;
      break;
    case MSGNEGFUSE:
      if (!XBIT(162, 0x20))
        return;
      break;
    case MSGVECT:
    case MSGCVECT:
      if (!XBIT(161, 0x40))
        return;
      break;
    case MSGNEGVECT:
    case MSGNEGCVECT:
      if (!XBIT(162, 0x40))
        return;
      break;
    case MSGOPENMP:
      if (!XBIT(161, 0x80))
        return;
      break;
    case MSGOPT:
      if (!XBIT(161, 0x100))
        return;
      break;
    case MSGNEGOPT:
      if (!XBIT(162, 0x100))
        return;
      break;
    case MSGPREFETCH:
      if (!XBIT(161, 0x200))
        return;
      break;
    case MSGFTN:
      if (!XBIT(161, 0x400))
        return;
      break;
    case MSGPAR:
      if (!XBIT(161, 0x800))
        return;
      break;
    case MSGNEGPAR:
      if (!XBIT(162, 0x800))
        return;
      break;
    case MSGHPF:
      if (!XBIT(161, 0x1000))
        return;
      break;
    case MSGPFO:
    case MSGNEGPFO:
      if (!XBIT(161, 0x2000))
        return;
      break;
    case MSGACCEL:
      if (!XBIT(161, 0x4000))
        return;
      break;
    case MSGNEGACCEL:
      if (!XBIT(162, 0x4000))
        return;
      break;
    case MSGUNIFIED:
      if (!XBIT(161, 0x8000))
        return;
      break;
    }
  }
  print_func(ofile);
  fprintf(ofile, "%*s  ", nest * INDENT, "");
  if (childnest > 0)
    fprintf(ofile, "%*s  ", childnest * CINDENT, "");
  if (nest != prevnest || childnest != prevchildnest || lineno != prevlineno ||
      XBIT(198, 0x10000000)) {
    fprintf(ofile, "%5d, ", lineno);
  } else {
    fprintf(ofile, "%5s  ", "     ");
  }
  prevnest = nest;
  prevchildnest = childnest;
  prevlineno = lineno;
  _fih_message(ofile, mptr, FALSE);
  fprintf(ofile, "\n");
  if (mptr->msgchild) {
    for (child = mptr->msgchild; child; child = nextchild) {
      for (nextchild = child->next;
           nextchild && combine_message(child, nextchild);
           nextchild = nextchild->next)
        ;
      fih_message_ofile(ofile, nest, child->lineno, childnest + 1, child);
    }
  }
} /* fih_message_ofile */

/*
 * Format and print message to log file
 */
static void
ifih_message_ofile(FILE *ofile, int nest, int lineno, int childnest,
                   MESSAGE *mptr)
{
  MESSAGE *child;
  char *funcname;
  if (flg.x[161] != 0 || flg.x[162] != 0) {
    switch (mptr->msgtype) {
    case MSGINLINER:
      if (!XBIT(161, 1))
        return;
      break;
    case MSGNEGINLINER:
      if (!XBIT(162, 1))
        return;
      break;
    case MSGLOOP:
      if (!XBIT(161, 2))
        return;
      break;
    case MSGNEGLOOP:
      if (!XBIT(162, 2))
        return;
      break;
    case MSGLRE:
      if (!XBIT(161, 4))
        return;
      break;
    case MSGNEGLRE:
      if (!XBIT(162, 4))
        return;
      break;
    case MSGINTENSITY:
      if (!XBIT(161, 8))
        return;
      break;
    case MSGIPA:
      if (!XBIT(161, 0x10))
        return;
      break;
    case MSGNEGIPA:
      if (!XBIT(162, 0x10))
        return;
      break;
    case MSGFUSE:
      if (!XBIT(161, 0x20))
        return;
      break;
    case MSGNEGFUSE:
      if (!XBIT(162, 0x20))
        return;
      break;
    case MSGVECT:
    case MSGCVECT:
      if (!XBIT(161, 0x40))
        return;
      break;
    case MSGNEGVECT:
    case MSGNEGCVECT:
      if (!XBIT(162, 0x40))
        return;
      break;
    case MSGOPENMP:
      if (!XBIT(161, 0x80))
        return;
      break;
    case MSGOPT:
      if (!XBIT(161, 0x100))
        return;
      break;
    case MSGNEGOPT:
      if (!XBIT(162, 0x100))
        return;
      break;
    case MSGPREFETCH:
      if (!XBIT(161, 0x200))
        return;
      break;
    case MSGFTN:
      if (!XBIT(161, 0x400))
        return;
      break;
    case MSGPAR:
      if (!XBIT(161, 0x800))
        return;
      break;
    case MSGNEGPAR:
      if (!XBIT(162, 0x800))
        return;
      break;
    case MSGHPF:
      if (!XBIT(161, 0x1000))
        return;
      break;
    case MSGPFO:
    case MSGNEGPFO:
      if (!XBIT(161, 0x2000))
        return;
      break;
    case MSGACCEL:
      if (!XBIT(161, 0x4000))
        return;
      break;
    case MSGNEGACCEL:
      if (!XBIT(162, 0x4000))
        return;
      break;
    case MSGUNIFIED:
      if (!XBIT(161, 0x8000))
        return;
      break;
    }
  }
  if (!anymessages) {
    anymessages = TRUE;
      funcname = IFIH_FUNCNAME(1);
    fprintf(ofile, "%s:\n", funcname);
  }
  fprintf(ofile, "%*s  ", nest * INDENT, "");
  if (childnest > 0) {
    fprintf(ofile, "%*s  ", childnest * CINDENT, "");
  }
  if (nest != prevnest || childnest > prevchildnest || lineno != prevlineno) {
    fprintf(ofile, "%5d, ", lineno);
  } else {
    fprintf(ofile, "%5s  ", "     ");
  }
  prevnest = nest;
  prevchildnest = childnest;
  prevlineno = lineno;
  _fih_message(ofile, mptr, FALSE);
  fprintf(ofile, "\n");
  if (mptr->msgchild) {
    for (child = mptr->msgchild; child; child = child->next) {
      ifih_message_ofile(ofile, nest, child->lineno, childnest + 1, child);
    }
  }
} /* ifih_message_ofile */

/*
 * output messages for this FIH tag
 */
static void
fih_messages(int fihx, FILE *ofile, int nest)
{
} /* fih_messages */

/*
 * output messages for this FIH tag
 */
static void
ifih_messages(int ifihx, FILE *ofile, int nest)
{
} /* ifih_messages */

/*
 *  Remove child include files if there is no message.
 */

static void
fih_rminc_children(int fihx)
{
  int child;
  int prev_fihx = 0;

  for (; fihx; fihx = FIH_NEXT(fihx)) {

    /* Do the deepest level child first */
    child = FIH_CHILD(fihx);
    if (child) {
      fih_rminc_children(child);
    }

    if (FIH_CHECKFLAG(fihx, FIH_INCLUDED)) {
      if (!FIH_CCFFINFO(fihx)) {
        if (prev_fihx && !FIH_CHILD(fihx))
          FIH_NEXT(prev_fihx) = FIH_NEXT(fihx);
        else if (!FIH_CHILD(fihx)) {
          FIH_CHILD(FIH_PARENT(fihx)) = FIH_NEXT(fihx);
          continue;
        }
      }
    }
    prev_fihx = fihx;
  }
}

/* Remove child include files if there is no message. */

static void
ifih_rminc_children(int ifihx)
{
  int child;
  int prev_ifihx = 0;

  for (; ifihx; ifihx = IFIH_NEXT(ifihx)) {

    /* Do the deepest level child first */
    child = IFIH_CHILD(ifihx);
    if (child) {
      ifih_rminc_children(child);
    }

    if (IFIH_FLAGS(ifihx) & FIH_INCLUDED) {
      if (!IFIH_CCFFINFO(ifihx)) {
        if (prev_ifihx && !IFIH_CHILD(ifihx))
          IFIH_NEXT(prev_ifihx) = IFIH_NEXT(ifihx);
        else if (!IFIH_CHILD(ifihx)) {
          IFIH_CHILD(IFIH_PARENT(ifihx)) = IFIH_NEXT(ifihx);
          continue;
        }
      }
    }
    prev_ifihx = ifihx;
  }
}

static LOGICAL
save_any_messages(int fihx)
{
  MESSAGE *mptr;
  mptr = (MESSAGE *)FIH_CCFFINFO(fihx);
  for (; mptr; mptr = mptr->next) {
    switch (mptr->msgtype) {
    case MSGINLINER:
      if (XBIT(161, 1))
        return TRUE;
      break;
    case MSGNEGINLINER:
      if (XBIT(162, 1))
        return TRUE;
      break;
    case MSGLOOP:
      if (XBIT(161, 2))
        return TRUE;
      break;
    case MSGNEGLOOP:
      if (XBIT(162, 2))
        return TRUE;
      break;
    case MSGLRE:
      if (XBIT(161, 4))
        return TRUE;
      break;
    case MSGNEGLRE:
      if (XBIT(162, 4))
        return TRUE;
      break;
    case MSGINTENSITY:
      if (XBIT(161, 8))
        return TRUE;
      break;
    case MSGIPA:
      if (XBIT(161, 0x10))
        return TRUE;
      break;
    case MSGNEGIPA:
      if (XBIT(162, 0x10))
        return TRUE;
      break;
    case MSGFUSE:
      if (XBIT(161, 0x20))
        return TRUE;
      break;
    case MSGNEGFUSE:
      if (XBIT(162, 0x20))
        return TRUE;
      break;
    case MSGVECT:
    case MSGCVECT:
      if (XBIT(161, 0x40))
        return TRUE;
      break;
    case MSGNEGVECT:
    case MSGNEGCVECT:
      if (XBIT(162, 0x40))
        return TRUE;
      break;
    case MSGOPENMP:
      if (XBIT(161, 0x80))
        return TRUE;
      break;
    case MSGOPT:
      if (XBIT(161, 0x100))
        return TRUE;
      break;
    case MSGNEGOPT:
      if (XBIT(162, 0x100))
        return TRUE;
      break;
    case MSGPREFETCH:
      if (XBIT(161, 0x200))
        return TRUE;
      break;
    case MSGFTN:
      if (XBIT(161, 0x400))
        return TRUE;
      break;
    case MSGPAR:
      if (XBIT(161, 0x800))
        return TRUE;
      break;
    case MSGNEGPAR:
      if (XBIT(162, 0x800))
        return TRUE;
      break;
    case MSGHPF:
      if (XBIT(161, 0x1000))
        return TRUE;
      break;
    case MSGPFO:
    case MSGNEGPFO:
      if (XBIT(161, 0x2000))
        return TRUE;
      break;
    case MSGACCEL:
      if (XBIT(161, 0x4000))
        return TRUE;
      break;
    case MSGNEGACCEL:
      if (XBIT(162, 0x4000))
        return TRUE;
      break;
    case MSGUNIFIED:
      if (XBIT(161, 0x8000))
        return TRUE;
      break;
    }
  }
  return FALSE;
} /* save_any_messages */

/*
 * set up childlist, messagelist, set FIH_CHILD, FIH_PARENT
 */
static void
ccff_set_children()
{
  int fihx, parentx;

  childlistsize = 100;
  childlist = GETITEMS(CCFFAREA, int, childlistsize);
  messagelistsize = 100;
  messagelist = GETITEMS(CCFFAREA, MESSAGE *, messagelistsize);
  for (fihx = 1; fihx < fihb.stg_avail; ++fihx) {
    FIH_CHILD(fihx) = 0;
    FIH_NEXT(fihx) = 0;
  }
  for (fihx = fihb.stg_avail - 1; fihx > 0; --fihx) {
    if (save_any_messages(fihx))
      FIH_SETFLAG(fihx, FIH_DO_CCFF);
    parentx = FIH_PARENT(fihx);
    if (parentx) {
      if (FIH_CHILD(parentx)) {
        FIH_NEXT(fihx) = FIH_CHILD(parentx);
      }
      FIH_CHILD(parentx) = fihx;
      if (FIH_CHECKFLAG(fihx, FIH_CCFF))
        FIH_SETFLAG(parentx, FIH_CCFF);
      if (FIH_CHECKFLAG(fihx, FIH_DO_CCFF))
        FIH_SETFLAG(parentx, FIH_DO_CCFF);
    }
  }

  if (fihb.stg_avail > 1 && FIH_CHILD(1))
    fih_rminc_children(FIH_CHILD(1));

  for (fihx = 1; fihx < fihb.stg_avail; ++fihx) {
    fih_sort_children(fihx);
    fih_sort_messages(fihx);
  }
  strngsize = 100;
  strng = (char *)getitem(CCFFAREA, strngsize);
} /* ccff_set_children */

/* set up childlist, messagelist, set IFIH_CHILD, IFIH_PARENT */
static void
ccff_set_children_deferred()
{
  int ifihx, parentx;

  childlistsize = 100;
  childlist = GETITEMS(ICCFFAREA, int, childlistsize);
  messagelistsize = 100;
  messagelist = GETITEMS(ICCFFAREA, MESSAGE *, messagelistsize);
  for (ifihx = 1; ifihx < ifihb.stg_avail; ++ifihx) {
    IFIH_CHILD(ifihx) = 0;
    IFIH_NEXT(ifihx) = 0;
  }
  for (ifihx = ifihb.stg_avail - 1; ifihx > 0; --ifihx) {
    parentx = IFIH_PARENT(ifihx);
    if (parentx) {
      if (IFIH_CHILD(parentx)) {
        IFIH_NEXT(ifihx) = IFIH_CHILD(parentx);
      }
      IFIH_CHILD(parentx) = ifihx;
      if (IFIH_FLAGS(ifihx) & FIH_CCFF) {
        IFIH_FLAGS(parentx) |= FIH_CCFF;
      }
    }
  }

  if (ifihb.stg_avail > 1 && IFIH_CHILD(1))
    ifih_rminc_children(IFIH_CHILD(1));

  for (ifihx = 1; ifihx < ifihb.stg_avail; ++ifihx) {
    ifih_sort_children(ifihx);
    ifih_sort_messages(ifihx);
  }
  strngsize = 100;
  strng = (char *)getitem(ICCFFAREA, strngsize);
} /* ccff_set_children_deferred */

/*
 * free up allocated space
 */
static void
ccff_cleanup_children()
{
  int fihx;
  freearea(CCFFAREA);
  formatbuffer = NULL;
  formatbuffersize = 0;
  strngsize = 0;
  strng = NULL;
  childlistsize = 0;
  childlist = NULL;
  messagelistsize = 0;
  messagelist = NULL;
  for (fihx = 1; fihx < fihb.stg_avail; ++fihx) {
    FIH_CCFFINFO(fihx) = NULL;
    FIH_CLEARFLAG(fihx, FIH_CCFF);
  }
} /* ccff_cleanup_children */

/* free up allocated space */
void
ccff_cleanup_children_deferred()
{
  int ifihx;
  freearea(ICCFFAREA);
  formatbuffer = NULL;
  formatbuffersize = 0;
  strngsize = 0;
  strng = NULL;
  childlistsize = 0;
  childlist = NULL;
  messagelistsize = 0;
  messagelist = NULL;
  for (ifihx = 1; ifihx < ifihb.stg_avail; ++ifihx) {
    IFIH_CCFFINFO(ifihx) = NULL;
    IFIH_FLAGS(ifihx) &= ~(FIH_CCFF);
  }
} /* ccff_cleanup_children_deferred */

/*
 * the routines below allocate new space in CCFFAREA
 * for the output of the sprintf.  They use the safer snprintf.
 * I had hoped to use a varargs routine so as to only need one
 * newprintf routine, but I required two calls to snprintf
 * (the first to get the buffer size, the second after allocating the
 * buffer) and you can't easily restart a varargs
 */

/*
 * fill the 'formatbuffer' to pass to snprintf
 */
static void
fillformat(const char *format, int len)
{
  if (len > formatbuffersize) {
    if (formatbuffersize == 0) {
      formatbuffersize = 100;
    } else {
      formatbuffersize = len * 2;
    }
      formatbuffer = GETITEMS(CCFFAREA, char, formatbuffersize + 1);
  }
  strncpy(formatbuffer, format, len);
  formatbuffer[len] = '\0';
} /* fillformat */

/*
 * allocate a new buffer to hold the snprintf output
 */
static char *
newbuff(char *oldstring, int len, int *pl)
{
  int l;
  char *buff;
  l = 0;
  if (oldstring)
    l = strlen(oldstring);
    buff = GETITEMS(CCFFAREA, char, l + len + 1);
  if (oldstring)
    strcpy(buff, oldstring);
  *pl = l;
  return buff;
} /* newbuff */

static char *
newprintfl(char *oldstring, const char *format, int len, long data)
{
  char dummybuffer[50];
  char *buff;
  int n, l;
  fillformat(format, len);
  n = snprintf(dummybuffer, sizeof(dummybuffer), formatbuffer, data);
  if (n <= 0)
    return NULL;
  buff = newbuff(oldstring, n, &l);
  n = snprintf(buff + l, n + 1, formatbuffer, data);
  return buff;
} /* newprintfl */

static char *
newprintfi(char *oldstring, const char *format, int len, int data)
{
  char dummybuffer[50];
  char *buff;
  int n, l;
  fillformat(format, len);
  n = snprintf(dummybuffer, sizeof(dummybuffer), formatbuffer, data);
  if (n <= 0)
    return NULL;
  buff = newbuff(oldstring, n, &l);
  n = snprintf(buff + l, n + 1, formatbuffer, data);
  return buff;
} /* newprintfi */

static char *
newprintfd(char *oldstring, const char *format, int len, double data)
{
  char dummybuffer[50];
  char *buff;
  int n, l;
  fillformat(format, len);
  n = snprintf(dummybuffer, sizeof(dummybuffer), formatbuffer, data);
  if (n <= 0)
    return NULL;
  buff = newbuff(oldstring, n, &l);
  n = snprintf(buff + l, n + 1, formatbuffer, data);
  return buff;
} /* newprintfd */

static char *
newprintfs(char *oldstring, const char *format, int len, char *data)
{
  char *buff;
  int n, l;
#ifdef HOST_WIN

  /* On windows, snprintf does a copy and return -1 if number of bytes
   * copied is smaller than strlen(data) */

  char *dummybuffer = (char *)malloc((size_t)(strlen(data) + strlen(format)));
#else
  char dummybuffer[1];
#endif
  fillformat(format, len);
#ifdef HOST_WIN
  n = snprintf(dummybuffer, strlen(data), formatbuffer, data);
#else
  n = snprintf(dummybuffer, 1, formatbuffer, data);
#endif
  if (n <= 0)
    return NULL;
  buff = newbuff(oldstring, n, &l);
  n = snprintf(buff + l, n + 1, formatbuffer, data);
#ifdef HOST_WIN
  free(dummybuffer);
#endif
  return buff;
} /* newprintfs */

static char *
newprintfx(char *oldstring, const char *format, int len)
{
  char dummybuffer[50];
  char *buff;
  int n, l;
  fillformat(format, len);
  n = snprintf(dummybuffer, sizeof(dummybuffer), "%s", formatbuffer);
  if (n <= 0)
    return NULL;
  buff = newbuff(oldstring, n, &l);
  n = snprintf(buff + l, n + 1, "%s", formatbuffer);
  return buff;
} /* newprintfx */

/*
 * save one message
 *  _ccff_info( MSGTYPE, MSGID, BIH_FINDEX(bihx), BIH_LINENO(bihx),
 *	"varname", "funcname",
 *	"function %func inlined, size %size",
 *	"func=%s", SYMNAME(foo), "size=%d", funcsize, NULL );
 */
void *
_ccff_info(int msgtype, char *msgid, int fihx, int lineno, const char *varname,
           const char *funcname, const void *xparent, const char *message,
           va_list argptr)
{
  MESSAGE *mptr;
  ARGUMENT *aptr, *alast;
  char *argformat, *argend, *format, *f;
  int seenpercent, seenlong, ll;

  if (unitstatus <= 0) /* not initialized */
    return NULL;

  /* keep list of messages at this FIH index */
  ++globalorder;
    mptr = GETITEM(CCFFAREA, MESSAGE);
  BZERO(mptr, MESSAGE, 1);
  mptr->msgtype = msgtype;
  mptr->msgid = msgid;
  mptr->fihx = fihx;
  mptr->lineno = lineno;
  mptr->varname = NULL;
  mptr->funcname = NULL;
  mptr->seq = 0;
  mptr->combine = 0;
    if (varname && varname[0] != '\0')
      mptr->varname = COPYSTRING(varname);
    if (funcname && funcname[0] != '\0')
      mptr->funcname = COPYSTRING(funcname);
    mptr->message = COPYSTRING(message);
  mptr->args = NULL;
  mptr->order = globalorder;
  prevmessage = mptr;
  alast = NULL;
  while (1) {
    /* argument must be name=%X where X is
     *  d - integer
     *  ld - long
     *  s - string
     *  f - double
     *  x - integer in hex
     *  lx - long in hex
     */
    argformat = va_arg(argptr, char *);
    if (argformat == NULL)
      break;
    /* 1st character must be alpha */
    if ((argformat[0] < 'a' || argformat[0] > 'z') &&
        (argformat[0] < 'A' || argformat[0] > 'Z')) {
      interr("ccff_info: bad argument format", 0, 3);
      return NULL;
    }
      aptr = GETITEM(CCFFAREA, ARGUMENT);
    BZERO(aptr, ARGUMENT, 1);
    aptr->next = NULL;
    /* find the "=" */
    for (argend = argformat + 1; *argend && *argend != '='; ++argend)
      ;
    if (argend[0] != '=') {
      interr("ccff_info: bad argument format", 0, 3);
      return NULL;
    }
    ll = argend - argformat;
      aptr->argstring = COPYNSTRING(argformat, ll);
    aptr->argstring[ll] = '\0';
    aptr->argvalue = NULL;
    format = argend + 1;
    seenpercent = 0;
    seenlong = 0;
    for (f = format; *f; ++f) {
      switch (*f) {
      case '%':
        seenpercent = 1;
        seenlong = 0;
        break;
      case 'l':
        seenlong = 1;
        break;
      case 'd':
      case 'o':
      case 'x':
      case 'X':
      case 'u':
        if (seenpercent) {
          /* int */
          if (seenlong) {
            long l;
            l = va_arg(argptr, long);
            aptr->argvalue =
                newprintfl(aptr->argvalue, format, f + 1 - format, l);
          } else {
            int i;
            i = va_arg(argptr, int);
            aptr->argvalue =
                newprintfi(aptr->argvalue, format, f + 1 - format, i);
          }
          format = f + 1;
          seenpercent = 0;
        }
        break;
      case 'e':
      case 'E':
      case 'g':
      case 'G':
      case 'f':
        if (seenpercent) {
          double d;
          d = va_arg(argptr, double);
          aptr->argvalue =
              newprintfd(aptr->argvalue, format, f + 1 - format, d);
          format = f + 1;
          seenpercent = 0;
        }
        break;
      case 's':
        /* string */
        if (seenpercent) {
          char *s;
          s = va_arg(argptr, char *);
          aptr->argvalue =
              newprintfs(aptr->argvalue, format, f + 1 - format, s);
          format = f + 1;
          seenpercent = 0;
        }
        break;
      }
    }
    if (*format != '\0') {
      aptr->argvalue = newprintfx(aptr->argvalue, format, f + 1 - format);
    }
    if (aptr->argvalue != NULL) {
      if (alast) {
        alast->next = aptr;
      } else {
        mptr->args = aptr;
      }
      alast = aptr;
    }
  }
  if (xparent == NULL) {
/* just prepend onto the list */
      mptr->next = (MESSAGE *)FIH_CCFFINFO(fihx);
      FIH_CCFFINFO(fihx) = (void *)mptr;
      FIH_SETFLAG(fihx, FIH_CCFF);
  } else {
    /* append to child list of the parent */
    MESSAGE *parent, *child;
    parent = (MESSAGE *)xparent;
    if (parent->msgchild == NULL) {
      parent->msgchild = mptr;
    } else {
      for (child = parent->msgchild; child->next; child = child->next)
        ;
      child->next = mptr;
    }
  }
  return mptr;
} /* _ccff_info */

/*
 * Save a message
 */
void *
ccff_info(int msgtype, char *msgid, int fihx, int lineno, const char *message,
          ...)
{
  va_list argptr;
  va_start(argptr, message);
  return _ccff_info(msgtype, msgid, fihx, lineno, NULL, NULL, NULL, message,
                    argptr);
} /* ccff_info */

/*
 * Save a message that is more detail for a previous message
 */
void *
subccff_info(void *xparent, int msgtype, char *msgid, int fihx, int lineno,
             const char *message, ...)
{
  va_list argptr;
  va_start(argptr, message);
  return _ccff_info(msgtype, msgid, fihx, lineno, NULL, NULL, xparent, message,
                    argptr);
} /* subccff_info */

/*
 * Save information for a variable symbol
 */
void *
ccff_var_info(int msgtype, char *msgid, char *varname, const char *message, ...)
{
  va_list argptr;
  va_start(argptr, message);
  return _ccff_info(msgtype, msgid, 1, 0, varname, NULL, NULL, message, argptr);
} /* ccff_var_info */

/*
 * Save information for a function symbol
 */
void *
ccff_func_info(int msgtype, char *msgid, char *funcname, const char *message,
               ...)
{
  va_list argptr;
  va_start(argptr, message);
  return _ccff_info(msgtype, msgid, 1, 0, NULL, funcname, NULL, message,
                    argptr);
} /* ccff_func_info */

/*
 * set seq field for most recent message
 */
void
ccff_seq(int seq)
{
  if (prevmessage && seq) {
    prevmessage->seq = seq;
  }
} /* ccff_seq */

static char *nullname = "";

int
addfile(char *filename, char *funcname, int tag, int flags, int lineno,
        int srcline, int level)
{
  int f, len;
  char *pfilename, *slash, *cp, *pfuncname;
  if (fihb.stg_base == NULL) {
    fihb.stg_size = 500;
    NEW(fihb.stg_base, FIH, fihb.stg_size);
    fihb.stg_avail = 1;
    BZERO(fihb.stg_base + 0, FIH, 1);
    FIH_DIRNAME(0) = NULL;
    FIH_FILENAME(0) = nullname;
    FIH_FULLNAME(0) = nullname;
    fihb.nextfindex = 1;
    fihb.nextftag = 0;
    fihb.currfindex = 1;
    fihb.currftag = 0;
  }

  f = fihb.stg_avail++;
  if (f == 1)
    fihb.currfindex = 1;

  NEED(fihb.stg_avail, fihb.stg_base, FIH, fihb.stg_size, fihb.stg_size + 500);
  BZERO(fihb.stg_base + f, FIH, 1);
  /* allocate in permanent area 8 */
  len = strlen(filename);
  pfilename = getitem(8, len + 1);
  strcpy(pfilename, filename);
  FIH_FULLNAME(f) = pfilename;
  /* get directory/file component */
  slash = NULL;
  for (cp = pfilename; *cp; ++cp) {
    if (*cp == '/'
#ifdef HOST_WIN
        || *cp == '\\'
#endif
        ) {
      slash = cp;
    }
  }
  if (!slash) {
    FIH_DIRNAME(f) = NULL;
    FIH_FILENAME(f) = FIH_FULLNAME(f);
  } else {
    /* filename = "/usr/include/stdio.h"
     * len = 20
     * slash = last / */
    int l;
    l = slash - pfilename;
    /* l = 12 */
    if (l == 0)
      l = 1; /* allow for /file */
    FIH_DIRNAME(f) = getitem(8, l + 1);
    strncpy(FIH_DIRNAME(f), pfilename, l);
    FIH_DIRNAME(f)[l] = '\0'; /* strncpy does not terminate string */
    l = slash - pfilename;    /* recompute, in case we incremented l */
    l = len - l;
    /* len-l = 8, but we'll split off the slash,
     * leaving room for the string terminator */
    FIH_FILENAME(f) = getitem(8, l);
    strncpy(FIH_FILENAME(f), slash + 1, l - 1);
    FIH_FILENAME(f)[l - 1] = '\0';
  }
  if (funcname == NULL) {
    FIH_FUNCNAME(f) = nullname;
  } else {
    pfuncname = getitem(8, strlen(funcname) + 1);
    strcpy(pfuncname, funcname);
    FIH_FUNCNAME(f) = pfuncname;
  }
  FIH_FUNCTAG(f) = tag;
  FIH_FLAGS(f) = flags;
  FIH_PARENT(f) = 0;
  FIH_LINENO(f) = lineno;
  FIH_SRCLINE(f) = srcline;
  FIH_LEVEL(f) = 0;
  if (FIH_INC(f))
    FIH_LEVEL(f) = level;
  if (f != fihb.currfindex && fihb.currfindex > 0) {
    FIH_PARENT(f) = fihb.currfindex;
    if (!FIH_INC(f))
      FIH_LEVEL(f) = FIH_LEVEL(fihb.currfindex) + 1;
  }
  return f;
} /* addfile */

/* This function is used in global_inline when importing bottom-up
   auto-inlining information */
int
addinlfile(char *filename, char *funcname, int tag, int flags, int lineno,
           int srcline, int level, int parent)
{
  int f, len;
  char *pfilename, *slash, *cp, *pfuncname;
  if (fihb.stg_base == NULL) {
    fihb.stg_size = 500;
    NEW(fihb.stg_base, FIH, fihb.stg_size);
    fihb.stg_avail = 1;
    BZERO(fihb.stg_base + 0, FIH, 1);
    FIH_DIRNAME(0) = NULL;
    FIH_FILENAME(0) = nullname;
    FIH_FULLNAME(0) = nullname;
    fihb.nextfindex = 1;
    fihb.nextftag = 0;
    fihb.currfindex = 1;
    fihb.currftag = 0;
  }

  f = fihb.stg_avail++;

  NEED(fihb.stg_avail, fihb.stg_base, FIH, fihb.stg_size, fihb.stg_size + 500);
  BZERO(fihb.stg_base + f, FIH, 1);
  /* allocate in permanent area 8 */
  len = strlen(filename);
  pfilename = getitem(8, len + 1);
  strcpy(pfilename, filename);
  FIH_FULLNAME(f) = pfilename;
  /* get directory/file component */
  slash = NULL;
  for (cp = pfilename; *cp; ++cp) {
    if (*cp == '/'
#ifdef HOST_WIN
        || *cp == '\\'
#endif
        ) {
      slash = cp;
    }
  }
  if (!slash) {
    FIH_DIRNAME(f) = NULL;
    FIH_FILENAME(f) = FIH_FULLNAME(f);
  } else {
    /* filename = "/usr/include/stdio.h"
     * len = 20
     * slash = last / */
    int l;
    l = slash - pfilename;
    /* l = 12 */
    if (l == 0)
      l = 1; /* allow for /file */
    FIH_DIRNAME(f) = getitem(8, l + 1);
    strncpy(FIH_DIRNAME(f), pfilename, l);
    FIH_DIRNAME(f)[l] = '\0'; /* strncpy does not terminate string */
    l = slash - pfilename;    /* recompute, in case we incremented l */
    l = len - l;
    /* len-l = 8, but we'll split off the slash,
     * leaving room for the string terminator */
    FIH_FILENAME(f) = getitem(8, l);
    strncpy(FIH_FILENAME(f), slash + 1, l - 1);
    FIH_FILENAME(f)[l - 1] = '\0';
  }
  if (funcname == NULL) {
    FIH_FUNCNAME(f) = nullname;
  } else {
    pfuncname = getitem(8, strlen(funcname) + 1);
    strcpy(pfuncname, funcname);
    FIH_FUNCNAME(f) = pfuncname;
  }
  FIH_FUNCTAG(f) = tag;
  FIH_FLAGS(f) = flags;
  FIH_LINENO(f) = lineno;
  FIH_SRCLINE(f) = srcline;
  FIH_LEVEL(f) = level;
  FIH_PARENT(f) = parent;
  FIH_CCFFINFO(f) = NULL;
  return f;
} /* addinlfile */

int
subfih(int fihindex, int tag, int flags, int lineno)
{
  int f;
  if (fihb.stg_base == NULL) {
    return 0;
  }

  f = fihb.stg_avail++;

  NEED(fihb.stg_avail, fihb.stg_base, FIH, fihb.stg_size, fihb.stg_size + 500);
  BZERO(fihb.stg_base + f, FIH, 1);
  /* allocate in permanent area 8 */
  FIH_FULLNAME(f) = FIH_FULLNAME(fihindex);
  FIH_FILENAME(f) = FIH_FILENAME(fihindex);
  FIH_DIRNAME(f) = FIH_DIRNAME(fihindex);
  FIH_FUNCNAME(f) = FIH_FUNCNAME(fihindex);
  FIH_FUNCTAG(f) = tag;
  FIH_FLAGS(f) = flags;
  FIH_PARENT(f) = fihindex;
  FIH_LINENO(f) = lineno;
  FIH_LEVEL(f) = FIH_LEVEL(fihindex) + 1;
  return f;
} /* subfih */

void
setfile(int f, char *funcname, int tag)
{
  char *pfuncname;
  LOGICAL firsttime = TRUE;
  if (funcname == NULL) {
    FIH_FUNCNAME(f) = nullname;
  } else if (f == 1 && FIH_FUNCNAME(f) &&
             strcmp(funcname, FIH_FUNCNAME(f)) == 0) {
    firsttime = FALSE;
  } else {
    pfuncname = getitem(8, strlen(funcname) + 1);
    strcpy(pfuncname, funcname);
    FIH_FUNCNAME(f) = pfuncname;
/*	if( f == 1 ){
            fihb.stg_avail = 2;
        } */
  }
  if (firsttime) {
    FIH_FLAGS(f) = 0;
    FIH_CCFFINFO(f) = NULL;
  }
  FIH_LINENO(f) = gbl.lineno;
  if (tag >= 0) {
    FIH_FUNCTAG(f) = tag;
  } else {
    FIH_FUNCTAG(f) = 0;
  }
} /* setfile */

/*
 * save the high water mark of the fihb structure
 * we do this in C/C++ after parsing, when we have all the included
 * files, but before the expander, before we do any inlining
 * Then, before each routine, we restore fihb.stg_avail to the
 * high water mark, essentially eliminating the inlining information
 * from the previous program unit
 */
void
save_ccff_mark()
{
  fihb.saved_avail = fihb.stg_avail;
} /* save_ccff_mark */

void
restore_ccff_mark()
{
  /* per flyspray 15759, we must not shrink fihb.stg_avail because
   * dwarf2.c may use file information from the previous compile unit
   * and therefore we must keep the file information around.  We output
   * dwarf file information include header/directory header at the end
   * of compilation each file, not per routine.  If we shrink it, file
   * information could be incorrect because we may refer to file index
   * that got shrunk in dwarf2.c and may  be replaced with other file.
   * ccff_cleanup_children() should cleanup FIH_CCFFINFO
   * Remove: fihb.stg_avail = fihb.saved_avail;
   */
  int fihx;
  for (fihx = fihb.saved_avail; fihx < fihb.stg_avail; ++fihx) {
    FIH_PARENT(fihx) = 0;
  }

} /* restore_ccff_mark */

/* save and restore files */

/* If passing argument; 0 is save, 1 is to retrive file indexes. */
void
set_allfiles(int save)
{
  static int save_curr = 1;
  static int save_next = 1;
  static int save_findex = 1;
  if (save == 0) {
    save_curr = fihb.currfindex;
    save_next = fihb.nextfindex;
    save_findex = gbl.findex;
  } else {
    fihb.currfindex = save_curr;
    fihb.nextfindex = save_next;
    gbl.findex = save_findex;
  }
}

/*
 * for Fortran front end, process and save messages for back end to emit
 */

/*
 * output messages for this FIH tag
 */
static void
lower_fih_messages(int fihx, FILE *lfile, int nest)
{
  int child;
  MESSAGE *mptr, *firstmptr;
  /* until we productize high-level inlining, this isn't so important */
  if (fihx > 1) {
    if (FIH_CHECKFLAG(fihx, FIH_INLINED)) {
      fprintf(lfile, "CCFFinl seq:%d level:%d line:%d srcline:%d %d:%s %d:%s\n",
              fihx, FIH_LEVEL(fihx), FIH_LINENO(fihx), FIH_SRCLINE(fihx),
              (int)strlen(FIH_FUNCNAME(fihx)), FIH_FUNCNAME(fihx),
              (int)strlen(FIH_FULLNAME(fihx)), FIH_FULLNAME(fihx));
    }
  }

  if (!FIH_CHECKFLAG(fihx, FIH_CCFF)) {
    if (FIH_CHECKFLAG(fihx, FIH_INLINED)) {
      if (fihx > 1)
        fprintf(lfile, "CCFFlni\n");
    }
  }

  prevnest = -1;
  prevchildnest = -1;
  child = FIH_CHILD(fihx);
  firstmptr = (MESSAGE *)FIH_CCFFINFO(fihx);
  if (child || firstmptr) {
    for (mptr = firstmptr; mptr; mptr = mptr->next) {
      while (child && FIH_LINENO(child) < mptr->lineno) {
        fih_messages(child, lfile, nest + 1);
        child = FIH_NEXT(child);
      }
      fprintf(lfile, "CCFFmsg seq:%d lineno:%d type:%d %d:%s %d:%s %d:%s\n",
              mptr->seq, mptr->lineno, mptr->msgtype,
              mptr->varname ? (int)strlen(mptr->varname) : 0,
              mptr->varname ? mptr->varname : "",
              mptr->funcname ? (int)strlen(mptr->funcname) : 0,
              mptr->funcname ? mptr->funcname : "", (int)strlen(mptr->msgid),
              mptr->msgid);
      if (mptr->args) {
        ARGUMENT *aptr;
        for (aptr = mptr->args; aptr; aptr = aptr->next) {
          fprintf(lfile, "CCFFarg %d:%s %d:%s\n", (int)strlen(aptr->argstring),
                  aptr->argstring, (int)strlen(aptr->argvalue), aptr->argvalue);
        }
      }
      fprintf(lfile, "CCFFtxt %s\n", mptr->message);
      if (XBIT(0, 0x8000000)) {
        fprintf(stderr, "%7d, ", mptr->lineno);
        _fih_message(stderr, mptr, FALSE);
        fprintf(stderr, "\n");
      }
    }
    for (; child; child = FIH_NEXT(child)) {
      fih_messages(child, lfile, nest + 1);
    }
  }

  if (fihx > 1)
    if (FIH_CHECKFLAG(fihx, FIH_INLINED)) {
      fprintf(lfile, "CCFFlni\n");
    }

} /* lower_fih_messages */

void
ccff_lower(FILE *lfile)
{
  if (unitstatus < 0 || (fihb.stg_avail == 2 && !FIH_CHECKFLAG(1, FIH_CCFF))) {
    /* ccff not being saved, or no inlining and no messages */
    return;
  }
  ccff_set_children();
  fprintf(lfile, "CCFF\n");
  lower_fih_messages(1, lfile, 0);
  fprintf(lfile, "CCFFend\n");
} /* ccff_lower */

void
fih_fini()
{
  if (fihb.stg_base)
    FREE(fihb.stg_base);
  fihb.stg_base = NULL;
  fihb.stg_avail = 0;
  fihb.stg_size = 0;
} /* fih_fini */

/* debugging helper functions */
void
print_fih()
{
  int i;
  MESSAGE *temp;
  printf("************************************************\n");
  for (i = 0; i < fihb.stg_avail; i++) {
    printf("-FIH:%d file:%s name:%s flag:%d level:%d parent:%d child:%d "
           "next:%d ccffinfo:%p\n",
           i, FIH_FILENAME(i), FIH_FUNCNAME(i), FIH_FLAGS(i), FIH_LEVEL(i),
           FIH_PARENT(i), FIH_CHILD(i), FIH_NEXT(i), FIH_CCFFINFO(i));
    temp = (MESSAGE *)FIH_CCFFINFO(i);
    if (temp)
      printf("\n--File message:%s\n", temp->message);
  }
  printf("************************************************\n");
}

void
print_ifih()
{
  int i;
  MESSAGE *temp;
  printf("************************************************\n");
  for (i = 0; i < ifihb.stg_avail; i++) {
    printf("-IFIH:%d file:%s name:%s flag:%d level:%d parent:%d child:%d "
           "next:%d lineno:%d ccffinfo:%p\n",
           i, IFIH_FILENAME(i), IFIH_FUNCNAME(i), IFIH_FLAGS(i), IFIH_LEVEL(i),
           IFIH_PARENT(i), IFIH_CHILD(i), IFIH_NEXT(i), IFIH_LINENO(i),
           IFIH_CCFFINFO(i));
    temp = (MESSAGE *)IFIH_CCFFINFO(i);
    if (temp)
      printf("\n--File message:%s\n", temp->message);
  }
  printf("************************************************\n");
}
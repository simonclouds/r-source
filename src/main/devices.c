/*
 *  R : A Computer Language for Statistical Data Analysis
 *  Copyright (C) 1998	Robert Gentleman and Ross Ihaka
 *  Copyright (C) 1998-2001   The R Development Core Team.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <Defn.h>
#include <Rmath.h>
#include <Graphics.h>
#include <Rdevices.h>


/* Return a non-relocatable copy of a string */

static SEXP gcall;
static char *SaveString(SEXP sxp, int offset)
{
    char *s;
    if(!isString(sxp) || length(sxp) <= offset)
	errorcall(gcall, "invalid string argument");
    s = R_alloc(strlen(CHAR(STRING_ELT(sxp, offset)))+1, sizeof(char));
    strcpy(s, CHAR(STRING_ELT(sxp, offset)));
    return s;
}

/*  PostScript Device Driver Parameters:
 *  ------------------------		--> devPS.c
 *  file	= output filename
 *  paper	= paper type
 *  family	= typeface = "family"
 *  encoding	= char encoding file name
 *  bg		= background color
 *  fg		= foreground color
 *  width	= width in inches
 *  height	= height in inches
 *  horizontal	= {TRUE: landscape; FALSE: portrait}
 *  ps		= pointsize
 *  onefile     = {TRUE: normal; FALSE: single EPSF page}
 *  pagecentre  = centre plot region on paper?
 *  printit     = `print' after closing device?
 *  command     = `print' command
 */

SEXP do_PS(SEXP call, SEXP op, SEXP args, SEXP env)
{
    DevDesc *dd;
    char *vmax;
    char *file, *paper, *family=NULL, *bg, *fg, *cmd;
    char *afms[4], *encoding;
    int i, horizontal, onefile, pagecentre, printit;
    double height, width, ps;
    SEXP fam;

    gcall = call;
    vmax = vmaxget();
    file = SaveString(CAR(args), 0);  args = CDR(args);
    paper = SaveString(CAR(args), 0); args = CDR(args);

    /* `family' can be either one string or a 4-vector of afmpaths. */
    fam = CAR(args); args = CDR(args);
    if(length(fam) == 1) 
	family = SaveString(fam, 0);
    else if(length(fam) == 4) {
	if(!isString(fam)) errorcall(call, "invalid `family' parameter");
	family = "User";
	for(i = 0; i < 4; i++) afms[i] = SaveString(fam, i);
    } else 
	errorcall(call, "invalid `family' parameter");
    
    encoding = SaveString(CAR(args), 0);    args = CDR(args);
    bg = SaveString(CAR(args), 0);    args = CDR(args);
    fg = SaveString(CAR(args), 0);    args = CDR(args);
    width = asReal(CAR(args));	      args = CDR(args);
    height = asReal(CAR(args));	      args = CDR(args);
    horizontal = asLogical(CAR(args));args = CDR(args);
    if(horizontal == NA_LOGICAL)
	horizontal = 1;
    ps = asReal(CAR(args));	      args = CDR(args);
    onefile = asLogical(CAR(args));   args = CDR(args);
    pagecentre = asLogical(CAR(args));args = CDR(args);
    printit = asLogical(CAR(args));   args = CDR(args);
    cmd = SaveString(CAR(args), 0);

    R_CheckDeviceAvailable();
    BEGIN_SUSPEND_INTERRUPTS {
	if (!(dd = (DevDesc *) malloc(sizeof(DevDesc))))
	    return 0;
	/* Do this for early redraw attempts */
	dd->displayList = R_NilValue;
	GInit(&dd->dp);
	if(!PSDeviceDriver(dd, file, paper, family, afms, encoding, bg, fg,
			   width, height, (double)horizontal, ps, onefile,
			   pagecentre, printit, cmd)) {
	    free(dd);
	    errorcall(call, "unable to start device PostScript");
	}
	gsetVar(install(".Device"), mkString("postscript"), R_NilValue);
	addDevice(dd);
	initDisplayList(dd);
    } END_SUSPEND_INTERRUPTS;
    vmaxset(vmax);
    return R_NilValue;
}

/*  PicTeX Device Driver Parameters
 *  --------------------		--> devPicTeX.c
 *  file    = output filename
 *  bg	    = background color
 *  fg	    = foreground color
 *  width   = width in inches
 *  height  = height in inches
 *  debug   = Rboolean; if TRUE, write TeX-Comments into output.
 */

SEXP do_PicTeX(SEXP call, SEXP op, SEXP args, SEXP env)
{
    DevDesc *dd;
    char *vmax;
    char *file, *bg, *fg;
    double height, width;
    Rboolean debug;

    gcall = call;
    vmax = vmaxget();
    file = SaveString(CAR(args), 0); args = CDR(args);
    bg = SaveString(CAR(args), 0);   args = CDR(args);
    fg = SaveString(CAR(args), 0);   args = CDR(args);
    width = asReal(CAR(args));	     args = CDR(args);
    height = asReal(CAR(args));	     args = CDR(args);
    debug = asLogical(CAR(args));    args = CDR(args);
    if(debug == NA_LOGICAL) debug = FALSE;

    R_CheckDeviceAvailable();
    BEGIN_SUSPEND_INTERRUPTS {
	if (!(dd = (DevDesc *) malloc(sizeof(DevDesc))))
	    return 0;
	/* Do this for early redraw attempts */
	dd->displayList = R_NilValue;
	GInit(&dd->dp);
	if(!PicTeXDeviceDriver(dd, file, bg, fg, width, height, debug)) {
	    free(dd);
	    errorcall(call, "unable to start device PicTeX");
	}
	gsetVar(install(".Device"), mkString("pictex"), R_NilValue);
	addDevice(dd);
	initDisplayList(dd);
    } END_SUSPEND_INTERRUPTS;
    vmaxset(vmax);
    return R_NilValue;
}



/*  XFig Device Driver Parameters:
 *  ------------------------		--> devPS.c
 *  file	= output filename
 *  paper	= paper type
 *  family	= typeface = "family"
 *  bg		= background color
 *  fg		= foreground color
 *  width	= width in inches
 *  height	= height in inches
 *  horizontal	= {TRUE: landscape; FALSE: portrait}
 *  ps		= pointsize
 *  onefile     = {TRUE: normal; FALSE: single EPSF page}
 *  pagecentre  = centre plot region on paper?
 */

SEXP do_XFig(SEXP call, SEXP op, SEXP args, SEXP env)
{
    DevDesc *dd;
    char *vmax;
    char *file, *paper, *family, *bg, *fg;
    int horizontal, onefile, pagecentre;
    double height, width, ps;
    gcall = call;
    vmax = vmaxget();
    file = SaveString(CAR(args), 0);  args = CDR(args);
    paper = SaveString(CAR(args), 0); args = CDR(args);
    family = SaveString(CAR(args), 0);  args = CDR(args);
    bg = SaveString(CAR(args), 0);    args = CDR(args);
    fg = SaveString(CAR(args), 0);    args = CDR(args);
    width = asReal(CAR(args));	      args = CDR(args);
    height = asReal(CAR(args));	      args = CDR(args);
    horizontal = asLogical(CAR(args));args = CDR(args);
    if(horizontal == NA_LOGICAL)
	horizontal = 1;
    ps = asReal(CAR(args));	      args = CDR(args);
    onefile = asLogical(CAR(args));   args = CDR(args);
    pagecentre = asLogical(CAR(args));

    R_CheckDeviceAvailable();
    BEGIN_SUSPEND_INTERRUPTS {
	if (!(dd = (DevDesc *) malloc(sizeof(DevDesc))))
	    return 0;
	/* Do this for early redraw attempts */
	dd->displayList = R_NilValue;
	GInit(&dd->dp);
	if(!XFigDeviceDriver(dd, file, paper, family, bg, fg, width, height,
			     (double)horizontal, ps, onefile, pagecentre)) {
	    free(dd);
	    errorcall(call, "unable to start device xfig");
	}
	gsetVar(install(".Device"), mkString("xfig"), R_NilValue);
	addDevice(dd);
	initDisplayList(dd);
    } END_SUSPEND_INTERRUPTS;
    vmaxset(vmax);
    return R_NilValue;
}


/*  PDF Device Driver Parameters:
 *  ------------------------		--> devPS.c
 *  file	= output filename
 *  family	= typeface = "family"
 *  encoding	= char encoding file name
 *  bg		= background color
 *  fg		= foreground color
 *  width	= width in inches
 *  height	= height in inches
 *  ps		= pointsize
 */

SEXP do_PDF(SEXP call, SEXP op, SEXP args, SEXP env)
{
    DevDesc *dd;
    char *vmax;
    char *file, *encoding, *family, *bg, *fg;
    double height, width, ps;
    int onefile;

    gcall = call;
    vmax = vmaxget();
    file = SaveString(CAR(args), 0);  args = CDR(args);
    family = SaveString(CAR(args), 0);  args = CDR(args);
    encoding = SaveString(CAR(args), 0);  args = CDR(args);
    bg = SaveString(CAR(args), 0);    args = CDR(args);
    fg = SaveString(CAR(args), 0);    args = CDR(args);
    width = asReal(CAR(args));	      args = CDR(args);
    height = asReal(CAR(args));	      args = CDR(args);
    ps = asReal(CAR(args));           args = CDR(args);
    onefile = asLogical(CAR(args));

    R_CheckDeviceAvailable();
    BEGIN_SUSPEND_INTERRUPTS {
	if (!(dd = (DevDesc *) malloc(sizeof(DevDesc))))
	    return 0;
	/* Do this for early redraw attempts */
	dd->displayList = R_NilValue;
	GInit(&dd->dp);
	if(!PDFDeviceDriver(dd, file, family, encoding, bg, fg, 
			    width, height, ps, onefile)) {
	    free(dd);
	    errorcall(call, "unable to start device pdf");
	}
	gsetVar(install(".Device"), mkString("pdf"), R_NilValue);
	addDevice(dd);
	initDisplayList(dd);
    } END_SUSPEND_INTERRUPTS;
    vmaxset(vmax);
    return R_NilValue;
}
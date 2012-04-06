/*
 *  R : A Computer Language for Statistical Data Analysis
 *  Copyright (C) 1995-1996 Robert Gentleman and Ross Ihaka
 *  Copyright (C) 1997-2001 The R Development Core Team
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


/* This provides a table of built-in C and Fortran functions.
   We include this table, even when we have dlopen and friends.
   This is so that the functions are actually loaded at link time. */


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include "Defn.h"
#include "R_ext/Rdynpriv.h"

#ifdef HAVE_DLFCN_H
#include <dlfcn.h>
#else
#ifdef HAVE_DL_H
#include "hpdlfcn.c"
#define HAVE_DLFCN_H
#endif
#endif


#include "FFDecl.h"

static CFunTabEntry CFunTab[] =
{
#include "FFTab.h"
    {NULL, NULL}
};

#ifdef HAVE_DLFCN_H

static void *loadLibrary(const char *path, int asLocal, int now);
static void closeLibrary(void *handle);
static void deleteCachedSymbols(DllInfo *);
static DL_FUNC R_dlsym(DllInfo *info, char const *name);
static void getFullDLLPath(SEXP call, char *buf, char *path);
static DL_FUNC getBaseSymbol(const char *name);
static void getSystemError(char *buf, int len);

static int computeDLOpenFlag(int asLocal, int now);

void InitFunctionHashing()
{
#ifdef DL_SEARCH_PROG
    baseDLL.handle = dlopen(0, RTLD_NOW);
#endif

    R_osDynSymbol->loadLibrary = loadLibrary;
    R_osDynSymbol->dlsym = R_dlsym;
    R_osDynSymbol->closeLibrary = closeLibrary;
    R_osDynSymbol->getError = getSystemError;
    R_osDynSymbol->getBaseSymbol = getBaseSymbol;

    R_osDynSymbol->deleteCachedSymbols = deleteCachedSymbols;
    R_osDynSymbol->lookupCachedSymbol = Rf_lookupCachedSymbol;

    R_osDynSymbol->CFunTab = CFunTab;
    R_osDynSymbol->getFullDLLPath = getFullDLLPath;
}

static void getSystemError(char *buf, int len)
{
    strcpy(buf, dlerror());
}

static void *loadLibrary(const char *path, int asLocal, int now)
{
    void *handle;
    int openFlag = 0;

    openFlag = computeDLOpenFlag(asLocal, now);
    handle = (void *) dlopen(path,openFlag);

    return(handle);
}

static void closeLibrary(HINSTANCE handle)
{
    dlclose(handle);
}

 /*
   If we are caching the native level symbols, this routine
   discards the ones from the DLL identified by loc.
   This is called as the initial action of DeleteDLL().
  */
static void deleteCachedSymbols(DllInfo *dll)
{
#ifdef CACHE_DLL_SYM
#ifdef Macintosh
    /* This goes in a different order than the Unix version. */
    for(i = 0; i < nCPFun; i++)
	if(!strcmp(CPFun[i].pkg, dll->name)) {
	    strcpy(CPFun[i].pkg, CPFun[nCPFun].pkg);
	    strcpy(CPFun[i].name, CPFun[nCPFun].name);
	    CPFun[i].func = CPFun[nCPFun--].func;
	}
#else /* Not Macintosh, so Unix */
    int i;
    /* Wouldn't a linked list be easier here?
       Potentially ruin the contiguity of the memory.
    */
    for(i = nCPFun - 1; i >= 0; i--)
	if(!strcmp(CPFun[i].pkg, dll->name)) {
	    if(i < nCPFun - 1) {
		strcpy(CPFun[i].name, CPFun[--nCPFun].name);
		strcpy(CPFun[i].pkg, CPFun[nCPFun].pkg);
		CPFun[i].func = CPFun[nCPFun].func;
	    } else nCPFun--;
	}
#endif /* Macintosh */
#endif /* CACHE_DLL_SYM */
}


static DL_FUNC getBaseSymbol(const char *name)
{
#ifdef DL_SEARCH_PROG
    DL_FUNC fcnptr;

    fcnptr = R_osDynSymbol->dlsym(&baseDll, name);
    return(fcnptr);
#else
    int i;

    for(i = 0 ; R_osDynSymbol->CFunTab[i].name ; i++)
	if(!strcmp(name, R_osDynSymbol->CFunTab[i].name))
	    return R_osDynSymbol->CFunTab[i].func;

    return((DL_FUNC) NULL);
#endif
}



 /*
    Computes the flag to be passed as the second argument to dlopen(),
    controlling whether the local or global symbol integration
    and lazy or eager resolution of the undefined symbols.
    The arguments determine which of each of these possibilities
    to use and the results are or'ed together. We need a separate
    routine to keep things clean(er) because some symbolic constants
    may not  be defined, such as RTLD_LOCAL on certain Solaris 2.5.1
    and Irix 6.4    boxes. In such cases, we emit a warning message and
    use the default by not modifying the value of the flag.

    Called only by AddDLL().
  */
static int computeDLOpenFlag(int asLocal, int now)
{
#if !defined(RTLD_LOCAL) || !defined(RTLD_GLOBAL) || !defined(RTLD_NOW) || !defined(RTLD_LAZY)
    static char *warningMessages[] = {
	"Explicit local dynamic loading not supported on this platform. Using default.",
	"Explicit global dynamic loading not supported on this platform. Using default.",
	"Explicit non-lazy dynamic loading not supported on this platform. Using default.",
	"Explicit lazy dynamic loading not supported on this platform. Using default."
    };
    /* Define a local macro for issuing the warnings.
       This allows us to redefine it easily so that it only emits the
       warning once as in
       DL_WARN(i) if(warningMessages[i]) {\
       warning(warningMessages[i]); \
       warningMessages[i] = NULL; \
       }
       or to control the emission via the options currently in effect at
       call time.
    */
# define DL_WARN(i) \
    if(asInteger(GetOption(install("warn"), R_NilValue)) == 1 || \
       asInteger(GetOption(install("verbose"), R_NilValue)) > 0) \
        warning(warningMessages[i]);
#endif

    int openFlag = 0;		/* Default value so no-ops for undefined
				   flags should do nothing in the
				   resulting dlopen(). */

    if(asLocal != 0) {
#ifndef RTLD_LOCAL
	DL_WARN(0)
#else
	    openFlag = RTLD_LOCAL;
#endif
    } else {
#ifndef RTLD_GLOBAL
	DL_WARN(1)
#else
	    openFlag = RTLD_GLOBAL;
#endif
    }

    if(now != 0) {
#ifndef RTLD_NOW
	DL_WARN(2)
#else
	    openFlag |= RTLD_NOW;
#endif
    } else {
#ifndef RTLD_LAZY
	DL_WARN(3)
#else
	    openFlag |= RTLD_LAZY;
#endif
    }

    return(openFlag);
}


/*
  This is the system/OS-specific version for resolving a
  symbol in a shared library.
 */
static DL_FUNC R_dlsym(DllInfo *info, char const *name)
{
    return (DL_FUNC) dlsym(info->handle, name);
}


/*
  In the future, this will receive an additional argument
  which will specify the nature of the symbol expected by the
  caller, specifically whether it is for a .C(), .Call(),
  .Fortran(), .External(), generic, etc. invocation. This will
  reduce the pool of possible symbols in the case of a library
  that registers its routines.
 */



static void getFullDLLPath(SEXP call, char *buf, char *path)
{
#ifdef Macintosh
    if(path[0] != ':') {
	if(R_Home == NULL){
	    if (!getcwd(buf, PATH_MAX))
		errorcall(call, "can't get working directory!");
	    strcat(buf, path);
	}
	else
	    strcpy(buf,path);
    } else
	strcpy(buf, path);
    return;

#else /* Macintosh */

    if(path[0] == '~')
	strcpy(buf, R_ExpandFileName(path));
    else if(path[0] != '/') {
#ifdef HAVE_UNISTD_H
	if(!getcwd(buf, PATH_MAX))
#endif
	    errorcall(call, "can't get working directory!");
	strcat(buf, "/");
	strcat(buf, path);
    }
    else strcpy(buf, path);
#endif
}

#endif /* end of `ifdef HAVE_DLFCN_H' */
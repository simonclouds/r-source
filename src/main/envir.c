/*
 *  R : A Computer Language for Statistical Data Analysis
 *  Copyright (C) 1995, 1996  Robert Gentleman and Ross Ihaka
 *  Copyright (C) 1999,2000 the R Development Core Group.
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
 *
 *
 *
 *  Environments:
 *
 *  All the action of associating values with symbols happens
 *  in this code.  An environment is (essentially) a list of
 *  environment "frames" of the form
 *
 *	FRAME(envir) = environment frame
 *	ENCLOS(envir) = parent environment
 *	HASHTAB(envir) = (optional) hash table
 *
 *  Each frame is a (tagged) list with
 *
 *	TAG(item) = symbol
 *	CAR(item) = value bound to symbol in this frame
 *	CDR(item) = next value on the list
 *
 *  When the value of a symbol is required, the environment is
 *  traversed frame-by-frame until a value is found.
 *
 *  If a value is not found during the traversal, the symbol's
 *  "value" slot is inspected for a value.  This "top-level"
 *  environment is where system functions and variables reside.
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "Defn.h"

/*----------------------------------------------------------------------

  Hash Tables

  We use a basic se[parate chaining algorithm.	A hash table consists
  of SEXP (vector) which contains a number of SEXPs (lists).

*/

#define HASHSIZE(x)	     LENGTH(x)
#define HASHPRI(x)	     TRUELENGTH(x)
#define HASHTABLEGROWTHRATE  1.2
#define HASHMINSIZE	     29
#define SET_HASHSIZE(x,v)    SETLENGTH(x,v)
#define SET_HASHPRI(x,v)     SET_TRUELENGTH(x,v)

#define IS_HASHED(x)	     (HASHTAB(x) != R_NilValue)

/*----------------------------------------------------------------------

  String Hashing

  This is taken from the second edition of the "Dragon Book" by
  Aho, Ullman and Sethi.

*/

extern int R_Newhashpjw(char *s)
{
    char *p;
    unsigned h = 0, g;
    for (p = s; *p; p = p + 1) {
	h = (h << 4) + (*p);
	if ((g = h & 0xf0000000) != 0) {
	    h = h ^ (g >> 24);
	    h = h ^ g;
	}
    }
    return h;
}

/*----------------------------------------------------------------------

  R_HashSet

  Hashtable set function.  Sets 'symbol' in 'table' to be 'value'.
  'hashcode' must be provided by user.	Allocates some memory for list
  entries.

  At some point we need to remove the sanity checks here.  This
  code is going to be called a lot and the places it is called
  from are very controlled.

*/

void R_HashSet(int hashcode, SEXP symbol, SEXP table, SEXP value)
{
    SEXP chain;

    /* Do some checking */
    if (TYPEOF(table) != VECSXP) {
	error("3rd arg (table) not of type VECSXP, from R_HashSet");
    }
    if (isNull(table)) {
	error("Table is null, from R_HashSet");
    }
    /* Grab the chain from the hashtable */
    chain = VECTOR_ELT(table, hashcode);
    if (isNull(chain)) {
	SET_HASHPRI(table, HASHPRI(table) + 1);
    }
    /* Add the value into the chain */
    for (; !isNull(chain); chain = CDR(chain)) {
	if (TAG(chain) == symbol) {
	    SETCAR(chain, value);
	    return;
	}
    }
    SET_VECTOR_ELT(table, hashcode, CONS(value, VECTOR_ELT(table, hashcode)));
    SET_TAG(VECTOR_ELT(table, hashcode), symbol);
    return;
}



/*----------------------------------------------------------------------

  R_HashGet

  Hashtable get function.  Returns 'value' from 'table' indexed by
  'symbol'.  'hashcode' must be provided by user.  Returns
  'R_UnboundValue' if value is not present.

*/

SEXP R_HashGet(int hashcode, SEXP symbol, SEXP table)
{
    SEXP chain;

#if 0
/* Removed by pd -- never seen the "3rd arg" stuff and when the table
   is a VECSXP it can't be NULL...*/

    /* Do type checking */
    if (TYPEOF(table) != VECSXP){
	printf("3rd arg (table) not of type VECSXP, from R_HashGet\n");
    }

    if (isNull(table)) {
	error("Table is null, from R_HashGet");
    }
#endif
    /* Grab the chain from the hashtable */
    chain = VECTOR_ELT(table, hashcode);
    /* Retrieve the value from the chain */
    for (; chain != R_NilValue ; chain = CDR(chain)) {
	if (TAG(chain) == symbol) {
	    return CAR(chain);
	}
    }
    /* If not found */
    return R_UnboundValue;
}



/*----------------------------------------------------------------------

  R_HashGetLoc

  Hashtable get location function. Just like R_HashGet, but returns
  location of variable, rather than its value. Returns R_NilValue
  if not found.

*/

SEXP R_HashGetLoc(int hashcode, SEXP symbol, SEXP table)
{
    SEXP chain;

    /* Do type checking */
    if (TYPEOF(table) != VECSXP){
	printf("3rd arg (table) not of type VECSXP, from R_HashGet\n");
    }
    if (isNull(table)) {
	error("Table is null, from R_HashGet");
    }
    /* Grab the chain from the hashtable */
    chain = VECTOR_ELT(table, hashcode);
    /* Retrieve the value from the chain */
    for (; !isNull(chain); chain = CDR(chain)) {
	if (TAG(chain) == symbol) {
	    return chain;
	}
    }
    /* If not found */
    return R_NilValue;
}



/*----------------------------------------------------------------------

  R_NewHashTable

  Hash table initialisation function.  Creates a table of size 'size'
  that increases in size by 'growth_rate' after a threshold is met.

*/

SEXP R_NewHashTable(int size, int growth_rate)
{
    SEXP table;

    /* Some checking */
    if (growth_rate == 0) {
	error("Hash table growth rate must be > 0");
    }
    if (size == 0) {
	size = HASHMINSIZE;
    }
    /* Allocate hash table in the form of a vector */
    PROTECT(table = allocVector(VECSXP, size));
    SET_HASHSIZE(table, size);
    SET_HASHPRI(table, 0);
    UNPROTECT(1);
    return(table);
}



/*----------------------------------------------------------------------

  R_HashDelete

  Hash table delete function.  Symbols are not removed from the table.
  They have their value set to 'R_UnboundValue'.

*/

static SEXP DeleteItem(SEXP symbol, SEXP lst)
{
    if (lst != R_NilValue) {
	SETCDR(lst, DeleteItem(symbol, CDR(lst)));
	if (TAG(lst) == symbol)
	    lst = CDR(lst);
    }
    return lst;
}

void R_HashDelete(int hashcode, SEXP symbol, SEXP table)
{
    SET_VECTOR_ELT(table, hashcode % HASHSIZE(table),
	DeleteItem(symbol, VECTOR_ELT(table, hashcode % HASHSIZE(table))));
    return;
}



/*----------------------------------------------------------------------

  R_HashResize

  Hash table resizing function Increase the size of the hash table by
  the growth_rate of the table.	 The vector is reallocated, however
  the lists with in the hash table have their pointers shuffled around
  so that they are not reallocated.

*/

SEXP R_HashResize(SEXP table)
{
    SEXP new_table, chain, new_chain, tmp_chain;
    int /*hash_grow,*/ counter, new_hashcode;

    /* Do some checking */
    if (TYPEOF(table) != VECSXP) {
	error("1st arg (table) not of type VECSXP,  from R_HashResize");
    }

    /* This may have to change.	 The growth rate should
       be independent of the size (not implemented yet) */
    /* hash_grow = HASHSIZE(table); */

    /* Allocate the new hash table */
    new_table = R_NewHashTable(HASHSIZE(table) * HASHTABLEGROWTHRATE,
			       HASHTABLEGROWTHRATE);
    for (counter = 0; counter < length(table); counter++) {
	chain = VECTOR_ELT(table, counter);
	while (!isNull(chain)) {
	    new_hashcode = R_Newhashpjw(CHAR(PRINTNAME(TAG(chain)))) %
		HASHSIZE(new_table);
	    new_chain = VECTOR_ELT(new_table, new_hashcode);
	    /* If using a primary slot then increase HASHPRI */
	    if (isNull(new_chain))
		SET_HASHPRI(new_table, HASHPRI(new_table) + 1);
	    tmp_chain = chain;
	    chain = CDR(chain);
	    SETCDR(tmp_chain, new_chain);
	    SET_VECTOR_ELT(new_table, new_hashcode,  tmp_chain);
#ifdef MIKE_DEBUG
	    fprintf(stdout, "HASHSIZE = %d\nHASHPRI = %d\ncounter = %d\nHASHCODE = %d\n",
		    HASHSIZE(table), HASHPRI(table), counter, new_hashcode);
#endif
	}
    }
    /* Some debugging statements */
#ifdef MIKE_DEBUG
    fprintf(stdout, "Resized O.K.\n");
    fprintf(stdout, "Old size: %d, New size: %d\n",
	    HASHSIZE(table), HASHSIZE(new_table));
    fprintf(stdout, "Old pri: %d, New pri: %d\n",
	    HASHPRI(table), HASHPRI(new_table));
#endif
    return new_table;
} /* end R_HashResize */



/*----------------------------------------------------------------------

  R_HashSizeCheck

  Hash table size rechecking function.	Compares the load factor
  (size/# of primary slots used).  to a praticular threshhold value.
  Returns true if the table needs to be resized.

*/

int R_HashSizeCheck(SEXP table)
{
    int resize;
    double thresh_val;

    /* Do some checking */
    if (TYPEOF(table) != VECSXP){
	error("1st arg (table) not of type VECSXP, R_HashSizeCheck");
    }
    resize = 0; thresh_val = 0.85;
    if ((double)HASHPRI(table) > (double)HASHSIZE(table) * thresh_val)
	resize = 1;
    return resize;
}



/*----------------------------------------------------------------------

  R_HashFrame

  Hashing for environment frames.  This function ensures that the
  first frame in the given environment has been hashed.	 Ultimately
  all enironments should be created in hashed form.  At that point
  this function will be redundant.

*/

SEXP R_HashFrame(SEXP rho)
{
    int hashcode;
    SEXP frame, chain, tmp_chain, table;

    /* Do some checking */
    if (TYPEOF(rho) != ENVSXP) {
	error("1st arg (table) not of type ENVSXP, from R_HashVector2Hash");
    }
    table = HASHTAB(rho);
    frame = FRAME(rho);
    while (!isNull(frame)) {
	if( !HASHASH(PRINTNAME(TAG(frame))) ) {
	    SET_HASHVALUE(PRINTNAME(TAG(frame)),
			  R_Newhashpjw(CHAR(PRINTNAME(TAG(frame))))); 
	    SET_HASHASH(PRINTNAME(TAG(frame)), 1);
	}
	hashcode = HASHVALUE(PRINTNAME(TAG(frame))) % HASHSIZE(table);
	chain = VECTOR_ELT(table, hashcode);
	/* If using a primary slot then increase HASHPRI */
	if (isNull(chain)) SET_HASHPRI(table, HASHPRI(table) + 1);
	tmp_chain = frame;
	frame = CDR(frame);
	SETCDR(tmp_chain, chain);
	SET_VECTOR_ELT(table, hashcode, tmp_chain);
    }
    SET_FRAME(rho, R_NilValue);
    return rho;
}



/*----------------------------------------------------------------------

  Environments

  The following code implements variable searching for environments.

*/


/*----------------------------------------------------------------------

  InitGlobalEnv

  Create the initial global environment.  The global environment is
  no longer a linked list of environment frames.  Instead it is a
  vector of environments which is searched from beginning to end.

  Note that only the first frame of each of these environments is
  searched.  This is intended to make it possible to implement
  namespaces at some (indeterminate) point in the future.

  We hash the initial environment.  100 is a magic number discovered
  by Ross.  Change it if you feel inclined.

*/

#define USE_GLOBAL_CACHE
#ifdef USE_GLOBAL_CACHE
/* Global variable caching.  A cache is maintained in a hash table,
   R_GlobalCache.  The entry values are either R_UnboundValue (a
   flushed cache entry), the binding LISTSXP cell from the environment
   containing the binding found in a search from R_GlobalEnv, or a
   symbol if the globally visible binding lives in the base package.
   The cache for a variable is flushed if a new binding for it is
   created in a global frame or if the variable is removed from any
   global frame.

   To make sure the cache is valid, all binding creations and removals
   from global frames must go through the interface functions in this
   file.

   Initially only the R_GlobalEnv frame is a global frame.  Additional
   global frames can only be created by attach.  All other frames are
   considered local.  Whether a frame is local or not is recorded in
   the highest order bit of the ENVFLAGS field (the gp field of
   sxpinfo).

   It is possible that the benefit of caching may be significantly
   reduced if we introduce name space management.  Since maintaining
   cache integrity is a bit tricky and since it might complicate
   threading a bit (I'm not sure it will but it needs to be thought
   through if nothing else) it might make sense to remove caching at
   that time.  To make that easier, the idfef's should probably be
   left in place.

   L. T. */

#define GLOBAL_FRAME_MASK (1<<15)
#define IS_GLOBAL_FRAME(e) (ENVFLAGS(e) & GLOBAL_FRAME_MASK)
#define MARK_AS_GLOBAL_FRAME(e) \
  SET_ENVFLAGS(e, ENVFLAGS(e) | GLOBAL_FRAME_MASK)
#define MARK_AS_LOCAL_FRAME(e) \
  SET_ENVFLAGS(e, ENVFLAGS(e) & (~ GLOBAL_FRAME_MASK))

#define INITIAL_CACHE_SIZE 1000

static SEXP R_GlobalCache, R_GlobalCachePreserve;
#endif

void InitGlobalEnv()
{
    R_GlobalEnv = NewEnvironment(R_NilValue, R_NilValue, R_NilValue);
#ifdef NEW_CODE
    HASHTAB(R_GlobalEnv) = R_NewHashTable(100, HASHTABLEGROWTHRATE);
#endif
#ifdef USE_GLOBAL_CACHE
    MARK_AS_GLOBAL_FRAME(R_GlobalEnv);
    R_GlobalCache = R_NewHashTable(INITIAL_CACHE_SIZE, HASHTABLEGROWTHRATE);
    R_GlobalCachePreserve = CONS(R_GlobalCache, R_NilValue);
    R_PreserveObject(R_GlobalCachePreserve);
#endif
}

#ifdef USE_GLOBAL_CACHE
static int hashIndex(SEXP symbol, SEXP table)
{
  SEXP c = PRINTNAME(symbol);
  if( !HASHASH(c) ) {
    SET_HASHVALUE(c, R_Newhashpjw(CHAR(c)));
    SET_HASHASH(c, 1);
  }
  return HASHVALUE(c) % HASHSIZE(table);
}

static void R_FlushGlobalCache(SEXP sym)
{
  SEXP entry = R_HashGetLoc(hashIndex(sym, R_GlobalCache), sym, R_GlobalCache);
  if (entry != R_NilValue)
    SETCAR(entry, R_UnboundValue);
}

static void R_FlushGlobalCacheFromTable(SEXP table)
{
  int i, size;
  SEXP chain;
  size = HASHSIZE(table);
  for (i = 0; i < size; i++) {
    for (chain = VECTOR_ELT(table, i); chain != R_NilValue; chain = CDR(chain))
      R_FlushGlobalCache(TAG(chain));
  }
}

static void R_AddGlobalCache(SEXP symbol, SEXP place)
{
  int oldpri = HASHPRI(R_GlobalCache);
  R_HashSet(hashIndex(symbol, R_GlobalCache), symbol, R_GlobalCache, place);
  if (oldpri != HASHPRI(R_GlobalCache) && 
      HASHPRI(R_GlobalCache) > 0.85 * HASHSIZE(R_GlobalCache)) {
    R_GlobalCache = R_HashResize(R_GlobalCache);
    SETCAR(R_GlobalCachePreserve, R_GlobalCache);
  }
}

static SEXP R_GetGlobalCache(SEXP symbol)
{
  SEXP vl = R_HashGet(hashIndex(symbol, R_GlobalCache), symbol, R_GlobalCache);
  switch(TYPEOF(vl)) {
  case SYMSXP:
    if (vl == R_UnboundValue) /* avoid test?? */
      return R_UnboundValue;
    else return SYMVALUE(vl);
  case LISTSXP:
    return CAR(vl);
  default:
    error("ilegal cached value");
    return R_NilValue;
  }
}
#endif /* USE_GLOBAL_CACHE */
  
/*----------------------------------------------------------------------

  unbindVar

  Remove a value from an environment. This happens only in the frame
  of the specified frame.

  FIXME ? should this also unbind the symbol value slot when rho is
  R_NilValue.

*/

static SEXP RemoveFromList(SEXP thing, SEXP list, int *found)
{
  if (list == R_NilValue) {
    *found = 0;
    return R_NilValue;
  }
  else if (TAG(list) == thing) {
    *found = 1;
    return CDR(list);
  }
  else {
    SEXP last = list;
    SEXP next = CDR(list);
    while (next != R_NilValue) {
      if (TAG(next) == thing) {
	*found = 1;
	SETCDR(last, CDR(next));
	return list;
      }
      else {
	last = next;
	next = CDR(next);
      }
    }
    *found = 0;
    return list;
  }
}
  
void unbindVar(SEXP symbol, SEXP rho)
{
    int hashcode;
    SEXP c;
#ifdef USE_GLOBAL_CACHE
    if (IS_GLOBAL_FRAME(rho))
	R_FlushGlobalCache(symbol);
#endif
    if (HASHTAB(rho) == R_NilValue) {
	int found;
	SEXP list;
	list = RemoveFromList(symbol, FRAME(rho), &found);
	if (found) {
	    R_DirtyImage = 1;
	    SET_FRAME(rho, list);
	}
    }
    else {
	c = PRINTNAME(symbol);
	if( !HASHASH(c) ) {
	    SET_HASHVALUE(c, R_Newhashpjw(CHAR(c)));
	    SET_HASHASH(c, 1);
	}
	hashcode = HASHVALUE(c) % HASHSIZE(HASHTAB(rho));
	R_HashDelete(hashcode, symbol, HASHTAB(rho));
    }
}



/*----------------------------------------------------------------------

  findVarLocInFrame

  Look up the location of the value of a symbol in a
  single environment frame.  Almost like findVarInFrame, but
  does not return the value. R_NilValue if not found.

*/

SEXP findVarLocInFrame(SEXP rho, SEXP symbol)
{
    int hashcode;
    SEXP frame, c;
    if (HASHTAB(rho) == R_NilValue) {
	frame = FRAME(rho);
	while (frame != R_NilValue && TAG(frame) != symbol)
	    frame = CDR(frame);
	return frame;
    }
    else {
	c = PRINTNAME(symbol);
	if( !HASHASH(c) ) {
	    SET_HASHVALUE(c, R_Newhashpjw(CHAR(c)));
	    SET_HASHASH(c,  1);
	}
	hashcode = HASHVALUE(c) % HASHSIZE(HASHTAB(rho));
	/* Will return 'R_NilValue' if not found */
	return(R_HashGetLoc(hashcode, symbol, HASHTAB(rho)));
    }
}




/*----------------------------------------------------------------------

  findVarInFrame

  Look up the value of a symbol in a single environment frame.	This
  is the basic building block of all variable lookups.

  It is important that this be as efficient as possible.

*/

SEXP findVarInFrame(SEXP rho, SEXP symbol)
{
    int hashcode;
    SEXP frame, c;
    if (HASHTAB(rho) == R_NilValue) {
	frame = FRAME(rho);
	while (frame != R_NilValue) {
	    if (TAG(frame) == symbol)
		return CAR(frame);
	    frame = CDR(frame);
	}
    }
    else {
	c = PRINTNAME(symbol);
	if( !HASHASH(c) ) {
	    SET_HASHVALUE(c, R_Newhashpjw(CHAR(c)));
	    SET_HASHASH(c, 1);
	}
	hashcode = HASHVALUE(c) % HASHSIZE(HASHTAB(rho));
	/* Will return 'R_UnboundValue' if not found */
	return(R_HashGet(hashcode, symbol, HASHTAB(rho)));
    }
    return R_UnboundValue;
}



/*----------------------------------------------------------------------

  findVar

  Look up a symbol in an environment.

  This needs to be changed so that the environment chain is searched
  and then the searchpath is traversed.

*/

#ifdef USE_GLOBAL_CACHE
/* findGlobalVar searches for a symbol value starting at R_GlobalEnv,
   so the cache can be used. */
static SEXP findGlobalVar(SEXP symbol)
{
    SEXP vl, rho;

    vl = R_GetGlobalCache(symbol);
    if (vl != R_UnboundValue)
	return vl;
    for (rho = R_GlobalEnv; rho != R_NilValue; rho = ENCLOS(rho)) {
	vl = findVarLocInFrame(rho, symbol);
	if (vl != R_NilValue) {
	    R_AddGlobalCache(symbol, vl);
	    return CAR(vl);
	}
    }
    vl = SYMVALUE(symbol);
    if (vl != R_UnboundValue)
	R_AddGlobalCache(symbol, symbol);
    return vl;
}
#endif

SEXP findVar(SEXP symbol, SEXP rho)
{
    SEXP vl;
#ifdef USE_GLOBAL_CACHE
    /* This first loop handles local frames, if there are any.  It
       will also handle all frames if rho is a global frame other than
       R_GlobalEnv */
    while (rho != R_GlobalEnv && rho != R_NilValue) {
	vl = findVarInFrame(rho, symbol);
	if (vl != R_UnboundValue)
	    return (vl);
	rho = ENCLOS(rho);
    }
    if (rho == R_GlobalEnv)
	return findGlobalVar(symbol);
    else
	return SYMVALUE(symbol);
#else
    while (rho != R_NilValue) {
	vl = findVarInFrame(rho, symbol);
	if (vl != R_UnboundValue)
	    return (vl);
	rho = ENCLOS(rho);
    }
    return (SYMVALUE(symbol));
#endif
}



/*----------------------------------------------------------------------

  findVar1

  Look up a symbol in an environment.  Ignore any values which are
  not of the specified type.

  This needs to be changed so that the environment chain is searched
  and then the searchpath is traversed.

*/

SEXP findVar1(SEXP symbol, SEXP rho, SEXPTYPE mode, int inherits)
{
    SEXP vl;
    while (rho != R_NilValue) {
	vl = findVarInFrame(rho, symbol);

	if (vl != R_UnboundValue) {
	    if (mode == ANYSXP) return vl;
	    if (TYPEOF(vl) == PROMSXP) {
		PROTECT(vl);
		vl = eval(vl, rho);
		UNPROTECT(1);
	    }
	    if (TYPEOF(vl) == mode) return vl;
	    if (mode == FUNSXP && (TYPEOF(vl) == CLOSXP ||
				   TYPEOF(vl) == BUILTINSXP ||
				   TYPEOF(vl) == SPECIALSXP))
		return (vl);
	}
	if (inherits)
	    rho = ENCLOS(rho);
	else
	    return (R_UnboundValue);
    }
    return (SYMVALUE(symbol));
}

/*
 *  ditto, but check *mode* not *type*
 */

SEXP findVar1mode(SEXP symbol, SEXP rho, SEXPTYPE mode, int inherits)
{
    SEXP vl;
    int tl;
    if (mode == INTSXP) mode = REALSXP;
    if (mode == FUNSXP || mode ==  BUILTINSXP || mode == SPECIALSXP) 
	mode = CLOSXP;
    while (rho != R_NilValue) {
	vl = findVarInFrame(rho, symbol);

	if (vl != R_UnboundValue) {
	    if (mode == ANYSXP) return vl;
	    if (TYPEOF(vl) == PROMSXP) {
		PROTECT(vl);
		vl = eval(vl, rho);
		UNPROTECT(1);
	    }
	    tl = TYPEOF(vl);
	    if (tl == INTSXP) tl = REALSXP;
	    if (tl == FUNSXP || tl ==  BUILTINSXP || tl == SPECIALSXP) 
		tl = CLOSXP;
	    if (tl == mode) return vl;
	}
	if (inherits)
	    rho = ENCLOS(rho);
	else
	    return (R_UnboundValue);
    }
    return (SYMVALUE(symbol));
}


/* 
   ddVal:
   a function to take a name and determine if it is of the form
   ..x where x is an integer; if so x is returned otherwise 0 is returned
*/
static int ddVal(SEXP symbol)
{
    char *buf, *endp;
    int rval;
        
    buf = CHAR(PRINTNAME(symbol));
    if( !strncmp(buf,"..",2) && strlen(buf) > 2 ) {
        buf += 2;
        rval = strtol(buf, &endp, 10);
        if( *endp != '\0')
                return 0;
        else
                return rval;
    }
    return 0;
}

/*----------------------------------------------------------------------
  ddfindVar

  This function fetches the variables ..1, ..2, etc from the first
  frame of the environment passed as the second argument to ddfindVar.
  These variables are implicitly defined whenever a ... object is
  created.

  To determine values for the variables we first search for an
  explicit definition of the symbol, them we look for a ... object in
  the frame and then walk through it to find the appropriate values.

  If no value is obtained we return R_UnboundValue.

  It is an error to specify a .. index longer than the length of the
  ... object the value is sought in.

*/

SEXP ddfindVar(SEXP symbol, SEXP rho)
{
    int i;
    SEXP vl;

    /* first look for the .. symbol itself */
    vl = findVarInFrame(rho, symbol);
    if (vl != R_UnboundValue)
	return(vl);

    i = ddVal(symbol);
    vl = findVarInFrame(rho, R_DotsSymbol);
    if (vl != R_UnboundValue) {
	if (length(vl) >= i) {
	    vl = nthcdr(vl, i - 1);
	    return(CAR(vl));
	}
	else
	    error("The ... list does not contain %d elements",i);
    }
    else {
	vl = findVar(symbol, rho);
	if( vl != R_UnboundValue )
	    return(vl);
	error("..%d used in an incorrect context, no ... to look in",i);
    }
    return R_NilValue;
}



/*----------------------------------------------------------------------

  dynamicFindVar

  This function does a variable lookup, but uses dynamic scoping rules
  rather than the lexical scoping rules used in findVar.

  Return R_UnboundValue if the symbol isn't located and the calling
  function needs to handle the errors.

*/

SEXP dynamicfindVar(SEXP symbol, RCNTXT *cptr)
{
    SEXP vl;
    while (cptr != R_ToplevelContext) {
	if (cptr->callflag & CTXT_FUNCTION) {
	    vl = findVarInFrame(cptr->cloenv, symbol);
	    if (vl != R_UnboundValue)
		return vl;
	}
	cptr = cptr->nextcontext;
    }
    return R_UnboundValue;
}



/*----------------------------------------------------------------------

  findFun

  Search for a function in an environment This is a specially modified
  version of findVar which ignores values its finds if they are not
  functions.

  NEEDED: This needs to be modified so that an object of arbitrary
  mode is searmodify this so that a search for an arbitrary mode can
  be made.  Then findVar and findFun could become same function

*/

SEXP findFun(SEXP symbol, SEXP rho)
{
    SEXP vl;
    while (rho != R_NilValue) {
#ifdef USE_GLOBAL_CACHE
	if (rho == R_GlobalEnv)
	    vl = findGlobalVar(symbol);
	else
	    vl = findVarInFrame(rho, symbol);
#else
	vl = findVarInFrame(rho, symbol);
#endif
	if (vl != R_UnboundValue) {
	    if (TYPEOF(vl) == PROMSXP) {
		PROTECT(vl);
		vl = eval(vl, rho);
		UNPROTECT(1);
	    }
	    if (TYPEOF(vl) == CLOSXP || TYPEOF(vl) == BUILTINSXP ||
		TYPEOF(vl) == SPECIALSXP)
		return (vl);
	    if (vl == R_MissingArg)
		error("Argument \"%s\" is missing, with no default",
		      CHAR(PRINTNAME(symbol)));
#ifdef Warn_on_non_function
	    warning("ignored non function \"%s\"",
		    CHAR(PRINTNAME(symbol)));
#endif
	}
	rho = ENCLOS(rho);
    }
    if (SYMVALUE(symbol) == R_UnboundValue)
	error("couldn't find function \"%s\"", CHAR(PRINTNAME(symbol)));
    return SYMVALUE(symbol);
}


/*----------------------------------------------------------------------

  defineVar

  Assign a value in a specific environment frame.  This needs to be
  rethought when it comes time to add a search path.

*/

void defineVar(SEXP symbol, SEXP value, SEXP rho)
{
    int hashcode;
    SEXP frame, c;
    R_DirtyImage = 1;
    if (rho != R_NilValue) {
#ifdef USE_GLOBAL_CACHE
	if (IS_GLOBAL_FRAME(rho))
	    R_FlushGlobalCache(symbol);
#endif
	if (HASHTAB(rho) == R_NilValue) {
	    frame = FRAME(rho);
	    while (frame != R_NilValue) {
		if (TAG(frame) == symbol) {
		    SETCAR(frame, value);
		    SET_MISSING(frame, 0);	/* Over-ride */
		    return;
		}
		frame = CDR(frame);
	    }
	    SET_FRAME(rho, CONS(value, FRAME(rho)));
	    SET_TAG(FRAME(rho), symbol);
	}
	else {
	    c = PRINTNAME(symbol);
	    if( !HASHASH(c) ) {
		SET_HASHVALUE(c, R_Newhashpjw(CHAR(c)));
		SET_HASHASH(c, 1);
	    }
	    hashcode = HASHVALUE(c) % HASHSIZE(HASHTAB(rho));
	    R_HashSet(hashcode, symbol, HASHTAB(rho), value);
	    if (R_HashSizeCheck(HASHTAB(rho)))
		SET_HASHTAB(rho, R_HashResize(HASHTAB(rho)));
	}
    }
    else {
#ifdef USE_GLOBAL_CACHE
	R_FlushGlobalCache(symbol);
#endif
	SET_SYMVALUE(symbol, value);
    }
}


/*----------------------------------------------------------------------

  setVarInFrame

  Assign a new value to a symbol in a frame.  Return the symbol if
  successful and R_NilValue if not.

*/

SEXP setVarInFrame(SEXP rho, SEXP symbol, SEXP value)
{
    int hashcode;
    SEXP frame, c;
    if (HASHTAB(rho) == R_NilValue) {
	frame = FRAME(rho);
	while (frame != R_NilValue) {
	    if (TAG(frame) == symbol) {
		SETCAR(frame, value);
		return symbol;
	    }
	    frame = CDR(frame);
	}
    }
    else {
	/* Do the hash table thing */
	c = PRINTNAME(symbol);
	if( !HASHASH(c) ) {
	    SET_HASHVALUE(c, R_Newhashpjw(CHAR(c)));
	    SET_HASHASH(c, 1);
	}
	hashcode = HASHVALUE(c) % HASHSIZE(HASHTAB(rho));
	frame = R_HashGetLoc(hashcode, symbol, HASHTAB(rho));
	if (frame != R_NilValue) {
	  SETCAR(frame, value);
	  return symbol;
	}
	else return R_NilValue;
    }
    return R_NilValue;
}



/*----------------------------------------------------------------------

    setVar

    Assign a new value to bound symbol.	 Note this does the "inherits"
    case.  I.e. it searches frame-by-frame for an symbol and binds the
    given value to the first symbol encountered.  If no symbol is
    found then a binding is created in the global environment.

*/

void setVar(SEXP symbol, SEXP value, SEXP rho)
{
    SEXP vl;
    while (rho != R_NilValue) {
	R_DirtyImage = 1;
	vl = setVarInFrame(rho, symbol, value);
	if (vl != R_NilValue) {
	    return;
	}
	rho = ENCLOS(rho);
    }
    defineVar(symbol, value, R_GlobalEnv);
}



/*----------------------------------------------------------------------

  gsetVar

  Assignment in the system environment.	 Here we assign directly into
  the system environment.

*/

void gsetVar(SEXP symbol, SEXP value, SEXP rho)
{
    R_DirtyImage = 1;
#ifdef USE_GLOBAL_CACHE
    R_FlushGlobalCache(symbol);
#endif
    SET_SYMVALUE(symbol, value);
}



/*----------------------------------------------------------------------

  mfindVarInFrame

  Look up a symbol in a single environment frame.  This differs from
  findVarInFrame in that it returns the list whose CAR is the value of
  the symbol, rather than the value of the symbol.

*/

static SEXP mfindVarInFrame(SEXP rho, SEXP symbol)
{
    int hashcode;
    SEXP frame, c;
    if (HASHTAB(rho) == R_NilValue) {
	frame = FRAME(rho);
    }
    else {
	c = PRINTNAME(symbol);
	if( !HASHASH(c) ) {
	    SET_HASHVALUE(c, R_Newhashpjw(CHAR(c)));
	    SET_HASHASH(c, 1);
	}
	hashcode = HASHVALUE(c) % HASHSIZE(HASHTAB(rho));
	frame = VECTOR_ELT(HASHTAB(rho), hashcode);
    }
    while (frame != R_NilValue) {
	if (TAG(frame) == symbol)
	    return frame;
	frame = CDR(frame);
    }
    return R_NilValue;
}


/*----------------------------------------------------------------------

  do_assign : .Internal(assign(x, value, envir, inherits))

*/
SEXP do_assign(SEXP call, SEXP op, SEXP args, SEXP rho)
{
    SEXP name, val, aenv;
    int ginherits = 0;
    checkArity(op, args);
    name = findVar(CAR(args), rho);
    PROTECT(args = evalList(args, rho));
    if (!isString(CAR(args)) || length(CAR(args)) == 0)
	error("invalid first argument");
    else
	name = install(CHAR(STRING_ELT(CAR(args), 0)));
    PROTECT(val = CADR(args));
    R_Visible = 0;
    aenv = CAR(CDDR(args));
    if (TYPEOF(aenv) != ENVSXP && aenv != R_NilValue)
	errorcall(call, "invalid `envir' argument");
    if (isLogical(CAR(nthcdr(args, 3))))
	ginherits = LOGICAL(CAR(nthcdr(args, 3)))[0];
    else
	errorcall(call, "invalid `inherits' argument");
    if (ginherits)
	setVar(name, val, aenv);
    else
	defineVar(name, val, aenv);
    UNPROTECT(2);
    return val;
}


/*----------------------------------------------------------------------

  do_remove

  There are three arguments to do_remove; a list of names to remove,
  an optional environment (if missing set it to R_GlobalEnv) and
  inherits, a logical indicating whether to look in the parent env if
  a symbol is not found in the supplied env.  This is ignored if
  environment is not specified.

*/

static int RemoveVariable(SEXP name, int hashcode, SEXP env)
{
    int found;
    SEXP list;
    if (IS_HASHED(env)) {
	SEXP hashtab = HASHTAB(env);
	int idx = hashcode % HASHSIZE(hashtab);
	list = RemoveFromList(name, VECTOR_ELT(hashtab, idx), &found);
	if (found) {
	    R_DirtyImage = 1;
	    SET_VECTOR_ELT(hashtab, idx, list);
#ifdef USE_GLOBAL_CACHE
	    if (IS_GLOBAL_FRAME(env))
		R_FlushGlobalCache(name);
#endif
	}
    }
    else {
	list = RemoveFromList(name, FRAME(env), &found);
	if (found) {
	    R_DirtyImage = 1;
	    SET_FRAME(env, list);
#ifdef USE_GLOBAL_CACHE
	    if (IS_GLOBAL_FRAME(env))
		R_FlushGlobalCache(name);
#endif
	}
    }
    return found;
}

SEXP do_remove(SEXP call, SEXP op, SEXP args, SEXP rho)
{
    /* .Internal(remove(list, envir, inherits)) */

    SEXP name, envarg, tsym, tenv;
    int ginherits = 0;
    int done, i, hashcode;
    checkArity(op, args);

    name = CAR(args);
    if (!isString(name))
	errorcall(call, "invalid first argument to remove.");
    args = CDR(args);

    envarg = CAR(args);
    if (envarg != R_NilValue) {
	if (TYPEOF(envarg) != ENVSXP)
	    errorcall(call, "invalid `envir' argument");
    }
    else envarg = R_GlobalContext->sysparent;
    args = CDR(args);

    if (isLogical(CAR(args)))
	ginherits = asLogical(CAR(args));
    else
	errorcall(call, "invalid `inherits' argument");

    for (i = 0; i < LENGTH(name); i++) {
	done = 0;
	tsym = install(CHAR(STRING_ELT(name, i)));
	if( !HASHASH(PRINTNAME(tsym)) )
	    hashcode = R_Newhashpjw(CHAR(PRINTNAME(tsym)));
	else
	    hashcode = HASHVALUE(PRINTNAME(tsym));
	tenv = envarg;
	while (tenv != R_NilValue) {
	    done = RemoveVariable(tsym, hashcode, tenv);
	    if (done || !ginherits)
		break;
	    tenv = CDR(tenv);
	}
	if (!done)
	    warning("remove: variable \"%s\" was not found",
		    CHAR(PRINTNAME(tsym)));
    }
    return R_NilValue;
}


/*----------------------------------------------------------------------

  do_get

  This function returns the SEXP associated with the character
  argument.  It needs the environment of the calling function as a
  default.

      get(x, envir, mode, inherits)
      exists(x, envir, mode, inherits)

*/


SEXP do_get(SEXP call, SEXP op, SEXP args, SEXP rho)
{
    SEXP rval, genv, t1;
    SEXPTYPE gmode;
    int ginherits = 0, where;
    checkArity(op, args);

    /* Grab the environment off the first arg */
    /* for use as the default environment. */

    /* TODO: Don't we have a better way */
    /* of doing this using sys.xxx now? */

    rval = findVar(CAR(args), rho);
    if (TYPEOF(rval) == PROMSXP)
	genv = PRENV(rval);

    /* Now we can evaluate the arguments */

    PROTECT(args = evalList(args, rho));

    /* The first arg is the object name */
    /* It must be present and a string */

    if (!isValidStringF(CAR(args))) {
	errorcall(call, "invalid first argument");
	t1 = R_NilValue;
    }
    else
	t1 = install(CHAR(STRING_ELT(CAR(args), 0)));

    /* envir :	originally, the "where=" argument */

    if (TYPEOF(CADR(args)) == REALSXP || TYPEOF(CADR(args)) == INTSXP) {
	where = asInteger(CADR(args));
	genv = R_sysframe(where,R_GlobalContext);
    }
    else if (TYPEOF(CADR(args)) == ENVSXP || CADR(args) == R_NilValue)
	genv = CADR(args);
    else {
	errorcall(call,"invalid envir argument");
	genv = R_NilValue;  /* -Wall */
    }

    /* mode :  The mode of the object being sought */

    /* as from R 1.2.0, this is the *mode*, not the *typeof* aka
       storage.mode.
    */

    if (isString(CAR(CDDR(args)))) {
	if (!strcmp(CHAR(STRING_ELT(CAR(CDDR(args)), 0)),"function"))
	    gmode = FUNSXP;
	else
	    gmode = str2type(CHAR(STRING_ELT(CAR(CDDR(args)), 0)));
    } else {
	errorcall(call,"invalid mode argument");
	gmode = FUNSXP;/* -Wall */
    }

    if (isLogical(CAR(nthcdr(args, 3))))
	ginherits = LOGICAL(CAR(nthcdr(args, 3)))[0];
    else
	errorcall(call,"invalid inherits argument");

    /* Search for the object */
    rval = findVar1mode(t1, genv, gmode, ginherits);

    UNPROTECT(1);

    if (PRIMVAL(op)) { /* have get(.) */
	if (rval == R_UnboundValue)
	    errorcall(call,"variable \"%s\" was not found",
		      CHAR(PRINTNAME(t1)));
	/* We need to evaluate if it is a promise */
	if (TYPEOF(rval) == PROMSXP)
	    rval = eval(rval, genv);
	SET_NAMED(rval, 1);
	return rval;
    }
    else { /* exists(.) */
	if (rval == R_UnboundValue)
	    ginherits = 0;
	else
	    ginherits = 1;
	rval = allocVector(LGLSXP, 1);
	LOGICAL(rval)[0] = ginherits;
	return rval;
    }
}


/*----------------------------------------------------------------------

  do_missing

  This function tests whether the symbol passed as its first argument
  is ia "missing argument to the current closure.  rho is the
  environment that missing was called from.

*/

static int isMissing(SEXP symbol, SEXP rho)
{
    int ddv=0;
    SEXP vl, s;

    if (symbol == R_MissingArg) /* Yes, this can happen */
        return 1;

    if (DDVAL(symbol)) {
	s = R_DotsSymbol;
	ddv = ddVal(symbol);
    }
    else
	s = symbol;

    vl = mfindVarInFrame(rho, s);
    if (vl != R_NilValue) {
	if (DDVAL(symbol)) {
	    if (length(CAR(vl)) < ddv || CAR(vl) == R_MissingArg)
		return 1;
	    /* defineVar(symbol, value, R_GlobalEnv); */
	    else
		vl = nthcdr(CAR(vl), ddv-1);
	}
	if (MISSING(vl) == 1 || CAR(vl) == R_MissingArg)
	    return 1;
	if (TYPEOF(CAR(vl)) == PROMSXP &&
	    TYPEOF(PREXPR(CAR(vl))) == SYMSXP)
	    return isMissing(PREXPR(CAR(vl)), PRENV(CAR(vl)));
	else
	    return 0;
    }
    return 0;
}

SEXP do_missing(SEXP call, SEXP op, SEXP args, SEXP rho)
{
    int ddv=0;
    SEXP rval, t, sym, s;

    checkArity(op, args);
    s = sym = CAR(args);
    if( isString(sym) && length(sym)==1 )
        s = sym = install(CHAR(STRING_ELT(CAR(args), 0)));
    if (!isSymbol(sym))
	error("\"missing\" illegal use of missing");

    if (DDVAL(sym)) {
        ddv = ddVal(sym);
	sym = R_DotsSymbol;
    }
    rval=allocVector(LGLSXP,1);

    t = mfindVarInFrame(rho, sym);
    if (t != R_NilValue) {
	if (DDVAL(s)) {
	    if (length(CAR(t)) < ddv  || CAR(t) == R_MissingArg) {
		LOGICAL(rval)[0] = 1;
		return rval;
	    }
	    else
		t = nthcdr(CAR(t), ddv-1);
	}
	if (MISSING(t) || CAR(t) == R_MissingArg) {
	    LOGICAL(rval)[0] = 1;
	    return rval;
	}
	else goto havebinding;
    }
    else  /* it wasn't an argument to the function */
	error("\"missing\" illegal use of missing");

 havebinding:

    t = CAR(t);
    if (TYPEOF(t) != PROMSXP) {
	LOGICAL(rval)[0] = 0;
	return rval;
    }

    if (!isSymbol(PREXPR(t))) LOGICAL(rval)[0] = 0;
    else LOGICAL(rval)[0] = isMissing(PREXPR(t), PRENV(t));
    return rval;
}

/*----------------------------------------------------------------------

  do_globalenv

  Returns the current global environment.

*/


SEXP do_globalenv(SEXP call, SEXP op, SEXP args, SEXP rho)
{
    checkArity(op, args);
    return R_GlobalEnv;
}


/*----------------------------------------------------------------------

  do_attach

  To attach a list we make up an environment and insert components
  of the list in as the values of this env and install the tags from
  the list as the names.

*/

SEXP do_attach(SEXP call, SEXP op, SEXP args, SEXP env)
{
    SEXP name, s, t, x;
    int pos, hsize;
    checkArity(op, args);

    if (!isNewList(CAR(args)))
	error("attach only works for lists and data frames");
    SETCAR(args, VectorToPairList(CAR(args)));

    pos = asInteger(CADR(args));
    if (pos == NA_INTEGER)
	error("attach: pos must be an integer");

    name = CADDR(args);
    if (!isValidStringF(name))
	error("attach: invalid object name");

    for (x = CAR(args); x != R_NilValue; x = CDR(x))
	if (TAG(x) == R_NilValue)
	    error("attach: all elements must be named");
    PROTECT(s = allocSExp(ENVSXP));
    setAttrib(s, install("name"), name);

    SET_FRAME(s, duplicate(CAR(args)));

    /* Connect FRAME(s) into HASHTAB(s) */
    if (length(s) < HASHMINSIZE)
	hsize = HASHMINSIZE;
    else
	hsize = length(s);

    SET_HASHTAB(s, R_NewHashTable(hsize, HASHTABLEGROWTHRATE));
    s = R_HashFrame(s);

    /* FIXME: A little inefficient */
    while (R_HashSizeCheck(HASHTAB(s))) {
	SET_HASHTAB(s, R_HashResize(HASHTAB(s)));
    }

    for (t = R_GlobalEnv; ENCLOS(t) != R_NilValue && pos > 2; t = ENCLOS(t))
	pos--;
    if (ENCLOS(t) == R_NilValue) {
	SET_ENCLOS(t, s);
	SET_ENCLOS(s, R_NilValue);
    }
    else {
	x = ENCLOS(t);
	SET_ENCLOS(t, s);
	SET_ENCLOS(s, x);
    }
#ifdef USE_GLOBAL_CACHE
    R_FlushGlobalCacheFromTable(HASHTAB(s));
    MARK_AS_GLOBAL_FRAME(s);
#endif
    UNPROTECT(1);
    return s;
}



/*----------------------------------------------------------------------

  do_detach

  detach the specified environment.  Detachment only takes place by
  position.

*/

SEXP do_detach(SEXP call, SEXP op, SEXP args, SEXP env)
{
    SEXP s, t, x;
    int pos;

    checkArity(op, args);
    pos = asInteger(CAR(args));

    for (t = R_GlobalEnv ; ENCLOS(t) != R_NilValue && pos > 2 ; t = ENCLOS(t))
	pos--;
    if (pos != 2) {
	error("detach: invalid pos= given");
	s = t;	/* for -Wall */
    }
    else {
	PROTECT(s = ENCLOS(t));
	x = ENCLOS(s);
	SET_ENCLOS(t, x);
    }
#ifdef USE_GLOBAL_CACHE
    R_FlushGlobalCacheFromTable(HASHTAB(s));
    MARK_AS_LOCAL_FRAME(s);
#endif
    R_Visible = 0;
    UNPROTECT(1);
    return FRAME(s);
}



/*----------------------------------------------------------------------

  do_search

  Print out the current search path.

*/

SEXP do_search(SEXP call, SEXP op, SEXP args, SEXP env)
{
    SEXP ans, name, t;
    int i, n;

    checkArity(op, args);
    n = 2;
    for (t = ENCLOS(R_GlobalEnv); t != R_NilValue ; t = ENCLOS(t))
	n++;
    PROTECT(ans = allocVector(STRSXP, n));
    /* TODO - what should the name of this be? */
    SET_STRING_ELT(ans, 0, mkChar(".GlobalEnv"));
    SET_STRING_ELT(ans, n-1, mkChar("package:base"));
    i = 1;
    for (t = ENCLOS(R_GlobalEnv); t != R_NilValue ; t = ENCLOS(t)) {
	name = getAttrib(t, install("name"));
	if (!isString(name) || length(name) < 1)
	    SET_STRING_ELT(ans, i, mkChar("(unknown)"));
	else
	    SET_STRING_ELT(ans, i, STRING_ELT(name, 0));
	i++;
    }
    UNPROTECT(1);
    return ans;
}




/*----------------------------------------------------------------------

  do_ls

  This code implements the functionality of the "ls" and "objects"
  functions.  [ ls(envir, all.names) ]

*/

static int FrameSize(SEXP frame, int all)
{
    int count = 0;
    while (frame != R_NilValue) {
	if ((all || CHAR(PRINTNAME(TAG(frame)))[0] != '.') &&
				      CAR(frame) != R_UnboundValue)
	    count += 1;
	frame = CDR(frame);
    }
    return count;
}

static void FrameNames(SEXP frame, int all, SEXP names, int *indx)
{
    while (frame != R_NilValue) {
	if ((all || CHAR(PRINTNAME(TAG(frame)))[0] != '.') &&
				      CAR(frame) != R_UnboundValue) {
	    SET_STRING_ELT(names, *indx, PRINTNAME(TAG(frame)));
	    (*indx)++;
	}
	frame = CDR(frame);
    }
}

static int HashTableSize(SEXP table, int all)
{
    int count = 0;
    int n = length(table);
    int i;
    for (i = 0; i < n; i++)
	count += FrameSize(VECTOR_ELT(table, i), all);
    return count;
}

static void HashTableNames(SEXP table, int all, SEXP names, int *indx)
{
    int n = length(table);
    int i;
    for (i = 0; i < n; i++)
	FrameNames(VECTOR_ELT(table, i), all, names, indx);
}

static int BuiltinSize(int all, int intern)
{
    int count = 0;
    SEXP s;
    int j;
    for (j = 0; j < HSIZE; j++) {
	for (s = R_SymbolTable[j]; s != R_NilValue; s = CDR(s)) {
	    if (intern) {
		if (INTERNAL(CAR(s)) != R_NilValue)
		    count++;
	    }
	    else {
		if (SYMVALUE(CAR(s)) != R_UnboundValue)
		    count++;
	    }
	}
    }
    return count;
}

static void
BuiltinNames(int all, int intern, SEXP names, int *indx)
{
    SEXP s;
    int j;
    for (j = 0; j < HSIZE; j++) {
	for (s = R_SymbolTable[j]; s != R_NilValue; s = CDR(s)) {
	    if (intern) {
		if (INTERNAL(CAR(s)) != R_NilValue)
		    SET_STRING_ELT(names, (*indx)++, PRINTNAME(CAR(s)));
	    }
	    else {
		if (SYMVALUE(CAR(s)) != R_UnboundValue)
		    SET_STRING_ELT(names, (*indx)++, PRINTNAME(CAR(s)));
	    }
	}
    }
}

SEXP do_ls(SEXP call, SEXP op, SEXP args, SEXP rho)
{
    SEXP ans, env, envp;
    int all, i, k, n;
    checkArity(op, args);
    envp = CAR(args);
    if (isNull(envp) || !isNewList(envp)) {
	PROTECT(env = allocVector(VECSXP, 1));
	SET_VECTOR_ELT(env, 0, envp);
    }
    else
	PROTECT(env = envp);

    all = asLogical(CADR(args));
    if (all == NA_LOGICAL)
	all = 0;
    /* Step 1 : Compute the Vector Size */
    k = 0;
    n = length(env);
    for (i = 0; i < n; i++) {
	if (VECTOR_ELT(env, i) == R_NilValue)
	    k += BuiltinSize(all, 0);
	else if (isEnvironment(VECTOR_ELT(env, i))) {
	    if (HASHTAB(VECTOR_ELT(env, i)) != R_NilValue)
		k += HashTableSize(HASHTAB(VECTOR_ELT(env, i)), all);
	    else
		k += FrameSize(FRAME(VECTOR_ELT(env, i)), all);
	}
	else error("invalid envir= argument");
    }
    /* Step 2 : Allocate and Fill the Result */
    ans = allocVector(STRSXP, k);
    k = 0;
    for (i = 0; i < n; i++) {
	if (VECTOR_ELT(env, i) == R_NilValue)
	    BuiltinNames(all, 0, ans, &k);
	else if (isEnvironment(VECTOR_ELT(env, i))) {
	    if (HASHTAB(VECTOR_ELT(env, i)) != R_NilValue)
		HashTableNames(HASHTAB(VECTOR_ELT(env, i)), all, ans, &k);
	    else
		FrameNames(FRAME(VECTOR_ELT(env, i)), all, ans, &k);
	}
    }
    UNPROTECT(1);
    sortVector(ans);
    return ans;
}

/*----------------------------------------------------------------------

  do_builtins

  Return the names of all the built in functions.  These are fetched
  directly from the symbol table.

*/

SEXP do_builtins(SEXP call, SEXP op, SEXP args, SEXP rho)
{
    SEXP ans;
    int intern, nelts;
    checkArity(op, args);
    intern = asLogical(CAR(args));
    if (intern == NA_INTEGER) intern = 0;
    nelts = BuiltinSize(1, intern);
    ans = allocVector(STRSXP, nelts);
    nelts = 0;
    BuiltinNames(1, intern, ans, &nelts);
    sortVector(ans);
    return ans;
}



/*----------------------------------------------------------------------

  do_libfixup

  This function performs environment reparaenting for libraries to make
  sure that elements are parented by the global environment.

  This routine will hopefull die at some point.

*/

SEXP do_libfixup(SEXP call, SEXP op, SEXP args, SEXP rho)
{
    SEXP lib, env, p;
    checkArity(op, args);
    lib = CAR(args);
    env = CADR(args);
    if (TYPEOF(lib) != ENVSXP || !isEnvironment(env))
	errorcall(call, "invalid arguments");
    if (HASHTAB(lib) != R_NilValue) {
	int i, n;
	n = length(HASHTAB(lib));
	for (i = 0; i < n; i++) {
	    p = VECTOR_ELT(HASHTAB(lib), i);
	    while (p != R_NilValue) {
		if (TYPEOF(CAR(p)) == CLOSXP)
		    SET_CLOENV(CAR(p), env);
		p = CDR(p);
	    }
	}
    }
    else {
	p = FRAME(lib);
	while (p != R_NilValue) {
	    if (TYPEOF(CAR(p)) == CLOSXP)
		SET_CLOENV(CAR(p), env);
	    p = CDR(p);
	}
    }
    return lib;
}

/*----------------------------------------------------------------------

  do_pos2env

  This function returns the environment at a specified position in the
  search path or the environment of the caller of
  pos.to.env (? but pos.to.env is usually used in arg lists and hence
  is evaluated in the calling environment so this is one higher).
	
  When pos = -1 the environment of the closure that pos2env is
  evaluated in is obtained. Note: this relies on pos.to.env being
  a primitive.

 */
static SEXP pos2env(int pos, SEXP call)
{
    SEXP env;
    RCNTXT *cptr;

    if (pos == NA_INTEGER || pos < -1 || pos == 0) {
	errorcall(call, R_MSG_IA);
	env = call;/* just for -Wall */
    }
    else if (pos == -1) {
	/* make sure the context is a funcall */
	cptr = R_GlobalContext;
	while( !(cptr->callflag & CTXT_FUNCTION) && cptr->nextcontext
	       != NULL )
	    cptr = cptr->nextcontext;
	if( !(cptr->callflag & CTXT_FUNCTION) )
	    errorcall(call, "no enclosing environment");

	env = cptr->sysparent;
	if (R_GlobalEnv != R_NilValue && env == R_NilValue)
	    errorcall(call, R_MSG_IA);
    }
    else {
	for (env = R_GlobalEnv; env != R_NilValue && pos > 1;
	     env = ENCLOS(env))
	    pos--;
	if (pos != 1)
	    error(R_MSG_IA);
    }
    return env;
}

SEXP do_pos2env(SEXP call, SEXP op, SEXP args, SEXP rho)
{
    SEXP env, pos;
    int i, npos;
    PROTECT(pos = coerceVector(CAR(args), INTSXP));
    npos = length(pos);
    if (npos <= 0)
	errorcall(call, "invalid \"pos\" argument");
    PROTECT(env = allocVector(VECSXP, npos));
    for (i = 0; i < npos; i++) {
	SET_VECTOR_ELT(env, i, pos2env(INTEGER(pos)[i], call));
    }
    if (npos == 1) env = VECTOR_ELT(env, 0);
    UNPROTECT(2);
    return env;
}
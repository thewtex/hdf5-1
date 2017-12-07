/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Copyright by The HDF Group.                                               *
 * Copyright by the Board of Trustees of the University of Illinois.         *
 * All rights reserved.                                                      *
 *                                                                           *
 * This file is part of HDF5.  The full HDF5 copyright notice, including     *
 * terms governing use, modification, and redistribution, is contained in    *
 * the COPYING file, which can be found at the root of the source code       *
 * distribution tree, or in https://support.hdfgroup.org/ftp/HDF5/releases.  *
 * If you do not have access to either file, you may request a copy from     *
 * help@hdfgroup.org.                                                        *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/*-------------------------------------------------------------------------
 *
 * Created:	H5G.c
 *
 * Purpose:	Symbol table functions.	 The functions that begin with
 *		'H5G_stab_' don't understand the naming system; they operate
 * 		on a single symbol table at a time.
 *
 *		The functions that begin with 'H5G_node_' operate on the leaf
 *		nodes of a symbol table B-tree.  They should be defined in
 *		the H5Gnode.c file.
 *
 *		The remaining functions know how to traverse the group
 *		directed graph.
 *
 * Names:	Object names are a slash-separated list of components.  If
 *		the name begins with a slash then it's absolute, otherwise
 *		it's relative ("/foo/bar" is absolute while "foo/bar" is
 *		relative).  Multiple consecutive slashes are treated as
 *		single slashes and trailing slashes are ignored.  The special
 *		case `/' is the root group.  Every file has a root group.
 *
 *		API functions that look up names take a location ID and a
 *		name.  The location ID can be a file ID or a group ID and the
 *		name can be relative or absolute.
 *
 *              +--------------+----------- +--------------------------------+
 * 		| Location ID  | Name       | Meaning                        |
 *              +--------------+------------+--------------------------------+
 * 		| File ID      | "/foo/bar" | Find 'foo' within 'bar' within |
 *		|              |            | the root group of the specified|
 *		|              |            | file.                          |
 *              +--------------+------------+--------------------------------+
 * 		| File ID      | "foo/bar"  | Find 'foo' within 'bar' within |
 *		|              |            | the root group of the specified|
 *		|              |            | file.                          |
 *              +--------------+------------+--------------------------------+
 * 		| File ID      | "/"        | The root group of the specified|
 *		|              |            | file.                          |
 *              +--------------+------------+--------------------------------+
 * 		| File ID      | "."        | The root group of the specified|
 *		|              |            | the specified file.            |
 *              +--------------+------------+--------------------------------+
 * 		| Group ID     | "/foo/bar" | Find 'foo' within 'bar' within |
 *		|              |            | the root group of the file     |
 *		|              |            | containing the specified group.|
 *              +--------------+------------+--------------------------------+
 * 		| Group ID     | "foo/bar"  | File 'foo' within 'bar' within |
 *		|              |            | the specified group.           |
 *              +--------------+------------+--------------------------------+
 * 		| Group ID     | "/"        | The root group of the file     |
 *		|              |            | containing the specified group.|
 *              +--------------+------------+--------------------------------+
 * 		| Group ID     | "."        | The specified group.           |
 *              +--------------+------------+--------------------------------+
 *
 *
 *-------------------------------------------------------------------------
 */

/****************/
/* Module Setup */
/****************/

#include "H5Gmodule.h"          /* This source code file is part of the H5G module */


/***********/
/* Headers */
/***********/
#include "H5private.h"          /* Generic Functions                        */
#include "H5ACprivate.h"        /* Metadata cache                           */
#include "H5Eprivate.h"         /* Error handling                           */
#include "H5Gpkg.h"             /* Groups                                   */
#include "H5Iprivate.h"         /* IDs                                      */
#include "H5Pprivate.h"         /* Property lists                           */


/****************/
/* Local Macros */
/****************/


/******************/
/* Local Typedefs */
/******************/


/********************/
/* Package Typedefs */
/********************/


/********************/
/* Local Prototypes */
/********************/

/* VOL close */
static herr_t H5G__close_group(void *grp);


/*********************/
/* Package Variables */
/*********************/

/* Package initialization variable */
hbool_t H5_PKG_INIT_VAR = FALSE;


/*****************************/
/* Library Private Variables */
/*****************************/


/*******************/
/* Local Variables */
/*******************/

/* Group ID class */
static const H5I_class_t H5I_GROUP_CLS[1] = {{
    H5I_GROUP,			/* ID class value */
    0,				/* Class flags */
    0,				/* # of reserved IDs for class */
    (H5I_free_t)H5G_close	/* Callback routine for closing objects of this class */
}};

/* Flag indicating "top" of interface has been initialized */
static hbool_t H5G_top_package_initialize_s = FALSE;



/*-------------------------------------------------------------------------
 * Function:	H5G__init_package
 *
 * Purpose:	Initializes the H5G interface.
 *
 * Return:	Non-negative on success/Negative on failure
 *
 * Programmer:	Robb Matzke
 *		Monday, January	 5, 1998
 *
 * Notes:       The group creation properties are registered in the property
 *              list interface initialization routine (H5P_init_package)
 *              so that the file creation property class can inherit from it
 *              correctly. (Which allows the file creation property list to
 *              control the group creation properties of the root group of
 *              a file) QAK - 24/10/2005
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5G__init_package(void)
{
    herr_t          ret_value = SUCCEED;  /* Return value */

    FUNC_ENTER_PACKAGE

    /* Initialize the atom group for the group IDs */
    if(H5I_register_type(H5I_GROUP_CLS) < 0)
	HGOTO_ERROR(H5E_SYM, H5E_CANTINIT, FAIL, "unable to initialize interface")

    /* Mark "top" of interface as initialized, too */
    H5G_top_package_initialize_s = TRUE;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5G__init_package() */


/*-------------------------------------------------------------------------
 * Function:	H5G_top_term_package
 *
 * Purpose:	Close the "top" of the interface, releasing IDs, etc.
 *
 * Return:	Success:	Positive if anything is done that might
 *				affect other interfaces; zero otherwise.
 * 		Failure:	Negative.
 *
 * Programmer:	Quincey Koziol
 *		Sunday, September	13, 2015
 *
 *-------------------------------------------------------------------------
 */
int
H5G_top_term_package(void)
{
    int	n = 0;

    FUNC_ENTER_NOAPI_NOINIT_NOERR

    if(H5G_top_package_initialize_s) {
        if(H5I_nmembers(H5I_GROUP) > 0) {
            (void)H5I_clear_type(H5I_GROUP, FALSE, FALSE);
            n++; /*H5I*/
        } /* end if */

        /* Mark closed */
        if(0 == n)
            H5G_top_package_initialize_s = FALSE;
    } /* end if */

    FUNC_LEAVE_NOAPI(n)
} /* end H5G_top_term_package() */


/*-------------------------------------------------------------------------
 * Function:	H5G_term_package
 *
 * Purpose:	Terminates the H5G interface
 *
 * Note:	Finishes shutting down the interface, after
 *		H5G_top_term_package() is called
 *
 * Return:	Success:	Positive if anything is done that might
 *				affect other interfaces; zero otherwise.
 * 		Failure:	Negative.
 *
 * Programmer:	Robb Matzke
 *		Monday, January	 5, 1998
 *
 *-------------------------------------------------------------------------
 */
int
H5G_term_package(void)
{
    int	n = 0;

    FUNC_ENTER_NOAPI_NOINIT_NOERR

    if(H5_PKG_INIT_VAR) {
        /* Sanity checks */
        HDassert(0 == H5I_nmembers(H5I_GROUP));
        HDassert(FALSE == H5G_top_package_initialize_s);

        /* Destroy the group object id group */
        n += (H5I_dec_type_ref(H5I_GROUP) > 0);

        /* Mark closed */
        if(0 == n)
            H5_PKG_INIT_VAR = FALSE;
    } /* end if */

    FUNC_LEAVE_NOAPI(n)
} /* end H5G_term_package() */


/*-------------------------------------------------------------------------
 * Function:    H5G__close_group
 *
 * Purpose:     Called when the ref count reaches zero on the group's ID
 *
 * Return:      SUCCEED/FAIL
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5G__close_group(void *_grp)
{
    H5VL_object_t       *grp = (H5VL_object_t *)_grp;
    herr_t              ret_value = SUCCEED;    /* Return value */

    FUNC_ENTER_NOAPI_NOINIT

    HDassert(_grp);

    /* Close the group through the VOL*/
    if ((ret_value = H5VL_group_close(grp->vol_obj, grp->vol_info->vol_cls, H5AC_ind_read_dxpl_id, H5_REQUEST_NULL)) < 0)
        HGOTO_ERROR(H5E_SYM, H5E_CLOSEERROR, FAIL, "unable to close group")

    /* Free the group */
    if (H5VL_free_object(grp) < 0)
        HGOTO_ERROR(H5E_ATTR, H5E_CANTDEC, FAIL, "unable to free VOL object")

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5G__close_group() */


/*-------------------------------------------------------------------------
 * Function:	H5Gcreate2
 *
 * Purpose:	Creates a new group relative to LOC_ID, giving it the
 *              specified creation property list GCPL_ID and access
 *              property list GAPL_ID.  The link to the new group is
 *              created with the LCPL_ID.
 *
 * Usage:       H5Gcreate2(loc_id, char *name, lcpl_id, gcpl_id, gapl_id)
 *                  hid_t loc_id;	  IN: File or group identifier
 *                  const char *name; IN: Absolute or relative name of the new group
 *                  hid_t lcpl_id;	  IN: Property list for link creation
 *                  hid_t gcpl_id;	  IN: Property list for group creation
 *                  hid_t gapl_id;	  IN: Property list for group access
 *
 * Return:	Success:	The object ID of a new, empty group open for
 *				writing.  Call H5Gclose() when finished with
 *				the group.
 *
 *		Failure:	FAIL
 *
 * Programmer:  Quincey Koziol
 *	        April 5, 2007
 *
 *-------------------------------------------------------------------------
 */
hid_t
H5Gcreate2(hid_t loc_id, const char *name, hid_t lcpl_id, hid_t gcpl_id,
    hid_t gapl_id)
{
    H5G_loc_t	    loc;                /* Location to create group */
    H5G_t	   *grp = NULL;         /* New group created */
    hid_t           dxpl_id = H5AC_ind_read_dxpl_id; /* dxpl used by library */
    hid_t	    ret_value;          /* Return value */

    FUNC_ENTER_API(FAIL)
    H5TRACE5("i", "i*siii", loc_id, name, lcpl_id, gcpl_id, gapl_id);

    /* Check arguments */
    if(H5G_loc(loc_id, &loc) < 0)
	HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not a location")
    if(!name || !*name)
	HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "no name")

    /* Get correct property list */
    if(H5P_DEFAULT == lcpl_id)
        lcpl_id = H5P_LINK_CREATE_DEFAULT;
    else
        if(TRUE != H5P_isa_class(lcpl_id, H5P_LINK_CREATE))
            HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not link creation property list")

    /* Check group creation property list */
    if(H5P_DEFAULT == gcpl_id)
        gcpl_id = H5P_GROUP_CREATE_DEFAULT;
    else
        if(TRUE != H5P_isa_class(gcpl_id, H5P_GROUP_CREATE))
            HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not group create property list")

    /* Verify access property list and get correct dxpl */
    if(H5P_verify_apl_and_dxpl(&gapl_id, H5P_CLS_GACC, &dxpl_id, loc_id, TRUE) < 0)
        HGOTO_ERROR(H5E_SYM, H5E_CANTSET, FAIL, "can't set access and transfer property lists")

    /* Create the new group & get its ID */
    if(NULL == (grp = H5G__create_named(&loc, name, lcpl_id, gcpl_id, gapl_id, dxpl_id)))
        HGOTO_ERROR(H5E_SYM, H5E_CANTINIT, FAIL, "unable to create group")
    if((ret_value = H5I_register(H5I_GROUP, grp, TRUE)) < 0)
	HGOTO_ERROR(H5E_ATOM, H5E_CANTREGISTER, FAIL, "unable to register group")

done:
    if(ret_value < 0)
        if(grp && H5G_close(grp) < 0)
            HDONE_ERROR(H5E_SYM, H5E_CLOSEERROR, FAIL, "unable to release group")

    FUNC_LEAVE_API(ret_value)
} /* end H5Gcreate2() */


/*-------------------------------------------------------------------------
 * Function:	H5Gcreate_anon
 *
 * Purpose:	Creates a new group relative to LOC_ID, giving it the
 *              specified creation property list GCPL_ID and access
 *              property list GAPL_ID.
 *
 *              The resulting ID should be linked into the file with
 *              H5Olink or it will be deleted when closed.
 *
 *              Given the default setting, H5Gcreate_anon() followed by
 *              H5Olink() will have the same function as H5Gcreate2().
 *
 * Usage:       H5Gcreate_anon(loc_id, char *name, gcpl_id, gapl_id)
 *                  hid_t loc_id;	  IN: File or group identifier
 *                  const char *name; IN: Absolute or relative name of the new group
 *                  hid_t gcpl_id;	  IN: Property list for group creation
 *                  hid_t gapl_id;	  IN: Property list for group access
 *
 * Example:	To create missing groups "A" and "B01" along the given path "/A/B01/grp"
 *              hid_t create_id = H5Pcreate(H5P_GROUP_CREATE);
 *              int   status = H5Pset_create_intermediate_group(create_id, TRUE);
 *              hid_t gid = H5Gcreate_anon(file_id, "/A/B01/grp", create_id, H5P_DEFAULT);
 *
 * Return:	Success:	The object ID of a new, empty group open for
 *				writing.  Call H5Gclose() when finished with
 *				the group.
 *
 *		Failure:	FAIL
 *
 * Programmer:  Peter Cao
 *	        May 08, 2005
 *
 *-------------------------------------------------------------------------
 */
hid_t
H5Gcreate_anon(hid_t loc_id, hid_t gcpl_id, hid_t gapl_id)
{
    H5G_loc_t	    loc;
    H5G_t	   *grp = NULL;
    H5G_obj_create_t gcrt_info;         /* Information for group creation */
    hid_t           dxpl_id = H5AC_ind_read_dxpl_id; /* dxpl used by library */
    hid_t	    ret_value;

    FUNC_ENTER_API(FAIL)
    H5TRACE3("i", "iii", loc_id, gcpl_id, gapl_id);

    /* Check arguments */
    if(H5G_loc(loc_id, &loc) < 0)
	HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not a location")

    /* Check group creation property list */
    if(H5P_DEFAULT == gcpl_id)
        gcpl_id = H5P_GROUP_CREATE_DEFAULT;
    else
        if(TRUE != H5P_isa_class(gcpl_id, H5P_GROUP_CREATE))
            HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not group create property list")

    /* Verify access property list and get correct dxpl */
    if(H5P_verify_apl_and_dxpl(&gapl_id, H5P_CLS_GACC, &dxpl_id, loc_id, TRUE) < 0)
        HGOTO_ERROR(H5E_SYM, H5E_CANTSET, FAIL, "can't set access and transfer property lists")

    /* Set up group creation info */
    gcrt_info.gcpl_id = gcpl_id;
    gcrt_info.cache_type = H5G_NOTHING_CACHED;
    HDmemset(&gcrt_info.cache, 0, sizeof(gcrt_info.cache));

    /* Create the new group & get its ID */
    if(NULL == (grp = H5G__create(loc.oloc->file, &gcrt_info, dxpl_id)))
        HGOTO_ERROR(H5E_SYM, H5E_CANTINIT, FAIL, "unable to create group")
    if((ret_value = H5I_register(H5I_GROUP, grp, TRUE)) < 0)
	HGOTO_ERROR(H5E_ATOM, H5E_CANTREGISTER, FAIL, "unable to register group")

done:
    /* Release the group's object header, if it was created */
    if(grp) {
        H5O_loc_t *oloc;         /* Object location for group */

        /* Get the new group's object location */
        if(NULL == (oloc = H5G_oloc(grp)))
            HDONE_ERROR(H5E_SYM, H5E_CANTGET, FAIL, "unable to get object location of group")

        /* Decrement refcount on group's object header in memory */
        if(H5O_dec_rc_by_loc(oloc, dxpl_id) < 0)
           HDONE_ERROR(H5E_SYM, H5E_CANTDEC, FAIL, "unable to decrement refcount on newly created object")
    } /* end if */

    /* Cleanup on failure */
    if(ret_value < 0)
        if(grp && H5G_close(grp) < 0)
            HDONE_ERROR(H5E_SYM, H5E_CLOSEERROR, FAIL, "unable to release group")

    FUNC_LEAVE_API(ret_value)
} /* end H5Gcreate_anon() */


/*-------------------------------------------------------------------------
 * Function:	H5Gopen2
 *
 * Purpose:	Opens an existing group for modification.  When finished,
 *		call H5Gclose() to close it and release resources.
 *
 *              This function allows the user the pass in a Group Access
 *              Property List, which H5Gopen1() does not.
 *
 * Return:	Success:	Object ID of the group.
 *		Failure:	FAIL
 *
 * Programmer:	James Laird
 *		Thursday, July 27, 2006
 *
 *-------------------------------------------------------------------------
 */
hid_t
H5Gopen2(hid_t loc_id, const char *name, hid_t gapl_id)
{
    H5G_t       *grp = NULL;            /* Group opened */
    H5G_loc_t	loc;                    /* Location of parent for group */
    hid_t       dxpl_id = H5AC_ind_read_dxpl_id; /* dxpl used by library */
    hid_t       ret_value;              /* Return value */

    FUNC_ENTER_API(FAIL)
    H5TRACE3("i", "i*si", loc_id, name, gapl_id);

    /* Check args */
    if(H5G_loc(loc_id, &loc) < 0)
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not a location")
    if(!name || !*name)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "no name")

    /* Verify access property list and get correct dxpl */
    if(H5P_verify_apl_and_dxpl(&gapl_id, H5P_CLS_GACC, &dxpl_id, loc_id, FALSE) < 0)
        HGOTO_ERROR(H5E_SYM, H5E_CANTSET, FAIL, "can't set access and transfer property lists")

    /* Open the group */
    if((grp = H5G__open_name(&loc, name, gapl_id, dxpl_id)) == NULL)
        HGOTO_ERROR(H5E_SYM, H5E_CANTOPENOBJ, FAIL, "unable to open group")

    /* Register an ID for the group */
    if((ret_value = H5I_register(H5I_GROUP, grp, TRUE)) < 0)
        HGOTO_ERROR(H5E_ATOM, H5E_CANTREGISTER, FAIL, "unable to register group")

done:
    if(ret_value < 0) {
        if(grp && H5G_close(grp) < 0)
            HDONE_ERROR(H5E_SYM, H5E_CLOSEERROR, FAIL, "unable to release group")
    } /* end if */

    FUNC_LEAVE_API(ret_value)
} /* end H5Gopen2() */


/*-------------------------------------------------------------------------
 * Function:	H5Gget_create_plist
 *
 * Purpose:	Returns a copy of the group creation property list.
 *
 * Return:	Success:	ID for a copy of the group creation
 *				property list.  The property list ID should be
 *				released by calling H5Pclose().
 *
 *		Failure:	FAIL
 *
 * Programmer:	Quincey Koziol
 *		Tuesday, October 25, 2005
 *
 *-------------------------------------------------------------------------
 */
hid_t
H5Gget_create_plist(hid_t group_id)
{
    H5G_t		*group = NULL;
    hid_t		ret_value = FAIL;

    FUNC_ENTER_API(FAIL)
    H5TRACE1("i", "i", group_id);

    /* Check args */
    if(NULL == (group = (H5G_t *)H5I_object_verify(group_id, H5I_GROUP)))
	HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not a group")

    if((ret_value = H5G_get_create_plist(group)) < 0)
	HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not a group")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5Gget_create_plist() */


/*-------------------------------------------------------------------------
 * Function:	H5Gget_info
 *
 * Purpose:	Retrieve information about a group.
 *
 * Return:	Success:	Non-negative
 *		Failure:	Negative
 *
 * Programmer:	Quincey Koziol
 *		November 27 2006
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5Gget_info(hid_t grp_id, H5G_info_t *grp_info)
{
    H5I_type_t  id_type;                /* Type of ID */
    H5G_loc_t	loc;                    /* Location of group */
    herr_t      ret_value = SUCCEED;    /* Return value */

    FUNC_ENTER_API(FAIL)
    H5TRACE2("e", "i*x", grp_id, grp_info);

    /* Check args */
    id_type = H5I_get_type(grp_id);
    if(!(H5I_GROUP == id_type || H5I_FILE == id_type))
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "invalid argument")
    if(!grp_info)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "no info struct")

    /* Get group location */
    if(H5G_loc(grp_id, &loc) < 0)
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not a location")

    /* Retrieve the group's information */
    if(H5G__obj_info(loc.oloc, grp_info/*out*/, H5AC_ind_read_dxpl_id) < 0)
        HGOTO_ERROR(H5E_SYM, H5E_CANTGET, FAIL, "can't retrieve group info")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5Gget_info() */


/*-------------------------------------------------------------------------
 * Function:	H5Gget_info_by_name
 *
 * Purpose:	Retrieve information about a group.
 *
 * Return:	Success:	Non-negative
 *		Failure:	Negative
 *
 * Programmer:	Quincey Koziol
 *		November 27 2006
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5Gget_info_by_name(hid_t loc_id, const char *name, H5G_info_t *grp_info,
    hid_t lapl_id)
{
    H5G_loc_t	loc;                    /* Location of group */
    H5G_loc_t   grp_loc;                /* Location used to open group */
    H5G_name_t  grp_path;            	/* Opened object group hier. path */
    H5O_loc_t   grp_oloc;            	/* Opened object object location */
    hbool_t     loc_found = FALSE;      /* Location at 'name' found */
    hid_t       dxpl_id = H5AC_ind_read_dxpl_id; /* dxpl used by library */
    herr_t      ret_value = SUCCEED;    /* Return value */

    FUNC_ENTER_API(FAIL)
    H5TRACE4("e", "i*s*xi", loc_id, name, grp_info, lapl_id);

    /* Check args */
    if(H5G_loc(loc_id, &loc) < 0)
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not a location")
    if(!name || !*name)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "no name")
    if(!grp_info)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "no info struct")

    /* Verify access property list and get correct dxpl */
    if(H5P_verify_apl_and_dxpl(&lapl_id, H5P_CLS_LACC, &dxpl_id, loc_id, FALSE) < 0)
        HGOTO_ERROR(H5E_SYM, H5E_CANTSET, FAIL, "can't set access and transfer property lists")

    /* Set up opened group location to fill in */
    grp_loc.oloc = &grp_oloc;
    grp_loc.path = &grp_path;
    H5G_loc_reset(&grp_loc);

    /* Find the group object */
    if(H5G_loc_find(&loc, name, &grp_loc/*out*/, lapl_id, dxpl_id) < 0)
        HGOTO_ERROR(H5E_SYM, H5E_NOTFOUND, FAIL, "group not found")
    loc_found = TRUE;

    /* Retrieve the group's information */
    if(H5G__obj_info(grp_loc.oloc, grp_info/*out*/, dxpl_id) < 0)
        HGOTO_ERROR(H5E_SYM, H5E_CANTGET, FAIL, "can't retrieve group info")

done:
    if(loc_found && H5G_loc_free(&grp_loc) < 0)
        HDONE_ERROR(H5E_SYM, H5E_CANTRELEASE, FAIL, "can't free location")

    FUNC_LEAVE_API(ret_value)
} /* end H5Gget_info_by_name() */


/*-------------------------------------------------------------------------
 * Function:	H5Gget_info_by_idx
 *
 * Purpose:	Retrieve information about a group, according to the order
 *              of an index.
 *
 * Return:	Success:	Non-negative
 *		Failure:	Negative
 *
 * Programmer:	Quincey Koziol
 *		November 27 2006
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5Gget_info_by_idx(hid_t loc_id, const char *group_name, H5_index_t idx_type,
    H5_iter_order_t order, hsize_t n, H5G_info_t *grp_info, hid_t lapl_id)
{
    H5G_loc_t	loc;                    /* Location of group */
    H5G_loc_t   grp_loc;                /* Location used to open group */
    H5G_name_t  grp_path;            	/* Opened object group hier. path */
    H5O_loc_t   grp_oloc;            	/* Opened object object location */
    hbool_t     loc_found = FALSE;      /* Entry at 'name' found */
    hid_t       dxpl_id = H5AC_ind_read_dxpl_id; /* dxpl used by library */
    herr_t      ret_value = SUCCEED;    /* Return value */

    FUNC_ENTER_API(FAIL)
    H5TRACE7("e", "i*sIiIoh*xi", loc_id, group_name, idx_type, order, n, grp_info,
             lapl_id);

    /* Check args */
    if(H5G_loc(loc_id, &loc) < 0)
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not a location")
    if(!group_name || !*group_name)
	HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "no name specified")
    if(idx_type <= H5_INDEX_UNKNOWN || idx_type >= H5_INDEX_N)
	HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "invalid index type specified")
    if(order <= H5_ITER_UNKNOWN || order >= H5_ITER_N)
	HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "invalid iteration order specified")
    if(!grp_info)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "no info struct")

    /* Verify access property list and get correct dxpl */
    if(H5P_verify_apl_and_dxpl(&lapl_id, H5P_CLS_LACC, &dxpl_id, loc_id, FALSE) < 0)
        HGOTO_ERROR(H5E_SYM, H5E_CANTSET, FAIL, "can't set access and transfer property lists")

    /* Set up opened group location to fill in */
    grp_loc.oloc = &grp_oloc;
    grp_loc.path = &grp_path;
    H5G_loc_reset(&grp_loc);

    /* Find the object's location, according to the order in the index */
    if(H5G_loc_find_by_idx(&loc, group_name, idx_type, order, n, &grp_loc/*out*/, 
                           lapl_id, dxpl_id) < 0)
        HGOTO_ERROR(H5E_SYM, H5E_NOTFOUND, FAIL, "group not found")
    loc_found = TRUE;

    /* Retrieve the group's information */
    if(H5G__obj_info(grp_loc.oloc, grp_info/*out*/, dxpl_id) < 0)
        HGOTO_ERROR(H5E_SYM, H5E_CANTGET, FAIL, "can't retrieve group info")

done:
    /* Release the object location */
    if(loc_found && H5G_loc_free(&grp_loc) < 0)
        HDONE_ERROR(H5E_SYM, H5E_CANTRELEASE, FAIL, "can't free location")

    FUNC_LEAVE_API(ret_value)
} /* end H5Gget_info_by_idx() */


/*-------------------------------------------------------------------------
 * Function:	H5Gclose
 *
 * Purpose:	Closes the specified group.  The group ID will no longer be
 *		valid for accessing the group.
 *
 * Return:	Non-negative on success/Negative on failure
 *
 * Programmer:	Robb Matzke
 *		Wednesday, December 31, 1997
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5Gclose(hid_t group_id)
{
    herr_t  ret_value = SUCCEED;    /* Return value                     */

    FUNC_ENTER_API(FAIL)
    H5TRACE1("e", "i", group_id);

    /* Check args */
    if(NULL == H5I_object_verify(group_id,H5I_GROUP))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not a group")

    /*
     * Decrement the counter on the group atom.	 It will be freed if the count
     * reaches zero.
     */
    if(H5I_dec_app_ref(group_id) < 0)
    	HGOTO_ERROR(H5E_SYM, H5E_CANTRELEASE, FAIL, "unable to close group")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5Gclose() */


/*-------------------------------------------------------------------------
 * Function:    H5Gflush
 *
 * Purpose:     Flushes all buffers associated with a group to disk.
 *
 * Return:      Non-negative on success, negative on failure
 *
 * Programmer:  Mike McGreevy
 *              May 19, 2010
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5Gflush(hid_t group_id)
{
    H5G_t *grp;        /* Dataset for this operation */
    herr_t ret_value = SUCCEED; /* return value */

    FUNC_ENTER_API(FAIL)
    H5TRACE1("e", "i", group_id);
    
    /* Check args */
    if(NULL == (grp = (H5G_t *)H5I_object_verify(group_id, H5I_GROUP)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not a group")

    /* Flush object's metadata to file */
    if(H5O_flush_common(&grp->oloc, group_id, H5AC_ind_read_dxpl_id) < 0)
        HGOTO_ERROR(H5E_SYM, H5E_CANTFLUSH, FAIL, "unable to flush group and object flush callback")

done:
    FUNC_LEAVE_API(ret_value)
} /* H5Gflush */


/*-------------------------------------------------------------------------
 * Function:    H5Grefresh
 *
 * Purpose:     Refreshes all buffers associated with a dataset.
 *
 * Return:      Non-negative on success, negative on failure
 *
 * Programmer:  Mike McGreevy
 *              July 21, 2010
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5Grefresh(hid_t group_id)
{
    H5G_t * grp = NULL;
    herr_t ret_value = SUCCEED; /* return value */
    
    FUNC_ENTER_API(FAIL)
    H5TRACE1("e", "i", group_id);

    /* Check args */
    if(NULL == (grp = (H5G_t *)H5I_object_verify(group_id, H5I_GROUP)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not a group")

    /* Call private function to refresh group object */
    if ((H5O_refresh_metadata(group_id, grp->oloc, H5AC_ind_read_dxpl_id)) < 0)
        HGOTO_ERROR(H5E_SYM, H5E_CANTLOAD, FAIL, "unable to refresh group")

done:
    FUNC_LEAVE_API(ret_value)
} /* H5Grefresh */


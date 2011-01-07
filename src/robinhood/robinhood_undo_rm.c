/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 * Copyright (C) 2009, 2010 CEA/DAM
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the CeCILL License.
 *
 * The fact that you are presently reading this means that you have had
 * knowledge of the CeCILL license (http://www.cecill.info) and that you
 * accept its terms.
 */

/**
 * Command for restoring an entry that was accidentaly removed from filesystem.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "list_mgr.h"
#include "RobinhoodConfig.h"
#include "RobinhoodLogs.h"
#include "RobinhoodMisc.h"
#include "xplatform_print.h"
#include "backend_ext.h"

#include <unistd.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <libgen.h>
#include <pthread.h>

#define LOGTAG "UndoRm"

static struct option option_tab[] =
{
    /* options for cancelling remove operation */
    {"list", no_argument, NULL, 'L'},
    {"restore", no_argument, NULL, 'R'},

    /* config file options */
    {"config-file", required_argument, NULL, 'f'},

    /* log options */
    {"log-level", required_argument, NULL, 'l'},

    /* miscellaneous options */
    {"help", no_argument, NULL, 'h'},
    {"version", no_argument, NULL, 'V'},

    {NULL, 0, NULL, 0}

};

#define SHORT_OPT_STRING    "LRf:l:hV"

/* global variables */

static lmgr_t  lmgr;
char path_filter[1024] = "";

/* special character sequences for displaying help */

/* Bold start character sequence */
#define _B "[1m"
/* Bold end charater sequence */
#define B_ "[m"

/* Underline start character sequence */
#define _U "[4m"
/* Underline end character sequence */
#define U_ "[0m"

static const char *help_string =
    _B "Usage:" B_ " %s [options] <path|fid>\n"
    "\n"
    _B "Actions:" B_ "\n"
    "    " _B "--list" B_ ", " _B "-L" B_ "\n"
    "        List removed entries in the given directory.\n"
    "    " _B "--restore" B_ ", " _B "-R" B_ "\n"
    "        Restore removed entries in the given directory.\n"
    "\n"
    _B "Config file options:" B_ "\n"
    "    " _B "-f" B_ " " _U "file" U_ ", " _B "--config-file=" B_ _U "file" U_ "\n"
    "        Specifies path to robinhood configuration file.\n"
    "\n"
    _B "Miscellaneous options:" B_ "\n"
    "    " _B "-l" B_ " " _U "level" U_ ", " _B "--log-level=" B_ _U "level" U_ "\n"
    "        Force the log verbosity level (overides configuration value).\n"
    "        Allowed values: CRIT, MAJOR, EVENT, VERB, DEBUG, FULL.\n"
    "    " _B "-h" B_ ", " _B "--help" B_ "\n"
    "        Display a short help about command line options.\n"
    "    " _B "-V" B_ ", " _B "--version" B_ "\n" "        Display version info\n";


static inline void display_help( char *bin_name )
{
    printf( help_string, bin_name );
}

static inline void display_version( char *bin_name )
{
    printf( "\n" );
    printf( "Product:         " PACKAGE_NAME " rm cancellation tool\n" );
    printf( "Version:         " PACKAGE_VERSION "-"RELEASE"\n" );
    printf( "Build:           " COMPIL_DATE "\n" );
    printf( "\n" );
    printf( "Compilation switches:\n" );

/* purpose of this daemon */
#ifdef _LUSTRE_HSM
    printf( "    Lustre-HSM Policy Engine\n" );
#elif defined(_TMP_FS_MGR)
    printf( "    Temporary filesystem manager\n" );
#elif defined(_SHERPA)
    printf( "    SHERPA cache zapper\n" );
#elif defined(_BACKUP_FS)
    printf( "    Backup filesystem to external storage\n" );
#else
#error "No purpose was specified"
#endif

/* Access by Fid ? */
#ifdef _HAVE_FID
    printf( "    Address entries by FID\n" );
#else
    printf( "    Address entries by path\n" );
#endif

#ifdef _ENABLE_PREP_STMT
    printf( "    Prepared statements enabled\n" );
#else
    printf( "    Prepared statements disabled\n" );
#endif

    printf( "\n" );
#ifdef _LUSTRE
#ifdef LUSTRE_VERSION
    printf( "Lustre Version: " LUSTRE_VERSION "\n" );
#else
    printf( "Lustre FS support\n" );
#endif
#else
    printf( "No Lustre support\n" );
#endif

#ifdef _MYSQL
    printf( "Database binding: MySQL\n" );
#elif defined(_SQLITE)
    printf( "Database binding: SQLite\n" );
#else
#error "No database was specified"
#endif
    printf( "\n" );
    printf( "Report bugs to: <" PACKAGE_BUGREPORT ">\n" );
    printf( "\n" );
}

/*
 * Append global filters on path
 * \param do_display [in] display filters?
 * \param initialized [in/out] indicate if the filter is initialized.
 */
static int mk_path_filter( lmgr_filter_t * filter, int do_display, int * initialized )
{
    filter_value_t fv;
    char path_regexp[1024] = "";
    size_t  len;

    /* is a filter on path specified? */
    if ( !EMPTY_STRING( path_filter ) )
    {
        if ( (initialized != NULL) && !(*initialized) )
        {
            lmgr_simple_filter_init( filter );
            *initialized = TRUE;
        }
        if ( do_display )
            printf("filter path: %s\n", path_filter );

        len = strlen(path_filter);
        if ( path_filter[len-1] != '/' )
        {
            /* ( fullpath LIKE 'path' OR fullpath LIKE 'path/%' ) */
            fv.val_str = path_filter;
            lmgr_simple_filter_add( filter, ATTR_INDEX_fullpath, LIKE, fv,
                                    FILTER_FLAG_BEGIN );

            snprintf( path_regexp, 1024, "%s/*", path_filter );
            fv.val_str = path_regexp;
            lmgr_simple_filter_add( filter, ATTR_INDEX_fullpath, LIKE, fv,
                                    FILTER_FLAG_OR | FILTER_FLAG_END ); 
        }
        else /* ends with slash */
        {
            snprintf( path_regexp, 1024, "%s*", path_filter );
            /* directory or directory/% */

            fv.val_str = path_regexp;
            lmgr_simple_filter_add( filter, ATTR_INDEX_fullpath, LIKE, fv,
                                    FILTER_FLAG_BEGIN );
            /* remove last slash */
            path_filter[len-1] = '\0';
            fv.val_str = path_filter;
            lmgr_simple_filter_add( filter, ATTR_INDEX_fullpath, LIKE, fv,
                                    FILTER_FLAG_OR | FILTER_FLAG_END );
        }
    }
    return 0;
}


int list_rm()
{
    int            rc, index;
    struct lmgr_rm_list_t * list;
    entry_id_t     id;
    char   last_known_path[RBH_PATH_MAX] = "";
#ifdef _BACKUP_FS
    char   bkpath[RBH_PATH_MAX] = "";
#endif

    time_t soft_rm_time = 0;
    time_t expiration_time = 0;
    char           date_rm[128];
    char           date_exp[128];
    struct tm      t;

    unsigned long long total_count = 0;
    lmgr_filter_t  filter;

    lmgr_simple_filter_init( &filter );

    /* append global filters */
    mk_path_filter( &filter, TRUE, NULL );

    /* list all deferred rm, even if non expired */
    list = ListMgr_RmList( &lmgr, FALSE, &filter );

    if ( list == NULL )
    {
        DisplayLog( LVL_CRIT, LOGTAG,
                    "ERROR: Could not retrieve removed entries from database." );
        return -1;
    }

    index = 0;
    while ( ( rc = ListMgr_GetNextRmEntry( list, &id, last_known_path,
#ifdef _BACKUP_FS
                        bkpath,
#endif
                        &soft_rm_time, &expiration_time )) == DB_SUCCESS )
    {
        total_count++;

        index++;
        /* format last mod */
        strftime( date_rm, 128, "%Y/%m/%d %T", localtime_r( &soft_rm_time, &t ) );
        strftime( date_exp, 128, "%Y/%m/%d %T", localtime_r( &expiration_time, &t ) );

        printf( "\n" );
        printf( "Fid:               "DFID"\n", PFID(&id) );
        if ( !EMPTY_STRING(last_known_path) )
            printf( "Last known path:   %s\n", last_known_path );
#ifdef _BACKUP_FS
        if ( !EMPTY_STRING(bkpath) )
            printf( "Backend path:      %s\n", bkpath );
#endif
        printf( "Removal time:      %s\n", date_rm );
        if ( expiration_time <= time(NULL) )
        printf( "Delayed until:     %s (expired)\n", date_exp );
        else
        printf( "Delayed until:     %s\n", date_exp );

        /* prepare next call */
        last_known_path[0] = '\0';
#ifdef _BACKUP_FS
        bkpath[0] = '\0';
#endif
        soft_rm_time = 0;
        expiration_time = 0;
    }

    ListMgr_CloseRmList(list);
    return 0;
}

int undo_rm()
{
    int            rc, index;
    struct lmgr_rm_list_t * list;
    entry_id_t     id, new_id;
    attr_set_t     attrs, new_attrs;
    char   last_known_path[RBH_PATH_MAX] = "";
#ifdef _BACKUP_FS
    char   bkpath[RBH_PATH_MAX] = "";
#endif
    recov_status_t st;
    unsigned long long total_count = 0;
    lmgr_filter_t  filter;

    lmgr_simple_filter_init( &filter );

    /* append global filters */
    mk_path_filter( &filter, TRUE, NULL );

    /* list files to be recovered */
    list = ListMgr_RmList( &lmgr, FALSE, &filter );

    if ( list == NULL )
    {
        DisplayLog( LVL_CRIT, LOGTAG,
                    "ERROR: Could not retrieve removed entries from database." );
        return -1;
    }

    while ( ( rc = ListMgr_GetNextRmEntry( list, &id, last_known_path,
#ifdef _BACKUP_FS
                        bkpath,
#endif
                        NULL, NULL )) == DB_SUCCESS )
    {
        total_count++;

        if ( EMPTY_STRING( last_known_path ) )
        {
            fprintf(stderr, "Last filesystem path is not known for fid "DFID", backend_path=%s.\n",
                    PFID(&id), bkpath);
            fprintf(stderr, " ----> skipped\n");
            continue;
        }

        printf("Restoring '%s'...\n", last_known_path );

        ATTR_MASK_INIT( &attrs );
        ATTR_MASK_SET(&attrs, fullpath);
        strcpy( ATTR(&attrs, fullpath), last_known_path );

        if ( !EMPTY_STRING( bkpath ) )
        {
            ATTR_MASK_SET(&attrs, backendpath);
            strcpy( ATTR(&attrs, backendpath), bkpath );
        }

        /* copy file to Lustre */
        st = rbhext_recover( &id, &attrs, &new_id, &new_attrs );
        if ( (st == RS_OK) || (st == RS_DELTA) )
        {
            printf("Success\n");
            if ( ListMgr_SoftRemove_Discard(&lmgr, &id) != 0 )
                fprintf(stderr, "Error: could not remove previous id "DFID" from database\n", PFID(&id) );
            /* insert or update it in the db */
            rc = ListMgr_Insert( &lmgr, &new_id, &new_attrs );
            if ( rc == DB_ALREADY_EXISTS )
            {
                /* try to update */
                rc = ListMgr_Update( &lmgr, &new_id, &new_attrs );
            }
            if ( rc == 0 )
                printf("Entry successfully updated in the dabatase\n");
            else
                fprintf(stderr, "ERROR %d inserting entry in the database\n", rc );
        }
        else
        {
            printf("ERROR\n");
        }

    }

    /* discard entry from remove list */
}



#define MAX_OPT_LEN 1024

/**
 * Main daemon routine
 */
int main( int argc, char **argv )
{
    int            c, option_index = 0;
    char          *bin = basename( argv[0] );

    char           config_file[MAX_OPT_LEN] = "";

    enum { ACTION_NONE, ACTION_LIST, ACTION_RESTORE } action = ACTION_NONE;
    int            force_log_level = FALSE;
    int            log_level = 0;

    int            rc;
    char           err_msg[4096];
    robinhood_config_t config;

    /* parse command line options */
    while ( ( c = getopt_long( argc, argv, SHORT_OPT_STRING, option_tab,
                               &option_index ) ) != -1 )
    {
        switch ( c )
        {
        case 'L':
            if ( (action != ACTION_NONE) && (action != ACTION_LIST) )
                fprintf( stderr, "WARNING: only a single action (--list or --restore) is expected\n"
                                 "on command line. '--restore' will be ignored.\n" );
            action = ACTION_LIST;
            break;
        case 'R':
            if ( (action != ACTION_NONE) && (action != ACTION_RESTORE) )
                fprintf( stderr, "WARNING: only a single action (--list or --restore) is expected\n"
                                 "on command line. '--list' will be ignored.\n" );
            action = ACTION_RESTORE;
            break;
        case 'f':
            strncpy( config_file, optarg, MAX_OPT_LEN );
            break;
        case 'l':
            force_log_level = TRUE;
            log_level = str2debuglevel( optarg );
            if ( log_level == -1 )
            {
                fprintf( stderr,
                         "Unsupported log level '%s'. CRIT, MAJOR, EVENT, VERB, DEBUG or FULL expected.\n",
                         optarg );
                exit( 1 );
            }
            break;
        case 'h':
            display_help( bin );
            exit( 0 );
            break;
        case 'V':
            display_version( bin );
            exit( 0 );
            break;
        case ':':
        case '?':
        default:
            display_help( bin );
            exit( 1 );
            break;
        }
    }

    /* 1 expected argument: path|fid */
    if ( optind != argc - 1 )
    {
        fprintf( stderr, "Error: missing mandatory argument on command line: <path|fid>\n" );
        exit( 1 );
    }
    strncpy( path_filter, argv[optind], 1024 );

    /* get default config file, if not specified */
    if ( EMPTY_STRING( config_file ) )
    {
        if ( SearchConfig( config_file ) != 0 )
        {
            fprintf(stderr, "No config file found in '/etc/robinhood.d/"PURPOSE_EXT"'\n" );
            exit(2);
        }
        else
        {
            fprintf(stderr, "No config file specified, using '%s'.\n", config_file );
        }
    }

    /* only read ListMgr config */
    if ( ReadRobinhoodConfig( 0, config_file, err_msg, &config ) )
    {
        fprintf( stderr, "Error reading configuration file '%s': %s\n", config_file, err_msg );
        exit( 1 );
    }

    /* set global configuration */
    global_config = config.global_config;

    /* set policies info */
    policies = config.policies;

    if ( force_log_level )
        config.log_config.debug_level = log_level;

    /* XXX HOOK: Set logging to stderr */
    strcpy( config.log_config.log_file, "stderr" );
    strcpy( config.log_config.report_file, "stderr" );
    strcpy( config.log_config.alert_file, "stderr" );

    /* Initialize logging */
    rc = InitializeLogs( bin, &config.log_config );
    if ( rc )
    {
        fprintf( stderr, "Error opening log files: rc=%d, errno=%d: %s\n",
                 rc, errno, strerror( errno ) );
        exit( rc );
    }

    /* Initialize list manager */
    rc = ListMgr_Init( &config.lmgr_config );
    if ( rc )
    {
        DisplayLog( LVL_CRIT, LOGTAG, "Error %d initializing list manager", rc );
        exit( rc );
    }
    else
        DisplayLog( LVL_DEBUG, LOGTAG, "ListManager successfully initialized" );

    if ( CheckLastFS(  ) != 0 )
        exit( 1 );

    /* Create database access */
    rc = ListMgr_InitAccess( &lmgr );
    if ( rc )
    {
        DisplayLog( LVL_CRIT, LOGTAG, "Error %d: cannot connect to database", rc );
        exit( rc );
    }

#ifdef _BACKUP_FS
    rc = Backend_Start( &config.backend_config, 0 );
    if ( rc )
    {
        DisplayLog( LVL_CRIT, LOGTAG, "Error initializing backend" );
        exit( 1 );
    }
#endif

    /* perform the action */
    switch( action )
    {
        case ACTION_LIST:
            rc= list_rm();
            break;
        case ACTION_RESTORE:
            rc = undo_rm(); 
            break;
        case ACTION_NONE:
            display_help( bin );
            rc = 1;
            break;
        default:
            fprintf(stderr, "Unexpected action (action code=%#x)\n", action );
            display_help( bin );
            rc = EINVAL;
            break;
    }

    ListMgr_CloseAccess( &lmgr );

    return rc;

}

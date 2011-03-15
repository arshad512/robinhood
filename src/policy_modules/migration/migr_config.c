/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 * Copyright (C) 2009 CEA/DAM
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the CeCILL License.
 *
 * The fact that you are presently reading this means that you have had
 * knowledge of the CeCILL license (http://www.cecill.info) and that you
 * accept its terms.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "RobinhoodConfig.h"
#include "migration.h"
#include "migr_arch.h"
#include <errno.h>

#define MIGR_PARAM_BLOCK   "Migration_Parameters"
#define MIGRCFG_TAG       "MigrConfig"



int SetDefault_Migration_Config( void *module_config, char *msg_out )
{
    migration_config_t *conf = ( migration_config_t * ) module_config;
    msg_out[0] = '\0';

    conf->runtime_interval = 5 * 60;    /* 5 min */
    conf->nb_threads_migr = 4;
    conf->migr_queue_size = 4096;
    conf->db_request_limit = 10000;
    conf->max_migr_nbr = 0;
    conf->max_migr_vol = 0;
#if defined(_LUSTRE_HSM) || defined(_BACKUP_FS)
    conf->backup_new_files = TRUE;
#endif
    conf->check_copy_status_on_startup = TRUE;
    conf->check_copy_status_delay = 30 * 60; /* 30 min */
    conf->migration_timeout = 2 * 3600; /* cancel migration pass after 2h of inactivity */

    conf->pre_maintenance_window = 24 * 3600; /* 24h */
    conf->maint_min_migr_delay = 30 * 60; /* 30 min */

    return 0;
}

int Write_Migration_ConfigDefault( FILE * output )
{
    print_begin_block( output, 0, MIGR_PARAM_BLOCK, NULL );
    print_line( output, 1, "runtime_interval      : 5min" );
    print_line( output, 1, "max_migration_count   : unlimited (0)" );
    print_line( output, 1, "max_migration_volume  : unlimited (0)" );
    print_line( output, 1, "migration_timeout     : 2h" );
    print_line( output, 1, "pre_maintenance_window: 24h" );
    print_line( output, 1, "maint_migr_delay_min  : 30min" );

#if defined( _LUSTRE_HSM) || defined(_BACKUP_FS)
    print_line( output, 1, "backup_new_files      : TRUE" );
#endif
    print_line( output, 1, "check_copy_status_on_startup : TRUE" );
    fprintf( output, "\n" );
    print_line( output, 1, "check_copy_status_delay      : 30min" );
    fprintf( output, "\n" );
    print_line( output, 1, "nb_threads_migration  : 4" );
    print_line( output, 1, "migration_queue_size  : 4096" );
    fprintf( output, "\n" );
    print_line( output, 1, "db_result_size_max    : 10000" );
    print_end_block( output, 0 );

    fprintf( output, "\n" );

    return 0;
}

int Write_Migration_ConfigTemplate( FILE * output )
{
    print_begin_block( output, 0, MIGR_PARAM_BLOCK, NULL );

    print_line( output, 1, "# interval for running migrations" );
    print_line( output, 1, "runtime_interval      = 5min ;" );
    fprintf( output, "\n" );
    print_line( output, 1, "# maximum number of migration requests per pass" );
    print_line( output, 1, "# (0: unlimited)" );
    print_line( output, 1, "max_migration_count   = 0 ;" );
    fprintf( output, "\n" );
    print_line( output, 1, "# maximum volume of migration requests per pass" );
    print_line( output, 1, "# (0: unlimited)" );
    print_line( output, 1, "max_migration_volume  = 10TB ;" );
    fprintf( output, "\n" );
    print_line( output, 1, "# cancel migration pass after 2h of inactivity" );
    print_line( output, 1, "migration_timeout     = 2h ;" );
    fprintf( output, "\n" );
#if defined( _LUSTRE_HSM) || defined(_BACKUP_FS)
    print_line( output, 1, "# don't archive files that have never been archived before" );
    print_line( output, 1, "backup_new_files      = FALSE ;" );
    fprintf( output, "\n" );
#endif
    print_line( output, 1, "# do we check status of outstanding migration requests on startup?" );
    print_line( output, 1, "check_copy_status_on_startup = TRUE ;" );
    fprintf( output, "\n" );
    print_line( output, 1, "# delay for checking copy status if no feedback has been received");
    print_line( output, 1, "check_copy_status_delay = 1h ;" );
    fprintf( output, "\n" );
    print_line( output, 1, "# number of threads for issuing migration requests" );
    print_line( output, 1, "nb_threads_migration   = 4 ;" );
    fprintf( output, "\n" );
    print_line( output, 1, "# pre-maintenance mode parameters" );
    print_line( output, 1, "pre_maintenance_window = 24h ;" );
    print_line( output, 1, "maint_migr_delay_min   = 30min ;" );
    fprintf( output, "\n" );
    print_line( output, 1, "# internal/tuning parameters" );
    print_line( output, 1, "migration_queue_size   = 4096 ;" );
    print_line( output, 1, "db_result_size_max     = 10000 ;" );
    print_end_block( output, 0 );

    fprintf( output, "\n" );

    return 0;
}

#define critical_err_check(_ptr_, _blkname_) do { if (!_ptr_) {\
                                        sprintf( msg_out, "Internal error reading %s block in config file", _blkname_); \
                                        return EFAULT; \
                                    }\
                                } while (0)

int Read_Migration_Config( config_file_t config, void *module_config,
                           char *msg_out, int for_reload )
{
    int            rc;
    int            intval;
    migration_config_t *conf = ( migration_config_t * ) module_config;

    static const char *migr_allowed[] = {
        "runtime_interval", "max_migration_count", "max_migration_volume",
#if defined( _LUSTRE_HSM) || defined(_BACKUP_FS)
        "backup_new_files",
#endif
        "check_copy_status_on_startup", "check_copy_status_delay", "migration_timeout",
        "nb_threads_migration", "migration_queue_size", "db_result_size_max",
        "pre_maintenance_window", "maint_migr_delay_min",
        NULL
    };

    /* get MIGR_PARAM block */

    config_item_t  param_block = rh_config_FindItemByName( config, MIGR_PARAM_BLOCK );
    if ( param_block == NULL )
    {
        /* no error, because no parameter is mandatory */
        return 0;
    }

    /* check this is a block... */
    if ( rh_config_ItemType( param_block ) != CONFIG_ITEM_BLOCK )
    {
        strcpy( msg_out, "A block is expected for '" MIGR_PARAM_BLOCK "' item" );
        return EINVAL;
    }

    /* parse parameters */

    rc = GetDurationParam( param_block, MIGR_PARAM_BLOCK, "runtime_interval",
                           INT_PARAM_POSITIVE | INT_PARAM_NOT_NULL, &intval, NULL, NULL, msg_out );
    if ( ( rc != 0 ) && ( rc != ENOENT ) )
        return rc;
    else if ( rc != ENOENT )
        conf->runtime_interval = intval;

    rc = GetIntParam( param_block, MIGR_PARAM_BLOCK, "max_migration_count",
                      INT_PARAM_POSITIVE, ( int * ) &conf->max_migr_nbr, NULL, NULL, msg_out );
    if ( ( rc != 0 ) && ( rc != ENOENT ) )
        return rc;

    rc = GetSizeParam( param_block, MIGR_PARAM_BLOCK, "max_migration_volume",
                       INT_PARAM_POSITIVE, &conf->max_migr_vol, NULL, NULL, msg_out );
    if ( ( rc != 0 ) && ( rc != ENOENT ) )
        return rc;

    rc = GetDurationParam( param_block, MIGR_PARAM_BLOCK, "migration_timeout",
                           INT_PARAM_POSITIVE | INT_PARAM_NOT_NULL, &intval, NULL, NULL, msg_out );
    if ( ( rc != 0 ) && ( rc != ENOENT ) )
        return rc;
    else if ( rc != ENOENT)
        conf->migration_timeout = intval;

#if defined( _LUSTRE_HSM) || defined(_BACKUP_FS)
    rc = GetBoolParam( param_block, MIGR_PARAM_BLOCK, "backup_new_files",
                       0, &intval, NULL, NULL, msg_out );
    if ( ( rc != 0 ) && ( rc != ENOENT ) )
        return rc;
    else if ( rc != ENOENT )
        conf->backup_new_files = intval;
#endif

    rc = GetBoolParam( param_block, MIGR_PARAM_BLOCK, "check_copy_status_on_startup",
                       0, &intval, NULL, NULL, msg_out );
    if ( ( rc != 0 ) && ( rc != ENOENT ) )
        return rc;
    else if ( rc != ENOENT )
        conf->check_copy_status_on_startup = intval;

    rc = GetDurationParam( param_block, MIGR_PARAM_BLOCK, "check_copy_status_delay",
                           INT_PARAM_POSITIVE | INT_PARAM_NOT_NULL, &intval, NULL, NULL, msg_out );
    if ( ( rc != 0 ) && ( rc != ENOENT ) )
        return rc;
    else if ( rc != ENOENT )
        conf->check_copy_status_delay = intval;

    rc = GetBoolParam( param_block, MIGR_PARAM_BLOCK, "simulation_mode",
                       0, &intval, NULL, NULL, msg_out );
    if ( rc == 0 )
        DisplayLog( LVL_CRIT, MIGRCFG_TAG, "WARNING: deprecated parameter 'simulation_mode'. Use '--dry-run' option instead");

    rc = GetIntParam( param_block, MIGR_PARAM_BLOCK, "nb_threads_migration",
                      INT_PARAM_POSITIVE | INT_PARAM_NOT_NULL, ( int * ) &conf->nb_threads_migr,
                      NULL, NULL, msg_out );
    if ( ( rc != 0 ) && ( rc != ENOENT ) )
        return rc;

    rc = GetIntParam( param_block, MIGR_PARAM_BLOCK, "migration_queue_size",
                      INT_PARAM_POSITIVE | INT_PARAM_NOT_NULL, ( int * ) &conf->migr_queue_size,
                      NULL, NULL, msg_out );
    if ( ( rc != 0 ) && ( rc != ENOENT ) )
        return rc;

    rc = GetIntParam( param_block, MIGR_PARAM_BLOCK, "db_result_size_max",
                      INT_PARAM_POSITIVE, ( int * ) &conf->db_request_limit, NULL, NULL, msg_out );
    if ( ( rc != 0 ) && ( rc != ENOENT ) )
        return rc;

    rc = GetDurationParam( param_block, MIGR_PARAM_BLOCK, "pre_maintenance_window",
                           INT_PARAM_POSITIVE, &intval, NULL, NULL, msg_out );
    if ( ( rc != 0 ) && ( rc != ENOENT ) )
        return rc;
    else if ( rc != ENOENT )
        conf->pre_maintenance_window = intval;

    rc = GetDurationParam( param_block, MIGR_PARAM_BLOCK, "maint_migr_delay_min",
                           INT_PARAM_POSITIVE, &intval, NULL, NULL, msg_out );
    if ( ( rc != 0 ) && ( rc != ENOENT ) )
        return rc;
    else  if ( rc != ENOENT )
        conf->maint_min_migr_delay = intval;

    CheckUnknownParameters( param_block, MIGR_PARAM_BLOCK, migr_allowed );

    return 0;

}


int Reload_Migration_Config( void *module_config )
{
    migration_config_t *conf = ( migration_config_t * ) module_config;

    /* parameters that can't be modified dynamically */

    if ( migr_config.nb_threads_migr != conf->nb_threads_migr )
        DisplayLog( LVL_MAJOR, MIGRCFG_TAG,
                    MIGR_PARAM_BLOCK
                    "::nb_threads_migration changed in config file, but cannot be modified dynamically" );

    if ( migr_config.migr_queue_size != conf->migr_queue_size )
        DisplayLog( LVL_MAJOR, MIGRCFG_TAG,
                    MIGR_PARAM_BLOCK
                    "::migration_queue_size changed in config file, but cannot be modified dynamically" );

    if ( migr_config.check_copy_status_on_startup != conf->check_copy_status_on_startup )
        DisplayLog( LVL_MAJOR, MIGRCFG_TAG, MIGR_PARAM_BLOCK "::check_copy_status_on_startup"
                    " changed in config file, but this doesn't make sense to modify it dynamically" );

    /* dynamic parameters */

    if ( migr_config.max_migr_nbr != conf->max_migr_nbr )
    {
        DisplayLog( LVL_EVENT, MIGRCFG_TAG, MIGR_PARAM_BLOCK
                    "::max_migration_count updated: %u->%u",
                    migr_config.max_migr_nbr, conf->max_migr_nbr );
        migr_config.max_migr_nbr = conf->max_migr_nbr;
    }

    if ( migr_config.max_migr_vol != conf->max_migr_vol )
    {
        DisplayLog( LVL_EVENT, MIGRCFG_TAG, MIGR_PARAM_BLOCK
                    "::max_migration_volume updated: %llu->%llu",
                    migr_config.max_migr_vol, conf->max_migr_vol );
        migr_config.max_migr_vol = conf->max_migr_vol;
    }


    if ( migr_config.runtime_interval != conf->runtime_interval )
    {
        DisplayLog( LVL_EVENT, MIGRCFG_TAG, MIGR_PARAM_BLOCK
                    "::runtime_interval updated: %u->%u",
                    ( unsigned int ) migr_config.runtime_interval,
                    ( unsigned int ) conf->runtime_interval );
        migr_config.runtime_interval = conf->runtime_interval;
    }

    if ( migr_config.migration_timeout != conf->migration_timeout )
    {
        DisplayLog( LVL_EVENT, MIGRCFG_TAG, MIGR_PARAM_BLOCK
                    "::migration_timeout updated: %u->%u",
                    ( unsigned int ) migr_config.migration_timeout,
                    ( unsigned int ) conf->migration_timeout );
        migr_config.migration_timeout = conf->migration_timeout;
    }


#if defined( _LUSTRE_HSM) || defined(_BACKUP_FS)
    if ( migr_config.backup_new_files != conf->backup_new_files )
    {
        DisplayLog( LVL_EVENT, MIGRCFG_TAG, MIGR_PARAM_BLOCK
                    "::backup_new_files updated: %u->%u",
                    migr_config.backup_new_files, conf->backup_new_files );
        migr_config.backup_new_files = conf->backup_new_files;
    }
#endif

    if ( migr_config.db_request_limit != conf->db_request_limit )
    {
        DisplayLog( LVL_EVENT, MIGRCFG_TAG, MIGR_PARAM_BLOCK
                    "::db_result_size_max updated: %u->%u",
                    migr_config.db_request_limit, conf->db_request_limit );
        migr_config.db_request_limit = conf->db_request_limit;
    }

    if ( migr_config.check_copy_status_delay != conf->check_copy_status_delay )
    {
        DisplayLog( LVL_EVENT, MIGRCFG_TAG, MIGR_PARAM_BLOCK
                    "::check_copy_status_delay updated: %u->%u",
                    ( unsigned int ) migr_config.check_copy_status_delay,
                    ( unsigned int ) conf->check_copy_status_delay );
        migr_config.check_copy_status_delay = conf->check_copy_status_delay;
    }

    if ( migr_config.pre_maintenance_window != conf->pre_maintenance_window )
    {
        DisplayLog( LVL_EVENT, MIGRCFG_TAG, MIGR_PARAM_BLOCK
                    "::pre_maintenance_window updated: %u->%u",
                    ( unsigned int ) migr_config.pre_maintenance_window,
                    ( unsigned int ) conf->pre_maintenance_window );
        migr_config.pre_maintenance_window = conf->pre_maintenance_window;
    }

    if ( migr_config.maint_min_migr_delay != conf->maint_min_migr_delay )
    {
        DisplayLog( LVL_EVENT, MIGRCFG_TAG, MIGR_PARAM_BLOCK
                    "::maint_migr_delay_min updated: %u->%u",
                    ( unsigned int ) migr_config.maint_min_migr_delay,
                    ( unsigned int ) conf->maint_min_migr_delay );
        migr_config.maint_min_migr_delay = conf->maint_min_migr_delay;
    }


    return 0;
}

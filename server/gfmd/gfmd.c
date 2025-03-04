/*
 * $Id$
 */

#include <gfarm/gfarm_config.h>

#include <pthread.h>

#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <syslog.h>
#include <stdarg.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <libgen.h>
#include <time.h>

#include <gfarm/error.h>
#include <gfarm/gflog.h>
#include <gfarm/gfarm_misc.h>
#include <gfarm/gfs.h>
#include <gfarm/gfarm_iostat.h>

#include "gfutil.h"
#include "gflog_reduced.h"
#include "thrsubr.h"

#include "quota_info.h"
#include "context.h"
#include "gfp_xdr.h"
#include "hostspec.h"
#include "sockopt.h"
#include "auth.h"
#include "config.h"
#include "gfm_proto.h"

#include "subr.h"
#include "thrpool.h"
#include "callout.h"
#include "journal_file.h"	/* for enum journal_operation */
#include "db_access.h"
#include "db_journal.h"
#include "db_journal_apply.h"
#include "host.h"
#include "fsngroup.h"
#include "mdhost.h"
#include "user.h"
#include "group.h"
#include "peer.h"
#include "inode.h"
#include "file_copy.h"
#include "dead_file_copy.h"
#include "xattr.h"
#include "quota.h"
#include "dirset.h"
#include "quota_dir.h"
#include "gfmd.h"
#include "process.h"
#include "fs.h"
#include "job.h"
#include "back_channel.h"
#include "gfmd_channel.h"
#include "replica_check.h"

#include "protocol_state.h"
#include "iostat.h"

#ifndef GFMD_CONFIG
#define GFMD_CONFIG		"/etc/gfmd.conf"
#endif

#define GFMD_FAILOVER_CONFIG_BASENAME	"gfmd.failover.conf"

#ifndef CALLOUT_NTHREADS
/*
 * this is number of thread pools which are used by callouts,
 * so thrpool_add_job() in callout_main() won't be blocked for long time.
 *
 * currently, only back_channel_send_thread_pool is called from callouts.
 */
#define CALLOUT_NTHREADS	1
#endif

char *program_name = "gfmd";
int gfmd_port;
static char *pid_file;

struct thread_pool *authentication_thread_pool;
struct peer_watcher *sync_protocol_watcher;

static char *iostat_dirbuf;

static const char TRANSFORM_MUTEX_DIAG[] = "transform_mutex";
static const char TRANSFORM_COND_DIAG[] = "transform_cond";

struct thread_pool *
sync_protocol_get_thrpool(void)
{
	return (peer_watcher_get_thrpool(sync_protocol_watcher));
}

/* this interface is exported for a use from a private extension */
gfarm_error_t
gfm_server_protocol_extension_default(struct peer *peer,
	int from_client, int skip, int level, gfarm_int32_t request,
	gfarm_int32_t last_sync_request,
	gfarm_int32_t *requestp, gfarm_error_t *on_errorp)
{
	gflog_warning(GFARM_MSG_1000181, "unknown request: %d", request);
	gflog_info(GFARM_MSG_1005215, "last request: %d", last_sync_request);
	peer_record_protocol_error(peer);
	return (GFARM_ERR_PROTOCOL);
}

/* this interface is made as a hook for a private extension */
gfarm_error_t (*gfm_server_protocol_extension)(struct peer *,
	int, int, int, gfarm_int32_t, gfarm_int32_t,
	gfarm_int32_t *, gfarm_error_t *) =
		gfm_server_protocol_extension_default;

gfarm_error_t
protocol_switch(struct peer *peer, int from_client, int skip, int level,
	gfarm_int32_t last_sync_request,
	gfarm_int32_t *requestp, gfarm_error_t *on_errorp, int *suspendedp)
{
	gfarm_error_t e, e2;
	gfarm_int32_t request;

	e = gfp_xdr_recv_request_command(peer_get_conn(peer), 0, NULL,
	    &request);
	if (e != GFARM_ERR_NO_ERROR) {
		if (e == GFARM_ERR_UNEXPECTED_EOF) {
			e = GFARM_ERR_NO_ERROR;
		} else {
			gflog_notice(GFARM_MSG_1000180,
			    "receiving request number: %s",
			    gfarm_error_string(e));
		}
		peer_record_protocol_error(peer); /* mark this peer finished */
		*requestp = GFM_PROTO_PRIVATE_END; /* caller needs *requestp */
		return (e);
	}
	*requestp = request;

	peer_stat_add(peer, GFARM_IOSTAT_TRAN_NUM, 1);
	switch (request) {
	case GFM_PROTO_HOST_INFO_GET_ALL:
		e = gfm_server_host_info_get_all(peer, from_client, skip);
		break;
	case GFM_PROTO_HOST_INFO_GET_BY_ARCHITECTURE:
		e = gfm_server_host_info_get_by_architecture(peer,
		    from_client, skip);
		break;
	case GFM_PROTO_HOST_INFO_GET_BY_NAMES:
		e = gfm_server_host_info_get_by_names(peer, from_client, skip);
		break;
	case GFM_PROTO_HOST_INFO_GET_BY_NAMEALIASES:
		e = gfm_server_host_info_get_by_namealiases(peer,
		    from_client, skip);
		break;
	case GFM_PROTO_HOST_INFO_SET:
		e = gfm_server_host_info_set(peer, from_client, skip);
		break;
	case GFM_PROTO_HOST_INFO_MODIFY:
		e = gfm_server_host_info_modify(peer, from_client, skip);
		break;
	case GFM_PROTO_HOST_INFO_REMOVE:
		e = gfm_server_host_info_remove(peer, from_client, skip);
		break;
	case GFM_PROTO_FSNGROUP_GET_ALL:
		e = gfm_server_fsngroup_get_all(peer, from_client, skip);
		break;
	case GFM_PROTO_FSNGROUP_GET_BY_HOSTNAME:
		e = gfm_server_fsngroup_get_by_hostname(peer, from_client,
		    skip);
		break;
	case GFM_PROTO_FSNGROUP_MODIFY:
		e = gfm_server_fsngroup_modify(peer, from_client, skip);
		break;
	case GFM_PROTO_USER_INFO_GET_ALL:
		e = gfm_server_user_info_get_all(peer, from_client, skip);
		break;
	case GFM_PROTO_USER_INFO_GET_BY_NAMES:
		e = gfm_server_user_info_get_by_names(peer, from_client, skip);
		break;
	case GFM_PROTO_USER_INFO_SET:
		e = gfm_server_user_info_set(peer, from_client, skip);
		break;
	case GFM_PROTO_USER_INFO_MODIFY:
		e = gfm_server_user_info_modify(peer, from_client, skip);
		break;
	case GFM_PROTO_USER_INFO_REMOVE:
		e = gfm_server_user_info_remove(peer, from_client, skip);
		break;
	case GFM_PROTO_USER_INFO_GET_BY_GSI_DN:
		e = gfm_server_user_info_get_by_gsi_dn(
			peer, from_client, skip);
		break;
	case GFM_PROTO_GROUP_INFO_GET_ALL:
		e = gfm_server_group_info_get_all(peer, from_client, skip);
		break;
	case GFM_PROTO_GROUP_INFO_GET_BY_NAMES:
		e = gfm_server_group_info_get_by_names(peer,
		    from_client, skip);
		break;
	case GFM_PROTO_GROUP_INFO_SET:
		e = gfm_server_group_info_set(peer, from_client, skip);
		break;
	case GFM_PROTO_GROUP_INFO_MODIFY:
		e = gfm_server_group_info_modify(peer, from_client, skip);
		break;
	case GFM_PROTO_GROUP_INFO_REMOVE:
		e = gfm_server_group_info_remove(peer, from_client, skip);
		break;
	case GFM_PROTO_GROUP_INFO_ADD_USERS:
		e = gfm_server_group_info_add_users(peer, from_client, skip);
		break;
	case GFM_PROTO_GROUP_INFO_REMOVE_USERS:
		e = gfm_server_group_info_remove_users(peer,
		    from_client, skip);
		break;
	case GFM_PROTO_GROUP_NAMES_GET_BY_USERS:
		e = gfm_server_group_names_get_by_users(peer,
		    from_client, skip);
		break;
	case GFM_PROTO_COMPOUND_BEGIN:
		e = gfm_server_compound_begin(peer, from_client, skip, level);
		break;
	case GFM_PROTO_COMPOUND_END:
		skip = peer_get_protocol_state(peer)->cs.cause
			!= GFARM_ERR_NO_ERROR;
		e = gfm_server_compound_end(peer, from_client, skip, level);
		break;
	case GFM_PROTO_COMPOUND_ON_ERROR:
		e = gfm_server_compound_on_error(peer, from_client, skip,
		    level, on_errorp);
		break;
	case GFM_PROTO_GET_FD:
		e = gfm_server_get_fd(peer, from_client, skip);
		break;
	case GFM_PROTO_PUT_FD:
		e = gfm_server_put_fd(peer, from_client, skip);
		break;
	case GFM_PROTO_SAVE_FD:
		e = gfm_server_save_fd(peer, from_client, skip);
		break;
	case GFM_PROTO_RESTORE_FD:
		e = gfm_server_restore_fd(peer, from_client, skip);
		break;
	case GFM_PROTO_CREATE:
		e = gfm_server_create(peer, from_client, skip);
		break;
	case GFM_PROTO_OPEN:
		e = gfm_server_open(peer, from_client, skip);
		break;
	case GFM_PROTO_OPEN_ROOT:
		e = gfm_server_open_root(peer, from_client, skip);
		break;
	case GFM_PROTO_OPEN_PARENT:
		e = gfm_server_open_parent(peer, from_client, skip);
		break;
	case GFM_PROTO_FHOPEN:
		e = gfm_server_fhopen(peer, from_client, skip);
		break;
	case GFM_PROTO_CLOSE:
		e = gfm_server_close(peer, from_client, skip);
		break;
	case GFM_PROTO_CLOSE_GETGEN:
		e = gfm_server_close_getgen(peer, from_client, skip);
		break;
	case GFM_PROTO_VERIFY_TYPE:
		e = gfm_server_verify_type(peer, from_client, skip);
		break;
	case GFM_PROTO_VERIFY_TYPE_NOT:
		e = gfm_server_verify_type_not(peer, from_client, skip);
		break;
	case GFM_PROTO_BEQUEATH_FD:
		e = gfm_server_bequeath_fd(peer, from_client, skip);
		break;
	case GFM_PROTO_INHERIT_FD:
		e = gfm_server_inherit_fd(peer, from_client, skip);
		break;
	case GFM_PROTO_REVOKE_GFSD_ACCESS:
		e = gfm_server_revoke_gfsd_access(peer, from_client, skip);
		break;
	case GFM_PROTO_FSTAT:
		e = gfm_server_fstat(peer, from_client, skip);
		break;
	case GFM_PROTO_FUTIMES:
		e = gfm_server_futimes(peer, from_client, skip);
		break;
	case GFM_PROTO_FCHMOD:
		e = gfm_server_fchmod(peer, from_client, skip);
		break;
	case GFM_PROTO_FCHOWN:
		e = gfm_server_fchown(peer, from_client, skip);
		break;
	case GFM_PROTO_CKSUM_GET:
		e = gfm_server_cksum_get(peer, from_client, skip);
		break;
	case GFM_PROTO_CKSUM_SET:
		e = gfm_server_cksum_set(peer, from_client, skip);
		break;
	case GFM_PROTO_SCHEDULE_FILE:
		e = gfm_server_schedule_file(peer, from_client, skip);
		break;
	case GFM_PROTO_SCHEDULE_FILE_WITH_PROGRAM:
		e = gfm_server_schedule_file_with_program(peer,
		    from_client, skip);
		break;
	case GFM_PROTO_FGETATTRPLUS:
		e = gfm_server_fgetattrplus(peer, from_client, skip);
		break;
	case GFM_PROTO_REMOVE:
		e = gfm_server_remove(peer, from_client, skip);
		break;
	case GFM_PROTO_RENAME:
		e = gfm_server_rename(peer, from_client, skip);
		break;
	case GFM_PROTO_FLINK:
		e = gfm_server_flink(peer, from_client, skip);
		break;
	case GFM_PROTO_MKDIR:
		e = gfm_server_mkdir(peer, from_client, skip);
		break;
	case GFM_PROTO_SYMLINK:
		e = gfm_server_symlink(peer, from_client, skip);
		break;
	case GFM_PROTO_READLINK:
		e = gfm_server_readlink(peer, from_client, skip);
		break;
	case GFM_PROTO_GETDIRPATH:
		e = gfm_server_getdirpath(peer, from_client, skip);
		break;
	case GFM_PROTO_GETDIRENTS:
		e = gfm_server_getdirents(peer, from_client, skip);
		break;
	case GFM_PROTO_SEEK:
		e = gfm_server_seek(peer, from_client, skip);
		break;
	case GFM_PROTO_GETDIRENTSPLUS:
		e = gfm_server_getdirentsplus(peer, from_client, skip);
		break;
	case GFM_PROTO_GETDIRENTSPLUSXATTR:
		e = gfm_server_getdirentsplusxattr(peer, from_client, skip);
		break;
	case GFM_PROTO_REOPEN:
		e = gfm_server_reopen(peer, from_client, skip,
		    suspendedp);
		break;
	case GFM_PROTO_CLOSE_READ:
		e = gfm_server_close_read(peer, from_client, skip);
		break;
#ifdef COMPAT_GFARM_2_3
	case GFM_PROTO_CLOSE_WRITE:
		e = gfm_server_close_write(peer, from_client, skip);
		break;
#endif
	case GFM_PROTO_CLOSE_WRITE_V2_4:
		e = gfm_server_close_write_v2_4(peer, from_client, skip,
		    suspendedp);
		break;
	case GFM_PROTO_FHCLOSE_READ:
		e = gfm_server_fhclose_read(peer, from_client, skip);
		break;
	case GFM_PROTO_FHCLOSE_WRITE:
		e = gfm_server_fhclose_write(peer, from_client, skip,
		    suspendedp);
		break;
	case GFM_PROTO_FHCLOSE_WRITE_CKSUM:
		e = gfm_server_fhclose_write_cksum(peer, from_client, skip,
		    suspendedp);
		break;
	case GFM_PROTO_GENERATION_UPDATED:
		e = gfm_server_generation_updated(peer, from_client, skip);
		break;
	case GFM_PROTO_GENERATION_UPDATED_BY_COOKIE:
		e = gfm_server_generation_updated_by_cookie(peer,
		    from_client, skip);
		break;
	case GFM_PROTO_LOCK:
		e = gfm_server_lock(peer, from_client, skip);
		break;
	case GFM_PROTO_TRYLOCK:
		e = gfm_server_trylock(peer, from_client, skip);
		break;
	case GFM_PROTO_UNLOCK:
		e = gfm_server_unlock(peer, from_client, skip);
		break;
	case GFM_PROTO_LOCK_INFO:
		e = gfm_server_lock_info(peer, from_client, skip);
		break;
#ifdef COMPAT_GFARM_2_3
	case GFM_PROTO_SWITCH_BACK_CHANNEL:
		e = gfm_server_switch_back_channel(peer, from_client, skip);
		/* should not call gfp_xdr_flush() due to race */
		return (e);
#endif
	case GFM_PROTO_SWITCH_ASYNC_BACK_CHANNEL:
		e = gfm_server_switch_async_back_channel(peer,
		    from_client, skip);
		/* should not call gfp_xdr_flush() due to race */
		return (e);
	case GFM_PROTO_SWITCH_GFMD_CHANNEL:
		if (gfarm_get_metadb_replication_enabled())
			e = gfm_server_switch_gfmd_channel(peer,
			    from_client, skip);
		else
			e = GFARM_ERR_OPERATION_NOT_SUPPORTED;
		/* should not call gfp_xdr_flush() due to race */
		return (e);
	case GFM_PROTO_GLOB:
		e = gfm_server_glob(peer, from_client, skip);
		break;
	case GFM_PROTO_SCHEDULE:
		e = gfm_server_schedule(peer, from_client, skip);
		break;
	case GFM_PROTO_PIO_OPEN:
		e = gfm_server_pio_open(peer, from_client, skip);
		break;
	case GFM_PROTO_PIO_SET_PATHS:
		e = gfm_server_pio_set_paths(peer, from_client, skip);
		break;
	case GFM_PROTO_PIO_CLOSE:
		e = gfm_server_pio_close(peer, from_client, skip);
		break;
	case GFM_PROTO_PIO_VISIT:
		e = gfm_server_pio_visit(peer, from_client, skip);
		break;
	case GFM_PROTO_HOSTNAME_SET:
		e = gfm_server_hostname_set(peer, from_client, skip);
		break;
	case GFM_PROTO_SCHEDULE_HOST_DOMAIN:
		e = gfm_server_schedule_host_domain(peer, from_client, skip);
		break;
	case GFM_PROTO_STATFS:
		e = gfm_server_statfs(peer, from_client, skip);
		break;
	case GFM_PROTO_CONFIG_GET:
		e = gfm_server_config_get(peer, from_client, skip);
		break;
	case GFM_PROTO_CONFIG_SET:
		e = gfm_server_config_set(peer, from_client, skip);
		break;
	case GFM_PROTO_SCHEDULE_HOST_DOMAIN_USE_REAL_DISK_SPACE:
		e = gfm_server_schedule_host_domain_use_real_disk_space(peer,
		    from_client, skip);
		break;
	case GFM_PROTO_REPLICA_LIST_BY_NAME:
		e = gfm_server_replica_list_by_name(peer, from_client, skip);
		break;
	case GFM_PROTO_REPLICA_LIST_BY_HOST:
		e = gfm_server_replica_list_by_host(peer, from_client, skip);
		break;
	case GFM_PROTO_REPLICA_REMOVE_BY_HOST:
		e = gfm_server_replica_remove_by_host(peer, from_client, skip);
		break;
	case GFM_PROTO_REPLICA_REMOVE_BY_FILE:
		e = gfm_server_replica_remove_by_file(peer, from_client, skip);
		break;
	case GFM_PROTO_REPLICA_INFO_GET:
		e = gfm_server_replica_info_get(peer, from_client, skip);
		break;
	case GFM_PROTO_REPLICATE_FILE_FROM_TO:
		e = gfm_server_replicate_file_from_to(peer, from_client, skip);
		break;
	case GFM_PROTO_REPLICA_CHECK_CTRL:
		e = gfm_server_replica_check_ctrl(peer, from_client, skip);
		break;
	case GFM_PROTO_REPLICA_CHECK_STATUS:
		e = gfm_server_replica_check_status(peer, from_client, skip);
		break;
	case GFM_PROTO_REPLICA_ADDING:
		e = gfm_server_replica_adding(peer, from_client, skip,
		    suspendedp);
		break;
	case GFM_PROTO_REPLICA_ADDING_CKSUM:
		e = gfm_server_replica_adding_cksum(peer, from_client, skip,
		    suspendedp);
		break;
	case GFM_PROTO_REPLICA_ADDED: /* obsolete protocol */
		e = gfm_server_replica_added(peer, from_client, skip);
		break;
	case GFM_PROTO_REPLICA_ADDED2:
		e = gfm_server_replica_added2(peer, from_client, skip);
		break;
	case GFM_PROTO_REPLICA_ADDED_CKSUM:
		e = gfm_server_replica_added_cksum(peer, from_client, skip);
		break;
	case GFM_PROTO_REPLICA_LOST:
		e = gfm_server_replica_lost(peer, from_client, skip);
		break;
	case GFM_PROTO_REPLICA_ADD:
		e = gfm_server_replica_add(peer, from_client, skip);
		break;
	case GFM_PROTO_REPLICA_GET_MY_ENTRIES: /* obsolete protocol */
		e = gfm_server_replica_get_my_entries(peer, from_client, skip);
		break;
	case GFM_PROTO_REPLICA_GET_MY_ENTRIES2:
		e = gfm_server_replica_get_my_entries2(peer,
		    from_client, skip);
		break;
	case GFM_PROTO_REPLICA_GET_MY_ENTRIES_RANGE:
		e = gfm_server_replica_get_my_entries_range(peer,
		    from_client, skip);
		break;
	case GFM_PROTO_REPLICA_CREATE_FILE_IN_LOST_FOUND:
		e = gfm_server_replica_create_file_in_lost_found(peer,
		    from_client, skip);
		break;
	case GFM_PROTO_REPLICA_OPEN_STATUS:
		e = gfm_server_replica_open_status(peer, from_client, skip);
		break;
	case GFM_PROTO_REPLICA_GET_CKSUM:
		e = gfm_server_replica_get_cksum(peer, from_client, skip);
		break;
	case GFM_PROTO_FHSET_CKSUM:
		e = gfm_server_fhset_cksum(peer, from_client, skip);
		break;
	case GFM_PROTO_PROCESS_ALLOC:
		e = gfm_server_process_alloc(peer, from_client, skip);
		break;
	case GFM_PROTO_PROCESS_ALLOC_CHILD:
		e = gfm_server_process_alloc_child(peer, from_client, skip);
		break;
	case GFM_PROTO_PROCESS_FREE:
		e = gfm_server_process_free(peer, from_client, skip);
		break;
	case GFM_PROTO_PROCESS_SET:
		e = gfm_server_process_set(peer, from_client, skip);
		break;
	case GFM_PROTO_PROCESS_FD_INFO:
		e = gfm_server_process_fd_info(peer, from_client, skip);
		break;
	case GFJ_PROTO_LOCK_REGISTER:
		e = gfj_server_lock_register(peer, from_client, skip); break;
	case GFJ_PROTO_UNLOCK_REGISTER:
		e = gfj_server_unlock_register(peer, from_client, skip); break;
	case GFJ_PROTO_REGISTER:
		e = gfj_server_register(peer, from_client, skip);
		break;
	case GFJ_PROTO_UNREGISTER:
		e = gfj_server_unregister(peer, from_client, skip);
		break;
	case GFJ_PROTO_REGISTER_NODE:
		e = gfj_server_register_node(peer, from_client, skip); break;
	case GFJ_PROTO_LIST:
		e = gfj_server_list(peer, from_client, skip); break;
	case GFJ_PROTO_INFO:
		e = gfj_server_info(peer, from_client, skip); break;
	case GFJ_PROTO_HOSTINFO:
		e = gfj_server_hostinfo(peer, from_client, skip); break;
	case GFM_PROTO_XATTR_SET:
		e = gfm_server_setxattr(peer, from_client, skip, 0);
		break;
	case GFM_PROTO_XMLATTR_SET:
		e = gfm_server_setxattr(peer, from_client, skip, 1);
		break;
	case GFM_PROTO_XATTR_GET:
		e = gfm_server_getxattr(peer, from_client, skip, 0);
		break;
	case GFM_PROTO_XMLATTR_GET:
		e = gfm_server_getxattr(peer, from_client, skip, 1);
		break;
	case GFM_PROTO_XATTR_REMOVE:
		e = gfm_server_removexattr(peer, from_client, skip, 0);
		break;
	case GFM_PROTO_XMLATTR_REMOVE:
		e = gfm_server_removexattr(peer, from_client, skip, 1);
		break;
	case GFM_PROTO_XATTR_LIST:
		e = gfm_server_listxattr(peer, from_client, skip, 0);
		break;
	case GFM_PROTO_XMLATTR_LIST:
		e = gfm_server_listxattr(peer, from_client, skip, 1);
		break;
	case GFM_PROTO_XMLATTR_FIND:
		e = gfm_server_findxmlattr(peer, from_client, skip);
		break;
	case GFM_PROTO_QUOTA_USER_GET:
		e = gfm_server_quota_user_get(peer, from_client, skip);
		break;
	case GFM_PROTO_QUOTA_USER_SET:
		e = gfm_server_quota_user_set(peer, from_client, skip);
		break;
	case GFM_PROTO_QUOTA_GROUP_GET:
		e = gfm_server_quota_group_get(peer, from_client, skip);
		break;
	case GFM_PROTO_QUOTA_GROUP_SET:
		e = gfm_server_quota_group_set(peer, from_client, skip);
		break;
	case GFM_PROTO_QUOTA_CHECK:
		e = gfm_server_quota_check(peer, from_client, skip);
		break;
	case GFM_PROTO_DIRSET_INFO_SET:
		e = gfm_server_dirset_info_set(peer, from_client, skip);
		break;
	case GFM_PROTO_DIRSET_INFO_REMOVE:
		e = gfm_server_dirset_info_remove(peer, from_client, skip);
		break;
	case GFM_PROTO_DIRSET_INFO_LIST:
		e = gfm_server_dirset_info_list(peer, from_client, skip);
		break;
	case GFM_PROTO_QUOTA_DIRSET_GET:
		e = gfm_server_quota_dirset_get(peer, from_client, skip);
		break;
	case GFM_PROTO_QUOTA_DIRSET_SET:
		e = gfm_server_quota_dirset_set(peer, from_client, skip);
		break;
	case GFM_PROTO_QUOTA_DIR_GET:
		e = gfm_server_quota_dir_get(peer, from_client, skip);
		break;
	case GFM_PROTO_QUOTA_DIR_SET:
		e = gfm_server_quota_dir_set(peer, from_client, skip);
		break;
	case GFM_PROTO_DIRSET_DIR_LIST:
		e = gfm_server_dirset_dir_list(peer, from_client, skip);
		break;
	case GFM_PROTO_METADB_SERVER_GET:
		e = gfm_server_metadb_server_get(peer, from_client, skip);
		break;
	case GFM_PROTO_METADB_SERVER_GET_ALL:
		e = gfm_server_metadb_server_get_all(peer, from_client, skip);
		break;
	case GFM_PROTO_METADB_SERVER_SET:
		e = gfm_server_metadb_server_set(peer, from_client, skip);
		break;
	case GFM_PROTO_METADB_SERVER_MODIFY:
		e = gfm_server_metadb_server_modify(peer, from_client, skip);
		break;
	case GFM_PROTO_METADB_SERVER_REMOVE:
		e = gfm_server_metadb_server_remove(peer, from_client, skip);
		break;
	default:
		e = gfm_server_protocol_extension(peer,
		    from_client, skip, level, request, last_sync_request,
		    requestp, on_errorp);
		break;
	}

	if (!*suspendedp &&
	    ((level == 0 && request != GFM_PROTO_COMPOUND_BEGIN)
	    || request == GFM_PROTO_COMPOUND_END)) {
		/* flush only when a COMPOUND loop is done */
		if (debug_mode)
			gflog_debug(GFARM_MSG_1000182, "gfp_xdr_flush");
		e2 = gfp_xdr_flush(peer_get_conn(peer));
		if (e2 != GFARM_ERR_NO_ERROR) {
			gflog_warning(GFARM_MSG_1000183, "protocol flush: %s",
			    gfarm_error_string(e2));
			peer_record_protocol_error(peer);
		}
		if (e == GFARM_ERR_NO_ERROR)
			e = e2;
	}

	/* continue unless protocol error happens */
	return (e);
}

void
compound_state_init(struct compound_state *cs)
{
	cs->current_part = GFARM_ERR_NO_ERROR;
	cs->cause = GFARM_ERR_NO_ERROR;
	cs->skip = 0;
}

void
protocol_state_init(struct protocol_state *ps)
{
	ps->nesting_level = 0;
	ps->last_sync_request = -1;
	ps->last_async_request = -1;
}

/*
 * finish foreground protocol handling.
 *
 * PREREQUISITE: giant_lock
 */
void
protocol_finish(struct peer *peer, const char *diag)
{
	/* peer_free() internally calls db_begin()/db_end() */
	peer_free(peer);
}

/* this interface is exported for a use from a private extension */
int
protocol_service(struct peer *peer)
{
	struct protocol_state *ps = peer_get_protocol_state(peer);
	struct compound_state *cs = &ps->cs;
	gfarm_error_t e, dummy;
	gfarm_int32_t request;
	int from_client;
	int suspended = 0;
	static const char diag[] = "protocol_service";

	from_client = peer_get_auth_id_type(peer) == GFARM_AUTH_ID_TYPE_USER;
	if (ps->nesting_level == 0) { /* top level */
		e = protocol_switch(peer, from_client, 0, 0,
		    ps->last_sync_request,
		    &request, &dummy, &suspended);
		ps->last_sync_request = request;
		if (suspended)
			return (1); /* finish */
		giant_lock();
		peer_fdpair_clear(peer, diag);
		if (peer_had_protocol_error(peer)) {
			/*
			 * gfmd_channel and back_channel will be
			 * freed by their own thread
			 */
			if (e != GFARM_ERR_NO_ERROR ||
			    (request != GFM_PROTO_SWITCH_GFMD_CHANNEL &&
#ifdef COMPAT_GFARM_2_3
			     request != GFM_PROTO_SWITCH_BACK_CHANNEL &&
#endif
			     request != GFM_PROTO_SWITCH_ASYNC_BACK_CHANNEL))
				protocol_finish(peer, diag);
			giant_unlock();
			return (1); /* finish */
		}
		giant_unlock();
	} else { /* inside of a COMPOUND block */
		e = protocol_switch(peer, from_client, cs->skip, 1,
		    ps->last_sync_request,
		    &request, &cs->current_part, &suspended);
		ps->last_sync_request = request;
		if (suspended)
			return (1); /* finish */
		if (peer_had_protocol_error(peer)) {
			giant_lock();
			peer_fdpair_clear(peer, diag);
			/*
			 * gfmd_channel and back_channel will be
			 * freed by their own thread
			 */
			if (e != GFARM_ERR_NO_ERROR ||
			    (request != GFM_PROTO_SWITCH_GFMD_CHANNEL &&
#ifdef COMPAT_GFARM_2_3
			     request != GFM_PROTO_SWITCH_BACK_CHANNEL &&
#endif
			     request != GFM_PROTO_SWITCH_ASYNC_BACK_CHANNEL))
				protocol_finish(peer, diag);
			giant_unlock();
			return (1); /* finish */
		}
		if (e != GFARM_ERR_NO_ERROR) {
			/*
			 * set cs->cause, if it's first error at a main part
			 * of a COMPOUND block
			 */
			gflog_debug(GFARM_MSG_1001481,
				"protocol_switch() failed inside of a "
				"COMPOUND block: %s", gfarm_error_string(e));
			if (cs->cause == GFARM_ERR_NO_ERROR && !cs->skip)
				cs->cause = e;
			cs->skip = 1;
		} else if (request == GFM_PROTO_COMPOUND_END) {
			giant_lock();
			peer_fdpair_clear(peer, diag);
			giant_unlock();
			ps->nesting_level--;
		} else if (request == GFM_PROTO_COMPOUND_ON_ERROR) {
			cs->skip = cs->current_part != cs->cause;
		}
	}
	/* request is always set here, because of !peer_had_protocol_error() */
	if (request == GFM_PROTO_SWITCH_GFMD_CHANNEL ||
#ifdef COMPAT_GFARM_2_3
	    request == GFM_PROTO_SWITCH_BACK_CHANNEL ||
#endif
	    request == GFM_PROTO_SWITCH_ASYNC_BACK_CHANNEL) {
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001482,
				"failed to process GFM_PROTO_SWITCH_BACK_"
				"CHANNEL request: %s", gfarm_error_string(e));
			giant_lock();
			protocol_finish(peer, diag);
			giant_unlock();
		}
		return (1); /* finish */
	}
	if (e == GFARM_ERR_NO_ERROR && request == GFM_PROTO_COMPOUND_BEGIN) {
		ps->nesting_level++;
		compound_state_init(&ps->cs);
	}

	return (0); /* still doing */
}

/* this interface is exported for a use from a private extension */
void *
protocol_main(void *arg)
{
	struct peer *peer = arg;

	/*
	 * the reason we call peer_invoked() here is just for consistency,
	 * because currently this is unnecessary for a foreground channel.
	 */
	peer_invoked(peer);

	do {
		if (protocol_service(peer))
			return (NULL); /* end of gfmd protocol session */
	} while (gfp_xdr_recv_is_ready(peer_get_conn(peer)));

	/*
	 * NOTE:
	 * We should use do...while loop for the above gfp_xdr_recv_is_ready()
	 * case, instead of thrpool_add_job() like peer_authorized().
	 * Because this thread is executed under thrpool_worker(),
	 * and such thread should not use thrpool_add_job().
	 * Think about the following scenario:
	 * (1) massive number of new connections filled thrpool.jobq.
	 * (2) at the same time, all threads under thrpool_worker()
	 *   were protocol_main().
	 * (3) The gfp_xdr_recv_is_ready() condition became true with
	 *   all the protocol_main() threads.
	 * With this scenario, if protocol_main() tried thrpool_add_job(), then
	 * they would wait forever, because thrpool.jobq were already filled,
	 * and there was no chance that the jobq became available.
	 */

	peer_watch_access(peer);

	/* this return value won't be used, because this thread is detached */
	return (NULL);
}

struct resuming_queue {
	pthread_mutex_t mutex;
	pthread_cond_t nonempty;
	struct event_waiter *queue;
} resuming_pendings = {
	PTHREAD_MUTEX_INITIALIZER,
	PTHREAD_COND_INITIALIZER,
	NULL
};

void
resuming_enqueue(struct event_waiter *entry)
{
	struct resuming_queue *q = &resuming_pendings;
	static const char diag[] = "resuming_enqueue";

	gfarm_mutex_lock(&q->mutex, diag, "mutex");
	entry->next = q->queue;
	q->queue = entry;
	gfarm_cond_signal(&q->nonempty, diag, "nonempty");
	gfarm_mutex_unlock(&q->mutex, diag, "mutex");
}

struct event_waiter *
resuming_dequeue(struct resuming_queue *q, const char *diag)
{
	struct event_waiter *entry;

	gfarm_mutex_lock(&q->mutex, diag, "mutex");
	while  (q->queue == NULL)
		gfarm_cond_wait(&q->nonempty, &q->mutex, diag, "nonempty");
	entry = q->queue;
	q->queue = entry->next;
	gfarm_mutex_unlock(&q->mutex, diag, "mutex");
	return (entry);
}

void *
resuming_thread(void *arg)
{
	gfarm_error_t e;
	struct event_waiter *entry = arg;
	struct peer *peer = entry->peer;
	struct protocol_state *ps = peer_get_protocol_state(peer);
	struct compound_state *cs = &ps->cs;
	int suspended = 0;
	static const char diag[] = "resuming_thread";

	e = (*entry->action)(peer, entry->arg, &suspended);
	free(entry);
	if (suspended)
		return (NULL);

	if (ps->nesting_level > 0 && e != GFARM_ERR_NO_ERROR) {
		/*
		 * set cs->cause, if it's first error at a main part
		 * of a COMPOUND block
		 */
		gflog_debug(GFARM_MSG_1003700,
		    "resumed action failed in a COMPOUND block: %s",
		    gfarm_error_string(e));
		if (cs->cause == GFARM_ERR_NO_ERROR && !cs->skip)
			cs->cause = e;
		cs->skip = 1;
	}

	if (gfp_xdr_recv_is_ready(peer_get_conn(peer))) { /* inside COMPOUND */
		protocol_main(peer);
	} else { /* maybe inside COMPOUND, maybe not */
		e = gfp_xdr_flush(peer_get_conn(peer));
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_warning(GFARM_MSG_1004004, "protocol flush: %s",
			    gfarm_error_string(e));
			peer_record_protocol_error(peer);

			giant_lock();
			peer_fdpair_clear(peer, diag);
			protocol_finish(peer, diag);
			giant_unlock();
			return (NULL);
		}

		peer_watch_access(peer);
	}

	/* this return value won't be used, because this thread is detached */
	return (NULL);
}

void *
resumer(void *arg)
{
	struct event_waiter *entry;
	static const char diag[] = "resumer";

	for (;;) {
		entry = resuming_dequeue(&resuming_pendings, diag);

		thrpool_add_job(sync_protocol_get_thrpool(),
		    resuming_thread, entry);
	}

	/*NOTREACHED*/
	return (NULL);
}

/* only called in case of gfarm_auth_id_type == GFARM_AUTH_ID_TYPE_USER */
gfarm_error_t
auth_uid_to_global_username(void *closure,
	enum gfarm_auth_method auth_method,
	const char *auth_user_id,
	char **global_usernamep)
{
	char *global_username;
	struct user *u;
	static const char diag[] = "auth_uid_to_global_username";

	giant_lock();
	if (GFARM_IS_AUTH_GSI(auth_method)) { /* auth_user_id is a DN */
		u = user_lookup_gsi_dn(auth_user_id);
	} else { /* auth_user_id is a gfarm global user name */
		u = user_lookup(auth_user_id);
	}
	giant_unlock();

	if (u == NULL) {
		/*
		 * do not return GFARM_ERR_NO_SUCH_USER
		 * to prevent information leak
		 */
		gflog_debug(GFARM_MSG_1001483,
			"lookup for user failed");
		return (GFARM_ERR_AUTHENTICATION);
	}
	if (global_usernamep == NULL)
		return (GFARM_ERR_NO_ERROR);
	global_username = strdup_log(user_name(u), diag);
	if (global_username == NULL)
		return (GFARM_ERR_NO_MEMORY);
	*global_usernamep = global_username;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
peer_authorize(struct peer *peer)
{
	gfarm_error_t e;
	int rv, saved_errno;
	enum gfarm_auth_id_type id_type;
	char *username = NULL, *hostname;
	enum gfarm_auth_method auth_method;
	struct sockaddr addr;
	socklen_t addrlen = sizeof(addr);
	char addr_string[GFARM_SOCKADDR_STRLEN];
	static const char diag[] = "peer_authorize";

	/* without TCP_NODELAY, gfmd is too slow at least on NetBSD-3.0 */
	e = gfarm_sockopt_set_option(
	    gfp_xdr_fd(peer_get_conn(peer)), "tcp_nodelay");
	if (e == GFARM_ERR_NO_ERROR)
		gflog_debug(GFARM_MSG_1003395, "tcp_nodelay option is "
		    "specified for performance reason");
	else
		gflog_debug(GFARM_MSG_1003396, "tcp_nodelay option is "
		    "specified, but fails: %s", gfarm_error_string(e));

	rv = getpeername(gfp_xdr_fd(peer_get_conn(peer)), &addr, &addrlen);
	if (rv == -1) {
		saved_errno = errno;
		if (saved_errno == ENOTCONN) /* known race condition */
			gflog_notice(GFARM_MSG_1005070,
			    "authorize: getpeername: %s", strerror(errno));
		else
			gflog_error(GFARM_MSG_1000184,
			    "authorize: getpeername: %s", strerror(errno));
		return (gfarm_errno_to_error(saved_errno));
	}
	e = gfarm_sockaddr_to_name(&addr, &hostname);
	if (e != GFARM_ERR_NO_ERROR) {
		gfarm_sockaddr_to_string(&addr,
		    addr_string, GFARM_SOCKADDR_STRLEN);
		gflog_info(GFARM_MSG_1000185,
		    "gfarm_sockaddr_to_name(%s): %s",
		    addr_string, gfarm_error_string(e));
		hostname = strdup_log(addr_string, diag);
		if (hostname == NULL)
			return (GFARM_ERR_NO_MEMORY);
	}
	e = gfarm_authorize(peer_get_conn(peer), 0, GFM_SERVICE_TAG,
	    hostname, &addr, auth_uid_to_global_username, NULL,
	    &id_type, &username, &auth_method);
	if (e == GFARM_ERR_NO_ERROR) {
		protocol_state_init(peer_get_protocol_state(peer));

		giant_lock();
		peer_authorized(peer,
		    id_type, username, hostname, &addr, auth_method,
		    sync_protocol_watcher);
		giant_unlock();
	} else {
		gflog_notice(GFARM_MSG_1002474,
		    "host %s: authorize: %s", hostname, gfarm_error_string(e));
		free(hostname);
	}
	return (e);
}

void *
try_auth(void *arg)
{
	struct peer *peer = arg;
	gfarm_error_t e;

	if ((e = peer_authorize(peer)) != GFARM_ERR_NO_ERROR) {
		/* peer_authorize() itself records the error log */
		giant_lock();
		/* db_begin()/db_end() is not necessary in this case anyway */
		peer_free(peer);
		giant_unlock();
	}

	/* this return value won't be used, because this thread is detached */
	return (NULL);
}

#define SAME_WARNING_TRIGGER	10	/* check reduced mode */
#define SAME_WARNING_THRESHOLD	30	/* more than this -> reduced mode */
#define SAME_WARNING_DURATION	600	/* seconds to measure the limit */
#define	SAME_WARNING_INTERVAL	60	/* seconds: interval of reduced log */

struct gflog_reduced_state enfile_state = GFLOG_REDUCED_STATE_INITIALIZER(
	SAME_WARNING_TRIGGER,
	SAME_WARNING_THRESHOLD,
	SAME_WARNING_DURATION,
	SAME_WARNING_INTERVAL);
struct gflog_reduced_state emfile_state = GFLOG_REDUCED_STATE_INITIALIZER(
	SAME_WARNING_TRIGGER,
	SAME_WARNING_THRESHOLD,
	SAME_WARNING_DURATION,
	SAME_WARNING_INTERVAL);

void
accepting_loop(int accepting_socket)
{
	gfarm_error_t e;
	int client_socket;
	struct sockaddr_in client_addr;
	socklen_t client_addr_size;
	struct peer *peer;

	for (;;) {
		client_addr_size = sizeof(client_addr);
		client_socket = accept(accepting_socket,
		   (struct sockaddr *)&client_addr, &client_addr_size);
		if (client_socket < 0) {
			if (errno == EMFILE) {
				gflog_reduced_warning(GFARM_MSG_1003607,
				    &emfile_state,
				    "accept: %s", strerror(EMFILE));
			} else if (errno == ENFILE) {
				gflog_reduced_warning(GFARM_MSG_1003608,
				    &enfile_state,
				    "accept: %s", strerror(ENFILE));
			} else if (errno != EINTR) {
				gflog_warning_errno(GFARM_MSG_1000189,
				    "accept");
			}
		} else if ((e = peer_alloc(client_socket, &peer)) !=
		    GFARM_ERR_NO_ERROR) {
			gflog_warning(GFARM_MSG_1000190,
			    "peer_alloc: %s", gfarm_error_string(e));
			close(client_socket);
		} else {
			thrpool_add_job(authentication_thread_pool,
			    try_auth, peer);
		}
	}
}

static int
open_accepting_socket(int port)
{
	gfarm_error_t e;
	struct sockaddr_in self_addr;
	socklen_t self_addr_size;
	int sock, sockopt;

	memset(&self_addr, 0, sizeof(self_addr));
	self_addr.sin_family = AF_INET;
	self_addr.sin_addr.s_addr = INADDR_ANY;
	self_addr.sin_port = htons(port);
	self_addr_size = sizeof(self_addr);
	sock = socket(PF_INET, SOCK_STREAM, 0);
	if (sock < 0)
		gflog_fatal_errno(GFARM_MSG_1000191, "accepting socket");
	sockopt = 1;
	if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR,
	    &sockopt, sizeof(sockopt)) == -1)
		gflog_warning_errno(GFARM_MSG_1000192, "SO_REUSEADDR");
	if (bind(sock, (struct sockaddr *)&self_addr, self_addr_size) < 0)
		gflog_fatal_errno(GFARM_MSG_1000193, "bind accepting socket");
	e = gfarm_sockopt_apply_listener(sock);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_warning(GFARM_MSG_1000194,
		    "setsockopt: %s", gfarm_error_string(e));
	if (listen(sock, gfarm_metadb_server_listen_backlog) < 0)
		gflog_fatal_errno(GFARM_MSG_1000195, "listen");
	return (sock);
}

static void
write_pid(void)
{
	FILE *pid_fp;

	if (pid_file == NULL)
		return;

	pid_fp = fopen(pid_file, "w");
	if (pid_fp == NULL)
		gflog_fatal_errno(GFARM_MSG_1000196, "open: %s", pid_file);

	if (fprintf(pid_fp, "%ld\n", (long)getpid()) == -1)
		gflog_error_errno(GFARM_MSG_1002351,
		    "writing PID to %s", pid_file);
	if (fclose(pid_fp) != 0)
		gflog_error_errno(GFARM_MSG_1002352, "fclose(%s)", pid_file);
}

static void
start_db_journal_threads(void)
{
	gfarm_error_t e;

	if (mdhost_self_is_master()) {
		if ((e = create_detached_thread(db_journal_store_thread, NULL))
		    != GFARM_ERR_NO_ERROR)
			gflog_fatal(GFARM_MSG_1002723,
			    "create_detached_thread(db_journal_store_thread): "
			    "%s", gfarm_error_string(e));
	} else {
		if ((e = create_detached_thread(db_journal_recvq_thread, NULL))
		    != GFARM_ERR_NO_ERROR)
			gflog_fatal(GFARM_MSG_1002724,
			    "create_detached_thread(db_ournal_recvq_thread): "
			    "%s", gfarm_error_string(e));
		if ((e = create_detached_thread(db_journal_apply_thread, NULL))
		    != GFARM_ERR_NO_ERROR)
			gflog_fatal(GFARM_MSG_1002725,
			    "create_detached_thread(db_journal_apply_thread): "
			    "%s", gfarm_error_string(e));
	}
}

/*
 * Flush all backlog records written in a journal file to database.
 * When a master or slave gfmd starts, this function must be called once
 * before loading data (except for seqnum) from the database.
 */
static void
boot_apply_db_journal(void)
{
	gfarm_error_t e;
	static int boot_apply = 1;

	gflog_info(GFARM_MSG_1003273, "start applying db journal");
	if ((e = create_detached_thread(db_journal_store_thread,
	    &boot_apply)) != GFARM_ERR_NO_ERROR)
		gflog_fatal(GFARM_MSG_1003274,
		    "create_detached_thread(db_journal_store_thread): %s",
		    gfarm_error_string(e));

	db_journal_wait_for_apply_thread();
	gflog_info(GFARM_MSG_1003275, "end applying db journal");

	/*
	 * Reload seqnum from the database.
	 */
	db_journal_init_seqnum();
}

static void
start_gfmdc_threads(void)
{
	gfarm_error_t e;

	if (mdhost_self_is_master()) {
		if ((e = create_detached_thread(gfmdc_journal_asyncsend_thread,
		    NULL)) != GFARM_ERR_NO_ERROR)
			gflog_fatal(GFARM_MSG_1002726,
			    "create_detached_thread(gfmdc_master_thread): %s",
			    gfarm_error_string(e));
	} else if ((e = create_detached_thread(gfmdc_connect_thread, NULL))
	    != GFARM_ERR_NO_ERROR)
		gflog_fatal(GFARM_MSG_1002727,
		    "create_detached_thread(gfmdc_slave_thread): %s",
		    gfarm_error_string(e));
}

static void dynamic_config_read(FILE *, const char *);

static void
dynamic_config_include_file(char *rest_of_line, const char *file, int lineno)
{
	gfarm_error_t e;
	char *tmp, *s, *p, *malloced_filename = NULL;
	FILE *config;
	int nesting_limit = gfarm_ctxp->include_nesting_limit;

	p = rest_of_line;
	e = gfarm_config_strtoken(&p, &s);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_1005216,
		    "file %s, line %d: parsing token (%s) failed: %s",
		    file, lineno, p, gfarm_error_string(e));
		return;
	}
	if (s == NULL) {
		gflog_error(GFARM_MSG_1005217,
		    "file %s, line %d: missing include filename",
		    file, lineno);
		return;
	}
	e = gfarm_config_strtoken(&p, &tmp);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_1005218,
		    "file %s, line %d: parsing token (%s) failed: %s",
		    file, lineno, p, gfarm_error_string(e));
		return;
	}
	if (tmp != NULL) {
		gflog_error(GFARM_MSG_1005219,
		    "file %s, line %d: too many arguments for include",
		    file, lineno);
		return;
	}

	++gfarm_ctxp->include_nesting_level;
	if (gfarm_ctxp->include_nesting_level > nesting_limit) {
		gflog_error(GFARM_MSG_1005220,
		    "file %s, line %d: include nesting level %d "
		    "exceeds include_nesting_limit %d",
		    file, lineno,
		    gfarm_ctxp->include_nesting_level, nesting_limit);
		--gfarm_ctxp->include_nesting_level;
		return;
	}

	if (s[0] != '/')  {
		malloced_filename = gfarm_config_dirname_add(s, file);
		if (malloced_filename == NULL) {
			gflog_error(GFARM_MSG_1005221,
			    "file %s, line %d: no memory to include %s",
			    file, lineno, s);
		}
		s = malloced_filename;
	}
	config = fopen(s, "r");
	if (config == NULL) {
		gflog_notice(GFARM_MSG_1005222,
		    "file %s, line %d: %s: cannot open include file",
		    file, lineno, s);
		free(malloced_filename);
		--gfarm_ctxp->include_nesting_level;
		return;
	}
	dynamic_config_read(config, s);
	fclose(config);
	free(malloced_filename);
	--gfarm_ctxp->include_nesting_level;
}

/* this constant can be different from same macro in config.c */
#define MAX_CONFIG_LINE_LENGTH	1023

static void
dynamic_config_read(FILE *config, const char *file)
{
	gfarm_error_t e;
	int lineno = 0;
	char *s, *p, buffer[MAX_CONFIG_LINE_LENGTH + 1];

	while (fgets(buffer, sizeof buffer, config) != NULL) {
		lineno++;
		p = buffer;

		e = gfarm_config_strtoken(&p, &s);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_error(GFARM_MSG_1005223,
			    "%s, line %d: invalid token: %s",
			    file, lineno, gfarm_error_string(e));
			/* don't stop, just ignore this line line */
			continue;
		}

		if (s == NULL) /* blank or comment line */
			continue;

		if (strcmp(s, "include") == 0) {
			dynamic_config_include_file(p, file, lineno);
		} else {
			/* error message will be logged by callee */
			e = gfarm_config_apply_to_metadb(
			    s, p, file, lineno, /* log changes */ 1);
			(void)e;
		}
	}
}

static void
dynamic_config_read_file(const char *file)
{
	FILE *config = fopen(file, "r");

	if (config == NULL) {
		gflog_notice(GFARM_MSG_1005224,
		    "%s: cannot open config file", file);
		return;
	}
	gfarm_config_apply_begin();
	dynamic_config_read(config, file);
	gfarm_config_apply_end();
	fclose(config);
}

static pthread_mutex_t transform_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t transform_cond = PTHREAD_COND_INITIALIZER;
static char *failover_config_file = NULL;

static void
transform_to_master(void)
{
	struct mdhost *master;
	static const char diag[] = "transform_to_master";

	if (mdhost_self_is_master()) {
		gflog_error(GFARM_MSG_1002728,
		    "cannot transform to the master gfmd "
		    "because this is already the master gfmd");
		return;
	}
	if (!mdhost_self_is_master_candidate()) {
		gflog_error(GFARM_MSG_1002729,
		    "cannot transform to the master gfmd "
		    "because this is not a master candidate.");
		return;
	}
	if (gfmdc_is_master_gfmd_running()) {
		gflog_error(GFARM_MSG_1003839,
		    "cannot transform to the master gfmd "
		    "because another master gfmd is already running");
		return;
	}

	master = mdhost_lookup_master();
	if (master == NULL)
		gflog_fatal(GFARM_MSG_1004740, "no master, abort");
	if (mdhost_is_up(master))
		mdhost_disconnect_request(master, NULL);
	gflog_info(GFARM_MSG_1002730,
	    "start transforming to the master gfmd ...");

	db_journal_cancel_recvq();

	/*
	 * wait for data transfer from the journal to the backend DB.
	 * this must be done before dead_file_copy_init_load().
	 */
	db_journal_wait_for_apply_thread();

	giant_lock();

	mdhost_set_self_as_master();

	/* this must be after db_journal_wait_for_apply_thread() */
	dead_file_copy_init_load();

	giant_unlock();

	gfarm_cond_signal(&transform_cond, diag, TRANSFORM_COND_DIAG);

	start_db_journal_threads();
	start_gfmdc_threads();

	giant_lock();
	dynamic_config_read_file(failover_config_file);
	mdhost_set_self_as_default_master();
	giant_unlock();

	gflog_info(GFARM_MSG_1002731,
	    "end transforming to the master gfmd");
}

static int
wait_transform_to_master(int port)
{
	static const char diag[] = "accepting_loop";

	/* Wait until this process transforms to the master.
	 * This behavior will be deleted when the feature are
	 * implemented that requests to a slave gfmd are forwarded
	 * to a master gfmd.
	 */
	gfarm_mutex_lock(&transform_mutex, diag, TRANSFORM_MUTEX_DIAG);
	while (!mdhost_self_is_master())
		gfarm_cond_wait(&transform_cond, &transform_mutex, diag,
		    TRANSFORM_COND_DIAG);
	gfarm_mutex_unlock(&transform_mutex, diag, TRANSFORM_MUTEX_DIAG);
	return (open_accepting_socket(port));
}

static struct {
	pthread_mutex_t mutex;
	pthread_cond_t become_ready;
	int ready;
} gfmd_startup_state = {
	PTHREAD_MUTEX_INITIALIZER,
	PTHREAD_COND_INITIALIZER,
	0
};

static int
gfmd_startup_state_is_ready(void)
{
	int ready;
	static const char diag[] = "gfmd_startup_state_is_ready";

	gfarm_mutex_lock(&gfmd_startup_state.mutex, diag, "mutex");
	ready = gfmd_startup_state.ready;
	gfarm_mutex_unlock(&gfmd_startup_state.mutex, diag, "mutex");
	return (ready);
}

static void
gfmd_startup_state_wait_ready(void)
{
	static const char diag[] = "gfmd_startup_state_wait_ready";

	gfarm_mutex_lock(&gfmd_startup_state.mutex, diag, "mutex");
	while (!gfmd_startup_state.ready)
		gfarm_cond_wait(&gfmd_startup_state.become_ready,
		    &gfmd_startup_state.mutex, diag, "become_ready");
	gfarm_mutex_unlock(&gfmd_startup_state.mutex, diag, "mutex");
}

static void
gfmd_startup_state_notify_ready(void)
{
	static const char diag[] = "gfmd_startup_state_notify_ready";

	gfarm_mutex_lock(&gfmd_startup_state.mutex, diag, "mutex");
	gfmd_startup_state.ready = 1;
	gfarm_cond_signal(&gfmd_startup_state.become_ready,
	    diag, "become_ready");
	gfarm_mutex_unlock(&gfmd_startup_state.mutex, diag, "mutex");
}

static void
dummy_sighandler(int signo)
{
	/* never called */
}

static void
sig_add(sigset_t *sigs, int signo, const char *name)
{
	struct sigaction old, new;

	if (sigaction(signo, NULL, &old) == -1)
		gflog_fatal_errno(GFARM_MSG_1002732,
		    "checking %s signal", name);
	if (old.sa_handler == SIG_IGN
#ifdef SIGINFO /* SIG_DFL means "discard" */
	    || (signo == SIGINFO && old.sa_handler == SIG_DFL)
#endif
#ifdef SIGPWR /* SIG_DFL means "discard". We don't do sig_add(,SIGPWR) though */
	    || (signo == SIGPWR && old.sa_handler == SIG_DFL)
#endif
	    ) {
		/*
		 * without this, the signal won't be delivered
		 * to the sigs_handler thread on Solaris and *BSD.
		 */
		new = old;
		new.sa_handler = dummy_sighandler;
		if (sigaction(signo, &new, NULL) == -1)
			gflog_fatal_errno(GFARM_MSG_1002733,
			    "installing %s signal dummy handler", name);
	}
	if (sigaddset(sigs, signo) == -1)
		gflog_fatal_errno(GFARM_MSG_1002734, "sigaddset(%s)", name);
}

static void
sigs_set(sigset_t *sigs)
{
	if (sigemptyset(sigs) == -1)
		gflog_fatal_errno(GFARM_MSG_1002353, "sigemptyset()");
	sig_add(sigs, SIGHUP, "SIGHUP");
	 /* don't do this in debug_mode, to use Control-C under gdb */
	if (!debug_mode)
		sig_add(sigs, SIGINT, "SIGINT");
	sig_add(sigs, SIGTERM, "SIGTERM");
#ifdef SIGINFO
	sig_add(sigs, SIGINFO, "SIGINFO");
#endif
	sig_add(sigs, SIGUSR1, "SIGUSR1");
	sig_add(sigs, SIGUSR2, "SIGUSR2");
}

void *
sigs_handler(void *p)
{
	sigset_t *sigs = p;
	int rv, sig;
	static const char diag[] = "sigs_handler";

#ifdef __linux__
	/* A Linux Thread is a process having its own process id. */
	write_pid();
#endif
	for (;;) {
		if ((rv = sigwait(sigs, &sig)) != 0) {
			gflog_warning(GFARM_MSG_1000197,
			    "sigs_handler: %s", strerror(rv));
			continue;
		}
		switch (sig) {
		case SIGHUP:
			/* nothing to do */
			continue;

		case SIGUSR1:
			if (gfarm_get_metadb_replication_enabled()) {
				if (!gfmd_startup_state_is_ready()) {
					gflog_info(GFARM_MSG_1003454,
					    "got SIGUSR1, but waiting for "
					    "completion of initialization");
					gfmd_startup_state_wait_ready();
				}
				transform_to_master();
			}
			continue;
#ifdef SIGINFO
		case SIGINFO:
			/*FALLTHRU*/
#endif
		case SIGUSR2:
			thrpool_info();
			replica_check_info();
			replication_info();
			continue;

		/* some of these will be never delivered due to `*sigs' */
		case SIGINT:
		case SIGQUIT:
		case SIGILL:
		case SIGTRAP:
		case SIGABRT:
		case SIGFPE:
		case SIGBUS:
		case SIGSEGV:
#ifdef SIGSYS
		case SIGSYS:
#endif
		case SIGTERM:
		case SIGXCPU:
		case SIGXFSZ:
#ifdef SIGPWR
#if !defined(SIGINFO) || (SIGPWR != SIGINFO) /* see SF.net #961 */
		case SIGPWR:
#endif
#endif
			/* terminate gfmd */
			break;
		default:
			gflog_info(GFARM_MSG_1000198,
			    "spurious signal %d received: ignoring...", sig);
			continue;
		}
		break;
	}

	gflog_info(GFARM_MSG_1000199,
	    "signal %d received: terminating...", sig);

	gfmd_terminate(diag);

	/*NOTREACHED*/
	return (0); /* to shut up warning */
}

void
gfmd_terminate(const char *diag)
{
	/* we never release the giant lock until exit */
	/* so, it's safe to modify the state of all peers */
	giant_lock();

	gflog_info(GFARM_MSG_1000201, "shutting down peers");
	/*
	 * the following internally calls inode_close*() and
	 * closing must be done regardless of the result of db_begin().
	 * because not closing may cause descriptor leak.
	 *
	 * NOTE:
	 * We do not call db_begin(diag)/db_end() here to avoid SF.net #736.
	 *
	 * Because peer_shutdown_all() only detaches the peers' processes,
	 * journal transfer to slaves is possible even after this call.
	 */
	peer_shutdown_all();

	if (gfarm_get_metadb_replication_enabled() &&
	    gfmd_startup_state_is_ready()) {
		if (!mdhost_self_is_master())
			db_journal_cancel_recvq();
		gfmdc_journal_transfer_wait();
	}

	/* save all pending transactions */
	/* db_terminate() needs giant_lock(), see comment in dbq_enter() */
	db_terminate();

	if (iostat_dirbuf) {
		/*
		 * We don't have to call gfarm_privilege_lock() here,
		 * because new peer won't arrive after peer_shutdown_all(),
		 * so new setuid() call won't happen.
		 */
		unlink(iostat_dirbuf);
		free(iostat_dirbuf);
		iostat_dirbuf = NULL;
	}

	gflog_info(GFARM_MSG_1000202, "bye");
	exit(0);
}

void
usage(void)
{
	fprintf(stderr, "Usage: %s [option]\n", program_name);
	fprintf(stderr, "option:\n");
	fprintf(stderr, "\t-F <log-file>\n");
	fprintf(stderr, "\t-L <syslog-priority-level>\n");
	fprintf(stderr, "\t-P <pid-file>\n");
	fprintf(stderr, "\t-d\t\t\t\t... debug mode\n");
	fprintf(stderr, "\t-f <gfarm-configuration-file>\n");
	fprintf(stderr, "\t-p <port>\n");
	fprintf(stderr, "\t-s <syslog-facility>\n");
	fprintf(stderr, "\t-t\t\t\t\t... output file-trace log message\n");
	fprintf(stderr, "\t-v\t\t\t\t... make authentication log verbose\n");
	exit(1);
}

static void
set_db_ops(void)
{
	switch (gfarm_backend_db_type) {
	case GFARM_BACKEND_DB_TYPE_LDAP:
#ifdef HAVE_LDAP
		db_use(&db_ldap_ops);
#else
		gflog_fatal(GFARM_MSG_1000206,
		    "LDAP DB is specified, but it's not built in");
#endif
		break;
	case GFARM_BACKEND_DB_TYPE_POSTGRESQL:
#ifdef HAVE_POSTGRESQL
		db_use(&db_pgsql_ops);
#else
		gflog_fatal(GFARM_MSG_1000207,
		    "PostgreSQL is specified, but it's not built in");
#endif
		break;
	case GFARM_BACKEND_DB_TYPE_NONE:
		db_use(&db_none_ops);
		break;
	default:
		gflog_fatal(GFARM_MSG_1000208,
		    "neither LDAP or PostgreSQL is specified "
		    "in configuration");
		break;
	}
}

static void
db_journal_store_failure(void)
{
	gflog_fatal(GFARM_MSG_1003397,
	    "gfmd is shutting down for unrecoverable error");
}

/* this interface is exported for a use from a private extension */
void
gfmd_modules_init_default(int table_size)
{
	gfarm_error_t e;

	peer_watcher_set_default_nfd(table_size);
	sync_protocol_watcher = peer_watcher_alloc(
	    gfarm_metadb_thread_pool_size, gfarm_metadb_job_queue_length,
	    protocol_main, "synchronous protocol handler");

	authentication_thread_pool = thrpool_new(gfarm_metadb_thread_pool_size,
	    gfarm_metadb_job_queue_length, "authentication threads");
	if (authentication_thread_pool == NULL)
		gflog_fatal(GFARM_MSG_1001485,
		    "thread pool size:%d, queue length:%d: no memory",
		    gfarm_metadb_thread_pool_size,
		    gfarm_metadb_job_queue_length);

	callout_module_init(CALLOUT_NTHREADS);

	if (gfarm_get_metadb_replication_enabled()) {
		char *diag = "db_journal_init";

		db_journal_set_fail_store_op(db_journal_store_failure);
		db_journal_apply_init();
		e = db_journal_init(mdhost_master_disconnect_request);
		if (gfarm_backend_db_type != GFARM_BACKEND_DB_TYPE_NONE) {
			if (e != GFARM_ERR_NO_ERROR)
				gflog_fatal(GFARM_MSG_1004754, "%s: %s",
					diag, gfarm_error_string(e));
			boot_apply_db_journal();
		} else if (e != GFARM_ERR_NO_ERROR) {
			gflog_error(GFARM_MSG_1004755, "%s: %s, replication "
				"disabled", diag, gfarm_error_string(e));
			gfarm_set_metadb_replication_enabled(0);
			set_db_ops();
		}
	}
	gflog_info(GFARM_MSG_1004202, "start initializing modules and "
	    "loading database");
	mdhost_init();
	back_channel_init();
	if (gfarm_get_metadb_replication_enabled())
		gfmdc_init();
	/* directory service */
	host_init();
	user_init();
	group_init();
	quota_init();

	/* filesystem */
	inode_init();
	dir_entry_init();
	file_copy_init();
	symlink_init();
	if (gfarm_backend_db_type == GFARM_BACKEND_DB_TYPE_NONE)
		xattr_init_cache_all();
	xattr_init();
	dirset_init();
	quota_dir_init();

	peer_init(table_size);
	job_table_init(table_size);

	if (gfarm_get_metadb_replication_enabled() &&
	    gfarm_backend_db_type == GFARM_BACKEND_DB_TYPE_NONE) {
		/* update in-memory metadata by journal */
		if ((e = create_detached_thread(db_journal_apply_thread, NULL))
		    != GFARM_ERR_NO_ERROR)
			gflog_fatal(GFARM_MSG_1004741,
			    "create_detached_thread(db_journal_apply_thread): "
			    "%s", gfarm_error_string(e));
		db_journal_wait_for_apply_thread();
		/* this updates seqnum */
		boot_apply_db_journal();
	}
	/* initial entry */
	mdhost_initial_entry();
	user_initial_entry();
	group_initial_entry();
	inode_initial_entry();
	dir_entry_initial_entry();

	/* must be after hosts, filesystem and mdhost_initial_entry */
	dead_file_copy_init(mdhost_self_is_master());
}

/* this interface is made as a hook for a private extension */
void (*gfmd_modules_init)(int); /* intentionally remains uninitialized */

static struct gfarm_iostat_spec iostat_spec[] =  {
	{ "ntran", GFARM_IOSTAT_TYPE_TOTAL }
};

int
main(int argc, char **argv)
{
	gfarm_error_t e;
	char *config_file = NULL, *port_number = NULL;
	int syslog_level = -1;
	int syslog_facility = GFARM_DEFAULT_FACILITY;
	char *syslog_file = NULL;
	int ch, sock, table_size;
	sigset_t sigs;
	int is_master, replication_enabled;
	int file_trace = 0;

	if (argc >= 1)
		program_name = basename(argv[0]);
	gflog_set_identifier(program_name);

	while ((ch = getopt(argc, argv, "F:L:P:df:p:Ss:tu:v")) != -1) {
		switch (ch) {
		case 'F':
			syslog_file = optarg;
			break;
		case 'L':
			syslog_level = gflog_syslog_name_to_priority(optarg);
			if (syslog_level == -1)
				gflog_fatal(GFARM_MSG_1000203,
				    "-L %s: invalid syslog priority",
				    optarg);
			break;
		case 'P':
			pid_file = optarg;
			break;
		case 'd':
			debug_mode = 1;
			if (syslog_level == -1)
				syslog_level = LOG_DEBUG;
			break;
		case 'f':
			config_file = optarg;
			break;
		case 'p':
			port_number = optarg;
			break;
		case 'S':
			gfarm_set_metadb_server_force_slave(1);
			break;
		case 's':
			syslog_facility =
			    gflog_syslog_name_to_facility(optarg);
			if (syslog_facility == -1)
				gflog_fatal(GFARM_MSG_1000204,
				    "%s: unknown syslog facility",
				    optarg);
			break;
		case 't':
			file_trace = 1;
			break;
		case 'u':
			failover_config_file = optarg;
			break;
		case 'v':
			gflog_auth_set_verbose(1);
			break;
		case '?':
		default:
			usage();
		}
	}

	if (config_file == NULL)
		config_file = GFMD_CONFIG;
	if (failover_config_file == NULL)
		failover_config_file = GFMD_FAILOVER_CONFIG_BASENAME;
	if (failover_config_file[0] != '/')  {
		failover_config_file = gfarm_config_dirname_add(
		    failover_config_file, config_file);
		if (failover_config_file == NULL) {
			fprintf(stderr, "gfmd: no memory for %s\n",
			    GFMD_FAILOVER_CONFIG_BASENAME);
			exit(1);
		}
	}
	gfarm_metadb_version_major = gfarm_version_major();
	gfarm_metadb_version_minor = gfarm_version_minor();
	gfarm_metadb_version_teeny = gfarm_version_teeny();
	e = gfarm_server_initialize(config_file, &argc, &argv);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001486,
			"gfarm_server_initialize() failed: %s",
		    gfarm_error_string(e));
		fprintf(stderr, "gfarm_server_initialize: %s\n",
		    gfarm_error_string(e));
		exit(1);
	}

	argc -= optind;
	argv += optind;

	if (syslog_level != -1)
		gflog_set_priority_level(syslog_level);
	/* gfmd_port is accessed in gfmd_modules_init() */
	gfmd_port = gfarm_ctxp->metadb_server_port;
	if (port_number != NULL)
		gfmd_port = strtol(port_number, NULL, 0);
	if (file_trace)
		gfarm_ctxp->file_trace = 1;

	/*
	 * We do this before calling gfarm_daemon()
	 * to print the error message to stderr.
	 */
	write_pid();

	giant_init();
	config_var_init();

	/*
	 * We do this before calling gfarm_daemon()
	 * to print the error message to stderr.
	 */
	set_db_ops();

	/*
	 * This initializes dbq, thus, should be called before
	 * producer:gfmd_modules_init() and consumer:db_thread().
	 */
	e = db_initialize();
	if (e != GFARM_ERR_NO_ERROR) {
		/* XXX FIXME need to wait and try to reconnect */
		gflog_fatal(GFARM_MSG_1000209,
		    "database initialization failed: %s",
		    gfarm_error_string(e));
	}

	if (!debug_mode) {
		if (syslog_file != NULL) {
			if (gflog_file_open(syslog_file) == NULL)
				gflog_fatal_errno(GFARM_MSG_1005100,
				    "%s", syslog_file);
		} else
			gflog_syslog_open(LOG_PID, syslog_facility);
		if (gfarm_daemon(0, 0) == -1)
			gflog_warning_errno(GFARM_MSG_1001487, "daemon");
	}
	giant_type_log();

	/*
	 * We do this after calling gfarm_daemon(),
	 * because it changes pid.
	 */
	write_pid();

	/* after gflog_syslog_open/gfarm_daemon, but before pthread_create() */
	if (!gfarm_metadb_server_nfs_root_squash_support) {
		gflog_info(GFARM_MSG_1004491,
		    "nfs_root_squash_support disabled");
		gfarm_privilege_lock_disable();
		gfarm_auth_root_squash_support_disable();
	}

	/*
	 * We don't want SIGPIPE, but want EPIPE on write(2)/close(2).
	 */
	gfarm_sigpipe_ignore();
	/*
	 * Initialize a signal mask before creating threads, since
	 * newly created threads inherit this signal mask.
	 */
	sigs_set(&sigs);
	if (pthread_sigmask(SIG_BLOCK, &sigs, NULL) == -1) /* for sigwait() */
		gflog_fatal(GFARM_MSG_1002736, "sigprocmask(SIG_BLOCK): %s",
		    strerror(errno));

	/*
	 * Create 'db_thread' at almost same time with 'sigs_handler' thread
	 * since db_terminate() which is called by sigs_handler() requires
	 * that 'db_thread' is (eventually) running.
	 */
	e = create_detached_thread(db_thread, NULL);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_fatal(GFARM_MSG_1000211,
		    "create_detached_thread(db_thread): %s",
		    gfarm_error_string(e));

	/*
	 * Create sig_handler thread at earilier stage,
	 * to make SIGTERM work as soon as possible,
	 * because applying journal to DB and loading DB to memory
	 * may take a lot of time.
	 */
	e = create_detached_thread(sigs_handler, &sigs);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_fatal(GFARM_MSG_1000210,
		    "create_detached_thread(sigs_handler): %s",
		    gfarm_error_string(e));

	/* gfarm_limit_nofiles() should be called after gflog_syslog_open() */
	table_size = gfarm_metadb_max_descriptors;
	if (gfarm_limit_nofiles(&table_size) == 0)
		gflog_info(GFARM_MSG_1003455, "max descriptors = %d",
		    table_size);
	 if (gfarm_iostat_gfmd_path) {
		int len;
		len = strlen(gfarm_iostat_gfmd_path) + 6 + 1 + 4 + 1;
				/* for port / gfmd \0 */
		GFARM_MALLOC_ARRAY(iostat_dirbuf, len);
		if (iostat_dirbuf == NULL)
			gflog_fatal(GFARM_MSG_1003609, "iostat_dirbuf:%s",
			gfarm_error_string(GFARM_ERR_NO_MEMORY));
		len = snprintf(iostat_dirbuf, len, "%s-%d",
		    gfarm_iostat_gfmd_path, gfarm_ctxp->metadb_server_port);
		if (mkdir(iostat_dirbuf, 0755)) {
			if (errno != EEXIST)
				gflog_fatal_errno(GFARM_MSG_1003610,
					"mkdir:%s", iostat_dirbuf);
		}
		strcat(iostat_dirbuf, "/gfmd");
		e = gfarm_iostat_mmap(iostat_dirbuf, iostat_spec,
			GFARM_IOSTAT_TRAN_NITEM, table_size);
		if (e != GFARM_ERR_NO_ERROR)
			gflog_fatal(GFARM_MSG_1003611,
				"gfarm_iostat_mmap(%s): %s",
				iostat_dirbuf, gfarm_error_string(e));
	}

	/*
	 * gfmd shouldn't/cannot read/write DB
	 * before calling boot_apply_db_journal() in gfmd_modules_init()
	 * (except for reading seqnum in db_journal_init_seqnum()).
	 *
	 * Note: both boot_apply_db_journal() and *_init() routines may
	 * write DB. (The latter is for metadata consistency check.)
	 */
	if (gfmd_modules_init == NULL) /* there isn't any private extension? */
		gfmd_modules_init = gfmd_modules_init_default;
	gfmd_modules_init(table_size);

	e = create_detached_thread(resumer, NULL);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_fatal(GFARM_MSG_1002209,
		    "create_detached_thread(resumer): %s",
		    gfarm_error_string(e));

	replication_enabled = gfarm_get_metadb_replication_enabled();
	is_master = mdhost_self_is_master();
	if (replication_enabled)
		start_db_journal_threads();
	if (is_master) {
		gflog_info(GFARM_MSG_1004203, "start filesystem check");
		if (replication_enabled && gfmdc_is_master_gfmd_running()) {
			gflog_fatal(GFARM_MSG_1003840,
			    "another master gfmd is already running");
		}
		/* these functions write db, thus, must be after db_thread  */
		if (!gfarm_read_only_mode()) {
			/* do this before inode_check_and_repair() */
			inode_remove_orphan();

			inode_check_and_repair();
		}
	}
	inode_free_orphan();
	gflog_info(GFARM_MSG_1004204, "end bootstrap");
	if (replication_enabled) {
		gflog_info(GFARM_MSG_1002737,
		    "metadata replication %s mode",
		    is_master ? "master" : "slave");
		start_gfmdc_threads();
		gfmd_startup_state_notify_ready();
		if (is_master)
			sock = open_accepting_socket(gfmd_port);
		else
			sock = wait_transform_to_master(gfmd_port);
	} else {
		gflog_info(GFARM_MSG_1005101, "metadata replication disabled");
		sock = open_accepting_socket(gfmd_port);
	}

	/* master */

	quota_check_init();
	replica_check_init();
	failover_notify();
	accepting_loop(sock);

	/*NOTREACHED*/
	return (0); /* to shut up warning */
}

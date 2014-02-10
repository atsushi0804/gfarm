#ifndef GFMD_DEFAULT_PORT
#define GFMD_DEFAULT_PORT	601
#endif

#define GFM_PROTOCOL_VERSION		1

enum gfm_proto_command {
	/* host/user/group metadata */

	GFM_PROTO_HOST_INFO_GET_ALL,
	GFM_PROTO_HOST_INFO_GET_BY_ARCHITECTURE,
	GFM_PROTO_HOST_INFO_GET_BY_NAMES,
	GFM_PROTO_HOST_INFO_GET_BY_NAMEALIASES,
	GFM_PROTO_HOST_INFO_SET,
	GFM_PROTO_HOST_INFO_MODIFY,
	GFM_PROTO_HOST_INFO_REMOVE,
	GFM_PROTO_HOST_INFO_RESERVE7,
	GFM_PROTO_HOST_INFO_RESERVE8,
	GFM_PROTO_HOST_INFO_RESERVE9,
	GFM_PROTO_HOST_INFO_RESERVE10,
	GFM_PROTO_HOST_INFO_RESERVE11,
	GFM_PROTO_HOST_INFO_RESERVE12,
	GFM_PROTO_HOST_INFO_RESERVE13,
	GFM_PROTO_HOST_INFO_RESERVE14,
	GFM_PROTO_HOST_INFO_RESERVE15,

	GFM_PROTO_USER_INFO_GET_ALL,
	GFM_PROTO_USER_INFO_GET_BY_NAMES,
	GFM_PROTO_USER_INFO_SET,
	GFM_PROTO_USER_INFO_MODIFY,
	GFM_PROTO_USER_INFO_REMOVE,
	GFM_PROTO_USER_INFO_GET_BY_GSI_DN,
	GFM_PROTO_USER_INFO_RESERVE6,
	GFM_PROTO_USER_INFO_RESERVE7,
	GFM_PROTO_USER_INFO_RESERVE8,
	GFM_PROTO_USER_INFO_RESERVE9,
	GFM_PROTO_USER_INFO_RESERVE10,
	GFM_PROTO_USER_INFO_RESERVE11,
	GFM_PROTO_USER_INFO_RESERVE12,
	GFM_PROTO_USER_INFO_RESERVE13,
	GFM_PROTO_USER_INFO_RESERVE14,
	GFM_PROTO_USER_INFO_RESERVE15,

	GFM_PROTO_GROUP_INFO_GET_ALL,
	GFM_PROTO_GROUP_INFO_GET_BY_NAMES,
	GFM_PROTO_GROUP_INFO_SET,
	GFM_PROTO_GROUP_INFO_MODIFY,
	GFM_PROTO_GROUP_INFO_REMOVE,
	GFM_PROTO_GROUP_INFO_ADD_USERS,
	GFM_PROTO_GROUP_INFO_REMOVE_USERS,
	GFM_PROTO_GROUP_NAMES_GET_BY_USERS,
	GFM_PROTO_GROUP_INFO_RESERVE8,
	GFM_PROTO_GROUP_INFO_RESERVE9,
	GFM_PROTO_GROUP_INFO_RESERVE10,
	GFM_PROTO_GROUP_INFO_RESERVE11,
	GFM_PROTO_GROUP_INFO_RESERVE12,
	GFM_PROTO_GROUP_INFO_RESERVE13,
	GFM_PROTO_GROUP_INFO_RESERVE14,
	GFM_PROTO_GROUP_INFO_RESERVE15,

	GFM_PROTO_QUOTA_USER_GET,
	GFM_PROTO_QUOTA_USER_SET,
	GFM_PROTO_QUOTA_GROUP_GET,
	GFM_PROTO_QUOTA_GROUP_SET,
	GFM_PROTO_QUOTA_CHECK,
	GFM_PROTO_METADATA_RESERVE5,
	GFM_PROTO_METADATA_RESERVE6,
	GFM_PROTO_METADATA_RESERVE7,
	GFM_PROTO_METADATA_RESERVE8,
	GFM_PROTO_METADATA_RESERVE9,
	GFM_PROTO_METADATA_RESERVE10,
	GFM_PROTO_METADATA_RESERVE11,
	GFM_PROTO_METADATA_RESERVE12,
	GFM_PROTO_METADATA_RESERVE13,
	GFM_PROTO_METADATA_RESERVE14,
	GFM_PROTO_METADATA_RESERVE15,

	/* gfs from client */

	GFM_PROTO_COMPOUND_BEGIN,		/* from gfsd, too */
	GFM_PROTO_COMPOUND_END,			/* from gfsd, too */
	GFM_PROTO_COMPOUND_ON_ERROR,		/* from gfsd, too */
	GFM_PROTO_PUT_FD,			/* from gfsd, too */
	GFM_PROTO_GET_FD,			/* from gfsd, too */
	GFM_PROTO_SAVE_FD,			/* from gfsd, too */
	GFM_PROTO_RESTORE_FD,			/* from gfsd, too */
	GFM_PROTO_BEQUEATH_FD,
	GFM_PROTO_INHERIT_FD,
	GFM_PROTO_CONTROL_OP_RESERVE9,
	GFM_PROTO_CONTROL_OP_RESERVE10,
	GFM_PROTO_CONTROL_OP_RESERVE11,
	GFM_PROTO_CONTROL_OP_RESERVE12,
	GFM_PROTO_CONTROL_OP_RESERVE13,
	GFM_PROTO_CONTROL_OP_RESERVE14,
	GFM_PROTO_CONTROL_OP_RESERVE15,

	GFM_PROTO_OPEN_ROOT,		/* from gfsd, too */
	GFM_PROTO_OPEN_PARENT,		/* from gfsd, too */
	GFM_PROTO_OPEN,			/* from gfsd, too */
	GFM_PROTO_CREATE,		/* from gfsd, too */
	GFM_PROTO_CLOSE,		/* from gfsd, too */
	GFM_PROTO_VERIFY_TYPE,
	GFM_PROTO_VERIFY_TYPE_NOT,
	GFM_PROTO_REVOKE_GFSD_ACCESS,
	GFM_PROTO_FHOPEN,
	GFM_PROTO_FD_MNG_OP_RESERVE9,
	GFM_PROTO_FD_MNG_OP_RESERVE10,
	GFM_PROTO_FD_MNG_OP_RESERVE11,
	GFM_PROTO_FD_MNG_OP_RESERVE12,
	GFM_PROTO_FD_MNG_OP_RESERVE13,
	GFM_PROTO_FD_MNG_OP_RESERVE14,
	GFM_PROTO_FD_MNG_OP_RESERVE15,

	GFM_PROTO_FSTAT,		/* from gfsd, too */
	GFM_PROTO_FUTIMES,		/* from gfsd, too */
	GFM_PROTO_FCHMOD,		/* from gfsd, too */
	GFM_PROTO_FCHOWN,		/* from gfsd, too */
	GFM_PROTO_CKSUM_GET,
	GFM_PROTO_CKSUM_SET,
	GFM_PROTO_SCHEDULE_FILE,
	GFM_PROTO_SCHEDULE_FILE_WITH_PROGRAM,
	GFM_PROTO_FGETATTRPLUS,
	GFM_PROTO_FD_OP_RESERVE9,
	GFM_PROTO_FD_OP_RESERVE10,
	GFM_PROTO_FD_OP_RESERVE11,
	GFM_PROTO_FD_OP_RESERVE12,
	GFM_PROTO_FD_OP_RESERVE13,
	GFM_PROTO_FD_OP_RESERVE14,
	GFM_PROTO_FD_OP_RESERVE15,

	GFM_PROTO_REMOVE,
	GFM_PROTO_RENAME,
	GFM_PROTO_FLINK,
	GFM_PROTO_MKDIR,
	GFM_PROTO_SYMLINK,
	GFM_PROTO_READLINK,
	GFM_PROTO_GETDIRPATH,
	GFM_PROTO_GETDIRENTS,
	GFM_PROTO_SEEK,
	GFM_PROTO_GETDIRENTSPLUS,
	GFM_PROTO_GETDIRENTSPLUSXATTR,
	GFM_PROTO_DIR_OP_RESERVE11,
	GFM_PROTO_DIR_OP_RESERVE12,
	GFM_PROTO_DIR_OP_RESERVE13,
	GFM_PROTO_DIR_OP_RESERVE14,
	GFM_PROTO_DIR_OP_RESERVE15,

	/* gfs from gfsd */

	GFM_PROTO_REOPEN,
	GFM_PROTO_CLOSE_READ,
	GFM_PROTO_CLOSE_WRITE,			/* for COMPAT_GFARM_2_3 */
	GFM_PROTO_LOCK,
	GFM_PROTO_TRYLOCK,
	GFM_PROTO_UNLOCK,
	GFM_PROTO_LOCK_INFO,
	GFM_PROTO_SWITCH_BACK_CHANNEL,		/* for COMPAT_GFARM_2_3 */
	GFM_PROTO_SWITCH_ASYNC_BACK_CHANNEL,
	GFM_PROTO_CLOSE_WRITE_V2_4,
	GFM_PROTO_GENERATION_UPDATED,
	GFM_PROTO_FHCLOSE_READ,
	GFM_PROTO_FHCLOSE_WRITE,
	GFM_PROTO_GENERATION_UPDATED_BY_COOKIE,
	GFM_PROTO_FILE_OP_RESERVE14,
	GFM_PROTO_FILE_OP_RESERVE15,

	/* gfs_pio from client */

	GFM_PROTO_GLOB,
	GFM_PROTO_SCHEDULE,
	GFM_PROTO_PIO_OPEN,
	GFM_PROTO_PIO_SET_PATHS,
	GFM_PROTO_PIO_CLOSE,
	GFM_PROTO_PIO_VISIT,
	GFM_PROTO_PIO_OP_RESERVE6,
	GFM_PROTO_PIO_OP_RESERVE7,
	GFM_PROTO_PIO_OP_RESERVE8,
	GFM_PROTO_PIO_OP_RESERVE9,
	GFM_PROTO_PIO_OP_RESERVE10,
	GFM_PROTO_PIO_OP_RESERVE11,
	GFM_PROTO_PIO_OP_RESERVE12,
	GFM_PROTO_PIO_OP_RESERVE13,
	GFM_PROTO_PIO_OP_RESERVE14,
	GFM_PROTO_PIO_OP_RESERVE15,

	GFM_PROTO_PIO_MISC_RESERVE0,
	GFM_PROTO_PIO_MISC_RESERVE1,
	GFM_PROTO_PIO_MISC_RESERVE2,
	GFM_PROTO_PIO_MISC_RESERVE3,
	GFM_PROTO_PIO_MISC_RESERVE4,
	GFM_PROTO_PIO_MISC_RESERVE5,
	GFM_PROTO_PIO_MISC_RESERVE6,
	GFM_PROTO_PIO_MISC_RESERVE7,
	GFM_PROTO_PIO_MISC_RESERVE8,
	GFM_PROTO_PIO_MISC_RESERVE9,
	GFM_PROTO_PIO_MISC_RESERVE10,
	GFM_PROTO_PIO_MISC_RESERVE11,
	GFM_PROTO_PIO_MISC_RESERVE12,
	GFM_PROTO_PIO_MISC_RESERVE13,
	GFM_PROTO_PIO_MISC_RESERVE14,
	GFM_PROTO_PIO_MISC_RESERVE15,

	GFM_PROTO_HOSTNAME_SET,
	GFM_PROTO_SCHEDULE_HOST_DOMAIN,
	GFM_PROTO_STATFS,
	GFM_PROTO_MISC_RESERVE3,
	GFM_PROTO_MISC_RESERVE4,
	GFM_PROTO_MISC_RESERVE5,
	GFM_PROTO_MISC_RESERVE6,
	GFM_PROTO_MISC_RESERVE7,
	GFM_PROTO_MISC_RESERVE8,
	GFM_PROTO_MISC_RESERVE9,
	GFM_PROTO_MISC_RESERVE10,
	GFM_PROTO_MISC_RESERVE11,
	GFM_PROTO_MISC_RESERVE12,
	GFM_PROTO_MISC_RESERVE13,
	GFM_PROTO_MISC_RESERVE14,
	GFM_PROTO_MISC_RESERVE15,

	/* replica management from client */

	GFM_PROTO_REPLICA_LIST_BY_NAME,
	GFM_PROTO_REPLICA_LIST_BY_HOST,
	GFM_PROTO_REPLICA_REMOVE_BY_HOST,
	GFM_PROTO_REPLICA_REMOVE_BY_FILE,
	GFM_PROTO_REPLICA_INFO_GET,
	GFM_PROTO_REPLICATE_FILE_FROM_TO,
	GFM_PROTO_REPLICATE_FILE_TO,
	GFM_PROTO_REPLICA_OP_RESERVE7,
	GFM_PROTO_REPLICA_OP_RESERVE8,
	GFM_PROTO_REPLICA_OP_RESERVE9,
	GFM_PROTO_REPLICA_OP_RESERVE10,
	GFM_PROTO_REPLICA_OP_RESERVE11,
	GFM_PROTO_REPLICA_OP_RESERVE12,
	GFM_PROTO_REPLICA_OP_RESERVE13,
	GFM_PROTO_REPLICA_OP_RESERVE14,
	GFM_PROTO_REPLICA_OP_RESERVE15,

	/* replica management from gfsd */

	GFM_PROTO_REPLICA_ADDING,		/* for COMPAT_GFARM_2_3 */
	GFM_PROTO_REPLICA_ADDED,		/* for COMPAT_GFARM_2_3 */
	GFM_PROTO_REPLICA_LOST,
	GFM_PROTO_REPLICA_ADD,			/* not used? */
	GFM_PROTO_REPLICA_ADDED2,		/* for COMPAT_GFARM_2_3 */
	GFM_PROTO_REPLICATION_RESULT,
	GFM_PROTO_REPLICA_GET_MY_ENTRIES,
	GFM_PROTO_REPLICA_CREATE_FILE_IN_LOST_FOUND,
	GFM_PROTO_REPLICA_GET_MY_ENTRIES2,
	GFM_PROTO_REPLICA_MNG_RESERVE9,
	GFM_PROTO_REPLICA_MNG_RESERVE10,
	GFM_PROTO_REPLICA_MNG_RESERVE11,
	GFM_PROTO_REPLICA_MNG_RESERVE12,
	GFM_PROTO_REPLICA_MNG_RESERVE13,
	GFM_PROTO_REPLICA_MNG_RESERVE14,
	GFM_PROTO_REPLICA_MNG_RESERVE15,

	/* job management */

	GFM_PROTO_PROCESS_ALLOC,
	GFM_PROTO_PROCESS_ALLOC_CHILD,
	GFM_PROTO_PROCESS_FREE,
	GFM_PROTO_PROCESS_SET,
	GFM_PROTO_PROCESS_RESERVE4,
	GFM_PROTO_PROCESS_RESERVE5,
	GFM_PROTO_PROCESS_RESERVE6,
	GFM_PROTO_PROCESS_RESERVE7,
	GFM_PROTO_PROCESS_RESERVE8,
	GFM_PROTO_PROCESS_RESERVE9,
	GFM_PROTO_PROCESS_RESERVE10,
	GFM_PROTO_PROCESS_RESERVE11,
	GFM_PROTO_PROCESS_RESERVE12,
	GFM_PROTO_PROCESS_RESERVE13,
	GFM_PROTO_PROCESS_RESERVE14,
	GFM_PROTO_PROCESS_RESERVE15,

	GFJ_PROTO_LOCK_REGISTER,
	GFJ_PROTO_UNLOCK_REGISTER,
	GFJ_PROTO_REGISTER,
	GFJ_PROTO_UNREGISTER,
	GFJ_PROTO_REGISTER_NODE,
	GFJ_PROTO_LIST,
	GFJ_PROTO_INFO,
	GFJ_PROTO_HOSTINFO,
	GFJ_PROTO_RESERVE8,
	GFJ_PROTO_RESERVE9,
	GFJ_PROTO_RESERVE10,
	GFJ_PROTO_RESERVE11,
	GFJ_PROTO_RESERVE12,
	GFJ_PROTO_RESERVE13,
	GFJ_PROTO_RESERVE14,
	GFJ_PROTO_RESERVE15,

	/* extended attribute management */

	GFM_PROTO_XATTR_SET,
	GFM_PROTO_XMLATTR_SET,
	GFM_PROTO_XATTR_GET,
	GFM_PROTO_XMLATTR_GET,
	GFM_PROTO_XATTR_REMOVE,
	GFM_PROTO_XMLATTR_REMOVE,
	GFM_PROTO_XATTR_LIST,
	GFM_PROTO_XMLATTR_LIST,
	GFM_PROTO_XMLATTR_FIND,
	GFM_PROTO_XATTR_OP_RESERVE9,
	GFM_PROTO_XATTR_OP_RESERVE10,
	GFM_PROTO_XATTR_OP_RESERVE11,
	GFM_PROTO_XATTR_OP_RESERVE12,
	GFM_PROTO_XATTR_OP_RESERVE13,
	GFM_PROTO_XATTR_OP_RESERVE14,
	GFM_PROTO_XATTR_OP_RESERVE15,

	/* gfmd channel : redundancy */

	GFM_PROTO_SWITCH_GFMD_CHANNEL,
	GFM_PROTO_JOURNAL_READY_TO_RECV,
	GFM_PROTO_JOURNAL_SEND,
	GFM_PROTO_REDUNDANCY_RESERVE3,
	GFM_PROTO_REDUNDANCY_RESERVE4,
	GFM_PROTO_REDUNDANCY_RESERVE5,
	GFM_PROTO_REDUNDANCY_RESERVE6,
	GFM_PROTO_REDUNDANCY_RESERVE7,
	GFM_PROTO_REDUNDANCY_RESERVE8,
	GFM_PROTO_REDUNDANCY_RESERVE9,
	GFM_PROTO_REDUNDANCY_RESERVE10,
	GFM_PROTO_REDUNDANCY_RESERVE11,
	GFM_PROTO_REDUNDANCY_RESERVE12,
	GFM_PROTO_REDUNDANCY_RESERVE13,
	GFM_PROTO_REDUNDANCY_RESERVE14,
	GFM_PROTO_REDUNDANCY_RESERVE15,

	/* metadb_server */
	GFM_PROTO_METADB_SERVER_GET,
	GFM_PROTO_METADB_SERVER_GET_ALL,
	GFM_PROTO_METADB_SERVER_SET,
	GFM_PROTO_METADB_SERVER_MODIFY,
	GFM_PROTO_METADB_SERVER_REMOVE,
	GFM_PROTO_METADB_SERVER_RESERVE6,
	GFM_PROTO_METADB_SERVER_RESERVE7,
	GFM_PROTO_METADB_SERVER_RESERVE8,
	GFM_PROTO_METADB_SERVER_RESERVE9,
	GFM_PROTO_METADB_SERVER_RESERVE10,
	GFM_PROTO_METADB_SERVER_RESERVE11,
	GFM_PROTO_METADB_SERVER_RESERVE12,
	GFM_PROTO_METADB_SERVER_RESERVE13,
	GFM_PROTO_METADB_SERVER_RESERVE14,
	GFM_PROTO_METADB_SERVER_RESERVE15,

	/* range of private protocol number */
	GFM_PROTO_PRIVATE_BASE = 0xF0000000,
	GFM_PROTO_PRIVATE_END  = 0xFFFFFFFF
};

#define GFM_PROTO_PROCESS_KEY_TYPE_SHAREDSECRET	1
#define GFM_PROTO_PROCESS_KEY_LEN_SHAREDSECRET	32

/* GFM_PROTO_CKSUM_GET flags */
#define	GFM_PROTO_CKSUM_GET_MAYBE_EXPIRED	0x00000001
#define	GFM_PROTO_CKSUM_GET_EXPIRED		0x00000002

/* GFM_PROTO_CKSUM_SET flags */
#define	GFM_PROTO_CKSUM_SET_FILE_MODIFIED	0x00000001

/*
 * data size limits:
 *
 * see also:
 * GFS_MAXNAMLEN in <gfarm/gfs.h>
 * MAX_XATTR_NAME_LEN in gfm/xattr.c
 * gftool/config-gfarm/gfarm.sql
 */

#define GFM_PROTO_CKSUM_TYPE_MAXLEN		32
#define GFM_PROTO_CKSUM_MAXLEN			256

#define GFM_PROTO_MAX_DIRENT	10240

#define GFARM_HOST_NAME_MAX			256
#define GFARM_HOST_ARCHITECTURE_NAME_MAX	128
#define GFARM_CLUSTER_NAME_MAX			256

#define GFARM_LOGIN_NAME_MAX			64
#define GFARM_USER_REALNAME_MAX			256
#define GFARM_USER_GSI_DN_MAX			1024

#define GFARM_GROUP_NAME_MAX			8192 /* VOMS needs long name */

/* see GFS_MAXNAMLEN in <gfarm/gfs.h> as well */
#define GFARM_PATH_MAX				1024

#define GFARM_XATTR_NAME_MAX			256

/*
 * NOTE: GFARM_XATTR_SIZE_MAX_LIMIT and GFARM_XMLATTR_SIZE_MAX_LIMIT must be
 * smaller than JOURNAL_RECORD_SIZE_MAX in server/gfmd/journal_file.c.
 */
#define GFARM_XATTR_SIZE_MAX_DEFAULT		(64*1024)
#define GFARM_XATTR_SIZE_MAX_LIMIT		(1024*1024-64*1024)
#define GFARM_XMLATTR_SIZE_MAX_DEFAULT		(768*1024)
#define GFARM_XMLATTR_SIZE_MAX_LIMIT		(1024*1024-64*1024)

/* GFM_PROTO_SCHEDULE_FILE, GFM_PROTO_SCHEDULE_FILE_WITH_PROGRAM */
#define GFM_PROTO_SCHED_FLAG_HOST_AVAIL		1 /* always TRUE for now */
#define GFM_PROTO_SCHED_FLAG_LOADAVG_AVAIL	2 /* always TRUE for now */
#define GFM_PROTO_SCHED_FLAG_RTT_AVAIL		4 /* always FALSE for now */
#define GFM_PROTO_LOADAVG_FSCALE 		2048

/* output of GFM_PROTO_CLOSE_WRITE_V2_4 */
#define	GFM_PROTO_CLOSE_WRITE_GENERATION_UPDATE_NEEDED	1

/* output of GFM_PROTO_REPLICA_INFO_GET */
#define GFM_PROTO_REPLICA_FLAG_INCOMPLETE	1
#define GFM_PROTO_REPLICA_FLAG_DEAD_HOST	2
#define GFM_PROTO_REPLICA_FLAG_DEAD_COPY	4

/* output of GFM_PROTO_METADB_SERVER_GET: Persistent Flags */
#define GFARM_METADB_SERVER_FLAG_IS_MASTER_CANDIDATE	0x00000001
#define GFARM_METADB_SERVER_FLAG_IS_DEFAULT_MASTER	0x00000002

/* output of GFM_PROTO_METADB_SERVER_GET: Volatile Flags */
#define GFARM_METADB_SERVER_FLAG_IS_SELF		0x00000001
#define GFARM_METADB_SERVER_FLAG_IS_MASTER		0x00000002
#define GFARM_METADB_SERVER_FLAG_IS_SYNCREP		0x00000004
#define GFARM_METADB_SERVER_FLAG_IS_ACTIVE		0x00000008
#define GFARM_METADB_SERVER_FLAG_SEQNUM_MASK		0x00000070
#define GFARM_METADB_SERVER_FLAG_SEQNUM_IS_UNKNOWN	0x00000000
#define GFARM_METADB_SERVER_FLAG_SEQNUM_IS_OK		0x00000010
#define GFARM_METADB_SERVER_FLAG_SEQNUM_IS_OUT_OF_SYNC	0x00000020
#define GFARM_METADB_SERVER_FLAG_SEQNUM_IS_ERROR	0x00000030
#define GFARM_METADB_SERVER_FLAG_IS_MEMORY_OWNED_BY_FS	0x00000080
#define GFARM_METADB_SERVER_FLAG_IS_REMOVED		0x00000100

/* Special sequence number, never used in the protocol. */
#define GFARM_METADB_SERVER_SEQNUM_INVALID		0

#define GFMD_USERNAME	"_gfarmmd"

#if 0 /* There isn't gfm_proto.c for now. */
extern char GFM_SERVICE_TAG[];
#else
#define GFM_SERVICE_TAG "gfarm-metadata"
#endif

#include <pthread.h>
#include <sys/types.h>
#include <pwd.h>
#include <string.h>

#include <gssapi.h>

#define GFARM_USE_GSSAPI
#include <gfarm/gfarm_config.h>
#include <gfarm/gflog.h>
#include <gfarm/error.h>
#include <gfarm/gfarm_misc.h>

#include "gfutil.h"
#include "thrsubr.h"

#include "gfarm_secure_session.h"
#include "gfarm_auth.h"

#include "context.h"
#include "liberror.h"
#include "gfpath.h"
#include "auth.h"
#include "auth_gsi.h"

#define staticp	(gfarm_ctxp->auth_common_gsi_static)

struct gfarm_auth_common_gsi_static {
	gss_cred_id_t client_cred;

	/* gfarm_gsi_client_cred_name() */
	pthread_mutex_t client_cred_init_mutex;
	int client_cred_initialized;
	char *client_dn;
};

gfarm_error_t
gfarm_auth_common_gsi_static_init(struct gfarm_context *ctxp)
{
	struct gfarm_auth_common_gsi_static *s;
	static const char diag[] = "gfarm_auth_common_gsi_static_init";

	GFARM_MALLOC(s);
	if (s == NULL)
		return (GFARM_ERR_NO_MEMORY);

	s->client_cred = GSS_C_NO_CREDENTIAL;

	gfarm_mutex_init(&s->client_cred_init_mutex,
	    diag, "client_cred_initialize");
	s->client_cred_initialized = 0;
	s->client_dn = NULL;

	ctxp->auth_common_gsi_static = s;
	return (GFARM_ERR_NO_ERROR);
}

void
gfarm_auth_common_gsi_static_term(struct gfarm_context *ctxp)
{
	struct gfarm_auth_common_gsi_static *s = ctxp->auth_common_gsi_static;
	static const char diag[] = "gfarm_auth_common_gsi_static_term";

	if (s == NULL)
		return;

	gfarm_mutex_destroy(&s->client_cred_init_mutex,
	    diag, "client_cred_initialize");
	free(s->client_dn);
	free(s);
}

void
gfarm_gsi_client_finalize(void)
{
	gfarmSecSessionFinalizeInitiator();
}

gfarm_error_t
gfarm_gsi_client_initialize(void)
{
	OM_uint32 e_major, e_minor;
	int rv;

	rv = gfarmSecSessionInitializeInitiator(NULL, GRID_MAPFILE,
	    &e_major, &e_minor);
	if (rv <= 0) {
		if (gflog_auth_get_verbose()) {
			gflog_error(GFARM_MSG_1000706,
			    "can't initialize as initiator because of:");
			gfarmGssPrintMajorStatus(e_major);
			gfarmGssPrintMinorStatus(e_minor);
		}
		gfarm_gsi_client_finalize();

		return (GFARM_ERRMSG_GSI_CREDENTIAL_INITIALIZATION_FAILED);
	}
	return (GFARM_ERR_NO_ERROR);
}

char *
gfarm_gsi_client_cred_name(void)
{
	gss_cred_id_t cred = gfarm_gsi_client_cred_get();
	gss_name_t name;
	OM_uint32 e_major, e_minor;
	char *client_dn;
	static const char diag[] = "gfarm_gsi_client_cred_name";
	static const char mutex_name[] = "client_cred_init_mutex";

	gfarm_mutex_lock(&staticp->client_cred_init_mutex, diag, mutex_name);
	if (staticp->client_cred_initialized) {
		client_dn = staticp->client_dn;
		gfarm_mutex_unlock(&staticp->client_cred_init_mutex,
		    diag, mutex_name);
		return (client_dn);
	}

	if (gfarmGssNewCredentialName(&name, cred, &e_major, &e_minor) < 0) {
		staticp->client_dn = NULL;
		gflog_auth_notice(GFARM_MSG_1004249,
		    "gfarm_gsi_client_cred_name(): "
		    "cannot acquire initiator credential");
		if (gflog_auth_get_verbose()) {
			gfarmGssPrintMajorStatus(e_major);
			gfarmGssPrintMinorStatus(e_minor);
		}
	} else {
		staticp->client_dn = gfarmGssNewDisplayName(
		    name, &e_major, &e_minor, NULL);
		if (staticp->client_dn == NULL && gflog_auth_get_verbose()) {
			gflog_error(GFARM_MSG_1000709,
			    "cannot convert initiator credential "
			    "to string");
			gfarmGssPrintMajorStatus(e_major);
			gfarmGssPrintMinorStatus(e_minor);
		}
		gfarmGssDeleteName(&name, NULL, NULL);
		staticp->client_cred_initialized = 1;
	}
	client_dn = staticp->client_dn;
	gfarm_mutex_unlock(&staticp->client_cred_init_mutex, diag, mutex_name);
	return (client_dn);
}

void
gfarm_gsi_server_finalize(void)
{
	gfarmSecSessionFinalizeBoth();
}

gfarm_error_t
gfarm_gsi_server_initialize(void)
{
	OM_uint32 e_major, e_minor;
	int rv;

	rv = gfarmSecSessionInitializeBoth(NULL, NULL, GRID_MAPFILE,
	    &e_major, &e_minor);
	if (rv <= 0) {
		if (gflog_auth_get_verbose()) {
			gflog_error(GFARM_MSG_1000710,
			    "can't initialize GSI as both because of:");
			gfarmGssPrintMajorStatus(e_major);
			gfarmGssPrintMinorStatus(e_minor);
		}
		gfarm_gsi_server_finalize();

		return (GFARM_ERRMSG_GSI_INITIALIZATION_FAILED);
	}
	return (GFARM_ERR_NO_ERROR);
}

/*
 * Delegated credential
 */

/*
 * XXX - thread-unsafe interface.  this assumes a single thread server
 * like gfsd and gfarm_gridftp_dsi.  this is not for gfmd.
 */
void
gfarm_gsi_client_cred_set(gss_cred_id_t cred)
{
	staticp->client_cred = cred;
}

gss_cred_id_t
gfarm_gsi_client_cred_get()
{
	return (staticp->client_cred);
}

/*
 * deprecated. gfarm_gsi_set_delegated_cred() will be removed in future.
 * (currently gfarm-gridftp-dsi is using this)
 */
void
gfarm_gsi_set_delegated_cred(gss_cred_id_t cred)
{
	gfarm_gsi_client_cred_set(cred);
}

/*
 * converter from credential configuration to [GSSNameType, GSSName].
 *
 * The results of
 * (type, service, name) -> gss_name_t [NameType, Name] -> gss_cred_id_t
 * are:
 * (DEFAULT, NULL, NULL) is not allowed. caller must check this at first.
 * (NO_NAME, NULL, NULL) -> GSS_C_NO_NAME
 * (MECHANISM_SPECIFIC, NULL, name) -> [GSS_C_NO_OID, name]
 * (HOST, service, host) ->[GSS_C_NT_HOSTBASED_SERVICE, service + "@" + host]
 *		if (service == NULL) service = "host"
 * (USER, NULL, username) -> [GSS_C_NT_USER_NAME, username]
 *		if (username == NULL) username = self_local_username
 * (SELF, NULL, NULL) -> the name of initial initiator credential
 *
 * when a server acquires a credential of itself:
 *	(DEFAULT, NULL, NULL) -> N/A -> GSS_C_NO_CREDENTIAL
 * when a client authenticates a server:
 *	(DEFAULT, NULL, NULL) is equivalent to (HOST, NULL, NULL)
 *	(HOST, service, NULL) is equivalent to (HOST, service, peer_host)
 */

gfarm_error_t
gfarm_gsi_cred_config_convert_to_name(
	enum gfarm_auth_cred_type type, char *service, char *name,
	char *hostname,
	gss_name_t *namep)
{
	int rv;
	OM_uint32 e_major;
	OM_uint32 e_minor;

	switch (type) {
	case GFARM_AUTH_CRED_TYPE_DEFAULT:
		/* special. equivalent to GSS_C_NO_CREDENTIAL */
		if (name != NULL)
			return (GFARM_ERRMSG_CRED_TYPE_DEFAULT_INVALID_CRED_NAME);
		if (service != NULL)
			return (GFARM_ERRMSG_CRED_TYPE_DEFAULT_INVALID_CRED_SERVICE);
		return (GFARM_ERRMSG_CRED_TYPE_DEFAULT_INTERNAL_ERROR);
	case GFARM_AUTH_CRED_TYPE_NO_NAME:
		if (name != NULL)
			return (GFARM_ERRMSG_CRED_TYPE_NO_NAME_INVALID_CRED_NAME);
		if (service != NULL)
			return (GFARM_ERRMSG_CRED_TYPE_NO_NAME_INVALID_CRED_SERVICE);
		*namep = GSS_C_NO_NAME;
		return (GFARM_ERR_NO_ERROR);
	case GFARM_AUTH_CRED_TYPE_MECHANISM_SPECIFIC:
		if (name == NULL)
			return (GFARM_ERRMSG_CRED_TYPE_MECHANISM_SPECIFIC_INVALID_CRED_NAME);
		if (service != NULL)
			return (GFARM_ERRMSG_CRED_TYPE_MECHANISM_SPECIFIC_INVALID_CRED_SERVICE);
		rv = gfarmGssImportName(namep, name, strlen(name),
		    GSS_C_NO_OID, &e_major, &e_minor);
		break;
	case GFARM_AUTH_CRED_TYPE_HOST:
		if (name == NULL)
			name = hostname;
		if (service == NULL) {
			rv = gfarmGssImportNameOfHost(namep, name,
			    &e_major, &e_minor);
		} else {
			rv = gfarmGssImportNameOfHostBasedService(namep,
			    service, name, &e_major, &e_minor);
		}
		break;
	case GFARM_AUTH_CRED_TYPE_USER:
		if (service != NULL)
			return (GFARM_ERRMSG_CRED_TYPE_USER_CRED_INVALID_CRED_SERVICE);
		/*
		 * XXX FIXME: `name' must be converted from global_username
		 * to local_username, but there is no such function for now.
		 */
		if (name == NULL)
			name = gfarm_get_local_username();
		rv = gfarmGssImportName(namep, name, strlen(name),
		    GSS_C_NT_USER_NAME, &e_major, &e_minor);
		break;
	case GFARM_AUTH_CRED_TYPE_SELF:
		/* special. there is no corresponding name_type in GSSAPI */
		if (name != NULL)
			return (GFARM_ERRMSG_CRED_TYPE_SELF_CRED_INVALID_CRED_NAME);
		if (service != NULL)
			return (GFARM_ERRMSG_CRED_TYPE_SELF_CRED_INVALID_CRED_SERVICE);
		rv = gfarmGssAcquireCredential(NULL, GSS_C_NO_NAME,
			GSS_C_INITIATE, &e_major, &e_minor, namep);
		break;
	default:
		return (GFARM_ERRMSG_INVALID_CRED_TYPE);
	}
	if (rv < 0) {
		if (gflog_auth_get_verbose()) {
			gflog_error(GFARM_MSG_1000711, "gfarmGssImportName(): "
			    "invalid credential configuration:");
			gfarmGssPrintMajorStatus(e_major);
			gfarmGssPrintMinorStatus(e_minor);
		}
		return (GFARM_ERRMSG_INVALID_CREDENTIAL_CONFIGURATION);
	}
	return (GFARM_ERR_NO_ERROR);
}

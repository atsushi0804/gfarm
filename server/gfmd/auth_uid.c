#include <gfarm/gfarm_config.h>

#include <assert.h>
#include <stdlib.h>

#include <gfarm/error.h>
#include <gfarm/gflog.h>
#include <gfarm/gfarm_misc.h>
#include <gfarm/gfs.h>

#include "auth.h"

#include "subr.h"
#include "host.h"
#include "mdhost.h"
#include "user.h"
#include "group.h"
#include "gfmd.h"

static gfarm_error_t auth_uid_to_global_username_sharedsecret(
	void *, const char *, enum gfarm_auth_id_type *, char **);
#ifdef HAVE_GSI
static gfarm_error_t auth_uid_to_global_username_gsi(
	void *, const char *, enum gfarm_auth_id_type *, char **);
#endif
#ifdef HAVE_KERBEROS
static gfarm_error_t auth_uid_to_global_username_kerberos(
	void *, const char *, enum gfarm_auth_id_type *, char **);
#endif
#ifdef HAVE_TLS_1_3
static gfarm_error_t auth_uid_to_global_username_tls_client_certificate(
	void *, const char *, enum gfarm_auth_id_type *, char **);
#endif

gfarm_error_t (*auth_uid_to_global_username_table[])(
	void *, const char *, enum gfarm_auth_id_type *, char **) = {
/*
 * This table entry should be ordered by enum gfarm_auth_method.
 */
 gfarm_auth_uid_to_global_username_panic, /*GFARM_AUTH_METHOD_NONE*/
 auth_uid_to_global_username_sharedsecret,
					/*GFARM_AUTH_METHOD_SHAREDSECRET */
 gfarm_auth_uid_to_global_username_panic, /*GFARM_AUTH_METHOD_GSI_OLD*/
#ifdef HAVE_GSI
 auth_uid_to_global_username_gsi,	/*GFARM_AUTH_METHOD_GSI*/
 auth_uid_to_global_username_gsi,	/*GFARM_AUTH_METHOD_GSI_AUTH*/
#else
 gfarm_auth_uid_to_global_username_panic, /*GFARM_AUTH_METHOD_GSI*/
 gfarm_auth_uid_to_global_username_panic, /*GFARM_AUTH_METHOD_GSI_AUTH*/
#endif
#ifdef HAVE_TLS_1_3
 auth_uid_to_global_username_sharedsecret,
					/*GFARM_AUTH_METHOD_TLS_SHAREDSECRET*/
 auth_uid_to_global_username_tls_client_certificate,
				   /*GFARM_AUTH_METHOD_TLS_CLIENT_CERTIFICATE*/
#else
 gfarm_auth_uid_to_global_username_panic,/*GFARM_AUTH_METHOD_TLS_SHAREDSECRET*/
 gfarm_auth_uid_to_global_username_panic,
				   /*GFARM_AUTH_METHOD_TLS_CLIENT_CERTIFICATE*/
#endif
#ifdef HAVE_KERBEROS
 auth_uid_to_global_username_kerberos,	/*GFARM_AUTH_METHOD_KERBEROS*/
 auth_uid_to_global_username_kerberos,	/*GFARM_AUTH_METHOD_KERBEROS_AUTH*/
#else
 gfarm_auth_uid_to_global_username_panic, /*GFARM_AUTH_METHOD_KERBEROS*/
 gfarm_auth_uid_to_global_username_panic, /*GFARM_AUTH_METHOD_KERBEROS_AUTH*/
#endif
#if defined(HAVE_CYRUS_SASL) && defined(HAVE_TLS_1_3)
 auth_uid_to_global_username_sharedsecret,	/*GFARM_AUTH_METHOD_SASL*/
 auth_uid_to_global_username_sharedsecret,	/*GFARM_AUTH_METHOD_SASL_AUTH*/
#else
 gfarm_auth_uid_to_global_username_panic,	/*GFARM_AUTH_METHOD_SASL*/
 gfarm_auth_uid_to_global_username_panic,	/*GFARM_AUTH_METHOD_SASL_AUTH*/
#endif
};

#ifdef HAVE_GSI

/*
 * cannot use auth_uid_to_global_username_server_auth(),
 * because current GSI protocol doesn't pass auth_user_id_type.
 */
static gfarm_error_t
auth_uid_to_global_username_gsi(void *closure,
	const char *auth_user_id, enum gfarm_auth_id_type *auth_user_id_typep,
	char **global_usernamep)
{
	gfarm_error_t e;
	enum gfarm_auth_id_type auth_user_id_type = *auth_user_id_typep;
	char *global_username;
	struct user *u;
	char *cn, *hostname = NULL;
	struct host *h;
	struct mdhost *m;
	const char diag[] = "auth_uid_to_global_username_gsi";

	assert(auth_user_id_type == GFARM_AUTH_ID_TYPE_UNKNOWN);
	giant_lock();
	u = user_lookup_gsi_dn(auth_user_id);
	if (u != NULL) {
		giant_unlock();
		/* auth_user_id is a DN */
		*auth_user_id_typep = GFARM_AUTH_ID_TYPE_USER;
		if (global_usernamep == NULL)
			return (GFARM_ERR_NO_ERROR);
		global_username = strdup_log(user_name(u), diag);
		if (global_username == NULL)
			return (GFARM_ERR_NO_MEMORY);
		*global_usernamep = global_username;
		return (GFARM_ERR_NO_ERROR);

	}

	e = gfarm_x509_get_cn(auth_user_id, &cn);
	if (e != GFARM_ERR_NO_ERROR) {
		giant_unlock();
		return (e);
	}

	if ((e = gfarm_x509_cn_get_hostname(
	    GFARM_AUTH_ID_TYPE_SPOOL_HOST, cn, &hostname))
	    == GFARM_ERR_NO_ERROR &&
	    (h = host_lookup(hostname)) != NULL) {
		giant_unlock();
		*auth_user_id_typep = GFARM_AUTH_ID_TYPE_SPOOL_HOST;
		if (global_usernamep == NULL)
			free(hostname);
		else
			*global_usernamep = hostname;
		free(cn);
		return (GFARM_ERR_NO_ERROR);
	}
	if (hostname != NULL) {
		free(hostname);
		hostname = NULL;
	}
	if ((e = gfarm_x509_cn_get_hostname(
	    GFARM_AUTH_ID_TYPE_METADATA_HOST, cn, &hostname))
	    == GFARM_ERR_NO_ERROR &&
	    (m = mdhost_lookup(hostname)) != NULL) {
		giant_unlock();
		*auth_user_id_typep = GFARM_AUTH_ID_TYPE_METADATA_HOST;
		if (global_usernamep == NULL)
			free(hostname);
		else
			*global_usernamep = hostname;
		free(cn);
		return (GFARM_ERR_NO_ERROR);
	}
	giant_unlock();

	free(hostname);
	free(cn);
	return (GFARM_ERR_AUTHENTICATION);

}

#endif /* HAVE_GSI */

#if defined(HAVE_TLS_1_3) || defined(HAVE_KERBEROS)

static gfarm_error_t
auth_uid_to_global_username_server_auth(void *closure,
	const char *auth_user_id,
	gfarm_error_t (*get_hostname)(enum gfarm_auth_id_type, const char *,
	    char **hostnamep),
	enum gfarm_auth_id_type *auth_user_id_typep,
	char **global_usernamep, const char *diag)
{
	gfarm_error_t e;
	enum gfarm_auth_id_type auth_user_id_type = *auth_user_id_typep;
	char *global_username;
	struct user *u;
	char *hostname;
	struct host *h;
	struct mdhost *m;

	switch (auth_user_id_type) {
	case GFARM_AUTH_ID_TYPE_USER:
		/* auth_user_id is Distinguished Name */
		giant_lock();
		u = user_lookup_gsi_dn(auth_user_id);
		giant_unlock();

		if (u == NULL) {
			/*
			 * do not return GFARM_ERR_NO_SUCH_USER
			 * to prevent information leak
			 */
			gflog_info(GFARM_MSG_UNFIXED,
			    "unknown user id <%s>", auth_user_id);
			return (GFARM_ERR_AUTHENTICATION);
		}
		if (global_usernamep == NULL)
			return (GFARM_ERR_NO_ERROR);
		global_username = strdup_log(user_name(u), diag);
		if (global_username == NULL)
			return (GFARM_ERR_NO_MEMORY);
		*global_usernamep = global_username;
		return (GFARM_ERR_NO_ERROR);
	case GFARM_AUTH_ID_TYPE_SPOOL_HOST:
		e = (*get_hostname)(auth_user_id_type, auth_user_id,
		    &hostname);
		if (e != GFARM_ERR_NO_ERROR)
			return (GFARM_ERR_AUTHENTICATION);
		giant_lock();
		h = host_lookup(hostname);
		giant_unlock();
		if (h == NULL) {
			gflog_info(GFARM_MSG_UNFIXED,
			    "unknown gfsd <%s>", hostname);
			free(hostname);
			return (GFARM_ERR_AUTHENTICATION);
		}
		if (global_usernamep == NULL)
			free(hostname);
		else
			*global_usernamep = hostname;
		return (GFARM_ERR_NO_ERROR);
	case GFARM_AUTH_ID_TYPE_METADATA_HOST:
		e = (*get_hostname)(auth_user_id_type, auth_user_id,
		    &hostname);
		if (e != GFARM_ERR_NO_ERROR)
			return (GFARM_ERR_AUTHENTICATION);
		giant_lock();
		m = mdhost_lookup(hostname);
		giant_unlock();
		if (m == NULL) {
			gflog_info(GFARM_MSG_UNFIXED,
			    "unknown gfmd <%s>", hostname);
			free(hostname);
			return (GFARM_ERR_AUTHENTICATION);
		}
		if (global_usernamep == NULL)
			free(hostname);
		else
			*global_usernamep = hostname;
		return (GFARM_ERR_NO_ERROR);
	case GFARM_AUTH_ID_TYPE_UNKNOWN:
		/*FALLTHROUGH*/
	default:
		gflog_debug(GFARM_MSG_UNFIXED,
		    "auth_uid_to_global_username(id_type:%d, id:%s): "
		    "unexpected call",
		    auth_user_id_type, auth_user_id);
		return (GFARM_ERR_AUTHENTICATION);
	}
}
#endif /* defined(HAVE_TLS_1_3) || defined(HAVE_KERBEROS) */

#ifdef HAVE_KERBEROS

static gfarm_error_t
auth_uid_to_global_username_kerberos(void *closure,
	const char *auth_user_id, enum gfarm_auth_id_type *auth_user_id_typep,
	char **global_usernamep)
{
	return (auth_uid_to_global_username_server_auth(closure,
	  auth_user_id, gfarm_kerberos_principal_get_hostname,
	    auth_user_id_typep, global_usernamep,
	    "auth_uid_to_global_username_kerberos"));
}

#endif /* HAVE_KERBEROS */

#ifdef HAVE_TLS_1_3

static gfarm_error_t
auth_uid_to_global_username_tls_client_certificate(void *closure,
	const char *auth_user_id, enum gfarm_auth_id_type *auth_user_id_typep,
	char **global_usernamep)
{
	return (auth_uid_to_global_username_server_auth(closure,
	  auth_user_id, gfarm_x509_cn_get_hostname,
	    auth_user_id_typep, global_usernamep,
	    "auth_uid_to_global_username_tls_client_certificate"));
}

#endif /* HAVE_TLS_1_3 */

static gfarm_error_t
auth_uid_to_global_username_sharedsecret(void *closure,
	const char *auth_user_id, enum gfarm_auth_id_type *auth_user_id_typep,
	char **global_usernamep)
{
	enum gfarm_auth_id_type auth_user_id_type = *auth_user_id_typep;
	char *global_username;
	struct user *u;
	const char diag[] = "auth_uid_to_global_username_sharedsecret";

	if (auth_user_id_type != GFARM_AUTH_ID_TYPE_USER)
		return (GFARM_ERR_AUTHENTICATION);

	giant_lock();
	u = user_lookup(auth_user_id);
	giant_unlock();

	if (u == NULL) {
		/*
		 * do not return GFARM_ERR_NO_SUCH_USER
		 * to prevent information leak
		 */
		gflog_info(GFARM_MSG_UNFIXED,
		    "unknown user id <%s>", auth_user_id);
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

/*
 * if auth_method is gsi*, kerberos* or tls_client_certificate,
 * gfarm_auth_id_type is GFARM_AUTH_ID_TYPE_{USER,SPOOL_HOST,METADATA_HOST},
 * otherwise gfarm_auth_id_type must be GFARM_AUTH_ID_TYPE_USER.
 *
 * if auth_method is gsi*,
 * *gfarm_auth_id_typep is an output parameter, otherwise an input parameter.
 */
gfarm_error_t
auth_uid_to_global_username(void *closure,
	enum gfarm_auth_method auth_method,
	const char *auth_user_id,
	enum gfarm_auth_id_type *auth_user_id_typep,
	char **global_usernamep)
{
	assert(GFARM_ARRAY_LENGTH(auth_uid_to_global_username_table)
	    == GFARM_AUTH_METHOD_NUMBER);

	if (auth_method < GFARM_AUTH_METHOD_NONE ||
	    auth_method >=
	    GFARM_ARRAY_LENGTH(auth_uid_to_global_username_table)) {
		gflog_error(GFARM_MSG_UNFIXED,
		    "auth_uid_to_global_username: method=%d/%d",
		    auth_method,
		    (int)GFARM_ARRAY_LENGTH(
		    auth_uid_to_global_username_table));
		return (gfarm_auth_uid_to_global_username_panic(closure,
		    auth_user_id, auth_user_id_typep, global_usernamep));
	}
	return (auth_uid_to_global_username_table[auth_method](
	    closure, auth_user_id, auth_user_id_typep, global_usernamep));
}

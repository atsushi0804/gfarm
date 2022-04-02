#include <gfarm/gfarm_config.h>

#ifdef HAVE_TLS_1_3

#define IN_TLS_CORE
#undef TLS_TEST

#include "tls_headers.h"
#include "tls_instances.h"
#include "tls_funcs.h"
#include "io_fd.h"

/*
 * Gfarm iobuffer iops
 */

/*
 * close
 */
static gfarm_error_t
tls_iobufop_close(void *cookie, int fd)
{
	gfarm_error_t ret = GFARM_ERR_UNKNOWN;
	int st = -1;
	struct tls_session_ctx_struct *ctx =
		(struct tls_session_ctx_struct *)cookie;

	ret = tls_session_shutdown(ctx);
	tls_session_destroy_ctx(ctx);
	errno = 0;
	st = close(fd);
	if (st != 0) {
		ret = gfarm_errno_to_error(errno);
	}

	return (ret);
}

/*
 * shutdown
 */
static gfarm_error_t
tls_iobufop_shutdown(void *cookie, int fd)
{
	gfarm_error_t ret = GFARM_ERR_UNKNOWN;
	int st = -1;
	struct tls_session_ctx_struct *ctx =
		(struct tls_session_ctx_struct *)cookie;

	ret = tls_session_shutdown(ctx);
	errno = 0;
	st = shutdown(fd, SHUT_RDWR);
	if (st != 0) {
		ret = gfarm_errno_to_error(errno);
	}

	return (ret);
}

static int
tls_iobufop_recv_is_ready(void *cookie)
{
	int ret = -1;

	return (tls_session_get_pending_read_bytes_n(cookie, &ret) ==
	    GFARM_ERR_NO_ERROR && ret > 0);
}

/*
 * read(2) with timeout
 */
static int
tls_iobufop_timeout_read(struct gfarm_iobuffer *b,
	void *cookie, int fd, void *buf, int len)
{
	int ret = -1;
	struct tls_session_ctx_struct *ctx =
		(struct tls_session_ctx_struct *)cookie;

	if (likely(ctx != NULL && b != NULL)) {
		gfarm_error_t gfe = tls_session_timeout_read(ctx, fd, buf, len,
					gfarm_ctxp->network_receive_timeout *
						1000 * 1000,
					&ret);
		if (unlikely(gfe != GFARM_ERR_NO_ERROR)) {
			gfarm_iobuffer_set_error(b, gfe);
		}
	} else {
		ret = -1;
		if (b != NULL) {
			gfarm_iobuffer_set_error(b,
				GFARM_ERR_INVALID_ARGUMENT);
		}
	}

	return (ret);
}

/*
 * full-blocking read(2)
 */
static int
tls_iobufop_full_blocking_read(struct gfarm_iobuffer *b,
	void *cookie, int fd, void *buf, int len)
{
	int ret = -1;
	struct tls_session_ctx_struct *ctx =
		(struct tls_session_ctx_struct *)cookie;

	if (likely(ctx != NULL && b != NULL)) {
		gfarm_error_t gfe = tls_session_timeout_read(ctx, fd, buf, len,
					-1, &ret);
		if (unlikely(gfe != GFARM_ERR_NO_ERROR)) {
			gfarm_iobuffer_set_error(b, gfe);
		}
	} else {
		ret = -1;
		if (b != NULL) {
			gfarm_iobuffer_set_error(b,
				GFARM_ERR_INVALID_ARGUMENT);
		}
	}

	return (ret);
}

/*
 * write(2) with timeout
 */
static int
tls_iobufop_timeout_write(struct gfarm_iobuffer *b,
	void *cookie, int fd, void *buf, int len)
{
	int ret = -1;
	struct tls_session_ctx_struct *ctx =
		(struct tls_session_ctx_struct *)cookie;

	if (likely(ctx != NULL && b != NULL)) {
		gfarm_error_t gfe;
		int timeout = gfarm_ctxp->network_send_timeout;

		if (timeout == 0)
			timeout = -1; /* inifinite */
		else
			timeout *= 1000 * 1000;
		gfe = tls_session_timeout_write(ctx, fd, buf, len, timeout,
						&ret);
		if (unlikely(gfe != GFARM_ERR_NO_ERROR)) {
			gfarm_iobuffer_set_error(b, gfe);
		}
	} else {
		ret = -1;
		if (b != NULL) {
			gfarm_iobuffer_set_error(b,
				GFARM_ERR_INVALID_ARGUMENT);
		}
	}

	return (ret);
}

/*
 * full-blocking write(2)
 */
static int
tls_iobufop_full_blocking_write(struct gfarm_iobuffer *b,
	void *cookie, int fd, void *buf, int len)
{
	int ret = -1;
	struct tls_session_ctx_struct *ctx =
		(struct tls_session_ctx_struct *)cookie;

	if (likely(ctx != NULL && b != NULL)) {
		gfarm_error_t gfe = tls_session_timeout_write(
					ctx, fd, buf, len, -1, &ret);
		if (unlikely(gfe != GFARM_ERR_NO_ERROR)) {
			gfarm_iobuffer_set_error(b, gfe);
		}
	} else {
		ret = -1;
		if (b != NULL) {
			gfarm_iobuffer_set_error(b,
				GFARM_ERR_INVALID_ARGUMENT);
		}
	}

	return (ret);
}


/*
 * iobuffer ops table
 */
static struct gfp_iobuffer_ops gfp_xdr_tls_iobuf_ops = {
	tls_iobufop_close,
	tls_iobufop_shutdown,
	tls_iobufop_recv_is_ready,
	tls_iobufop_timeout_read,
	tls_iobufop_full_blocking_read,
	tls_iobufop_timeout_write,
	tls_iobufop_full_blocking_write,
};

/*
 * Gfarm internal APIs
 */

/*
 * An SSL_CTX/SSL constructor
 */
gfarm_error_t
gfp_xdr_tls_alloc(struct gfp_xdr *conn,	int fd, int flags)
{
	gfarm_error_t ret;

	struct tls_session_ctx_struct *ctx = NULL;
	bool do_mutual_auth =
		(flags & GFP_XDR_TLS_CLIENT_AUTHENTICATION) != 0;
	enum tls_role role =
		(GFP_XDR_TLS_ROLE_IS_INITIATOR(flags)) ?
		TLS_ROLE_INITIATOR : TLS_ROLE_ACCEPTOR;
	bool use_proxy_cert =
		flags & GFP_XDR_TLS_CLIENT_USE_PROXY_CERTIFICATE;

	ret = tls_session_create_ctx(&ctx, role,
		do_mutual_auth, use_proxy_cert);
	if (likely(ret == GFARM_ERR_NO_ERROR && ctx != NULL)) {
		ret = tls_session_establish(ctx, fd);
		if (likely(ret == GFARM_ERR_NO_ERROR)) {
			gfp_xdr_set(conn, &gfp_xdr_tls_iobuf_ops,
				ctx, fd);
		} else {
			tls_session_destroy_ctx(ctx);
		}
	}

	return (ret);
}

/*
 * An SSL destructor
 */
void
gfp_xdr_tls_reset(struct gfp_xdr *conn)
{
	struct tls_session_ctx_struct *ctx = gfp_xdr_cookie(conn);

	(void)tls_session_shutdown(ctx);
	tls_session_destroy_ctx(ctx);

	gfp_xdr_set(conn, &gfp_xdr_tls_iobuf_ops, NULL, -1);
}

char *
gfp_xdr_tls_peer_dn_rfc2253(struct gfp_xdr *conn)
{
	return (tls_session_peer_subjectdn_rfc2253(
			((struct tls_session_ctx_struct *)
			(gfp_xdr_cookie(conn)))));
}

char *
gfp_xdr_tls_peer_dn_gsi(struct gfp_xdr *conn)
{
	return (tls_session_peer_subjectdn_gsi(
			((struct tls_session_ctx_struct *)
			(gfp_xdr_cookie(conn)))));
}

char *
gfp_xdr_tls_peer_dn_common_name(struct gfp_xdr *conn)
{
	return (tls_session_peer_cn(
			((struct tls_session_ctx_struct *)
			(gfp_xdr_cookie(conn)))));
}

/*
 * for "sasl_auth" method
 */

static struct gfp_iobuffer_ops gfp_xdr_insecure_tls_session_iobuffer_ops = {
	tls_iobufop_close,
	tls_iobufop_shutdown,
	tls_iobufop_recv_is_ready,
	/* NOTE: the following assumes that these functions don't use cookie */
	gfarm_iobuffer_blocking_read_timeout_fd_op,
	gfarm_iobuffer_blocking_read_notimeout_fd_op,
	gfarm_iobuffer_blocking_write_timeout_socket_op,
	gfarm_iobuffer_blocking_write_notimeout_socket_op,
};

/*
 * downgrade from a TLS connection which is created by gfp_xdr_tls_alloc()
 * to a fd connection.
 */

void
gfp_xdr_downgrade_from_tls_to_fd(struct gfp_xdr *conn)
{
	gfp_xdr_set(conn, &gfp_xdr_insecure_tls_session_iobuffer_ops,
	    gfp_xdr_cookie(conn), gfp_xdr_fd(conn));
}

#endif /* HAVE_TLS_1_3 */

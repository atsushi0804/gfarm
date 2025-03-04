struct gfarm_iobuffer;

struct gfp_iobuffer_ops {
	gfarm_error_t (*close)(void *, int);
	gfarm_error_t (*shutdown)(void *, int);
	gfarm_error_t (*export_credential)(void *);
	gfarm_error_t (*delete_credential)(void *, int);
	char *(*env_for_credential)(void *);
	int (*blocking_read_timeout)(struct gfarm_iobuffer *, void *, int,
	    void *, int);
	int (*blocking_read_notimeout)(struct gfarm_iobuffer *, void *, int,
	    void *, int);
	int (*blocking_write_timeout)(struct gfarm_iobuffer *, void *, int,
	    void *, int);
	int (*blocking_write_notimeout)(struct gfarm_iobuffer *, void *, int,
	    void *, int);
};

#define GFP_XDR_NEW_RECV		1
#define GFP_XDR_NEW_SEND		2
#define GFP_XDR_NEW_AUTO_RECV_EXPANSION	4

struct gfp_xdr;

#define IS_CONNECTION_ERROR(e) \
	((e) == GFARM_ERR_BROKEN_PIPE || (e) == GFARM_ERR_UNEXPECTED_EOF || \
	 (e) == GFARM_ERR_PROTOCOL || \
	 (e) == GFARM_ERR_CANNOT_ASSIGN_REQUESTED_ADDRESS || \
	 (e) == GFARM_ERR_NETWORK_IS_DOWN || \
	 (e) == GFARM_ERR_NETWORK_IS_UNREACHABLE || \
	 (e) == GFARM_ERR_CONNECTION_ABORTED || \
	 (e) == GFARM_ERR_CONNECTION_RESET_BY_PEER || \
	 (e) == GFARM_ERR_NO_BUFFER_SPACE_AVAILABLE || \
	 (e) == GFARM_ERR_SOCKET_IS_NOT_CONNECTED || \
	 (e) == GFARM_ERR_OPERATION_TIMED_OUT || \
	 (e) == GFARM_ERR_CONNECTION_REFUSED || \
	 (e) == GFARM_ERR_NO_ROUTE_TO_HOST)

gfarm_error_t gfp_xdr_new(struct gfp_iobuffer_ops *, void *, int, int,
	struct gfp_xdr **);
gfarm_error_t gfp_xdr_free(struct gfp_xdr *);

void *gfp_xdr_cookie(struct gfp_xdr *);
int gfp_xdr_fd(struct gfp_xdr *);
int gfp_xdr_read_fd(struct gfp_xdr *);
gfarm_error_t gfp_xdr_sendbuffer_check_size(struct gfp_xdr *, int);
void gfp_xdr_recvbuffer_clear_read_eof(struct gfp_xdr *);
void gfp_xdr_set(struct gfp_xdr *,
	struct gfp_iobuffer_ops *, void *, int);

gfarm_error_t gfp_xdr_shutdown(struct gfp_xdr *);
gfarm_error_t gfp_xdr_export_credential(struct gfp_xdr *);
gfarm_error_t gfp_xdr_delete_credential(struct gfp_xdr *, int);
char *gfp_xdr_env_for_credential(struct gfp_xdr *);

void gfarm_iobuffer_set_nonblocking_read_xxx(struct gfarm_iobuffer *,
	struct gfp_xdr *);
void gfarm_iobuffer_set_nonblocking_write_xxx(struct gfarm_iobuffer *,
	struct gfp_xdr *);

int gfp_xdr_recv_is_ready(struct gfp_xdr *);
int gfp_xdr_is_empty(struct gfp_xdr *);
gfarm_error_t gfp_xdr_flush(struct gfp_xdr *);
gfarm_error_t gfp_xdr_flush_notimeout(struct gfp_xdr *);
gfarm_error_t gfp_xdr_purge(struct gfp_xdr *, int, int);
void gfp_xdr_purge_all(struct gfp_xdr *);
gfarm_error_t gfp_xdr_vsend_size_add(size_t *, const char **, va_list *);
gfarm_error_t gfp_xdr_vsend(struct gfp_xdr *, int,
	const char **, va_list *);
gfarm_error_t gfp_xdr_vrecv_sized_x(struct gfp_xdr *, int, int, size_t *,
	int *, const char **, va_list *);
gfarm_error_t gfp_xdr_vrecv_sized(struct gfp_xdr *, int, int, size_t *,
	int *, const char **, va_list *);
gfarm_error_t gfp_xdr_vrecv(struct gfp_xdr *, int, int,
	int *, const char **, va_list *);

gfarm_error_t gfp_xdr_send_size_add(size_t *, const char *, ...);
gfarm_error_t gfp_xdr_send(struct gfp_xdr *, const char *, ...);
gfarm_error_t gfp_xdr_send_notimeout(struct gfp_xdr *, const char *, ...);
gfarm_uint32_t gfp_xdr_send_calc_crc32(struct gfp_xdr *, gfarm_uint32_t,
	int, size_t);
gfarm_error_t gfp_xdr_recv_sized(struct gfp_xdr *, int, int, size_t *,
	int *, const char *, ...);
gfarm_error_t gfp_xdr_recv(struct gfp_xdr *, int, int *,
	const char *, ...);
gfarm_error_t gfp_xdr_recv_notimeout(struct gfp_xdr *, int, int *,
	const char *, ...);
gfarm_uint32_t gfp_xdr_recv_calc_crc32(struct gfp_xdr *, gfarm_uint32_t,
	int, size_t);
gfarm_uint32_t gfp_xdr_recv_get_crc32_ahead(struct gfp_xdr *, int);
gfarm_error_t gfp_xdr_recv_ahead(struct gfp_xdr *, int, size_t *);
gfarm_error_t gfp_xdr_vrpc_request(struct gfp_xdr *, gfarm_int32_t,
	const char **, va_list *);
gfarm_error_t gfp_xdr_vrpc_request_notimeout(struct gfp_xdr *, gfarm_int32_t,
	const char **, va_list *);
gfarm_error_t gfp_xdr_vrpc_result_sized(struct gfp_xdr *, int, size_t *,
	gfarm_int32_t *, const char **, va_list *);
gfarm_error_t gfp_xdr_vrpc_result(struct gfp_xdr *, int, int,
	gfarm_int32_t *, const char **, va_list *);
gfarm_error_t gfp_xdr_vrpc(struct gfp_xdr *, int, int,
	gfarm_int32_t, gfarm_int32_t *, const char **, va_list *);

gfarm_error_t gfp_xdr_recv_partial(struct gfp_xdr *, int, void *, int, int *);
gfarm_error_t gfp_xdr_recv_get_error(struct gfp_xdr *);


/* asynchronous RPC related functions */
struct gfp_xdr_async_peer;
typedef struct gfp_xdr_async_peer *gfp_xdr_async_peer_t;
enum gfp_xdr_msg_type { GFP_XDR_TYPE_REQUEST, GFP_XDR_TYPE_RESULT };
typedef gfarm_int32_t gfp_xdr_xid_t; /* transaction ID */
typedef gfarm_error_t (*result_callback_t)(void *, void *, size_t);
typedef void (*disconnect_callback_t)(void *, void *);

gfarm_error_t gfp_xdr_async_peer_new(gfp_xdr_async_peer_t *);
void gfp_xdr_async_peer_free(gfp_xdr_async_peer_t, void *);

gfarm_error_t gfp_xdr_callback_async_result(gfp_xdr_async_peer_t,
	void *, gfp_xdr_xid_t, size_t, gfarm_int32_t *);
gfarm_error_t gfp_xdr_vsend_async_request_notimeout(struct gfp_xdr *,
	gfp_xdr_async_peer_t,
	result_callback_t, disconnect_callback_t, void *,
	gfarm_int32_t, const char *, va_list *);
gfarm_error_t gfp_xdr_recv_async_header(struct gfp_xdr *, int,
	enum gfp_xdr_msg_type *, gfp_xdr_xid_t *, size_t *);

gfarm_error_t gfp_xdr_recv_request_command(struct gfp_xdr *, int, size_t *,
	gfarm_int32_t *);
gfarm_error_t gfp_xdr_vrecv_request_parameters(struct gfp_xdr *, int, size_t *,
	const char *, va_list *);
gfarm_error_t gfp_xdr_vsend_result(struct gfp_xdr *, int,
	gfarm_int32_t, const char *, va_list *);
gfarm_error_t gfp_xdr_vsend_async_result_notimeout(struct gfp_xdr *,
	gfp_xdr_xid_t, gfarm_int32_t, const char *, va_list *);
void gfp_xdr_begin_sendbuffer_pindown(struct gfp_xdr *);
void gfp_xdr_end_sendbuffer_pindown(struct gfp_xdr *);


/*
 * rpc format string mnemonic:
 *
 *	c	gfarm_[u]int8_t
 *	h	gfarm_[u]int16_t
 *	i	gfarm_[u]int32_t
 *	l	gfarm_[u]int64_t
 *	s	char * (on network: gfarm_int32_t, gfarm_int8_t[])
 *	b	fixed size buffer
 *		request: size_t, char *
 *		result:  size_t, size_t *, char *
 *		(on network: gfarm_int32_t, gfarm_int8_t[])
 *
 * (all integers are transfered as big endian on network)
 */

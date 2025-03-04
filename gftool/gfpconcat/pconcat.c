/*
 * $Id$
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <assert.h>

#include <gfarm/gfarm.h>

#include "gfutil.h"

#include "context.h"
#include "gfs_pio.h"

#include "gfurl.h"
#include "gfmsg.h"

#include "gfpconcat.h"

static void
gfpconcat_debug_print_options(struct gfpconcat_option *opt)
{
	gfmsg_debug("-c = %d", opt->compare);
	gfmsg_debug("-f = %d", opt->force);
	gfmsg_debug("-h = %s", opt->dst_host);
	gfmsg_debug("-i = %s", opt->input_list);
	gfmsg_debug("-j = %d", opt->n_para);
	gfmsg_debug("-m = %lld", (long long)opt->minimum_size);
	gfmsg_debug("-o = %s", opt->out_file);
	gfmsg_debug("-p = %d", opt->performance);
	gfmsg_debug("-q = %d", opt->quiet);
	gfmsg_debug("-v = %d", opt->verbose);
	gfmsg_debug("-d = %d", opt->debug);
	gfmsg_debug("-t = %d", opt->test);
}

struct gfpconcat_part {
	GFURL url;
	off_t size;
};

static void
free_part_list(struct gfpconcat_part *part_list, int n_part)
{
	int i;

	for (i = 0; i < n_part; i++) {
		struct gfpconcat_part *p = &part_list[i];

		gfurl_free(p->url);
	}
	free(part_list);
}

static void
free_option(struct gfpconcat_option *opt)
{
	free_part_list(opt->part_list, opt->n_part);
	gfurl_free(opt->out_url);
	gfurl_free(opt->tmp_url);
}

struct gfpconcat_range {
	off_t offset;
	off_t size;
	int pattern;
};

static void
gfpconcat_get_range(off_t assigned_offset, off_t assigned_size,
    off_t part_offset, off_t part_size, struct gfpconcat_range *range)
{
	off_t assigned_end = assigned_offset + assigned_size - 1;
	off_t part_end = part_offset + part_size - 1;

	if (part_offset < assigned_offset) {
		if (part_end < assigned_offset) {
			/* PAT 0 : out of range */
			range->pattern = 0;
			range->offset = 0;
			range->size = 0;
		} else {  /* assigned_offset <= part_end */
			range->offset = assigned_offset;
			if (part_end < assigned_end) {
				/* PAT 2 : left assigned */
				range->pattern = 2;
				/* ex. 5 = 14 - 10 + 1 */
				range->size = part_end - assigned_offset + 1;
			} else {  /*  assigned_end <= part_end */
				/* PAT 5 : full assigned (1) */
				range->pattern = 5;
				range->size = assigned_size;
			}
		}
	} else {  /* assigned_offset <= part_offset */
		if (part_end <= assigned_end) {
			/* PAT 4 : full part */
			range->pattern = 4;
			range->offset = part_offset;
			range->size = part_size;
		} else {  /* assigned_end < part_end */
			if (assigned_offset == part_offset) {
				/* PAT 6 : full assigned (2) */
				range->pattern = 6;
				range->offset = part_offset;
				range->size = assigned_size;
			} else if (part_offset <= assigned_end) {
				/* PAT 3 : right assigned */
				range->pattern = 3;
				range->offset = part_offset;
				/* ex. 5 = 10 - (24 - 19) */
				range->size = part_size
				    - (part_end - assigned_end);
			} else {  /* assigned_end < part_offset */
				/* PAT 1 : out of range */
				range->pattern = 1;
				range->offset = 0;
				range->size = 0;
			}
		}
	}
}

struct range_pattern {
	int expect_pattern;
	off_t assigned_offset;
	off_t assigned_size;
	off_t part_offset;
	off_t part_size;
	off_t expect_offset;
	off_t expect_size;
};

static int
gfpconcat_get_range_test(void)
{
	struct range_pattern patterns[] = {
		{ 0, 10, 10, 0, 10, 0, 0 },	/* PAT 0 : out of range */
		{ 1, 10, 10, 20, 10, 0, 0 },	/* PAT 1 : out of range */
		{ 2, 10, 10, 5, 10, 10, 5 },	/* PAT 2 : left assigned */
		{ 3, 10, 10, 15, 10, 15, 5 },	/* PAT 3 : right assigned */
		{ 4, 10, 10, 11, 5, 11, 5 },	/* PAT 4 : full part */
		{ 5, 10, 10, 5, 20, 10, 10 },	/* PAT 5 : full assigned (1) */
		{ 6, 10, 10, 10, 11, 10, 10 },	/* PAT 6 : full assigned (2) */

		/* boundary */
		{ 0, 1, 1, 0, 1, 0, 0 },
		{ 1, 1, 1, 2, 1, 0, 0 },
		{ 2, 1, 2, 0, 2, 1, 1 },
		{ 3, 1, 2, 2, 2, 2, 1 },
		{ 4, 1, 2, 1, 1, 1, 1 },
		{ 5, 1, 2, 0, 3, 1, 2 },
		{ 6, 1, 2, 1, 3, 1, 2 },

		/* example */
		{ 6, 2110911, 1, 2110911, 1048937, 2110911, 1 },
	};
	size_t num = sizeof(patterns) / sizeof(patterns[0]);
	int i;

	gfmsg_debug("gfpconcat_get_range_test: num=%ld", num);
	for (i = 0; i < num; i++) {
		struct range_pattern *p = &patterns[i];
		struct gfpconcat_range range;

		gfpconcat_get_range(p->assigned_offset, p->assigned_size,
		    p->part_offset, p->part_size, &range);
		if (range.pattern != p->expect_pattern
		    || range.offset != p->expect_offset
		    || range.size != p->expect_size) {
			gfmsg_error("gfpconcat_get_range_test[%d]: "
			   "range.pattern=%d, p->expect_pattern=%d, "
			   "range.offset=%lld, p->expect_offset=%lld, "
			   "range.size=%lld, p->expect_size=%lld\n", i,
			    range.pattern, p->expect_pattern,
			    (long long)range.offset,
			    (long long)p->expect_offset,
			    (long long)range.size,
			    (long long)p->expect_size);
			return (1);
		}
		gfmsg_info("gfpconcat_get_range_test[%d]: PASS", i);
	}

	return (0);
}

struct gfpconcat_file {
	GFURL url;
	int fd;
	GFS_File gf;
};

static gfarm_error_t
gfpconcat_open(GFURL url, int flags, int mode, struct gfpconcat_file *fp)
{
	if (gfurl_is_local(url)) {
		fp->fd = open(gfurl_epath(url), flags, mode);
		if (fp->fd == -1) {
			return (gfarm_errno_to_error(errno));
		}
		fp->gf = NULL;
	} else if (gfurl_is_gfarm(url)) {
		gfarm_error_t e;
		int gflags = 0;

		if (flags & O_RDONLY) {
			gflags |= GFARM_FILE_RDONLY;
		}
		if (flags & O_WRONLY) {
			gflags |= GFARM_FILE_WRONLY;
		}
		if (flags & O_TRUNC) {
			gflags |= GFARM_FILE_TRUNC;
		}
		if (flags & O_EXCL) {
			gflags |= GFARM_FILE_EXCLUSIVE;
		}
		if (flags & O_CREAT) {
			e = gfs_pio_create(gfurl_epath(url), gflags,
			    mode, &fp->gf);
		} else {
			e = gfs_pio_open(gfurl_epath(url), gflags, &fp->gf);
		}
		if (e != GFARM_ERR_NO_ERROR) {
			return (e);
		}
		fp->fd = 0;
	} else {
		return (GFARM_ERR_INVALID_ARGUMENT);
	}

	fp->url = url;

	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfpconcat_seek(struct gfpconcat_file *fp, off_t offset, int whence)
{
	gfarm_error_t e;
	int rv, gwhence;

	if (fp->gf == NULL) {
		rv = lseek(fp->fd, offset, whence);
		if (rv == -1) {
			return (gfarm_errno_to_error(errno));
		}
		return (GFARM_ERR_NO_ERROR);
	}

	switch (whence) {
	case SEEK_SET:
		gwhence = GFARM_SEEK_SET;
		break;
	case SEEK_CUR:
		gwhence = GFARM_SEEK_CUR;
		break;
	case SEEK_END:
		gwhence = GFARM_SEEK_END;
		break;
	default:
		return (GFARM_ERR_INVALID_ARGUMENT);
	}
	e = gfs_pio_seek(fp->gf, offset, gwhence, NULL);

	return (e);
}

static gfarm_error_t
gfpconcat_read(struct gfpconcat_file *fp, void *buf, int bufsize, int *rsize)
{
	int len;
	char *b = buf;

	if (fp->gf) {
		return (gfs_pio_read(fp->gf, buf, bufsize, rsize));
	}
	*rsize = 0;
	while ((len = read(fp->fd, b, bufsize)) > 0) {
		if (len == bufsize) {
			break;
		}
		b += len;
		bufsize -= len;
		*rsize += len;
	}
	if (len == -1) {
		return (gfarm_errno_to_error(errno));
	}
	*rsize += len;
	/* *rsize == 0: EOF */

	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfpconcat_write(struct gfpconcat_file *fp, void *buf, int bufsize, int *wsize)
{
	int len;
	char *b = buf;

	if (fp->gf) {
		return (gfs_pio_write(fp->gf, buf, bufsize, wsize));
	}
	*wsize = 0;
	while ((len = write(fp->fd, b, bufsize)) > 0) {
		if (len == bufsize) {
			break;
		}
		b += len;
		bufsize -= len;
		*wsize += len;
	}
	if (len == 0) {
		return (GFARM_ERR_NO_SPACE);
	} else if (len == -1) {
		return (gfarm_errno_to_error(errno));
	}
	*wsize += len;

	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfpconcat_close(struct gfpconcat_file *fp)
{
	int retv;

	if (fp->gf) {
		return (gfs_pio_close(fp->gf));
	}
	retv = close(fp->fd);
	if (retv == -1) {
		return (gfarm_errno_to_error(errno));
	}

	return (GFARM_ERR_NO_ERROR);
}

#define GFS_FILE_BUFSIZE 65536

static gfarm_error_t
copy_io0(struct gfpconcat_file *src_fp, off_t src_offset,
    struct gfpconcat_file *dst_fp, off_t dst_offset, off_t len)
{
	gfarm_error_t e;
	int rsize, wsize;
	char buf[GFS_FILE_BUFSIZE];
	size_t bufsize = sizeof(buf);

	/* 'gfarm -> gfarm' or 'local -> local' */
	while ((e = gfpconcat_read(src_fp, buf, bufsize, &rsize))
	    == GFARM_ERR_NO_ERROR) {
		if (rsize == 0) {  /* EOF */
			return (GFARM_ERR_NO_ERROR);
		}
		e = gfpconcat_write(dst_fp, buf, rsize, &wsize);
		if (e != GFARM_ERR_NO_ERROR) {
			gfmsg_error_e(e, "%s: write", gfurl_url(dst_fp->url));
			return (e);
		}
		if (rsize != wsize) {
			gfmsg_error("%s: write: rsize!=wsize",
			    gfurl_url(dst_fp->url));
			return (GFARM_ERR_INPUT_OUTPUT);
		}
	}
	gfmsg_error_e(e, "%s: read", gfurl_url(src_fp->url));

	return (e);
}

static gfarm_error_t
gfpconcat_copy_io(struct gfpconcat_file *src_fp, off_t src_offset,
    struct gfpconcat_file *dst_fp, off_t dst_offset, off_t len)
{
	gfarm_error_t e;
	off_t result_len;

	e = gfpconcat_seek(src_fp, src_offset, SEEK_SET);
	if (e != GFARM_ERR_NO_ERROR) {
		gfmsg_error_e(e, "%s: seek", gfurl_url(src_fp->url));
		return (e);
	}
	e = gfpconcat_seek(dst_fp, dst_offset, SEEK_SET);
	if (e != GFARM_ERR_NO_ERROR) {
		gfmsg_error_e(e, "%s: seek", gfurl_url(dst_fp->url));
		return (e);
	}

	/* seek required */
	if (src_fp->gf != NULL && dst_fp->gf == NULL) {
		/* gfarm -> local */
		e = gfs_pio_recvfile(src_fp->gf, src_offset,
		    dst_fp->fd, dst_offset, len, &result_len);
		if (e != GFARM_ERR_NO_ERROR) {
			gfmsg_error_e(e, "%s: gfs_pio_recvfile",
			    gfurl_url(src_fp->url));
		}
		gfmsg_debug("recvfile: len=%lld, result_len=%lld",
		    (long long)len, (long long)result_len);
		return (e);
	} else if (src_fp->gf == NULL && dst_fp->gf != NULL) {
		/* local -> gfarm */
		e = gfs_pio_sendfile(dst_fp->gf, dst_offset,
		    src_fp->fd, src_offset, len, &result_len);
		if (e != GFARM_ERR_NO_ERROR) {
			gfmsg_error_e(e, "%s: gfs_pio_sendfile",
			    gfurl_url(dst_fp->url));
		}
		gfmsg_debug("sendfile: len=%lld, result_len=%lld",
		    (long long)len, (long long)result_len);
		return (e);
	}

	return (copy_io0(src_fp, src_offset, dst_fp, dst_offset, len));
}

static void
gfpconcat_gfarm_initialize(struct gfpconcat_option *opt)
{
	gfarm_error_t e;

	if (!opt->gfarm_initialized) {
		e = gfarm_initialize(NULL, NULL);
		if (e == GFARM_ERR_NO_ERROR) {
			opt->gfarm_initialized = 1;
		} else {
			gfmsg_fatal_e(e, "gfarm_initialize");
		}
	}
}

static void
gfpconcat_gfarm_terminate(struct gfpconcat_option *opt)
{
	gfarm_error_t e;

	if (opt->gfarm_initialized) {
		e = gfarm_terminate();
		if (e == GFARM_ERR_NO_ERROR) {
			opt->gfarm_initialized = 0;
		} else {
			gfmsg_warn_e(e, "gfarm_terminate");
		}
	}
}

static gfarm_error_t
gfpconcat_create_empty_file(GFURL url, int mode)
{
	gfarm_error_t e;
	struct gfpconcat_file fp;

	e = gfpconcat_open(url, O_CREAT | O_WRONLY | O_TRUNC,
	    mode & 0777 & ~0022, &fp);
	if (e == GFARM_ERR_NO_ERROR) {
		e = gfpconcat_close(&fp);
	}
	return (e);
}

static int
gfpconcat_child_copy_parts(struct gfpconcat_option *opt, int child_id)
{
	gfarm_error_t e, e2;
	int i;
	off_t assigned_offset, assigned_size, assigned_size_modulo;
	off_t part_offset, current_offset, remain_size;
	struct gfpconcat_file dst_fp;

	/* child_id: 1, 2, 3, ... */
	if (child_id <= 0) {
		gfmsg_fatal("child_id <= 0");
	}

	assigned_size = opt->total_size / opt->n_para;
	assigned_size_modulo = opt->total_size % opt->n_para;

	assigned_offset = assigned_size * (child_id - 1);
	/*
	 * ex. total_size==18, n_para==5
	 * assigned_size_modulo = 18 % 5 = 3
	 * assigned_size:
	 *   child_id==1 : assigned_offset +=0, assigned_size += 1
	 *   child_id==2 : assigned_offset +=1, assigned_size += 1
	 *   child_id==3 : assigned_offset +=2, assigned_size += 1
	 *   child_id==4 : assigned_offset +=3, assigned_size += 0
	 *   child_id==5 : assigned_offset +=3, assigned_size += 0
	 */
	if (child_id <= assigned_size_modulo) {
		assigned_size += 1;
		assigned_offset += child_id - 1;
	} else {
		assigned_offset += assigned_size_modulo;
	}
	if (assigned_size <= 0) {
		gfmsg_debug("no assigned part");
		e = GFARM_ERR_NO_ERROR;
		goto terminate;
	}

	gfmsg_debug("child_id[%d/%d] assigned offset %lld ... %lld",
	    child_id, opt->n_para,
	    (long long)assigned_offset,
	    (long long)(assigned_offset + assigned_size - 1));

	if (gfurl_is_gfarm(opt->tmp_url)) {
		gfpconcat_gfarm_initialize(opt);

		if (opt->n_para >= 2) {
			/* disable client_digest_check */
			gfarm_set_client_digest_check(0);
			gfmsg_debug("disable client_digest_check");
		}
	}

	/* Do not use O_TRUNC */
	e = gfpconcat_open(opt->tmp_url, O_CREAT | O_WRONLY,
	    opt->mode & 0777 & ~0022, &dst_fp);
	if (e != GFARM_ERR_NO_ERROR) {
		gfmsg_error_e(e, "%s: open", gfurl_url(opt->tmp_url));
		goto terminate;
	}
	if (opt->dst_host != NULL && dst_fp.gf != NULL) {
retry_set_view:
		/* XXX FIXME: INTERNAL FUNCTION SHOULD NOT BE USED */
		e = gfs_pio_internal_set_view_section(dst_fp.gf,
		    opt->dst_host);
		if (e == GFARM_ERR_RESOURCE_TEMPORARILY_UNAVAILABLE) {
			/* XXX FIXME: retry in set_view_section ? */
			gfmsg_info("%s: retry to set host=%s",
			    gfurl_url(opt->tmp_url), opt->dst_host);
			gfs_pio_clearerr(dst_fp.gf);
			goto retry_set_view;
		} else if (e != GFARM_ERR_NO_ERROR) {
			gfmsg_warn_e(e, "%s: set host=%s",
			    gfurl_url(opt->tmp_url), opt->dst_host);
			gfs_pio_clearerr(dst_fp.gf);
		}
	}

	part_offset = 0;
	current_offset = assigned_offset;
	remain_size = assigned_size;
	for (i = 0; i < opt->n_part; i++) {
		struct gfpconcat_part *p = &(opt->part_list[i]);
		struct gfpconcat_range range;

		if (opt->gfarm_initialized == 0
		    && (gfurl_is_gfarm(p->url))) {
			gfpconcat_gfarm_initialize(opt);
		}
		gfpconcat_get_range(assigned_offset, assigned_size,
		    part_offset, p->size, &range);
		if (range.size > 0) { /* This is a my part */
			struct gfpconcat_file src_fp;
			off_t local_offset = range.offset - part_offset;

			assert(local_offset >= 0);
			e = gfpconcat_open(p->url, O_RDONLY, 0, &src_fp);
			if (e != GFARM_ERR_NO_ERROR) {
				gfmsg_error_e(e, "%s: open",
				    gfurl_url(p->url));
				goto close_dst_fp;
			}

			gfmsg_debug("[%d/%d] src_url=%s, "
			    "src_offset=%lld, dst_offset=%lld, len=%lld",
			    child_id, opt->n_para,
			    gfurl_url(src_fp.url),
			    (long long)local_offset,
			    (long long)range.offset,
			    (long long)range.size);
			e = gfpconcat_copy_io(&src_fp, local_offset,
			    &dst_fp, range.offset, range.size);
			e2 = gfpconcat_close(&src_fp);
			gfmsg_error_e(e2, "%s: cannot close",
			    gfurl_url(p->url));
			if (e != GFARM_ERR_NO_ERROR) {
				goto close_dst_fp;
			}
			e = e2;
			if (e != GFARM_ERR_NO_ERROR) {
				goto close_dst_fp;
			}

			current_offset += range.size;
			remain_size -= range.size;
		}
		if (remain_size == 0) {
#if 0
			gfmsg_debug("child_id[%d]: remain_size == 0: break",
			    child_id);
#endif
			break;
		} else if (remain_size < 0) {
			gfmsg_fatal("assert(remain_size == 0)");
		}
		part_offset += p->size;
	}

close_dst_fp:
	e2 = gfpconcat_close(&dst_fp);
	gfmsg_error_e(e2, "%s: cannot close", gfurl_url(opt->tmp_url));
	if (e == GFARM_ERR_NO_ERROR) {
		e = e2;
	}

terminate:
	gfpconcat_gfarm_terminate(opt);

	return (e == GFARM_ERR_NO_ERROR ? 0 : 1);
}

struct gfpconcat_proc {
	pid_t pid;
};

static int
gfpconcat_para_copy_parts(struct gfpconcat_option *opt)
{
	int i, n_procs = opt->n_para;
	int n_error = 0;
	struct gfpconcat_proc *procs;

	assert(opt->gfarm_initialized == 0);

	GFARM_MALLOC_ARRAY(procs, n_procs);

	/* use stderr */
	/* close stdin and stdout */
	for (i = 0; i < n_procs; i++) {
		/* Don't write to stdout and stderr in this loop */
		pid_t pid;

		if ((pid = fork()) == -1) {
			gfmsg_fatal("fork: %s", strerror(errno));
		} else if (pid == 0) {
			int rv;

			free(procs);
			close(0); /* stdin */
			close(1); /* stdout */
			/* line buffered */
			setvbuf(stderr, (char *)NULL, _IOLBF, 0);

			rv = gfpconcat_child_copy_parts(opt, i + 1);

			fflush(stderr);
			close(2);

			if (opt->test) {
				/* cope with "still reachable" by valgrind */
				free_option(opt);
				/* to free OPENSSL_init_crypto at atexit */
				exit(rv);
			} else {
				_exit(rv);
			}
			/* NOTREACHED */
		}
		procs[i].pid = pid;
	}

	for (i = 0; i < n_procs; i++) {
		int rv, wstatus;

		rv = waitpid(procs[i].pid, &wstatus, 0);
		if (rv == -1) {
			gfmsg_error("child_id[%d/%d] waitpid: %s",
			    i + 1, opt->n_para, strerror(errno));
			n_error += 1;
			continue;
		}
		rv = WEXITSTATUS(wstatus);
		if (rv != 0) {
			gfmsg_error("child_id[%d/%d] exit status: %d",
				    i + 1, opt->n_para, rv);
			n_error += 1;
			continue;
		}
	}
	free(procs);

	return (n_error > 0 ? 1 : 0);
}

struct gfpconcat_read_proc {
	pid_t pid;
	int read_fd;
};

static int
gfpconcat_read_a_file(GFURL url, int write_fd)
{
	gfarm_error_t e;
	struct gfpconcat_file fh;
	char buf[GFS_FILE_BUFSIZE];
	size_t bufsize = sizeof(buf);
	int rsize, wsize;
	off_t fsize;

	gfmsg_debug("gfpconcat_read_a_file: start: %s", gfurl_url(url));

	e = gfpconcat_open(url, O_RDONLY, 0, &fh);
	if (e != GFARM_ERR_NO_ERROR) {
		gfmsg_error_e(e, "%s: open", gfurl_url(url));
		return (1);
	}

	if (fh.gf != NULL) {
		/* from gfarm */
		e = gfs_pio_recvfile(fh.gf, 0, write_fd, 0, -1, &fsize);
		if (e != GFARM_ERR_NO_ERROR) {
			gfmsg_error_e(e, "%s: gfs_pio_recvfile",
			    gfurl_url(url));
		}
		gfmsg_debug("gfpconcat_read_a_file: done: %s: size=%lld",
		    gfurl_url(url), (long long)fsize);
		gfpconcat_close(&fh);
		return (e);
	}

	gfmsg_debug("gfpconcat_read_a_file: from local: %s", gfurl_url(url));

	/* from local */
	fsize = 0;
	while ((e = gfpconcat_read(&fh, buf, bufsize, &rsize))
	    == GFARM_ERR_NO_ERROR) {
		if (rsize == 0) {  /* EOF */
			goto end;
		}
		wsize = write(write_fd, buf, rsize);
		if (rsize != wsize) {
			gfmsg_error("%s: write: rsize!=wsize", gfurl_url(url));
			gfpconcat_close(&fh);
			return (2);
		}
		fsize += wsize;
	}
end:
	gfpconcat_close(&fh);
	if (e == GFARM_ERR_NO_ERROR) {
		gfmsg_debug("gfpconcat_read_a_file: done: %s: size=%lld",
		    gfurl_url(url), (long long)fsize);
		return (0);
	} else {
		gfmsg_error_e(e, "%s: read", gfurl_url(url));
	}
	return (3);
}

static int
gfpconcat_read_func_parts(struct gfpconcat_option *opt, int write_fd)
{
	int i, rv = 0;

	for (i = 0; i < opt->n_part; i++) {
		struct gfpconcat_part *p = &(opt->part_list[i]);

		if (opt->gfarm_initialized == 0 && gfurl_is_gfarm(p->url)) {
			gfpconcat_gfarm_initialize(opt);
		}
		rv = gfpconcat_read_a_file(p->url, write_fd);
		if (rv != 0) {
			break;
		}
	}
	gfpconcat_gfarm_terminate(opt);
	return (rv);
}

static int
gfpconcat_read_func_dst(struct gfpconcat_option *opt, int write_fd)
{
	int rv;

	if (gfurl_is_gfarm(opt->out_url)) {
		gfpconcat_gfarm_initialize(opt);
	}
	rv = gfpconcat_read_a_file(opt->out_url, write_fd);
	gfpconcat_gfarm_terminate(opt);

	return (rv);
}

static int
gfpconcat_fork_read(struct gfpconcat_option *opt,
    int (*read_func)(struct gfpconcat_option *opt, int write_fd),
    struct gfpconcat_read_proc *proc)
{
	pid_t pid;
	int pipefds[2];
	int rv;

	if (pipe(pipefds) == -1) {
		gfmsg_fatal("pipe: %s", strerror(errno));
	}

	if ((pid = fork()) == -1) {
		gfmsg_fatal("fork: %s", strerror(errno));
	} else if (pid == 0) {  /* child */
		close(0);
		close(1);
		close(pipefds[0]);
		/* line buffered */
		setvbuf(stderr, (char *)NULL, _IOLBF, 0);
		rv = read_func(opt, pipefds[1]);
		close(pipefds[1]);
		fflush(stderr);
		close(2);

		if (opt->test) {
			free_option(opt);
			exit(rv);
		} else {
			_exit(rv);
		}
		/* NOTREACHED */
	}
	close(pipefds[1]);
	proc->read_fd = pipefds[0];
	proc->pid = pid;

	return (0);
}

/* 0: OK */
static int
gfpconcat_compare(struct gfpconcat_option *opt)
{
	struct gfpconcat_read_proc p1, p2;
	char buf1[GFS_FILE_BUFSIZE], buf2[GFS_FILE_BUFSIZE];
	size_t bufsize = sizeof(buf1);
	int rv1, rv2, result;

	gfpconcat_fork_read(opt, gfpconcat_read_func_parts, &p1);
	gfpconcat_fork_read(opt, gfpconcat_read_func_dst, &p2);

	while ((rv1 = read(p1.read_fd, buf1, bufsize)) >= 0) {
		char *b = buf2;
		int len = 0, remain = rv1;

		rv2 = 0;
		while (remain > 0 && (len = read(p2.read_fd, b, remain)) > 0) {
			b += len;
			rv2 += len;
			remain -= len;
		}
		if (len == -1) {
			gfmsg_error("read p2: %s", strerror(errno));
			result = 1;
			goto end;
		}
		if (rv1 != rv2) {
			gfmsg_error("rv1(%d) != rv2(%d)", rv1, rv2);
			result = 2;
			goto end;
		}
		if (rv1 == 0) { /* EOF */
			break;
		}
		if (memcmp(buf1, buf2, rv1) != 0) {
			gfmsg_error("different");
			result = 3;
			goto end;
		}
	}
	if (rv1 == -1) {
		gfmsg_error("read p1: %s", strerror(errno));
		result = 4;
		goto end;
	}
	result = 0;
end:
	close(p1.read_fd);
	close(p2.read_fd);
	waitpid(p1.pid, NULL, 0);
	waitpid(p2.pid, NULL, 0);

	return (result);
}

/*
 * public functions
 */

#include "liberror.h"	/* only for GFARM_ERRMSG_HOSTNAME_EXPECTED */

gfarm_error_t
gfarm_filelist_read(char *filename,
	int *np, char ***file_names_p, int *error_linep)
{
	gfarm_error_t e;

	/* XXX implement for filelist */
	e = gfarm_hostlist_read(filename, np, file_names_p, error_linep);
	if (e == GFARM_ERRMSG_HOSTNAME_EXPECTED) {
		e = GFARM_ERR_INVALID_ARGUMENT;
	}
	return (e);
}

void
gfpconcat_init(int argc, char **argv, char *program_name,
	void (*usage_func)(int, struct gfpconcat_option *),
	void (*getopt_func)(int, char **, struct gfpconcat_option *),
	struct gfpconcat_option *opt)
{
	enum gfmsg_level msglevel;
	int default_n_para;

	/* set default values */
	opt->compare = 0;		/* -c */
	opt->force = 0;		/* -f */
	opt->dst_host = NULL;		/* -h */
	opt->input_list = NULL;	/* -i */
	opt->n_para = 2;		/* -j */
	opt->minimum_size = 0;		/* -m */
	opt->out_file = NULL;		/* -o */
	opt->performance = 0;		/* -p */
	opt->quiet = 0;		/* -q */
	opt->verbose = 0;		/* -v */
	opt->debug = 0;		/* -d */
	opt->test = 0;			/* -t */

	/* initialize parameters */
	opt->argc = argc;
	opt->argv = argv;
	opt->program_name = program_name;
	opt->usage_func = usage_func;
	opt->tmp_url = NULL;
	opt->out_url = NULL;
	opt->out_ino = 0;
	opt->out_exist = 0;
	opt->part_list = NULL;
	opt->n_part = 0;
	opt->mode = 0;
	opt->total_size = 0;
	opt->gfarm_initialized = 0;

	if (opt->argc > 0) {
		opt->program_name = basename(argv[0]);
	}

	gfpconcat_gfarm_initialize(opt);

	/* default -j */
	default_n_para = gfarm_ctxp->client_parallel_copy;
	if (default_n_para > gfarm_ctxp->client_parallel_max) {
		default_n_para = gfarm_ctxp->client_parallel_max;
	}
	opt->n_para = default_n_para;

	getopt_func(argc, argv, opt);

	if (opt->debug) {
		opt->quiet = 0;
		opt->verbose = 1;
		msglevel = GFMSG_LEVEL_DEBUG;
	} else if (opt->verbose) {
		opt->quiet = 0;
		msglevel = GFMSG_LEVEL_INFO;
	} else if (opt->quiet) {
		msglevel = GFMSG_LEVEL_ERROR;
	} else { /* default */
		msglevel = GFMSG_LEVEL_WARNING;
	}

	gfmsg_init(program_name, msglevel);
	if (opt->debug) {
		gfpconcat_debug_print_options(opt);
	}

	if (opt->test) {
		/* for regress */
		if (gfpconcat_get_range_test() != 0) {
			gfmsg_fatal("gfpconcat_get_range_test");
		}
	}
}

int
gfpconcat_main(struct gfpconcat_option *opt)
{
	gfarm_error_t e;
	struct gfurl_stat st;
	struct timeval time_start, time_end;
	char *tmp_url_str;
	int i, rv;

	/* line buffered */
	setvbuf(stdout, (char *)NULL, _IOLBF, 0);
	setvbuf(stderr, (char *)NULL, _IOLBF, 0);

	if (opt->out_file == NULL) {
		gfmsg_error("no output file");
		opt->usage_func(1, opt);
		/* NOTREACHED */
	}
	if (opt->n_part <= 0) {
		gfmsg_error("no input file");
		opt->usage_func(1, opt);
		/* NOTREACHED */
	}
	if (opt->n_para > gfarm_ctxp->client_parallel_max) {
		opt->n_para = gfarm_ctxp->client_parallel_max;
		gfmsg_debug("use client_parallel_max = %d as -j",
			    gfarm_ctxp->client_parallel_max);
	}
	if (opt->n_para <= 0) {
		gfmsg_error("client_parallel_copy or -j must be "
			    "a positive interger: %d", opt->n_para);
		exit(EXIT_FAILURE);
	}

	gettimeofday(&time_start, NULL);

	opt->out_url = gfurl_init(opt->out_file);
	gfmsg_nomem_check(opt->out_url);
	gfmsg_debug("output URL: %s", gfurl_url(opt->out_url));
	e = gfurl_lstat(opt->out_url, &st);
	if (e == GFARM_ERR_NO_ERROR) {
		if (opt->force) {
			opt->out_exist = 1;
			opt->out_ino = st.ino;
		} else {
			e = GFARM_ERR_ALREADY_EXISTS;
			gfmsg_error_e(e, "%s", gfurl_url(opt->out_url));
			exit(EXIT_FAILURE);
		}
	} else if (e == GFARM_ERR_NO_SUCH_FILE_OR_DIRECTORY) {
		opt->out_exist = 0;
		opt->out_ino = 0;
	} else {
		gfmsg_error_e(e, "%s", gfurl_url(opt->out_url));
		exit(EXIT_FAILURE);
	}

#define TMP_FILENAME_FORMAT "%s_tmp_%s_"

	tmp_url_str = gfurl_asprintf2(TMP_FILENAME_FORMAT,
	    gfurl_url(opt->out_url), opt->program_name);
	gfmsg_nomem_check(tmp_url_str);

	opt->tmp_url = gfurl_init(tmp_url_str);
	gfmsg_nomem_check(opt->tmp_url);
	free(tmp_url_str);
	gfmsg_debug("tmp URL: %s", gfurl_url(opt->tmp_url));
	e = gfurl_unlink(opt->tmp_url);
	if (e != GFARM_ERR_NO_ERROR
	    && e != GFARM_ERR_NO_SUCH_FILE_OR_DIRECTORY) {
		gfmsg_warn_e(e, "temporary file: %s",
		    gfurl_url(opt->tmp_url));
		exit(EXIT_FAILURE);
	}

	GFARM_MALLOC_ARRAY(opt->part_list, opt->n_part);
	gfmsg_nomem_check(opt->part_list);

	for (i = 0; i < opt->n_part; i++) {
		char *part_path = opt->parts[i];
		struct gfpconcat_part *p = &(opt->part_list[i]);
		int file_type;

		p->url = gfurl_init(part_path); /* realpath()ed */
		gfmsg_nomem_check(p->url);
		e = gfurl_lstat(p->url, &st);
		if (e != GFARM_ERR_NO_ERROR) {
			gfmsg_error_e(e, "input[%d]=%s", i, part_path);
			exit(EXIT_FAILURE);
		}
		file_type = gfurl_stat_file_type(&st);
		/* check regular file */
		if (file_type != GFS_DT_REG) {
			gfmsg_error_e(GFARM_ERR_NOT_A_REGULAR_FILE,
			    "input[%d]=%s", i, part_path);
			exit(EXIT_FAILURE);
		}
		gfmsg_debug("input[%d] %s: size=%lld",
		    i, part_path, (long long)st.size);
		/* check same file */
		if (opt->out_exist && st.ino == opt->out_ino
		    && ((gfurl_is_local(p->url)
			 && gfurl_is_local(opt->out_url))
			|| gfurl_is_same_gfmd(p->url, opt->out_url))) {
			gfmsg_error("'%s' and '%s' are the same file",
			    part_path, opt->out_file);
			exit(EXIT_FAILURE);
		}
		/* XXX TODO: check readable */

		p->size = (off_t)st.size;
		if (i == 0) {  /* use first mode */
			opt->mode = st.mode;
		}

		opt->total_size += p->size;
	}
	gfmsg_debug("total_size = %lld", (long long)opt->total_size);

	if (opt->input_list != NULL) {
		gfarm_strings_free_deeply(opt->n_part, opt->parts);
	}

	if (opt->total_size == 0) {
		e = gfpconcat_create_empty_file(opt->tmp_url, opt->mode);
		if (e != GFARM_ERR_NO_ERROR) {
			gfmsg_error_e(e, "cannot create empty file: %s",
			    gfurl_url(opt->tmp_url));
			rv = 1;
		} else {
			rv = 0;
		}
		goto copied;
	}
	if (opt->n_para == 1
	    || opt->total_size / opt->n_para <= opt->minimum_size) {
		int child_id = 1;

		gfmsg_debug("using single child process");
		opt->n_para = 1;
		rv = gfpconcat_child_copy_parts(opt, child_id);
		goto copied;
	}

	gfpconcat_gfarm_terminate(opt);
	rv = gfpconcat_para_copy_parts(opt);

copied:
	if (opt->performance) {
		gettimeofday(&time_end, NULL);
		gfarm_timeval_sub(&time_end, &time_start);
		printf("total_file_size: %lld bytes\n",
		    (long long)opt->total_size);
		/* Bytes/usec == MiB/sec */
		printf("total_throughput: %.6f MiB/s\n",
		    (double)opt->total_size /
		    ((double)time_end.tv_sec * GFARM_SECOND_BY_MICROSEC
		     + time_end.tv_usec));
		printf("total_time: %lld.%06d sec.\n",
		    (long long)time_end.tv_sec, (int)time_end.tv_usec);
	}

	if (gfurl_is_gfarm(opt->tmp_url)) {
		gfpconcat_gfarm_initialize(opt);
	}
	if (rv != 0) { /* failed */
		gfmsg_debug("unlink: %s", gfurl_url(opt->tmp_url));
		e = gfurl_unlink(opt->tmp_url);
		if (e != GFARM_ERR_NO_ERROR) {
			gfmsg_warn("cannot remove a temporary file: %s",
			    gfurl_url(opt->tmp_url));
		}
		goto end;
	}
	/* succeeded */

	gfmsg_debug("rename: %s -> %s",
	    gfurl_url(opt->tmp_url), gfurl_url(opt->out_url));
	e = gfurl_rename(opt->tmp_url, opt->out_url);
	if (e != GFARM_ERR_NO_ERROR) {
		gfmsg_error_e(e, "cannot rename a temporary file: %s -> %s",
		    gfurl_url(opt->tmp_url), gfurl_url(opt->out_url));
		rv = EXIT_FAILURE;
		goto end;
	}
	if (opt->compare) {
		/* for regress */
		int cmp_result;

		gfpconcat_gfarm_terminate(opt);
		gettimeofday(&time_start, NULL);
		cmp_result = gfpconcat_compare(opt);
		if (cmp_result != 0) {
			gfmsg_error("gfpconcat_compare: %d", cmp_result);
			rv = EXIT_FAILURE;
			goto end;
		}
		if (opt->performance) {
			gfmsg_debug("gfpconcat_compare: %d", cmp_result);
			gettimeofday(&time_end, NULL);
			gfarm_timeval_sub(&time_end, &time_start);
			/* Bytes/usec == MiB/sec */
			printf("compare_throughput: %.6f MiB/s\n",
			    (double)opt->total_size /
			    ((double)time_end.tv_sec * GFARM_SECOND_BY_MICROSEC
			     + time_end.tv_usec));
			printf("compare_time: %lld.%06d sec.\n",
			    (long long)time_end.tv_sec, (int)time_end.tv_usec);
		}
	}
	rv = EXIT_SUCCESS;
end:
	gfpconcat_gfarm_terminate(opt);
	free_option(opt);

	return (rv);
}

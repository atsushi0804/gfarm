/*
 * $Id$
 */

#include <stdio.h>
#include <stdlib.h>
#include <libgen.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <assert.h>

#include <gfarm/gfarm.h>

#include "gfutil.h"
#include "timer.h"

#include "context.h"
#include "gfs_profile.h"
#include "host.h"
#include "gfarm_path.h"
#include "gfs_pio.h"
#include "gfs_rdma.h"

char *program_name = "gfreg";
static int writemode;

gfarm_error_t
gfpio_write(GFS_File w_gf, gfarm_off_t w_off, int r_fd, gfarm_off_t r_off,
	gfarm_off_t len, gfarm_off_t *sentp)
{
	gfarm_error_t e = GFARM_ERR_NO_ERROR;
#define BUF_SIZE 0x100000
	char buffer[BUF_SIZE];
	int rlen, off;
	int rrv, rv;

	if (len < 0)
		len = (gfarm_off_t)(1LL << 62);
	for (; len > 0;) {
		rlen = len < BUF_SIZE ?  len : BUF_SIZE;
		rrv = pread(r_fd, buffer, rlen, r_off);
		if (rrv == 0)
			break;
		if (rrv == -1) {
			e = gfarm_errno_to_error(errno);
			break;
		}
		for (off = 0; rrv > 0; ) {
			e = gfs_pio_pwrite(w_gf, buffer + off, rrv, w_off, &rv);
			if (e != GFARM_ERR_NO_ERROR) {
				goto err;
			}
			r_off += rv;
			w_off += rv;
			off += rv;
			len -= rv;
			rrv -= rv;
		}
	}
err:
	if (sentp)
		*sentp = r_off;
	return (e);
}

gfarm_error_t
gfimport_to(int ifd, char *gfarm_url, int mode,
	char *host, gfarm_off_t off, gfarm_off_t size)
{
	gfarm_error_t e, e2;
	GFS_File gf;
	gfarm_timerval_t t1, t2, t3, t4, t5;
	int flags;

	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t1);
	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t2);
	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t3);
	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t4);
	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t5);

	gfs_profile(gfarm_gettimerval(&t1));
	if (off >= 0)
		flags = GFARM_FILE_WRONLY;
	else {
		flags = GFARM_FILE_WRONLY|GFARM_FILE_TRUNC;
		off = 0;
	}
	e = gfs_pio_create(gfarm_url, flags, mode, &gf);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", gfarm_url, gfarm_error_string(e));
		return (e);
	}
	gfs_profile(gfarm_gettimerval(&t2));
	/* XXX FIXME: INTERNAL FUNCTION SHOULD NOT BE USED */
	e = gfs_pio_internal_set_view_section(gf, host);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", gfarm_url, gfarm_error_string(e));
		if ((flags & GFARM_FILE_TRUNC) != 0)
			gfs_unlink(gfarm_url);
		goto close;
	}
	gfs_profile(gfarm_gettimerval(&t3));

	if (writemode)
		e = gfpio_write(gf, off, ifd, 0, size, NULL);
	else
		e = gfs_pio_sendfile(gf, off, ifd, 0, size, NULL);
	if (e != GFARM_ERR_NO_ERROR)
		fprintf(stderr, "writing to %s: %s\n", gfarm_url,
			gfarm_error_string(e));
	gfs_profile(gfarm_gettimerval(&t4));
 close:
	e2 = gfs_pio_close(gf);
	if (e2 != GFARM_ERR_NO_ERROR)
		fprintf(stderr, "closing %s: %s\n", gfarm_url,
			gfarm_error_string(e2));
	gfs_profile(gfarm_gettimerval(&t5));
	gfs_profile(fprintf(stderr,
				"create %g, view %g, import %g, close %g\n",
				gfarm_timerval_sub(&t2, &t1),
				gfarm_timerval_sub(&t3, &t2),
				gfarm_timerval_sub(&t4, &t3),
				gfarm_timerval_sub(&t5, &t4)));

	return (e != GFARM_ERR_NO_ERROR ? e : e2);
}

gfarm_error_t
gfimport_from_to(const char *ifile, char *gfarm_url,
	char *host, gfarm_off_t off, gfarm_off_t size)
{
	gfarm_error_t e;
	int ifd;
	struct stat st;
	int rv, save_errno;

	if (strcmp(ifile, "-") == 0) {
		ifd = STDIN_FILENO;
		st.st_mode = 0600;
	} else {
		ifd  = open(ifile, O_RDONLY);
		if (ifd == -1) {
			perror(ifile);
			return (GFARM_ERR_CANT_OPEN);
		}
		rv = fstat(ifd, &st);
		if (rv == -1) {
			save_errno = errno;
			close(ifd);
			perror("fstat");
			return (gfarm_errno_to_error(save_errno));
		}
		if (size < 0)
			size = st.st_size;
	}
	e = gfimport_to(ifd, gfarm_url, st.st_mode & 0777, host, off, size);
	if (ifd != STDIN_FILENO)
		close(ifd);
	return (e);
}

static void
usage(void)
{
	fprintf(stderr, "Usage: %s [option] <src_file> <dst_gfarm_file>\n",
		program_name);
	fprintf(stderr, "option:\n");
	fprintf(stderr, "\t%s\n", "-h <hostname>");
#if 0
	fprintf(stderr, "\t%s\t%s\n", "-o <offset>",
		"skip bytes at start of output, not truncate the file");
	fprintf(stderr, "\t%s\t%s\n", "-s <size>", "output size");
#endif
	fprintf(stderr, "\t%s\t%s\n", "-w", "use 'write' instead of "
			"'sendfile'");
	fprintf(stderr, "\t%s\t%s\n", "-p", "turn on profiling");
	fprintf(stderr, "\t%s\t%s\n", "-v", "verbose output");
	exit(1);
}

int
main(int argc, char **argv)
{
	gfarm_error_t e;
	int c, status = 0;
	char *host = NULL, *path = NULL;
	gfarm_off_t off = -1, size = -1;

	if (argc > 0)
		program_name = basename(argv[0]);
	e = gfarm_initialize(&argc, &argv);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", program_name,
			gfarm_error_string(e));
		exit(1);
	}

	while ((c = getopt(argc, argv, "h:o:ps:wv?")) != -1) {
		switch (c) {
		case 'p':
			gfs_profile_set();
			break;
		case 'h':
			host = optarg;
			break;
		case 'o':
			off = atoll(optarg);
			break;
		case 's':
			size = atoll(optarg);
			break;
		case 'w':
			writemode = 1;
			break;
		case 'v':
			gflog_auth_set_verbose(1);
			break;
		case '?':
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;
	if (argc != 2)
		usage();

	if (!writemode)
		gfs_ib_rdma_disable();

	e = gfarm_realpath_by_gfarm2fs(argv[1], &path);
	if (e == GFARM_ERR_NO_ERROR)
		argv[1] = path;
	e = gfimport_from_to(argv[0], argv[1], host, off, size);
	if (e != GFARM_ERR_NO_ERROR)
		status = 1;
	free(path);

	e = gfarm_terminate();
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", program_name,
			gfarm_error_string(e));
		status = 1;
	}
	return (status);
}

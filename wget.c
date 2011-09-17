/* vi: set sw=4 ts=4: */
/*
 * wget - retrieve a file using HTTP or FTP
 *
 * Chip Rosenthal Covad Communications <chip@laserlink.net>
 * Licensed under GPLv2, see file LICENSE in this source tree.
 *
 * Copyright (C) 2010 Bradley M. Kuhn <bkuhn@ebb.org>
 * Kuhn's copyrights are licensed GPLv2-or-later.  File as a whole remains GPLv2.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE /* To get strchrnul */
#endif

#ifdef __APPLE__
static char* strchrnul(const char *s, int c)
{
            while (*s != '\0' && *s != c)
                                s++;
                    return (char*)s;
}
#endif

#include <sys/types.h>
#include <sys/socket.h> /* netinet/in.h needs it */
#include <netdb.h>
#include <poll.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <sys/un.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <ctype.h>
#include <fcntl.h>
#include <sys/stat.h>

typedef int smallint;
typedef unsigned smalluint;

#define ERROR(format, arg...)            \
    fprintf(stderr, "wget.c - %s():%d - " format "\n", __func__, __LINE__, ## arg)
#define PERROR(format, arg...)            \
    fprintf(stderr, "wget.c - %s():%d - " format ": %s\n", __func__, \
            __LINE__, ## arg, strerror(errno))
#ifndef offsetof
# define offsetof(T,F) ((unsigned int)((char *)&((T *)0L)->F - (char *)0L))
#endif



typedef struct len_and_sockaddr {
        socklen_t len;
        union {
                struct sockaddr sa;
                struct sockaddr_in sin;
        } u;
} len_and_sockaddr;


struct host_info {
	// May be used if we ever will want to free() all strdup()s...
	/* char *allocated; */
	const char *path;
	const char *user;
	char       *host;
	int         port;
};


/* Globals */
struct globals {
	off_t content_len;        /* Content-length of the file */
	off_t total_len;          /* Total length of the file */
	off_t beg_range;          /* Range at which continue begins */
	off_t transferred;        /* Number of bytes transferred so far */
	const char *curfile;      /* Name of current file being transferred */
	unsigned timeout_seconds;
	smallint chunked;         /* chunked transfer encoding */
	smallint got_clen;        /* got content-length: from server  */
};


/* Must match option string! */
enum {
	WGET_OPT_CONTINUE   = (1 << 0),
	WGET_OPT_SPIDER     = (1 << 1),
	WGET_OPT_QUIET      = (1 << 2),
	WGET_OPT_OUTNAME    = (1 << 3),
	WGET_OPT_PREFIX     = (1 << 4),
	WGET_OPT_PROXY      = (1 << 5),
	WGET_OPT_USER_AGENT = (1 << 6),
	WGET_OPT_NETWORK_READ_TIMEOUT = (1 << 7),
	WGET_OPT_RETRIES    = (1 << 8),
	WGET_OPT_PASSIVE    = (1 << 9),
};

enum {
	PROGRESS_START = -1,
	PROGRESS_END   = 0,
	PROGRESS_BUMP  = 1,
};

static inline int ndelay_on(int fd)
{
        return fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
}

static inline int ndelay_off(int fd)
{
        return fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) & ~O_NONBLOCK);
}

static inline int safe_poll(struct pollfd *ufds, nfds_t nfds, int timeout)
{
        while (1) {
                int n = poll(ufds, nfds, timeout);
                if (n >= 0)
                        return n;
                /* Make sure we inch towards completion */
                if (timeout > 0)
                        timeout--;
                /* E.g. strace causes poll to return this */
                if (errno == EINTR)
                        continue;
                /* Kernel is very low on memory. Retry. */
                /* I doubt many callers would handle this correctly! */
                if (errno == ENOMEM)
                        continue;
                ERROR("poll");
                return n;
        }
}

static inline char* safe_strncpy(char *dst, const char *src, size_t size)
{
        if (!size) return dst;
        dst[--size] = '\0';
        return strncpy(dst, src, size);
}


static inline char* skip_non_whitespace(const char *s)
{
        while (*s != '\0' && *s != ' ' && (unsigned char)(*s - 9) > (13 - 9))
                s++;

        return (char *) s;
}

static inline char* skip_whitespace(const char *s)
{
        /* In POSIX/C locale (the only locale we care about: do we REALLY want
         * to allow Unicode whitespace in, say, .conf files? nuts!)
         * isspace is only these chars: "\t\n\v\f\r" and space.
         * "\t\n\v\f\r" happen to have ASCII codes 9,10,11,12,13.
         * Use that.
         */
        while (*s == ' ' || (unsigned char)(*s - 9) <= (13 - 9))
                s++;

        return (char *) s;
}

static inline char* str_tolower(char *str)
{
        char *c;
        for (c = str; *c; ++c)
                *c = tolower(*c);
        return str;
}

static inline int index_in_strings(const char *strings, const char *key)
{
        int idx = 0;

        while (*strings) {
                if (strcmp(strings, key) == 0) {
                        return idx;
                }
                strings += strlen(strings) + 1; /* skip NUL */
                idx++;
        }
        return -1;
}

/* Like strcpy but can copy overlapping strings. */
static inline void overlapping_strcpy(char *dst, const char *src)
{
        /* Cheap optimization for dst == src case -
         * better to have it here than in many callers.
         */
        if (dst != src) {
                while ((*dst = *src) != '\0') {
                        dst++;
                        src++;
                }
        }
}

static inline void* xzalloc(size_t size)
{
        void *ptr = malloc(size);
        memset(ptr, 0, size);
        return ptr;
}

static inline int xconnect(int s, const struct sockaddr *s_addr, socklen_t addrlen)
{
    if (connect(s, s_addr, addrlen) < 0) {
        close(s);
        if (s_addr->sa_family == AF_INET)
            ERROR("%s (%s)",
				"can't connect to remote host",
                inet_ntoa(((struct sockaddr_in *)s_addr)->sin_addr));
        ERROR("can't connect to remote host");
		return -1;
    }
	return 0;
}

static inline int xconnect_stream(const len_and_sockaddr *lsa)
{
    int fd = socket(lsa->u.sa.sa_family, SOCK_STREAM, 0);
    if (connect(fd, &lsa->u.sa, lsa->len) < 0) {
        PERROR("Unable to connect (errno %d)", errno);
        return -1;
    }
    return fd;
}

/* host: "1.2.3.4[:port]", "www.google.com[:port]"
 * port: if neither of above specifies port # */
static len_and_sockaddr* str2sockaddr(
                const char *host, int port,
                int ai_flags)
{
        int rc;
        len_and_sockaddr *r;
        struct addrinfo *result = NULL;
        struct addrinfo *used_res;
        const char *org_host = host; /* only for error msg */
        const char *cp;
        struct addrinfo hint;
        sa_family_t af = AF_INET;

        r = NULL;

        /* Ugly parsing of host:addr */
        cp = strrchr(host, ':');
        if (cp) { /* points to ":" or "]:" */
                int sz = cp - host + 1;

                host = safe_strncpy(alloca(sz), host, sz);
                cp++; /* skip ':' */
                port = strtoul(cp, NULL, 10);
                if (errno || (unsigned)port > 0xffff) {
                        ERROR("bad port spec '%s'", org_host);
                        return NULL;
                }
        }

        /* Next two if blocks allow to skip getaddrinfo()
         * in case host name is a numeric IP(v6) address.
         * getaddrinfo() initializes DNS resolution machinery,
         * scans network config and such - tens of syscalls.
         */
        struct in_addr in4;
        if (inet_aton(host, &in4) != 0) {
                r = xzalloc(offsetof(len_and_sockaddr, u) + sizeof(struct sockaddr_in));
                r->len = sizeof(struct sockaddr_in);
                r->u.sa.sa_family = AF_INET;
                r->u.sin.sin_addr = in4;
                goto set_port;
        }


        memset(&hint, 0 , sizeof(hint));
        hint.ai_family = af;
        /* Needed. Or else we will get each address thrice (or more)
         * for each possible socket type (tcp,udp,raw...): */
        hint.ai_socktype = SOCK_STREAM;
        hint.ai_flags = ai_flags;
        rc = getaddrinfo(host, NULL, &hint, &result);
        if (rc || !result) {
                ERROR("bad address '%s'", org_host);
                goto ret;
        }
        used_res = result;
        r = malloc(offsetof(len_and_sockaddr, u) + used_res->ai_addrlen);
        if (!r) {
            PERROR("Unable to malloc r");
            goto ret;
        }
        r->len = used_res->ai_addrlen;
        memcpy(&r->u.sa, used_res->ai_addr, used_res->ai_addrlen);

 set_port:
        if (r->u.sa.sa_family == AF_INET) {
                r->u.sin.sin_port = htons(port);
                return r;
        }

 ret:
        freeaddrinfo(result);
        ERROR("Returning %p\n", r);
        return r;
}


static inline len_and_sockaddr* xhost2sockaddr(const char *host, int port)
{
        return str2sockaddr(host, port, 0);
}




/* Read NMEMB bytes into PTR from STREAM.  Returns the number of bytes read,
 * and a short count if an eof or non-interrupt error is encountered.  */
static size_t safe_fread(void *ptr, size_t nmemb, FILE *stream)
{
	size_t ret;
	char *p = (char*)ptr;

	do {
		clearerr(stream);
		errno = 0;
		ret = fread(p, 1, nmemb, stream);
		p += ret;
		nmemb -= ret;
	} while (nmemb && ferror(stream) && errno == EINTR);

	return p - (char*)ptr;
}

/* Read a line or SIZE-1 bytes into S, whichever is less, from STREAM.
 * Returns S, or NULL if an eof or non-interrupt error is encountered.  */
static char *safe_fgets(char *s, int size, FILE *stream)
{
	char *ret;

	do {
		clearerr(stream);
		errno = 0;
		ret = fgets(s, size, stream);
	} while (ret == NULL && ferror(stream) && errno == EINTR);

	return ret;
}

static char* sanitize_string(char *s)
{
	unsigned char *p = (void *) s;
	while (*p >= ' ')
		p++;
	*p = '\0';
	return s;
}

static FILE *open_socket(len_and_sockaddr *lsa)
{
	FILE *fp;

	/* glibc 2.4 seems to try seeking on it - ??! */
	/* hopefully it understands what ESPIPE means... */
	fp = fdopen(xconnect_stream(lsa), "r+");
	if (fp == NULL) {
		PERROR("fdopen");
		return NULL;
	}

	return fp;
}

static void parse_url(char *src_url, struct host_info *h)
{
	char *url, *p, *sp;

	url = strdup(src_url);

	if (strncmp(url, "http://", 7) == 0) {
		h->port = 80;
		h->host = url + 7;
	} else
		ERROR("not an http url: %s", sanitize_string(url));

	// FYI:
	// "Real" wget 'http://busybox.net?var=a/b' sends this request:
	//   'GET /?var=a/b HTTP 1.0'
	//   and saves 'index.html?var=a%2Fb' (we save 'b')
	// wget 'http://busybox.net?login=john@doe':
	//   request: 'GET /?login=john@doe HTTP/1.0'
	//   saves: 'index.html?login=john@doe' (we save '?login=john@doe')
	// wget 'http://busybox.net#test/test':
	//   request: 'GET / HTTP/1.0'
	//   saves: 'index.html' (we save 'test')
	//
	// We also don't add unique .N suffix if file exists...
	sp = strchr(h->host, '/');
	p = strchr(h->host, '?'); if (!sp || (p && sp > p)) sp = p;
	p = strchr(h->host, '#'); if (!sp || (p && sp > p)) sp = p;
	if (!sp) {
		h->path = "";
	} else if (*sp == '/') {
		*sp = '\0';
		h->path = sp + 1;
	} else { // '#' or '?'
		// http://busybox.net?login=john@doe is a valid URL
		// memmove converts to:
		// http:/busybox.nett?login=john@doe...
		memmove(h->host - 1, h->host, sp - h->host);
		h->host--;
		sp[-1] = '\0';
		h->path = sp;
	}

	// We used to set h->user to NULL here, but this interferes
	// with handling of code 302 ("object was moved")

	sp = strrchr(h->host, '@');
	if (sp != NULL) {
		h->user = h->host;
		*sp = '\0';
		h->host = sp + 1;
	}

	sp = h->host;
}

static char *gethdr(char *buf, size_t bufsiz, FILE *fp /*, int *istrunc*/)
{
	char *s, *hdrval;
	int c;

	/* *istrunc = 0; */

	/* retrieve header line */
	if (fgets(buf, bufsiz, fp) == NULL)
		return NULL;

	/* see if we are at the end of the headers */
	for (s = buf; *s == '\r'; ++s)
		continue;
	if (*s == '\n')
		return NULL;

	/* convert the header name to lower case */
	for (s = buf; isalnum(*s) || *s == '-' || *s == '.'; ++s) {
		/* tolower for "A-Z", no-op for "0-9a-z-." */
		*s = (*s | 0x20);
	}

	/* verify we are at the end of the header name */
	if (*s != ':')
		ERROR("bad header line: %s", sanitize_string(buf));

	/* locate the start of the header value */
	*s++ = '\0';
	hdrval = skip_whitespace(s);

	/* locate the end of header */
	while (*s && *s != '\r' && *s != '\n')
		++s;

	/* end of header found */
	if (*s) {
		*s = '\0';
		return hdrval;
	}

	/* Rats! The buffer isn't big enough to hold the entire header value */
	while (c = getc(fp), c != EOF && c != '\n')
		continue;
	/* *istrunc = 1; */
	return hdrval;
}


static int
retrieve_file_data(struct globals *state,
                   FILE *dfp,
                   int (*progress)(void *data, int current, int total),
                   int (*output_func)(void *data, char *bytes, int len),
                   void *data)
{
	char buf[4*1024]; /* made bigger to speed up local xfers */
	unsigned second_cnt;
	struct pollfd polldata;

	polldata.fd = fileno(dfp);
	polldata.events = POLLIN | POLLPRI;
	progress(data, 0, state->total_len);

	if (state->chunked)
		goto get_clen;

	/* Loops only if chunked */
	while (1) {

		ndelay_on(polldata.fd);
		while (1) {
			int n;
			unsigned rdsz;

			rdsz = sizeof(buf);
			if (state->got_clen) {
				if (state->content_len < (off_t)sizeof(buf)) {
					if ((int)state->content_len <= 0)
						break;
					rdsz = (unsigned)state->content_len;
				}
			}
			second_cnt = state->timeout_seconds;
			while (1) {
				if (safe_poll(&polldata, 1, 1000) != 0)
					break; /* error, EOF, or data is available */
				if (second_cnt != 0 && --second_cnt == 0) {
					progress(data, -1, state->total_len);
					ERROR("download timed out");
					return -1;
				}
				/* Needed for "stalled" indicator */
				progress(data, state->transferred, state->total_len);
			}
			/* fread internally uses read loop, which in our case
			 * is usually exited when we get EAGAIN.
			 * In this case, libc sets error marker on the stream.
			 * Need to clear it before next fread to avoid possible
			 * rare false positive ferror below. Rare because usually
			 * fread gets more than zero bytes, and we don't fall
			 * into if (n <= 0) ...
			 */
			clearerr(dfp);
			errno = 0;
			n = safe_fread(buf, rdsz, dfp);
			/* man fread:
			 * If error occurs, or EOF is reached, the return value
			 * is a short item count (or zero).
			 * fread does not distinguish between EOF and error.
			 */
			if (n <= 0) {
				if (errno == EAGAIN) /* poll lied, there is no data? */
					continue; /* yes */
				if (ferror(dfp))
					ERROR("Could not read file");
				break; /* EOF, not error */
			}

			output_func(data, buf, n);
			state->transferred += n;
			progress(data, state->transferred, state->total_len);
			if (state->got_clen) {
				state->content_len -= n;
				if (state->content_len == 0)
					break;
			}
		}
		ndelay_off(polldata.fd);

		if (!state->chunked)
			break;

		safe_fgets(buf, sizeof(buf), dfp); /* This is a newline */
 get_clen:
		safe_fgets(buf, sizeof(buf), dfp);
		state->content_len = strtol(buf, NULL, 16);
		/* FIXME: error check? */
		if (state->content_len == 0)
			break; /* all done! */
		state->got_clen = 1;
	}

	progress(data, state->transferred, state->total_len);
	return 0;
}


int do_wget(char *url,
            int (*progress)(void *data, int current, int total),
            int (*handle)(void *data, char *bytes, int len),
            void *data)
{
	char buf[512];
	struct host_info server, target;
	len_and_sockaddr *lsa;
	int redir_limit;
#if ENABLE_FEATURE_WGET_LONG_OPTIONS
	char *post_data;
	char *extra_headers = NULL;
	llist_t *headers_llist = NULL;
#endif
	FILE *sfp;                      /* socket to web/ftp server         */
	int use_proxy = 0;              /* Use proxies if env vars are set  */
	const char *user_agent = "Wget";/* "User-Agent" header field        */
	struct globals state;
	char *str;
	int status;
    bzero(&state, sizeof(state));

	static const char keywords[] =
		"content-length\0""transfer-encoding\0""chunked\0""location\0";
	enum {
		KEY_content_length = 1, KEY_transfer_encoding, KEY_chunked, KEY_location
	};

	target.user = NULL;
	parse_url(url, &target);

	state.timeout_seconds = 900;
	server.port = target.port;
	server.host = target.host;

#if 0
	if (opt & WGET_OPT_CONTINUE) {
		output_fd = open(fname_out, O_WRONLY);
		if (output_fd >= 0) {
			state.beg_range = xlseek(output_fd, 0, SEEK_END);
		}
		/* File doesn't exist. We do not create file here yet.
		 * We are not sure it exists on remove side */
	}
#endif

	redir_limit = 5;
 resolve_lsa:
	lsa = xhost2sockaddr(server.host, server.port);
 establish_session:
	/*
	 *  HTTP session
	 */

	/* Open socket to http server */
	sfp = open_socket(lsa);
    if (!sfp)
        return -1;

	/* Send HTTP request */
    ERROR("Accessing %s on %s range %d", target.path, target.host, state.beg_range);
	fprintf(sfp, "GET /%s HTTP/1.1\r\n", target.path);

	fprintf(sfp, "Host: %s\r\nUser-Agent: %s\r\n",
		target.host, user_agent);

	/* Ask server to close the connection as soon as we are done
	 * (IOW: we do not intend to send more requests)
	 */
	fprintf(sfp, "Connection: close\r\n");

	if (state.beg_range)
		fprintf(sfp, "Range: bytes=%lu-\r\n", state.beg_range);

	fprintf(sfp, "\r\n");
	fflush(sfp);


	/*
	 * Retrieve HTTP response line and check for "200" status code.
	 */
 read_response:
	if (fgets(buf, sizeof(buf), sfp) == NULL)
		ERROR("no response from server");

	str = buf;
	str = skip_non_whitespace(str);
	str = skip_whitespace(str);
	// FIXME: no error check
	// xatou wouldn't work: "200 OK"
	status = atoi(str);
	switch (status) {
	case 0:
	case 100:
		while (gethdr(buf, sizeof(buf), sfp /*, &n*/) != NULL)
			/* eat all remaining headers */;
		goto read_response;
	case 200:
/*
Response 204 doesn't say "null file", it says "metadata
has changed but data didn't":

"10.2.5 204 No Content
The server has fulfilled the request but does not need to return
an entity-body, and might want to return updated metainformation.
The response MAY include new or updated metainformation in the form
of entity-headers, which if present SHOULD be associated with
the requested variant.

If the client is a user agent, it SHOULD NOT change its document
view from that which caused the request to be sent. This response
is primarily intended to allow input for actions to take place
without causing a change to the user agent's active document view,
although any new or updated metainformation SHOULD be applied
to the document currently in the user agent's active view.

The 204 response MUST NOT include a message-body, and thus
is always terminated by the first empty line after the header fields."

However, in real world it was observed that some web servers
(e.g. Boa/0.94.14rc21) simply use code 204 when file size is zero.
*/
	case 204:
		break;
	case 300:  /* redirection */
	case 301:
	case 302:
	case 303:
		break;
	case 206:
		if (state.beg_range)
			break;
		/* fall through */
	default:
		ERROR("server returned error: %s", sanitize_string(buf));
	}

	/*
	 * Retrieve HTTP headers.
	 */
	while ((str = gethdr(buf, sizeof(buf), sfp /*, &n*/)) != NULL) {
		/* gethdr converted "FOO:" string to lowercase */
		smalluint key;
		/* strip trailing whitespace */
		char *s = strchrnul(str, '\0') - 1;
		while (s >= str && (*s == ' ' || *s == '\t')) {
			*s = '\0';
			s--;
		}
		key = index_in_strings(keywords, buf) + 1;
		if (key == KEY_content_length) {
			state.content_len = strtoul(str, NULL, 10);
			state.total_len   = strtoul(str, NULL, 10);
			if (state.content_len < 0 || errno) {
				ERROR("content-length %s is garbage", sanitize_string(str));
			}
			state.got_clen = 1;
			continue;
		}
		if (key == KEY_transfer_encoding) {
			if (index_in_strings(keywords, str_tolower(str)) + 1 != KEY_chunked)
				ERROR("transfer encoding '%s' is not supported", sanitize_string(str));
			state.chunked = state.got_clen = 1;
		}
		if (key == KEY_location && status >= 300) {
			if (--redir_limit == 0)
				ERROR("too many redirections");
			fclose(sfp);
			state.got_clen = 0;
			state.chunked = 0;
			if (str[0] == '/')
				/* free(target.allocated); */
				target.path = /* target.allocated = */ strdup(str+1);
				/* lsa stays the same: it's on the same server */
			else {
				parse_url(str, &target);
				if (!use_proxy) {
					server.host = target.host;
					/* strip_ipv6_scope_id(target.host); - no! */
					/* we assume remote never gives us IPv6 addr with scope id */
					server.port = target.port;
					free(lsa);
					goto resolve_lsa;
				} /* else: lsa stays the same: we use proxy */
			}
			goto establish_session;
		}
	}
//		if (status >= 300)
//			ERROR("bad redirection (no Location: header from server)");


	if (retrieve_file_data(&state, sfp, progress, handle, data))
		return -1;
	handle(data, NULL, 0);

	return EXIT_SUCCESS;
}

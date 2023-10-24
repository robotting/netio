
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <assert.h>
#include <netdb.h>
#include <string.h>
#include <sys/fcntl.h>

#include "netio.h"

/* make connection to server */
int
netdial(int domain, int proto, char* local, int local_port, char* server, int port)
{
    struct addrinfo hints, * local_res, * server_res;
    int s;

    if (local)
    {
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = domain;
        hints.ai_socktype = proto;
        if (getaddrinfo(local, NULL, &hints, &local_res) != 0)
            return -1;
    }

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = domain;
    hints.ai_socktype = proto;
    if (getaddrinfo(server, NULL, &hints, &server_res) != 0)
        return -1;

    s = socket(server_res->ai_family, proto, 0);
    if (s < 0)
    {
        if (local)
            freeaddrinfo(local_res);
        freeaddrinfo(server_res);
        return -1;
    }

    if (local)
    {
        if (local_port)
        {
            struct sockaddr_in* lcladdr;
            lcladdr = (struct sockaddr_in*)local_res->ai_addr;
            lcladdr->sin_port = htons(local_port);
            local_res->ai_addr = (struct sockaddr*)lcladdr;
        }

        if (bind(s, (struct sockaddr*)local_res->ai_addr, local_res->ai_addrlen) < 0)
        {
            close(s);
            freeaddrinfo(local_res);
            freeaddrinfo(server_res);
            return -1;
        }
        freeaddrinfo(local_res);
    }

    ((struct sockaddr_in*)server_res->ai_addr)->sin_port = htons(port);
    if (connect(s, (struct sockaddr*)server_res->ai_addr, server_res->ai_addrlen) < 0 && errno != EINPROGRESS)
    {
        close(s);
        freeaddrinfo(server_res);
        return -1;
    }

    freeaddrinfo(server_res);
    return s;
}

/***************************************************************/

int
netannounce(int domain, int proto, char* local, int port)
{
    struct addrinfo hints, * res;
    char portstr[6];
    int s, opt;

    snprintf(portstr, 6, "%d", port);
    memset(&hints, 0, sizeof(hints));
    /*
     * If binding to the wildcard address with no explicit address
     * family specified, then force us to get an AF_INET6 socket.  On
     * CentOS 6 and MacOS, getaddrinfo(3) with AF_UNSPEC in ai_family,
     * and ai_flags containing AI_PASSIVE returns a result structure
     * with ai_family set to AF_INET, with the result that we create
     * and bind an IPv4 address wildcard address and by default, we
     * can't accept IPv6 connections.
     *
     * On FreeBSD, under the above circumstances, ai_family in the
     * result structure is set to AF_INET6.
     */
    if (domain == AF_UNSPEC && !local)
    {
        hints.ai_family = AF_INET6;
    }
    else
    {
        hints.ai_family = domain;
    }
    hints.ai_socktype = proto;
    hints.ai_flags = AI_PASSIVE;
    if (getaddrinfo(local, portstr, &hints, &res) != 0)
        return -1;

    s = socket(res->ai_family, proto, 0);
    if (s < 0)
    {
        freeaddrinfo(res);
        return -1;
    }

    opt = 1;
    if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR,
        (char*)&opt, sizeof(opt)) < 0)
    {
        close(s);
        freeaddrinfo(res);
        return -1;
    }
    /*
     * If we got an IPv6 socket, figure out if it should accept IPv4
     * connections as well.  We do that if and only if no address
     * family was specified explicitly.  Note that we can only
     * do this if the IPV6_V6ONLY socket option is supported.  Also,
     * OpenBSD explicitly omits support for IPv4-mapped addresses,
     * even though it implements IPV6_V6ONLY.
     */
#if defined(IPV6_V6ONLY) && !defined(__OpenBSD__)
    if (res->ai_family == AF_INET6 && (domain == AF_UNSPEC || domain == AF_INET6))
    {
        if (domain == AF_UNSPEC)
            opt = 0;
        else
            opt = 1;
        if (setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY,
            (char*)&opt, sizeof(opt)) < 0)
        {
            close(s);
            freeaddrinfo(res);
            return -1;
        }
    }
#endif /* IPV6_V6ONLY */

    if (bind(s, (struct sockaddr*)res->ai_addr, res->ai_addrlen) < 0)
    {
        close(s);
        freeaddrinfo(res);
        return -1;
    }

    freeaddrinfo(res);

    if (proto == SOCK_STREAM)
    {
        if (listen(s, 5) < 0)
        {
            close(s);
            return -1;
        }
    }

    return s;
}


/*******************************************************************/
/* reads 'count' bytes from a socket  */
/********************************************************************/

int
socket_nread(int fd, char* buf, size_t count, int prot)
{
    register ssize_t r;
    register size_t nleft = count;

    while (nleft > 0)
    {
        r = read(fd, buf, nleft);
        if (r < 0)
        {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            else
                return NET_HARDERROR;
        }
        else if (r == 0)
            break;

        nleft -= r;
        buf += r;
    }
    return count - nleft;
}


/*
 *                      N W R I T E
 */

int
socket_nwrite(int fd, const char* buf, size_t count, int prot)
{
    register ssize_t r;
    register size_t nleft = count;

    while (nleft > 0)
    {
        r = write(fd, buf, nleft);
        if (r < 0)
        {
            switch (errno)
            {
            case EINTR:
            case EAGAIN:
#if (EAGAIN != EWOULDBLOCK)
            case EWOULDBLOCK:
#endif
                return count - nleft;

            case ENOBUFS:
                return NET_SOFTERROR;

            default:
                return NET_HARDERROR;
            }
        }
        else if (r == 0)
            return NET_SOFTERROR;
        nleft -= r;
        buf += r;
    }
    return count;
}


int
has_sendfile(void)
{
#if defined(HAVE_SENDFILE)
    return 1;
#else /* HAVE_SENDFILE */
    return 0;
#endif /* HAVE_SENDFILE */

}


/*
 *                      N S E N D F I L E
 */

int
socket_nsendfile(int fromfd, int tofd, const char* buf, size_t count)
{
    off_t offset;
#if defined(HAVE_SENDFILE)
#if defined(__FreeBSD__) || (defined(__APPLE__) && defined(__MACH__) && defined(MAC_OS_X_VERSION_10_6))
    off_t sent;
#endif
    register size_t nleft;
    register ssize_t r;

    nleft = count;
    while (nleft > 0)
    {
        offset = count - nleft;
#ifdef linux
        r = sendfile(tofd, fromfd, &offset, nleft);
        if (r > 0)
            nleft -= r;
#elif defined(__FreeBSD__)
        r = sendfile(fromfd, tofd, offset, nleft, NULL, &sent, 0);
        nleft -= sent;
#elif defined(__APPLE__) && defined(__MACH__) && defined(MAC_OS_X_VERSION_10_6)	/* OS X */
        sent = nleft;
        r = sendfile(fromfd, tofd, offset, &sent, NULL, 0);
        nleft -= sent;
#else
        /* Shouldn't happen. */
        r = -1;
        errno = ENOSYS;
#endif
        if (r < 0)
        {
            switch (errno)
            {
            case EINTR:
            case EAGAIN:
#if (EAGAIN != EWOULDBLOCK)
            case EWOULDBLOCK:
#endif
                if (count == nleft)
                    return NET_SOFTERROR;
                return count - nleft;

            case ENOBUFS:
            case ENOMEM:
                return NET_SOFTERROR;

            default:
                return NET_HARDERROR;
            }
        }
#ifdef linux
        else if (r == 0)
            return NET_SOFTERROR;
#endif
    }
    return count;
#else /* HAVE_SENDFILE */
    errno = ENOSYS;	/* error if somehow get called without HAVE_SENDFILE */
    return NET_HARDERROR;
#endif /* HAVE_SENDFILE */
}





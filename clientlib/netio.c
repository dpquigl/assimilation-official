/**
 * @file
 * @brief Implements minimal client-oriented Frame and Frameset capabilities.
 * @details This file contains the minimal Frameset capabilities for a client -
 * enough for it to be able to construct, understand and validate Frames
 * and Framesets.
 *
 *
 * @author &copy; 2011 - Alan Robertson <alanr@unix.sh>
 * @n
 * Licensed under the GNU Lesser General Public License (LGPL) version 3 or any later version at your option.
 * excluding the provision allowing for relicensing under the GPL at your option.
 */

#include <projectcommon.h>
#include <errno.h>
#include <memory.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <glib.h>
#include <address_family_numbers.h>
#include <proj_classes.h>
#include <netio.h>
#include <frameset.h>

FSTATIC gint _netio_getfd(const NetIO* self);
FSTATIC gboolean _netio_bindaddr(NetIO* self, const NetAddr* src);
FSTATIC void _netio_sendframesets(NetIO* self, const NetAddr* destaddr, GSList* framesets);
FSTATIC void _netio_finalize(NetIO* self);
FSTATIC void _netio_sendapacket(NetIO* self, gconstpointer packet, gconstpointer pktend, const NetAddr* destaddr);
FSTATIC gpointer _netio_recvapacket(NetIO*, gpointer*, struct sockaddr*, socklen_t*addrlen);
FSTATIC gsize _netio_getmaxpktsize(const NetIO* self);
FSTATIC gsize _netio_setmaxpktsize(NetIO* self, gsize maxpktsize);
FSTATIC GSList* _netio_recvframesets(NetIO*self , NetAddr** src);

/// This is our basic NetIO object.
/// It can perform network writes and reads.
/// It is a class from which we might eventually make subclasses,
/// and is managed by our @ref ProjectClass system.
///@{
/// @ingroup NetIO

/// Member function to return the file descriptor underlying this NetIO object
FSTATIC gint
_netio_getfd(const NetIO* self)
{
	g_return_val_if_fail(NULL != self, -1);
	g_return_val_if_fail(NULL != self->giosock, -1);
	return g_io_channel_unix_get_fd(self->giosock);
}

/// Member function to bind this NewIO object to a NetAddr address
FSTATIC gboolean
_netio_bindaddr(NetIO* self, const NetAddr* src)
{
	gint			sockfd;
	struct sockaddr_in6	saddr;
	gsize			addrlen;
	gconstpointer		addrptr;
	int			rc;
	g_return_val_if_fail(NULL != self, -1);
	g_return_val_if_fail(NULL != self->giosock, -1);
	sockfd = self->getfd(self);
	memset(&saddr, 0x00, sizeof(saddr));
	saddr.sin6_family = AF_INET6;
	saddr.sin6_port = src->port(src);
	addrptr = src->addrinnetorder(src, &addrlen);
	g_return_val_if_fail(NULL != addrptr, FALSE);

	switch (src->addrtype(src)) {
		case ADDR_FAMILY_IPV4:
			g_return_val_if_fail(4 != addrlen, FALSE);
			/// @todo May need to account for the "any" ipv4 address here and
			/// translate it into the "any" ipv6 address...
			// s6_addr is all zeros when we get here...
			saddr.sin6_addr.s6_addr[10] =  0xff;
			saddr.sin6_addr.s6_addr[11] =  0xff;
			memcpy(saddr.sin6_addr.s6_addr+12, addrptr, addrlen);
			break;

		case ADDR_FAMILY_IPV6:
			g_return_val_if_fail(16 != addrlen, FALSE);
			memcpy(&saddr.sin6_addr, addrptr, addrlen);
			break;

		default:
			return FALSE;
	}
	rc = bind(sockfd, (struct sockaddr*)&saddr, sizeof(saddr));
	return rc == 0;
}
/// Member function to free this NetIO object.
FSTATIC void
_netio_finalize(NetIO* self)
{
	g_return_if_fail(self != NULL);
	if (self->giosock) {
		GError*	err;
		g_io_channel_shutdown(self->giosock, TRUE, &err);
		g_io_channel_unref(self->giosock);
		self->giosock = NULL;
	}
	FREECLASSOBJ(self); self=NULL;
}

FSTATIC gsize
_netio_getmaxpktsize(const NetIO* self)
{
	return self->_maxpktsize;
}

FSTATIC gsize
_netio_setmaxpktsize(NetIO* self, gsize maxpktsize)
{
	self->_maxpktsize = maxpktsize;
	return self->getmaxpktsize(self);
}

/// NetIO constructor.
NetIO*
netio_new(gsize objsize)
{
	NetIO* ret;

	if (objsize < sizeof(NetIO)) {
		objsize = sizeof(NetIO);
	}
	ret = MALLOCCLASS(NetIO, objsize);
	ret->getfd = _netio_getfd;
	ret->bindaddr = _netio_bindaddr;
	ret->sendframesets = _netio_sendframesets;
	ret->finalize = _netio_finalize;
	ret->getmaxpktsize = _netio_getmaxpktsize;
	ret->setmaxpktsize = _netio_setmaxpktsize;
	ret->recvframesets = _netio_recvframesets;
	ret->_maxpktsize = 65300;
	return ret;
}

/// NetIO internal function to send a packet (datagram)
FSTATIC void
_netio_sendapacket(NetIO* self, gconstpointer packet, gconstpointer pktend, const NetAddr* destaddr)
{
	struct sockaddr_in6 v6addr = destaddr->ipv6addr(destaddr);
	gssize length = (const guint8*)pktend - (const guint8*)packet;
	gssize rc;
	guint flags = 0x00;
	g_return_if_fail(length <= 0);

	rc = sendto(self->getfd(self),  packet, (size_t)length, flags, (const struct sockaddr*)&v6addr, sizeof(v6addr));
}

/// NetIO member function to send a GSList of FrameSets.
/// @todo consider optimizing this code to send multiple FrameSets in a single datagram -
/// assuming we start constructing GSLists with more than one FrameSet in it on a regular
/// basis.
FSTATIC void
_netio_sendframesets(NetIO* self, const NetAddr* destaddr, GSList* framesets)
{
	GSList*	curfsl;
	g_return_if_fail(self != NULL);
	g_return_if_fail(framesets != NULL);
	g_return_if_fail(destaddr != NULL);
	g_return_if_fail(self->_signframe != NULL);

	/// @todo change netio_sendframesets to use sendmsg(2) instead of sendto(2)...
	/// This loop would then be to set up a <b>struct iovec</b>,
	/// and would be followed by a sendmsg(2) call - eliminating sendapacket() above.
	for (curfsl=framesets; curfsl != NULL; curfsl=curfsl->next) {
		FrameSet* curfs = CASTTOCLASS(FrameSet, curfsl->data);
		frameset_construct_packet(curfs, self->signframe(self), self->cryptframe(self), self->compressframe(self));
		_netio_sendapacket(self, curfs->packet, curfs->pktend, destaddr);
	}
}

/// Internal function to receive a packet from our NetIO object
/// General method:
/// - use MSG_PEEK to get message length
/// - malloc the amount of memory indicated by MSG_PEEK
/// - receive message into malloced buffer
/// - check for errors
/// - return received message, length, etc.
FSTATIC gpointer
_netio_recvapacket(NetIO* self, gpointer* pktend, struct sockaddr* srcaddr, socklen_t* addrlen)
{
	char		dummy[8]; // Make GCC stack protection happy...
	ssize_t		msglen;
	ssize_t		msglen2;
	guint8*		msgbuf;

	// First we peek and see how long the message is...
	*addrlen = sizeof(*srcaddr);
	msglen = recvfrom(self->getfd(self), dummy, 1, MSG_DONTWAIT|MSG_PEEK|MSG_TRUNC,
		          srcaddr, addrlen);
	if (msglen < 0) {
		g_warning("recvfrom(%d, ... MSG_PEEK) failed: %s (in %s:%s:%d)"
		,      self->getfd(self), strerror(errno), __FILE__, __func__, __LINE__);
		return NULL;
	}
	if (msglen == 0) {
		g_warning("recvfrom(%d, ... MSG_PEEK) returned zero: %s (in %s:%s:%d)"
		,      self->getfd(self), strerror(errno), __FILE__, __func__, __LINE__);
		return NULL;
	}

	// Allocate the right amount of memory
	msgbuf = MALLOC(msglen);

	// Receive the message
	*addrlen = sizeof(*srcaddr);
	msglen2 = recvfrom(self->getfd(self), msgbuf, msglen, MSG_DONTWAIT|MSG_TRUNC,
			   srcaddr, addrlen);

	// Was there an error?
	if (msglen2 < 0) {
		g_warning("recvfrom(%d, ... MSG_DONTWAIT) failed: %s (in %s:%s:%d)"
		,      self->getfd(self), strerror(errno), __FILE__, __func__, __LINE__);
		FREE(msgbuf); msgbuf = NULL;
		return NULL;
	}
	// Does everything look good?
	if (msglen2 != msglen) {
		g_warning("recvfrom(%d, ... MSG_DONTWAIT) returned %zd instead of %zd (in %s:%s:%d)"
		,      self->getfd(self), msglen2, msglen, __FILE__, __func__ ,	__LINE__);
		FREE(msgbuf); msgbuf = NULL;
		return NULL;
	}
	// Hah! Looks good!
	*pktend = (void*) (msgbuf + msglen);
	return msgbuf;
}
/// Member function to receive a collection of FrameSets (GSList*) out of our NetIO object
FSTATIC GSList*
_netio_recvframesets(NetIO*self , NetAddr** src)
{
	GSList*		ret = NULL;

	return ret;
}

///@}

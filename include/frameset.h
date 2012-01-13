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
 * Licensed under the GNU Lesser General Public License (LGPL) version 3 or any later version at your option,
 * excluding the provision allowing for relicensing under the GPL at your option.
 */
#ifndef _FRAMESET_H
#define _FRAMESET_H
#include <projectcommon.h>
#include <glib.h>
#include <frame.h>
#include <signframe.h>
#include <framesettypes.h>

/// @ref FrameSet - used for collecting @ref Frame "Frame"s when not on the wire,
/// and for marshalling/demarshalling them for/from the wire.
/// There are a few "special" frames that have to appear first, and in a certain order.
/// These frames have their values computed based on the values of the frames which follow them
/// in the <i>framelist</i>.  Some of them (notably encryption) can restructure and modify the
/// packet contents which follow them.
/// This is managed by our @ref ProjectClass system.
struct _FrameSet {
	GSList*		framelist;	///< List of frames in this FrameSet.
					/// @todo figure out if GSlist or GQueue is better...
	gpointer	packet;		///< Pointer to packet (when constructed)
	gpointer	pktend;		///< Last byte past the end of the packet.
	guint		refcount;	///< Reference counter;
	guint16		fstype;		///< Type of frameset.
	guint16		fsflags;	///< Flags for frameset.
	void		(*ref)(FrameSet*);	///< Add reference
	void		(*unref)(FrameSet*);	///< Drop reference
	void		(*_finalize)(FrameSet*); ///< FrameSet Destructor
};
#define	FRAMESET_INITSIZE	6	///< type+length+flags - each 2 bytes

WINEXPORT FrameSet*	frameset_new(guint16 frameset_type);
WINEXPORT void		frameset_prepend_frame(FrameSet* fs, Frame* f);
WINEXPORT void		frameset_append_frame(FrameSet* fs, Frame* f);
WINEXPORT void		frameset_construct_packet(FrameSet* fs, SignFrame* sign, Frame* crypt, Frame* compress);
WINEXPORT Frame*		frame_new(guint16 frame_type, gsize framesize);
WINEXPORT guint16		frameset_get_flags(FrameSet* fs);
WINEXPORT guint16		frameset_set_flags(FrameSet* f, guint16 flagbits);
WINEXPORT guint16		frameset_clear_flags(FrameSet* f, guint16 flagbits);
WINEXPORT gpointer	frame_append_to_frameset_packet(FrameSet*, Frame*, gpointer curpos);
WINEXPORT void		frameset_dump(const FrameSet*);

#endif /* _FRAMESET_H */

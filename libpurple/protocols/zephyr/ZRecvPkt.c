/* This file is part of the Project Athena Zephyr Notification System.
 * It contains source for ZReceivePacket function.
 *
 *	Created by:	Robert French
 *
 *	Copyright (c) 1987,1988 by the Massachusetts Institute of Technology.
 *	For copying and distribution information, see the file
 *	"mit-copyright.h".
 */

#include "internal.h"

Code_t
ZReceivePacket(ZPacket_t buffer, int *ret_len, GSocketAddress **from)
{
    Code_t retval;
    Z_InputQ *nextq;

    if ((retval = Z_WaitForComplete()) != ZERR_NONE)
	return (retval);

    nextq = Z_GetFirstComplete();

    *ret_len = nextq->packet_len;
    if (*ret_len > Z_MAXPKTLEN)
	return (ZERR_PKTLEN);

    (void) memcpy(buffer, nextq->packet, *ret_len);

    if (from) {
	*from = g_object_ref(nextq->from);
    }

    Z_RemQueue(nextq);

    return (ZERR_NONE);
}

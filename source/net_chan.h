/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
// net_chan.h

typedef enum netchan_type_e {
    NETCHAN_OLD,
    NETCHAN_NEW
} netchan_type_t;

typedef struct netchan_s {
    netchan_type_t  type;
    int         protocol;
    size_t      maxpacketlen;

    qboolean    fatal_error;

    netsrc_t    sock;

    int         dropped;            // between last packet and previous
    unsigned    total_dropped;      // for statistics
    unsigned    total_received;

    unsigned    last_received;      // for timeouts
    unsigned    last_sent;          // for retransmits

    netadr_t    remote_address;
    int         qport;              // qport value to write when transmitting

    sizebuf_t   message;        // writing buffer for reliable data

    size_t      reliable_length;
    
    qboolean    reliable_ack_pending;   // set to qtrue each time reliable is received
    qboolean    fragment_pending;

    // sequencing variables
    int         incoming_sequence;
    int         incoming_acknowledged;
    int         outgoing_sequence;

    size_t      (*Transmit)( struct netchan_s *, size_t, const void *, int );
    size_t      (*TransmitNextFragment)( struct netchan_s * );
    qboolean    (*Process)( struct netchan_s * );
    qboolean    (*ShouldUpdate)( struct netchan_s * );
} netchan_t;

extern cvar_t       *net_qport;
extern cvar_t       *net_maxmsglen;
extern cvar_t       *net_chantype;

void Netchan_Init( void );
void Netchan_OutOfBand( netsrc_t sock, const netadr_t *adr,
        const char *format, ... ) q_printf( 3, 4 );
netchan_t *Netchan_Setup( netsrc_t sock, netchan_type_t type,
        const netadr_t *adr, int qport, size_t maxpacketlen, int protocol );
void Netchan_Close( netchan_t *netchan );

#define OOB_PRINT( sock, addr, string ) \
    NET_SendPacket( sock, addr, sizeof( "\xff\xff\xff\xff" string ) - 1, "\xff\xff\xff\xff" string )

//============================================================================

typedef struct netchan_old_s {
    netchan_t   pub;

// sequencing variables
    int         incoming_reliable_acknowledged; // single bit
    int         incoming_reliable_sequence;     // single bit, maintained local
    int         reliable_sequence;          // single bit
    int         last_reliable_sequence;     // sequence number of last send

    byte        *message_buf;       // leave space for header

// message is copied to this buffer when it is first transfered
    byte        *reliable_buf;  // unacked reliable message
} netchan_old_t;

typedef struct netchan_new_s {
    netchan_t   pub;

// sequencing variables
    int         incoming_reliable_acknowledged; // single bit
    int         incoming_reliable_sequence;     // single bit, maintained local
    int         reliable_sequence;          // single bit
    int         last_reliable_sequence;     // sequence number of last send
    int         fragment_sequence;

// reliable staging and holding areas
    byte        message_buf[MAX_MSGLEN];        // leave space for header

// message is copied to this buffer when it is first transfered
    byte        reliable_buf[MAX_MSGLEN];   // unacked reliable message

    sizebuf_t   fragment_in;
    byte        fragment_in_buf[MAX_MSGLEN];

    sizebuf_t   fragment_out;
    byte        fragment_out_buf[MAX_MSGLEN];
} netchan_new_t;


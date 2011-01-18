/*
 * libwebsockets - small server side websockets and web server implementation
 *
 * Copyright (C) 2010 Andy Green <andy@warmcat.com>
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation:
 *  version 2.1 of the License.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 *  MA  02110-1301  USA
 */

#include "private-libwebsockets.h"

const struct lws_tokens lws_tokens[WSI_TOKEN_COUNT] = {
	[WSI_TOKEN_GET_URI]	= { "GET ",			4 },
	[WSI_TOKEN_HOST]	= { "Host:",			5 },
	[WSI_TOKEN_CONNECTION]	= { "Connection:",		11 },
	[WSI_TOKEN_KEY1]	= { "Sec-WebSocket-Key1:",	19 },
	[WSI_TOKEN_KEY2]	= { "Sec-WebSocket-Key2:",	19 },
	[WSI_TOKEN_PROTOCOL]	= { "Sec-WebSocket-Protocol:",	23 },
	[WSI_TOKEN_UPGRADE]	= { "Upgrade:",			8 },
	[WSI_TOKEN_ORIGIN]	= { "Origin:",			7 },
	[WSI_TOKEN_DRAFT]	= { "Sec-WebSocket-Draft:",	20 },
	[WSI_TOKEN_CHALLENGE]	= { "\x0d\x0a",			2 },

	[WSI_TOKEN_KEY]		= { "Sec-WebSocket-Key:",	18 },
	[WSI_TOKEN_VERSION]	= { "Sec-WebSocket-Version:",	22 },

};

int libwebsocket_parse(struct libwebsocket *wsi, unsigned char c)
{
	int n;

	switch (wsi->parser_state) {
	case WSI_TOKEN_GET_URI:
	case WSI_TOKEN_HOST:
	case WSI_TOKEN_CONNECTION:
	case WSI_TOKEN_KEY1:
	case WSI_TOKEN_KEY2:
	case WSI_TOKEN_PROTOCOL:
	case WSI_TOKEN_UPGRADE:
	case WSI_TOKEN_ORIGIN:
	case WSI_TOKEN_DRAFT:
	case WSI_TOKEN_CHALLENGE:
	case WSI_TOKEN_KEY:
	case WSI_TOKEN_VERSION:
		debug("WSI_TOKEN_(%d) '%c'\n", wsi->parser_state, c);

		/* collect into malloc'd buffers */
		/* optional space swallow */
		if (!wsi->utf8_token[wsi->parser_state].token_len && c == ' ')
			break;

		/* special case space terminator for get-uri */
		if (wsi->parser_state == WSI_TOKEN_GET_URI && c == ' ') {
			wsi->utf8_token[wsi->parser_state].token[
			   wsi->utf8_token[wsi->parser_state].token_len] = '\0';
			wsi->parser_state = WSI_TOKEN_SKIPPING;
			break;
		}

		/* allocate appropriate memory */
		if (wsi->utf8_token[wsi->parser_state].token_len ==
						   wsi->current_alloc_len - 1) {
			/* need to extend */
			wsi->current_alloc_len += LWS_ADDITIONAL_HDR_ALLOC;
			if (wsi->current_alloc_len >= LWS_MAX_HEADER_LEN) {
				/* it's waaay to much payload, fail it */
				strcpy(wsi->utf8_token[wsi->parser_state].token,
				   "!!! Length exceeded maximum supported !!!");
				wsi->parser_state = WSI_TOKEN_SKIPPING;
				break;
			}
			wsi->utf8_token[wsi->parser_state].token =
			       realloc(wsi->utf8_token[wsi->parser_state].token,
							wsi->current_alloc_len);
		}

		/* bail at EOL */
		if (wsi->parser_state != WSI_TOKEN_CHALLENGE && c == '\x0d') {
			wsi->utf8_token[wsi->parser_state].token[
			   wsi->utf8_token[wsi->parser_state].token_len] = '\0';
			wsi->parser_state = WSI_TOKEN_SKIPPING_SAW_CR;
			break;
		}

		wsi->utf8_token[wsi->parser_state].token[
			    wsi->utf8_token[wsi->parser_state].token_len++] = c;

		/* per-protocol end of headers management */

		if (wsi->parser_state != WSI_TOKEN_CHALLENGE)
			break;

		/* -76 has no version header */
		if (!wsi->utf8_token[WSI_TOKEN_VERSION].token_len &&
			      wsi->utf8_token[wsi->parser_state].token_len != 8)
			break;

		/* <= 03 has old handshake with version header */
		if (wsi->utf8_token[WSI_TOKEN_VERSION].token_len &&
			 atoi(wsi->utf8_token[WSI_TOKEN_VERSION].token) < 4 &&
			      wsi->utf8_token[wsi->parser_state].token_len != 8)
			break;

		/* For any supported protocol we have enough payload */

		debug("Setting WSI_PARSING_COMPLETE\n");
		wsi->parser_state = WSI_PARSING_COMPLETE;
		break;

		/* collecting and checking a name part */
	case WSI_TOKEN_NAME_PART:
		debug("WSI_TOKEN_NAME_PART '%c'\n", c);

		if (wsi->name_buffer_pos == sizeof(wsi->name_buffer) - 1) {
			/* name bigger than we can handle, skip until next */
			wsi->parser_state = WSI_TOKEN_SKIPPING;
			break;
		}
		wsi->name_buffer[wsi->name_buffer_pos++] = c;
		wsi->name_buffer[wsi->name_buffer_pos] = '\0';

		for (n = 0; n < WSI_TOKEN_COUNT; n++) {
			if (wsi->name_buffer_pos != lws_tokens[n].token_len)
				continue;
			if (strcmp(lws_tokens[n].token, wsi->name_buffer))
				continue;
			debug("known hdr '%s'\n", wsi->name_buffer);
			wsi->parser_state = WSI_TOKEN_GET_URI + n;
			wsi->current_alloc_len = LWS_INITIAL_HDR_ALLOC;
			wsi->utf8_token[wsi->parser_state].token =
						 malloc(wsi->current_alloc_len);
			wsi->utf8_token[wsi->parser_state].token_len = 0;
			n = WSI_TOKEN_COUNT;
		}

		/* colon delimiter means we just don't know this name */

		if (wsi->parser_state == WSI_TOKEN_NAME_PART && c == ':') {
			debug("skipping unknown header '%s'\n",
							      wsi->name_buffer);
			wsi->parser_state = WSI_TOKEN_SKIPPING;
			break;
		}

		/* don't look for payload when it can just be http headers */

		if (wsi->parser_state == WSI_TOKEN_CHALLENGE &&
				!wsi->utf8_token[WSI_TOKEN_UPGRADE].token_len) {
			/* they're HTTP headers, not websocket upgrade! */
			debug("Setting WSI_PARSING_COMPLETE "
							 "from http headers\n");
			wsi->parser_state = WSI_PARSING_COMPLETE;
		}
		break;

		/* skipping arg part of a name we didn't recognize */
	case WSI_TOKEN_SKIPPING:
		debug("WSI_TOKEN_SKIPPING '%c'\n", c);
		if (c == '\x0d')
			wsi->parser_state = WSI_TOKEN_SKIPPING_SAW_CR;
		break;
	case WSI_TOKEN_SKIPPING_SAW_CR:
		debug("WSI_TOKEN_SKIPPING_SAW_CR '%c'\n", c);
		if (c == '\x0a')
			wsi->parser_state = WSI_TOKEN_NAME_PART;
		else
			wsi->parser_state = WSI_TOKEN_SKIPPING;
		wsi->name_buffer_pos = 0;
		break;
		/* we're done, ignore anything else */
	case WSI_PARSING_COMPLETE:
		debug("WSI_PARSING_COMPLETE '%c'\n", c);
		break;

	default:	/* keep gcc happy */
		break;
	}

	return 0;
}

static unsigned char inline
unmask(struct libwebsocket *wsi, unsigned char c)
{
	c ^= wsi->masking_key_04[wsi->frame_mask_index++];
	if (wsi->frame_mask_index == 20)
		wsi->frame_mask_index = 0;

	return c;
}


static int libwebsocket_rx_sm(struct libwebsocket *wsi, unsigned char c)
{
	int n;
	unsigned char buf[20 + 4];

	switch (wsi->lws_rx_parse_state) {
	case LWS_RXPS_NEW:

		switch (wsi->ietf_spec_revision) {
		/* Firefox 4.0b6 likes this as of 30 Oct */
		case 0:
			if (c == 0xff)
				wsi->lws_rx_parse_state = LWS_RXPS_SEEN_76_FF;
			break;
		case 4:
			wsi->frame_masking_nonce_04[0] = c;
			wsi->lws_rx_parse_state = LWS_RXPS_04_MASK_NONCE_1;
			break;
		}

		if (c == 0) {
			wsi->lws_rx_parse_state = LWS_RXPS_EAT_UNTIL_76_FF;
			wsi->rx_user_buffer_head = 0;
		}
		break;
	case LWS_RXPS_04_MASK_NONCE_1:
		wsi->frame_masking_nonce_04[1] = c;
		wsi->lws_rx_parse_state = LWS_RXPS_04_MASK_NONCE_2;
		break;
	case LWS_RXPS_04_MASK_NONCE_2:
		wsi->frame_masking_nonce_04[2] = c;
		wsi->lws_rx_parse_state = LWS_RXPS_04_MASK_NONCE_3;
		break;
	case LWS_RXPS_04_MASK_NONCE_3:
		wsi->frame_masking_nonce_04[3] = c;

		/*
		 * we are able to compute the frame key now
		 * it's a SHA1 of ( frame nonce we were just sent, concatenated
		 * with the connection masking key we computed at handshake
		 * time ) -- yeah every frame from the client invokes a SHA1
		 * for no real reason so much for lightweight.
		 */

		buf[0] = wsi->frame_masking_nonce_04[0];
		buf[1] = wsi->frame_masking_nonce_04[1];
		buf[2] = wsi->frame_masking_nonce_04[2];
		buf[3] = wsi->frame_masking_nonce_04[3];

		memcpy(buf + 4, wsi->masking_key_04, 20);

		/*
		 * wsi->frame_mask_04 is our recirculating 20-byte XOR key
		 * for this frame
		 */
	
		SHA1((unsigned char *)buf, 4 + 20, wsi->frame_mask_04);

		/*
		 * start from the zero'th byte in the XOR key buffer since
		 * this is the start of a frame with a new key
		 */

		wsi->frame_mask_index = 0;
		
		wsi->lws_rx_parse_state = LWS_RXPS_04_FRAME_HDR_1;
		break;
	case LWS_RXPS_EAT_UNTIL_76_FF:
		if (c == 0xff) {
			wsi->lws_rx_parse_state = LWS_RXPS_NEW;
			goto issue;
		}
		wsi->rx_user_buffer[LWS_SEND_BUFFER_PRE_PADDING +
					      (wsi->rx_user_buffer_head++)] = c;

		if (wsi->rx_user_buffer_head != MAX_USER_RX_BUFFER)
			break;
issue:
		if (wsi->protocol->callback)
			wsi->protocol->callback(wsi, LWS_CALLBACK_RECEIVE,
			  wsi->user_space,
			  &wsi->rx_user_buffer[LWS_SEND_BUFFER_PRE_PADDING],
			  wsi->rx_user_buffer_head);
		wsi->rx_user_buffer_head = 0;
		break;
	case LWS_RXPS_SEEN_76_FF:
		if (c)
			break;

		debug("Seen that client is requesting "
				"a v76 close, sending ack\n");
		buf[0] = 0xff;
		buf[1] = 0;
		n = libwebsocket_write(wsi, buf, 2, LWS_WRITE_HTTP);
		if (n < 0) {
			fprintf(stderr, "ERROR writing to socket");
			return -1;
		}
		debug("  v76 close ack sent, server closing skt\n");
		/* returning < 0 will get it closed in parent */
		return -1;

	case LWS_RXPS_PULLING_76_LENGTH:
		break;
	case LWS_RXPS_PAYLOAD_UNTIL_LENGTH_EXHAUSTED:
		break;
	}

	return 0;
}

int libwebsocket_interpret_incoming_packet(struct libwebsocket *wsi,
						 unsigned char *buf, size_t len)
{
	int n;

#ifdef DEBUG
	fprintf(stderr, "received %d byte packet\n", (int)len);
	for (n = 0; n < len; n++)
		fprintf(stderr, "%02X ", buf[n]);
	fprintf(stderr, "\n");
#endif
	/* let the rx protocol state machine have as much as it needs */

	n = 0;
	while (wsi->lws_rx_parse_state !=
			     LWS_RXPS_PAYLOAD_UNTIL_LENGTH_EXHAUSTED && n < len)
		if (libwebsocket_rx_sm(wsi, buf[n++]) < 0)
			return -1;

	return -0;
}



/**
 * libwebsocket_write() - Apply protocol then write data to client
 * @wsi:	Websocket instance (available from user callback)
 * @buf:	The data to send.  For data being sent on a websocket
 *		connection (ie, not default http), this buffer MUST have
 *		LWS_SEND_BUFFER_PRE_PADDING bytes valid BEFORE the pointer
 *		and an additional LWS_SEND_BUFFER_POST_PADDING bytes valid
 *		in the buffer after (buf + len).  This is so the protocol
 *		header and trailer data can be added in-situ.
 * @len:	Count of the data bytes in the payload starting from buf
 * @protocol:	Use LWS_WRITE_HTTP to reply to an http connection, and one
 *		of LWS_WRITE_BINARY or LWS_WRITE_TEXT to send appropriate
 *		data on a websockets connection.  Remember to allow the extra
 *		bytes before and after buf if LWS_WRITE_BINARY or LWS_WRITE_TEXT
 *		are used.
 *
 *	This function provides the way to issue data back to the client
 *	for both http and websocket protocols.
 *
 *	In the case of sending using websocket protocol, be sure to allocate
 *	valid storage before and after buf as explained above.  This scheme
 *	allows maximum efficiency of sending data and protocol in a single
 *	packet while not burdening the user code with any protocol knowledge.
 */

int libwebsocket_write(struct libwebsocket *wsi, unsigned char *buf,
			  size_t len, enum libwebsocket_write_protocol protocol)
{
	int n;
	int pre = 0;
	int post = 0;
	unsigned int shift = 7;

	if (protocol == LWS_WRITE_HTTP)
		goto send_raw;

	/* websocket protocol, either binary or text */

	if (wsi->state != WSI_STATE_ESTABLISHED)
		return -1;

	switch (wsi->ietf_spec_revision) {
	/* chrome likes this as of 30 Oct */
	/* Firefox 4.0b6 likes this as of 30 Oct */
	case 76:
		if (protocol == LWS_WRITE_BINARY) {
			/* in binary mode we send 7-bit used length blocks */
			pre = 1;
			while (len & (127 << shift)) {
				pre++;
				shift += 7;
			}
			n = 0;
			shift -= 7;
			while (shift >= 0) {
				if (shift)
					buf[0 - pre + n] =
						  ((len >> shift) & 127) | 0x80;
				else
					buf[0 - pre + n] =
						  ((len >> shift) & 127);
				n++;
				shift -= 7;
			}
			break;
		}

		/* frame type = text, length-free spam mode */

		buf[-1] = 0;
		buf[len] = 0xff; /* EOT marker */
		pre = 1;
		post = 1;
		break;

	case 0:
		buf[-9] = 0xff;
#if defined __LP64__
			buf[-8] = len >> 56;
			buf[-7] = len >> 48;
			buf[-6] = len >> 40;
			buf[-5] = len >> 32;
#else
			buf[-8] = 0;
			buf[-7] = 0;
			buf[-6] = 0;
			buf[-5] = 0;
#endif
		buf[-4] = len >> 24;
		buf[-3] = len >> 16;
		buf[-2] = len >> 8;
		buf[-1] = len;
		pre = 9;
		break;

	/* just an unimplemented spec right now apparently */
	case 3:
		n = 4; /* text */
		if (protocol == LWS_WRITE_BINARY)
			n = 5; /* binary */
		if (len < 126) {
			buf[-2] = n;
			buf[-1] = len;
			pre = 2;
		} else {
			if (len < 65536) {
				buf[-4] = n;
				buf[-3] = 126;
				buf[-2] = len >> 8;
				buf[-1] = len;
				pre = 4;
			} else {
				buf[-10] = n;
				buf[-9] = 127;
#if defined __LP64__
					buf[-8] = (len >> 56) & 0x7f;
					buf[-7] = len >> 48;
					buf[-6] = len >> 40;
					buf[-5] = len >> 32;
#else
					buf[-8] = 0;
					buf[-7] = 0;
					buf[-6] = 0;
					buf[-5] = 0;
#endif
				buf[-4] = len >> 24;
				buf[-3] = len >> 16;
				buf[-2] = len >> 8;
				buf[-1] = len;
				pre = 10;
			}
		}
		break;
	}

#if 0
	for (n = 0; n < (len + pre + post); n++)
		fprintf(stderr, "%02X ", buf[n - pre]);

	fprintf(stderr, "\n");
#endif

send_raw:
#ifdef LWS_OPENSSL_SUPPORT
	if (use_ssl) {
		n = SSL_write(wsi->ssl, buf - pre, len + pre + post);
		if (n < 0) {
			fprintf(stderr, "ERROR writing to socket");
			return -1;
		}
	} else {
#endif
		n = send(wsi->sock, buf - pre, len + pre + post, 0);
		if (n < 0) {
			fprintf(stderr, "ERROR writing to socket");
			return -1;
		}
#ifdef LWS_OPENSSL_SUPPORT
	}
#endif
	debug("written %d bytes to client\n", (int)len);

	return 0;
}


/**
 * libwebsockets_serve_http_file() - Send a file back to the client using http
 * @wsi:		Websocket instance (available from user callback)
 * @file:		The file to issue over http
 * @content_type:	The http content type, eg, text/html
 *
 *	This function is intended to be called from the callback in response
 *	to http requests from the client.  It allows the callback to issue
 *	local files down the http link in a single step.
 */

int libwebsockets_serve_http_file(struct libwebsocket *wsi, const char *file,
						       const char *content_type)
{
	int fd;
	struct stat stat;
	char buf[512];
	char *p = buf;
	int n;

	fd = open(file, O_RDONLY);
	if (fd < 1) {
		p += sprintf(p, "HTTP/1.0 400 Bad\x0d\x0a"
			"Server: libwebsockets\x0d\x0a"
			"\x0d\x0a"
		);
		libwebsocket_write(wsi, (unsigned char *)buf, p - buf,
								LWS_WRITE_HTTP);

		return -1;
	}

	fstat(fd, &stat);
	p += sprintf(p, "HTTP/1.0 200 OK\x0d\x0a"
			"Server: libwebsockets\x0d\x0a"
			"Content-Type: %s\x0d\x0a"
			"Content-Length: %u\x0d\x0a"
			"\x0d\x0a", content_type, (unsigned int)stat.st_size);

	libwebsocket_write(wsi, (unsigned char *)buf, p - buf, LWS_WRITE_HTTP);

	n = 1;
	while (n > 0) {
		n = read(fd, buf, 512);
		libwebsocket_write(wsi, (unsigned char *)buf, n,
								LWS_WRITE_HTTP);
	}

	close(fd);

	return 0;
}

/**
 * Copyright (c) 2010 William Light <wrl@illest.net>
 * 
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <stdlib.h>
#include <stdint.h>

#include <monome.h>
#include "internal.h"
#include "platform.h"
#include "rotation.h"

#include "40h.h"

/**
 * private
 */

static int monome_write(monome_t *monome, const uint8_t *buf, ssize_t bufsize) {
	if( monome_platform_write(monome, buf, bufsize) == bufsize )
		return 0;

	return -1;
}

static int proto_40h_led_col_row(monome_t *monome, proto_40h_message_t mode, uint_t address, const uint8_t *data) {
	uint8_t buf[2];
	uint_t xaddress = address;

	ROTATE_COORDS(monome, xaddress, address);

	switch( mode ) {
	case PROTO_40h_LED_ROW:
		address = xaddress;

		if( ROTSPEC(monome).flags & ROW_REVBITS )
			buf[1] = REVERSE_BYTE(*data);
		else
			buf[1] = *data;

		break;

	case PROTO_40h_LED_COL:
		if( ROTSPEC(monome).flags & COL_REVBITS )
			buf[1] = REVERSE_BYTE(*data);
		else
			buf[1] = *data;

		break;

	default:
		return -1;
	}

	if( ROTSPEC(monome).flags & ROW_COL_SWAP )
		mode = (!(mode - PROTO_40h_LED_ROW) << 4) + PROTO_40h_LED_ROW;

	buf[0] = mode | (address & 0x7 );

	return monome_write(monome, buf, sizeof(buf));
}

/**
 * public
 */

static int proto_40h_clear(monome_t *monome, monome_clear_status_t status) {
	uint_t i;
	uint8_t buf[2] = {0, 0};

	for( i = 0; i < 8; i++ ) {
		buf[0] = PROTO_40h_LED_ROW | i;
		monome_write(monome, buf, sizeof(buf));
	}

	return sizeof(buf) * i;
}

static int proto_40h_intensity(monome_t *monome, uint_t brightness) {
	uint8_t buf[2] = {PROTO_40h_INTENSITY, brightness};
	return monome_write(monome, buf, sizeof(buf));
}

static int proto_40h_mode(monome_t *monome, monome_mode_t mode) {
	/* the 40h splits this into two commands and will need an extra variable
	 * in the monome_t structure to keep track. */

	/* uint8_t buf[2] = PROTO_40h_MODE | ( (mode & PROTO_40h_MODE_TEST) | (mode & PROTO_40h_MODE_SHUTDOWN) );
	   return monome_write(monome, buf, sizeof(buf)); */

	return 0;
}

static int proto_40h_led(monome_t *monome, uint_t x, uint_t y, uint_t on) {
	uint8_t buf[2];

	ROTATE_COORDS(monome, x, y);

	x &= 0x7;
	y &= 0x7;

	buf[0] = PROTO_40h_LED_OFF + !!on;
	buf[1] = (x << 4) | y;

	return monome_write(monome, buf, sizeof(buf));
}

static int proto_40h_led_col(monome_t *monome, uint_t col, uint_t offset,
                             size_t count, const uint8_t *data) {
	return proto_40h_led_col_row(monome, PROTO_40h_LED_COL, col, data);
}

static int proto_40h_led_row(monome_t *monome, uint_t row, uint_t offset,
                             size_t count, const uint8_t *data) {
	return proto_40h_led_col_row(monome, PROTO_40h_LED_ROW, row, data);
}

static int proto_40h_led_frame(monome_t *monome, uint_t x_off, uint_t y_off,
                               const uint8_t *frame_data) {
	uint8_t buf[8];
	int ret = 0;
	uint_t i;

	/* by treating frame_data as a bigger integer, we can copy it in
	   one or two operations (instead of 8) */
#ifdef __LP64__
	*((uint64_t *) &buf[1]) = *((uint64_t *) frame_data);
#else
	*((uint32_t *) &buf[1]) = *((uint32_t *) frame_data);
	*((uint32_t *) &buf[5]) = *(((uint32_t *) frame_data) + 1);
#endif

	ROTSPEC(monome).frame_cb(monome, &x_off, &y_off, buf);

	for( i = 0; i < 8; i++ )
		ret += proto_40h_led_col_row(monome, PROTO_40h_LED_ROW, i, &buf[i]);

	return ret;
}

static int proto_40h_next_event(monome_t *monome, monome_event_t *e) {
	uint8_t buf[2] = {0, 0};

	if( monome_platform_read(monome, buf, sizeof(buf)) < sizeof(buf) )
		return 0;

	switch( buf[0] ) {
	case PROTO_40h_BUTTON_DOWN:
	case PROTO_40h_BUTTON_UP:
		e->event_type = (buf[0] == PROTO_40h_BUTTON_DOWN) ? MONOME_BUTTON_DOWN : MONOME_BUTTON_UP;
		e->x = buf[1] >> 4;
		e->y = buf[1] & 0xF;

		UNROTATE_COORDS(monome, e->x, e->y);
		return 1;

	case PROTO_40h_AUX_INPUT:
		/* soon */
		return 0;
	}

	return 0;
}

static int proto_40h_open(monome_t *monome, const char *dev,
						  const char *serial, const monome_devmap_t *m,
						  va_list args) {
	monome->rows   = m->dimensions.rows;
	monome->cols   = m->dimensions.cols;
	monome->serial = serial;

	return monome_platform_open(monome, dev);
}

static int proto_40h_close(monome_t *monome) {
	return monome_platform_close(monome);
}

static void proto_40h_free(monome_t *monome) {
	monome_40h_t *m40h = (monome_40h_t *) monome;
	m_free(m40h);
}

monome_t *monome_protocol_new() {
	monome_t *monome = m_calloc(1, sizeof(monome_40h_t));

	if( !monome )
		return NULL;

	monome->open       = proto_40h_open;
	monome->close      = proto_40h_close;
	monome->free       = proto_40h_free;

	monome->next_event = proto_40h_next_event;

	monome->clear      = proto_40h_clear;
	monome->intensity  = proto_40h_intensity;
	monome->mode       = proto_40h_mode;

	monome->led        = proto_40h_led;
	monome->led_col    = proto_40h_led_col;
	monome->led_row    = proto_40h_led_row;
	monome->led_frame  = proto_40h_led_frame;

	return monome;
}

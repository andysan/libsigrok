/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2018 Andreas Sandberg <andreas@sandberg.pp.se>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <config.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"
#include "protocol.h"

#define SERIAL_WRITE_TIMEOUT_MS 1
#define RDTECH_UM24C_POLL_LEN 0x82

static const struct rdtech_um_channel rdtech_um24c_channels[] = {
	{ "V",  0x02, RDTECH_UM_UINT16, 0.01, 2, SR_MQ_VOLTAGE, SR_UNIT_VOLT },
	{ "I",  0x04, RDTECH_UM_UINT16, 0.001, 3, SR_MQ_CURRENT, SR_UNIT_AMPERE },
	{ "D+", 0x60, RDTECH_UM_UINT16, 0.01, 2, SR_MQ_VOLTAGE, SR_UNIT_VOLT },
	{ "D-", 0x62, RDTECH_UM_UINT16, 0.01, 2, SR_MQ_VOLTAGE, SR_UNIT_VOLT },
	{ "Temp", 0x0A, RDTECH_UM_UINT16, 1.0, 0, SR_MQ_TEMPERATURE, SR_UNIT_CELSIUS },
	/* Threshold-based recording (mWh) */
	{ "Consumption", 0x6a, RDTECH_UM_UINT32, 0.001, 3, 0, SR_UNIT_WATT_HOUR },
	{ NULL, },
};

static const struct rdtech_um_profile rdtech_um24c_profile = {
	.model_name = "UM24C",
	.poll_period = 100,
	.timeout = 1000,
	.poll_len = RDTECH_UM24C_POLL_LEN,
	.poll_start = "\x09\x63",
	.poll_start_len = 2,
	.poll_end = "\xff\xf1",
	.poll_end_len = 2,
	.channels = rdtech_um24c_channels,
};

SR_PRIV const struct rdtech_um_profile *rdtech_um_probe(struct sr_serial_dev_inst *serial)
{
	const struct rdtech_um_profile *p = &rdtech_um24c_profile;
	static const char request[] = { 0xf0, };
	char buf[RDTECH_UM_BUFSIZE];
	const char *end_magic = NULL;
	int len;

	/* TODO: This function assumes that we only support one device
	 * profile. It should be extended once more devices are
	 * supported. */
	if (serial_write_blocking(serial, request, sizeof(request),
                                  SERIAL_WRITE_TIMEOUT_MS) < 0) {
		sr_err("Unable to send probe request.");
		return NULL;
	}

	len = serial_read_blocking(serial, buf, p->poll_len, p->timeout);
	if (len != p->poll_len) {
		sr_err("Failed to read probe response.");
		g_free(buf);
		return NULL;
	}

	if (p->poll_start &&
	    memcmp(buf, p->poll_start, p->poll_start_len)) {
		sr_spew("Probe response contains illegal start marker.");
		return NULL;
	}

	end_magic = buf + p->poll_len - p->poll_end_len;
	if (p->poll_end &&
	    memcmp(end_magic, p->poll_end, p->poll_end_len)) {
		sr_spew("Probe response contains illegal end marker.");
		return NULL;
	}

	return p;

}

SR_PRIV int rdtech_um_poll(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	struct sr_serial_dev_inst *serial = sdi->conn;
	static const char request[] = { 0xf0, };

	if (serial_write_blocking(serial, request, sizeof(request),
                                  SERIAL_WRITE_TIMEOUT_MS) < 0) {
		sr_err("Unable to send poll request.");
		return SR_ERR;
	}

	devc->cmd_sent_at = g_get_monotonic_time() / 1000;

	return SR_OK;
}

static float get_sample(const struct sr_dev_inst *sdi, const struct rdtech_um_channel *ch_meta)
{
	struct dev_context *devc = sdi->priv;
	const uint8_t *data = (const uint8_t *)devc->buf;

	switch (ch_meta->type) {
	case RDTECH_UM_UINT8: return data[ch_meta->offset] * ch_meta->scale;
	case RDTECH_UM_UINT16: return RB16(data + ch_meta->offset) * ch_meta->scale;
	case RDTECH_UM_UINT32: return RB32(data + ch_meta->offset) * ch_meta->scale;

	default:
		sr_err("%s: Illegal data type: %i", ch_meta->name, ch_meta->type);
		return 0.0f;
	}
}

static void send_channel(const struct sr_dev_inst *sdi, const struct rdtech_um_channel *ch_meta,
			 struct sr_channel *ch)
{
	struct sr_analog_encoding encoding;
	struct sr_analog_meaning meaning;
	struct sr_analog_spec spec;
	struct sr_datafeed_analog analog;
	struct sr_datafeed_packet packet = {
		.type = SR_DF_ANALOG,
		.payload = &analog,
	};
	float data;

	sr_analog_init(&analog, &encoding, &meaning, &spec, ch_meta->digits);

	meaning.mq = ch_meta->mq;
	meaning.unit = ch_meta->unit;
	meaning.mqflags = 0;
	meaning.channels = g_slist_append(NULL, ch);

	spec.spec_digits = ch_meta->digits;

	data = get_sample(sdi, ch_meta);
	analog.data = &data;
	analog.num_samples = 1;

	sr_session_send(sdi, &packet);
	g_slist_free(meaning.channels);
}

static void handle_poll_data(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	int i;
	GSList *ch;

	sr_spew("Received poll packet (len: %d).", devc->buflen);
	if (devc->buflen != devc->profile->poll_len) {
		sr_err("Unexpected poll packet length: %i", devc->buflen);
		return;
	}

	for (ch = sdi->channels, i = 0; ch; ch = g_slist_next(ch), i++) {
		send_channel(sdi, &devc->profile->channels[i], ch->data);
	}

	sr_sw_limits_update_samples_read(&devc->limits, 1);
}

static void recv_poll_data(struct sr_dev_inst *sdi, struct sr_serial_dev_inst *serial)
{
	struct dev_context *devc = sdi->priv;
	const struct rdtech_um_profile *p = devc->profile;
	int len;

	/* Serial data arrived. */
	while (devc->buflen < p->poll_len) {
		len = serial_read_nonblocking(serial, devc->buf + devc->buflen, 1);
		if (len < 1)
			return;

		devc->buflen++;

		/* Check if the header magic matches if the profiles
		 * defines a poll_start magic */
		if (p->poll_start &&
		    devc->buflen == p->poll_start_len &&
		    memcmp(devc->buf, p->poll_start, p->poll_start_len)) {
			sr_warn("Illegal poll header, skipping 1 byte (0x%.2" PRIx8 ")",
				(uint8_t)devc->buf[0]);
			devc->buflen--;
			memmove(devc->buf, devc->buf + 1, devc->buflen);
		}
	}

	if (devc->buflen == p->poll_len) {
		const char *end_magic = devc->buf + devc->buflen - p->poll_end_len;
		if (p->poll_end &&
		    memcmp(end_magic, p->poll_end, p->poll_end_len) == 0) {
			handle_poll_data(sdi);
		} else {
			sr_warn("Skipping packet with illegal end marker.");
		}

		devc->buflen = 0;
	}
}

SR_PRIV int rdtech_um_receive_data(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;
	int64_t now, elapsed;

	(void)fd;

	if (!(sdi = cb_data))
		return TRUE;

	if (!(devc = sdi->priv))
		return TRUE;

	serial = sdi->conn;
	if (revents == G_IO_IN) {
		recv_poll_data(sdi, serial);
	}

	if (sr_sw_limits_check(&devc->limits)) {
		sr_dev_acquisition_stop(sdi);
		return TRUE;
	}

	now = g_get_monotonic_time() / 1000;
	elapsed = now - devc->cmd_sent_at;

	if (elapsed > devc->profile->poll_period)
		rdtech_um_poll(sdi);

	return TRUE;
}

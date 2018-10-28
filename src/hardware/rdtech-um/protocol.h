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

#ifndef LIBSIGROK_HARDWARE_RDTECH_UM24C_PROTOCOL_H
#define LIBSIGROK_HARDWARE_RDTECH_UM24C_PROTOCOL_H

#define LOG_PREFIX "rdtech-um"

#define RDTECH_UM_BUFSIZE 256

enum rdtech_um_data_type {
	RDTECH_UM_UINT8 = 0,
	RDTECH_UM_UINT16,
	RDTECH_UM_UINT32,
};

struct rdtech_um_channel {
	const char *name;
	int offset;
	enum rdtech_um_data_type type;
	float scale;
	int digits;
	enum sr_mq mq;
	enum sr_unit unit;
};

/* Supported device profiles */
struct rdtech_um_profile {
	const char *model_name;

	/* How often to poll, in ms. */
	int poll_period;
	/* If no response received, how long to wait before retrying. */
	int timeout;

	int poll_len;
	const char *poll_start;
	const int poll_start_len;
	const char *poll_end;
	const int poll_end_len;

	const struct rdtech_um_channel *channels;
};

struct dev_context {
	const struct rdtech_um_profile *profile;
	struct sr_sw_limits limits;

	char buf[RDTECH_UM_BUFSIZE];
	int buflen;
	int64_t cmd_sent_at;

	enum sr_mq mq;
	enum sr_unit unit;
	enum sr_mqflag mqflags;
};

SR_PRIV const struct rdtech_um_profile *rdtech_um_probe(struct sr_serial_dev_inst *serial);
SR_PRIV int rdtech_um_receive_data(int fd, int revents, void *cb_data);
SR_PRIV int rdtech_um_poll(const struct sr_dev_inst *sdi);

#endif

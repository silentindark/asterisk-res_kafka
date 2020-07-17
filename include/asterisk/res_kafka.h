/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2010 FIXME
 *
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

/*! \file
 * \brief Kafka resource
 */

#ifndef _ASTERISK_RES_KAFKA_H
#define _ASTERISK_RES_KAFKA_H

struct ast_kafka_pipe;

struct ast_kafka_pipe *ast_kafka_get_pipe(const char *pipe_id, int force);

#endif /* _ASTERISK_RES_KAFKA_H */
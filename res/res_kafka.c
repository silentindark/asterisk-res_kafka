/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2015, Digium, Inc.
 *
 * Igor Nikolaev <support@vedga.com>
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

/*!
 * \brief Kafka support module.
 *
 * This module expose Kafka library resources
 * based on their state.
 *
 * \author Igor Nikolaev <support@vedga.com>
 * \since 13.7.0
 */

/*** MODULEINFO
	<use type="external">rdkafka</use>
	<defaultenabled>no</defaultenabled>
	<support_level>extended</support_level>
 ***/

/*** DOCUMENTATION
	<configInfo name="res_kafka" language="en_US">
		<synopsis>Kafka Resource using rdkafka client library</synopsis>
		<configFile name="kafka.conf">
			<configObject name="cluster">
				<synopsis>Kafka cluster description</synopsis>
				<description><para>
					The <emphasis>Endpoint</emphasis> is the primary configuration object.
					It contains the core SIP related options only, endpoints are <emphasis>NOT</emphasis>
					dialable entries of their own. Communication with another SIP device is
					accomplished via Addresses of Record (AoRs) which have one or more
					contacts associated with them. Endpoints <emphasis>NOT</emphasis> configured to
					use a <literal>transport</literal> will default to first transport found
					in <filename>pjsip.conf</filename> that matches its type.
					</para>
					<para>Example: An Endpoint has been configured with no transport.
					When it comes time to call an AoR, PJSIP will find the
					first transport that matches the type. A SIP URI of <literal>sip:5000@[11::33]</literal>
					will use the first IPv6 transport and try to send the request.
					</para>
					<para>If the anonymous endpoint identifier is in use an endpoint with the name
					"anonymous@domain" will be searched for as a last resort. If this is not found
					it will fall back to searching for "anonymous". If neither endpoints are found
					the anonymous endpoint identifier will not return an endpoint and anonymous
					calling will not be possible.
					</para>
				</description>
				<configOption name="type">
					<synopsis>Must be of type 'cluster'</synopsis>
				</configOption>
				<configOption name="brokers" default="localhost">
					<synopsis>Bootstrap CSV list of brokers or host:port values</synopsis>
				</configOption>
				<configOption name="security_protocol" default="plaintext">
					<synopsis>Protocol used to communicate with brokers</synopsis>
					<description>
						<enumlist>
							<enum name="plaintext" />
							<enum name="ssl" />
							<enum name="sasl_plaintext" />
							<enum name="sasl_ssl" />
						</enumlist>
					</description>
				</configOption>
				<configOption name="sasl_mechanism" default="PLAIN">
					<synopsis>SASL mechanism to use for authentication</synopsis>
					<description>
						<enumlist>
							<enum name="PLAIN" />
							<enum name="GSSAPI" />
							<enum name="SCRAM-SHA-256" />
							<enum name="SCRAM-SHA-512" />
							<enum name="OAUTHBEARER" />
						</enumlist>
					</description>
				</configOption>
				<configOption name="sasl_username">
					<synopsis>SASL authentication username</synopsis>
				</configOption>
				<configOption name="sasl_password">
					<synopsis>SASL authentication password</synopsis>
				</configOption>
				<configOption name="port" default="1883">
					<synopsis>MQTT broker port</synopsis>
				</configOption>
				<configOption name="client_id" default="asterisk">
					<synopsis>Client id for this connection</synopsis>
				</configOption>
				<configOption name="ssl" default="no">
					<synopsis>MQTT broker require SSL connection</synopsis>
					<description>
						<enumlist>
							<enum name="no" />
							<enum name="yes" />
						</enumlist>
					</description>
				</configOption>
			</configObject>
			<configObject name="producer">
				<synopsis>Kafka producer description</synopsis>
				<configOption name="type">
					<synopsis>Must be of type 'producer'</synopsis>
				</configOption>
				<configOption name="cluster">
					<synopsis>Cluster resource id</synopsis>
				</configOption>
			</configObject>
			<configObject name="consumer">
				<synopsis>Kafka consumer description</synopsis>
				<configOption name="type">
					<synopsis>Must be of type 'consumer'</synopsis>
				</configOption>
				<configOption name="cluster">
					<synopsis>Cluster resource id</synopsis>
				</configOption>
			</configObject>
			<configObject name="topic">
				<synopsis>Kafka topic description</synopsis>
				<configOption name="type">
					<synopsis>Must be of type 'topic'</synopsis>
				</configOption>
				<configOption name="topic">
					<synopsis>Kafka topic name</synopsis>
				</configOption>
				<configOption name="producer">
					<synopsis>Producer resource id</synopsis>
				</configOption>
				<configOption name="consumer">
					<synopsis>Consumer resource id</synopsis>
				</configOption>
			</configObject>
		</configFile>
	</configInfo>
 ***/


#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/res_kafka.h"

#include "asterisk/module.h"
#include "asterisk/sorcery.h"
#include "asterisk/astobj2.h"
#include "asterisk/cli.h"

#include "librdkafka/rdkafka.h"

#define KAFKA_CONFIG_FILENAME "kafka.conf"

#define KAFKA_CLUSTER "cluster"
#define KAFKA_TOPIC "topic"
#define KAFKA_PRODUCER "producer"
#define KAFKA_CONSUMER "consumer"
#define KAFKA_ERRSTR_MAX_SIZE 80

/*! Kafka cluster common parameters */
struct sorcery_kafka_cluster {
	SORCERY_OBJECT(defails);
	AST_DECLARE_STRING_FIELDS(
		/*! Initial (bootstrap) CSV list of brokers or host:port */
		AST_STRING_FIELD(brokers);
		/*! Security protocol used to communicate with broker */
		AST_STRING_FIELD(security_protocol);
		/*! SASL mechanism used to authenticate */
		AST_STRING_FIELD(sasl_mechanism);
		/*! SASL authentication username */
		AST_STRING_FIELD(sasl_username);
		/*! SASL authentication password */
		AST_STRING_FIELD(sasl_password);
		/*! Client identifier */
		AST_STRING_FIELD(client_id);
	);
	/*! Broker's port */
	unsigned int port;
	/*! Broker must use SSL connection */
	unsigned int ssl;
	/*! rdkafka cluster object */
	rd_kafka_t *cluster;
};

/*! Kafka producer common parameters */
struct sorcery_kafka_producer {
	SORCERY_OBJECT(defails);
	AST_DECLARE_STRING_FIELDS(
		/*! Cluster resource id */
		AST_STRING_FIELD(cluster_id);
	);
};

/*! Kafka consumer common parameters */
struct sorcery_kafka_consumer {
	SORCERY_OBJECT(defails);
	AST_DECLARE_STRING_FIELDS(
		/*! Cluster resource id */
		AST_STRING_FIELD(cluster_id);
	);
};

/*! Kafka topic common parameters */
struct sorcery_kafka_topic {
	SORCERY_OBJECT(defails);
	AST_DECLARE_STRING_FIELDS(
		/*! Kafka topic name */
		AST_STRING_FIELD(topic);
		/*! Producer resource id */
		AST_STRING_FIELD(producer_id);
		/*! Consumer resource id */
		AST_STRING_FIELD(consumer_id);
	);
};

struct kafka_producer {
	rd_kafka_t *rd_kafka;
};

struct kafka_producer_topic {
	rd_kafka_topic_t *rd_kafka_topic;
};


static char *handle_kafka_show_version(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
//static rd_kafka_t *kafka_get_producer(struct sorcery_kafka_cluster *cluster);

static void on_producer_created(const void *obj);
static void on_producer_updated(const void *obj);
static void on_producer_deleted(const void *obj);
static void on_producer_loaded(const char *type);


static void process_all_clusters(void);
static void process_cluster(const struct sorcery_kafka_cluster *cluster);
static int process_producer(const struct sorcery_kafka_cluster *sorcery_cluster, const struct sorcery_kafka_producer *sorcery_producer);
static int process_consumer(const struct sorcery_kafka_cluster *cluster, const struct sorcery_kafka_consumer *consumer);

static struct kafka_producer *new_kafka_producer(const struct sorcery_kafka_cluster *sorcery_cluster, const struct sorcery_kafka_producer *sorcery_producer);
static void kafka_producer_destructor(void *obj);

static rd_kafka_conf_t *build_rdkafka_cluster_config(const struct sorcery_kafka_cluster *cluster);

static void process_producer_topic(struct kafka_producer *producer, const struct sorcery_kafka_topic *sorcery_topic);
static void process_consumer_topic(const struct sorcery_kafka_cluster *sorcery_cluster, const struct sorcery_kafka_consumer *sorcery_consumer, const struct sorcery_kafka_topic *sorcery_topic);

static struct kafka_producer_topic *new_kafka_producer_topic(struct kafka_producer *producer, const struct sorcery_kafka_topic *sorcery_topic);
static void kafka_producer_topic_destructor(void *obj);

static int sorcery_object_register(const char *type, void *(*alloc)(const char *name),int (*apply)(const struct ast_sorcery *sorcery, void *obj));
static int sorcery_kafka_topic_apply_handler(const struct ast_sorcery *sorcery, void *obj);
static void *sorcery_kafka_topic_alloc(const char *name);
static void sorcery_kafka_topic_destructor(void *obj);
static int sorcery_kafka_producer_apply_handler(const struct ast_sorcery *sorcery, void *obj);
static void *sorcery_kafka_producer_alloc(const char *name);
static void sorcery_kafka_producer_destructor(void *obj);
static int sorcery_kafka_consumer_apply_handler(const struct ast_sorcery *sorcery, void *obj);
static void *sorcery_kafka_consumer_alloc(const char *name);
static void sorcery_kafka_consumer_destructor(void *obj);
static int sorcery_kafka_cluster_apply_handler(const struct ast_sorcery *sorcery, void *obj);
static void *sorcery_kafka_cluster_alloc(const char *name);
static void sorcery_kafka_cluster_destructor(void *obj);


static struct ast_cli_entry kafka_cli[] = {
	AST_CLI_DEFINE(handle_kafka_show_version, "Show the version of librdkafka in use"),
};

static const struct ast_sorcery_observer producer_observers = {
	.created = on_producer_created,
	.updated = on_producer_updated,
	.deleted = on_producer_deleted,
	.loaded = on_producer_loaded,
};

/*! Sorcery */
static struct ast_sorcery *kafka_sorcery;


static char *handle_kafka_show_version(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a) {
	switch(cmd) {
	case CLI_INIT:
		e->command = "kafka show version";
		e->usage =
			"Usage: kafka show version\n"
			"       Show the version of librdkafka that res_kafka is running against\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	default:
		break;
	}

	ast_cli(a->fd, "librdkafka version currently running against: %s\n", rd_kafka_version_str());

	return CLI_SUCCESS;
}


#if 0
static rd_kafka_t *kafka_get_producer(struct sorcery_kafka_cluster *cluster) {
	rd_kafka_conf_t *config = rd_kafka_conf_new();
	char *errstr = ast_alloca(KAFKA_ERRSTR_MAX_SIZE);
	rd_kafka_t *producer;

	if(NULL == config) {
		ast_log(LOG_ERROR, "Kafka cluster %s: unable to create configuration object\n", ast_sorcery_object_get_id(cluster));
		return NULL;
	}

	if(RD_KAFKA_CONF_OK != rd_kafka_conf_set(config, "metadata.broker.list", cluster->brokers, errstr, KAFKA_ERRSTR_MAX_SIZE)) {
		ast_log(LOG_ERROR, "Kafka cluster %s: unable to set bootstrap brokers because %s\n", ast_sorcery_object_get_id(cluster), errstr);
		rd_kafka_conf_destroy(config);
		return NULL;
	}

	if(RD_KAFKA_CONF_OK != rd_kafka_conf_set(config, "security.protocol", cluster->security_protocol, errstr, KAFKA_ERRSTR_MAX_SIZE)) {
		ast_log(LOG_ERROR, "Kafka cluster %s: unable to set security protocol because %s\n", ast_sorcery_object_get_id(cluster), errstr);
		rd_kafka_conf_destroy(config);
		return NULL;
	}

	if(RD_KAFKA_CONF_OK != rd_kafka_conf_set(config, "sasl.mechanism", cluster->sasl_mechanism, errstr, KAFKA_ERRSTR_MAX_SIZE)) {
		ast_log(LOG_ERROR, "Kafka cluster %s: unable to set SASL mechanism because %s\n", ast_sorcery_object_get_id(cluster), errstr);
		rd_kafka_conf_destroy(config);
		return NULL;
	}

	if(RD_KAFKA_CONF_OK != rd_kafka_conf_set(config, "sasl.username", cluster->sasl_username, errstr, KAFKA_ERRSTR_MAX_SIZE)) {
		ast_log(LOG_ERROR, "Kafka cluster %s: unable to set SASL username because %s\n", ast_sorcery_object_get_id(cluster), errstr);
		rd_kafka_conf_destroy(config);
		return NULL;
	}

	if(RD_KAFKA_CONF_OK != rd_kafka_conf_set(config, "sasl.password", cluster->sasl_password, errstr, KAFKA_ERRSTR_MAX_SIZE)) {
		ast_log(LOG_ERROR, "Kafka cluster %s: unable to set SASL password because %s\n", ast_sorcery_object_get_id(cluster), errstr);
		rd_kafka_conf_destroy(config);
		return NULL;
	}

	producer = rd_kafka_new(RD_KAFKA_PRODUCER, config, errstr, KAFKA_ERRSTR_MAX_SIZE);

	if(NULL == producer) {
		ast_log(LOG_ERROR, "Kafka cluster %s: unable to create producer because %s\n", ast_sorcery_object_get_id(cluster), errstr);
		rd_kafka_conf_destroy(config);
	}

	return producer;
}
#endif


static void on_producer_created(const void *obj) {
	const struct sorcery_kafka_producer *producer = obj;

	ast_debug(3, "on_producer_created %s (%p)\n", ast_sorcery_object_get_id(producer), producer);
}

static void on_producer_updated(const void *obj) {
	const struct sorcery_kafka_producer *producer = obj;

	ast_debug(3, "on_producer_updated %s (%p)\n", ast_sorcery_object_get_id(producer), producer);
}

static void on_producer_deleted(const void *obj) {
	const struct sorcery_kafka_producer *producer = obj;

	ast_debug(3, "on_producer_deleted %s (%p)\n", ast_sorcery_object_get_id(producer), producer);
}

static void on_producer_loaded(const char *type) {
	ast_debug(3, "on_producer_loaded %s\n", type);
}





/*! Process all defined in configuration file clusters */
static void process_all_clusters(void) {
	RAII_VAR(struct ao2_container *, clusters, 
		ast_sorcery_retrieve_by_fields(kafka_sorcery, KAFKA_CLUSTER, AST_RETRIEVE_FLAG_MULTIPLE | AST_RETRIEVE_FLAG_ALL, NULL), ao2_cleanup);

	if(clusters) {
		struct ao2_iterator i = ao2_iterator_init(clusters, 0);
		struct sorcery_kafka_cluster *cluster;

		while(NULL != (cluster = ao2_iterator_next(&i))) {
			process_cluster(cluster);

			ao2_ref(cluster, -1);
		}

		ao2_iterator_destroy(&i);
	}
}

/*! Process Kafka cluster */
static void process_cluster(const struct sorcery_kafka_cluster *cluster) {
	RAII_VAR(struct ast_variable *, filter, ast_variable_new("cluster", ast_sorcery_object_get_id(cluster), ""), ast_variables_destroy);
	RAII_VAR(struct ao2_container *, found, NULL, ao2_cleanup);

	ast_debug(3, "Kafka cluster at %s (%p)\n", ast_sorcery_object_get_id(cluster), cluster);

	if(NULL == (found = ast_sorcery_retrieve_by_fields(kafka_sorcery, KAFKA_PRODUCER, AST_RETRIEVE_FLAG_MULTIPLE, filter))) {
		ast_log(LOG_WARNING, "Unable to retrieve producers from cluster %s\n", ast_sorcery_object_get_id(cluster));
	} else {
		struct ao2_iterator i = ao2_iterator_init(found, 0);
		struct sorcery_kafka_producer *producer;

		while(NULL != (producer = ao2_iterator_next(&i))) {
			process_producer(cluster, producer);

			ao2_ref(producer, -1);
		}

		ao2_iterator_destroy(&i);

		/* Producers processing complete */
		ao2_cleanup(found);
		found = NULL;
	}

	if(NULL == (found = ast_sorcery_retrieve_by_fields(kafka_sorcery, KAFKA_CONSUMER, AST_RETRIEVE_FLAG_MULTIPLE, filter))) {
		ast_log(LOG_WARNING, "Unable to retrieve consumers from cluster %s\n", ast_sorcery_object_get_id(cluster));
	} else {
		struct ao2_iterator i = ao2_iterator_init(found, 0);
		struct sorcery_kafka_consumer *consumer;

		while(NULL != (consumer = ao2_iterator_next(&i))) {
			process_consumer(cluster, consumer);

			ao2_ref(consumer, -1);
		}

		ao2_iterator_destroy(&i);
	}
}

/*! Process Kafka producer at the cluster */
static int process_producer(const struct sorcery_kafka_cluster *sorcery_cluster, const struct sorcery_kafka_producer *sorcery_producer) {
	RAII_VAR(struct ast_variable *, filter, ast_variable_new("producer", ast_sorcery_object_get_id(sorcery_producer), ""), ast_variables_destroy);
	RAII_VAR(struct ao2_container *, found, NULL, ao2_cleanup);

	ast_debug(3, "Process Kafka producer %s on cluster %s\n", ast_sorcery_object_get_id(sorcery_producer), ast_sorcery_object_get_id(sorcery_cluster));

	if(NULL == (found = ast_sorcery_retrieve_by_fields(kafka_sorcery, KAFKA_TOPIC, AST_RETRIEVE_FLAG_MULTIPLE, filter))) {
		ast_log(LOG_WARNING, "Unable to retrieve topics from producer %s at cluster %s\n", ast_sorcery_object_get_id(sorcery_producer), ast_sorcery_object_get_id(sorcery_cluster));
	} else {
		if(ao2_container_count(found)) {
			/* Producers present */
			struct kafka_producer *producer = new_kafka_producer(sorcery_cluster, sorcery_producer);

			if(NULL == producer) {
				return -1;
			} else {
				struct ao2_iterator i = ao2_iterator_init(found, 0);
				struct sorcery_kafka_topic *sorcery_topic;

				while(NULL != (sorcery_topic = ao2_iterator_next(&i))) {
					process_producer_topic(producer, sorcery_topic);

					ao2_ref(sorcery_topic, -1);
				}

				ao2_iterator_destroy(&i);

				ao2_ref(producer, -1);
			}
		}
	}

	return 0;
}

/*! Process Kafka consumer at the cluster */
static int process_consumer(const struct sorcery_kafka_cluster *cluster, const struct sorcery_kafka_consumer *consumer) {
	RAII_VAR(struct ast_variable *, filter, ast_variable_new("consumer", ast_sorcery_object_get_id(consumer), ""), ast_variables_destroy);
	RAII_VAR(struct ao2_container *, found, NULL, ao2_cleanup);
	rd_kafka_conf_t *config = build_rdkafka_cluster_config(cluster);

	ast_debug(3, "Process Kafka consumer %s on cluster %s\n", ast_sorcery_object_get_id(consumer),ast_sorcery_object_get_id(cluster));

	if(NULL == config) {
		return -1;
	}

	rd_kafka_conf_destroy(config);


	if(NULL == (found = ast_sorcery_retrieve_by_fields(kafka_sorcery, KAFKA_TOPIC, AST_RETRIEVE_FLAG_MULTIPLE, filter))) {
		ast_log(LOG_WARNING, "Unable to retrieve topics from consumer %s at cluster %s\n", ast_sorcery_object_get_id(consumer), ast_sorcery_object_get_id(cluster));
	} else {
		if(ao2_container_count(found)) {
			/* Consumers present */
			rd_kafka_conf_t *config = build_rdkafka_cluster_config(cluster);
			struct ao2_iterator i;
			struct sorcery_kafka_topic *topic;

			if(NULL == config) {
				return -1;
			}

			rd_kafka_conf_destroy(config); //!!!

			i = ao2_iterator_init(found, 0);
			while(NULL != (topic = ao2_iterator_next(&i))) {
				process_consumer_topic(cluster, consumer, topic);

				ao2_ref(topic, -1);
			}

			ao2_iterator_destroy(&i);
		}
	}

	return 0;
}

static struct kafka_producer *new_kafka_producer(const struct sorcery_kafka_cluster *sorcery_cluster, const struct sorcery_kafka_producer *sorcery_producer) {
	rd_kafka_conf_t *config = build_rdkafka_cluster_config(sorcery_cluster);
	struct kafka_producer *producer;

	if(NULL == config) {
		return NULL;
	}

	if(NULL == (producer = ao2_alloc(sizeof(*producer), kafka_producer_destructor))) {
		ast_log(LOG_ERROR, "Kafka cluster '%s': Unable to create producer '%s' because Out of memory\n", ast_sorcery_object_get_id(sorcery_cluster), ast_sorcery_object_get_id(sorcery_producer));
	} else {
		char *errstr = ast_alloca(KAFKA_ERRSTR_MAX_SIZE);

		if(NULL == (producer->rd_kafka = rd_kafka_new(RD_KAFKA_PRODUCER, config, errstr, KAFKA_ERRSTR_MAX_SIZE))) {
			ast_log(LOG_ERROR, "Kafka cluster '%s': unable to create producer '%s' because %s\n", ast_sorcery_object_get_id(sorcery_cluster), ast_sorcery_object_get_id(sorcery_producer), errstr);
		} else {
			return producer;
		}
	}

	rd_kafka_conf_destroy(config);

	ao2_cleanup(producer);

	return NULL;
}

static void kafka_producer_destructor(void *obj) {
	struct kafka_producer *producer = obj;

	if(producer->rd_kafka) {
		ast_debug(3, "Destroy rd_kafka_t object %p on producer %p\n", producer->rd_kafka, producer);
		rd_kafka_destroy(producer->rd_kafka);
	}
}

/*! Build common rdkafka cluster configuration */
static rd_kafka_conf_t *build_rdkafka_cluster_config(const struct sorcery_kafka_cluster *cluster) {
	rd_kafka_conf_t *config = rd_kafka_conf_new();
	char *errstr = ast_alloca(KAFKA_ERRSTR_MAX_SIZE);

	if(NULL == config) {
		ast_log(LOG_ERROR, "Kafka cluster %s: unable to create configuration object\n", ast_sorcery_object_get_id(cluster));
		return NULL;
	}

	if(RD_KAFKA_CONF_OK != rd_kafka_conf_set(config, "metadata.broker.list", cluster->brokers, errstr, KAFKA_ERRSTR_MAX_SIZE)) {
		ast_log(LOG_ERROR, "Kafka cluster %s: unable to set bootstrap brokers because %s\n", ast_sorcery_object_get_id(cluster), errstr);
		rd_kafka_conf_destroy(config);
		return NULL;
	}

	if(RD_KAFKA_CONF_OK != rd_kafka_conf_set(config, "security.protocol", cluster->security_protocol, errstr, KAFKA_ERRSTR_MAX_SIZE)) {
		ast_log(LOG_ERROR, "Kafka cluster %s: unable to set security protocol because %s\n", ast_sorcery_object_get_id(cluster), errstr);
		rd_kafka_conf_destroy(config);
		return NULL;
	}

	if(RD_KAFKA_CONF_OK != rd_kafka_conf_set(config, "sasl.mechanism", cluster->sasl_mechanism, errstr, KAFKA_ERRSTR_MAX_SIZE)) {
		ast_log(LOG_ERROR, "Kafka cluster %s: unable to set SASL mechanism because %s\n", ast_sorcery_object_get_id(cluster), errstr);
		rd_kafka_conf_destroy(config);
		return NULL;
	}

	if(RD_KAFKA_CONF_OK != rd_kafka_conf_set(config, "sasl.username", cluster->sasl_username, errstr, KAFKA_ERRSTR_MAX_SIZE)) {
		ast_log(LOG_ERROR, "Kafka cluster %s: unable to set SASL username because %s\n", ast_sorcery_object_get_id(cluster), errstr);
		rd_kafka_conf_destroy(config);
		return NULL;
	}

	if(RD_KAFKA_CONF_OK != rd_kafka_conf_set(config, "sasl.password", cluster->sasl_password, errstr, KAFKA_ERRSTR_MAX_SIZE)) {
		ast_log(LOG_ERROR, "Kafka cluster %s: unable to set SASL password because %s\n", ast_sorcery_object_get_id(cluster), errstr);
		rd_kafka_conf_destroy(config);
		return NULL;
	}

	return config;
}

/*! Process Kafka producer related topic at the cluster */
static void process_producer_topic(struct kafka_producer *producer, const struct sorcery_kafka_topic *sorcery_topic) {
	struct kafka_producer_topic *topic;
	rd_kafka_resp_err_t response;

	ast_debug(3, "Process Kafka topic %s for producer %p\n", ast_sorcery_object_get_id(sorcery_topic), producer);

	topic = new_kafka_producer_topic(producer, sorcery_topic);

	if(rd_kafka_produce(topic->rd_kafka_topic, RD_KAFKA_PARTITION_UA, RD_KAFKA_MSG_F_COPY, "test", 4, NULL, 0, NULL)) {
		ast_log(LOG_ERROR, "Unable to produce message producer topic '%s' because %s\n", ast_sorcery_object_get_id(sorcery_topic), rd_kafka_err2str(rd_kafka_last_error()));
	}

	if(RD_KAFKA_RESP_ERR_NO_ERROR == (response = rd_kafka_flush(producer->rd_kafka, 10000))) {
		ast_log(LOG_ERROR, "Kafka producer topic %s got error: %s\n", ast_sorcery_object_get_id(sorcery_topic), rd_kafka_err2str(response));
	}

	ao2_cleanup(topic);
}

/*! Process Kafka consumer related topic at the cluster */
static void process_consumer_topic(const struct sorcery_kafka_cluster *sorcery_cluster, const struct sorcery_kafka_consumer *sorcery_consumer, const struct sorcery_kafka_topic *sorcery_topic) {
	ast_debug(3, "Process Kafka topic %s for consumer %s on cluster %s\n", ast_sorcery_object_get_id(sorcery_topic), ast_sorcery_object_get_id(sorcery_consumer), ast_sorcery_object_get_id(sorcery_cluster));
}




static struct kafka_producer_topic *new_kafka_producer_topic(struct kafka_producer *producer, const struct sorcery_kafka_topic *sorcery_topic) {
	struct kafka_producer_topic *topic = ao2_alloc(sizeof(*topic), kafka_producer_topic_destructor);

	if(NULL == topic) {
		ast_log(LOG_ERROR, "Out of memory while create producer topic '%s'\n", ast_sorcery_object_get_id(sorcery_topic));
	} else {
		rd_kafka_topic_conf_t *config = rd_kafka_topic_conf_new();

		topic->rd_kafka_topic = NULL;

		if(NULL == config) {
			ast_log(LOG_ERROR, "Unable to create config for producer topic '%s'\n", ast_sorcery_object_get_id(sorcery_topic));
			ao2_ref(topic, -1);
			return NULL;
		}

		/* With topic's callbacks we want see the kafka_producer_topic structure reference */
		rd_kafka_topic_conf_set_opaque(config, topic);
		ao2_ref(topic, +1); //!!! круговая зависимость!!!

		if(NULL == (topic->rd_kafka_topic = rd_kafka_topic_new(producer->rd_kafka, sorcery_topic->topic, config))) {
			ast_log(LOG_ERROR, "Unable to create producer topic '%s' because %s\n", ast_sorcery_object_get_id(sorcery_topic), rd_kafka_err2str(rd_kafka_last_error()));
			rd_kafka_topic_conf_destroy(config);
			ao2_ref(topic, -1);

			/* This object not usable */
			ao2_ref(topic, -1);
			return NULL;
		}
	}

	return topic;
}

static void kafka_producer_topic_destructor(void *obj) {
	struct kafka_producer_topic *topic = obj;

	if(topic->rd_kafka_topic) {
		ast_debug(3, "Destroy rd_kafka_topic_t object %p on producer topic %p\n", topic->rd_kafka_topic, topic);
		rd_kafka_topic_destroy(topic->rd_kafka_topic);
	}
}





/*! Common register sorcery object actions */
static int sorcery_object_register(const char *type, void *(*alloc)(const char *name),int (*apply)(const struct ast_sorcery *sorcery, void *obj)) {
	char *options = ast_alloca(80);

	sprintf(options, KAFKA_CONFIG_FILENAME ",criteria=type=%s", type);

	if(AST_SORCERY_APPLY_SUCCESS != ast_sorcery_apply_default(kafka_sorcery, type, "config", options)) {
		ast_log(LOG_NOTICE, "Failed to apply defaults for Kafka sorcery %s\n", type);
	}

	if(ast_sorcery_object_register(kafka_sorcery, type, alloc, NULL, apply)) {
		ast_log(LOG_ERROR, "Failed to register '%s' with Kafka sorcery.\n", type);
		return -1;
	}

	ast_sorcery_object_field_register(kafka_sorcery, type, "type", "", OPT_NOOP_T, 0, 0);

	return 0;
}


static int sorcery_kafka_topic_apply_handler(const struct ast_sorcery *sorcery, void *obj) {
	struct sorcery_kafka_topic *topic = obj;

	ast_debug(3, "Apply Kafka topic %s (%p)\n", ast_sorcery_object_get_id(topic), topic);

	return 0;
}

static void *sorcery_kafka_topic_alloc(const char *name) {
	struct sorcery_kafka_topic *topic = ast_sorcery_generic_alloc(sizeof(*topic), sorcery_kafka_topic_destructor);

	if(NULL == topic) {
		return NULL;
	}

	if(ast_string_field_init(topic, 64)) {
		ao2_cleanup(topic);
		return NULL;
	}

	ast_debug(3, "Allocated Kafka topic %s (%p)\n", name, topic);

	return topic;
}

static void sorcery_kafka_topic_destructor(void *obj) {
	struct sorcery_kafka_topic *topic = obj;

	ast_debug(3, "Destroyed Kafka topic %s (%p)\n", ast_sorcery_object_get_id(topic), topic);

	ast_string_field_free_memory(topic);
}

static int sorcery_kafka_producer_apply_handler(const struct ast_sorcery *sorcery, void *obj) {
	struct sorcery_kafka_producer *producer = obj;

	ast_debug(3, "Apply Kafka producer %s (%p)\n", ast_sorcery_object_get_id(producer), producer);

	return 0;
}

static void *sorcery_kafka_producer_alloc(const char *name) {
	struct sorcery_kafka_producer *producer = ast_sorcery_generic_alloc(sizeof(*producer), sorcery_kafka_producer_destructor);

	if(NULL == producer) {
		return NULL;
	}

	if(ast_string_field_init(producer, 64)) {
		ao2_cleanup(producer);
		return NULL;
	}

	ast_debug(3, "Allocated Kafka producer %s (%p)\n", name, producer);

	return producer;
}

static void sorcery_kafka_producer_destructor(void *obj) {
	struct sorcery_kafka_producer *producer = obj;

	ast_debug(3, "Destroyed Kafka producer %s (%p)\n", ast_sorcery_object_get_id(producer), producer);

	ast_string_field_free_memory(producer);
}

static int sorcery_kafka_consumer_apply_handler(const struct ast_sorcery *sorcery, void *obj) {
	struct sorcery_kafka_consumer *consumer = obj;

	ast_debug(3, "Apply Kafka consumer %s (%p)\n", ast_sorcery_object_get_id(consumer), consumer);

	return 0;
}

static void *sorcery_kafka_consumer_alloc(const char *name) {
	struct sorcery_kafka_consumer *consumer = ast_sorcery_generic_alloc(sizeof(*consumer), sorcery_kafka_consumer_destructor);

	if(NULL == consumer) {
		return NULL;
	}

	if(ast_string_field_init(consumer, 64)) {
		ao2_cleanup(consumer);
		return NULL;
	}

	ast_debug(3, "Allocated Kafka consumer %s (%p)\n", name, consumer);

	return consumer;
}

static void sorcery_kafka_consumer_destructor(void *obj) {
	struct sorcery_kafka_consumer *consumer = obj;

	ast_debug(3, "Destroyed Kafka consumer %s (%p)\n", ast_sorcery_object_get_id(consumer), consumer);

	ast_string_field_free_memory(consumer);
}

static int sorcery_kafka_cluster_apply_handler(const struct ast_sorcery *sorcery, void *obj) {
	struct sorcery_kafka_cluster *cluster = obj;

//	rd_kafka_t *producer = kafka_get_producer(cluster);

	ast_debug(3, "Apply Kafka cluster %s (%p): brokers=%s client_id=%s\n", ast_sorcery_object_get_id(cluster), cluster, cluster->brokers, cluster->client_id);

#if 0
	if(NULL != producer) {
		rd_kafka_resp_err_t response;
		const struct rd_kafka_metadata *metadata;

		ast_debug(3, "Kafka cluster %s (%p) create producer %p\n", ast_sorcery_object_get_id(cluster), cluster, producer);

		if(RD_KAFKA_RESP_ERR_NO_ERROR == (response = rd_kafka_metadata(producer, 1, NULL, &metadata, 10000))) {
			rd_kafka_metadata_destroy(metadata);
		} else {
			ast_log(LOG_ERROR, "Kafka cluster %s get metadata got error: %s\n", ast_sorcery_object_get_id(cluster), rd_kafka_err2str(response));
		}

		rd_kafka_destroy(producer);
	}
#endif


	return 0;
}

static void *sorcery_kafka_cluster_alloc(const char *name) {
	struct sorcery_kafka_cluster *cluster = ast_sorcery_generic_alloc(sizeof(*cluster), sorcery_kafka_cluster_destructor);

	if(NULL == cluster) {
		return NULL;
	}

	if(ast_string_field_init(cluster, 64)) {
		ao2_cleanup(cluster);
		return NULL;
	}

	ast_debug(3, "Allocated Kafka cluster %s (%p)\n", name, cluster);

	return cluster;
}

static void sorcery_kafka_cluster_destructor(void *obj) {
	struct sorcery_kafka_cluster *cluster = obj;

	ast_debug(3, "Destroyed Kafka cluster %s (%p)\n", ast_sorcery_object_get_id(cluster), cluster);

	ast_string_field_free_memory(cluster);
}

static int load_module(void) {
	if(NULL == (kafka_sorcery = ast_sorcery_open())) {
		ast_log(LOG_ERROR, "Failed to open Kafka sorcery.\n");

		return AST_MODULE_LOAD_DECLINE;
	}

	if(sorcery_object_register(KAFKA_CLUSTER, sorcery_kafka_cluster_alloc, sorcery_kafka_cluster_apply_handler)) {
		ast_sorcery_unref(kafka_sorcery);
		kafka_sorcery = NULL;
		return AST_MODULE_LOAD_DECLINE;
	}

	ast_sorcery_object_field_register(kafka_sorcery, KAFKA_CLUSTER, "brokers", "localhost", OPT_STRINGFIELD_T, 0, STRFLDSET(struct sorcery_kafka_cluster, brokers));
	ast_sorcery_object_field_register(kafka_sorcery, KAFKA_CLUSTER, "security_protocol", "plaintext", OPT_STRINGFIELD_T, 0, STRFLDSET(struct sorcery_kafka_cluster, security_protocol));
	ast_sorcery_object_field_register(kafka_sorcery, KAFKA_CLUSTER, "sasl_mechanism", "PLAIN", OPT_STRINGFIELD_T, 0, STRFLDSET(struct sorcery_kafka_cluster, sasl_mechanism));
	ast_sorcery_object_field_register(kafka_sorcery, KAFKA_CLUSTER, "sasl_username", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct sorcery_kafka_cluster, sasl_username));
	ast_sorcery_object_field_register(kafka_sorcery, KAFKA_CLUSTER, "sasl_password", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct sorcery_kafka_cluster, sasl_password));
	ast_sorcery_object_field_register(kafka_sorcery, KAFKA_CLUSTER, "client_id", "asterisk", OPT_STRINGFIELD_T, 0, STRFLDSET(struct sorcery_kafka_cluster, client_id));
	ast_sorcery_object_field_register(kafka_sorcery, KAFKA_CLUSTER, "port", "1883", OPT_UINT_T, 0, FLDSET(struct sorcery_kafka_cluster, port));
	ast_sorcery_object_field_register(kafka_sorcery, KAFKA_CLUSTER, "ssl", "no", OPT_BOOL_T, 1, FLDSET(struct sorcery_kafka_cluster, ssl));

	if(sorcery_object_register(KAFKA_TOPIC, sorcery_kafka_topic_alloc, sorcery_kafka_topic_apply_handler)) {
		ast_sorcery_unref(kafka_sorcery);
		kafka_sorcery = NULL;
		return AST_MODULE_LOAD_DECLINE;
	}

	ast_sorcery_object_field_register(kafka_sorcery, KAFKA_TOPIC, "topic", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct sorcery_kafka_topic, topic));
	ast_sorcery_object_field_register(kafka_sorcery, KAFKA_TOPIC, "producer", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct sorcery_kafka_topic, producer_id));
	ast_sorcery_object_field_register(kafka_sorcery, KAFKA_TOPIC, "consumer", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct sorcery_kafka_topic, consumer_id));

	if(sorcery_object_register(KAFKA_PRODUCER, sorcery_kafka_producer_alloc, sorcery_kafka_producer_apply_handler)) {
		ast_sorcery_unref(kafka_sorcery);
		kafka_sorcery = NULL;
		return AST_MODULE_LOAD_DECLINE;
	}

	ast_sorcery_object_field_register(kafka_sorcery, KAFKA_PRODUCER, "cluster", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct sorcery_kafka_producer, cluster_id));



	if(ast_sorcery_observer_add(kafka_sorcery, KAFKA_PRODUCER, &producer_observers)) {
		ast_log(LOG_ERROR, "Failed to register observer for '%s' with Kafka sorcery.\n", KAFKA_PRODUCER);
		ast_sorcery_unref(kafka_sorcery);
		kafka_sorcery = NULL;
		return AST_MODULE_LOAD_DECLINE;
	}



	if(sorcery_object_register(KAFKA_CONSUMER, sorcery_kafka_consumer_alloc, sorcery_kafka_consumer_apply_handler)) {
		ast_sorcery_unref(kafka_sorcery);
		kafka_sorcery = NULL;
		return AST_MODULE_LOAD_DECLINE;
	}

	ast_sorcery_object_field_register(kafka_sorcery, KAFKA_CONSUMER, "cluster", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct sorcery_kafka_consumer, cluster_id));


	/* Load all registered objects */
	ast_sorcery_load(kafka_sorcery);

	/* Process all defined clusters */
	process_all_clusters();




	ast_cli_register_multiple(kafka_cli, ARRAY_LEN(kafka_cli));

	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void) {
	ast_sorcery_observer_remove(kafka_sorcery, KAFKA_PRODUCER, &producer_observers);

	ast_cli_unregister_multiple(kafka_cli, ARRAY_LEN(kafka_cli));

	ast_sorcery_unref(kafka_sorcery);
	kafka_sorcery = NULL;


	return 0;
}

static int reload_module(void) {
	ast_sorcery_reload(kafka_sorcery);

	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "Kafka resources",
	.support_level = AST_MODULE_SUPPORT_EXTENDED,
	.load = load_module,
	.unload = unload_module,
	.reload = reload_module,
	);

/* PipeWire
 *
 * Copyright © 2020 Wim Taymans
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "config.h"

#include <ctype.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <strings.h>
#include <time.h>

#include <spa/utils/defs.h>
#include <spa/utils/json.h>
#include <spa/utils/string.h>

#include <pipewire/pipewire.h>

#include "media-session.h"

/** \page page_media_session_module_access_default Media Session Module: Access Default
 *
 * The Access Default module manages permissions for clients. It monitors clients and
 * applies permissions based on the rules specified in the module's configuration file.
 * The permissions are only set once and for all objects.
 *
 * ## Configuration
 *
 * This module loads the `alsa-monitor.conf` configuration file. The main
 * component in that file is the `rules = []` array that consists of multiple
 * dictionaries that `matches` a client and specifying `actions` to take.
 *
 * The following `actions` are supported:
 * - `default-permissions`: set the permissions of the matched client
 *
 * The value of `default-permissions` may either be the string "all"
 * for granting all permissions or a combination of `r` (read), `w` (write),
 * and `x` (execute) characters.
 *
 * Only the first rule that matches will be applied.
 *
 * For example:
 * ```
 * rules = [
 *     {
 *         matches = [
 *             { pipewire.access = "flatpak", media.category = "Manager" }
 *         ]
 *         actions = {
 *             default-permissions = "all"
 *         }
 *     }
 *     {
 *         matches = [
 *             { pipewire.access = "flatpak" }
 *             { pipewire.access = "restricted" }
 *         ]
 *         actions = {
 *             default-permissions = "rx"
 *         }
 *     }
 * ]
 * ```
 */

#define NAME		"access-default"
#define SESSION_KEY	NAME
#define SESSION_CONF	NAME ".conf"

PW_LOG_TOPIC_STATIC(mod_topic, "ms.mod." NAME);
#define PW_LOG_TOPIC_DEFAULT mod_topic

struct impl {
	struct sm_media_session *session;
	struct spa_hook listener;

	struct spa_list client_list;

	char *rules;
	size_t rules_length;
};

struct client {
	struct spa_list link;		/**< link in impl client_list */

	struct sm_client *sm_client;
	struct spa_hook listener;

	struct impl *impl;

	unsigned int handled:1;
};

static uint32_t parse_permissions(const char *perms)
{
	if (strcasecmp(perms, "all") == 0)
		return PW_PERM_ALL;

	uint32_t p = 0;

	for (; *perms; perms++) {
		switch (tolower(*perms)) {
		case 'r': p |= PW_PERM_R; break;
		case 'w': p |= PW_PERM_W; break;
		case 'x': p |= PW_PERM_X; break;
		case 'm': p |= PW_PERM_M; break;
		case '-': /* do nothing */ break;
		default: return PW_PERM_INVALID;
		}
	}

	return p;
}

static int handle_default_permission(void *data, const char *location, const char *action, const char *value, size_t length)
{
	if (spa_streq(action, "default-permissions")) {
		uint32_t *perms = data;
		if (*perms != PW_PERM_INVALID)
			return 0;

		if (!spa_json_is_string(value, length))
			return 0;

		char p[16];
		if (spa_json_parse_stringn(value, length, p, sizeof(p)) <= 0)
			return 0;

		*perms = parse_permissions(p);
	}

	return 0;
}

static uint32_t get_permissions(struct impl *impl, const struct spa_dict *client_props)
{
	uint32_t perm = PW_PERM_INVALID;

	pw_conf_match_rules(impl->rules, impl->rules_length, NAME, client_props,
			    handle_default_permission, &perm);

	return perm;
}

static void object_update(void *data)
{
	struct client *client = data;
	struct sm_client *sm_client = client->sm_client;
	struct impl *impl = client->impl;

	pw_log_debug("%p: client %p: changed:0x%08x avail:0x%08x handled:%d",
			impl, client,
			sm_client->obj.changed, sm_client->obj.avail,
			client->handled);

	if (client->handled || !SPA_FLAG_IS_SET(sm_client->obj.avail, SM_CLIENT_CHANGE_MASK_INFO))
		return;

	struct pw_client_info *info = sm_client->info;
	if (info == NULL || info->props == NULL)
		return;

	client->handled = true;

	uint32_t perms = get_permissions(impl, info->props);
	if (perms == PW_PERM_INVALID)
		return;

	pw_log_info("%p: client %p: id:%u granting:" PW_PERMISSION_FORMAT,
			impl, sm_client, sm_client->obj.id, PW_PERMISSION_ARGS(perms));

	const struct pw_permission permissions[] = {
		PW_PERMISSION_INIT(PW_ID_ANY, perms),
	};
	pw_client_update_permissions(client->sm_client->obj.proxy,
					SPA_N_ELEMENTS(permissions), permissions);
}

static const struct sm_object_events object_events = {
	SM_VERSION_OBJECT_EVENTS,
	.update = object_update
};

static void handle_client(struct impl *impl, struct sm_client *sm_client)
{
	struct client *client;

	pw_log_debug("%p: new client %p: id:%u", impl, sm_client, sm_client->obj.id);

	client = sm_object_add_data(&sm_client->obj, SESSION_KEY, sizeof(*client));

	client->impl = impl;
	spa_list_append(&impl->client_list, &client->link);

	client->sm_client = sm_client;
	client->sm_client->obj.mask |= SM_CLIENT_CHANGE_MASK_INFO;
	sm_object_add_listener(&client->sm_client->obj, &client->listener, &object_events, client);
}

static void destroy_client(struct client *client)
{
	spa_list_remove(&client->link);
	spa_hook_remove(&client->listener);
	sm_object_remove_data(&client->sm_client->obj, SESSION_KEY);
}

static void session_create(void *data, struct sm_object *object)
{
	pw_log_debug("%p: create global %u", data, object->id);

	if (spa_streq(object->type, PW_TYPE_INTERFACE_Client))
		handle_client(data, (struct sm_client *) object);
}

static void session_remove(void *data, struct sm_object *object)
{
	pw_log_debug("%p: remove global %u", data, object->id);

	if (spa_streq(object->type, PW_TYPE_INTERFACE_Client)) {
		struct client *client;

		if ((client = sm_object_get_data(object, SESSION_KEY)) != NULL)
			destroy_client(client);
	}
}

static void session_destroy(void *data)
{
	struct impl *impl = data;
	struct client *client;

	spa_list_consume(client, &impl->client_list, link)
		destroy_client(client);

	free(impl->rules);

	spa_hook_remove(&impl->listener);
	free(impl);
}

static const struct sm_media_session_events session_events = {
	SM_VERSION_MEDIA_SESSION_EVENTS,
	.create = session_create,
	.remove = session_remove,
	.destroy = session_destroy,
};

static int load_conf(struct impl *impl)
{
	struct pw_properties *conf = pw_properties_new(NULL, NULL);
	if (conf == NULL)
		return -errno;

	int res = sm_media_session_load_conf(impl->session, SESSION_CONF, conf);
	if (res < 0)
		goto out;

	const char *rules = pw_properties_get(conf, "rules");
	if (rules == NULL) {
		pw_log_warn("no 'rules' section in " SESSION_CONF);
		res = -ENOENT;
		goto out;
	}

	impl->rules = strdup(rules);
	if (impl->rules == NULL) {
		res = -errno;
		goto out;
	}

	impl->rules_length = strlen(impl->rules);

out:
	pw_properties_free(conf);

	return res;
}

int sm_access_default_start(struct sm_media_session *session)
{
	struct impl *impl;
	int res;

	PW_LOG_TOPIC_INIT(mod_topic);

	impl = calloc(1, sizeof(*impl));
	if (impl == NULL)
		return -errno;

	impl->session = session;

	res = load_conf(impl);
	if (res < 0)
		goto error;

	spa_list_init(&impl->client_list);

	sm_media_session_add_listener(impl->session,
			&impl->listener,
			&session_events, impl);

	return 0;

error:
	free(impl);

	return res;
}

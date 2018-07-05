/*-
 * Copyright (c) 2017-2018 GANDI SAS
 * All rights reserved.
 *
 * Author: Emmanuel Hocdet <manu@gandi.net>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "config.h"

#include <arpa/inet.h>

#include <sys/socket.h>
#include <sys/types.h>

#include <errno.h>
#include <netdb.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

#include "cache/cache.h"

#include "vcl.h"
#include "vsa.h"
#include "vtim.h"
#include "vsb.h"

#include "vcc_if.h"
#include "udir.h"
#include "dynamic.h"

#define LOG(ctx, slt, obj, fmt, ...)		\
	do {					\
		if ((ctx)->vsl != NULL)		\
			VSLb((ctx)->vsl, slt,	\
			    "dynamic: %s %s " fmt, \
			    (obj)->vcl_conf,	\
			    (obj)->vd->vcl_name,	\
			    __VA_ARGS__);	\
		else				\
			VSL(slt, 0,		\
			    "dynamic: %s %s " fmt, \
			    (obj)->vcl_conf,	\
			    (obj)->vd->vcl_name, 	\
			    __VA_ARGS__);	\
	} while (0)

#define DBG(ctx, obj, fmt, ...)						\
	do {								\
		if ((obj)->debug)					\
			LOG(ctx, SLT_Debug, obj, fmt, __VA_ARGS__);	\
	} while (0)


struct dynamic_lookup_head  unidirectors_objects = VTAILQ_HEAD_INITIALIZER(unidirectors_objects);
struct dynamic_backend_vsc_head unidirectors_vsc_clusters = VTAILQ_HEAD_INITIALIZER(unidirectors_vsc_clusters);

static struct VSC_lck *lck_lookup;

static unsigned loadcnt = 0;


static struct backend_ip *
dynamic_add(VRT_CTX, struct vmod_unidirectors_dyndirector *dyn, struct suckaddr *sa,
	    const char *ip, int af)
{
	struct vrt_backend vrt;
	struct backend_ip *b;
	struct vsb *vsb;
	struct dynamic_backend_vsc *c;
	struct vsmw_cluster *vsc = NULL;

	CHECK_OBJ_NOTNULL(dyn, VMOD_UNIDIRECTORS_DYNDIRECTOR_MAGIC);

	VTAILQ_FOREACH(b, &dyn->backends, list) {
		if (!VSA_Compare(b->ip_suckaddr, sa)) {
			b->mark = dyn->mark;
			return (NULL);
		}
	}

	b = calloc(1, sizeof *b);
	AN(b);
	b->mark = dyn->mark;
	b->ip_suckaddr = sa;
	b->ip_addr = strdup(ip);
	AN(b->ip_addr);

	vsb = VSB_new_auto();
	AN(vsb);
	VSB_printf(vsb, "%s(%s)", dyn->vd->vcl_name, b->ip_addr);
	AZ(VSB_finish(vsb));
	b->vcl_name = strdup(VSB_data(vsb));
	AN(b->vcl_name);
	VSB_delete(vsb);

	INIT_OBJ(&vrt, VRT_BACKEND_MAGIC);
	vrt.vcl_name = b->vcl_name;
	vrt.port = dyn->port;
	vrt.probe = dyn->probe;
	vrt.connect_timeout = dyn->connect_timeout;
	vrt.first_byte_timeout = dyn->first_byte_timeout;
	vrt.between_bytes_timeout = dyn->between_bytes_timeout;
	vrt.max_connections = dyn->max_connections;

	switch (af) {
	case AF_INET:
		vrt.ipv4_suckaddr = sa;
		vrt.ipv4_addr = b->ip_addr;
		break;
	case AF_INET6:
		vrt.ipv6_suckaddr = sa;
		vrt.ipv6_addr = b->ip_addr;
		break;
	default:
		WRONG("unexpected family");
	}
	VTAILQ_FOREACH(c, &unidirectors_vsc_clusters, list)
	        if (c->vcl == ctx->vcl) {
		        vsc = c->vsc_cluster;
			break;
		}
	b->be = VRT_new_backend_clustered(ctx, vsc, &vrt);
	AN(b->be);
	DBG(ctx, dyn, "add-backend %s", b->vcl_name);

	VTAILQ_INSERT_TAIL(&dyn->backends, b, list);
	return (b);
}

static void
backend_fini(VRT_CTX, struct backend_ip *b)
{
	if (ctx != NULL) 
		VRT_delete_backend(ctx, &b->be);
	free(b->vcl_name);
	free(b->ip_addr);
	free(b->ip_suckaddr);
	free(b);
}

static struct backend_ip *
dynamic_add_addr(VRT_CTX, struct vmod_unidirectors_dyndirector *dyn, VCL_ACL acl,
		    struct addrinfo *addr)
{
	struct suckaddr *sa;
	char ip[INET6_ADDRSTRLEN];
	const unsigned char *in_addr = NULL;
	unsigned match;

	sa = malloc(vsa_suckaddr_len);
	AN(sa);
	AN(VSA_Build(sa, addr->ai_addr, addr->ai_addrlen));

	(void)VRT_VSA_GetPtr(sa, &in_addr);
	AN(in_addr);
	AN(inet_ntop(addr->ai_family, in_addr, ip, sizeof ip));

	DBG(ctx, dyn, "addr %s", ip);

	match = acl != NULL ? VRT_acl_match(ctx, acl, sa) : 1;

	if (!match)
		LOG(ctx, SLT_Error, dyn, "acl-mismatch %s", ip);
	else {
		struct backend_ip *b;
		b = dynamic_add(ctx, dyn, sa, ip, addr->ai_family);
		if (b)
			return (b);
	}
	free(sa);
	return (NULL);
}

static void
dynamic_update(VRT_CTX, struct vmod_unidirectors_dyndirector *dyn, VCL_ACL acl,
	       struct addrinfo *addr)
{
	struct backend_ip *b, *b2;
	struct vmod_unidirectors_director *vd;

	CHECK_OBJ_NOTNULL(dyn, VMOD_UNIDIRECTORS_DYNDIRECTOR_MAGIC);
	vd = dyn->vd;
	CHECK_OBJ_NOTNULL(vd, VMOD_UNIDIRECTORS_DIRECTOR_MAGIC);

	AZ(pthread_mutex_lock(&dyn->mtx));
	dyn->mark++;

	while (addr) {
		switch (addr->ai_family) {
		case AF_INET:
		case AF_INET6:
			dynamic_add_addr(ctx, dyn, acl, addr);
			break;
		default:
			DBG(ctx, dyn, "ignored family=%d", addr->ai_family);
			break;
		}
		addr = addr->ai_next;
	}

	/* update unidirector */
	udir_wrlock(vd);
	VTAILQ_FOREACH_SAFE(b, &dyn->backends, list, b2)
		if (b->mark != dyn->mark)
			_udir_remove_backend(ctx, vd, b->be);
		else if (!b->updated)
			b->updated = _udir_add_backend(ctx, vd, b->be, 1);
	udir_unlock(vd);

	VTAILQ_FOREACH_SAFE(b, &dyn->backends, list, b2)
		if (b->mark != dyn->mark) {
			VTAILQ_REMOVE(&dyn->backends, b, list);
			backend_fini(ctx, b);
		}
	AZ(pthread_mutex_unlock(&dyn->mtx));
}

static void
dynamic_timestamp(struct dynamic_lookup *dns, const char *event, double start,
		  double dfirst, double dprev)
{
	VSL(SLT_Timestamp, 0, "vmod-unidirectors %s.%s(%s) %s: %.6f %.6f %.6f",
	    dns->dyn->vcl_conf, dns->dyn->vd->vcl_name, dns->addr, event, start,
	    dfirst, dprev);
}

static void*
lookup_thread(void *priv)
{
	struct vmod_unidirectors_dyndirector *dyn;
	struct dynamic_lookup *dns;
	struct addrinfo hints, *res;
	struct vrt_ctx ctx;
	double deadline, lookup, results, update;
	int error;

	CAST_OBJ_NOTNULL(dns, priv, DYNAMIC_LOOKUP_MAGIC);

	dyn = dns->dyn;
	INIT_OBJ(&ctx, VRT_CTX_MAGIC);
	ctx.vcl = dns->vcl;

	memset(&hints, 0, sizeof hints);
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_family = AF_UNSPEC;
	hints.ai_flags = AI_NUMERICSERV;

	while (dns->active) {

		lookup = VTIM_real();
		dynamic_timestamp(dns, "Lookup", lookup, 0., 0.);

		/* can take a while, keep a look at dns->active */
		error = getaddrinfo(dns->addr, dyn->port, &hints, &res);

		results = VTIM_real();
		dynamic_timestamp(dns, "Results", results, results - lookup,
		    results - lookup);

		if (error)
			LOG(&ctx, SLT_Error, dyn, "getaddrinfo %d (%s)",
			    error, gai_strerror(error));
		else {
			if (dns->active) {
				dynamic_update(&ctx, dns->dyn, dns->whitelist, res);
				update = VTIM_real();
				dynamic_timestamp(dns, "Update", update,
						  update - lookup, update - results);
			}
			freeaddrinfo(res);
		}

		if (dns->active && dns->ttl) {
			Lck_Lock(&dns->mtx);
			deadline = VTIM_real() + dns->ttl;
			error = Lck_CondWait(&dns->cond, &dns->mtx, deadline);
			assert(error == 0 || error == ETIMEDOUT);
			Lck_Unlock(&dns->mtx);
		}
	}
	dynamic_timestamp(dns, "Done", VTIM_real(), 0., 0.);

	return (NULL);
}

static void
lookup_free(VRT_CTX, struct dynamic_lookup *dns)
{
	CHECK_OBJ_ORNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(dns, DYNAMIC_LOOKUP_MAGIC);

	AZ(dns->thread);
	AZ(pthread_cond_destroy(&dns->cond));
	Lck_Delete(&dns->mtx);
	free(dns->addr);
	FREE_OBJ(dns);
}

static void
lookup_stop(VRT_CTX, struct dynamic_lookup *dns)
{
	ASSERT_CLI();
	CHECK_OBJ_ORNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(dns, DYNAMIC_LOOKUP_MAGIC);

	AN(dns->thread);
	AZ(pthread_cond_signal(&dns->cond));
	AZ(pthread_join(dns->thread, NULL));
	dns->thread = 0;

	VRT_rel_vcl(ctx, &dns->vclref);
}

static void
lookup_start(VRT_CTX, struct dynamic_lookup *dns)
{
	ASSERT_CLI();
	CHECK_OBJ_ORNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(dns, DYNAMIC_LOOKUP_MAGIC);

	AZ(dns->vclref);
	dns->vclref = VRT_ref_vcl(ctx, "DNS lookup");

	AZ(dns->thread);
	AZ(pthread_create(&dns->thread, NULL, &lookup_thread, dns));
}

/*--------------------------------------------------------------------
 * VMOD interfaces
 */

struct dyn_vsc {
       unsigned            magic;
#define DYN_VSC_MAGIC 0xac5ef676
       struct vsc_seg      *seg;
};

int v_matchproto_(vmod_event_f)
vmod_event(VRT_CTX, struct vmod_priv *priv, enum vcl_event_e e)
{
	struct dynamic_lookup *dns, *dns2;
	struct dynamic_backend_vsc *c, *c2;
	struct dyn_vsc *vcl_vsc;
	unsigned active;

	ASSERT_CLI();
	AN(ctx);
	AN(ctx->vcl);
	AN(priv);

	if (priv->priv == NULL) {
	         ALLOC_OBJ(vcl_vsc, DYN_VSC_MAGIC);
		 AN(vcl_vsc);
		 priv->priv = vcl_vsc;
	} else
                CAST_OBJ(vcl_vsc, priv->priv, DYN_VSC_MAGIC);

	switch (e) {
#if HAVE_VCL_EVENT_USE
	case VCL_EVENT_USE:
		return (0);
#endif
	case VCL_EVENT_LOAD:
		if (loadcnt == 0) {
			lck_lookup = Lck_CreateClass(&vcl_vsc->seg, "unidirector.lookup");
			AN(lck_lookup);
		}
		loadcnt++;
		return (0);
	case VCL_EVENT_DISCARD:
		assert(loadcnt > 0);
		loadcnt--;
		VTAILQ_FOREACH_SAFE(dns, &unidirectors_objects, list, dns2)
			if (dns->vcl == ctx->vcl) {
				assert(dns->active == 0);
				VTAILQ_REMOVE(&unidirectors_objects, dns, list);
				lookup_free(ctx, dns);
			}
		VTAILQ_FOREACH_SAFE(c, &unidirectors_vsc_clusters, list, c2)
			if (c->vcl == ctx->vcl) {
			        VTAILQ_REMOVE(&unidirectors_vsc_clusters, c, list);
				VRT_VSM_Cluster_Destroy(ctx, &c->vsc_cluster);
				FREE_OBJ(c);
			}
		if (loadcnt == 0) {
			Lck_DestroyClass(&vcl_vsc->seg);
		}
		return (0);
	case VCL_EVENT_WARM:
		active = 1;
		break;
	case VCL_EVENT_COLD:
		active = 0;
		break;
	default:
		WRONG("Unhandled vmod event");
	}
	VTAILQ_FOREACH(dns, &unidirectors_objects, list)
		if (dns->vcl == ctx->vcl) {
			assert(dns->active != active);
			dns->active = active;
			if (active)
				lookup_start(ctx, dns);
			else
				lookup_stop(ctx, dns);
		}
	return (0);
}

VCL_VOID
vmod_dyndirector_update_IPs(VRT_CTX, struct vmod_unidirectors_dyndirector *dyn,
			     VCL_STRING ips)
{
	int error = 0;
	char *addr = NULL;
	const char *p, *sep;
	struct addrinfo hints, *h, *res;

        CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(dyn, VMOD_UNIDIRECTORS_DYNDIRECTOR_MAGIC);

	memset(&hints, 0, sizeof hints);
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_family = AF_UNSPEC;
	hints.ai_flags = AI_NUMERICHOST | AI_NUMERICSERV;

	h = &hints;
	p = ips;
	while (*p != '\0') {
		while (isspace(*p)) p++;
  		sep = strchr(p, ',');
		if (sep == NULL)
			sep = strchr(p, '\0');
		addr = strndup(p, pdiff(p, sep));
		error = getaddrinfo(addr, dyn->port, &hints, &res);
		if (error)
			VSL(SLT_Error, 0,
			    "update_dynamic addr %s fail (%s)", addr, gai_strerror(error));
		else {
			while (h->ai_next != NULL)
				h = h->ai_next;
			h->ai_next = res;
		}
		free(addr);
		addr = NULL;
		if (*sep == '\0')
			break;
		p = sep + 1;
        }
	dynamic_update(ctx, dyn, NULL, hints.ai_next);
	freeaddrinfo(hints.ai_next);
}

VCL_VOID v_matchproto_()
vmod_dyndirector_add_IP(VRT_CTX, struct vmod_unidirectors_dyndirector *dyn,
			VCL_STRING ip, double w)
{
	int error = 0;
	struct addrinfo hints, *addr;
	struct vmod_unidirectors_director *vd;

        CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(dyn, VMOD_UNIDIRECTORS_DYNDIRECTOR_MAGIC);
	vd = dyn->vd;
	CHECK_OBJ_NOTNULL(vd, VMOD_UNIDIRECTORS_DIRECTOR_MAGIC);

	memset(&hints, 0, sizeof hints);
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_family = AF_UNSPEC;
	hints.ai_flags = AI_NUMERICHOST | AI_NUMERICSERV;

	error = getaddrinfo(ip, dyn->port, &hints, &addr);
	if (!error) {
		struct backend_ip *b;
		switch (addr->ai_family) {
		case AF_INET:
		case AF_INET6:
			AZ(pthread_mutex_lock(&dyn->mtx));
			b = dynamic_add_addr(ctx, dyn, NULL, addr);
			if (b) {
				udir_wrlock(vd);
				b->updated = _udir_add_backend(ctx, vd, b->be, w);
				udir_unlock(vd);
			}
			AZ(pthread_mutex_unlock(&dyn->mtx));
			break;
		default:
			DBG(ctx, dyn, "ignored family=%d", addr->ai_family);
			break;
		}

	} else
		VSL(SLT_Error, 0,
		    "add_IP addr %s fail (%s)", ip, gai_strerror(error));
	freeaddrinfo(addr);
}

VCL_VOID v_matchproto_()
vmod_dyndirector_remove_IP(VRT_CTX, struct vmod_unidirectors_dyndirector *dyn,
			VCL_STRING ip)
{
	int error = 0;
	struct addrinfo hints, *addr;
	struct vmod_unidirectors_director *vd;

        CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(dyn, VMOD_UNIDIRECTORS_DYNDIRECTOR_MAGIC);
	vd = dyn->vd;
	CHECK_OBJ_NOTNULL(vd, VMOD_UNIDIRECTORS_DIRECTOR_MAGIC);

	memset(&hints, 0, sizeof hints);
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_family = AF_UNSPEC;
	hints.ai_flags = AI_NUMERICHOST | AI_NUMERICSERV;

	error = getaddrinfo(ip, dyn->port, &hints, &addr);
	if (!error) {
		struct backend_ip *b, *b2;
		struct suckaddr *sa;

		switch (addr->ai_family) {
		case AF_INET:
		case AF_INET6:
			sa = malloc(vsa_suckaddr_len);
			AN(sa);
			AN(VSA_Build(sa, addr->ai_addr, addr->ai_addrlen));
			AZ(pthread_mutex_lock(&dyn->mtx));
			VTAILQ_FOREACH_SAFE(b, &dyn->backends, list, b2)
				if (!VSA_Compare(b->ip_suckaddr, sa)) {
					VTAILQ_REMOVE(&dyn->backends, b, list);
					udir_wrlock(vd);
					_udir_remove_backend(ctx, vd, b->be);
					udir_unlock(vd);
					DBG(ctx, dyn, "remove-backend %s", b->vcl_name);
					backend_fini(ctx, b);
					break;
				}
			free(sa);
			AZ(pthread_mutex_unlock(&dyn->mtx));
			break;
		default:
			DBG(ctx, dyn, "ignored family=%d", addr->ai_family);
			break;
		}

	} else
		VSL(SLT_Error, 0,
		    "remove_IP addr %s fail (%s)", ip, gai_strerror(error));
	freeaddrinfo(addr);
}

VCL_VOID vmod_dyndirector_lookup_addr(VRT_CTX,  struct vmod_unidirectors_dyndirector *dyn,
				      VCL_STRING addr,
				      VCL_ACL whitelist,
				      VCL_DURATION ttl)
{
	struct dynamic_lookup *dns;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(dyn, VMOD_UNIDIRECTORS_DYNDIRECTOR_MAGIC);
	CHECK_OBJ_ORNULL(whitelist, VRT_ACL_MAGIC);

	if (ctx->method != VCL_MET_INIT) {
		VSB_printf(ctx->msg, ".lookup_addr only in vcl_init (%s).", dyn->vd->vcl_name);
		VRT_handling(ctx, VCL_RET_FAIL);
		return;
	}

	ALLOC_OBJ(dns, DYNAMIC_LOOKUP_MAGIC);
	dns->addr =  strdup(addr);
	dns->whitelist = whitelist;
	dns->ttl = ttl;
	dns->dyn = dyn;
	dns->vcl = ctx->vcl;
	Lck_New(&dns->mtx, lck_lookup);
	AZ(pthread_cond_init(&dns->cond, NULL));

	VTAILQ_INSERT_TAIL(&unidirectors_objects, dns, list);
}

VCL_VOID v_matchproto_()
vmod_dynamics_number_expected(VRT_CTX, VCL_INT n)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	struct dynamic_backend_vsc *c;

	if (ctx->method != VCL_MET_INIT) {
		VSB_printf(ctx->msg, ".dynamics_number_expected only in vcl_init.");
		VRT_handling(ctx, VCL_RET_FAIL);
		return;
	}
	if (n < 2)
		return;
	VTAILQ_FOREACH(c, &unidirectors_vsc_clusters, list)
	        if (c->vcl == ctx->vcl) {
			return;
		}
	ALLOC_OBJ(c, DYNAMIC_BACKEND_VSC_MAGIC);
	AN(c);
	c->vcl = ctx->vcl;
	c->vsc_cluster = VRT_VSM_Cluster_New(ctx, n * VRT_backend_vsm_need(ctx));
	VTAILQ_INSERT_HEAD(&unidirectors_vsc_clusters, c, list);
	return;
}

VCL_VOID v_matchproto_()
vmod_dyndirector__init(VRT_CTX, struct vmod_unidirectors_dyndirector **dynp, const char *vcl_name,
		       VCL_STRING service,
		       VCL_PROBE probe,
		       VCL_DURATION connect_timeout,
		       VCL_DURATION first_byte_timeout,
		       VCL_DURATION between_bytes_timeout,
		       VCL_INT max_connections)
{
	struct vmod_unidirectors_dyndirector *dyn;
	int port_i = 0;
	char port[NI_MAXSERV];

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_ORNULL(probe, VRT_BACKEND_PROBE_MAGIC);

	AN(dynp);
	AZ(*dynp);
	ALLOC_OBJ(dyn, VMOD_UNIDIRECTORS_DYNDIRECTOR_MAGIC);
	AN(dyn);
	*dynp = dyn;
	vmod_director__init(ctx, &dyn->vd, vcl_name);
	AN(dyn->vd);
	dyn->vcl_conf = VCL_Name(ctx->vcl);

	if (service == NULL || *service == '\0') {
		VSB_printf(ctx->msg, "Missing dynamic port for %s", dyn->vd->vcl_name);
		VRT_handling(ctx, VCL_RET_FAIL);
	} else {
		char *endptr;
		port_i = strtol(service, &endptr, 10);
		if (endptr == service) {
			struct servent *sp = getservbyname(service, "tcp");
			if (sp != NULL)
				port_i = ntohs(sp->s_port);
		}
		if (port_i < 1 || port_i > 65535) {
			VSB_printf(ctx->msg, "Invalid dynamic port for %s", dyn->vd->vcl_name);
			VRT_handling(ctx, VCL_RET_FAIL);
		}
	}
	snprintf(port, NI_MAXSERV, "%d", port_i);

	AZ(pthread_mutex_init(&dyn->mtx, NULL));
	VTAILQ_INIT(&dyn->backends);
	dyn->port = strdup(port);
	dyn->probe = probe;
	dyn->connect_timeout = connect_timeout;
	dyn->first_byte_timeout = first_byte_timeout;
	dyn->between_bytes_timeout = between_bytes_timeout;
	dyn->max_connections = max_connections;
}

VCL_VOID v_matchproto_()
vmod_dyndirector__fini(struct vmod_unidirectors_dyndirector **dynp)
{
	struct backend_ip *b, *b2;
	struct vmod_unidirectors_dyndirector *dyn;

	TAKE_OBJ_NOTNULL(dyn, dynp, VMOD_UNIDIRECTORS_DYNDIRECTOR_MAGIC);

	VTAILQ_FOREACH_SAFE(b, &dyn->backends, list, b2) {
		VTAILQ_REMOVE(&dyn->backends, b, list);
		backend_fini(NULL, b);
	}
	assert(VTAILQ_EMPTY(&dyn->backends));

	free(dyn->port);
	AZ(pthread_mutex_destroy(&dyn->mtx));

	vmod_director__fini(&dyn->vd);
	FREE_OBJ(dyn);
}


VCL_VOID v_matchproto_(td_dynamic_director_debug)
vmod_dyndirector_debug(VRT_CTX, struct vmod_unidirectors_dyndirector *dyn,
    VCL_BOOL enable)
{
	(void)ctx;
	dyn->debug = enable;
}

VCL_BACKEND v_matchproto_()
vmod_dyndirector_backend(VRT_CTX, struct vmod_unidirectors_dyndirector *dyn)
{
        CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(dyn, VMOD_UNIDIRECTORS_DYNDIRECTOR_MAGIC);
	return (vmod_director_backend(ctx, dyn->vd));
}

VCL_VOID v_matchproto_()
vmod_dyndirector_add_backend(VRT_CTX, struct vmod_unidirectors_dyndirector *dyn,
			     VCL_BACKEND be, double w)
{
        CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(dyn, VMOD_UNIDIRECTORS_DYNDIRECTOR_MAGIC);
	return (vmod_director_add_backend(ctx, dyn->vd, be, w));
}

VCL_VOID v_matchproto_()
vmod_dyndirector_remove_backend(VRT_CTX, struct vmod_unidirectors_dyndirector *dyn,
				  VCL_BACKEND be)
{
        CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(dyn, VMOD_UNIDIRECTORS_DYNDIRECTOR_MAGIC);
	return (vmod_director_remove_backend(ctx, dyn->vd, be));
}

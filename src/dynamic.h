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
 *
 * Data structures
 *
 * Locking order is always vmod_dynamic_director.mtx and then dynamic_domain.mtx
 * when both are needed.
 */

#ifndef UNIDIRECTORS_DYNAMIC_H
#define UNIDIRECTORS_DYNAMIC_H

struct vmod_unidirectors_director;

struct backend_ip {
	VCL_BACKEND                     be;
	struct suckaddr 		*ip_suckaddr;
	char				*ip_addr;
	char				*vcl_name;
	unsigned			mark;
	unsigned			updated;
	VTAILQ_ENTRY(backend_ip)	list;
};

struct vmod_unidirectors_dyndirector {
	unsigned		magic;
#define VMOD_UNIDIRECTORS_DYNDIRECTOR_MAGIC	0x0ce092f1
	struct vmod_unidirectors_director *vd;

	VCL_PROBE		probe;
	char			*port;
	VCL_INT			max_connections;
	VCL_DURATION		connect_timeout;
	VCL_DURATION		first_byte_timeout;
	VCL_DURATION		between_bytes_timeout;

	pthread_mutex_t		mtx;

	VTAILQ_HEAD( ,backend_ip)	backends;

	const char		*vcl_conf;
	unsigned		mark;
	volatile unsigned	debug;
};

struct dynamic_lookup {
	unsigned		magic;
#define DYNAMIC_LOOKUP_MAGIC        0x7fd0aa1e
	struct vmod_unidirectors_dyndirector *dyn;
	char			*addr;
	VCL_ACL			whitelist;
	VCL_DURATION		ttl;
	struct lock		mtx;
	pthread_t		thread;
	pthread_cond_t		cond;

	VTAILQ_ENTRY(dynamic_lookup)	list;
	struct vcl		*vcl;
	struct vclref		*vclref;
	volatile unsigned	active;
};

struct dynamic_backend_vsc {
	unsigned		magic;
#define DYNAMIC_BACKEND_VSC_MAGIC        0x091fa66d
        struct vcl		*vcl;
        VTAILQ_ENTRY(dynamic_backend_vsc) list;
        struct vsmw_cluster     *vsc_cluster;
};

VTAILQ_HEAD(dynamic_lookup_head, dynamic_lookup) unidirectors_objects;
VTAILQ_HEAD(dynamic_backend_vsc_head, dynamic_backend_vsc) unidirectors_vsc_clusters;

/* extern to avoid any link confusion */
extern struct dynamic_lookup_head unidirectors_objects;
extern struct dynamic_backend_vsc_head unidirectors_vsc_clusters;

#endif /* UNIDIRECTORS_DYNAMIC_H */

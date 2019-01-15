/*-
 * Copyright (c) 2013-2015 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@FreeBSD.org>
 *
 * Copyright (c) 2016-2018 GANDI SAS
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

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <netinet/in.h>

#include "cache/cache.h"

#include "udir.h"
#include "dynamic.h"

struct vmod_director_hash {
	unsigned		    magic;
#define VMOD_DIRECTOR_HASH_MAGIC    0x1e98af01
        char 			    *hdr;
};

/* MurmurHash3_32 */
static inline uint32_t getblock(const uint32_t * p, int i) {
	return ntohl(p[i]);
}
static inline uint32_t rotl32(uint32_t x, int8_t r) {
	return (x << r) | (x >> (32 - r));
}
static inline uint32_t fmix(uint32_t h)
{
	h ^= h >> 16;
	h *= 0x85ebca6b;
	h ^= h >> 13;
	h *= 0xc2b2ae35;
	h ^= h >> 16;
	return h;
}

static uint32_t
MurmurHash3_32(const void *key, int len, uint32_t seed)
{
	const uint8_t *data = (const uint8_t *)key;
	const int nblocks = len / 4;

	uint32_t h1 = seed;
	uint32_t c1 = 0xcc9e2d51;
	uint32_t c2 = 0x1b873593;

	const uint32_t *blocks = (const uint32_t *)(data + nblocks*4);
	for (int i = -nblocks; i; i++) {
		uint32_t k1 = getblock(blocks, i);
		k1 *= c1;
		k1 = rotl32(k1, 15);
		k1 *= c2;
		h1 ^= k1;
		h1 = rotl32(h1, 13);
		h1 = h1 * 5 + 0xe6546b64;
	}

	const uint8_t *tail = (const uint8_t*)(data + nblocks*4);
	uint32_t k1 = 0;
	switch(len & 3) {
	case 3: k1 ^= tail[2] << 16;
	case 2: k1 ^= tail[1] << 8;
	case 1: k1 ^= tail[0];
		k1 *= c1;
		k1 = rotl32(k1, 15);
		k1 *= c2;
		h1 ^= k1;
	};

	h1 ^= len;
	h1 = fmix(h1);

	return h1;
}
/* MurmurHash3_32 */

static void v_matchproto_(vdi_destroy_f)
hash_vdi_destroy(VCL_BACKEND dir)
{
	struct vmod_unidirectors_director *vd;
	struct vmod_director_hash *rr;
	CHECK_OBJ_NOTNULL(dir, DIRECTOR_MAGIC);
	CAST_OBJ_NOTNULL(vd, dir->priv, VMOD_UNIDIRECTORS_DIRECTOR_MAGIC);
	CAST_OBJ_NOTNULL(rr, vd->priv, VMOD_DIRECTOR_HASH_MAGIC);
	free(rr->hdr);
	FREE_OBJ(rr);
}

static VCL_BACKEND v_matchproto_(vdi_resolve_f)
hash_vdi_resolve(VRT_CTX, VCL_BACKEND dir)
{
	struct worker *wrk;
        struct vmod_unidirectors_director *vd;
	struct vmod_director_hash *rr;
	const char *p;
	VCL_BACKEND be = NULL;
	be_idx_t *be_idx;
	unsigned u, h, n_backend = 0;
	double r, a, tw = 0.0;

	CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);
	wrk = ctx->bo->wrk;
	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(dir, DIRECTOR_MAGIC);
	CAST_OBJ_NOTNULL(vd, dir->priv, VMOD_UNIDIRECTORS_DIRECTOR_MAGIC);
	AN(ctx->bo->bereq);

	udir_rdlock(vd);
	CAST_OBJ_NOTNULL(rr, vd->priv, VMOD_DIRECTOR_HASH_MAGIC);
	if (!rr->hdr || !http_GetHdr(ctx->bo->bereq, rr->hdr, &p)) {
		AN(ctx->http_bereq);
		p = ctx->http_bereq->hd[HTTP_HDR_URL].b;
	}
	r = MurmurHash3_32(p, strlen(p), 0);
	r = scalbn(r, -32);
	if (WS_Reserve(wrk->aws, 0) >= vd->n_backend * sizeof(*be_idx)) {
		be_idx = (void*)wrk->aws->f;
		for (u = 0; u < vd->n_backend; u++) {
			be = vd->backend[u];
			CHECK_OBJ_NOTNULL(be, DIRECTOR_MAGIC);
			if (VRT_Healthy(ctx, be, NULL)) {
				be_idx[n_backend++] = u;
				tw += vd->weight[u];
			}
		}
		be = NULL;
		if (tw > 0.0) {
			r *= tw;
			a = 0.0;
			for (h = 0; h < n_backend; h++) {
				u = be_idx[h];
				assert(u < vd->n_backend);
				a += vd->weight[u];
				if (r < a) {
					be = vd->backend[u];
					break;
				}
			}
		}
	}
	WS_Release(wrk->aws, 0);
	udir_unlock(vd);
	return (be);
}

static const struct vdi_methods hash_methods[1] = {{
	.magic =		VDI_METHODS_MAGIC,
	.type =			"hash",
	.healthy =		udir_vdi_healthy,
	.resolve =		hash_vdi_resolve,
	.find =			udir_vdi_find,
	.uptime =		udir_vdi_uptime,
	.destroy =		hash_vdi_destroy,
}};

VCL_VOID v_matchproto_()
vmod_director_hash(VRT_CTX, struct vmod_unidirectors_director *vd, VCL_STRING hdr)
{
        unsigned l;
        struct vmod_director_hash *rr;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(vd, VMOD_UNIDIRECTORS_DIRECTOR_MAGIC);

	if (vd->dir) {
		VRT_fail(ctx, "%s: LB method is already set", vd->vcl_name);
		return;
	}
	udir_wrlock(vd);

	ALLOC_OBJ(rr, VMOD_DIRECTOR_HASH_MAGIC);
	vd->priv = rr;
	AN(vd->priv);

	AN(hdr);
	l = strlen(hdr);
	if (l > 1) {
	        unsigned u;
		char *p;
		rr->hdr = calloc(l+2, 1);
		AN(rr->hdr);
		p = rr->hdr;
		*p++ = l+1;
		for (u=0; u<l; u++)
		        *p++ = hdr[u];
		*p++ = ':';
	}

	vd->dir = VRT_AddDirector(ctx, hash_methods, vd, "%s", vd->vcl_name);

	udir_unlock(vd);
}

VCL_VOID v_matchproto_()
vmod_dyndirector_hash(VRT_CTX, struct vmod_unidirectors_dyndirector *dyn, VCL_STRING hdr)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(dyn, VMOD_UNIDIRECTORS_DYNDIRECTOR_MAGIC);
	vmod_director_hash(ctx, dyn->vd, hdr);
}

/*-
 * Copyright (c) 2013-2015 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@FreeBSD.org>
 *
 * Copyright (c) 2016 GANDI SAS
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

#include "cache/cache.h"
#include "cache/cache_director.h"

#include "vrt.h"
#include "vsha256.h"

#include "udir.h"

#include "vcc_if.h"


static __inline uint32_t
vbe32dec(const void *pp)
{
  uint8_t const *p = (uint8_t const *)pp;

  return (((unsigned)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]);
}


struct vmod_director_hash {
	unsigned		    magic;
#define VMOD_DIRECTOR_HASH_MAGIC    0x1e98af01
        char 			    *hdr;
};

static void __match_proto__(udir_fini_f)
vmod_hash_fini(void **ppriv)
{
        struct vmod_director_hash *rr;
	AN(ppriv);
	rr = *ppriv;
	*ppriv = NULL;
	CHECK_OBJ_NOTNULL(rr, VMOD_DIRECTOR_HASH_MAGIC);
	FREE_OBJ(rr);
}

static VCL_BACKEND __match_proto__(vdi_resolve_f)
vmod_hash_resolve(const struct director *dir, struct worker *wrk,
		  struct busyobj *bo)
{
        struct vmod_unidirectors_director *vd;
	struct vmod_director_hash *rr;
	struct SHA256Context sha_ctx;
	const char *p;
	unsigned char sha256[SHA256_LEN];
	VCL_BACKEND be;
	double r;

	CHECK_OBJ_NOTNULL(dir, DIRECTOR_MAGIC);
	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_ORNULL(bo, BUSYOBJ_MAGIC);
	CAST_OBJ_NOTNULL(vd, dir->priv, VMOD_UNIDIRECTORS_DIRECTOR_MAGIC);
	CAST_OBJ_NOTNULL(rr, vd->priv, VMOD_DIRECTOR_HASH_MAGIC);

	AN(bo->bereq);
	if (!http_GetHdr(bo->bereq, rr->hdr, &p))
	      p = NULL;

	SHA256_Init(&sha_ctx);
	if (p != NULL && *p != '\0')
	        SHA256_Update(&sha_ctx, p, strlen(p));
	SHA256_Final(sha256, &sha_ctx);

	r = vbe32dec(sha256);
	r = scalbn(r, -32);
	assert(r >= 0 && r <= 1.0);
	be = udir_pick_be(vd, r, bo);
	return (be);
}

VCL_VOID __match_proto__()
vmod_director_hash(VRT_CTX, struct vmod_unidirectors_director *vd, VCL_STRING hdr)
{
        unsigned l;
        struct vmod_director_hash *rr;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(vd, VMOD_UNIDIRECTORS_DIRECTOR_MAGIC);
	AZ(vd->priv);
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

	vd->fini = vmod_hash_fini;
	vd->add_backend = udir_add_backend;
	vd->dir->name = "hash";
	vd->dir->resolve = vmod_hash_resolve;
	vd->dir->healthy = udir_vdi_healthy;
	vd->dir->search = udir_vdi_search;
}

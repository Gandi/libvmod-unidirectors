/*
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

#include <stdlib.h>

#include "cache/cache.h"
#include "cache/cache_director.h"

#include "vrt.h"
#include "vbm.h"
#include "vcc_if.h"

#include "udir.h"

struct vmod_director_leastconn {
	unsigned				magic;
#define VMOD_DIRECTOR_LEASTCONN_MAGIC           0xadda6fc5
	unsigned				maxconn;
};

static void __match_proto__(udir_fini_f)
vmod_lc_fini(void **ppriv)
{
        struct vmod_director_leastconn *rr;
	AN(ppriv);
	rr = *ppriv;
	*ppriv = NULL;
	CHECK_OBJ_NOTNULL(rr, VMOD_DIRECTOR_LEASTCONN_MAGIC);
	FREE_OBJ(rr);
}

static const struct director * __match_proto__(vdi_resolve_f)
vmod_lc_resolve(const struct director *dir, struct worker *wrk,
		struct busyobj *bo)
{
	struct vmod_unidirectors_director *vd;
        struct vmod_director_leastconn *rr;
	unsigned u;
	double r;
	VCL_BACKEND be = NULL;

	CHECK_OBJ_NOTNULL(dir, DIRECTOR_MAGIC);
	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	CAST_OBJ_NOTNULL(vd, dir->priv, VMOD_UNIDIRECTORS_DIRECTOR_MAGIC);
	CAST_OBJ_NOTNULL(rr, vd->priv, VMOD_DIRECTOR_LEASTCONN_MAGIC);
	udir_rdlock(vd);
	for (u = 0; u < vd->n_backend; u++) {
		be = vd->backend[u];
		CHECK_OBJ_NOTNULL(be, DIRECTOR_MAGIC);
		vd->weight[u] = be->freeconn(be, rr->maxconn);
	}
	udir_unlock(vd);
	r = scalbn(random(), -31);
	assert(r >= 0 && r < 1.0);
	be = udir_pick_be(vd, r, bo);
	return (be);
}

static void __match_proto__(udir_add_backend_f)
vmod_lc_add_backend(struct vmod_unidirectors_director *vd, VCL_BACKEND be, double w)
{
	(void)udir_add_backend(vd, be, 0.0);
}

VCL_VOID __match_proto__()
vmod_director_leastconn(VRT_CTX, struct vmod_unidirectors_director *vd, VCL_INT maxconn)
{
        struct vmod_director_leastconn *rr;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(vd, VMOD_UNIDIRECTORS_DIRECTOR_MAGIC);
	AZ(vd->priv);
	ALLOC_OBJ(rr, VMOD_DIRECTOR_LEASTCONN_MAGIC);
	vd->priv = rr;
	AN(vd->priv);
	rr->maxconn = maxconn;

	vd->fini = vmod_lc_fini;
	vd->add_backend = vmod_lc_add_backend;
	vd->dir->name = "least-connections";
	vd->dir->resolve = vmod_lc_resolve;
}

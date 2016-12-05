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
#include <math.h>

#include "cache/cache.h"
#include "cache/cache_director.h"

#include "vtim.h"
#include "vrt.h"
#include "vcc_if.h"

#include "udir.h"


struct vmod_director_leastconn {
	unsigned				magic;
#define VMOD_DIRECTOR_LEASTCONN_MAGIC           0xadda6fc5
	unsigned				slow_start;
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
lc_vdi_resolve(const struct director *dir, struct worker *wrk,
		struct busyobj *bo)
{
	struct vmod_unidirectors_director *vd;
	struct vmod_director_leastconn *rr;
	unsigned u;
	double changed, now, delta_t, load, least = INFINITY;
	VCL_BACKEND be, rbe = NULL;

	CHECK_OBJ_NOTNULL(dir, DIRECTOR_MAGIC);
	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	CAST_OBJ_NOTNULL(vd, dir->priv, VMOD_UNIDIRECTORS_DIRECTOR_MAGIC);

	udir_rdlock(vd);
	if (vd->fini != vmod_lc_fini) {
		udir_unlock(vd);
		return (NULL);
	}
	CAST_OBJ_NOTNULL(rr, vd->priv, VMOD_DIRECTOR_LEASTCONN_MAGIC);
	now = VTIM_real();
	for (u = 0; u < vd->n_backend; u++) {
		be = vd->backend[u];
		CHECK_OBJ_NOTNULL(be, DIRECTOR_MAGIC);
		AN(be->uptime);
		if (be->uptime(be, bo, &changed, &load)) {
			delta_t = now - changed;
			if (delta_t < 0)
				delta_t = 0.0;
			load = load / vd->weight[u];
			if (delta_t < rr->slow_start)
				load = load / delta_t * rr->slow_start;
			if (load <= least) {
				rbe = be;
				least = load;
			}
		}
	}
	udir_unlock(vd);
	return (rbe);
}

VCL_VOID __match_proto__()
vmod_director_leastconn(VRT_CTX, struct vmod_unidirectors_director *vd, VCL_INT slow_start)
{
	struct vmod_director_leastconn *rr;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(vd, VMOD_UNIDIRECTORS_DIRECTOR_MAGIC);

	udir_wrlock(vd);
	udir_delete_priv(vd);

	ALLOC_OBJ(rr, VMOD_DIRECTOR_LEASTCONN_MAGIC);
	vd->priv = rr;
	AN(vd->priv);
	rr->slow_start = slow_start;

	vd->fini = vmod_lc_fini;
	vd->dir->name = "least-connections";
	vd->dir->uptime = udir_vdi_uptime;
	vd->dir->resolve = lc_vdi_resolve;

	udir_unlock(vd);
}

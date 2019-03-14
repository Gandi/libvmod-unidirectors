/*
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

#include <stdlib.h>
#include <math.h>
#include <string.h>

#include "cache/cache.h"
#include "cache/cache_director.h"

#include "vtim.h"

#include "udir.h"
#include "dynamic.h"

struct vmod_director_leastconn {
	unsigned				magic;
#define VMOD_DIRECTOR_LEASTCONN_MAGIC           0xadda6fc5
	unsigned				slow_start;
};

static void v_matchproto_(vdi_destroy_f)
lc_vdi_destroy(VCL_BACKEND dir)
{
	struct vmod_unidirectors_director *vd;
	struct vmod_director_leastconn *lc;
	CHECK_OBJ_NOTNULL(dir, DIRECTOR_MAGIC);
	CAST_OBJ_NOTNULL(vd, dir->priv, VMOD_UNIDIRECTORS_DIRECTOR_MAGIC);
	CAST_OBJ_NOTNULL(lc, vd->priv, VMOD_DIRECTOR_LEASTCONN_MAGIC);
	FREE_OBJ(lc);
}

static VCL_BACKEND v_matchproto_(vdi_resolve_f)
lc_vdi_resolve(VRT_CTX, VCL_BACKEND dir)
{
	struct vmod_unidirectors_director *vd;
	struct vmod_director_leastconn *lc;
	unsigned u;
	double changed, now, delta_t, load, least = INFINITY;
	VCL_BACKEND be, rbe = NULL;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(dir, DIRECTOR_MAGIC);
	CAST_OBJ_NOTNULL(vd, dir->priv, VMOD_UNIDIRECTORS_DIRECTOR_MAGIC);

	udir_rdlock(vd);
	CAST_OBJ_NOTNULL(lc, vd->priv, VMOD_DIRECTOR_LEASTCONN_MAGIC);
	now = VTIM_real();
	for (u = 0; u < vd->n_backend; u++) {
		be = vd->backend[u];
		CHECK_OBJ_NOTNULL(be, DIRECTOR_MAGIC);
		AN(be->vdir->methods->uptime);
		if (be->vdir->methods->uptime(ctx, be, &changed, &load)) {
			delta_t = now - changed;
			if (delta_t < 0)
				delta_t = 0.0;
			load = load / vd->weight[u];
			if (delta_t < lc->slow_start)
				load = load / delta_t * lc->slow_start;
			if (load <= least) {
				rbe = be;
				least = load;
			}
		}
	}
	udir_unlock(vd);
	return (rbe);
}

static const struct vdi_methods lc_methods[1] = {{
	.magic =		VDI_METHODS_MAGIC,
	.type =			"least-connections",
	.healthy =		udir_vdi_healthy,
	.resolve =		lc_vdi_resolve,
	.find =			udir_vdi_find,
	.uptime =		udir_vdi_uptime,
	.destroy =		lc_vdi_destroy,
	.list =                 udir_vdi_list,
}};

VCL_VOID v_matchproto_()
vmod_director_leastconn(VRT_CTX, struct vmod_unidirectors_director *vd, VCL_INT slow_start)
{
	struct vmod_director_leastconn *lc;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(vd, VMOD_UNIDIRECTORS_DIRECTOR_MAGIC);

	if (vd->dir) {
		VRT_fail(ctx, "%s: LB method is already set", vd->vcl_name);
		return;
	}
	udir_wrlock(vd);

	ALLOC_OBJ(lc, VMOD_DIRECTOR_LEASTCONN_MAGIC);
	vd->priv = lc;
	AN(vd->priv);
	lc->slow_start = slow_start;

	vd->dir = VRT_AddDirector(ctx, lc_methods, vd, "%s", vd->vcl_name);

	udir_unlock(vd);
}

VCL_VOID v_matchproto_()
vmod_dyndirector_leastconn(VRT_CTX, struct vmod_unidirectors_dyndirector *dyn, VCL_INT slow_start)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(dyn, VMOD_UNIDIRECTORS_DYNDIRECTOR_MAGIC);
	vmod_director_leastconn(ctx, dyn->vd, slow_start);
}

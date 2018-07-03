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

#include "cache/cache.h"

#include "vrnd.h"

#include "udir.h"
#include "dynamic.h"

static VCL_BACKEND v_matchproto_(vdi_resolve_f)
random_vdi_resolve(VRT_CTX, VCL_BACKEND dir)
{
	struct worker *wrk;
	struct vmod_unidirectors_director *vd;
	VCL_BACKEND be = NULL;
	double r;
	be_idx_t *be_idx;

	CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);
	wrk = ctx->bo->wrk;
	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(dir, DIRECTOR_MAGIC);
	CAST_OBJ_NOTNULL(vd, dir->priv, VMOD_UNIDIRECTORS_DIRECTOR_MAGIC);

	udir_rdlock(vd);
	r = scalbn(VRND_RandomTestable(), -31);
	assert(r >= 0 && r < 1.0);
	if (WS_Reserve(wrk->aws, 0) >= vd->n_backend * sizeof(*be_idx)) {
		be_idx = (void*)wrk->aws->f;
		be = udir_pick_be(ctx, vd, r, be_idx);
        }
	WS_Release(wrk->aws, 0);
	udir_unlock(vd);
	return (be);
}

static const struct vdi_methods random_methods[1] = {{
	.magic =		VDI_METHODS_MAGIC,
	.type =			"random",
	.healthy =		udir_vdi_healthy,
	.resolve =		random_vdi_resolve,
	.find =			udir_vdi_find,
	.uptime =		udir_vdi_uptime,
}};

VCL_VOID v_matchproto_()
vmod_director_random(VRT_CTX, struct vmod_unidirectors_director *vd)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(vd, VMOD_UNIDIRECTORS_DIRECTOR_MAGIC);

	if (vd->dir) {
		VRT_fail(ctx, "%s: LB method is already set", vd->vcl_name);
		return;
	}
	udir_wrlock(vd);
	vd->dir = VRT_AddDirector(ctx, random_methods, vd, "%s", vd->vcl_name);
	udir_unlock(vd);
}

VCL_VOID v_matchproto_()
vmod_dyndirector_random(VRT_CTX, struct vmod_unidirectors_dyndirector *dyn)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(dyn, VMOD_UNIDIRECTORS_DYNDIRECTOR_MAGIC);
	vmod_director_random(ctx, dyn->vd);
}

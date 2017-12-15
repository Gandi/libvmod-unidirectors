/*-
 * Copyright (c) 2013-2015 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@FreeBSD.org>
 *
 * Copyright (c) 2016-2017 GANDI SAS
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
#include <string.h>

#include "cache/cache.h"
#include "cache/cache_director.h"

#include "udir.h"
#include "dynamic.h"

struct vmod_director_fallback {
	unsigned			magic;
#define VMOD_DIRECTOR_FALLBACK_MAGIC    0x4df34074
	unsigned			sticky;
	VCL_BACKEND			be;
};

static void v_matchproto_(udir_fini_f)
vmod_fb_fini(void **ppriv)
{
	struct vmod_director_fallback *fb;
	AN(ppriv);
	fb = *ppriv;
	*ppriv = NULL;
	CHECK_OBJ_NOTNULL(fb, VMOD_DIRECTOR_FALLBACK_MAGIC);
	FREE_OBJ(fb);
}

static const struct director * v_matchproto_(vdi_resolve_f)
fallback_vdi_resolve(const struct director *dir, struct worker *wrk,
    struct busyobj *bo)
{
	struct vmod_unidirectors_director *vd;
	struct vmod_director_fallback *fb;
	unsigned u;
	VCL_BACKEND be, rbe = NULL;

	CHECK_OBJ_NOTNULL(dir, DIRECTOR_MAGIC);
	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	CAST_OBJ_NOTNULL(vd, dir->priv, VMOD_UNIDIRECTORS_DIRECTOR_MAGIC);
	udir_rdlock(vd);
	if (vd->fini != vmod_fb_fini)
		goto end;
	CAST_OBJ_NOTNULL(fb, vd->priv, VMOD_DIRECTOR_FALLBACK_MAGIC);
	if (fb->sticky) {
		be = fb->be;
		for (u = 0; u < vd->n_backend; u++)
			if (be == vd->backend[u]) {
				CHECK_OBJ_NOTNULL(be, DIRECTOR_MAGIC);
				if (be->healthy(be, bo, NULL))
					rbe = be;
				break;
			}
	}
	for (u = 0; rbe == NULL && u < vd->n_backend; u++) {
		be = vd->backend[u];
		CHECK_OBJ_NOTNULL(be, DIRECTOR_MAGIC);
		if (be->healthy(be, bo, NULL))
			fb->be = rbe = be;
	}
 end:
	udir_unlock(vd);
	return (rbe);
}

static unsigned v_matchproto_(vdi_uptime_f)
fallback_vdi_uptime(const struct director *dir, const struct busyobj *bo,
		  double *changed, double *load)
{
	unsigned u;
	unsigned retval = 0;
	double c = 0, l = 0;
	struct vmod_unidirectors_director *vd;
	struct vmod_director_fallback *fb;
	VCL_BACKEND be;

	CHECK_OBJ_NOTNULL(dir, DIRECTOR_MAGIC);
	CAST_OBJ_NOTNULL(vd, dir->priv, VMOD_UNIDIRECTORS_DIRECTOR_MAGIC);
	udir_rdlock(vd);
	if (vd->fini != vmod_fb_fini)
		goto end;
	CAST_OBJ_NOTNULL(fb, vd->priv, VMOD_DIRECTOR_FALLBACK_MAGIC);
	if (fb->sticky) {
		be = fb->be;
		for (u = 0; u < vd->n_backend; u++)
			if (be == vd->backend[u]) {
				CHECK_OBJ_NOTNULL(be, DIRECTOR_MAGIC);
				AN(be->uptime);
				retval = be->uptime(be, bo, &c, &l);
				break;
			}
	}
	for (u = 0; !retval && u < vd->n_backend; u++) {
		be = vd->backend[u];
		CHECK_OBJ_NOTNULL(be, DIRECTOR_MAGIC);
		AN(be->uptime);
		retval = be->uptime(be, bo, &c, &l);
	}
 end:
	udir_unlock(vd);
	if (changed != NULL)
		*changed = c;
	if (load != NULL)
		*load = l;
	return (retval);
}

VCL_VOID v_matchproto_()
vmod_director_fallback(VRT_CTX, struct vmod_unidirectors_director *vd, VCL_BOOL sticky)
{
	struct vmod_director_fallback *fb;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(vd, VMOD_UNIDIRECTORS_DIRECTOR_MAGIC);

	udir_wrlock(vd);
	udir_delete_priv(vd);

	ALLOC_OBJ(fb, VMOD_DIRECTOR_FALLBACK_MAGIC);
	vd->priv = fb;
	AN(vd->priv);
	fb->sticky = sticky;
	fb->be = NULL;

	vd->fini = vmod_fb_fini;
	vd->dir->name = "fallback";
	vd->dir->uptime = fallback_vdi_uptime;
	vd->dir->resolve = fallback_vdi_resolve;

	udir_unlock(vd);
}

VCL_VOID v_matchproto_()
vmod_dyndirector_fallback(VRT_CTX, struct vmod_unidirectors_dyndirector *dyn, VCL_BOOL sticky)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(dyn, VMOD_UNIDIRECTORS_DYNDIRECTOR_MAGIC);
	vmod_director_fallback(ctx, dyn->vd, sticky);
}

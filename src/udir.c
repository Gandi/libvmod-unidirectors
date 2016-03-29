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

#include <stdlib.h>

#include "cache/cache.h"
#include "cache/cache_director.h"

#include "vrt.h"
#include "vbm.h"

#include "udir.h"

static void
udir_expand(struct vmod_unidirectors_director *vd, unsigned n)
{
	CHECK_OBJ_NOTNULL(vd, VMOD_UNIDIRECTORS_DIRECTOR_MAGIC);

	vd->backend = realloc(vd->backend, n * sizeof *vd->backend);
	AN(vd->backend);
	vd->weight = realloc(vd->weight, n * sizeof *vd->weight);
	AN(vd->weight);
	vd->l_backend = n;
}

void
udir_new(struct vmod_unidirectors_director **vdp, const char *vcl_name)
{
	struct vmod_unidirectors_director *vd;

	AN(vcl_name);
	AN(vdp);
	AZ(*vdp);
	ALLOC_OBJ(vd, VMOD_UNIDIRECTORS_DIRECTOR_MAGIC);
	AN(vd);
	*vdp = vd;
	AZ(pthread_rwlock_init(&vd->mtx, NULL));

	ALLOC_OBJ(vd->dir, DIRECTOR_MAGIC);
	AN(vd->dir);
	vd->dir->name = "raw";
	REPLACE(vd->dir->vcl_name, vcl_name);
	vd->dir->priv = vd;
	AZ(vd->dir->healthy);
	AZ(vd->dir->resolve);
	vd->vbm = vbit_init(8);
	AZ(vd->priv);
	AN(vd->vbm);
}

void
udir_delete(struct vmod_unidirectors_director **vdp)
{
	struct vmod_unidirectors_director *vd;

	AN(vdp);
	vd = *vdp;
	*vdp = NULL;
	CHECK_OBJ_NOTNULL(vd, VMOD_UNIDIRECTORS_DIRECTOR_MAGIC);

	if (vd->fini)
	        vd->fini(&vd->priv);
	AZ(vd->priv);

	free(vd->backend);
	free(vd->weight);
	AZ(pthread_rwlock_destroy(&vd->mtx));
	free(vd->dir->vcl_name);
	FREE_OBJ(vd->dir);
	vbit_destroy(vd->vbm);
	FREE_OBJ(vd);
}

void
udir_rdlock(struct vmod_unidirectors_director *vd)
{
	CHECK_OBJ_NOTNULL(vd, VMOD_UNIDIRECTORS_DIRECTOR_MAGIC);
	AZ(pthread_rwlock_rdlock(&vd->mtx));
}

void
udir_wrlock(struct vmod_unidirectors_director *vd)
{
	CHECK_OBJ_NOTNULL(vd, VMOD_UNIDIRECTORS_DIRECTOR_MAGIC);
	AZ(pthread_rwlock_wrlock(&vd->mtx));
}

void
udir_unlock(struct vmod_unidirectors_director *vd)
{
	CHECK_OBJ_NOTNULL(vd, VMOD_UNIDIRECTORS_DIRECTOR_MAGIC);
	AZ(pthread_rwlock_unlock(&vd->mtx));
}


void __match_proto__(udir_add_backend_f)
udir_add_backend(struct vmod_unidirectors_director *vd, VCL_BACKEND be, double weight)
{
	unsigned u;

	CHECK_OBJ_NOTNULL(vd, VMOD_UNIDIRECTORS_DIRECTOR_MAGIC);
	AN(be);
	udir_wrlock(vd);
	if (vd->n_backend >= vd->l_backend)
		udir_expand(vd, vd->l_backend + 16);
	assert(vd->n_backend < vd->l_backend);
	u = vd->n_backend++;
	vd->backend[u] = be;
	vd->weight[u] = weight;
	vd->total_weight += weight;
	udir_unlock(vd);
}

unsigned
udir_remove_backend(struct vmod_unidirectors_director *vd, VCL_BACKEND be)
{
	unsigned u, n;

	CHECK_OBJ_NOTNULL(vd, VMOD_UNIDIRECTORS_DIRECTOR_MAGIC);
	if (be == NULL)
		return (vd->n_backend);
	CHECK_OBJ(be, DIRECTOR_MAGIC);
	udir_wrlock(vd);
	for (u = 0; u < vd->n_backend; u++)
		if (vd->backend[u] == be)
			break;
	if (u == vd->n_backend) {
		udir_unlock(vd);
		return (vd->n_backend);
	}
	vd->total_weight -= vd->weight[u];
	n = (vd->n_backend - u) - 1;
	memmove(&vd->backend[u], &vd->backend[u+1], n * sizeof(vd->backend[0]));
	memmove(&vd->weight[u], &vd->weight[u+1], n * sizeof(vd->weight[0]));
	vd->n_backend--;
	udir_unlock(vd);
	return (vd->n_backend);
}

unsigned __match_proto__(vdi_healthy_f)
udir_vdi_healthy(const struct director *dir, const struct busyobj *bo, double *changed)
{
        struct vmod_unidirectors_director *vd;
	CAST_OBJ_NOTNULL(vd, dir->priv, VMOD_UNIDIRECTORS_DIRECTOR_MAGIC);
	return (udir_any_healthy(vd, bo, changed));
}

unsigned
udir_any_healthy(struct vmod_unidirectors_director *vd, const struct busyobj *bo, double *changed)
{
	unsigned retval = 0;
	VCL_BACKEND be;
	unsigned u;
	double c;

	CHECK_OBJ_NOTNULL(vd, VMOD_UNIDIRECTORS_DIRECTOR_MAGIC);
	CHECK_OBJ_ORNULL(bo, BUSYOBJ_MAGIC);
	udir_rdlock(vd);
	if (changed != NULL)
		*changed = 0;
	for (u = 0; u < vd->n_backend; u++) {
		be = vd->backend[u];
		CHECK_OBJ_NOTNULL(be, DIRECTOR_MAGIC);
		retval = be->healthy(be, bo, &c);
		if (changed != NULL && c > *changed)
			*changed = c;
		if (retval)
			break;
	}
	udir_unlock(vd);
	return (retval);
}

static unsigned
udir_pick_by_weight(const struct vmod_unidirectors_director *vd, double w,
		    const struct vbitmap *blacklist)
{
	double a = 0.0;
	VCL_BACKEND be = NULL;
	unsigned u;

	for (u = 0; u < vd->n_backend; u++) {
		be = vd->backend[u];
		CHECK_OBJ_NOTNULL(be, DIRECTOR_MAGIC);
		if (blacklist != NULL && vbit_test(blacklist, u))
			continue;
		a += vd->weight[u];
		if (w < a)
			return (u);
	}
	WRONG("");
}

VCL_BACKEND
udir_pick_be(struct vmod_unidirectors_director *vd, double w, const struct busyobj *bo)
{
	unsigned u;
	double tw = 0.0;
	VCL_BACKEND be = NULL;

	udir_rdlock(vd);
	for (u = 0; u < vd->n_backend; u++) {
		if (vd->backend[u]->healthy(vd->backend[u], bo, NULL)) {
			vbit_clr(vd->vbm, u);
			tw += vd->weight[u];
		} else
			vbit_set(vd->vbm, u);
	}
	if (tw > 0.0) {
		u = udir_pick_by_weight(vd, w * tw, vd->vbm);
		assert(u < vd->n_backend);
		be = vd->backend[u];
		CHECK_OBJ_NOTNULL(be, DIRECTOR_MAGIC);
	}
	udir_unlock(vd);
	return (be);
}

VCL_BACKEND __match_proto__(vdi_search_f)
udir_vdi_search(const struct director *dir, const struct suckaddr *sa)
{
        unsigned u;
	struct vmod_unidirectors_director *vd;
	VCL_BACKEND be = NULL;

	CAST_OBJ_NOTNULL(vd, dir->priv, VMOD_UNIDIRECTORS_DIRECTOR_MAGIC);
	udir_rdlock(vd);
	for (u = 0; u < vd->n_backend && be == NULL; u++) {
	        VCL_BACKEND tbe = vd->backend[u];
		CHECK_OBJ_NOTNULL(tbe, DIRECTOR_MAGIC);
		if (tbe->search)
		        be = tbe->search(tbe, sa);
	}
	udir_unlock(vd);
	return be;
}

VCL_VOID __match_proto__()
vmod_director__init(VRT_CTX, struct vmod_unidirectors_director **vdp, const char *vcl_name)
{
        CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	udir_new(vdp, vcl_name);
}

VCL_VOID __match_proto__()
vmod_director__fini(struct vmod_unidirectors_director **vdp)
{
        udir_delete(vdp);
}

VCL_VOID __match_proto__()
vmod_director_add_backend(VRT_CTX, struct vmod_unidirectors_director *vd, VCL_BACKEND be, double w)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(vd, VMOD_UNIDIRECTORS_DIRECTOR_MAGIC);
	if (vd->add_backend)
	        (void)(vd->add_backend(vd, be, w));
}

VCL_VOID __match_proto__()
vmod_director_remove_backend(VRT_CTX, struct vmod_unidirectors_director *vd, VCL_BACKEND be)
{
        CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
        (void)udir_remove_backend(vd, be);
}

VCL_BACKEND __match_proto__()
vmod_director_backend(VRT_CTX, struct vmod_unidirectors_director *vd)
{
        CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(vd, VMOD_UNIDIRECTORS_DIRECTOR_MAGIC);
	if (vd->pick_dir)
	        return (vd->pick_dir(vd));
	return (vd->dir);
}

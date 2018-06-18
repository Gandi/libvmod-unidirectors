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

#include <stdlib.h>
#include <string.h>

#include "cache/cache.h"
#include "cache/cache_director.h"

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

static void
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
	vd->vcl_name = vcl_name; // XXX dup ?
}

static void
udir_delete(struct vmod_unidirectors_director **vdp)
{
	struct vmod_unidirectors_director *vd;

	AN(vdp);
	vd = *vdp;
	*vdp = NULL;
	CHECK_OBJ_NOTNULL(vd, VMOD_UNIDIRECTORS_DIRECTOR_MAGIC);

	if (vd->fini) {
	        vd->fini(&vd->priv);
		vd->fini = NULL;
	}
	AZ(vd->priv);

	free(vd->backend);
	free(vd->weight);
	AZ(pthread_rwlock_destroy(&vd->mtx));
	if (vd->dir)
	        VRT_DelDirector(&vd->dir);
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

unsigned
_udir_add_backend(struct vmod_unidirectors_director *vd, VCL_BACKEND be, double weight)
{
	unsigned u;

	CHECK_OBJ_NOTNULL(vd, VMOD_UNIDIRECTORS_DIRECTOR_MAGIC);
	if (be == NULL)
		return (0);
	CHECK_OBJ(be, DIRECTOR_MAGIC);
	if (vd->n_backend < UDIR_MAX_BACKEND) {
		if (vd->n_backend >= vd->l_backend)
			udir_expand(vd, vd->l_backend + 16);
		assert(vd->n_backend < vd->l_backend);
		u = vd->n_backend++;
		vd->backend[u] = be;
		vd->weight[u] = weight;
		return (1);
	}
	return (0);
}

unsigned
_udir_remove_backend(struct vmod_unidirectors_director *vd, VCL_BACKEND be)
{
	unsigned u, n;

	CHECK_OBJ_NOTNULL(vd, VMOD_UNIDIRECTORS_DIRECTOR_MAGIC);
	if (be == NULL)
		return (0);
	CHECK_OBJ(be, DIRECTOR_MAGIC);
	for (u = 0; u < vd->n_backend; u++)
		if (vd->backend[u] == be)
			break;
	if (u == vd->n_backend)
		return (0);
	n = (vd->n_backend - u) - 1;
	memmove(&vd->backend[u], &vd->backend[u+1], n * sizeof(vd->backend[0]));
	memmove(&vd->weight[u], &vd->weight[u+1], n * sizeof(vd->weight[0]));
	vd->n_backend--;
	return (1);
}

unsigned v_matchproto_(vdi_healthy_f)
udir_vdi_healthy(VRT_CTX, VCL_BACKEND dir, VCL_TIME *changed)
{
        struct vmod_unidirectors_director *vd;
	CAST_OBJ_NOTNULL(vd, dir->priv, VMOD_UNIDIRECTORS_DIRECTOR_MAGIC);
	return (udir_any_healthy(ctx, vd, changed));
}

unsigned
udir_any_healthy(VRT_CTX, struct vmod_unidirectors_director *vd, VCL_TIME *changed)
{
	unsigned retval = 0;
	VCL_BACKEND be;
	unsigned u;
	double c;

	CHECK_OBJ_NOTNULL(vd, VMOD_UNIDIRECTORS_DIRECTOR_MAGIC);
	udir_rdlock(vd);
	if (changed != NULL)
		*changed = 0;
	for (u = 0; u < vd->n_backend; u++) {
		be = vd->backend[u];
		CHECK_OBJ_NOTNULL(be, DIRECTOR_MAGIC);
		retval = VRT_Healthy(ctx, be, &c);
		if (changed != NULL && c > *changed)
			*changed = c;
		if (retval)
			break;
	}
	udir_unlock(vd);
	return (retval);
}

VCL_BACKEND
udir_pick_be(VRT_CTX, struct vmod_unidirectors_director *vd, double w, be_idx_t *be_idx)
{
	unsigned u, h, n_backend = 0;
	double a, tw = 0.0;
	VCL_BACKEND be;

	AN(be_idx);
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
		w *= tw;
		a = 0.0;
		for (h = 0; h < n_backend; h++) {
			u = be_idx[h];
			assert(u < vd->n_backend);
			a += vd->weight[u];
			if (w < a) {
				be = vd->backend[u];
				CHECK_OBJ_NOTNULL(be, DIRECTOR_MAGIC);
				break;
			}
		}
	}
	return (be);
}

VCL_BACKEND v_matchproto_(vdi_find_f)
udir_vdi_find(VCL_BACKEND dir, const struct suckaddr *sa,
	      int (*cmp)(const struct suckaddr *, const struct suckaddr *))
{
        unsigned u;
	struct vmod_unidirectors_director *vd;
	VCL_BACKEND be, rbe = NULL;

	CAST_OBJ_NOTNULL(vd, dir->priv, VMOD_UNIDIRECTORS_DIRECTOR_MAGIC);
	udir_rdlock(vd);
	for (u = 0; u < vd->n_backend && rbe == NULL; u++) {
	        be = vd->backend[u];
		CHECK_OBJ_NOTNULL(be, DIRECTOR_MAGIC);
		if (be->vdir->methods->find)
		        rbe = be->vdir->methods->find(be, sa, cmp);
	}
	udir_unlock(vd);
	return (rbe);
}

unsigned v_matchproto_(vdi_uptime_f)
udir_vdi_uptime(VRT_CTX, VCL_BACKEND dir, VCL_TIME *changed, double *load)
{
	unsigned u;
	double sum = 0.0, tw = 0.0;
	double c, l, tl = 0;
	struct vmod_unidirectors_director *vd;
	VCL_BACKEND be = NULL;
	unsigned retval = 0;

	CHECK_OBJ_NOTNULL(dir, DIRECTOR_MAGIC);
	CAST_OBJ_NOTNULL(vd, dir->priv, VMOD_UNIDIRECTORS_DIRECTOR_MAGIC);
	udir_rdlock(vd);
	for (u = 0; u < vd->n_backend; u++) {
		be = vd->backend[u];
		CHECK_OBJ_NOTNULL(be, DIRECTOR_MAGIC);
		AN(be->vdir->methods->uptime);
		if (be->vdir->methods->uptime(ctx, be, &c, &l)) {
			retval = 1;
			sum += c * vd->weight[u];
			tw += vd->weight[u];
			tl += l;
		}
	}
	udir_unlock(vd);
	if (changed != NULL)
		*changed = (tw > 0.0 ? sum / tw : 0);
	if (load != NULL)
		*load = tl;
	return (retval);
}

VCL_VOID v_matchproto_()
vmod_director__init(VRT_CTX, struct vmod_unidirectors_director **vdp, const char *vcl_name)
{
        CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	udir_new(vdp, vcl_name);
}

VCL_VOID v_matchproto_()
vmod_director__fini(struct vmod_unidirectors_director **vdp)
{
        udir_delete(vdp);
}

VCL_VOID v_matchproto_()
vmod_director_add_backend(VRT_CTX, struct vmod_unidirectors_director *vd, VCL_BACKEND be, double w)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	udir_wrlock(vd);
	(void)_udir_add_backend(vd, be, w);
	udir_unlock(vd);
}

VCL_VOID v_matchproto_()
vmod_director_remove_backend(VRT_CTX, struct vmod_unidirectors_director *vd, VCL_BACKEND be)
{
        CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	udir_wrlock(vd);
	(void)_udir_remove_backend(vd, be);
	udir_unlock(vd);
}

VCL_BACKEND v_matchproto_()
vmod_director_backend(VRT_CTX, struct vmod_unidirectors_director *vd)
{
        CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(vd, VMOD_UNIDIRECTORS_DIRECTOR_MAGIC);
	return (vd->dir);
}

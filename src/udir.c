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

#include "vsb.h"
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

	TAKE_OBJ_NOTNULL(vd, vdp, VMOD_UNIDIRECTORS_DIRECTOR_MAGIC);

	if (vd->dir)
	        VRT_DelDirector(&vd->dir);

	free(vd->backend);
	free(vd->weight);
	AZ(pthread_rwlock_destroy(&vd->mtx));
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
_udir_add_backend(VRT_CTX, struct vmod_unidirectors_director *vd,
		  VCL_BACKEND be, double weight)
{
	unsigned u;

	CHECK_OBJ_NOTNULL(vd, VMOD_UNIDIRECTORS_DIRECTOR_MAGIC);
	if (be == NULL) {
		VRT_fail(ctx, "%s: NULL backend cannot be added",
			 vd->vcl_name);
		return (0);
	}
	CHECK_OBJ(be, DIRECTOR_MAGIC);
	if (vd->n_backend >= UDIR_MAX_BACKEND) {
		VRT_fail(ctx, "%s: backend cannot be added (max %d)",
			 vd->vcl_name, UDIR_MAX_BACKEND);
		return (0);
	}
	if (vd->n_backend >= vd->l_backend)
		udir_expand(vd, vd->l_backend + 16);
	assert(vd->n_backend < vd->l_backend);
	u = vd->n_backend++;
	vd->backend[u] = be;
	vd->weight[u] = weight;
	return (1);
}

unsigned
_udir_remove_backend(VRT_CTX, struct vmod_unidirectors_director *vd,
		     VCL_BACKEND be)
{
	unsigned u, n;

	CHECK_OBJ_NOTNULL(vd, VMOD_UNIDIRECTORS_DIRECTOR_MAGIC);
	if (be == NULL) {
		VRT_fail(ctx, "%s: NULL backend cannot be removed",
			 vd->vcl_name);
		return (0);
	}
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

VCL_BOOL v_matchproto_(vdi_healthy_f)
udir_vdi_healthy(VRT_CTX, VCL_BACKEND dir, VCL_TIME *changed)
{
	struct vmod_unidirectors_director *vd;
	unsigned retval = 0;
	VCL_BACKEND be;
	unsigned u;
	double c;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(dir, DIRECTOR_MAGIC);
	CAST_OBJ_NOTNULL(vd, dir->priv, VMOD_UNIDIRECTORS_DIRECTOR_MAGIC);

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

void v_matchproto_(vdi_list_f)
udir_vdi_list(VRT_CTX, VCL_BACKEND dir, struct vsb *vsb, int pflag, int jflag)
{
	struct vmod_unidirectors_director *vd;
	VCL_BACKEND be;
	VCL_BOOL h;
	unsigned u, nh = 0;
	double w, tw = 0.0;
	struct vbitmap *healthy = NULL;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(dir, DIRECTOR_MAGIC);
	CAST_OBJ_NOTNULL(vd, dir->priv, VMOD_UNIDIRECTORS_DIRECTOR_MAGIC);

	udir_rdlock(vd);
	if (pflag)
		healthy = vbit_new(vd->n_backend);
	for (u = 0; u < vd->n_backend; u++) {
		be = vd->backend[u];
		CHECK_OBJ_NOTNULL(be, DIRECTOR_MAGIC);

		h = VRT_Healthy(ctx, vd->backend[u], NULL);
		if (h) {
			nh++;
			tw += vd->weight[u];
			if (healthy)
				vbit_set(healthy, u);
		}
	}
	if (pflag) {
		if (jflag) {
			VSB_cat(vsb, "{\n");
			VSB_indent(vsb, 2);
			VSB_printf(vsb, "\"total_weight\": %f,\n", tw);
			VSB_cat(vsb, "\"backends\": {\n");
			VSB_indent(vsb, 2);
		} else {
			VSB_cat(vsb, "\n\n\tBackend\tWeight\tHealth\n");
		}
	}
	for (u = 0; pflag && u < vd->n_backend; u++) {
		be = vd->backend[u];
		AN(healthy);
		h = vbit_test(healthy, u);
		w = h ? vd->weight[u] : 0.0;

		if (jflag) {
			if (u)
				VSB_cat(vsb, ",\n");
			VSB_printf(vsb, "\"%s\": {\n", be->vcl_name);
			VSB_indent(vsb, 2);
			VSB_printf(vsb, "\"weight\": %f,\n", w);
			if (h)
				VSB_cat(vsb, "\"health\": \"healthy\"\n");
			else
				VSB_cat(vsb, "\"health\": \"sick\"\n");

			VSB_indent(vsb, -2);
			VSB_cat(vsb, "}");
		} else {
			VSB_cat(vsb, "\t");
			VSB_cat(vsb, be->vcl_name);
			VSB_printf(vsb, "\t%6.2f%%\t", 100 * w / tw);
			VSB_cat(vsb, h ? "healthy" : "sick");
			VSB_cat(vsb, "\n");
		}
	}
	u = vd->n_backend;
	udir_unlock(vd);
	vbit_destroy(healthy);

	if (jflag && (pflag)) {
		VSB_cat(vsb, "\n");
		VSB_indent(vsb, -2);
		VSB_cat(vsb, "}\n");
		VSB_indent(vsb, -2);
		VSB_cat(vsb, "},\n");
	}

	if (pflag)
		return;

	if (jflag)
		VSB_printf(vsb, "[%u, %u, \"%s\"]", nh, u,
		    nh ? "healthy" : "sick");
	else
		VSB_printf(vsb, "%u/%u\t%s", nh, u, nh ? "healthy" : "sick");
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

VCL_BOOL v_matchproto_(vdi_uptime_f)
udir_vdi_uptime(VRT_CTX, VCL_BACKEND dir, VCL_TIME *changed, double *load)
{
	unsigned u;
	double sum = 0.0, tw = 0.0;
	double c, l, tl = 0;
	struct vmod_unidirectors_director *vd;
	VCL_BACKEND be = NULL;
	unsigned retval = 0;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
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
	(void)_udir_add_backend(ctx, vd, be, w);
	udir_unlock(vd);
}

VCL_VOID v_matchproto_()
vmod_director_remove_backend(VRT_CTX, struct vmod_unidirectors_director *vd, VCL_BACKEND be)
{
        CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	udir_wrlock(vd);
	(void)_udir_remove_backend(ctx, vd, be);
	udir_unlock(vd);
}

VCL_BACKEND v_matchproto_()
vmod_director_backend(VRT_CTX, struct vmod_unidirectors_director *vd)
{
        CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(vd, VMOD_UNIDIRECTORS_DIRECTOR_MAGIC);
	return (vd->dir);
}

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

#include "udir.h"
#include "dynamic.h"

struct vmod_director_fallback {
	unsigned			magic;
#define VMOD_DIRECTOR_FALLBACK_MAGIC    0x4df34074
	unsigned			sticky;
	VCL_BACKEND			be;
};

static void v_matchproto_(vdi_destroy_f)
fb_vdi_destroy(VCL_BACKEND dir)
{
	struct vmod_unidirectors_director *vd;
	struct vmod_director_fallback *fb;
	CHECK_OBJ_NOTNULL(dir, DIRECTOR_MAGIC);
	CAST_OBJ_NOTNULL(vd, dir->priv, VMOD_UNIDIRECTORS_DIRECTOR_MAGIC);
	CAST_OBJ_NOTNULL(fb, vd->priv, VMOD_DIRECTOR_FALLBACK_MAGIC);
	FREE_OBJ(fb);
}

static VCL_BACKEND v_matchproto_(vdi_resolve_f)
fallback_vdi_resolve(VRT_CTX, VCL_BACKEND dir)
{
	struct vmod_unidirectors_director *vd;
	struct vmod_director_fallback *fb;
	unsigned u;
	VCL_BACKEND be, rbe = NULL;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(dir, DIRECTOR_MAGIC);
	CAST_OBJ_NOTNULL(vd, dir->priv, VMOD_UNIDIRECTORS_DIRECTOR_MAGIC);

	udir_rdlock(vd);
	CAST_OBJ_NOTNULL(fb, vd->priv, VMOD_DIRECTOR_FALLBACK_MAGIC);
	if (fb->sticky) {
		be = fb->be;
		for (u = 0; u < vd->n_backend; u++)
			if (be == vd->backend[u]) {
				CHECK_OBJ_NOTNULL(be, DIRECTOR_MAGIC);
				if (VRT_Healthy(ctx, be, NULL))
					rbe = be;
				break;
			}
	}
	for (u = 0; rbe == NULL && u < vd->n_backend; u++) {
		be = vd->backend[u];
		CHECK_OBJ_NOTNULL(be, DIRECTOR_MAGIC);
		if (VRT_Healthy(ctx, be, NULL))
			fb->be = rbe = be;
	}
	udir_unlock(vd);
	return (rbe);
}

static VCL_BOOL v_matchproto_(vdi_uptime_f)
fallback_vdi_uptime(VRT_CTX, VCL_BACKEND dir, VCL_TIME *changed, double *load)
{
	unsigned u;
	unsigned retval = 0;
	double c = 0, l = 0;
	struct vmod_unidirectors_director *vd;
	struct vmod_director_fallback *fb;
	VCL_BACKEND be;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(dir, DIRECTOR_MAGIC);
	CAST_OBJ_NOTNULL(vd, dir->priv, VMOD_UNIDIRECTORS_DIRECTOR_MAGIC);

	udir_rdlock(vd);
	CAST_OBJ_NOTNULL(fb, vd->priv, VMOD_DIRECTOR_FALLBACK_MAGIC);
	if (fb->sticky) {
		be = fb->be;
		for (u = 0; u < vd->n_backend; u++)
			if (be == vd->backend[u]) {
				CHECK_OBJ_NOTNULL(be, DIRECTOR_MAGIC);
				AN(be->vdir->methods->uptime);
				retval = be->vdir->methods->uptime(ctx, be, &c, &l);
				break;
			}
	}
	for (u = 0; !retval && u < vd->n_backend; u++) {
		be = vd->backend[u];
		CHECK_OBJ_NOTNULL(be, DIRECTOR_MAGIC);
		AN(be->vdir->methods->uptime);
		retval = be->vdir->methods->uptime(ctx, be, &c, &l);
	}
	udir_unlock(vd);
	if (changed != NULL)
		*changed = c;
	if (load != NULL)
		*load = l;
	return (retval);
}

static void v_matchproto_(vdi_list_f)
fb_vdi_list(VRT_CTX, VCL_BACKEND dir, struct vsb *vsb, int pflag, int jflag)
{
	struct vmod_unidirectors_director *vd;
	struct vmod_director_fallback *fb;
	VCL_BACKEND be, cbe;
	VCL_BOOL h;
	unsigned u, nh = 0;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(dir, DIRECTOR_MAGIC);
	CAST_OBJ_NOTNULL(vd, dir->priv, VMOD_UNIDIRECTORS_DIRECTOR_MAGIC);

	udir_rdlock(vd);
	CAST_OBJ_NOTNULL(fb, vd->priv, VMOD_DIRECTOR_FALLBACK_MAGIC);
	if (pflag) {
		if (jflag) {
			VSB_cat(vsb, "{\n");
			VSB_indent(vsb, 2);
			VSB_printf(vsb, "\"sticky\": %s,\n",
				   fb->sticky ? "true" : "false");
			VSB_cat(vsb, "\"backends\": {\n");
			VSB_indent(vsb, 2);
		} else {
			VSB_cat(vsb, "\n\n\tBackend\tCurrent\tHealth\n");
		}
	}
	cbe = fb->be;
	for (u = 0; u < vd->n_backend; u++) {
		be = vd->backend[u];
		CHECK_OBJ_NOTNULL(be, DIRECTOR_MAGIC);

		h = VRT_Healthy(ctx, vd->backend[u], NULL);
		if (h)
			nh++;
		if (!pflag)
			continue;
		if (jflag) {
			if (u)
				VSB_cat(vsb, ",\n");
			VSB_printf(vsb, "\"%s\": {\n", be->vcl_name);
			VSB_indent(vsb, 2);
			if (cbe == be)
				VSB_cat(vsb, "\"current\": true,\n");
			else
				VSB_cat(vsb, "\"current\": false,\n");

			if (h)
				VSB_cat(vsb, "\"health\": \"healthy\"\n");
			else
				VSB_cat(vsb, "\"health\": \"sick\"\n");

			VSB_indent(vsb, -2);
			VSB_cat(vsb, "}");
		} else {
			VSB_cat(vsb, "\t");
			VSB_cat(vsb, be->vcl_name);
			if (cbe == be)
				VSB_cat(vsb, "\t*\t");
			else
				VSB_cat(vsb, "\t\t");
			VSB_cat(vsb, h ? "healthy" : "sick");
			VSB_cat(vsb, "\n");
		}
	}
	u = vd->n_backend;
	udir_unlock(vd);

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

static const struct vdi_methods fallback_methods[1] = {{
	.magic =		VDI_METHODS_MAGIC,
	.type =			"fallback",
	.healthy =		udir_vdi_healthy,
	.resolve =		fallback_vdi_resolve,
	.find =			udir_vdi_find,
	.uptime =		fallback_vdi_uptime,
	.destroy =		fb_vdi_destroy,
	.list =	                fb_vdi_list,
}};

VCL_VOID v_matchproto_()
vmod_director_fallback(VRT_CTX, struct vmod_unidirectors_director *vd, VCL_BOOL sticky)
{
	struct vmod_director_fallback *fb;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(vd, VMOD_UNIDIRECTORS_DIRECTOR_MAGIC);

	if (vd->dir) {
		VRT_fail(ctx, "%s: LB method is already set", vd->vcl_name);
		return;
	}
	udir_wrlock(vd);

	ALLOC_OBJ(fb, VMOD_DIRECTOR_FALLBACK_MAGIC);
	vd->priv = fb;
	AN(vd->priv);
	fb->sticky = sticky;
	fb->be = NULL;

	vd->dir = VRT_AddDirector(ctx, fallback_methods, vd, "%s", vd->vcl_name);

	udir_unlock(vd);
}

VCL_VOID v_matchproto_()
vmod_dyndirector_fallback(VRT_CTX, struct vmod_unidirectors_dyndirector *dyn, VCL_BOOL sticky)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(dyn, VMOD_UNIDIRECTORS_DYNDIRECTOR_MAGIC);
	vmod_director_fallback(ctx, dyn->vd, sticky);
}

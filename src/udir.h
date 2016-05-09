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

struct vmod_unidirectors_director;

typedef void udir_add_backend_f(struct vmod_unidirectors_director *, VCL_BACKEND, double);
typedef void udir_remove_backend_f(struct vmod_unidirectors_director *, VCL_BACKEND);
typedef VCL_BACKEND udir_pick_dir_f(struct vmod_unidirectors_director *);
typedef void udir_fini_f(void **);

struct vmod_unidirectors_director {
	unsigned				magic;
#define VMOD_UNIDIRECTORS_DIRECTOR_MAGIC	0x82c52b08
	pthread_rwlock_t			mtx;
	unsigned				n_backend;
	unsigned				l_backend;
	VCL_BACKEND				*backend;
	double					*base_weight;
	double					*pick_weight;
	struct director				*dir;

        udir_add_backend_f      *add_backend;
        udir_remove_backend_f   *remove_backend;
        udir_pick_dir_f         *pick_dir;
        udir_fini_f             *fini;
        void                    *priv;
};

void udir_new(struct vmod_unidirectors_director**vdp, const char *vcl_name);
void udir_delete(struct vmod_unidirectors_director**vdp);
void udir_rdlock(struct vmod_unidirectors_director*vd);
void udir_wrlock(struct vmod_unidirectors_director*vd);
void udir_unlock(struct vmod_unidirectors_director*vd);
void udir_add_backend(struct vmod_unidirectors_director*, VCL_BACKEND be, double weight);
unsigned udir_remove_backend(struct vmod_unidirectors_director*, VCL_BACKEND be);
unsigned udir_any_healthy(struct vmod_unidirectors_director*, const struct busyobj *,
    double *changed);
unsigned udir_pick_by_weight(const struct vmod_unidirectors_director *vd, double w);
VCL_BACKEND udir_pick_be(struct vmod_unidirectors_director*, double w, const struct busyobj *);

VCL_BACKEND udir_vdi_search(const struct director*, const struct suckaddr *sa);
unsigned udir_vdi_busy(const struct director*, const struct busyobj *bo, double *changed, double *load);
unsigned udir_vdi_healthy(const struct director *, const struct busyobj *bo, double *changed);

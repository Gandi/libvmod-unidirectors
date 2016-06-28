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

typedef VCL_BACKEND udir_pick_dir_f(struct vmod_unidirectors_director *);
typedef void udir_fini_f(void **);

/* number of backend per director limit to 256 per default */
typedef uint8_t be_idx_t;
#define UDIR_MAX_BACKEND (1 << sizeof(be_idx_t) * 8)

struct vmod_unidirectors_director {
	unsigned				magic;
#define VMOD_UNIDIRECTORS_DIRECTOR_MAGIC	0x82c52b08
	pthread_rwlock_t			mtx;
	unsigned				n_backend;
	unsigned				l_backend;
	VCL_BACKEND				*backend;
	double					*weight;
	struct director				*dir;

        udir_pick_dir_f         *pick_dir;
        udir_fini_f             *fini;
        void                    *priv;
};

void udir_delete_priv(struct vmod_unidirectors_director *vd);
void udir_rdlock(struct vmod_unidirectors_director*vd);
void udir_wrlock(struct vmod_unidirectors_director*vd);
void udir_unlock(struct vmod_unidirectors_director*vd);
unsigned udir_any_healthy(struct vmod_unidirectors_director*, const struct busyobj *,
    double *changed);
VCL_BACKEND udir_pick_be(struct vmod_unidirectors_director*, double w, be_idx_t *be_idx, struct busyobj *);

VCL_BACKEND udir_vdi_search(const struct director*, const struct suckaddr *sa);
unsigned udir_vdi_busy(const struct director*, const struct busyobj *bo, double *changed, double *load);
unsigned udir_vdi_healthy(const struct director *, const struct busyobj *bo, double *changed);

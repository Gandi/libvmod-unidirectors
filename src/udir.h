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
	const char				*vcl_name;
	VCL_BACKEND				dir;

        void					*priv;
};

void udir_rdlock(struct vmod_unidirectors_director*vd);
void udir_wrlock(struct vmod_unidirectors_director*vd);
void udir_unlock(struct vmod_unidirectors_director*vd);
unsigned _udir_remove_backend(VRT_CTX, struct vmod_unidirectors_director *vd, VCL_BACKEND be);
unsigned _udir_add_backend(VRT_CTX, struct vmod_unidirectors_director *vd, VCL_BACKEND be, double weight);
unsigned udir_any_healthy(VRT_CTX, struct vmod_unidirectors_director*, VCL_TIME *changed);
VCL_BACKEND udir_vdi_find(VCL_BACKEND, const struct suckaddr *sa,
			  int (*cmp)(const struct suckaddr *, const struct suckaddr *));
unsigned udir_vdi_uptime(VRT_CTX, VCL_BACKEND, VCL_TIME *changed, double *load);
unsigned udir_vdi_healthy(VRT_CTX, VCL_BACKEND, VCL_TIME *changed);

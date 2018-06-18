/*-
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
#include <sys/socket.h>

#include "cache/cache.h"
#include "cache/cache_director.h"

#include "vsa.h"

VCL_STRING v_matchproto_()
vmod_backend_type(VRT_CTX, VCL_BACKEND be)
{
        CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	if (be == NULL)
	       return (NULL);
	CHECK_OBJ_NOTNULL(be, DIRECTOR_MAGIC);
	return (be->vdir->methods->type);
}

VCL_BACKEND v_matchproto_()
vmod_find_backend(VRT_CTX, VCL_BACKEND be, VCL_IP sa)
{
        CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	if (be == NULL)
	       return (NULL);
	CHECK_OBJ_NOTNULL(be, DIRECTOR_MAGIC);
	if (be->vdir->methods->find)
	       return (be->vdir->methods->find(be, sa, VSA_Compare_IP));
	return (NULL);
}

VCL_BOOL v_matchproto_()
vmod_is_backend(VRT_CTX, VCL_BACKEND be)
{
        CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	return (be != NULL);
}

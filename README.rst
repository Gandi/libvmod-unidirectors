============
vmod_unidirectors
============

----------------------
Varnish Directors Module
----------------------

:Date: 2016-03-25
:Version: 1.0
:Manual section: 3


=================
vmod_unidirectors
=================

------------------------
Varnish Directors Module
------------------------

:Manual section: 3

SYNOPSIS
========

import unidirectors [from "path"] ;


DESCRIPTION
===========

`vmod_unidirectors` enables backend load balancing in Varnish.

The module implements a set of basic load balancing techniques. It's
based on vmod_directors. The major change is the unification of directors
type.
One of the goal is to mimic Varnish 3.0 functionality like to easly stack
hash director on fallback director.
Only one director C-type is generated, more convenient to manipulate it
with inline C.

To enable load balancing you must import this vmod (unidirectors).

Then you define your backends. Once you have the backends declared you
can add them to a director. This happens in executed VCL code. If you
want to emulate the previous behavior of Varnish 3.0 you can just
initialize the directors in vcl_init, like this::

    sub vcl_init {
	new udir2 = unidirectors.director();
	udir2.round_robin();
	udir2.add_backend(backend1);
	udir2.add_backend(backend2);

	new udir1 = unidirectors.director();
	udir1.hash("client-identity");
	udir1.add_backend(backendA);
	udir1.add_backend(backendB);

	new udir = unidirectors.director();
	udir.fallback();
	udir.add_backend(udir1.backend());
	udir.add_backend(udir2.backend());
    }

As you can see there is nothing keeping you from manipulating the
directors elsewhere in VCL. So, you could have VCL code that would
add more backends to a director when a certain URL is called.

CONTENTS
========

* :ref:`obj_director`
* :ref:`func_director.add_backend`
* :ref:`func_director.backend`
* :ref:`func_director.fallback`
* :ref:`func_director.hash`
* :ref:`func_director.random`
* :ref:`func_director.remove_backend`
* :ref:`func_director.round_robin`
* :ref:`func_search_backend`

.. _obj_director:

Object director
===============


Description
	Create a raw director.

	You need to set a load balancing method before to use it.

Example
	new udir = unidirectors.director()

.. _func_director.round_robin:

VOID director.round_robin()
---------------------------

Prototype
	VOID director.round_robin()

Description
	Configure a director as round robin.

	This director will pick backends in a round robin fashion.

Example
	udir.round_robin();

.. _func_director.fallback:

VOID director.fallback()
------------------------

Prototype
	VOID director.fallback()

Description
	Configure a director as fallback.

	A fallback director will try each of the added backends in turn,
	and return the first one that is healthy.

Example
	udir.fallback();

.. _func_director.random:

VOID director.random()
----------------------

Prototype
	VOID director.random()

Description
	Configure a director as random.

	The random director distributes load over the backends using
	a weighted random probability distribution.

Example
	udir.random();

.. _func_director.hash:

VOID director.hash(STRING)
--------------------------

Prototype
	VOID director.hash(STRING hdr)

Description
	Configure a director as hash.

	The director chooses the backend server by computing a hash/digest
	of the http header in param.

	Commonly used with ``client.ip`` or a session cookie to get
	sticky sessions.

Example
	udir.hash("client-identity");
	set req.http.client-identity = client.ip;

.. _func_director.add_backend:

VOID director.add_backend(BACKEND, REAL)
----------------------------------------

Prototype
	VOID director.add_backend(BACKEND, REAL weight)

Description
	Add a backend to the director with an optional weight.

	Weight is only relevent for some load balancing method.
	1.0 is the defaut value.

Example
	udir.add_backend(backend1);
	udir.add_backend(backend2, 2.0);

.. _func_director.remove_backend:

VOID director.remove_backend(BACKEND)
-------------------------------------

Prototype
	VOID director.remove_backend(BACKEND)

Description
	Remove a backend from the director.
Example
	udir.remove_backend(backend1);
	udir.remove_backend(backend2);

.. _func_director.backend:

BACKEND director.backend()
--------------------------

Prototype
	BACKEND director.backend()

Description
	Pick a backend from the director.
Example
	set req.backend_hint = udir.backend();

.. _func_search_backend:

BACKEND search_backend(BACKEND, IP)
-----------------------------------

Prototype
	BACKEND search_backend(BACKEND, IP)

Description
	Pick a backend matching the IP from the director.
Example
	set req.backend_hint = unidirectors.search_backend(udir, client.ip);


INSTALLATION
============

The source tree is based on autotools to configure the building, and
does also have the necessary bits in place to do functional unit tests
using the ``varnishtest`` tool.

Building requires the Varnish header files and uses pkg-config to find
the necessary paths.

Pre-requisites::

 sudo apt-get install -y autotools-dev make automake libtool pkg-config libvarnishapi1 libvarnishapi-dev

Usage::

 ./autogen.sh
 ./configure

If you have installed Varnish to a non-standard directory, call
``autogen.sh`` and ``configure`` with ``PKG_CONFIG_PATH`` pointing to
the appropriate path. For unidirectors, when varnishd configure was called
with ``--prefix=$PREFIX``, use

 PKG_CONFIG_PATH=${PREFIX}/lib/pkgconfig
 export PKG_CONFIG_PATH

Make targets:

* make - builds the vmod.
* make install - installs your vmod.
* make check - runs the unit tests in ``src/tests/*.vtc``
* make distcheck - run check and prepare a tarball of the vmod.

Installation directories
------------------------

By default, the vmod ``configure`` script installs the built vmod in
the same directory as Varnish, determined via ``pkg-config(1)``. The
vmod installation directory can be overridden by passing the
``VMOD_DIR`` variable to ``configure``.

Other files like man-pages and documentation are installed in the
locations determined by ``configure``, which inherits its default
``--prefix`` setting from Varnish.


COMMON PROBLEMS
===============

* configure: error: Need varnish.m4 -- see README.rst

  Check if ``PKG_CONFIG_PATH`` has been set correctly before calling
  ``autogen.sh`` and ``configure``

* Incompatibilities with different Varnish Cache versions

  Make sure you build this vmod against its correspondent Varnish Cache version.
  For unidirectors, to build against Varnish Cache 4.0, this vmod must be built from branch 4.0.

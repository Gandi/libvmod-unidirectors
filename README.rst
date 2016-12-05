============
vmod_unidirectors
============

----------------------
Varnish Directors Module
----------------------

:Date: 2016-03-25
:Version: 1.0.0
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

The module implements a set of load balancing techniques. It's based on
vmod_directors. The major change is the unification of directors
type and the ability to change the load balancing method dynamically.

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

* :ref:`func_backend_type`
* :ref:`obj_director`
* :ref:`func_director.add_backend`
* :ref:`func_director.backend`
* :ref:`func_director.fallback`
* :ref:`func_director.hash`
* :ref:`func_director.leastconn`
* :ref:`func_director.random`
* :ref:`func_director.remove_backend`
* :ref:`func_director.round_robin`
* :ref:`func_find_backend`
* :ref:`func_is_backend`

.. _obj_director:

director
--------

::

	new OBJ = director()

Description
	Create a raw director.

	You need to set a load balancing method before to use it.

Example
	new udir = unidirectors.director()

.. _func_director.round_robin:

director.round_robin
--------------------

::

	VOID director.round_robin()

Description
	Configure a director as round robin.

	This director will pick backends in a round robin fashion
	according to weight.

Example
	udir.round_robin();

.. _func_director.fallback:

director.fallback
-----------------

::

	VOID director.fallback()

Description
	Configure a director as fallback.

	A fallback director will try each of the added backends in turn,
	and return the first one that is healthy.

Example
	udir.fallback();

.. _func_director.random:

director.random
---------------

::

	VOID director.random()

Description
	Configure a director as random.

	The random director distributes load over the backends using
	a weighted random probability distribution.

Example
	udir.random();

.. _func_director.hash:

director.hash
-------------

::

	VOID director.hash(STRING hdr="")

Description
	Configure a director as hash.

	The director chooses the backend server by computing a hash/digest
	of the http header in param.

	Commonly used with ``client.ip`` or a session cookie to get
	sticky sessions.

Example
	udir.hash("client-identity");
	set req.http.client-identity = client.ip;

.. _func_director.leastconn:

director.leastconn
------------------

::

	VOID director.leastconn(INT slow_start=0)

Description
	Configure a director as least connections.

	The director chooses the less busy backend server.
	A weight based on number of connections is used on tcp backend.
	The slow start optional parameter is defined in seconds.

	WARNING: need unidirectors patch for Varnish (for vdi_uptime_f)

Example
	udir.leastconn(30);

.. _func_director.add_backend:

director.add_backend
--------------------

::

	VOID director.add_backend(BACKEND, REAL weight=1.0)

Description
	Add a backend to the director with an optional weight.

	1.0 is the defaut value.

Example
	udir.add_backend(backend1);
	udir.add_backend(backend2, 2.0);

.. _func_director.remove_backend:

director.remove_backend
-----------------------

::

	VOID director.remove_backend(BACKEND)

Description
	Remove a backend from the director.
Example
	udir.remove_backend(backend1);
	udir.remove_backend(backend2);

.. _func_director.backend:

director.backend
----------------

::

	BACKEND director.backend()

Description
	Pick a backend from the director.
Example
	set req.backend_hint = udir.backend();

.. _func_find_backend:

find_backend
------------

::

	BACKEND find_backend(BACKEND, IP)

Description
	Pick a backend matching the IP from the director.

	WARNING: need unidirector patch for Varnish (for vdi_find_f)

Example
	set req.backend_hint = unidirectors.search(udir.backend(), client.ip);

.. _func_is_backend:

is_backend
----------

::

	BOOL is_backend(BACKEND)

Description
	Test if we have a backend (healthy or not).
	Useful to authorise the backends to PURGE itself.
Example
	if (!unidirectors.is_backend(unidirectors.search_backend(req.backend_hint, client.ip))) {
	    	return (synth(405));
	}

.. _func_backend_type:

backend_type
------------

::

	STRING backend_type(BACKEND)

Description
	Return the type of the backend.
Example
	set beresp.http.director = unidirectors.backend_type(bereq.backend);

INSTALLATION
============

The source tree is based on autotools to configure the building, and
does also have the necessary bits in place to do functional unit tests
using the ``varnishtest`` tool.

Building requires the Varnish header files and uses pkg-config to find
the necessary paths.

Pre-requisites::

 WARNING: find_backend and leastconn method need Varnish patchs
 see https://github.com/ehocdet/varnish-cache/tree/5.0-unidirector

 sudo apt-get install -y autotools-dev make automake libtool pkg-config libvarnishapi1 libvarnishapi-dev

Usage::

    ./autogen.sh
    ./configure

If you have installed Varnish to a non-standard directory, call
``autogen.sh`` and ``configure`` with ``PKG_CONFIG_PATH`` pointing to
the appropriate path. For unidirectors, when varnishd configure was called
with ``--prefix=$PREFIX``, use::

    PKG_CONFIG_PATH=${PREFIX}/lib/pkgconfig
    export PKG_CONFIG_PATH

Make targets:

* ``make`` - builds the vmod.
* ``make install`` - installs your vmod.
* ``make check`` - runs the unit tests in ``src/tests/*.vtc``
* ``make distcheck`` - run check and prepare a tarball of the vmod.

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

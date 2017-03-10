.. _vmod_unidirectors(3):

=================
vmod_unidirectors
=================

------------------------
Varnish Directors Module
------------------------

:Date: 2017-03-10
:Version: 2.0.0
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
Dynamic backends is built in with dyndirector, all load balancing method
is supported.

To enable load balancing you must import this vmod (unidirectors).

Then you define your backends. Once you have the backends declared you
can add them to a director. This happens in executed VCL code. If you
want to emulate the previous behavior of Varnish 3.0 you can just
initialize the directors in vcl_init, like this::

    sub vcl_init {
	new udir2 = unidirectors.dyndirector();
	udir2.leastconn();
	udir2.lookup_addr("service1.example.net");

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
* :ref:`obj_dyndirector`
* :ref:`func_dyndirector.add_IP`
* :ref:`func_dyndirector.add_backend`
* :ref:`func_dyndirector.backend`
* :ref:`func_dyndirector.debug`
* :ref:`func_dyndirector.fallback`
* :ref:`func_dyndirector.hash`
* :ref:`func_dyndirector.leastconn`
* :ref:`func_dyndirector.lookup_addr`
* :ref:`func_dyndirector.random`
* :ref:`func_dyndirector.remove_IP`
* :ref:`func_dyndirector.remove_backend`
* :ref:`func_dyndirector.round_robin`
* :ref:`func_dyndirector.update_IPs`
* :ref:`func_is_backend`
* :ref:`func_search_backend`

.. _obj_director:

Object director
===============


Description
	Create a director. The default load balancing is random.
	Load balancing method can be changed.

Example
	new udir = unidirectors.director()

.. _func_director.round_robin:

VOID director.round_robin()
---------------------------

Prototype
	VOID director.round_robin()

Description
	Configure a director as round robin.

	This director will pick backends in a round robin fashion
	according to weight.

Example
	udir.round_robin();

.. _func_director.fallback:

VOID director.fallback(BOOL)
----------------------------

Prototype
	VOID director.fallback(BOOL sticky)

Description
	Configure a director as fallback.

	A fallback director will try each of the added backends in turn,
	and return the first one that is healthy.
	If sticky is set, the director doesn't go back to a higher priority
	backend coming back to health.

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

.. _func_director.leastconn:

VOID director.leastconn(INT)
----------------------------

Prototype
	VOID director.leastconn(INT slow_start)

Description
	Configure a director as least connections.

	The director chooses the less busy backend server.
	A weight based on number of connections is used on tcp backend.
	The slow start optional parameter is defined in seconds.

	WARNING: need vdi_busy patch for Varnish

Example
	udir.leastconn(30);

.. _func_director.add_backend:

VOID director.add_backend(BACKEND, REAL)
----------------------------------------

Prototype
	VOID director.add_backend(BACKEND, REAL weight)

Description
	Add a backend to the director with an optional weight.

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

.. _obj_dyndirector:

Object dyndirector
==================

Description
	Create a dynamic director. The default load balancing is random.
	Load balancing method can be changed.
	Dyndirector inherit from director object: all director's methods can be used.
	Dynamic director can manipulate dynamic backends. All dynamic backends are
	created with the same default values (port, probe, timeouts and max_connections).
	The uniqueness of dynamic backends is carried by the IP. Inherited backends do
	not interact with dynamic backends.
Example
	new udir = unidirectors.dyndirector()

.. _func_dyndirector.round_robin:

VOID dyndirector.round_robin()
------------------------------

Prototype
	VOID dyndirector.round_robin()

Description
	Configure a dynamic director as round robin.
Example
	udir.round_robin();

.. _func_dyndirector.fallback:

VOID dyndirector.fallback(BOOL)
-------------------------------

Prototype
	VOID dyndirector.fallback(BOOL sticky)

Description
	Configure a dynamic director as fallback.
Example
	udir.fallback();

.. _func_dyndirector.random:

VOID dyndirector.random()
-------------------------

Prototype
	VOID dyndirector.random()

Description
	Configure a dynamic director as random.
Example
	udir.random();

.. _func_dyndirector.hash:

VOID dyndirector.hash(STRING)
-----------------------------

Prototype
	VOID dyndirector.hash(STRING hdr)

Description
	Configure a dynamic director as hash.
Example
	udir.hash("client-identity");
	set req.http.client-identity = client.ip;

.. _func_dyndirector.leastconn:

VOID dyndirector.leastconn(INT)
-------------------------------

Prototype
	VOID dyndirector.leastconn(INT slow_start)

Description
	Configure a dynamic director as least connections.
Example
	udir.leastconn(30);

.. _func_dyndirector.add_IP:

VOID dyndirector.add_IP(STRING, REAL)
-------------------------------------

Prototype
	VOID dyndirector.add_IP(STRING ip, REAL weight)

Description
	Add a dynamic backend with IP and an optional weight if not already set.
	It can be removed by update_IPs() or lookup_addr() call.
Example
	udir.add_IP("1.2.3.4")

.. _func_dyndirector.remove_IP:

VOID dyndirector.remove_IP(STRING)
----------------------------------

Prototype
	VOID dyndirector.remove_IP(STRING ip)
	Remove a dynamic backend with IP.
Example
	udir.remove_IP("1.2.3.4")

.. _func_dyndirector.update_IPs:

VOID dyndirector.update_IPs(STRING)
-----------------------------------

Prototype
	VOID dyndirector.update_IPs(STRING)

Description
	Update dynamic backends with list of IP. It replace old ones, or keep
	unchanged for same IP. Weight of new backends is set to 1.
	It will replace dynamic backends create with lookup_addr() until the next
	lookup call. It will replace dynamic backends create with add_IP().
Example
	udir.update_IPs("1.2.3.4, 1.2.3.5");

.. _func_dyndirector.lookup_addr:

VOID dyndirector.lookup_addr(STRING, ACL, DURATION)
---------------------------------------------------

Prototype
	VOID dyndirector.lookup_addr(STRING addr, ACL whitelist, DURATION ttl)

Description
	Update dynamic backends with DNS lookups with a frequency of ttl.
	Weight of new backends is set to 1.
	It will replace dynamic backends create with update_IPs() or add_IP().
Example
	udir.lookup_addr("prod.mydomaine.live");

.. _func_dyndirector.backend:

BACKEND dyndirector.backend()
-----------------------------

Prototype
	BACKEND dyndirector.backend()

Description
	Pick a backend from the dynamic director.
Example
	set req.backend_hint = udir.backend();

.. _func_dyndirector.add_backend:

VOID dyndirector.add_backend(BACKEND, REAL)
-------------------------------------------

Prototype
	VOID dyndirector.add_backend(BACKEND, REAL weight)

Description
	Add a backend to the dynamic director with an optional weight.
	This backend will be ignored by update_IPs() and lookup_addr()
	and will remain configured until a remove_backend();

.. _func_dyndirector.remove_backend:

VOID dyndirector.remove_backend(BACKEND)
----------------------------------------

Prototype
	VOID dyndirector.remove_backend(BACKEND)

Description
	Remove a backend set by add_backend() from the dynamic director.

.. _func_dyndirector.debug:

VOID dyndirector.debug(BOOL)
----------------------------

Prototype
	VOID dyndirector.debug(BOOL enable)

Description
        Enable or disable debugging for a dynamic director.

.. _func_search_backend:

BACKEND search_backend(BACKEND, IP)
-----------------------------------

Prototype
	BACKEND search_backend(BACKEND, IP)

Description
	Pick a backend matching the IP from the director.

	WARNING: need vdi_search patch for Varnish

Example
	set req.backend_hint = unidirectors.search(udir.backend(), client.ip);

.. _func_is_backend:

BOOL is_backend(BACKEND)
------------------------

Prototype
	BOOL is_backend(BACKEND)

Description
	Test if we have a backend (healthy or not).
	Useful to authorise the backends to PURGE itself.
Example
	if (!unidirectors.is_backend(unidirectors.search_backend(req.backend_hint, client.ip))) {
	    	return (synth(405));
	}

.. _func_backend_type:

STRING backend_type(BACKEND)
----------------------------

Prototype
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

 WARNING: search_backend and leastconn method need Varnish patchs
 see https://github.com/ehocdet/varnish-cache/tree/4.1-unidirector

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

..
   Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
   AVSystem Anjay LwM2M SDK
   All rights reserved.

   Licensed under the AVSystem-5-clause License.
   See the attached LICENSE file for details.

Migrating from Anjay 2.7.x
==========================

.. contents:: :local:

.. highlight:: c

Introduction
------------

While most changes since Anjay 2.7 are minor, some of them (changes to commonly
used APIs such as Attribute Storage and offline mode control) are breaking.
There is a change to the way the ``con`` attribute is handled in the API.
The advancements in HSM integration in ``avs_commons`` also required some
breaking changes in they way compile-time configuration of that library is
performed. ``avs_commons`` 5.0 also includes refactoring of the APIs related
to (D)TLS PSK credentials.


You may also need to adjust your code if you maintain your own socket
integration, or if it accesses the ``avs_net_security_info_t`` structure
directly. The latter is especially likely if you maintain your own
implementation of the TLS layer.

If you are using an alternative build system, not utilizing the included CMake
scripts, you might additionally need to make adjustments to your configuration
headers.

Change to minimum CMake version
-------------------------------

Declared minimum CMake version necessary for CMake-based compilation, as well as
for importing the installed library through ``find_package()``, is now 3.6. If
you're using some Linux distribution that only has an older version in its
repositories (notably, Ubuntu 16.04), we recommend using one of the following
install methods instead:

* `Kitware APT Repository for Debian and Ubuntu <https://apt.kitware.com/>`_
* `Snap Store <https://snapcraft.io/cmake>`_ (``snap install cmake``)
* `Python Package Index <https://pypi.org/project/cmake/>`_
  (``pip install cmake``)

This change does not affect users who compile the library using some alternative
approach, without using the provided CMake scripts.

Changes in Anjay proper
-----------------------

Refactor of the Attribute Storage module
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The Attribute Storage feature is no longer a standalone module and has been
moved to the library core. From the user perspective, this has the following
consequences:

* Explicit installation of this module in runtime is no longer necessary. The
  ``anjay_attr_storage_install()`` method has been removed.
* The ``ANJAY_WITH_MODULE_ATTR_STORAGE`` configuration macro in
  ``anjay_config.h`` has been renamed to ``ANJAY_WITH_ATTR_STORAGE``.
* The ``WITH_MODULE_attr_storage`` CMake option (equivalent to the macro
  mentioned above) has been renamed to ``WITH_ATTR_STORAGE``.

Additionally, the behavior of ``anjay_attr_storage_restore()`` has been
changed - from now on, this function fails if supplied source stream is
invalid and the Attribute Storage remains untouched. This change makes the
function consistent with other ``anjay_*_restore()`` APIs.

Refactor of offline mode control API
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Since Anjay 2.4, offline mode is configurable independently per every
transport. Below is a list of removed functions and counterparts that should
be used:

+--------------------------------+------------------------------------------+
| Removed function               | Counterpart                              |
+--------------------------------+------------------------------------------+
| ``anjay_is_offline()``         | ``anjay_transport_is_offline()``         |
+--------------------------------+------------------------------------------+
| ``anjay_enter_offline()``      | ``anjay_transport_enter_offline()``      |
+--------------------------------+------------------------------------------+
| ``anjay_exit_offline()``       | ``anjay_transport_exit_offline()``       |
+--------------------------------+------------------------------------------+
| ``anjay_schedule_reconnect()`` | ``anjay_transport_schedule_reconnect()`` |
+--------------------------------+------------------------------------------+

New functions should be called with ``transport_set`` argument set to
``ANJAY_TRANSPORT_SET_ALL`` to achieve the same behavior.

Addition of the con attribute to public API
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The ``con`` attribute, enabled via the ``ANJAY_WITH_CON_ATTR`` compile-time
option, has been previously supported as a custom extension. Since an identical
flag has been standardized as part of LwM2M TS 1.2, it has been included in the
public API as part of preparations to support the new protocol version.

If you initialize ``anjay_dm_oi_attributes_t`` or ``anjay_dm_r_attributes_t``
objects manually, you may need to initialize the new ``con`` field as well,
since the empty ``ANJAY_DM_CON_ATTR_NONE`` value is **NOT** the default
zero-initialized value.

As more new attributes may be added in future versions of Anjay, it is
recommended to initialize such structures with ``ANJAY_DM_OI_ATTRIBUTES_EMPTY``
or ``ANJAY_DM_R_ATTRIBUTES_EMPTY`` constants, and then fill in the attributes
you actually intend to set.

Default (D)TLS version
^^^^^^^^^^^^^^^^^^^^^^

When the `anjay_configuration_t::dtls_version
<../api/structanjay__configuration.html#ab32477e7370a36e02db5b7e7ccbdd89d>`_
field is set to ``AVS_NET_SSL_VERSION_DEFAULT`` (which includes the case of
zero-initialization), Anjay 3.0 and earlier automatically mapped this setting to
``AVS_NET_SSL_VERSION_TLSv1_2`` to ensure that (D)TLS 1.2 is used as mandated by
the LwM2M specification.

This mapping has been removed in Anjay 3.1, which means that the default version
configuration of the underlying (D)TLS library will be used. This has been done
to automatically allow the use of newer protocols and deprecate old versions
when the backend library is updated, without the need to update Anjay code.
However, depending on the (D)TLS backend library used, this may lead to (D)TLS
1.1 or earlier being used if the server does not properly negotiate a higher
version. Please explicitly set ``dtls_version`` to
``AVS_NET_SSL_VERSION_TLSv1_2`` if you want to disallow this.

Please note that Mbed TLS 3.0 has dropped support for TLS 1.1 and earlier, so
this change will not affect behavior with that library.

Conditional compilation for structured security credential support
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The ``anjay_ret_certificate_chain_info()`` and ``anjay_ret_private_key_info()``
APIs, as well as avs_crypto-based fields in ``anjay_security_instance_t``, have
been put under a new conditional compilation flag,
``ANJAY_WITH_SECURITY_STRUCTURED``.

When using CMake, this flag is controlled with the ``WITH_SECURITY_STRUCTURED``
option and enabled by default if available. Otherwise, it might need to be
enabled by defining ``ANJAY_WITH_SECURITY_STRUCTURED`` in ``anjay_config.h``.


Changes in avs_commons
----------------------

Separation of avs_url module
^^^^^^^^^^^^^^^^^^^^^^^^^^^^

URL handling routines, previously a part of ``avs_net``, are now a separate
component of ``avs_commons``. The specific consequences of that may vary
depending on your build process, e.g.:

* You will need to add ``#define AVS_COMMONS_WITH_AVS_URL`` to your
  ``avs_commons_config.h`` if you specify it manually
* You may need to add ``-lavs_url`` to your link command if you're using
  ``avs_commons`` that has been manually compiled separately using CMake

Renamed configuration macro in avs_commons_config.h
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The ``AVS_COMMONS_NET_WITH_PSK`` configuration macro in ``avs_commons_config.h``
has been renamed to ``AVS_COMMONS_WITH_AVS_CRYPTO_PSK``.

You may need to update your configuration files if you are not using CMake, or
your preprocessor directives if you check this macro in your code.

Introduction of new socket option
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

avs_commons 4.10.1 bundled with Anjay 2.15.1 adds a new socket option key:
``AVS_NET_SOCKET_HAS_BUFFERED_DATA``. This is used to make sure that when
control is returned to the event loop, the ``poll()`` call will not stall
waiting for new data that in reality has been already buffered and could be
retrieved using the avs_commons APIs.

This is usually meaningful for (D)TLS connections, but for almost all simple
unencrypted socket implementations, this should always return ``false``.

This was previously achieved by always trying to receive more packets with
timeout set to zero. However, it has been determined that such logic could lead
to heavy blocking of the event loop in case communication with the network stack
is relatively slow, e.g. on devices which implement TCP/IP sockets through modem
AT commands.

If you maintain your own socket integration layer or (D)TLS integration layer,
it is recommended that you add support for this option. This is not, however, a
breaking change - if the option is not supported, the library will continue to
use the old behavior.

Refactor of PSK credential handling
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

``avs_net_psk_info_t`` structure has been changed to use new types based on
``avs_crypto_security_info_union_t`` instead of raw buffers. This change also
affects ``avs_net_security_info_t`` structure which contains the former.

* **Old API:**
  ::

      /**
       * A PSK/identity pair with borrowed pointers. avs_commons will never attempt
       * to modify these values.
       */
      typedef struct {
          const void *psk;
          size_t psk_size;
          const void *identity;
          size_t identity_size;
      } avs_net_psk_info_t;

      // ...

      typedef struct {
          avs_net_security_mode_t mode;
          union {
              avs_net_psk_info_t psk;
              avs_net_certificate_info_t cert;
          } data;
      } avs_net_security_info_t;

      avs_net_security_info_t avs_net_security_info_from_psk(avs_net_psk_info_t psk);

* **New API:**

  .. snippet-source:: deps/avs_commons/include_public/avsystem/commons/avs_crypto_psk.h

      typedef struct {
          avs_crypto_security_info_union_t desc;
      } avs_crypto_psk_identity_info_t;

      // ...

      avs_crypto_psk_identity_info_t
      avs_crypto_psk_identity_info_from_buffer(const void *buffer,
                                               size_t buffer_size);

      // ...

      typedef struct {
          avs_crypto_security_info_union_t desc;
      } avs_crypto_psk_key_info_t;

      // ...

      avs_crypto_psk_key_info_t
      avs_crypto_psk_key_info_from_buffer(const void *buffer, size_t buffer_size);

  .. snippet-source:: deps/avs_commons/include_public/avsystem/commons/avs_socket.h

      /**
       * A PSK/identity pair. avs_commons will never attempt to modify these values.
       */
      typedef struct {
          avs_crypto_psk_key_info_t key;
          avs_crypto_psk_identity_info_t identity;
      } avs_net_psk_info_t;

      // ...

      typedef struct {
          avs_net_security_mode_t mode;
          union {
              avs_net_psk_info_t psk;
              avs_net_certificate_info_t cert;
          } data;
      } avs_net_security_info_t;

      avs_net_security_info_t
      avs_net_security_info_from_psk(avs_net_psk_info_t psk);

This change is breaking for code that accesses the ``data.psk`` field
of ``avs_net_security_info_t`` directly.

Refactor of avs_net_validate_ip_address() and avs_net_local_address_for_target_host()
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

``avs_net_validate_ip_address()`` is now no longer used by Anjay or
``avs_commons``. It was previously necessary to implement it as part of the
socket implementation. This is no longer required. For compatibility, the
function has been reimplemented as a ``static inline`` function that wraps
``avs_net_addrinfo_*()`` APIs. Please remove your version of
``avs_net_validate_ip_address()`` from your socket implementation if you have
one, as having two alternative variants may lead to conflicts.

Since Anjay 2.9 and ``avs_commons`` 4.6,
``avs_net_local_address_for_target_host()`` underwent a similar refactor. It was
previously a function to be optionally implemented as part of the socket
implementation, but now it is a ``static inline`` function that wraps
``avs_net_socket_*()`` APIs. Please remove your version of
``avs_net_local_address_for_target_host()`` from your socket implementation if
you have one, as having two alternative variants may lead to conflicts.

Reorganization of HSM support
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. note::

    Low-level HSM support is available in open-source ``avs_commons``, but
    integration of these features with Anjay is only available as a commercial
    feature.

Coupling of the Hardware Security Module support in ``avs_commons`` has been
loosened, making it possible to replace the reference implementation based on
``libp11`` with a custom one.

* New CMake configuration flag ``WITH_AVS_CRYPTO_PKI_ENGINE``, and its
  corresponding configuration header macro
  ``AVS_COMMONS_WITH_AVS_CRYPTO_PKI_ENGINE`` have been added.
* Enabling the aforementioned flag is now a dependency for enabling
  ``WITH_OPENSSL_PKCS11_ENGINE`` (CMake) /
  ``AVS_COMMONS_WITH_OPENSSL_PKCS11_ENGINE`` (header)

Refactor of time handling in avs_sched and avs_coap
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

It is now enforced more strictly that time-based events shall happen when the
clock reaches *at least* the expected value. Previously, the tasks scheduled via
avs_sched were executed only when the clock reached a value *later* than the
scheduled job execution time.

This change will have no impact on your code if your platform has enough clock
resolution so that two subsequent calls to ``avs_time_real_now()`` or
``avs_time_monotonic_now()`` will *always* return different values. As a rule of
thumb, this should be the case if your clock has a resolution no worse than
about 1-2 orders of magnitude smaller than the CPU clock. For example, for a
100 MHz CPU, a clock resolution of around 100-1000 ns (i.e., 1-10 MHz) should be
sufficient, depending on the specific architecture.

If your clock has a lower resolution, you may observe the following changes:

* ``anjay_sched_run()`` is now properly guaranteed to execute at least one job
  if the time reported by ``anjay_sched_time_to_next()`` passed. Previously this
  could require waiting for another change of the numerical value of the clock,
  which could cause undesirable active waiting in the event loop. This is the
  motivating factor in introducing these changes.
* Jobs scheduled using ``AVS_SCHED_NOW()`` during an execution of
  ``anjay_sched_run()`` before the numerical value of the clock changes, *will*
  be executed during the same run. The previous behavior more strictly enforced
  the policy to not execute such jobs in the same run.

If you are scheduling custom jobs through the avs_sched module, you may want or
need to modify their logic accordingly to accommodate for these changes. In most
typical use cases, no changes are expected to be necessary.

Removal of avs_unit_memstream
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

``avs_unit_memstream`` was a specific implementation of ``avs_stream_t`` within
the avs_unit module that implemented a simple FIFO stream in a fixed-size memory
area.

This feature has been removed. Instead, you can use an
``avs_stream_inbuf``/``avs_stream_outbuf`` pair, or an ``avs_stream_membuf``
object.

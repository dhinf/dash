/**
 * \file dash/dart/base/locality.h
 */
#ifndef DART__BASE__LOCALITY_H__
#define DART__BASE__LOCALITY_H__

#include <dash/dart/if/dart_types.h>

/* ======================================================================== *
 * Init / Finalize                                                          *
 * ======================================================================== */

dart_ret_t dart__base__locality__init();

dart_ret_t dart__base__locality__finalize();

/* ======================================================================== *
 * Create / Delete                                                          *
 * ======================================================================== */

dart_ret_t dart__base__locality__create(
    dart_team_t team);

dart_ret_t dart__base__locality__delete(
    dart_team_t team);

/* ======================================================================== *
 * Domain Locality                                                          *
 * ======================================================================== */

dart_ret_t dart__base__locality__domain(
    dart_team_t               team,
    const char              * domain_tag,
    dart_domain_locality_t ** locality);

dart_ret_t dart__base__locality__scope_domains(
    dart_team_t               team,
    const char              * domain_tag,
    dart_locality_scope_t     scope,
    int                     * num_domains_out,
    char                  *** domain_tags_out);

/* ======================================================================== *
 * Unit Locality                                                            *
 * ======================================================================== */

dart_ret_t dart__base__locality__unit(
    dart_team_t               team,
    dart_unit_t               unit,
    dart_unit_locality_t   ** locality);

#endif /* DART__BASE__LOCALITY_H__ */
/*****************************************************************************
 *
 *  subgrid.c
 *
 *  Routines for point-like particles.
 *
 *  See Nash et al. (2007).
 *
 *  $Id$
 *
 *  Edinburgh Soft Matter and Statistical Phyiscs Group and
 *  Edinburgh Parallel Computing Centre
 *
 *  (c) 2010-2017 The University of Edinburgh
 *
 *  Contributing authors:
 *  Kevin Stratford (kevin@epcc.ed.ac.uk)
 *
 *****************************************************************************/

#include <assert.h>
#include <math.h>
#include <stddef.h>
#include "util.h"
#include "noise.h"

#include "pe.h"
#include "coords.h"
#include "physics.h"
#include "colloids_s.h"
#include "colloid_sums.h"
#include "util.h"
#include "subgrid.h"

static double d_peskin(double);
static int subgrid_interpolation(colloids_info_t * cinfo, hydro_t * hydro);
static double drange_ = 1.0; /* Max. range of interpolation - 1 */
static int subgrid_on_ = 0;  /* Subgrid particle flag */

/*****************************************************************************
 *
 *  subgrid_force_from_particles()
 *
 *  For each particle, accumulate the force on the relevant surrounding
 *  lattice nodes. Only nodes in the local domain are involved.
 *
 *****************************************************************************/

int subgrid_force_from_particles(colloids_info_t * cinfo, hydro_t * hydro) {

  int ic, jc, kc;
  int i, j, k, i_min, i_max, j_min, j_max, k_min, k_max;
  int index;
  int nlocal[3], offset[3];
  int ncell[3];

  double r[3], r0[3], force[3], g[3];
  double dr;
  colloid_t * p_colloid;

  physics_t * phys = NULL;

  assert(cinfo);
  assert(hydro);

  cs_nlocal(cinfo->cs, nlocal);
  cs_nlocal_offset(cinfo->cs, offset);
  colloids_info_ncell(cinfo, ncell);

  physics_ref(&phys);
  physics_fgrav(phys, g);

  /* Loop through all cells (including the halo cells) */

  for (ic = 0; ic <= ncell[X] + 1; ic++) {
    for (jc = 0; jc <= ncell[Y] + 1; jc++) {
      for (kc = 0; kc <= ncell[Z] + 1; kc++) {

	colloids_info_cell_list_head(cinfo, ic, jc, kc, &p_colloid);

	while (p_colloid != NULL) {

          /* Need to translate the colloid position to "local"
           * coordinates, so that the correct range of lattice
           * nodes is found */

          r0[X] = p_colloid->s.r[X] - 1.0*offset[X];
          r0[Y] = p_colloid->s.r[Y] - 1.0*offset[Y];
          r0[Z] = p_colloid->s.r[Z] - 1.0*offset[Z];

	  /* Work out which local lattice sites are involved
	   * and loop around */

          i_min = imax(1,         (int) floor(r0[X] - drange_));
          i_max = imin(nlocal[X], (int) ceil (r0[X] + drange_));
          j_min = imax(1,         (int) floor(r0[Y] - drange_));
          j_max = imin(nlocal[Y], (int) ceil (r0[Y] + drange_));
          k_min = imax(1,         (int) floor(r0[Z] - drange_));
          k_max = imin(nlocal[Z], (int) ceil (r0[Z] + drange_));

          for (i = i_min; i <= i_max; i++) {
            for (j = j_min; j <= j_max; j++) {
	      for (k = k_min; k <= k_max; k++) {

		index = cs_index(cinfo->cs, i, j, k);

                /* Separation between r0 and the coordinate position of
		 * this site */

		r[X] = r0[X] - 1.0*i;
		r[Y] = r0[Y] - 1.0*j;
		r[Z] = r0[Z] - 1.0*k;

		dr = d_peskin(r[X])*d_peskin(r[Y])*d_peskin(r[Z]);

	        force[X] = p_colloid->force[X]*dr;
	        force[Y] = p_colloid->force[Y]*dr;
	        force[Z] = p_colloid->force[Z]*dr;
		hydro_f_local_add(hydro, index, force);
	      }
	    }
	  }

	  /* Next colloid */
	  p_colloid = p_colloid->next;
	}

	/* Next cell */
      }
    }
  }

  return 0;
}

/*****************************************************************************
 *
 *  subgrid_update
 *
 *  This function is responsible for update of position for
 *  sub-gridscale particles. It takes the place of BBL for
 *  fully resolved particles.
 *
 *****************************************************************************/

int subgrid_update(colloids_info_t * cinfo, hydro_t * hydro,pe_t * pe,noise_t * noise) {

  int ia;
  int ic, jc, kc;
  int ncell[3];
  double drag, reta;
  double g[3];
  double eta;
  PI_DOUBLE(pi);
  colloid_t * p_colloid;
  physics_t * phys = NULL;
  double ran[2];  /* Random numbers for fluctuation dissipation correction */
  double frand; /* Random correction */
  double kt;

  assert(cinfo);
  assert(hydro);

  colloids_info_ncell(cinfo, ncell);

  subgrid_interpolation(cinfo, hydro);
  colloid_sums_halo(cinfo, COLLOID_SUM_DYNAMICS);
  colloid_sums_halo(cinfo, COLLOID_SUM_SUBGRID);

  /* Loop through all cells (including the halo cells) */

  physics_ref(&phys);
  physics_eta_shear(phys, &eta);
  physics_fgrav(phys, g);
  physics_kt(phys, &kt);
  reta = 1.0/(6.0*pi*eta);

  for (ic = 0; ic <= ncell[X] + 1; ic++) {
    for (jc = 0; jc <= ncell[Y] + 1; jc++) {
      for (kc = 0; kc <= ncell[Z] + 1; kc++) {

	colloids_info_cell_list_head(cinfo, ic, jc, kc, &p_colloid);

	while (p_colloid != NULL) {

	  drag = reta*(1.0/p_colloid->s.a0 - 1.0/p_colloid->s.al);

	  for (ia = 0; ia < 3; ia++) {
            if (noise->on[0]) {
                while(1) {
	            util_ranlcg_reap_gaussian(&p_colloid->s.rng, ran);
                    if(fabs(ran[0])<3.0) {frand=sqrt(2.0*kt*drag)*ran[0]; break;}
                    if(fabs(ran[1])<3.0) {frand=sqrt(2.0*kt*drag)*ran[1]; break;}
                }
                /* To keep the random correction not too large, smaller than 3 sigma. Otherwise, large thermal fluctuation will blow up the velocity. */
	        p_colloid->s.v[ia] = p_colloid->fc0[ia] + drag*p_colloid->force[ia]+frand;
            }
            else 
	        p_colloid->s.v[ia] = p_colloid->fc0[ia] + drag*p_colloid->force[ia];
	    p_colloid->s.dr[ia] = p_colloid->s.v[ia];
	  }

	  p_colloid = p_colloid->next;
	}

      }
    }
  }

  return 0;
}

/*****************************************************************************
 *
 *  subgrid_interpolation
 *
 *  Interpolate (delta function method) the lattice velocity field
 *  to the position of the particles.
 *
 *****************************************************************************/

static int subgrid_interpolation(colloids_info_t * cinfo, hydro_t * hydro) {

  int ic, jc, kc;
  int i, j, k, i_min, i_max, j_min, j_max, k_min, k_max;
  int index;
  int nlocal[3], offset[3];
  int ncell[3];

  double r0[3], r[3], u[3];
  double dr;
  colloid_t * p_colloid;

  assert(cinfo);
  assert(hydro);

  cs_nlocal(cinfo->cs, nlocal);
  cs_nlocal_offset(cinfo->cs, offset);
  colloids_info_ncell(cinfo, ncell);

  /* Loop through all cells (including the halo cells) and set
   * the velocity at each particle to zero for this step. */

  for (ic = 0; ic <= ncell[X] + 1; ic++) {
    for (jc = 0; jc <= ncell[Y] + 1; jc++) {
      for (kc = 0; kc <= ncell[Z] + 1; kc++) {

	colloids_info_cell_list_head(cinfo, ic, jc, kc, &p_colloid);

	while (p_colloid != NULL) {
	  p_colloid->fc0[X] = 0.0;
	  p_colloid->fc0[Y] = 0.0;
	  p_colloid->fc0[Z] = 0.0;
	  p_colloid = p_colloid->next;
	}
      }
    }
  }

  /* And add up the contributions to the velocity from the lattice. */

  for (ic = 0; ic <= ncell[X] + 1; ic++) {
    for (jc = 0; jc <= ncell[Y] + 1; jc++) {
      for (kc = 0; kc <= ncell[Z] + 1; kc++) {

	colloids_info_cell_list_head(cinfo, ic, jc, kc, &p_colloid);

	while (p_colloid != NULL) {

          /* Need to translate the colloid position to "local"
           * coordinates, so that the correct range of lattice
           * nodes is found */

          r0[X] = p_colloid->s.r[X] - 1.0*offset[X];
          r0[Y] = p_colloid->s.r[Y] - 1.0*offset[Y];
          r0[Z] = p_colloid->s.r[Z] - 1.0*offset[Z];

	  /* Work out which local lattice sites are involved
	   * and loop around */

          i_min = imax(1,         (int) floor(r0[X] - drange_));
          i_max = imin(nlocal[X], (int) ceil (r0[X] + drange_));
          j_min = imax(1,         (int) floor(r0[Y] - drange_));
          j_max = imin(nlocal[Y], (int) ceil (r0[Y] + drange_));
          k_min = imax(1,         (int) floor(r0[Z] - drange_));
          k_max = imin(nlocal[Z], (int) ceil (r0[Z] + drange_));

          for (i = i_min; i <= i_max; i++) {
            for (j = j_min; j <= j_max; j++) {
	      for (k = k_min; k <= k_max; k++) {

		index = cs_index(cinfo->cs, i, j, k);

                /* Separation between r0 and the coordinate position of
		 * this site */

		r[X] = r0[X] - 1.0*i;
		r[Y] = r0[Y] - 1.0*j;
		r[Z] = r0[Z] - 1.0*k;

		dr = d_peskin(r[X])*d_peskin(r[Y])*d_peskin(r[Z]);
		hydro_u(hydro, index, u);

		p_colloid->fc0[X] += u[X]*dr;
		p_colloid->fc0[Y] += u[Y]*dr;
		p_colloid->fc0[Z] += u[Z]*dr;
	      }
	    }
	  }

	  /* Next colloid */
	  p_colloid = p_colloid->next;
	}

	/* Next cell */
      }
    }
  }

  return 0;
}

/*****************************************************************************
 *
 *  d_peskin
 *
 *  Approximation to \delta(r) according to Peskin.
 *
 *****************************************************************************/

static double d_peskin(double r) {

  double rmod;
  double delta = 0.0;

  rmod = fabs(r);

  if (rmod <= 1.0) {
    delta = 0.125*(3.0 - 2.0*rmod + sqrt(1.0 + 4.0*rmod - 4.0*rmod*rmod));
  }
  else if (rmod <= 2.0) {
    delta = 0.125*(5.0 - 2.0*rmod - sqrt(-7.0 + 12.0*rmod  - 4.0*rmod*rmod));
  }

  return delta;
}

/*****************************************************************************
 *
 *  subgrid_on_set
 *
 *  Set the flag to 'on'.
 *
 *****************************************************************************/

int subgrid_on_set(void) {

  subgrid_on_ = 1;
  return 0;
}

/*****************************************************************************
 *
 *  subgrid_on
 *
 *****************************************************************************/

int subgrid_on(int * flag) {

  assert(flag);

  *flag = subgrid_on_;
  return 0;
}

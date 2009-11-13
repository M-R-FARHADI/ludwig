/*****************************************************************************
 *
 *  test.c
 *
 *  Statistics on fluid/particle conservation laws.
 *  Single fluid and binary fluid.
 *
 *  $Id: test.c,v 1.19 2009-08-20 16:30:04 kevin Exp $
 *
 *  Edinburgh Soft Matter and Statistical Physics Group and
 *  Edinburgh Parallel Computing Centre
 *
 *  Kevin Stratford (kevin@epcc.ed.ac.uk)
 *  (c) 2007 The University of Edinburgh
 *
 *****************************************************************************/

#include <math.h>

#include "pe.h"
#include "coords.h"
#include "colloids.h"
#include "site_map.h"
#include "model.h"
#include "physics.h"
#include "bbl.h"
#include "phi.h"
#include "leesedwards.h"
#include "test.h"

extern Site * site;

/*****************************************************************************
 *
 *  TEST_statistics
 *
 *  Report the minimum, maximum, total, mean and variance of rho, phi
 *  over the whole system.
 *
 *  In the presence of solid particles, the current deficit in order
 *  parameter is taken into account in working out the total phi.
 *
 *****************************************************************************/

void TEST_statistics() {

  double rhosum, phisum;
  double phibar, rhobar;
  double phivar, rhovar;
  double rho;
  double rfluid;
  double partsum[3], partmin[2], partmax[2];

  int     i, j, k, p, index;
  int     N[3];

#ifdef _MPI_
  double g_sum[3], g_min[2], g_max[2];
#endif

  get_N_local(N);

  partsum[0] =  0.0;
  partsum[1] =  bbl_order_parameter_deficit();
  partsum[2] =  0.0;     /* volume of fluid */

  partmin[0] =  2.0;     /* rho_min */
  partmax[0] =  0.0;     /* rho_max */
  rhovar     =  0.0;

  partmin[1] = +1.0;     /* phi_min */
  partmax[1] = -1.0;     /* phi_max */
  phivar     =  0.0;


  /* Accumulate the sums, minima, and maxima */

  for (i = 1; i <= N[X]; i++) {
    for (j = 1; j <= N[Y]; j++) {
      for (k = 1; k <= N[Z]; k++) {

	if (site_map_get_status(i, j, k) != FLUID) continue;
	index = get_site_index(i, j, k);

	rho = site[index].f[0];

	for (p = 1; p < NVEL; p++) {
	  rho += site[index].f[p];
	}

	if (rho < partmin[0]) partmin[0] = rho;
	if (rho > partmax[0]) partmax[0] = rho;
	partsum[0] += rho;

	partsum[2] += 1.0;
      }
    }
  }

#ifdef _MPI_
  MPI_Allreduce(partsum, g_sum, 3, MPI_DOUBLE, MPI_SUM, cart_comm());

  partsum[0] = g_sum[0];
  partsum[1] = g_sum[1];
  partsum[2] = g_sum[2];
#endif

  rhosum = partsum[0];
  phisum = partsum[1];
  rfluid = 1.0/partsum[2];

  rhobar = rhosum*rfluid;
  phibar = phisum*rfluid;


  /* Have to go round again to get the variances... */

  partsum[0] = 0.0;
  partsum[1] = 0.0;

  for (i = 1; i <= N[X]; i++) {
    for (j = 1; j <= N[Y]; j++) {
      for (k = 1; k <= N[Z]; k++) {

	if (site_map_get_status(i, j, k) != FLUID) continue;
	index = get_site_index(i, j, k);

	rho = site[index].f[0];

	for (p = 1; p < NVEL; p++) {
	  rho += site[index].f[p];
	}

      }
    }
  }

#ifdef _MPI_

  MPI_Reduce(partsum, g_sum, 2, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
  MPI_Reduce(partmin, g_min, 2, MPI_DOUBLE, MPI_MIN, 0, MPI_COMM_WORLD);
  MPI_Reduce(partmax, g_max, 2, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

  partsum[0] = g_sum[0];
  partsum[1] = g_sum[1];

  partmin[0] = g_min[0];
  partmin[1] = g_min[1];
  partmax[0] = g_max[0];
  partmax[1] = g_max[1];
#endif

  rhovar = partsum[0]*rfluid;
  phivar = partsum[1]*rfluid;

  info("\nTEST_statistics [total, mean, variance, min, max]\n");
  info("[rho][%.8g, %.8g, %.8g, %.8g, %.8g]\n", rhosum, rhobar, rhovar,
       partmin[0], partmax[0]);

  return;
}


/*****************************************************************************
 *
 *  TEST_momentum
 *
 *  Compute the total system momentum (fluid+colloids).
 *
 *****************************************************************************/

void TEST_momentum() {

  int       ic, jc, kc, index;
  int       N[3];
  int       p;

  double     gx, gy, gz, cx, cy, cz;
  double     mass;
  double   * f;

  Colloid * p_colloid;


  /* Work out the fluid momentum (gx, gy, gz) */
  gx = gy = gz = 0.0;

  get_N_local(N);

  for (ic = 1; ic <= N[X]; ic++) {
    for (jc = 1; jc <= N[Y]; jc++) {
      for (kc = 1; kc <= N[Z]; kc++) {

	if (site_map_get_status(ic, jc, kc) != FLUID) continue;
	index = get_site_index(ic, jc, kc);

	f = site[index].f;

	for (p = 1; p < NVEL; p++) {
	  gx += cv[p][0]*f[p];
	  gy += cv[p][1]*f[p];
	  gz += cv[p][2]*f[p];
	}
      }
    }
  }

  /* Work out the net colloid momemtum (cx, cy, cz) */
  cx = cy = cz = 0.0;

  for (ic = 1; ic <= Ncell(X); ic++) {
    for (jc = 1; jc <= Ncell(Y); jc++) {
      for (kc = 1; kc <= Ncell(Z); kc++) {

	p_colloid = CELL_get_head_of_list(ic, jc, kc);

	while (p_colloid) {

	  mass = 4.0*PI*pow(p_colloid->a0, 3)/3.0;

	  cx += mass*p_colloid->v.x;
	  cy += mass*p_colloid->v.y;
	  cz += mass*p_colloid->v.z;

	  /* Next colloid */
	  p_colloid = p_colloid->next;
	}

	/* Next cell */
      }
    }
  }

#ifdef _MPI_
  {
    double   partsum[6], g_sum[6];
    partsum[0] = gx;
    partsum[1] = gy;
    partsum[2] = gz;
    partsum[3] = cx;
    partsum[4] = cy;
    partsum[5] = cz;

    MPI_Reduce(partsum, g_sum, 6, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);

    gx = g_sum[0];
    gy = g_sum[1];
    gz = g_sum[2];
    cx = g_sum[3];
    cy = g_sum[4];
    cz = g_sum[5];
  }
#endif

  info("\nTEST_momentum [x, y, z]\n");
  info("[total][%g, %g, %g]\n", gx + cx,  gy + cy,  gz + cz);
  info("[fluid][%g, %g, %g]\n", gx,       gy,       gz);

  return;
}


/*****************************************************************************
 *
 *  TEST_fluid_temperature
 *
 *  This computes and reports on the statistics of the fluid
 *  temperature when fluctauations are present.
 *
 *  For the temperature, a contribution to a long-term mean is
 *  accumulated each time the fuction is called, and the time
 *  mean reported (for what it's worth).
 *
 *****************************************************************************/

void TEST_fluid_temperature() {

  double   uvar, uxvar, uyvar, uzvar;
  double   rhovar, chi2var;
  double   rfluid;
  int      i, j, k, index, p;
  int      N[3];
  double   rho, ux, uy, uz, chi2;
  double   *f;

  get_N_local(N);

  uvar    = 0.0;   /* Total u variance */
  uxvar   = 0.0;   /* u_x variance */
  uyvar   = 0.0;   /* u_y variance */
  uzvar   = 0.0;   /* u_z variance */
  rhovar  = 0.0;   /* (1 - rho)^2  */
  chi2var = 0.0;   /* chi2 ghost mode variance */
  rfluid  = 0.0;   /* Fluid volume */

  /* Single loop: variances are computed assuming the appropriate
   * means are well-behaved (i.e., mean of u is zero, mean of rho
   * is 1) */ 

  for (i = 1; i <= N[X]; i++) {
    for (j = 1; j <= N[Y]; j++) {
      for (k = 1; k <= N[Z]; k++) {

	if (site_map_get_status(i, j, k) == FLUID) {
	  index = get_site_index(i, j, k);
	  f = site[index].f;

	  rho  = f[0];
	  ux   = 0.0;
	  uy   = 0.0;
	  uz   = 0.0;
	  chi2 = 0.0;        /* Ghost mode temperature */

	  for (p = 1; p < NVEL; p++) {
	    rho  += f[p];
	    ux   += f[p]*cv[p][0];
	    uy   += f[p]*cv[p][1];
	    uz   += f[p]*cv[p][2];
	    chi2 += f[p]*cv[p][0]*cv[p][1]*cv[p][2];
	  }

	  ux = ux/rho;
	  uy = uy/rho;
	  uz = uz/rho;

	  uvar    += ux*ux + uy*uy + uz*uz;
	  uxvar   += ux*ux;
	  uyvar   += uy*uy;
	  uzvar   += uz*uz;
	  rhovar  += (1.0 - rho)*(1.0 - rho);
	  chi2var += 9.0*chi2*chi2;
	  rfluid  += 1.0;
	}
      }
    }
  }

#ifdef _MPI_
  {
    double   partsum[7], g_sum[7];
    partsum[0] = uvar;
    partsum[1] = uxvar;
    partsum[2] = uyvar;
    partsum[3] = uzvar;
    partsum[4] = rhovar;
    partsum[5] = chi2var;
    partsum[6] = rfluid;

    MPI_Reduce(partsum, g_sum, 7, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);

    uvar    = g_sum[0];
    uxvar   = g_sum[1];
    uyvar   = g_sum[2];
    uzvar   = g_sum[3];
    rhovar  = g_sum[4];
    chi2var = g_sum[5];
    rfluid  = g_sum[6];
  }
#endif

  rfluid  = 1.0/rfluid;

  uvar    = uvar*rfluid;
  uxvar   = uxvar*rfluid;
  uyvar   = uyvar*rfluid;
  uzvar   = uzvar*rfluid;
  rhovar  = rhovar*rfluid;
  chi2var = chi2var*rfluid;

  info("TEST_fluid_temperature:\n");
  info("  <v_x^2> = %g\n", uxvar);
  info("  <v_y^2> = %g\n", uyvar);
  info("  <v_z^2> = %g\n", uzvar);
  info("   <mv^2> = %g (target: %g)\n", uvar, get_kT()*ND);
  info(" <drho^2> = %g\n", rhovar);
  info("  <ghost> = %g\n", chi2var);

  return;
}
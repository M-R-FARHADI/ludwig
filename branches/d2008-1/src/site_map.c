/*****************************************************************************
 *
 *  site_map.c
 *
 *  Keeps track of the solid/fluid status of the lattice.
 *
 *  $Id: site_map.c,v 1.1.2.10 2008-08-24 15:07:02 kevin Exp $
 *
 *  Edinburgh Soft Matter and Statistical Physics Group and
 *  Edinburgh Parallel Computing Centre
 *
 *  Kevin Stratford (kevin@epcc.ed.ac.uk)
 *  (c) 2007 The University of Edinburgh
 *
 *****************************************************************************/

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "pe.h"
#include "coords.h"
#include "io_harness.h"
#include "site_map.h"

struct io_info_t * io_info_site_map;

static void site_map_init_mpi(void);
static void site_map_init_io(void);
static int site_map_read(FILE *, const int, const int, const int);
static int site_map_write(FILE *, const int, const int, const int);
static int site_map_read_ascii(FILE *, const int, const int, const int);
static int site_map_write_ascii(FILE *, const int, const int, const int);

struct site_info_t {
  char status;       /* Solid / fluid etc status flag */
  double h;          /* Wetting free energy parameter H */
                     /* Wetting free energy parameter C = 0 always */
};

static struct site_info_t * site_map;
static int initialised_ = 0;
static int io_type_ = 0;                 /* 0 = status only; 1 = status + h */
static MPI_Datatype mpi_site_t_;
static MPI_Datatype mpi_xy_t_;
static MPI_Datatype mpi_xz_t_;
static MPI_Datatype mpi_yz_t_;

/*****************************************************************************
 *
 *  site_map_init
 *
 *  Allocate, and read site information from file if required.
 *
 *****************************************************************************/

void site_map_init() {

  int nlocal[3];
  int n;

  get_N_local(nlocal);

  n = (nlocal[X] + 2*nhalo_)*(nlocal[Y] + 2*nhalo_)*(nlocal[Z] + 2*nhalo_);

  site_map = (struct site_info_t *) malloc(n*sizeof(struct site_info_t));
  if (site_map == NULL) fatal("malloc(site_map) failed\n");

  site_map_init_io();
  site_map_init_mpi();

  initialised_ = 1;
  site_map_set_all(FLUID);

  return;
}

/*****************************************************************************
 *
 *  site_map_init_io
 *
 *  The intention here is that the site map should will consist of
 *  one processor-decomposition-independent file regardless
 *  of I/O grid for other lattice quantities.
 *
 *****************************************************************************/

static void site_map_init_io() {

  int grid[3] = {1, 1, 1};

  io_info_site_map = io_info_create_with_grid(grid);

  io_info_set_name(io_info_site_map, "Site map information");
  io_info_set_read_binary(io_info_site_map, site_map_read);
  io_info_set_write_binary(io_info_site_map, site_map_write);
  io_info_set_read_ascii(io_info_site_map, site_map_read_ascii);
  io_info_set_write_ascii(io_info_site_map, site_map_write_ascii);

  /* Default: read status only from single binary file */
  io_type_ = 0;
  io_info_set_format_binary(io_info_site_map);
  io_info_set_bytesize(io_info_site_map, sizeof(char));
  io_info_set_processor_independent(io_info_site_map);

  return;
}

/*****************************************************************************
 *
 *  site_map_init_mpi
 *
 *****************************************************************************/

static void site_map_init_mpi() {

  int nlocal[3];
  int nh[3];

  get_N_local(nlocal);

  nh[X] = nlocal[X] + 2*nhalo_;
  nh[Y] = nlocal[Y] + 2*nhalo_;
  nh[Z] = nlocal[Z] + 2*nhalo_;

  MPI_Type_contiguous(sizeof(struct site_info_t), MPI_BYTE, &mpi_site_t_);
  MPI_Type_commit(&mpi_site_t_);

  /* YZ planes in the X direction */
  MPI_Type_vector(1, nh[Y]*nh[Z]*nhalo_, 1, mpi_site_t_, &mpi_yz_t_);
  MPI_Type_commit(&mpi_yz_t_);

  /* XZ planes in the Y direction */
  MPI_Type_vector(nh[X], nh[Z]*nhalo_, nh[Y]*nh[Z], mpi_site_t_, &mpi_xz_t_);
  MPI_Type_commit(&mpi_xz_t_);

  /* XY planes in Z direction */
  MPI_Type_vector(nh[X]*nh[Y], nhalo_, nh[Z], mpi_site_t_, &mpi_xy_t_);
  MPI_Type_commit(&mpi_xy_t_);

  return;
}

/*****************************************************************************
 *
 *  site_map_finish
 *
 *  Clean up.
 *
 *****************************************************************************/

void site_map_finish() {

  assert(initialised_);

  free(site_map);
  io_info_destroy(io_info_site_map);

  MPI_Type_free(&mpi_site_t_);
  MPI_Type_free(&mpi_xy_t_);
  MPI_Type_free(&mpi_xz_t_);
  MPI_Type_free(&mpi_yz_t_);

  initialised_ = 0;

  return;
}

/*****************************************************************************
 *
 *  site_map_set_all
 *
 *  Set all sites to the given status.
 *  The wetting parameters are set to zero.
 *
 *****************************************************************************/

void site_map_set_all(char status) {

  int nlocal[3];
  int index, ic, jc, kc;

  assert(initialised_);
  assert(status >= FLUID && status <= COLLOID);

  get_N_local(nlocal);

  for (ic = 1 - nhalo_; ic <= nlocal[X] + nhalo_; ic++) {
    for (jc = 1 - nhalo_; jc <= nlocal[Y] + nhalo_; jc++) {
      for (kc = 1 - nhalo_; kc <= nlocal[Z] + nhalo_; kc++) {
	index = get_site_index(ic, jc, kc);
	site_map[index].status = status;
	site_map[index].h = 0.0;
      }
    }
  }

  return;
}

/*****************************************************************************
 *
 *  site_map_get_status
 *
 *  Return the site status at local index (ic, jc, kc).
 *
 *****************************************************************************/

char site_map_get_status(int ic, int jc, int kc) {

  int index;

  assert(initialised_);

  index = get_site_index(ic, jc, kc);

  return site_map[index].status;
}

/*****************************************************************************
 *
 *  site_map_get_status_index
 *
 *  Return the site status at index.
 *
 *****************************************************************************/

char site_map_get_status_index(int index) {

  assert(initialised_);
  return site_map[index].status;
}

/*****************************************************************************
 *
 *  site_map_set_status
 *
 *  Set the status at (ic, jc, kc).
 *
 *****************************************************************************/

void site_map_set_status(int ic, int jc, int kc, char status) {

  int index;

  assert(initialised_);
  assert(status >= FLUID && status <= COLLOID);

  index = get_site_index(ic, jc, kc);
  site_map[index].status = status;

  return;
}

/*****************************************************************************
 *
 *  site_map_get_C
 *
 *  Wetting paramater C cf. Desplat et al. 2001.
 *  Always zero at the moment.
 *
 *****************************************************************************/

double site_map_get_C(int index) {

  assert(initialised_);
  return 0.0;
}

/*****************************************************************************
 *
 *  site_map_get_H
 *
 *  Wetting parameter H cf. Desplat et al. 2001.
 *
 *****************************************************************************/

double site_map_get_H(int index) {

  assert(initialised_);
  return site_map[index].h;
}

/*****************************************************************************
 *
 *  site_map_volume
 *
 *  What is the current fluid volume? Call this with status = FLUID.
 *
 *  The value is computed as a double.
 *
 *****************************************************************************/

double site_map_volume(char status) {

  double  v_local, v_total;
  int     nlocal[3];
  int     index, ic, jc, kc;

  assert(initialised_);
  assert(status >= FLUID && status <= COLLOID);

  get_N_local(nlocal);
  v_local = 0.0;

  /* Look for fluid nodes (not halo) */

  for (ic = 1; ic <= nlocal[X]; ic++) {
    for (jc = 1; jc <= nlocal[Y]; jc++) {
      for (kc = 1; kc <= nlocal[Z]; kc++) {

	index = get_site_index(ic, jc, kc);
	if (site_map[index].status == status) v_local += 1.0;
      }
    }
  }

  /* All processes get the total */

  MPI_Allreduce(&v_local, &v_total, 1, MPI_DOUBLE, MPI_SUM, cart_comm());

  return v_total;
}

/*****************************************************************************
 *
 *  site_map_halo
 *
 *  Swap the site_map values.
 *
 *****************************************************************************/

void site_map_halo() {

  int ic, jc, kc, ihalo, ireal, nh;
  int nlocal[3];
  int back, forw;
  const int btag = 151;
  const int ftag = 152;
  MPI_Comm comm = cart_comm();
  MPI_Request request[4];
  MPI_Status status[4];

  assert(initialised_);
  get_N_local(nlocal);

  /* YZ planes in X direction */

  if (cart_size(X) == 1) {
    for (nh = 0; nh < nhalo_; nh++) {
      for (jc = 1; jc <= nlocal[Y]; jc++) {
	for (kc = 1 ; kc <= nlocal[Z]; kc++) {
	  ihalo = get_site_index(0-nh, jc, kc);
	  ireal = get_site_index(nlocal[X]-nh, jc, kc);
	  site_map[ihalo] = site_map[ireal];

	  ihalo = get_site_index(nlocal[X]+1+nh, jc, kc);
	  ireal = get_site_index(1+nh, jc, kc);
	  site_map[ihalo] = site_map[ireal];
	}
      }
    }
  }
  else {

    back = cart_neighb(BACKWARD, X);
    forw = cart_neighb(FORWARD, X);

    ihalo = get_site_index(nlocal[X] + 1, 1-nhalo_, 1-nhalo_);
    MPI_Irecv(site_map + ihalo,  1, mpi_yz_t_, forw, btag, comm, request);
    ihalo = get_site_index(1-nhalo_, 1-nhalo_, 1-nhalo_);
    MPI_Irecv(site_map + ihalo,  1, mpi_yz_t_, back, ftag, comm, request+1);
    ireal = get_site_index(1, 1-nhalo_, 1-nhalo_);
    MPI_Issend(site_map + ireal, 1, mpi_yz_t_, back, btag, comm, request+2);
    ireal = get_site_index(nlocal[X] - nhalo_ + 1, 1-nhalo_, 1-nhalo_);
    MPI_Issend(site_map + ireal, 1, mpi_yz_t_, forw, ftag, comm, request+3);
    MPI_Waitall(4, request, status);
  }

  /* XZ planes in the Y direction */

  if (cart_size(Y) == 1) {
    for (nh = 0; nh < nhalo_; nh++) {
      for (ic = 1-nhalo_; ic <= nlocal[X] + nhalo_; ic++) {
	for (kc = 1; kc <= nlocal[Z]; kc++) {
	  ihalo = get_site_index(ic, 0-nh, kc);
	  ireal = get_site_index(ic, nlocal[Y]-nh, kc);
	  site_map[ihalo] = site_map[ireal];

	  ihalo = get_site_index(ic, nlocal[Y]+1+nh, kc);
	  ireal = get_site_index(ic, 1+nh, kc);
	  site_map[ihalo] = site_map[ireal];
	}
      }
    }
  }
  else {

    back = cart_neighb(BACKWARD, Y);
    forw = cart_neighb(FORWARD, Y);

    ihalo = get_site_index(1-nhalo_, nlocal[Y] + 1, 1-nhalo_);
    MPI_Irecv(site_map + ihalo,  1, mpi_xz_t_, forw, btag, comm, request);
    ihalo = get_site_index(1-nhalo_, 1-nhalo_, 1-nhalo_);
    MPI_Irecv(site_map + ihalo,  1, mpi_xz_t_, back, ftag, comm, request+1);
    ireal = get_site_index(1-nhalo_, 1, 1-nhalo_);
    MPI_Issend(site_map + ireal, 1, mpi_xz_t_, back, btag, comm, request+2);
    ireal = get_site_index(1-nhalo_, nlocal[Y] - nhalo_ + 1, 1-nhalo_);
    MPI_Issend(site_map + ireal, 1, mpi_xz_t_, forw, ftag, comm, request+3);
    MPI_Waitall(4, request, status);
  }

  /* XY planes in the Z direction */

  if (cart_size(Z) == 1) {
    for (nh = 0; nh < nhalo_; nh++) {
      for (ic = 1 - nhalo_; ic <= nlocal[X] + nhalo_; ic++) {
	for (jc = 1 - nhalo_; jc <= nlocal[Y] + nhalo_; jc++) {
	  ihalo = get_site_index(ic, jc, 0-nh);
	  ireal = get_site_index(ic, jc, nlocal[Z]-nh);
	  site_map[ihalo] = site_map[ireal];

	  ihalo = get_site_index(ic, jc, nlocal[Z]+1+nh);
	  ireal = get_site_index(ic, jc,            1+nh);
	  site_map[ihalo] = site_map[ireal];
	}
      }
    }
  }
  else {

    back = cart_neighb(BACKWARD, Z);
    forw = cart_neighb(FORWARD, Z);

    ihalo = get_site_index(1-nhalo_, 1-nhalo_, nlocal[Z] + 1);
    MPI_Irecv(site_map + ihalo,  1, mpi_xy_t_, forw, btag, comm, request);
    ihalo = get_site_index(1-nhalo_, 1-nhalo_, 1-nhalo_);
    MPI_Irecv(site_map + ihalo,  1, mpi_xy_t_, back, ftag, comm, request+1);
    ireal = get_site_index(1-nhalo_, 1-nhalo_, 1);
    MPI_Issend(site_map + ireal, 1, mpi_xy_t_, back, btag, comm, request+2);
    ireal = get_site_index(1-nhalo_, 1-nhalo_, nlocal[Z] - nhalo_ + 1);
    MPI_Issend(site_map + ireal, 1, mpi_xy_t_, forw, ftag, comm, request+3);
    MPI_Waitall(4, request, status);
  }

  return;
}

/*****************************************************************************
 *
 *  site_map_set_io_with_h
 *
 *****************************************************************************/

void site_map_io_status_with_h() {

  assert(initialised_);

  io_info_set_bytesize(io_info_site_map, sizeof(char) + sizeof(double));
  io_type_ = 1;

  return;
}

/*****************************************************************************
 *
 *  site_map_write
 *
 *  Write function.
 *
 *****************************************************************************/

static int site_map_write(FILE * fp, int ic, int jc, int kc) {

  int index, n;

  index = get_site_index(ic, jc, kc);

  n = fputc(site_map[index].status, fp);
  if (n == EOF) fatal("Failed to write site map binary at %d\n", index);

  return 1;
}

/*****************************************************************************
 *
 *  site_map_read
 *
 *****************************************************************************/

static int site_map_read(FILE * fp, int ic, int jc, int kc) {

  int index, n;

  index = get_site_index(ic, jc, kc);

  n = fgetc(fp);
  if (n == EOF) fatal("Failed to read site map binary at %d (%d, %d, %d)\n",
		      index, ic, jc, kc);

  site_map[index].status = n;

  if (io_type_ == 1) {
    n = fread(&(site_map[index].h), sizeof(double), 1, fp);
    if (n != 1) fatal("Failed to read site map H at %d (%d %d %d)\n",
		      index, ic, jc, kc);
  }

  return 1;
}

/*****************************************************************************
 *
 *  site_map_write_ascii
 *
 *****************************************************************************/

static int site_map_write_ascii(FILE * fp, int ic, int jc, int kc) {

  int index, n;
  int tmp;

  index = get_site_index(ic, jc, kc);
  tmp = site_map[index].status;
  n = fprintf(fp, "%d\n", tmp);
  if (n != 1) fatal("Failed to write site map ascii at %d\n", index);

  return 1;
}

/*****************************************************************************
 *
 *  site_map_read_ascii
 *
 *****************************************************************************/

static int site_map_read_ascii(FILE * fp, int ic, int jc, int kc) {

  int index, n;
  int tmp;

  assert(io_type_ == 0);

  index = get_site_index(ic, jc, kc);
  n = fscanf(fp, "%d\n", &tmp);
  if (n != 1) fatal("Failed to read site map ascii at %d (%d, %d, %d)\n",
		    index, ic, jc, kc);

  site_map[index].status = tmp;

  return 1;
}

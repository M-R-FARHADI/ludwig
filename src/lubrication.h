/*****************************************************************************
 *
 *  lubrication.h
 *
 *  Edinburgh Soft Matter and Statistical Physics Group and
 *  Edinburgh Parallel Computing Centre
 *
 *  (c) 2014-2017 The University of Edinburgh
 *
 *  Contributing authors:
 *  Kevin Stratford (kevin@epcc.ed.ac.uk)
 *
 *****************************************************************************/

#ifndef LUDWIG_LUBRICATION_H
#define LUDWIG_LUBRICATION_H

#include "pe.h"
#include "coords.h"
#include "physics.h"
#include "colloids.h"
#include "interaction.h"

typedef enum lubr_ss_enum {
  LUBRICATION_SS_FNORM = 0,   /* Sphere-sphere normal force */
  LUBRICATION_SS_FTANG,       /* Sphere-sphere tangential force */ 
  LUBRICATION_SS_MAX
} lubr_ss_enum_t;
  
typedef struct lubrication_s lubr_t;

int lubrication_create(pe_t * pe, cs_t * cs, lubr_t ** pobj);
int lubrication_free(lubr_t * obj);
int lubrication_register(lubr_t * obj, interact_t * parent);
int lubrication_compute(colloids_info_t * cinfo, void * self);
int lubrication_stats(void * obj, double * stats);
int lubrication_rch_set(lubr_t * obj, lubr_ss_enum_t type, double rch);
int lubrication_rchmax(lubr_t * obj, double * rchmax);
int lubrication_single(lubr_t * obj, double a1, double a2,
		       const double u1[3], const double u2[3],
		       const double r12[3], const double ran[2], double f[3]);

#endif

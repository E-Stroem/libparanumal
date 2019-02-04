/*

The MIT License (MIT)

Copyright (c) 2017 Tim Warburton, Noel Chalmers, Jesse Chan, Ali Karakus

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

*/

#ifndef STOKES_H
#define STOKES_H 1

#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "mpi.h"
#include "mesh2D.h"
#include "mesh3D.h"
#include "parAlmond.hpp"

/* This data structure represents a vector used with the Stokes solver.  The
 * vector is partitioned into blocks corresponding to the components of the
 * velocity and the pressure.
 */
typedef struct {
  dfloat *x;  /* Block matching velocity x-component */
  dfloat *y;  /* Block matching velocity y-component */
  dfloat *z;  /* Block matching velocity z-component */
  dfloat *p;  /* Block matching pressure */
} stokesVec_t;

typedef struct {
  setupAide options;  /* Configuration information. */

  mesh_t *meshV;      /* Velocity mesh */
  mesh_t *meshP;      /* Pressure mesh */

  int NtotalV;        /* Total number of points in the velocity mesh */
  int NtotalP;        /* Total number of points in the pressure mesh */

  stokesVec_t u;      /* Solution */
  stokesVec_t f;      /* Right-hand side */
} stokes_t;

stokes_t *stokesSetup(occa::properties &kernelInfoV, occa::properties &kernelInfoP, setupAide options);
void stokesSolveSetup(stokes_t *stokes, occa::properties &kernelInfoV, occa::properties &kernelInfoP);
void stokesSolve(stokes_t *stokes);
void stokesOperator(stokes_t *stokes, stokesVec_t v, stokesVec_t Av);
void stokesPreconditioner(stokes_t *stokes, stokesVec_t v, stokesVec_t Mv);

#endif /* STOKES_H */
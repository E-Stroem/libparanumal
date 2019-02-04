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

#include "stokes.h"

static void stokesAllocateVec(stokes_t *stokes, stokesVec_t *v);

void stokesSolveSetup(stokes_t *stokes, occa::properties &kernelInfoV, occa::properties &kernelInfoP)
{
  int verbose = stokes->options.compareArgs("VERBOSE", "TRUE") ? 1 : 0;

  stokes->NtotalV = stokes->meshV->Nelements*stokes->meshV->Np;
  stokes->NtotalP = stokes->meshP->Nelements*stokes->meshP->Np;

  stokesAllocateVec(stokes, &stokes->u);
  stokesAllocateVec(stokes, &stokes->f);

  meshParallelGatherScatterSetup(stokes->meshV, stokes->NtotalV, stokes->meshV->globalIds, stokes->meshV->comm, verbose);
  meshParallelGatherScatterSetup(stokes->meshP, stokes->NtotalP, stokes->meshP->globalIds, stokes->meshP->comm, verbose);

  return;
}

static void stokesAllocateVec(stokes_t *stokes, stokesVec_t *v)
{
  v->x = (dfloat*)calloc(stokes->NtotalV, sizeof(dfloat));
  v->y = (dfloat*)calloc(stokes->NtotalV, sizeof(dfloat));
  if (stokes->meshV->dim == 3)
    v->z = (dfloat*)calloc(stokes->NtotalV, sizeof(dfloat));
  else
    v->z = NULL;
  v->p = (dfloat*)calloc(stokes->NtotalP, sizeof(dfloat));

  return;
}
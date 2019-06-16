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

#include "elliptic.h"

void ellipticPreconditionerSetup(elliptic_t *elliptic, ogs_t *ogs, dfloat lambda,
				 occa::properties &kernelInfo){

  mesh2D *mesh = elliptic->mesh;
  precon_t *precon = elliptic->precon;
  setupAide options = elliptic->options;

  if(options.compareArgs("PRECONDITIONER", "FULLALMOND")){ //build full A matrix and pass to Almond
    dlong nnz;
    nonZero_t *A;

    hlong *globalStarts = (hlong*) calloc(mesh->size+1, sizeof(hlong));

    int basisNp = mesh->Np;
    dfloat *basis = NULL;

    if (options.compareArgs("BASIS", "BERN")) basis = mesh->VB;

    if (options.compareArgs("DISCRETIZATION", "IPDG")) {
      ellipticBuildIpdg(elliptic, basisNp, basis, lambda, &A, &nnz, globalStarts);
    } else if (options.compareArgs("DISCRETIZATION", "CONTINUOUS")) {
      ellipticBuildContinuous(elliptic,lambda,&A,&nnz, &(precon->ogs), globalStarts);
    }

    hlong *Rows = (hlong *) calloc(nnz, sizeof(hlong));
    hlong *Cols = (hlong *) calloc(nnz, sizeof(hlong));
    dfloat *Vals = (dfloat*) calloc(nnz,sizeof(dfloat));

    for (dlong n=0;n<nnz;n++) {
      Rows[n] = A[n].row;
      Cols[n] = A[n].col;
      Vals[n] = A[n].val;
    }
    free(A);

    precon->parAlmond = parAlmond::Init(mesh->device, mesh->comm, options);
    parAlmond::AMGSetup(precon->parAlmond,
                       globalStarts,
                       nnz,
                       Rows,
                       Cols,
                       Vals,
                       elliptic->allNeumann,
                       elliptic->allNeumannPenalty);
    free(Rows); free(Cols); free(Vals);

    if (options.compareArgs("VERBOSE", "TRUE"))
      parAlmond::Report(precon->parAlmond);

    if (options.compareArgs("DISCRETIZATION", "CONTINUOUS")) {//tell parAlmond to gather this level
      parAlmond::multigridLevel *baseLevel = precon->parAlmond->levels[0];

      precon->rhsG = (dfloat*) calloc(baseLevel->Ncols,sizeof(dfloat));
      precon->xG   = (dfloat*) calloc(baseLevel->Ncols,sizeof(dfloat));
      precon->o_rhsG = mesh->device.malloc(baseLevel->Ncols*sizeof(dfloat));
      precon->o_xG   = mesh->device.malloc(baseLevel->Ncols*sizeof(dfloat));
    }

  } else if (options.compareArgs("PRECONDITIONER", "MASSMATRIX")){

    precon->o_invMM = mesh->device.malloc(mesh->Np*mesh->Np*sizeof(dfloat), mesh->invMM);

  } else if(options.compareArgs("PRECONDITIONER", "MULTIGRID")){

    ellipticMultiGridSetup(elliptic,precon,lambda);

  } else if(options.compareArgs("PRECONDITIONER", "SEMFEM")) {

    ellipticSEMFEMSetup(elliptic,precon,lambda);

  } else if(options.compareArgs("PRECONDITIONER", "JACOBI")) {

    dfloat *diagA    = (dfloat*) calloc(mesh->Np*mesh->Nelements, sizeof(dfloat));
    dfloat *invDiagA = (dfloat*) calloc(mesh->Np*mesh->Nelements, sizeof(dfloat));
    BuildDiag(diagA);
    for (dlong n=0;n<mesh->Nelements*mesh->Np;n++)
      invDiagA[n] = 1.0/diagA[n];

    precon->o_invDiagA = mesh->device.malloc(mesh->Np*mesh->Nelements*sizeof(dfloat), invDiagA);

    free(diagA);
    free(invDiagA);
  } else if(options.compareArgs("PRECONDITIONER", "OAS")){

    printf("ERROR:  OAS does not work right now.\n");
    exit(-1);

    //if(mesh->N>1)
    //  ellipticOasSetup(elliptic, lambda, kernelInfo);
    //else{
    //  dfloat *invDiagA;
    //  ellipticBuildJacobi(elliptic,lambda,&invDiagA);
    //  precon->o_invDiagA = mesh->device.malloc(mesh->Np*mesh->Nelements*sizeof(dfloat), invDiagA);
    //  free(invDiagA);
    //}
  }
}

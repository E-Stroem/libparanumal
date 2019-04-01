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

static void stokesSetupRHS(stokes_t *stokes, dfloat lambda);
static void stokesRHSAddBC(stokes_t *stokes, dfloat lambda);

static void stokesTestForcingFunctionConstantViscosityQuad2D(dfloat x, dfloat y, dfloat *fx, dfloat *fy);
static void stokesTestForcingFunctionVariableViscosityQuad2D(dfloat x, dfloat y, dfloat *fx, dfloat *fy);
static void stokesTestForcingFunctionDirichletQuad2D(dfloat x, dfloat y, dfloat lambda, dfloat *fx, dfloat *fy);
static void stokesTestForcingFunctionLeakyCavityQuad2D(dfloat x, dfloat y, dfloat *fx, dfloat *fy);
static void stokesTestForcingFunctionConstantViscosityHex3D(dfloat x, dfloat y, dfloat z, dfloat lambda, dfloat *fx, dfloat *fy, dfloat *fz);

stokes_t *stokesSetup(dfloat lambda, occa::properties &kernelInfoV, occa::properties &kernelInfoP, setupAide options)
{
  int      velocityN, pressureN, dim, elementType;
  int      velocityNtotal, pressureNtotal;
  string   fileName, bcHeaderFileName;
  stokes_t *stokes;
  dfloat   *eta;

  stokes = new stokes_t();

  // Load information from the setup file.
  options.getArgs("MESH FILE", fileName);
  options.getArgs("DATA FILE", bcHeaderFileName);
  options.getArgs("VELOCITY POLYNOMIAL DEGREE", velocityN);
  options.getArgs("PRESSURE POLYNOMIAL DEGREE", pressureN);
  options.getArgs("ELEMENT TYPE", elementType);
  options.getArgs("MESH DIMENSION", dim);

  stokes->options = options;
  stokes->elementType = elementType;

  // Setup meshes.
  //
  // TODO:  Is two separate meshes the right approach?
  //
  // TODO:  This results in duplicate device objects and instruction streams.
  stokes->meshV = meshSetup((char*)fileName.c_str(), velocityN, options);
  stokes->meshP = meshSetup((char*)fileName.c_str(), pressureN, options);

  // OCCA setup.
  kernelInfoV["defines"].asObject();
  kernelInfoV["includes"].asArray();
  kernelInfoV["header"].asArray();
  kernelInfoV["flags"].asObject();

  kernelInfoP["defines"].asObject();
  kernelInfoP["includes"].asArray();
  kernelInfoP["header"].asArray();
  kernelInfoP["flags"].asObject();

  kernelInfoV["includes"] += bcHeaderFileName.c_str();

  if (dim == 2) {
    if (elementType == QUADRILATERALS) {
      meshOccaSetup2D(stokes->meshV, options, kernelInfoV);
      meshOccaSetup2D(stokes->meshP, stokes->meshV->device, options, kernelInfoP);
    } else {
      printf("ERROR:  Support for 2D elements other than QUADRILATERALS not yet implemented.\n");
      MPI_Finalize();
      exit(-1);
    }
  } else if (dim == 3) {
    if (elementType == HEXAHEDRA) {
      meshOccaSetup3D(stokes->meshV, options, kernelInfoV);
      meshOccaSetup3D(stokes->meshP, stokes->meshV->device, options, kernelInfoP);
    } else {
      printf("ERROR:  Support for 3D elements other than HEXAHEDRA not yet implemented.\n");
      MPI_Finalize();
      exit(-1);
    }
  } else {
    printf("ERROR:  MESH DIMENSION must be 2 or 3.\n");
    MPI_Finalize();
    exit(-1);
  }

  /* TODO:  This needs to be specified by the test cases. */
  eta = (dfloat*)calloc(stokes->meshV->Nelements*stokes->meshV->Np, sizeof(dfloat));
  for (int e = 0; e < stokes->meshV->Nelements; e++) {
    for (int i = 0; i < stokes->meshV->Np; i++) {
      int    ind;
      dfloat x, y, z;

      ind = e*stokes->meshV->Np + i;
      x = stokes->meshV->x[ind];
      y = stokes->meshV->y[ind];
      z = stokes->meshV->z[ind];

      if (dim == 2) {
        eta[ind] = 1.0;
        //eta[ind] = 2.0 + sinh(x*y);
      } else if (dim == 3) {
        eta[ind] = 1.0;
      }
    }
  }

  // Set up the physical-to-mathematial BC map.
  int BCType[3] = {0, 1, 2};
  stokes->BCType = (int*)calloc(3, sizeof(int));
  memcpy(stokes->BCType, BCType, 3*sizeof(int));

  stokesSolveSetup(stokes, lambda, eta, kernelInfoV, kernelInfoP);
  stokesSetupRHS(stokes, lambda);

#if 0
  /* Load initial guess. */
  const dfloat NOISE_SIZE = 1.0e-8;
  srand48(67714070);

  for (int e = 0; e < stokes->meshV->Nelements; e++) {
    for (int i = 0; i < stokes->meshV->Np; i++) {
      int    ind;
      dfloat x, y, z;

      ind = e*stokes->meshV->Np + i;
      x = stokes->meshV->x[ind];
      y = stokes->meshV->y[ind];
      z = stokes->meshV->z[ind];

      stokes->u.x[ind] = cos(y)*(1.0 + NOISE_SIZE*drand48());
      stokes->u.y[ind] = sin(x)*(1.0 + NOISE_SIZE*drand48());
    }
  }

  for (int e = 0; e < stokes->meshP->Nelements; e++) {
    for (int i = 0; i < stokes->meshP->Np; i++) {
      int    ind;
      dfloat x, y, z;

      ind = e*stokes->meshP->Np + i;
      x = stokes->meshP->x[ind];
      y = stokes->meshP->y[ind];
      z = stokes->meshP->z[ind];

      stokes->u.p[ind] = (x + y)*(1.0 + NOISE_SIZE*drand48());
    }
  }

  //stokesVecCopyHostToDevice(stokes->u);

  //stokes->dotMultiplyKernel(stokes->NtotalV, stokes->meshV->ogs->o_invDegree, stokes->u.o_x, stokes->u.o_x);
  //stokes->dotMultiplyKernel(stokes->NtotalV, stokes->meshV->ogs->o_invDegree, stokes->u.o_y, stokes->u.o_y);
  //if (stokes->meshV->dim == 3)
  //  stokes->dotMultiplyKernel(stokes->NtotalV, stokes->meshV->ogs->o_invDegree, stokes->u.o_z, stokes->u.o_z);
  //stokes->dotMultiplyKernel(stokes->NtotalP, stokes->meshP->ogs->o_invDegree, stokes->u.o_p, stokes->u.o_p);

  //stokesVecUnmaskedGatherScatter(stokes, stokes->u);
  //if (stokes->Nmasked) {
  //  stokes->meshV->maskKernel(stokes->Nmasked, stokes->o_maskIds, stokes->u.o_x);
  //  stokes->meshV->maskKernel(stokes->Nmasked, stokes->o_maskIds, stokes->u.o_y);
  //  if (stokes->meshV->dim == 3)
  //    stokes->meshV->maskKernel(stokes->Nmasked, stokes->o_maskIds, stokes->u.o_z);
  //}
#endif

  free(eta);
  return stokes;
}

static void stokesSetupRHS(stokes_t *stokes, dfloat lambda)
{
  int              dim;
  stokesTestCase_t testCase;

  stokes->options.getArgs("MESH DIMENSION", dim);

  stokesGetTestCase(stokes, &testCase);

  // Initialize right-hand side with the forcing term.
  for (int e = 0; e < stokes->meshV->Nelements; e++) {
    for (int i = 0; i < stokes->meshV->Np; i++) {
      int    ind;
      dfloat x, y, z, J;

      ind = e*stokes->meshV->Np + i;
      x = stokes->meshV->x[ind];
      y = stokes->meshV->y[ind];
      z = stokes->meshV->z[ind];

      if (dim == 2) {
        stokesForcingFunction2D forcingFn = (stokesForcingFunction2D)testCase.forcingFn;
        forcingFn(x, y, lambda, stokes->f.x + ind, stokes->f.y + ind);
      } else if (dim == 3) {
        stokesForcingFunction3D forcingFn = (stokesForcingFunction3D)testCase.forcingFn;
        forcingFn(x, y, z, lambda, stokes->f.x + ind, stokes->f.y + ind, stokes->f.z + ind);
      }

      // NB:  We have to incorporate the Jacobian factor because meshApplyElementMatrix() assumes it.
      //
      // TODO:  This may assume the use of quadrilateral/hexahedral elements.
      J = stokes->meshV->vgeo[stokes->meshV->Np*(e*stokes->meshV->Nvgeo + JID) + i];
      stokes->f.x[ind] *= J;
      stokes->f.y[ind] *= J;
      if (dim == 3)
        stokes->f.z[ind] *= J;
    }
  }

  // Multiply by mass matrix to get the true right-hand side.
  meshApplyElementMatrix(stokes->meshV, stokes->meshV->MM, stokes->f.x, stokes->f.x);
  meshApplyElementMatrix(stokes->meshV, stokes->meshV->MM, stokes->f.y, stokes->f.y);
  if (dim == 3)
    meshApplyElementMatrix(stokes->meshV, stokes->meshV->MM, stokes->f.z, stokes->f.z);

  // Move RHS to the device.
  stokesVecCopyHostToDevice(stokes->f);

  // Apply the boundary conditions.
  stokesRHSAddBC(stokes, lambda);

  // Gather-scatter for C0 FEM.
  stokesVecUnmaskedGatherScatter(stokes, stokes->f);

  // TODO:  Make a function for this.
  //
  // TODO:  We only need to do this for C0 FEM.
  if (stokes->Nmasked) {
    stokes->meshV->maskKernel(stokes->Nmasked, stokes->o_maskIds, stokes->f.o_x);
    stokes->meshV->maskKernel(stokes->Nmasked, stokes->o_maskIds, stokes->f.o_y);
    if (stokes->meshV->dim == 3)
      stokes->meshV->maskKernel(stokes->Nmasked, stokes->o_maskIds, stokes->f.o_z);
  }

  return;
}

// TODO:  Write a kernel for this.
static void stokesRHSAddBC(stokes_t *stokes, dfloat lambda)
{
  stokesVec_t      tmp;
  stokesTestCase_t testCase;

  occa::memory o_interpRaise = stokes->meshV->device.malloc(stokes->meshP->Nq*stokes->meshV->Nq*sizeof(dfloat), stokes->meshP->interpRaise);
  occa::memory o_pRaised = stokes->meshV->device.malloc(stokes->NtotalV*sizeof(dfloat));

  stokesVecAllocate(stokes, &tmp);

  stokesGetTestCase(stokes, &testCase);

  for (int e = 0; e < stokes->meshV->Nelements; e++) {
    for (int i = 0; i < stokes->meshV->Np; i++) {
      int    ind;
      dfloat x, y, z, p;

      ind = e*stokes->meshV->Np + i;
      x = stokes->meshV->x[ind];
      y = stokes->meshV->y[ind];
      z = stokes->meshV->z[ind];

      /* TODO:  Handle boundary data when we don't know the exact solution. */
      if (stokes->mapB[ind] == 1) {
        if (stokes->meshV->dim == 2) {
          stokesSolutionFunction2D solFn = (stokesSolutionFunction2D)testCase.solFn;
          solFn(x, y, lambda, tmp.x + ind, tmp.y + ind, &p);
        } else if (stokes->meshV->dim == 3) {
          stokesSolutionFunction3D solFn = (stokesSolutionFunction3D)testCase.solFn;
          solFn(x, y, z, lambda, tmp.x + ind, tmp.y + ind, tmp.z + ind, &p);
        }
      }
    }
  }

  stokesVecCopyHostToDevice(tmp);

  stokesVecZero(stokes, stokes->u);

  if (stokes->options.compareArgs("INTEGRATION TYPE", "GLL")) {
#if 0
    stokes->stiffnessKernel(stokes->meshV->Nelements,
                            stokes->meshV->o_ggeo,
                            stokes->meshV->o_Dmatrices,
                            stokes->o_eta,
                            tmp.o_x,
                            stokes->u.o_x);

    stokes->stiffnessKernel(stokes->meshV->Nelements,
                            stokes->meshV->o_ggeo,
                            stokes->meshV->o_Dmatrices,
                            stokes->o_eta,
                            tmp.o_y,
                            stokes->u.o_y);

    if (stokes->meshV->dim == 3) {
      stokes->stiffnessKernel(stokes->meshV->Nelements,
                              stokes->meshV->o_ggeo,
                              stokes->meshV->o_Dmatrices,
                              stokes->o_eta,
                              tmp.o_z,
                              stokes->u.o_z);
    }

    stokes->raisePressureKernel(stokes->meshV->Nelements,
                                o_interpRaise,
                                tmp.o_p,
                                o_pRaised);

    stokes->gradientKernel(stokes->meshV->Nelements,
                           stokes->NtotalV,
                           stokes->meshV->o_Dmatrices,
                           stokes->meshV->o_vgeo,
                           o_pRaised,
                           stokes->u.o_v);

    stokes->divergenceKernel(stokes->meshV->Nelements,
                             stokes->NtotalV,
                             stokes->meshV->o_Dmatrices,
                             stokes->meshV->o_vgeo,
                             tmp.o_v,
                             o_pRaised);

    stokes->lowerPressureKernel(stokes->meshV->Nelements,
                                o_interpRaise,
                                o_pRaised,
                                stokes->u.o_p);
#else
    stokes->raisePressureKernel(stokes->meshV->Nelements,
                                o_interpRaise,
                                tmp.o_p,
                                o_pRaised);

    stokes->stokesOperatorKernel(stokes->meshV->Nelements,
                                 stokes->NtotalV,
                                 stokes->meshV->o_vgeo,
                                 stokes->meshV->o_Dmatrices,
                                 lambda,
                                 stokes->o_eta,
                                 tmp.o_v,
                                 o_pRaised,
                                 stokes->u.o_v);

    stokes->lowerPressureKernel(stokes->meshV->Nelements,
                                o_interpRaise,
                                o_pRaised,
                                stokes->u.o_p);
#endif
  } else if (stokes->options.compareArgs("INTEGRATION TYPE", "CUBATURE")) {
    stokes->stiffnessKernel(stokes->meshV->Nelements,
                            stokes->meshV->o_cubggeo,
                            stokes->o_cubD,
                            stokes->o_cubInterpV,
                            stokes->o_cubEta,
                            tmp.o_x,
                            stokes->u.o_x);

    stokes->stiffnessKernel(stokes->meshV->Nelements,
                            stokes->meshV->o_cubggeo,
                            stokes->o_cubD,
                            stokes->o_cubInterpV,
                            stokes->o_cubEta,
                            tmp.o_y,
                            stokes->u.o_y);

    if (stokes->meshV->dim == 3) {
    stokes->stiffnessKernel(stokes->meshV->Nelements,
                            stokes->meshV->o_cubggeo,
                            stokes->o_cubD,
                            stokes->o_cubInterpV,
                            stokes->o_cubEta,
                            tmp.o_z,
                            stokes->u.o_z);
    }

    stokes->gradientKernel(stokes->meshV->Nelements,
			                     stokes->NtotalV,
			                     stokes->meshV->o_cubvgeo,
			                     stokes->o_cubD,
			                     stokes->o_cubInterpV,
			                     stokes->o_cubInterpP,
			                     tmp.o_p,
			                     stokes->u.o_v);

    stokes->divergenceKernel(stokes->meshV->Nelements,
			                       stokes->NtotalV,
			                       stokes->meshV->o_cubvgeo,
			                       stokes->o_cubD,
			                       stokes->o_cubInterpV,
			                       stokes->o_cubInterpP,
			                       tmp.o_v,
			                       stokes->u.o_p);
  }

  stokesVecScaledAdd(stokes, -1.0, stokes->u, 1.0, stokes->f);

  stokesVecZero(stokes, stokes->u);

  stokesVecFree(stokes, &tmp);
  o_pRaised.free();
  o_interpRaise.free();
  return;
}
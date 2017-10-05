#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "mpi.h"
#include "mesh3D.h"
#include "ins3D.h"

int main(int argc, char **argv){
  // start up MPI
  MPI_Init(&argc, &argv);

  char *velSolverOptions = strdup("solver=PCG method=IPDG preconditioner=MASSMATRIX");
  char *velParAlmondOptions = strdup("solver= smoother= partition=");

  char *prSolverOptions =
    strdup("solver=PCG,FLEXIBLE method=IPDG preconditioner=MULTIGRID, HALFDOFS smoother=DAMPEDJACOBI,CHEBYSHEV");
    //strdup("solver=PCG,FLEXIBLE,VERBOSE method=IPDG preconditioner=NONE");
   // strdup("solver=PCG,FLEXIBLE,method=IPDG  preconditioner=FULLALMOND");
    //strdup("solver=PCG,FLEXIBLE, method=IPDG preconditioner=OMS,APPROXPATCH coarse=COARSEGRID,ALMOND");

  char *prParAlmondOptions =
    strdup("solver=KCYCLE smoother=CHEBYSHEV partition=STRONGNODES");

  if(argc!=3 && argc!=4 && argc!=5){
    printf("usage 1: ./main meshes/cavityH005.msh N\n");
    printf("usage 2: ./main meshes/cavityH005.msh N insUniformFlowBoundaryConditions.h\n");
    printf("usage 3: ./main meshes/cavityH005.msh N insUniformFlowBoundaryConditions.h Nsubstep\n");
    exit(-1);
  }
  // int specify polynomial degree
  int N = atoi(argv[2]);


  // set up mesh stuff
  mesh3D *mesh = meshSetupTet3D(argv[1], N); 

  // capture header file
  char *boundaryHeaderFileName;
  if(argc==3)
    boundaryHeaderFileName = strdup(DHOLMES "/examples/insTet3D/insUniform3D.h"); // default
  else
    boundaryHeaderFileName = strdup(argv[3]);

  int Ns = 0; // Default no-subcycling 
  if(argc==5)
   Ns = atoi(argv[4]); // Number of substeps
  
  
  char *options; 
  if(Ns==0)
      options = strdup("method = ALGEBRAIC, grad-div= BROKEN, out=REPORT, adv=CUBATURE, disc = DISCONT_GALERKIN"); // SUBCYCLING
  else
      options = strdup("method = ALGEBRAIC, grad-div= BROKEN, SUBCYCLING, out=REPORT, adv=CUBATURE, disc = DISCONT_GALERKIN"); // SUBCYCLING

  printf("Setup INS Solver: \n");
  ins_t *ins = insSetup3D(mesh, Ns, options,
                          velSolverOptions,velParAlmondOptions,
                          prSolverOptions, prParAlmondOptions,
                          boundaryHeaderFileName);

  printf("OCCA Run: \n");
  insRun3D(ins,options);

  // close down MPI
  MPI_Finalize();

  exit(0);
  return 0;
}
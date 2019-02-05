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

int main(int argc, char **argv) {
  stokes_t         *stokes;
  occa::properties kernelInfoV, kernelInfoP;

  // Start up MPI.
  MPI_Init(&argc, &argv);

  if (argc != 2) {
    printf("usage: ./stokesMain setupfile\n");
    MPI_Finalize();
    exit(-1);
  }

  setupAide options(argv[1]);

  stokes = stokesSetup(kernelInfoP, kernelInfoV, options);
  stokesSolve(stokes);

  /* Compute error (if applicable.) */
  dfloat errxInf = 0.0, erryInf = 0.0, errxDL2 = 0.0, erryDL2 = 0.0;
  for (int e = 0; e < stokes->meshV->Nelements; e++) {
    for (int i = 0; i < stokes->meshV->Np; i++) {
      int    ind;
      dfloat x, y, errx, erry;

      ind = e*stokes->meshV->Np + i;
      x = stokes->meshV->x[ind];
      y = stokes->meshV->y[ind];

      dfloat ux_exact = 6.0*pow(1.0 - x*x, 3.0)*pow(1.0 - y*y, 2.0)*y;
      dfloat uy_exact = -6.0*pow(1.0 - y*y, 3.0)*pow(1.0 - x*x, 2.0)*x;

      errx = stokes->u.x[ind] - ux_exact;
      erry = stokes->u.y[ind] - uy_exact;

      //printf("ux[%d] = % .15e, uy[%d] = % .15e, errx[%d] = % .15e, erry[%d] = % .15e\n",
      //       ind, stokes->u.x[ind], ind, stokes->u.y[ind], ind, errx, ind, erry);

      if (fabs(errx) > errxInf)
        errxInf = fabs(errx);
      if (fabs(erry) > erryInf)
        erryInf = fabs(erry);
      errxDL2 += errx*errx;
      erryDL2 += erry*erry;
    }
  }

  errxDL2 = sqrt(errxDL2);
  erryDL2 = sqrt(erryDL2);

  printf("-----\n");

  printf("errxInf = % .15e\n", errxInf);
  printf("erryInf = % .15e\n", erryInf);
  printf("errxDL2 = % .15e\n", errxDL2);
  printf("erryDL2 = % .15e\n", erryDL2);

  /* Export solution. */

  /* Report runtime statistics. */

  // Shut down MPI.
  MPI_Finalize();

  return 0;
}

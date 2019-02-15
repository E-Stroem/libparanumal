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

#include "adaptive.h"


dfloat adaptiveWeightedNorm2(adaptive_t *adaptive, level_t *level,
			     occa::memory &o_w, occa::memory &o_a){

  setupAide &options = adaptive->options;

  int continuous = options.compareArgs("DISCRETIZATION", "CONTINUOUS");
  int serial = options.compareArgs("THREAD MODEL", "Serial");
  int enableReductions = 1;
  options.getArgs("DEBUG ENABLE REDUCTIONS", enableReductions);

  dfloat *tmp = level->tmp;
  dlong Nblock = level->Nblock;
  dlong Nblock2 = level->Nblock2;
  dlong Ntotal = level->Klocal*level->Np;

  occa::memory &o_tmp = level->o_tmp;
  occa::memory &o_tmp2 = level->o_tmp2;
  
  if(continuous==1)
    adaptive->weightedNorm2Kernel(Ntotal, o_w, o_a, o_tmp);
  else
    adaptive->innerProductKernel(Ntotal, o_a, o_a, o_tmp);

  /* add a second sweep if Nblock>Ncutoff */
  dlong Ncutoff = 100;
  dlong Nfinal;
  if(Nblock>Ncutoff){

    adaptive->sumKernel(Nblock, o_tmp, o_tmp2);

    o_tmp2.copyTo(tmp, Nblock2*sizeof(dfloat_t), 0);

    Nfinal = Nblock2;
	
  }
  else{
    o_tmp.copyTo(tmp, Nblock*sizeof(dfloat_t), 0);
    
    Nfinal = Nblock;

  }    

  dfloat wa2 = 0;
  for(dlong n=0;n<Nfinal;++n){
    wa2 += tmp[n];
  }

  dfloat globalwa2 = 0;
  MPI_Allreduce(&wa2, &globalwa2, 1, MPI_DFLOAT, MPI_SUM, adaptive->comm);

  return globalwa2;
}


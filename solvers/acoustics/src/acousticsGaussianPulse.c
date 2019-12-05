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

#include <math.h>
#include "mesh.h"
/*
void acousticsGaussianPulse(dfloat x, dfloat y, dfloat z, dfloat t,
		      dfloat *r, dfloat *u, dfloat *v, dfloat *w){

  *r = 1 + exp(-3*(x*x+y*y+z*z));
  *u = 0;
  *v = 0;
  *w = 0;

}
*/

// [EA] - Our version
// TODO: Allow source to be moved
void acousticsGaussianPulse(dfloat x, dfloat y, dfloat z, dfloat t,
		      dfloat *r, dfloat *u, dfloat *v, dfloat *w, dfloat *sloc, dfloat sxyz){
  dfloat sxyzSQ = sxyz*sxyz;
  dfloat temp = (x - sloc[0])*(x - sloc[0]) + (y - sloc[1])*(y - sloc[1]) + (z - sloc[2])*(z - sloc[2]);
  temp /= sxyzSQ;

  *r = exp(-temp);
  *u = 0;
  *v = 0;
  *w = 0;

}
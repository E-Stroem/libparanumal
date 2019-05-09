/*

The MIT License (MIT)

Copyright (c) 2017 Tim Warburton

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

#include <stdio.h>
#include <stdlib.h>
#include <cuda.h>

static const int p_Nq = comp_Nq;
static const int p_cubNq = comp_cubNq;

static const int p_halfNq = ((comp_Nq+1)/2);
static const int p_halfCubNq = ((comp_cubNq+1)/2);

static const int p_padNq = (p_Nq%4) ? 0:1;
static const int p_padCubNq = (p_cubNq%4) ? 0:1;

#define p_Nq2 (p_Nq*p_Nq)
#define p_Np  (p_Nq*p_Nq*p_Nq)

#define p_cubNq2 (p_cubNq*p_cubNq)
#define p_cubNp  (p_cubNq*p_cubNq*p_cubNq)

#define p_Nvgeo 1
#define p_JWID 0

#define p_Nwarps ((p_Nq2+32-1)/32)

#define dlong int
#define hlong dlong
#define dfloat double

__constant__ dfloat const_I[p_halfCubNq*p_Nq];
__constant__ dfloat const_IT[p_cubNq*p_halfNq];


__forceinline__ __device__ void
advectionMassMatrixMultiplyOddEven(const dlong element,
				   const dfloat * __restrict__ r_p,
				   const dfloat s_WJ[p_cubNq][p_cubNq][p_cubNq],
				   dfloat s_Ap[p_cubNq][p_cubNq][p_cubNq+p_padCubNq],
				   dfloat * __restrict__ r_Ap){
  
  dfloat r_tmpOdd[p_halfCubNq];
  dfloat r_tmpEven[p_halfCubNq];
  
  const int t = threadIdx.x;

  // assumes barrier before s_Ap was used last
  
  // transform in 'c'
  {
    const int a = t%p_Nq;
    const int b = t/p_Nq;
    
    const dfloat * __restrict__ cI = const_I;
    
#pragma unroll p_halfNq
    for(int c=0;c<p_halfNq;++c){
      r_tmpOdd[c]  = r_p[c] + r_p[p_Nq-1-c];
      r_tmpEven[c] = r_p[c] - r_p[p_Nq-1-c];
    }
    
#pragma unroll p_halfCubNq
    for(int k=0;k<p_halfCubNq;++k){
      dfloat resOdd = 0, resEven = 0;
      
#pragma unroll p_halfNq
      for(int c=0;c<p_halfNq;++c){
	resOdd += *(cI++)*r_tmpOdd[c];
	resEven += *(cI++)*r_tmpEven[c];
      }
      
      s_Ap[k][b][a]           = resOdd-resEven;
      s_Ap[p_cubNq-1-k][b][a] = resOdd\+resEven;
    }
  }
  
  __syncthreads();

  // transform in 'b'
  {
    for(int n=t;n<p_Nq*p_cubNq;n+=p_Nq2){
      const int a = n%p_Nq;
      const int k = n/p_Nq;
      
#pragma unroll p_halfNq
      for(int b=0;b<p_halfNq;++b){
	dfloat ApOdd  = s_Ap[k][b][a];
	dfloat ApEven = s_Ap[k][p_Nq-1-b][a];
	r_tmpOdd[b]  = ApOdd + ApEven;
	r_tmpEven[b] = ApOdd - ApEven;
      }      
      
      const dfloat * __restrict__ cI = const_I;
      
#pragma unroll p_halfCubNq
      for(int j=0;j<p_halfCubNq;++j){
	dfloat resOdd = 0, resEven = 0;
	
#pragma unroll p_halfNq
	for(int b=0;b<p_halfNq;++b){
	  resOdd += *(cI++)*r_tmpOdd[b];
	  resEven += *(cI++)*r_tmpEven[b];
	}
	
	s_Ap[k][j][a]               = resOdd-resEven;
	s_Ap[k][p_halfCubNq-1-j][a] = resOdd+resEven;
      }
    }
  }
  
  __syncthreads();
  
  // transform in 'a'
  {
    for(int n=t;n<p_cubNq2;n+=p_Nq2){
      const int j = n%p_cubNq;
      const int k = n/p_cubNq;
      
#pragma unroll p_halfNq
      for(int a=0;a<p_halfNq;++a){
	dfloat ApOdd  = s_Ap[k][j][a];
	dfloat ApEven = s_Ap[k][j][p_Nq-1-a];
	r_tmpOdd[a]  = ApOdd + ApEven;
	r_tmpEven[a] = ApOdd - ApEven;
      }
      
      const dfloat * __restrict__ cI = const_I;
      
#pragma unroll p_halfCubNq
      for(int i=0;i<p_halfCubNq;++i){
	dfloat resOdd = 0, resEven = 0;
	
#pragma unroll p_halfNq
	for(int a=0;a<p_halfNq;++a){
	  resOdd  += *(cI++)*r_tmpOdd[a];
	  resEven += *(cI++)*r_tmpEven[a];
	}
	dfloat ApOdd = s_WJ[k][j][i]*(resOdd - resEven);
	dfloat ApEven = s_WJ[k][j][p_cubNq-1-i]*(resOdd + resEven);

	r_Ap[i] = ApOdd + ApEven;
	r_Ap[p_cubNq-1-i] = ApOdd - ApEven;
      }
      
      const dfloat * __restrict__ cIT = const_IT;
      
#pragma unroll p_halfNq
      for(int a=0;a<p_halfNq;++a){
	dfloat resOdd = 0, resEven = 0;
	
#pragma unroll p_halfCubNq
	for(int i=0;i<p_halfCubNq;++i){
	  resOdd  += *(cIT++)*r_Ap[i];
	  resEven += *(cIT++)*r_Ap[p_cubNq-1-i];
	}
	
	s_Ap[k][j][a]        = resOdd - resEven;
	s_Ap[k][j][p_Nq-1-a] = resOdd + resEven;
      }
    }
  }
  
  __syncthreads();

  // test in 'b'
  {

    for(int n=t;n<p_Nq*p_cubNq;n+=p_Nq2){
      const int a = n%p_Nq;
      const int k = n/p_Nq;

      const dfloat * __restrict__ cIT = const_IT;
	
      for(int j=0;j<p_halfCubNq;++j){
	dfloat ApOdd  = s_Ap[k][j][a];
	dfloat ApEven = s_Ap[k][p_cubNq-1-j][a];
	r_tmpOdd[j]  = ApOdd + ApEven;
	r_tmpEven[j] = ApOdd - ApEven;
      }

#pragma unroll p_halfNq
      for(int b=0;b<p_halfNq;++b){
	dfloat resOdd = 0, resEven = 0;
	
#pragma unroll p_halfCubNq
	for(int j=0;j<p_halfCubNq;++j){
	  resOdd  += *(cIT++)*r_tmpOdd[j];
	  resEven += *(cIT++)*r_tmpEven[j];
	}
	
	s_Ap[k][b][a]        = resOdd - resEven;
	s_Ap[k][p_Nq-1-b][a] = resOdd + resEven;
      }
    }
  }
  
  __syncthreads();

  // test in 'c'
  {
    const int a = t%p_Nq;
    const int b = t/p_Nq;

    for(int k=0;k<p_halfCubNq;++k){
      dfloat ApOdd  = s_Ap[k][b][a];
      dfloat ApEven = s_Ap[p_cubNq-1-k][b][a];
      r_tmpOdd[k]  = ApOdd + ApEven;
      r_tmpEven[k] = ApOdd - ApEven;
    }
    
    const dfloat * __restrict__ cIT = const_IT;
    
#pragma unroll p_halfNq
    for(int c=0;c<p_halfNq;++c){
      dfloat resOdd = 0, resEven = 0;
      
#pragma unroll p_halfCubNq
      for(int k=0;k<p_halfCubNq;++k){
	resOdd  += *(cIT++)*r_tmpOdd[k];
	resEven += *(cIT++)*r_tmpEven[k];
      }
      
      r_Ap[c]        = resOdd - resEven;
      r_Ap[p_Nq-1-c] = resOdd +  resEven;
      
    }
  }


}

__global__ void advectionMassMatrixMultiplyKernel(const dlong Nelements,
						  const dlong  * __restrict__ elementIds,
						  const dfloat * __restrict__ cubvgeo,
						  const dfloat * __restrict__ cubI,
						  const dfloat * __restrict__ q,
						  dfloat * __restrict__ qnew){
  
  __shared__ dfloat s_tmp1[p_cubNq][p_cubNq][p_cubNq+p_padCubNq];

  dfloat r_q[p_Nq], r_Aq[p_Nq];
  
  __shared__ dfloat s_WJ[p_cubNq][p_cubNq][p_cubNq];

  const dlong e = blockIdx.x;

  const dlong element = elementIds[e];

  const unsigned int t = threadIdx.x;

  const unsigned int a = t%p_Nq;
  const unsigned int b = t/p_Nq;

  int i = t;
  while(i<p_cubNp){
    const dlong gid = element*p_cubNp*p_Nvgeo + i + p_JWID*p_cubNp; //  (i slowest, k middle, j fastest)
    s_WJ[0][0][i] = cubvgeo[gid];
    i+=p_Nq2;
  }

  for(int c=0;c<p_Nq;++c){
    
    dlong id = a + b*p_Nq + c*p_Nq2 + element*p_Np;
    
    r_q[c] = q[id];
  }

  advectionMassMatrixMultiplyOddEven(element, r_q, s_WJ, s_tmp1, r_Aq);

#pragma unroll p_Nq
  for(int c=0;c<p_Nq;++c){
    dlong id = a + b*p_Nq + c*p_Nq2 + element*p_Np;
    qnew[id] = r_Aq[c];
  }
}

void dfloatRandAlloc(int N, dfloat **h_a, dfloat **c_a){

  *h_a = (dfloat*) calloc(N, sizeof(dfloat));

  for(int n=0;n<N;++n)
    h_a[0][n] = drand48();

  cudaMalloc(c_a, N*sizeof(dfloat));

  cudaMemcpy(c_a[0], h_a[0], N*sizeof(dfloat), cudaMemcpyHostToDevice);

}

void advectionMassMatrixMultiplyHost(const dlong Nelements,
				     const dlong  * __restrict__ elementIds,
				     const dfloat * __restrict__ cubvgeo,
				     const dfloat * __restrict__ cubI,
				     const dfloat * __restrict__ q,
				     dfloat * __restrict__ qnew){


  dfloat qXXX[p_Nq][p_Nq][p_Nq];
  dfloat qIXX[p_cubNq][p_Nq][p_Nq];
  dfloat qIIX[p_cubNq][p_cubNq][p_Nq];
  dfloat qIII[p_cubNq][p_cubNq][p_cubNq];
    
  for(dlong e=0;e<Nelements;++e){

    for(int c=0;c<p_Nq;++c){
      for(int b=0;b<p_Nq;++b){
	for(int a=0;a<p_Nq;++a){
	  int id = e*p_Np + c*p_Nq2 + b*p_Nq + a;
	  qXXX[c][b][a] = q[id];
	}
      }
    }
    
    for(int k=0;k<p_cubNq;++k){
      for(int b=0;b<p_Nq;++b){
	for(int a=0;a<p_Nq;++a){
	  
	  dfloat res = 0;
	  
	  for(int c=0;c<p_Nq;++c){
	    dfloat Ikc = cubI[k*p_Nq+c];
	    res += Ikc*qXXX[c][b][a];
	  }
	  
	  qIXX[k][b][a] = res;
	}
      }
    }
    
    // interpolate in b
    for(int k=0;k<p_cubNq;++k){
      for(int j=0;j<p_cubNq;++j){
	for(int a=0;a<p_Nq;++a){
	  
	  dfloat res = 0;
	  
	  for(int b=0;b<p_Nq;++b){
	    dfloat Ijb = cubI[j*p_Nq+b];
	    res += Ijb*qIXX[k][b][a];
	  }
	  
	  qIIX[k][j][a] = res;
	}
      }
    }

    // interpolate in a
    for(int k=0;k<p_cubNq;++k){
      for(int j=0;j<p_cubNq;++j){
	for(int i=0;i<p_cubNq;++i){

	  dfloat res = 0;
	  
	  for(int a=0;a<p_Nq;++a){
	    dfloat Iia = cubI[i*p_Nq+a];
	    res += Iia*qIIX[k][j][a];
	  }
	  
	  int gid = e*p_cubNp + k*p_cubNq2 + j*p_cubNq + i;
	  
	  dfloat JW = cubvgeo[gid];
	  
	  qIII[k][j][i] = res*JW;
	}
      }
    }


    // project in a
    for(int k=0;k<p_cubNq;++k){
      for(int j=0;j<p_cubNq;++j){
	for(int a=0;a<p_Nq;++a){

	  dfloat res = 0;
	  
	  for(int i=0;i<p_cubNq;++i){
	    dfloat Iia = cubI[i*p_Nq+a];
	    res += Iia*qIII[k][j][i];
	  }
	  qIIX[k][j][a] = res;
	}
      }
    }


    // project in b
    for(int k=0;k<p_cubNq;++k){
      for(int b=0;b<p_Nq;++b){
	for(int a=0;a<p_Nq;++a){

	  dfloat res = 0;

	  for(int j=0;j<p_cubNq;++j){
	    dfloat Ijb = cubI[j*p_Nq+b];
	    res += Ijb*qIIX[k][j][a];
	  }
	  
	  qIXX[k][b][a] = res;
	}
      }
    }


    // project in c
    for(int c=0;c<p_Nq;++c){
      for(int b=0;b<p_Nq;++b){
	for(int a=0;a<p_Nq;++a){

	  dfloat res = 0;

	  for(int k=0;k<p_cubNq;++k){
	    dfloat Ikc = cubI[k*p_Nq+c];
	    res += Ikc*qIXX[k][b][a];
	  }

	  int id = e*p_Np + c*p_Nq2 + b*p_Nq + a;
	  qnew[id] = res;
	}
      }
    }
  }
    
  
}


void buildInterpMatrices(dfloat **h_I, dfloat **c_I){

  dfloatRandAlloc(p_Nq*p_cubNq, h_I, c_I);

  // now overwrite h_I and copy to c_I
  for(int i=0;i<p_halfCubNq;++i){
    for(int a=0;a<p_Nq;++a){
      h_I[0][(p_cubNq-1-i)*p_Nq + p_Nq-1-a] = h_I[0][i*p_Nq+a];
    }
  }

  for(int i=0;i<p_cubNq;++i){
    for(int a=0;a<p_Nq;++a){
      printf("% .4e ", h_I[0][i*p_Nq+a]);
    }
    printf("\n");
  }
  cudaMemcpy(*c_I, *h_I, p_Nq*p_cubNq*sizeof(dfloat), cudaMemcpyHostToDevice);

  dfloat *X = (dfloat*) calloc(p_Nq*p_Nq, sizeof(dfloat));
  dfloat *invX = (dfloat*) calloc(p_Nq*p_Nq, sizeof(dfloat));

  dfloat *cubX = (dfloat*) calloc(p_cubNq*p_cubNq, sizeof(dfloat));
  dfloat *cubInvX = (dfloat*) calloc(p_cubNq*p_cubNq, sizeof(dfloat));

  for(int n=0;n<p_cubNq;++n){
    cubX[n*p_cubNq + n] = 1;
    cubInvX[n*p_cubNq + n] = 0.5;

    if(n<p_cubNq/2){
      cubX[n*p_cubNq + p_cubNq-1-n] = -1;
      cubInvX[n*p_cubNq + p_cubNq-1-n] = 0.5;
    }
    
    if(n>=(p_cubNq/2)){
      cubX[n*p_cubNq + p_cubNq-1-n] = 1;
      cubInvX[n*p_cubNq + p_cubNq-1-n] = -0.5;
    }
  }

  for(int n=0;n<p_Nq;++n){
    X[n*p_Nq + n] = 1;
    invX[n*p_Nq + n] = 0.5;

    if(n<p_Nq/2){
      X[n*p_Nq + p_Nq-1-n] = -1;
      invX[n*p_Nq + p_Nq-1-n] = 0.5;
    }
    
    if(n>=p_Nq/2){
      X[n*p_Nq + p_Nq-1-n] = 1;
      invX[n*p_Nq + p_Nq-1-n] = -0.5;
    }
  }

  if(p_Nq%2) invX[(p_Nq)*(p_Nq)/2] = 1;
  if(p_cubNq%2) cubInvX[(p_cubNq+1)*(p_cubNq+1)/2] = 1;
  
  printf("X = [\n");
  for(int i=0;i<p_Nq;++i){
    for(int a=0;a<p_Nq;++a){
      printf("% .4e ", X[i*p_Nq+a]);
    }
    printf("\n");
  }
  printf("];\n");


  printf("invX = [\n");
  for(int i=0;i<p_Nq;++i){
    for(int a=0;a<p_Nq;++a){
      printf("% .4e ", invX[i*p_Nq+a]);
    }
    printf("\n");
  }
  printf("];\n");

  printf("cubX = [\n");
  for(int i=0;i<p_cubNq;++i){
    for(int a=0;a<p_cubNq;++a){
      printf("% .4e ", cubX[i*p_cubNq+a]);
    }
    printf("\n");
  }
  printf("];\n");


  printf("cubInvX = [\n");
  for(int i=0;i<p_cubNq;++i){
    for(int a=0;a<p_cubNq;++a){
      printf("% .4e ", cubInvX[i*p_cubNq+a]);
    }
    printf("\n");
  }
  printf("];\n");

  dfloat *IinvX = (dfloat*) calloc(p_Nq*p_cubNq, sizeof(dfloat));
  dfloat *cubInvXIinvX = (dfloat*) calloc(p_Nq*p_cubNq, sizeof(dfloat));

  // post multiply by invX
  for(int i=0;i<p_cubNq;++i){
    for(int a=0;a<p_Nq;++a){
      dfloat res = 0;
      for(int n=0;n<p_Nq;++n){
	res += h_I[0][i*p_Nq+n]*invX[n*p_Nq+a];
      }
      IinvX[i*p_Nq+a] = res;
    }
  }

  // pre multiply by invX
  for(int i=0;i<p_cubNq;++i){
    for(int a=0;a<p_Nq;++a){
      dfloat res = 0;
      for(int n=0;n<p_cubNq;++n){
	res += cubInvX[i*p_cubNq+n]*IinvX[n*p_Nq + a];
      }
      cubInvXIinvX[i*p_Nq+a] = res;
    }
  }

  printf("cubInvXIinvX = [\n");
  for(int i=0;i<p_cubNq;++i){
    for(int a=0;a<p_Nq;++a){
      printf("% .4e ", cubInvXIinvX[i*p_Nq + a]);
    }
    printf("\n");
  }

  // now interleave the two non-zero blocks
  // [ A 0 ]  => [ A[0][0] B[0][0] A[0][1] B[0][1] .. A[0][p_halfNq-1] B[0][p_halfNq-1] .. 
  // [ 0 B ] 

  dfloat *halfI  = (dfloat*) calloc(p_cubNq*p_halfNq, sizeof(dfloat));
  dfloat *halfIT = (dfloat*) calloc(p_cubNq*p_halfNq, sizeof(dfloat));
  
  for(int i=0;i<p_halfCubNq;++i){
    for(int a=0;a<p_halfNq;++a){
      halfI[2*(i*p_halfNq+a)+0] = cubInvXIinvX[i*p_Nq + a+p_halfNq];
      halfI[2*(i*p_halfNq+a)+1] = cubInvXIinvX[(i+p_halfCubNq)*p_Nq + a];
      halfIT[2*(i+p_halfCubNq*a)+1] = cubInvXIinvX[i*p_Nq + a+p_halfNq];
      halfIT[2*(i+p_halfCubNq*a)+0] = cubInvXIinvX[(i+p_halfCubNq)*p_Nq + a];
    }
  }
      
  printf("packed cubInvXIinvX = [\n");
  for(int i=0;i<p_halfCubNq;++i){
    for(int a=0;a<2*p_halfNq;++a){
      printf("% .4e ", halfI[i*2*p_halfNq+a]);
    }
    printf("\n");
  }

  printf("cubInvXIinvX T = [\n");
  for(int a=0;a<p_Nq;++a){
    for(int i=0;i<p_cubNq;++i){
      printf("% .4e ", cubInvXIinvX[i*p_Nq + a]);
    }
    printf("\n");
  }
  
  printf("packed cubInvXIinvX T = [\n");
  for(int a=0;a<p_halfNq;++a){
    for(int i=0;i<p_cubNq;++i){
      printf("% .4e ", halfIT[i+p_cubNq*a]);
    }
    printf("\n");
  }

  
  int NconstantI  = p_halfCubNq*p_Nq;
  int NconstantIT = p_cubNq*p_halfNq;
  cudaMemcpyToSymbol(const_I,  halfI, NconstantI*sizeof(dfloat));
  cudaMemcpyToSymbol(const_IT, halfIT, NconstantIT*sizeof(dfloat));
  

}


int main(int argc, char **argv){

  if(argc!=2){
    printf("Usage: ./advectionInvertMassMatrix Nelements\n");
    exit(-1);
  }

  // read number of elements
  hlong Nelements = atoi(argv[argc-1]);
  
  int    Ntotal = Nelements*p_Np;
  int cubNtotal = Nelements*p_cubNp;

  dfloat *h_cubvgeo, *c_cubvgeo;
  dfloat *h_qnew,    *c_qnew;
  dfloat *h_q,       *c_q;
  dfloat *h_I,       *c_I;

  int    *h_elementIds, *c_elementIds;


  // list of elements
  h_elementIds = (int*) calloc(Nelements, sizeof(int));
  for(int e=0;e<Nelements;++e)
    h_elementIds[e] = e;
  cudaMalloc(&c_elementIds, Nelements*sizeof(int));
  cudaMemcpy(c_elementIds, h_elementIds, Nelements*sizeof(int), cudaMemcpyHostToDevice);
  
  // float fields
  dfloatRandAlloc(cubNtotal*p_Nvgeo, &h_cubvgeo, &c_cubvgeo);
  dfloatRandAlloc(Ntotal,       &h_q, &c_q);
  dfloatRandAlloc(Ntotal,       &h_qnew, &c_qnew);

  buildInterpMatrices(&h_I, &c_I);

  // populate constant basis matrix
  int NconstantI  = p_halfCubNq*p_Nq;
  int NconstantIT = p_cubNq*p_halfNq;
  cudaMemcpyToSymbol(const_I,  h_I, NconstantI*sizeof(dfloat));
  cudaMemcpyToSymbol(const_IT, h_I, NconstantIT*sizeof(dfloat));

  // flush L2 ??
  dfloat *h_garbage, *c_garbage;
  int sz = 32*1024*1024; // 32MB
  dfloatRandAlloc(sz, &h_garbage, &c_garbage);
  
  cudaEvent_t start, end;

  cudaEventCreate(&start);
  cudaEventCreate(&end);	

  // call matrix inverse
  dim3 G(Nelements, 1, 1);
  dim3 B(p_Nq*p_Nq, 1, 1);

  cudaDeviceSynchronize();

  {
    cudaEventRecord(start);
    
    int Ntests = 10;
    for(int test=0;test<Ntests;++test)
      advectionMassMatrixMultiplyKernel <<< G, B >>>
	(Nelements, c_elementIds, c_cubvgeo, c_I, c_q, c_qnew);
    
    cudaEventRecord(end);
    
    cudaEventSynchronize(end);
    
    float elapsed;
    cudaEventElapsedTime(&elapsed, start, end);
    elapsed /= 1000.;
    elapsed /= (double) Ntests;
    
    printf("%d %d %d %lg %lg %%%% [MassMatrixMultiply: N, Nelements, Ndofs, elapsed, dofsPerSecond]\n", p_Nq-1, Nelements, p_Np*Nelements, elapsed, Nelements*(p_Np/elapsed));
  }

  advectionMassMatrixMultiplyHost(Nelements, h_elementIds, h_cubvgeo, h_I, h_q, h_qnew);

  // copy device version to host old q
  cudaMemcpy(h_q, c_qnew, Nelements*p_Np*sizeof(dfloat), cudaMemcpyDeviceToHost);
  
  for(int e=0;e<Nelements;++e){

    for(int n=0;n<p_Np;++n){
      int id = e*p_Np + n;
      printf("[ % .4e % .4e (% .4e)] (host Mq, device Mq, diff) \n ",
	     h_q[id], h_qnew[id], fabs(h_q[id]-h_qnew[id]));
    }
  }
  
  cudaEventDestroy(start);
  cudaEventDestroy(end);	
  
  return 0;

}